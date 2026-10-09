#include "TextLayout.h"
#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QRawFont>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>
#include <QTextLayout>
#include <algorithm>
#include <cmath>

namespace serika {
namespace {
qreal number(const QJsonObject &object, const char *key, qreal fallback, qreal minimum, qreal maximum) {
    const qreal value = object.value(QLatin1String(key)).toDouble(fallback);
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}
Qt::Alignment alignment(const QString &name) {
    if (name == "center")
        return Qt::AlignHCenter;
    if (name == "right")
        return Qt::AlignRight;
    if (name == "justify")
        return Qt::AlignJustify;
    return Qt::AlignLeft;
}
QString alignmentName(Qt::Alignment value) {
    if (value.testFlag(Qt::AlignJustify))
        return "justify";
    if (value.testFlag(Qt::AlignHCenter))
        return "center";
    if (value.testFlag(Qt::AlignRight))
        return "right";
    return "left";
}
QTextCharFormat readFormat(const QJsonObject &object, const Layer &layer) {
    QTextCharFormat format;
    QFont font = layer.font;
    if (object.contains("font"))
        font.fromString(object.value("font").toString());
    if (object.contains("family"))
        font.setFamily(object.value("family").toString());
    if (object.contains("size"))
        font.setPointSizeF(number(object, "size", 12, .1, 4096));
    if (object.contains("pixelSize"))
        font.setPixelSize(qRound(number(object, "pixelSize", 16, 1, 8192)));
    if (object.contains("bold"))
        font.setBold(object.value("bold").toBool());
    if (object.contains("italic"))
        font.setItalic(object.value("italic").toBool());
    if (object.contains("underline"))
        font.setUnderline(object.value("underline").toBool());
    if (object.contains("strike"))
        font.setStrikeOut(object.value("strike").toBool());
    if (object.contains("kerning"))
        font.setKerning(object.value("kerning").toBool(true));
    if (object.contains("tracking"))
        font.setLetterSpacing(QFont::AbsoluteSpacing, number(object, "tracking", 0, -1000, 1000));
    if (object.contains("spacingPercent")) {
        const qreal spacing = number(object, "spacingPercent", 0, 0, 1000);
        // Qt's default font reports zero here to mean unset; it is not 0% advance.
        if (spacing > 0)
            font.setLetterSpacing(QFont::PercentageSpacing, spacing);
    }
    if (object.contains("stretch")) {
        const int stretch = qRound(number(object, "stretch", 100, 0, 400));
        // QFont::AnyStretch (0) selects the font's normal width, not a 0% scale.
        font.setStretch(stretch == QFont::AnyStretch ? QFont::Unstretched : stretch);
    }
    if (object.contains("capitalization"))
        font.setCapitalization(QFont::Capitalization(qRound(number(object, "capitalization", 0, 0, 4))));
    if (font.pixelSize() > 0)
        font.setPixelSize(std::clamp(font.pixelSize(), 1, 8192));
    else
        font.setPointSizeF(
            std::isfinite(font.pointSizeF()) ? std::clamp(font.pointSizeF(), qreal(.1), qreal(4096)) : 12);
    format.setFont(font);
    const QColor color(object.value("color").toString());
    format.setForeground(color.isValid() ? color : layer.color);
    format.setBaselineOffset(number(object, "baseline", 0, -1000, 1000));
    const QString vertical = object.value("verticalAlignment").toString();
    if (vertical == "super")
        format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    if (vertical == "sub")
        format.setVerticalAlignment(QTextCharFormat::AlignSubScript);
    return format;
}
QJsonObject writeFormat(const QTextCharFormat &format, const QFont &fallback) {
    const QFont font = format.font().resolve(fallback);
    QJsonObject object{{"font", font.toString()},
                       {"color", format.foreground().color().name(QColor::HexArgb)},
                       {"baseline", format.baselineOffset()},
                       {"kerning", font.kerning()}};
    // QFont::toString does not retain every spacing property.
    if (font.letterSpacingType() == QFont::AbsoluteSpacing)
        object["tracking"] = font.letterSpacing();
    else if (font.letterSpacing() > 0)
        object["spacingPercent"] = font.letterSpacing();
    object["stretch"] = font.stretch() == QFont::AnyStretch ? QFont::Unstretched : font.stretch();
    object["capitalization"] = int(font.capitalization());
    if (format.verticalAlignment() == QTextCharFormat::AlignSuperScript)
        object["verticalAlignment"] = "super";
    if (format.verticalAlignment() == QTextCharFormat::AlignSubScript)
        object["verticalAlignment"] = "sub";
    return object;
}
QTextBlockFormat readParagraph(const QJsonObject &object, const Layer &layer) {
    QTextBlockFormat format;
    format.setAlignment(
        alignment(object.value("alignment").toString(layer.parameters.value("alignment").toString("left"))));
    format.setTopMargin(number(object, "before", 0, 0, 10000));
    format.setBottomMargin(number(object, "after", 0, 0, 10000));
    format.setLeftMargin(number(object, "leftIndent", 0, -10000, 10000));
    format.setRightMargin(number(object, "rightIndent", 0, -10000, 10000));
    format.setTextIndent(number(object, "firstIndent", 0, -10000, 10000));
    const qreal leading = number(object, "leading", 0, 0, 10000);
    if (leading > 0)
        format.setLineHeight(leading, QTextBlockFormat::FixedHeight);
    return format;
}
QJsonObject writeParagraph(const QTextBlock &block) {
    const QTextBlockFormat f = block.blockFormat();
    return {{"position", block.position()},
            {"alignment", alignmentName(f.alignment())},
            {"before", f.topMargin()},
            {"after", f.bottomMargin()},
            {"leftIndent", f.leftMargin()},
            {"rightIndent", f.rightMargin()},
            {"firstIndent", f.textIndent()},
            {"leading", f.lineHeightType() == QTextBlockFormat::FixedHeight ? f.lineHeight() : 0}};
}
void legacyPaint(QPainter &p, const Layer &layer, QSize canvas) {
    p.setFont(layer.font);
    p.setPen(layer.color);
    if (layer.parameters.value("vertical").toBool()) {
        const QFontMetricsF metrics(layer.font);
        qreal x = 0, y = 0;
        for (char32_t code : layer.text.toUcs4()) {
            if (code == '\n') {
                x += metrics.height();
                y = 0;
                continue;
            }
            p.drawText(QPointF(x, y + metrics.ascent()), QString::fromUcs4(&code, 1));
            y += metrics.height();
        }
    } else {
        const QString value = layer.parameters.value("alignment").toString();
        p.drawText(QRectF(QPointF(), canvas),
                   (value == "center"  ? Qt::AlignHCenter
                    : value == "right" ? Qt::AlignRight
                                       : Qt::AlignLeft) |
                       Qt::AlignTop | Qt::TextWordWrap,
                   layer.text);
    }
}
void appendPart(QVector<TextOutlinePart> &parts, QPainterPath path, QColor color) {
    if (path.isEmpty() || color.alpha() == 0)
        return;
    path.setFillRule(Qt::WindingFill);
    for (auto &part : parts) {
        if (part.color == color) {
            part.path.addPath(path);
            return;
        }
    }
    parts.append({std::move(path), color});
}
} // namespace
bool hasRichTypography(const Layer &layer) { return layer.parameters.value("typography").isObject(); }
void populateTextDocument(QTextDocument &document, const Layer &layer, QSize canvas) {
    document.clear();
    document.setUndoRedoEnabled(false);
    document.setDocumentMargin(0);
    document.setDefaultFont(layer.font);
    document.setUseDesignMetrics(true);
    const QJsonObject typography = layer.parameters.value("typography").toObject();
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(alignment(layer.parameters.value("alignment").toString()));
    document.setDefaultTextOption(option);
    document.setPlainText(layer.text);
    QTextCursor all(&document);
    all.select(QTextCursor::Document);
    all.setCharFormat(readFormat({}, layer));
    all.setBlockFormat(readParagraph({}, layer));
    for (const QJsonValue &value : typography.value("spans").toArray()) {
        const QJsonObject span = value.toObject();
        const int start = qRound(number(span, "start", 0, 0, layer.text.size()));
        const int length = qRound(number(span, "length", 0, 0, layer.text.size() - start));
        if (!length)
            continue;
        QTextCursor cursor(&document);
        cursor.setPosition(start);
        cursor.setPosition(start + length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(readFormat(span, layer));
    }
    for (const QJsonValue &value : typography.value("paragraphs").toArray()) {
        const QJsonObject paragraph = value.toObject();
        QTextCursor cursor(&document);
        cursor.setPosition(qRound(number(paragraph, "position", 0, 0, layer.text.size())));
        cursor.setBlockFormat(readParagraph(paragraph, layer));
    }
    const QString mode = typography.value("layout").toString("paragraph");
    document.setTextWidth(
        mode == "point" ? -1 : number(typography, "width", std::max(1, canvas.width()), 1, 1000000));
    (void)document.documentLayout()->documentSize();
}
void serializeTextDocument(Layer &layer, const QTextDocument &document) {
    layer.text = document.toPlainText();
    QJsonObject typography = layer.parameters.value("typography").toObject();
    typography["version"] = 1;
    if (!typography.contains("layout"))
        typography["layout"] = "paragraph";
    if (!typography.contains("width") && document.textWidth() > 0)
        typography["width"] = document.textWidth();
    QJsonArray spans, paragraphs;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        paragraphs.append(writeParagraph(block));
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
                continue;
            QJsonObject span = writeFormat(fragment.charFormat(), document.defaultFont());
            span["start"] = fragment.position();
            span["length"] = fragment.length();
            spans.append(span);
        }
    }
    typography["spans"] = spans;
    typography["paragraphs"] = paragraphs;
    layer.parameters["typography"] = typography;
}
QJsonArray textPathToJson(const QPainterPath &path) {
    QJsonArray array;
    for (int i = 0; i < path.elementCount(); ++i) {
        const auto e = path.elementAt(i);
        array.append(QJsonArray{int(e.type), e.x, e.y});
    }
    return array;
}
QPainterPath textPathFromJson(const QJsonArray &elements) {
    QPainterPath path;
    if (elements.size() > 100000)
        return path;
    const auto validCoordinate = [](const QJsonValue &value) {
        return value.isDouble() && std::isfinite(value.toDouble()) && std::abs(value.toDouble()) <= 10000000;
    };
    for (qsizetype i = 0; i < elements.size(); ++i) {
        const auto e = elements[i].toArray();
        if (e.size() != 3 || !validCoordinate(e[1]) || !validCoordinate(e[2]))
            return {};
        const QPointF point(e[1].toDouble(), e[2].toDouble());
        if (e[0].toInt() == QPainterPath::MoveToElement)
            path.moveTo(point);
        else if (e[0].toInt() == QPainterPath::LineToElement)
            path.lineTo(point);
        else if (e[0].toInt() == QPainterPath::CurveToElement && i + 2 < elements.size()) {
            const auto b = elements[++i].toArray(), c = elements[++i].toArray();
            if (b.size() != 3 || c.size() != 3 || b[0].toInt() != QPainterPath::CurveToDataElement ||
                c[0].toInt() != QPainterPath::CurveToDataElement || !validCoordinate(b[1]) ||
                !validCoordinate(b[2]) || !validCoordinate(c[1]) || !validCoordinate(c[2]))
                return {};
            path.cubicTo(point, QPointF(b[1].toDouble(), b[2].toDouble()),
                         QPointF(c[1].toDouble(), c[2].toDouble()));
        } else
            return {};
    }
    return path;
}
QVector<TextOutlinePart> textLayerOutlineParts(const Layer &layer, QSize canvas) {
    QVector<TextOutlinePart> parts;
    if (!hasRichTypography(layer) && layer.parameters.value("vertical").toBool()) {
        const QFontMetricsF metrics(layer.font);
        QPainterPath outlines;
        qreal x = 0, y = 0;
        for (char32_t code : layer.text.toUcs4()) {
            if (code == '\n') {
                x += metrics.height();
                y = 0;
                continue;
            }
            outlines.addText(QPointF(x, y + metrics.ascent()), layer.font, QString::fromUcs4(&code, 1));
            y += metrics.height();
        }
        QTransform transform;
        transform.rotate(number(layer.parameters, "rotation", 0, -360000, 360000));
        appendPart(parts, transform.map(outlines), layer.color);
        return parts;
    }
    QTextDocument document;
    Layer working = layer;
    const QJsonObject typography = layer.parameters.value("typography").toObject();
    const QPainterPath path = textPathFromJson(typography.value("path").toArray());
    const bool onPath = !path.isEmpty() && path.length() > .001;
    if (onPath) {
        working.text.replace('\n', ' ');
        auto data = working.parameters.value("typography").toObject();
        data["layout"] = "point";
        working.parameters["typography"] = data;
    }
    populateTextDocument(document, working, canvas);
    const qreal pathLength = onPath ? path.length() : 0;
    const qreal startOffset = number(typography, "pathOffset", 0, -1000000, 1000000);
    const bool reverse = typography.value("pathReverse").toBool();
    QTransform rotation;
    rotation.rotate(number(layer.parameters, "rotation", 0, -360000, 360000) +
                    (hasRichTypography(layer) && layer.parameters.value("vertical").toBool() ? 90 : 0));
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        QTextLayout *layout = block.layout();
        const QPointF origin = document.documentLayout()->blockBoundingRect(block).topLeft();
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
                continue;
            const auto format = fragment.charFormat();
            const QColor color = format.foreground().color();
            const int from = fragment.position() - block.position();
            for (const QGlyphRun &run : layout->glyphRuns(from, fragment.length())) {
                const auto glyphs = run.glyphIndexes();
                const auto positions = run.positions();
                for (qsizetype i = 0; i < glyphs.size() && i < positions.size(); ++i) {
                    const QPointF position = origin + positions[i];
                    QPainterPath glyph = run.rawFont().pathForGlyph(glyphs[i]);
                    QTransform placement;
                    if (onPath) {
                        const qreal distance = startOffset + position.x();
                        if (distance < 0 || distance > pathLength)
                            continue;
                        const qreal percent =
                            path.percentAtLength(reverse ? pathLength - distance : distance);
                        const QPointF point = path.pointAtPercent(percent);
                        placement.translate(point.x(), point.y());
                        placement.rotate(-path.angleAtPercent(percent) + (reverse ? 180 : 0));
                        const QTextLine line = layout->lineForTextPosition(from);
                        if (line.isValid())
                            placement.translate(0, position.y() - origin.y() - line.y() - line.ascent());
                    } else
                        placement.translate(position.x(), position.y());
                    appendPart(parts, rotation.map(placement.map(glyph)), color);
                }
            }
            // Decoration geometry follows the same paragraph layout; curved path decorations are omitted.
            if (!onPath && (format.fontUnderline() || format.fontStrikeOut())) {
                const QFontMetricsF metrics(format.font().resolve(layer.font));
                for (int lineIndex = 0; lineIndex < layout->lineCount(); ++lineIndex) {
                    const QTextLine line = layout->lineAt(lineIndex);
                    const int start = std::max(from, line.textStart());
                    const int end = std::min(from + fragment.length(), line.textStart() + line.textLength());
                    if (start >= end)
                        continue;
                    const qreal x1 = line.cursorToX(start), x2 = line.cursorToX(end);
                    const qreal baseline = origin.y() + line.y() + line.ascent();
                    QPainterPath decorations;
                    if (format.fontUnderline())
                        decorations.addRect(QRectF(origin.x() + std::min(x1, x2),
                                                   baseline + metrics.underlinePos(), std::abs(x2 - x1),
                                                   std::max(qreal(1), metrics.lineWidth())));
                    if (format.fontStrikeOut())
                        decorations.addRect(QRectF(origin.x() + std::min(x1, x2),
                                                   baseline - metrics.strikeOutPos(), std::abs(x2 - x1),
                                                   std::max(qreal(1), metrics.lineWidth())));
                    appendPart(parts, rotation.map(decorations), color);
                }
            }
        }
    }
    return parts;
}
QPainterPath textLayerOutline(const Layer &layer, QSize canvas) {
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    for (const auto &part : textLayerOutlineParts(layer, canvas))
        path.addPath(part.path);
    return path;
}
QRectF textLayerBounds(const Layer &layer, QSize canvas) {
    return textLayerOutline(layer, canvas).boundingRect();
}
void paintTextLayer(QPainter &painter, const Layer &layer, QSize canvas) {
    painter.save();
    if (!hasRichTypography(layer)) {
        painter.rotate(layer.parameters.value("rotation").toDouble());
        legacyPaint(painter, layer, canvas);
    } else if (!layer.parameters.value("typography").toObject().value("path").toArray().isEmpty()) {
        painter.setPen(Qt::NoPen);
        for (const auto &part : textLayerOutlineParts(layer, canvas)) {
            painter.setBrush(part.color);
            painter.drawPath(part.path);
        }
    } else {
        QTextDocument document;
        populateTextDocument(document, layer, canvas);
        painter.rotate(number(layer.parameters, "rotation", 0, -360000, 360000) +
                       (layer.parameters.value("vertical").toBool() ? 90 : 0));
        document.drawContents(&painter);
    }
    painter.restore();
}
} // namespace serika
