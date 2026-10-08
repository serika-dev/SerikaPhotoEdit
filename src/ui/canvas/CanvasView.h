#pragma once
#include "document/Document.h"
#include <QIcon>
#include <QPolygonF>
#include <QTimer>
#include <QTransform>
#include <QWidget>
namespace serika {
class CanvasView : public QWidget {
    Q_OBJECT
  public:
    explicit CanvasView(Document *document, QWidget *parent = nullptr);
    Document *document() const { return m_document; }
    QString tool() const { return m_tool; }
    void setTool(const QString &tool);
    void setForeground(QColor color);
    QColor foreground() const;
    void setBackground(QColor color);
    void setBrushSize(int size);
    void setBrushHardness(qreal hardness);
    void setBrushOpacity(qreal opacity);
    void setBrushFlow(qreal flow);
    void setBrushSpacing(qreal spacing);
    void setBrushAngle(qreal degrees);
    void setBrushRoundness(qreal roundness);
    void setBrushSmoothing(qreal smoothing);
    void setShowRulers(bool enabled);
    void setShowGrid(bool enabled);
    void setShowGuides(bool enabled);
    void setQuickMask(bool enabled);
    void setSurround(QColor color);
    void setZoom(qreal zoom);
    qreal zoom() const;
    void fitToView();
    void resetView();
    void rotateView(qreal degrees);
    void flipView();
    QPointF toDocument(QPointF point) const;
    QPointF fromDocument(QPointF point) const;
    void fillSelection(QColor color);
    void strokeSelection(QColor color, int width);
    void selectSubject();
    void removeBackground();
    void contentAwareFill();
    void transformActive(qreal scale, qreal angle);
  signals:
    void zoomChanged(qreal zoom);
    void colorPicked(QColor color);
    void cursorInfo(QPointF position, QColor color);
    void toolChanged(const QString &tool);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void tabletEvent(QTabletEvent *) override;
    void resizeEvent(QResizeEvent *) override;

  private:
    Document *m_document;
    QString m_tool = "Move";
    QColor m_foreground = Qt::black;
    QColor m_background = Qt::white;
    QColor m_surround = QColor("#262626");
    int m_brushSize = 40;
    qreal m_hardness = 0.8;
    qreal m_opacity = 1;
    qreal m_flow = 1;
    qreal m_spacing = 0.1;
    qreal m_brushAngle = 0;
    qreal m_roundness = 1;
    qreal m_smoothing = 0;
    QHash<quint64, QImage> m_strokeCoverage;
    qreal m_zoom = 1;
    qreal m_rotation = 0;
    qreal m_pressure = 1;
    QPointF m_pan;
    bool m_rulers = true;
    bool m_grid = false;
    bool m_guides = true;
    bool m_quickMask = false;
    bool m_flip = false;
    bool m_space = false;
    bool m_dragging = false;
    bool m_panning = false;
    bool m_fitPending = true;
    QPointF m_start;
    QPointF m_last;
    QPointF m_cursor;
    QPointF m_initialOffset;
    QPointF m_cloneSource;
    bool m_hasCloneSource = false;
    QImage m_strokeSource;
    QPolygonF m_lasso;
    QRectF m_dragRect;
    QString m_selectionOperation;
    QTimer m_ants;
    int m_antOffset = 0;
    qint64 m_selectionKey = -1;
    bool m_selectionActive = false;
    bool m_moving = false;
    QPainterPath m_selectionOutline;
    QImage m_maskOverlay;
    int m_guideDrag = 0;
    qreal m_guidePosition = 0;
    bool m_rotating = false;
    bool m_hudBrush = false;
    qreal m_initialRotation = 0;
    int m_initialBrushSize = 40;
    qreal m_initialHardness = 0.8;
    TileImage m_originalPixels;
    QImage m_healMask;
    quint64 m_penLayerId = 0;
    QPainterPath m_penPath;
    QImage m_historySource;
    int m_pathElement = -1;
    QPainterPath m_initialPath;
    QPolygonF m_perspectivePoints;
    QRectF activeLayerBounds() const;
    void dab(QPointF point);
    void drawStroke(QPointF from, QPointF to);
    void finishSelection();
    void applyGradient();
    void bucket(QPoint point);
    void applyImage(const QImage &image, QPoint documentOrigin, bool erase = false);
    void updateSelectionOutline();
    void chooseSelectionOperation(Qt::KeyboardModifiers modifiers);
    void sampleColor(QPointF point);
    void perspectiveCrop();
    QTransform viewTransform() const;
};
QStringList toolNames();
QIcon toolIcon(const QString &name, QColor color = QColor("#d4d4d4"));
} // namespace serika
