#pragma once
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QKeySequence>
#include <QObject>
#include <QPointer>
#include <functional>

class QWidget;
class QKeyEvent;
namespace serika {
enum class ShortcutKind { Command, Tool, CycleTool, Local, Blend, Opacity, Flow, Hold };
struct ShortcutEntry {
    QString id;
    QString label;
    QString category;
    ShortcutKind kind = ShortcutKind::Command;
    QString payload;
    QList<QKeySequence> defaults;
    QList<QKeySequence> bindings;
    bool textSafe = false;
    bool repeat = false;
    QString scope = "editor";
};
class ShortcutRegistry : public QObject {
    Q_OBJECT
  public:
    explicit ShortcutRegistry(QWidget *owner, QObject *parent = nullptr);
    static QString commandId(const QString &name);
    static QList<ShortcutEntry> defaultEntries();
    static QList<QPair<QString, QStringList>> toolGroups();
    static bool isTextInput(QWidget *widget);
    const QList<ShortcutEntry> &entries() const { return m_entries; }
    const ShortcutEntry *entry(const QString &id) const;
    void addCommand(const QString &name, const QString &category = "Application menus");
    QHash<QString, QList<QKeySequence>> bindingMap() const;
    QStringList conflicts(const QHash<QString, QList<QKeySequence>> &bindings) const;
    bool setBindings(const QString &id, const QList<QKeySequence> &sequences, QString *error = nullptr);
    bool setAllBindings(const QHash<QString, QList<QKeySequence>> &bindings, QString *error = nullptr);
    void reset();
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject &json, QString *error = nullptr);
    bool load(const QString &path, QString *error = nullptr);
    bool save(const QString &path, QString *error = nullptr) const;
    bool dispatch(QKeyEvent *event, QWidget *focus);
    std::function<void(const ShortcutEntry &, bool released)> activated;
    std::function<bool(const ShortcutEntry &)> enabled;
  signals:
    void bindingsChanged();

  protected:
    bool eventFilter(QObject *, QEvent *) override;

  private:
    QPointer<QWidget> m_owner;
    QList<ShortcutEntry> m_entries;
    QList<QKeyCombination> m_prefix;
    QElapsedTimer m_prefixTimer;
    QHash<int, QString> m_held;
    bool inOwner(QWidget *widget) const;
    QList<const ShortcutEntry *> matching(const QList<QKeyCombination> &prefix, bool exact) const;
    bool eligible(const ShortcutEntry &entry, QWidget *focus) const;
    void releaseHolds();
};
} // namespace serika
