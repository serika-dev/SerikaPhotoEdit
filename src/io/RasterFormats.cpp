#include "FormatInternal.h"
#include <QColorSpace>
#include <QDataStream>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QtEndian>
#include <cmath>
#include <stdexcept>

namespace serika::io {
QImage readTga(const QString &path, QString *error) {
    try {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            throw std::runtime_error(f.errorString().toStdString());
        QByteArray bytes = f.readAll();
        qsizetype pos = 0;
        auto take = [&](int n) {
            if (n < 0 || n > bytes.size() - pos)
                throw std::runtime_error("Truncated TGA pixels.");
            auto v = bytes.mid(pos, n);
            pos += n;
            return v;
        };
        auto header = take(18);
        auto u8 = [&](int p) { return quint8(header[p]); };
        auto u16 = [&](int p) { return qFromLittleEndian<quint16>(header.constData() + p); };
        int id = u8(0), map = u8(1), type = u8(2), paletteFirst = u16(3), paletteCount = u16(5),
            paletteDepth = u8(7), w = u16(12), h = u16(14), depth = u8(16), flags = u8(17);
        bool indexed = type == 1 || type == 9, gray = type == 3 || type == 11, rle = type >= 9;
        if (!w || !h || quint64(w) * h * 4 > 1024ull * 1024 * 1024 ||
            !(type == 1 || type == 2 || type == 3 || type == 9 || type == 10 || type == 11) || map > 1 ||
            !(depth == 8 || depth == 16 || depth == 24 || depth == 32))
            throw std::runtime_error("Unsupported or invalid TGA header.");
        take(id);
        auto rgb = [&](const QByteArray &data, int bits, bool alpha) -> QRgb {
            if (data.size() < (bits + 7) / 8)
                throw std::runtime_error("Invalid TGA colour sample.");
            if (bits == 8) {
                int v = quint8(data[0]);
                return qRgba(v, v, v, 255);
            }
            if (bits == 16) {
                quint16 v = quint8(data[0]) | (quint16(quint8(data[1])) << 8);
                return qRgba(((v >> 10) & 31) * 255 / 31, ((v >> 5) & 31) * 255 / 31, (v & 31) * 255 / 31,
                             alpha ? (v & 32768 ? 255 : 0) : 255);
            }
            return qRgba(quint8(data[2]), quint8(data[1]), quint8(data[0]),
                         bits == 32 && alpha ? quint8(data[3]) : 255);
        };
        QVector<QRgb> palette;
        if (map) {
            if (!(paletteDepth == 16 || paletteDepth == 24 || paletteDepth == 32))
                throw std::runtime_error("Unsupported TGA palette depth.");
            palette.resize(paletteFirst + paletteCount);
            for (int i = 0; i < paletteCount; ++i)
                palette[paletteFirst + i] =
                    rgb(take((paletteDepth + 7) / 8), paletteDepth, paletteDepth == 32);
        }
        if (indexed && !map)
            throw std::runtime_error("TGA indexed image has no palette.");
        QImage im(w, h, QImage::Format_RGBA8888);
        if (im.isNull())
            throw std::runtime_error("Cannot allocate TGA image.");
        int written = 0, total = w * h;
        auto readPixel = [&]() {
            auto b = take(depth / 8);
            if (indexed) {
                int index = depth == 8 ? quint8(b[0]) : qFromLittleEndian<quint16>(b.constData());
                if (index < paletteFirst || index >= palette.size())
                    throw std::runtime_error("TGA palette index is out of range.");
                return palette[index];
            }
            if (gray) {
                int v = quint8(b[0]);
                return qRgba(v, v, v, depth == 16 ? quint8(b[1]) : 255);
            }
            return rgb(b, depth, (flags & 15) > 0);
        };
        auto put = [&](QRgb c) {
            if (written >= total)
                throw std::runtime_error("TGA run exceeds image dimensions.");
            int x = written % w, y = written / w;
            if (flags & 16)
                x = w - 1 - x;
            if (!(flags & 32))
                y = h - 1 - y;
            auto p = im.scanLine(y) + x * 4;
            p[0] = qRed(c);
            p[1] = qGreen(c);
            p[2] = qBlue(c);
            p[3] = qAlpha(c);
            ++written;
        };
        while (written < total) {
            int count = 1;
            bool repeat = false;
            if (rle) {
                int code = quint8(take(1)[0]);
                count = (code & 127) + 1;
                repeat = code & 128;
            }
            if (count > total - written)
                throw std::runtime_error("TGA run exceeds image dimensions.");
            if (repeat) {
                auto c = readPixel();
                for (int i = 0; i < count; ++i)
                    put(c);
            } else
                for (int i = 0; i < count; ++i)
                    put(readPixel());
        }
        return im;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return {};
    }
}
bool writeTga(const QImage &source, const QString &path, QString *error) {
    if (source.isNull() || source.width() > 65535 || source.height() > 65535) {
        if (error)
            *error = "TGA dimensions must be between 1 and 65535.";
        return false;
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QDataStream s(&f);
    s.setByteOrder(QDataStream::LittleEndian);
    s << quint8(0) << quint8(0) << quint8(2) << quint16(0) << quint16(0) << quint8(0) << quint16(0)
      << quint16(0) << quint16(source.width()) << quint16(source.height()) << quint8(32) << quint8(0x28);
    auto image = source.convertToFormat(QImage::Format_RGBA8888);
    QByteArray row(image.width() * 4, Qt::Uninitialized);
    for (int y = 0; y < image.height(); ++y) {
        const auto p = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            row[x * 4] = p[x * 4 + 2];
            row[x * 4 + 1] = p[x * 4 + 1];
            row[x * 4 + 2] = p[x * 4];
            row[x * 4 + 3] = p[x * 4 + 3];
        }
        s.writeRawData(row.constData(), row.size());
    }
    s << quint32(0) << quint32(0);
    s.writeRawData("TRUEVISION-XFILE.\0", 18);
    if (s.status() != QDataStream::Ok || !f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

// Radiance RGBE uses a common base-two exponent. Values stay scene linear
// in the document's 32-bit floating point tiles.
QImage readHdr(const QString &path, QString *error) {
    try {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            throw std::runtime_error(f.errorString().toStdString());
        auto magic = f.readLine();
        if (!magic.startsWith("#?RADIANCE") && !magic.startsWith("#?RGBE"))
            throw std::runtime_error("Not a Radiance HDR image.");
        bool rgbe = false;
        for (int i = 0; i < 1000; ++i) {
            auto line = f.readLine(65536);
            if (line.isEmpty())
                throw std::runtime_error("Truncated HDR header.");
            if (line.trimmed().isEmpty())
                break;
            if (line.startsWith("FORMAT=32-bit_rle_rgbe"))
                rgbe = true;
        }
        if (!rgbe)
            throw std::runtime_error("HDR format must be RGBE.");
        auto resolution = QString::fromLatin1(f.readLine(65536)).trimmed();
        QRegularExpression pattern("^([+-])Y\\s+(\\d+)\\s+([+-])X\\s+(\\d+)$");
        auto match = pattern.match(resolution);
        if (!match.hasMatch())
            throw std::runtime_error("Unsupported HDR pixel orientation.");
        int h = match.captured(2).toInt(), w = match.captured(4).toInt();
        if (w < 1 || h < 1 || w > 300000 || h > 300000 || quint64(w) * h * 16 > 1024ull * 1024 * 1024)
            throw std::runtime_error("HDR dimensions exceed safe allocation limits.");
        QImage image(w, h, QImage::Format_RGBA32FPx4);
        if (image.isNull())
            throw std::runtime_error("Cannot allocate HDR image.");
        auto take = [&](qsizetype n) {
            auto b = f.read(n);
            if (b.size() != n)
                throw std::runtime_error("Truncated HDR pixels.");
            return b;
        };
        for (int y = 0; y < h; ++y) {
            QByteArray scan(w * 4, Qt::Uninitialized);
            auto first = take(4);
            bool rle = w >= 8 && w <= 32767 && quint8(first[0]) == 2 && quint8(first[1]) == 2 &&
                       !(quint8(first[2]) & 128);
            if (rle) {
                if ((quint8(first[2]) << 8 | quint8(first[3])) != w)
                    throw std::runtime_error("Invalid HDR scanline length.");
                for (int c = 0; c < 4; ++c) {
                    int x = 0;
                    while (x < w) {
                        auto run = take(2);
                        int count = quint8(run[0]);
                        if (!count)
                            throw std::runtime_error("Invalid HDR RLE run.");
                        if (count > 128) {
                            count -= 128;
                            if (x + count > w)
                                throw std::runtime_error("HDR RLE run exceeds scanline.");
                            for (int k = 0; k < count; ++k)
                                scan[(x++) * 4 + c] = run[1];
                        } else {
                            if (x + count > w)
                                throw std::runtime_error("HDR literal exceeds scanline.");
                            scan[(x++) * 4 + c] = run[1];
                            auto rest = take(count - 1);
                            for (char value : rest)
                                scan[(x++) * 4 + c] = value;
                        }
                    }
                }
            } else {
                int x = 0, repeatShift = 0;
                QByteArray pixel = first, previous;
                while (x < w) {
                    if (quint8(pixel[0]) == 1 && quint8(pixel[1]) == 1 && quint8(pixel[2]) == 1) {
                        if (previous.isEmpty() || repeatShift > 24)
                            throw std::runtime_error("Invalid legacy HDR repeat.");
                        const quint64 count = quint64(quint8(pixel[3])) << repeatShift;
                        if (!count || count > quint64(w - x))
                            throw std::runtime_error("Legacy HDR repeat exceeds scanline.");
                        for (quint64 i = 0; i < count; ++i) {
                            scan.replace(x * 4, 4, previous);
                            ++x;
                        }
                        repeatShift += 8;
                    } else {
                        scan.replace(x * 4, 4, pixel);
                        previous = pixel;
                        ++x;
                        repeatShift = 0;
                    }
                    if (x < w)
                        pixel = take(4);
                }
            }
            int dy = match.captured(1) == "-" ? y : h - 1 - y;
            auto dest = reinterpret_cast<float *>(image.scanLine(dy));
            for (int x = 0; x < w; ++x) {
                int dx = match.captured(3) == "+" ? x : w - 1 - x;
                int e = quint8(scan[x * 4 + 3]);
                float scale = e ? std::ldexp(1.0f, e - 136) : 0;
                for (int c = 0; c < 3; ++c)
                    dest[dx * 4 + c] = (quint8(scan[x * 4 + c]) + 0.5f) * scale;
                dest[dx * 4 + 3] = 1;
            }
        }
        image.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
        return image;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return {};
    }
}
bool writeHdr(const QImage &source, const QString &path, QString *error) {
    if (source.isNull()) {
        if (error)
            *error = "No image to write.";
        return false;
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QImage image = source.convertToFormat(QImage::Format_RGBA32FPx4);
    if (image.colorSpace().isValid())
        image = image.convertedToColorSpace(QColorSpace(QColorSpace::SRgbLinear), QImage::Format_RGBA32FPx4);
    f.write("#?RADIANCE\n# Serika PhotoEdit\nFORMAT=32-bit_rle_rgbe\n\n-Y " +
            QByteArray::number(image.height()) + " +X " + QByteArray::number(image.width()) + "\n");
    QByteArray row(image.width() * 4, Qt::Uninitialized);
    for (int y = 0; y < image.height(); ++y) {
        auto p = reinterpret_cast<const float *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            float r = qMax(0.0f, p[x * 4]), g = qMax(0.0f, p[x * 4 + 1]), b = qMax(0.0f, p[x * 4 + 2]);
            float v = qMax(r, qMax(g, b));
            int exponent = 0;
            if (v < 1e-32f || !std::isfinite(v)) {
                for (int c = 0; c < 4; ++c)
                    row[x * 4 + c] = 0;
            } else {
                float scale = std::frexp(v, &exponent) * 256.0f / v;
                row[x * 4] = char(qBound(0, int(r * scale), 255));
                row[x * 4 + 1] = char(qBound(0, int(g * scale), 255));
                row[x * 4 + 2] = char(qBound(0, int(b * scale), 255));
                row[x * 4 + 3] = char(qBound(0, exponent + 128, 255));
            }
        }
        QByteArray encoded;
        if (image.width() >= 8 && image.width() <= 32767) {
            encoded.append(char(2));
            encoded.append(char(2));
            encoded.append(char(image.width() >> 8));
            encoded.append(char(image.width() & 255));
            for (int c = 0; c < 4; ++c) {
                int x = 0;
                while (x < image.width()) {
                    int run = 1;
                    while (run < 127 && x + run < image.width() && row[(x + run) * 4 + c] == row[x * 4 + c])
                        ++run;
                    if (run >= 4) {
                        encoded.append(char(128 + run));
                        encoded.append(row[x * 4 + c]);
                        x += run;
                    } else {
                        const int start = x;
                        ++x;
                        while (x < image.width() && x - start < 128) {
                            int next = 1;
                            while (next < 4 && x + next < image.width() &&
                                   row[(x + next) * 4 + c] == row[x * 4 + c])
                                ++next;
                            if (next >= 4)
                                break;
                            ++x;
                        }
                        encoded.append(char(x - start));
                        for (int i = start; i < x; ++i)
                            encoded.append(row[i * 4 + c]);
                    }
                }
            }
        } else
            encoded = row;
        if (f.write(encoded) != encoded.size()) {
            if (error)
                *error = f.errorString();
            return false;
        }
    }
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}
} // namespace serika::io
