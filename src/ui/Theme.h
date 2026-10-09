#pragma once
#include <QColor>
#include <QPalette>
#include <QString>
namespace serika {
void initializeBrandFonts();
QPalette themePalette(const QString &name);
QString themeStyle(const QString &name);
QColor themeCanvas(const QString &name);
} // namespace serika
