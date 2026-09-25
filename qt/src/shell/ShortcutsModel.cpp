#include "ShortcutsModel.h"

#include <algorithm>

#include "control/settings/Settings.h"

namespace xqt {

namespace {
const char* const CUSTOM = "xournalQt";

/// The keys Qt uses on this platform for one of its standard actions.
QStringList standard(QKeySequence::StandardKey key) {
    QStringList list;
    for (const QKeySequence& sequence: QKeySequence::keyBindings(key)) {
        list << sequence.toString(QKeySequence::PortableText);
    }
    return list;
}
}  // namespace

ShortcutsModel::ShortcutsModel(Settings& settings, QObject* parent): QAbstractListModel(parent), settings(settings) {
    const QString document = tr("Document");
    const QString pages = tr("Pages");
    const QString edit = tr("Editing");
    const QString view = tr("View");
    const QString search = tr("Search");
    const QString tools = tr("Tools");
    actions = {
            {"newDocument", tr("New document"), document, QStringList{"Ctrl+Shift+N"} + standard(QKeySequence::AddTab)},
            {"open", tr("Open…"), document, standard(QKeySequence::Open)},
            {"save", tr("Save"), document, standard(QKeySequence::Save)},
            {"saveAs", tr("Save as…"), document, standard(QKeySequence::SaveAs)},
            {"export", tr("Export as plain PDF…"), document, {"Ctrl+E"}},
            {"print", tr("Print…"), document, standard(QKeySequence::Print)},
            {"closeTab", tr("Close the document"), document, standard(QKeySequence::Close)},
            {"quit", tr("Quit"), document, standard(QKeySequence::Quit)},
            {"home", tr("Library and recent documents"), document, {"Ctrl+Shift+L", "Alt+Home"}},
            {"tabOverview", tr("All open documents"), document, {"Ctrl+Shift+E"}},
            {"nextTab", tr("Next document"), document, {"Ctrl+Tab", "Ctrl+PgDown"}},
            {"previousTab", tr("Previous document"), document,
             {"Ctrl+Shift+Tab", "Ctrl+Backtab", "Ctrl+Shift+Backtab", "Ctrl+PgUp"}},

            {"addPage", tr("Add a page"), pages, {"Ctrl+N"}},
            {"pageGrid", tr("All pages"), pages, {"Ctrl+Alt+G"}},
            {"contents", tr("Contents overview"), pages, {"Ctrl+Alt+O"}},

            {"undo", tr("Undo"), edit, standard(QKeySequence::Undo)},
            {"redo", tr("Redo"), edit, standard(QKeySequence::Redo)},  // (Qt already has Ctrl+Y in there)
            {"copy", tr("Copy"), edit, standard(QKeySequence::Copy)},
            {"cut", tr("Cut"), edit, standard(QKeySequence::Cut)},
            {"paste", tr("Paste"), edit, standard(QKeySequence::Paste)},
            {"deleteSelection", tr("Delete what is selected"), edit,
             standard(QKeySequence::Delete) + QStringList{"Backspace"}},
            {"selectAll", tr("Select everything on the page"), edit, standard(QKeySequence::SelectAll)},
            {"textMode", tr("Text mode"), edit, {"Ctrl+Alt+E"}},
            {"markdownMode", tr("Markdown box"), edit, {"Ctrl+Alt+M"}},

            {"zoomIn", tr("Zoom in"), view, standard(QKeySequence::ZoomIn)},
            {"zoomOut", tr("Zoom out"), view, standard(QKeySequence::ZoomOut)},
            {"fitWidth", tr("Fit the width"), view, {"Ctrl+0"}},
            // (Ctrl+0 fits the width here, as it long has; 1:1 as in image and drawing programs)
            {"realSize", tr("Real size (100 %, as large as the paper)"), view, {"Ctrl+1"}},
            {"fullScreen", tr("Full screen"), view, {"F11"}},
            {"present", tr("Present (full screen, page by page)"), view, {"F5"}},
            {"back", tr("Back"), view, standard(QKeySequence::Back)},
            {"forward", tr("Forward"), view, standard(QKeySequence::Forward)},
            {"settings", tr("Settings"), view, {"Ctrl+,"}},
            {"shortcuts", tr("These shortcuts"), view, {"F1", "Ctrl+/"}},

            // Single keys, like Xournal++'s tools (only while the page is at hand: not while typing, and not in the
            // overviews, which search what is typed)
            {"toolPen", tr("Pen"), tools, {"P"}},
            {"toolEraser", tr("Eraser"), tools, {"E"}},
            {"toolHighlighter", tr("Highlighter"), tools, {"H"}},
            {"toolText", tr("Text"), tools, {"T"}},
            {"toolSelect", tr("Select (rectangle)"), tools, {"S"}},
            {"toolLasso", tr("Select (lasso)"), tools, {"L"}},
            {"toolHand", tr("Hand (scroll)"), tools, {"A"}},
            {"insertImage", tr("Insert an image…"), tools, {"I"}},

            {"find", tr("Search"), search, standard(QKeySequence::Find)},
            {"searchAllDocuments", tr("Search all open documents"), search, {"Ctrl+Shift+F"}},
            {"searchLibrary", tr("Search the library"), search, {"Ctrl+Alt+F"}},
            {"findNext", tr("Next hit"), search, standard(QKeySequence::FindNext)},
            {"findPrevious", tr("Previous hit"), search, standard(QKeySequence::FindPrevious)},
    };

    // No keys twice: Qt calls a shortcut that two actions want "ambiguous" and then does nothing at all. The
    // action that comes first in this list keeps the keys.
    QStringList taken;
    for (Action& action: actions) {
        QStringList kept;
        for (const QString& keys: action.defaults) {
            const QString text = QKeySequence(keys, QKeySequence::PortableText).toString(QKeySequence::PortableText);
            if (!text.isEmpty() && !taken.contains(text)) {
                taken << text;
                kept << text;
            }
        }
        action.defaults = kept;
    }

    std::string stored;
    settings.getCustomElement(CUSTOM).getString("shortcuts", stored);
    for (const QString& entry: QString::fromStdString(stored).split(';', Qt::SkipEmptyParts)) {
        const QString id = entry.section('=', 0, 0).trimmed();
        if (find(id)) {
            custom[id] = entry.section('=', 1).trimmed();
        }
    }
}

const ShortcutsModel::Action* ShortcutsModel::find(const QString& id) const {
    const auto it = std::find_if(actions.begin(), actions.end(), [&id](const Action& a) { return a.id == id; });
    return it == actions.end() ? nullptr : &*it;
}

int ShortcutsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(actions.size());
}

QStringList ShortcutsModel::keys(const QString& id) const {
    if (const auto it = custom.find(id); it != custom.end()) {
        return it->second.isEmpty() ? QStringList{} : QStringList{it->second};
    }
    const Action* action = find(id);
    return action ? action->defaults : QStringList{};
}

QString ShortcutsModel::conflict(const QString& id, const QString& keys) const {
    const QKeySequence wanted(keys, QKeySequence::PortableText);
    if (keys.isEmpty() || wanted.isEmpty()) {
        return {};
    }
    for (const Action& action: actions) {
        if (action.id == id) {
            continue;
        }
        for (const QString& other: this->keys(action.id)) {
            if (QKeySequence(other, QKeySequence::PortableText) == wanted) {
                return action.name;
            }
        }
    }
    return {};
}

bool ShortcutsModel::setKeys(const QString& id, const QString& keys) {
    if (!find(id)) {
        return false;
    }
    const QString trimmed = keys.trimmed();
    if (!trimmed.isEmpty() && QKeySequence(trimmed, QKeySequence::PortableText).isEmpty()) {
        return false;
    }
    if (trimmed.isEmpty()) {
        custom.erase(id);  // back to the default
    } else {
        custom[id] = QKeySequence(trimmed, QKeySequence::PortableText).toString(QKeySequence::PortableText);
    }
    store();
    return true;
}

QString ShortcutsModel::keyText(int combination) const {
    return QKeySequence(combination).toString(QKeySequence::PortableText);
}

void ShortcutsModel::resetAll() {
    custom.clear();
    store();
}

void ShortcutsModel::store() {
    QStringList entries;
    for (const auto& [id, keys]: custom) {
        entries << id + "=" + keys;
    }
    settings.getCustomElement(CUSTOM).setString("shortcuts", entries.join(';').toStdString());
    settings.customSettingsChanged();
    ++rev;
    beginResetModel();
    endResetModel();
    Q_EMIT changed();
}

QVariant ShortcutsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const Action& action = actions[static_cast<size_t>(index.row())];
    const QStringList current = keys(action.id);
    switch (role) {
        case IdRole:
            return action.id;
        case NameRole:
            return action.name;
        case GroupRole:
            return action.group;
        case KeysRole:
            return current.isEmpty() ? QString() : current.first();
        case DefaultKeysRole:
            return action.defaults.isEmpty() ? QString() : action.defaults.first();
        case IsDefaultRole:
            return custom.find(action.id) == custom.end();
        case ConflictRole:
            return conflict(action.id, current.isEmpty() ? QString() : current.first());
        default:
            return {};
    }
}

QHash<int, QByteArray> ShortcutsModel::roleNames() const {
    return {{IdRole, "actionId"},   {NameRole, "name"},          {GroupRole, "group"},
            {KeysRole, "keys"},     {DefaultKeysRole, "defaultKeys"}, {IsDefaultRole, "isDefault"},
            {ConflictRole, "conflict"}};
}

QVariantList ShortcutsModel::sheet() const {
    QVariantList list;
    for (const Action& action: actions) {
        const QStringList current = keys(action.id);
        if (current.isEmpty()) {
            continue;
        }
        list.append(QVariantMap{{"group", action.group},
                                {"name", action.name},
                                {"keys", QKeySequence(current.first(), QKeySequence::PortableText)
                                                 .toString(QKeySequence::NativeText)}});
    }
    return list;
}

}  // namespace xqt
