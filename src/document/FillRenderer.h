#pragma once
#include <QGradient>
#include <QImage>
#include <QJsonObject>
namespace serika {
QGradientStops gradientStops(const QJsonObject &parameters, QColor start = Qt::black, QColor end = Qt::white);
QColor sampleGradient(const QGradientStops &stops, qreal position);
QImage renderGradientFill(QSize size, int depth, const QJsonObject &parameters, QColor start = Qt::black);
QImage renderPatternFill(QSize size, int depth, const QJsonObject &parameters);
QImage defaultPattern();
} // namespace serika
