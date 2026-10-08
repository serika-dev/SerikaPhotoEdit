#include "actions/ActionRunner.h"
#include "io/FormatIO.h"
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <memory>

using namespace serika;
class ActionTests : public QObject {
    Q_OBJECT
  private slots:
    void adjustmentRetainsParameters() {
        std::unique_ptr<Document> d(Document::create({7, 5}, QColor(30, 60, 90), 16));
        QString error;
        QVERIFY2(
            ActionRunner::execute(d.get(),
                                  {{"command", "adjustment"},
                                   {"name", "Exposure"},
                                   {"parameters", QJsonObject{{"exposure", 1.0}, {"destructive", true}}}},
                                  &error),
            qPrintable(error));
        QVERIFY(d->composite().pixelColor(3, 2).red() > 30);
        QCOMPARE(d->state.layers.size(), 1);
        QVERIFY(ActionRunner::execute(d.get(),
                                      {{"command", "adjustment"},
                                       {"name", "Invert"},
                                       {"parameters", QJsonObject{{"destructive", false}}}},
                                      &error));
        QCOMPARE(d->state.layers.size(), 2);
        QCOMPARE(d->activeLayer()->kind, LayerKind::Adjustment);
        QCOMPARE(d->activeLayer()->adjustment, QString("Invert"));
    }
    void selectionLimitsFill() {
        std::unique_ptr<Document> d(Document::create({8, 6}, Qt::red));
        QString error;
        QVERIFY(ActionRunner::execute(
            d.get(),
            {{"command", "selection"},
             {"name", "rectangle"},
             {"parameters", QJsonObject{{"x", 2}, {"y", 2}, {"width", 3}, {"height", 2}}}},
            &error));
        QVERIFY(ActionRunner::execute(
            d.get(), {{"command", "fill"}, {"parameters", QJsonObject{{"color", "#0000ff"}}}}, &error));
        auto image = d->composite();
        QCOMPARE(image.pixelColor(3, 3), QColor(Qt::blue));
        QCOMPARE(image.pixelColor(0, 0), QColor(Qt::red));
    }
    void failureRollsBack() {
        std::unique_ptr<Document> d(Document::create({11, 9}, Qt::green));
        auto before = d->composite();
        QString error;
        QJsonArray steps = {QJsonObject{{"command", "Duplicate Layer"}},
                            QJsonObject{{"command", "filter"}, {"name", "Does Not Exist"}}};
        QVERIFY(!ActionRunner::run(d.get(), steps, &error));
        QVERIFY(error.contains("Step 2"));
        QCOMPARE(d->state.layers.size(), 1);
        QCOMPARE(d->composite(), before);
        QVERIFY(!d->canUndo());
        QVERIFY(!d->isModified());
    }
    void transformParameters() {
        std::unique_ptr<Document> d(Document::create({12, 8}, Qt::yellow));
        QString error;
        QVERIFY2(ActionRunner::execute(
                     d.get(),
                     {{"command", "transform"}, {"parameters", QJsonObject{{"scale", 50}, {"angle", 90}}}},
                     &error),
                 qPrintable(error));
        QCOMPARE(d->activeLayer()->pixels.size, QSize(4, 6));
        auto before = d->activeLayer()->pixels.image();
        QVERIFY(!ActionRunner::execute(
            d.get(), {{"command", "transform"}, {"parameters", QJsonObject{{"scaleX", 0}}}}, &error));
        QCOMPARE(d->activeLayer()->pixels.image(), before);
    }
    void headlessFile() {
        QTemporaryDir dir;
        std::unique_ptr<Document> d(Document::create({10, 7}, QColor(12, 34, 56)));
        QString error;
        QString input = dir.filePath("input.spe"), output = dir.filePath("output.png"),
                action = dir.filePath("invert.speaction");
        QVERIFY(FormatIO::saveNative(d.get(), input, &error));
        QJsonObject json{
            {"version", 1},
            {"steps", QJsonArray{QJsonObject{{"command", "adjustment"},
                                             {"name", "Invert"},
                                             {"parameters", QJsonObject{{"destructive", true}}}}}}};
        QFile file(action);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(json).toJson());
        file.close();
        QVERIFY2(ActionRunner::runFile(action, input, output, &error), qPrintable(error));
        std::unique_ptr<Document> result(FormatIO::open(output, &error));
        QVERIFY2(result, qPrintable(error));
        QCOMPARE(result->composite().pixelColor(5, 3), QColor(243, 221, 199));
    }
    void malformedAction() {
        QTemporaryDir dir;
        QFile file(dir.filePath("bad.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"version\":1,\"steps\":[");
        file.close();
        QJsonArray steps;
        QString error;
        QVERIFY(!ActionRunner::load(file.fileName(), &steps, &error));
        QVERIFY(!error.isEmpty());
    }
    void fillTransparentKeepsStraightColour() {
        std::unique_ptr<Document> d(Document::create({3, 3}, Qt::transparent, 16));
        QString error;
        QVERIFY(ActionRunner::execute(
            d.get(),
            {{"command", "fill"}, {"parameters", QJsonObject{{"color", "#ff0000"}, {"opacity", .5}}}},
            &error));
        const auto colour = d->activeLayer()->pixels.image().pixelColor(1, 1);
        QCOMPARE(colour.red(), 255);
        QVERIFY(std::abs(colour.alphaF() - .5) < .001);
    }
};
QTEST_MAIN(ActionTests)
#include "actions_tests.moc"
