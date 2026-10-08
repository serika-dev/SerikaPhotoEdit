#include "CanvasView.h"
#include "tools/LocalAlgorithms.h"
#include <QApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QSet>
#include <QTabletEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace serika {
namespace {
bool named(const QString &tool, std::initializer_list<const char *> names) {
    for (const char *name : names)
        if (tool == QLatin1String(name))
            return true;
    return false;
}
bool paintTool(const QString &tool) {
    return named(tool, {"Brush",
                        "Pencil",
                        "Color Replacement",
                        "Mixer Brush",
                        "Clone Stamp",
                        "Pattern Stamp",
                        "History Brush",
                        "Art History Brush",
                        "Eraser",
                        "Background Eraser",
                        "Blur",
                        "Sharpen",
                        "Smudge",
                        "Dodge",
                        "Burn",
                        "Sponge",
                        "Healing Brush",
                        "Spot Healing",
                        "Patch",
                        "Content-Aware Move",
                        "Quick Selection",
                        "Red Eye"});
}
bool shapeTool(const QString &tool) {
    return named(
        tool, {"Rectangle", "Ellipse", "Triangle", "Polygon", "Line", "Custom Shape", "Frame", "Artboard"});
}
bool selectionTool(const QString &tool) {
    return named(tool, {"Rectangular Marquee", "Elliptical Marquee", "Single Row", "Single Column", "Lasso",
                        "Polygonal Lasso", "Magnetic Lasso", "Object Selection"});
}
int selectionAt(const Document *document, int x, int y) {
    const QImage &mask = document->state.selection;
    if (mask.isNull())
        return 255;
    if (x < 0 || y < 0 || x >= mask.width() || y >= mask.height())
        return 0;
    return mask.format() == QImage::Format_Grayscale8 ? mask.constScanLine(y)[x] : qGray(mask.pixel(x, y));
}
QRect documentRect(const Document *document) { return QRect(QPoint(), document->state.size); }
QImage::Format paintingFormat(const Document *document) {
    return document->state.bitDepth == 16   ? QImage::Format_RGBA64
           : document->state.bitDepth == 32 ? QImage::Format_RGBA32FPx4
                                            : QImage::Format_ARGB32;
}
QPointF effectiveOffset(const Document *document, const Layer &layer) {
    QPointF offset = layer.offset;
    quint64 parent = layer.parentId;
    QSet<quint64> visited{layer.id};
    while (parent && !visited.contains(parent)) {
        visited.insert(parent);
        const int index = document->indexForId(parent);
        if (index < 0)
            break;
        const Layer &group = document->state.layers[index];
        offset += group.offset;
        parent = group.parentId;
    }
    return offset;
}
void shiftTiles(TileImage &image, QPoint shift) {
    if (shift.isNull())
        return;
    QHash<quint64, QImage> shifted;
    for (auto it = image.tiles.cbegin(); it != image.tiles.cend(); ++it) {
        const int x = int(quint32(it.key() >> 32)), y = int(quint32(it.key()));
        shifted.insert(TileImage::key(x + shift.x() / 256, y + shift.y() / 256), it.value());
    }
    image.tiles = std::move(shifted);
    image.size += QSize(shift.x(), shift.y());
}
QRect alphaBounds(const QImage &image) {
    if (image.isNull())
        return {};
    int top = 0, bottom = image.height() - 1, left = 0, right = image.width() - 1;
    auto rowHasAlpha = [&](int y) {
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(image.pixel(x, y)))
                return true;
        return false;
    };
    while (top <= bottom && !rowHasAlpha(top))
        ++top;
    if (top > bottom)
        return {};
    while (bottom > top && !rowHasAlpha(bottom))
        --bottom;
    auto columnHasAlpha = [&](int x) {
        for (int y = top; y <= bottom; ++y)
            if (qAlpha(image.pixel(x, y)))
                return true;
        return false;
    };
    while (left <= right && !columnHasAlpha(left))
        ++left;
    while (right > left && !columnHasAlpha(right))
        --right;
    return QRect(QPoint(left, top), QPoint(right, bottom));
}
QRectF layerBounds(const Document *document, const Layer &layer) {
    QRectF bounds;
    if (layer.kind == LayerKind::Shape || layer.kind == LayerKind::Artboard)
        bounds = layer.shape.boundingRect().adjusted(-layer.strokeWidth / 2, -layer.strokeWidth / 2,
                                                     layer.strokeWidth / 2, layer.strokeWidth / 2);
    else if (layer.kind == LayerKind::Pixel || layer.kind == LayerKind::SmartObject) {
        for (auto it = layer.pixels.tiles.cbegin(); it != layer.pixels.tiles.cend(); ++it) {
            const QPoint origin(int(quint32(it.key() >> 32)) * 256, int(quint32(it.key())) * 256);
            const QRect tile = alphaBounds(it.value());
            if (!tile.isEmpty())
                bounds = bounds.united(QRectF(tile.translated(origin)));
        }
    } else if (layer.kind != LayerKind::Group && layer.kind != LayerKind::Adjustment)
        bounds = alphaBounds(document->layerImage(layer));
    return bounds.isEmpty() ? QRectF() : bounds.translated(effectiveOffset(document, layer));
}
QColor weightedColor(QColor a, QColor b, qreal amount) {
    return QColor::fromRgbF(
        a.redF() * (1 - amount) + b.redF() * amount, a.greenF() * (1 - amount) + b.greenF() * amount,
        a.blueF() * (1 - amount) + b.blueF() * amount, a.alphaF() * (1 - amount) + b.alphaF() * amount);
}
QPainterPath polygonShape(const QRectF &rect, int sides) {
    QPainterPath path;
    const QPointF center = rect.center();
    for (int i = 0; i < sides; ++i) {
        const qreal angle = -std::numbers::pi / 2 + 2 * std::numbers::pi * i / sides;
        const QPointF p =
            center + QPointF(std::cos(angle) * rect.width() / 2, std::sin(angle) * rect.height() / 2);
        if (!i)
            path.moveTo(p);
        else
            path.lineTo(p);
    }
    path.closeSubpath();
    return path;
}
} // namespace

CanvasView::CanvasView(Document *document, QWidget *parent) : QWidget(parent), m_document(document) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(160, 120);
    connect(document, &Document::changed, this, [this] {
        updateSelectionOutline();
        update();
    });
    connect(&m_ants, &QTimer::timeout, this, [this] {
        m_antOffset = (m_antOffset + 1) % 8;
        if (m_selectionActive)
            update();
    });
    m_ants.start(150);
    updateSelectionOutline();
}
void CanvasView::setTool(const QString &tool) {
    if (m_dragging) {
        m_document->cancelTransaction();
        m_dragging = false;
    }
    m_tool = tool;
    m_lasso.clear();
    m_penLayerId = 0;
    m_penPath = {};
    m_perspectivePoints.clear();
    m_pathElement = -1;
    if (tool == "Hand")
        setCursor(Qt::OpenHandCursor);
    else if (tool == "Type" || tool == "Horizontal Type" || tool == "Vertical Type")
        setCursor(Qt::IBeamCursor);
    else if (tool == "Move" || tool == "Path Selection" || tool == "Direct Selection")
        setCursor(Qt::ArrowCursor);
    else
        setCursor(Qt::CrossCursor);
    emit toolChanged(tool);
    update();
}
void CanvasView::setForeground(QColor color) {
    m_foreground = color;
    update();
}
QColor CanvasView::foreground() const { return m_foreground; }
void CanvasView::setBackground(QColor color) { m_background = color; }
void CanvasView::setBrushSize(int size) {
    m_brushSize = std::clamp(size, 1, 5000);
    update();
}
void CanvasView::setBrushHardness(qreal hardness) { m_hardness = std::clamp(hardness, qreal(0), qreal(1)); }
void CanvasView::setBrushOpacity(qreal opacity) { m_opacity = std::clamp(opacity, qreal(0), qreal(1)); }
void CanvasView::setBrushFlow(qreal flow) { m_flow = std::clamp(flow, qreal(0), qreal(1)); }
void CanvasView::setBrushSpacing(qreal spacing) { m_spacing = std::clamp(spacing, qreal(0.01), qreal(2)); }
void CanvasView::setBrushAngle(qreal degrees) {
    m_brushAngle = degrees;
    update();
}
void CanvasView::setBrushRoundness(qreal roundness) {
    m_roundness = std::clamp(roundness, qreal(0.05), qreal(1));
    update();
}
void CanvasView::setBrushSmoothing(qreal smoothing) {
    m_smoothing = std::clamp(smoothing, qreal(0), qreal(1));
}
void CanvasView::setShowRulers(bool enabled) {
    m_rulers = enabled;
    update();
}
void CanvasView::setShowGrid(bool enabled) {
    m_grid = enabled;
    update();
}
void CanvasView::setShowGuides(bool enabled) {
    m_guides = enabled;
    update();
}
void CanvasView::setQuickMask(bool enabled) {
    m_quickMask = enabled;
    update();
}
void CanvasView::setSurround(QColor color) {
    m_surround = color;
    update();
}
QTransform CanvasView::viewTransform() const {
    QTransform transform;
    const qreal ruler = m_rulers ? 20 : 0;
    transform.translate((width() + ruler) / 2.0 + m_pan.x(), (height() + ruler) / 2.0 + m_pan.y());
    transform.rotate(m_rotation);
    transform.scale(m_flip ? -m_zoom : m_zoom, m_zoom);
    transform.translate(-m_document->state.size.width() / 2.0, -m_document->state.size.height() / 2.0);
    return transform;
}
QPointF CanvasView::toDocument(QPointF point) const { return viewTransform().inverted().map(point); }
QPointF CanvasView::fromDocument(QPointF point) const { return viewTransform().map(point); }
void CanvasView::setZoom(qreal zoom) {
    m_zoom = std::clamp(zoom, qreal(0.005), qreal(64));
    m_fitPending = false;
    emit zoomChanged(m_zoom);
    update();
}
qreal CanvasView::zoom() const { return m_zoom; }
void CanvasView::fitToView() {
    const qreal ruler = m_rulers ? 20 : 0;
    const QSize size = m_document->state.size;
    if (size.isEmpty())
        return;
    m_pan = {};
    m_rotation = 0;
    m_zoom =
        std::clamp(std::min((width() - ruler - 64) / size.width(), (height() - ruler - 64) / size.height()),
                   qreal(0.005), qreal(64));
    m_fitPending = false;
    emit zoomChanged(m_zoom);
    update();
}
void CanvasView::resetView() {
    m_pan = {};
    m_rotation = 0;
    m_flip = false;
    setZoom(1);
}
void CanvasView::rotateView(qreal degrees) {
    m_rotation = std::fmod(degrees, 360.0);
    update();
}
void CanvasView::flipView() {
    m_flip = !m_flip;
    update();
}
void CanvasView::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (m_fitPending)
        fitToView();
}

void CanvasView::updateSelectionOutline() {
    const QImage &mask = m_document->state.selection;
    const qint64 key = mask.isNull() ? 0 : mask.cacheKey();
    if (key == m_selectionKey)
        return;
    m_selectionKey = key;
    m_selectionOutline = {};
    m_maskOverlay = {};
    m_selectionActive = false;
    if (mask.isNull())
        return;
    const QRect bounds = m_document->selectionBounds();
    if (bounds.isEmpty())
        return;
    m_selectionActive = true;
    auto selected = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < mask.width() && y < mask.height() &&
               selectionAt(m_document, x, y) > 127;
    };
    // Coalesce adjacent edge pixels into line segments, keeping large rectangular masks small.
    for (int y = bounds.top(); y <= bounds.bottom() + 1; ++y) {
        int run = -1;
        for (int x = bounds.left(); x <= bounds.right() + 1; ++x) {
            const bool edge = x <= bounds.right() && (selected(x, y) != selected(x, y - 1));
            if (edge && run < 0)
                run = x;
            if (!edge && run >= 0) {
                m_selectionOutline.moveTo(run, y);
                m_selectionOutline.lineTo(x, y);
                run = -1;
            }
        }
    }
    for (int x = bounds.left(); x <= bounds.right() + 1; ++x) {
        int run = -1;
        for (int y = bounds.top(); y <= bounds.bottom() + 1; ++y) {
            const bool edge = y <= bounds.bottom() && (selected(x, y) != selected(x - 1, y));
            if (edge && run < 0)
                run = y;
            if (!edge && run >= 0) {
                m_selectionOutline.moveTo(x, run);
                m_selectionOutline.lineTo(x, y);
                run = -1;
            }
        }
    }
    m_maskOverlay = QImage(mask.size(), QImage::Format_ARGB32);
    for (int y = 0; y < mask.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(m_maskOverlay.scanLine(y));
        for (int x = 0; x < mask.width(); ++x)
            row[x] = qRgba(220, 44, 55, (255 - selectionAt(m_document, x, y)) / 2);
    }
}

void CanvasView::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), m_surround);
    const QRect viewport = m_rulers ? rect().adjusted(20, 20, 0, 0) : rect();
    painter.setClipRect(viewport);
    painter.setTransform(viewTransform());
    const QRect imageRect = documentRect(m_document);
    for (int i = 8; i > 0; --i) {
        QPen shadow(QColor(0, 0, 0, 8), i * 2 / m_zoom);
        painter.setPen(shadow);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(imageRect).translated(2 / m_zoom, 3 / m_zoom));
    }
    QPixmap checker(16, 16);
    checker.fill(QColor("#9a9a9a"));
    {
        QPainter tiles(&checker);
        tiles.fillRect(0, 0, 8, 8, QColor("#c8c8c8"));
        tiles.fillRect(8, 8, 8, 8, QColor("#c8c8c8"));
    }
    painter.fillRect(imageRect, QBrush(checker));
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1);
    painter.drawImage(QPoint(0, 0), m_document->composite());
    if (m_quickMask && !m_maskOverlay.isNull())
        painter.drawImage(QPoint(), m_maskOverlay);
    painter.save();
    painter.setClipRect(imageRect);
    if (m_grid && m_zoom >= 0.1) {
        const int spacing = std::max(1, m_document->state.metadata.value("gridSpacing").toInt(64));
        QPen grid(QColor(150, 150, 150, 95), 1 / m_zoom, Qt::DotLine);
        painter.setPen(grid);
        for (int x = 0; x < imageRect.width(); x += spacing)
            painter.drawLine(x, 0, x, imageRect.height());
        for (int y = 0; y < imageRect.height(); y += spacing)
            painter.drawLine(0, y, imageRect.width(), y);
    }
    if (m_guides) {
        painter.setPen(QPen(QColor("#58d1d8"), 1 / m_zoom));
        for (const Guide &guide : m_document->state.guides) {
            if (guide.vertical)
                painter.drawLine(QPointF(guide.position, 0), QPointF(guide.position, imageRect.height()));
            else
                painter.drawLine(QPointF(0, guide.position), QPointF(imageRect.width(), guide.position));
        }
    }
    if (m_guideDrag) {
        painter.setPen(QPen(QColor("#58d1d8"), 1 / m_zoom));
        if (m_guideDrag == 1)
            painter.drawLine(QPointF(m_guidePosition, 0), QPointF(m_guidePosition, imageRect.height()));
        else
            painter.drawLine(QPointF(0, m_guidePosition), QPointF(imageRect.width(), m_guidePosition));
    }
    painter.restore();
    if (!m_quickMask && !m_selectionOutline.isEmpty()) {
        painter.setRenderHint(QPainter::Antialiasing, false);
        QPen white(Qt::white, 1);
        white.setCosmetic(true);
        painter.setPen(white);
        painter.drawPath(m_selectionOutline);
        QPen ants(Qt::black, 1);
        ants.setCosmetic(true);
        ants.setDashPattern({4, 4});
        ants.setDashOffset(m_antOffset);
        painter.setPen(ants);
        painter.drawPath(m_selectionOutline);
    }
    if (m_dragging && (selectionTool(m_tool) || shapeTool(m_tool) ||
                       named(m_tool, {"Crop", "Perspective Crop", "Slice", "Slice Select", "Gradient"}))) {
        QPen preview(Qt::white, 1);
        preview.setCosmetic(true);
        preview.setStyle(Qt::DashLine);
        painter.setPen(preview);
        painter.setBrush(Qt::NoBrush);
        if (named(m_tool, {"Lasso", "Polygonal Lasso", "Magnetic Lasso"})) {
            painter.drawPolyline(m_lasso);
            if (!m_lasso.isEmpty())
                painter.drawLine(m_lasso.last(), toDocument(m_cursor));
        } else if (m_tool == "Elliptical Marquee" || m_tool == "Ellipse")
            painter.drawEllipse(m_dragRect);
        else if (m_tool == "Gradient" || m_tool == "Line")
            painter.drawLine(m_start, toDocument(m_cursor));
        else if (m_tool == "Triangle" || m_tool == "Polygon")
            painter.drawPath(polygonShape(m_dragRect, m_tool == "Triangle" ? 3 : 6));
        else
            painter.drawRect(m_dragRect);
        if (named(m_tool, {"Crop", "Perspective Crop"})) {
            painter.setPen(QPen(QColor(255, 255, 255, 95), 1 / m_zoom));
            for (int i = 1; i <= 2; ++i) {
                const qreal x = m_dragRect.left() + m_dragRect.width() * i / 3;
                const qreal y = m_dragRect.top() + m_dragRect.height() * i / 3;
                painter.drawLine(QPointF(x, m_dragRect.top()), QPointF(x, m_dragRect.bottom()));
                painter.drawLine(QPointF(m_dragRect.left(), y), QPointF(m_dragRect.right(), y));
            }
        }
    }
    if (!m_penPath.isEmpty()) {
        QPen pen(QColor("#e8893a"), 1);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(m_penPath);
        for (int i = 0; i < m_penPath.elementCount(); ++i) {
            auto element = m_penPath.elementAt(i);
            painter.fillRect(QRectF(element.x - 2 / m_zoom, element.y - 2 / m_zoom, 4 / m_zoom, 4 / m_zoom),
                             QColor("#e8893a"));
        }
    }
    if (m_tool == "Direct Selection") {
        const Layer *layer = m_document->activeLayer();
        if (layer && layer->kind == LayerKind::Shape) {
            painter.save();
            painter.translate(effectiveOffset(m_document, *layer));
            QPen outline(QColor("#e8893a"), 1);
            outline.setCosmetic(true);
            painter.setPen(outline);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(layer->shape);
            for (int i = 0; i < layer->shape.elementCount(); ++i) {
                const auto element = layer->shape.elementAt(i);
                painter.fillRect(
                    QRectF(element.x - 3 / m_zoom, element.y - 3 / m_zoom, 6 / m_zoom, 6 / m_zoom),
                    QColor("#e8893a"));
            }
            painter.restore();
        }
    }
    if (property("showTransformControls").toBool()) {
        const QRectF bounds = activeLayerBounds();
        if (!bounds.isEmpty()) {
            QPen outline(QColor("#dddddd"), 1);
            outline.setCosmetic(true);
            painter.setPen(outline);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(bounds);
            const QList<QPointF> handles{bounds.topLeft(),
                                         bounds.topRight(),
                                         bounds.bottomRight(),
                                         bounds.bottomLeft(),
                                         {bounds.center().x(), bounds.top()},
                                         {bounds.center().x(), bounds.bottom()},
                                         {bounds.left(), bounds.center().y()},
                                         {bounds.right(), bounds.center().y()}};
            painter.setBrush(QColor("#262626"));
            for (const QPointF handle : handles)
                painter.drawRect(
                    QRectF(handle - QPointF(3 / m_zoom, 3 / m_zoom), QSizeF(6 / m_zoom, 6 / m_zoom)));
        }
    }
    if (!m_perspectivePoints.isEmpty()) {
        QPen outline(QColor("#e8893a"), 1);
        outline.setCosmetic(true);
        painter.setPen(outline);
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(m_perspectivePoints);
        painter.drawLine(m_perspectivePoints.last(), toDocument(m_cursor));
        for (const QPointF point : m_perspectivePoints)
            painter.fillRect(QRectF(point - QPointF(3 / m_zoom, 3 / m_zoom), QSizeF(6 / m_zoom, 6 / m_zoom)),
                             QColor("#e8893a"));
    }
    if (paintTool(m_tool) && underMouse() && !m_panning) {
        const QPointF cursor = toDocument(m_cursor);
        QPen pen(Qt::white, 1);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        const qreal radius = m_brushSize / 2.0;
        painter.drawEllipse(cursor, radius, radius);
        pen.setColor(Qt::black);
        painter.setPen(pen);
        painter.drawEllipse(cursor, radius + 1 / m_zoom, radius + 1 / m_zoom);
        painter.drawLine(cursor + QPointF(-3 / m_zoom, 0), cursor + QPointF(3 / m_zoom, 0));
        painter.drawLine(cursor + QPointF(0, -3 / m_zoom), cursor + QPointF(0, 3 / m_zoom));
    }
    painter.resetTransform();
    painter.setClipping(false);
    if (m_tool == "Perspective Crop") {
        painter.setPen(QColor("#e8e8e8"));
        painter.drawText(QPointF(32, height() - 16), tr("Click four corners clockwise. Esc cancels."));
    }
    if (m_rulers) {
        painter.fillRect(0, 0, width(), 20, QColor("#363636"));
        painter.fillRect(0, 0, 20, height(), QColor("#363636"));
        painter.setPen(QColor("#bbbbbb"));
        QFont font = painter.font();
        font.setPixelSize(9);
        painter.setFont(font);
        const qreal ideal = 70 / m_zoom;
        const qreal power = std::pow(10, std::floor(std::log10(std::max(ideal, qreal(1)))));
        const qreal major = (ideal / power < 2 ? 2 : ideal / power < 5 ? 5 : 10) * power;
        const QPointF origin = fromDocument({0, 0});
        if (std::abs(std::remainder(m_rotation, 180.0)) < 0.1) {
            const qreal sign = m_flip ? -1 : 1;
            for (qreal value = std::floor(toDocument({20, 20}).x() / major) * major - major;
                 value <= std::max(toDocument({qreal(width()), 20}).x(), toDocument({20, 20}).x()) + major;
                 value += major) {
                const qreal x = origin.x() + value * m_zoom * sign;
                if (x < 20 || x > width())
                    continue;
                painter.drawLine(QPointF(x, 12), QPointF(x, 20));
                painter.drawText(QPointF(x + 3, 10), QString::number(value, 'f', 0));
                for (int i = 1; i < 5; ++i) {
                    const qreal minor = x + major * m_zoom * sign * i / 5;
                    painter.drawLine(QPointF(minor, 16), QPointF(minor, 20));
                }
            }
            for (qreal value = std::floor(toDocument({20, 20}).y() / major) * major;
                 value < toDocument({20, qreal(height())}).y() + major; value += major) {
                const qreal y = origin.y() + value * m_zoom;
                if (y < 20 || y > height())
                    continue;
                painter.drawLine(QPointF(12, y), QPointF(20, y));
                painter.save();
                painter.translate(10, y + 3);
                painter.rotate(-90);
                painter.drawText(QPointF(0, 0), QString::number(value, 'f', 0));
                painter.restore();
                for (int i = 1; i < 5; ++i) {
                    const qreal minor = y + major * m_zoom * i / 5;
                    painter.drawLine(QPointF(16, minor), QPointF(20, minor));
                }
            }
        }
        painter.fillRect(0, 0, 20, 20, QColor("#424242"));
        painter.setPen(QColor("#222222"));
        painter.drawLine(20, 0, 20, height());
        painter.drawLine(0, 20, width(), 20);
    }
}

void CanvasView::chooseSelectionOperation(Qt::KeyboardModifiers modifiers) {
    m_selectionOperation = modifiers.testFlag(Qt::ShiftModifier)
                               ? (modifiers.testFlag(Qt::AltModifier) ? "intersect" : "add")
                               : (modifiers.testFlag(Qt::AltModifier) ? "subtract" : "replace");
}
void CanvasView::sampleColor(QPointF point) {
    const QImage composite = m_document->composite();
    const QPoint p = point.toPoint();
    if (composite.rect().contains(p)) {
        m_foreground = composite.pixelColor(p);
        emit colorPicked(m_foreground);
    }
}

void CanvasView::applyImage(const QImage &input, QPoint documentOrigin, bool erase) {
    Layer *layer = m_document->activeLayer();
    if (!layer || layer->locked || input.isNull())
        return;
    QImage image = input.depth() > 32 ? input.copy() : input.convertToFormat(QImage::Format_ARGB32);
    const QRect docBounds = QRect(documentOrigin, image.size()).intersected(documentRect(m_document));
    if (docBounds.isEmpty())
        return;
    const bool selectionActive = m_selectionActive;
    if (m_quickMask) {
        QImage mask = m_document->state.selection.convertToFormat(QImage::Format_Grayscale8);
        if (mask.isNull()) {
            mask = QImage(m_document->state.size, QImage::Format_Grayscale8);
            mask.fill(255);
        }
        for (int y = 0; y < image.height(); ++y) {
            const int sy = y + documentOrigin.y();
            if (sy < 0 || sy >= mask.height())
                continue;
            auto *row = mask.scanLine(sy);
            for (int x = 0; x < image.width(); ++x) {
                const int sx = x + documentOrigin.x();
                if (sx < 0 || sx >= mask.width())
                    continue;
                const QRgb pixel = image.pixel(x, y);
                const int alpha = qAlpha(pixel);
                row[sx] = quint8((row[sx] * (255 - alpha) + (erase ? 255 : qGray(pixel)) * alpha) / 255);
            }
        }
        m_document->state.selection = mask;
        m_document->touch();
        return;
    }
    if (selectionActive)
        for (int y = 0; y < image.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const int sx = x + documentOrigin.x(), sy = y + documentOrigin.y();
                if (image.depth() > 32) {
                    QColor pixel = image.pixelColor(x, y);
                    pixel.setAlphaF(pixel.alphaF() * selectionAt(m_document, sx, sy) / 255.0);
                    image.setPixelColor(x, y, pixel);
                } else {
                    const int a = qAlpha(row[x]) * selectionAt(m_document, sx, sy) / 255;
                    row[x] = qRgba(qRed(row[x]), qGreen(row[x]), qBlue(row[x]), a);
                }
            }
        }
    QPoint localOrigin = documentOrigin - effectiveOffset(m_document, *layer).toPoint();
    if (layer->maskTarget && !layer->mask.isNull()) {
        QImage mask = layer->mask.convertToFormat(QImage::Format_Grayscale8);
        for (int y = 0; y < image.height(); ++y) {
            const int ly = y + localOrigin.y();
            if (ly < 0 || ly >= mask.height())
                continue;
            auto *row = mask.scanLine(ly);
            for (int x = 0; x < image.width(); ++x) {
                const int lx = x + localOrigin.x();
                if (lx < 0 || lx >= mask.width())
                    continue;
                const QRgb pixel = image.pixel(x, y);
                const int alpha = qAlpha(pixel);
                const int value = erase ? 0 : qGray(pixel);
                row[lx] = quint8((row[lx] * (255 - alpha) + value * alpha) / 255);
            }
        }
        layer->mask = mask;
    } else {
        if (layer->kind == LayerKind::Group || layer->kind == LayerKind::Artboard ||
            layer->kind == LayerKind::Adjustment)
            return;
        if (erase && layer->lockAlpha)
            return;
        if (layer->kind != LayerKind::Pixel && layer->kind != LayerKind::SmartObject) {
            const QImage raster = m_document->layerImage(*layer);
            layer->pixels.setImage(raster);
            layer->kind = LayerKind::Pixel;
        }
        if (!erase && !layer->lockAlpha) {
            QRect localBounds = docBounds.translated(-effectiveOffset(m_document, *layer).toPoint());
            const QPoint shift(localBounds.left() < 0 ? ((-localBounds.left() + 255) / 256) * 256 : 0,
                               localBounds.top() < 0 ? ((-localBounds.top() + 255) / 256) * 256 : 0);
            if (!shift.isNull()) {
                shiftTiles(layer->pixels, shift);
                shiftTiles(m_originalPixels, shift);
                layer->offset -= shift;
                localOrigin += shift;
                localBounds.translate(shift);
                if (!layer->mask.isNull()) {
                    QImage mask(layer->pixels.size, layer->mask.format());
                    mask.fill(Qt::white);
                    QPainter painter(&mask);
                    painter.drawImage(shift, layer->mask);
                    painter.end();
                    layer->mask = mask;
                }
            }
            layer->pixels.size =
                layer->pixels.size.expandedTo(QSize(localBounds.right() + 1, localBounds.bottom() + 1));
        }
        const QRect bounds =
            QRect(localOrigin, image.size()).intersected(QRect(QPoint(), layer->pixels.size));
        layer->pixels.paint(bounds, [&](QPainter &painter) {
            painter.setCompositionMode(erase ? QPainter::CompositionMode_DestinationOut
                                             : (layer->lockAlpha ? QPainter::CompositionMode_SourceAtop
                                                                 : QPainter::CompositionMode_SourceOver));
            painter.drawImage(localOrigin, image);
        });
    }
    m_document->touch();
}

void CanvasView::dab(QPointF point) {
    if (!documentRect(m_document).contains(point.toPoint()))
        return;
    if (m_tool == "Quick Selection") {
        QImage mask(m_document->state.size, QImage::Format_Grayscale8);
        mask.fill(0);
        const QImage image = m_document->composite();
        const QColor seed = image.pixelColor(point.toPoint());
        const qreal radius = m_brushSize / 2.0;
        const QRect bounds = QRectF(point - QPointF(radius, radius), QSizeF(radius * 2, radius * 2))
                                 .toAlignedRect()
                                 .intersected(image.rect());
        for (int y = bounds.top(); y <= bounds.bottom(); ++y)
            for (int x = bounds.left(); x <= bounds.right(); ++x) {
                const QColor sample = image.pixelColor(x, y);
                const int difference = std::abs(sample.red() - seed.red()) +
                                       std::abs(sample.green() - seed.green()) +
                                       std::abs(sample.blue() - seed.blue());
                if (QLineF(point, QPointF(x, y)).length() <= radius && difference < 110)
                    mask.scanLine(y)[x] = 255;
            }
        m_document->setSelection(mask, m_selectionOperation == "subtract" ? "subtract" : "add");
        return;
    }
    if (named(m_tool, {"Spot Healing", "Patch", "Content-Aware Move"})) {
        if (m_healMask.isNull()) {
            m_healMask = QImage(m_document->state.size, QImage::Format_Grayscale8);
            m_healMask.fill(0);
        }
        QPainter painter(&m_healMask);
        painter.setBrush(Qt::white);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(point, m_brushSize / 2.0, m_brushSize / 2.0);
        return;
    }
    const qreal pressure = std::clamp(m_pressure, qreal(0.05), qreal(1));
    const qreal radius = std::max(qreal(0.5), m_brushSize * pressure / 2);
    const int extent = int(std::ceil(radius)) + 1;
    const QPoint origin(int(std::floor(point.x())) - extent, int(std::floor(point.y())) - extent);
    QImage image(extent * 2 + 1, extent * 2 + 1, paintingFormat(m_document));
    image.fill(Qt::transparent);
    const Layer *layer = m_document->activeLayer();
    if (!layer || layer->locked)
        return;
    const bool erase = named(m_tool, {"Eraser", "Background Eraser"});
    const bool pixelFilter = named(m_tool, {"Blur", "Sharpen", "Smudge", "Dodge", "Burn", "Sponge",
                                            "Color Replacement", "Mixer Brush", "Red Eye"});
    QImage region;
    if (pixelFilter || m_tool == "Background Eraser")
        region = m_originalPixels.region(
            QRect(origin - effectiveOffset(m_document, *layer).toPoint() - QPoint(2, 2),
                  image.size() + QSize(4, 4)));
    QImage stamp;
    if (named(m_tool, {"Clone Stamp", "Healing Brush"}) && m_hasCloneSource) {
        const QPoint sourceOrigin =
            (m_cloneSource + QPointF(origin) - m_start - effectiveOffset(m_document, *layer)).toPoint();
        stamp = m_originalPixels.region(QRect(sourceOrigin, image.size()));
    }
    QPoint healingDifference;
    qreal healingBlueDifference = 0;
    if (m_tool == "Healing Brush" && !stamp.isNull()) {
        const QImage targetRegion = m_originalPixels.region(
            QRect(origin - effectiveOffset(m_document, *layer).toPoint(), image.size()));
        qreal tr = 0, tg = 0, tb = 0, sr = 0, sg = 0, sb = 0;
        int samples = 0;
        for (int y = 0; y < stamp.height(); y += 3)
            for (int x = 0; x < stamp.width(); x += 3) {
                const QColor target = targetRegion.pixelColor(x, y), sample = stamp.pixelColor(x, y);
                if (target.alpha() && sample.alpha()) {
                    tr += target.red();
                    tg += target.green();
                    tb += target.blue();
                    sr += sample.red();
                    sg += sample.green();
                    sb += sample.blue();
                    ++samples;
                }
            }
        if (samples) {
            healingDifference = QPoint(int((tr - sr) / samples), int((tg - sg) / samples));
            healingBlueDifference = (tb - sb) / samples;
        }
    }
    const qreal radians = m_brushAngle * std::numbers::pi / 180;
    const qreal cosine = std::cos(radians), sine = std::sin(radians);
    for (int y = 0; y < image.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const qreal dx = origin.x() + x + 0.5 - point.x(), dy = origin.y() + y + 0.5 - point.y();
            const qreal bx = dx * cosine + dy * sine, by = (-dx * sine + dy * cosine) / m_roundness;
            const qreal distance = std::sqrt(bx * bx + by * by) / radius;
            if (distance > 1)
                continue;
            const qreal edge =
                m_tool == "Pencil"
                    ? 1
                    : (distance <= m_hardness
                           ? 1
                           : std::pow((1 - distance) / std::max(qreal(0.001), 1 - m_hardness), 2));
            qreal alpha = edge * m_flow * m_opacity * pressure;
            QColor color = m_foreground;
            if (!stamp.isNull())
                color = stamp.pixelColor(x, y);
            else if (m_tool == "Clone Stamp" || m_tool == "Healing Brush")
                continue;
            else if (m_tool == "Pattern Stamp")
                color = ((origin.x() + x) / 12 + (origin.y() + y) / 12) % 2 ? m_foreground : m_background;
            else if (named(m_tool, {"History Brush", "Art History Brush"}) && !m_historySource.isNull()) {
                const QPoint p = origin + QPoint(x, y) - effectiveOffset(m_document, *layer).toPoint();
                if (m_historySource.rect().contains(p))
                    color = m_historySource.pixelColor(p);
            }
            if (m_tool == "Healing Brush") {
                color.setRed(std::clamp(color.red() + healingDifference.x(), 0, 255));
                color.setGreen(std::clamp(color.green() + healingDifference.y(), 0, 255));
                color.setBlue(std::clamp(color.blue() + int(healingBlueDifference), 0, 255));
            }
            if (pixelFilter && !region.isNull()) {
                const QColor old = region.pixelColor(x + 2, y + 2);
                color = old;
                if (m_tool == "Dodge" || m_tool == "Burn") {
                    const qreal amount = m_tool == "Dodge" ? 0.12 : -0.12;
                    color =
                        QColor::fromRgbF(std::clamp(old.redF() + amount, qreal(0), qreal(1)),
                                         std::clamp(old.greenF() + amount, qreal(0), qreal(1)),
                                         std::clamp(old.blueF() + amount, qreal(0), qreal(1)), old.alphaF());
                } else if (m_tool == "Sponge") {
                    const qreal gray = old.redF() * 0.2126 + old.greenF() * 0.7152 + old.blueF() * 0.0722;
                    color =
                        QColor::fromRgbF(old.redF() * 0.85 + gray * 0.15, old.greenF() * 0.85 + gray * 0.15,
                                         old.blueF() * 0.85 + gray * 0.15, old.alphaF());
                } else if (m_tool == "Color Replacement") {
                    color = m_foreground;
                    color.setHslF(m_foreground.hslHueF(), m_foreground.hslSaturationF(), old.lightnessF(),
                                  old.alphaF());
                } else if (m_tool == "Mixer Brush")
                    color = weightedColor(old, m_foreground, 0.25);
                else if (m_tool == "Smudge") {
                    const QPoint delta = (m_last - point).toPoint();
                    color = region.pixelColor(std::clamp(x + 2 + delta.x(), 0, region.width() - 1),
                                              std::clamp(y + 2 + delta.y(), 0, region.height() - 1));
                } else if (m_tool == "Red Eye") {
                    if (old.red() > old.green() * 1.4 && old.red() > old.blue() * 1.4)
                        color.setRed((old.green() + old.blue()) / 2);
                    else
                        alpha = 0;
                } else {
                    qreal r = 0, g = 0, b = 0, a = 0;
                    for (int sy = -1; sy <= 1; ++sy)
                        for (int sx = -1; sx <= 1; ++sx) {
                            const QColor sample = region.pixelColor(x + 2 + sx, y + 2 + sy);
                            r += sample.redF();
                            g += sample.greenF();
                            b += sample.blueF();
                            a += sample.alphaF();
                        }
                    r /= 9;
                    g /= 9;
                    b /= 9;
                    a /= 9;
                    if (m_tool == "Sharpen") {
                        r = std::clamp(old.redF() * 2 - r, qreal(0), qreal(1));
                        g = std::clamp(old.greenF() * 2 - g, qreal(0), qreal(1));
                        b = std::clamp(old.blueF() * 2 - b, qreal(0), qreal(1));
                        a = old.alphaF();
                    }
                    color = QColor::fromRgbF(r, g, b, a);
                }
            }
            if (m_tool == "Background Eraser" && !region.isNull()) {
                const QColor old = region.pixelColor(x + 2, y + 2);
                const int distanceColor = std::abs(old.red() - m_background.red()) +
                                          std::abs(old.green() - m_background.green()) +
                                          std::abs(old.blue() - m_background.blue());
                if (distanceColor > 100)
                    alpha = 0;
            }
            const int docX = origin.x() + x, docY = origin.y() + y;
            if (docX < 0 || docY < 0 || docX >= m_document->state.size.width() ||
                docY >= m_document->state.size.height())
                continue;
            const quint64 key = TileImage::key(docX / 256, docY / 256);
            auto coverage = m_strokeCoverage.find(key);
            if (coverage == m_strokeCoverage.end()) {
                QImage tile(256, 256, QImage::Format_Grayscale8);
                tile.fill(0);
                coverage = m_strokeCoverage.insert(key, tile);
            }
            uchar &covered = coverage.value().scanLine(docY % 256)[docX % 256];
            const qreal before = covered / 255.0;
            const qreal cap = std::min(qreal(1), m_opacity * pressure);
            const qreal after = std::max(before, before + (cap - before) * edge * m_flow * pressure);
            const qreal increment = before < 1 ? (after - before) / (1 - before) : 0;
            // Opacity caps a complete gesture; flow controls accumulation between dabs.
            alpha = std::min(alpha, increment);
            covered = quint8(std::clamp(qRound(after * 255), 0, 255));
            color.setAlphaF(std::clamp(color.alphaF() * alpha, qreal(0), qreal(1)));
            if (image.depth() > 32)
                image.setPixelColor(x, y, color);
            else
                row[x] = color.rgba();
        }
    }
    applyImage(image, origin, erase);
}
void CanvasView::drawStroke(QPointF from, QPointF to) {
    const qreal distance = QLineF(from, to).length();
    const qreal spacing = std::max(qreal(1), m_brushSize * m_spacing);
    const int count = std::max(1, int(std::ceil(distance / spacing)));
    for (int i = 1; i <= count; ++i)
        dab(from + (to - from) * (qreal(i) / count));
}

void CanvasView::mousePressEvent(QMouseEvent *event) {
    setFocus();
    m_cursor = event->position();
    const QPointF point = toDocument(event->position());
    if (event->button() == Qt::RightButton && event->modifiers().testFlag(Qt::AltModifier)) {
        m_hudBrush = true;
        m_start = event->position();
        m_initialBrushSize = m_brushSize;
        m_initialHardness = m_hardness;
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton || m_space || m_tool == "Hand") {
        m_panning = true;
        m_last = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    if (m_rulers && (event->position().x() < 20 || event->position().y() < 20)) {
        m_guideDrag = event->position().x() < 20 ? 1 : 2;
        m_guidePosition = m_guideDrag == 1 ? point.x() : point.y();
        return;
    }
    if (m_tool == "Rotate View") {
        m_rotating = true;
        m_start = event->position();
        m_initialRotation = m_rotation;
        return;
    }
    if (m_tool == "Zoom") {
        const QPointF anchor = point;
        setZoom(m_zoom * (event->modifiers().testFlag(Qt::AltModifier) ? 0.8 : 1.25));
        m_pan += event->position() - fromDocument(anchor);
        update();
        return;
    }
    if (named(m_tool, {"Eyedropper", "3D Material Eyedropper", "Color Sampler"}) ||
        (paintTool(m_tool) && event->modifiers().testFlag(Qt::AltModifier) &&
         !named(m_tool, {"Clone Stamp", "Healing Brush"}))) {
        sampleColor(point);
        return;
    }
    if (named(m_tool, {"Clone Stamp", "Healing Brush"}) && event->modifiers().testFlag(Qt::AltModifier)) {
        m_cloneSource = point;
        m_hasCloneSource = true;
        return;
    }
    if (named(m_tool,
              {"Type", "Horizontal Type", "Vertical Type", "Horizontal Type Mask", "Vertical Type Mask"})) {
        bool accepted = false;
        const QString text =
            QInputDialog::getMultiLineText(this, tr("Type"), tr("Text"), tr("Serika"), &accepted);
        if (accepted && !text.isEmpty())
            m_document->mutate("Type", [&] {
                m_document->addLayer("Text: " + text.left(24), LayerKind::Text);
                Layer *layer = m_document->activeLayer();
                layer->text = text;
                layer->font = property("typeFont").isValid() ? qvariant_cast<QFont>(property("typeFont"))
                                                             : QFont("Arial");
                const int typeSize = property("typeSize").toInt();
                layer->font.setPointSize(typeSize > 0 ? typeSize : 48);
                layer->color = m_foreground;
                layer->offset = point;
                if (m_tool == "Vertical Type")
                    layer->parameters["vertical"] = true;
            });
        return;
    }
    if (named(m_tool,
              {"Pen", "Freeform Pen", "Curvature Pen", "Add Anchor", "Delete Anchor", "Convert Point"})) {
        m_document->mutate("Pen path", [&] {
            if (!m_penLayerId) {
                m_document->addLayer("Pen path", LayerKind::Shape);
                m_penLayerId = m_document->activeLayer()->id;
                m_penPath.moveTo(point);
            } else if (m_tool == "Delete Anchor" && m_penPath.elementCount() > 1) {
                QPainterPath path;
                for (int i = 0; i < m_penPath.elementCount() - 1; ++i) {
                    const auto e = m_penPath.elementAt(i);
                    if (!i)
                        path.moveTo(e.x, e.y);
                    else
                        path.lineTo(e.x, e.y);
                }
                m_penPath = path;
            } else if (m_tool == "Curvature Pen") {
                const QPointF previous = m_penPath.currentPosition();
                const QPointF delta = point - previous;
                QPointF tangent = delta / 3;
                if (m_penPath.elementCount() > 1) {
                    const auto element = m_penPath.elementAt(m_penPath.elementCount() - 2);
                    tangent = (point - QPointF(element.x, element.y)) / 6;
                }
                m_penPath.cubicTo(previous + tangent, point - delta / 3, point);
            } else
                m_penPath.lineTo(point);
            const int index = m_document->indexForId(m_penLayerId);
            if (index >= 0) {
                Layer &layer = m_document->state.layers[index];
                layer.shape = m_penPath;
                layer.color = Qt::transparent;
                layer.stroke = m_foreground;
                layer.strokeWidth = std::max(1, m_brushSize / 10);
            }
        });
        update();
        return;
    }
    if (m_tool == "Note" || m_tool == "Count") {
        m_document->mutate(m_tool, [&] {
            const QString key = m_tool == "Note" ? "notes" : "counts";
            QJsonArray array = m_document->state.metadata.value(key).toArray();
            QJsonObject note{{"x", point.x()}, {"y", point.y()}};
            if (m_tool == "Note") {
                bool accepted;
                const QString text =
                    QInputDialog::getMultiLineText(this, tr("Note"), tr("Note text"), {}, &accepted);
                if (!accepted)
                    return;
                note["text"] = text;
            }
            array.append(note);
            m_document->state.metadata[key] = array;
        });
        return;
    }
    chooseSelectionOperation(event->modifiers());
    if (m_tool == "Perspective Crop") {
        m_perspectivePoints.append(point);
        if (m_perspectivePoints.size() == 4)
            perspectiveCrop();
        update();
        return;
    }
    if (m_tool == "Direct Selection") {
        Layer *layer = m_document->activeLayer();
        if (layer && layer->kind == LayerKind::Shape && !layer->locked) {
            qreal nearest = 14 / m_zoom;
            const QPointF local = point - effectiveOffset(m_document, *layer);
            m_pathElement = -1;
            for (int i = 0; i < layer->shape.elementCount(); ++i) {
                const auto anchor = layer->shape.elementAt(i);
                const qreal distance = QLineF(local, QPointF(anchor.x, anchor.y)).length();
                if (distance < nearest) {
                    nearest = distance;
                    m_pathElement = i;
                }
            }
            if (m_pathElement >= 0) {
                m_document->beginTransaction("Edit path anchor");
                m_initialPath = layer->shape;
                m_start = point;
                m_dragging = true;
                return;
            }
        }
    }
    if (m_tool == "Magic Wand" || m_tool == "Magic Eraser" || m_tool == "Paint Bucket") {
        m_document->beginTransaction(m_tool);
        bucket(point.toPoint());
        m_document->endTransaction();
        return;
    }
    if (m_tool == "Object Selection") {
        selectSubject();
        return;
    }
    if (named(m_tool, {"Polygonal Lasso", "Magnetic Lasso"})) {
        if (!m_dragging) {
            m_dragging = true;
            m_start = point;
            m_lasso.clear();
        }
        m_lasso.append(point);
        m_last = point;
        update();
        return;
    }
    m_dragging = true;
    m_moving = false;
    m_start = point;
    m_last = point;
    m_dragRect = QRectF(point, point);
    m_lasso.clear();
    m_lasso.append(point);
    if (m_tool == "Ruler")
        return;
    if (named(m_tool, {"Move", "Path Selection", "Direct Selection"}) ||
        event->modifiers().testFlag(Qt::ControlModifier)) {
        if (m_tool == "Move" && (!property("autoSelect").isValid() || property("autoSelect").toBool())) {
            for (int i = int(m_document->state.layers.size()) - 1; i >= 0; --i) {
                const Layer &candidate = m_document->state.layers[i];
                if (!candidate.visible || candidate.opacity <= 0 || candidate.kind == LayerKind::Group ||
                    candidate.kind == LayerKind::Artboard || candidate.kind == LayerKind::Adjustment)
                    continue;
                bool visible = true;
                quint64 parent = candidate.parentId;
                QSet<quint64> visited{candidate.id};
                while (parent && !visited.contains(parent)) {
                    visited.insert(parent);
                    const int ancestor = m_document->indexForId(parent);
                    if (ancestor < 0)
                        break;
                    const Layer &group = m_document->state.layers[ancestor];
                    if (!group.visible || group.opacity <= 0) {
                        visible = false;
                        break;
                    }
                    parent = group.parentId;
                }
                if (!visible)
                    continue;
                const QPoint local = (point - effectiveOffset(m_document, candidate)).toPoint();
                QColor color;
                if (candidate.kind == LayerKind::Pixel || candidate.kind == LayerKind::SmartObject)
                    color = candidate.pixels.region(QRect(local, QSize(1, 1))).pixelColor(0, 0);
                else {
                    const QImage image = m_document->layerImage(candidate);
                    if (image.rect().contains(local))
                        color = image.pixelColor(local);
                }
                if (color.alpha() < 5)
                    continue;
                if (candidate.maskEnabled && !candidate.mask.isNull() &&
                    (!candidate.mask.rect().contains(local) || qGray(candidate.mask.pixel(local)) < 5))
                    continue;
                m_document->setActiveIndex(i);
                break;
            }
        }
        Layer *layer = m_document->activeLayer();
        if (!layer || layer->locked || layer->lockPosition) {
            m_dragging = false;
            return;
        }
        m_moving = true;
        m_initialOffset = layer->offset;
        m_document->beginTransaction("Move layer");
        return;
    }
    if (paintTool(m_tool)) {
        Layer *layer = m_document->activeLayer();
        if (!layer || layer->locked) {
            m_dragging = false;
            return;
        }
        m_document->beginTransaction(m_tool);
        m_originalPixels = layer->pixels;
        if (named(m_tool, {"History Brush", "Art History Brush"}) && m_historySource.isNull() &&
            !layer->pixels.empty())
            m_historySource = layer->pixels.image();
        m_healMask = {};
        m_strokeCoverage.clear();
        dab(point);
    }
    update();
}

void CanvasView::mouseMoveEvent(QMouseEvent *event) {
    m_cursor = event->position();
    const QPointF point = toDocument(m_cursor);
    const QImage composite = m_document->composite();
    const QPoint sample = point.toPoint();
    emit cursorInfo(point, composite.rect().contains(sample) ? composite.pixelColor(sample)
                                                             : QColor(Qt::transparent));
    if (m_hudBrush) {
        setBrushSize(m_initialBrushSize + int(event->position().x() - m_start.x()));
        setBrushHardness(m_initialHardness - (event->position().y() - m_start.y()) / 200);
        return;
    }
    if (m_panning) {
        m_pan += event->position() - m_last;
        m_last = event->position();
        update();
        return;
    }
    if (m_rotating) {
        const QPointF center(width() / 2.0, height() / 2.0);
        const qreal first = std::atan2(m_start.y() - center.y(), m_start.x() - center.x());
        const qreal current = std::atan2(m_cursor.y() - center.y(), m_cursor.x() - center.x());
        rotateView(m_initialRotation + (current - first) * 180 / std::numbers::pi);
        return;
    }
    if (m_guideDrag) {
        m_guidePosition = m_guideDrag == 1 ? point.x() : point.y();
        update();
        return;
    }
    if (!m_dragging) {
        update();
        return;
    }
    if (m_pathElement >= 0) {
        Layer *layer = m_document->activeLayer();
        if (layer) {
            layer->shape = m_initialPath;
            const auto anchor = m_initialPath.elementAt(m_pathElement);
            const QPointF delta = point - m_start;
            layer->shape.setElementPositionAt(m_pathElement, anchor.x + delta.x(), anchor.y + delta.y());
            const int last = m_initialPath.elementCount() - 1;
            if (last > 0 && (m_pathElement == 0 || m_pathElement == last)) {
                const auto first = m_initialPath.elementAt(0), end = m_initialPath.elementAt(last);
                if (first.x == end.x && first.y == end.y)
                    layer->shape.setElementPositionAt(m_pathElement == 0 ? last : 0, anchor.x + delta.x(),
                                                      anchor.y + delta.y());
            }
            m_document->touch();
        }
    } else if (m_moving) {
        Layer *layer = m_document->activeLayer();
        if (layer && !layer->locked && !layer->lockPosition) {
            QPointF delta = point - m_start;
            if (event->modifiers().testFlag(Qt::ShiftModifier)) {
                if (std::abs(delta.x()) > std::abs(delta.y()))
                    delta.setY(0);
                else
                    delta.setX(0);
            }
            layer->offset = m_initialOffset + delta;
            m_document->touch();
        }
    } else if (paintTool(m_tool)) {
        const QPointF smoothed = m_last + (point - m_last) * (1 - m_smoothing * 0.85);
        drawStroke(m_last, smoothed);
        m_last = smoothed;
        update();
        return;
    } else if (m_tool == "Lasso" || m_tool == "Magnetic Lasso") {
        QPointF snapped = point;
        if (m_tool == "Magnetic Lasso" && composite.rect().contains(sample)) {
            int strongest = 0;
            for (int dy = -5; dy <= 5; ++dy)
                for (int dx = -5; dx <= 5; ++dx) {
                    const QPoint p = sample + QPoint(dx, dy);
                    if (!composite.rect().adjusted(1, 1, -1, -1).contains(p))
                        continue;
                    const int strength = std::abs(qGray(composite.pixel(p + QPoint(1, 0))) -
                                                  qGray(composite.pixel(p - QPoint(1, 0)))) +
                                         std::abs(qGray(composite.pixel(p + QPoint(0, 1))) -
                                                  qGray(composite.pixel(p - QPoint(0, 1))));
                    if (strength > strongest) {
                        strongest = strength;
                        snapped = p;
                    }
                }
        }
        if (QLineF(m_lasso.last(), snapped).length() > 1 / m_zoom)
            m_lasso.append(snapped);
    } else {
        QPointF end = point;
        if (event->modifiers().testFlag(Qt::ShiftModifier) && !event->modifiers().testFlag(Qt::AltModifier)) {
            const qreal extent =
                std::max(std::abs(point.x() - m_start.x()), std::abs(point.y() - m_start.y()));
            end = m_start + QPointF(point.x() >= m_start.x() ? extent : -extent,
                                    point.y() >= m_start.y() ? extent : -extent);
        }
        m_dragRect = QRectF(m_start, end).normalized();
        if (m_tool == "Single Row")
            m_dragRect = QRectF(0, point.y(), m_document->state.size.width(), 1);
        if (m_tool == "Single Column")
            m_dragRect = QRectF(point.x(), 0, 1, m_document->state.size.height());
    }
    m_last = point;
    update();
}

void CanvasView::finishSelection() {
    QImage mask(m_document->state.size, QImage::Format_Grayscale8);
    mask.fill(0);
    QPainter painter(&mask);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    if (named(m_tool, {"Lasso", "Polygonal Lasso", "Magnetic Lasso"}))
        painter.drawPolygon(m_lasso);
    else if (m_tool == "Elliptical Marquee")
        painter.drawEllipse(m_dragRect);
    else
        painter.drawRect(m_dragRect);
    painter.end();
    m_document->setSelection(mask, m_selectionOperation);
    m_lasso.clear();
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event) {
    if (m_hudBrush) {
        m_hudBrush = false;
        return;
    }
    if (m_panning) {
        m_panning = false;
        setCursor(m_tool == "Hand" ? Qt::OpenHandCursor : Qt::CrossCursor);
        return;
    }
    if (m_rotating) {
        m_rotating = false;
        return;
    }
    if (m_guideDrag) {
        const int direction = m_guideDrag;
        const qreal position = m_guidePosition;
        m_guideDrag = 0;
        if (event->position().x() > 20 && event->position().y() > 20)
            m_document->mutate("New guide",
                               [&] { m_document->state.guides.append({direction == 1, position}); });
        update();
        return;
    }
    if (!m_dragging || event->button() != Qt::LeftButton)
        return;
    if (named(m_tool, {"Polygonal Lasso", "Magnetic Lasso"}))
        return;
    const QPointF end = toDocument(event->position());
    if (m_pathElement >= 0) {
        m_document->endTransaction();
        m_pathElement = -1;
    } else if (m_moving) {
        m_document->endTransaction();
        m_moving = false;
    } else if (selectionTool(m_tool))
        finishSelection();
    else if (paintTool(m_tool)) {
        if (!m_healMask.isNull()) {
            Layer *layer = m_document->activeLayer();
            if (layer) {
                QImage localMask(layer->pixels.size, QImage::Format_Grayscale8);
                localMask.fill(0);
                QPainter painter(&localMask);
                painter.drawImage(-effectiveOffset(m_document, *layer), m_healMask);
                painter.end();
                const QImage source = layer->pixels.image();
                const QImage synthesized = synthesizePatches(source, localMask);
                QImage patch = synthesized;
                for (int y = 0; y < patch.height(); ++y) {
                    auto *row = reinterpret_cast<QRgb *>(patch.scanLine(y));
                    for (int x = 0; x < patch.width(); ++x) {
                        const int alpha = qGray(localMask.pixel(x, y));
                        if (patch.depth() > 32) {
                            QColor pixel = patch.pixelColor(x, y);
                            pixel.setAlphaF(pixel.alphaF() * alpha / 255.0);
                            patch.setPixelColor(x, y, pixel);
                        } else
                            row[x] = qRgba(qRed(row[x]), qGreen(row[x]), qBlue(row[x]),
                                           qAlpha(row[x]) * alpha / 255);
                    }
                }
                applyImage(patch, effectiveOffset(m_document, *layer).toPoint());
            }
        }
        m_document->endTransaction();
        m_healMask = {};
    } else if (named(m_tool, {"Move", "Path Selection", "Direct Selection"}))
        m_document->endTransaction();
    else if (named(m_tool, {"Crop", "Perspective Crop"})) {
        const QRect crop = m_dragRect.toAlignedRect().intersected(documentRect(m_document));
        if (crop.width() > 1 && crop.height() > 1)
            m_document->crop(crop);
    } else if (m_tool == "Gradient")
        applyGradient();
    else if (shapeTool(m_tool) && QLineF(m_start, end).length() > 1) {
        m_document->mutate(m_tool, [&] {
            m_document->addLayer(m_tool, m_tool == "Artboard" ? LayerKind::Artboard : LayerKind::Shape);
            Layer *layer = m_document->activeLayer();
            QPainterPath path;
            if (m_tool == "Ellipse")
                path.addEllipse(m_dragRect);
            else if (m_tool == "Triangle" || m_tool == "Polygon")
                path = polygonShape(m_dragRect, m_tool == "Triangle" ? 3 : 6);
            else if (m_tool == "Line") {
                path.moveTo(m_start);
                path.lineTo(end);
            } else
                path.addRect(m_dragRect);
            layer->shape = path;
            layer->color = m_tool == "Line" || m_tool == "Frame" ? QColor(Qt::transparent) : m_foreground;
            layer->stroke = m_tool == "Line" || m_tool == "Frame" ? m_foreground : QColor(Qt::transparent);
            layer->strokeWidth = std::max(1, m_brushSize / 10);
        });
    } else if (m_tool == "Ruler") {
        m_document->state.metadata["rulerDistance"] = QLineF(m_start, end).length();
        m_document->touch();
    } else if (m_tool == "Slice" || m_tool == "Slice Select")
        m_document->mutate("Slice", [&] {
            QJsonArray slices = m_document->state.metadata.value("slices").toArray();
            slices.append(QJsonObject{{"x", m_dragRect.x()},
                                      {"y", m_dragRect.y()},
                                      {"width", m_dragRect.width()},
                                      {"height", m_dragRect.height()}});
            m_document->state.metadata["slices"] = slices;
        });
    else
        m_document->endTransaction();
    m_dragging = false;
    m_dragRect = {};
    m_pressure = 1;
    update();
}
void CanvasView::mouseDoubleClickEvent(QMouseEvent *event) {
    if (named(m_tool, {"Polygonal Lasso", "Magnetic Lasso"}) && m_dragging) {
        m_lasso.append(toDocument(event->position()));
        finishSelection();
        m_dragging = false;
        update();
    } else if (named(m_tool, {"Pen", "Curvature Pen", "Freeform Pen"}) && !m_penPath.isEmpty()) {
        m_document->mutate("Close path", [&] {
            m_penPath.closeSubpath();
            const int index = m_document->indexForId(m_penLayerId);
            if (index >= 0) {
                m_document->state.layers[index].shape = m_penPath;
                m_document->state.layers[index].color = m_foreground;
            }
        });
        m_penLayerId = 0;
        m_penPath = {};
        m_perspectivePoints.clear();
        m_pathElement = -1;
        update();
    } else if (m_tool == "Hand")
        fitToView();
    else if (m_tool == "Zoom")
        resetView();
}

void CanvasView::wheelEvent(QWheelEvent *event) {
    const QPointF anchor = toDocument(event->position());
    const qreal delta = event->angleDelta().y() / 120.0;
    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        m_pan += QPointF(event->angleDelta().y() / 2.0, event->angleDelta().x() / 2.0);
        update();
    } else {
        setZoom(m_zoom * std::pow(1.15, delta));
        m_pan += event->position() - fromDocument(anchor);
        update();
    }
    event->accept();
}
void CanvasView::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_space = true;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (m_dragging)
            m_document->cancelTransaction();
        m_dragging = false;
        m_lasso.clear();
        m_penLayerId = 0;
        m_penPath = {};
        m_dragRect = {};
        update();
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && m_dragging &&
        named(m_tool, {"Polygonal Lasso", "Magnetic Lasso"})) {
        finishSelection();
        m_dragging = false;
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_BracketLeft) {
        setBrushSize(int(m_brushSize / 1.15));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_BracketRight) {
        setBrushSize(int(m_brushSize * 1.15) + 1);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_X) {
        std::swap(m_foreground, m_background);
        emit colorPicked(m_foreground);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_D) {
        m_foreground = Qt::black;
        m_background = Qt::white;
        emit colorPicked(m_foreground);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Q) {
        setQuickMask(!m_quickMask);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
void CanvasView::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_space = false;
        setCursor(m_tool == "Hand" ? Qt::OpenHandCursor : Qt::CrossCursor);
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}
void CanvasView::tabletEvent(QTabletEvent *event) {
    m_pressure = event->pressure();
    if (std::abs(event->xTilt()) + std::abs(event->yTilt()) > 1) {
        m_brushAngle = std::atan2(event->yTilt(), event->xTilt()) * 180 / std::numbers::pi;
        m_roundness =
            std::clamp(1 - std::hypot(event->xTilt(), event->yTilt()) / 120.0, qreal(0.25), qreal(1));
    }
    if (event->type() == QEvent::TabletPress) {
        QMouseEvent mouse(QEvent::MouseButtonPress, event->position(), event->globalPosition(),
                          Qt::LeftButton, Qt::LeftButton, event->modifiers());
        mousePressEvent(&mouse);
    } else if (event->type() == QEvent::TabletMove) {
        QMouseEvent mouse(QEvent::MouseMove, event->position(), event->globalPosition(), Qt::NoButton,
                          Qt::LeftButton, event->modifiers());
        mouseMoveEvent(&mouse);
    } else if (event->type() == QEvent::TabletRelease) {
        QMouseEvent mouse(QEvent::MouseButtonRelease, event->position(), event->globalPosition(),
                          Qt::LeftButton, Qt::NoButton, event->modifiers());
        mouseReleaseEvent(&mouse);
        m_pressure = 1;
    }
    event->accept();
}

void CanvasView::fillSelection(QColor color) {
    if (!m_document->activeLayer())
        return;
    m_document->mutate("Fill", [&] {
        const QRect bounds =
            m_document->hasSelection() ? m_document->selectionBounds() : documentRect(m_document);
        if (bounds.isEmpty())
            return;
        QImage image(bounds.size(), QImage::Format_ARGB32);
        color.setAlphaF(color.alphaF() * m_opacity);
        image.fill(color);
        applyImage(image, bounds.topLeft());
    });
}
void CanvasView::strokeSelection(QColor color, int width) {
    if (!m_document->hasSelection())
        return;
    updateSelectionOutline();
    m_document->mutate("Stroke selection", [&] {
        const QRect bounds = m_document->selectionBounds()
                                 .adjusted(-width, -width, width, width)
                                 .intersected(documentRect(m_document));
        QImage image(bounds.size(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.translate(-bounds.topLeft());
        painter.setPen(QPen(color, width));
        painter.drawPath(m_selectionOutline);
        painter.end();
        applyImage(image, bounds.topLeft());
    });
}
void CanvasView::applyGradient() {
    if (!m_document->activeLayer())
        return;
    m_document->mutate("Gradient", [&] {
        const QRect bounds =
            m_document->hasSelection() ? m_document->selectionBounds() : documentRect(m_document);
        if (bounds.isEmpty())
            return;
        QImage image(bounds.size(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        QLinearGradient gradient(m_start - QPointF(bounds.topLeft()), m_last - QPointF(bounds.topLeft()));
        gradient.setColorAt(0, m_foreground);
        gradient.setColorAt(1, m_background);
        painter.setOpacity(m_opacity);
        painter.fillRect(image.rect(), gradient);
        painter.end();
        applyImage(image, bounds.topLeft());
    });
}
void CanvasView::bucket(QPoint point) {
    const QImage source = m_document->composite().convertToFormat(QImage::Format_ARGB32);
    if (!source.rect().contains(point))
        return;
    const QRgb target = source.pixel(point);
    const int tolerance = 32;
    QImage mask(source.size(), QImage::Format_Grayscale8);
    mask.fill(0);
    QVector<QPoint> queue;
    queue.append(point);
    mask.scanLine(point.y())[point.x()] = 255;
    auto enqueue = [&](int x, int y) {
        if (mask.constScanLine(y)[x])
            return;
        const QRgb sample = source.pixel(x, y);
        if (std::abs(qRed(sample) - qRed(target)) + std::abs(qGreen(sample) - qGreen(target)) +
                    std::abs(qBlue(sample) - qBlue(target)) >
                tolerance * 3 ||
            std::abs(qAlpha(sample) - qAlpha(target)) > tolerance)
            return;
        mask.scanLine(y)[x] = 255;
        queue.append(QPoint(x, y));
    };
    int left = point.x(), top = point.y(), right = point.x(), bottom = point.y();
    for (qsizetype i = 0; i < queue.size(); ++i) {
        const QPoint p = queue[i];
        left = std::min(left, p.x());
        top = std::min(top, p.y());
        right = std::max(right, p.x());
        bottom = std::max(bottom, p.y());
        if (p.x() > 0)
            enqueue(p.x() - 1, p.y());
        if (p.x() + 1 < source.width())
            enqueue(p.x() + 1, p.y());
        if (p.y() > 0)
            enqueue(p.x(), p.y() - 1);
        if (p.y() + 1 < source.height())
            enqueue(p.x(), p.y() + 1);
    }
    if (m_tool == "Magic Wand") {
        m_document->setSelection(mask, m_selectionOperation);
        return;
    }
    const QRect bounds(QPoint(left, top), QPoint(right, bottom));
    QImage image(bounds.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    for (const QPoint p : queue) {
        QColor color = m_foreground;
        color.setAlphaF(m_opacity);
        image.setPixelColor(p - bounds.topLeft(), color);
    }
    applyImage(image, bounds.topLeft(), m_tool == "Magic Eraser");
}
void CanvasView::selectSubject() {
    const QImage mask = selectSubjectLocally(m_document->composite());
    if (!mask.isNull())
        m_document->setSelection(mask);
}
QRectF CanvasView::activeLayerBounds() const {
    const Layer *active = m_document->activeLayer();
    if (!active)
        return {};
    if (active->kind != LayerKind::Group)
        return layerBounds(m_document, *active);
    QSet<quint64> descendants{active->id};
    bool found = true;
    while (found) {
        found = false;
        for (const Layer &layer : m_document->state.layers)
            if (descendants.contains(layer.parentId) && !descendants.contains(layer.id)) {
                descendants.insert(layer.id);
                found = true;
            }
    }
    QRectF bounds;
    for (const Layer &layer : m_document->state.layers)
        if (descendants.contains(layer.id) && layer.kind != LayerKind::Group)
            bounds = bounds.united(layerBounds(m_document, layer));
    return bounds;
}
void CanvasView::perspectiveCrop() {
    if (m_perspectivePoints.size() != 4)
        return;
    const QPolygonF quad = m_perspectivePoints;
    m_perspectivePoints.clear();
    const int width =
        std::max(1, qRound((QLineF(quad[0], quad[1]).length() + QLineF(quad[3], quad[2]).length()) / 2));
    const int height =
        std::max(1, qRound((QLineF(quad[0], quad[3]).length() + QLineF(quad[1], quad[2]).length()) / 2));
    QTransform transform;
    if (!QTransform::quadToQuad(
            quad, QPolygonF{{0, 0}, {qreal(width), 0}, {qreal(width), qreal(height)}, {0, qreal(height)}},
            transform))
        return;
    m_document->mutate("Perspective Crop", [&] {
        QVector<QPointF> offsets;
        for (const Layer &layer : m_document->state.layers)
            offsets.append(effectiveOffset(m_document, layer));
        for (int i = 0; i < m_document->state.layers.size(); ++i) {
            Layer &layer = m_document->state.layers[i];
            const QPointF offset = offsets[i];
            if (layer.kind == LayerKind::Shape || layer.kind == LayerKind::Artboard) {
                QTransform position;
                position.translate(offset.x(), offset.y());
                layer.shape = transform.map(position.map(layer.shape));
            } else if (layer.kind != LayerKind::Group && layer.kind != LayerKind::Adjustment) {
                QImage output(width, height, paintingFormat(m_document));
                output.fill(Qt::transparent);
                QPainter painter(&output);
                painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                painter.setTransform(transform);
                painter.drawImage(offset, m_document->layerImage(layer));
                painter.end();
                layer.pixels.setImage(output);
                if (layer.kind == LayerKind::Text)
                    m_document->importReport.append("Perspective crop rasterized a text layer: " +
                                                    layer.name);
                layer.kind = LayerKind::Pixel;
            }
            if (!layer.mask.isNull()) {
                QImage mask(width, height, layer.mask.format());
                mask.fill(0);
                QPainter painter(&mask);
                painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                painter.setTransform(transform);
                painter.drawImage(offset, layer.mask);
                painter.end();
                layer.mask = mask;
            }
            layer.offset = {};
        }
        m_document->state.size = {width, height};
        m_document->state.selection = {};
        m_document->state.guides.clear();
        QJsonArray points;
        for (const QPointF point : quad)
            points.append(QJsonArray{point.x(), point.y()});
        m_document->state.metadata["perspectiveCropCorners"] = points;
        m_document->touch();
    });
    fitToView();
}
void CanvasView::removeBackground() {
    const Layer *active = m_document->activeLayer();
    if (!active || active->locked)
        return;
    const QImage selection = selectSubjectLocally(m_document->composite());
    if (selection.isNull())
        return;
    m_document->mutate("Remove background", [&] {
        Layer *layer = m_document->activeLayer();
        QImage mask(layer->pixels.size.isEmpty() ? m_document->state.size : layer->pixels.size,
                    QImage::Format_Grayscale8);
        mask.fill(0);
        QPainter painter(&mask);
        painter.drawImage(-effectiveOffset(m_document, *layer), selection);
        painter.end();
        if (!layer->mask.isNull()) {
            for (int y = 0; y < mask.height(); ++y) {
                auto *row = mask.scanLine(y);
                for (int x = 0; x < mask.width(); ++x)
                    row[x] = quint8(row[x] * qGray(layer->mask.pixel(x, y)) / 255);
            }
        }
        layer->mask = mask;
        layer->maskEnabled = true;
        m_document->touch();
    });
}
void CanvasView::contentAwareFill() {
    Layer *active = m_document->activeLayer();
    if (!active || active->locked || !m_document->hasSelection())
        return;
    m_document->mutate("Content-aware fill", [&] {
        Layer *layer = m_document->activeLayer();
        const QImage source = m_document->layerImage(*layer);
        QImage localMask(source.size(), QImage::Format_Grayscale8);
        localMask.fill(0);
        QPainter painter(&localMask);
        painter.drawImage(-effectiveOffset(m_document, *layer), m_document->state.selection);
        painter.end();
        QImage result = synthesizePatches(source, localMask);
        layer->pixels.setImage(result);
        layer->kind = LayerKind::Pixel;
        m_document->touch();
    });
}
void CanvasView::transformActive(qreal scale, qreal angle) {
    Layer *active = m_document->activeLayer();
    if (!active || active->locked)
        return;
    m_document->mutate("Transform", [&] {
        Layer *layer = m_document->activeLayer();
        QTransform transform;
        const QPointF center(m_document->state.size.width() / 2.0, m_document->state.size.height() / 2.0);
        transform.translate(center.x(), center.y());
        transform.rotate(angle);
        transform.scale(scale, scale);
        transform.translate(-center.x(), -center.y());
        if (layer->kind == LayerKind::Shape) {
            layer->shape = transform.map(layer->shape);
        } else {
            QImage image(m_document->state.size, paintingFormat(m_document));
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.setTransform(transform);
            painter.drawImage(QPoint(), m_document->layerImage(*layer));
            painter.end();
            layer->pixels.setImage(image);
            layer->kind = LayerKind::Pixel;
        }
        m_document->touch();
    });
}

QStringList toolNames() {
    return {"Move",
            "Artboard",
            "Rectangular Marquee",
            "Elliptical Marquee",
            "Single Row",
            "Single Column",
            "Lasso",
            "Polygonal Lasso",
            "Magnetic Lasso",
            "Object Selection",
            "Quick Selection",
            "Magic Wand",
            "Crop",
            "Perspective Crop",
            "Slice",
            "Slice Select",
            "Frame",
            "Eyedropper",
            "3D Material Eyedropper",
            "Color Sampler",
            "Ruler",
            "Note",
            "Count",
            "Spot Healing",
            "Healing Brush",
            "Patch",
            "Content-Aware Move",
            "Red Eye",
            "Brush",
            "Pencil",
            "Color Replacement",
            "Mixer Brush",
            "Clone Stamp",
            "Pattern Stamp",
            "History Brush",
            "Art History Brush",
            "Eraser",
            "Background Eraser",
            "Magic Eraser",
            "Gradient",
            "Paint Bucket",
            "Blur",
            "Sharpen",
            "Smudge",
            "Dodge",
            "Burn",
            "Sponge",
            "Pen",
            "Freeform Pen",
            "Curvature Pen",
            "Add Anchor",
            "Delete Anchor",
            "Convert Point",
            "Horizontal Type",
            "Vertical Type",
            "Horizontal Type Mask",
            "Vertical Type Mask",
            "Path Selection",
            "Direct Selection",
            "Rectangle",
            "Ellipse",
            "Triangle",
            "Polygon",
            "Line",
            "Custom Shape",
            "Hand",
            "Rotate View",
            "Zoom"};
}

QIcon toolIcon(const QString &name, QColor color) {
    QPixmap pixmap(44, 44);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(2, 2);
    QPen pen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    if (name == "Move" || name == "Content-Aware Move") {
        painter.drawLine(11, 3, 11, 19);
        painter.drawLine(3, 11, 19, 11);
        for (int i = 0; i < 4; ++i) {
            painter.save();
            painter.translate(11, 11);
            painter.rotate(i * 90);
            painter.drawPolyline(QPolygonF{{-3, -5}, {0, -8}, {3, -5}});
            painter.restore();
        }
    } else if (name.contains("Marquee") || name == "Object Selection" || name == "Single Row" ||
               name == "Single Column") {
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        if (name.contains("Elliptical"))
            painter.drawEllipse(4, 5, 14, 12);
        else if (name == "Single Row")
            painter.drawRect(3, 10, 16, 2);
        else if (name == "Single Column")
            painter.drawRect(10, 3, 2, 16);
        else
            painter.drawRect(4, 4, 14, 14);
    } else if (name.contains("Lasso")) {
        QPainterPath p;
        p.moveTo(8, 16);
        p.cubicTo(0, 12, 3, 3, 11, 4);
        p.cubicTo(20, 4, 20, 14, 12, 16);
        p.cubicTo(5, 18, 5, 14, 8, 13);
        p.cubicTo(12, 11, 13, 18, 10, 20);
        painter.drawPath(p);
    } else if (name.contains("Crop")) {
        painter.drawPolyline(QPolygonF{{6, 2}, {6, 16}, {20, 16}});
        painter.drawPolyline(QPolygonF{{2, 6}, {16, 6}, {16, 20}});
        painter.drawLine(7, 15, 18, 4);
    } else if (name.contains("Eyedropper") || name == "Color Sampler") {
        painter.drawPolygon(QPolygonF{{5, 15}, {14, 6}, {17, 9}, {8, 18}, {4, 18}});
        painter.drawLine(12, 4, 19, 11);
        painter.drawLine(15, 3, 20, 8);
    } else if (name.contains("Healing") || name == "Patch") {
        painter.save();
        painter.translate(11, 11);
        painter.rotate(-40);
        painter.drawRoundedRect(-8, -4, 16, 8, 3, 3);
        painter.drawRect(-3, -3, 6, 6);
        painter.drawPoint(-6, 0);
        painter.drawPoint(6, 0);
        painter.restore();
    } else if (name.contains("Brush") || name == "Pencil" || name == "Color Replacement") {
        painter.drawPolygon(QPolygonF{{9, 13}, {17, 3}, {20, 6}, {12, 16}});
        QPainterPath p;
        p.moveTo(9, 12);
        p.cubicTo(3, 12, 7, 18, 3, 19);
        p.cubicTo(11, 20, 14, 16, 11, 14);
        painter.drawPath(p);
    } else if (name.contains("Stamp")) {
        painter.drawRoundedRect(4, 15, 14, 4, 1, 1);
        QPainterPath p;
        p.moveTo(6, 15);
        p.lineTo(8, 11);
        p.lineTo(8, 7);
        p.cubicTo(8, 2, 14, 2, 14, 7);
        p.lineTo(14, 11);
        p.lineTo(16, 15);
        painter.drawPath(p);
    } else if (name.contains("Eraser")) {
        painter.drawPolygon(QPolygonF{{3, 13}, {12, 4}, {19, 11}, {11, 19}, {8, 19}});
        painter.drawLine(7, 9, 15, 16);
        painter.drawLine(11, 19, 20, 19);
    } else if (name == "Gradient") {
        QLinearGradient gradient(4, 0, 18, 0);
        gradient.setColorAt(0, color);
        gradient.setColorAt(1, Qt::transparent);
        painter.setBrush(gradient);
        painter.drawRect(4, 5, 14, 12);
    } else if (name == "Paint Bucket") {
        painter.drawPolygon(QPolygonF{{3, 11}, {10, 4}, {17, 11}, {10, 18}});
        painter.drawLine(3, 11, 17, 11);
        painter.drawLine(8, 3, 13, 8);
        QPainterPath drop;
        drop.moveTo(18, 12);
        drop.cubicTo(14, 17, 17, 20, 19, 18);
        drop.cubicTo(21, 17, 19, 14, 18, 12);
        painter.drawPath(drop);
    } else if (name == "Blur" || name == "Smudge") {
        QPainterPath p;
        p.moveTo(11, 3);
        p.cubicTo(8, 8, 4, 11, 5, 15);
        p.cubicTo(7, 22, 17, 20, 17, 14);
        p.cubicTo(17, 10, 13, 6, 11, 3);
        painter.drawPath(p);
    } else if (name == "Sharpen")
        painter.drawPolygon(QPolygonF{{11, 3}, {3, 19}, {19, 19}});
    else if (name == "Dodge" || name == "Burn" || name == "Sponge") {
        painter.drawEllipse(4, 4, 10, 10);
        painter.drawLine(12, 12, 19, 19);
        if (name == "Sponge") {
            painter.drawPoint(7, 7);
            painter.drawPoint(11, 8);
            painter.drawPoint(8, 11);
        }
    } else if (name.contains("Pen") || name.contains("Anchor") || name == "Convert Point") {
        painter.drawPolygon(QPolygonF{{4, 18}, {7, 7}, {17, 3}, {19, 5}, {15, 15}});
        painter.drawLine(4, 18, 12, 10);
        painter.drawEllipse(10, 8, 4, 4);
        painter.drawLine(12, 18, 18, 12);
    } else if (name.contains("Type")) {
        painter.drawLine(4, 5, 18, 5);
        painter.drawLine(11, 5, 11, 18);
        painter.drawLine(7, 18, 15, 18);
        painter.drawLine(4, 5, 4, 8);
        painter.drawLine(18, 5, 18, 8);
    } else if (name.contains("Selection")) {
        painter.drawPolygon(QPolygonF{{5, 3}, {5, 18}, {9, 14}, {12, 20}, {15, 18}, {12, 12}, {18, 12}});
    } else if (name == "Ellipse")
        painter.drawEllipse(4, 4, 14, 14);
    else if (name == "Triangle")
        painter.drawPolygon(QPolygonF{{11, 3}, {3, 19}, {19, 19}});
    else if (name == "Polygon" || name == "Custom Shape")
        painter.drawPath(polygonShape(QRectF(3, 3, 16, 16), 6));
    else if (name == "Line")
        painter.drawLine(4, 18, 18, 4);
    else if (name == "Hand") {
        QPainterPath p;
        p.moveTo(7, 18);
        p.lineTo(3, 11);
        p.cubicTo(3, 8, 6, 10, 7, 11);
        p.lineTo(7, 5);
        p.cubicTo(7, 3, 9, 3, 9, 5);
        p.lineTo(9, 10);
        p.lineTo(10, 3);
        p.cubicTo(10, 1, 12, 1, 12, 4);
        p.lineTo(12, 10);
        p.lineTo(13, 4);
        p.cubicTo(13, 2, 15, 3, 15, 5);
        p.lineTo(15, 11);
        p.lineTo(16, 6);
        p.cubicTo(17, 4, 19, 5, 18, 8);
        p.lineTo(18, 15);
        p.lineTo(15, 20);
        p.lineTo(8, 20);
        p.closeSubpath();
        painter.drawPath(p);
    } else if (name == "Rotate View") {
        painter.drawArc(4, 4, 14, 14, 20 * 16, 290 * 16);
        painter.drawPolyline(QPolygonF{{14, 2}, {19, 5}, {15, 8}});
        painter.drawLine(8, 11, 14, 11);
        painter.drawLine(11, 8, 11, 14);
    } else if (name == "Zoom") {
        painter.drawEllipse(3, 3, 12, 12);
        painter.drawLine(13, 13, 20, 20);
        painter.drawLine(6, 9, 12, 9);
        painter.drawLine(9, 6, 9, 12);
    } else if (name == "Ruler") {
        painter.drawRect(3, 7, 16, 8);
        for (int x = 6; x <= 16; x += 3)
            painter.drawLine(x, 7, x, 11);
    } else if (name == "Note") {
        painter.drawPolygon(QPolygonF{{4, 3}, {18, 3}, {18, 14}, {12, 20}, {4, 20}});
        painter.drawPolyline(QPolygonF{{12, 20}, {12, 14}, {18, 14}});
        painter.drawLine(7, 7, 15, 7);
        painter.drawLine(7, 10, 15, 10);
    } else if (name == "Count") {
        painter.drawText(QRect(2, 1, 20, 20), Qt::AlignCenter, "#");
    } else if (name == "Magic Wand" || name == "Magic Eraser") {
        painter.drawLine(4, 19, 16, 7);
        painter.drawLine(13, 3, 13, 6);
        painter.drawLine(18, 5, 21, 5);
        painter.drawLine(17, 10, 20, 13);
        painter.drawLine(7, 4, 9, 6);
    } else if (name == "Red Eye") {
        painter.drawEllipse(3, 6, 16, 10);
        painter.drawEllipse(8, 8, 6, 6);
    } else {
        painter.drawRect(4, 4, 14, 14);
        if (name == "Frame") {
            painter.drawLine(4, 4, 18, 18);
            painter.drawLine(18, 4, 4, 18);
        } else if (name == "Artboard") {
            painter.drawLine(2, 7, 6, 7);
            painter.drawLine(4, 5, 4, 9);
        }
    }
    painter.end();
    return QIcon(pixmap);
}
} // namespace serika
