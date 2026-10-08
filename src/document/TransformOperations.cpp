#include "TransformOperations.h"
#include <cmath>
namespace serika {
QJsonArray transformJson(const QTransform &t) {
    return {t.m11(), t.m12(), t.m13(), t.m21(), t.m22(), t.m23(), t.m31(), t.m32(), t.m33()};
}
QTransform transformFromJson(const QJsonArray &a) {
    return a.size() == 9 ? QTransform(a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble(),
                                      a[4].toDouble(), a[5].toDouble(), a[6].toDouble(), a[7].toDouble(),
                                      a[8].toDouble())
                         : QTransform();
}
bool applyLayerTransform(Document *d, const QTransform &t, QString *error) {
    auto fail = [&](const QString &s) {
        if (error)
            *error = s;
        return false;
    };
    if (!d || !d->activeLayer())
        return fail("Select a layer to transform.");
    const auto *l = d->activeLayer();
    if (l->locked || l->lockPosition)
        return fail("The layer is locked.");
    if (!t.isInvertible())
        return fail("This transform has zero area.");
    for (const auto &n : transformJson(t))
        if (!std::isfinite(n.toDouble()))
            return fail("Transform values must be finite.");
    const bool maskOnly = l->maskTarget && !l->mask.isNull();
    if (!maskOnly &&
        (l->kind == LayerKind::Group || l->kind == LayerKind::Adjustment || l->kind == LayerKind::Artboard))
        return fail("Select a pixel, text, shape or Smart Object layer to transform.");
    const auto source = maskOnly ? normalizeMask(l->mask, d->state.bitDepth) : d->layerImage(*l);
    if (source.isNull())
        return fail("The layer has no transformable content.");
    const QRectF bounds = t.mapRect(QRectF(source.rect()));
    if (!std::isfinite(bounds.width()) || !std::isfinite(bounds.height()) || bounds.width() < .5 ||
        bounds.height() < .5 || bounds.width() > 300000 || bounds.height() > 300000 ||
        bounds.width() * bounds.height() > 80000000)
        return fail("The transformed layer exceeds the supported image size.");
    const QImage result = source.transformed(t, Qt::SmoothTransformation);
    if (result.isNull())
        return fail("Could not allocate transformed pixels.");
    const QPointF displacement = bounds.toAlignedRect().topLeft();
    const QTransform local = t * QTransform::fromTranslate(-displacement.x(), -displacement.y());
    d->mutate("Transform layer", [&] {
        auto *layer = d->activeLayer();
        if (maskOnly) {
            layer->mask = normalizeMask(result, d->state.bitDepth);
            layer->maskOffset = (layer->maskLinked ? QPointF() : layer->maskOffset) + displacement;
            layer->maskLinked = false;
            return;
        }
        if (layer->kind == LayerKind::SmartObject || layer->kind == LayerKind::Text ||
            layer->kind == LayerKind::Shape) {
            const auto old = transformFromJson(layer->parameters["contentTransform"].toArray());
            Layer original = *layer;
            original.parameters.remove("contentTransform");
            const auto originalImage = d->layerImage(original);
            const auto combined = QImage::trueMatrix(old, originalImage.width(), originalImage.height()) * t;
            layer->parameters["contentTransform"] = transformJson(combined);
        } else {
            layer->pixels.setImage(result);
            layer->kind = LayerKind::Pixel;
            layer->parameters.remove("contentTransform");
            layer->smartFilters = {};
        }
        layer->offset += displacement;
        if (layer->maskLinked && !layer->mask.isNull()) {
            QImage output = makeMask(result.size(), 32);
            QPainter p(&output);
            p.setTransform(local);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(0, 0, layer->mask);
            p.end();
            layer->mask = normalizeMask(output, d->state.bitDepth);
        } else if (!layer->mask.isNull())
            layer->maskOffset -= displacement;
        if (layer->vectorMaskLinked && !layer->vectorMask.isEmpty())
            layer->vectorMask = local.map(layer->vectorMask);
        else if (!layer->vectorMask.isEmpty())
            layer->vectorMaskOffset -= displacement;
    });
    return true;
}
} // namespace serika
