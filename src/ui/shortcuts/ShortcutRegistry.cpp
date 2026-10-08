#include "ShortcutRegistry.h"
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QSet>
#include <QTextEdit>
#include <QWidget>
#include <algorithm>

namespace serika {
namespace {
QList<QKeySequence> keys(const QStringList &texts) {
    QList<QKeySequence> result;
    for (const QString &text : texts) {
        const auto sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
        if (!sequence.isEmpty() && !result.contains(sequence))
            result.append(sequence);
    }
    return result;
}
QKeyCombination combination(const QKeyEvent &event) {
    int key = event.key();
    const bool backtab = key == Qt::Key_Backtab;
    if (backtab)
        key = Qt::Key_Tab;
    // Qt reports shifted punctuation on some keyboard layouts and the base key on others.
    if (key == Qt::Key_BraceLeft)
        key = Qt::Key_BracketLeft;
    if (key == Qt::Key_BraceRight)
        key = Qt::Key_BracketRight;
    const QString shiftedDigits = ")!@#$%^&*(";
    if (event.modifiers().testFlag(Qt::ShiftModifier) && key < 128) {
        int index = shiftedDigits.indexOf(QChar(key));
        if (index >= 0)
            key = Qt::Key_0 + index;
    }
    auto modifiers =
        event.modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (backtab)
        modifiers |= Qt::ShiftModifier;
    if (key == Qt::Key_Control)
        modifiers &= ~Qt::ControlModifier;
    if (key == Qt::Key_Shift)
        modifiers &= ~Qt::ShiftModifier;
    if (key == Qt::Key_Alt)
        modifiers &= ~Qt::AltModifier;
    if (key == Qt::Key_Meta)
        modifiers &= ~Qt::MetaModifier;
    return QKeyCombination(modifiers, Qt::Key(key));
}
bool startsWith(const QKeySequence &sequence, const QList<QKeyCombination> &prefix) {
    if (sequence.count() < prefix.size())
        return false;
    for (qsizetype i = 0; i < prefix.size(); ++i)
        if (sequence[int(i)] != prefix[i])
            return false;
    return true;
}
bool validSequence(const QKeySequence &sequence) {
    if (sequence.isEmpty())
        return false;
    for (int index = 0; index < sequence.count(); ++index)
        if (sequence[index].key() == Qt::Key_unknown || int(sequence[index].key()) == 0)
            return false;
    return true;
}
} // namespace
QString ShortcutRegistry::commandId(const QString &name) {
    QString id = name.toLower();
    const bool dialog = id.endsWith("...");
    id.replace("...", "");
    id.replace('/', '-');
    id.replace(' ', '-');
    id.remove(':');
    id.remove('%');
    id.remove(QChar(0xb0));
    return "command." + id + (dialog ? "-dialog" : "");
}
QList<QPair<QString, QStringList>> ShortcutRegistry::toolGroups() {
    return {{"V", {"Move", "Artboard"}},
            {"M", {"Rectangular Marquee", "Elliptical Marquee", "Single Row", "Single Column"}},
            {"L", {"Lasso", "Polygonal Lasso", "Magnetic Lasso"}},
            {"W", {"Object Selection", "Quick Selection", "Magic Wand"}},
            {"C", {"Crop", "Perspective Crop", "Slice", "Slice Select"}},
            {"K", {"Frame"}},
            {"I", {"Eyedropper", "Color Sampler", "Ruler", "Note", "Count"}},
            {"J", {"Spot Healing", "Healing Brush", "Patch", "Content-Aware Move", "Red Eye"}},
            {"B", {"Brush", "Pencil", "Color Replacement", "Mixer Brush"}},
            {"S", {"Clone Stamp", "Pattern Stamp"}},
            {"Y", {"History Brush", "Art History Brush"}},
            {"E", {"Eraser", "Background Eraser", "Magic Eraser"}},
            {"G", {"Gradient", "Paint Bucket"}},
            {"", {"Blur", "Sharpen", "Smudge"}},
            {"O", {"Dodge", "Burn", "Sponge"}},
            {"P", {"Pen", "Freeform Pen", "Curvature Pen", "Add Anchor", "Delete Anchor", "Convert Point"}},
            {"T", {"Horizontal Type", "Vertical Type", "Horizontal Type Mask", "Vertical Type Mask"}},
            {"A", {"Path Selection", "Direct Selection"}},
            {"U", {"Rectangle", "Ellipse", "Triangle", "Polygon", "Line", "Custom Shape"}},
            {"H", {"Hand"}},
            {"R", {"Rotate View"}},
            {"Z", {"Zoom"}}};
}
QList<ShortcutEntry> ShortcutRegistry::defaultEntries() {
    QList<ShortcutEntry> rows;
    auto add = [&](QString id, QString label, QString category, ShortcutKind kind, QString payload,
                   QStringList shortcuts, bool textSafe = false, bool repeat = false) {
        auto bindings = keys(shortcuts);
        rows.append({id, label, category, kind, payload, bindings, bindings, textSafe, repeat});
        if (textSafe)
            rows.last().scope = "global";
    };
    auto command = [&](QString name, QString category, QStringList shortcuts, bool textSafe = false) {
        add(commandId(name), name, category, ShortcutKind::Command, name, shortcuts, textSafe);
    };
    // In Qt, Ctrl in PortableText maps to Command on macOS. Option is Alt on every platform.
    for (auto row : QList<QPair<QString, QString>>{{"New...", "Ctrl+N"},
                                                   {"Open...", "Ctrl+O"},
                                                   {"Save", "Ctrl+S"},
                                                   {"Save As...", "Ctrl+Shift+S"},
                                                   {"Save a Copy...", "Ctrl+Alt+S"},
                                                   {"Close", "Ctrl+W"},
                                                   {"Close All", "Ctrl+Alt+W"},
                                                   {"Exit", "Ctrl+Q"},
                                                   {"Print...", "Ctrl+P"},
                                                   {"Print One Copy", "Ctrl+Alt+Shift+P"},
                                                   {"Export As...", "Ctrl+Alt+Shift+S"}})
        command(row.first, "File", {row.second}, true);
    const QList<QPair<QString, QString>> commands = {{"Undo", "Ctrl+Z"},
                                                     {"Redo", "Ctrl+Shift+Z"},
                                                     {"Step Backward", "Ctrl+Alt+Z"},
                                                     {"Free Transform...", "Ctrl+T"},
                                                     {"Cut", "Ctrl+X"},
                                                     {"Copy", "Ctrl+C"},
                                                     {"Copy Merged", "Ctrl+Shift+C"},
                                                     {"Paste", "Ctrl+V"},
                                                     {"Paste in Place", "Ctrl+Shift+V"},
                                                     {"Paste Into", "Ctrl+Alt+Shift+V"},
                                                     {"Fill...", "Shift+F5"},
                                                     {"Fade...", "Ctrl+Shift+F"},
                                                     {"Preferences...", "Ctrl+,"},
                                                     {"Keyboard Shortcuts...", "Ctrl+Alt+Shift+K"},
                                                     {"New Layer...", "Ctrl+Shift+N"},
                                                     {"New Layer", "Ctrl+Alt+Shift+N"},
                                                     {"Duplicate Layer", "Ctrl+J"},
                                                     {"Layer via Cut", "Ctrl+Shift+J"},
                                                     {"Group Layers", "Ctrl+G"},
                                                     {"Ungroup Layers", "Ctrl+Shift+G"},
                                                     {"Merge Down", "Ctrl+E"},
                                                     {"Merge Visible", "Ctrl+Shift+E"},
                                                     {"Stamp Visible", "Ctrl+Alt+Shift+E"},
                                                     {"Bring Forward", "Ctrl+]"},
                                                     {"Send Backward", "Ctrl+["},
                                                     {"Bring to Front", "Ctrl+Shift+]"},
                                                     {"Send to Back", "Ctrl+Shift+["},
                                                     {"Create Clipping Mask", "Ctrl+Alt+G"},
                                                     {"All", "Ctrl+A"},
                                                     {"Deselect", "Ctrl+D"},
                                                     {"Reselect", "Ctrl+Shift+D"},
                                                     {"Inverse", "Ctrl+Shift+I"},
                                                     {"All Layers", "Ctrl+Alt+A"},
                                                     {"Select and Mask...", "Ctrl+Alt+R"},
                                                     {"Zoom Out", "Ctrl+-"},
                                                     {"Fit on Screen", "Ctrl+0"},
                                                     {"100%", "Ctrl+1"},
                                                     {"Rulers", "Ctrl+R"},
                                                     {"Guides", "Ctrl+;"},
                                                     {"Grid", "Ctrl+'"},
                                                     {"Extras", "Ctrl+H"},
                                                     {"Levels", "Ctrl+L"},
                                                     {"Curves", "Ctrl+M"},
                                                     {"Hue/Saturation", "Ctrl+U"},
                                                     {"Color Balance", "Ctrl+B"},
                                                     {"Invert", "Ctrl+I"},
                                                     {"Desaturate", "Ctrl+Shift+U"},
                                                     {"Auto Tone", "Ctrl+Shift+L"},
                                                     {"Auto Color", "Ctrl+Shift+B"},
                                                     {"Auto Contrast", "Ctrl+Alt+Shift+L"},
                                                     {"Black & White", "Ctrl+Alt+Shift+B"},
                                                     {"Image Size...", "Ctrl+Alt+I"},
                                                     {"Canvas Size...", "Ctrl+Alt+C"},
                                                     {"Last Filter", "Ctrl+F"},
                                                     {"Liquify...", "Ctrl+Shift+X"},
                                                     {"Camera Raw Filter...", "Ctrl+Shift+A"},
                                                     {"Command Palette...", "Ctrl+Shift+P"},
                                                     {"Select Previous Layer", "Alt+["},
                                                     {"Select Next Layer", "Alt+]"},
                                                     {"Select Top Layer", "Alt+."},
                                                     {"Select Bottom Layer", "Alt+,"}};
    for (const auto &row : commands)
        command(row.first, "Application menus", {row.second},
                row.first == "Preferences..." || row.first == "Keyboard Shortcuts..." ||
                    row.first == "Command Palette...");
    command("Flatten Image", "Layer", {});
    for (auto &row : rows)
        if (row.payload == "Duplicate Layer") {
            row.id = commandId("Layer via Copy");
            row.label = row.payload = "Layer via Copy";
        }
    command("Zoom In", "View", {"Ctrl++", "Ctrl+=", "Ctrl+Shift+="});
    command("Delete Selected Content", "Edit", {"Backspace", "Del"});
    command("Fill Foreground", "Edit", {"Alt+Backspace", "Alt+Del"});
    command("Fill Background", "Edit", {"Ctrl+Backspace", "Ctrl+Del"});
    for (const auto &group : toolGroups()) {
        for (qsizetype index = 0; index < group.second.size(); ++index) {
            QString name = group.second[index];
            QString id = "tool." + commandId(name).mid(8);
            add(id, name, "Tools", ShortcutKind::Tool, name,
                index == 0 && !group.first.isEmpty() ? QStringList{group.first} : QStringList{});
        }
        if (!group.first.isEmpty())
            add("tool.cycle-" + group.first.toLower(), "Cycle " + group.second.join(" / "), "Tools",
                ShortcutKind::CycleTool, group.first, {"Shift+" + group.first});
    }
    const QList<QPair<QString, QString>> local = {
        {"swap-colors", "X"},        {"default-colors", "D"},   {"quick-mask", "Q"},
        {"screen-mode", "F"},        {"toggle-panels", "Tab"},  {"toggle-docks", "Shift+Tab"},
        {"brush-smaller", "["},      {"brush-larger", "]"},     {"brush-softer", "Shift+["},
        {"brush-harder", "Shift+]"}, {"mask-overlay", "\\"},    {"commit", "Ctrl+Return"},
        {"cancel", "Esc"},           {"blend-next", "Shift++"}, {"blend-previous", "Shift+-"}};
    for (const auto &row : local)
        add("editor." + row.first, QString(row.first).replace('-', ' '), "Editor", ShortcutKind::Local,
            row.first, {row.second}, false, row.first.startsWith("brush-") || row.first.startsWith("blend-"));
    add("editor.commit-enter", "Commit (numeric keypad)", "Editor", ShortcutKind::Local, "commit",
        {"Ctrl+Enter"});
    add("editor.commit-return", "Commit", "Editor", ShortcutKind::Local, "commit", {"Return", "Enter"});
    add("editor.blend-next-equals", "Next blend mode (keyboard variant)", "Editor", ShortcutKind::Local,
        "blend-next", {"Shift+="});
    add("editor.temporary-hand", "Temporary Hand (hold)", "Tools", ShortcutKind::Hold, "hand", {"Space"});
    add("editor.temporary-move", "Temporary Move (hold)", "Tools", ShortcutKind::Hold, "move", {});
    rows.last().defaults = rows.last().bindings = {QKeySequence(Qt::Key_Control)};
    add("crop.cycle-overlay", "Cycle crop composition overlay", "Crop", ShortcutKind::Local, "crop-overlay",
        {"O"});
    rows.last().scope = "crop";
    add("crop.swap-ratio", "Swap crop width and height", "Crop", ShortcutKind::Local, "crop-swap-ratio",
        {"X"});
    rows.last().scope = "crop";
    for (const QString &direction : QStringList{"Left", "Right", "Up", "Down"}) {
        add("editor.nudge-" + direction.toLower(), "Nudge " + direction.toLower(), "Move",
            ShortcutKind::Local, "nudge-" + direction.toLower(), {direction}, false, true);
        rows.last().scope = "move";
        add("editor.nudge-fast-" + direction.toLower(), "Nudge " + direction.toLower() + " 10 pixels", "Move",
            ShortcutKind::Local, "nudge-fast-" + direction.toLower(), {"Shift+" + direction}, false, true);
        rows.last().scope = "move";
    }
    const QList<QPair<QString, QString>> blendKeys = {{"Normal", "N"},       {"Dissolve", "I"},
                                                      {"Darken", "K"},       {"Multiply", "M"},
                                                      {"Color Burn", "B"},   {"Linear Burn", "A"},
                                                      {"Lighten", "G"},      {"Screen", "S"},
                                                      {"Color Dodge", "D"},  {"Linear Dodge (Add)", "W"},
                                                      {"Overlay", "O"},      {"Soft Light", "F"},
                                                      {"Hard Light", "H"},   {"Vivid Light", "V"},
                                                      {"Linear Light", "J"}, {"Pin Light", "Z"},
                                                      {"Hard Mix", "L"},     {"Difference", "E"},
                                                      {"Exclusion", "X"},    {"Hue", "U"},
                                                      {"Saturation", "T"},   {"Color", "C"},
                                                      {"Luminosity", "Y"}};
    for (const auto &row : blendKeys)
        add("blend." + commandId(row.first).mid(8), row.first, "Blending modes", ShortcutKind::Blend,
            row.first, {"Shift+Alt+" + row.second});
    for (const QString &name : QStringList{"Darker Color", "Lighter Color", "Subtract", "Divide"})
        add("blend." + commandId(name).mid(8), name, "Blending modes", ShortcutKind::Blend, name, {});
    add("brush.behind", "Brush Behind blend mode", "Painting", ShortcutKind::Local, "brush-behind",
        {"Shift+Alt+Q"});
    rows.last().scope = "painting";
    add("brush.clear", "Brush Clear blend mode", "Painting", ShortcutKind::Local, "brush-clear",
        {"Shift+Alt+R"});
    rows.last().scope = "painting";
    for (int digit = 0; digit <= 9; ++digit) {
        QString number = QString::number(digit);
        add("opacity." + number, "Opacity " + QString::number(digit ? digit * 10 : 100) + "%", "Painting",
            ShortcutKind::Opacity, number, {number});
        add("flow." + number, "Flow / layer fill " + QString::number(digit ? digit * 10 : 100) + "%",
            "Painting", ShortcutKind::Flow, number, {"Shift+" + number});
    }
    return rows;
}
ShortcutRegistry::ShortcutRegistry(QWidget *owner, QObject *parent)
    : QObject(parent ? parent : owner), m_owner(owner), m_entries(defaultEntries()) {
    if (qApp)
        qApp->installEventFilter(this);
}
const ShortcutEntry *ShortcutRegistry::entry(const QString &id) const {
    for (const auto &row : m_entries)
        if (row.id == id)
            return &row;
    return nullptr;
}
void ShortcutRegistry::addCommand(const QString &name, const QString &category) {
    if (entry(commandId(name)))
        return;
    m_entries.append({commandId(name), name, category, ShortcutKind::Command, name, {}, {}, false, false});
}
QHash<QString, QList<QKeySequence>> ShortcutRegistry::bindingMap() const {
    QHash<QString, QList<QKeySequence>> result;
    for (const auto &row : m_entries)
        result.insert(row.id, row.bindings);
    return result;
}
QStringList ShortcutRegistry::conflicts(const QHash<QString, QList<QKeySequence>> &bindings) const {
    QStringList result;
    QList<QPair<QString, QKeySequence>> all;
    for (const auto &row : m_entries)
        for (const auto &key : bindings.value(row.id, row.bindings)) {
            if (key.isEmpty() || key[0].key() == Qt::Key_unknown)
                continue;
            for (const auto &other : all)
                if (other.first != row.id &&
                    (row.scope == entry(other.first)->scope || row.scope == "global" ||
                     entry(other.first)->scope == "global") &&
                    (key.matches(other.second) != QKeySequence::NoMatch ||
                     other.second.matches(key) != QKeySequence::NoMatch))
                    result.append(QString("%1 conflicts with %2: %3")
                                      .arg(row.label, entry(other.first)->label,
                                           key.toString(QKeySequence::NativeText)));
            all.append({row.id, key});
        }
    return result;
}
bool ShortcutRegistry::setBindings(const QString &id, const QList<QKeySequence> &sequences, QString *error) {
    if (!entry(id)) {
        if (error)
            *error = "Unknown shortcut ID: " + id;
        return false;
    }
    auto map = bindingMap();
    map[id] = sequences;
    return setAllBindings(map, error);
}
bool ShortcutRegistry::setAllBindings(const QHash<QString, QList<QKeySequence>> &bindings, QString *error) {
    for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) {
        if (!entry(it.key())) {
            if (error)
                *error = "Unknown shortcut ID: " + it.key();
            return false;
        }
        for (const auto &key : it.value())
            if (!validSequence(key)) {
                if (error)
                    *error = "Invalid shortcut for " + it.key();
                return false;
            }
    }
    auto errors = conflicts(bindings);
    if (!errors.isEmpty()) {
        if (error)
            *error = errors.join('\n');
        return false;
    }
    releaseHolds();
    m_prefix.clear();
    for (auto &row : m_entries)
        if (bindings.contains(row.id)) {
            row.bindings.clear();
            for (const auto &key : bindings[row.id])
                if (!row.bindings.contains(key))
                    row.bindings.append(key);
        }
    emit bindingsChanged();
    return true;
}
void ShortcutRegistry::reset() {
    releaseHolds();
    m_prefix.clear();
    for (auto &row : m_entries)
        row.bindings = row.defaults;
    emit bindingsChanged();
}
QJsonObject ShortcutRegistry::toJson() const {
    QJsonObject overrides;
    for (const auto &row : m_entries)
        if (row.bindings != row.defaults) {
            QJsonArray array;
            for (const auto &key : row.bindings)
                array.append(key.toString(QKeySequence::PortableText));
            overrides.insert(row.id, array);
        }
    return {{"schema", 1}, {"application", "Serika PhotoEdit"}, {"bindings", overrides}};
}
bool ShortcutRegistry::fromJson(const QJsonObject &json, QString *error) {
    if (json.value("schema").toInt() != 1 || !json.value("bindings").isObject()) {
        if (error)
            *error = "Expected a Serika shortcut set with schema 1 and a bindings object.";
        return false;
    }
    QHash<QString, QList<QKeySequence>> map;
    for (const auto &row : m_entries)
        map[row.id] = row.defaults;
    const auto object = json.value("bindings").toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!entry(it.key()) || !it.value().isArray()) {
            if (error)
                *error = "Unknown shortcut or malformed bindings: " + it.key();
            return false;
        }
        QList<QKeySequence> list;
        for (const auto &value : it.value().toArray()) {
            if (!value.isString() || value.toString().isEmpty()) {
                if (error)
                    *error = "Shortcut values must be nonempty PortableText strings.";
                return false;
            }
            auto sequence = QKeySequence::fromString(value.toString(), QKeySequence::PortableText);
            if (!validSequence(sequence)) {
                if (error)
                    *error = "Invalid shortcut: " + value.toString();
                return false;
            }
            list.append(sequence);
        }
        map[it.key()] = list;
    }
    return setAllBindings(map, error);
}
bool ShortcutRegistry::load(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = parse.errorString();
        return false;
    }
    return fromJson(document.object(), error);
}
bool ShortcutRegistry::save(const QString &path, QString *error) const {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(toJson()).toJson()) < 0 ||
        !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
bool ShortcutRegistry::isTextInput(QWidget *widget) {
    for (auto *current = widget; current; current = current->parentWidget()) {
        if (qobject_cast<QLineEdit *>(current) || qobject_cast<QTextEdit *>(current) ||
            qobject_cast<QPlainTextEdit *>(current) || qobject_cast<QAbstractSpinBox *>(current) ||
            qobject_cast<QKeySequenceEdit *>(current))
            return true;
        if (qobject_cast<QComboBox *>(current))
            return true;
    }
    return false;
}
bool ShortcutRegistry::inOwner(QWidget *widget) const {
    return m_owner && widget && (widget == m_owner || m_owner->isAncestorOf(widget));
}
bool ShortcutRegistry::eligible(const ShortcutEntry &row, QWidget *focus) const {
    return (!isTextInput(focus) || row.textSafe) && (!enabled || enabled(row));
}
QList<const ShortcutEntry *> ShortcutRegistry::matching(const QList<QKeyCombination> &prefix,
                                                        bool exact) const {
    QList<const ShortcutEntry *> rows;
    for (const auto &row : m_entries)
        for (const auto &key : row.bindings)
            if ((!exact || key.count() == prefix.size()) && startsWith(key, prefix)) {
                rows.append(&row);
                break;
            }
    std::stable_sort(rows.begin(), rows.end(), [](const auto *a, const auto *b) {
        return a->scope != "editor" && b->scope == "editor";
    });
    return rows;
}
void ShortcutRegistry::releaseHolds() {
    const auto held = m_held;
    m_held.clear();
    for (const QString &id : held)
        if (const auto *row = entry(id); row && activated)
            activated(*row, true);
}
bool ShortcutRegistry::dispatch(QKeyEvent *event, QWidget *focus) {
    if (event->type() == QEvent::KeyRelease) {
        if (event->isAutoRepeat() || !m_held.contains(event->key()))
            return false;
        QString id = m_held.take(event->key());
        if (const auto *row = entry(id); row && activated)
            activated(*row, true);
        return true;
    }
    if (event->type() != QEvent::KeyPress)
        return false;
    if (!m_prefix.isEmpty() && m_prefixTimer.elapsed() > 1500)
        m_prefix.clear();
    auto prefix = m_prefix;
    prefix.append(combination(*event));
    auto matches = matching(prefix, true);
    if (matches.isEmpty() && !m_prefix.isEmpty() && matching(prefix, false).isEmpty()) {
        prefix = {combination(*event)};
        matches = matching(prefix, true);
        m_prefix.clear();
    }
    for (const auto *row : matches) {
        if (!eligible(*row, focus))
            continue;
        m_prefix.clear();
        if (event->isAutoRepeat() && !row->repeat)
            return true;
        if (row->kind == ShortcutKind::Hold)
            m_held.insert(event->key(), row->id);
        if (activated)
            activated(*row, false);
        return true;
    }
    for (const auto *row : matching(prefix, false))
        if (eligible(*row, focus)) {
            m_prefix = prefix;
            m_prefixTimer.restart();
            return true;
        }
    m_prefix.clear();
    return false;
}
bool ShortcutRegistry::eventFilter(QObject *object, QEvent *event) {
    if (property("forwardingNative").toBool())
        return false;
    if (event->type() == QEvent::ApplicationDeactivate || event->type() == QEvent::WindowDeactivate) {
        releaseHolds();
        m_prefix.clear();
        return false;
    }
    auto *widget = qobject_cast<QWidget *>(object);
    if (widget && widget->property("shortcutForwarding").toBool())
        return false;
    if (!inOwner(widget) || (qApp->activeModalWidget() && qApp->activeModalWidget() != m_owner))
        return false;
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        const QList<QKeyCombination> prefix{combination(*key)};
        // Claim registered keys even in text controls so native QAction shortcuts cannot steal editing.
        if (!matching(prefix, false).isEmpty() || !m_prefix.isEmpty()) {
            key->accept();
            return true;
        }
    } else if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        QWidget *focus = QApplication::focusWidget();
        if (!inOwner(focus))
            focus = widget;
        if (dispatch(static_cast<QKeyEvent *>(event), focus)) {
            event->accept();
            return true;
        }
    }
    return false;
}
} // namespace serika
