#include "EmbeddedDocument.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <array>
namespace serika::io {
namespace {
quint32 checksum(const char *data, qsizetype length) {
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
    quint32 c = 0xffffffffu;
    for (qsizetype i = 0; i < length; ++i)
        c = table[(c ^ quint8(data[i])) & 255] ^ (c >> 8);
    return c ^ 0xffffffffu;
}
} // namespace
bool validEmbeddedDocument(const QByteArray &bytes) {
    if (bytes.size() < 28 || bytes.size() > MaxEmbeddedDocumentBytes ||
        bytes.first(4) != QByteArray("SPE\0", 4) || qFromLittleEndian<quint32>(bytes.constData() + 4) != 1)
        return false;
    const auto headerLength = qFromLittleEndian<quint32>(bytes.constData() + 8);
    if (headerLength > 16 * 1024 * 1024 || headerLength > quint64(bytes.size() - 28))
        return false;
    const auto header = bytes.mid(12, headerLength);
    const auto json = QJsonDocument::fromJson(header);
    if (!json.isObject())
        return false;
    const auto object = json.object();
    if (object["width"].toInt() < 1 || object["height"].toInt() < 1 || object["width"].toInt() > 300000 ||
        object["height"].toInt() > 300000)
        return false;
    const auto depth = object["depth"].toInt(8);
    if (depth != 8 && depth != 16 && depth != 32)
        return false;
    qsizetype at = 12 + headerLength;
    bool checkedHeader = false;
    while (bytes.size() - at >= 16) {
        const auto type = bytes.mid(at, 4);
        const auto length = qFromLittleEndian<quint64>(bytes.constData() + at + 4);
        if (length > quint64(bytes.size() - at - 16))
            return false;
        const auto end = at + 12 + qsizetype(length);
        if (checksum(bytes.constData() + at + 12, length) !=
            qFromLittleEndian<quint32>(bytes.constData() + end))
            return false;
        if (type == "HEAD") {
            if (checkedHeader || length != headerLength || bytes.mid(at + 12, length) != header)
                return false;
            checkedHeader = true;
        }
        at = end + 4;
        if (type == "DONE")
            return checkedHeader && length == 0 && at == bytes.size();
    }
    return false;
}
} // namespace serika::io
