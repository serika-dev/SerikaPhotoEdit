#include "LayerExtras.h"
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace serika::io {
namespace {
QJsonArray point(QPointF p) { return {p.x(), p.y()}; }
QPointF point(const QJsonValue &v) {
    const auto a = v.toArray();
    return a.size() == 2 ? QPointF(a[0].toDouble(), a[1].toDouble()) : QPointF();
}
QJsonObject pathData(const QPainterPath &path) {
    QJsonArray elements;
    for (int i = 0; i < path.elementCount(); ++i) {
        const auto e = path.elementAt(i);
        elements.append(QJsonArray{int(e.type), e.x, e.y});
    }
    return {{"elements", elements}, {"fillRule", int(path.fillRule())}};
}
bool readPath(QPainterPath &path, const QJsonObject &o) {
    const auto elements = o["elements"].toArray();
    if (elements.size() > 200000)
        return false;
    QPainterPath restored;
    const auto validPoint = [](const QJsonArray &e) {
        return e.size() == 3 && e[0].isDouble() && e[1].isDouble() && e[2].isDouble() &&
               std::isfinite(e[1].toDouble()) && std::isfinite(e[2].toDouble()) &&
               std::abs(e[1].toDouble()) <= 1e9 && std::abs(e[2].toDouble()) <= 1e9;
    };
    for (int i = 0; i < elements.size(); ++i) {
        auto e = elements[i].toArray();
        if (!validPoint(e))
            return false;
        const QPointF p(e[1].toDouble(), e[2].toDouble());
        if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
            return false;
        if (e[0].toInt() == QPainterPath::MoveToElement)
            restored.moveTo(p);
        else if (e[0].toInt() == QPainterPath::LineToElement)
            restored.lineTo(p);
        else if (e[0].toInt() == QPainterPath::CurveToElement) {
            if (i + 2 >= elements.size())
                return false;
            const auto b = elements[++i].toArray(), c = elements[++i].toArray();
            if (!validPoint(b) || !validPoint(c) || b[0].toInt() != QPainterPath::CurveToDataElement ||
                c[0].toInt() != QPainterPath::CurveToDataElement)
                return false;
            restored.cubicTo(p, QPointF(b[1].toDouble(), b[2].toDouble()),
                             QPointF(c[1].toDouble(), c[2].toDouble()));
        } else
            return false;
    }
    restored.setFillRule(o["fillRule"].toInt() == Qt::WindingFill ? Qt::WindingFill : Qt::OddEvenFill);
    path = restored;
    return true;
}
} // namespace
QJsonObject layerExtras(const Layer &l, bool retainSmartSource) {
    QJsonObject o{{"maskDensity", l.maskDensity},
                  {"maskFeather", l.maskFeather},
                  {"maskLinked", l.maskLinked},
                  {"maskOffset", point(l.maskOffset)},
                  {"vectorMask", pathData(l.vectorMask)},
                  {"vectorMaskEnabled", l.vectorMaskEnabled},
                  {"vectorMaskDensity", l.vectorMaskDensity},
                  {"vectorMaskFeather", l.vectorMaskFeather},
                  {"vectorMaskLinked", l.vectorMaskLinked},
                  {"vectorMaskOffset", point(l.vectorMaskOffset)},
                  {"smartFilters", l.smartFilters}};
    if (retainSmartSource && l.kind == LayerKind::SmartObject) {
        QJsonArray tiles;
        for (auto it = l.pixels.tiles.cbegin(); it != l.pixels.tiles.cend(); ++it) {
            const QImage &im = it.value();
            QByteArray bytes(reinterpret_cast<const char *>(im.constBits()), im.sizeInBytes());
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
            const int unit = im.format() == QImage::Format_RGBA64       ? 2
                             : im.format() == QImage::Format_RGBA32FPx4 ? 4
                                                                        : 1;
            for (qsizetype i = 0; i + unit <= bytes.size(); i += unit)
                std::reverse(bytes.begin() + i, bytes.begin() + i + unit);
#endif
            tiles.append(QJsonObject{{"x", int(it.key() >> 32)},
                                     {"y", int(it.key() & 0xffffffffu)},
                                     {"w", im.width()},
                                     {"h", im.height()},
                                     {"stride", im.bytesPerLine()},
                                     {"data", QString::fromLatin1(qCompress(bytes).toBase64())}});
        }
        o["smartSource"] = QJsonObject{{"width", l.pixels.size.width()},
                                       {"height", l.pixels.size.height()},
                                       {"format", int(l.pixels.format)},
                                       {"tiles", tiles}};
    }
    return o;
}
bool restoreLayerExtras(Layer &l, const QJsonObject &o) {
    l.maskDensity = qBound(0., o["maskDensity"].toDouble(1), 1.);
    l.maskFeather = qBound(0., o["maskFeather"].toDouble(), 1000.);
    l.maskLinked = o["maskLinked"].toBool(true);
    l.maskOffset = point(o["maskOffset"]);
    if (!readPath(l.vectorMask, o["vectorMask"].toObject()))
        return false;
    l.vectorMaskEnabled = o["vectorMaskEnabled"].toBool(true);
    l.vectorMaskDensity = qBound(0., o["vectorMaskDensity"].toDouble(1), 1.);
    l.vectorMaskFeather = qBound(0., o["vectorMaskFeather"].toDouble(), 1000.);
    l.vectorMaskLinked = o["vectorMaskLinked"].toBool(true);
    l.vectorMaskOffset = point(o["vectorMaskOffset"]);
    l.smartFilters = o["smartFilters"].toArray();
    if (o.contains("smartSource")) {
        const auto source = o["smartSource"].toObject();
        TileImage pixels;
        pixels.size = {source["width"].toInt(), source["height"].toInt()};
        pixels.format = QImage::Format(source["format"].toInt());
        if (pixels.size.width() < 1 || pixels.size.height() < 1 || pixels.size.width() > 300000 ||
            pixels.size.height() > 300000 ||
            (pixels.format != QImage::Format_RGBA8888 && pixels.format != QImage::Format_RGBA64 &&
             pixels.format != QImage::Format_RGBA32FPx4))
            return false;
        quint64 allocatedBytes = 0;
        for (const auto &entry : source["tiles"].toArray()) {
            const auto tile = entry.toObject();
            const int x = tile["x"].toInt(), y = tile["y"].toInt(), w = tile["w"].toInt(),
                      h = tile["h"].toInt();
            if (x < 0 || y < 0 || qint64(x) * 256 >= pixels.size.width() ||
                qint64(y) * 256 >= pixels.size.height() || w < 1 || h < 1 || w > 256 || h > 256 ||
                qint64(x) * 256 + w > pixels.size.width() || qint64(y) * 256 + h > pixels.size.height() ||
                pixels.tiles.contains(TileImage::key(x, y)))
                return false;
            QImage im(w, h, pixels.format);
            if (im.isNull() || tile["stride"].toInt() != im.bytesPerLine())
                return false;
            allocatedBytes += im.sizeInBytes();
            if (allocatedBytes > 1024ull * 1024 * 1024)
                return false;
            const auto compressed = QByteArray::fromBase64(tile["data"].toString().toLatin1());
            if (compressed.size() < 4 ||
                qFromBigEndian<quint32>(compressed.constData()) != quint64(im.sizeInBytes()))
                return false;
            auto bytes = qUncompress(compressed);
            if (bytes.size() != im.sizeInBytes())
                return false;
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
            const int unit = im.format() == QImage::Format_RGBA64       ? 2
                             : im.format() == QImage::Format_RGBA32FPx4 ? 4
                                                                        : 1;
            for (qsizetype i = 0; i + unit <= bytes.size(); i += unit)
                std::reverse(bytes.begin() + i, bytes.begin() + i + unit);
#endif
            memcpy(im.bits(), bytes.constData(), size_t(bytes.size()));
            pixels.tiles.insert(TileImage::key(x, y), im);
        }
        l.pixels = pixels;
    }
    return true;
}
} // namespace serika::io
