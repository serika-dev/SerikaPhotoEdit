#include "document/Document.h"
#include "io/FormatIO.h"
#include "tools/LocalAlgorithms.h"
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

using namespace serika;
namespace {
quint16 value16(const QImage &image, int x, int y) {
    return reinterpret_cast<const quint16 *>(image.constScanLine(y))[x];
}
Layer pixelLayer(quint64 id, QSize size, int depth = 16) {
    Layer layer;
    layer.id = id;
    QImage image(size, depth == 16 ? QImage::Format_RGBA64 : QImage::Format_RGBA8888);
    image.fill(Qt::red);
    layer.pixels = TileImage::fromImage(image);
    return layer;
}
} // namespace
class MaskTests : public QObject {
    Q_OBJECT
  private slots:
    void precisionSafeSelectionCombinations() {
        std::unique_ptr<Document> document(Document::create({5, 3}, Qt::white, 16));
        QImage first = makeMask({5, 3}, 16);
        setMaskSample(first, 1, 1, 1 / 65535.);
        setMaskSample(first, 2, 1, 12345 / 65535.);
        document->setSelection(first);
        QCOMPARE(document->state.selection.format(), QImage::Format_Grayscale16);
        QCOMPARE(document->selectionBounds(), QRect(1, 1, 2, 1));
        QCOMPARE(value16(document->state.selection, 1, 1), quint16(1));
        QImage second = makeMask({5, 3}, 16);
        setMaskSample(second, 2, 1, 100 / 65535.);
        document->setSelection(second, "subtract");
        QCOMPARE(value16(document->state.selection, 2, 1), quint16(12245));
        document->invertSelection();
        QCOMPARE(value16(document->state.selection, 2, 1), quint16(53290));
        document->undo();
        QCOMPARE(value16(document->state.selection, 2, 1), quint16(12245));
    }
    void nativeMaskCreatedFromPreciseSelection() {
        std::unique_ptr<Document> document(Document::create({4, 2}, Qt::red, 16));
        QImage selection = makeMask({4, 2}, 16);
        setMaskSample(selection, 1, 0, 333 / 65535.);
        document->setSelection(selection);
        document->addMask();
        QCOMPARE(value16(document->activeLayer()->mask, 1, 0), quint16(333));
        const QImage composite = document->composite();
        QCOMPARE(reinterpret_cast<const QRgba64 *>(composite.constScanLine(0))[1].alpha(), quint16(333));
        document->invertMask();
        QCOMPARE(value16(document->activeLayer()->mask, 1, 0), quint16(65202));
    }
    void densityAndFeatherAffectRendering() {
        DocumentState state;
        state.size = {25, 3};
        state.bitDepth = 16;
        Layer layer = pixelLayer(1, state.size);
        layer.mask = makeMask(state.size, 16);
        for (int y = 0; y < 3; ++y)
            for (int x = 12; x < 25; ++x)
                setMaskSample(layer.mask, x, y, 1);
        layer.maskDensity = .5;
        state.layers = {layer};
        QVERIFY(std::abs(compositeDocument(state).pixelColor(0, 0).alphaF() - .5) < .001);
        state.layers[0].maskDensity = 1;
        state.layers[0].maskFeather = 2;
        const QImage result = compositeDocument(state);
        QVERIFY(result.pixelColor(11, 1).alphaF() > .1);
        QVERIFY(result.pixelColor(11, 1).alphaF() < .5);
        QVERIFY(result.pixelColor(12, 1).alphaF() > .5);
        QVERIFY(result.pixelColor(12, 1).alphaF() < .9);
    }
    void vectorAndRasterMasksCombine() {
        DocumentState state;
        state.size = {8, 4};
        state.bitDepth = 16;
        Layer layer = pixelLayer(1, state.size);
        layer.mask = makeMask(state.size, 16, .5);
        layer.vectorMask.addRect(2, 0, 3, 4);
        state.layers = {layer};
        QImage result = compositeDocument(state);
        QVERIFY(result.pixelColor(3, 1).alphaF() > .49);
        QCOMPARE(result.pixelColor(0, 1).alpha(), 0);
        state.layers[0].vectorMaskDensity = .5;
        result = compositeDocument(state);
        QVERIFY(std::abs(result.pixelColor(0, 1).alphaF() - .25) < .001);
        state.layers[0].vectorMaskEnabled = false;
        result = compositeDocument(state);
        QVERIFY(std::abs(result.pixelColor(0, 1).alphaF() - .5) < .001);
    }
    void unlinkedMasksRetainTheirOffset() {
        Layer layer = pixelLayer(1, {10, 2});
        layer.offset = {3, 0};
        layer.mask = makeMask({10, 2}, 16);
        setMaskSample(layer.mask, 2, 0, 1);
        layer.maskLinked = false;
        layer.maskOffset = {-3, 0};
        const QImage result = renderedLayerMask(layer, {15, 2}, 16);
        QCOMPARE(maskSample(result, 2, 0), 1.);
        QCOMPARE(maskSample(result, 5, 0), 0.);
    }
    void refineEdgesPreservesSixteenBitCoverage() {
        QImage mask = makeMask({15, 3}, 16);
        setMaskSample(mask, 7, 1, 10001 / 65535.);
        QImage grown = refineMask(mask, {{"shiftEdge", 2}});
        QCOMPARE(value16(grown, 5, 1), quint16(10001));
        QImage softened = refineMask(mask, {{"feather", 1}});
        QCOMPARE(softened.format(), QImage::Format_Grayscale16);
        QVERIFY(value16(softened, 6, 1) > 0);
        QVERIFY(value16(softened, 6, 1) % 257 != 0);
        QImage contrasted = refineMask(makeMask({1, 1}, 16, .75), {{"contrast", 50}});
        QCOMPARE(value16(contrasted, 0, 0), quint16(65535));
    }
    void sourceGuidedRadiusRespectsColorEdge() {
        QImage image(20, 4, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.fillRect(10, 0, 10, 4, Qt::black);
        painter.end();
        QImage mask = makeMask(image.size(), 16);
        for (int y = 0; y < 4; ++y)
            for (int x = 10; x < 20; ++x)
                setMaskSample(mask, x, y, 1);
        QImage refined = refineMask(mask, {{"radius", 3}, {"smartRadius", true}}, image);
        QVERIFY(maskSample(refined, 9, 2) < .01);
        QVERIFY(maskSample(refined, 10, 2) > .99);
    }
    void maskAppliesToAdjustmentAndPassThroughGroup() {
        DocumentState state;
        state.size = {4, 1};
        state.bitDepth = 16;
        Layer base = pixelLayer(1, state.size);
        Layer adjustment;
        adjustment.id = 2;
        adjustment.kind = LayerKind::Adjustment;
        adjustment.adjustment = "Invert";
        adjustment.mask = makeMask(state.size, 16, 0);
        adjustment.maskDensity = .5;
        state.layers = {base, adjustment};
        const QColor color = compositeDocument(state).pixelColor(1, 0);
        QVERIFY(std::abs(color.redF() - .5) < .001);
        QVERIFY(std::abs(color.greenF() - .5) < .001);
        Layer group;
        group.id = 3;
        group.kind = LayerKind::Group;
        group.blendMode = "Pass Through";
        group.vectorMask.addRect(0, 0, 2, 1);
        adjustment.parentId = group.id;
        state.layers = {base, group, adjustment};
        const QImage result = compositeDocument(state);
        QVERIFY(std::abs(result.pixelColor(0, 0).greenF() - .5) < .001);
        QCOMPARE(result.pixelColor(3, 0), QColor(Qt::red));
    }
    void smartFilterUpdatesFromRetainedSource() {
        std::unique_ptr<Document> document(Document::create({16, 8}, Qt::red, 16));
        const QImage original = document->activeLayer()->pixels.image();
        document->addSmartFilter("Invert");
        QCOMPARE(document->activeLayer()->kind, LayerKind::SmartObject);
        QCOMPARE(document->activeLayer()->pixels.image(), original);
        QCOMPARE(document->composite().pixelColor(0, 0), QColor(Qt::cyan));
        QJsonObject entry = document->activeLayer()->smartFilters[0].toObject();
        entry["enabled"] = false;
        QVERIFY(document->updateSmartFilter(0, entry));
        QCOMPARE(document->composite().pixelColor(0, 0), QColor(Qt::red));
        document->undo();
        QCOMPARE(document->composite().pixelColor(0, 0), QColor(Qt::cyan));
        QVERIFY(document->removeSmartFilter(0));
        QCOMPARE(document->composite().pixelColor(0, 0), QColor(Qt::red));
    }
    void smartFilterBlendingAndTransforms() {
        std::unique_ptr<Document> document(Document::create({8, 4}, Qt::red, 16));
        document->addSmartFilter("Invert");
        QJsonObject entry = document->activeLayer()->smartFilters[0].toObject();
        entry["opacity"] = .5;
        document->updateSmartFilter(0, entry);
        QVERIFY(std::abs(document->composite().pixelColor(0, 0).redF() - .5) < .001);
        document->mutate("Scale retained content", [&] {
            document->activeLayer()->parameters["contentTransform"] = QJsonArray{2, 0, 0, 0, 2, 0, 0, 0, 1};
        });
        QCOMPARE(document->layerImage(*document->activeLayer()).size(), QSize(16, 8));
        QCOMPARE(document->activeLayer()->pixels.size, QSize(8, 4));
    }
    void hdrExposureDoesNotClamp() {
        QImage image(1, 1, QImage::Format_RGBA32FPx4);
        auto *pixel = reinterpret_cast<float *>(image.scanLine(0));
        pixel[0] = 2.5f;
        pixel[1] = .5f;
        pixel[2] = .25f;
        pixel[3] = 1;
        QImage adjusted = applyAdjustment(image, "Exposure", {{"exposure", 1}, {"gamma", 1}});
        const auto *result = reinterpret_cast<const float *>(adjusted.constScanLine(0));
        QVERIFY(result[0] > 2.5f);
        QVERIFY(result[1] > .5f);
    }
    void floatMaskSelectionAndSavedChannelsRetainTinyCoverage() {
        std::unique_ptr<Document> document(Document::create({3, 2}, Qt::red, 32));
        QImage selection = makeMask({3, 2}, 32);
        setMaskSample(selection, 1, 0, 1e-8);
        document->setSelection(selection);
        QCOMPARE(document->state.selection.format(), QImage::Format_RGBA32FPx4);
        QVERIFY(std::abs(maskSample(document->state.selection, 1, 0) - 1e-8) < 1e-14);
        document->saveSelection("Float alpha");
        document->deselect();
        QVERIFY(document->loadSelection("Float alpha"));
        QVERIFY(std::abs(maskSample(document->state.selection, 1, 0) - 1e-8) < 1e-14);
        document->addMask();
        const QImage result = document->composite();
        QVERIFY(std::abs(reinterpret_cast<const float *>(result.constScanLine(0))[7] - 1e-8f) < 1e-14);
        QImage feathered = refineMask(selection, {{"feather", 1}});
        QCOMPARE(feathered.format(), QImage::Format_RGBA32FPx4);
        QVERIFY(maskSample(feathered, 0, 0) > 0);
        QVERIFY(maskSample(feathered, 0, 0) < 1e-8);
    }
    void savedChannelsAndLayerCompsAreUndoable() {
        std::unique_ptr<Document> document(Document::create({6, 4}, Qt::red, 16));
        QImage selection = makeMask({6, 4}, 16, 12345 / 65535.);
        document->setSelection(selection);
        document->saveSelection("Alpha 1");
        document->deselect();
        QVERIFY(document->loadSelection("Alpha 1"));
        QCOMPARE(value16(document->state.selection, 0, 0), quint16(12345));
        document->activeLayer()->offset = {2, 1};
        document->activeLayer()->opacity = .4;
        const int comp = document->captureLayerComp("Variant");
        document->mutate("Change appearance", [&] {
            document->activeLayer()->offset = {0, 0};
            document->activeLayer()->opacity = 1;
            document->activeLayer()->visible = false;
        });
        QVERIFY(document->applyLayerComp(comp));
        QCOMPARE(document->activeLayer()->offset, QPointF(2, 1));
        QCOMPARE(document->activeLayer()->opacity, .4);
        QVERIFY(document->activeLayer()->visible);
        document->undo();
        QVERIFY(!document->activeLayer()->visible);
        QVERIFY(document->deleteLayerComp(comp));
        QVERIFY(document->layerComps().isEmpty());
    }
    void historyBrushRestoresChosenSnapshotWithinCoverage() {
        std::unique_ptr<Document> document(Document::create({6, 4}, Qt::red, 16));
        const quint64 id = document->activeLayer()->id;
        document->mutate("Paint blue", [&] {
            QImage image(6, 4, QImage::Format_RGBA64);
            image.fill(Qt::blue);
            document->activeLayer()->pixels.setImage(image);
        });
        QImage coverage = makeMask({6, 4}, 16);
        setMaskSample(coverage, 2, 1, .5);
        QVERIFY(document->restoreHistoryPixels(id, coverage, -1));
        const QColor restored = document->composite().pixelColor(2, 1);
        QVERIFY(std::abs(restored.redF() - .5) < .001);
        QVERIFY(std::abs(restored.blueF() - .5) < .001);
        QCOMPARE(document->composite().pixelColor(0, 0), QColor(Qt::blue));
        document->undo();
        QCOMPARE(document->composite().pixelColor(2, 1), QColor(Qt::blue));
    }
    void cropRetainsHiddenPixelsAndTranslatesGroupsOnce() {
        std::unique_ptr<Document> document(Document::create({10, 6}, Qt::transparent, 16));
        Layer group;
        group.id = 10;
        group.kind = LayerKind::Group;
        group.blendMode = "Pass Through";
        group.offset = {2, 1};
        Layer child = pixelLayer(11, {4, 2});
        child.parentId = group.id;
        document->state.layers = {group, child};
        document->state.activeIndex = 1;
        document->touch();
        document->mutate("Crop and resize", [&] {
            document->crop({2, 1, 5, 3});
            document->resizeCanvas({8, 5});
        });
        QCOMPARE(document->historyNames().last(), QString("Crop and resize"));
        QCOMPARE(document->state.layers[0].offset, QPointF());
        QCOMPARE(document->state.layers[1].offset, QPointF());
        QCOMPARE(document->state.layers[1].pixels.size, QSize(4, 2));
        QCOMPARE(document->composite().pixelColor(0, 0), QColor(Qt::red));
        document->undo();
        QCOMPARE(document->state.size, QSize(10, 6));
        QCOMPARE(document->state.layers[0].offset, QPointF(2, 1));
    }
    void destructiveCropClearsOutsidePixels() {
        std::unique_ptr<Document> document(Document::create({10, 6}, Qt::red, 16));
        document->crop({3, 2, 4, 2}, true);
        const QImage retained = document->activeLayer()->pixels.image();
        QCOMPARE(retained.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(retained.pixelColor(3, 2), QColor(Qt::red));
        document->undo();
        QCOMPARE(document->activeLayer()->pixels.image().pixelColor(0, 0), QColor(Qt::red));
    }
    void subjectKeepsEnclosedRegionsAndFineContrastingStrands() {
        QImage image(96, 96, QImage::Format_RGBA64);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.fillRect(24, 24, 48, 48, Qt::red);
        painter.fillRect(38, 38, 20, 20, Qt::white);
        painter.fillRect(47, 10, 1, 16, Qt::red);
        painter.end();
        QImage mask = selectSubjectLocally(image);
        QCOMPARE(mask.format(), QImage::Format_Grayscale16);
        QVERIFY(maskSample(mask, 45, 45) > .9);
        QVERIFY(maskSample(mask, 47, 14) > .75);
        QVERIFY(maskSample(mask, 0, 0) < .01);
    }
    void focusAreaUsesSharpnessRatherThanSubjectColor() {
        QImage image(160, 80, QImage::Format_RGBA64);
        image.fill(QColor(128, 128, 128));
        QPainter painter(&image);
        for (int y = 0; y < 80; y += 2)
            for (int x = 0; x < 60; x += 2)
                painter.fillRect(x, y, 2, 2, ((x + y) / 2) % 2 ? Qt::white : Qt::black);
        painter.end();
        const QImage mask = selectFocusAreaLocally(image, {{"feather", 0}});
        QCOMPARE(mask.format(), QImage::Format_Grayscale16);
        QVERIFY(maskSample(mask, 20, 40) > .9);
        QVERIFY(maskSample(mask, 130, 40) < .01);
    }
    void nativeRoundtripRetainsMaskGraphAndComps() {
        QTemporaryDir directory;
        std::unique_ptr<Document> document(Document::create({10, 5}, Qt::red, 16));
        document->addMask();
        document->activeLayer()->maskDensity = .42;
        document->activeLayer()->maskFeather = 2;
        document->activeLayer()->vectorMask.addEllipse(1, 1, 6, 3);
        document->addSmartFilter("Invert");
        document->captureLayerComp("Original visibility");
        const QString path = directory.filePath("masked.spe");
        QString error;
        QVERIFY2(FormatIO::saveNative(document.get(), path, &error), qPrintable(error));
        std::unique_ptr<Document> restored(FormatIO::openNative(path, &error));
        QVERIFY2(restored != nullptr, qPrintable(error));
        QCOMPARE(restored->activeLayer()->maskDensity, .42);
        QCOMPARE(restored->activeLayer()->vectorMask, document->activeLayer()->vectorMask);
        QCOMPARE(restored->activeLayer()->smartFilters, document->activeLayer()->smartFilters);
        QCOMPARE(restored->layerComps(), document->layerComps());
        QCOMPARE(restored->composite(), document->composite());
    }
};
QTEST_MAIN(MaskTests)
#include "mask_tests.moc"
