#include "document/Document.h"
#include "io/FormatIO.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/CurvesEditor.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QJsonArray>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <memory>
using namespace serika;
class CurvesTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary;
  private slots:
    void initTestCase() {
        QVERIFY(temporary.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    }
    void channelEditingChangesOnlyTheChosenChannel() {
        CurvesEditor editor;
        editor.setParameters(
            {{"redPoints", QJsonArray{QJsonArray{0, 0}, QJsonArray{128, 128}, QJsonArray{255, 255}}}});
        editor.findChild<QComboBox *>("curveChannel")->setCurrentIndex(1);
        QSignalSpy changed(&editor, &CurvesEditor::parametersChanged);
        editor.findChild<QSpinBox *>("curveOutput")->setValue(64);
        QCOMPARE(changed.count(), 1);
        QImage input(1, 1, QImage::Format_RGBA64);
        input.fill(QColor(0, 0, 0, 127));
        const auto result = applyAdjustment(input, "Curves", editor.parameters());
        QCOMPARE(result.format(), QImage::Format_RGBA64);
        QVERIFY(result.pixelColor(0, 0).red() >= 63);
        QCOMPARE(result.pixelColor(0, 0).green(), 0);
        QCOMPARE(result.pixelColor(0, 0).blue(), 0);
        QCOMPARE(result.pixelColor(0, 0).alpha(), 127);
    }
    void clickAddKeyboardDeleteAndEndpoints() {
        CurvesEditor editor;
        editor.resize(440, 420);
        editor.show();
        QTest::qWait(10);
        auto *graph = editor.findChild<QWidget *>("curveGraph");
        QTest::mouseClick(graph, Qt::LeftButton, Qt::NoModifier, graph->rect().center());
        QCOMPARE(editor.parameters()["points"].toArray().size(), 3);
        editor.findChild<QSpinBox *>("curveOutput")->setValue(220);
        QCOMPARE(editor.parameters()["points"].toArray()[1].toArray()[1].toInt(), 220);
        QTest::keyClick(graph, Qt::Key_Delete);
        QCOMPARE(editor.parameters()["points"].toArray().size(), 2);
        QTest::keyClick(graph, Qt::Key_Delete);
        QCOMPARE(editor.parameters()["points"].toArray().size(), 2);
    }
    void savedPointsSurviveChannelSwitchAndMalformedInput() {
        CurvesEditor editor;
        QJsonArray points{QJsonArray{255, 240}, QJsonArray{64, 90}, QJsonArray{0, 20}, QJsonArray{64, 100},
                          QJsonArray{"invalid", 2}};
        editor.setParameters(
            {{"points", points}, {"bluePoints", QJsonArray{QJsonArray{0, 12}, QJsonArray{255, 200}}}});
        const auto before = editor.parameters();
        editor.findChild<QComboBox *>("curveChannel")->setCurrentIndex(3);
        editor.findChild<QComboBox *>("curveChannel")->setCurrentIndex(0);
        QCOMPARE(editor.parameters(), before);
        QCOMPARE(before["points"].toArray().size(), 3);
        QCOMPARE(before["points"].toArray()[1].toArray()[1].toInt(), 100);
        QCOMPARE(before["bluePoints"].toArray()[0].toArray()[1].toInt(), 12);
    }
    void openingCurvesWithoutEndpointsRetainsExactOutputAndFractionalInputs() {
        CurvesEditor editor;
        const QJsonObject parameters{{"points", QJsonArray{QJsonArray{64.5, 100}, QJsonArray{192.25, 220}}}};
        QImage input(256, 1, QImage::Format_RGBA64);
        for (int x = 0; x < input.width(); ++x)
            input.setPixelColor(x, 0, QColor::fromRgbF(x / 255., x / 255., x / 255., .6));
        const auto expected = applyAdjustment(input, "Curves", parameters);
        editor.setParameters(parameters);
        QCOMPARE(editor.parameters()["points"], parameters["points"]);
        QCOMPARE(applyAdjustment(input, "Curves", editor.parameters()), expected);
        editor.findChild<QSpinBox *>("curveOutput")->setValue(140);
        QCOMPARE(editor.parameters()["points"].toArray()[0].toArray()[0].toDouble(), 64.5);
        QCOMPARE(editor.parameters()["points"].toArray()[0].toArray()[1].toInt(), 140);
    }
    void graphCanAddBeyondExistingEndpointAndEditEndpointInput() {
        CurvesEditor editor;
        editor.setParameters({{"points", QJsonArray{QJsonArray{40, 80}, QJsonArray{180, 210}}}});
        editor.resize(440, 420);
        editor.show();
        QTest::qWait(10);
        auto *graph = editor.findChild<QWidget *>("curveGraph");
        QVERIFY(graph);
        QTest::mouseClick(graph, Qt::LeftButton, Qt::NoModifier,
                          QPoint(graph->width() - 25, graph->height() / 2));
        QCOMPARE(editor.parameters()["points"].toArray().size(), 3);
        QVERIFY(editor.parameters()["points"].toArray().last().toArray()[0].toDouble() > 180);
        editor.findChild<QSpinBox *>("curveInput")->setValue(250);
        QCOMPARE(editor.parameters()["points"].toArray().last().toArray()[0].toInt(), 250);
    }
    void hugeFiniteInputCoordinatesAreClampedBeforeIntegerConversion() {
        CurvesEditor editor;
        editor.setParameters({{"points", QJsonArray{QJsonArray{-1e100, -1e100}, QJsonArray{1e100, 1e100}}}});
        QCOMPARE(editor.parameters()["points"].toArray(),
                 QJsonArray({QJsonArray{0, 0}, QJsonArray{255, 255}}));
    }
    void lockedAdjustmentCannotOpenEditor_data() {
        QTest::addColumn<bool>("ancestor");
        QTest::newRow("layer lock") << false;
        QTest::newRow("parent group lock") << true;
    }
    void lockedAdjustmentCannotOpenEditor() {
        QFETCH(bool, ancestor);
        std::unique_ptr<Document> fixture(Document::create({16, 8}, Qt::transparent, 16));
        const QJsonObject original{
            {"points", QJsonArray{QJsonArray{0, 0}, QJsonArray{128, 180}, QJsonArray{255, 255}}}};
        fixture->addAdjustment("Curves", original);
        const quint64 adjustment = fixture->activeLayer()->id;
        fixture->addLayer("Locked parent", LayerKind::Group);
        const quint64 parent = fixture->activeLayer()->id;
        fixture->activeLayer()->locked = ancestor;
        auto &layer = fixture->state.layers[fixture->indexForId(adjustment)];
        layer.parentId = parent;
        layer.locked = !ancestor;
        fixture->setActiveIndex(fixture->indexForId(adjustment));
        const auto path = temporary.filePath(ancestor ? "ancestor.spe" : "layer.spe");
        QString error;
        QVERIFY2(FormatIO::saveNative(fixture.get(), path, &error), qPrintable(error));
        MainWindow window;
        window.openFile(path);
        auto *document = window.currentDocument();
        QVERIFY(document);
        bool opened = false;
        QTimer::singleShot(0, &window, [&] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                opened = true;
                dialog->reject();
            }
        });
        window.runCommand("Adjustment: Curves");
        QCoreApplication::processEvents();
        QVERIFY(!opened);
        QCOMPARE(document->activeLayer()->parameters, original);
        QVERIFY(!document->canUndo());
        QVERIFY(!document->isModified());
    }
};
QTEST_MAIN(CurvesTests)
#include "curves_tests.moc"
