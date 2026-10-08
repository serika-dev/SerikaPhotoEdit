#pragma once
#include "Document.h"
#include <QTransform>
namespace serika {
bool applyLayerTransform(Document *document, const QTransform &transform, QString *error = nullptr);
QJsonArray transformJson(const QTransform &transform);
QTransform transformFromJson(const QJsonArray &array);
} // namespace serika
