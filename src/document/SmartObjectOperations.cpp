#include "SmartObjectOperations.h"
#include "TransformOperations.h"
#include "io/EmbeddedDocument.h"
#include "io/FormatIO.h"
#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <cmath>
#include <memory>
namespace serika {
namespace {
bool fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}
bool validateAncestors(const Document *document, const Layer &layer, QString *error, bool editable) {
    if (editable && layer.locked)
        return fail(error, "The layer is locked.");
    QSet<quint64> visited{layer.id};
    quint64 parentId = layer.parentId;
    while (parentId) {
        if (visited.contains(parentId))
            return fail(error, "The layer's parent groups contain a cycle.");
        visited.insert(parentId);
        const auto index = document->indexForId(parentId);
        if (index < 0)
            return fail(error, "The layer's parent group is missing.");
        const auto &parent = document->state.layers[index];
        if (parent.kind != LayerKind::Group && parent.kind != LayerKind::Artboard)
            return fail(error, "The layer's parent is not a group or artboard.");
        if (editable && parent.locked)
            return fail(error, "An ancestor group is locked.");
        parentId = parent.parentId;
    }
    return true;
}
const Layer *target(const Document *document, quint64 id, QString *error, bool editable = true) {
    if (error)
        error->clear();
    const int index = document ? document->indexForId(id) : -1;
    if (index < 0) {
        fail(error, "The parent layer no longer exists.");
        return nullptr;
    }
    const auto &layer = document->state.layers[index];
    if (layer.kind != LayerKind::SmartObject) {
        fail(error, "Select a Smart Object layer.");
        return nullptr;
    }
    if (!validateAncestors(document, layer, error, editable)) {
        return nullptr;
    }
    return &layer;
}
bool serialize(const Document *contents, QByteArray *bytes, QString *error) {
    if (!contents)
        return fail(error, "The contents document is unavailable.");
    quint64 estimated = 4096;
    for (const auto &layer : contents->state.layers) {
        estimated += layer.embeddedDocument.size() + layer.mask.sizeInBytes() + 1024;
        for (const auto &tile : layer.pixels.tiles)
            estimated += tile.sizeInBytes() + 1024;
        if (estimated > quint64(io::MaxEmbeddedDocumentBytes))
            return fail(error, "Embedded contents exceed the 512 MB native payload limit.");
    }
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return fail(error, "Could not create temporary storage for the contents document.");
    const auto path = temporary.filePath("contents.spe");
    Document snapshot;
    snapshot.state = contents->state;
    snapshot.title = contents->title;
    snapshot.blendLinear = contents->blendLinear;
    snapshot.historyLimit = contents->historyLimit;
    if (!contents->filePath.isEmpty() &&
        !QDir::isAbsolutePath(snapshot.state.metadata["_smartObjectBaseDirectory"].toString()))
        snapshot.state.metadata["_smartObjectBaseDirectory"] = QFileInfo(contents->filePath).absolutePath();
    if (!FormatIO::saveNative(&snapshot, path, error))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, file.errorString());
    if (file.size() > io::MaxEmbeddedDocumentBytes)
        return fail(error, "Embedded contents exceed the 512 MB native payload limit.");
    *bytes = file.readAll();
    if (!io::validEmbeddedDocument(*bytes))
        return fail(error, "The contents document has an invalid native payload.");
    return true;
}
Document *deserialize(const QByteArray &bytes, QString *error, QObject *parent) {
    if (!io::validEmbeddedDocument(bytes)) {
        fail(error, "The embedded contents are malformed or exceed the 512 MB limit.");
        return nullptr;
    }
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        fail(error, "Could not create temporary storage for embedded contents.");
        return nullptr;
    }
    QFile file(temporary.filePath("contents.spe"));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        fail(error, "Could not write temporary embedded contents.");
        return nullptr;
    }
    file.close();
    auto *contents = FormatIO::openNative(file.fileName(), error, parent);
    if (contents)
        contents->filePath.clear();
    return contents;
}
std::unique_ptr<Document> retainedContents(const Document *document, const Layer &layer, QString *error) {
    if (!layer.embeddedDocument.isEmpty())
        return std::unique_ptr<Document>(deserialize(layer.embeddedDocument, error, nullptr));
    // Older documents retained only pixels. Their first contents edit starts a real subdocument.
    if (layer.pixels.size.isEmpty()) {
        fail(error, "The Smart Object has no retained source pixels.");
        return {};
    }
    auto contents = std::make_unique<Document>();
    contents->state.size = layer.pixels.size;
    contents->state.bitDepth = layer.pixels.format == QImage::Format_RGBA32FPx4 ? 32
                               : layer.pixels.format == QImage::Format_RGBA64   ? 16
                                                                                : 8;
    contents->state.iccProfile = document->state.iccProfile;
    Layer source;
    source.id = 1;
    source.name = layer.name;
    source.pixels = layer.pixels;
    contents->state.layers = {source};
    contents->title = layer.name + " Contents";
    contents->touch();
    contents->markSaved();
    return contents;
}
struct Prepared {
    TileImage pixels;
    QByteArray embedded;
    QJsonObject parameters;
};
bool prepare(const Document *document, const Layer *layer, const Document *contents, Prepared *prepared,
             QString *error) {
    if (contents == document)
        return fail(error, "The contents must be a separate document from their parent.");
    if (!contents || contents->state.size.isEmpty())
        return fail(error, "The contents document is empty.");
    const QSize size = contents->state.size;
    const int pixelBytes = contents->state.bitDepth == 32 ? 16 : contents->state.bitDepth == 16 ? 8 : 4;
    if (size.width() > 300000 || size.height() > 300000 || qint64(size.width()) * size.height() > 80000000 ||
        qint64(size.width()) * size.height() * pixelBytes > io::MaxEmbeddedDocumentBytes)
        return fail(error, "The contents exceed the supported preview size.");
    QImage image = contents->composite();
    if (image.isNull())
        return fail(error, "Could not render the contents document.");
    const auto profile = document->state.iccProfile.isEmpty()
                             ? QColorSpace()
                             : QColorSpace::fromIccProfile(document->state.iccProfile);
    if (image.colorSpace().isValid() && profile.isValid() && image.colorSpace() != profile)
        image.convertToColorSpace(profile);
    if (image.isNull())
        return fail(error, "Could not convert the contents colour profile.");
    if (!serialize(contents, &prepared->embedded, error))
        return false;
    prepared->pixels = TileImage::fromImage(image);
    prepared->parameters = layer ? layer->parameters : QJsonObject();
    if (layer && !layer->pixels.size.isEmpty() && layer->pixels.size != image.size()) {
        const auto scale = QTransform::fromScale(qreal(layer->pixels.size.width()) / image.width(),
                                                 qreal(layer->pixels.size.height()) / image.height());
        const auto existing = transformFromJson(layer->parameters["contentTransform"].toArray());
        prepared->parameters["contentTransform"] = transformJson(scale * existing);
    }
    const auto transform = transformFromJson(prepared->parameters["contentTransform"].toArray());
    const auto bounds = transform.mapRect(QRectF(QPointF(), image.size()));
    if (!transform.isInvertible() || !std::isfinite(bounds.width()) || !std::isfinite(bounds.height()) ||
        bounds.width() < .5 || bounds.height() < .5 || bounds.width() > 300000 || bounds.height() > 300000 ||
        bounds.width() * bounds.height() > 80000000)
        return fail(error, "The contents would produce an unsupported parent transform.");
    auto info = prepared->parameters["smartObject"].toObject();
    info["version"] = 1;
    info["sourceWidth"] = size.width();
    info["sourceHeight"] = size.height();
    info["sourceTitle"] = contents->title;
    prepared->parameters["smartObject"] = info;
    return true;
}
void commit(Document *document, quint64 id, const Prepared &prepared, const QString &link,
            const QString &name) {
    document->mutate(name, [&] {
        auto &layer = document->state.layers[document->indexForId(id)];
        layer.pixels = prepared.pixels;
        layer.embeddedDocument = prepared.embedded;
        layer.parameters = prepared.parameters;
        layer.linkedPath = link;
    });
}
bool loadAndReplace(Document *document, quint64 id, const QString &path, const QString &link,
                    const QString &history, QString *error) {
    const auto *layer = target(document, id, error);
    if (!layer)
        return false;
    std::unique_ptr<Document> contents(FormatIO::open(path, error));
    if (!contents)
        return false;
    Prepared prepared;
    if (!prepare(document, layer, contents.get(), &prepared, error))
        return false;
    commit(document, id, prepared, link, history);
    return true;
}
} // namespace
QString smartObjectLinkedPath(const Document *document, const Layer &layer) {
    if (layer.linkedPath.isEmpty())
        return {};
    if (QDir::isAbsolutePath(layer.linkedPath))
        return QDir::cleanPath(layer.linkedPath);
    if (document) {
        const auto base = document->state.metadata["_smartObjectBaseDirectory"].toString();
        if (!base.isEmpty() && QDir::isAbsolutePath(base))
            return QDir(base).absoluteFilePath(layer.linkedPath);
    }
    return QFileInfo(document && !document->filePath.isEmpty() ? document->filePath
                                                               : QDir::currentPath() + "/Untitled.spe")
        .absoluteDir()
        .absoluteFilePath(layer.linkedPath);
}
bool placeSmartObject(Document *document, const QString &path, bool linked, QString *error) {
    if (error)
        error->clear();
    if (!document || document->state.size.isEmpty())
        return fail(error, "Open a parent document first.");
    std::unique_ptr<Document> contents(FormatIO::open(path, error));
    if (!contents)
        return false;
    Prepared prepared;
    if (!prepare(document, nullptr, contents.get(), &prepared, error))
        return false;
    const auto sourceSize = prepared.pixels.size;
    const auto fit = sourceSize.scaled(document->state.size, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
    if (sourceSize.width() > document->state.size.width() ||
        sourceSize.height() > document->state.size.height())
        prepared.parameters["contentTransform"] = transformJson(QTransform::fromScale(
            qreal(fit.width()) / sourceSize.width(), qreal(fit.height()) / sourceSize.height()));
    document->mutate(linked ? "Place linked Smart Object" : "Place embedded Smart Object", [&] {
        document->addLayer(QFileInfo(path).fileName(), LayerKind::SmartObject);
        auto *layer = document->activeLayer();
        layer->pixels = prepared.pixels;
        layer->embeddedDocument = prepared.embedded;
        layer->parameters = prepared.parameters;
        layer->linkedPath = linked ? QFileInfo(path).absoluteFilePath() : QString();
    });
    return true;
}
bool convertLayerToEmbeddedSmartObject(Document *document, quint64 id, QString *error) {
    if (error)
        error->clear();
    const auto index = document ? document->indexForId(id) : -1;
    if (index < 0)
        return fail(error, "Select a layer to convert.");
    const auto &layer = document->state.layers[index];
    if (!validateAncestors(document, layer, error, true))
        return false;
    if (layer.kind == LayerKind::SmartObject)
        return embedSmartObject(document, id, error);
    if (layer.locked || layer.kind == LayerKind::Group || layer.kind == LayerKind::Artboard ||
        layer.kind == LayerKind::Adjustment)
        return fail(error, "Select an unlocked pixel, text, shape or fill layer.");
    Document contents;
    contents.state.size = layer.kind == LayerKind::Pixel ? layer.pixels.size : document->state.size;
    contents.state.bitDepth = document->state.bitDepth;
    contents.state.iccProfile = document->state.iccProfile;
    contents.state.resolution = document->state.resolution;
    contents.title = layer.name + " Contents";
    Layer source = layer;
    source.id = 1;
    source.parentId = 0;
    source.offset = {};
    source.opacity = source.fill = 1;
    source.blendMode = "Normal";
    source.clipped = source.locked = source.lockAlpha = source.lockPosition = false;
    source.visible = true;
    source.mask = {};
    source.maskTarget = false;
    source.vectorMask = {};
    source.effects = {};
    source.parameters.remove("contentTransform");
    source.linkedPath.clear();
    source.embeddedDocument.clear();
    source.smartFilters = {};
    contents.state.layers = {source};
    Prepared prepared;
    if (!prepare(document, nullptr, &contents, &prepared, error))
        return false;
    prepared.parameters = layer.parameters;
    document->mutate("Convert to embedded Smart Object", [&] {
        auto &updated = document->state.layers[index];
        updated.kind = LayerKind::SmartObject;
        updated.pixels = prepared.pixels;
        updated.embeddedDocument = prepared.embedded;
        updated.linkedPath.clear();
        updated.smartFilters = {};
    });
    return true;
}
bool replaceSmartObjectContents(Document *document, quint64 id, const QString &path, QString *error) {
    const auto *layer = target(document, id, error);
    if (!layer)
        return false;
    const auto link = layer->linkedPath.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
    return loadAndReplace(document, id, path, link, "Replace Smart Object contents", error);
}
bool relinkSmartObject(Document *document, quint64 id, const QString &path, QString *error) {
    return loadAndReplace(document, id, path, QFileInfo(path).absoluteFilePath(), "Relink Smart Object",
                          error);
}
bool reloadSmartObject(Document *document, quint64 id, QString *error) {
    const auto *layer = target(document, id, error);
    if (!layer)
        return false;
    if (layer->linkedPath.isEmpty())
        return fail(error, "The Smart Object has no linked original.");
    return loadAndReplace(document, id, smartObjectLinkedPath(document, *layer), layer->linkedPath,
                          "Reload linked Smart Object", error);
}
bool embedSmartObject(Document *document, quint64 id, QString *error) {
    const auto *layer = target(document, id, error);
    if (!layer)
        return false;
    auto contents = retainedContents(document, *layer, error);
    if (!contents)
        return false;
    QByteArray bytes;
    if (!serialize(contents.get(), &bytes, error))
        return false;
    document->mutate("Embed Smart Object", [&] {
        auto &updated = document->state.layers[document->indexForId(id)];
        updated.embeddedDocument = bytes;
        updated.linkedPath.clear();
    });
    return true;
}
Document *openSmartObjectContents(const Document *document, quint64 id, QString *error, QObject *parent) {
    const auto *layer = target(document, id, error, false);
    if (!layer)
        return nullptr;
    if (!layer->linkedPath.isEmpty()) {
        const auto path = smartObjectLinkedPath(document, *layer);
        auto *contents = FormatIO::open(path, error, parent);
        if (contents &&
            !QDir::isAbsolutePath(contents->state.metadata["_smartObjectBaseDirectory"].toString()))
            contents->state.metadata["_smartObjectBaseDirectory"] = QFileInfo(path).absolutePath();
        return contents;
    }
    auto contents = retainedContents(document, *layer, error);
    if (!contents)
        return nullptr;
    contents->setParent(parent);
    return contents.release();
}
bool applySmartObjectContents(Document *document, quint64 id, const Document *contents, QString *error) {
    const auto *layer = target(document, id, error);
    if (!layer)
        return false;
    Prepared prepared;
    if (!prepare(document, layer, contents, &prepared, error))
        return false;
    commit(document, id, prepared, layer->linkedPath, "Save Smart Object contents");
    return true;
}
bool saveLinkedSmartObjectContents(Document *document, quint64 id, Document *contents, QString *error) {
    const auto *layer = target(document, id, error);
    if (!layer)
        return false;
    if (layer->linkedPath.isEmpty())
        return fail(error, "The Smart Object has no linked save target.");
    const auto link = layer->linkedPath;
    const auto path = smartObjectLinkedPath(document, *layer);
    if (!FormatIO::writableFormats().contains(QFileInfo(path).suffix().toLower()))
        return fail(error, "The linked original's format is read-only. Embed it or relink to a writable file "
                           "before saving.");
    Prepared prepared;
    if (!prepare(document, layer, contents, &prepared, error))
        return false;
    if (!FormatIO::save(contents, path, error))
        return false;
    commit(document, id, prepared, link, "Save linked Smart Object contents");
    return true;
}
bool exportSmartObjectContents(const Document *document, quint64 id, const QString &path, QString *error) {
    const auto *layer = target(document, id, error, false);
    if (!layer)
        return false;
    auto contents = retainedContents(document, *layer, error);
    return contents && FormatIO::save(contents.get(), path, error);
}
} // namespace serika
