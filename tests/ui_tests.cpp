#include "ui/MainWindow.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

using namespace serika;
class UiTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_settingsDirectory;
    static void show(MainWindow &window) {
        window.resize(1440, 900);
        window.show();
        window.activateWindow();
        QTest::qWait(20);
        window.setWorkspace("Essentials", true);
        QCoreApplication::processEvents();
    }
  private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
    }
    void essentialsCreatesFourDocksAndHome() {
        MainWindow window;
        show(window);
        QVERIFY(!window.currentDocument());
        QWidget *home = window.findChild<QWidget *>("home");
        QVERIFY(home);
        QVERIFY(home->isVisible());
        for (const auto &name : QStringList{"Color", "Adjustments", "Layers", "Properties"}) {
            QVERIFY(window.dockNames().contains(name));
            QDockWidget *dock = window.findChild<QDockWidget *>("panel_" + name);
            QVERIFY(dock);
            QVERIFY(!dock->isVisible());
            QCOMPARE(window.dockWidgetArea(dock), Qt::RightDockWidgetArea);
        }
        QDockWidget *tools = window.findChild<QDockWidget *>("tools");
        QVERIFY(tools);
        QVERIFY(!tools->isVisible());
        QCOMPARE(window.dockWidgetArea(tools), Qt::LeftDockWidgetArea);
        auto *tabs = window.findChild<QTabWidget *>("documentTabs");
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 0);
        window.openDemo();
        window.setWorkspace("Essentials", true);
        QCoreApplication::processEvents();
        const QStringList order{"Color", "Adjustments", "Layers", "Properties"};
        for (const QString &name : order)
            QVERIFY(window.findChild<QDockWidget *>("panel_" + name)->isVisible());
        for (int i = 1; i < order.size(); ++i) {
            const QRect upper = window.findChild<QDockWidget *>("panel_" + order[i - 1])->geometry();
            const QRect lower = window.findChild<QDockWidget *>("panel_" + order[i])->geometry();
            QVERIFY2(upper.bottom() < lower.top(),
                     qPrintable(QString("%1 at y=%2..%3 must be above %4 at y=%5..%6")
                                    .arg(order[i - 1])
                                    .arg(upper.top())
                                    .arg(upper.bottom())
                                    .arg(order[i])
                                    .arg(lower.top())
                                    .arg(lower.bottom())));
            QVERIFY(upper != lower);
        }
    }
    void newDocumentDialogCreatesSixteenBitCanvas() {
        MainWindow window;
        show(window);
        bool accepted = false;
        QTimer::singleShot(3000, &window, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            QCOMPARE(dialog->windowTitle(), QString("New document"));
            const auto dimensions = dialog->findChildren<QSpinBox *>();
            QCOMPARE(dimensions.size(), 2);
            dimensions[0]->setValue(320);
            dimensions[1]->setValue(240);
            for (auto *combo : dialog->findChildren<QComboBox *>()) {
                const int sixteen = combo->findText("16 bit"), transparent = combo->findText("Transparent");
                if (sixteen >= 0)
                    combo->setCurrentIndex(sixteen);
                if (transparent >= 0)
                    combo->setCurrentIndex(transparent);
            }
            for (auto *edit : dialog->findChildren<QLineEdit *>())
                if (edit->text() == "Untitled-1")
                    edit->setText("UI test canvas");
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            QVERIFY(buttons);
            accepted = true;
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
        });
        window.newDocument();
        QVERIFY(accepted);
        QVERIFY(window.currentDocument());
        QVERIFY(window.currentCanvas());
        QCoreApplication::processEvents();
        Document *doc = window.currentDocument();
        QCOMPARE(doc->title, QString("UI test canvas"));
        QCOMPARE(doc->state.size, QSize(320, 240));
        QCOMPARE(doc->state.bitDepth, 16);
        QVERIFY(!doc->isModified());
        QCOMPARE(doc->composite().format(), QImage::Format_RGBA64);
        QCOMPARE(doc->composite().pixelColor(0, 0).alpha(), 0);
        QVERIFY(window.currentCanvas()->isVisible());
    }
    void homeWelcomeProjectOpensCanvas() {
        MainWindow window;
        show(window);
        window.openDemo();
        QCoreApplication::processEvents();
        QVERIFY(window.currentDocument());
        QVERIFY(window.currentCanvas());
        QVERIFY(window.currentCanvas()->isVisible());
        QCOMPARE(window.currentDocument()->state.size, QSize(1600, 1000));
        QVERIFY(window.currentDocument()->state.layers.size() >= 5);
        QVERIFY(!window.currentDocument()->isModified());
        QVERIFY(!window.findChild<QWidget *>("home")->isVisible());
        QCOMPARE(window.findChild<QTabWidget *>("documentTabs")->count(), 1);
        QCOMPARE(window.currentDocument()->composite().size(), QSize(1600, 1000));
    }
    void brushGestureChangesPixelsAndKeyboardUndo() {
        MainWindow window;
        show(window);
        window.openDemo();
        Document *doc = window.currentDocument();
        doc->resizeImage({320, 200}, Qt::FastTransformation);
        window.runCommand("New Layer");
        CanvasView *canvas = window.currentCanvas();
        QCoreApplication::processEvents();
        QVERIFY(doc->activeLayer());
        QCOMPARE(doc->activeLayer()->kind, LayerKind::Pixel);
        canvas->setShowRulers(false);
        canvas->setZoom(1);
        canvas->setTool("Brush");
        canvas->setBrushSize(20);
        canvas->setBrushHardness(1);
        canvas->setBrushOpacity(1);
        canvas->setForeground(Qt::red);
        doc->markSaved();
        const int historyBefore = doc->historyNames().size();
        const quint64 layerId = doc->activeLayer()->id;
        const QPoint start = canvas->fromDocument({140, 100}).toPoint(),
                     end = canvas->fromDocument({180, 100}).toPoint();
        QVERIFY(canvas->rect().contains(start));
        QVERIFY(canvas->rect().contains(end));
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(canvas, end, 5);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
        QCOMPARE(doc->historyNames().size(), historyBefore + 1);
        QVERIFY(doc->isModified());
        QVERIFY(doc->state.layers[doc->indexForId(layerId)]
                    .pixels.region({140, 100, 1, 1})
                    .pixelColor(0, 0)
                    .red() > 200);
        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier);
        QCoreApplication::processEvents();
        QCOMPARE(doc->historyNames().size(), historyBefore);
        QVERIFY(doc->state.layers[doc->indexForId(layerId)].pixels.empty());
        QVERIFY(!doc->isModified());
        QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QCoreApplication::processEvents();
        QCOMPARE(doc->historyNames().size(), historyBefore + 1);
        QVERIFY(!doc->state.layers[doc->indexForId(layerId)].pixels.empty());
    }
    void layerVisibilityAndReorderCommands() {
        MainWindow window;
        show(window);
        window.openDemo();
        Document *doc = window.currentDocument();
        doc->resizeImage({160, 100}, Qt::FastTransformation);
        window.runCommand("New Layer");
        const quint64 topId = doc->activeLayer()->id;
        const int original = doc->state.activeIndex;
        auto *tree = window.findChild<QTreeWidget *>("layersTree");
        QVERIFY(tree);
        QTRY_VERIFY(tree->topLevelItemCount() > 1);
        QTRY_COMPARE(tree->topLevelItem(0)->data(1, Qt::UserRole).toULongLong(), topId);
        QTreeWidgetItem *top = tree->topLevelItem(0);
        // The panel's itemChanged signal must update the document and record an undo step.
        top->setCheckState(0, Qt::Unchecked);
        QVERIFY(!doc->state.layers[doc->indexForId(topId)].visible);
        window.runCommand("Undo");
        QVERIFY(doc->state.layers[doc->indexForId(topId)].visible);
        doc->setActiveIndex(doc->indexForId(topId));
        window.runCommand("Send Backward");
        QCOMPARE(doc->indexForId(topId), original - 1);
        window.runCommand("Undo");
        QCOMPARE(doc->indexForId(topId), original);
    }
    void primaryShortcutsAndToolButtonsAreRegistered() {
        MainWindow window;
        show(window);
        for (const auto &name : QStringList{"Brush", "Move", "Rectangular Marquee", "Horizontal Type",
                                            "Eraser", "Crop", "Gradient", "Eyedropper", "Clone Stamp"})
            QVERIFY2(window.findChild<QToolButton *>("tool_" + name), qPrintable(name));
        bool undo = false, save = false, open = false;
        for (const auto *action : window.findChildren<QAction *>()) {
            if (action->text() == "Undo")
                undo = action->shortcut() == QKeySequence::Undo;
            if (action->text() == "Save")
                save = action->shortcut() == QKeySequence::Save;
            if (action->text() == "Open...")
                open = action->shortcut() == QKeySequence::Open;
        }
        QVERIFY(undo);
        QVERIFY(save);
        QVERIFY(open);
    }
};
QTEST_MAIN(UiTests)
#include "ui_tests.moc"
