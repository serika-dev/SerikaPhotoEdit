#include "FormatInternal.h"
#include <QBuffer>
#include <QColorSpace>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QSaveFile>
#include <QtMath>
#include <memory>
#ifdef SERIKA_HAVE_QTPDF
#include <QPdfDocument>
#endif

namespace serika {
namespace {
const QStringList rawSuffixes = {"dng", "cr2", "cr3", "nef", "nrw", "arw", "srf", "sr2", "raf", "orf", "rw2",
                                 "pef", "srw", "rwl", "3fr", "fff", "iiq", "erf", "mef", "mos", "x3f"};
QStringList plugins(bool writing) {
    QStringList result;
    const auto formats =
        writing ? QImageWriter::supportedImageFormats() : QImageReader::supportedImageFormats();
    for (const auto &f : formats)
        result.append(QString::fromLatin1(f));
    result.removeDuplicates();
    result.sort();
    return result;
}
Document *imageDocument(const QImage &image, const QString &path, QObject *parent) {
    if (image.isNull())
        return nullptr;
    auto d = std::make_unique<Document>(parent);
    d->state.size = image.size();
    d->state.bitDepth =
        image.format() == QImage::Format_RGBA32FPx4 || image.format() == QImage::Format_RGBX32FPx4 ? 32
        : image.depth() > 32 || image.format() == QImage::Format_Grayscale16                       ? 16
                                                                                                   : 8;
    d->state.iccProfile = image.colorSpace().iccProfile();
    d->state.resolution = image.dotsPerMeterX() > 0 ? image.dotsPerMeterX() * 0.0254 : 72;
    Layer l;
    l.id = 1;
    l.name = "Background";
    l.pixels = TileImage::fromImage(image);
    d->state.layers.append(l);
    d->title = QFileInfo(path).completeBaseName();
    d->filePath = path;
    d->markSaved();
    return d.release();
}
} // namespace
bool FormatIO::isRaw(const QString &path) { return rawSuffixes.contains(QFileInfo(path).suffix().toLower()); }
bool FormatIO::rawAvailable() {
#ifdef SERIKA_HAVE_LIBRAW
    return true;
#else
    return false;
#endif
}
QStringList FormatIO::readableFormats() {
    auto list = plugins(false);
    list << "spe" << "speb" << "psd" << "psb" << "tga" << "hdr";
#ifdef SERIKA_HAVE_QTPDF
    list << "pdf";
#endif
    if (rawAvailable())
        list << rawSuffixes;
    list.removeDuplicates();
    list.sort();
    return list;
}
QStringList FormatIO::writableFormats() {
    auto list = plugins(true);
    list << "spe" << "speb" << "psd" << "psb" << "tga" << "hdr";
    list.removeDuplicates();
    list.sort();
    return list;
}
QString FormatIO::openFilter() {
    QStringList patterns;
    for (const auto &f : readableFormats())
        patterns << "*." + f;
    return "All supported images (" + patterns.join(' ') +
           ");;Serika documents (*.spe *.speb);;Layered documents (*.psd *.psb);;All files (*)";
}
QString FormatIO::saveFilter() {
    QStringList result = {"Serika document (*.spe)", "Serika large document (*.speb)", "Layered PSD (*.psd)",
                          "Large layered PSB (*.psb)"};
    for (const auto &s : writableFormats())
        if (s != "spe" && s != "speb" && s != "psd" && s != "psb")
            result << s.toUpper() + " image (*." + s + ")";
    return result.join(";;");
}
Document *FormatIO::open(const QString &path, QString *error, QObject *parent) {
    if (error)
        error->clear();
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "pdf")
        return openPdf(path, 150, error, parent);
    if (suffix == "spe" || suffix == "speb")
        return openNative(path, error, parent);
    if (suffix == "psd" || suffix == "psb")
        return openPsd(path, error, parent);
    if (isRaw(path))
        return io::readRaw(path, error, parent);
    if (suffix == "tga")
        return imageDocument(io::readTga(path, error), path, parent);
    if (suffix == "hdr" || suffix == "pic")
        return imageDocument(io::readHdr(path, error), path, parent);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        if (error)
            *error = reader.errorString();
        return nullptr;
    }
    auto doc = std::unique_ptr<Document>(imageDocument(image, path, parent));
    int frame = 1;
    const bool animation = reader.supportsAnimation();
    QJsonArray delays{reader.nextImageDelay()};
    doc->state.layers[0].parameters["frameDelayMs"] = reader.nextImageDelay();
    quint64 allocated = image.sizeInBytes();
    // Animated image handlers advance during read(); still-image page handlers
    // advance through jumpToNextImage(). Mixing those contracts skips GIF frames.
    while (frame < 10000 && (animation ? reader.canRead() : reader.jumpToNextImage())) {
        QImage page = reader.read();
        if (page.isNull()) {
            doc->importReport.append("A later frame could not be read: " + reader.errorString());
            break;
        }
        allocated += page.sizeInBytes();
        if (allocated > 1024ull * 1024 * 1024) {
            doc->importReport.append("Later pages/frames exceed the safe combined pixel allocation limit.");
            break;
        }
        Layer l;
        l.id = ++frame;
        l.name = "Frame / page " + QString::number(frame);
        l.pixels = TileImage::fromImage(page);
        l.parameters["frameDelayMs"] = reader.nextImageDelay();
        delays.append(reader.nextImageDelay());
        doc->state.layers.append(l);
        doc->state.size = doc->state.size.expandedTo(page.size());
    }
    if (frame > 1) {
        doc->state.metadata["frameCount"] = frame;
        doc->state.metadata["animationDelayMs"] = reader.nextImageDelay();
        doc->state.metadata["frameDelaysMs"] = delays;
        doc->state.activeIndex = frame - 1;
        doc->importReport.append(QString::number(frame) + " frames/pages imported as layers.");
    }
    if (suffix == "svg" || suffix == "svgz")
        doc->importReport.append("SVG imported as pixels; paths remain available in the source SVG.");
    return doc.release();
}
Document *FormatIO::openPdf(const QString &path, qreal dpi, QString *error, QObject *parent) {
#ifdef SERIKA_HAVE_QTPDF
    if (error)
        error->clear();
    if (!qIsFinite(dpi) || dpi < 1 || dpi > 2400) {
        if (error)
            *error = "PDF resolution must be between 1 and 2400 dpi.";
        return nullptr;
    }
    QPdfDocument pdf;
    const auto status = pdf.load(path);
    if (status != QPdfDocument::Error::None) {
        if (error)
            *error = "Could not open PDF (renderer error " + QString::number(int(status)) + ").";
        return nullptr;
    }
    if (pdf.pageCount() <= 0 || pdf.pageCount() > 10000) {
        if (error)
            *error = "PDF has no pages or exceeds the page limit.";
        return nullptr;
    }
    auto doc = std::make_unique<Document>(parent);
    doc->state.size = QSize();
    doc->state.resolution = dpi;
    doc->state.bitDepth = 8;
    doc->state.iccProfile = QColorSpace(QColorSpace::SRgb).iccProfile();
    quint64 allocated = 0;
    for (int i = 0; i < pdf.pageCount(); ++i) {
        const auto points = pdf.pagePointSize(i);
        const QSize pixels(qCeil(points.width() * dpi / 72.0), qCeil(points.height() * dpi / 72.0));
        allocated += quint64(qMax(0, pixels.width())) * qMax(0, pixels.height()) * 4;
        if (pixels.isEmpty() || pixels.width() > 300000 || pixels.height() > 300000 ||
            allocated > 1024ull * 1024 * 1024) {
            if (error)
                *error = "PDF pages exceed the safe pixel allocation limit at this resolution.";
            return nullptr;
        }
        const QImage rendered = pdf.render(i, pixels);
        if (rendered.isNull()) {
            if (error)
                *error = "Could not render PDF page " + QString::number(i + 1) + ".";
            return nullptr;
        }
        Layer layer;
        layer.id = i + 1;
        layer.name = "Page " + QString::number(i + 1);
        layer.pixels.setImage(rendered);
        doc->state.layers.append(layer);
        doc->state.size = doc->state.size.expandedTo(pixels);
    }
    doc->state.activeIndex = doc->state.layers.size() - 1;
    doc->state.metadata["importDpi"] = dpi;
    doc->state.metadata["pageCount"] = pdf.pageCount();
    doc->state.metadata["pdfSource"] = QFileInfo(path).absoluteFilePath();
    doc->title = QFileInfo(path).completeBaseName();
    doc->filePath = path;
    doc->importReport.append("PDF pages rasterized at " + QString::number(dpi) +
                             " dpi and imported as layers.");
    doc->touch();
    doc->markSaved();
    return doc.release();
#else
    Q_UNUSED(path);
    Q_UNUSED(dpi);
    Q_UNUSED(parent);
    if (error)
        *error = "PDF import requires the dynamically linked Qt PDF module.";
    return nullptr;
#endif
}
bool FormatIO::save(Document *doc, const QString &path, QString *error, int quality) {
    if (error)
        error->clear();
    if (!doc) {
        if (error)
            *error = "No document to save.";
        return false;
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "spe" || suffix == "speb")
        return saveNative(doc, path, error);
    if (suffix == "psd" || suffix == "psb") {
        for (const auto &l : doc->state.layers)
            if (l.kind != LayerKind::Pixel && l.kind != LayerKind::Group && l.kind != LayerKind::Artboard)
                doc->importReport.append("PSD export: “" + l.name +
                                         "” uses raster compatibility pixels; Serika retains editable data "
                                         "in a private tagged block.");
        return savePsd(doc, path, error);
    }
    QImage image = doc->composite();
    if (!doc->state.iccProfile.isEmpty())
        image.setColorSpace(QColorSpace::fromIccProfile(doc->state.iccProfile));
    image.setDotsPerMeterX(qRound(doc->state.resolution / 0.0254));
    image.setDotsPerMeterY(qRound(doc->state.resolution / 0.0254));
    if (suffix == "tga")
        return io::writeTga(image, path, error);
    if (suffix == "hdr")
        return io::writeHdr(image, path, error);
    QSaveFile out(path);
    out.setDirectWriteFallback(false);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error)
            *error = out.errorString();
        return false;
    }
    QByteArray format = suffix.toLatin1();
    if (format == "jpg")
        format = "jpeg";
    if (format == "tif")
        format = "tiff";
    QImageWriter writer(&out, format);
    writer.setQuality(qBound(0, quality, 100));
    if (format == "jpeg")
        writer.setProgressiveScanWrite(true);
    if (!writer.write(image)) {
        if (error)
            *error = writer.errorString();
        return false;
    }
    if (!out.commit()) {
        if (error)
            *error = out.errorString();
        return false;
    }
    return true;
}
bool FormatIO::saveNative(const Document *d, const QString &p, QString *e) {
    return io::writeNative(d, p, e);
}
Document *FormatIO::openNative(const QString &p, QString *e, QObject *parent) {
    return io::readNative(p, e, parent);
}
bool FormatIO::savePsd(const Document *d, const QString &p, QString *e) { return io::writePsd(d, p, e); }
Document *FormatIO::openPsd(const QString &p, QString *e, QObject *parent) {
    return io::readPsd(p, e, parent);
}
} // namespace serika
