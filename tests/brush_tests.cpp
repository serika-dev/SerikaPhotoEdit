#include "tools/BrushDynamics.h"
#include "ui/MainWindow.h"
#include "ui/canvas/CanvasView.h"
#include "ui/panels/BrushSettingsWidget.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineF>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QtTest>
#include <cmath>
#include <limits>
#include <memory>

using namespace serika;
class BrushTests final : public QObject {
    Q_OBJECT
  private:
    QTemporaryDir m_settingsDirectory;
    static bool newSmallDocument(MainWindow &window) {
        bool accepted = false;
        QTimer watchdog;
        watchdog.setSingleShot(true);
        QObject::connect(&watchdog, &QTimer::timeout, &window, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        watchdog.start(3000);
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->windowTitle() != "New document")
                return;
            const auto dimensions = dialog->findChildren<QSpinBox *>();
            if (dimensions.size() != 2)
                return;
            dimensions[0]->setValue(160);
            dimensions[1]->setValue(128);
            for (auto *combo : dialog->findChildren<QComboBox *>())
                if (const int index = combo->findText("Transparent"); index >= 0)
                    combo->setCurrentIndex(index);
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (!buttons || !buttons->button(QDialogButtonBox::Ok))
                return;
            accepted = true;
            buttons->button(QDialogButtonBox::Ok)->click();
        });
        window.newDocument();
        watchdog.stop();
        QCoreApplication::processEvents();
        return accepted && window.currentCanvas();
    }
    static Layer &layer(Document &document, quint64 id) {
        return document.state.layers[document.indexForId(id)];
    }
    static void setPixels(Layer &layer, QColor color, QPointF offset) {
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(color);
        layer.pixels = TileImage::fromImage(image);
        layer.offset = offset;
    }
    static void selectLayers(CanvasView &canvas, std::initializer_list<quint64> ids) {
        QVariantList selected;
        for (quint64 id : ids)
            selected.append(QVariant::fromValue(id));
        canvas.setProperty("selectedLayerIds", selected);
    }
    static void moveTo(CanvasView &canvas, QPointF point) {
        const QPointF position = canvas.fromDocument(point);
        QMouseEvent movement(QEvent::MouseMove, position, canvas.mapToGlobal(position.toPoint()),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&canvas, &movement);
    }
    static QVector<BrushDab> stroke(const BrushPreset &preset, int subdivisions, quint64 seed = 123) {
        BrushDynamics dynamics;
        QVector<BrushDab> result = dynamics.begin(preset, {{0, 0}, 0.25, {0, 0}}, seed);
        for (int i = 1; i <= subdivisions; ++i) {
            const qreal t = qreal(i) / subdivisions;
            result += dynamics.append({{120 * t, 0}, 0.25 + 0.75 * t, {30 * t, 15 * t}});
        }
        result += dynamics.finish({{120, 0}, 1, {30, 15}});
        return result;
    }
    static void equalDabs(const QVector<BrushDab> &first, const QVector<BrushDab> &second) {
        QCOMPARE(first.size(), second.size());
        for (int i = 0; i < first.size(); ++i) {
            QVERIFY(QLineF(first[i].position, second[i].position).length() < 1e-8);
            QVERIFY(std::abs(first[i].diameter - second[i].diameter) < 1e-8);
            QVERIFY(std::abs(first[i].opacity - second[i].opacity) < 1e-8);
            QVERIFY(std::abs(first[i].angle - second[i].angle) < 1e-8);
            QVERIFY(std::abs(first[i].roundness - second[i].roundness) < 1e-8);
        }
    }
    static void initialize(CanvasView &canvas) {
        canvas.resize(640, 480);
        canvas.setShowRulers(false);
        canvas.show();
        canvas.setZoom(2);
        canvas.setTool("Brush");
        canvas.setProperty("brushRandomSeed", 923);
        QCoreApplication::processEvents();
    }
    static void gesture(CanvasView &canvas, int subdivisions, QPointF from = {20, 64},
                        QPointF to = {108, 64}) {
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument(from).toPoint());
        for (int i = 1; i <= subdivisions; ++i) {
            const QPointF position = canvas.fromDocument(from + (to - from) * (qreal(i) / subdivisions));
            QMouseEvent movement(QEvent::MouseMove, position, canvas.mapToGlobal(position.toPoint()),
                                 Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&canvas, &movement);
        }
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument(to).toPoint());
    }
  private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
    }
    void eventSubdivisionPreservesRandomDynamicsAndSmoothing() {
        BrushPreset preset;
        preset.size = 32;
        preset.spacing = 0.13;
        preset.sizeJitter = 0.7;
        preset.opacityJitter = 0.6;
        preset.angleJitter = 90;
        preset.roundnessJitter = 0.6;
        preset.scatter = 1.2;
        preset.scatterBothAxes = true;
        preset.count = 4;
        preset.countJitter = 0.5;
        preset.smoothing = 0.7;
        preset.tiltShape = true;
        equalDabs(stroke(preset, 1), stroke(preset, 31));
    }
    void spacingCarriesRemainderAndStationaryEventsAddNoDabs() {
        BrushPreset preset;
        preset.size = 20;
        preset.spacing = 0.75;
        BrushDynamics dynamics;
        auto dabs = dynamics.begin(preset, {{0, 0}}, 1);
        QCOMPARE(dabs.size(), 1);
        for (int i = 0; i < 50; ++i)
            QVERIFY(dynamics.append({{0, 0}}).isEmpty());
        QVERIFY(dynamics.append({{7, 0}}).isEmpty());
        QVERIFY(dynamics.append({{14, 0}}).isEmpty());
        dabs = dynamics.append({{31, 0}});
        QCOMPARE(dabs.size(), 2);
        QCOMPARE(dabs[0].position, QPointF(15, 0));
        QCOMPARE(dabs[1].position, QPointF(30, 0));
        auto tail = dynamics.finish({{31, 0}});
        QCOMPARE(tail.size(), 1);
        QCOMPARE(tail.first().position, QPointF(31, 0));
        QVERIFY(!dynamics.active());
    }
    void pressureIsInterpolatedAtDabPositions() {
        BrushPreset preset;
        preset.size = 20;
        preset.spacing = 0.25;
        preset.opacity = 0.8;
        BrushDynamics dynamics;
        dynamics.begin(preset, {{0, 0}, 0.1}, 0);
        const auto dabs = dynamics.append({{20, 0}, 1});
        QCOMPARE(dabs.size(), 4);
        QVERIFY(std::abs(dabs[0].diameter - 6.5) < 1e-9);
        QVERIFY(std::abs(dabs[1].diameter - 11) < 1e-9);
        QVERIFY(std::abs(dabs[0].opacity - 0.26) < 1e-9);
        QCOMPARE(dabs.last().diameter, qreal(20));
        QCOMPARE(dabs.last().opacity, qreal(0.8));
    }
    void scatterAndJitterRemainWithinConfiguredBounds() {
        BrushPreset preset;
        preset.size = 40;
        preset.minimumSize = 0.2;
        preset.sizeJitter = 1;
        preset.opacity = 0.7;
        preset.opacityJitter = 0.8;
        preset.angleJitter = 90;
        preset.roundness = 0.8;
        preset.roundnessJitter = 0.5;
        preset.scatter = 2;
        preset.scatterBothAxes = true;
        preset.count = 16;
        preset.countJitter = 0.25;
        BrushDynamics dynamics;
        const auto dabs = dynamics.begin(preset, {{100, 100}}, 7);
        QVERIFY(dabs.size() >= 12 && dabs.size() <= 16);
        bool differentSizes = false, parallelScatter = false;
        for (const auto &dab : dabs) {
            QVERIFY(dab.diameter >= 8 && dab.diameter <= 40);
            QVERIFY(dab.opacity >= 0.14 - 1e-9 && dab.opacity <= 0.7);
            QVERIFY(dab.roundness >= 0.4 && dab.roundness <= 0.8);
            QVERIFY(std::abs(dab.angle) <= 90);
            QVERIFY(std::abs(dab.position.x() - 100) <= 40);
            QVERIFY(std::abs(dab.position.y() - 100) <= 40);
            differentSizes |= std::abs(dab.diameter - dabs.first().diameter) > 0.01;
            parallelScatter |= std::abs(dab.position.x() - 100) > 0.01;
        }
        QVERIFY(differentSizes);
        QVERIFY(parallelScatter);
    }
    void pressureTiltAndDirectionControlsAreFunctional() {
        BrushPreset preset;
        preset.size = 20;
        preset.opacity = 0.8;
        preset.angle = 22;
        preset.pressureSize = false;
        preset.pressureOpacity = false;
        preset.tiltShape = true;
        const QJsonObject original = preset.toJson();
        BrushDynamics dynamics;
        const auto dabs = dynamics.begin(preset, {{0, 0}, 0.1, {30, 0}}, 1);
        QCOMPARE(dabs.first().diameter, qreal(20));
        QCOMPARE(dabs.first().opacity, qreal(0.8));
        QCOMPARE(dabs.first().angle, qreal(22));
        QCOMPARE(dabs.first().roundness, qreal(0.75));
        QCOMPARE(preset.toJson(), original);
        preset.tiltShape = false;
        preset.angleFollowsStroke = true;
        dynamics.begin(preset, {{0, 0}}, 1);
        const auto vertical = dynamics.append({{0, 20}});
        QCOMPARE(vertical.first().angle, qreal(112));
    }
    void invalidSettingsNormalizeWithoutNaNs() {
        BrushPreset preset;
        preset.size = -20;
        preset.opacity = std::numeric_limits<qreal>::quiet_NaN();
        preset.spacing = -1;
        preset.roundness = 0;
        preset.scatter = 999;
        preset.count = 999;
        preset.angle = std::numeric_limits<qreal>::infinity();
        const BrushPreset safe = preset.normalized();
        QCOMPARE(safe.size, 1);
        QCOMPARE(safe.opacity, qreal(1));
        QCOMPARE(safe.spacing, qreal(0.01));
        QCOMPARE(safe.roundness, qreal(0.05));
        QCOMPARE(safe.scatter, qreal(4));
        QCOMPARE(safe.count, 16);
        QCOMPARE(safe.angle, qreal(0));
    }
    void presetLibraryRoundTripsAllDynamics() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QList<BrushPreset> source = builtinBrushPresets();
        const QString path = directory.filePath("brushes.sbrush.json");
        QString error;
        QVERIFY2(saveBrushPresets(path, source, &error), qPrintable(error));
        const auto loaded = loadBrushPresets(path, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(loaded.size(), source.size());
        for (int i = 0; i < source.size(); ++i)
            QCOMPARE(loaded[i].toJson(), source[i].toJson());
        QVERIFY(!saveBrushPresets(directory.filePath("empty.json"), {}, &error));
        QVERIFY(!error.isEmpty());
    }
    void presetReaderRejectsCorruptAndUnsupportedFiles() {
        QTemporaryDir directory;
        const QString path = directory.filePath("invalid.json");
        const auto write = [&](const QByteArray &bytes) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(bytes), qint64(bytes.size()));
        };
        QString error;
        write("not JSON or an ABR brush file");
        QVERIFY(loadBrushPresets(path, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        write("{\"format\":\"serika-brush-presets\",\"version\":2,\"presets\":[]}");
        QVERIFY(loadBrushPresets(path, &error).isEmpty());
        QJsonObject invalid = BrushPreset().toJson();
        invalid["spacing"] = -2;
        write(QJsonDocument(QJsonObject{{"format", "serika-brush-presets"},
                                        {"version", 1},
                                        {"presets", QJsonArray{invalid}}})
                  .toJson());
        QVERIFY(loadBrushPresets(path, &error).isEmpty());
        QVERIFY(error.contains("spacing"));
        write(QByteArray(2 * 1024 * 1024 + 1, ' '));
        QVERIFY(loadBrushPresets(path, &error).isEmpty());
        QVERIFY(error.contains("2 MiB"));
    }
    void canvasPixelsAreIndependentOfEventDensity() {
        std::unique_ptr<Document> first(Document::create({160, 128}, Qt::transparent));
        std::unique_ptr<Document> second(Document::create({160, 128}, Qt::transparent));
        CanvasView a(first.get()), b(second.get());
        initialize(a);
        initialize(b);
        BrushPreset preset;
        preset.size = 24;
        preset.hardness = 0.8;
        preset.opacity = 0.45;
        preset.flow = 0.4;
        preset.spacing = 0.25;
        preset.sizeJitter = 0.6;
        preset.opacityJitter = 0.4;
        preset.scatter = 0.5;
        preset.count = 3;
        preset.angleJitter = 70;
        preset.roundness = 0.6;
        preset.smoothing = 0.5;
        a.setBrushPreset(preset);
        b.setBrushPreset(preset);
        a.setForeground(Qt::red);
        b.setForeground(Qt::red);
        gesture(a, 1);
        gesture(b, 22);
        QCOMPARE(first->composite(), second->composite());
        QCOMPARE(first->historyNames().size(), 1);
        QCOMPARE(second->historyNames().size(), 1);
        first->undo();
        QCOMPARE(first->composite().pixelColor(64, 64).alpha(), 0);
        first->redo();
        QCOMPARE(first->composite(), second->composite());
    }
    void scatteredDabsRespectSelectionAndOpacityCap() {
        std::unique_ptr<Document> document(Document::create({160, 128}, Qt::transparent));
        QImage mask(document->state.size, QImage::Format_Grayscale8);
        mask.fill(0);
        for (int y = 40; y < 85; ++y)
            for (int x = 40; x < 90; ++x)
                mask.scanLine(y)[x] = 255;
        document->setSelection(mask);
        CanvasView canvas(document.get());
        initialize(canvas);
        BrushPreset preset;
        preset.size = 50;
        preset.opacity = 0.4;
        preset.flow = 0.8;
        preset.scatter = 1.5;
        preset.count = 5;
        preset.opacityJitter = 0.5;
        canvas.setBrushPreset(preset);
        gesture(canvas, 22);
        const QImage image = document->composite();
        bool painted = false;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const int alpha = image.pixelColor(x, y).alpha();
                QVERIFY(alpha <= 104);
                if (!mask.constScanLine(y)[x])
                    QCOMPARE(alpha, 0);
                painted |= alpha > 0;
            }
        QVERIFY(painted);
    }
    void cancelGestureRestoresPixelsAndStopsSampler() {
        std::unique_ptr<Document> document(Document::create({160, 128}, Qt::transparent));
        CanvasView canvas(document.get());
        initialize(canvas);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({50, 50}).toPoint());
        QVERIFY(document->composite().pixelColor(50, 50).alpha() > 0);
        canvas.cancelInteraction();
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier,
                            canvas.fromDocument({100, 50}).toPoint());
        QCOMPARE(document->composite().pixelColor(50, 50).alpha(), 0);
        QCOMPARE(document->historyNames().size(), 0);
    }
    void widgetControlsEmitRealPresetChangesWithoutFeedback() {
        BrushSettingsWidget widget;
        QSignalSpy spy(&widget, &BrushSettingsWidget::presetChanged);
        BrushPreset preset;
        preset.sizeJitter = 0.3;
        preset.scatter = 0.5;
        widget.setPreset(preset);
        QCOMPARE(spy.size(), 0);
        auto *jitter = widget.findChild<QDoubleSpinBox *>("sizeJitter");
        auto *pressure = widget.findChild<QCheckBox *>("pressureSize");
        QVERIFY(jitter);
        QVERIFY(pressure);
        QCOMPARE(jitter->value(), 30.0);
        jitter->setValue(75);
        QCOMPARE(widget.preset().sizeJitter, qreal(0.75));
        pressure->setChecked(false);
        QVERIFY(!widget.preset().pressureSize);
        QCOMPARE(spy.size(), 2);
        QCOMPARE(qvariant_cast<BrushPreset>(spy.last().first()).pressureSize, false);
    }
    void multiLayerDragFreezesSelectionAndHasOneUndo() {
        std::unique_ptr<Document> document(Document::create({160, 128}, Qt::transparent));
        const quint64 lower = document->activeLayer()->id;
        const quint64 upper = document->addLayer("Upper");
        setPixels(layer(*document, lower), Qt::red, {10, 10});
        setPixels(layer(*document, upper), Qt::blue, {60, 10});
        document->setActiveIndex(document->indexForId(lower));
        document->touch();
        document->clearHistory();
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setTool("Move");
        canvas.setProperty("autoSelect", false);
        selectLayers(canvas, {lower, upper});
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({20, 20}).toPoint());
        selectLayers(canvas, {lower}); // The roots are frozen for the whole drag.
        moveTo(canvas, {30, 25});
        moveTo(canvas, {45, 35});
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({45, 35}).toPoint());
        QCOMPARE(layer(*document, lower).offset, QPointF(35, 25));
        QCOMPARE(layer(*document, upper).offset, QPointF(85, 25));
        QCOMPARE(document->historyNames().size(), 1);
        document->undo();
        QCOMPARE(layer(*document, lower).offset, QPointF(10, 10));
        QCOMPARE(layer(*document, upper).offset, QPointF(60, 10));
        document->redo();
        selectLayers(canvas, {lower, upper});
        QTest::keyClick(&canvas, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(layer(*document, lower).offset, QPointF(45, 25));
        QCOMPARE(layer(*document, upper).offset, QPointF(95, 25));
        QCOMPARE(document->historyNames().size(), 2);
        document->undo();
        QCOMPARE(layer(*document, lower).offset, QPointF(35, 25));
        QCOMPARE(layer(*document, upper).offset, QPointF(85, 25));
    }
    void selectedGroupAndChildDragOnlyOnceAndRetainSibling() {
        std::unique_ptr<Document> document(Document::create({160, 128}, Qt::transparent));
        const quint64 group = document->addLayer("Group", LayerKind::Group);
        const quint64 child = document->addLayer("Child");
        const quint64 sibling = document->addLayer("Sibling");
        layer(*document, group).offset = {20, 20};
        layer(*document, child).parentId = group;
        // addLayer inherits the active layer's group; this sibling must be an independent root.
        layer(*document, sibling).parentId = 0;
        setPixels(layer(*document, child), Qt::red, {5, 5});
        setPixels(layer(*document, sibling), Qt::blue, {90, 10});
        QImage mask(16, 16, QImage::Format_Grayscale8);
        mask.fill(255);
        layer(*document, child).mask = mask;
        layer(*document, child).maskLinked = false;
        document->setActiveIndex(document->indexForId(sibling));
        document->touch();
        document->clearHistory();
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setTool("Move");
        selectLayers(canvas, {group, child, sibling});
        // Auto-selecting a descendant of a selected group keeps every selected root.
        gesture(canvas, 3, {28, 28}, {61, 40});
        QCOMPARE(document->activeLayer()->id, child);
        QCOMPARE(layer(*document, group).offset, QPointF(53, 32));
        QCOMPARE(layer(*document, child).offset, QPointF(5, 5));
        QCOMPARE(document->effectiveLayerOffset(layer(*document, child)), QPointF(58, 37));
        QCOMPARE(layer(*document, sibling).offset, QPointF(123, 22));
        QCOMPARE(layer(*document, child).maskOffset, QPointF(-33, -12));
        QCOMPARE(document->historyNames().size(), 1);
        document->undo();
        QCOMPARE(layer(*document, group).offset, QPointF(20, 20));
        QCOMPARE(layer(*document, child).offset, QPointF(5, 5));
        QCOMPARE(layer(*document, sibling).offset, QPointF(90, 10));
        QCOMPARE(layer(*document, child).maskOffset, QPointF());
    }
    void lockedMemberAbortsMoveAtomicallyAndCancelRestoresAllRoots() {
        std::unique_ptr<Document> document(Document::create({160, 128}, Qt::transparent));
        const quint64 first = document->activeLayer()->id;
        const quint64 second = document->addLayer("Locked sibling");
        setPixels(layer(*document, first), Qt::red, {10, 10});
        setPixels(layer(*document, second), Qt::blue, {60, 10});
        layer(*document, second).lockPosition = true;
        document->setActiveIndex(document->indexForId(first));
        document->touch();
        document->clearHistory();
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setTool("Move");
        canvas.setProperty("autoSelect", false);
        selectLayers(canvas, {first, second});
        QSignalSpy errors(&canvas, &CanvasView::interactionError);
        gesture(canvas, 3, {20, 20}, {40, 30});
        QCOMPARE(layer(*document, first).offset, QPointF(10, 10));
        QCOMPARE(layer(*document, second).offset, QPointF(60, 10));
        QCOMPARE(document->historyNames().size(), 0);
        QCOMPARE(errors.size(), 1);
        layer(*document, second).lockPosition = false;
        document->touch();
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({20, 20}).toPoint());
        moveTo(canvas, {40, 30});
        QCOMPARE(layer(*document, first).offset, QPointF(30, 20));
        canvas.cancelInteraction();
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.fromDocument({40, 30}).toPoint());
        QCOMPARE(layer(*document, first).offset, QPointF(10, 10));
        QCOMPARE(layer(*document, second).offset, QPointF(60, 10));
        QCOMPARE(document->historyNames().size(), 0);
    }
    void targetedUnlinkedMaskMovesWithoutLayerContent() {
        std::unique_ptr<Document> document(Document::create({160, 128}, Qt::transparent));
        const quint64 id = document->activeLayer()->id;
        setPixels(layer(*document, id), Qt::red, {10, 10});
        QImage mask(16, 16, QImage::Format_Grayscale8);
        mask.fill(255);
        layer(*document, id).mask = mask;
        layer(*document, id).maskTarget = true;
        layer(*document, id).maskLinked = false;
        layer(*document, id).maskOffset = {3, 4};
        document->touch();
        document->clearHistory();
        CanvasView canvas(document.get());
        initialize(canvas);
        canvas.setTool("Move");
        canvas.setProperty("autoSelect", false);
        gesture(canvas, 2, {20, 20}, {30, 25});
        QCOMPARE(layer(*document, id).offset, QPointF(10, 10));
        QCOMPARE(layer(*document, id).maskOffset, QPointF(13, 9));
        QCOMPARE(document->historyNames().size(), 1);
        canvas.nudgeSelectedLayers({2, -1});
        QCOMPARE(layer(*document, id).offset, QPointF(10, 10));
        QCOMPARE(layer(*document, id).maskOffset, QPointF(15, 8));
        QCOMPARE(document->historyNames().size(), 2);
        document->undo();
        QCOMPARE(layer(*document, id).maskOffset, QPointF(13, 9));
        document->undo();
        QCOMPARE(layer(*document, id).maskOffset, QPointF(3, 4));
    }
    void mainWindowSynchronizesBrushPresetAndReusesItForNewDocuments() {
        MainWindow window;
        window.resize(1440, 900);
        window.show();
        QCoreApplication::processEvents();
        auto *settings = window.findChild<BrushSettingsWidget *>("brushSettings");
        auto *options = window.findChild<QToolBar *>("options");
        QVERIFY(settings);
        QVERIFY(options);
        auto *jitter = settings->findChild<QDoubleSpinBox *>("sizeJitter");
        auto *scatter = settings->findChild<QDoubleSpinBox *>("scatter");
        auto *size = settings->findChild<QSpinBox *>("brushPresetSize");
        auto *toolbarSize = options->findChild<QSpinBox *>("brushSize");
        auto *toolbarOpacity = options->findChild<QSpinBox *>("brushOpacity");
        QVERIFY(jitter && scatter && size && toolbarSize && toolbarOpacity);
        jitter->setValue(65);
        scatter->setValue(125);
        size->setValue(37);
        QCOMPARE(toolbarSize->value(), 37);
        QVERIFY(newSmallDocument(window));
        CanvasView *first = window.currentCanvas();
        QVERIFY(first);
        QCOMPARE(first->brushPreset().sizeJitter, qreal(0.65));
        QCOMPARE(first->brushPreset().scatter, qreal(1.25));
        QCOMPARE(first->brushSize(), 37);
        auto *brush = window.findChild<QToolButton *>("tool_Brush");
        QVERIFY(brush);
        brush->click();
        first->setBrushSize(53);
        QCOMPARE(size->value(), 53);
        QCOMPARE(toolbarSize->value(), 53);
        toolbarOpacity->setValue(43);
        QCOMPARE(first->brushOpacity(), qreal(0.43));
        QCOMPARE(settings->preset().opacity, qreal(0.43));
        QCOMPARE(first->brushPreset().sizeJitter, qreal(0.65));
        const QJsonObject current = settings->preset().toJson();
        QVERIFY(newSmallDocument(window));
        QVERIFY(window.currentCanvas() != first);
        QCOMPARE(window.currentCanvas()->brushPreset().toJson(), current);
        QCOMPARE(settings->preset().toJson(), current);
        auto *tabs = window.findChild<QTabWidget *>("documentTabs");
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 2);
    }
};
QTEST_MAIN(BrushTests)
#include "brush_tests.moc"
