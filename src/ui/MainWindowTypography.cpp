#include "MainWindow.h"
#include "document/TextLayout.h"
#include "ui/dialogs/TypeEditorDialog.h"
#include <QInputDialog>
#include <QSet>
#include <algorithm>

namespace serika {
namespace {
QString textLock(const Document *document, const Layer &layer) {
    quint64 id = layer.id;
    QSet<quint64> visited;
    while (id && !visited.contains(id)) {
        visited.insert(id);
        const int index = document->indexForId(id);
        if (index < 0)
            break;
        const Layer &ancestor = document->state.layers[index];
        if (ancestor.locked)
            return "Unlock “" + ancestor.name + "” before editing type.";
        id = ancestor.parentId;
    }
    return {};
}
} // namespace
bool MainWindow::runTypographyCommand(const QString &name) {
    const bool edit = name == "Edit Text..." || name == "Edit Typography..." || name == "Edit Typography";
    const bool shape = name == "Convert to Shape";
    const bool point = name == "Convert to Point Text";
    const bool paragraph = name == "Convert to Paragraph Text";
    const bool onPath = name == "Type on Path..." || name == "Type on Path";
    const bool clearPath = name == "Clear Type Path";
    if (!edit && !shape && !point && !paragraph && !onPath && !clearPath)
        return false;
    Document *document = currentDocument();
    if (!document) {
        showMessage("Open a document and select a text layer.");
        return true;
    }
    Layer *active = document->activeLayer();
    if (!active || active->kind != LayerKind::Text) {
        if (edit)
            selectTool("Horizontal Type");
        else
            showMessage("Select an editable text layer.");
        return true;
    }
    const QString locked = textLock(document, *active);
    if (!locked.isEmpty()) {
        showMessage(locked);
        return true;
    }
    const quint64 id = active->id;
    if (shape) {
        const Layer original = *active;
        const auto parts = textLayerOutlineParts(original, document->state.size);
        if (parts.isEmpty()) {
            showMessage("This text contains no convertible glyph outlines.");
            return true;
        }
        document->mutate("Convert type to shape", [&] {
            const int index = document->indexForId(id);
            if (index < 0)
                return;
            if (parts.size() == 1) {
                Layer &layer = document->state.layers[index];
                layer.kind = LayerKind::Shape;
                layer.shape = parts[0].path;
                layer.color = parts[0].color;
                layer.stroke = Qt::transparent;
                layer.parameters.remove("typography");
                layer.parameters.remove("rotation");
                layer.parameters.remove("vertical");
            } else {
                quint64 next = 1;
                for (const auto &layer : document->state.layers)
                    next = std::max(next, layer.id + 1);
                Layer group = original;
                group.kind = LayerKind::Group;
                group.name = original.name + " outlines";
                group.text.clear();
                group.shape = {};
                group.parameters.remove("typography");
                group.parameters.remove("rotation");
                group.parameters.remove("vertical");
                group.parameters.remove("contentTransform");
                document->state.layers[index] = group;
                int insertion = index;
                for (const auto &part : parts) {
                    Layer child;
                    child.id = next++;
                    child.kind = LayerKind::Shape;
                    child.name = "Type outline " + part.color.name();
                    child.parentId = id;
                    child.shape = part.path;
                    child.color = part.color;
                    if (original.parameters.contains("contentTransform"))
                        child.parameters["contentTransform"] = original.parameters["contentTransform"];
                    document->state.layers.insert(insertion++, child);
                }
                document->state.activeIndex = document->indexForId(id);
            }
        });
        if (m_recording)
            showMessage("Type outline conversion is not captured by the action recorder.");
        return true;
    }
    if (point || paragraph || clearPath) {
        Layer updated = *active;
        QTextDocument text;
        populateTextDocument(text, updated, document->state.size);
        serializeTextDocument(updated, text);
        auto typography = updated.parameters.value("typography").toObject();
        if (clearPath)
            typography.remove("path");
        else {
            typography["layout"] = point ? "point" : "paragraph";
            if (paragraph && !typography.contains("width"))
                typography["width"] = std::max(1, document->state.size.width());
        }
        updated.parameters["typography"] = typography;
        if (updated.parameters != active->parameters)
            document->mutate(clearPath ? "Clear type path"
                             : point   ? "Point text"
                                       : "Paragraph text",
                             [&] { document->state.layers[document->indexForId(id)] = updated; });
        if (m_recording)
            showMessage("Typography changes are not captured by the action recorder.");
        return true;
    }
    QPainterPath chosenPath;
    if (onPath) {
        QStringList names;
        QVector<quint64> ids;
        for (const Layer &candidate : document->state.layers)
            if (candidate.kind == LayerKind::Shape && !candidate.shape.isEmpty()) {
                names.append(candidate.name + QString(" [%1]").arg(candidate.id));
                ids.append(candidate.id);
            }
        if (names.isEmpty()) {
            showMessage("Draw a shape or Pen path first, then select your text and use Type on Path.");
            return true;
        }
        bool accepted = false;
        const QString selected = QInputDialog::getItem(this, "Type on Path", "Choose a shape or Pen path",
                                                       names, 0, false, &accepted);
        if (!accepted)
            return true;
        const Layer &source = document->state.layers[document->indexForId(ids[names.indexOf(selected)])];
        QTransform local;
        const QPointF delta =
            document->effectiveLayerOffset(source) - document->effectiveLayerOffset(*active);
        local.translate(delta.x(), delta.y());
        QPainterPath sourcePath = source.shape;
        const QJsonArray matrix = source.parameters.value("contentTransform").toArray();
        if (matrix.size() == 9) {
            const QTransform transform(matrix[0].toDouble(), matrix[1].toDouble(), matrix[2].toDouble(),
                                       matrix[3].toDouble(), matrix[4].toDouble(), matrix[5].toDouble(),
                                       matrix[6].toDouble(), matrix[7].toDouble(), matrix[8].toDouble());
            if (transform.isInvertible())
                sourcePath =
                    QImage::trueMatrix(transform, document->state.size.width(), document->state.size.height())
                        .map(sourcePath);
        }
        chosenPath = local.map(sourcePath);
        if (chosenPath.length() < .001) {
            showMessage("Choose a path with nonzero length.");
            return true;
        }
    }
    TypeEditorDialog dialog(*active, document->state.size, this);
    document->beginTransaction("Typography");
    connect(&dialog, &TypeEditorDialog::previewChanged, &dialog, [&] {
        const int index = document->indexForId(id);
        if (index >= 0)
            document->mutate("Typography", [&] { document->state.layers[index] = dialog.editedLayer(); });
    });
    if (onPath)
        dialog.setTextPath(chosenPath);
    if (dialog.exec() == QDialog::Accepted) {
        const int index = document->indexForId(id);
        if (index >= 0 && (dialog.editedLayer().text != document->state.layers[index].text ||
                           dialog.editedLayer().parameters != document->state.layers[index].parameters))
            document->mutate("Typography", [&] { document->state.layers[index] = dialog.editedLayer(); });
        document->endTransaction();
    } else
        document->cancelTransaction();
    if (m_recording)
        showMessage("Rich typography changes are not captured by the action recorder.");
    return true;
}
} // namespace serika
