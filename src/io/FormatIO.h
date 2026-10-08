#pragma once
#include "document/Document.h"
#include <QStringList>
namespace serika {
class FormatIO {
  public:
    static Document *open(const QString &path, QString *error = nullptr, QObject *parent = nullptr);
    static bool save(Document *doc, const QString &path, QString *error = nullptr, int quality = 92);
    static bool saveNative(const Document *doc, const QString &path, QString *error = nullptr);
    static Document *openNative(const QString &path, QString *error = nullptr, QObject *parent = nullptr);
    static bool savePsd(const Document *doc, const QString &path, QString *error = nullptr);
    static Document *openPsd(const QString &path, QString *error = nullptr, QObject *parent = nullptr);
    static bool isRaw(const QString &path);
    static QString openFilter();
    static QString saveFilter();
    static QStringList readableFormats();
    static QStringList writableFormats();
    static bool rawAvailable();
    static Document *openPdf(const QString &path, qreal dpi = 150, QString *error = nullptr,
                             QObject *parent = nullptr);
};
QImage applyFilter(const QImage &source, const QString &name, const QJsonObject &parameters = {});
QImage developImage(const QImage &source, const QJsonObject &parameters, int bitDepth = 16);
} // namespace serika
