#pragma once
#include "document/Document.h"
namespace serika::io {
QJsonObject layerExtras(const Layer &layer, bool retainSmartSource = false);
bool restoreLayerExtras(Layer &layer, const QJsonObject &extras);
} // namespace serika::io
