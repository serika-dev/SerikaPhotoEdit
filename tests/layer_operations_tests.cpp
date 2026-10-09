#include "document/LayerOperations.h"
#include "document/TransformOperations.h"
#include "ui/MainWindow.h"
#include "ui/panels/LayerTree.h"
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>
#include <limits>
#include <memory>

using namespace serika;
namespace {
std::unique_ptr<Document> emptyDocument() {
    auto document = std::make_unique<Document>();
    document->state.size = {256, 192};
    document->state.activeIndex = -1;
    return document;
}
Layer pixel(quint64 id, QSize extent, QPointF offset = {}, quint64 parent = 0, QColor color = Qt::red) {
    Layer layer;
    layer.id = id;
    layer.name = "Layer " + QString::number(id);
    layer.parentId = parent;
    layer.offset = offset;
    QImage image(extent, QImage::Format_RGBA64);
    image.fill(color);
    layer.pixels = TileImage::fromImage(image);
    return layer;
}
Layer group(quint64 id, QPointF offset = {}, quint64 parent = 0) {
    Layer layer;
    layer.id = id;
    layer.kind = LayerKind::Group;
    layer.name = "Group " + QString::number(id);
    layer.blendMode = "Pass Through";
    layer.parentId = parent;
    layer.offset = offset;
    return layer;
}
QVector<quint64> siblings(const Document &document, quint64 parent = 0) {
    QVector<quint64> result;
    for (const Layer &layer : document.state.layers)
        if (layer.parentId == parent)
            result.append(layer.id);
    return result;
}
QTreeWidgetItem *itemFor(QTreeWidget *tree, quint64 id) {
    QTreeWidgetItemIterator it(tree);
    while (*it) {
        if ((*it)->data(1, Qt::UserRole).toULongLong() == id)
            return *it;
        ++it;
    }
    return nullptr;
}
void choose(QTreeWidget *tree, const QVector<quint64> &ids) {
    tree->clearSelection();
    if (!ids.isEmpty())
        tree->setCurrentItem(itemFor(tree, ids.first()));
    for (quint64 id : ids)
        if (auto *item = itemFor(tree, id))
            item->setSelected(true);
}
} // namespace

class LayerOperationTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
  private slots:
    void initTestCase() {
        QVERIFY(m_settings.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    void rootsRemoveDuplicateAndNestedSelection() {
        auto d = emptyDocument();
        d->state.layers = {pixel(1, {5, 5}, {}, 4), pixel(2, {4, 4}, {}, 3), group(3, {}, 4), group(4),
                           pixel(5, {6, 6})};
        QCOMPARE(selectedLayerRoots(d.get(), {2, 4, 1, 4, 999, 5}), QVector<quint64>({4, 5}));
        QCOMPARE(selectedLayerRoots(d.get(), {1, 2}), QVector<quint64>({1, 2}));
    }
    void contentBoundsIgnoreTransparentPaddingAndRetainNativeAlpha() {
        auto d = emptyDocument();
        Layer layer;
        layer.id = 1;
        layer.offset = {3.5, 4.25};
        QImage image(520, 280, QImage::Format_RGBA64);
        image.fill(Qt::transparent);
        reinterpret_cast<QRgba64 *>(image.scanLine(270))[510] = QRgba64::fromRgba64(100, 200, 300, 1);
        layer.pixels.setImage(image);
        d->state.layers = {layer};
        QCOMPARE(layerContentBounds(d.get(), 1), QRectF(513.5, 274.25, 1, 1));
        QImage floating(8, 8, QImage::Format_RGBA32FPx4);
        floating.fill(Qt::transparent);
        auto *sample = reinterpret_cast<float *>(floating.scanLine(6)) + 4 * 7;
        sample[0] = 4;
        sample[3] = .00001f;
        d->state.layers[0].pixels.setImage(floating);
        QCOMPARE(layerContentBounds(d.get(), 1), QRectF(10.5, 10.25, 1, 1));
    }
    void alignmentUsesContentBoundsAndOneUndo() {
        auto d = emptyDocument();
        auto a = pixel(1, {60, 20}, {10, 10});
        QImage padded(60, 20, QImage::Format_RGBA64);
        padded.fill(Qt::transparent);
        QPainter painter(&padded);
        painter.fillRect(QRect(20, 2, 10, 10), Qt::red);
        painter.end();
        a.pixels.setImage(padded);
        d->state.layers = {a, pixel(2, {15, 10}, {80, 25})};
        d->state.activeIndex = 1;
        d->markSaved();
        const auto result = alignLayers(d.get(), {1, 2}, LayerAlignment::Left);
        QVERIFY2(result.changed, qPrintable(result.error));
        QCOMPARE(layerContentBounds(d.get(), 1).left(), 30.);
        QCOMPARE(layerContentBounds(d.get(), 2).left(), 30.);
        QCOMPARE(d->state.layers[1].offset.y(), 25.);
        QCOMPARE(d->historyNames().size(), 1);
        QVERIFY(d->isModified());
        d->undo();
        QCOMPARE(d->state.layers[1].offset, QPointF(80, 25));
        QVERIFY(!d->isModified());
        d->redo();
        QCOMPARE(d->state.layers[1].offset, QPointF(30, 25));
    }
    void alignmentToCanvasAndSelectionSupportsSingleLayer() {
        auto d = emptyDocument();
        d->state.layers = {pixel(1, {11, 9}, {3, 4}, 2), group(2, {20, 30})};
        d->state.activeIndex = 0;
        QVERIFY(alignLayers(d.get(), {1}, LayerAlignment::Center, LayerAlignmentReference::Canvas));
        QCOMPARE(layerContentBounds(d.get(), 1).center(), QPointF(128, 96));
        QImage selection = makeMask(d->state.size, 16);
        for (int y = 12; y < 32; ++y)
            for (int x = 8; x < 38; ++x)
                setMaskSample(selection, x, y, .1);
        d->setSelection(selection);
        auto result = alignLayers(d.get(), {1}, LayerAlignment::Right, LayerAlignmentReference::Selection);
        QVERIFY2(result, qPrintable(result.error));
        QCOMPARE(layerContentBounds(d.get(), 1).right(), 38.);
        d->deselect();
        const int before = d->historyNames().size();
        result = alignLayers(d.get(), {1}, LayerAlignment::Left, LayerAlignmentReference::Selection);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(d->historyNames().size(), before);
    }
    void transformedVectorsAlignWithoutRasterization() {
        auto d = emptyDocument();
        Layer shape;
        shape.id = 1;
        shape.kind = LayerKind::Shape;
        shape.shape.addRect(10, 10, 12, 8);
        shape.parameters["contentTransform"] = transformJson(QTransform::fromScale(2, 2));
        shape.color = Qt::green;
        d->state.layers = {shape, pixel(2, {15, 6}, {100, 50})};
        d->state.activeIndex = 0;
        auto result = alignLayers(d.get(), {1, 2}, LayerAlignment::Bottom);
        QVERIFY2(result, qPrintable(result.error));
        QCOMPARE(layerContentBounds(d.get(), 1).bottom(), layerContentBounds(d.get(), 2).bottom());
        QCOMPARE(d->state.layers[0].kind, LayerKind::Shape);
        QVERIFY(d->state.layers[0].parameters.contains("contentTransform"));
        QVERIFY(!d->state.layers[0].shape.isEmpty());
    }
    void distributeCentersPreservesEndpointsAndOrthogonalPositions() {
        auto d = emptyDocument();
        d->state.layers = {pixel(1, {10, 8}, {0, 15}), pixel(2, {20, 8}, {17, 25}),
                           pixel(3, {30, 8}, {100, 35})};
        d->state.activeIndex = 1;
        auto result = distributeLayers(d.get(), {3, 1, 2}, LayerDistribution::HorizontalCenters);
        QVERIFY2(result.changed, qPrintable(result.error));
        QCOMPARE(d->state.layers[0].offset, QPointF(0, 15));
        QCOMPARE(d->state.layers[1].offset, QPointF(50, 25));
        QCOMPARE(d->state.layers[2].offset, QPointF(100, 35));
        QCOMPARE(d->historyNames().size(), 1);
        d->undo();
        QCOMPARE(d->state.layers[1].offset, QPointF(17, 25));
    }
    void distributeEqualGapsHandlesDifferentWidthsAndOverlappingSpan() {
        auto d = emptyDocument();
        d->state.layers = {pixel(1, {10, 8}, {0, 0}), pixel(2, {20, 8}, {15, 0}),
                           pixel(3, {30, 8}, {100, 0})};
        d->state.activeIndex = 0;
        QVERIFY(distributeLayers(d.get(), {1, 2, 3}, LayerDistribution::HorizontalGaps));
        QCOMPARE(d->state.layers[1].offset.x(), 45.);
        const qreal firstGap = layerContentBounds(d.get(), 2).left() - layerContentBounds(d.get(), 1).right();
        const qreal secondGap =
            layerContentBounds(d.get(), 3).left() - layerContentBounds(d.get(), 2).right();
        QCOMPARE(firstGap, secondGap);
        d->state.layers[1].offset = {12, 0};
        d->state.layers[2].offset = {20, 0};
        QVERIFY(distributeLayers(d.get(), {1, 2, 3}, LayerDistribution::HorizontalGaps));
        QCOMPARE(layerContentBounds(d.get(), 2).left() - layerContentBounds(d.get(), 1).right(), -5.);
        QCOMPARE(layerContentBounds(d.get(), 3).left() - layerContentBounds(d.get(), 2).right(), -5.);
    }
    void distributionEdges_data() {
        QTest::addColumn<int>("operation");
        QTest::newRow("left") << int(LayerDistribution::LeftEdges);
        QTest::newRow("right") << int(LayerDistribution::RightEdges);
        QTest::newRow("top") << int(LayerDistribution::TopEdges);
        QTest::newRow("bottom") << int(LayerDistribution::BottomEdges);
        QTest::newRow("vertical centers") << int(LayerDistribution::VerticalCenters);
        QTest::newRow("vertical gaps") << int(LayerDistribution::VerticalGaps);
    }
    void distributionEdges() {
        QFETCH(int, operation);
        auto d = emptyDocument();
        d->state.layers = {pixel(1, {10, 10}, {0, 0}), pixel(2, {10, 10}, {12, 13}),
                           pixel(3, {10, 10}, {100, 100})};
        d->state.activeIndex = 1;
        const auto result = distributeLayers(d.get(), {1, 2, 3}, LayerDistribution(operation));
        QVERIFY2(result.changed, qPrintable(result.error));
        const bool horizontal =
            operation == int(LayerDistribution::LeftEdges) || operation == int(LayerDistribution::RightEdges);
        QCOMPARE(d->state.layers[1].offset, horizontal ? QPointF(50, 13) : QPointF(12, 50));
        QCOMPARE(d->state.layers[0].offset, QPointF());
        QCOMPARE(d->state.layers[2].offset, QPointF(100, 100));
    }
    void locksAbortMultiOperationAtomicallyButStationaryAnchorIsAllowed() {
        auto d = emptyDocument();
        auto anchor = pixel(1, {10, 10}, {0, 0});
        anchor.lockPosition = true;
        d->state.layers = {anchor, pixel(2, {10, 10}, {20, 0}), pixel(3, {10, 10}, {60, 0})};
        d->state.activeIndex = 1;
        QVERIFY(alignLayers(d.get(), {1, 2, 3}, LayerAlignment::Left));
        QCOMPARE(d->state.layers[1].offset.x(), 0.);
        d->undo();
        d->clearHistory();
        auto result = alignLayers(d.get(), {1, 2, 3}, LayerAlignment::Right);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(d->state.layers[1].offset.x(), 20.);
        QCOMPARE(d->historyNames().size(), 0);
        d->state.layers[0].lockPosition = false;
        d->state.layers[1].lockPosition = true;
        result = distributeLayers(d.get(), {1, 2, 3}, LayerDistribution::HorizontalCenters);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(d->state.layers[1].offset.x(), 20.);
        QVERIFY(!d->canUndo());
    }
    void translateGroupAvoidsDoubleMovementAndKeepsUnlinkedMasksFixed() {
        auto d = emptyDocument();
        auto child = pixel(1, {10, 10}, {3, 4}, 2);
        child.mask = makeMask({10, 10}, 16, 1);
        child.maskLinked = false;
        child.maskOffset = {5, 6};
        child.vectorMask.addRect(0, 0, 6, 6);
        child.vectorMaskLinked = false;
        child.vectorMaskOffset = {7, 8};
        d->state.layers = {child, group(2, {20, 30}), pixel(3, {8, 8}, {60, 50})};
        d->state.activeIndex = 1;
        const QPointF maskOrigin = d->effectiveLayerOffset(child) + child.maskOffset;
        const QPointF vectorOrigin = d->effectiveLayerOffset(child) + child.vectorMaskOffset;
        d->beginTransaction("Drag selected layers");
        QVERIFY(translateLayers(d.get(), {1, 2, 3}, {4, 5}));
        QVERIFY(translateLayers(d.get(), {1, 2, 3}, {2, -1}));
        d->endTransaction();
        QCOMPARE(d->effectiveLayerOffset(d->state.layers[d->indexForId(1)]), QPointF(29, 38));
        QCOMPARE(d->state.layers[d->indexForId(1)].offset, QPointF(3, 4));
        QCOMPARE(d->effectiveLayerOffset(d->state.layers[d->indexForId(1)]) +
                     d->state.layers[d->indexForId(1)].maskOffset,
                 maskOrigin);
        QCOMPARE(d->effectiveLayerOffset(d->state.layers[d->indexForId(1)]) +
                     d->state.layers[d->indexForId(1)].vectorMaskOffset,
                 vectorOrigin);
        QCOMPARE(d->state.layers[d->indexForId(3)].offset, QPointF(66, 54));
        QCOMPARE(d->historyNames().size(), 1);
        d->undo();
        QCOMPARE(d->state.layers[d->indexForId(2)].offset, QPointF(20, 30));
        QCOMPARE(d->state.layers[d->indexForId(1)].maskOffset, QPointF(5, 6));
    }
    void descendantAndAncestorLocksProtectWholeSelectedSubtrees() {
        auto d = emptyDocument();
        d->state.layers = {pixel(1, {10, 10}, {}, 2), group(2), pixel(3, {5, 5}, {30, 30})};
        d->state.activeIndex = 1;
        d->state.layers[0].lockPosition = true;
        QVERIFY(!translateLayers(d.get(), {2, 3}, {3, 4}).error.isEmpty());
        QCOMPARE(d->state.layers[2].offset, QPointF(30, 30));
        d->state.layers[0].lockPosition = false;
        d->state.layers[1].locked = true;
        QVERIFY(!deleteLayers(d.get(), {1, 3}).error.isEmpty());
        QCOMPARE(d->state.layers.size(), 3);
        QVERIFY(!d->canUndo());
    }
    void duplicateMixedRootsClonesSubtreeOnceWithUniqueIdsAndSharedPixels() {
        auto d = emptyDocument();
        auto child = pixel(17, {10, 10}, {4, 5}, 400);
        child.kind = LayerKind::SmartObject;
        child.embeddedDocument = QByteArray("SPE retained source");
        child.smartFilters.append(
            QJsonObject{{"name", "Gaussian Blur"}, {"parameters", QJsonObject{{"radius", 2}}}});
        d->state.layers = {child, group(400, {10, 20}), pixel(900, {8, 8}, {80, 90})};
        d->state.activeIndex = 0;
        const auto result = duplicateLayers(d.get(), {17, 400, 900});
        QVERIFY2(result.changed, qPrintable(result.error));
        QCOMPARE(d->state.layers.size(), 6);
        QCOMPARE(result.selection.size(), 2);
        QSet<quint64> unique;
        for (const auto &layer : d->state.layers)
            unique.insert(layer.id);
        QCOMPARE(unique.size(), 6);
        const quint64 newGroup = result.selection[0];
        QCOMPARE(siblings(*d, newGroup).size(), 1);
        const auto copy = d->state.layers[d->indexForId(siblings(*d, newGroup).first())];
        QVERIFY(copy.id > 900);
        QCOMPARE(copy.embeddedDocument, child.embeddedDocument);
        QCOMPARE(copy.smartFilters, child.smartFilters);
        QCOMPARE(copy.pixels.tiles.cbegin().value().cacheKey(),
                 child.pixels.tiles.cbegin().value().cacheKey());
        QCOMPARE(d->effectiveLayerOffset(copy), QPointF(14, 25));
        QCOMPARE(d->activeLayer()->id, copy.id);
        QCOMPARE(d->historyNames().size(), 1);
        d->undo();
        QCOMPARE(d->state.layers.size(), 3);
        d->addLayer();
        QVERIFY(d->activeLayer()->id > 900);
    }
    void deleteSelectedGroupAndLayerIsOneUndoAndDoesNotLeaveOrphans() {
        auto d = emptyDocument();
        d->state.layers = {pixel(1, {8, 8}, {}, 2), group(2), pixel(3, {8, 8}), pixel(4, {8, 8})};
        d->state.activeIndex = 2;
        d->markSaved();
        const auto result = deleteLayers(d.get(), {1, 2, 3});
        QVERIFY2(result.changed, qPrintable(result.error));
        QCOMPARE(siblings(*d), QVector<quint64>({4}));
        QCOMPARE(d->state.layers.size(), 1);
        QCOMPARE(d->historyNames().size(), 1);
        d->undo();
        QCOMPARE(d->state.layers.size(), 4);
        QVERIFY(!d->isModified());
        QVERIFY(deleteLayers(d.get(), {1, 2, 3, 4}));
        QVERIFY(d->state.layers.isEmpty());
        QVERIFY(!d->activeLayer());
        d->addLayer();
        QVERIFY(d->activeLayer());
    }
    void groupAndUngroupPreserveOffsetsMasksAndComposite() {
        auto d = emptyDocument();
        auto a = pixel(1, {10, 10}, {5, 7}, 10);
        a.mask = makeMask({10, 10}, 16, .5);
        a.maskLinked = false;
        a.maskOffset = {2, 3};
        d->state.layers = {a, pixel(2, {8, 8}, {40, 25}, 10, Qt::blue), group(10, {20, 30})};
        d->state.activeIndex = 0;
        const auto expected = d->composite();
        const QPointF first = d->effectiveLayerOffset(a);
        const auto grouped = groupLayers(d.get(), {1, 2}, "Pair");
        QVERIFY2(grouped.changed, qPrintable(grouped.error));
        QCOMPARE(grouped.selection.size(), 1);
        const quint64 id = grouped.selection.first();
        QCOMPARE(d->state.layers[d->indexForId(id)].parentId, quint64(10));
        QCOMPARE(d->effectiveLayerOffset(d->state.layers[d->indexForId(1)]), first);
        QCOMPARE(d->state.layers[d->indexForId(1)].maskOffset, QPointF(2, 3));
        QCOMPARE(d->composite(), expected);
        d->state.layers[d->indexForId(id)].offset = {4, 6};
        d->touch();
        const auto moved = d->composite();
        const QPointF world = d->effectiveLayerOffset(d->state.layers[d->indexForId(1)]);
        const auto ungrouped = ungroupLayers(d.get(), {id});
        QVERIFY2(ungrouped.changed, qPrintable(ungrouped.error));
        QCOMPARE(d->indexForId(id), -1);
        QCOMPARE(siblings(*d, 10), QVector<quint64>({1, 2}));
        QCOMPARE(d->effectiveLayerOffset(d->state.layers[d->indexForId(1)]), world);
        QCOMPARE(d->composite(), moved);
    }
    void groupingDifferentParentsUsesCommonAncestorWithoutChangingWorldOrigins() {
        auto d = emptyDocument();
        d->state.layers = {pixel(1, {8, 8}, {1, 2}, 10), group(10, {20, 30}, 100),
                           pixel(2, {8, 8}, {3, 4}, 20), group(20, {40, 50}, 100), group(100, {7, 9})};
        d->state.activeIndex = 0;
        const QPointF a = d->effectiveLayerOffset(d->state.layers[0]),
                      b = d->effectiveLayerOffset(d->state.layers[2]);
        const auto result = groupLayers(d.get(), {1, 2});
        QVERIFY2(result, qPrintable(result.error));
        const quint64 grouped = result.selection.first();
        QCOMPARE(d->state.layers[d->indexForId(grouped)].parentId, quint64(100));
        QCOMPARE(d->effectiveLayerOffset(d->state.layers[d->indexForId(1)]), a);
        QCOMPARE(d->effectiveLayerOffset(d->state.layers[d->indexForId(2)]), b);
        QVERIFY(siblings(*d, 10).isEmpty());
        QVERIFY(siblings(*d, 20).isEmpty());
        d->undo();
        QCOMPARE(d->state.layers[d->indexForId(1)].parentId, quint64(10));
        QCOMPARE(d->state.layers[d->indexForId(2)].parentId, quint64(20));
    }
    void orderingMovesWholeGroupsAndPreservesSelectedRelativeOrder() {
        auto d = emptyDocument();
        d->state.layers = {pixel(1, {8, 8}), pixel(2, {8, 8}, {}, 10), group(10),
                           pixel(3, {8, 8}), pixel(4, {8, 8}),         pixel(5, {8, 8})};
        d->state.activeIndex = 2;
        QVERIFY(orderLayers(d.get(), {2, 10, 4}, LayerOrder::Forward));
        QCOMPARE(siblings(*d), QVector<quint64>({1, 3, 10, 5, 4}));
        QCOMPARE(siblings(*d, 10), QVector<quint64>({2}));
        QCOMPARE(d->activeLayer()->id, quint64(10));
        QVERIFY(orderLayers(d.get(), {10, 4}, LayerOrder::Back));
        QCOMPARE(siblings(*d), QVector<quint64>({10, 4, 1, 3, 5}));
        d->undo();
        QCOMPARE(siblings(*d), QVector<quint64>({1, 3, 10, 5, 4}));
        QVERIFY(orderLayers(d.get(), {2}, LayerOrder::Front));
        QCOMPARE(siblings(*d), QVector<quint64>({1, 3, 10, 5, 4}));
        QCOMPARE(d->state.layers[d->indexForId(2)].parentId, quint64(10));
    }
    void reparentPreservesTransformedContentAndRejectsCycles() {
        auto d = emptyDocument();
        auto a = pixel(1, {8, 8}, {3, 4}, 10);
        a.kind = LayerKind::SmartObject;
        a.parameters["contentTransform"] = transformJson(QTransform::fromScale(2, 3));
        d->state.layers = {a, group(10, {20, 30}), group(20, {60, 70})};
        d->state.activeIndex = 0;
        const auto oldBounds = layerContentBounds(d.get(), 1);
        QVERIFY(reparentLayers(d.get(), {1}, 20));
        QCOMPARE(d->state.layers[d->indexForId(1)].parentId, quint64(20));
        QCOMPARE(layerContentBounds(d.get(), 1), oldBounds);
        QVERIFY(d->state.layers[d->indexForId(1)].parameters.contains("contentTransform"));
        const int history = d->historyNames().size();
        QVERIFY(!reparentLayers(d.get(), {20}, 1).error.isEmpty());
        QVERIFY(!reparentLayers(d.get(), {20}, 20).error.isEmpty());
        QCOMPARE(d->historyNames().size(), history);
        d->undo();
        QCOMPARE(d->state.layers[d->indexForId(1)].parentId, quint64(10));
    }
    void dropTreePreservesWorldOriginsAndRejectsInvalidTargets() {
        auto d = emptyDocument();
        d->state.layers = {pixel(1, {8, 8}, {3, 4}, 10), group(10, {20, 30}), pixel(2, {8, 8}, {5, 6}, 20),
                           group(20, {60, 70})};
        d->state.activeIndex = 0;
        const auto worldA = d->effectiveLayerOffset(d->state.layers[0]);
        const auto worldB = d->effectiveLayerOffset(d->state.layers[2]);
        auto result = applyLayerTree(d.get(), {{10, 0}, {1, 20}, {2, 20}, {20, 0}}, {1});
        QVERIFY2(result.changed, qPrintable(result.error));
        QCOMPARE(d->effectiveLayerOffset(d->state.layers[d->indexForId(1)]), worldA);
        QCOMPARE(d->effectiveLayerOffset(d->state.layers[d->indexForId(2)]), worldB);
        QCOMPARE(d->historyNames().size(), 1);
        result = applyLayerTree(d.get(), {{10, 20}, {1, 20}, {2, 20}, {20, 10}}, {10});
        QVERIFY(!result.error.isEmpty());
        result = applyLayerTree(d.get(), {{10, 0}, {1, 2}, {2, 20}, {20, 0}}, {1});
        QVERIFY(!result.error.isEmpty());
        result = applyLayerTree(d.get(), {{10, 0}, {1, 20}, {1, 20}, {20, 0}}, {1});
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(d->historyNames().size(), 1);
    }
    void noOpAndMalformedTreeDoNotCreateHistory() {
        auto d = emptyDocument();
        d->state.layers = {pixel(1, {10, 10}), pixel(2, {10, 10}, {0, 20})};
        d->state.activeIndex = 1;
        QVERIFY(!alignLayers(d.get(), {1, 2}, LayerAlignment::Left).changed);
        QVERIFY(!orderLayers(d.get(), {2}, LayerOrder::Front).changed);
        QVERIFY(!translateLayers(d.get(), {1, 2}, {}).changed);
        QVERIFY(!d->canUndo());
        QVERIFY(!translateLayers(d.get(), {1}, {std::numeric_limits<double>::infinity(), 0}).error.isEmpty());
        d->state.layers[0].parentId = 2;
        QVERIFY(!duplicateLayers(d.get(), {1}).error.isEmpty());
        QCOMPARE(d->state.layers.size(), 2);
        QVERIFY(!d->canUndo());
    }
    void uiSelectionCommandsCountAndUndoOperateOnMultipleLayers() {
        MainWindow window;
        window.resize(1440, 900);
        window.show();
        window.openDemo();
        auto *document = window.currentDocument();
        document->state.size = {256, 192};
        document->state.layers = {pixel(1, {10, 10}, {5, 20}), pixel(2, {20, 10}, {60, 40}),
                                  pixel(3, {8, 8}, {100, 80})};
        document->state.activeIndex = 2;
        document->clearHistory();
        document->markSaved();
        document->touch();
        auto *tree = window.findChild<QTreeWidget *>("layersTree");
        QVERIFY(tree);
        QTRY_COMPARE(tree->topLevelItemCount(), 3);
        choose(tree, {1, 2});
        QTRY_COMPARE(tree->selectedItems().size(), 2);
        auto *label = window.findChild<QLabel *>("selectedLayerCount");
        QVERIFY(label);
        QVERIFY(label->text().contains("2 layers selected"));
        QCOMPARE(window.currentCanvas()->property("selectedLayerIds").toList().size(), 2);
        window.runCommand("Align Left Edges");
        QCOMPARE(layerContentBounds(document, 1).left(), layerContentBounds(document, 2).left());
        QCOMPARE(document->historyNames().size(), 1);
        window.runCommand("Duplicate Layer");
        QCOMPARE(document->state.layers.size(), 5);
        QCOMPARE(tree->selectedItems().size(), 2);
        window.runCommand("Delete Layer");
        QCOMPARE(document->state.layers.size(), 3);
        window.runCommand("Undo");
        QCOMPARE(document->state.layers.size(), 5);
        window.runCommand("Undo");
        QCOMPARE(document->state.layers.size(), 3);
    }
    void uiLayerSelectionIsRestoredPerDocument() {
        MainWindow window;
        window.show();
        window.openDemo();
        auto *first = window.currentDocument();
        auto *tree = window.findChild<QTreeWidget *>("layersTree");
        QTRY_VERIFY(tree && tree->topLevelItemCount() >= 5);
        QTRY_VERIFY(window.findChild<QLabel *>("selectedLayerCount"));
        const quint64 a = first->state.layers[0].id, b = first->state.layers[1].id;
        choose(tree, {a, b});
        QCOMPARE(tree->selectedItems().size(), 2);
        window.openDemo();
        QVERIFY(window.currentDocument() != first);
        QTRY_COMPARE(tree->selectedItems().size(), 1);
        auto *tabs = window.findChild<QTabWidget *>("documentTabs");
        QVERIFY(tabs);
        tabs->setCurrentIndex(0);
        QTRY_COMPARE(tree->selectedItems().size(), 2);
        QCOMPARE(window.currentCanvas()->property("selectedLayerIds").toList().size(), 2);
    }
    void actionRecordingSkipsMultipleTargetsAndFailedDeletes() {
        MainWindow window;
        window.show();
        window.openDemo();
        auto *document = window.currentDocument();
        auto *tree = window.findChild<QTreeWidget *>("layersTree");
        QTRY_VERIFY(window.findChild<QLabel *>("selectedLayerCount"));
        auto *actions = window.findChild<QDockWidget *>("panel_Actions");
        QVERIFY(actions);
        auto *steps = actions->findChild<QListWidget *>();
        QVERIFY(steps);
        QPushButton *record = nullptr;
        for (auto *button : actions->findChildren<QPushButton *>())
            if (button->text().contains("Record"))
                record = button;
        QVERIFY(record);
        record->click();
        choose(tree, {document->state.layers[0].id, document->state.layers[1].id});
        const int initial = document->state.layers.size();
        window.runCommand("Duplicate Layer");
        QCOMPARE(document->state.layers.size(), initial + 2);
        QCOMPARE(steps->count(), 0);
        const quint64 active = document->activeLayer()->id;
        choose(tree, {active});
        window.runCommand("Duplicate Layer");
        QCOMPARE(document->state.layers.size(), initial + 3);
        QCOMPARE(steps->count(), 1);
        QVERIFY(steps->item(0)->text().contains("Duplicate Layer"));
        document->activeLayer()->locked = true;
        window.runCommand("Delete Layer");
        QCOMPARE(document->state.layers.size(), initial + 3);
        QCOMPARE(steps->count(), 1);
        record->click();
    }
};
QTEST_MAIN(LayerOperationTests)
#include "layer_operations_tests.moc"
