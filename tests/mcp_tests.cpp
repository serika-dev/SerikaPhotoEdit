#include "io/FormatIO.h"
#include "mcp/McpServer.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
using namespace serika;
namespace {
QJsonObject request(const QString &method, QJsonObject params = {}) {
    return {{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}, {"params", params}};
}
void initialize(McpServer &server) {
    server.handle(request("initialize", {{"protocolVersion", "2025-11-25"},
                                         {"capabilities", QJsonObject{}},
                                         {"clientInfo", QJsonObject{{"name", "test"}, {"version", "1"}}}}));
    server.handle({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
}
QJsonObject call(McpServer &server, const QString &name, QJsonObject args = {}) {
    return server.handle(request("tools/call", {{"name", name}, {"arguments", args}})).value();
}
QJsonObject result(const QJsonObject &response) { return response["result"].toObject(); }
QJsonObject data(const QJsonObject &response) { return result(response)["structuredContent"].toObject(); }
QString create(McpServer &server) {
    return data(call(server, "create_document",
                     {{"width", 32}, {"height", 24}, {"background", "white"}}))["document_id"]
        .toString();
}
bool failed(const QJsonObject &response) { return result(response)["isError"].toBool(); }
} // namespace
class McpTests : public QObject {
    Q_OBJECT
  private slots:
    void lifecycle() {
        QTemporaryDir root;
        McpServer server(root.path());
        QVERIFY(server.startupError().isEmpty());
        QCOMPARE(server.handle(request("tools/list"))->value("error").toObject()["code"].toInt(), -32002);
        initialize(server);
        QCOMPARE(result(server.handle(request("tools/list")).value())["tools"].toArray().size(), 15);
        QVERIFY(server.handle(request("ping"))->contains("result"));
        QCOMPARE(server.handle(request("unknown"))->value("error").toObject()["code"].toInt(), -32601);
        QVERIFY(!server.handle({{"jsonrpc", "2.0"}, {"method", "notifications/cancelled"}}));
        QCOMPARE(server.handle({})->value("error").toObject()["code"].toInt(), -32600);
    }
    void rejectsInvalidInputs() {
        QTemporaryDir root;
        McpServer server(root.path());
        initialize(server);
        for (const auto &args :
             {QJsonObject{{"width", 0}, {"height", 32}}, QJsonObject{{"width", 1.5}, {"height", 32}},
              QJsonObject{{"width", 32}, {"height", 32}, {"surprise", true}},
              QJsonObject{{"width", "32"}, {"height", 32}}})
            QCOMPARE(call(server, "create_document", args)["error"].toObject()["code"].toInt(), -32602);
        QVERIFY(failed(call(server, "create_document", {{"width", 30000}, {"height", 30000}})));
        QVERIFY(failed(call(server, "inspect_document", {{"document_id", "missing"}})));
        McpServer absent(root.filePath("missing"));
        QVERIFY(!absent.startupError().isEmpty());
        McpServer empty("");
        QVERIFY(!empty.startupError().isEmpty());
    }
    void editsUndoAndNativeRoundTrip() {
        QTemporaryDir root;
        McpServer server(root.path());
        initialize(server);
        const auto id = create(server);
        QVERIFY(!id.isEmpty());
        auto added = call(server, "create_text_layer",
                          {{"document_id", id}, {"text", "Serika"}, {"font_size", 20}, {"x", 3}, {"y", 4}});
        QVERIFY(!failed(added));
        QCOMPARE(data(added)["layers"].toArray().size(), 2);
        const auto layer = data(added)["active_layer_id"].toString();
        QVERIFY(
            !failed(call(server, "set_layer_properties",
                         {{"document_id", id}, {"layer_id", layer}, {"opacity", 0.5}, {"name", "Title"}})));
        auto undone = call(server, "undo", {{"document_id", id}});
        QCOMPARE(data(undone)["layers"].toArray().last().toObject()["opacity"].toDouble(), 1.0);
        auto redone = call(server, "redo", {{"document_id", id}});
        QCOMPARE(data(redone)["layers"].toArray().last().toObject()["name"].toString(), QString("Title"));
        QVERIFY(failed(call(server, "close_document", {{"document_id", id}})));
        QVERIFY(!failed(call(server, "save_document", {{"document_id", id}, {"path", "master.spe"}})));
        QString error;
        std::unique_ptr<Document> reopened(FormatIO::open(root.filePath("master.spe"), &error));
        QVERIFY2(reopened, error.toUtf8());
        QCOMPARE(reopened->state.layers.last().text, QString("Serika"));
        QVERIFY(!failed(call(server, "close_document", {{"document_id", id}})));
        QVERIFY(!failed(call(server, "open_document", {{"path", "master.spe"}})));
    }
    void confinementAndOverwrite() {
        QTemporaryDir root, outside;
        McpServer server(root.path());
        initialize(server);
        const auto id = create(server);
        for (const auto &path :
             QStringList{"../escaped.spe", outside.filePath("escaped.spe"), "missing/output.spe"})
            QVERIFY(failed(call(server, "save_document", {{"document_id", id}, {"path", path}})));
        QVERIFY(!QFileInfo::exists(outside.filePath("escaped.spe")));
        QVERIFY(!failed(call(server, "save_document", {{"document_id", id}, {"path", "test.spe"}})));
        QVERIFY(failed(call(server, "save_document", {{"document_id", id}, {"path", "test.spe"}})));
        QVERIFY(!failed(
            call(server, "save_document", {{"document_id", id}, {"path", "test.spe"}, {"overwrite", true}})));
        QVERIFY(failed(call(server, "open_document", {{"path", outside.filePath("missing.png")}})));
#ifndef Q_OS_WIN
        QVERIFY(QFile::link(outside.path(), root.filePath("escape")));
        QVERIFY(failed(call(server, "save_document", {{"document_id", id}, {"path", "escape/escaped.spe"}})));
#endif
    }
    void actionsAreAtomicAndBounded() {
        QTemporaryDir root;
        McpServer server(root.path());
        initialize(server);
        const auto id = create(server);
        const QJsonObject newLayer{{"command", "New Layer"}};
        auto response =
            call(server, "run_actions",
                 {{"document_id", id}, {"steps", QJsonArray{newLayer, QJsonObject{{"command", "Unknown"}}}}});
        QVERIFY(failed(response));
        QCOMPARE(data(call(server, "inspect_document", {{"document_id", id}}))["layers"].toArray().size(), 1);
        QVERIFY(!data(call(server, "inspect_document", {{"document_id", id}}))["can_undo"].toBool());
        for (const auto &command :
             QStringList{"resize", "image size...", "resize canvas", "canvas size..."}) {
            response = call(server, "run_actions",
                            {{"document_id", id},
                             {"steps", QJsonArray{QJsonObject{
                                           {"type", command},
                                           {"params", QJsonObject{{"width", 30000}, {"height", 30000}}}}}}});
            QVERIFY(failed(response));
        }
        response =
            call(server, "run_actions", {{"document_id", id}, {"steps", QJsonArray{newLayer, newLayer}}});
        QVERIFY(!failed(response));
        QCOMPARE(data(response)["layers"].toArray().size(), 3);
        QCOMPARE(data(call(server, "undo", {{"document_id", id}}))["layers"].toArray().size(), 1);
    }
    void previewIsPngAndExportKeepsDirty() {
        QTemporaryDir root;
        McpServer server(root.path());
        initialize(server);
        const auto id = create(server);
        call(server, "add_layer", {{"document_id", id}, {"kind", "solid-fill"}, {"color", "#8b5cf6"}});
        const auto content =
            result(call(server, "preview_document", {{"document_id", id}, {"max_dimension", 64}}))["content"]
                .toArray()
                .first()
                .toObject();
        QCOMPARE(content["mimeType"].toString(), QString("image/png"));
        QImage image;
        QVERIFY(image.loadFromData(QByteArray::fromBase64(content["data"].toString().toLatin1()), "PNG"));
        QCOMPARE(image.size(), QSize(64, 48));
        QCOMPARE(image.pixelColor(10, 10), QColor("#8b5cf6"));
        QVERIFY(!failed(call(server, "save_document", {{"document_id", id}, {"path", "preview.png"}})));
        QVERIFY(data(call(server, "inspect_document", {{"document_id", id}}))["modified"].toBool());
    }
    void evolvingResizeAndTransformLimitsRollBack() {
        QTemporaryDir root;
        McpServer server(root.path());
        initialize(server);
        const auto id = create(server);
        auto resize = call(
            server, "run_actions",
            {{"document_id", id},
             {"steps", QJsonArray{QJsonObject{{"command", "resize"},
                                              {"parameters", QJsonObject{{"width", 16000}, {"height", 1}}}},
                                  QJsonObject{{"type", "canvas size..."},
                                              {"params", QJsonObject{{"height", 2000}}}}}}});
        QVERIFY(failed(resize));
        const auto after = data(call(server, "inspect_document", {{"document_id", id}}));
        QCOMPARE(after["width"].toInt(), 32);
        QCOMPARE(after["height"].toInt(), 24);
        QVERIFY(!after["can_undo"].toBool());
        const auto large =
            data(call(server, "create_document",
                      {{"width", 1000}, {"height", 1000}, {"background", "white"}}))["document_id"]
                .toString();
        for (const auto &parameters :
             {QJsonObject{{"scale", 500}}, QJsonObject{{"matrix", QJsonArray{5, 0, 0, 0, 5, 0, 0, 0, 1}}}}) {
            const auto response = call(
                server, "run_actions",
                {{"document_id", large},
                 {"steps", QJsonArray{QJsonObject{{"command", "transform"}, {"parameters", parameters}}}}});
            QVERIFY(failed(response));
            QVERIFY(data(response)["error"].toString().contains("MCP"));
            QVERIFY(!data(call(server, "inspect_document", {{"document_id", large}}))["can_undo"].toBool());
        }
    }
    void svgCannotReadExternalImages() {
        QTemporaryDir root, outside;
        McpServer server(root.path());
        initialize(server);
        const auto external = outside.filePath("external.png");
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(external));
        const auto svg =
            QString("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                    "width=\"8\" height=\"8\"><image width=\"8\" height=\"8\" xlink:href=\"%1\"/></svg>")
                .arg(external)
                .toUtf8();
        for (const auto &name : QStringList{"external.svg", "misleading.png", "misleading.dat"}) {
            QFile file(root.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(svg), svg.size());
            file.close();
            QVERIFY(failed(call(server, "open_document", {{"path", name}})));
        }
    }
    void linkedDocumentsUseCachedPixels() {
        QTemporaryDir root, outside;
        McpServer server(root.path());
        initialize(server);
        const auto external = outside.filePath("outside.png");
        QImage source(8, 8, QImage::Format_RGB32);
        source.fill(Qt::red);
        QVERIFY(source.save(external));
        std::unique_ptr<Document> document(Document::create({8, 8}, Qt::blue));
        document->state.layers[0].kind = LayerKind::SmartObject;
        document->state.layers[0].linkedPath = external;
        QString error;
        QVERIFY2(FormatIO::saveNative(document.get(), root.filePath("linked.spe"), &error),
                 qPrintable(error));
        const auto opened = call(server, "open_document", {{"path", "linked.spe"}});
        QVERIFY(!failed(opened));
        const auto id = data(opened)["document_id"].toString();
        const auto content =
            result(call(server, "preview_document", {{"document_id", id}, {"max_dimension", 32}}))["content"]
                .toArray()
                .first()
                .toObject();
        QImage preview;
        QVERIFY(preview.loadFromData(QByteArray::fromBase64(content["data"].toString().toLatin1())));
        QCOMPARE(preview.pixelColor(4, 4), QColor(Qt::blue));
    }
    void stdioTransport() {
        QTemporaryDir root;
        QProcess process;
        const auto executable = QString::fromUtf8(SERIKA_APP_PATH);
        process.start(executable, {"--mcp", "--mcp-root", root.path()});
        QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
        const auto init =
            request("initialize", {{"protocolVersion", "2025-11-25"},
                                   {"capabilities", QJsonObject{}},
                                   {"clientInfo", QJsonObject{{"name", "test"}, {"version", "1"}}}});
        process.write(QJsonDocument(init).toJson(QJsonDocument::Compact) + '\n');
        QVERIFY(process.waitForReadyRead(5000));
        const auto initial = process.readAllStandardOutput();
        QVERIFY(QJsonDocument::fromJson(initial).object().contains("result"));
        process.write("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n");
        process.write(QJsonDocument(request("tools/list")).toJson(QJsonDocument::Compact) + '\n');
        process.write("invalid json\n");
        process.closeWriteChannel();
        QVERIFY(process.waitForFinished(15000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        const auto output = (initial + process.readAllStandardOutput()).trimmed().split('\n');
        QCOMPARE(output.size(), 3);
        for (const auto &line : output) {
            const auto json = QJsonDocument::fromJson(line);
            QVERIFY(json.isObject());
            QCOMPARE(json.object()["jsonrpc"].toString(), QString("2.0"));
        }
        QCOMPARE(QJsonDocument::fromJson(output.last()).object()["error"].toObject()["code"].toInt(), -32700);
    }
};
QTEST_MAIN(McpTests)
#include "mcp_tests.moc"
