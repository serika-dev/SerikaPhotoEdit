#include "tools/LocalAlgorithms.h"
#include "ui/canvas/CanvasView.h"
#include "ui/canvas/CropAlgorithms.h"
#include "ui/dialogs/LiquifyDialog.h"
#include <QComboBox>
#include <QInputDialog>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QtTest>

using namespace serika;
class CanvasTests final : public QObject {
    Q_OBJECT
  private:
    static void initialize(CanvasView &canvas) {
        canvas.resize(640, 480);
        canvas.setShowRulers(false);
        canvas.show();
        canvas.setZoom(2);
        QCoreApplication::processEvents();
    }
    static void gesture(CanvasView &canvas, QString tool, QPointF start, QPointF end,
                        Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        canvas.setTool(tool);
        QTest::mousePress(&canvas, Qt::LeftButton, modifiers, canvas.fromDocument(start).toPoint());
        const QPointF position = canvas.fromDocument(end);
        QMouseEvent movement(QEvent::MouseMove, position, canvas.mapToGlobal(position.toPoint()),
                             Qt::NoButton, Qt::LeftButton, modifiers);
        QCoreApplication::sendEvent(&canvas, &movement);
        QTest::mouseRelease(&canvas, Qt::LeftButton, modifiers, canvas.fromDocument(end).toPoint());
    }
  private slots:
    void brushEraserHistory() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::transparent));
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setForeground(QColor("#e8893a"));
        canvas.setBrushSize(18);
        canvas.setBrushHardness(1);
        gesture(canvas, "Brush", {30, 40}, {70, 40});
        QCOMPARE(document->historyNames().size(), 1);
        QVERIFY(document->composite().pixelColor(50, 40).alpha() > 240);
        QVERIFY(document->activeLayer()->pixels.tiles.size() <= 1);
        const QImage painted = document->composite();
        document->undo();
        QCOMPARE(document->composite().pixelColor(50, 40).alpha(), 0);
        document->redo();
        QCOMPARE(document->composite(), painted);
        gesture(canvas, "Eraser", {50, 40}, {50, 40});
        QCOMPARE(document->composite().pixelColor(50, 40).alpha(), 0);
        document->undo();
        QCOMPARE(document->composite(), painted);
    }
    void selectionConstrainsPixels() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::transparent));
        CanvasView canvas(document.get());
        initialize(canvas);
        gesture(canvas, "Rectangular Marquee", {40, 30}, {80, 60});
        QVERIFY(document->hasSelection());
        QVERIFY(document->selectionBounds().contains({50, 40}));
        canvas.setForeground(Qt::red);
        canvas.setBrushSize(40);
        canvas.setBrushHardness(1);
        gesture(canvas, "Brush", {35, 40}, {70, 40});
        QCOMPARE(document->composite().pixelColor(35, 40).alpha(), 0);
        QVERIFY(document->composite().pixelColor(55, 40).red() > 240);
        document->undo();
        QCOMPARE(document->composite().pixelColor(55, 40).alpha(), 0);
    }
    void brushOpacityCapsGesture() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::transparent));
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setForeground(Qt::red);
        canvas.setBrushSize(30);
        canvas.setBrushHardness(1);
        canvas.setBrushOpacity(.5);
        canvas.setBrushFlow(1);
        gesture(canvas, "Brush", {35, 40}, {80, 40});
        const int alpha = document->composite().pixelColor(60, 40).alpha();
        QVERIFY2(alpha >= 120 && alpha <= 130, qPrintable(QString::number(alpha)));
    }
    void paintingUsesGroupCoordinates() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::transparent));
        document->addLayer("Group", LayerKind::Group);
        document->activeLayer()->offset = {20, 10};
        document->addLayer("Child");
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setForeground(Qt::red);
        canvas.setBrushHardness(1);
        canvas.setBrushSize(10);
        gesture(canvas, "Brush", {40, 40}, {40, 40});
        QCOMPARE(document->activeLayer()->pixels.region({20, 30, 1, 1}).pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(document->composite().pixelColor(40, 40), QColor(Qt::red));
        document->undo();
        QCOMPARE(document->composite().pixelColor(40, 40).alpha(), 0);
    }
    void clonePreservesSixteenBitPixels() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::white, 16));
        const QColor detailed = QColor::fromRgbF(.12345, .45678, .78901, 1);
        document->activeLayer()->pixels.paint(
            {10, 10, 20, 20}, [&](QPainter &painter) { painter.fillRect(10, 10, 20, 20, detailed); });
        document->touch();
        const QColor original = document->activeLayer()->pixels.region({20, 20, 1, 1}).pixelColor(0, 0);
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setBrushSize(10);
        canvas.setBrushHardness(1);
        canvas.setTool("Clone Stamp");
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::AltModifier, canvas.fromDocument({20, 20}).toPoint());
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({70, 70}).toPoint());
        QCOMPARE(document->activeLayer()->pixels.region({70, 70, 1, 1}).pixelColor(0, 0), original);
        QCOMPARE(document->activeLayer()->pixels.format, QImage::Format_RGBA64);
    }
    void maskPainting() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::white));
        document->addMask();
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setForeground(Qt::black);
        canvas.setBrushSize(20);
        canvas.setBrushHardness(1);
        gesture(canvas, "Brush", {50, 50}, {50, 50});
        QCOMPARE(qGray(document->activeLayer()->mask.pixel(50, 50)), 0);
        QCOMPARE(document->composite().pixelColor(50, 50).alpha(), 0);
        document->undo();
        QCOMPARE(document->composite().pixelColor(50, 50).alpha(), 255);
    }
    void moveCropShapeAndPen() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::white));
        CanvasView canvas(document.get());
        initialize(canvas);
        gesture(canvas, "Move", {30, 30}, {45, 42});
        QCOMPARE(document->activeLayer()->offset, QPointF(15, 12));
        document->undo();
        QCOMPARE(document->activeLayer()->offset, QPointF());
        gesture(canvas, "Crop", {10, 10}, {110, 90});
        QCOMPARE(document->state.size, QSize(160, 120));
        QVERIFY(canvas.hasCropPreview());
        QTest::keyClick(&canvas, Qt::Key_Return);
        QCOMPARE(document->state.size, QSize(100, 80));
        document->undo();
        QCOMPARE(document->state.size, QSize(160, 120));
        canvas.setForeground(Qt::red);
        gesture(canvas, "Ellipse", {20, 20}, {60, 60});
        QCOMPARE(document->activeLayer()->kind, LayerKind::Shape);
        QVERIFY(!document->activeLayer()->shape.isEmpty());
        QVERIFY(document->composite().pixelColor(40, 40).red() > 240);
        document->undo();
        QCOMPARE(document->state.layers.size(), 1);
        canvas.setTool("Pen");
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({20, 20}).toPoint());
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({70, 60}).toPoint());
        QVERIFY(document->activeLayer()->shape.elementCount() >= 2);
        document->undo();
        QCOMPARE(document->activeLayer()->shape.elementCount(), 1);
    }
    void gradientBucketCloneAndEyedropper() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::white));
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setForeground(Qt::red);
        canvas.setBackground(Qt::blue);
        gesture(canvas, "Gradient", {0, 50}, {160, 50});
        QVERIFY(document->composite().pixelColor(10, 50).red() >
                document->composite().pixelColor(10, 50).blue());
        QVERIFY(document->composite().pixelColor(150, 50).blue() >
                document->composite().pixelColor(150, 50).red());
        document->undo();
        QCOMPARE(document->composite().pixelColor(50, 50), QColor(Qt::white));
        gesture(canvas, "Paint Bucket", {30, 30}, {30, 30});
        QCOMPARE(document->composite().pixelColor(50, 50), QColor(Qt::red));
        document->undo();
        canvas.setBrushSize(20);
        canvas.setBrushHardness(1);
        gesture(canvas, "Brush", {25, 25}, {25, 25});
        canvas.setTool("Clone Stamp");
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::AltModifier, canvas.fromDocument({25, 25}).toPoint());
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({80, 80}).toPoint());
        QCOMPARE(document->composite().pixelColor(80, 80), QColor(Qt::red));
        document->undo();
        QCOMPARE(document->composite().pixelColor(80, 80), QColor(Qt::white));
        QSignalSpy picked(&canvas, &CanvasView::colorPicked);
        gesture(canvas, "Eyedropper", {25, 25}, {25, 25});
        QCOMPARE(picked.size(), 1);
        QCOMPARE(canvas.foreground(), QColor(Qt::red));
    }
    void textCreatesEditableLayer() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::white));
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setProperty("typeFont", QFont("Georgia"));
        canvas.setProperty("typeSize", 72);
        QTimer::singleShot(0, [] {
            if (auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
                dialog->setTextValue("Editable Serika");
                dialog->accept();
            }
        });
        gesture(canvas, "Horizontal Type", {15, 60}, {15, 60});
        QCOMPARE(document->activeLayer()->kind, LayerKind::Text);
        QCOMPARE(document->activeLayer()->text, QString("Editable Serika"));
        QCOMPARE(document->activeLayer()->font.family(), QString("Georgia"));
        QCOMPARE(document->activeLayer()->font.pointSize(), 72);
        document->undo();
        QCOMPARE(document->state.layers.size(), 1);
    }
    void moveOptionsSelectLayerAndShowBounds() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::white));
        document->addLayer("Top");
        document->activeLayer()->pixels.paint(
            {40, 40, 20, 20}, [](QPainter &painter) { painter.fillRect(40, 40, 20, 20, Qt::red); });
        document->touch();
        document->setActiveIndex(0);
        CanvasView canvas(document.get());
        initialize(canvas);
        gesture(canvas, "Move", {50, 50}, {60, 55});
        QCOMPARE(document->state.activeIndex, 1);
        QCOMPARE(document->activeLayer()->offset, QPointF(10, 5));
        document->undo();
        document->setActiveIndex(0);
        canvas.setProperty("autoSelect", false);
        gesture(canvas, "Move", {50, 50}, {60, 55});
        QCOMPARE(document->state.activeIndex, 0);
        QCOMPARE(document->state.layers[1].offset, QPointF());
        const QImage plain = canvas.grab().toImage();
        canvas.setProperty("showTransformControls", true);
        const QImage controls = canvas.grab().toImage();
        QVERIFY(plain != controls);
    }
    void localSubjectAndPatchFallback() {
        QImage image(96, 96, QImage::Format_RGBA64);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.fillRect(25, 25, 46, 46, Qt::red);
        painter.end();
        ColorModelSubjectSelector selector;
        const QImage selection = selector.select(image);
        QVERIFY(qGray(selection.pixel(48, 48)) > 240);
        QVERIFY(qGray(selection.pixel(0, 0)) < 10);
        QImage texture(64, 64, QImage::Format_RGBA64);
        texture.fill(QColor::fromRgbF(.12345, .45678, .78901));
        QImage damaged = texture;
        QPainter damage(&damaged);
        damage.fillRect(25, 25, 12, 12, Qt::magenta);
        damage.end();
        QImage mask(64, 64, QImage::Format_Grayscale8);
        mask.fill(0);
        QPainter selectionPainter(&mask);
        selectionPainter.fillRect(25, 25, 12, 12, Qt::white);
        selectionPainter.end();
        const QImage repaired = synthesizePatches(damaged, mask);
        QCOMPARE(repaired.format(), QImage::Format_RGBA64);
        QCOMPARE(repaired.pixelColor(30, 30), texture.pixelColor(30, 30));
    }
    void customSubjectSelectorIsUsed() {
        class TestSelector final : public ISubjectSelector {
          public:
            QImage select(const QImage &image) const override {
                QImage mask(image.size(), QImage::Format_Grayscale8);
                mask.fill(64);
                return mask;
            }
        };
        registerSubjectSelector(std::make_shared<TestSelector>());
        std::unique_ptr<Document> document(Document::create({64, 64}, Qt::white));
        CanvasView canvas(document.get());
        canvas.selectSubject();
        QCOMPARE(qGray(document->state.selection.pixel(20, 20)), 64);
        registerSubjectSelector({});
    }
    void directAnchorCurvatureAndPerspective() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::white));
        CanvasView canvas(document.get());
        initialize(canvas);
        gesture(canvas, "Rectangle", {20, 20}, {60, 60});
        const QPainterPath original = document->activeLayer()->shape;
        gesture(canvas, "Direct Selection", {20, 20}, {25, 25});
        QCOMPARE(document->activeLayer()->shape.elementAt(0).x, 25.0);
        document->undo();
        QCOMPARE(document->activeLayer()->shape, original);
        canvas.setTool("Curvature Pen");
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({20, 20}).toPoint());
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({60, 50}).toPoint());
        QCOMPARE(document->activeLayer()->shape.elementAt(1).type, QPainterPath::CurveToElement);
        canvas.setTool("Perspective Crop");
        for (const QPointF point : QPolygonF{{10, 10}, {130, 15}, {120, 100}, {20, 95}})
            QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument(point).toPoint());
        QCOMPARE(document->state.size, QSize(160, 120));
        QVERIFY(canvas.hasCropPreview());
        QTest::keyClick(&canvas, Qt::Key_Return);
        QVERIFY(document->state.size != QSize(160, 120));
        QVERIFY(document->state.metadata.contains("perspectiveCropCorners"));
        document->undo();
        QCOMPARE(document->state.size, QSize(160, 120));
    }
    void liquifyWarpPreservesDepth() {
        QImage input(64, 64, QImage::Format_RGBA64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                input.setPixelColor(x, y, QColor::fromRgbF(x / 63.0, y / 63.0, .3, 1));
        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            auto *canvas = dialog->findChild<QWidget *>("liquifyCanvas");
            if (!canvas) {
                dialog->reject();
                return;
            }
            const QPoint center = canvas->rect().center();
            QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, center);
            QTest::mouseMove(canvas, center + QPoint(40, 0));
            QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(40, 0));
            dialog->accept();
        });
        const QImage warped = LiquifyDialog::edit(input);
        QVERIFY(!warped.isNull());
        QCOMPARE(warped.format(), input.format());
        QCOMPARE(warped.size(), input.size());
        QVERIFY(warped != input);
        QCOMPARE(warped.pixelColor(32, 32).alpha(), 255);
    }
    void cropPreviewCancelAndHandles() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::white));
        const QImage before = document->composite();
        document->markSaved();
        CanvasView canvas(document.get());
        initialize(canvas);
        gesture(canvas, "Crop", {20, 15}, {120, 95});
        QCOMPARE(canvas.cropPreviewRect(), QRectF(20, 15, 100, 80));
        QVERIFY(!document->isModified());
        QCOMPARE(document->composite(), before);
        const QImage preview = canvas.grab().toImage();
        QTest::keyClick(&canvas, Qt::Key_Escape);
        QVERIFY(!canvas.hasCropPreview());
        QVERIFY(!document->isModified());
        QVERIFY(canvas.grab().toImage() != preview);
        canvas.setCropPreviewRect({20, 15, 100, 80});
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({120, 95}).toPoint());
        QTest::mouseMove(&canvas, canvas.fromDocument({140, 105}).toPoint());
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier,
                            canvas.fromDocument({140, 105}).toPoint());
        QCOMPARE(canvas.cropPreviewRect(), QRectF(20, 15, 120, 90));
        QVERIFY(canvas.commitCrop());
        QCOMPARE(document->state.size, QSize(120, 90));
        QCOMPARE(document->historyNames().size(), 1);
        document->undo();
        QCOMPARE(document->composite(), before);
        QVERIFY(!document->isModified());
    }
    void cropRatioOutputResolutionAndOutsidePixels() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::transparent, 16));
        document->activeLayer()->pixels.paint({0, 0, 160, 120}, [](QPainter &painter) {
            painter.fillRect(0, 0, 160, 120, Qt::blue);
            painter.fillRect(0, 0, 20, 20, Qt::red);
        });
        document->touch();
        const QImage before = document->composite();
        CanvasView canvas(document.get());
        initialize(canvas);
        CropSettings settings;
        settings.ratio = {1, 1};
        canvas.setCropSettings(settings);
        gesture(canvas, "Crop", {30, 30}, {90, 110});
        QCOMPARE(canvas.cropPreviewRect().size(), QSizeF(60, 60));
        QVERIFY(canvas.commitCrop());
        QCOMPARE(document->activeLayer()->pixels.size, QSize(160, 120));
        QCOMPARE(document->activeLayer()->pixels.region({0, 0, 1, 1}).pixelColor(0, 0), QColor(Qt::red));
        document->undo();
        QCOMPARE(document->composite(), before);
        settings.outputSize = {40, 40};
        settings.resolution = 300;
        canvas.setCropSettings(settings);
        canvas.setCropPreviewRect({30, 30, 60, 60});
        const int history = document->historyNames().size();
        QVERIFY(canvas.commitCrop());
        QCOMPARE(document->state.size, QSize(40, 40));
        QCOMPARE(document->state.resolution, 300.0);
        QCOMPARE(document->historyNames().size(), history + 1);
        document->undo();
        QCOMPARE(document->composite(), before);
        QCOMPARE(document->state.resolution, 72.0);
    }
    void autoCropTransparentAndUniformBorder() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::transparent));
        document->activeLayer()->pixels.paint(
            {32, 24, 70, 60}, [](QPainter &painter) { painter.fillRect(32, 24, 70, 60, Qt::red); });
        document->touch();
        CanvasView canvas(document.get());
        initialize(canvas);
        QVERIFY(canvas.autoCropTransparent());
        QCOMPARE(canvas.cropPreviewRect(), QRectF(32, 24, 70, 60));
        QCOMPARE(document->state.size, QSize(160, 120));
        canvas.cancelCrop();
        QVERIFY(canvas.autoCropTransparent(false));
        QCOMPARE(document->state.size, QSize(70, 60));
        QCOMPARE(document->composite().pixelColor(0, 0), QColor(Qt::red));
        document->undo();
        document->activeLayer()->pixels.paint({0, 0, 160, 120}, [](QPainter &painter) {
            painter.fillRect(0, 0, 160, 120, Qt::white);
            painter.fillRect(32, 24, 70, 60, Qt::red);
        });
        document->touch();
        QVERIFY(canvas.autoCropContent());
        QCOMPARE(canvas.cropPreviewRect(), QRectF(32, 24, 70, 60));
        canvas.cancelCrop();
        QImage empty(24, 24, QImage::Format_ARGB32);
        empty.fill(Qt::transparent);
        QVERIFY(transparentContentBounds(empty).isEmpty());
        QVERIFY(uniformBorderContentBounds(empty).isEmpty());
    }
    void cropStraightenPreviewPreservesDepthAndOneUndo() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::white, 16));
        document->activeLayer()->pixels.paint({0, 0, 160, 120}, [](QPainter &painter) {
            painter.setPen(QPen(Qt::black, 4));
            painter.drawLine(0, 50, 160, 70);
        });
        document->touch();
        document->markSaved();
        const QImage before = document->composite();
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setTool("Crop");
        canvas.beginStraighten();
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({0, 50}).toPoint());
        QTest::mouseMove(&canvas, canvas.fromDocument({160, 70}).toPoint());
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier,
                            canvas.fromDocument({160, 70}).toPoint());
        QVERIFY(std::abs(canvas.cropSettings().angle + 7.125) < .1);
        QCOMPARE(document->composite(), before);
        QVERIFY(!document->isModified());
        QVERIFY(canvas.commitCrop());
        QCOMPARE(document->activeLayer()->pixels.format, QImage::Format_RGBA64);
        QCOMPARE(document->historyNames().size(), 1);
        QVERIFY(document->composite() != before);
        document->undo();
        QCOMPARE(document->composite(), before);
        QVERIFY(!document->isModified());
        const qreal angle = estimateStraightenAngle(before);
        QVERIFY2(std::abs(angle + 7.125) < 2, qPrintable(QString::number(angle)));
    }
    void cropUsesRemappedCommitAndCancel() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::white));
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setTool("Crop");
        canvas.setCropPreviewRect({20, 20, 80, 60});
        canvas.setProperty("externalShortcuts", true);
        QTest::keyClick(&canvas, Qt::Key_Return);
        QCOMPARE(document->state.size, QSize(160, 120));
        QTest::keyClick(&canvas, Qt::Key_Escape);
        QVERIFY(canvas.hasCropPreview());
        canvas.setProperty("shortcutForwarding", true);
        QTest::keyClick(&canvas, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(document->state.size, QSize(80, 60));
        document->undo();
        canvas.setCropPreviewRect({20, 20, 80, 60});
        QTest::keyClick(&canvas, Qt::Key_Escape);
        QVERIFY(!canvas.hasCropPreview());
        QCOMPARE(document->state.size, QSize(160, 120));
    }
    void maskPreviewAndNativeSixteenBitPainting() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::white, 16));
        document->addMask();
        setMaskSample(document->activeLayer()->mask, 3, 3, 1.0 / 65535);
        document->touch();
        const QImage before = document->composite();
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setMaskPreview(CanvasView::MaskPreview::Grayscale);
        const QImage grayscale = canvas.grab().toImage();
        canvas.setMaskPreview(CanvasView::MaskPreview::Overlay);
        QVERIFY(canvas.grab().toImage() != grayscale);
        QCOMPARE(document->composite(), before);
        canvas.setForeground(QColor::fromRgbF(.12345, .12345, .12345));
        canvas.setBrushHardness(1);
        canvas.setBrushSize(14);
        gesture(canvas, "Brush", {60, 60}, {60, 60});
        QCOMPARE(document->activeLayer()->mask.format(), QImage::Format_Grayscale16);
        QVERIFY(std::abs(maskSample(document->activeLayer()->mask, 60, 60) - .12345) < 2.0 / 65535);
        QCOMPARE(maskSample(document->activeLayer()->mask, 3, 3), 1.0 / 65535);
        document->undo();
        QCOMPARE(maskSample(document->activeLayer()->mask, 60, 60), 1.0);
        QCOMPARE(maskSample(document->activeLayer()->mask, 3, 3), 1.0 / 65535);
    }
    void unlinkedMaskStaysStillWhenLayerMoves() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::white));
        document->addMask();
        document->activeLayer()->maskTarget = false;
        document->activeLayer()->maskLinked = false;
        document->activeLayer()->mask.fill(0);
        for (int y = 30; y < 80; ++y)
            for (int x = 30; x < 80; ++x)
                setMaskSample(document->activeLayer()->mask, x, y, 1);
        document->touch();
        const QImage before = document->composite();
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setProperty("autoSelect", false);
        gesture(canvas, "Move", {50, 50}, {70, 55});
        QCOMPARE(document->activeLayer()->offset, QPointF(20, 5));
        QCOMPARE(document->activeLayer()->maskOffset, QPointF(-20, -5));
        QCOMPARE(document->composite().pixelColor(40, 40), QColor(Qt::white));
        QCOMPARE(document->composite().pixelColor(85, 50).alpha(), 0);
        document->undo();
        QCOMPARE(document->composite(), before);
    }
    void contentAwareMoveTransportsSelectionAndUndoes() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::white, 16));
        document->activeLayer()->pixels.paint(
            {25, 35, 20, 20}, [](QPainter &painter) { painter.fillRect(25, 35, 20, 20, Qt::red); });
        document->touch();
        QImage mask = makeMask({160, 120}, 16);
        for (int y = 35; y < 55; ++y)
            for (int x = 25; x < 45; ++x)
                setMaskSample(mask, x, y, 1);
        document->setSelection(mask);
        const QImage before = document->composite();
        const int history = document->historyNames().size();
        CanvasView canvas(document.get());
        initialize(canvas);
        gesture(canvas, "Content-Aware Move", {35, 45}, {95, 65});
        QCOMPARE(document->composite().pixelColor(95, 65), QColor(Qt::red));
        QCOMPARE(document->composite().pixelColor(35, 45), QColor(Qt::white));
        QVERIFY(document->selectionBounds().contains({95, 65}));
        QCOMPARE(document->activeLayer()->pixels.format, QImage::Format_RGBA64);
        QCOMPARE(document->historyNames().size(), history + 1);
        document->undo();
        QCOMPARE(document->composite(), before);
        QCOMPARE(document->state.selection, mask);
        document->redo();
        QCOMPARE(document->composite().pixelColor(95, 65), QColor(Qt::red));
    }
    void patchUsesDraggedDonorAndCancelKeepsPixels() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::white));
        document->activeLayer()->pixels.paint({20, 20, 110, 50}, [](QPainter &painter) {
            painter.fillRect(20, 20, 20, 20, Qt::magenta);
            painter.fillRect(80, 20, 20, 20, Qt::blue);
        });
        document->touch();
        QImage mask = makeMask({160, 120});
        for (int y = 20; y < 40; ++y)
            for (int x = 20; x < 40; ++x)
                setMaskSample(mask, x, y, 1);
        document->setSelection(mask);
        const QImage before = document->composite();
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setTool("Patch");
        canvas.setProperty("patchColorAdaptation", 0);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({30, 30}).toPoint());
        QTest::mouseMove(&canvas, canvas.fromDocument({90, 30}).toPoint());
        QTest::keyClick(&canvas, Qt::Key_Escape);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({90, 30}).toPoint());
        QCOMPARE(document->composite(), before);
        gesture(canvas, "Patch", {30, 30}, {90, 30});
        QCOMPARE(document->composite().pixelColor(30, 30), QColor(Qt::blue));
        QCOMPARE(document->composite().pixelColor(90, 30), QColor(Qt::blue));
        document->undo();
        QCOMPARE(document->composite(), before);
    }
    void paintingMuscleMemoryModifiersAndOpacityDigits() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::transparent));
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setForeground(Qt::red);
        canvas.setBrushSize(10);
        canvas.setBrushHardness(1);
        gesture(canvas, "Brush", {20, 40}, {20, 40});
        gesture(canvas, "Brush", {100, 40}, {100, 40}, Qt::ShiftModifier);
        QCOMPARE(document->composite().pixelColor(60, 40), QColor(Qt::red));
        const int history = document->historyNames().size();
        canvas.setForeground(Qt::blue);
        gesture(canvas, "Brush", {60, 40}, {60, 40}, Qt::AltModifier);
        QCOMPARE(canvas.foreground(), QColor(Qt::red));
        QCOMPARE(document->historyNames().size(), history);
        QTest::keyClick(&canvas, Qt::Key_3);
        QCOMPARE(canvas.brushOpacity(), .3);
        QTest::keyClick(&canvas, Qt::Key_7);
        QCOMPARE(canvas.brushOpacity(), .37);
        QTest::keyClick(&canvas, Qt::Key_5, Qt::ShiftModifier);
        QCOMPARE(canvas.brushFlow(), .5);
        QTest::keyClick(&canvas, Qt::Key_BracketLeft, Qt::ShiftModifier);
        QCOMPARE(canvas.brushHardness(), .75);
        canvas.setProperty("externalShortcuts", true);
        const int size = canvas.brushSize();
        QTest::keyClick(&canvas, Qt::Key_BracketRight);
        QCOMPARE(canvas.brushSize(), size);
        canvas.setProperty("temporaryMove", true);
        gesture(canvas, "Brush", {60, 40}, {70, 45});
        QCOMPARE(document->activeLayer()->offset, QPointF(10, 5));
        document->undo();
        QCOMPARE(document->activeLayer()->offset, QPointF());
    }
    void inlineTransformPreviewsCancelsAndCommitsOneUndo() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::white, 16));
        document->addLayer("Object");
        document->activeLayer()->pixels.paint(
            {20, 30, 20, 20}, [](QPainter &painter) { painter.fillRect(20, 30, 20, 20, Qt::red); });
        document->touch();
        document->markSaved();
        const QImage before = document->composite();
        const int history = document->historyNames().size();
        CanvasView canvas(document.get());
        initialize(canvas);
        QVERIFY(canvas.beginTransform());
        const QImage unchangedPreview = canvas.grab().toImage();
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({40, 50}).toPoint());
        QTest::mouseMove(&canvas, canvas.fromDocument({60, 70}).toPoint());
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({60, 70}).toPoint());
        QVERIFY(canvas.hasTransformPreview());
        QCOMPARE(document->composite(), before);
        QVERIFY(!document->isModified());
        QVERIFY(canvas.grab().toImage() != unchangedPreview);
        QCOMPARE(canvas.transformPreview().map(QPointF(40, 50)), QPointF(60, 70));
        QTest::keyClick(&canvas, Qt::Key_Escape);
        QVERIFY(!canvas.hasTransformPreview());
        QCOMPARE(document->composite(), before);
        QVERIFY(canvas.beginTransform());
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({40, 50}).toPoint());
        QTest::mouseMove(&canvas, canvas.fromDocument({60, 70}).toPoint());
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({60, 70}).toPoint());
        QTest::keyClick(&canvas, Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(!canvas.hasTransformPreview());
        const QColor transformedColor = document->composite().pixelColor(50, 60);
        QVERIFY(transformedColor.redF() > .99 && transformedColor.greenF() < .01 &&
                transformedColor.blueF() < .01 && transformedColor.alphaF() > .99);
        QCOMPARE(document->activeLayer()->pixels.format, QImage::Format_RGBA64);
        QCOMPARE(document->historyNames().size(), history + 1);
        document->undo();
        QCOMPARE(document->composite(), before);
        QVERIFY(!document->isModified());
    }
    void inlineTransformLinkedAndUnlinkedMasks() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::red, 16));
        document->addMask();
        document->activeLayer()->maskTarget = false;
        document->activeLayer()->mask = makeMask({160, 120}, 16);
        for (int y = 30; y < 80; ++y)
            for (int x = 30; x < 80; ++x)
                setMaskSample(document->activeLayer()->mask, x, y, 1);
        document->touch();
        const QImage before = document->composite();
        CanvasView canvas(document.get());
        initialize(canvas);
        QVERIFY(canvas.beginTransform("Rotate"));
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({170, 60}).toPoint());
        QTest::mouseMove(&canvas, canvas.fromDocument({80, 150}).toPoint());
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier,
                            canvas.fromDocument({80, 150}).toPoint());
        QVERIFY(canvas.commitTransform());
        QCOMPARE(document->composite().pixelColor(90, 35), QColor(Qt::red));
        QCOMPARE(document->composite().pixelColor(40, 70).alpha(), 0);
        QCOMPARE(document->activeLayer()->mask.format(), QImage::Format_Grayscale16);
        document->undo();
        QCOMPARE(document->composite(), before);
        document->activeLayer()->maskLinked = false;
        document->touch();
        QVERIFY(canvas.beginTransform());
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({50, 50}).toPoint());
        QTest::mouseMove(&canvas, canvas.fromDocument({65, 60}).toPoint());
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({65, 60}).toPoint());
        QVERIFY(canvas.commitTransform());
        QCOMPARE(document->activeLayer()->offset, QPointF(15, 10));
        QCOMPARE(document->activeLayer()->maskOffset, QPointF(-15, -10));
        QCOMPARE(document->composite().pixelColor(35, 40), QColor(Qt::red));
        QCOMPARE(document->composite().pixelColor(85, 40).alpha(), 0);
        document->undo();
        QCOMPARE(document->composite(), before);
    }
    void inlineTransformTargetsMaskAndPreservesSmartSources() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::red, 16));
        document->addMask();
        document->activeLayer()->mask = makeMask({160, 120}, 16);
        for (int y = 30; y < 80; ++y)
            for (int x = 30; x < 80; ++x)
                setMaskSample(document->activeLayer()->mask, x, y, 1);
        document->touch();
        const QImage pixels = document->activeLayer()->pixels.image();
        CanvasView canvas(document.get());
        initialize(canvas);
        QVERIFY(canvas.beginTransform());
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({50, 50}).toPoint());
        QTest::mouseMove(&canvas, canvas.fromDocument({65, 60}).toPoint());
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({65, 60}).toPoint());
        QVERIFY(canvas.commitTransform());
        QCOMPARE(document->activeLayer()->pixels.image(), pixels);
        QCOMPARE(document->activeLayer()->offset, QPointF());
        QCOMPARE(document->activeLayer()->maskOffset, QPointF(15, 10));
        QCOMPARE(document->composite().pixelColor(35, 40).alpha(), 0);
        QCOMPARE(document->composite().pixelColor(85, 55), QColor(Qt::red));
        document->undo();
        document->activeLayer()->maskTarget = false;
        document->activeLayer()->kind = LayerKind::SmartObject;
        document->touch();
        QVERIFY(canvas.beginTransform());
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({50, 50}).toPoint());
        QTest::mouseMove(&canvas, canvas.fromDocument({65, 60}).toPoint());
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({65, 60}).toPoint());
        QVERIFY(canvas.commitTransform());
        QCOMPARE(document->activeLayer()->kind, LayerKind::SmartObject);
        QCOMPARE(document->activeLayer()->pixels.image(), pixels);
        QVERIFY(document->activeLayer()->parameters.contains("contentTransform"));
    }
    void brushBehindClearAndBlendModes() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::transparent));
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setForeground(Qt::red);
        canvas.setBrushSize(14);
        canvas.setBrushHardness(1);
        gesture(canvas, "Brush", {50, 50}, {50, 50});
        canvas.setProperty("brushBlendMode", "Behind");
        canvas.setForeground(Qt::blue);
        gesture(canvas, "Brush", {50, 50}, {50, 50});
        QCOMPARE(document->composite().pixelColor(50, 50), QColor(Qt::red));
        gesture(canvas, "Brush", {90, 50}, {90, 50});
        QCOMPARE(document->composite().pixelColor(90, 50), QColor(Qt::blue));
        canvas.setProperty("brushBlendMode", "Clear");
        gesture(canvas, "Brush", {50, 50}, {50, 50});
        QCOMPARE(document->composite().pixelColor(50, 50).alpha(), 0);
        document->undo();
        QCOMPARE(document->composite().pixelColor(50, 50), QColor(Qt::red));
        canvas.setProperty("brushBlendMode", "Multiply");
        gesture(canvas, "Brush", {50, 50}, {50, 50});
        QCOMPARE(document->composite().pixelColor(50, 50), QColor(Qt::black));
    }
    void nativeFloatMaskPaintingAndSelection() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::white, 32));
        document->addMask();
        setMaskSample(document->activeLayer()->mask, 3, 3, 1e-7);
        document->touch();
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setForeground(QColor::fromRgbF(.5, .5, .5));
        canvas.setBrushSize(14);
        canvas.setBrushHardness(1);
        gesture(canvas, "Brush", {60, 60}, {60, 60});
        QCOMPARE(document->activeLayer()->mask.format(), QImage::Format_RGBA32FPx4);
        QVERIFY(std::abs(maskSample(document->activeLayer()->mask, 3, 3) - 1e-7) < 1e-12);
        QVERIFY(std::abs(maskSample(document->activeLayer()->mask, 60, 60) - .5) < 1e-4);
        document->undo();
        QVERIFY(std::abs(maskSample(document->activeLayer()->mask, 3, 3) - 1e-7) < 1e-12);
        QCOMPARE(maskSample(document->activeLayer()->mask, 60, 60), 1.0);
        gesture(canvas, "Elliptical Marquee", {30, 30}, {100, 90});
        QCOMPARE(document->state.selection.format(), QImage::Format_RGBA32FPx4);
        QCOMPARE(maskSample(document->state.selection, 60, 60), 1.0);
    }
    void cropStraightenRetainsPreviouslyTransformedSmartSource() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::transparent, 16));
        document->activeLayer()->pixels.paint(
            {20, 30, 20, 20}, [](QPainter &painter) { painter.fillRect(20, 30, 20, 20, Qt::red); });
        document->activeLayer()->kind = LayerKind::SmartObject;
        QJsonArray scale{1.5, 0, 0, 0, 1.5, 0, 0, 0, 1};
        document->activeLayer()->parameters["contentTransform"] = scale;
        document->touch();
        const QImage pixels = document->activeLayer()->pixels.image();
        const QImage before = document->composite();
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setTool("Crop");
        CropSettings settings;
        settings.angle = 12;
        canvas.setCropSettings(settings);
        canvas.setCropPreviewRect({10, 10, 130, 90});
        QVERIFY(canvas.commitCrop());
        QCOMPARE(document->activeLayer()->kind, LayerKind::SmartObject);
        QCOMPARE(document->activeLayer()->pixels.image(), pixels);
        QVERIFY(document->activeLayer()->parameters["contentTransform"].toArray() != scale);
        document->undo();
        QCOMPARE(document->composite(), before);
        QCOMPARE(document->activeLayer()->parameters["contentTransform"].toArray(), scale);
    }
    void historyBrushUsesChosenStateAndRestoresTransparency() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::transparent, 16));
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setBrushHardness(1);
        canvas.setBrushSize(14);
        canvas.setForeground(Qt::red);
        gesture(canvas, "Brush", {50, 50}, {50, 50});
        canvas.setForeground(Qt::blue);
        gesture(canvas, "Brush", {50, 50}, {50, 50});
        canvas.setProperty("historySourceIndex", 0);
        gesture(canvas, "History Brush", {50, 50}, {50, 50});
        QCOMPARE(document->composite().pixelColor(50, 50), QColor(Qt::red));
        document->undo();
        QCOMPARE(document->composite().pixelColor(50, 50), QColor(Qt::blue));
        canvas.setProperty("historySourceIndex", -1);
        gesture(canvas, "History Brush", {50, 50}, {50, 50});
        QCOMPARE(document->composite().pixelColor(50, 50).alpha(), 0);
        QCOMPARE(document->activeLayer()->pixels.format, QImage::Format_RGBA64);
        document->undo();
        QCOMPARE(document->composite().pixelColor(50, 50), QColor(Qt::blue));
    }
    void inlineSkewUsesShearAndProjectiveModesRemainEditable() {
        std::unique_ptr<Document> document(Document::create({160, 120}, Qt::transparent));
        document->activeLayer()->pixels.paint(
            {20, 30, 80, 40}, [](QPainter &painter) { painter.fillRect(20, 30, 80, 40, Qt::red); });
        document->touch();
        CanvasView canvas(document.get());
        initialize(canvas);
        QVERIFY(canvas.beginTransform("Skew"));
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({60, 30}).toPoint());
        QTest::mouseMove(&canvas, canvas.fromDocument({80, 30}).toPoint());
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({80, 30}).toPoint());
        QCOMPARE(canvas.transformPreview().map(QPointF(20, 30)), QPointF(40, 30));
        QCOMPARE(canvas.transformPreview().map(QPointF(20, 70)), QPointF(20, 70));
        QVERIFY(canvas.commitTransform());
        QVERIFY(document->composite().pixelColor(50, 40).red() > 250);
        document->undo();
        QVERIFY(canvas.beginTransform("Perspective"));
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({20, 30}).toPoint());
        QTest::mouseMove(&canvas, canvas.fromDocument({30, 30}).toPoint());
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({30, 30}).toPoint());
        QVERIFY(!canvas.transformPreview().isAffine());
        QCOMPARE(canvas.transformPreview().map(QPointF(20, 30)), QPointF(30, 30));
        canvas.cancelTransform();
        QCOMPARE(document->state.size, QSize(160, 120));
    }
};
QTEST_MAIN(CanvasTests)
#include "canvas_tests.moc"
