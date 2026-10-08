#pragma once
#include "io/FormatIO.h"
namespace serika::io {
Document *readNative(const QString &, QString *, QObject *);
bool writeNative(const Document *, const QString &, QString *);
Document *readPsd(const QString &, QString *, QObject *);
bool writePsd(const Document *, const QString &, QString *);
Document *readRaw(const QString &, QString *, QObject *);
QImage readTga(const QString &, QString *);
bool writeTga(const QImage &, const QString &, QString *);
QImage readHdr(const QString &, QString *);
bool writeHdr(const QImage &, const QString &, QString *);
} // namespace serika::io
