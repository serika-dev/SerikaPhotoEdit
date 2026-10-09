#include "CmykExport.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace serika {
namespace {
bool fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}
struct TiffEntry {
    quint16 tag;
    quint16 type;
    quint32 count;
    QByteArray data;
};
QByteArray shortValue(quint16 value) {
    QByteArray result(2, '\0');
    qToLittleEndian(value, reinterpret_cast<uchar *>(result.data()));
    return result;
}
QByteArray longValue(quint32 value) {
    QByteArray result(4, '\0');
    qToLittleEndian(value, reinterpret_cast<uchar *>(result.data()));
    return result;
}
bool saveBytes(const QString &path, const QByteArray &bytes, QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, "Cannot write " + path + ": " + file.errorString());
    return true;
}
bool tiff(const QString &path, QSize size, int depth, int channels, const QByteArray &samples,
          const QByteArray &icc, qreal dpi, const QString &description, QString *error) {
    if (!std::isfinite(dpi) || dpi < 1 || dpi > 100000)
        return fail(error, "TIFF resolution must be between 1 and 100000 DPI.");
    const qint64 byteCount = qint64(size.width()) * size.height() * channels * (depth / 8);
    if (size.width() <= 0 || size.height() <= 0 || (depth != 8 && depth != 16) ||
        (channels != 1 && channels != 4) || byteCount != samples.size() || byteCount > 512ll * 1024 * 1024)
        return fail(error, "The TIFF ink sample buffer has invalid dimensions, depth, or length.");
    QVector<TiffEntry> entries;
    const auto addShort = [&](quint16 tag, quint16 value) { entries.append({tag, 3, 1, shortValue(value)}); };
    const auto addLong = [&](quint16 tag, quint32 value) { entries.append({tag, 4, 1, longValue(value)}); };
    const auto addAscii = [&](quint16 tag, QByteArray text) {
        text.append('\0');
        entries.append({tag, 2, quint32(text.size()), text});
    };
    addLong(256, quint32(size.width()));
    addLong(257, quint32(size.height()));
    QByteArray bits;
    for (int c = 0; c < channels; ++c)
        bits.append(shortValue(quint16(depth)));
    entries.append({258, 3, quint32(channels), bits});
    addShort(259, 1);                     // Uncompressed.
    addShort(262, channels == 4 ? 5 : 1); // Separated CMYK, or grayscale black-is-zero.
    addAscii(270, description.toUtf8());
    addLong(273, 0); // Single strip; offset assigned below.
    addShort(277, quint16(channels));
    addLong(278, quint32(size.height()));
    addLong(279, quint32(samples.size()));
    const auto resolution = longValue(quint32(std::lround(dpi * 1000))) + longValue(1000);
    entries.append({282, 5, 1, resolution});
    entries.append({283, 5, 1, resolution});
    addShort(284, 1); // Interleaved samples.
    addShort(296, 2); // Inches.
    addAscii(305, "Serika PhotoEdit");
    if (channels == 4) {
        addShort(332, 1); // InkSet = CMYK.
        const QByteArray names("Cyan\0Magenta\0Yellow\0Black\0", 26);
        entries.append({333, 2, quint32(names.size()), names});
        addShort(334, 4); // Four inks; no alpha channel.
    }
    if (!icc.isEmpty())
        entries.append({34675, 7, quint32(icc.size()), icc});
    std::sort(entries.begin(), entries.end(),
              [](const TiffEntry &a, const TiffEntry &b) { return a.tag < b.tag; });
    const quint32 baseOffset = 8 + 2 + quint32(entries.size()) * 12 + 4;
    QByteArray extra;
    QVector<quint32> offsets;
    for (const auto &entry : entries) {
        if (entry.data.size() <= 4)
            offsets.append(0);
        else {
            offsets.append(baseOffset + quint32(extra.size()));
            extra.append(entry.data);
            if (extra.size() % 2)
                extra.append('\0');
        }
    }
    const quint32 pixelOffset = baseOffset + quint32(extra.size());
    if (quint64(pixelOffset) + quint64(samples.size()) > 0xffffffffu)
        return fail(error, "This classic TIFF export would exceed the 4 GiB file limit.");
    QByteArray header;
    QDataStream stream(&header, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("II", 2);
    stream << quint16(42) << quint32(8) << quint16(entries.size());
    for (qsizetype i = 0; i < entries.size(); ++i) {
        const auto &entry = entries[i];
        stream << entry.tag << entry.type << entry.count;
        if (entry.tag == 273)
            stream << pixelOffset;
        else if (entry.data.size() <= 4) {
            QByteArray inlineData = entry.data;
            inlineData.resize(4, '\0');
            stream.writeRawData(inlineData.constData(), 4);
        } else
            stream << offsets[i];
    }
    stream << quint32(0); // No next IFD.
    header.append(extra);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(header) != header.size())
        return fail(error, "Cannot write TIFF header: " + file.errorString());
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    if (depth == 16) {
        QByteArray row(qsizetype(size.width()) * channels * 2, Qt::Uninitialized);
        for (int y = 0; y < size.height(); ++y) {
            const char *input = samples.constData() + y * row.size();
            for (qsizetype p = 0; p < row.size(); p += 2) {
                quint16 value;
                std::memcpy(&value, input + p, 2);
                qToLittleEndian(value, reinterpret_cast<uchar *>(row.data() + p));
            }
            if (file.write(row) != row.size())
                return fail(error, "Cannot write TIFF samples: " + file.errorString());
        }
    } else
#endif
        if (file.write(samples) != samples.size())
        return fail(error, "Cannot write TIFF samples: " + file.errorString());
    if (!file.commit())
        return fail(error, "Cannot commit TIFF: " + file.errorString());
    return true;
}
bool validate(const CmykImage &image, QString *error) {
    if (!image.isValid())
        return fail(error,
                    "The CMYK image does not contain four complete integer ink channels and an ICC profile.");
    IccProfileInfo info;
    if (!inspectIccProfile(image.iccProfile, &info, error))
        return false;
    return info.cmykOutput || fail(error, "The exported ICC must describe a CMYK output device.");
}
} // namespace
bool writeCmykTiff(const QString &path, const CmykImage &image, qreal dpi, QString *error) {
    if (error)
        error->clear();
    if (!validate(image, error))
        return false;
    const QString description = "ICC-separated CMYK; " + image.profileName + "; " +
                                renderingIntentName(image.intent) +
                                (image.blackPointCompensation ? "; black-point compensation" : "") +
                                "; alpha flattened on " + image.paper.name(QColor::HexRgb);
    return tiff(path, image.size, image.bitDepth, 4, image.samples, image.iccProfile, dpi, description,
                error);
}
bool writeCmykSeparations(const QString &directory, const CmykImage &image, qreal dpi, QString *error) {
    if (error)
        error->clear();
    if (!validate(image, error))
        return false;
    const QFileInfo destination(QDir::cleanPath(QFileInfo(directory).absoluteFilePath()));
    if (destination.exists() || destination.fileName().isEmpty())
        return fail(error, "Choose a new directory for the ink separations; the destination already exists.");
    QDir parent(destination.absolutePath());
    if (!parent.exists())
        return fail(error, "The parent directory for the ink separations does not exist.");
    QTemporaryDir staging(parent.filePath(".serika-separations-XXXXXX"));
    if (!staging.isValid())
        return fail(error, "Cannot create a temporary separation directory.");
    const QStringList names{"Cyan", "Magenta", "Yellow", "Black"};
    QJsonArray channels;
    for (int c = 0; c < 4; ++c) {
        auto samples = image.channelSamples(c);
        if (image.bitDepth == 8)
            for (auto &sample : samples)
                sample = char(255 - quint8(sample));
        else
            for (qsizetype i = 0; i < samples.size(); i += 2) {
                quint16 value;
                std::memcpy(&value, samples.constData() + i, 2);
                value = quint16(65535 - value);
                std::memcpy(samples.data() + i, &value, 2);
            }
        const auto name = names[c] + ".tif";
        if (!tiff(staging.filePath(name), image.size, image.bitDepth, 1, samples, {}, dpi,
                  names[c] + " ink separation; white = no ink, black = 100% ink", error))
            return false;
        channels.append(QJsonObject{{"ink", names[c]}, {"file", name}, {"index", c}});
    }
    const QJsonObject manifest{
        {"format", "Serika CMYK ink separations"},
        {"version", 1},
        {"width", image.size.width()},
        {"height", image.size.height()},
        {"bitDepth", image.bitDepth},
        {"dpi", dpi},
        {"channels", channels},
        {"profile", "output.icc"},
        {"profileName", image.profileName},
        {"profileSha256",
         QString::fromLatin1(QCryptographicHash::hash(image.iccProfile, QCryptographicHash::Sha256).toHex())},
        {"intent", renderingIntentName(image.intent)},
        {"blackPointCompensation", image.blackPointCompensation},
        {"paper", image.paper.name(QColor::HexRgb)},
        {"sampleMeaning",
         "grayscale white=0% ink, black=100% ink; these are coverage plates, not grayscale ICC colors"}};
    if (!saveBytes(staging.filePath("output.icc"), image.iccProfile, error) ||
        !saveBytes(staging.filePath("manifest.json"), QJsonDocument(manifest).toJson(), error))
        return false;
    if (!parent.rename(QFileInfo(staging.path()).fileName(), destination.fileName()))
        return fail(error, "Cannot move the completed separations into the destination directory.");
    staging.setAutoRemove(false);
    return true;
}
} // namespace serika
