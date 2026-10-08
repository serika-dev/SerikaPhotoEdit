#include "io/FormatIO.h"
#include <QColorSpace>
#include <QJsonArray>
#include <QRandomGenerator>
#include <algorithm>
#include <array>
#include <cmath>

namespace serika {
namespace {
constexpr double pi = 3.14159265358979323846;
struct Pixel {
    float r = 0, g = 0, b = 0, a = 0;
    float &operator[](int i) { return i == 0 ? r : i == 1 ? g : i == 2 ? b : a; }
    float operator[](int i) const { return i == 0 ? r : i == 1 ? g : i == 2 ? b : a; }
};
Pixel operator+(Pixel a, Pixel b) { return {a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a}; }
Pixel operator-(Pixel a, Pixel b) { return {a.r - b.r, a.g - b.g, a.b - b.b, a.a - b.a}; }
Pixel operator*(Pixel a, float b) { return {a.r * b, a.g * b, a.b * b, a.a * b}; }
float luminance(Pixel p) { return .2126f * p.r + .7152f * p.g + .0722f * p.b; }
float clamp(float v) { return qBound(0.0f, v, 1.0f); }
struct Image {
    int w, h;
    QVector<Pixel> p;
    explicit Image(const QImage &im) : w(im.width()), h(im.height()), p(w * h) {
        QImage f = im.convertToFormat(QImage::Format_RGBA32FPx4);
        for (int y = 0; y < h; ++y) {
            const float *line = reinterpret_cast<const float *>(f.constScanLine(y));
            for (int x = 0; x < w; ++x)
                p[y * w + x] = {line[x * 4], line[x * 4 + 1], line[x * 4 + 2], line[x * 4 + 3]};
        }
    }
    Pixel at(int x, int y) const { return p[qBound(0, y, h - 1) * w + qBound(0, x, w - 1)]; }
    Pixel sample(double x, double y, bool wrap = false) const {
        if (!std::isfinite(x) || !std::isfinite(y))
            return {};
        if (wrap) {
            x = std::fmod(std::fmod(x, w) + w, w);
            y = std::fmod(std::fmod(y, h) + h, h);
        } else {
            x = qBound(0.0, x, double(w - 1));
            y = qBound(0.0, y, double(h - 1));
        }
        int ix = std::floor(x), iy = std::floor(y);
        float fx = x - ix, fy = y - iy;
        auto pixel = [&](int xx, int yy) {
            return wrap ? p[((yy % h + h) % h) * w + (xx % w + w) % w] : at(xx, yy);
        };
        return (pixel(ix, iy) * (1 - fx) + pixel(ix + 1, iy) * fx) * (1 - fy) +
               (pixel(ix, iy + 1) * (1 - fx) + pixel(ix + 1, iy + 1) * fx) * fy;
    }
    QImage output(const QImage &original, int depth = 0) const {
        QImage f(w, h, QImage::Format_RGBA32FPx4);
        for (int y = 0; y < h; ++y) {
            auto line = reinterpret_cast<float *>(f.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const auto pixel = p[y * w + x];
                for (int c = 0; c < 4; ++c)
                    line[x * 4 + c] = std::isfinite(pixel[c]) ? pixel[c] : 0;
            }
        }
        f.setColorSpace(original.colorSpace());
        f.setDotsPerMeterX(original.dotsPerMeterX());
        f.setDotsPerMeterY(original.dotsPerMeterY());
        f.setDevicePixelRatio(original.devicePixelRatio());
        QImage::Format format = depth == 16  ? QImage::Format_RGBA64
                                : depth == 8 ? QImage::Format_RGBA8888
                                : original.format() == QImage::Format_RGBA32FPx4 ||
                                        original.format() == QImage::Format_RGBA32FPx4_Premultiplied ||
                                        original.format() == QImage::Format_RGBX32FPx4
                                    ? QImage::Format_RGBA32FPx4
                                : original.depth() > 32 || original.format() == QImage::Format_Grayscale16
                                    ? QImage::Format_RGBA64
                                    : QImage::Format_RGBA8888;
        return f.convertToFormat(format);
    }
};
double parameter(const QJsonObject &o, const QString &name, double fallback) {
    return o.value(name).toDouble(fallback);
}
void premultiply(Image &im) {
    for (auto &p : im.p) {
        p.r *= p.a;
        p.g *= p.a;
        p.b *= p.a;
    }
}
void unpremultiply(Image &im) {
    for (auto &p : im.p) {
        if (p.a > 1e-6f) {
            p.r /= p.a;
            p.g /= p.a;
            p.b /= p.a;
        } else
            p.r = p.g = p.b = 0;
    }
}
Image box(const Image &source, int radius) {
    if (radius <= 0)
        return source;
    Image temp = source, out = source;
    const float n = 2 * radius + 1;
    for (int y = 0; y < source.h; ++y) {
        Pixel sum;
        for (int dx = -radius; dx <= radius; ++dx)
            sum = sum + source.at(dx, y);
        for (int x = 0; x < source.w; ++x) {
            temp.p[y * source.w + x] = sum * (1 / n);
            sum = sum - source.at(x - radius, y) + source.at(x + radius + 1, y);
        }
    }
    for (int x = 0; x < source.w; ++x) {
        Pixel sum;
        for (int dy = -radius; dy <= radius; ++dy)
            sum = sum + temp.at(x, dy);
        for (int y = 0; y < source.h; ++y) {
            out.p[y * source.w + x] = sum * (1 / n);
            sum = sum - temp.at(x, y - radius) + temp.at(x, y + radius + 1);
        }
    }
    return out;
}
Image gaussian(const Image &source, double sigma) {
    if (sigma < .01)
        return source;
    sigma = qBound(.1, sigma, 100.0);
    Image out = source;
    premultiply(out);
    if (sigma > 12) {
        const int r = qMax(1, qRound((std::sqrt(4 * sigma * sigma + 1) - 1) * .5));
        for (int i = 0; i < 3; ++i)
            out = box(out, r);
        unpremultiply(out);
        return out;
    }
    const int radius = std::ceil(sigma * 3);
    QVector<float> kernel(2 * radius + 1);
    double sum = 0;
    for (int i = -radius; i <= radius; ++i) {
        kernel[i + radius] = std::exp(-i * i / (2 * sigma * sigma));
        sum += kernel[i + radius];
    }
    for (auto &k : kernel)
        k /= sum;
    Image temp = out;
    const Image input = out;
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x) {
            Pixel p;
            for (int i = -radius; i <= radius; ++i)
                p = p + input.at(x + i, y) * kernel[i + radius];
            temp.p[y * out.w + x] = p;
        }
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x) {
            Pixel p;
            for (int i = -radius; i <= radius; ++i)
                p = p + temp.at(x, y + i) * kernel[i + radius];
            out.p[y * out.w + x] = p;
        }
    unpremultiply(out);
    return out;
}
Image median(const Image &source, int radius) {
    Image out = source;
    radius = qBound(1, radius, 12);
    QVector<float> values;
    values.reserve((2 * radius + 1) * (2 * radius + 1));
    for (int y = 0; y < source.h; ++y)
        for (int x = 0; x < source.w; ++x) {
            Pixel pixel = source.at(x, y);
            for (int c = 0; c < 3; ++c) {
                values.clear();
                for (int dy = -radius; dy <= radius; ++dy)
                    for (int dx = -radius; dx <= radius; ++dx)
                        values.append(source.at(x + dx, y + dy)[c]);
                auto middle = values.begin() + values.size() / 2;
                std::nth_element(values.begin(), middle, values.end());
                pixel[c] = *middle;
            }
            out.p[y * source.w + x] = pixel;
        }
    return out;
}
Image surface(const Image &source, int radius, float threshold, bool disk = false) {
    Image out = source;
    radius = qBound(1, radius, 30);
    for (int y = 0; y < source.h; ++y)
        for (int x = 0; x < source.w; ++x) {
            Pixel sum;
            float weight = 0;
            const Pixel center = source.at(x, y);
            for (int dy = -radius; dy <= radius; ++dy)
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (disk && dx * dx + dy * dy > radius * radius)
                        continue;
                    auto p = source.at(x + dx, y + dy);
                    float range = std::abs(luminance(center) - luminance(p));
                    float w = threshold <= 0 ? 1 : std::exp(-range * range / (2 * threshold * threshold));
                    sum = sum + p * w;
                    weight += w;
                }
            auto p = sum * (1 / qMax(.00001f, weight));
            p.a = center.a;
            out.p[y * source.w + x] = p;
        }
    return out;
}
Image sharpen(const Image &source, double radius, double amount, double threshold = 0) {
    Image blur = gaussian(source, radius), out = source;
    for (qsizetype i = 0; i < source.p.size(); ++i) {
        Pixel p = source.p[i];
        for (int c = 0; c < 3; ++c) {
            float difference = p[c] - blur.p[i][c];
            if (std::abs(difference) >= threshold)
                p[c] += difference * amount;
        }
        out.p[i] = p;
    }
    return out;
}
float hashNoise(int x, int y, quint32 seed) {
    quint32 h = quint32(x) * 374761393u + quint32(y) * 668265263u + seed * 69069u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffffff) / float(0xffffff);
}
float noise(double x, double y, quint32 seed) {
    int ix = std::floor(x), iy = std::floor(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    return (hashNoise(ix, iy, seed) * (1 - fx) + hashNoise(ix + 1, iy, seed) * fx) * (1 - fy) +
           (hashNoise(ix, iy + 1, seed) * (1 - fx) + hashNoise(ix + 1, iy + 1, seed) * fx) * fy;
}
} // namespace

QImage applyFilter(const QImage &source, const QString &name, const QJsonObject &parameters) {
    if (source.isNull())
        return {};
    Image input(source), out = input;
    QString key = name.toLower().trimmed();
    double radius = qBound(0.0, parameter(parameters, "radius", 3), 100.0),
           amount = parameter(parameters, "amount", 50) / 100.0,
           angle = parameter(parameters, "angle", 0) * pi / 180.0;
    int size = qBound(2, int(parameter(parameters, "size", 12)), 512);
    if (key == "gaussian blur" || key == "gaussian" || key == "field blur")
        out = gaussian(input, radius);
    else if (key == "box blur" || key == "box") {
        premultiply(out);
        out = box(out, qRound(radius));
        unpremultiply(out);
    } else if (key == "motion blur" || key == "motion") {
        int n = qMax(1, qRound(radius) * 2 + 1);
        for (int y = 0; y < input.h; ++y)
            for (int x = 0; x < input.w; ++x) {
                Pixel p;
                for (int i = 0; i < n; ++i) {
                    double t = i - (n - 1) * .5;
                    p = p + input.sample(x + std::cos(angle) * t, y + std::sin(angle) * t);
                }
                out.p[y * input.w + x] = p * (1.0f / n);
            }
    } else if (key == "radial blur" || key == "radial") {
        double cx = input.w * .5, cy = input.h * .5;
        for (int y = 0; y < input.h; ++y)
            for (int x = 0; x < input.w; ++x) {
                Pixel p;
                for (int i = 0; i < 25; ++i) {
                    double a = (i / 24.0 - .5) * amount * .4, dx = x - cx, dy = y - cy;
                    p = p + input.sample(cx + dx * std::cos(a) - dy * std::sin(a),
                                         cy + dx * std::sin(a) + dy * std::cos(a));
                }
                out.p[y * input.w + x] = p * (1 / 25.f);
            }
    } else if (key == "surface blur" || key == "smart blur" || key == "reduce noise")
        out = surface(input, qRound(radius), parameter(parameters, "threshold", 25) / 255.0f);
    else if (key == "lens blur")
        out = surface(input, qRound(radius), 0, true);
    else if (key == "iris blur" || key == "tilt-shift") {
        auto blurred = gaussian(input, radius);
        double cx = input.w * .5, cy = input.h * .5;
        for (int y = 0; y < input.h; ++y)
            for (int x = 0; x < input.w; ++x) {
                float d =
                    key == "iris blur" ? std::hypot((x - cx) / cx, (y - cy) / cy) : std::abs((y - cy) / cy);
                float mix = clamp((d - .25f) * 2);
                out.p[y * input.w + x] = input.at(x, y) * (1 - mix) + blurred.at(x, y) * mix;
            }
    } else if (key == "median" || key == "dust & scratches" || key == "despeckle") {
        auto med = median(input, key == "despeckle" ? 1 : qRound(radius));
        float threshold = parameter(parameters, "threshold", key == "dust & scratches" ? 15 : 0) / 255.0;
        for (qsizetype i = 0; i < input.p.size(); ++i)
            if (std::abs(luminance(input.p[i]) - luminance(med.p[i])) >= threshold)
                out.p[i] = med.p[i];
    } else if (key == "add noise") {
        QRandomGenerator rng(quint32(parameter(parameters, "seed", 42)));
        bool mono = parameters.value("monochromatic").toBool();
        bool normal = parameters.value("gaussian").toBool();
        for (auto &p : out.p) {
            float shared = 0;
            for (int c = 0; c < 3; ++c) {
                float n = normal ? std::sqrt(-2 * std::log(qMax(1e-10, rng.generateDouble()))) *
                                       std::cos(2 * pi * rng.generateDouble()) * .333
                                 : rng.generateDouble() * 2 - 1;
                if (c == 0)
                    shared = n;
                p[c] = clamp(p[c] + (mono ? shared : n) * amount);
            }
        }
    } else if (key == "sharpen" || key == "sharpen more" || key == "sharpen edges" || key == "unsharp mask" ||
               key == "smart sharpen") {
        out = sharpen(input, key == "sharpen" ? 1 : radius,
                      key == "sharpen more" ? 1.5
                      : key == "sharpen"    ? 1
                                            : amount,
                      parameter(parameters, "threshold", 0) / 255);
        if (key == "sharpen edges") {
            auto edgeBlur = gaussian(input, 1);
            for (qsizetype i = 0; i < input.p.size(); ++i) {
                float mix = clamp(std::abs(luminance(input.p[i]) - luminance(edgeBlur.p[i])) * 10);
                out.p[i] = input.p[i] * (1 - mix) + out.p[i] * mix;
            }
        }
    } else if (key == "high pass") {
        auto blur = gaussian(input, radius);
        for (qsizetype i = 0; i < input.p.size(); ++i) {
            for (int c = 0; c < 3; ++c)
                out.p[i][c] = input.p[i][c] - blur.p[i][c] + .5f;
        }
    } else if (key == "minimum" || key == "maximum") {
        int r = qBound(1, qRound(radius), 15);
        for (int y = 0; y < input.h; ++y)
            for (int x = 0; x < input.w; ++x) {
                auto p = input.at(x, y);
                for (int dy = -r; dy <= r; ++dy)
                    for (int dx = -r; dx <= r; ++dx) {
                        auto neighbor = input.at(x + dx, y + dy);
                        for (int c = 0; c < 3; ++c)
                            p[c] = key == "minimum" ? qMin(p[c], neighbor[c]) : qMax(p[c], neighbor[c]);
                    }
                out.p[y * input.w + x] = p;
            }
    } else if (key == "offset") {
        double dx = parameter(parameters, "x", parameter(parameters, "horizontal", input.w * .5)),
               dy = parameter(parameters, "y", parameter(parameters, "vertical", input.h * .5));
        for (int y = 0; y < input.h; ++y)
            for (int x = 0; x < input.w; ++x)
                out.p[y * input.w + x] = input.sample(x - dx, y - dy, true);
    } else if (key == "find edges" || key == "emboss" || key == "custom") {
        QJsonArray kernel = parameters.value("kernel").toArray();
        int ksize = key == "custom" ? 5 : 3;
        double divisor = parameter(parameters, "divisor", 1),
               offset = parameter(parameters, "offset", key == "emboss" ? .5 : 0);
        if (std::abs(divisor) < 1e-6)
            divisor = 1;
        const float sobelX[9] = {-1, 0, 1, -2, 0, 2, -1, 0, 1}, sobelY[9] = {-1, -2, -1, 0, 0, 0, 1, 2, 1},
                    emboss[9] = {-2, -1, 0, -1, 1, 1, 0, 1, 2};
        for (int y = 0; y < input.h; ++y)
            for (int x = 0; x < input.w; ++x) {
                Pixel sum, gy;
                for (int ky = 0; ky < ksize; ++ky)
                    for (int kx = 0; kx < ksize; ++kx) {
                        int i = ky * ksize + kx;
                        auto p = input.at(x + kx - ksize / 2, y + ky - ksize / 2);
                        float weight = key == "custom"   ? (kernel.size() == 25 ? kernel.at(i).toDouble()
                                                            : i == 12           ? 1
                                                                                : 0)
                                       : key == "emboss" ? emboss[i]
                                                         : sobelX[i];
                        sum = sum + p * weight;
                        if (key == "find edges")
                            gy = gy + p * sobelY[i];
                    }
                Pixel p = input.at(x, y);
                for (int c = 0; c < 3; ++c)
                    p[c] = key == "find edges" ? 1 - clamp(std::hypot(sum[c], gy[c]))
                                               : sum[c] / divisor + offset;
                out.p[y * input.w + x] = p;
            }
    } else if (key == "mosaic" || key == "fragment") {
        if (key == "fragment") {
            for (int y = 0; y < input.h; ++y)
                for (int x = 0; x < input.w; ++x) {
                    auto p = (input.at(x - 2, y - 2) + input.at(x + 2, y - 2) + input.at(x - 2, y + 2) +
                              input.at(x + 2, y + 2)) *
                             .25f;
                    p.a = input.at(x, y).a;
                    out.p[y * input.w + x] = p;
                }
        } else
            for (int by = 0; by < input.h; by += size)
                for (int bx = 0; bx < input.w; bx += size) {
                    Pixel sum;
                    int count = 0;
                    for (int y = by; y < qMin(by + size, input.h); ++y)
                        for (int x = bx; x < qMin(bx + size, input.w); ++x) {
                            sum = sum + input.at(x, y);
                            ++count;
                        }
                    sum = sum * (1.0f / count);
                    for (int y = by; y < qMin(by + size, input.h); ++y)
                        for (int x = bx; x < qMin(bx + size, input.w); ++x)
                            out.p[y * input.w + x] = sum;
                }
    } else if (key == "crystallize" || key == "pointillize" || key == "color halftone") {
        quint32 seed = parameter(parameters, "seed", 7);
        for (int y = 0; y < input.h; ++y)
            for (int x = 0; x < input.w; ++x) {
                double best = 1e30, cx = 0, cy = 0;
                int cellx = x / size, celly = y / size;
                for (int oy = -1; oy <= 1; ++oy)
                    for (int ox = -1; ox <= 1; ++ox) {
                        double xx = (cellx + ox + .15 + .7 * hashNoise(cellx + ox, celly + oy, seed)) * size,
                               yy = (celly + oy + .15 + .7 * hashNoise(cellx + ox, celly + oy, seed + 1)) *
                                    size,
                               d = (x - xx) * (x - xx) + (y - yy) * (y - yy);
                        if (d < best) {
                            best = d;
                            cx = xx;
                            cy = yy;
                        }
                    }
                auto p = input.sample(cx, cy);
                if (key == "pointillize" && best > size * size * .15)
                    p = {1, 1, 1, p.a};
                if (key == "color halftone") {
                    double distance = std::hypot(x - (cellx + .5) * size, y - (celly + .5) * size) / size;
                    for (int c = 0; c < 3; ++c)
                        p[c] = distance < std::sqrt(qMax(0.f, 1 - p[c])) * .65 ? 0 : 1;
                }
                p.a = input.at(x, y).a;
                out.p[y * input.w + x] = p;
            }
    } else if (key == "clouds" || key == "difference clouds" || key == "fibers") {
        quint32 seed = parameter(parameters, "seed", 42);
        double scale = parameter(parameters, "scale", 100);
        for (int y = 0; y < input.h; ++y)
            for (int x = 0; x < input.w; ++x) {
                float v = 0, norm = 0;
                for (int octave = 0; octave < 5; ++octave) {
                    float weight = std::pow(.5f, octave);
                    double f = std::pow(2.0, octave) / qMax(1.0, scale);
                    v += noise(x * f, key == "fibers" ? y * f * .035 : y * f, seed + octave) * weight;
                    norm += weight;
                }
                v /= norm;
                auto p = input.at(x, y);
                for (int c = 0; c < 3; ++c)
                    p[c] = key == "difference clouds" ? std::abs(p[c] - v) : v;
                out.p[y * input.w + x] = p;
            }
    } else if (key == "lens flare") {
        double cx = parameter(parameters, "x", input.w * .35), cy = parameter(parameters, "y", input.h * .35);
        for (int y = 0; y < input.h; ++y)
            for (int x = 0; x < input.w; ++x) {
                double d = std::hypot(x - cx, y - cy) / qMax(1.0, qMin(input.w, input.h) * .12);
                float glow = amount * std::exp(-d * d) * 2 + amount * .035 / qMax(.02, d);
                auto p = input.at(x, y);
                p.r += glow;
                p.g += glow * .8;
                p.b += glow * .6;
                out.p[y * input.w + x] = p;
            }
    } else if (key == "diffuse" || key == "wind") {
        for (int y = 0; y < input.h; ++y)
            for (int x = 0; x < input.w; ++x) {
                if (key == "diffuse") {
                    int dx = hashNoise(x, y, 13) * 5 - 2, dy = hashNoise(x, y, 29) * 5 - 2;
                    out.p[y * input.w + x] = input.at(x + dx, y + dy);
                } else {
                    auto p = input.at(x, y);
                    for (int i = 1; i < qMax(2, qRound(radius)); ++i) {
                        auto neighbor = input.at(x - i, y);
                        for (int c = 0; c < 3; ++c)
                            p[c] = qMax(p[c], neighbor[c] * (1 - i / (radius + 1)));
                    }
                    out.p[y * input.w + x] = p;
                }
            }
    } else if (key == "oil paint") {
        int r = qBound(1, qRound(radius), 8);
        for (int y = 0; y < input.h; ++y)
            for (int x = 0; x < input.w; ++x) {
                std::array<int, 16> counts{};
                std::array<Pixel, 16> sums{};
                for (int dy = -r; dy <= r; ++dy)
                    for (int dx = -r; dx <= r; ++dx) {
                        auto p = input.at(x + dx, y + dy);
                        int bucket = qBound(0, int(luminance(p) * 15), 15);
                        ++counts[bucket];
                        sums[bucket] = sums[bucket] + p;
                    }
                int index = std::max_element(counts.begin(), counts.end()) - counts.begin();
                auto p = sums[index] * (1.0f / counts[index]);
                p.a = input.at(x, y).a;
                out.p[y * input.w + x] = p;
            }
    } else if (key == "spherize" || key == "pinch" || key == "twirl" || key == "ripple" || key == "wave" ||
               key == "polar coordinates") {
        double cx = (input.w - 1) * .5, cy = (input.h - 1) * .5, R = qMax(1.0, qMin(input.w, input.h) * .5);
        for (int y = 0; y < input.h; ++y)
            for (int x = 0; x < input.w; ++x) {
                double dx = x - cx, dy = y - cy, r = std::hypot(dx, dy) / R, a = std::atan2(dy, dx), sx = x,
                       sy = y;
                if (key == "twirl" && r < 1) {
                    a += amount * 2 * pi * (1 - r) * (1 - r);
                    sx = cx + std::cos(a) * r * R;
                    sy = cy + std::sin(a) * r * R;
                } else if ((key == "spherize" || key == "pinch") && r < 1) {
                    double mapped = key == "pinch" ? std::pow(r, std::exp(amount)) : std::sin(r * pi * .5);
                    if (key == "spherize")
                        mapped = r + (mapped - r) * amount;
                    sx = cx + std::cos(a) * mapped * R;
                    sy = cy + std::sin(a) * mapped * R;
                } else if (key == "ripple") {
                    double rr = r * R + std::sin(r * R / qMax(1, size) * 2 * pi) * amount * size * .5;
                    sx = cx + std::cos(a) * rr;
                    sy = cy + std::sin(a) * rr;
                } else if (key == "wave") {
                    sx = x + std::sin(y * 2 * pi / qMax(2, size)) * amount * size;
                    sy = y + std::sin(x * 2 * pi / qMax(2, size)) * amount * size;
                } else if (key == "polar coordinates") {
                    sx = (a + pi) / (2 * pi) * (input.w - 1);
                    sy = r * (input.h - 1);
                }
                out.p[y * input.w + x] = input.sample(sx, sy);
            }
    } else if (key == "camera raw filter")
        return developImage(source, parameters, source.depth() > 32 ? 16 : 8);
    else
        return applyAdjustment(source, name, parameters);
    return out.output(source);
}

QImage developImage(const QImage &source, const QJsonObject &parameters, int bitDepth) {
    if (source.isNull())
        return {};
    QImage working = source;
    if (working.colorSpace().isValid() && working.colorSpace() != QColorSpace(QColorSpace::SRgb))
        working = working.convertedToColorSpace(QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA32FPx4);
    Image input(working), out = input;
    double exposure = std::exp2(parameter(parameters, "exposure", 0)),
           contrast = parameter(parameters, "contrast", 0) / 100.0,
           highlights = parameter(parameters, "highlights", 0) / 100.0,
           shadows = parameter(parameters, "shadows", 0) / 100.0,
           whites = parameter(parameters, "whites", 0) / 100.0,
           blacks = parameter(parameters, "blacks", 0) / 100.0;
    double temperature = (parameter(parameters, "temperature", 6500) - 6500) / 6500.0,
           tint = parameter(parameters, "tint", 0) / 100.0,
           saturation = 1 + parameter(parameters, "saturation", 0) / 100.0,
           vibrance = parameter(parameters, "vibrance", 0) / 100.0,
           dehaze = parameter(parameters, "dehaze", 0) / 100.0,
           toneGamma = qBound(.01, parameter(parameters, "gamma", 1), 10.0),
           hueAngle = parameter(parameters, "hue", 0) * pi / 180;
    auto linear = [](float v) {
        return v <= .04045f ? v / 12.92f : std::pow((qMax(0.f, v) + .055f) / 1.055f, 2.4f);
    };
    auto srgb = [](float v) {
        return v <= .0031308f ? v * 12.92f : 1.055f * std::pow(qMax(0.f, v), 1 / 2.4f) - .055f;
    };
    const bool isLinear = working.colorSpace() == QColorSpace(QColorSpace::SRgbLinear);
    for (qsizetype i = 0; i < out.p.size(); ++i) {
        auto p = input.p[i];
        for (int c = 0; c < 3; ++c) {
            double value = isLinear ? p[c] : linear(p[c]);
            value *= exposure * (c == 0   ? std::exp(temperature * .35 - tint * .08)
                                 : c == 2 ? std::exp(-temperature * .35 - tint * .08)
                                          : std::exp(tint * .15));
            value = srgb(value);
            double low = std::pow(1 - clamp(value), 3), high = std::pow(clamp(value), 3);
            value += shadows * .25 * low + highlights * .25 * high + whites * .15 * high + blacks * .15 * low;
            value = (value - .5) * (1 + contrast) + .5;
            value = (value - .08 * dehaze) / (1 - .16 * dehaze);
            value = std::copysign(std::pow(std::abs(value), 1.0 / toneGamma), value);
            p[c] = value;
        }
        float l = luminance(p), max = qMax(p.r, qMax(p.g, p.b)), min = qMin(p.r, qMin(p.g, p.b));
        float sat = saturation + vibrance * (1 - qBound(0.f, max - min, 1.f));
        for (int c = 0; c < 3; ++c)
            p[c] = l + (p[c] - l) * sat;
        if (std::abs(hueAngle) > .00001) {
            const double Y = .299 * p.r + .587 * p.g + .114 * p.b;
            const double I = .596 * p.r - .274 * p.g - .322 * p.b;
            const double Q = .211 * p.r - .523 * p.g + .312 * p.b;
            const double rotatedI = I * std::cos(hueAngle) - Q * std::sin(hueAngle);
            const double rotatedQ = I * std::sin(hueAngle) + Q * std::cos(hueAngle);
            p.r = Y + .956 * rotatedI + .621 * rotatedQ;
            p.g = Y - .272 * rotatedI - .647 * rotatedQ;
            p.b = Y - 1.106 * rotatedI + 1.703 * rotatedQ;
        }
        out.p[i] = p;
    }
    double clarity = parameter(parameters, "clarity", 0) / 100,
           texture = parameter(parameters, "texture", 0) / 100;
    if (std::abs(clarity) > .001)
        out = sharpen(out, 12, clarity * .8);
    if (std::abs(texture) > .001)
        out = sharpen(out, 1.1, texture * .6);
    double nr = parameter(parameters, "luminanceNR", parameter(parameters, "noiseReduction", 0)) / 100.0;
    if (nr > 0) {
        auto reduced = surface(out, qBound(1, qRound(1 + nr * 4), 5), nr * .1);
        for (qsizetype i = 0; i < out.p.size(); ++i)
            out.p[i] = out.p[i] * (1 - nr) + reduced.p[i] * nr;
    }
    double colorNr = parameter(parameters, "colorNR", 0) / 100;
    if (colorNr > 0) {
        auto blurred = gaussian(out, 1.5);
        for (qsizetype i = 0; i < out.p.size(); ++i) {
            float originalL = luminance(out.p[i]), blurL = luminance(blurred.p[i]);
            for (int c = 0; c < 3; ++c)
                out.p[i][c] = out.p[i][c] * (1 - colorNr) + (blurred.p[i][c] + originalL - blurL) * colorNr;
        }
    }
    double sharpness = parameter(parameters, "sharpness", 0) / 100.0;
    if (sharpness > 0)
        out = sharpen(out, parameter(parameters, "sharpnessRadius", 1), sharpness * 1.5,
                      parameter(parameters, "masking", 0) / 100 * .1);
    double distortion = parameter(parameters, "distortion", 0) / 100,
           vignette = parameter(parameters, "vignette", 0) / 100,
           rotation = parameter(parameters, "angle", 0) * pi / 180;
    if (std::abs(distortion) > .001 || std::abs(vignette) > .001 || std::abs(rotation) > .00001) {
        Image corrected = out;
        double cx = (out.w - 1) * .5, cy = (out.h - 1) * .5, rx = qMax(1.0, cx), ry = qMax(1.0, cy);
        for (int y = 0; y < out.h; ++y)
            for (int x = 0; x < out.w; ++x) {
                double dx = (x - cx) / rx, dy = (y - cy) / ry, r2 = dx * dx + dy * dy;
                const double localX = dx * rx * (1 + distortion * r2 * .3);
                const double localY = dy * ry * (1 + distortion * r2 * .3);
                const double sx = cx + localX * std::cos(rotation) + localY * std::sin(rotation);
                const double sy = cy - localX * std::sin(rotation) + localY * std::cos(rotation);
                auto p = sx < 0 || sy < 0 || sx > out.w - 1 || sy > out.h - 1 ? Pixel{} : out.sample(sx, sy);
                for (int c = 0; c < 3; ++c)
                    p[c] *= qMax(0.0, 1 + vignette * r2 * .5);
                corrected.p[y * out.w + x] = p;
            }
        out = corrected;
    }
    QImage result = out.output(source, bitDepth);
    result.setColorSpace(QColorSpace(QColorSpace::SRgb));
    QString profile = parameters.value("profile").toString("sRGB");
    QColorSpace target = profile == "Display P3" ? QColorSpace(QColorSpace::DisplayP3)
                         : profile.contains("Adobe", Qt::CaseInsensitive) ? QColorSpace(QColorSpace::AdobeRgb)
                         : profile.contains("ProPhoto", Qt::CaseInsensitive)
                             ? QColorSpace(QColorSpace::ProPhotoRgb)
                             : QColorSpace(QColorSpace::SRgb);
    if (target != result.colorSpace())
        result = result.convertedToColorSpace(target);
    return result;
}
} // namespace serika
