#pragma once
#include "document/Document.h"
#include "ui/canvas/CanvasView.h"
#include <QHash>
#include <QJsonArray>
#include <QMainWindow>
#include <QPointer>
#include <QSettings>
#include <QTimer>
class QTabWidget;
class QTreeWidget;
class QListWidget;
class QStackedWidget;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QLabel;
class QToolBar;
class QToolButton;
class QCheckBox;
namespace serika {
class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr);
    Document *currentDocument() const;
    CanvasView *currentCanvas() const;
    void openFile(const QString &path);
    void newDocument();
    void openDemo();
    void applyTheme(const QString &name);
    void setWorkspace(const QString &name, bool reset = false);
    QStringList dockNames() const;
    void runCommand(const QString &name);
    void saveWorkspace();

  protected:
    void closeEvent(QCloseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;

  private:
    QStackedWidget *m_center = nullptr;
    QWidget *m_home = nullptr;
    QTabWidget *m_tabs = nullptr;
    QToolBar *m_options = nullptr;
    QDockWidget *m_toolDock = nullptr;
    QWidget *m_toolWidget = nullptr;
    QHash<QString, QDockWidget *> m_docks;
    QHash<QString, QAction *> m_commands;
    QHash<QString, QToolButton *> m_tools;
    QTreeWidget *m_layers = nullptr;
    QComboBox *m_blend = nullptr;
    QSpinBox *m_layerOpacity = nullptr;
    QSpinBox *m_layerFill = nullptr;
    QListWidget *m_history = nullptr;
    QListWidget *m_actionsList = nullptr;
    QLabel *m_info = nullptr;
    QLabel *m_histogram = nullptr;
    QLabel *m_navigator = nullptr;
    QWidget *m_properties = nullptr;
    QLabel *m_fgSwatch = nullptr;
    QColor m_foreground = QColor("#E8893A");
    QColor m_background = Qt::white;
    QString m_tool = "Move";
    QString m_theme = "Dark";
    QString m_workspace = "Essentials";
    QString m_lastFilter = "Gaussian Blur";
    QJsonObject m_lastFilterParameters;
    QStringList m_recent;
    QStringList m_recorded;
    QJsonArray m_actionSteps;
    bool m_recording = false;
    bool m_refreshing = false;
    bool m_editing = false;
    bool m_twoColumns = false;
    bool m_panelsHidden = false;
    bool m_docksHidden = false;
    bool m_rulers = true;
    bool m_grid = false;
    bool m_guides = true;
    bool m_quickMask = false;
    int m_screenMode = 0;
    QTimer m_recovery;
    QTimer m_refreshTimer;
    QSettings m_settings;
    QByteArray m_essentialState;
    QHash<QString, bool> m_visibilityBeforeHide;
    QPointer<QWidget> m_draggedPage;
    void detachDocument(int index);
    void buildMenus();
    void buildHome();
    void buildTools();
    void buildOptions();
    void buildDocks();
    void buildLayers(QWidget *parent);
    QAction *command(QMenu *menu, const QString &name, const QKeySequence &shortcut = {});
    QDockWidget *dock(const QString &name, QWidget *widget, bool visible = false);
    void addDocument(Document *document);
    bool closeDocument(int index);
    bool saveDocument(bool saveAs = false, bool copy = false);
    void refresh();
    void refreshLayers();
    void refreshProperties();
    void selectTool(const QString &tool);
    void adjust(const QString &name, bool destructive = false);
    void filter(const QString &name, bool repeat = false);
    void develop(Document *document);
    void preferences();
    void transformDialog();
    void layerStyles();
    void exportAs();
    void batchDialog();
    void palette();
    void keyboardShortcuts();
    void showImportReport(Document *document);
    void addRecent(const QString &path);
    void showMessage(const QString &text);
    void renameLayer();
    void selectAndMask();
    void saveRecovery();
    void setPanelsVisible(bool toolbarAlso);
    void recordStep(const QString &command, const QString &name = {}, const QJsonObject &parameters = {});
};
} // namespace serika
