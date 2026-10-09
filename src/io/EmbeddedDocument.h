#pragma once
#include <QByteArray>
namespace serika::io {
inline constexpr qsizetype MaxEmbeddedDocumentBytes = 512ll * 1024 * 1024;
// Validate the bounded native envelope without recursively opening nested objects.
bool validEmbeddedDocument(const QByteArray &bytes);
} // namespace serika::io
