#include "document/Document.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QPainter>
#include <QSaveFile>
#include <QTextStream>
#include <algorithm>
#include <vector>

using namespace serika;
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("SerikaPerformanceBench");
    QCommandLineParser parser;
    parser.setApplicationDescription("Measure the native CPU compositor and tiled history on a 4000 x 3000 "
                                     "document with ten raster layers.");
    parser.addHelpOption();
    parser.addOption(QCommandLineOption("output", "Write the measured results as JSON.", "path"));
    parser.addPositionalArgument("output-path", "JSON output path, alternatively use --output.");
    parser.process(app);
    QString path = parser.value("output");
    if (path.isEmpty() && !parser.positionalArguments().isEmpty())
        path = parser.positionalArguments().first();
    if (path.isEmpty()) {
        QTextStream(stderr) << "Supply an output path or --output path.\n";
        return 2;
    }
    constexpr int width = 4000, height = 3000, layerCount = 10, cacheSamples = 120, zoomSamples = 30;
    Document document;
    document.state.size = {width, height};
    document.state.bitDepth = 8;
    document.state.layers.clear();
    QElapsedTimer timer;
    timer.start();
    quint64 allocatedTiles = 0, rasterBytes = 0;
    for (int i = 0; i < layerCount; ++i) {
        Layer layer;
        layer.id = i + 1;
        layer.name = QString("Benchmark layer %1").arg(i + 1);
        layer.pixels.size = {width, height};
        const QColor color =
            QColor::fromHsv((i * 37) % 360, 60 + i * 12, 130 + i * 7, i == 0 ? 255 : 40 + i * 7);
        layer.pixels.paint(QRect(0, 0, width, height),
                           [&](QPainter &p) { p.fillRect(QRect(0, 0, width, height), color); });
        allocatedTiles += layer.pixels.tiles.size();
        for (const QImage &tile : layer.pixels.tiles)
            rasterBytes += tile.sizeInBytes();
        document.state.layers.append(layer);
    }
    document.state.activeIndex = layerCount - 1;
    document.touch();
    document.markSaved();
    document.clearHistory();
    const double preparationMs = timer.nsecsElapsed() / 1e6;
    timer.restart();
    {
        const QImage image = document.composite();
        if (image.size() != QSize(width, height))
            return 3;
    }
    const double coldMs = timer.nsecsElapsed() / 1e6;
    quint64 checksum = 0;
    timer.restart();
    for (int i = 0; i < cacheSamples; ++i) {
        const QImage cached = document.composite();
        checksum ^= quint64(cached.cacheKey()) + i;
    }
    const double cacheUs = timer.nsecsElapsed() / 1000. / cacheSamples;
    QImage viewport(1920, 1080, QImage::Format_RGBA8888);
    std::vector<double> zoomTimings;
    zoomTimings.reserve(zoomSamples);
    for (int frame = 0; frame < zoomSamples; ++frame) {
        timer.restart();
        viewport.fill(QColor("#262626"));
        {
            QPainter painter(&viewport);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            const QImage cached = document.composite();
            const double zoom = .2 + (frame % 4) * .1;
            const QSizeF scaled(width * zoom, height * zoom);
            painter.drawImage(QRectF((viewport.width() - scaled.width()) / 2,
                                     (viewport.height() - scaled.height()) / 2, scaled.width(),
                                     scaled.height()),
                              cached);
        }
        zoomTimings.push_back(timer.nsecsElapsed() / 1e6);
        checksum ^= viewport.pixel(960, 540);
    }
    std::sort(zoomTimings.begin(), zoomTimings.end());
    document.beginTransaction("Benchmark brush dab");
    const QRect dab(2050, 1550, 24, 24);
    timer.restart();
    document.activeLayer()->pixels.paint(dab, [&](QPainter &p) { p.fillRect(dab, QColor(232, 137, 58)); });
    document.touch();
    const double paintMs = timer.nsecsElapsed() / 1e6;
    timer.restart();
    {
        const QImage image = document.composite();
        checksum ^= image.pixel(2060, 1560);
    }
    const double roiMs = timer.nsecsElapsed() / 1e6;
    document.endTransaction();
    timer.restart();
    document.undo();
    const double undoUs = timer.nsecsElapsed() / 1000.;
    timer.restart();
    {
        const QImage image = document.composite();
        checksum ^= image.pixel(2060, 1560);
    }
    const double undoRedrawMs = timer.nsecsElapsed() / 1e6;
    const QJsonObject result{
        {"document_width", width},
        {"document_height", height},
        {"layers", layerCount},
        {"bit_depth", 8},
        {"allocated_tiles", double(allocatedTiles)},
        {"raster_bytes", double(rasterBytes)},
        {"preparation_ms", preparationMs},
        {"cold_composite_ms", coldMs},
        {"cached_access_us", cacheUs},
        {"cached_access_samples", cacheSamples},
        {"zoom_frame_median_ms", zoomTimings[zoomSamples / 2]},
        {"zoom_frame_max_ms", zoomTimings.back()},
        {"zoom_frame_samples", zoomSamples},
        {"brush_paint_ms", paintMs},
        {"dirty_tile_roi_composite_ms", roiMs},
        {"brush_dab_with_roi_ms", paintMs + roiMs},
        {"undo_state_us", undoUs},
        {"undo_redraw_ms", undoRedrawMs},
        {"checksum", QString::number(checksum)},
        {"qt_version", QString(qVersion())},
        {"platform", QGuiApplication::platformName()},
        {"notes", "CPU measurements on the running host; zoom measures a 1920x1080 raster viewport with "
                  "smooth resampling. No latency thresholds are asserted."}};
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        QTextStream(stderr) << output.errorString() << '\n';
        return 4;
    }
    output.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
    if (!output.commit()) {
        QTextStream(stderr) << output.errorString() << '\n';
        return 5;
    }
    QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << '\n';
    return 0;
}
