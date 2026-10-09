#include "Theme.h"
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>

namespace serika {
namespace {
QString headingFamily = QStringLiteral("Outfit");
QString bodyFamily = QStringLiteral("Onest");
struct ThemeColors {
    const char *window;
    const char *panel;
    const char *base;
    const char *text;
    const char *muted;
    const char *border;
    const char *control;
    const char *accent;
    const char *accentText;
    const char *onAccent;
    const char *soft;
};
ThemeColors colors(const QString &name) {
    if (name == "Darkest")
        return {"#0E0B15", "#181421", "#0A0A0A", "#F4F1FB", "#A098B4", "#2D273C",
                "#796C8F", "#8B5CF6", "#C4B5FD", "#0A0A0A", "#29214A"};
    if (name == "Light")
        return {"#EEEBF6", "#FFFFFF", "#FAF7FF", "#17131F", "#6F6880", "#D8D1E5",
                "#8A7A9E", "#5B21B6", "#5B21B6", "#FAFAFA", "#ECE7FF"};
    if (name == "Lightest")
        return {"#F5F3FA", "#FFFFFF", "#FFFFFF", "#17131F", "#6F6880", "#E5E1EF",
                "#8A7A9E", "#5B21B6", "#5B21B6", "#FAFAFA", "#ECE7FF"};
    return {"#181421", "#221D2E", "#0E0B15", "#F4F1FB", "#A098B4", "#2D273C",
            "#81728F", "#8B5CF6", "#C4B5FD", "#0A0A0A", "#29214A"};
}
QString loadedFamily(const QString &path, const QString &preferred) {
    const int id = QFontDatabase::addApplicationFont(path);
    if (id < 0)
        return preferred;
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (families.contains(preferred))
        return preferred;
    return families.isEmpty() ? preferred : families.first();
}
} // namespace
void initializeBrandFonts() {
    static bool initialized = false;
    if (initialized || !QGuiApplication::instance())
        return;
    initialized = true;
    headingFamily = loadedFamily(":/serika/fonts/Outfit.ttf", "Outfit");
    bodyFamily = loadedFamily(":/serika/fonts/Onest.ttf", "Onest");
}
QColor themeCanvas(const QString &name) {
    return name == "Darkest"    ? QColor("#0A0A0A")
           : name == "Light"    ? QColor("#ADADAD")
           : name == "Lightest" ? QColor("#D6D6D6")
                                : QColor("#222222");
}
QPalette themePalette(const QString &name) {
    const ThemeColors c = colors(name);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(c.window));
    palette.setColor(QPalette::WindowText, QColor(c.text));
    palette.setColor(QPalette::Base, QColor(c.base));
    palette.setColor(QPalette::AlternateBase, QColor(c.panel));
    palette.setColor(QPalette::ToolTipBase, QColor(c.panel));
    palette.setColor(QPalette::ToolTipText, QColor(c.text));
    palette.setColor(QPalette::Text, QColor(c.text));
    palette.setColor(QPalette::Button, QColor(c.panel));
    palette.setColor(QPalette::ButtonText, QColor(c.text));
    palette.setColor(QPalette::BrightText, QColor("#FAFAFA"));
    palette.setColor(QPalette::Highlight, QColor(c.accent));
    palette.setColor(QPalette::Accent, QColor(c.accent));
    palette.setColor(QPalette::HighlightedText, QColor(c.onAccent));
    palette.setColor(QPalette::Link, QColor(c.accentText));
    palette.setColor(QPalette::LinkVisited, QColor(c.accentText));
    palette.setColor(QPalette::PlaceholderText, QColor(c.muted));
    palette.setColor(QPalette::Light, QColor(c.panel).lighter(125));
    palette.setColor(QPalette::Midlight, QColor(c.soft));
    palette.setColor(QPalette::Mid, QColor(c.control));
    palette.setColor(QPalette::Dark, QColor(c.border));
    palette.setColor(QPalette::Shadow, themeCanvas(name));
    for (const auto role :
         {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::PlaceholderText})
        palette.setColor(QPalette::Disabled, role, QColor(c.muted));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(c.soft));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(c.muted));
    return palette;
}
QString themeStyle(const QString &name) {
    initializeBrandFonts();
    const QString theme = name == "Darkest" || name == "Light" || name == "Lightest" ? name : "Dark";
    QFile file(":/serika/themes/" + theme.toLower() + ".qss");
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QString style = QString::fromUtf8(file.readAll());
    style.replace("'Outfit'", "'" + headingFamily + "'");
    style.replace("'Onest'", "'" + bodyFamily + "'");
    return style;
}
} // namespace serika
