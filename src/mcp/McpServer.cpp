#include "McpServer.h"
#include "actions/ActionRunner.h"
#include "document/LayerOperations.h"
#include "document/TransformOperations.h"
#include "io/FormatIO.h"
#include <QBuffer>
#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QUuid>
#include <cmath>
#include <cstdio>
#include <limits>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif
namespace serika {
namespace {
constexpr qint64 MaxPixels = 16'000'000;
constexpr int MaxDocuments = 8;
QJsonObject rpcError(QJsonValue id, int code, const QString &message) {
    return {{"jsonrpc", "2.0"},
            {"id", id.isUndefined() ? QJsonValue(QJsonValue::Null) : id},
            {"error", QJsonObject{{"code", code}, {"message", message}}}};
}
QJsonObject textResult(const QJsonObject &value, bool error = false) {
    return {{"content", QJsonArray{QJsonObject{{"type", "text"},
                                               {"text", QString::fromUtf8(QJsonDocument(value).toJson(
                                                            QJsonDocument::Compact))}}}},
            {"structuredContent", value},
            {"isError", error}};
}
QJsonObject failure(const QString &message) { return textResult({{"error", message}}, true); }
QJsonObject field(const QString &type, const QString &description = {}) {
    QJsonObject result{{"type", type}};
    if (!description.isEmpty())
        result["description"] = description;
    return result;
}
QJsonObject integer(int minimum, int maximum) {
    return {{"type", "integer"}, {"minimum", minimum}, {"maximum", maximum}};
}
QJsonObject tool(const QString &name, const QString &description, QJsonObject properties = {},
                 QJsonArray required = {}, bool readOnly = false, bool destructive = false) {
    return {{"name", name},
            {"description", description},
            {"inputSchema", QJsonObject{{"type", "object"},
                                        {"properties", properties},
                                        {"required", required},
                                        {"additionalProperties", false}}},
            {"annotations", QJsonObject{{"readOnlyHint", readOnly},
                                        {"destructiveHint", destructive},
                                        {"idempotentHint", readOnly},
                                        {"openWorldHint", false}}}};
}
QString kindName(LayerKind kind) {
    switch (kind) {
    case LayerKind::Pixel:
        return "pixel";
    case LayerKind::Group:
        return "group";
    case LayerKind::Text:
        return "text";
    case LayerKind::Shape:
        return "shape";
    case LayerKind::Adjustment:
        return "adjustment";
    case LayerKind::SolidFill:
        return "solid-fill";
    case LayerKind::GradientFill:
        return "gradient-fill";
    case LayerKind::PatternFill:
        return "pattern-fill";
    case LayerKind::SmartObject:
        return "smart-object";
    case LayerKind::Artboard:
        return "artboard";
    }
    return "unknown";
}
bool validate(const QJsonObject &arguments, const QJsonObject &schema, QString *error) {
    const auto properties = schema["properties"].toObject();
    for (const auto &required : schema["required"].toArray())
        if (!arguments.contains(required.toString())) {
            *error = "Missing argument: " + required.toString();
            return false;
        }
    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
        if (!properties.contains(it.key())) {
            *error = "Unknown argument: " + it.key();
            return false;
        }
        const auto rule = properties[it.key()].toObject();
        const auto type = rule["type"].toString();
        const auto value = it.value();
        const bool correct = type == "string"    ? value.isString()
                             : type == "boolean" ? value.isBool()
                             : type == "object"  ? value.isObject()
                             : type == "array"   ? value.isArray()
                                                 : value.isDouble();
        if (!correct) {
            *error = "Wrong argument type: " + it.key();
            return false;
        }
        if (type == "integer" &&
            (!std::isfinite(value.toDouble()) || std::floor(value.toDouble()) != value.toDouble())) {
            *error = "Expected an integer: " + it.key();
            return false;
        }
        if (value.isDouble() &&
            (!std::isfinite(value.toDouble()) ||
             (rule.contains("minimum") && value.toDouble() < rule["minimum"].toDouble()) ||
             (rule.contains("maximum") && value.toDouble() > rule["maximum"].toDouble()))) {
            *error = "Argument outside its supported range: " + it.key();
            return false;
        }
        if (value.isString() && value.toString().size() > 100000) {
            *error = "Text argument is too large.";
            return false;
        }
        if (rule.contains("enum") && !rule["enum"].toArray().contains(value)) {
            *error = "Unsupported value: " + it.key();
            return false;
        }
    }
    return true;
}
bool boundedRaster(QSize size, QString *error) {
    if (size.isEmpty() || size.width() > 30000 || size.height() > 30000 ||
        qint64(size.width()) * size.height() > MaxPixels) {
        *error = "MCP raster bounds are limited to 16 million pixels and 30,000 pixels per dimension.";
        return false;
    }
    return true;
}
bool transformedSize(QSize source, const QTransform &transform, QSize *output, QString *error) {
    if (!boundedRaster(source, error))
        return false;
    for (const auto &value : transformJson(transform))
        if (!std::isfinite(value.toDouble())) {
            *error = "Transform values must be finite.";
            return false;
        }
    if (!transform.isInvertible()) {
        *error = "This transform has zero area.";
        return false;
    }
    const QPointF corners[]{{0, 0},
                            {qreal(source.width()), 0},
                            {qreal(source.width()), qreal(source.height())},
                            {0, qreal(source.height())}};
    int denominatorSign = 0;
    for (const auto &corner : corners) {
        const double denominator =
            transform.m13() * corner.x() + transform.m23() * corner.y() + transform.m33();
        if (!std::isfinite(denominator) || std::abs(denominator) < 1e-9 ||
            (denominatorSign && denominatorSign != (denominator > 0 ? 1 : -1))) {
            *error = "Perspective transform crosses an unbounded projection.";
            return false;
        }
        denominatorSign = denominator > 0 ? 1 : -1;
    }
    const QRectF bounds = transform.mapRect(QRectF(QPointF(), source));
    constexpr double coordinateLimit = std::numeric_limits<int>::max() / 4.;
    for (double coordinate : {bounds.left(), bounds.top(), bounds.right(), bounds.bottom()})
        if (!std::isfinite(coordinate) || std::abs(coordinate) > coordinateLimit) {
            *error = "Transform coordinates exceed supported raster bounds.";
            return false;
        }
    const double width = std::ceil(bounds.right()) - std::floor(bounds.left()),
                 height = std::ceil(bounds.bottom()) - std::floor(bounds.top());
    if (width < 1 || height < 1 || width > 30000 || height > 30000 || width * height > MaxPixels) {
        *error = "Transform exceeds MCP raster limits before allocation.";
        return false;
    }
    *output = QSize(int(width), int(height));
    return true;
}
bool renderedSize(const Document *document, const Layer &layer, QSize *output, QString *error) {
    QSize size = layer.kind == LayerKind::Pixel || layer.kind == LayerKind::SmartObject
                     ? layer.pixels.size
                     : document->state.size;
    if (size.isEmpty()) {
        *output = {};
        return true;
    }
    if (!boundedRaster(size, error))
        return false;
    if (layer.parameters.contains("contentTransform")) {
        const auto matrix = layer.parameters["contentTransform"].toArray();
        if (matrix.size() != 9) {
            *error = "A content transform requires nine numeric matrix values.";
            return false;
        }
        for (const auto &value : matrix)
            if (!value.isDouble()) {
                *error = "A content transform requires nine numeric matrix values.";
                return false;
            }
        if (!transformedSize(size, transformFromJson(matrix), &size, error))
            return false;
    }
    *output = size;
    return true;
}
bool validDocument(const Document *document, QString *error) {
    const auto size = document->state.size;
    if (size.isEmpty() || size.width() > 30000 || size.height() > 30000 ||
        qint64(size.width()) * size.height() > MaxPixels) {
        *error = "MCP documents are limited to 16 million pixels and 30,000 pixels per dimension.";
        return false;
    }
    if (document->state.layers.size() > 256) {
        *error = "MCP documents are limited to 256 layers.";
        return false;
    }
    quint64 total = 0;
    for (const auto &layer : document->state.layers) {
        QSize renderSize;
        if (!renderedSize(document, layer, &renderSize, error) ||
            (!layer.mask.isNull() && !boundedRaster(layer.mask.size(), error)))
            return false;
        total += layer.embeddedDocument.size() + layer.mask.sizeInBytes();
        for (const auto &tile : layer.pixels.tiles)
            total += tile.sizeInBytes();
        if (total > 512ULL * 1024 * 1024) {
            *error = "MCP document storage exceeds 512 MB.";
            return false;
        }
    }
    return true;
}
bool validActionAllocation(const Document *document, const QJsonObject &step, QString *error) {
    const QString key = step.value("command").toString(step.value("type").toString()).trimmed().toLower();
    auto parameters = step["parameters"].toObject();
    if (parameters.isEmpty())
        parameters = step["params"].toObject();
    if (key == "resize" || key == "image size" || key == "image size..." || key == "resize canvas" ||
        key == "canvas size...") {
        const double width = parameters["width"].toDouble(document->state.size.width()),
                     height = parameters["height"].toDouble(document->state.size.height());
        if (!std::isfinite(width) || !std::isfinite(height) || width != std::floor(width) ||
            height != std::floor(height) || width < 1 || height < 1 || width > 30000 || height > 30000 ||
            width * height > MaxPixels) {
            *error = "Resize exceeds MCP document limits before allocation.";
            return false;
        }
    }
    if (key == "transform" || key == "flip horizontal" || key == "flip vertical" ||
        key == "rotate 90° clockwise" || key == "rotate 90° counterclockwise" || key == "rotate 180°") {
        const auto *layer = document->activeLayer();
        if (!layer)
            return true; // ActionRunner reports the missing-layer error.
        QSize source;
        if (layer->maskTarget && !layer->mask.isNull())
            source = layer->mask.size();
        else if (!renderedSize(document, *layer, &source, error))
            return false;
        QTransform transform;
        if (parameters.contains("matrix")) {
            const auto matrix = parameters["matrix"].toArray();
            if (matrix.size() != 9) {
                *error = "Transform requires nine numeric matrix values.";
                return false;
            }
            for (const auto &value : matrix)
                if (!value.isDouble()) {
                    *error = "Transform requires nine numeric matrix values.";
                    return false;
                }
            transform = transformFromJson(matrix);
        } else {
            const double scale = parameters["scale"].toDouble(100) / 100.;
            double sx = parameters["scaleX"].toDouble(scale), sy = parameters["scaleY"].toDouble(scale),
                   rotation = parameters["rotation"].toDouble(parameters["angle"].toDouble());
            if (key == "flip horizontal")
                sx = -1;
            if (key == "flip vertical")
                sy = -1;
            if (key == "rotate 90° clockwise")
                rotation = 90;
            if (key == "rotate 90° counterclockwise")
                rotation = -90;
            if (key == "rotate 180°")
                rotation = 180;
            if (!std::isfinite(rotation) || !std::isfinite(sx) || !std::isfinite(sy) || std::abs(sx) < .001 ||
                std::abs(sy) < .001 || std::abs(sx) > 100 || std::abs(sy) > 100) {
                *error = "Invalid transform scale or angle.";
                return false;
            }
            transform.rotate(rotation);
            transform.scale(sx, sy);
            transform.shear(parameters["shearX"].toDouble(), parameters["shearY"].toDouble());
            transform *= QTransform::fromTranslate(parameters["x"].toDouble(), parameters["y"].toDouble());
        }
        QSize output;
        if (!transformedSize(source, transform, &output, error))
            return false;
    }
    return true;
}
QString editLock(const Document *document, quint64 id) {
    QSet<quint64> visited;
    while (id) {
        if (visited.contains(id))
            return "The layer hierarchy contains a cycle.";
        visited.insert(id);
        const int index = document->indexForId(id);
        if (index < 0)
            return "The layer hierarchy is invalid.";
        const auto &layer = document->state.layers[index];
        if (layer.locked)
            return "The layer or a parent group is locked.";
        id = layer.parentId;
    }
    return {};
}
} // namespace
McpServer::McpServer(const QString &workspace) {
    const QFileInfo info(workspace);
    if (workspace.trimmed().isEmpty() || !info.isDir() || info.canonicalFilePath().isEmpty())
        m_startupError = "--mcp-root must name an existing workspace directory.";
    else
        m_root = info.canonicalFilePath();
}
QJsonArray McpServer::toolDefinitions() {
    const auto doc = field("string", "Session document ID returned by create_document or open_document.");
    const auto path = field("string", "Path inside the configured MCP workspace, relative or absolute.");
    const auto layer = field("string", "Stable layer ID from inspect_document.");
    const QJsonObject number{{"type", "number"}, {"minimum", -30000}, {"maximum", 30000}};
    return {
        tool("server_status", "Report capabilities, limits and the authorized workspace.", {}, {}, true),
        tool("list_documents", "List documents owned by this MCP session.", {}, {}, true),
        tool("create_document", "Create a native RGB document. No file is written until save_document.",
             {{"width", integer(1, 30000)},
              {"height", integer(1, 30000)},
              {"bit_depth", QJsonObject{{"type", "integer"}, {"enum", QJsonArray{8, 16, 32}}}},
              {"background", field("string", "Qt/CSS hex color, including #AARRGGBB; default transparent.")},
              {"title", field("string")}},
             {"width", "height"}),
        tool("open_document",
             "Open an image/native document inside the workspace into a new isolated session document.",
             {{"path", path}}, {"path"}),
        tool("inspect_document",
             "Read dimensions, layer IDs/properties, precision, selection and undo state.",
             {{"document_id", doc}}, {"document_id"}, true),
        tool("preview_document", "Render a bounded PNG preview as MCP image content without writing a file.",
             {{"document_id", doc}, {"max_dimension", integer(32, 2048)}}, {"document_id"}, true),
        tool("save_document",
             "Save SPE/SPEB/PSD/PSB or export a supported raster format inside the workspace. Existing files "
             "require overwrite=true.",
             {{"document_id", doc}, {"path", path}, {"overwrite", field("boolean")}}, {"document_id", "path"},
             false, true),
        tool("close_document", "Release a session document. Unsaved edits require discard=true.",
             {{"document_id", doc}, {"discard", field("boolean")}}, {"document_id"}, false, true),
        tool("select_layer", "Select a stable layer ID for subsequent actions.",
             {{"document_id", doc}, {"layer_id", layer}}, {"document_id", "layer_id"}),
        tool("add_layer",
             "Add an editable pixel, group, solid-fill, gradient-fill or pattern-fill layer. Parameters use "
             "native fill settings.",
             {{"document_id", doc},
              {"name", field("string")},
              {"kind", QJsonObject{{"type", "string"},
                                   {"enum", QJsonArray{"pixel", "group", "solid-fill", "gradient-fill",
                                                       "pattern-fill"}}}},
              {"color", field("string")},
              {"parameters", field("object")}},
             {"document_id", "kind"}),
        tool("create_text_layer",
             "Create editable text with native font, tracking, color and position. Rich typography "
             "parameters are retained by the native model.",
             {{"document_id", doc},
              {"text", field("string")},
              {"font_family", field("string")},
              {"font_size", integer(1, 2000)},
              {"bold", field("boolean")},
              {"italic", field("boolean")},
              {"color", field("string")},
              {"x", number},
              {"y", number},
              {"tracking", QJsonObject{{"type", "number"}, {"minimum", -100}, {"maximum", 1000}}},
              {"parameters", field("object")}},
             {"document_id", "text"}),
        tool("set_layer_properties",
             "Set selected layer appearance in one undo step. Honors layer/ancestor locks.",
             {{"document_id", doc},
              {"layer_id", layer},
              {"name", field("string")},
              {"opacity", QJsonObject{{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
              {"visible", field("boolean")},
              {"blend_mode",
               QJsonObject{{"type", "string"}, {"enum", QJsonArray::fromStringList(blendModeNames())}}}},
             {"document_id", "layer_id"}),
        tool("run_actions",
             "Run 1–100 native action steps atomically. Supports adjustments, filters, resize, fill, "
             "selection, transforms and other documented ActionRunner commands. File operations and scripts "
             "are not accepted.",
             {{"document_id", doc},
              {"steps", QJsonObject{{"type", "array"},
                                    {"items", QJsonObject{{"type", "object"}}},
                                    {"minItems", 1},
                                    {"maxItems", 100}}}},
             {"document_id", "steps"}),
        tool("undo", "Undo one edit in the session document.", {{"document_id", doc}}, {"document_id"}),
        tool("redo", "Redo one edit in the session document.", {{"document_id", doc}}, {"document_id"})};
}
QString McpServer::checkedPath(const QString &path, bool write, QString *error) const {
    if (path.trimmed().isEmpty() || path.contains(QChar(0))) {
        *error = "A valid workspace path is required.";
        return {};
    }
    const auto absolute = QDir::cleanPath(QDir(m_root).absoluteFilePath(path));
    const QFileInfo info(absolute);
    const auto resolved = info.exists()
                              ? info.canonicalFilePath()
                              : QFileInfo(info.absolutePath()).canonicalFilePath() + "/" + info.fileName();
    const auto relative = QDir(m_root).relativeFilePath(resolved);
    if (resolved.isEmpty() || relative == ".." || relative.startsWith("../") ||
        QDir::isAbsolutePath(relative)) {
        *error = "The path is outside the configured MCP workspace.";
        return {};
    }
    if (write) {
        if (!QFileInfo(info.absolutePath()).isDir() || info.isDir() || (info.isSymLink() && !info.exists())) {
            *error = "Output requires an existing workspace folder and a regular filename.";
            return {};
        }
    } else if (!info.isFile()) {
        *error = "The input file does not exist.";
        return {};
    }
    return info.exists() ? resolved : absolute;
}
QJsonObject McpServer::describe(const QString &id, const Document *document) const {
    QJsonArray layers;
    for (const auto &layer : document->state.layers) {
        QJsonObject item{{"id", QString::number(layer.id)},
                         {"parent_id", QString::number(layer.parentId)},
                         {"name", layer.name},
                         {"kind", kindName(layer.kind)},
                         {"visible", layer.visible},
                         {"locked", layer.locked},
                         {"opacity", layer.opacity},
                         {"blend_mode", layer.blendMode},
                         {"x", layer.offset.x()},
                         {"y", layer.offset.y()},
                         {"has_mask", !layer.mask.isNull()},
                         {"has_vector_mask", !layer.vectorMask.isEmpty()}};
        item["parameters"] = layer.parameters;
        if (layer.kind == LayerKind::Text)
            item["text"] = layer.text;
        layers.append(item);
    }
    return {{"document_id", id},
            {"title", document->title},
            {"width", document->state.size.width()},
            {"height", document->state.size.height()},
            {"bit_depth", document->state.bitDepth},
            {"color_mode", document->state.colorMode},
            {"modified", document->isModified()},
            {"can_undo", document->canUndo()},
            {"can_redo", document->canRedo()},
            {"active_layer_id",
             document->activeLayer() ? QString::number(document->activeLayer()->id) : QString()},
            {"has_selection", document->hasSelection()},
            {"layers", layers}};
}
QString McpServer::retain(std::unique_ptr<Document> document) {
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    document->historyLimit = 20;
    m_documents.emplace(id, std::move(document));
    return id;
}
QJsonObject McpServer::call(const QString &name, const QJsonObject &args) {
    if (name == "server_status")
        return textResult({{"name", "Serika PhotoEdit"},
                           {"transport", "stdio"},
                           {"workspace", m_root},
                           {"max_documents", MaxDocuments},
                           {"max_pixels_per_document", MaxPixels},
                           {"max_layers", 256},
                           {"session", "Isolated in-memory documents; GUI windows are not controlled."},
                           {"adjustments", QJsonArray::fromStringList(adjustmentNames())}});
    if (name == "list_documents") {
        QJsonArray documents;
        for (const auto &[id, document] : m_documents)
            documents.append(describe(id, document.get()));
        return textResult({{"documents", documents}});
    }
    QString error;
    if (name == "create_document" || name == "open_document") {
        if (m_documents.size() >= MaxDocuments)
            return failure("Close a document before opening another (limit 8).");
        std::unique_ptr<Document> document;
        if (name == "create_document") {
            const auto width = args["width"].toInt(), height = args["height"].toInt();
            if (qint64(width) * height > MaxPixels)
                return failure("MCP document limit is 16 million pixels.");
            const QColor color(args["background"].toString("transparent"));
            if (!color.isValid())
                return failure("Invalid background color.");
            document.reset(Document::create({width, height}, color, args["bit_depth"].toInt(8)));
            if (!document)
                return failure("Could not allocate the document.");
            document->title = args["title"].toString("Untitled");
        } else {
            const auto path = checkedPath(args["path"].toString(), false, &error);
            if (path.isEmpty())
                return failure(error);
            if (QFileInfo(path).size() > 512LL * 1024 * 1024)
                return failure("MCP input files must be at most 512 MB.");
            // Qt SVG can load external raster references while querying size. Reject it before
            // invoking that decoder, including content-sniffed files with a misleading suffix.
            const auto suffix = QFileInfo(path).suffix().toLower();
            QFile probe(path);
            if (!probe.open(QIODevice::ReadOnly))
                return failure("Could not read the input file.");
            const auto signature = probe.read(2);
            probe.close();
            if (suffix == "svg" || suffix == "svgz" || signature == QByteArray::fromHex("1f8b"))
                return failure("SVG and SVGZ input is disabled in MCP because it can reference files "
                               "outside the workspace.");
            QImageReader reader(path);
            reader.setDecideFormatFromContent(true);
            const auto format = reader.format().toLower();
            if (format == "svg" || format == "svgz")
                return failure("SVG and SVGZ input is disabled in MCP because it can reference files "
                               "outside the workspace.");
            const auto rasterSize = reader.size();
            if (rasterSize.isValid() && (rasterSize.width() > 30000 || rasterSize.height() > 30000 ||
                                         qint64(rasterSize.width()) * rasterSize.height() > MaxPixels))
                return failure("Input image exceeds MCP document limits.");
            document.reset(FormatIO::open(path, &error));
            if (!document)
                return failure(error);
            if (!validDocument(document.get(), &error))
                return failure(error);
        }
        const auto id = retain(std::move(document));
        return textResult(describe(id, m_documents.at(id).get()));
    }
    const auto id = args["document_id"].toString();
    const auto found = m_documents.find(id);
    if (found == m_documents.end())
        return failure("Unknown session document ID.");
    auto *document = found->second.get();
    if (name == "inspect_document")
        return textResult(describe(id, document));
    if (name == "close_document") {
        if (document->isModified() && !args["discard"].toBool())
            return failure("Document has unsaved edits; save it or explicitly set discard=true.");
        m_documents.erase(found);
        return textResult({{"closed_document_id", id}});
    }
    if (name == "preview_document") {
        const int dimension = args["max_dimension"].toInt(1024);
        auto image = document->composite()
                         .scaled(dimension, dimension, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                         .convertToFormat(QImage::Format_RGBA8888);
        if (image.isNull())
            return failure("Could not render the preview.");
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG"))
            return failure("Could not encode the preview.");
        return {{"content", QJsonArray{QJsonObject{{"type", "image"},
                                                   {"mimeType", "image/png"},
                                                   {"data", QString::fromLatin1(bytes.toBase64())}}}},
                {"isError", false}};
    }
    if (name == "save_document") {
        const auto path = checkedPath(args["path"].toString(), true, &error);
        if (path.isEmpty())
            return failure(error);
        if (QFileInfo::exists(path) && !args["overwrite"].toBool())
            return failure("Output exists; use another filename or explicitly set overwrite=true.");
        if (!FormatIO::save(document, path, &error))
            return failure(error);
        const auto suffix = QFileInfo(path).suffix().toLower();
        if (suffix == "spe" || suffix == "speb") {
            document->filePath = path;
            document->markSaved();
        }
        return textResult({{"path", path},
                           {"native_master", suffix == "spe" || suffix == "speb"},
                           {"document", describe(id, document)}});
    }
    if (name == "undo" || name == "redo") {
        if (name == "undo") {
            if (!document->canUndo())
                return failure("Nothing to undo.");
            document->undo();
        } else {
            if (!document->canRedo())
                return failure("Nothing to redo.");
            document->redo();
        }
        return textResult(describe(id, document));
    }
    if (name == "select_layer" || name == "set_layer_properties") {
        bool ok = false;
        const quint64 layerId = args["layer_id"].toString().toULongLong(&ok);
        const int index = document->indexForId(layerId);
        if (!ok || index < 0)
            return failure("Unknown layer ID.");
        if (name == "select_layer")
            document->setActiveIndex(index);
        else {
            const auto lock = editLock(document, layerId);
            if (!lock.isEmpty())
                return failure(lock);
            document->mutate("MCP layer properties", [&] {
                auto &layer = document->state.layers[index];
                if (args.contains("name"))
                    layer.name = args["name"].toString();
                if (args.contains("opacity"))
                    layer.opacity = args["opacity"].toDouble();
                if (args.contains("visible"))
                    layer.visible = args["visible"].toBool();
                if (args.contains("blend_mode"))
                    layer.blendMode = args["blend_mode"].toString();
            });
        }
        return textResult(describe(id, document));
    }
    if (name == "add_layer" || name == "create_text_layer") {
        if (document->state.layers.size() >= 256)
            return failure("MCP layer limit is 256.");
        const QColor color(args["color"].toString("#8b5cf6"));
        if (!color.isValid())
            return failure("Invalid layer color.");
        if (const auto *active = document->activeLayer()) {
            const quint64 parent = active->kind == LayerKind::Group ? active->id : active->parentId;
            const auto lock = editLock(document, parent);
            if (!lock.isEmpty())
                return failure(lock);
        }
        document->beginTransaction("MCP add layer");
        if (name == "create_text_layer") {
            document->addLayer("Text", LayerKind::Text);
            auto *layer = document->activeLayer();
            layer->text = args["text"].toString();
            layer->font = QFont(args["font_family"].toString("Onest"));
            layer->font.setPixelSize(args["font_size"].toInt(48));
            layer->font.setBold(args["bold"].toBool());
            layer->font.setItalic(args["italic"].toBool());
            layer->font.setLetterSpacing(QFont::AbsoluteSpacing, args["tracking"].toDouble());
            layer->offset = {args["x"].toDouble(), args["y"].toDouble()};
        } else {
            const auto kind = args["kind"].toString();
            document->addLayer(args["name"].toString(), kind == "group"           ? LayerKind::Group
                                                        : kind == "solid-fill"    ? LayerKind::SolidFill
                                                        : kind == "gradient-fill" ? LayerKind::GradientFill
                                                        : kind == "pattern-fill"  ? LayerKind::PatternFill
                                                                                  : LayerKind::Pixel);
        }
        document->activeLayer()->color = color;
        document->activeLayer()->parameters = args["parameters"].toObject();
        if (!validDocument(document, &error)) {
            document->cancelTransaction();
            return failure(error);
        }
        document->touch();
        document->endTransaction();
        return textResult(describe(id, document));
    }
    if (name == "run_actions") {
        const auto steps = args["steps"].toArray();
        if (steps.isEmpty() || steps.size() > 100)
            return failure("Provide 1–100 action objects.");
        for (const auto &value : steps) {
            if (!value.isObject())
                return failure("Every action must be an object.");
        }
        document->beginTransaction("MCP action set");
        for (const auto &step : steps) {
            // Resolve defaults against the state produced by the previous step, before allocating pixels.
            if (!validActionAllocation(document, step.toObject(), &error) ||
                !ActionRunner::execute(document, step.toObject(), &error) ||
                !validDocument(document, &error)) {
                document->cancelTransaction();
                return failure(error);
            }
        }
        document->endTransaction();
        return textResult(describe(id, document));
    }
    return failure("Tool is not implemented.");
}
std::optional<QJsonObject> McpServer::handle(const QJsonObject &message) {
    const auto id = message.value("id");
    const auto method = message.value("method").toString();
    if (message["jsonrpc"] != "2.0" || !message["method"].isString() || method.isEmpty() ||
        (message.contains("id") && !id.isString() && !id.isDouble()))
        return rpcError(QJsonValue::Null, -32600, "Invalid JSON-RPC request.");
    if (!message.contains("id")) {
        if (method == "notifications/initialized" && m_initialized)
            m_ready = true;
        return std::nullopt;
    }
    if (message.contains("params") && !message["params"].isObject())
        return rpcError(id, -32602, "Parameters must be an object.");
    const auto params = message["params"].toObject();
    QJsonObject result;
    if (method == "ping")
        result = {};
    else if (method == "initialize") {
        if (m_initialized)
            return rpcError(id, -32600, "This session is already initialized.");
        if (!m_startupError.isEmpty())
            return rpcError(id, -32000, m_startupError);
        if (!params["protocolVersion"].isString() || !params["capabilities"].isObject() ||
            !params["clientInfo"].isObject())
            return rpcError(id, -32602, "Initialize requires protocolVersion, capabilities and clientInfo.");
        const auto requested = params["protocolVersion"].toString();
        const auto version = QStringList{"2025-11-25", "2025-06-18", "2025-03-26"}.contains(requested)
                                 ? requested
                                 : QString("2025-11-25");
        result = {
            {"protocolVersion", version},
            {"capabilities", QJsonObject{{"tools", QJsonObject{{"listChanged", false}}}}},
            {"serverInfo",
             QJsonObject{{"name", "serika-photoedit"}, {"version", "0.0.1"}, {"title", "Serika PhotoEdit"}}},
            {"instructions",
             "Documents belong to this isolated local session. Inspect IDs before editing, preview before "
             "saving, and save native SPE for editable layers. Paths must stay inside the configured "
             "workspace. Existing files require overwrite=true. This server does not control GUI windows."}};
        m_initialized = true;
    } else {
        if (!m_ready)
            return rpcError(id, -32002, "Initialize the session and send notifications/initialized first.");
        if (method == "tools/list") {
            if (params.contains("cursor"))
                return rpcError(id, -32602, "This server has no additional tool pages.");
            result = {{"tools", toolDefinitions()}};
        } else if (method == "tools/call") {
            const auto name = params["name"].toString();
            QJsonObject definition;
            for (const auto &value : toolDefinitions())
                if (value.toObject()["name"] == name)
                    definition = value.toObject();
            if (definition.isEmpty())
                return rpcError(id, -32602, "Unknown tool: " + name);
            if (params.contains("arguments") && !params["arguments"].isObject())
                return rpcError(id, -32602, "Tool arguments must be an object.");
            QString error;
            const auto arguments = params["arguments"].toObject();
            if (!validate(arguments, definition["inputSchema"].toObject(), &error))
                return rpcError(id, -32602, error);
            result = call(name, arguments);
        } else
            return rpcError(id, -32601, "Method not found: " + method);
    }
    return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}
int McpServer::runStdio(const QString &workspace) {
    McpServer server(workspace);
    if (!server.startupError().isEmpty()) {
        std::fprintf(stderr, "%s\n", server.startupError().toUtf8().constData());
        return 2;
    }
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    QFile input, output;
    if (!input.open(stdin, QIODevice::ReadOnly) || !output.open(stdout, QIODevice::WriteOnly))
        return 2;
    constexpr qint64 limit = 4 * 1024 * 1024;
    while (true) {
        auto line = input.readLine(limit + 2);
        if (line.isEmpty())
            break;
        std::optional<QJsonObject> response;
        if (line.size() > limit) {
            while (!line.endsWith('\n') && !input.atEnd())
                line = input.readLine(limit + 2);
            response = rpcError(QJsonValue::Null, -32600, "MCP message exceeds 4 MiB.");
        } else {
            QJsonParseError parse;
            const auto json = QJsonDocument::fromJson(line, &parse);
            if (parse.error != QJsonParseError::NoError)
                response = rpcError(QJsonValue::Null, -32700, "Invalid JSON.");
            else if (!json.isObject())
                response = rpcError(QJsonValue::Null, -32600,
                                    "Requests must be JSON objects; batches are unsupported.");
            else
                response = server.handle(json.object());
        }
        if (response) {
            const auto bytes = QJsonDocument(*response).toJson(QJsonDocument::Compact) + '\n';
            if (output.write(bytes) != bytes.size() || !output.flush())
                return 1;
        }
    }
    return 0;
}
} // namespace serika
