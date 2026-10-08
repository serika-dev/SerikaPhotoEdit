#include "io/FormatInternal.h"
#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <memory>
#ifdef SERIKA_HAVE_LIBRAW
#include <libraw/libraw.h>
#endif

namespace serika::io {
Document *readRaw(const QString &path, QString *error, QObject *parent) {
#ifdef SERIKA_HAVE_LIBRAW
    LibRaw raw;
    raw.imgdata.params.output_bps = 16;
    raw.imgdata.params.output_color = 1;
    raw.imgdata.params.use_camera_wb = 1;
    raw.imgdata.params.no_auto_bright = 1;
    raw.imgdata.params.gamm[0] = 1.0 / 2.4;
    raw.imgdata.params.gamm[1] = 12.92;
    int status;
#ifdef Q_OS_WIN
    status = raw.open_file(reinterpret_cast<const wchar_t *>(path.utf16()));
#else
    status = raw.open_file(QFile::encodeName(path).constData());
#endif
    auto fail = [&](int code) -> Document * {
        if (error)
            *error = QString::fromUtf8(libraw_strerror(code));
        return nullptr;
    };
    if (status != LIBRAW_SUCCESS)
        return fail(status);
    if ((status = raw.unpack()) != LIBRAW_SUCCESS)
        return fail(status);
    if ((status = raw.dcraw_process()) != LIBRAW_SUCCESS)
        return fail(status);
    libraw_processed_image_t *decoded = raw.dcraw_make_mem_image(&status);
    if (!decoded)
        return fail(status);
    std::unique_ptr<libraw_processed_image_t, decltype(&LibRaw::dcraw_clear_mem)> image(
        decoded, &LibRaw::dcraw_clear_mem);
    if (image->type != LIBRAW_IMAGE_BITMAP || image->bits != 16 || image->colors < 3 || image->colors > 4 ||
        !image->width || !image->height ||
        quint64(image->width) * image->height * image->colors * 2 > image->data_size) {
        if (error)
            *error = "LibRaw did not produce a valid 16-bit RGB bitmap.";
        return nullptr;
    }
    QImage pixels(image->width, image->height, QImage::Format_RGBA64);
    if (pixels.isNull()) {
        if (error)
            *error = "Insufficient memory for RAW pixels.";
        return nullptr;
    }
    const auto *samples = reinterpret_cast<const quint16 *>(image->data);
    for (int y = 0; y < pixels.height(); ++y) {
        auto *line = reinterpret_cast<QRgba64 *>(pixels.scanLine(y));
        for (int x = 0; x < pixels.width(); ++x) {
            const auto p = samples + (qsizetype(y) * pixels.width() + x) * image->colors;
            line[x] = QRgba64::fromRgba64(p[0], p[1], p[2], 65535);
        }
    }
    pixels.setColorSpace(QColorSpace(QColorSpace::SRgb));
    auto document = std::make_unique<Document>(parent);
    document->state.size = pixels.size();
    document->state.bitDepth = 16;
    document->state.iccProfile = pixels.colorSpace().iccProfile();
    Layer layer;
    layer.id = 1;
    layer.name = "RAW — " + QFileInfo(path).fileName();
    layer.pixels = TileImage::fromImage(pixels);
    document->state.layers.append(layer);
    document->state.metadata = {{"rawSource", QFileInfo(path).absoluteFilePath()},
                                {"rawPendingDevelop", true},
                                {"rawMake", QString::fromUtf8(raw.imgdata.idata.make)},
                                {"rawModel", QString::fromUtf8(raw.imgdata.idata.model)},
                                {"ISO", raw.imgdata.other.iso_speed},
                                {"shutter", raw.imgdata.other.shutter},
                                {"aperture", raw.imgdata.other.aperture},
                                {"focalLength", raw.imgdata.other.focal_len},
                                {"timestamp", QString::number(raw.imgdata.other.timestamp)},
                                {"rawDecoder", QString::fromLatin1(LibRaw::version())}};
    QJsonObject settings;
    QFile sidecar(QFileInfo(path).absolutePath() + "/" + QFileInfo(path).completeBaseName() + ".xmp");
    if (sidecar.open(QIODevice::ReadOnly)) {
        QXmlStreamReader xml(&sidecar);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == u"Description") {
                for (const auto &attr : xml.attributes()) {
                    bool ok = false;
                    double v = attr.value().toDouble(&ok);
                    if (ok)
                        settings[attr.name().toString()] = v;
                }
            }
        }
        if (xml.hasError())
            document->importReport.append("The XMP sidecar could not be parsed: " + xml.errorString());
    }
    document->state.metadata["develop"] = settings;
    document->title = QFileInfo(path).completeBaseName();
    document->filePath = path;
    document->touch();
    document->markSaved();
    return document.release();
#else
    Q_UNUSED(path);
    Q_UNUSED(parent);
    if (error)
        *error = "RAW support requires the dynamically linked LibRaw library. Install LibRaw and rebuild "
                 "with SERIKA_HAVE_LIBRAW.";
    return nullptr;
#endif
}
} // namespace serika::io
