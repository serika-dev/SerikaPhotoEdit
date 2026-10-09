#pragma once
#include "Document.h"
namespace serika {
bool placeSmartObject(Document *document, const QString &path, bool linked, QString *error = nullptr);
bool convertLayerToEmbeddedSmartObject(Document *document, quint64 layerId, QString *error = nullptr);
bool replaceSmartObjectContents(Document *document, quint64 layerId, const QString &path,
                                QString *error = nullptr);
bool relinkSmartObject(Document *document, quint64 layerId, const QString &path, QString *error = nullptr);
bool reloadSmartObject(Document *document, quint64 layerId, QString *error = nullptr);
bool embedSmartObject(Document *document, quint64 layerId, QString *error = nullptr);
Document *openSmartObjectContents(const Document *document, quint64 layerId, QString *error = nullptr,
                                  QObject *parent = nullptr);
bool applySmartObjectContents(Document *document, quint64 layerId, const Document *contents,
                              QString *error = nullptr);
bool saveLinkedSmartObjectContents(Document *document, quint64 layerId, Document *contents,
                                   QString *error = nullptr);
bool exportSmartObjectContents(const Document *document, quint64 layerId, const QString &path,
                               QString *error = nullptr);
QString smartObjectLinkedPath(const Document *document, const Layer &layer);
} // namespace serika
