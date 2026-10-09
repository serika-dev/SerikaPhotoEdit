#include "CanvasView.h"
#include "document/TransformOperations.h"
#include <QLineF>
#include <QPainter>
#include <QSet>
#include <cmath>
#include <numbers>

namespace serika {
namespace {
QPointF layerOffset(const Document *document, const Layer &layer) {
    QPointF offset = layer.offset;
    QSet<quint64> seen{layer.id};
    quint64 parent = layer.parentId;
    while (parent && !seen.contains(parent)) {
        seen.insert(parent);
        const int index = document->indexForId(parent);
        if (index < 0)
            break;
        const Layer &ancestor = document->state.layers[index];
        offset += ancestor.offset;
        parent = ancestor.parentId;
    }
    return offset;
}
QPolygonF rectangleQuad(QRectF rectangle) {
    return {rectangle.topLeft(), rectangle.topRight(), rectangle.bottomRight(), rectangle.bottomLeft()};
}
} // namespace
bool CanvasView::beginTransform(const QString &mode) {
    cancelInteraction();
    const Layer *layer = m_document->activeLayer();
    if (!layer || layer->locked || layer->lockPosition || layer->kind == LayerKind::Group ||
        layer->kind == LayerKind::Adjustment || layer->kind == LayerKind::Artboard)
        return false;
    m_transformState = m_document->state;
    m_transformLayerId = layer->id;
    m_transformMode = mode;
    m_transformSource = m_document->layerImage(*layer);
    m_transformBounds = activeLayerBounds();
    if (layer->maskTarget && !layer->mask.isNull())
        m_transformBounds =
            QRectF(layerOffset(m_document, *layer) + (layer->maskLinked ? QPointF() : layer->maskOffset),
                   layer->mask.size());
    if (m_transformSource.isNull() || m_transformBounds.isEmpty())
        return false;
    m_transformQuad = rectangleQuad(m_transformBounds);
    m_transformPivot = m_transformBounds.center();
    m_previewTransform = {};
    m_transformPreview = m_document->composite();
    m_transformHandle = -1;
    m_transformActive = true;
    setCursor(Qt::ArrowCursor);
    emit transformPreviewChanged(true);
    update();
    return true;
}
void CanvasView::cancelTransform() {
    if (!m_transformActive)
        return;
    m_transformActive = false;
    m_transformHandle = -1;
    m_transformQuad.clear();
    m_transformPreview = {};
    m_transformSource = {};
    m_transformState = {};
    m_previewTransform = {};
    m_dragging = false;
    emit transformPreviewChanged(false);
    update();
}
bool CanvasView::commitTransform() {
    if (!m_transformActive)
        return false;
    const Layer *layer = m_document->activeLayer();
    if (!layer || layer->id != m_transformLayerId) {
        cancelTransform();
        return false;
    }
    const QPointF offset = layerOffset(m_document, *layer) +
                           (layer->maskTarget && !layer->maskLinked ? layer->maskOffset : QPointF());
    const QTransform local = QTransform::fromTranslate(offset.x(), offset.y()) * m_previewTransform *
                             QTransform::fromTranslate(-offset.x(), -offset.y());
    m_committingTransform = true;
    const bool success = m_previewTransform.isIdentity() || applyLayerTransform(m_document, local);
    m_committingTransform = false;
    if (success)
        cancelTransform();
    return success;
}
int CanvasView::transformHandleAt(QPointF point) const {
    if (!m_transformActive)
        return -1;
    for (int i = 0; i < 4; ++i)
        if (QLineF(point, m_transformQuad[i]).length() < 9 / m_zoom)
            return i;
    for (int i = 0; i < 4; ++i)
        if (QLineF(point, (m_transformQuad[i] + m_transformQuad[(i + 1) % 4]) / 2).length() < 9 / m_zoom)
            return i + 4;
    if (QLineF(point, m_transformPivot).length() < 7 / m_zoom)
        return 9;
    return m_transformQuad.containsPoint(point, Qt::OddEvenFill) ? 8 : 10;
}
void CanvasView::updateTransformDrag(QPointF point, Qt::KeyboardModifiers modifiers) {
    m_transformQuad = m_initialTransformQuad;
    const QPointF delta = point - m_start;
    if (m_transformHandle == 9) {
        m_transformPivot = m_initialTransformPivot + delta;
        update();
        return;
    }
    if (m_transformHandle == 8) {
        m_transformQuad.translate(delta);
        m_transformPivot = m_initialTransformPivot + delta;
    } else if (m_transformHandle == 10) {
        const QPointF pivot = m_initialTransformPivot;
        const qreal first = std::atan2(m_start.y() - pivot.y(), m_start.x() - pivot.x());
        const qreal next = std::atan2(point.y() - pivot.y(), point.x() - pivot.x());
        qreal degrees = (next - first) * 180 / std::numbers::pi;
        if (modifiers.testFlag(Qt::ShiftModifier))
            degrees = qRound(degrees / 15) * 15;
        QTransform rotation;
        rotation.translate(pivot.x(), pivot.y());
        rotation.rotate(degrees);
        rotation.translate(-pivot.x(), -pivot.y());
        m_transformQuad = rotation.map(m_transformQuad);
    } else if (m_transformHandle >= 0 && m_transformHandle < 8) {
        if (m_transformMode == "Skew") {
            const int edge = m_transformHandle >= 4                       ? m_transformHandle - 4
                             : std::abs(delta.x()) >= std::abs(delta.y()) ? (m_transformHandle < 2 ? 0 : 2)
                             : m_transformHandle == 0 || m_transformHandle == 3 ? 3
                                                                                : 1;
            const QPointF shear = edge == 0 || edge == 2 ? QPointF(delta.x(), 0) : QPointF(0, delta.y());
            m_transformQuad[edge] += shear;
            m_transformQuad[(edge + 1) % 4] += shear;
        } else if (m_transformHandle < 4 &&
                   (m_transformMode == "Distort" || m_transformMode == "Perspective" ||
                    modifiers.testFlag(Qt::ControlModifier))) {
            m_transformQuad[m_transformHandle] += delta;
            if (m_transformMode == "Perspective") {
                const int partner = m_transformHandle == 0   ? 1
                                    : m_transformHandle == 1 ? 0
                                    : m_transformHandle == 2 ? 3
                                                             : 2;
                m_transformQuad[partner] += QPointF(-delta.x(), delta.y());
            }
        } else {
            const QPolygonF unit = rectangleQuad({0, 0, 1, 1});
            QTransform toUnit;
            if (!QTransform::quadToQuad(m_initialTransformQuad, unit, toUnit))
                return;
            const bool center = modifiers.testFlag(Qt::AltModifier);
            QPointF handle = m_transformHandle < 4
                                 ? unit[m_transformHandle]
                                 : (unit[m_transformHandle - 4] + unit[(m_transformHandle - 3) % 4]) / 2;
            QPointF anchor = center                  ? toUnit.map(m_initialTransformPivot)
                             : m_transformHandle < 4 ? unit[(m_transformHandle + 2) % 4]
                                                     : QPointF(1 - handle.x(), 1 - handle.y());
            const QPointF target = toUnit.map(point);
            qreal sx = std::abs(handle.x() - anchor.x()) > .001
                           ? (target.x() - anchor.x()) / (handle.x() - anchor.x())
                           : 1;
            qreal sy = std::abs(handle.y() - anchor.y()) > .001
                           ? (target.y() - anchor.y()) / (handle.y() - anchor.y())
                           : 1;
            if (m_transformHandle == 4 || m_transformHandle == 6)
                sx = 1;
            if (m_transformHandle == 5 || m_transformHandle == 7)
                sy = 1;
            const bool aspect = m_transformHandle < 4 ? !modifiers.testFlag(Qt::ShiftModifier)
                                                      : modifiers.testFlag(Qt::ShiftModifier);
            if (aspect) {
                const qreal magnitude = std::max(std::abs(sx), std::abs(sy));
                sx = std::copysign(magnitude, sx);
                sy = std::copysign(magnitude, sy);
            }
            if (std::abs(sx) < .005 || std::abs(sy) < .005)
                return;
            QTransform resize;
            resize.translate(anchor.x(), anchor.y());
            resize.scale(sx, sy);
            resize.translate(-anchor.x(), -anchor.y());
            m_transformQuad = toUnit.inverted().map(resize.map(unit));
        }
    }
    if (!QTransform::quadToQuad(rectangleQuad(m_transformBounds), m_transformQuad, m_previewTransform) ||
        !m_previewTransform.isInvertible()) {
        m_transformQuad = m_initialTransformQuad;
        return;
    }
    renderTransformPreview();
    update();
}
void CanvasView::renderTransformPreview() {
    DocumentState preview = m_transformState;
    Layer &layer = preview.layers[preview.activeIndex];
    const QPointF offset = layerOffset(m_document, layer);
    const QPointF origin = offset + (layer.maskTarget && !layer.maskLinked ? layer.maskOffset : QPointF());
    const QTransform local = QTransform::fromTranslate(origin.x(), origin.y()) * m_previewTransform *
                             QTransform::fromTranslate(-origin.x(), -origin.y());
    const QImage source = layer.maskTarget ? layer.mask : m_transformSource;
    const QRectF bounds = local.mapRect(QRectF(source.rect()));
    if (bounds.width() * bounds.height() > 80000000 || bounds.width() < .5 || bounds.height() < .5)
        return;
    const QImage transformed = source.transformed(local, Qt::SmoothTransformation);
    if (transformed.isNull())
        return;
    if (layer.maskTarget) {
        layer.mask = transformed;
        layer.maskOffset =
            (layer.maskLinked ? QPointF() : layer.maskOffset) + bounds.toAlignedRect().topLeft();
        layer.maskLinked = false;
    } else {
        const QPointF displacement = bounds.toAlignedRect().topLeft();
        layer.pixels.setImage(transformed);
        layer.kind = LayerKind::Pixel;
        layer.parameters.remove("contentTransform");
        layer.smartFilters = {};
        layer.offset += displacement;
        if (layer.maskLinked && !layer.mask.isNull()) {
            QImage mask = makeMask(transformed.size(), 32);
            QPainter painter(&mask);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.setTransform(local * QTransform::fromTranslate(-displacement.x(), -displacement.y()));
            painter.drawImage(QPoint(), layer.mask);
            painter.end();
            layer.mask = normalizeMask(mask, m_document->state.bitDepth);
        } else if (!layer.maskLinked)
            layer.maskOffset -= displacement;
        if (layer.vectorMaskLinked && !layer.vectorMask.isEmpty())
            layer.vectorMask = (local * QTransform::fromTranslate(-displacement.x(), -displacement.y()))
                                   .map(layer.vectorMask);
        else if (!layer.vectorMaskLinked)
            layer.vectorMaskOffset -= displacement;
    }
    m_transformPreview = compositeDocument(preview, m_document->blendLinear);
}
void CanvasView::drawTransformPreview(QPainter &painter) {
    if (!m_transformActive)
        return;
    QPen outline(palette().color(QPalette::Highlight), 1);
    outline.setCosmetic(true);
    painter.setPen(outline);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(m_transformQuad);
    painter.setBrush(palette().color(QPalette::Base));
    const qreal radius = 3.5 / m_zoom;
    for (int i = 0; i < 4; ++i) {
        for (const QPointF point :
             {m_transformQuad[i], (m_transformQuad[i] + m_transformQuad[(i + 1) % 4]) / 2})
            painter.drawRect(QRectF(point - QPointF(radius, radius), QSizeF(radius * 2, radius * 2)));
    }
    painter.drawEllipse(m_transformPivot, 4 / m_zoom, 4 / m_zoom);
    painter.drawLine(m_transformPivot - QPointF(6 / m_zoom, 0), m_transformPivot + QPointF(6 / m_zoom, 0));
    painter.drawLine(m_transformPivot - QPointF(0, 6 / m_zoom), m_transformPivot + QPointF(0, 6 / m_zoom));
}
} // namespace serika
