#include "Document.h"
#include <QColorSpace>
#include <QSet>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace serika {
QImage renderLayerImage(const Layer &layer, const QSize &canvas, int bitDepth);

namespace {
QImage::Format storageFormat(const QImage &image) {
    if (image.format() == QImage::Format_RGBA32FPx4 ||
        image.format() == QImage::Format_RGBA32FPx4_Premultiplied ||
        image.format() == QImage::Format_RGBX32FPx4)
        return QImage::Format_RGBA32FPx4;
    return image.depth() > 32 ? QImage::Format_RGBA64 : QImage::Format_RGBA8888;
}
bool transparent(const QImage &image) {
    if (image.format() == QImage::Format_RGBA64) {
        for (int y = 0; y < image.height(); ++y) {
            const auto *line = reinterpret_cast<const QRgba64 *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x)
                if (line[x].alpha())
                    return false;
        }
    } else if (image.format() == QImage::Format_RGBA32FPx4) {
        for (int y = 0; y < image.height(); ++y) {
            const auto *line = reinterpret_cast<const float *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x)
                if (line[x * 4 + 3] != 0)
                    return false;
        }
    } else {
        for (int y = 0; y < image.height(); ++y) {
            const auto *line = image.constScanLine(y);
            for (int x = 0; x < image.width(); ++x)
                if (line[x * 4 + 3])
                    return false;
        }
    }
    return true;
}
QImage maskAtSize(const QImage &image, QSize size) {
    QImage result(size, QImage::Format_Grayscale8);
    result.fill(0);
    if (!image.isNull()) {
        const QImage input = image.convertToFormat(QImage::Format_Grayscale8);
        const QSize extent = input.size().boundedTo(size);
        for (int y = 0; y < extent.height(); ++y)
            std::copy_n(input.constScanLine(y), extent.width(), result.scanLine(y));
    }
    return result;
}
} // namespace

TileImage TileImage::fromImage(const QImage &image) {
    TileImage result;
    result.setImage(image);
    return result;
}
void TileImage::setImage(const QImage &image) {
    size = image.size();
    tiles.clear();
    if (image.isNull())
        return;
    format = storageFormat(image);
    const QImage input = image.convertToFormat(format);
    for (int y = 0; y < size.height(); y += TileSize)
        for (int x = 0; x < size.width(); x += TileSize) {
            const QImage tile = input.copy(QRect(x, y, TileSize, TileSize).intersected(input.rect()));
            if (!transparent(tile))
                tiles.insert(key(x / TileSize, y / TileSize), tile);
        }
}
QImage TileImage::image() const { return region(QRect(QPoint(), size)); }
QImage TileImage::region(const QRect &rect) const {
    if (rect.isEmpty())
        return {};
    QImage result(rect.size(), format);
    result.fill(Qt::transparent);
    const QRect clipped = rect.intersected(QRect(QPoint(), size));
    if (clipped.isEmpty())
        return result;
    for (int ty = clipped.top() / TileSize; ty <= clipped.bottom() / TileSize; ++ty)
        for (int tx = clipped.left() / TileSize; tx <= clipped.right() / TileSize; ++tx) {
            const auto it = tiles.constFind(key(tx, ty));
            if (it != tiles.cend()) {
                const QPoint origin(tx * TileSize, ty * TileSize);
                const QRect overlap = QRect(origin, it.value().size()).intersected(rect);
                const int bytesPerPixel = result.depth() / 8;
                // Copy straight-alpha bytes directly: a painter would round-trip through
                // premultiplied alpha and can lose a 16-bit value at partial transparency.
                for (int y = overlap.top(); y <= overlap.bottom(); ++y)
                    std::memcpy(result.scanLine(y - rect.top()) +
                                    (overlap.left() - rect.left()) * bytesPerPixel,
                                it.value().constScanLine(y - origin.y()) +
                                    (overlap.left() - origin.x()) * bytesPerPixel,
                                overlap.width() * bytesPerPixel);
            }
        }
    return result;
}
void TileImage::paint(const QRect &bounds, const std::function<void(QPainter &)> &draw) {
    const QRect clipped = bounds.intersected(QRect(QPoint(), size));
    if (clipped.isEmpty())
        return;
    for (int ty = clipped.top() / TileSize; ty <= clipped.bottom() / TileSize; ++ty)
        for (int tx = clipped.left() / TileSize; tx <= clipped.right() / TileSize; ++tx) {
            const quint64 tileKey = key(tx, ty);
            const QPoint origin(tx * TileSize, ty * TileSize);
            QImage tile = tiles.value(tileKey);
            const QSize tileSize = QSize(TileSize, TileSize).boundedTo(size - QSize(origin.x(), origin.y()));
            if (tile.isNull()) {
                tile = QImage(tileSize, format);
                tile.fill(Qt::transparent);
            } else if (tile.size() != tileSize) {
                QImage expanded(tileSize, format);
                expanded.fill(Qt::transparent);
                const QSize overlap = tile.size().boundedTo(tileSize);
                const int pixelBytes = tile.depth() / 8;
                for (int y = 0; y < overlap.height(); ++y)
                    std::memcpy(expanded.scanLine(y), tile.constScanLine(y), overlap.width() * pixelBytes);
                tile = expanded;
            }
            {
                // QPainter detaches only the modified tile. Undo snapshots retain other tiles.
                QPainter painter(&tile);
                painter.translate(-origin);
                painter.setClipRect(clipped);
                draw(painter);
            }
            if (transparent(tile))
                tiles.remove(tileKey);
            else
                tiles.insert(tileKey, tile);
        }
}

Document::Document(QObject *parent) : QObject(parent) {}
Document *Document::create(QSize size, QColor background, int bitDepth, QObject *parent) {
    auto *document = new Document(parent);
    document->state.size = size.expandedTo(QSize(1, 1));
    document->state.bitDepth = bitDepth == 16 ? 16 : bitDepth == 32 ? 32 : 8;
    Layer layer;
    layer.id = document->m_nextId++;
    layer.name = "Background";
    layer.pixels.size = document->state.size;
    layer.pixels.format = bitDepth == 16   ? QImage::Format_RGBA64
                          : bitDepth == 32 ? QImage::Format_RGBA32FPx4
                                           : QImage::Format_RGBA8888;
    if (background.alpha() != 0)
        layer.pixels.paint(QRect(QPoint(), document->state.size), [&](QPainter &p) {
            p.fillRect(QRect(QPoint(), document->state.size), background);
        });
    document->state.layers.append(layer);
    return document;
}
Layer *Document::activeLayer() {
    return state.activeIndex >= 0 && state.activeIndex < state.layers.size()
               ? &state.layers[state.activeIndex]
               : nullptr;
}
const Layer *Document::activeLayer() const {
    return state.activeIndex >= 0 && state.activeIndex < state.layers.size()
               ? &state.layers[state.activeIndex]
               : nullptr;
}
int Document::indexForId(quint64 id) const {
    for (int i = 0; i < state.layers.size(); ++i)
        if (state.layers[i].id == id)
            return i;
    return -1;
}
void Document::setActiveIndex(int index) {
    if (index < 0 || index >= state.layers.size() || index == state.activeIndex)
        return;
    state.activeIndex = index;
    emit activeLayerChanged();
    emit changed();
}
quint64 Document::addLayer(QString name, LayerKind kind) {
    quint64 id = 0;
    mutate("New Layer", [&] {
        for (const auto &layer : state.layers)
            m_nextId = std::max(m_nextId, layer.id + 1);
        Layer layer;
        layer.id = id = m_nextId++;
        layer.kind = kind;
        layer.name = name.isEmpty() ? QString("Layer %1").arg(state.layers.size() + 1) : name;
        layer.pixels.size = state.size;
        layer.pixels.format = state.bitDepth == 16   ? QImage::Format_RGBA64
                              : state.bitDepth == 32 ? QImage::Format_RGBA32FPx4
                                                     : QImage::Format_RGBA8888;
        if (kind == LayerKind::Group || kind == LayerKind::Artboard)
            layer.blendMode = "Pass Through";
        const Layer *active = activeLayer();
        if (active)
            layer.parentId = active->kind == LayerKind::Group ? active->id : active->parentId;
        int index = std::clamp(state.activeIndex + 1, 0, int(state.layers.size()));
        state.layers.insert(index, layer);
        state.activeIndex = index;
    });
    emit activeLayerChanged();
    return id;
}
void Document::removeActiveLayer() {
    if (!activeLayer() || activeLayer()->locked)
        return;
    mutate("Delete Layer", [&] {
        QSet<quint64> removed{activeLayer()->id};
        bool added = true;
        while (added) {
            added = false;
            for (const auto &l : state.layers)
                if (removed.contains(l.parentId) && !removed.contains(l.id)) {
                    removed.insert(l.id);
                    added = true;
                }
        }
        state.layers.erase(std::remove_if(state.layers.begin(), state.layers.end(),
                                          [&](const Layer &l) { return removed.contains(l.id); }),
                           state.layers.end());
        state.activeIndex = std::min(state.activeIndex, int(state.layers.size()) - 1);
    });
    emit activeLayerChanged();
}
void Document::duplicateActiveLayer() {
    if (!activeLayer())
        return;
    mutate("Duplicate Layer", [&] {
        const Layer original = *activeLayer();
        QSet<quint64> ids{original.id};
        bool added = true;
        while (added) {
            added = false;
            for (const auto &l : state.layers)
                if (ids.contains(l.parentId) && !ids.contains(l.id)) {
                    ids.insert(l.id);
                    added = true;
                }
        }
        QHash<quint64, quint64> mapping;
        for (const auto &l : state.layers)
            m_nextId = std::max(m_nextId, l.id + 1);
        QVector<Layer> copies;
        for (const auto &l : state.layers)
            if (ids.contains(l.id)) {
                copies.append(l);
                mapping.insert(l.id, m_nextId++);
            }
        int position = state.activeIndex + 1;
        for (auto &l : copies) {
            l.id = mapping.value(l.id);
            if (mapping.contains(l.parentId))
                l.parentId = mapping.value(l.parentId);
            if (l.id == mapping.value(original.id))
                l.name += " copy";
            state.layers.insert(position++, l);
        }
        state.activeIndex = indexForId(mapping.value(original.id));
    });
    emit activeLayerChanged();
}
void Document::moveLayer(int from, int to) {
    if (from < 0 || to < 0 || from >= state.layers.size() || to >= state.layers.size() || from == to ||
        state.layers[from].locked)
        return;
    mutate("Reorder Layers", [&] {
        quint64 active = activeLayer() ? activeLayer()->id : 0;
        state.layers.move(from, to);
        state.activeIndex = indexForId(active);
    });
    emit activeLayerChanged();
}
void Document::mergeDown() {
    if (!activeLayer() || state.activeIndex < 1 || activeLayer()->locked)
        return;
    const int upper = state.activeIndex, lower = upper - 1;
    if (state.layers[lower].parentId != state.layers[upper].parentId ||
        state.layers[lower].kind == LayerKind::Group || state.layers[upper].kind == LayerKind::Group)
        return;
    mutate("Merge Down", [&] {
        DocumentState pair = state;
        pair.layers = {state.layers[lower], state.layers[upper]};
        for (auto &l : pair.layers)
            l.parentId = 0;
        Layer merged;
        merged.id = state.layers[lower].id;
        merged.name = state.layers[lower].name;
        merged.parentId = state.layers[lower].parentId;
        merged.pixels = TileImage::fromImage(compositeDocument(pair, blendLinear));
        state.layers[lower] = merged;
        state.layers.removeAt(upper);
        state.activeIndex = lower;
    });
    emit activeLayerChanged();
}
void Document::flatten() {
    if (state.layers.isEmpty())
        return;
    const QImage flattened = composite();
    mutate("Flatten Image", [&] {
        for (const auto &layer : state.layers)
            m_nextId = std::max(m_nextId, layer.id + 1);
        Layer l;
        l.id = m_nextId++;
        l.name = "Background";
        l.pixels = TileImage::fromImage(flattened);
        state.layers = {l};
        state.activeIndex = 0;
    });
    emit activeLayerChanged();
}
void Document::addMask() {
    if (!activeLayer() || activeLayer()->locked)
        return;
    mutate("Add Layer Mask", [&] {
        Layer &l = *activeLayer();
        QSize size = l.pixels.size.isEmpty() ? state.size : l.pixels.size;
        l.mask = QImage(size, QImage::Format_Grayscale8);
        l.mask.fill(255);
        if (hasSelection()) {
            const QImage selection = state.selection.convertToFormat(QImage::Format_Grayscale8);
            for (int y = 0; y < size.height(); ++y)
                for (int x = 0; x < size.width(); ++x) {
                    const QPoint p = QPoint(x, y) + l.offset.toPoint();
                    l.mask.scanLine(y)[x] =
                        selection.rect().contains(p) ? selection.constScanLine(p.y())[p.x()] : 0;
                }
        }
        l.maskEnabled = true;
        l.maskTarget = true;
    });
}
void Document::addAdjustment(const QString &name, const QJsonObject &params) {
    mutate(name, [&] {
        addLayer(name, LayerKind::Adjustment);
        activeLayer()->adjustment = name;
        activeLayer()->parameters = params;
    });
}
void Document::beginTransaction(const QString &name) {
    if (m_inTransaction)
        return;
    m_before = state;
    m_beforeRevision = m_revision;
    m_transactionName = name;
    m_transactionChanged = false;
    m_inTransaction = true;
}
void Document::endTransaction() {
    if (!m_inTransaction)
        return;
    m_inTransaction = false;
    if (!m_transactionChanged) {
        m_before = DocumentState();
        return;
    }
    m_history.resize(m_historyCursor);
    const quint64 next = m_nextRevision++;
    m_history.append({m_transactionName, m_before, state, m_beforeRevision, next});
    m_revision = next;
    if (m_history.size() > std::max(1, historyLimit))
        m_history.remove(0, m_history.size() - std::max(1, historyLimit));
    m_historyCursor = m_history.size();
    m_before = DocumentState();
    emit historyChanged();
    emit changed();
}
void Document::cancelTransaction() {
    if (!m_inTransaction)
        return;
    state = m_before;
    m_revision = m_beforeRevision;
    m_inTransaction = false;
    m_transactionChanged = false;
    m_before = DocumentState();
    m_dirty = true;
    emit changed();
    emit activeLayerChanged();
    emit historyChanged();
}
void Document::mutate(const QString &name, const std::function<void()> &operation) {
    const bool outer = m_inTransaction;
    if (!outer)
        beginTransaction(name);
    operation();
    touch();
    if (!outer)
        endTransaction();
}
void Document::touch() {
    m_dirty = true;
    if (m_inTransaction)
        m_transactionChanged = true;
    else
        m_revision = m_nextRevision++;
    emit changed();
}
void Document::undo() {
    if (m_inTransaction)
        cancelTransaction();
    if (!canUndo())
        return;
    const auto &entry = m_history[--m_historyCursor];
    state = entry.before;
    m_revision = entry.beforeRevision;
    m_dirty = true;
    emit changed();
    emit activeLayerChanged();
    emit historyChanged();
}
void Document::redo() {
    if (m_inTransaction)
        cancelTransaction();
    if (!canRedo())
        return;
    const auto &entry = m_history[m_historyCursor++];
    state = entry.after;
    m_revision = entry.afterRevision;
    m_dirty = true;
    emit changed();
    emit activeLayerChanged();
    emit historyChanged();
}
bool Document::canUndo() const { return m_historyCursor > 0; }
bool Document::canRedo() const { return m_historyCursor < m_history.size(); }
QStringList Document::historyNames() const {
    QStringList result;
    for (int i = 0; i < m_historyCursor; ++i)
        result.append(m_history[i].name);
    return result;
}
void Document::clearHistory() {
    if (m_inTransaction)
        endTransaction();
    m_history.clear();
    m_historyCursor = 0;
    emit historyChanged();
    emit changed();
}
void Document::markSaved() {
    m_savedRevision = m_revision;
    emit changed();
}
bool Document::isModified() const {
    return m_revision != m_savedRevision || (m_inTransaction && m_transactionChanged);
}
QImage Document::composite() const {
    if (!m_dirty && m_cachedLinear == blendLinear)
        return m_composite;
    bool regional = m_inTransaction && !m_composite.isNull() && m_cachedLinear == blendLinear &&
                    m_composite.size() == state.size && m_before.size == state.size &&
                    m_before.bitDepth == state.bitDepth && m_before.iccProfile == state.iccProfile &&
                    m_before.layers.size() == state.layers.size();
    QRect dirty;
    if (regional)
        for (int i = 0; i < state.layers.size(); ++i) {
            const Layer &before = m_before.layers[i], &after = state.layers[i];
            const bool raster = after.kind == LayerKind::Pixel || after.kind == LayerKind::SmartObject;
            if (!raster || before.kind != after.kind || before.id != after.id ||
                before.visible != after.visible || before.offset != after.offset ||
                before.opacity != after.opacity || before.fill != after.fill ||
                before.blendMode != after.blendMode || after.blendMode == "Dissolve" ||
                before.clipped != after.clipped || before.parentId != after.parentId || after.parentId ||
                !before.effects.isEmpty() || !after.effects.isEmpty() ||
                before.mask.cacheKey() != after.mask.cacheKey() || before.maskEnabled != after.maskEnabled ||
                before.pixels.size != after.pixels.size || before.pixels.format != after.pixels.format ||
                after.offset != QPointF(after.offset.toPoint())) {
                regional = false;
                break;
            }
            QSet<quint64> keys;
            for (auto it = before.pixels.tiles.cbegin(); it != before.pixels.tiles.cend(); ++it)
                keys.insert(it.key());
            for (auto it = after.pixels.tiles.cbegin(); it != after.pixels.tiles.cend(); ++it)
                keys.insert(it.key());
            for (quint64 key : keys) {
                const auto old = before.pixels.tiles.constFind(key), now = after.pixels.tiles.constFind(key);
                if (old != before.pixels.tiles.cend() && now != after.pixels.tiles.cend() &&
                    old.value().cacheKey() == now.value().cacheKey())
                    continue;
                const int tx = int(quint32(key >> 32)), ty = int(quint32(key));
                dirty = dirty.united(
                    QRect(QPoint(tx * TileImage::TileSize, ty * TileImage::TileSize) + after.offset.toPoint(),
                          QSize(TileImage::TileSize, TileImage::TileSize)));
            }
        }
    if (regional) {
        dirty = dirty.intersected(QRect(QPoint(), state.size));
        if (!dirty.isEmpty()) {
            DocumentState region = state;
            region.size = dirty.size();
            for (Layer &layer : region.layers) {
                const QRect local = dirty.translated(-layer.offset.toPoint());
                layer.pixels = TileImage::fromImage(layer.pixels.region(local));
                if (!layer.mask.isNull())
                    layer.mask = layer.mask.copy(local);
                layer.offset = {};
            }
            const QImage patch = compositeDocument(region, blendLinear);
            const int pixelBytes = m_composite.depth() / 8;
            for (int y = 0; y < dirty.height(); ++y)
                std::memcpy(m_composite.scanLine(dirty.y() + y) + dirty.x() * pixelBytes,
                            patch.constScanLine(y), dirty.width() * pixelBytes);
        }
    } else
        m_composite = compositeDocument(state, blendLinear);
    m_dirty = false;
    m_cachedLinear = blendLinear;
    return m_composite;
}
QImage Document::layerImage(const Layer &layer) const {
    return renderLayerImage(layer, state.size, state.bitDepth);
}
QRect Document::selectionBounds() const {
    const qint64 key = state.selection.cacheKey();
    if (key == m_selectionCacheKey)
        return m_cachedSelectionBounds;
    m_selectionCacheKey = key;
    if (state.selection.isNull()) {
        m_cachedSelectionBounds = {};
        return {};
    }
    const QImage mask = state.selection.convertToFormat(QImage::Format_Grayscale8);
    int left = mask.width(), top = mask.height(), right = -1, bottom = -1;
    for (int y = 0; y < mask.height(); ++y)
        for (int x = 0; x < mask.width(); ++x)
            if (mask.constScanLine(y)[x]) {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x);
                bottom = std::max(bottom, y);
            }
    m_cachedSelectionBounds = right < 0 ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
    return m_cachedSelectionBounds;
}
bool Document::hasSelection() const { return !selectionBounds().isEmpty(); }
void Document::setSelection(const QImage &mask, const QString &operation) {
    mutate("Selection", [&] {
        QImage incoming = maskAtSize(mask, state.size);
        if (operation == "replace" || (state.selection.isNull() && operation == "add")) {
            state.selection = incoming;
            return;
        }
        QImage old = maskAtSize(state.selection, state.size);
        for (int y = 0; y < state.size.height(); ++y)
            for (int x = 0; x < state.size.width(); ++x) {
                const int a = old.constScanLine(y)[x], b = incoming.constScanLine(y)[x];
                int c = b;
                if (operation == "add")
                    c = std::max(a, b);
                else if (operation == "subtract")
                    c = std::max(0, a - b);
                else if (operation == "intersect")
                    c = std::min(a, b);
                else if (operation == "xor")
                    c = std::abs(a - b);
                old.scanLine(y)[x] = uchar(c);
            }
        state.selection = old;
    });
}
void Document::selectAll() {
    QImage mask(state.size, QImage::Format_Grayscale8);
    mask.fill(255);
    setSelection(mask);
}
void Document::deselect() {
    if (state.selection.isNull())
        return;
    mutate("Deselect", [&] { state.selection = {}; });
}
void Document::invertSelection() {
    mutate("Inverse Selection", [&] {
        QImage mask = maskAtSize(state.selection, state.size);
        for (int y = 0; y < mask.height(); ++y)
            for (int x = 0; x < mask.width(); ++x)
                mask.scanLine(y)[x] = 255 - mask.constScanLine(y)[x];
        state.selection = mask;
    });
}
void Document::resizeImage(QSize newSize, Qt::TransformationMode mode) {
    if (newSize.isEmpty() || newSize == state.size)
        return;
    mutate("Image Size", [&] {
        const qreal sx = qreal(newSize.width()) / state.size.width(),
                    sy = qreal(newSize.height()) / state.size.height();
        for (auto &l : state.layers) {
            if (!l.pixels.size.isEmpty()) {
                const QSize pixelSize(std::max(1, qRound(l.pixels.size.width() * sx)),
                                      std::max(1, qRound(l.pixels.size.height() * sy)));
                if (l.pixels.empty())
                    l.pixels.size = pixelSize;
                else
                    l.pixels.setImage(l.pixels.image().scaled(pixelSize, Qt::IgnoreAspectRatio, mode));
            }
            if (!l.mask.isNull())
                l.mask = l.mask.scaled(QSize(std::max(1, qRound(l.mask.width() * sx)),
                                             std::max(1, qRound(l.mask.height() * sy))),
                                       Qt::IgnoreAspectRatio, mode);
            l.offset = QPointF(l.offset.x() * sx, l.offset.y() * sy);
            QTransform transform;
            transform.scale(sx, sy);
            l.shape = transform.map(l.shape);
            l.strokeWidth *= std::sqrt(sx * sy);
            if (l.font.pixelSize() > 0)
                l.font.setPixelSize(std::max(1, qRound(l.font.pixelSize() * sy)));
            else if (l.font.pointSizeF() > 0)
                l.font.setPointSizeF(l.font.pointSizeF() * sy);
        }
        if (!state.selection.isNull())
            state.selection = state.selection.scaled(newSize, Qt::IgnoreAspectRatio, mode);
        for (auto &g : state.guides)
            g.position *= g.vertical ? sx : sy;
        state.size = newSize;
    });
}
void Document::resizeCanvas(QSize newSize, QPoint anchorOffset) {
    if (newSize.isEmpty() || (newSize == state.size && anchorOffset.isNull()))
        return;
    mutate("Canvas Size", [&] {
        for (auto &l : state.layers)
            l.offset += anchorOffset;
        if (!state.selection.isNull()) {
            QImage selection(newSize, QImage::Format_Grayscale8);
            selection.fill(0);
            const QImage old = state.selection.convertToFormat(QImage::Format_Grayscale8);
            for (int y = 0; y < old.height(); ++y)
                for (int x = 0; x < old.width(); ++x) {
                    QPoint p = QPoint(x, y) + anchorOffset;
                    if (selection.rect().contains(p))
                        selection.scanLine(p.y())[p.x()] = old.constScanLine(y)[x];
                }
            state.selection = selection;
        }
        for (auto &g : state.guides)
            g.position += g.vertical ? anchorOffset.x() : anchorOffset.y();
        state.size = newSize;
    });
}
void Document::crop(const QRect &rect) {
    if (rect.isEmpty())
        return;
    beginTransaction("Crop");
    resizeCanvas(rect.size(), -rect.topLeft());
    deselect();
    endTransaction();
}
} // namespace serika
