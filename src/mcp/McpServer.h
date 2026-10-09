#pragma once
#include "document/Document.h"
#include <QJsonObject>
#include <QString>
#include <map>
#include <memory>
#include <optional>
namespace serika {
// Local stdio session. Documents are isolated from interactive windows.
class McpServer {
  public:
    explicit McpServer(const QString &workspace);
    QString startupError() const { return m_startupError; }
    std::optional<QJsonObject> handle(const QJsonObject &message);
    static QJsonArray toolDefinitions();
    static int runStdio(const QString &workspace);

  private:
    QString m_root, m_startupError;
    bool m_initialized = false, m_ready = false;
    std::map<QString, std::unique_ptr<Document>> m_documents;
    QJsonObject call(const QString &name, const QJsonObject &args);
    QString checkedPath(const QString &path, bool write, QString *error) const;
    QJsonObject describe(const QString &id, const Document *document) const;
    QString retain(std::unique_ptr<Document> document);
};
} // namespace serika
