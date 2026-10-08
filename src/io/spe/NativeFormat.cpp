#include "io/FormatInternal.h"
#include "io/LayerExtras.h"
#include <QBuffer>
#include <QColorSpace>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <memory>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace serika::io {
namespace {
constexpr quint32 Version = 1;
constexpr quint64 MaxChunk = 1024ull * 1024 * 1024;
void configure(QDataStream &s) {
    s.setByteOrder(QDataStream::LittleEndian);
    s.setVersion(QDataStream::Qt_6_0);
}
quint32 crc32(const QByteArray &b) {
    static const auto table = [] {
        std::array<quint32, 256> values{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
            values[i] = c;
        }
        return values;
    }();
    quint32 crc = 0xffffffffu;
    for (const unsigned char c : b)
        crc = table[(crc ^ c) & 255] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}
QJsonArray color(const QColor &c) { return {c.redF(), c.greenF(), c.blueF(), c.alphaF()}; }
QColor color(const QJsonValue &v) {
    auto a = v.toArray();
    if (a.size() != 4)
        return Qt::black;
    return QColor::fromRgbF(a.at(0).toDouble(), a.at(1).toDouble(), a.at(2).toDouble(), a.at(3).toDouble(1));
}
QJsonObject layerHeader(const Layer &l) {
    QJsonObject o;
    o["id"] = QString::number(l.id);
    o["name"] = l.name;
    o["kind"] = int(l.kind);
    o["pixelWidth"] = l.pixels.size.width();
    o["pixelHeight"] = l.pixels.size.height();
    o["pixelFormat"] = int(l.pixels.format);
    o["maskEnabled"] = l.maskEnabled;
    o["maskTarget"] = l.maskTarget;
    o["visible"] = l.visible;
    o["locked"] = l.locked;
    o["lockAlpha"] = l.lockAlpha;
    o["lockPosition"] = l.lockPosition;
    o["clipped"] = l.clipped;
    o["opacity"] = l.opacity;
    o["fill"] = l.fill;
    o["blend"] = l.blendMode;
    o["parent"] = QString::number(l.parentId);
    o["offset"] = QJsonArray{l.offset.x(), l.offset.y()};
    o["text"] = l.text;
    o["font"] = l.font.toString();
    o["color"] = color(l.color);
    o["stroke"] = color(l.stroke);
    o["strokeWidth"] = l.strokeWidth;
    o["adjustment"] = l.adjustment;
    o["parameters"] = l.parameters;
    o["effects"] = l.effects;
    o["linkedPath"] = l.linkedPath;
    o["extras"] = layerExtras(l);
    return o;
}
Layer readLayerHeader(const QJsonObject &o, bool *valid) {
    Layer l;
    l.id = o["id"].toString().toULongLong();
    l.name = o["name"].toString();
    l.kind = LayerKind(o["kind"].toInt());
    l.pixels.size = QSize(o["pixelWidth"].toInt(), o["pixelHeight"].toInt());
    l.pixels.format = QImage::Format(o["pixelFormat"].toInt());
    l.maskEnabled = o["maskEnabled"].toBool(true);
    l.maskTarget = o["maskTarget"].toBool();
    l.visible = o["visible"].toBool(true);
    l.locked = o["locked"].toBool();
    l.lockAlpha = o["lockAlpha"].toBool();
    l.lockPosition = o["lockPosition"].toBool();
    l.clipped = o["clipped"].toBool();
    l.opacity = o["opacity"].toDouble(1);
    l.fill = o["fill"].toDouble(1);
    l.blendMode = o["blend"].toString("Normal");
    l.parentId = o["parent"].toString().toULongLong();
    auto offset = o["offset"].toArray();
    if (offset.size() == 2)
        l.offset = QPointF(offset.at(0).toDouble(), offset.at(1).toDouble());
    l.text = o["text"].toString();
    l.font.fromString(o["font"].toString());
    l.color = color(o["color"]);
    l.stroke = color(o["stroke"]);
    l.strokeWidth = o["strokeWidth"].toDouble(1);
    l.adjustment = o["adjustment"].toString();
    l.parameters = o["parameters"].toObject();
    l.effects = o["effects"].toObject();
    l.linkedPath = o["linkedPath"].toString();
    *valid = restoreLayerExtras(l, o["extras"].toObject());
    return l;
}
// The pixel payload preserves the Qt format (including float pixels), row alignment,
// colour table, profile, device pixel ratio, and physical resolution. Multi-byte
// native samples are normalized to little endian on a big endian host.
QByteArray imagePayload(quint64 id, qint32 x, qint32 y, const QImage &im) {
    QByteArray out;
    QDataStream s(&out, QIODevice::WriteOnly);
    configure(s);
    s << id << x << y << qint32(im.format()) << qint32(im.width()) << qint32(im.height())
      << qint32(im.bytesPerLine());
    s << quint8(0) << im.devicePixelRatio() << qint32(im.dotsPerMeterX()) << qint32(im.dotsPerMeterY())
      << im.colorTable() << im.colorSpace().iccProfile();
    QByteArray pixels(reinterpret_cast<const char *>(im.constBits()), im.sizeInBytes());
    const int activeBytes = (im.width() * im.depth() + 7) / 8;
    if (activeBytes < im.bytesPerLine())
        for (int row = 0; row < im.height(); ++row)
            std::fill(pixels.begin() + qsizetype(row) * im.bytesPerLine() + activeBytes,
                      pixels.begin() + qsizetype(row + 1) * im.bytesPerLine(), 0);
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    const int unit =
        im.format() == QImage::Format_RGBA64 || im.format() == QImage::Format_RGBA64_Premultiplied ||
                im.format() == QImage::Format_RGBX64 || im.format() == QImage::Format_Grayscale16
            ? 2
        : im.depth() >= 32 && im.format() != QImage::Format_RGBA8888 &&
                im.format() != QImage::Format_RGBA8888_Premultiplied && im.format() != QImage::Format_RGBX8888
            ? 4
            : 1;
    for (qsizetype i = 0; i + unit <= pixels.size(); i += unit)
        std::reverse(pixels.begin() + i, pixels.begin() + i + unit);
#endif
    s << quint64(pixels.size());
    s.writeRawData(pixels.constData(), pixels.size());
    return out;
}
bool readImage(const QByteArray &bytes, quint64 &id, qint32 &x, qint32 &y, QImage &im) {
    QDataStream s(bytes);
    configure(s);
    qint32 format, w, h, stride, dx, dy;
    quint8 codec;
    qreal ratio;
    QList<QRgb> palette;
    QByteArray profile;
    quint64 length;
    s >> id >> x >> y >> format >> w >> h >> stride >> codec >> ratio >> dx >> dy >> palette >> profile >>
        length;
    if (s.status() != QDataStream::Ok || codec != 0 || w <= 0 || h <= 0 || w > 300000 || h > 300000 ||
        stride <= 0 || length != quint64(stride) * h || length > MaxChunk ||
        length > quint64(s.device()->bytesAvailable()) || !qIsFinite(ratio) || ratio <= 0)
        return false;
    if (format <= QImage::Format_Invalid || format >= QImage::NImageFormats)
        return false;
    QImage result(w, h, QImage::Format(format));
    if (result.isNull() || result.bytesPerLine() != stride || quint64(result.sizeInBytes()) != length)
        return false;
    if (s.readRawData(reinterpret_cast<char *>(result.bits()), length) != qint64(length))
        return false;
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    const int unit =
        result.format() == QImage::Format_RGBA64 || result.format() == QImage::Format_RGBA64_Premultiplied ||
                result.format() == QImage::Format_RGBX64 || result.format() == QImage::Format_Grayscale16
            ? 2
        : result.depth() >= 32 && result.format() != QImage::Format_RGBA8888 &&
                result.format() != QImage::Format_RGBA8888_Premultiplied &&
                result.format() != QImage::Format_RGBX8888
            ? 4
            : 1;
    for (qsizetype i = 0; i + unit <= result.sizeInBytes(); i += unit)
        std::reverse(result.bits() + i, result.bits() + i + unit);
#endif
    result.setDevicePixelRatio(ratio);
    result.setDotsPerMeterX(dx);
    result.setDotsPerMeterY(dy);
    result.setColorTable(palette);
    if (!profile.isEmpty())
        result.setColorSpace(QColorSpace::fromIccProfile(profile));
    im = result;
    return true;
}
bool chunk(QDataStream &s, const char *type, const QByteArray &payload) {
    s.writeRawData(type, 4);
    s << quint64(payload.size());
    s.writeRawData(payload.constData(), payload.size());
    s << crc32(payload);
    return s.status() == QDataStream::Ok;
}
bool validSize(const QSize &s) {
    return s.width() > 0 && s.height() > 0 && s.width() <= 300000 && s.height() <= 300000;
}
} // namespace

bool writeNative(const Document *doc, const QString &path, QString *error) {
    if (!doc || !validSize(doc->state.size)) {
        if (error)
            *error = "Invalid document dimensions.";
        return false;
    }
    QSaveFile f(path);
    f.setDirectWriteFallback(false);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QJsonObject head{{"width", doc->state.size.width()},
                     {"height", doc->state.size.height()},
                     {"depth", doc->state.bitDepth},
                     {"mode", doc->state.colorMode},
                     {"resolution", doc->state.resolution},
                     {"active", doc->state.activeIndex},
                     {"profile", QString::fromLatin1(doc->state.iccProfile.toBase64())},
                     {"title", doc->title},
                     {"blendLinear", doc->blendLinear},
                     {"historyLimit", doc->historyLimit}};
    QJsonArray layers, guides;
    for (const auto &l : doc->state.layers)
        layers.append(layerHeader(l));
    for (const auto &g : doc->state.guides)
        guides.append(QJsonObject{{"vertical", g.vertical}, {"position", g.position}});
    head["layers"] = layers;
    head["guides"] = guides;
    QByteArray json = QJsonDocument(head).toJson(QJsonDocument::Compact);
    QDataStream s(&f);
    configure(s);
    s.writeRawData("SPE\0", 4);
    s << Version << quint32(json.size());
    s.writeRawData(json.constData(), json.size());
    // A separately checked header digest keeps corrupt scalar metadata from loading.
    chunk(s, "HEAD", json);
    for (const auto &l : doc->state.layers) {
        auto keys = l.pixels.tiles.keys();
        std::sort(keys.begin(), keys.end());
        for (auto key : keys)
            chunk(
                s, "TILE",
                imagePayload(l.id, qint32(key >> 32), qint32(key & 0xffffffffu), l.pixels.tiles.value(key)));
        if (!l.mask.isNull())
            chunk(s, "MASK", imagePayload(l.id, 0, 0, l.mask));
        if (!l.shape.isEmpty()) {
            QByteArray p;
            QDataStream ps(&p, QIODevice::WriteOnly);
            configure(ps);
            ps << l.id << l.shape;
            chunk(s, "PATH", p);
        }
    }
    if (!doc->state.selection.isNull())
        chunk(s, "SELE", imagePayload(0, 0, 0, doc->state.selection));
    chunk(s, "META", QJsonDocument(doc->state.metadata).toJson(QJsonDocument::Compact));
    QByteArray preview;
    QBuffer buffer(&preview);
    buffer.open(QIODevice::WriteOnly);
    doc->composite().scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&buffer, "JPG", 80);
    chunk(s, "PREV", preview);
    chunk(s, "DONE", {});
    if (s.status() != QDataStream::Ok || !f.flush()) {
        if (error)
            *error = f.errorString();
        return false;
    }
#ifdef Q_OS_WIN
    if (f.handle() != -1)
        FlushFileBuffers(reinterpret_cast<HANDLE>(f.handle()));
#else
    if (f.handle() != -1)
        ::fsync(int(f.handle()));
#endif
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

Document *readNative(const QString &path, QString *error, QObject *parent) {
    auto fail = [&](const QString &why) -> Document * {
        if (error)
            *error = why;
        return nullptr;
    };
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(f.errorString());
    QDataStream s(&f);
    configure(s);
    char magic[4];
    if (s.readRawData(magic, 4) != 4 || QByteArray(magic, 4) != QByteArray("SPE\0", 4))
        return fail("Not a Serika PhotoEdit document.");
    quint32 version, length;
    s >> version >> length;
    if (version != Version)
        return fail("Unsupported SPE version.");
    if (length > 16 * 1024 * 1024 || length > quint64(f.bytesAvailable()))
        return fail("Invalid SPE header length.");
    QByteArray json = f.read(length);
    QJsonParseError parse;
    QJsonDocument jd = QJsonDocument::fromJson(json, &parse);
    if (parse.error != QJsonParseError::NoError || !jd.isObject())
        return fail("Invalid SPE header JSON.");
    auto h = jd.object();
    auto doc = std::make_unique<Document>(parent);
    doc->state.size = QSize(h["width"].toInt(), h["height"].toInt());
    if (!validSize(doc->state.size))
        return fail("Invalid SPE dimensions.");
    doc->state.bitDepth = h["depth"].toInt(8);
    if (doc->state.bitDepth != 8 && doc->state.bitDepth != 16 && doc->state.bitDepth != 32)
        return fail("Invalid SPE bit depth.");
    doc->state.colorMode = h["mode"].toString("RGB");
    doc->state.resolution = h["resolution"].toDouble(72);
    doc->state.activeIndex = h["active"].toInt();
    doc->state.iccProfile = QByteArray::fromBase64(h["profile"].toString().toLatin1());
    doc->title = h["title"].toString(QFileInfo(path).completeBaseName());
    doc->filePath = path;
    doc->blendLinear = h["blendLinear"].toBool();
    doc->historyLimit = h["historyLimit"].toInt(50);
    QHash<quint64, int> indices;
    for (const auto &v : h["layers"].toArray()) {
        bool validExtras = true;
        auto l = readLayerHeader(v.toObject(), &validExtras);
        if (!validExtras)
            return fail("Invalid native mask or smart-object metadata.");
        if (l.id == 0 || indices.contains(l.id) || int(l.kind) < 0 ||
            int(l.kind) > int(LayerKind::Artboard) || (!l.pixels.size.isEmpty() && !validSize(l.pixels.size)))
            return fail("Invalid or duplicate SPE layer.");
        indices.insert(l.id, doc->state.layers.size());
        doc->state.layers.append(l);
    }
    if (doc->state.layers.size() > 100000)
        return fail("Excessive SPE layer count.");
    for (const auto &v : h["guides"].toArray()) {
        auto o = v.toObject();
        doc->state.guides.append({o["vertical"].toBool(), o["position"].toDouble()});
    }
    bool checkedHeader = false, done = false;
    while (!f.atEnd()) {
        char name[4];
        if (s.readRawData(name, 4) != 4)
            return fail("Truncated SPE chunk header.");
        quint64 n;
        s >> n;
        if (n > MaxChunk || n > quint64(qMax<qint64>(0, f.bytesAvailable() - 4)))
            return fail("Invalid SPE chunk length.");
        QByteArray b = f.read(n);
        quint32 crc;
        s >> crc;
        if (s.status() != QDataStream::Ok || crc32(b) != crc)
            return fail("SPE checksum failure; file is corrupt.");
        QByteArray type(name, 4);
        if (type == "HEAD") {
            if (b != json || checkedHeader)
                return fail("SPE header checksum mismatch.");
            checkedHeader = true;
        } else if (type == "TILE" || type == "MASK" || type == "SELE") {
            quint64 id;
            qint32 x, y;
            QImage im;
            if (!readImage(b, id, x, y, im))
                return fail("Invalid SPE image payload.");
            if (type == "SELE") {
                if (id != 0 || im.size() != doc->state.size)
                    return fail("Invalid selection size.");
                doc->state.selection = im;
            } else {
                if (!indices.contains(id))
                    return fail("SPE image references unknown layer.");
                auto &l = doc->state.layers[indices[id]];
                if (type == "MASK")
                    l.mask = im;
                else {
                    if (x < 0 || y < 0 || qint64(x) * TileImage::TileSize >= l.pixels.size.width() ||
                        qint64(y) * TileImage::TileSize >= l.pixels.size.height() ||
                        im.width() > TileImage::TileSize || im.height() > TileImage::TileSize ||
                        im.format() != l.pixels.format || l.pixels.tiles.contains(TileImage::key(x, y)))
                        return fail("Invalid SPE tile coordinates or format.");
                    l.pixels.tiles.insert(TileImage::key(x, y), im);
                }
            }
        } else if (type == "PATH") {
            QDataStream ps(b);
            configure(ps);
            quint64 id;
            QPainterPath shape;
            ps >> id >> shape;
            if (ps.status() != QDataStream::Ok || !indices.contains(id))
                return fail("Invalid SPE path.");
            doc->state.layers[indices[id]].shape = shape;
        } else if (type == "META") {
            auto m = QJsonDocument::fromJson(b, &parse);
            if (parse.error != QJsonParseError::NoError || !m.isObject())
                return fail("Invalid SPE metadata.");
            doc->state.metadata = m.object();
        } else if (type == "DONE") {
            if (n != 0 || !f.atEnd())
                return fail("Invalid SPE end marker.");
            done = true;
            break;
        } else if (type != "PREV")
            doc->importReport.append("Unknown SPE chunk preserved only in source: " +
                                     QString::fromLatin1(type));
    }
    if (!checkedHeader || !done)
        return fail("Incomplete SPE document.");
    for (const auto &l : doc->state.layers)
        if (l.parentId && !indices.contains(l.parentId))
            return fail("SPE layer references unknown parent.");
    doc->state.activeIndex = qBound(0, doc->state.activeIndex, qMax(0, doc->state.layers.size() - 1));
    doc->touch();
    doc->markSaved();
    return doc.release();
}
} // namespace serika::io
