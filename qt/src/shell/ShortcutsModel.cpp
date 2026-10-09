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

QStringList withoutBackKey(QStringList keys) {
    keys.removeAll(QKeySequence(Qt::Key_Back).toString(QKeySequence::PortableText));
    return keys;
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
            // (qt/docs/features/quick-note.md: a new note in the library's Inbox, or a line in today's Markdown note
            // there)
            {"quickNote", tr("Quick note"), document, {"Ctrl+Alt+N"}},
            // (qt/docs/features/audio.md: a recording for this document, started or stopped; the record button's tap)
            {"record", tr("Record audio (start or stop)"), document, {"Ctrl+Shift+R"}},
            {"open", tr("Open…"), document, standard(QKeySequence::Open)},
            {"save", tr("Save"), document, standard(QKeySequence::Save)},
            {"saveAs", tr("Save as…"), document, standard(QKeySequence::SaveAs)},
            // (version history: a milestone, qt/docs/features/hybrid-pdf.md "Version history")
            {"saveWithMessage", tr("Save with a message…"), document, {"Ctrl+Alt+S"}},
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
            // (qt/docs/features/page-files.md: the page as a high-resolution PNG on the clipboard)
            {"copyPageImage", tr("Copy the page as an image"), pages, {"Ctrl+Shift+C"}},

            {"undo", tr("Undo"), edit, standard(QKeySequence::Undo)},
            {"redo", tr("Redo"), edit, standard(QKeySequence::Redo)},  // (Qt already has Ctrl+Y in there)
            {"copy", tr("Copy"), edit, standard(QKeySequence::Copy)},
            {"cut", tr("Cut"), edit, standard(QKeySequence::Cut)},
            {"paste", tr("Paste"), edit, standard(QKeySequence::Paste)},
            {"deleteSelection", tr("Delete what is selected"), edit,
             standard(QKeySequence::Delete) + QStringList{"Backspace"}},
            {"selectAll", tr("Select everything on the page"), edit, standard(QKeySequence::SelectAll)},
            // (before the search: Qt has Ctrl+G and Ctrl+Shift+G for the next and previous hit on some systems, which
            // keep F3 and Shift+F3)
            {"group", tr("Group what is selected"), edit, {"Ctrl+G"}},
            {"ungroup", tr("Ungroup what is selected"), edit, {"Ctrl+Shift+G"}},
            {"markdownMode", tr("Markdown box"), edit, {"Ctrl+Alt+M"}},

            {"zoomIn", tr("Zoom in"), view, standard(QKeySequence::ZoomIn)},
            {"zoomOut", tr("Zoom out"), view, standard(QKeySequence::ZoomOut)},
            {"fitWidth", tr("Fit the width"), view, {"Ctrl+0"}},
            // (Ctrl+0 fits the width here, as it long has; 1:1 as in image and drawing programs)
            {"realSize", tr("Real size (100 %, as large as the paper)"), view, {"Ctrl+1"}},
            // (qt/docs/features/canvas-rotation.md: the canvas only, the pages stay as they are; a fit turns it
            // upright)
            {"rotateRight", tr("Turn the canvas clockwise (90°)"), view, {"Ctrl+]"}},
            {"rotateLeft", tr("Turn the canvas counter-clockwise (90°)"), view, {"Ctrl+["}},
            {"fullScreen", tr("Full screen"), view, {"F11"}},
            // (the reference view: both sides scrolled together; no key by default - Ctrl+Alt+L locks the screen on
            // many desktops)
            {"lockScroll", tr("Scroll the document and the reference together"), view, {}},
            {"present", tr("Present (full screen, page by page)"), view, {"F5"}},
            // (only the page: no floating toolbox; again while presenting: the controls back)
            {"presentClean", tr("Present without controls"), view, {"Ctrl+F5"}},
            // (qt/docs/features/zen.md: Read is Zen and read only, in full screen; again: back)
            {"read", tr("Read (Zen, read only, full screen)"), view, {"Ctrl+Alt+R"}},
            // (qt/docs/features/zen.md: only the page and a faint dot; again, or Esc: the controls back)
            {"zen", tr("Zen (only the page)"), view, {"Ctrl+Alt+Z"}},
            // (without the Back key, which Qt 6.11 lists here: Android's back button and gesture are the window's
            // step out of things, WindowShortcuts.qml; two shortcuts on one key make Qt do neither)
            {"back", tr("Back"), view, withoutBackKey(standard(QKeySequence::Back))},
            {"forward", tr("Forward"), view, standard(QKeySequence::Forward)},
            {"settings", tr("Settings"), view, {"Ctrl+,"}},
            {"shortcuts", tr("These shortcuts"), view, {"F1", "Ctrl+/"}},

            // Single keys, like Xournal++'s tools (only while the page is at hand: not while typing, and not in the
            // overviews, which search what is typed)
            {"toolPen", tr("Pen"), tools, {"P"}},
            {"toolEraser", tr("Eraser"), tools, {"E"}},
            {"toolHighlighter", tr("Highlighter"), tools, {"H"}},
            {"toolText", tr("Text box (Markdown)"), tools, {"T"}},
            {"toolSelect", tr("Select (rectangle)"), tools, {"S"}},
            {"toolLasso", tr("Select (lasso)"), tools, {"L"}},
            // (the snip tool: one picture of a rectangle or lasso to the clipboard, then the tool before; snip.md)
            {"snip", tr("Snip: copy the picture of a rectangle"), tools, {"Shift+S"}},
            {"snipLasso", tr("Snip with the lasso"), tools, {"Shift+L"}},
            // (one sweep over handwriting: its words to the clipboard as text, then the tool before; the "Text"
            // button's second tool, qt/docs/features/handwriting-search.md)
            {"copyInkText", tr("Copy handwriting as text"), tools, {"Shift+T"}},
            {"toolHand", tr("Hand (scroll)"), tools, {"A"}},
            {"insertImage", tr("Insert an image…"), tools, {"I"}},
            // (B as PowerPoint's black screen: a black sheet over part of the page)
            {"curtain", tr("Curtain (hide part of the page)"), tools, {"B"}},
            {"spotlight", tr("Spotlight (only a part of the page shown)"), tools, {"Shift+B"}},

            {"find", tr("Search"), search, standard(QKeySequence::Find)},
            // (where text can be written: a .md or .txt, Markdown text on pages; qt/docs/features/md-editor.md)
            {"replace", tr("Find and replace"), search, {"Ctrl+H"}},
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
        if (find(id) || id.startsWith(QLatin1String("plugin:"))) {  // (a plugin's: before its command is known)
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
    if (!action) {
        return {};
    }
    if (!id.startsWith(QLatin1String("plugin:")) || action->defaults.isEmpty()) {
        return action->defaults;
    }
    // A plugin's default key gives way to an action of the app that has it now (the user gave it one later): two
    // shortcuts on one key would make Qt do neither
    QStringList kept;
    for (const QString& k: action->defaults) {
        const QKeySequence wanted(k, QKeySequence::PortableText);
        const bool taken = std::any_of(actions.begin(), actions.end(), [&](const Action& other) {
            if (other.id.startsWith(QLatin1String("plugin:"))) {
                return false;
            }
            const QStringList otherKeys = keys(other.id);
            return std::any_of(otherKeys.begin(), otherKeys.end(), [&](const QString& o) {
                return QKeySequence(o, QKeySequence::PortableText) == wanted;
            });
        });
        if (!taken) {
            kept << k;
        }
    }
    return kept;
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

void ShortcutsModel::setPluginActions(const std::vector<PluginAction>& list) {
    beginResetModel();
    actions.erase(std::remove_if(actions.begin(), actions.end(),
                                 [](const Action& a) { return a.id.startsWith(QLatin1String("plugin:")); }),
                  actions.end());
    QStringList taken;
    for (const Action& a: actions) {
        for (const QString& k: keys(a.id)) {
            taken << QKeySequence(k, QKeySequence::PortableText).toString(QKeySequence::PortableText);
        }
    }
    const QString group = tr("Plugins");
    for (const PluginAction& p: list) {
        QStringList defaults;
        const QKeySequence seq(p.keys, QKeySequence::PortableText);
        const QString text = seq.toString(QKeySequence::PortableText);
        const bool modifier = seq.count() == 1 && (seq[0].keyboardModifiers() &
                                                    (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
        if (!text.isEmpty() && modifier && !taken.contains(text)) {
            defaults << text;
            taken << text;
        }
        actions.push_back({p.id, p.name, group, defaults});
    }
    endResetModel();
    ++rev;
    Q_EMIT changed();
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
