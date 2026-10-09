#include "compositor/GpuProcessor.h"
#include "document/Document.h"
#include "io/FormatIO.h"
#include <QColorSpace>
#include <QtTest>
#include <cmath>
#include <limits>
#include <memory>
#include <thread>

using namespace serika;
class GpuTests final : public QObject {
    Q_OBJECT
    bool m_available = false;
    QString m_unavailableReason;
    static QImage image(int depth) {
        QImage source(41, 23, QImage::Format_RGBA32FPx4);
        for (int y = 0; y < source.height(); ++y) {
            auto *row = reinterpret_cast<float *>(source.scanLine(y));
            for (int x = 0; x < source.width(); ++x) {
                row[4 * x] = float((x * 13 + y * 7) % 101) / 100;
                row[4 * x + 1] = float((x * 17 + y * 11) % 97) / 96;
                row[4 * x + 2] = float((x * 3 + y * 19) % 89) / 88;
                row[4 * x + 3] = float((x * 3 + y * 11) % 100) / 99;
            }
        }
        source.setColorSpace(QColorSpace(QColorSpace::SRgb));
        source.setDotsPerMeterX(4000);
        source.setDotsPerMeterY(4500);
        source.setDevicePixelRatio(1.25);
        return source.convertToFormat(depth == 8    ? QImage::Format_RGBA8888
                                      : depth == 16 ? QImage::Format_RGBA64
                                                    : QImage::Format_RGBA32FPx4);
    }
    static double maximumError(const QImage &a, const QImage &b) {
        if (a.size() != b.size() || a.isNull() || b.isNull())
            return std::numeric_limits<double>::infinity();
        // Compare straight native channels; Qt integer-to-float conversion itself rounds low-alpha RGB.
        if (a.format() == b.format() && a.format() == QImage::Format_RGBA8888) {
            int maximum = 0;
            for (int y = 0; y < a.height(); ++y)
                for (int x = 0; x < a.width() * 4; ++x)
                    maximum =
                        std::max(maximum, std::abs(int(a.constScanLine(y)[x]) - int(b.constScanLine(y)[x])));
            return maximum / 255.;
        }
        if (a.format() == b.format() && a.format() == QImage::Format_RGBA64) {
            int maximum = 0;
            for (int y = 0; y < a.height(); ++y) {
                const auto *p = reinterpret_cast<const quint16 *>(a.constScanLine(y));
                const auto *q = reinterpret_cast<const quint16 *>(b.constScanLine(y));
                for (int x = 0; x < a.width() * 4; ++x)
                    maximum = std::max(maximum, std::abs(int(p[x]) - int(q[x])));
            }
            return maximum / 65535.;
        }
        const QImage first = a.convertToFormat(QImage::Format_RGBA32FPx4);
        const QImage second = b.convertToFormat(QImage::Format_RGBA32FPx4);
        double error = 0;
        for (int y = 0; y < first.height(); ++y) {
            const auto *p = reinterpret_cast<const float *>(first.constScanLine(y));
            const auto *q = reinterpret_cast<const float *>(second.constScanLine(y));
            for (int x = 0; x < first.width() * 4; ++x) {
                if (!std::isfinite(p[x]) || !std::isfinite(q[x]))
                    return std::numeric_limits<double>::infinity();
                error = std::max(error, double(std::abs(p[x] - q[x])));
            }
        }
        return error;
    }
    static double tolerance(int depth) {
        return depth == 8 ? 1.01 / 255 : depth == 16 ? 3.1 / 65535 : 0.00006;
    }
  private slots:
    void initTestCase() {
        auto &processor = GpuProcessor::instance();
        processor.setEnabled(false);
        const auto report = processor.diagnostics();
        qInfo().noquote() << report.summary();
        m_available = report.available;
        m_unavailableReason = report.reason;
        if (qEnvironmentVariableIntValue("SERIKA_REQUIRE_GPU"))
            QVERIFY2(m_available, qPrintable(report.summary()));
        if (m_available) {
            QVERIFY(report.compiled);
            QVERIFY(!report.softwareRenderer);
            QVERIFY(!report.vendor.isEmpty());
            QVERIFY(!report.renderer.isEmpty());
            QVERIFY(!report.version.isEmpty());
            QVERIFY(report.maximumTextureSize > 0);
        }
    }
    void cleanup() { GpuProcessor::instance().setEnabled(false); }
    void cleanupTestCase() { GpuProcessor::instance().shutdown(); }
    void disabledBackendDefersToCpu() {
        auto &processor = GpuProcessor::instance();
        processor.setEnabled(false);
        const auto result = processor.processAdjustment(image(8), "Invert");
        QVERIFY(!result.usedGpu);
        QVERIFY(result.image.isNull());
        QVERIFY(result.reason.contains("disabled"));
        const auto report = processor.diagnostics(false);
        QCOMPARE(report.lastBackend, QString("CPU"));
        QCOMPARE(report.lastFallback, result.reason);
    }
    void unsupportedOperationsAndLargeBlurRetainCpuDispatch() {
        const QImage source = image(16);
        auto &processor = GpuProcessor::instance();
        processor.setEnabled(false);
        const QImage levels = applyAdjustment(source, "Levels", {{"gamma", 1.4}});
        const QImage largeBlur = applyFilter(source, "Gaussian Blur", {{"radius", 20}});
        processor.setEnabled(true);
        auto result = processor.processAdjustment(source, "Levels", {{"gamma", 1.4}});
        QVERIFY(!result.usedGpu);
        QVERIFY(result.image.isNull());
        QCOMPARE(applyAdjustment(source, "Levels", {{"gamma", 1.4}}), levels);
        result = processor.processFilter(source, "Gaussian Blur", {{"radius", 20}});
        QVERIFY(!result.usedGpu);
        QVERIFY(result.reason.contains("above 12"));
        QCOMPARE(applyFilter(source, "Gaussian Blur", {{"radius", 20}}), largeBlur);
        QVERIFY(!processor.processFilter(source, "Median", {{"radius", 1}}).usedGpu);
    }
    void zeroRadiusBlurIsAnExactCpuNoOp() {
        auto &processor = GpuProcessor::instance();
        const QImage source = image(32);
        processor.setEnabled(false);
        const QImage expected = applyFilter(source, "Gaussian Blur", {{"radius", 0}});
        processor.setEnabled(true);
        QVERIFY(!processor.processFilter(source, "Gaussian Blur", {{"radius", 0}}).usedGpu);
        QCOMPARE(applyFilter(source, "Gaussian Blur", {{"radius", 0}}), expected);
    }
    void workerThreadDefersWithoutCreatingOrUsingGuiContext() {
        auto &processor = GpuProcessor::instance();
        processor.setEnabled(true);
        GpuProcessingResult result;
        GpuDiagnostics report;
        const QImage source = image(8);
        std::thread worker([&] {
            result = processor.processAdjustment(source, "Invert");
            report = processor.diagnostics();
        });
        worker.join();
        QVERIFY(!result.usedGpu);
        QVERIFY(result.image.isNull());
        QVERIFY(result.reason.contains("GUI thread"));
        QVERIFY(!report.available);
        QVERIFY(report.reason.contains("GUI thread"));
    }
    void unsupportedPlatformReportsFallbackHonestly() {
        auto &processor = GpuProcessor::instance();
        if (m_available)
            QSKIP("A hardware OpenGL context is available; fallback is covered by separate disabled/worker "
                  "tests.");
        processor.setEnabled(true);
        const auto result = processor.processAdjustment(image(16), "Invert");
        QVERIFY(!result.usedGpu);
        QVERIFY(result.image.isNull());
        QVERIFY(!result.reason.isEmpty());
        const auto report = processor.diagnostics(false);
        QVERIFY(!report.available);
        QCOMPARE(report.lastBackend, QString("CPU"));
        QCOMPARE(report.completedOperations, quint64(0));
    }
    void shaderAdjustmentsMatchCpu_data() {
        QTest::addColumn<int>("depth");
        QTest::addColumn<QString>("operation");
        QTest::addColumn<QJsonObject>("parameters");
        const QList<QPair<QString, QJsonObject>> adjustments{
            {"Invert", {}},
            {"Brightness/Contrast", {{"brightness", 17}, {"contrast", 31}}},
            {"Brightness/Contrast", {{"brightness", -9}, {"contrast", -45}}},
            {"Exposure", {{"exposure", 1.3}, {"offset", .014}, {"gamma", 1.17}}},
            {"Hue/Saturation", {{"hue", -128}, {"saturation", 35}, {"lightness", -12}}},
            {"Hue/Saturation", {{"colorize", true}, {"hue", 270}, {"saturation", 62}, {"lightness", 18}}},
            {"Vibrance", {{"amount", 43}, {"saturation", -13}}},
            {"Desaturate", {}}};
        for (int depth : {8, 16, 32})
            for (int i = 0; i < adjustments.size(); ++i)
                QTest::newRow(qPrintable(QString("depth%1-operation%2").arg(depth).arg(i)))
                    << depth << adjustments[i].first << adjustments[i].second;
    }
    void shaderAdjustmentsMatchCpu() {
        if (!m_available)
            QSKIP(qPrintable(m_unavailableReason));
        QFETCH(int, depth);
        QFETCH(QString, operation);
        QFETCH(QJsonObject, parameters);
        auto &processor = GpuProcessor::instance();
        const QImage source = image(depth);
        processor.setEnabled(false);
        const QImage cpu = applyAdjustment(source, operation, parameters);
        const quint64 before = processor.diagnostics(false).completedOperations;
        processor.setEnabled(true);
        const auto gpu = processor.processAdjustment(source, operation, parameters);
        QVERIFY2(gpu.usedGpu, qPrintable(gpu.reason));
        QCOMPARE(gpu.image.format(), cpu.format());
        const double error = maximumError(cpu, gpu.image);
        QVERIFY2(error <= tolerance(depth),
                 qPrintable(QString("Maximum channel error: %1").arg(error, 0, 'g', 12)));
        QCOMPARE(gpu.image.colorSpace(), source.colorSpace());
        QCOMPARE(gpu.image.dotsPerMeterX(), source.dotsPerMeterX());
        QCOMPARE(gpu.image.dotsPerMeterY(), source.dotsPerMeterY());
        QCOMPARE(gpu.image.devicePixelRatio(), source.devicePixelRatio());
        const auto report = processor.diagnostics(false);
        QCOMPARE(report.completedOperations, before + 1);
        QCOMPARE(report.lastBackend, QString("OpenGL shader"));
        QVERIFY(report.lastFallback.isEmpty());
    }
    void gaussianShaderMatchesPremultipliedCpuKernel_data() {
        QTest::addColumn<int>("depth");
        QTest::addColumn<double>("radius");
        for (int depth : {8, 16, 32})
            for (double radius : {.15, 2.3, 12.})
                QTest::newRow(qPrintable(QString("depth%1-radius%2").arg(depth).arg(radius)))
                    << depth << radius;
    }
    void gaussianShaderMatchesPremultipliedCpuKernel() {
        if (!m_available)
            QSKIP(qPrintable(m_unavailableReason));
        QFETCH(int, depth);
        QFETCH(double, radius);
        auto &processor = GpuProcessor::instance();
        const QImage source = image(depth);
        processor.setEnabled(false);
        const QImage cpu = applyFilter(source, "Gaussian Blur", {{"radius", radius}});
        processor.setEnabled(true);
        const auto gpu = processor.processFilter(source, "Gaussian Blur", {{"radius", radius}});
        QVERIFY2(gpu.usedGpu, qPrintable(gpu.reason));
        QCOMPARE(gpu.image.format(), cpu.format());
        const double error = maximumError(cpu, gpu.image);
        QVERIFY2(error <= tolerance(depth),
                 qPrintable(QString("Maximum channel error: %1").arg(error, 0, 'g', 12)));
    }
    void nativeSixteenBitPrecisionSurvivesShaderReadback() {
        if (!m_available)
            QSKIP(qPrintable(m_unavailableReason));
        QImage source(8, 1, QImage::Format_RGBA64);
        auto *row = reinterpret_cast<QRgba64 *>(source.scanLine(0));
        for (int x = 0; x < source.width(); ++x)
            row[x] = QRgba64::fromRgba64(10000 + 13 * x, 20003, 30007, 62000);
        auto &processor = GpuProcessor::instance();
        processor.setEnabled(true);
        const auto result = processor.processAdjustment(source, "Invert");
        QVERIFY2(result.usedGpu, qPrintable(result.reason));
        QCOMPARE(result.image.format(), QImage::Format_RGBA64);
        const auto *pixels = reinterpret_cast<const QRgba64 *>(result.image.constScanLine(0));
        for (int x = 0; x < source.width(); ++x) {
            QVERIFY(std::abs(int(pixels[x].red()) - (55535 - 13 * x)) <= 1);
            QCOMPARE(pixels[x].alpha(), quint16(62000));
        }
        QVERIFY(pixels[0].red() != pixels[1].red());
    }
    void veryTransparentPixelsRetainStraightColorPrecision() {
        if (!m_available)
            QSKIP(qPrintable(m_unavailableReason));
        auto &processor = GpuProcessor::instance();
        for (auto format : {QImage::Format_RGBA8888, QImage::Format_RGBA64}) {
            QImage source(1, 1, format);
            if (format == QImage::Format_RGBA64)
                reinterpret_cast<QRgba64 *>(source.bits())[0] = QRgba64::fromRgba64(12345, 44444, 55555, 100);
            else {
                auto *pixel = source.bits();
                pixel[0] = 123;
                pixel[1] = 201;
                pixel[2] = 33;
                pixel[3] = 1;
            }
            const QJsonObject parameters{{"brightness", 17}, {"contrast", -23}};
            processor.setEnabled(false);
            const QImage cpu = applyAdjustment(source, "Brightness/Contrast", parameters);
            processor.setEnabled(true);
            const auto gpu = processor.processAdjustment(source, "Brightness/Contrast", parameters);
            QVERIFY2(gpu.usedGpu, qPrintable(gpu.reason));
            QVERIFY(maximumError(cpu, gpu.image) <=
                    (format == QImage::Format_RGBA64 ? 1.1 / 65535 : 1.1 / 255));
        }
    }
    void floatExposureRetainsHdrAndAlpha() {
        if (!m_available)
            QSKIP(qPrintable(m_unavailableReason));
        QImage source(1, 1, QImage::Format_RGBA32FPx4);
        auto *pixel = reinterpret_cast<float *>(source.scanLine(0));
        pixel[0] = 1.8f;
        pixel[1] = -.03f;
        pixel[2] = 2.3f;
        pixel[3] = .45f;
        auto &processor = GpuProcessor::instance();
        processor.setEnabled(false);
        const QJsonObject parameters{{"exposure", 1.1}, {"offset", .02}};
        const QImage cpu = applyAdjustment(source, "Exposure", parameters);
        processor.setEnabled(true);
        const auto gpu = processor.processAdjustment(source, "Exposure", parameters);
        QVERIFY2(gpu.usedGpu, qPrintable(gpu.reason));
        const auto *output = reinterpret_cast<const float *>(gpu.image.constScanLine(0));
        QVERIFY(output[0] > 1);
        QVERIFY(output[2] > 1);
        QCOMPARE(output[3], .45f);
        QVERIFY(maximumError(cpu, gpu.image) <= .0002);
    }
    void dispatcherRunsShaderAndDocumentUndoPreservesOriginalPixels() {
        if (!m_available)
            QSKIP(qPrintable(m_unavailableReason));
        auto &processor = GpuProcessor::instance();
        std::unique_ptr<Document> document(Document::create({41, 23}, Qt::transparent, 16));
        document->activeLayer()->pixels = TileImage::fromImage(image(16));
        const QImage original = document->activeLayer()->pixels.image();
        document->touch();
        document->clearHistory();
        processor.setEnabled(false);
        const QImage expected = applyAdjustment(original, "Invert", {});
        const quint64 before = processor.diagnostics(false).completedOperations;
        processor.setEnabled(true);
        document->mutate("GPU invert", [&] {
            auto *layer = document->activeLayer();
            layer->pixels = TileImage::fromImage(applyAdjustment(layer->pixels.image(), "Invert", {}));
        });
        QVERIFY(processor.diagnostics(false).completedOperations > before);
        QVERIFY(maximumError(document->activeLayer()->pixels.image(), expected) <= tolerance(16));
        QCOMPARE(document->historyNames().size(), 1);
        document->undo();
        QCOMPARE(document->activeLayer()->pixels.image(), original);
        document->redo();
        QVERIFY(maximumError(document->activeLayer()->pixels.image(), expected) <= tolerance(16));
    }
    void driverTextureLimitFallsBack() {
        if (!m_available)
            QSKIP(qPrintable(m_unavailableReason));
        auto &processor = GpuProcessor::instance();
        const int maximum = processor.diagnostics(false).maximumTextureSize;
        QImage source(maximum + 1, 1, QImage::Format_RGBA8888);
        QVERIFY(!source.isNull());
        source.fill(Qt::red);
        processor.setEnabled(true);
        const auto result = processor.processAdjustment(source, "Invert");
        QVERIFY(!result.usedGpu);
        QVERIFY(result.reason.contains("texture limit"));
    }
};
QTEST_MAIN(GpuTests)
#include "gpu_tests.moc"
