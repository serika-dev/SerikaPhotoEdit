#include "ui/MainWindow.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace serika {
namespace {
QString shortcutPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/shortcuts.json";
}
bool paintingTool(const QString &tool) {
    return QStringList{"Brush",         "Pencil",
                       "Mixer Brush",   "Color Replacement",
                       "Clone Stamp",   "Pattern Stamp",
                       "History Brush", "Art History Brush",
                       "Eraser",        "Background Eraser",
                       "Magic Eraser",  "Spot Healing",
                       "Healing Brush", "Blur",
                       "Sharpen",       "Smudge",
                       "Dodge",         "Burn",
                       "Sponge",        "Quick Selection"}
        .contains(tool);
}
} // namespace
void MainWindow::initializeShortcuts() {
    m_shortcuts = new ShortcutRegistry(this);
    for (auto it = m_commands.cbegin(); it != m_commands.cend(); ++it)
        m_shortcuts->addCommand(it.key());
    for (const QString &name : m_docks.keys())
        m_shortcuts->addCommand(name, "Panels");
    for (const QString &name :
         QStringList{"Toggle Layer Mask", "Invert Layer Mask", "Load Selection from Layer Mask",
                     "Add Vector Mask", "Delete Vector Mask", "Apply Vector Mask", "Toggle Vector Mask",
                     "Load Selection from Vector Mask"})
        m_shortcuts->addCommand(name, "Masks");
    for (const QString &name : QStringList{"Auto Crop Transparent", "Auto Crop Content", "Auto Straighten"})
        m_shortcuts->addCommand(name, "Crop");
    m_shortcuts->addCommand("Edit Smart Filters...", "Smart Filters");
    m_shortcuts->enabled = [this](const ShortcutEntry &entry) {
        if (entry.scope == "crop")
            return currentCanvas() && (m_tool == "Crop" || m_tool == "Perspective Crop");
        if (entry.scope == "move")
            return currentCanvas() && (m_tool == "Move" || m_tool == "Artboard");
        if (entry.scope == "painting")
            return currentCanvas() && paintingTool(m_tool);
        if (entry.kind == ShortcutKind::Command) {
            auto *action = m_commands.value(entry.payload);
            return !action || action->isEnabled();
        }
        if (entry.kind == ShortcutKind::Tool || entry.kind == ShortcutKind::CycleTool)
            return true;
        if (entry.kind == ShortcutKind::Local &&
            QStringList{"screen-mode", "toggle-panels", "toggle-docks", "swap-colors", "default-colors"}
                .contains(entry.payload))
            return true;
        return currentCanvas() != nullptr;
    };
    m_shortcuts->activated = [this](const ShortcutEntry &entry, bool released) {
        activateShortcut(entry, released);
    };
    connect(m_shortcuts, &ShortcutRegistry::bindingsChanged, this, &MainWindow::updateShortcutLabels);
    if (QFileInfo::exists(shortcutPath())) {
        QString error;
        if (!m_shortcuts->load(shortcutPath(), &error))
            QTimer::singleShot(0, this,
                               [this, error] { showMessage("Shortcut set was not loaded: " + error); });
    } else {
        // Import the old command-only settings once; new sets use stable IDs and explicit disabled keys.
        auto map = m_shortcuts->bindingMap();
        bool legacy = false;
        for (const auto &row : m_shortcuts->entries())
            if (row.kind == ShortcutKind::Command && m_settings.contains("shortcuts/" + row.payload)) {
                QString text = m_settings.value("shortcuts/" + row.payload).toString();
                auto sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
                map[row.id] = sequence.isEmpty() ? QList<QKeySequence>{} : QList<QKeySequence>{sequence};
                legacy = true;
            }
        QString error;
        if (legacy && m_shortcuts->setAllBindings(map, &error))
            m_shortcuts->save(shortcutPath());
    }
    updateShortcutLabels();
}
void MainWindow::updateShortcutLabels() {
    if (!m_shortcuts)
        return;
    for (auto it = m_commands.begin(); it != m_commands.end(); ++it)
        if (const auto *row = m_shortcuts->entry(ShortcutRegistry::commandId(it.key())))
            it.value()->setShortcuts(row->bindings);
    for (const auto &group : ShortcutRegistry::toolGroups()) {
        auto *button = m_tools.value(group.second.first());
        if (!button)
            continue;
        QStringList tooltips;
        for (const QString &name : group.second) {
            const auto *entry = m_shortcuts->entry("tool." + ShortcutRegistry::commandId(name).mid(8));
            QStringList bindings;
            if (entry)
                for (const auto &key : entry->bindings)
                    bindings.append(key.toString(QKeySequence::NativeText));
            tooltips.append(name + (bindings.isEmpty() ? QString() : " (" + bindings.join(", ") + ")"));
        }
        button->setToolTip(tooltips.join('\n'));
    }
}
void MainWindow::activateShortcut(const ShortcutEntry &entry, bool released) {
    auto *canvas = currentCanvas();
    auto *document = currentDocument();
    if (entry.kind == ShortcutKind::Hold) {
        if (!released)
            m_holdCanvases[entry.payload] = canvas;
        auto heldCanvas = m_holdCanvases.value(entry.payload);
        if (entry.payload == "hand" && heldCanvas) {
            QKeyEvent key(released ? QEvent::KeyRelease : QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
            // The property prevents the registry from consuming its own forwarded native canvas event.
            heldCanvas->setProperty("shortcutForwarding", true);
            QApplication::sendEvent(heldCanvas, &key);
            heldCanvas->setProperty("shortcutForwarding", false);
        } else if (entry.payload == "move" && heldCanvas)
            heldCanvas->setProperty("temporaryMove", !released && paintingTool(m_tool));
        if (released)
            m_holdCanvases.remove(entry.payload);
        return;
    }
    if (released)
        return;
    if (entry.kind == ShortcutKind::Command) {
        if (auto *action = m_commands.value(entry.payload))
            action->trigger();
        else
            runCommand(entry.payload);
        return;
    }
    if (entry.kind == ShortcutKind::Tool || entry.kind == ShortcutKind::CycleTool) {
        QString chosen = entry.payload;
        for (const auto &group : ShortcutRegistry::toolGroups()) {
            if (entry.kind == ShortcutKind::CycleTool && group.first == entry.payload) {
                int index = group.second.indexOf(m_tool);
                chosen = group.second[(index + 1) % group.second.size()];
                m_lastToolInGroup[group.first] = chosen;
                break;
            }
            if (entry.kind == ShortcutKind::Tool && group.second.contains(entry.payload)) {
                if (entry.payload == group.second.first())
                    chosen =
                        group.second.contains(m_tool) ? m_tool : m_lastToolInGroup.value(group.first, chosen);
                m_lastToolInGroup[group.first] = chosen;
                break;
            }
        }
        selectTool(chosen);
        if (canvas)
            canvas->setFocus(Qt::ShortcutFocusReason);
        return;
    }
    if (entry.kind == ShortcutKind::Blend) {
        if (paintingTool(m_tool)) {
            m_options->findChild<QComboBox *>("brushMode")->setCurrentText(entry.payload);
            return;
        }
        if (document && document->activeLayer())
            document->mutate("Blend mode",
                             [document, entry] { document->activeLayer()->blendMode = entry.payload; });
        return;
    }
    if (entry.kind == ShortcutKind::Opacity || entry.kind == ShortcutKind::Flow) {
        if (!document || !document->activeLayer())
            return;
        QString target = paintingTool(m_tool) ? "brush" : "layer";
        target += entry.kind == ShortcutKind::Flow ? "-flow" : "-opacity";
        if (!m_opacityTimer.isValid() || m_opacityTimer.elapsed() > 650 || m_opacityTarget != target ||
            m_opacityDigits.size() >= 2)
            m_opacityDigits.clear();
        m_opacityTarget = target;
        m_opacityDigits += entry.payload;
        m_opacityTimer.restart();
        int value = m_opacityDigits.toInt();
        if (m_opacityDigits.size() == 1)
            value = value == 0 ? 100 : value * 10;
        if (m_opacityDigits == "00")
            value = 0;
        if (paintingTool(m_tool)) {
            if (auto *spin = m_options->findChild<QSpinBox *>(
                    entry.kind == ShortcutKind::Flow ? "flow" : "brushOpacity"))
                spin->setValue(value);
        } else {
            document->mutate(entry.kind == ShortcutKind::Flow ? "Layer fill" : "Layer opacity",
                             [document, value, entry] {
                                 if (entry.kind == ShortcutKind::Flow)
                                     document->activeLayer()->fill = value / 100.0;
                                 else
                                     document->activeLayer()->opacity = value / 100.0;
                             });
        }
        return;
    }
    const QString &local = entry.payload;
    if (local == "brush-behind" || local == "brush-clear") {
        m_options->findChild<QComboBox *>("brushMode")
            ->setCurrentText(local == "brush-behind" ? "Behind" : "Clear");
    } else if (local.startsWith("nudge-") && document && document->activeLayer()) {
        int amount = local.contains("fast-") ? 10 : 1;
        QPointF offset(local.endsWith("left")    ? -amount
                       : local.endsWith("right") ? amount
                                                 : 0,
                       local.endsWith("up")     ? -amount
                       : local.endsWith("down") ? amount
                                                : 0);
        if (canvas)
            canvas->nudgeSelectedLayers(offset);
    } else if (local == "crop-overlay") {
        if (canvas)
            canvas->cycleCropOverlay();
    } else if (local == "crop-swap-ratio") {
        if (auto *swap = m_options->findChild<QToolButton *>("cropSwapRatio"))
            swap->click();
    } else if (local == "swap-colors") {
        std::swap(m_foreground, m_background);
        selectTool(m_tool);
    } else if (local == "default-colors") {
        m_foreground = Qt::black;
        m_background = Qt::white;
        selectTool(m_tool);
    } else if (local == "quick-mask") {
        m_quickMask = !m_quickMask;
        if (canvas)
            canvas->setQuickMask(m_quickMask);
    } else if (local == "screen-mode")
        runCommand("Screen Mode");
    else if (local == "toggle-panels")
        setPanelsVisible(true);
    else if (local == "toggle-docks")
        setPanelsVisible(false);
    else if (local == "brush-larger" || local == "brush-smaller") {
        if (auto *spin = m_options->findChild<QSpinBox *>("brushSize")) {
            int size = spin->value();
            int step = size < 10 ? 1 : size < 50 ? 5 : size < 100 ? 10 : size < 200 ? 25 : 50;
            spin->setValue(size + (local == "brush-larger" ? step : -step));
        }
    } else if (local == "brush-harder" || local == "brush-softer") {
        if (auto *spin = m_options->findChild<QSpinBox *>("hardness"))
            spin->setValue(spin->value() + (local == "brush-harder" ? 25 : -25));
    } else if (local == "blend-next" || local == "blend-previous") {
        if (paintingTool(m_tool)) {
            auto *mode = m_options->findChild<QComboBox *>("brushMode");
            mode->setCurrentIndex((mode->currentIndex() + (local == "blend-next" ? 1 : mode->count() - 1)) %
                                  mode->count());
            return;
        }
        if (document && document->activeLayer()) {
            auto modes = blendModeNames();
            int index = modes.indexOf(document->activeLayer()->blendMode);
            index = (index + (local == "blend-next" ? 1 : modes.size() - 1)) % modes.size();
            document->mutate("Blend mode",
                             [document, modes, index] { document->activeLayer()->blendMode = modes[index]; });
        }
    } else if (local == "mask-overlay") {
        if (canvas)
            canvas->setMaskPreview(canvas->maskPreview() == CanvasView::MaskPreview::Overlay
                                       ? CanvasView::MaskPreview::None
                                       : CanvasView::MaskPreview::Overlay);
    } else if ((local == "commit" || local == "cancel") && canvas) {
        m_shortcuts->setProperty("forwardingNative", true);
        canvas->setProperty("shortcutForwarding", true);
        QKeyEvent key(QEvent::KeyPress, local == "commit" ? Qt::Key_Return : Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(canvas, &key);
        canvas->setProperty("shortcutForwarding", false);
        m_shortcuts->setProperty("forwardingNative", false);
    }
}
void MainWindow::keyboardShortcuts() {
    QDialog dialog(this);
    dialog.setWindowTitle("Keyboard Shortcuts");
    dialog.setObjectName("keyboardShortcutDialog");
    dialog.resize(900, 680);
    auto *layout = new QVBoxLayout(&dialog);
    auto *search = new QLineEdit;
    search->setPlaceholderText("Search commands, tools, panels or shortcuts");
    search->setObjectName("shortcutSearch");
    layout->addWidget(search);
    auto *explanation = new QLabel(
        "Select a row, then press a key combination. Three bindings per command are shown; imported sets can "
        "contain more. Hold bindings are press/release pairs. Empty bindings disable a shortcut.");
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto *table = new QTableWidget(0, 5);
    table->setObjectName("shortcutTable");
    table->setHorizontalHeaderLabels({"Category", "Command / tool", "Binding 1", "Binding 2", "Binding 3"});
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setColumnWidth(0, 120);
    for (int column = 2; column < 5; ++column)
        table->setColumnWidth(column, 145);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table, 1);
    auto *status = new QLabel;
    status->setObjectName("shortcutConflict");
    status->setWordWrap(true);
    layout->addWidget(status);
    auto draft = m_shortcuts->bindingMap();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto validate = [&] {
        auto errors = m_shortcuts->conflicts(draft);
        status->setText(errors.isEmpty() ? "No conflicts. Text fields retain their normal editing shortcuts."
                                         : errors.join('\n'));
        buttons->button(QDialogButtonBox::Save)->setEnabled(errors.isEmpty());
    };
    auto populate = [&] {
        table->setRowCount(0);
        QList<ShortcutEntry> rows = m_shortcuts->entries();
        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
            return a.category == b.category ? a.label < b.label : a.category < b.category;
        });
        for (const auto &entry : rows) {
            int row = table->rowCount();
            table->insertRow(row);
            auto *category = new QTableWidgetItem(entry.category);
            category->setFlags(category->flags() & ~Qt::ItemIsEditable);
            table->setItem(row, 0, category);
            auto *name = new QTableWidgetItem(entry.label);
            name->setFlags(name->flags() & ~Qt::ItemIsEditable);
            name->setData(Qt::UserRole, entry.id);
            name->setToolTip(entry.id);
            table->setItem(row, 1, name);
            for (int binding = 0; binding < 3; ++binding) {
                auto *editor = new QKeySequenceEdit;
                editor->setClearButtonEnabled(true);
                editor->setMaximumSequenceLength(4);
                editor->setKeySequence(draft.value(entry.id).value(binding));
                table->setCellWidget(row, binding + 2, editor);
                connect(editor, &QKeySequenceEdit::keySequenceChanged, &dialog,
                        [&, id = entry.id, binding](const QKeySequence &sequence) {
                            auto list = draft.value(id);
                            while (list.size() <= binding)
                                list.append(QKeySequence());
                            list[binding] = sequence;
                            while (!list.isEmpty() && list.last().isEmpty())
                                list.removeLast();
                            draft[id] = list;
                            validate();
                        });
            }
        }
        validate();
    };
    auto compact = [&] {
        auto result = draft;
        for (auto it = result.begin(); it != result.end(); ++it) {
            QList<QKeySequence> nonempty;
            for (const auto &key : it.value())
                if (!key.isEmpty())
                    nonempty.append(key);
            it.value() = nonempty;
        }
        return result;
    };
    auto *utilities = new QHBoxLayout;
    auto *reset = new QPushButton("Reset all to defaults");
    auto *resetRow = new QPushButton("Reset selected");
    auto *clear = new QPushButton("Clear selected");
    auto *import = new QPushButton("Import...");
    auto *exportButton = new QPushButton("Export...");
    for (auto *button : {reset, resetRow, clear, import, exportButton})
        utilities->addWidget(button);
    utilities->addStretch();
    layout->addLayout(utilities);
    layout->addWidget(buttons);
    connect(search, &QLineEdit::textChanged, &dialog, [&](const QString &text) {
        for (int row = 0; row < table->rowCount(); ++row) {
            QString all = table->item(row, 0)->text() + " " + table->item(row, 1)->text();
            for (const auto &key : draft.value(table->item(row, 1)->data(Qt::UserRole).toString()))
                all += " " + key.toString(QKeySequence::NativeText);
            table->setRowHidden(row, !all.contains(text, Qt::CaseInsensitive));
        }
    });
    connect(reset, &QPushButton::clicked, &dialog, [&] {
        for (const auto &entry : m_shortcuts->entries())
            draft[entry.id] = entry.defaults;
        populate();
    });
    connect(resetRow, &QPushButton::clicked, &dialog, [&] {
        if (table->currentRow() >= 0) {
            QString id = table->item(table->currentRow(), 1)->data(Qt::UserRole).toString();
            draft[id] = m_shortcuts->entry(id)->defaults;
            populate();
        }
    });
    connect(clear, &QPushButton::clicked, &dialog, [&] {
        if (table->currentRow() >= 0) {
            draft[table->item(table->currentRow(), 1)->data(Qt::UserRole).toString()] = {};
            populate();
        }
    });
    connect(import, &QPushButton::clicked, &dialog, [&] {
        QString path =
            QFileDialog::getOpenFileName(&dialog, "Import shortcuts", {}, "Shortcut sets (*.json)");
        if (path.isEmpty())
            return;
        ShortcutRegistry temporary(nullptr);
        for (const auto &entry : m_shortcuts->entries())
            if (entry.kind == ShortcutKind::Command)
                temporary.addCommand(entry.payload, entry.category);
        QString error;
        if (!temporary.load(path, &error))
            QMessageBox::warning(&dialog, "Import failed", error);
        else {
            draft = temporary.bindingMap();
            populate();
        }
    });
    connect(exportButton, &QPushButton::clicked, &dialog, [&] {
        QString path = QFileDialog::getSaveFileName(&dialog, "Export shortcuts", "Serika-shortcuts.json",
                                                    "Shortcut sets (*.json)");
        if (path.isEmpty())
            return;
        ShortcutRegistry temporary(nullptr);
        for (const auto &entry : m_shortcuts->entries())
            if (entry.kind == ShortcutKind::Command)
                temporary.addCommand(entry.payload, entry.category);
        QString error;
        if (!temporary.setAllBindings(compact(), &error) || !temporary.save(path, &error))
            QMessageBox::warning(&dialog, "Export failed", error);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    populate();
    if (dialog.exec() == QDialog::Accepted) {
        QString error;
        if (!m_shortcuts->setAllBindings(compact(), &error) || !m_shortcuts->save(shortcutPath(), &error))
            QMessageBox::warning(this, "Shortcuts could not be saved", error);
    }
}
} // namespace serika
