/*
 * xournal-qt: the keyboard shortcuts of the window - the list for the cheat sheet and the settings, and the keys
 * the QML Shortcut items use.
 *
 * Every action has an id ("undo"), a name, a group and default keys. Defaults of the usual actions come from Qt,
 * so they follow the platform. What the user changes is kept in the settings ("shortcuts"), and `revision` changes
 * with it so the bindings in QML pick up the new keys at once.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <map>
#include <vector>

#include <QAbstractListModel>
#include <QKeySequence>
#include <QStringList>

class Settings;

namespace xqt {

class ShortcutsModel final: public QAbstractListModel {
    Q_OBJECT
    /// Changes whenever a shortcut changes (QML bindings follow it)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        GroupRole,
        /// What the user sees ("Ctrl+Z"), the first of the keys
        KeysRole,
        DefaultKeysRole,
        IsDefaultRole,
        /// Another action uses the same keys
        ConflictRole
    };

    explicit ShortcutsModel(Settings& settings, QObject* parent = nullptr);

    int revision() const { return rev; }
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// The keys of an action, for a QML Shortcut ("Ctrl+Z", ...).
    Q_INVOKABLE QStringList keys(const QString& id) const;
    /// The name of the action that uses these keys already ("" if none).
    Q_INVOKABLE QString conflict(const QString& id, const QString& keys) const;
    /// Give the action other keys (empty: back to the default). Returns false if the keys make no sense.
    Q_INVOKABLE bool setKeys(const QString& id, const QString& keys);
    Q_INVOKABLE void resetAll();
    /// Key combination of a QML key event (key | modifiers) as text ("Ctrl+Shift+P").
    Q_INVOKABLE QString keyText(int combination) const;
    /// The actions, for the cheat sheet: [{ group, name, keys }] in the order of the groups.
    Q_INVOKABLE QVariantList sheet() const;

Q_SIGNALS:
    void changed();

private:
    struct Action {
        QString id;
        QString name;
        QString group;
        QStringList defaults;
    };
    const Action* find(const QString& id) const;
    void store();

    Settings& settings;
    std::vector<Action> actions;
    std::map<QString, QString> custom;  ///< id -> keys of the user
    int rev = 0;
};

}  // namespace xqt
