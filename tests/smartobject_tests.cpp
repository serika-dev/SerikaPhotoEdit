#include "actions/ActionRunner.h"
#include "document/SmartObjectOperations.h"
#include "document/TransformOperations.h"
#include "io/EmbeddedDocument.h"
#include "io/FormatIO.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtEndian>
#include <QtTest>
#include <memory>
using namespace serika;
namespace {
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool imageFile(const QString &path, QSize size, QColor color) {
    QImage image(size, QImage::Format_RGBA64);
    image.fill(color);
    return image.save(path);
}
quint32 crc(const QByteArray &bytes) {
    quint32 value = 0xffffffffu;
    for (const auto c : bytes) {
        value ^= quint8(c);
        for (int bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
    }
    return value ^ 0xffffffffu;
}
void unchangedProperties(const Layer &a, const Layer &b) {
    QCOMPARE(a.id, b.id);
    QCOMPARE(a.name, b.name);
    QCOMPARE(a.offset, b.offset);
    QCOMPARE(a.parentId, b.parentId);
    QCOMPARE(a.opacity, b.opacity);
    QCOMPARE(a.fill, b.fill);
    QCOMPARE(a.blendMode, b.blendMode);
    QCOMPARE(a.smartFilters, b.smartFilters);
    QCOMPARE(a.mask, b.mask);
    QCOMPARE(a.maskEnabled, b.maskEnabled);
    QCOMPARE(a.maskTarget, b.maskTarget);
    QCOMPARE(a.maskDensity, b.maskDensity);
    QCOMPARE(a.maskFeather, b.maskFeather);
    QCOMPARE(a.maskLinked, b.maskLinked);
    QCOMPARE(a.maskOffset, b.maskOffset);
    QCOMPARE(a.vectorMask, b.vectorMask);
    QCOMPARE(a.vectorMaskLinked, b.vectorMaskLinked);
    QCOMPARE(a.vectorMaskOffset, b.vectorMaskOffset);
    QCOMPARE(a.vectorMaskDensity, b.vectorMaskDensity);
    QCOMPARE(a.vectorMaskFeather, b.vectorMaskFeather);
    QCOMPARE(a.effects, b.effects);
    QCOMPARE(a.lockAlpha, b.lockAlpha);
    QCOMPARE(a.lockPosition, b.lockPosition);
    QCOMPARE(a.clipped, b.clipped);
}
} // namespace
class SmartObjectTests : public QObject {
    Q_OBJECT
    QTemporaryDir settings;
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    }
    void embeddedTextContentsStayEditable() {
        std::unique_ptr<Document> parent(Document::create({64, 48}, Qt::transparent, 16));
        parent->addLayer("Editable Type", LayerKind::Text);
        auto *layer = parent->activeLayer();
        const auto id = layer->id;
        layer->text = "Original";
        layer->font.setPixelSize(18);
        layer->color = Qt::red;
        layer->offset = {4, 5};
        layer->opacity = .6;
        layer->mask = makeMask({64, 48}, 16, .7);
        const auto beforeMask = layer->mask;
        QString error;
        QVERIFY2(convertLayerToEmbeddedSmartObject(parent.get(), id, &error), qPrintable(error));
        QVERIFY(io::validEmbeddedDocument(parent->activeLayer()->embeddedDocument));
        std::unique_ptr<Document> contents(openSmartObjectContents(parent.get(), id, &error));
        QVERIFY2(contents, qPrintable(error));
        QCOMPARE(contents->state.layers.size(), 1);
        QCOMPARE(contents->activeLayer()->kind, LayerKind::Text);
        QCOMPARE(contents->activeLayer()->text, QString("Original"));
        QCOMPARE(contents->activeLayer()->offset, QPointF());
        QVERIFY(contents->activeLayer()->mask.isNull());
        QCOMPARE(contents->activeLayer()->opacity, 1.);
        contents->mutate("Edit type", [&] { contents->activeLayer()->text = "Edited"; });
        parent->clearHistory();
        const auto previous = parent->activeLayer()->embeddedDocument;
        QVERIFY2(applySmartObjectContents(parent.get(), id, contents.get(), &error), qPrintable(error));
        QCOMPARE(parent->activeLayer()->offset, QPointF(4, 5));
        QCOMPARE(parent->activeLayer()->opacity, .6);
        QCOMPARE(parent->activeLayer()->mask, beforeMask);
        QCOMPARE(parent->historyNames().size(), 1);
        parent->undo();
        QCOMPARE(parent->activeLayer()->embeddedDocument, previous);
        parent->redo();
        contents.reset(openSmartObjectContents(parent.get(), id, &error));
        QVERIFY(contents);
        QCOMPARE(contents->activeLayer()->text, QString("Edited"));
    }
    void modelSmartFilterAndActionConversionRetainEditableContents() {
        std::unique_ptr<Document> model(Document::create({64, 48}, Qt::transparent));
        auto *text = model->activeLayer();
        text->kind = LayerKind::Text;
        text->text = "Editable through model";
        text->font.setPixelSize(12);
        model->clearHistory();
        model->addSmartFilter("Invert");
        QCOMPARE(model->activeLayer()->kind, LayerKind::SmartObject);
        QCOMPARE(model->activeLayer()->smartFilters.size(), 1);
        QCOMPARE(model->historyNames().size(), 1);
        QString error;
        std::unique_ptr<Document> contents(
            openSmartObjectContents(model.get(), model->activeLayer()->id, &error));
        QVERIFY2(contents, qPrintable(error));
        QCOMPARE(contents->activeLayer()->kind, LayerKind::Text);
        QCOMPARE(contents->activeLayer()->text, QString("Editable through model"));
        std::unique_ptr<Document> action(Document::create({32, 24}, Qt::transparent));
        auto *shape = action->activeLayer();
        shape->kind = LayerKind::Shape;
        shape->shape.addEllipse(QRectF(2, 3, 8, 9));
        const auto originalShape = shape->shape;
        action->clearHistory();
        QVERIFY2(
            ActionRunner::run(action.get(), {QJsonObject{{"command", "Convert to Smart Object"}}}, &error),
            qPrintable(error));
        QCOMPARE(action->historyNames().size(), 1);
        contents.reset(openSmartObjectContents(action.get(), action->activeLayer()->id, &error));
        QVERIFY2(contents, qPrintable(error));
        QCOMPARE(contents->activeLayer()->kind, LayerKind::Shape);
        QCOMPARE(contents->activeLayer()->shape, originalShape);
        action->activeLayer()->linkedPath = "existing-linked.spe";
        const auto before = action->activeLayer()->embeddedDocument;
        action->convertToSmartObject();
        QVERIFY(ActionRunner::execute(action.get(), {{"command", "Convert to Smart Object"}}, &error));
        QCOMPARE(action->activeLayer()->linkedPath, QString("existing-linked.spe"));
        QCOMPARE(action->activeLayer()->embeddedDocument, before);
        QCOMPARE(action->historyNames().size(), 1);
    }
    void replacementPreservesTransformFiltersAndMasks() {
        QTemporaryDir dir;
        const auto first = dir.filePath("first.png"), second = dir.filePath("second.png");
        QVERIFY(imageFile(first, {8, 6}, Qt::red));
        QVERIFY(imageFile(second, {16, 12}, Qt::blue));
        std::unique_ptr<Document> parent(Document::create({100, 100}, Qt::transparent, 16));
        QString error;
        QVERIFY2(placeSmartObject(parent.get(), first, false, &error), qPrintable(error));
        parent->addSmartFilter("Exposure", {{"exposure", .5}});
        QVERIFY(applyLayerTransform(parent.get(), QTransform().scale(2, 2).rotate(90), &error));
        auto *layer = parent->activeLayer();
        layer->offset += QPointF(19, 23);
        layer->opacity = .42;
        layer->fill = .67;
        layer->blendMode = "Screen";
        layer->mask = makeMask(parent->layerImage(*layer).size(), 16, .7);
        layer->maskDensity = .61;
        layer->maskFeather = 1.5;
        layer->maskLinked = false;
        layer->maskOffset = {1, 2};
        layer->vectorMask.addRect(QRectF(2, 3, 7, 8));
        layer->vectorMaskLinked = false;
        layer->vectorMaskOffset = {-1, -2};
        layer->vectorMaskDensity = .8;
        layer->vectorMaskFeather = .5;
        layer->effects = {{"stroke", QJsonObject{{"size", 1}}}};
        layer->lockAlpha = layer->lockPosition = true;
        const Layer before = *layer;
        const auto size = parent->layerImage(before).size();
        parent->clearHistory();
        const auto activeId = parent->addLayer("Other layer");
        parent->clearHistory();
        QVERIFY2(replaceSmartObjectContents(parent.get(), before.id, second, &error), qPrintable(error));
        const Layer after = parent->state.layers[parent->indexForId(before.id)];
        unchangedProperties(after, before);
        QCOMPARE(parent->activeLayer()->id, activeId);
        QCOMPARE(after.pixels.size, QSize(16, 12));
        QCOMPARE(parent->layerImage(after).size(), size);
        QVERIFY(after.embeddedDocument != before.embeddedDocument);
        QCOMPARE(parent->historyNames().size(), 1);
        parent->undo();
        QCOMPARE(parent->state.layers[parent->indexForId(before.id)].embeddedDocument,
                 before.embeddedDocument);
        QCOMPARE(parent->state.layers[parent->indexForId(before.id)].parameters, before.parameters);
    }
    void linkedReloadRelinkAndOfflineEmbed() {
        QTemporaryDir dir;
        const auto first = dir.filePath("first.png"), second = dir.filePath("second.png");
        QVERIFY(imageFile(first, {8, 6}, Qt::red));
        QVERIFY(imageFile(second, {16, 12}, Qt::green));
        std::unique_ptr<Document> parent(Document::create({32, 24}, Qt::transparent, 16));
        QString error;
        QVERIFY(placeSmartObject(parent.get(), first, true, &error));
        const auto id = parent->activeLayer()->id;
        const auto original = parent->activeLayer()->pixels.image();
        QVERIFY(imageFile(first, {8, 6}, Qt::blue));
        parent->clearHistory();
        QVERIFY2(reloadSmartObject(parent.get(), id, &error), qPrintable(error));
        QCOMPARE(parent->activeLayer()->pixels.image().pixelColor(2, 2), QColor(Qt::blue));
        parent->undo();
        QCOMPARE(parent->activeLayer()->pixels.image(), original);
        QVERIFY2(relinkSmartObject(parent.get(), id, second, &error), qPrintable(error));
        QCOMPARE(parent->activeLayer()->linkedPath, second);
        const auto cached = parent->activeLayer()->embeddedDocument;
        const auto history = parent->historyNames();
        QVERIFY(QFile::remove(second));
        QVERIFY(!reloadSmartObject(parent.get(), id, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(parent->activeLayer()->embeddedDocument, cached);
        QCOMPARE(parent->historyNames(), history);
        std::unique_ptr<Document> unavailable(openSmartObjectContents(parent.get(), id, &error));
        QVERIFY(!unavailable);
        QVERIFY2(embedSmartObject(parent.get(), id, &error), qPrintable(error));
        QVERIFY(parent->activeLayer()->linkedPath.isEmpty());
        unavailable.reset(openSmartObjectContents(parent.get(), id, &error));
        QVERIFY2(unavailable, qPrintable(error));
        QCOMPARE(unavailable->composite().pixelColor(2, 2), QColor(Qt::green));
    }
    void nativeAndPsdRetainSubdocument_data() {
        QTest::addColumn<QString>("suffix");
        QTest::newRow("spe") << "spe";
        QTest::newRow("speb") << "speb";
        QTest::newRow("psd") << "psd";
        QTest::newRow("psb") << "psb";
    }
    void nativeAndPsdRetainSubdocument() {
        QFETCH(QString, suffix);
        QTemporaryDir dir;
        Document child;
        child.state.size = {16, 12};
        child.state.bitDepth = 32;
        Layer text;
        text.id = 19;
        text.kind = LayerKind::Text;
        text.text = "Still editable";
        text.font.setPixelSize(5);
        child.state.layers = {text};
        child.state.metadata = {{"source", "nested document"}};
        QString error;
        const auto source = dir.filePath("child.spe");
        QVERIFY(FormatIO::saveNative(&child, source, &error));
        std::unique_ptr<Document> parent(Document::create({32, 24}, Qt::transparent, 32));
        QVERIFY2(placeSmartObject(parent.get(), source, false, &error), qPrintable(error));
        const auto id = parent->activeLayer()->id;
        const auto bytes = parent->activeLayer()->embeddedDocument;
        parent->addSmartFilter("Gaussian Blur", {{"radius", 1}});
        parent->activeLayer()->mask = makeMask({16, 12}, 32, .5);
        const auto path = dir.filePath("parent." + suffix);
        QVERIFY2(FormatIO::save(parent.get(), path, &error), qPrintable(error));
        std::unique_ptr<Document> loaded(FormatIO::open(path, &error));
        QVERIFY2(loaded, qPrintable(error));
        const auto &restored = loaded->state.layers[loaded->indexForId(id)];
        QCOMPARE(restored.embeddedDocument, bytes);
        QCOMPARE(restored.smartFilters, parent->activeLayer()->smartFilters);
        std::unique_ptr<Document> editable(openSmartObjectContents(loaded.get(), id, &error));
        QVERIFY2(editable, qPrintable(error));
        QCOMPARE(editable->activeLayer()->kind, LayerKind::Text);
        QCOMPARE(editable->activeLayer()->text, text.text);
        QCOMPARE(editable->state.bitDepth, 32);
        auto metadata = editable->state.metadata;
        QCOMPARE(metadata.take("_smartObjectBaseDirectory").toString(), dir.path());
        QCOMPARE(metadata, child.state.metadata);
    }
    void failedInputsDoNotChangeParentOrHistory() {
        QTemporaryDir dir;
        const auto first = dir.filePath("first.png");
        QVERIFY(imageFile(first, {8, 6}, Qt::red));
        std::unique_ptr<Document> parent(Document::create({32, 24}, Qt::transparent));
        QString error;
        QVERIFY(placeSmartObject(parent.get(), first, false, &error));
        const auto id = parent->activeLayer()->id;
        parent->clearHistory();
        const auto before = parent->activeLayer()->embeddedDocument;
        QVERIFY(write(dir.filePath("broken.spe"), QByteArray("SPE\0malformed", 13)));
        QVERIFY(!replaceSmartObjectContents(parent.get(), id, dir.filePath("broken.spe"), &error));
        QVERIFY(!relinkSmartObject(parent.get(), id, dir.filePath("missing.png"), &error));
        QCOMPARE(parent->activeLayer()->embeddedDocument, before);
        QVERIFY(parent->historyNames().isEmpty());
        parent->activeLayer()->locked = true;
        QVERIFY(!replaceSmartObjectContents(parent.get(), id, first, &error));
        QVERIFY(!applySmartObjectContents(parent.get(), id, parent.get(), &error));
        QVERIFY(parent->historyNames().isEmpty());
    }
    void lockedAncestorsAndCyclesBlockContentsChanges() {
        QTemporaryDir dir;
        const auto source = dir.filePath("source.spe");
        std::unique_ptr<Document> contents(Document::create({8, 6}, Qt::blue));
        QString error;
        QVERIFY(FormatIO::saveNative(contents.get(), source, &error));
        std::unique_ptr<Document> parent(Document::create({32, 24}, Qt::transparent));
        const auto groupId = parent->addLayer("Locked group", LayerKind::Group);
        const auto childId = parent->addLayer("Child");
        parent->activeLayer()->parentId = groupId;
        parent->state.layers[parent->indexForId(groupId)].locked = true;
        parent->clearHistory();
        QVERIFY(!convertLayerToEmbeddedSmartObject(parent.get(), childId, &error));
        QVERIFY(error.contains("ancestor"));
        QCOMPARE(parent->activeLayer()->kind, LayerKind::Pixel);
        QVERIFY(parent->historyNames().isEmpty());
        parent->state.layers[parent->indexForId(groupId)].locked = false;
        QVERIFY(convertLayerToEmbeddedSmartObject(parent.get(), childId, &error));
        parent->activeLayer()->linkedPath = source;
        const auto before = parent->activeLayer()->embeddedDocument;
        const auto originalFile = read(source);
        parent->state.layers[parent->indexForId(groupId)].locked = true;
        parent->clearHistory();
        QVERIFY(!replaceSmartObjectContents(parent.get(), childId, source, &error));
        QVERIFY(!relinkSmartObject(parent.get(), childId, source, &error));
        QVERIFY(!reloadSmartObject(parent.get(), childId, &error));
        QVERIFY(!embedSmartObject(parent.get(), childId, &error));
        QVERIFY(!applySmartObjectContents(parent.get(), childId, contents.get(), &error));
        QVERIFY(!saveLinkedSmartObjectContents(parent.get(), childId, contents.get(), &error));
        QCOMPARE(parent->activeLayer()->embeddedDocument, before);
        QCOMPARE(read(source), originalFile);
        QVERIFY(parent->historyNames().isEmpty());
        auto &group = parent->state.layers[parent->indexForId(groupId)];
        group.locked = false;
        group.parentId = childId;
        QVERIFY(!reloadSmartObject(parent.get(), childId, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(parent->activeLayer()->embeddedDocument, before);
        QVERIFY(parent->historyNames().isEmpty());
    }
    void malformedEmbeddedPayloadsAreRejected() {
        QTemporaryDir dir;
        std::unique_ptr<Document> parent(Document::create({8, 6}, Qt::red));
        QString error;
        const auto id = parent->activeLayer()->id;
        QVERIFY(convertLayerToEmbeddedSmartObject(parent.get(), id, &error));
        const auto valid = parent->activeLayer()->embeddedDocument;
        auto invalid = valid;
        invalid[invalid.size() - 20] ^= 1;
        QVERIFY(!io::validEmbeddedDocument(invalid));
        parent->activeLayer()->embeddedDocument = invalid;
        QVERIFY(!FormatIO::saveNative(parent.get(), dir.filePath("bad.spe"), &error));
        QVERIFY(!FormatIO::savePsd(parent.get(), dir.filePath("bad.psd"), &error));
        QVERIFY(!openSmartObjectContents(parent.get(), id, &error));
        parent->activeLayer()->embeddedDocument = valid;
        const auto path = dir.filePath("valid.spe");
        QVERIFY(FormatIO::saveNative(parent.get(), path, &error));
        auto bytes = read(path);
        const auto at = bytes.indexOf("SOBD");
        QVERIFY(at >= 0);
        const auto length = qFromLittleEndian<quint64>(bytes.constData() + at + 4);
        const auto payload = bytes.mid(at + 12, 8);
        QByteArray emptyChunk("SOBD", 4);
        char sizeBytes[8], checksumBytes[4];
        qToLittleEndian<quint64>(8, sizeBytes);
        qToLittleEndian<quint32>(crc(payload), checksumBytes);
        emptyChunk.append(sizeBytes, 8);
        emptyChunk.append(payload);
        emptyChunk.append(checksumBytes, 4);
        bytes.replace(at, qsizetype(length) + 16, emptyChunk);
        QVERIFY(write(dir.filePath("empty-object.spe"), bytes));
        std::unique_ptr<Document> malformed(FormatIO::openNative(dir.filePath("empty-object.spe"), &error));
        QVERIFY(!malformed);
        QVERIFY(error.contains("embedded contents length"));
    }
    void linkedContentsSaveHasExplicitTargetAndFailureRollback() {
        QTemporaryDir dir;
        const auto path = dir.filePath("linked.spe");
        std::unique_ptr<Document> original(Document::create({8, 6}, Qt::red, 16));
        QString error;
        QVERIFY(FormatIO::saveNative(original.get(), path, &error));
        std::unique_ptr<Document> parent(Document::create({32, 24}, Qt::transparent, 16));
        QVERIFY(placeSmartObject(parent.get(), path, true, &error));
        const auto id = parent->activeLayer()->id;
        std::unique_ptr<Document> contents(openSmartObjectContents(parent.get(), id, &error));
        contents->mutate("Edit child", [&] {
            QImage blue(8, 6, QImage::Format_RGBA64);
            blue.fill(Qt::blue);
            contents->activeLayer()->pixels.setImage(blue);
        });
        parent->clearHistory();
        const auto before = parent->activeLayer()->embeddedDocument;
        QVERIFY2(saveLinkedSmartObjectContents(parent.get(), id, contents.get(), &error), qPrintable(error));
        QCOMPARE(parent->historyNames().size(), 1);
        std::unique_ptr<Document> reloaded(FormatIO::openNative(path, &error));
        QVERIFY(reloaded);
        QCOMPARE(reloaded->composite().pixelColor(2, 2), QColor(Qt::blue));
        parent->undo();
        QCOMPARE(parent->activeLayer()->embeddedDocument, before);
        QCOMPARE(reloaded->composite().pixelColor(2, 2), QColor(Qt::blue));
        parent->activeLayer()->linkedPath = dir.filePath("missing-folder/linked.spe");
        const auto history = parent->historyNames();
        QVERIFY(!saveLinkedSmartObjectContents(parent.get(), id, contents.get(), &error));
        QCOMPARE(parent->activeLayer()->embeddedDocument, before);
        QCOMPARE(parent->historyNames(), history);
    }
    void relativeLinksResolveFromParentDocument() {
        QTemporaryDir dir;
        QVERIFY(imageFile(dir.filePath("relative.png"), {4, 3}, Qt::green));
        std::unique_ptr<Document> parent(Document::create({8, 6}, Qt::transparent));
        parent->filePath = dir.filePath("parent.spe");
        parent->activeLayer()->kind = LayerKind::SmartObject;
        parent->activeLayer()->linkedPath = "relative.png";
        QString error;
        QVERIFY2(reloadSmartObject(parent.get(), parent->activeLayer()->id, &error), qPrintable(error));
        QCOMPARE(parent->activeLayer()->linkedPath, QString("relative.png"));
        QCOMPARE(parent->activeLayer()->pixels.image().pixelColor(1, 1), QColor(Qt::green));
    }
    void embeddedNestedRelativeLinksKeepTheirSourceDirectory() {
        QTemporaryDir dir;
        const auto assets = dir.filePath("assets");
        QVERIFY(QDir().mkpath(assets));
        QVERIFY(imageFile(assets + "/relative.png", {8, 6}, Qt::green));
        std::unique_ptr<Document> child(Document::create({8, 6}, Qt::blue));
        const auto nestedId = child->activeLayer()->id;
        child->activeLayer()->kind = LayerKind::SmartObject;
        child->activeLayer()->linkedPath = "relative.png";
        QString error;
        const auto original = assets + "/child.spe";
        QVERIFY(FormatIO::saveNative(child.get(), original, &error));
        std::unique_ptr<Document> parent(Document::create({16, 12}, Qt::transparent));
        QVERIFY(placeSmartObject(parent.get(), original, false, &error));
        const auto id = parent->activeLayer()->id;
        const auto path = dir.filePath("parent.spe");
        QVERIFY(FormatIO::saveNative(parent.get(), path, &error));
        parent.reset(FormatIO::openNative(path, &error));
        QVERIFY(parent);
        child.reset(openSmartObjectContents(parent.get(), id, &error));
        QVERIFY(child);
        QVERIFY(child->filePath.isEmpty());
        QCOMPARE(smartObjectLinkedPath(child.get(), *child->activeLayer()), assets + "/relative.png");
        QVERIFY2(reloadSmartObject(child.get(), nestedId, &error), qPrintable(error));
        QCOMPARE(child->activeLayer()->pixels.image().pixelColor(1, 1), QColor(Qt::green));
        const auto exported = dir.filePath("exported-child.spe");
        QVERIFY(exportSmartObjectContents(parent.get(), id, exported, &error));
        child.reset(FormatIO::openNative(exported, &error));
        QVERIFY(child);
        QVERIFY2(reloadSmartObject(child.get(), nestedId, &error), qPrintable(error));
        QCOMPARE(child->activeLayer()->pixels.image().pixelColor(1, 1), QColor(Qt::green));
        QVERIFY(placeSmartObject(parent.get(), exported, false, &error));
        child.reset(openSmartObjectContents(parent.get(), parent->activeLayer()->id, &error));
        QVERIFY(child);
        QVERIFY2(reloadSmartObject(child.get(), nestedId, &error), qPrintable(error));
        QCOMPARE(child->activeLayer()->pixels.image().pixelColor(1, 1), QColor(Qt::green));
    }
    void patternLayerKindNativeRoundTrip() {
        QTemporaryDir dir;
        std::unique_ptr<Document> parent(Document::create({8, 6}, Qt::transparent));
        auto *layer = parent->activeLayer();
        layer->kind = LayerKind::PatternFill;
        layer->parameters = {{"patternPng", "retained-data"}, {"scale", 125}, {"angle", 17}};
        QString error;
        const auto path = dir.filePath("pattern.spe");
        QVERIFY2(FormatIO::saveNative(parent.get(), path, &error), qPrintable(error));
        std::unique_ptr<Document> loaded(FormatIO::openNative(path, &error));
        QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->activeLayer()->kind, LayerKind::PatternFill);
        QCOMPARE(loaded->activeLayer()->parameters, layer->parameters);
    }
    void contentsTabSaveUpdatesParentAndSurvivesParentClose() {
        MainWindow window;
        window.openDemo();
        auto *parent = window.currentDocument();
        parent->mutate("Small fixture", [&] {
            parent->state.size = {16, 12};
            Layer layer;
            layer.id = 501;
            QImage image(8, 6, QImage::Format_RGBA8888);
            image.fill(Qt::red);
            layer.pixels.setImage(image);
            parent->state.layers = {layer};
            parent->state.activeIndex = 0;
        });
        window.runCommand("Convert to Smart Object");
        window.runCommand("Edit Contents...");
        auto *contents = window.currentDocument();
        QVERIFY(contents != parent);
        contents->mutate("Recolour child", [&] {
            QImage image(8, 6, QImage::Format_RGBA8888);
            image.fill(Qt::blue);
            contents->activeLayer()->pixels.setImage(image);
        });
        window.runCommand("Save");
        QVERIFY(!contents->isModified());
        QVERIFY(parent->isModified());
        QCOMPARE(parent->activeLayer()->pixels.image().pixelColor(2, 2), QColor(Qt::blue));
        QPointer<Document> parentGuard(parent);
        parent->markSaved();
        auto *tabs = window.findChild<QTabWidget *>("documentTabs");
        QVERIFY(tabs);
        tabs->setCurrentIndex(0);
        window.runCommand("Close");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!parentGuard);
        QCOMPARE(window.currentDocument(), contents);
        contents->mutate("Still editable", [&] { contents->activeLayer()->name = "surviving contents"; });
        bool warned = false;
        QTimer::singleShot(0, [&] {
            auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(message);
            warned = true;
            message->accept();
        });
        window.runCommand("Save");
        QVERIFY(warned);
        QVERIFY(contents->filePath.isEmpty());
        QVERIFY(contents->isModified());
    }
};
QTEST_MAIN(SmartObjectTests)
#include "smartobject_tests.moc"
