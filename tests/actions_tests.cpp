#include "actions/ActionRunner.h"
#include "document/TransformOperations.h"
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
    static std::unique_ptr<Document> transformedSmartObject() {
        std::unique_ptr<Document> document(Document::create({16, 16}, Qt::transparent, 16));
        QImage source({4, 3}, QImage::Format_RGBA64);
        source.fill(QColor(30, 60, 90));
        document->activeLayer()->pixels.setImage(source);
        document->activeLayer()->offset = {2, 3};
        document->convertToSmartObject();
        document->addSmartFilter("Invert");
        QTransform transform;
        transform.scale(2, 2);
        if (!applyLayerTransform(document.get(), transform))
            return {};
        document->clearHistory();
        document->markSaved();
        return document;
    }
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
    void groupedUnlinkedMaskFillAndClearRetainFloatSelectionPrecision() {
        std::unique_ptr<Document> document(Document::create({20, 16}, Qt::transparent, 32));
        document->state.layers.clear();
        const quint64 group = document->addLayer("Offset group", LayerKind::Group);
        document->activeLayer()->offset = {4, 3};
        document->addLayer("Masked child");
        auto *layer = document->activeLayer();
        layer->parentId = group;
        layer->offset = {2, 1};
        layer->maskOffset = {3, 2};
        layer->maskLinked = false;
        layer->maskTarget = true;
        const float oldValue = .12345679f;
        layer->mask = makeMask({4, 3}, 32, oldValue);
        const TileImage originalPixels = layer->pixels;
        QImage selection = makeMask(document->state.size, 32);
        const float selected = .33333334f;
        // Effective pixel origin (6,4) plus the independent mask offset (3,2).
        setMaskSample(selection, 10, 7, selected);
        document->setSelection(selection);
        document->clearHistory();
        QString error;
        QVERIFY2(ActionRunner::execute(
                     document.get(), {{"command", "fill"}, {"parameters", QJsonObject{{"color", "#ffffff"}}}},
                     &error),
                 qPrintable(error));
        layer = document->activeLayer();
        QCOMPARE(layer->mask.format(), QImage::Format_RGBA32FPx4);
        const qreal expected = oldValue * (1 - selected) + selected;
        QVERIFY(std::abs(maskSample(layer->mask, 1, 1) - expected) < 1e-7);
        QCOMPARE(maskSample(layer->mask, 0, 0), qreal(oldValue));
        QCOMPARE(layer->pixels.image(), originalPixels.image());
        QVERIFY(ActionRunner::execute(document.get(), {{"command", "clear"}}, &error));
        layer = document->activeLayer();
        QVERIFY(std::abs(maskSample(layer->mask, 1, 1) - expected * (1 - selected)) < 1e-7);
        QCOMPARE(maskSample(layer->mask, 0, 0), qreal(oldValue));
        document->undo();
        QVERIFY(std::abs(maskSample(document->activeLayer()->mask, 1, 1) - expected) < 1e-7);
    }
    void maskAdjustmentTargetsMaskWithoutRasterizingSmartContent() {
        auto document = transformedSmartObject();
        QVERIFY(document);
        auto *layer = document->activeLayer();
        const QImage originalPixels = layer->pixels.image();
        const QJsonArray originalFilters = layer->smartFilters;
        const QJsonObject originalParameters = layer->parameters;
        layer->maskTarget = true;
        layer->mask = makeMask({8, 6}, 16, .123456);
        const qreal originalCoverage = maskSample(layer->mask, 0, 0);
        QImage selection = makeMask(document->state.size, 16);
        setMaskSample(selection, 3, 4, 1);
        document->setSelection(selection);
        QString error;
        QVERIFY2(ActionRunner::execute(document.get(),
                                       {{"command", "adjustment"},
                                        {"name", "Invert"},
                                        {"parameters", QJsonObject{{"destructive", true}}}},
                                       &error),
                 qPrintable(error));
        layer = document->activeLayer();
        QCOMPARE(layer->kind, LayerKind::SmartObject);
        QCOMPARE(layer->pixels.image(), originalPixels);
        QCOMPARE(layer->smartFilters, originalFilters);
        QCOMPARE(layer->parameters, originalParameters);
        QCOMPARE(layer->mask.format(), QImage::Format_Grayscale16);
        QVERIFY(std::abs(maskSample(layer->mask, 1, 1) - (1 - originalCoverage)) < 2. / 65535);
        QCOMPARE(maskSample(layer->mask, 0, 0), originalCoverage);
    }
    void rasterizingAdjustmentBakesTransformAndFiltersExactlyOnceAndUndoes() {
        auto document = transformedSmartObject();
        QVERIFY(document);
        const QImage before = document->layerImage(*document->activeLayer());
        const QImage expected = applyAdjustment(before, "Invert", {});
        QCOMPARE(before.size(), QSize(8, 6));
        QString error;
        QVERIFY2(ActionRunner::execute(document.get(),
                                       {{"command", "adjustment"},
                                        {"name", "Invert"},
                                        {"parameters", QJsonObject{{"destructive", true}}}},
                                       &error),
                 qPrintable(error));
        const auto *layer = document->activeLayer();
        QCOMPARE(layer->kind, LayerKind::Pixel);
        QVERIFY(layer->smartFilters.isEmpty());
        QVERIFY(!layer->parameters.contains("contentTransform"));
        QCOMPARE(document->layerImage(*layer), expected);
        document->undo();
        QCOMPARE(document->activeLayer()->kind, LayerKind::SmartObject);
        QCOMPARE(document->activeLayer()->smartFilters.size(), 1);
        QVERIFY(document->activeLayer()->parameters.contains("contentTransform"));
        QCOMPARE(document->layerImage(*document->activeLayer()), before);
    }
    void destructiveFillClearAndFilterBakeSmartObjectWithoutDoubleTransform() {
        for (const QString &command : QStringList{"fill", "clear", "filter"}) {
            auto document = transformedSmartObject();
            QVERIFY(document);
            const QImage before = document->layerImage(*document->activeLayer());
            const QImage expected = applyFilter(before, "Gaussian Blur", {{"radius", 0}});
            QJsonObject step{
                {"command", command},
                {"name", "Gaussian Blur"},
                {"parameters", QJsonObject{{"destructive", true}, {"radius", 0}, {"color", "#0000ff"}}}};
            QString error;
            QVERIFY2(ActionRunner::execute(document.get(), step, &error), qPrintable(error));
            const auto *layer = document->activeLayer();
            QCOMPARE(layer->kind, LayerKind::Pixel);
            QVERIFY(layer->smartFilters.isEmpty());
            QVERIFY(!layer->parameters.contains("contentTransform"));
            QCOMPARE(layer->pixels.size, before.size());
            if (command == "fill")
                QCOMPARE(document->layerImage(*layer).pixelColor(3, 2), QColor(Qt::blue));
            else if (command == "clear")
                QCOMPARE(document->layerImage(*layer).pixelColor(3, 2).alpha(), 0);
            else
                QCOMPARE(document->layerImage(*layer), expected);
            document->undo();
            QCOMPARE(document->layerImage(*document->activeLayer()), before);
        }
    }
};
QTEST_MAIN(ActionTests)
#include "actions_tests.moc"
