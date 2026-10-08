#pragma once
#include <QImage>
#include <QRect>

namespace serika {
QRect transparentContentBounds(const QImage &image, int alphaThreshold = 0);
QRect uniformBorderContentBounds(const QImage &image, qreal tolerance = 0.04);
qreal estimateStraightenAngle(const QImage &image);
QRectF largestInscribedRotatedRectangle(QSize size, qreal angle);
} // namespace serika
