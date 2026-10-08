#include "tools/LocalAlgorithms.h"
#include "ui/canvas/CanvasView.h"
#include "ui/dialogs/LiquifyDialog.h"
#include <QComboBox>
#include <QInputDialog>
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
        QTest::mouseMove(&canvas, canvas.fromDocument(end).toPoint());
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
};
QTEST_MAIN(CanvasTests)
#include "canvas_tests.moc"
