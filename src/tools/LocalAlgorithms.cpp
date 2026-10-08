#include "LocalAlgorithms.h"
#include <QColor>
#include <QMutex>
#include <QMutexLocker>
#include <QPoint>
#include <QVector>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>

namespace serika {
namespace {
QMutex selectorMutex;
std::shared_ptr<ISubjectSelector> customSelector;
int colorDistance(QRgb a, QRgb b) {
    const int r = qRed(a) - qRed(b), g = qGreen(a) - qGreen(b), bl = qBlue(a) - qBlue(b);
    return r * r + g * g + bl * bl;
}
int maskValue(const QImage &mask, int x, int y) { return mask.isNull() ? 0 : qGray(mask.pixel(x, y)); }
} // namespace
void registerSubjectSelector(std::shared_ptr<ISubjectSelector> selector) {
    QMutexLocker locker(&selectorMutex);
    customSelector = std::move(selector);
}
QImage selectSubjectLocally(const QImage &image) {
    std::shared_ptr<ISubjectSelector> selector;
    {
        QMutexLocker locker(&selectorMutex);
        selector = customSelector;
    }
    LocalSubjectSelector fallback;
    QImage mask = selector ? selector->select(image) : fallback.select(image);
    if (mask.isNull())
        mask = fallback.select(image);
    return mask.size() == image.size()
               ? mask
               : mask.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QImage ColorModelSubjectSelector::select(const QImage &input) const {
    if (input.isNull())
        return {};
    const QSize originalSize = input.size();
    const QImage image = (input.width() > 800 || input.height() > 800
                              ? input.scaled(800, 800, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                              : input)
                             .convertToFormat(QImage::Format_ARGB32);
    const int w = image.width(), h = image.height();
    // Border samples train a small RGB codebook. Flooding only border-connected candidates
    // avoids discarding a similarly colored isolated object in the middle of the frame.
    std::array<QRgb, 12> centers{};
    QVector<QRgb> samples;
    const int edge = std::max(1, std::min(w, h) / 40);
    for (int y = 0; y < h; y += 2)
        for (int x = 0; x < w; x += 2)
            if (x < edge || y < edge || x >= w - edge || y >= h - edge)
                samples.append(image.pixel(x, y));
    if (samples.isEmpty())
        return {};
    for (size_t i = 0; i < centers.size(); ++i)
        centers[i] = samples[int(i * samples.size() / centers.size())];
    for (int iteration = 0; iteration < 6; ++iteration) {
        std::array<std::array<qint64, 4>, 12> sums{};
        for (const QRgb sample : samples) {
            int best = 0, distance = std::numeric_limits<int>::max();
            for (int c = 0; c < 12; ++c) {
                const int d = colorDistance(sample, centers[size_t(c)]);
                if (d < distance) {
                    distance = d;
                    best = c;
                }
            }
            auto &s = sums[size_t(best)];
            s[0] += qRed(sample);
            s[1] += qGreen(sample);
            s[2] += qBlue(sample);
            ++s[3];
        }
        for (int c = 0; c < 12; ++c) {
            const auto &s = sums[size_t(c)];
            if (s[3])
                centers[size_t(c)] = qRgb(int(s[0] / s[3]), int(s[1] / s[3]), int(s[2] / s[3]));
        }
    }
    QVector<quint8> background(w * h, 0);
    QVector<QPoint> queue;
    auto candidate = [&](int x, int y) {
        const QRgb pixel = image.pixel(x, y);
        if (qAlpha(pixel) < 20)
            return true;
        int d = std::numeric_limits<int>::max();
        for (const QRgb center : centers)
            d = std::min(d, colorDistance(pixel, center));
        return d < 35 * 35 * 3;
    };
    auto enqueue = [&](int x, int y) {
        const int i = y * w + x;
        if (!background[i] && candidate(x, y)) {
            background[i] = 1;
            queue.append(QPoint(x, y));
        }
    };
    for (int x = 0; x < w; ++x) {
        enqueue(x, 0);
        enqueue(x, h - 1);
    }
    for (int y = 0; y < h; ++y) {
        enqueue(0, y);
        enqueue(w - 1, y);
    }
    for (qsizetype i = 0; i < queue.size(); ++i) {
        const QPoint p = queue[i];
        if (p.x() > 0)
            enqueue(p.x() - 1, p.y());
        if (p.x() + 1 < w)
            enqueue(p.x() + 1, p.y());
        if (p.y() > 0)
            enqueue(p.x(), p.y() - 1);
        if (p.y() + 1 < h)
            enqueue(p.x(), p.y() + 1);
    }
    QImage mask(w, h, QImage::Format_Grayscale8);
    mask.fill(0);
    for (int y = 0; y < h; ++y) {
        auto *row = mask.scanLine(y);
        for (int x = 0; x < w; ++x)
            row[x] = background[y * w + x] ? 0 : quint8(qAlpha(image.pixel(x, y)));
    }
    // A small majority filter removes isolated background speckles while preserving edges.
    const QImage rough = mask;
    for (int y = 1; y < h - 1; ++y) {
        auto *row = mask.scanLine(y);
        for (int x = 1; x < w - 1; ++x) {
            int total = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    total += rough.constScanLine(y + dy)[x + dx];
            row[x] = quint8(total / 9);
        }
    }
    return mask.scaled(originalSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QImage synthesizePatches(const QImage &input, const QImage &inputMask) {
    if (input.isNull() || inputMask.isNull())
        return input;
    const QImage source = input.convertToFormat(QImage::Format_ARGB32);
    QImage result = source;
    const QImage mask = inputMask.convertToFormat(QImage::Format_Grayscale8);
    const int w = source.width(), h = source.height();
    QVector<quint8> unknown(w * h, 0);
    QVector<QPoint> pixels, donors;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (maskValue(mask, x, y) > 127) {
                unknown[y * w + x] = 1;
                pixels.append(QPoint(x, y));
            } else if ((x % 3) == 0 && (y % 3) == 0)
                donors.append(QPoint(x, y));
        }
    if (pixels.isEmpty() || donors.isEmpty())
        return input;
    const QVector<quint8> originalUnknown = unknown;
    std::mt19937 generator(0x53455249);
    std::uniform_int_distribution<int> pick(0, int(donors.size()) - 1);
    // Exemplar fill advances from the boundary toward the hole. Candidate patches are
    // initialized randomly, then improved by neighbor propagation and shrinking searches.
    QVector<QPoint> nearest(w * h, QPoint(-1, -1));
    int remaining = int(pixels.size());
    constexpr int patchRadius = 2;
    auto cost = [&](QPoint target, QPoint donor) {
        if (donor.x() < patchRadius || donor.y() < patchRadius || donor.x() >= w - patchRadius ||
            donor.y() >= h - patchRadius)
            return std::numeric_limits<double>::infinity();
        double sum = 0;
        int samples = 0;
        for (int dy = -patchRadius; dy <= patchRadius; ++dy)
            for (int dx = -patchRadius; dx <= patchRadius; ++dx) {
                const int tx = target.x() + dx, ty = target.y() + dy, sx = donor.x() + dx,
                          sy = donor.y() + dy;
                if (tx < 0 || ty < 0 || tx >= w || ty >= h)
                    continue;
                if (originalUnknown[sy * w + sx])
                    return std::numeric_limits<double>::infinity();
                if (!unknown[ty * w + tx]) {
                    sum += colorDistance(result.pixel(tx, ty), source.pixel(sx, sy));
                    ++samples;
                }
            }
        return samples ? sum / samples : std::numeric_limits<double>::infinity();
    };
    QVector<QPoint> frontier;
    QVector<quint8> queued(w * h, 0);
    auto enqueue = [&](QPoint p) {
        if (p.x() < 0 || p.y() < 0 || p.x() >= w || p.y() >= h)
            return;
        const int index = p.y() * w + p.x();
        if (unknown[index] && !queued[index]) {
            frontier.append(p);
            queued[index] = 1;
        }
    };
    for (const QPoint p : pixels) {
        const int x = p.x(), y = p.y();
        if ((x && !unknown[y * w + x - 1]) || (x + 1 < w && !unknown[y * w + x + 1]) ||
            (y && !unknown[(y - 1) * w + x]) || (y + 1 < h && !unknown[(y + 1) * w + x]))
            enqueue(p);
    }
    for (qsizetype cursor = 0; cursor < frontier.size() && remaining > 0; ++cursor) {
        const QPoint target = frontier[cursor];
        QPoint best = donors[pick(generator)];
        double bestCost = cost(target, best);
        auto improve = [&](QPoint candidate) {
            const double d = cost(target, candidate);
            if (d < bestCost) {
                bestCost = d;
                best = candidate;
            }
        };
        for (int attempt = 0; attempt < 32; ++attempt)
            improve(donors[pick(generator)]);
        const std::array<QPoint, 4> directions{QPoint(-1, 0), QPoint(1, 0), QPoint(0, -1), QPoint(0, 1)};
        for (const QPoint direction : directions) {
            const QPoint neighbor = target + direction;
            if (neighbor.x() >= 0 && neighbor.y() >= 0 && neighbor.x() < w && neighbor.y() < h) {
                const QPoint n = nearest[neighbor.y() * w + neighbor.x()];
                if (n.x() >= 0)
                    improve(n - direction);
            }
        }
        for (int radius = std::max(w, h); radius >= 1; radius /= 2) {
            std::uniform_int_distribution<int> delta(-radius, radius);
            for (int i = 0; i < 4; ++i)
                improve(best + QPoint(delta(generator), delta(generator)));
        }
        result.setPixel(target, source.pixel(best));
        nearest[target.y() * w + target.x()] = best;
        unknown[target.y() * w + target.x()] = 0;
        --remaining;
        for (const QPoint direction : directions)
            enqueue(target + direction);
    }
    // Blend the soft mask only once, retaining fractional selection boundaries.
    QImage highDepth = input;
    for (const QPoint p : pixels) {
        const QPoint donor = nearest[p.y() * w + p.x()];
        if (donor.x() < 0)
            continue;
        const qreal a = maskValue(mask, p.x(), p.y()) / 255.0;
        const QColor old = input.pixelColor(p), filled = input.pixelColor(donor);
        highDepth.setPixelColor(p, QColor::fromRgbF(old.redF() * (1 - a) + filled.redF() * a,
                                                    old.greenF() * (1 - a) + filled.greenF() * a,
                                                    old.blueF() * (1 - a) + filled.blueF() * a,
                                                    old.alphaF() * (1 - a) + filled.alphaF() * a));
    }
    return highDepth;
}
} // namespace serika
