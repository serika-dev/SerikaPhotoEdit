#include "document/TransformOperations.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/MaskRefineDialog.h"
#include "ui/dialogs/SmartFilterDialog.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <memory>
using namespace serika;
class WorkflowTests : public QObject {
    Q_OBJECT
    QTemporaryDir settings;
    static Document *small(MainWindow &window) {
        window.openDemo();
        auto *d = window.currentDocument();
        d->mutate("Test fixture", [&] {
            d->state.size = {32, 24};
            Layer layer;
            layer.id = 101;
            layer.name = "Pixels";
            QImage image(8, 6, QImage::Format_RGBA8888);
            image.fill(Qt::red);
            layer.pixels.setImage(image);
            layer.offset = {10, 8};
            d->state.layers = {layer};
            d->state.activeIndex = 0;
        });
        d->clearHistory();
        return d;
    }
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    }
    void linkedMasksRotateAtLayerOrigin() {
        std::unique_ptr<Document> d(Document::create({20, 20}, Qt::transparent, 16));
        auto *l = d->activeLayer();
        QImage source(4, 2, QImage::Format_RGBA64);
        source.fill(Qt::red);
        l->pixels.setImage(source);
        l->offset = {8, 7};
        l->mask = makeMask({4, 2}, 16);
        setMaskSample(l->mask, 0, 0, 1);
        setMaskSample(l->mask, 3, 1, 1. / 65535);
        l->vectorMask.addRect(QRectF(0, 0, 4, 2));
        d->touch();
        const auto before = d->composite();
        QString error;
        QVERIFY2(applyLayerTransform(d.get(), QTransform().rotate(90), &error), qPrintable(error));
        l = d->activeLayer();
        QCOMPARE(l->offset, QPointF(6, 7));
        QCOMPARE(l->mask.size(), QSize(2, 4));
        QVERIFY(maskSample(l->mask, 1, 0) > .99);
        QVERIFY(std::abs(maskSample(l->mask, 0, 3) - 1. / 65535) < 1e-8);
        QVERIFY(d->composite().pixelColor(7, 7).alphaF() > .99);
        d->undo();
        QCOMPARE(d->composite(), before);
    }
    void unlinkedMasksRemainAtDocumentPosition() {
        std::unique_ptr<Document> d(Document::create({20, 20}, Qt::red));
        auto *l = d->activeLayer();
        l->offset = {4, 5};
        l->mask = makeMask({20, 20}, 8, 1);
        l->maskLinked = false;
        l->maskOffset = {2, 3};
        l->vectorMask.addRect(QRectF(0, 0, 10, 10));
        l->vectorMaskLinked = false;
        l->vectorMaskOffset = {1, 2};
        QVERIFY(applyLayerTransform(d.get(), QTransform::fromTranslate(6, -2)));
        l = d->activeLayer();
        QCOMPARE(l->offset + l->maskOffset, QPointF(6, 8));
        QCOMPARE(l->offset + l->vectorMaskOffset, QPointF(5, 7));
    }
    void maskTargetTransformLeavesPixelsAndVectorUntouched() {
        std::unique_ptr<Document> d(Document::create({10, 10}, Qt::blue, 32));
        d->addMask();
        auto *l = d->activeLayer();
        const auto pixels = l->pixels.image();
        l->offset = {3, 2};
        setMaskSample(l->mask, 1, 1, 1e-8);
        QVERIFY(applyLayerTransform(d.get(), QTransform::fromTranslate(4, 5)));
        l = d->activeLayer();
        QCOMPARE(l->pixels.image(), pixels);
        QCOMPARE(l->offset, QPointF(3, 2));
        QCOMPARE(l->maskOffset, QPointF(4, 5));
        QVERIFY(!l->maskLinked);
        QVERIFY(std::abs(maskSample(l->mask, 1, 1) - 1e-8) < 1e-12);
    }
    void smartTransformRetainsSourceAndEditableFilter() {
        std::unique_ptr<Document> d(Document::create({16, 12}, Qt::red));
        const auto source = d->activeLayer()->pixels.image();
        d->convertToSmartObject();
        d->addSmartFilter("Invert");
        QVERIFY(applyLayerTransform(d.get(), QTransform::fromScale(2, 2)));
        QCOMPARE(d->activeLayer()->pixels.image(), source);
        QCOMPARE(d->layerImage(*d->activeLayer()).size(), QSize(32, 24));
        QCOMPARE(d->activeLayer()->smartFilters.size(), 1);
        QVERIFY(applyLayerTransform(d.get(), QTransform().rotate(90)));
        QCOMPARE(d->activeLayer()->pixels.image(), source);
        QCOMPARE(d->layerImage(*d->activeLayer()).size(), QSize(24, 32));
    }
    void invalidGroupTransformPreservesChildren() {
        std::unique_ptr<Document> d(Document::create({8, 8}, Qt::red));
        d->addLayer("Group", LayerKind::Group);
        const auto count = d->historyNames().size();
        QString error;
        QVERIFY(!applyLayerTransform(d.get(), QTransform::fromScale(2, 2), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(d->activeLayer()->kind, LayerKind::Group);
        QCOMPARE(d->historyNames().size(), count);
    }
    void smartFilterEditorCancelIsNonDestructive() {
        std::unique_ptr<Document> d(Document::create({10, 10}, Qt::red));
        d->convertToSmartObject();
        d->addSmartFilter("Gaussian Blur", {{"radius", 2}});
        const auto filters = d->activeLayer()->smartFilters;
        const auto source = d->activeLayer()->pixels.image();
        SmartFilterDialog dialog(d.get());
        auto *radius = dialog.findChild<QDoubleSpinBox *>("filter_radius");
        QVERIFY(radius);
        radius->setValue(12.5);
        QCOMPARE(dialog.filters()[0].toObject()["parameters"].toObject()["radius"].toDouble(), 12.5);
        dialog.reject();
        QCOMPARE(d->activeLayer()->smartFilters, filters);
        QCOMPARE(d->activeLayer()->pixels.image(), source);
    }
    void smartFilterOpacityDoesNotInjectOtherFiltersParameters() {
        std::unique_ptr<Document> d(Document::create({10, 10}, Qt::red));
        d->convertToSmartObject();
        d->addSmartFilter("Exposure", {{"exposure", 1}});
        d->addSmartFilter("Levels", {{"gamma", 2}});
        SmartFilterDialog dialog(d.get());
        dialog.findChild<QDoubleSpinBox *>("filterOpacity")->setValue(50);
        const auto entry = dialog.filters()[0].toObject();
        QCOMPARE(entry["opacity"].toDouble(), .5);
        QCOMPARE(entry["parameters"].toObject(), QJsonObject({{"exposure", 1}}));
    }
    void refinedNewLayerRetainsFloatCoverageAndHdrPixels() {
        QImage source(3, 2, QImage::Format_RGBA32FPx4);
        source.fill(Qt::white);
        reinterpret_cast<float *>(source.scanLine(0))[0] = 4.5f;
        auto mask = makeMask(source.size(), 32, 1);
        setMaskSample(mask, 0, 0, 1e-8);
        MaskRefineDialog dialog(source, mask, 32);
        dialog.findChild<QComboBox *>("maskOutput")->setCurrentIndex(2);
        const auto result = dialog.outputImage();
        const auto *pixel = reinterpret_cast<const float *>(result.constScanLine(0));
        QCOMPARE(pixel[0], 4.5f);
        QVERIFY(std::abs(pixel[3] - 1e-8) < 1e-12);
    }
    void selectAndMaskCancelLeavesHistoryAndSelection() {
        MainWindow window;
        auto *d = small(window);
        const auto before = d->state.selection;
        const auto history = d->historyNames();
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->reject();
        });
        window.runCommand("Select and Mask...");
        QCOMPARE(d->state.selection, before);
        QCOMPARE(d->historyNames(), history);
    }
    void refinementOutputMapsDocumentSelectionToLocalLayer() {
        MainWindow window;
        auto *d = small(window);
        auto selection = makeMask(d->state.size, 8);
        setMaskSample(selection, 11, 9, 1);
        d->setSelection(selection);
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<MaskRefineDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->findChild<QComboBox *>("maskOutput")->setCurrentIndex(1);
            dialog->accept();
        });
        window.runCommand("Select and Mask...");
        QCOMPARE(d->activeLayer()->mask.size(), QSize(8, 6));
        QCOMPARE(maskSample(d->activeLayer()->mask, 1, 1), 1.);
        QCOMPARE(maskSample(d->activeLayer()->mask, 0, 0), 0.);
        QCOMPARE(d->composite().pixelColor(11, 9).alpha(), 255);
    }
    void freeTransformCommandStartsCancellableCanvasSession() {
        MainWindow window;
        auto *d = small(window);
        window.runCommand("Free Transform...");
        QVERIFY(window.currentCanvas()->hasTransformPreview());
        QVERIFY(d->historyNames().isEmpty());
        window.currentCanvas()->cancelTransform();
        QVERIFY(!window.currentCanvas()->hasTransformPreview());
        QVERIFY(d->historyNames().isEmpty());
    }
    void autoCropMenuKeepsPreviewUntilCommit() {
        MainWindow window;
        auto *d = small(window);
        window.runCommand("Auto Crop Transparent");
        QVERIFY(window.currentCanvas()->hasCropPreview());
        QCOMPARE(d->state.size, QSize(32, 24));
        QCOMPARE(window.currentCanvas()->cropPreviewRect(), QRectF(10, 8, 8, 6));
        window.runCommand("Commit Crop");
        QCOMPARE(d->state.size, QSize(8, 6));
        d->undo();
        QCOMPARE(d->state.size, QSize(32, 24));
    }
    void deselectReselectRetainsFloatCoverageAndOneUndoStep() {
        std::unique_ptr<Document> d(Document::create({8, 6}, Qt::red, 32));
        auto mask = makeMask(d->state.size, 32);
        setMaskSample(mask, 3, 2, 1e-8);
        d->setSelection(mask);
        d->clearHistory();
        d->deselect();
        QVERIFY(!d->hasSelection());
        QCOMPARE(d->historyNames().size(), 1);
        QVERIFY(d->reselect());
        QVERIFY(std::abs(maskSample(d->state.selection, 3, 2) - 1e-8) < 1e-12);
        QVERIFY(d->savedSelectionNames().isEmpty());
        d->undo();
        QVERIFY(!d->hasSelection());
        d->undo();
        QCOMPARE(d->state.selection, mask);
    }
    void pasteInPlaceRestoresCopiedSelectionPosition() {
        MainWindow window;
        auto *d = small(window);
        auto mask = makeMask(d->state.size, 8);
        for (int y = 9; y < 12; ++y)
            for (int x = 11; x < 15; ++x)
                setMaskSample(mask, x, y, 1);
        d->setSelection(mask);
        window.runCommand("Copy");
        window.runCommand("Paste in Place");
        QCOMPARE(d->effectiveLayerOffset(*d->activeLayer()), QPointF(11, 9));
        QCOMPARE(d->activeLayer()->pixels.size, QSize(4, 3));
        QCOMPARE(d->activeLayer()->pixels.image().pixelColor(0, 0), QColor(Qt::red));
    }
    void pasteIntoCreatesMaskInLocalCoordinates() {
        MainWindow window;
        auto *d = small(window);
        auto mask = makeMask(d->state.size, 8);
        setMaskSample(mask, 15, 11, 1);
        d->setSelection(mask);
        QImage image(8, 6, QImage::Format_RGBA8888);
        image.fill(Qt::blue);
        qApp->clipboard()->setImage(image);
        window.runCommand("Paste Into");
        QCOMPARE(d->activeLayer()->offset, QPointF(12, 9));
        QCOMPARE(maskSample(d->activeLayer()->mask, 3, 2), 1.);
        QCOMPARE(maskSample(d->activeLayer()->mask, 0, 0), 0.);
    }
    void mergeVisiblePreservesHiddenLayersAndUndo() {
        MainWindow window;
        auto *d = small(window);
        const auto image = d->composite();
        d->addLayer("Hidden original");
        d->activeLayer()->visible = false;
        const auto id = d->activeLayer()->id;
        d->touch();
        window.runCommand("Merge Visible");
        QCOMPARE(d->state.layers.size(), 2);
        QVERIFY(d->indexForId(id) >= 0);
        QVERIFY(!d->state.layers[d->indexForId(id)].visible);
        QCOMPARE(d->composite(), image);
        d->undo();
        QCOMPARE(d->state.layers.size(), 2);
        QCOMPARE(d->state.layers[0].name, QString("Pixels"));
    }
    void groupingExistingGroupAvoidsCycleAndUngroupPreservesPosition() {
        MainWindow window;
        auto *d = small(window);
        const auto leaf = d->activeLayer()->id;
        d->addLayer("Existing group", LayerKind::Group);
        const auto originalGroup = d->activeLayer()->id;
        d->activeLayer()->offset = {3, 4};
        d->state.layers[d->indexForId(leaf)].parentId = originalGroup;
        d->touch();
        QCoreApplication::processEvents();
        const auto original = d->composite();
        window.runCommand("Group Layers");
        const auto wrapper = d->activeLayer()->id;
        QVERIFY(wrapper != originalGroup);
        QCOMPARE(d->activeLayer()->parentId, quint64(0));
        QCOMPARE(d->state.layers[d->indexForId(originalGroup)].parentId, wrapper);
        QCOMPARE(d->composite(), original);
        window.runCommand("Ungroup Layers");
        QCOMPARE(d->state.layers[d->indexForId(originalGroup)].parentId, quint64(0));
        QCOMPARE(d->composite(), original);
        d->setActiveIndex(d->indexForId(originalGroup));
        window.runCommand("Ungroup Layers");
        QCOMPARE(d->state.layers[d->indexForId(leaf)].offset, QPointF(13, 12));
        QCOMPARE(d->composite(), original);
    }
    void applyingVectorMaskUsesLayerCoordinatesInsideGroup() {
        MainWindow window;
        auto *d = small(window);
        const auto leaf = d->activeLayer()->id;
        d->addLayer("Offset group", LayerKind::Group);
        const auto group = d->activeLayer()->id;
        d->activeLayer()->offset = {100, 0};
        auto &child = d->state.layers[d->indexForId(leaf)];
        child.parentId = group;
        child.offset = {-100, 0};
        child.vectorMask.addRect(QRectF(0, 0, 4, 6));
        d->setActiveIndex(d->indexForId(leaf));
        d->touch();
        const auto before = d->composite();
        window.runCommand("Apply Vector Mask");
        QVERIFY(d->activeLayer()->vectorMask.isEmpty());
        QCOMPARE(d->composite(), before);
        QCOMPARE(d->activeLayer()->pixels.image().pixelColor(2, 2).alpha(), 255);
    }
    void fadeBlendsLastPixelEditWithoutChangingLayerOpacity() {
        MainWindow window;
        auto *d = small(window);
        d->mutate("Blue fill", [&] {
            QImage pixels = d->activeLayer()->pixels.image();
            pixels.fill(Qt::blue);
            d->activeLayer()->pixels.setImage(pixels);
        });
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->setIntValue(50);
            dialog->accept();
        });
        window.runCommand("Fade...");
        const auto color = d->activeLayer()->pixels.image().pixelColor(1, 1);
        QVERIFY(qAbs(color.red() - 128) <= 1);
        QVERIFY(qAbs(color.blue() - 128) <= 1);
        QCOMPARE(d->activeLayer()->opacity, 1.);
        d->undo();
        QCOMPARE(d->activeLayer()->pixels.image().pixelColor(1, 1), QColor(Qt::blue));
    }
};
QTEST_MAIN(WorkflowTests)
#include "workflow_tests.moc"
