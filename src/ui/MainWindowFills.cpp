#include "MainWindow.h"
#include "dialogs/FillEditorDialog.h"
#include <QColorDialog>
#include <QSet>
namespace serika {
void MainWindow::editFill(const QString &name) {
    auto *document = currentDocument();
    if (!document)
        return;
    const bool editing = name == "Edit Fill...";
    auto *layer = document->activeLayer();
    const auto isFill = [](LayerKind kind) {
        return kind == LayerKind::SolidFill || kind == LayerKind::GradientFill ||
               kind == LayerKind::PatternFill;
    };
    if (editing && (!layer || !isFill(layer->kind))) {
        showMessage("Select a solid, gradient, or pattern fill layer first.");
        return;
    }
    if (editing) {
        QSet<quint64> visited;
        const Layer *ancestor = layer;
        while (ancestor && !visited.contains(ancestor->id)) {
            visited.insert(ancestor->id);
            if (ancestor->locked) {
                showMessage("Unlock the layer and its parent groups to edit its fill.");
                return;
            }
            const int parent = document->indexForId(ancestor->parentId);
            ancestor = parent < 0 ? nullptr : &document->state.layers[parent];
        }
    }
    const auto kind = editing                     ? layer->kind
                      : name == "Solid Color..."  ? LayerKind::SolidFill
                      : name == "Pattern Fill..." ? LayerKind::PatternFill
                                                  : LayerKind::GradientFill;
    const auto id = editing ? layer->id : 0;
    QColor color = editing ? layer->color : m_foreground;
    auto parameters = editing ? layer->parameters : QJsonObject{};
    if (kind == LayerKind::SolidFill) {
        color = QColorDialog::getColor(color, this, "Fill color", QColorDialog::ShowAlphaChannel);
        if (!color.isValid())
            return;
    } else {
        FillEditorDialog dialog(kind, parameters, editing ? layer->color : m_foreground,
                                editing ? QColor(Qt::white) : m_background, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        parameters = dialog.parameters();
    }
    document->mutate(editing ? "Edit fill" : "Create fill layer", [=] {
        if (!editing)
            document->addLayer(name.left(name.size() - 3), kind);
        const int index = document->indexForId(id);
        auto *target =
            editing ? (index < 0 ? nullptr : &document->state.layers[index]) : document->activeLayer();
        if (target) {
            target->color = color;
            target->parameters = parameters;
        }
    });
}
} // namespace serika
