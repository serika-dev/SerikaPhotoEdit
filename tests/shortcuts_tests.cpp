#include "ui/MainWindow.h"
#include "ui/shortcuts/ShortcutRegistry.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFile>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QtTest>

using namespace serika;
class ShortcutTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_directory;
    static void key(ShortcutRegistry &registry, const QKeySequence &sequence, QWidget *focus) {
        for (int index = 0; index < sequence.count(); ++index) {
            auto combination = sequence[index];
            QKeyEvent press(QEvent::KeyPress, combination.key(), combination.keyboardModifiers());
            QVERIFY(registry.dispatch(&press, focus));
            QKeyEvent release(QEvent::KeyRelease, combination.key(), combination.keyboardModifiers());
            registry.dispatch(&release, focus);
        }
    }
    static void show(MainWindow &window) {
        window.show();
        window.activateWindow();
        window.openDemo();
        QTest::qWait(30);
        window.currentCanvas()->setFocus();
    }
  private slots:
    void initTestCase() {
        QVERIFY(m_directory.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_directory.path());
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
                      "/shortcuts.json");
    }
    void completeDefaultTableHasNoConflictsAndStableIds() {
        QWidget owner;
        ShortcutRegistry registry(&owner);
        QVERIFY(registry.entries().size() > 200);
        QSet<QString> ids;
        for (const auto &entry : registry.entries()) {
            QVERIFY2(!entry.id.isEmpty(), qPrintable(entry.label));
            QVERIFY2(!ids.contains(entry.id), qPrintable(entry.id));
            ids.insert(entry.id);
            QCOMPARE(entry.bindings, entry.defaults);
            for (const auto &sequence : entry.defaults) {
                QVERIFY(!sequence.isEmpty());
                QVERIFY(sequence[0].key() != Qt::Key_unknown);
            }
        }
        QVERIFY2(registry.conflicts(registry.bindingMap()).isEmpty(),
                 qPrintable(registry.conflicts(registry.bindingMap()).join('\n')));
        const QList<QPair<QString, QString>> required = {{"New...", "Ctrl+N"},
                                                         {"Open...", "Ctrl+O"},
                                                         {"Save", "Ctrl+S"},
                                                         {"Save As...", "Ctrl+Shift+S"},
                                                         {"Close", "Ctrl+W"},
                                                         {"Exit", "Ctrl+Q"},
                                                         {"Print...", "Ctrl+P"},
                                                         {"Export As...", "Ctrl+Alt+Shift+S"},
                                                         {"Undo", "Ctrl+Z"},
                                                         {"Redo", "Ctrl+Shift+Z"},
                                                         {"Step Backward", "Ctrl+Alt+Z"},
                                                         {"Free Transform...", "Ctrl+T"},
                                                         {"Copy Merged", "Ctrl+Shift+C"},
                                                         {"Paste in Place", "Ctrl+Shift+V"},
                                                         {"New Layer...", "Ctrl+Shift+N"},
                                                         {"New Layer", "Ctrl+Alt+Shift+N"},
                                                         {"Layer via Copy", "Ctrl+J"},
                                                         {"Layer via Cut", "Ctrl+Shift+J"},
                                                         {"Group Layers", "Ctrl+G"},
                                                         {"Ungroup Layers", "Ctrl+Shift+G"},
                                                         {"Merge Down", "Ctrl+E"},
                                                         {"Merge Visible", "Ctrl+Shift+E"},
                                                         {"Stamp Visible", "Ctrl+Alt+Shift+E"},
                                                         {"Fade...", "Ctrl+Shift+F"},
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
                                                         {"Zoom Out", "Ctrl+-"},
                                                         {"Fit on Screen", "Ctrl+0"},
                                                         {"100%", "Ctrl+1"},
                                                         {"Rulers", "Ctrl+R"},
                                                         {"Guides", "Ctrl+;"},
                                                         {"Grid", "Ctrl+'"},
                                                         {"Levels", "Ctrl+L"},
                                                         {"Curves", "Ctrl+M"},
                                                         {"Hue/Saturation", "Ctrl+U"},
                                                         {"Color Balance", "Ctrl+B"},
                                                         {"Invert", "Ctrl+I"},
                                                         {"Desaturate", "Ctrl+Shift+U"},
                                                         {"Auto Tone", "Ctrl+Shift+L"},
                                                         {"Auto Color", "Ctrl+Shift+B"},
                                                         {"Black & White", "Ctrl+Alt+Shift+B"}};
        for (const auto &requiredRow : required) {
            auto *row = registry.entry(ShortcutRegistry::commandId(requiredRow.first));
            QVERIFY2(row, qPrintable(requiredRow.first));
            QVERIFY2(row->defaults.contains(QKeySequence(requiredRow.second)), qPrintable(requiredRow.first));
        }
        for (const auto &group : ShortcutRegistry::toolGroups())
            if (!group.first.isEmpty()) {
                auto *row =
                    registry.entry("tool." + ShortcutRegistry::commandId(group.second.first()).mid(8));
                QVERIFY(row->defaults.contains(QKeySequence(group.first)));
                QVERIFY(registry.entry("tool.cycle-" + group.first.toLower())
                            ->defaults.contains(QKeySequence("Shift+" + group.first)));
            }
        int blends = 0;
        for (const auto &row : registry.entries())
            blends += row.kind == ShortcutKind::Blend;
        QCOMPARE(blends, 27);
    }
    void everyDefaultBindingDispatchesItsRegisteredBehavior() {
        QWidget owner;
        ShortcutRegistry registry(&owner);
        QString fired;
        QString released;
        registry.activated = [&](const ShortcutEntry &entry, bool release) {
            (release ? released : fired) = entry.id;
        };
        for (const auto &entry : registry.entries()) {
            registry.enabled = [id = entry.id](const ShortcutEntry &candidate) { return candidate.id == id; };
            for (const auto &sequence : entry.defaults) {
                fired.clear();
                released.clear();
                key(registry, sequence, &owner);
                QCOMPARE(fired, entry.id);
                if (entry.kind == ShortcutKind::Hold)
                    QCOMPARE(released, entry.id);
            }
        }
    }
    void textFieldsKeepTypingClipboardSelectAllAndUndo() {
        MainWindow window;
        show(window);
        auto *field = new QLineEdit(&window);
        field->show();
        field->setFocus();
        QTest::keyClicks(field, "bvxqm15");
        QCOMPARE(field->text(), "bvxqm15");
        QCOMPARE(window.currentCanvas()->tool(), "Move");
        QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(field->selectedText(), "bvxqm15");
        QVERIFY(!window.currentDocument()->hasSelection());
        QTest::keyClick(field, Qt::Key_X, Qt::ControlModifier);
        QCOMPARE(field->text(), "");
        QTest::keyClick(field, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(field->text(), "bvxqm15");
        QCOMPARE(window.currentCanvas()->tool(), "Move");
    }
    void toolsCycleAndReturnToLastFlyoutVariant() {
        MainWindow window;
        show(window);
        auto *canvas = window.currentCanvas();
        QTest::keyClick(canvas, Qt::Key_B);
        QCOMPARE(canvas->tool(), "Brush");
        QTest::keyClick(canvas, Qt::Key_B, Qt::ShiftModifier);
        QCOMPARE(canvas->tool(), "Pencil");
        QTest::keyClick(canvas, Qt::Key_V);
        QCOMPARE(canvas->tool(), "Move");
        QTest::keyClick(canvas, Qt::Key_B);
        QCOMPARE(canvas->tool(), "Pencil");
    }
    void blendOpacityFlowSizeHardnessAndUndoActuallyChangeState() {
        MainWindow window;
        show(window);
        auto *canvas = window.currentCanvas();
        auto *document = window.currentDocument();
        QTest::keyClick(canvas, Qt::Key_M, Qt::ShiftModifier | Qt::AltModifier);
        QCOMPARE(document->activeLayer()->blendMode, "Multiply");
        QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(document->activeLayer()->blendMode, "Normal");
        QTest::keyClick(canvas, Qt::Key_3);
        QTest::keyClick(canvas, Qt::Key_7);
        QCOMPARE(document->activeLayer()->opacity, .37);
        QTest::keyClick(canvas, Qt::Key_5, Qt::ShiftModifier);
        QCOMPARE(document->activeLayer()->fill, .5);
        QTest::keyClick(canvas, Qt::Key_B);
        QTest::keyClick(canvas, Qt::Key_2);
        QTest::keyClick(canvas, Qt::Key_5);
        QCOMPARE(window.findChild<QSpinBox *>("brushOpacity")->value(), 25);
        QTest::keyClick(canvas, Qt::Key_4, Qt::ShiftModifier);
        QCOMPARE(window.findChild<QSpinBox *>("flow")->value(), 40);
        int size = canvas->brushSize();
        QTest::keyClick(canvas, Qt::Key_BracketRight);
        QVERIFY(canvas->brushSize() > size);
        QTest::keyClick(canvas, Qt::Key_BracketLeft, Qt::ShiftModifier);
        QCOMPARE(window.findChild<QSpinBox *>("hardness")->value(), 55);
        QTest::keyClick(canvas, Qt::Key_Q, Qt::ShiftModifier | Qt::AltModifier);
        QCOMPARE(canvas->property("brushBlendMode").toString(), "Behind");
        QTest::keyClick(canvas, Qt::Key_R, Qt::ShiftModifier | Qt::AltModifier);
        QCOMPARE(canvas->property("brushBlendMode").toString(), "Clear");
        QTest::keyClick(canvas, Qt::Key_M, Qt::ShiftModifier | Qt::AltModifier);
        QCOMPARE(canvas->property("brushBlendMode").toString(), "Multiply");
        QCOMPARE(document->activeLayer()->blendMode, "Normal");
    }
    void remappingDisablesOldCanvasBindingAndCanAssignEveryTool() {
        MainWindow window;
        show(window);
        auto *registry = window.shortcutRegistry();
        QVERIFY(registry->setBindings("tool.brush", {QKeySequence("F7"), QKeySequence("Ctrl+K, Ctrl+B")}));
        auto *canvas = window.currentCanvas();
        QTest::keyClick(canvas, Qt::Key_B);
        QCOMPARE(canvas->tool(), "Move");
        QTest::keyClick(canvas, Qt::Key_F7);
        QCOMPARE(canvas->tool(), "Brush");
        QVERIFY(registry->setBindings("tool.magnetic-lasso", {QKeySequence("F8")}));
        QTest::keyClick(canvas, Qt::Key_F8);
        QCOMPARE(canvas->tool(), "Magnetic Lasso");
        QVERIFY(registry->setBindings("editor.swap-colors", {QKeySequence("F9")}));
        QColor previous = canvas->foreground();
        QTest::keyClick(canvas, Qt::Key_X);
        QCOMPARE(canvas->foreground(), previous);
        QTest::keyClick(canvas, Qt::Key_F9);
        QVERIFY(canvas->foreground() != previous);
    }
    void conflictsAndPrefixConflictsAreRejectedAtomically() {
        QWidget owner;
        ShortcutRegistry registry(&owner);
        QString error;
        auto previous = registry.bindingMap();
        QVERIFY(!registry.setBindings("tool.brush", {QKeySequence("V")}, &error));
        QVERIFY(error.contains("Move"));
        QCOMPARE(registry.bindingMap(), previous);
        QVERIFY(!registry.setBindings("tool.brush", {QKeySequence("Ctrl+S, B")}, &error));
        QVERIFY(error.contains("Save"));
        QVERIFY(!registry.setBindings("tool.missing", {QKeySequence("F7")}, &error));
        QVERIFY(!registry.setBindings("crop.cycle-overlay", {QKeySequence("Ctrl+S")}, &error));
        auto map = registry.bindingMap();
        map["tool.move"] = {};
        map["tool.brush"] = {QKeySequence("V")};
        QVERIFY(registry.setAllBindings(map, &error));
        QCOMPARE(registry.entry("tool.move")->bindings.size(), 0);
        QCOMPARE(registry.entry("tool.brush")->bindings.first(), QKeySequence("V"));
    }
    void multiChordHoldReleaseAndDeactivationWork() {
        QWidget owner;
        owner.show();
        ShortcutRegistry registry(&owner);
        QStringList events;
        registry.activated = [&](const ShortcutEntry &entry, bool released) {
            events.append(entry.id + (released ? ":release" : ":press"));
        };
        QVERIFY(registry.setBindings("tool.brush", {QKeySequence("Ctrl+K, Ctrl+B")}));
        key(registry, QKeySequence("Ctrl+K, Ctrl+B"), &owner);
        QCOMPARE(events.last(), "tool.brush:press");
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QVERIFY(registry.dispatch(&press, &owner));
        QEvent deactivate(QEvent::WindowDeactivate);
        QApplication::sendEvent(&owner, &deactivate);
        QCOMPARE(events.last(), "editor.temporary-hand:release");
        QKeyEvent backtab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
        QVERIFY(registry.dispatch(&backtab, &owner));
        QCOMPARE(events.last(), "editor.toggle-docks:press");
    }
    void jsonRoundTripIncludesDisabledKeysMultipleBindingsAndCustomCommands() {
        QWidget owner;
        ShortcutRegistry original(&owner), restored(&owner);
        original.addCommand("Custom panel command", "Panels");
        restored.addCommand("Custom panel command", "Panels");
        QVERIFY(original.setBindings("tool.brush", {QKeySequence("F7"), QKeySequence("F8")}));
        QVERIFY(original.setBindings("editor.swap-colors", {}));
        QVERIFY(original.setBindings(ShortcutRegistry::commandId("Custom panel command"),
                                     {QKeySequence("Ctrl+K, P")}));
        QString path = m_directory.filePath("keys.json");
        QVERIFY(original.save(path));
        QVERIFY(restored.load(path));
        QCOMPARE(restored.bindingMap(), original.bindingMap());
        QString error;
        auto before = restored.bindingMap();
        QVERIFY(!restored.fromJson(
            {{"schema", 1}, {"bindings", QJsonObject{{"tool.nonexistent", QJsonArray{"F7"}}}}}, &error));
        QCOMPARE(restored.bindingMap(), before);
        restored.reset();
        QCOMPARE(restored.entry("tool.brush")->bindings, restored.entry("tool.brush")->defaults);
    }
    void modalShortcutEditorUsesSequenceWidgetsAndLeavesBindingsUnchangedOnCancel() {
        MainWindow window;
        show(window);
        auto before = window.shortcutRegistry()->bindingMap();
        bool verified = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("keyboardShortcutDialog");
            if (dialog) {
                verified = dialog->findChildren<QKeySequenceEdit *>().size() > 200;
                dialog->reject();
            }
        });
        window.runCommand("Keyboard Shortcuts...");
        QVERIFY(verified);
        QCOMPARE(window.shortcutRegistry()->bindingMap(), before);
    }
    void cropOptionsPreviewAndContextKeysWork() {
        MainWindow window;
        show(window);
        auto *canvas = window.currentCanvas();
        QTest::keyClick(canvas, Qt::Key_C);
        QCOMPARE(canvas->tool(), "Crop");
        window.findChild<QComboBox *>("cropPreset")->setCurrentIndex(3);
        QCOMPARE(canvas->cropSettings().ratio, QSizeF(4, 5));
        QTest::keyClick(canvas, Qt::Key_X);
        QCOMPARE(canvas->cropSettings().ratio, QSizeF(5, 4));
        QTest::keyClick(canvas, Qt::Key_O);
        QVERIFY(canvas->cropSettings().overlay != "Thirds");
        canvas->setCropPreviewRect(QRectF(100, 100, 300, 300));
        QSize before = window.currentDocument()->state.size;
        QTest::keyClick(canvas, Qt::Key_Escape);
        QVERIFY(!canvas->hasCropPreview());
        QCOMPARE(window.currentDocument()->state.size, before);
    }
    void commitWithoutAnActiveOperationIsHarmless() {
        MainWindow window;
        show(window);
        auto *canvas = window.currentCanvas();
        const QSize size = window.currentDocument()->state.size;
        QTest::keyClick(canvas, Qt::Key_Return);
        QTest::keyClick(canvas, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(window.currentDocument()->state.size, size);
        QVERIFY(!canvas->hasPendingInteraction());
    }
};
QTEST_MAIN(ShortcutTests)
#include "shortcuts_tests.moc"
