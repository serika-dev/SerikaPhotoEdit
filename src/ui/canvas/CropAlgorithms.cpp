#include "CropAlgorithms.h"
#include <QColor>
#include <QVector>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace serika {
QRect transparentContentBounds(const QImage &image, int alphaThreshold) {
    int left = image.width(), right = -1, top = image.height(), bottom = -1;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y).alphaF() * 255 > alphaThreshold) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
    return right >= left ? QRect(QPoint(left, top), QPoint(right, bottom)) : QRect();
}

QRect uniformBorderContentBounds(const QImage &image, qreal tolerance) {
    if (image.isNull())
        return {};
    std::array<qreal, 4> red{}, green{}, blue{}, alpha{};
    const std::array<QPoint, 4> corners{image.rect().topLeft(), image.rect().topRight(),
                                        image.rect().bottomLeft(), image.rect().bottomRight()};
    for (int i = 0; i < 4; ++i) {
        const QColor color = image.pixelColor(corners[i]);
        red[i] = color.redF();
        green[i] = color.greenF();
        blue[i] = color.blueF();
        alpha[i] = color.alphaF();
    }
    auto median = [](auto values) {
        std::sort(values.begin(), values.end());
        return (values[1] + values[2]) / 2;
    };
    const QColor background = QColor::fromRgbF(median(red), median(green), median(blue), median(alpha));
    auto matches = [&](int x, int y) {
        const QColor color = image.pixelColor(x, y);
        if (color.alphaF() <= tolerance && background.alphaF() <= tolerance)
            return true;
        return std::max({std::abs(color.redF() - background.redF()),
                         std::abs(color.greenF() - background.greenF()),
                         std::abs(color.blueF() - background.blueF()),
                         std::abs(color.alphaF() - background.alphaF())}) <= tolerance;
    };
    auto emptyRow = [&](int y, int left, int right) {
        for (int x = left; x <= right; ++x)
            if (!matches(x, y))
                return false;
        return true;
    };
    auto emptyColumn = [&](int x, int top, int bottom) {
        for (int y = top; y <= bottom; ++y)
            if (!matches(x, y))
                return false;
        return true;
    };
    int left = 0, right = image.width() - 1, top = 0, bottom = image.height() - 1;
    while (top <= bottom && emptyRow(top, left, right))
        ++top;
    while (bottom > top && emptyRow(bottom, left, right))
        --bottom;
    if (top > bottom)
        return {};
    while (left <= right && emptyColumn(left, top, bottom))
        ++left;
    while (right > left && emptyColumn(right, top, bottom))
        --right;
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

qreal estimateStraightenAngle(const QImage &original) {
    if (original.width() < 8 || original.height() < 8)
        return 0;
    const QImage image = original.scaled(768, 768, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_Grayscale8);
    struct Edge {
        qreal x;
        qreal y;
        qreal weight;
    };
    QVector<Edge> edges;
    for (int y = 1; y < image.height() - 1; ++y)
        for (int x = 1; x < image.width() - 1; ++x) {
            const auto *above = image.constScanLine(y - 1), *row = image.constScanLine(y),
                       *below = image.constScanLine(y + 1);
            const qreal gx =
                above[x + 1] + 2 * row[x + 1] + below[x + 1] - above[x - 1] - 2 * row[x - 1] - below[x - 1];
            const qreal gy =
                below[x - 1] + 2 * below[x] + below[x + 1] - above[x - 1] - 2 * above[x] - above[x + 1];
            const qreal strength = std::hypot(gx, gy);
            if (strength > 40)
                edges.append({x - image.width() / 2.0, y - image.height() / 2.0, strength});
        }
    if (edges.size() < 16)
        return 0;
    // Rotate edge coordinates through a narrow Hough search and measure how tightly
    // horizontal and vertical edges align. Projection is robust to pixel stair steps,
    // unlike a histogram of individual gradient directions.
    const int extent = image.width() + image.height() + 8;
    std::vector<qreal> horizontal(size_t(extent), 0), vertical(size_t(extent), 0);
    qreal bestAngle = 0, bestScore = 0, zeroScore = 0;
    for (int step = -80; step <= 80; ++step) {
        const qreal angle = step / 4.0;
        const qreal sine = std::sin(angle * std::numbers::pi / 180),
                    cosine = std::cos(angle * std::numbers::pi / 180);
        std::fill(horizontal.begin(), horizontal.end(), 0);
        std::fill(vertical.begin(), vertical.end(), 0);
        auto accumulate = [&](auto &histogram, qreal position, qreal weight) {
            position += extent / 2.0;
            const int index = int(std::floor(position));
            const qreal fraction = position - index;
            if (index >= 0 && index + 1 < extent) {
                histogram[size_t(index)] += weight * (1 - fraction);
                histogram[size_t(index + 1)] += weight * fraction;
            }
        };
        for (const Edge &edge : edges) {
            accumulate(horizontal, sine * edge.x + cosine * edge.y, edge.weight);
            accumulate(vertical, cosine * edge.x - sine * edge.y, edge.weight);
        }
        qreal score = 0;
        for (int i = 0; i < extent; ++i)
            score +=
                horizontal[size_t(i)] * horizontal[size_t(i)] + vertical[size_t(i)] * vertical[size_t(i)];
        if (step == 0)
            zeroScore = score;
        if (score > bestScore) {
            bestScore = score;
            bestAngle = angle;
        }
    }
    return bestScore > zeroScore * 1.025 ? bestAngle : 0;
}

QRectF largestInscribedRotatedRectangle(QSize size, qreal degrees) {
    // The largest centered rectangle at the original aspect ratio inside the rotated image.
    const qreal radians = degrees * std::numbers::pi / 180;
    const qreal cosine = std::abs(std::cos(radians)), sine = std::abs(std::sin(radians));
    const qreal w = size.width(), h = size.height();
    const qreal scale = std::min(w / (w * cosine + h * sine), h / (h * cosine + w * sine));
    return QRectF((w - w * scale) / 2, (h - h * scale) / 2, w * scale, h * scale);
}
} // namespace serika
