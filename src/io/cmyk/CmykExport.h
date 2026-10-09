#pragma once
#include "document/ColorManagement.h"

namespace serika {
// Classic uncompressed TIFF: real separated CMYK samples and embedded output ICC.
bool writeCmykTiff(const QString &path, const CmykImage &image, qreal dpi = 300, QString *error = nullptr);
// Creates a new directory containing C/M/Y/K grayscale TIFFs, the ICC, and manifest.
// White is no ink; black is full ink. Fails if the destination directory exists.
bool writeCmykSeparations(const QString &directory, const CmykImage &image, qreal dpi = 300,
                          QString *error = nullptr);
} // namespace serika
