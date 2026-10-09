#include "document/FillRenderer.h"
#include "io/FormatIO.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/FillEditorDialog.h"
#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <memory>
using namespace serika;
class FillTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary;
    static QJsonObject colors() {
        return {{"stops", QJsonArray{QJsonObject{{"position", 0}, {"color", "#ff0000"}},
                                     QJsonObject{{"position", .5}, {"color", "#00ff00"}},
                                     QJsonObject{{"position", 1}, {"color", "#0000ff"}}}}};
    }
    static QJsonObject pattern() {
        QImage tile(2, 2, QImage::Format_RGBA8888);
        tile.fill(Qt::red);
        tile.setPixelColor(1, 0, Qt::blue);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        tile.save(&buffer, "PNG");
        return {{"patternPng", QString::fromLatin1(bytes.toBase64())}};
    }
  private slots:
    void initTestCase() {
        QVERIFY(temporary.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    }
    void gradientMapUsesIntermediateStopsAndAlpha() {
        QImage input(1, 1, QImage::Format_RGBA64);
        input.fill(QColor::fromRgbF(.5, .5, .5, .5));
        auto p = colors();
        auto stops = p["stops"].toArray();
        auto middle = stops[1].toObject();
        middle["color"] = "#8000ff00";
        stops[1] = middle;
        p["stops"] = stops;
        const auto mapped = applyAdjustment(input, "Gradient Map", p);
        QCOMPARE(mapped.format(), QImage::Format_RGBA64);
        QVERIFY(mapped.pixelColor(0, 0).green() > 253);
        QVERIFY(mapped.pixelColor(0, 0).red() < 2);
        QVERIFY(std::abs(mapped.pixelColor(0, 0).alpha() - 64) <= 1);
        p["reverse"] = true;
        QCOMPARE(sampleGradient(gradientStops(p), 0), QColor(Qt::blue));
        QCOMPARE(sampleGradient(gradientStops({{"startColor", "#ff0000"}, {"endColor", "#ffffff"}}), 0),
                 QColor(Qt::red));
    }
    void gradientStylesAndNativeDepths() {
        for (const auto &style : QStringList{"Linear", "Radial", "Angle", "Reflected", "Diamond"})
            for (int depth : {8, 16, 32}) {
                auto p = colors();
                p["style"] = style;
                auto image = renderGradientFill({65, 49}, depth, p);
                QVERIFY(!image.isNull());
                QCOMPARE(image.format(), depth == 32   ? QImage::Format_RGBA32FPx4
                                         : depth == 16 ? QImage::Format_RGBA64
                                                       : QImage::Format_RGBA8888);
                QVERIFY(image.pixelColor(3, 4) != image.pixelColor(32, 24));
                if (style == "Radial" || style == "Diamond" || style == "Reflected")
                    QVERIFY(image.pixelColor(32, 24).red() > 245);
            }
        auto p = colors();
        const auto a = renderGradientFill({80, 30}, 16, p);
        p["reverse"] = true;
        const auto b = renderGradientFill({80, 30}, 16, p);
        QVERIFY(a.pixelColor(0, 15).red() > 240);
        QVERIFY(b.pixelColor(0, 15).blue() > 240);
    }
    void patternTilesAndRoundTrips() {
        std::unique_ptr<Document> document(Document::create({12, 8}, Qt::transparent, 16));
        document->addLayer("Pattern", LayerKind::PatternFill);
        document->activeLayer()->parameters = pattern();
        document->touch();
        auto image = document->composite();
        QCOMPARE(image.pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(image.pixelColor(1, 0), QColor(Qt::blue));
        QCOMPARE(image.pixelColor(0, 0), image.pixelColor(2, 0));
        QString error;
        for (const auto &suffix : QStringList{"spe", "psd"}) {
            const auto path = temporary.filePath("pattern." + suffix);
            QVERIFY2(FormatIO::save(document.get(), path, &error), qPrintable(error));
            std::unique_ptr<Document> loaded(FormatIO::open(path, &error));
            QVERIFY2(loaded, qPrintable(error));
            QCOMPARE(loaded->activeLayer()->kind, LayerKind::PatternFill);
            QCOMPARE(loaded->activeLayer()->parameters, document->activeLayer()->parameters);
            QCOMPARE(loaded->composite().pixelColor(1, 0), QColor(Qt::blue));
        }
    }
    void reflectedGradientClampsOutsideItsScaledRamp() {
        auto p = colors();
        p["style"] = "Reflected";
        p["scale"] = 20;
        const auto image = renderGradientFill({101, 11}, 16, p);
        for (int x : {0, 10, 20, 30, 70, 80, 90, 100})
            QCOMPARE(image.pixelColor(x, 5), QColor(Qt::blue));
        QVERIFY(image.pixelColor(50, 5).red() > 245);
    }
    void legacyFillAcceptPreservesItsOriginalColors() {
        std::unique_ptr<Document> fixture(Document::create({32, 16}, Qt::transparent, 16));
        fixture->addLayer("Legacy gradient", LayerKind::GradientFill);
        fixture->activeLayer()->color = Qt::red;
        fixture->touch();
        QString error;
        const auto path = temporary.filePath("legacy-fill.spe");
        QVERIFY2(FormatIO::saveNative(fixture.get(), path, &error), qPrintable(error));
        MainWindow window;
        window.openFile(path);
        auto *document = window.currentDocument();
        QVERIFY(document);
        const auto before = document->composite();
        QTimer::singleShot(0, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                dialog->accept();
        });
        window.runCommand("Edit Fill...");
        QCOMPARE(document->composite(), before);
        document->markSaved();
    }
    void editorControlsAndCancelAreNonDestructive() {
        FillEditorDialog editor(LayerKind::GradientFill, colors(), Qt::black, Qt::white);
        auto *list = editor.findChild<QListWidget *>("gradientStops");
        QVERIFY(list);
        list->setCurrentRow(1);
        editor.findChild<QDoubleSpinBox *>("gradientStopOpacity")->setValue(25);
        auto stops = gradientStops(editor.parameters());
        QVERIFY(std::abs(stops[1].second.alphaF() - .25) < .01);
        editor.findChild<QComboBox *>("gradientStyle")->setCurrentText("Diamond");
        QCOMPARE(editor.parameters()["style"].toString(), QString("Diamond"));
        editor.findChild<QPushButton *>("gradientAddStop")->click();
        QCOMPARE(list->count(), 4);
        editor.findChild<QPushButton *>("gradientRemoveStop")->click();
        QCOMPARE(list->count(), 3);
        MainWindow window;
        std::unique_ptr<Document> fixture(Document::create({12, 8}, Qt::transparent));
        QString error;
        const auto path = temporary.filePath("ui.spe");
        QVERIFY(FormatIO::saveNative(fixture.get(), path, &error));
        window.openFile(path);
        auto *document = window.currentDocument();
        QVERIFY(document);
        const int count = document->state.layers.size();
        QTimer::singleShot(0, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        window.runCommand("Pattern Fill...");
        QCOMPARE(document->state.layers.size(), count);
        QVERIFY(!document->canUndo());
        QTimer::singleShot(0, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                dialog->accept();
        });
        window.runCommand("Pattern Fill...");
        QCOMPARE(document->activeLayer()->kind, LayerKind::PatternFill);
        QCOMPARE(document->state.layers.size(), count + 1);
        document->undo();
        QCOMPARE(document->state.layers.size(), count);
        document->redo();
        QTimer::singleShot(0, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                dialog->findChild<QDoubleSpinBox *>("fillScale")->setValue(200);
                dialog->accept();
            }
        });
        window.runCommand("Edit Fill...");
        QCOMPARE(document->activeLayer()->parameters["scale"].toInt(), 200);
        document->undo();
        QCOMPARE(document->activeLayer()->parameters["scale"].toInt(), 100);
        document->markSaved();
    }
};
QTEST_MAIN(FillTests)
#include "fills_tests.moc"
