#pragma once
#include "Document.h"
#include <QTextDocument>

namespace serika {
struct TextOutlinePart {
    QPainterPath path;
    QColor color;
};
// Plain text and explicit formatting only; no HTML parsing or external resources.
void populateTextDocument(QTextDocument &document, const Layer &layer, QSize canvas);
void serializeTextDocument(Layer &layer, const QTextDocument &document);
bool hasRichTypography(const Layer &layer);
void paintTextLayer(QPainter &painter, const Layer &layer, QSize canvas);
QRectF textLayerBounds(const Layer &layer, QSize canvas);
QVector<TextOutlinePart> textLayerOutlineParts(const Layer &layer, QSize canvas);
QPainterPath textLayerOutline(const Layer &layer, QSize canvas);
QJsonArray textPathToJson(const QPainterPath &path);
QPainterPath textPathFromJson(const QJsonArray &elements);
} // namespace serika
