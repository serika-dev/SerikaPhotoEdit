#include "Theme.h"
namespace serika {
QColor themeCanvas(const QString &name) {
    return name == "Darkest"    ? QColor("#141414")
           : name == "Light"    ? QColor("#A0A0A0")
           : name == "Lightest" ? QColor("#D6D6D6")
                                : QColor("#262626");
}
QString themeStyle(const QString &name) {
    const bool light = name == "Light" || name == "Lightest";
    QString bg = name == "Darkest"    ? "#1E1E1E"
                 : name == "Light"    ? "#B8B8B8"
                 : name == "Lightest" ? "#F0F0F0"
                                      : "#323232";
    QString panel = name == "Darkest"    ? "#262626"
                    : name == "Light"    ? "#C8C8C8"
                    : name == "Lightest" ? "#F7F7F7"
                                         : "#3A3A3A";
    QString text = light ? "#1A1A1A" : "#E8E8E8";
    QString dim = light ? "#555555" : "#B0B0B0";
    QString border = name == "Darkest"    ? "#111111"
                     : name == "Light"    ? "#8E8E8E"
                     : name == "Lightest" ? "#C4C4C4"
                                          : "#1E1E1E";
    QString input = light ? "#E2E2E2" : "#2A2A2A";
    QString qss = R"(
QWidget { background: $bg; color: $text; font-family: 'Segoe UI', 'DejaVu Sans', sans-serif; font-size: 11px; }
QMainWindow::separator { width: 3px; height: 3px; background: $border; }
QMenuBar { background: $bg; padding: 0 2px; min-height: 22px; }
QMenuBar::item { padding: 3px 7px; background: transparent; }
QMenuBar::item:selected, QMenu::item:selected { background: #E8893A; color: #171717; }
QMenu { border: 1px solid $border; padding: 4px; background: $panel; }
QMenu::item { padding: 5px 28px 5px 20px; }
QMenu::separator { height: 1px; background: $border; margin: 4px; }
QToolBar { background: $panel; border: 0; border-bottom: 1px solid $border; spacing: 6px; padding: 2px 5px; }
QDockWidget { titlebar-close-icon: none; }
QDockWidget::title { background: $panel; border-bottom: 1px solid $border; padding: 6px 8px; font-weight: 600; }
QTabWidget::pane { border: 0; }
QTabBar::tab { background: $bg; color: $dim; padding: 6px 12px; border-right: 1px solid $border; min-height: 14px; }
QTabBar::tab:selected { background: $panel; color: $text; }
QTabBar::close-button { margin-left: 7px; }
QPushButton, QToolButton { background: $panel; border: 1px solid $border; border-radius: 4px; padding: 3px 8px; min-height: 16px; }
QPushButton:hover, QToolButton:hover { border-color: #E8893A; }
QPushButton:pressed, QToolButton:checked { background: #805537; border-color: #E8893A; }
QPushButton:disabled { color: $dim; }
QPushButton#primary { background: #E8893A; color: #20160E; border: 1px solid #E8893A; font-weight: 600; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QTextEdit, QPlainTextEdit { background: $input; border: 1px solid $border; border-radius: 4px; padding: 3px 5px; selection-background-color: #805537; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #E8893A; }
QComboBox::drop-down { border: 0; width: 16px; }
QComboBox QAbstractItemView { background: $panel; selection-background-color: #805537; }
QTreeWidget, QListWidget, QTableWidget { background: $panel; border: 0; outline: 0; }
QTreeWidget::item { padding: 5px 1px; border-bottom: 1px solid $border; }
QTreeWidget::item:selected, QListWidget::item:selected { background: #70513B; }
QListWidget::item { padding: 5px; }
QHeaderView::section { background: $panel; color: $dim; border: 0; padding: 3px; }
QScrollBar:vertical { background: $bg; width: 10px; margin: 0; }
QScrollBar:horizontal { background: $bg; height: 10px; margin: 0; }
QScrollBar::handle { background: $dim; border: 2px solid $bg; border-radius: 4px; min-height: 20px; min-width: 20px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QSlider::groove:horizontal { height: 2px; background: $border; }
QSlider::sub-page:horizontal { background: #E8893A; }
QSlider::handle:horizontal { width: 10px; margin: -5px 0; background: #E8893A; border-radius: 5px; }
QCheckBox { spacing: 6px; background: transparent; }
QCheckBox::indicator { width: 12px; height: 12px; background: $input; border: 1px solid $border; border-radius: 2px; }
QCheckBox::indicator:checked { background: #E8893A; }
QGroupBox { border: 1px solid $border; margin-top: 12px; padding-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
QLabel { background: transparent; }
QLabel#muted { color: $dim; }
QLabel#eyebrow { color: #E8893A; font-size: 11px; font-weight: 600; }
QLabel#homeTitle { font-size: 32px; font-weight: 300; }
QLabel#sectionTitle { font-size: 17px; font-weight: 600; }
QFrame#card { background: $panel; border: 1px solid $border; border-radius: 8px; }
QFrame#contextBar { background: $panel; border: 1px solid #555555; border-radius: 6px; }
QToolTip { color: $text; background: $panel; border: 1px solid #E8893A; padding: 5px; }
QStatusBar { background: $bg; border-top: 1px solid $border; }
)";
    qss.replace("$bg", bg)
        .replace("$panel", panel)
        .replace("$text", text)
        .replace("$dim", dim)
        .replace("$border", border)
        .replace("$input", input);
    return qss;
}
} // namespace serika
