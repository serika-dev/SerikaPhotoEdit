#include "FillRenderer.h"
#include <QBuffer>
#include <QImageReader>
#include <QJsonArray>
#include <QMap>
#include <QPainter>
#include <algorithm>
#include <cmath>
namespace serika {
namespace {
QImage imageFor(QSize size, int depth) {
    QImage image(size, depth == 32   ? QImage::Format_RGBA32FPx4
                       : depth == 16 ? QImage::Format_RGBA64
                                     : QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    return image;
}
qreal number(const QJsonObject &p, const char *key, qreal fallback, qreal lo, qreal hi) {
    const qreal value = p.value(key).toDouble(fallback);
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}
} // namespace
QGradientStops gradientStops(const QJsonObject &p, QColor start, QColor end) {
    const QColor first(
        p.value("start").toString(p.value("startColor").toString(start.name(QColor::HexArgb))));
    const QColor last(p.value("end").toString(p.value("endColor").toString(end.name(QColor::HexArgb))));
    QMap<qreal, QColor> colors;
    const auto entries = p.value("stops").toArray();
    for (int i = 0; i < std::min(256, int(entries.size())); ++i) {
        const auto stop = entries[i].toObject();
        QColor color(stop.value("color").toString());
        const qreal position = stop.value("position").toDouble(-1);
        if (color.isValid() && std::isfinite(position) && position >= 0 && position <= 1)
            colors[position] = color;
    }
    if (colors.size() < 2) {
        colors[0] = first.isValid() ? first : start;
        colors[1] = last.isValid() ? last : end;
    } else {
        if (!colors.contains(0))
            colors[0] = colors.first();
        if (!colors.contains(1))
            colors[1] = colors.last();
    }
    QGradientStops result;
    const bool reverse = p.value("reverse").toBool();
    for (auto it = colors.begin(); it != colors.end(); ++it)
        result.append({reverse ? 1 - it.key() : it.key(), it.value()});
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    return result;
}
QColor sampleGradient(const QGradientStops &stops, qreal position) {
    if (stops.isEmpty())
        return Qt::transparent;
    position = std::isfinite(position) ? std::clamp(position, qreal(0), qreal(1)) : 0;
    auto right = std::upper_bound(stops.begin(), stops.end(), position,
                                  [](qreal x, const auto &stop) { return x < stop.first; });
    if (right == stops.begin())
        return right->second;
    if (right == stops.end())
        return stops.last().second;
    const auto &left = *(right - 1);
    const qreal t = (position - left.first) / (right->first - left.first);
    const auto a = left.second, b = right->second;
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}
QImage renderGradientFill(QSize size, int depth, const QJsonObject &p, QColor start) {
    QImage image = imageFor(size, depth);
    if (image.isNull())
        return image;
    const auto stops = gradientStops(p, start);
    const qreal angle = number(p, "angle", 0, -36000, 36000), radians = angle * 3.141592653589793 / 180;
    const qreal scale = number(p, "scale", 100, 1, 1000) / 100;
    const QPointF center(size.width() * number(p, "centerX", .5, -10, 10),
                         size.height() * number(p, "centerY", .5, -10, 10));
    const qreal length = std::max(
        qreal(1), (std::abs(size.width() * std::cos(radians)) + std::abs(size.height() * std::sin(radians))) *
                      scale / 2);
    const QPointF delta(std::cos(radians) * length, std::sin(radians) * length);
    const auto style = p.value("style").toString("Linear");
    if (style == "Diamond") {
        for (int y = 0; y < size.height(); ++y)
            for (int x = 0; x < size.width(); ++x) {
                const auto d = QPointF(x + .5, y + .5) - center;
                const qreal u = d.x() * std::cos(radians) + d.y() * std::sin(radians),
                            v = -d.x() * std::sin(radians) + d.y() * std::cos(radians);
                image.setPixelColor(x, y, sampleGradient(stops, (std::abs(u) + std::abs(v)) / length));
            }
        return image;
    }
    QBrush brush;
    if (style == "Radial") {
        QRadialGradient g(center, length);
        g.setStops(stops);
        brush = QBrush(g);
    } else if (style == "Angle") {
        QConicalGradient g(center, -angle);
        g.setStops(stops);
        brush = QBrush(g);
    } else {
        QLinearGradient g(center - delta, center + delta);
        if (style == "Reflected") {
            QGradientStops mirrored;
            for (auto it = stops.crbegin(); it != stops.crend(); ++it)
                mirrored.append({.5 - .5 * it->first, it->second});
            for (const auto &stop : stops)
                if (stop.first > 0)
                    mirrored.append({.5 + .5 * stop.first, stop.second});
            g.setStops(mirrored);
        } else
            g.setStops(stops);
        brush = QBrush(g);
    }
    QPainter painter(&image);
    painter.fillRect(image.rect(), brush);
    return image;
}
QImage defaultPattern() {
    QImage tile(24, 24, QImage::Format_RGBA8888);
    tile.fill(QColor("#d9cbfa"));
    QPainter painter(&tile);
    painter.fillRect(0, 0, 12, 12, QColor("#7254aa"));
    painter.fillRect(12, 12, 12, 12, QColor("#7254aa"));
    return tile;
}
QImage renderPatternFill(QSize size, int depth, const QJsonObject &p) {
    QImage image = imageFor(size, depth), tile;
    if (image.isNull())
        return image;
    const auto encoded = p.value("patternPng").toString().toLatin1();
    if (!encoded.isEmpty() && encoded.size() <= 8 * 1024 * 1024) {
        QByteArray data = QByteArray::fromBase64(encoded);
        QBuffer buffer(&data);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "PNG");
        const QSize source = reader.size();
        if (source.isValid() && source.width() <= 4096 && source.height() <= 4096)
            tile = reader.read();
    }
    if (tile.isNull())
        tile = defaultPattern();
    const qreal scale = number(p, "scale", 100, 1, 1000) / 100;
    QTransform transform;
    transform.translate(number(p, "offsetX", 0, -300000, 300000), number(p, "offsetY", 0, -300000, 300000));
    transform.rotate(number(p, "angle", 0, -36000, 36000));
    transform.scale(scale, scale);
    QBrush brush(tile);
    brush.setTransform(transform);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.fillRect(image.rect(), brush);
    return image;
}
} // namespace serika
