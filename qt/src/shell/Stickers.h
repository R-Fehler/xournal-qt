/*
 * xournal-qt: the sticker sets on disk (qt/docs/stickers.md) and the picker's list of them.
 *
 * Two sets: the library's, its visible folder "Stickers" (a fixed English name, subfolders for topics), and the
 * app-wide one in the app's data folder ("In all libraries"). A sticker is a .xopp (StickerFile.h) or a picture. The
 * own order of a folder is a hidden file in it, ".sticker-order.json" (it syncs with the folder); when each sticker was
 * last used is the device's, kept in the library's config folder ("stickers.json").
 *
 * The same folder model holds the page templates (qt/docs/templates.md, Kind::Templates): the library's folder
 * "Templates", the app-wide "<AppDataLocation>/templates", ".template-order.json", "templates.json"; a template is a
 * .xopp only (TemplateFile.h), with the PDF attached to it ("name.xopp.bg.pdf"), which goes wherever it goes.
 *
 * StickersModel is the picker's list: the stickers of one set (all of it, or one folder with its subfolders), found
 * by a search of their names, sorted by last use, the own order, the name or the date added. Listing reads the folders
 * (a few hundred files at most: on the UI thread, when the picker opens or a sticker changes).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <QAbstractListModel>
#include <QStringList>

#include "filesystem.h"

namespace xqt::stickers {

/// What a set holds: stickers (stickers.md) or page templates (templates.md)
enum class Kind { Stickers, Templates };

/// The library's sticker folder: "<library>/Stickers"
inline constexpr const char* FOLDER = "Stickers";
/// The library's template folder: "<library>/Templates"
inline constexpr const char* TEMPLATES_FOLDER = "Templates";
/// The own order of a folder, inside it
inline constexpr const char* ORDER_FILE = ".sticker-order.json";
inline constexpr const char* TEMPLATE_ORDER_FILE = ".template-order.json";
/// The own order's file of a set of this kind
const char* orderFile(Kind kind);

fs::path librarySet(const fs::path& libraryRoot, Kind kind = Kind::Stickers);
/// "<AppDataLocation>/stickers" ("templates")
fs::path appSet(Kind kind = Kind::Stickers);
/// (tests) Another folder for the app-wide set (empty: the app's again)
void setAppSet(const fs::path& folder, Kind kind = Kind::Stickers);

/// A sticker's file: a .xopp, or a picture (.png, .jpg, .jpeg, .webp); not hidden, not a backup. A template's: a .xopp.
bool isStickerFile(const fs::path& file, Kind kind = Kind::Stickers);
bool isPicture(const fs::path& file);
/// The files that belong to a .xopp next to it: its attached PDF ("name.xopp.bg.pdf") and background pictures
/// ("name.xopp.bg_1.png", ...), each with what follows the .xopp's name in its name (".bg.pdf")
std::vector<std::pair<fs::path, std::string>> companionsOf(const fs::path& file);

struct Entry {
    fs::path path;
    std::string folder;  ///< its folder relative to the set ("" for the set itself), with "/"
    std::string name;    ///< the file name without its extension
    qint64 added = 0;    ///< when it was made (ms since the epoch; the modification time where birth is unknown)
    bool picture = false;
};
/// Every sticker of a set (its subfolders too), in no particular order. A picture of the name of a .xopp beside it is
/// that sticker's (not one of its own).
std::vector<Entry> list(const fs::path& set, Kind kind = Kind::Stickers);
/// The set's folders (relative, with "/"), sorted; not the set itself, not hidden folders
QStringList folders(const fs::path& set);

/// A file for a new sticker named `name` in `folder`: "name.ext", or "name (2).ext", … when taken (by a sticker of any
/// kind of that name, or the attached PDF of a .xopp of that name)
fs::path uniqueTarget(const fs::path& folder, const std::string& name, const std::string& ext = ".xopp");

// --- the own order --------------------------------------------------------------------------------------------
/// The file names of a folder's own order, as written (some may be gone)
std::vector<std::string> readOrder(const fs::path& folder, Kind kind = Kind::Stickers);
bool writeOrder(const fs::path& folder, const std::vector<std::string>& names, Kind kind = Kind::Stickers);
/// The folder's stickers in their own order: those in the order file first, then the others by name
std::vector<Entry> ordered(std::vector<Entry> entries, Kind kind = Kind::Stickers);
/// A sticker one place earlier (delta -1) or later (+1) in its folder's own order (written out whole). False: it is
/// first or last already, or the file could not be written.
bool moveInOrder(const fs::path& sticker, int delta, Kind kind = Kind::Stickers);

// --- last used (per library, on this device) ---------------------------------------------------------------------
class LastUsed {
public:
    explicit LastUsed(fs::path file = {});
    qint64 when(const fs::path& sticker) const;
    void use(const fs::path& sticker, qint64 msecs);
    void moved(const fs::path& from, const fs::path& to);
    const fs::path& file() const { return path; }

private:
    void load();
    void save() const;
    fs::path path;
    std::map<std::string, qint64> used;
};

// --- changes (the order file and the files of a .xopp follow: companionsOf) -------------------------------------
/// Rename a sticker (its file, keeping the extension); the new path, or nothing when the name is taken or invalid
std::optional<fs::path> rename(const fs::path& sticker, const std::string& name, Kind kind = Kind::Stickers);
/// Move a sticker into another folder (of any set; made if needed); a name taken there becomes "name (2)"
std::optional<fs::path> moveTo(const fs::path& sticker, const fs::path& folder, Kind kind = Kind::Stickers);
/// Copy a sticker into a folder (made if needed); a name taken there becomes "name (2)"
std::optional<fs::path> copyTo(const fs::path& sticker, const fs::path& folder);
/// The sticker to the trash (deleted where there is none)
bool trash(const fs::path& sticker, Kind kind = Kind::Stickers);

}  // namespace xqt::stickers

namespace xqt {

class StickersModel final: public QAbstractListModel {
    Q_OBJECT
    /// "stickers" or "templates" (the picker's mode)
    Q_PROPERTY(QString kind READ kindName CONSTANT)
    /// "library" (the library's Stickers folder) or "app" (the app-wide set, "All libraries")
    Q_PROPERTY(QString scope READ scope WRITE setScope NOTIFY scopeChanged)
    /// A folder of the set (relative; "": all of it)
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    /// "used", "own", "name", "added"
    Q_PROPERTY(QString sort READ sort WRITE setSort NOTIFY sortChanged)
    /// The folders of the set in scope
    Q_PROPERTY(QStringList folders READ folderList NOTIFY listChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY listChanged)
    /// Whether there is a library (else only the app-wide set)
    Q_PROPERTY(bool hasLibrary READ hasLibrary NOTIFY scopeChanged)
    /// The library's Stickers (Templates) folder ("": no library): its card in the library shows the mark
    Q_PROPERTY(QString libraryFolder READ libraryFolder NOTIFY scopeChanged)
    /// The set in scope has no sticker at all (not: none found)
    Q_PROPERTY(bool setEmpty READ setEmpty NOTIFY listChanged)
public:
    enum Roles { NameRole = Qt::UserRole + 1, PathRole, FolderRole, PreviewRole, PictureRole };
    explicit StickersModel(stickers::Kind kind = stickers::Kind::Stickers, QObject* parent = nullptr);
    stickers::Kind kind() const { return setKind; }
    QString kindName() const {
        return setKind == stickers::Kind::Templates ? QStringLiteral("templates") : QStringLiteral("stickers");
    }

    /// The library (its root, and its config folder for the last uses; empty: none)
    void setLibrary(const fs::path& root, const fs::path& configDir);
    fs::path rootOf(const QString& scope) const;
    fs::path currentSet() const { return rootOf(scopeName); }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString scope() const { return scopeName; }
    void setScope(const QString& scope);
    QString folder() const { return folderName; }
    void setFolder(const QString& folder);
    QString search() const { return searchText; }
    void setSearch(const QString& text);
    QString sort() const { return sortName; }
    void setSort(const QString& sort);
    QStringList folderList() const { return folderNames; }
    bool hasLibrary() const { return !libraryRoot.empty(); }
    bool setEmpty() const { return allCount == 0; }
    QString libraryFolder() const {
        return libraryRoot.empty() ? QString()
                                   : QString::fromStdString(stickers::librarySet(libraryRoot, setKind).string());
    }

    /// Read the set again
    Q_INVOKABLE void refresh();
    /// The path of row `row` ("" if none)
    Q_INVOKABLE QString pathAt(int row) const;
    /// A sticker was used now
    Q_INVOKABLE void markUsed(const QString& path);
    /// The sticker last used of both sets, the last first, at most `count`: [{name, path}] (still there)
    Q_INVOKABLE QVariantList recent(int count) const;
    /// One place earlier / later in its folder's own order (the list shows the own order then)
    Q_INVOKABLE bool moveBy(const QString& path, int delta);
    Q_INVOKABLE bool rename(const QString& path, const QString& name);
    /// Into a folder of the same set ("": its root; a new name makes it)
    Q_INVOKABLE bool moveToFolder(const QString& path, const QString& folder);
    /// A copy in the other set (the library's <-> the app-wide one), in the same folder there
    Q_INVOKABLE bool copyToOtherSet(const QString& path);
    /// A copy in another library's Stickers (Templates) folder (its root)
    Q_INVOKABLE bool copyToLibrary(const QString& path, const QString& libraryRoot);
    Q_INVOKABLE bool remove(const QString& path);
    /// The set a path is in: "library", "app" or ""
    Q_INVOKABLE QString scopeOf(const QString& path) const;

Q_SIGNALS:
    void scopeChanged();
    void folderChanged();
    void searchChanged();
    void sortChanged();
    void listChanged();

private:
    void apply();

    stickers::Kind setKind = stickers::Kind::Stickers;
    fs::path libraryRoot;
    stickers::LastUsed lastUsed;
    QString scopeName = QStringLiteral("library");
    QString folderName;
    QString searchText;
    QString sortName = QStringLiteral("used");
    QStringList folderNames;
    std::vector<stickers::Entry> all;  ///< of the set in scope
    std::vector<stickers::Entry> shown;
    size_t allCount = 0;
};

}  // namespace xqt
