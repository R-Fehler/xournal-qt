/*
 * xournal-qt: the permission-checked operations layer (qt/docs/decisions/0008-js-plugins.md).
 *
 * Every change a plugin makes to a document is an *operation*: a name from the vocabulary of the collaboration
 * research (qt/research/collaboration.md, §4: stroke.insert, element.delete, layer.add, page.insert, background.set,
 * …), plain data as its arguments (QVariant: no script engine here), and an operation *class* (what a user grants:
 * "edit", "pages", "tools", "files"; "read", "view" and "ui" for what changes nothing). An operation is applied for a
 * *principal* (the local user, a plugin; later a remote peer or an agent), checked against what that principal may do
 * (its Authority: a plugin's manifest and the user's answers on first use; later a peer's signed capability), and it
 * changes the document through the session's own undo machinery. Operations of one command form a *transaction*: one
 * undo step, or nothing at all when one of them fails (UndoGathering).
 *
 * The table of operations is per environment: a window registers the document operations (DocumentOps.h) and its own
 * (the selection, the tool in hand, the view, dialogs, files); the CLI only the document's. A plugin's call, and later
 * an MCP tool call or a peer's operation, all go through apply().
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include "model/PageRef.h"

class Element;
class Layer;

namespace xqt {
class DocumentSession;
class UndoGathering;
}  // namespace xqt

namespace xqt::ops {

/// Who acts
struct Principal {
    enum class Kind { User, Plugin, Peer, Agent };
    Kind kind = Kind::User;
    QString id;    ///< a plugin's id (the key of its data on elements), a peer's key, an agent's name; "" the user
    QString name;  ///< shown to the user ("Function plotter")
    static Principal user() { return {}; }
    static Principal plugin(const QString& id, const QString& name) { return {Kind::Plugin, id, name}; }
};

/// What happens to an operation of a principal
enum class Decision { Allow, Deny, Ask };

/// Decides what principals may do. A plugin's: allowed when the user allowed its class, asked when the manifest names
/// the class and the user has not answered, denied otherwise.
class Authority {
public:
    virtual ~Authority() = default;
    virtual Decision decide(const Principal& who, const QString& op, const QString& opClass) = 0;
    /// The user is asked (may wait for an answer: a nested event loop) and the answer is kept. True: allowed.
    virtual bool ask(const Principal& who, const QString& op, const QString& opClass);
};

/// Everyone may do everything (the local user; tests)
class AllowAll final: public Authority {
public:
    Decision decide(const Principal&, const QString&, const QString&) override { return Decision::Allow; }
};

/// Operation patterns ("stroke.insert", "element.*", "*"), as capabilities name them
class Patterns {
public:
    Patterns() = default;
    explicit Patterns(QStringList patterns): list(std::move(patterns)) {}
    bool matches(const QString& op) const;
    const QStringList& patterns() const { return list; }

private:
    QStringList list;
};

/// The operations of a permission class ("edit": element.*, stroke.insert, …); the classes that need no permission
/// ("read", "view", "ui") give "*" of their own operations
Patterns patternsOfClass(const QString& opClass);
/// The classes a user grants, with what they are asked as ("change what is on the pages")
QStringList grantableClasses();
QString describeClass(const QString& opClass);
/// Classes that need no permission (reading the document, the view, dialogs)
bool isFreeClass(const QString& opClass);

/// Why an operation failed. Thrown by apply(); nothing of the failed operation was done.
class Error: public std::runtime_error {
public:
    enum class Kind {
        Denied,    ///< the principal may not (or the user said no)
        Invalid,   ///< bad arguments
        Stale,     ///< an element reference that is gone
        ReadOnly,  ///< the document cannot be changed now (read only, replayed, no document)
        Unknown,   ///< no such operation
        Failed     ///< it could not be done
    };
    Error(Kind kind, const QString& message): std::runtime_error(message.toStdString()), kind(kind), text(message) {}
    Kind kind;
    QString text;
    /// "PermissionError", "TypeError", … (the name a script sees)
    QString name() const;
};

/// Elements handed out as opaque references ("e12"): a page, a layer, an element. Checked when used (the element must
/// still be in that layer of that page of the document).
class ElementRefs {
public:
    struct Target {
        PageRef page;
        Layer* layer = nullptr;
        const Element* element = nullptr;
        size_t pageIndex = 0;
    };
    QString add(const PageRef& page, Layer* layer, const Element* element);
    /// The element, or Error::Stale. The caller holds the document's lock (shared is enough).
    Target resolve(const QString& ref, const DocumentSession& session) const;
    void clear() { refs.clear(); }

private:
    std::map<QString, Target> refs;
    quint64 next = 1;
};

class Operations;

/// One principal acting on one document (or none) through a table of operations: its references and its transaction
class Context {
public:
    Context(Operations& ops, Principal who, Authority& authority, DocumentSession* document);
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    Operations& operations;
    Principal principal;
    Authority& authority;
    DocumentSession* document;  ///< nullptr: none open
    ElementRefs refs;
    /// The window's own things (the app sets it for its window operations; nullptr headless)
    void* window = nullptr;

    /// Checks and applies an operation (Error when it is refused or fails: then it did nothing). A change outside a
    /// transaction is one of its own (one undo step, titled `name`).
    QVariant apply(const QString& name, const QVariantMap& args = {});
    /// Only the check of an operation (Error::Denied); asks when the authority says so. For operations that check
    /// their parts (element.insert checks stroke.insert, text.insert, … for each element).
    void check(const QString& op, const QString& opClass);

    /// A transaction: what the operations change until commit() is one undo step titled `title`; rollback() (or the
    /// end of the context) undoes it. One at a time.
    void begin(const QString& title);
    bool inTransaction() const;
    /// Something was changed in the transaction so far
    bool changed() const;
    /// Ends the transaction: true when it pushed an undo step
    bool commit();
    void rollback();

    /// The document, or Error::ReadOnly (none, read only, replayed)
    DocumentSession& writable() const;
    DocumentSession& readable() const;

private:
    std::unique_ptr<UndoGathering> transaction;
};

/// The table of operations of an environment
class Operations {
public:
    using Handler = std::function<QVariant(Context& context, const QVariantMap& args)>;
    struct Info {
        QString name;
        QString opClass;
        bool writes = false;  ///< changes the document (refused while it is read only; part of the transaction)
        Handler handler;
    };
    /// With the document operations (DocumentOps.h)
    Operations();
    void add(const QString& name, const QString& opClass, bool writes, Handler handler);
    const Info* find(const QString& name) const;
    QStringList names() const;

private:
    std::map<QString, Info> table;
};

// --- helpers for handlers -----------------------------------------------------------------------------------------
/// args[key] as a number (Error::Invalid when missing and no default)
double number(const QVariantMap& args, const QString& key, std::optional<double> fallback = std::nullopt);
int integer(const QVariantMap& args, const QString& key, std::optional<int> fallback = std::nullopt);
QString string(const QVariantMap& args, const QString& key, std::optional<QString> fallback = std::nullopt);
/// A page index of the context's document (Error::Invalid when out of range)
size_t pageIndex(const Context& context, const QVariantMap& args, const QString& key = QStringLiteral("page"));

}  // namespace xqt::ops
