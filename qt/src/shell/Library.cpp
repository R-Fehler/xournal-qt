#include "Library.h"

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <set>
#include <shared_mutex>

#include <QCborArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThreadPool>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfPage.h"
#include "session/DocumentSession.h"
#include "session/FuzzyQuery.h"
#include "session/TextMatch.h"
#include "session/Vocabulary.h"
#include "util/PathUtil.h"

#include "MarkdownFile.h"
#include "MdPassages.h"
#include "Previews.h"

namespace xqt {

namespace {
QString hashOf(const std::string& s, int length) {
    return QString::fromLatin1(
            QCryptographicHash::hash(QByteArray::fromStdString(s), QCryptographicHash::Sha1).toHex().left(length));
}
fs::path normalized(const fs::path& p) {
    std::error_code ec;
    fs::path n = fs::weakly_canonical(fs::absolute(p, ec), ec);
    if (ec) {
        std::error_code aec;  // (never throws, also not for an empty path)
        n = fs::absolute(p, aec).lexically_normal();
    }
    if (!n.has_filename() && n.has_parent_path() && n != n.root_path()) {
        n = n.parent_path();
    }
    return n;
}
}  // namespace

Library::Library(const fs::path& root): rootDir(normalized(root)) {}

namespace {
#ifdef Q_OS_ANDROID
/// A folder of android.os.Environment: getExternalStoragePublicDirectory(<type>), or ("") the storage itself.
fs::path environmentFolder(const char* type) {
    QJniObject dir;
    if (*type) {
        dir = QJniObject::callStaticObjectMethod("android/os/Environment", "getExternalStoragePublicDirectory",
                                                 "(Ljava/lang/String;)Ljava/io/File;",
                                                 QJniObject::fromString(QString::fromLatin1(type)).object<jstring>());
    } else {
        dir = QJniObject::callStaticObjectMethod("android/os/Environment", "getExternalStorageDirectory",
                                                 "()Ljava/io/File;");
    }
    if (!dir.isValid()) {
        return {};
    }
    return fs::path(dir.callObjectMethod("getAbsolutePath", "()Ljava/lang/String;").toString().toStdString());
}
#endif

struct Platform {
    std::mutex mtx;
    std::optional<PlatformFolders> folders;
    Library::Home home = Library::Home::App;
};
Platform& platform() {
    static Platform p;
    return p;
}
}  // namespace

PlatformFolders PlatformFolders::detect() {
    PlatformFolders f;
    f.appDocuments = fs::path(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation).toStdString());
    f.appDownloads = fs::path(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation).toStdString());
#ifdef Q_OS_ANDROID
    // (the names of Environment.DIRECTORY_DOCUMENTS and DIRECTORY_DOWNLOADS; asked once)
    static const std::array<fs::path, 3> shared{environmentFolder("Documents"), environmentFolder("Download"),
                                                environmentFolder("")};
    f.sharedDocuments = shared[0];
    f.sharedDownloads = shared[1];
    f.sharedStorage = shared[2];
#endif
    return f;
}

PlatformFolders Library::platformFolders() {
    // (detected each time: the desktop's folders can change while the app runs, e.g. user-dirs.dirs)
    {
        auto& p = platform();
        std::lock_guard lock(p.mtx);
        if (p.folders) {
            return *p.folders;
        }
    }
    return PlatformFolders::detect();
}

void Library::setPlatformFolders(const PlatformFolders* folders) {
    auto& p = platform();
    std::lock_guard lock(p.mtx);
    if (folders) {
        p.folders = *folders;
    } else {
        p.folders.reset();
    }
    p.home = Home::App;
}

Library::Home Library::home() {
    const bool shared = !platformFolders().sharedDocuments.empty();
    auto& p = platform();
    std::lock_guard lock(p.mtx);
    return shared ? p.home : Home::App;
}

void Library::setHome(Home home) {
    auto& p = platform();
    std::lock_guard lock(p.mtx);
    p.home = home;
}

Library::Home Library::chooseHome(const PlatformFolders& folders, bool access, bool moved) {
    return !folders.sharedDocuments.empty() && access && moved ? Home::Shared : Home::App;
}

fs::path Library::librariesFolder() { return librariesFolder(home()); }

fs::path Library::librariesFolder(Home home) {
    const PlatformFolders f = platformFolders();
    const bool shared = home == Home::Shared && !f.sharedDocuments.empty();
    return (shared ? f.sharedDocuments : f.appDocuments) / "Xournal_Libraries";
}

fs::path Library::defaultRoot() { return librariesFolder() / "Default"; }

fs::path Library::downloadsFolder() {
    // Android: the phone's Download folder, where the browser and the other apps put downloads (the app's own
    // "Download" folder stays empty)
    const PlatformFolders f = platformFolders();
    return f.sharedDownloads.empty() ? f.appDownloads : f.sharedDownloads;
}

bool Library::isTemporary() const {
    const fs::path downloads = normalized(downloadsFolder());
    return !downloads.empty() && DocumentFiles::remap(rootDir, downloads, "/") != rootDir;
}

QString Library::name() const { return QString::fromStdString(rootDir.filename().string()); }

bool Library::isInLibrariesFolder() const {
    const fs::path folder = normalized(librariesFolder());
    return rootDir != folder && DocumentFiles::remap(rootDir, folder, "/") != rootDir;
}

bool Library::isDefault() const { return rootDir == normalized(defaultRoot()); }

std::string Library::key() const { return hashOf(rootDir.string(), 12).toStdString(); }

fs::path Library::configDir() const { return Util::getConfigSubfolder(fs::path("libraries") / key()); }

namespace {
QJsonObject settingsOf(const fs::path& file) {
    QFile f(QString::fromStdString(file.string()));
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}
}  // namespace

namespace {
#ifdef Q_OS_ANDROID
CacheLocation::Mode platformCacheMode = CacheLocation::Mode::AppCache;
#else
CacheLocation::Mode platformCacheMode = CacheLocation::Mode::Folders;
#endif
}  // namespace

CacheLocation::Mode Library::defaultCacheMode() { return platformCacheMode; }
void Library::setDefaultCacheMode(CacheLocation::Mode mode) { platformCacheMode = mode; }

CacheLocation::Mode Library::cacheMode() const {
    const QString mode = settingsOf(configDir() / "library.json").value("cache").toString();
    return mode == QLatin1String("app")       ? CacheLocation::Mode::AppCache
           : mode == QLatin1String("folders") ? CacheLocation::Mode::Folders
                                              : defaultCacheMode();
}

bool Library::hasCacheSetting() const {
    return settingsOf(configDir() / "library.json").value("cache").isString();
}

namespace {
/// Change the settings of a library in its "library.json" (the others stay as they are).
void changeSettings(const fs::path& file, const fs::path& root, const std::function<void(QJsonObject&)>& change) {
    QJsonObject settings = settingsOf(file);
    settings["root"] = QString::fromStdString(root.string());  // (for people looking at the folder)
    change(settings);
    QSaveFile f(QString::fromStdString(file.string()));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(settings).toJson());
        if (f.flush()) {  // (see LibraryCache.cpp, writeFile)
            f.commit();
        }
    }
}
}  // namespace

void Library::setCacheMode(CacheLocation::Mode mode) const {
    changeSettings(configDir() / "library.json", rootDir, [mode](QJsonObject& settings) {
        settings["cache"] = mode == CacheLocation::Mode::AppCache ? "app" : "folders";
    });
}

ShowFilter Library::showFilter() const {
    const QJsonObject show = settingsOf(configDir() / "library.json").value("show").toObject();
    ShowFilter f;
    auto read = [&](const char* key, bool& value) { value = show.value(QLatin1String(key)).toBool(value); };
    read("notes", f.notes);
    read("pdfs", f.pdfs);
    read("onlyPdfsWithNotes", f.onlyPdfsWithNotes);
    read("markdown", f.markdown);
    read("images", f.images);
    read("text", f.text);
    read("other", f.other);
    return f;
}

void Library::setShowFilter(const ShowFilter& f) const {
    changeSettings(configDir() / "library.json", rootDir, [&f](QJsonObject& settings) {
        settings["show"] = QJsonObject{{"notes", f.notes},   {"pdfs", f.pdfs}, {"onlyPdfsWithNotes", f.onlyPdfsWithNotes},
                                       {"markdown", f.markdown}, {"images", f.images}, {"text", f.text},
                                       {"other", f.other}};
    });
}

fs::path Library::placesFile() const {
    const fs::path file = configDir() / "pages.json";
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        // Kept in the library's cache folder before (lost when it was removed): taken over, once. The old file
        // goes when the old cache is converted.
        for (const fs::path& old: {rootDir / DocumentFiles::META_DIR / "pages.json",
                                   CacheLocation(rootDir).appCacheDir() / "pages.json"}) {
            if (fs::exists(old, ec)) {
                fs::copy_file(old, file, ec);
                break;
            }
        }
    }
    return file;
}

bool Library::contains(const fs::path& p) const { return DocumentFiles::remap(p, rootDir, "/") != p; }

std::string Library::relative(const fs::path& p) const {
    const std::string rel = p.lexically_normal().lexically_relative(rootDir).string();
    return rel == "." ? std::string() : rel;
}

QString fileStamp(const fs::path& file) {
    if (file.empty()) {
        return {};
    }
    const QFileInfo info(QString::fromStdString(file.string()));
    if (!info.exists()) {
        return {};
    }
    return QString::number(info.size()) + ':' + QString::number(info.lastModified().toMSecsSinceEpoch());
}

QString documentStamp(const DocumentItem& item) {
    QString stamp;
    const fs::path none;
    for (const fs::path& f: {item.xopp, item.pdf, item.xopp.empty() ? none : DocumentFiles::attachmentOf(item.xopp),
                             item.xopp.empty() ? none : DocumentFiles::pagesOf(item.xopp), item.md, item.image,
                             item.other}) {
        if (!f.empty()) {
            stamp += fileStamp(f) + ';';
        }
    }
    return stamp;
}

// --- LibraryIndex ---------------------------------------------------------------------------------------------

const QString LibraryIndex::NOTES_PACK = QStringLiteral("notes");
const QString LibraryIndex::PDF_TEXT_PACK = QStringLiteral("pdf-text");

namespace {
QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }
fs::path toPath(const QString& s) { return fs::path(s.toStdString()); }
/// The stamp of the file an entry reads itself (its "xopp" stamp): the .xopp, a Markdown file, a lone image; a lone
/// PDF has none (its PDF stamp).
QString ownStamp(const DocumentItem& item) {
    if (!item.xopp.empty()) {
        return fileStamp(item.xopp);
    }
    return item.pdf.empty() ? fileStamp(item.main()) : QString();
}
/// The kind of an entry: what it read ("xopp" also for .xoj, "pdf", "md", "image", "text").
QString entryKind(const DocumentItem& item) {
    if (!item.xopp.empty()) {
        return QStringLiteral("xopp");
    }
    return !item.pdf.empty()   ? QStringLiteral("pdf")
           : !item.md.empty()  ? QStringLiteral("md")
           : !item.other.empty() ? QStringLiteral("text")
                                 : QStringLiteral("image");
}
/// A hash of the start and end of a file (all of a small one), with its size: two files with the same size and time
/// are not taken for each other unless it is the same too ("": cannot be read).
QString contentSample(const fs::path& file) {
    constexpr qint64 PART = 64 * 1024;
    QFile f(qstr(file));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha1);
    const qint64 size = f.size();
    hash.addData(QByteArray::number(size));
    if (size <= 2 * PART) {
        hash.addData(f.readAll());
    } else {
        hash.addData(f.read(PART));
        if (!f.seek(size - PART)) {
            return {};
        }
        hash.addData(f.read(PART));
    }
    return QString::fromLatin1(hash.result().toBase64(QByteArray::OmitTrailingEquals));
}
/// Entries without pages: a Markdown file, a text file, an image
bool pageless(const QString& kind) {
    return kind == QLatin1String("md") || kind == QLatin1String("text") || kind == QLatin1String("image");
}
}  // namespace

LibraryIndex::LibraryIndex(fs::path root, CacheLocation location, QObject* parent):
        QObject(parent),
        rootDir(std::move(root)),
        where(location.valid() ? std::move(location) : CacheLocation(rootDir)),
        pool(std::make_unique<QThreadPool>()),
        writer(std::make_unique<QThreadPool>()) {
    pool->setMaxThreadCount(1);  // in the background, one document after the other (also orders moves and updates)
    writer->setMaxThreadCount(1);
    scheduler = std::make_unique<WriteScheduler>([this] { writer->start([this] { writeChanged(); }); });
}

LibraryIndex::~LibraryIndex() {
    ++generation;  // stops a running update
    pool->waitForDone();
    writer->waitForDone();
    scheduler->cancel();
    writeChanged();
}

void LibraryIndex::waitForDone() { pool->waitForDone(); }

void LibraryIndex::flush() {
    pool->waitForDone();
    writer->waitForDone();
    scheduler->cancel();
    writeChanged();
}

void LibraryIndex::setWriteDelays(int quietMs, int maxDelayMs) { scheduler->setDelays(quietMs, maxDelayMs); }

void LibraryIndex::discard() {
    discarded = true;
    ++generation;  // stops a running update
    pool->waitForDone();
    writer->waitForDone();
    scheduler->cancel();
    std::lock_guard lock(mtx);
    folders.clear();
    running = false;
}

void LibraryIndex::update(std::vector<DocumentItem> items) {
    if (discarded) {
        return;
    }
    const quint64 gen = ++generation;
    running = true;
    doneCount = 0;
    totalCount = static_cast<int>(items.size());
    Q_EMIT progress();
    pool->start([this, items = std::move(items), gen]() mutable { run(std::move(items), gen); });
}

void LibraryIndex::moved(const std::vector<std::pair<fs::path, fs::path>>& moves) {
    if (!moves.empty() && !discarded) {
        pool->start([this, moves] { applyMoves(moves); });
    }
}

bool LibraryIndex::Entry::showsPdfPages() const {
    return std::any_of(pdfPage.begin(), pdfPage.end(), [](int p) { return p >= 0; });
}

bool LibraryIndex::Entry::upToDate(const DocumentItem& item) const {
    return file == item.main() && xoppStamp == ownStamp(item) && pdfStamp == fileStamp(pdf) && linksRead;
}

// --- the packs: entries by file name

QCborMap LibraryIndex::notesOf(const Entry& e) const {
    const fs::path folder = e.file.parent_path();
    // The PDF relative to the folder when it is in the library (next to it: its name), so a moved library or
    // folder keeps it; else where it is.
    QString pdf;
    if (!e.pdf.empty()) {
        pdf = where.contains(e.pdf) ? qstr(e.pdf.lexically_normal().lexically_relative(folder)) : qstr(e.pdf);
    }
    QCborArray pdfPages, text, aspects;
    for (int i = 0; i < e.pageCount(); ++i) {
        pdfPages.append(e.pdfPage[static_cast<size_t>(i)]);
        text.append(e.elementText[i]);
        aspects.append(e.aspects[static_cast<size_t>(i)]);
    }
    QCborMap notes{{QStringLiteral("kind"), e.kind},       {QStringLiteral("name"), e.name},
                   {QStringLiteral("xopp"), e.xoppStamp},  {QStringLiteral("pdf"), pdf},
                   {QStringLiteral("pdfStamp"), e.pdfStamp}, {QStringLiteral("pdfPages"), pdfPages},
                   {QStringLiteral("text"), text},         {QStringLiteral("aspects"), aspects}};
    if (!e.sample.isEmpty()) {
        notes.insert(QStringLiteral("sample"), e.sample);
    }
    if (e.kind == QLatin1String("md")) {
        QCborArray levels;
        for (int level: e.blockLevel) {
            levels.append(level);
        }
        notes.insert(QStringLiteral("blocks"), QCborArray::fromStringList(e.blockText));
        notes.insert(QStringLiteral("levels"), levels);
        notes.insert(QStringLiteral("links"), QCborArray::fromStringList(e.links));
        notes.insert(QStringLiteral("wikiLinks"), QCborArray::fromStringList(e.wikiLinks));
    } else if (e.kind == QLatin1String("text")) {
        notes.insert(QStringLiteral("blocks"), QCborArray::fromStringList(e.blockText));
    } else if (e.kind != QLatin1String("image")) {
        // Notes (and hybrid PDFs): the links of their Markdown boxes and link markers
        notes.insert(QStringLiteral("links"), QCborArray::fromStringList(e.links));
        notes.insert(QStringLiteral("wikiLinks"), QCborArray::fromStringList(e.wikiLinks));
    }
    return notes;
}

namespace {
QCborMap pdfTextOf(const QString& stamp, const std::map<int, QString>& pages) {
    QCborMap text;
    for (const auto& [page, t]: pages) {
        text.insert(page, t);
    }
    return QCborMap{{QStringLiteral("stamp"), stamp}, {QStringLiteral("pages"), text}};
}
}  // namespace

std::shared_ptr<LibraryIndex::Entry> LibraryIndex::entryOf(const fs::path& folder, const QString& name,
                                                           const QCborMap& notes, const QCborValue& text) const {
    auto e = std::make_shared<Entry>();
    e->file = folder / toPath(name);
    e->kind = notes.value(QStringLiteral("kind")).toString();
    e->name = notes.value(QStringLiteral("name")).toString();
    e->xoppStamp = notes.value(QStringLiteral("xopp")).toString();
    if (const fs::path pdf = toPath(notes.value(QStringLiteral("pdf")).toString()); !pdf.empty()) {
        e->pdf = pdf.is_absolute() ? pdf : (folder / pdf).lexically_normal();
    }
    e->pdfStamp = notes.value(QStringLiteral("pdfStamp")).toString();
    e->sample = notes.value(QStringLiteral("sample")).toString();
    const QCborArray pdfPages = notes.value(QStringLiteral("pdfPages")).toArray();
    const QCborArray texts = notes.value(QStringLiteral("text")).toArray();
    const QCborArray aspects = notes.value(QStringLiteral("aspects")).toArray();
    if (pdfPages.size() != texts.size() || aspects.size() != texts.size()) {
        return nullptr;
    }
    for (qsizetype i = 0; i < texts.size(); ++i) {
        e->pdfPage.push_back(static_cast<int>(pdfPages[i].toInteger(-1)));
        e->elementText << texts[i].toString();
        e->aspects.push_back(aspects[i].toDouble());
    }
    if (e->kind == QLatin1String("md")) {
        const QCborArray blocks = notes.value(QStringLiteral("blocks")).toArray();
        const QCborArray levels = notes.value(QStringLiteral("levels")).toArray();
        if (blocks.size() != levels.size()) {
            return nullptr;
        }
        for (qsizetype i = 0; i < blocks.size(); ++i) {
            e->blockText << blocks[i].toString();
            e->blockLevel.push_back(static_cast<int>(levels[i].toInteger()));
        }
        for (const auto& l: notes.value(QStringLiteral("links")).toArray()) {
            e->links << l.toString();
        }
        for (const auto& l: notes.value(QStringLiteral("wikiLinks")).toArray()) {
            e->wikiLinks << l.toString();
        }
    } else if (e->kind == QLatin1String("text")) {
        for (const auto& b: notes.value(QStringLiteral("blocks")).toArray()) {
            e->blockText << b.toString();
            e->blockLevel.push_back(0);
        }
    } else if (e->kind != QLatin1String("image")) {
        e->linksRead = notes.contains(QStringLiteral("links")) || e->kind != QLatin1String("xopp");
        for (const auto& l: notes.value(QStringLiteral("links")).toArray()) {
            e->links << l.toString();
        }
        for (const auto& l: notes.value(QStringLiteral("wikiLinks")).toArray()) {
            e->wikiLinks << l.toString();
        }
    }
    if (e->showsPdfPages()) {
        const QCborMap t = text.toMap();
        if (t.value(QStringLiteral("stamp")).toString() == e->pdfStamp && !e->pdfStamp.isEmpty()) {
            const QCborMap pages = t.value(QStringLiteral("pages")).toMap();
            for (auto it = pages.cbegin(); it != pages.cend(); ++it) {
                e->pdfText[static_cast<int>(it.key().toInteger())] = it.value().toString();
            }
        } else {
            e->pdfStamp.clear();  // its PDF text is missing: read again
        }
    }
    return e;
}

void LibraryIndex::load(const fs::path& folder) {
    {
        std::lock_guard lock(mtx);
        if (auto it = folders.find(folder); it != folders.end() && it->second.loaded) {
            return;
        }
    }
    // Where the library keeps it; else where it may have been kept before (a folder that cannot be written now,
    // or the other cache location)
    fs::path dir = where.dirOf(folder);
    auto notes = Packs::read(dir, NOTES_PACK, FORMAT);
    if (!notes) {
        const fs::path other = dir == where.inFolder(folder) ? where.mirrorOf(folder) : where.inFolder(folder);
        if (!other.empty() && (notes = Packs::read(other, NOTES_PACK, FORMAT))) {
            dir = other;
        }
    }
    std::map<std::string, EntryPtr> stored;
    if (notes) {
        const QCborMap text = Packs::read(dir, PDF_TEXT_PACK, FORMAT).value_or(QCborMap());
        for (auto it = notes->cbegin(); it != notes->cend(); ++it) {
            const QString name = it.key().toString();
            if (auto e = entryOf(folder, name, it.value().toMap(), text.value(name))) {
                stored[name.toStdString()] = std::move(e);
            }
        }
    }
    std::lock_guard lock(mtx);
    Folder& f = folders[folder];
    if (!f.loaded) {
        f.loaded = true;
        for (auto& [name, e]: f.docs) {
            stored[name] = e;  // (entries put meanwhile win)
        }
        f.docs = std::move(stored);
    }
}

LibraryIndex::EntryPtr LibraryIndex::find(const fs::path& file) const {
    auto f = folders.find(file.parent_path());
    if (f == folders.end()) {
        return nullptr;
    }
    auto it = f->second.docs.find(file.filename().string());
    return it == f->second.docs.end() ? nullptr : it->second;
}

void LibraryIndex::put(const EntryPtr& e) {
    Folder& f = folders[e->file.parent_path()];
    EntryPtr& slot = f.docs[e->file.filename().string()];
    f.notesChanged = true;
    if (!slot || slot->pdfStamp != e->pdfStamp || slot->pdfText.size() != e->pdfText.size()) {
        f.textChanged = true;
        f.changedText.insert(qstr(e->file.filename()));
    }
    slot = e;
    scheduler->changed();
}

void LibraryIndex::erase(const fs::path& file) {
    auto f = folders.find(file.parent_path());
    if (f != folders.end() && f->second.docs.erase(file.filename().string())) {
        f->second.notesChanged = f->second.textChanged = true;
        scheduler->changed();
    }
}

bool LibraryIndex::writeChanged() {
    std::lock_guard writeLock(writeMtx);
    if (discarded) {
        return true;
    }
    struct Job {
        fs::path folder;
        std::vector<EntryPtr> docs;
        bool notes = false, text = false;
        std::set<QString> changedText;
    };
    std::vector<Job> jobs;
    {
        std::lock_guard lock(mtx);
        for (auto it = folders.begin(); it != folders.end(); ++it) {
            Folder& f = it->second;
            if (f.loaded && (f.notesChanged || f.textChanged)) {
                Job job{it->first, {}, f.notesChanged, f.textChanged, std::move(f.changedText)};
                for (const auto& [name, e]: f.docs) {
                    job.docs.push_back(e);
                }
                f.notesChanged = f.textChanged = false;
                f.changedText.clear();
                jobs.push_back(std::move(job));
            }
        }
    }
    bool ok = true;
    for (const Job& job: jobs) {
        const fs::path dir = where.dirOf(job.folder);
        if (job.docs.empty()) {
            // Its last document is gone: its cache folder goes too (unless something else is in it)
            for (const QString& pack: {NOTES_PACK, PDF_TEXT_PACK, PreviewCache::PACK, PreviewCache::STAMPS_PACK}) {
                Packs::remove(dir, pack);
            }
            if (Packs::removeIfOnlyOurs(dir)) {
                removeEmptyMirrors(dir.parent_path());
            }
            continue;
        }
        if (job.notes) {
            QCborMap notes;
            for (const auto& e: job.docs) {
                notes.insert(qstr(e->file.filename()), notesOf(*e));
            }
            ok = Packs::write(dir, NOTES_PACK, FORMAT, notes, true) && ok;
            ++packWrites;
        }
        if (job.text) {
            QCborMap text;
            for (const auto& e: job.docs) {
                if (!e->pdfText.empty()) {
                    text.insert(qstr(e->file.filename()), pdfTextOf(e->pdfStamp, e->pdfText));
                }
            }
            ok = Packs::write(dir, PDF_TEXT_PACK, FORMAT, text, true, &job.changedText) && ok;
            ++packWrites;
        }
    }
    return ok;
}

void LibraryIndex::removeEmptyMirrors(fs::path dir) const {
    // Only folders in the library's folder in the app cache, not that folder itself
    const fs::path app = where.appCacheDir();
    std::error_code ec;
    while (dir != app && DocumentFiles::remap(dir, app, "/") != dir && fs::is_empty(dir, ec) && !ec) {
        fs::remove(dir, ec);
        dir = dir.parent_path();
    }
}

// --- the layout before the packs

bool LibraryIndex::hasOldLayout(const fs::path& dir) {
    std::error_code ec;
    return !dir.empty() && (fs::is_directory(dir / "index", ec) || fs::is_directory(dir / "previews", ec) ||
                            fs::exists(dir / "pages.json", ec));
}

void LibraryIndex::convertOldLayout(const fs::path& dir) {
    pool->start([this, dir] { convert(dir); });
}

void LibraryIndex::convert(const fs::path& dir) {
    // The index: "index/<hash>.json" per document, format 3, paths relative to the library
    std::map<fs::path, std::vector<EntryPtr>> byFolder;
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir / "index", ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (it->path().extension() != ".json") {
            continue;
        }
        QFile f(qstr(it->path()));
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonObject json = QJsonDocument::fromJson(f.readAll()).object();
        const fs::path rel = toPath(json["file"].toString());
        if (json["format"].toInt() != 3 || rel.empty() || rel.is_absolute()) {
            continue;  // (another format: read again, as it would have been)
        }
        const fs::path file = (rootDir / rel).lexically_normal();
        if (std::error_code fec; !fs::exists(file, fec) || !where.contains(file)) {
            continue;
        }
        auto e = std::make_shared<Entry>();
        e->file = file;
        const std::string ext = file.extension().string();
        e->kind = ext == ".xopp" || ext == ".xoj" ? QStringLiteral("xopp") : QStringLiteral("pdf");
        e->name = json["name"].toString();
        e->xoppStamp = json["xoppStamp"].toString();
        const fs::path pdf = toPath(json["pdf"].toString());
        e->pdf = pdf.empty() || pdf.is_absolute() ? pdf : (rootDir / pdf).lexically_normal();
        e->pdfStamp = json["pdfStamp"].toString();
        const QJsonObject pdfText = json["pdfText"].toObject();
        for (auto t = pdfText.begin(); t != pdfText.end(); ++t) {
            e->pdfText[t.key().toInt()] = t.value().toString();
        }
        for (const auto& page: json["pages"].toArray()) {
            const QJsonObject o = page.toObject();
            e->pdfPage.push_back(o["pdf"].toInt(-1));
            e->elementText << o["text"].toString();
            e->aspects.push_back(o["aspect"].toDouble());
        }
        byFolder[file.parent_path()].push_back(std::move(e));
    }
    for (const auto& [folder, entries]: byFolder) {
        load(folder);
        std::lock_guard lock(mtx);
        for (const auto& e: entries) {
            if (!find(e->file)) {  // (packs written since win)
                put(e);
            }
        }
    }
    // The previews: "previews/<hash>.png", named by the document's path and files
    PreviewCache::convertOldFiles(dir / "previews", DocumentFiles::scanRecursive(rootDir));
    // Written: the old files go (only those)
    const bool written = writeChanged();
    if (PreviewCache::flush() && written) {
        Packs::removeOldLayout(dir);
        ++conversions;
    }
}

// --- reading documents

std::shared_ptr<LibraryIndex::Entry> LibraryIndex::read(const DocumentItem& item, const EntryPtr& previous) {
    auto e = std::make_shared<Entry>();
    e->file = item.main();
    e->kind = entryKind(item);
    e->name = QString::fromStdString(item.name());
    e->xoppStamp = ownStamp(item);
    e->sample = contentSample(item.main());
    auto gone = [&] {
        std::error_code ec;
        return !fs::exists(item.main(), ec);
    };
    if (!item.md.empty()) {
        // Plain text: its passages through md4c, without the syntax
        const md::Document doc = md::parse(MarkdownFile::read(item.md));
        if (gone()) {
            return nullptr;
        }
        ++docsRead;
        for (const md::Passage& p: md::passages(doc)) {
            e->blockText << simplified(QString::fromStdString(p.text));
            e->blockLevel.push_back(p.kind == md::Passage::Kind::Heading ? p.level : 0);
        }
        for (const md::LinkTarget& l: md::linksOf(doc)) {
            (l.wiki ? e->wikiLinks : e->links) << QString::fromStdString(l.target);
        }
        return e;
    }
    if (!item.other.empty()) {
        // A text file: its text (a big one: its name)
        QFile f(qstr(item.other));
        if (!f.open(QIODevice::ReadOnly) && gone()) {
            return nullptr;
        }
        ++docsRead;
        if (f.isOpen() && f.size() <= TEXT_LIMIT) {
            QByteArray bytes = f.readAll();
            if (bytes.startsWith("\xEF\xBB\xBF")) {
                bytes.remove(0, 3);
            }
            if (!bytes.contains('\0')) {  // (not text after all)
                e->blockText << simplified(QString::fromUtf8(bytes));
                e->blockLevel.push_back(0);
            }
        }
        return e;
    }
    if (item.xopp.empty() && item.pdf.empty()) {
        if (gone()) {
            return nullptr;
        }
        ++docsRead;
        return e;  // an image: its name
    }
    auto loaded = DocumentSession::loadFile(item.main());
    if (!loaded.document && gone()) {
        return nullptr;
    }
    ++docsRead;
    if (!loaded.document) {
        return e;  // unreadable: empty, not read again until it changes
    }
    Document& doc = *loaded.document;
    std::shared_lock lock(doc);
    // A hybrid PDF: its pages are those of the file itself (read from the clean copy in the app cache, whose name
    // changes with every version of the file)
    e->pdf = loaded.hybrid ? item.main() : doc.getPdfFilepath();
    e->pdfStamp = fileStamp(e->pdf);
    fillPages(*e, doc, donorFor(*e, previous), true);
    return e;
}

LibraryIndex::EntryPtr LibraryIndex::donorFor(const Entry& e, const EntryPtr& previous) const {
    // PDF text read before: from this document's last entry, or another one with this PDF (e.g. the PDF of a
    // document that just got its .xopp, or that was moved by another program).
    if (e.pdfStamp.isEmpty()) {
        return nullptr;
    }
    if (previous && previous->pdf == e.pdf && previous->pdfStamp == e.pdfStamp) {
        return previous;
    }
    EntryPtr donor;
    std::lock_guard entriesLock(mtx);
    for (const auto& [folder, f]: folders) {
        for (const auto& [name, other]: f.docs) {
            if (other->pdfStamp == e.pdfStamp && (other->pdf == e.pdf || !donor)) {
                donor = other;  // the same size and time: the same file (renamed), preferably the same path
            }
        }
    }
    return donor;
}

bool LibraryIndex::fillPages(Entry& e, Document& doc, const EntryPtr& donor, bool readMissing) {
    const size_t pdfPages = doc.getPdfPageCount();
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        PageRef page = doc.getPage(i);
        const bool pdfPage = page->getBackgroundType().isPdfPage() && page->getPdfPageNr() < pdfPages;
        const int pdfNr = pdfPage ? static_cast<int>(page->getPdfPageNr()) : -1;  // (a shorter new PDF version)
        e.pdfPage.push_back(pdfNr);
        if (pdfNr >= 0 && !e.pdfText.count(pdfNr)) {
            if (donor && donor->pdfText.count(pdfNr)) {
                e.pdfText[pdfNr] = donor->pdfText.at(pdfNr);
            } else if (!readMissing) {
                return false;
            } else if (XojPdfPageSPtr pdf = doc.getPdfPage(static_cast<size_t>(pdfNr))) {
                const XojPdfRectangle all(0, 0, pdf->getWidth(), pdf->getHeight());
                e.pdfText[pdfNr] = simplified(QString::fromStdString(pdf->selectText(all, XojPdfPageSelectionStyle::Linear)));
                ++pdfRead;
            }
        }
        QString elements;
        for (const Layer* layer: page->getLayers()) {
            for (const auto& el: layer->getElementsView()) {
                if (el->getType() == ELEMENT_TEXT) {
                    const auto* text = static_cast<const Text*>(el);
                    elements += ' ' + QString::fromStdString(text->getText());
                    if (text->isMarkdown()) {
                        // Its links (Markdown boxes, link markers), for backlinks (qt/docs/links.md)
                        for (const md::LinkTarget& l: md::linksOf(md::parse(text->getText()))) {
                            QStringList& into = l.wiki ? e.wikiLinks : e.links;
                            if (const QString t = QString::fromStdString(l.target); !into.contains(t)) {
                                into << t;
                            }
                        }
                    }
                }
            }
        }
        e.elementText << simplified(elements);
        e.aspects.push_back(page->getWidth() > 0 ? page->getHeight() / page->getWidth() : 0);
    }
    return true;
}

bool LibraryIndex::documentSaved(const fs::path& file, Document& doc, const std::map<int, QString>& pdfText) {
    const DocumentItem item = DocumentFiles::itemOf(file);
    if (discarded || !item.valid() || item.xopp.empty() || !where.contains(item.main())) {
        return false;
    }
    EntryPtr previous;
    {
        std::lock_guard lock(mtx);
        auto f = folders.find(item.main().parent_path());
        if (f == folders.end() || !f->second.loaded) {
            return false;  // (its packs are read first: the next update reads it)
        }
        previous = find(item.main());
    }
    auto e = std::make_shared<Entry>();
    e->file = item.main();
    e->kind = QStringLiteral("xopp");
    e->name = QString::fromStdString(item.name());
    e->xoppStamp = fileStamp(item.xopp);
    e->sample = contentSample(item.xopp);
    std::shared_lock lock(doc);
    e->pdf = doc.getPdfFilepath();
    e->pdfStamp = fileStamp(e->pdf);
    // The PDF text the open document knows, and what the index knew before
    auto known = std::make_shared<Entry>();
    known->pdfText = pdfText;
    if (const EntryPtr donor = donorFor(*e, previous)) {
        known->pdfText.insert(donor->pdfText.begin(), donor->pdfText.end());
    }
    if (e->pdfStamp.isEmpty() && !e->pdf.empty()) {
        return false;
    }
    if (!fillPages(*e, doc, known, false)) {
        return false;  // (PDF text that is not known yet: the next update reads it)
    }
    lock.unlock();
    std::lock_guard entriesLock(mtx);
    put(e);
    ++handedOver;
    return true;
}

namespace {
/// What must be the same for a document to be the file of an entry: its kind and the size and time of its own file
/// (a PDF alone: of the PDF). "": nothing to go by.
QString orphanKey(const QString& kind, const QString& stamp) {
    return stamp.isEmpty() ? QString() : kind + '|' + stamp;
}
}  // namespace

LibraryIndex::EntryPtr LibraryIndex::movedHere(const DocumentItem& item, std::multimap<QString, EntryPtr>& orphans,
                                               bool& collected) {
    if (!collected) {
        // Entries whose file is gone, in any folder (once per update, when a document without an entry is found)
        collected = true;
        std::vector<EntryPtr> all;
        {
            std::lock_guard lock(mtx);
            for (const auto& [folder, f]: folders) {
                for (const auto& [name, e]: f.docs) {
                    all.push_back(e);
                }
            }
        }
        for (const auto& e: all) {
            if (std::error_code ec; !fs::exists(e->file, ec)) {
                const bool pdfAlone = e->kind == QLatin1String("pdf");
                if (QString key = orphanKey(e->kind, pdfAlone ? e->pdfStamp : e->xoppStamp); !key.isEmpty()) {
                    orphans.emplace(std::move(key), e);
                }
            }
        }
    }
    const fs::path file = item.main();
    const QString kind = entryKind(item);
    const QString key = orphanKey(kind, kind == QLatin1String("pdf") ? fileStamp(item.pdf) : ownStamp(item));
    if (key.isEmpty()) {
        return nullptr;
    }
    // The same size and time: the same file if it has the same name, or the same content (two different files can
    // have the same size and time; entries without a sample are only found by their name)
    const auto [from, to] = orphans.equal_range(key);
    auto match = to;
    QString sample;
    for (auto it = from; it != to; ++it) {
        const EntryPtr& old = it->second;
        const bool sameName = old->file.filename() == file.filename();
        if (!old->sample.isEmpty()) {
            if (sample.isEmpty()) {
                sample = contentSample(file);
            }
            if (old->sample != sample) {
                continue;
            }
        } else if (!sameName) {
            continue;
        }
        if (match == to || sameName) {
            match = it;
            if (sameName) {
                break;
            }
        }
    }
    if (match == to) {
        return nullptr;
    }
    const EntryPtr old = match->second;
    orphans.erase(match);
    auto e = std::make_shared<Entry>(*old);
    e->file = file;
    e->name = QString::fromStdString(item.name());
    // Its PDF, if it was next to it, came along (under the new name). Whether it is the same file (size and time),
    // the caller sees (Entry::upToDate); if not, it is read, with the PDF text of the entries that still fit.
    const fs::path oldFolder = old->file.parent_path();
    if (old->pdf == old->file) {
        e->pdf = file;
    } else if (!old->pdf.empty() && old->pdf.parent_path() == oldFolder) {
        if (old->pdf == DocumentFiles::attachmentOf(old->file)) {
            e->pdf = DocumentFiles::attachmentOf(file);
        } else if (old->pdf == DocumentFiles::pagesOf(old->file)) {
            e->pdf = DocumentFiles::pagesOf(file);
        } else if (!item.pdf.empty()) {
            e->pdf = item.pdf;
        } else {
            e->pdf = file.parent_path() / old->pdf.filename();
        }
    }
    return e;
}

void LibraryIndex::run(std::vector<DocumentItem> items, quint64 gen) {
    auto notify = [this] { QMetaObject::invokeMethod(this, [this] { Q_EMIT progress(); }, Qt::QueuedConnection); };
    // The stored packs of every folder with documents (once), and at the first update of the others too: their
    // documents may be gone since the last time (the packs go then).
    std::set<fs::path> dirs;
    for (const DocumentItem& item: items) {
        dirs.insert(item.main().parent_path());
    }
    if (firstRun) {
        dirs.insert(rootDir);
        for (const auto& f: DocumentFiles::foldersRecursive(rootDir)) {
            dirs.insert(f);
        }
        // In the app cache also those of folders that are gone (moved or renamed by another program: their
        // documents are found again elsewhere by name, size and time)
        std::error_code ec;
        const fs::path app = where.appCacheDir();
        for (auto it = fs::recursive_directory_iterator(app, ec); !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (it->is_directory() && it->path().filename() == DocumentFiles::META_DIR) {
                const fs::path rel = it->path().parent_path().lexically_relative(app);
                dirs.insert(rel.empty() || rel == "." ? rootDir : (rootDir / rel).lexically_normal());
                it.disable_recursion_pending();
            }
        }
    }
    for (const fs::path& dir: dirs) {
        if (generation != gen) {
            return;  // a newer update takes over
        }
        load(dir);
    }
    firstRun = false;
    notify();

    std::set<fs::path> alive;
    std::multimap<QString, EntryPtr> orphans;
    bool orphansCollected = false;
    int done = 0;
    for (const DocumentItem& item: items) {
        if (generation != gen) {
            return;  // a newer update takes over
        }
        const fs::path file = item.main();
        alive.insert(file);
        if (std::error_code ec; !fs::exists(file, ec)) {
            // Moved or deleted since the list was made: the next update (after the move) takes care of it; its
            // entry stays for the move.
            doneCount = ++done;
            continue;
        }
        if (checkHook) {
            checkHook(file);
        }
        EntryPtr current;
        {
            std::lock_guard lock(mtx);
            current = find(file);
        }
        const bool taken = !current && (current = movedHere(item, orphans, orphansCollected)) != nullptr;
        if (current && current->upToDate(item)) {
            if (taken) {
                std::lock_guard lock(mtx);
                put(current);
            }
        } else if (auto fresh = read(item, current)) {
            std::error_code ec;
            if (fs::exists(file, ec)) {
                std::lock_guard lock(mtx);
                put(std::move(fresh));
            }  // else moved while it was read (its PDF perhaps missing): the entry it had goes with the move
        }
        doneCount = ++done;
        notify();
    }
    // Documents that are gone
    {
        std::lock_guard lock(mtx);
        std::vector<fs::path> gone;
        for (const auto& [folder, f]: folders) {
            for (const auto& [name, e]: f.docs) {
                if (!alive.count(e->file)) {
                    gone.push_back(e->file);
                }
            }
        }
        for (const auto& file: gone) {
            erase(file);
        }
        // Folders without documents whose packs are gone (written) are forgotten
        for (auto it = folders.begin(); it != folders.end();) {
            const Folder& f = it->second;
            it = f.docs.empty() && !f.notesChanged && !f.textChanged ? folders.erase(it) : std::next(it);
        }
    }
    if (generation == gen) {
        running = false;
    }
    notify();
}

void LibraryIndex::applyMoves(const std::vector<std::pair<fs::path, fs::path>>& moves) {
    auto remapAll = [&](fs::path p) {
        for (const auto& [from, to]: moves) {
            p = DocumentFiles::remap(p, from, to);
        }
        return p;
    };
    for (const auto& [from, to]: moves) {
        std::error_code ec;
        if (fs::is_directory(to, ec)) {
            // A folder with its subfolders: their packs came along (the cache folders are in them); in the app
            // cache, their mirrors move the same way. Only the entries in memory get their new paths.
            const fs::path oldMirror = where.mirrorOf(from).parent_path();
            if (!oldMirror.empty() && fs::exists(oldMirror, ec)) {
                const fs::path newMirror = where.mirrorOf(to).parent_path();
                if (newMirror.empty()) {
                    fs::remove_all(oldMirror, ec);  // moved out of the library
                } else {
                    fs::create_directories(newMirror.parent_path(), ec);
                    fs::rename(oldMirror, newMirror, ec);
                }
                removeEmptyMirrors(oldMirror.parent_path());
            }
            std::lock_guard lock(mtx);
            std::vector<fs::path> moved;
            for (const auto& [folder, f]: folders) {
                if (DocumentFiles::remap(folder, from, to) != folder) {
                    moved.push_back(folder);
                }
            }
            for (const fs::path& folder: moved) {
                Folder f = std::move(folders[folder]);
                folders.erase(folder);
                const fs::path target = DocumentFiles::remap(folder, from, to);
                if (!where.contains(target)) {
                    continue;  // into another library: its packs went with it
                }
                for (auto& [name, e]: f.docs) {
                    auto copy = std::make_shared<Entry>(*e);
                    copy->file = target / e->file.filename();
                    copy->pdf = remapAll(e->pdf);
                    e = std::move(copy);
                }
                // Moved without its cache folder (e.g. copied to another disk, which leaves hidden folders
                // behind): written anew
                if (std::error_code pec; f.loaded && !fs::exists(Packs::fileOf(where.dirOf(target), NOTES_PACK), pec)) {
                    f.notesChanged = f.textChanged = true;
                    for (const auto& [name, e]: f.docs) {
                        f.changedText.insert(QString::fromStdString(name));
                    }
                    scheduler->changed();
                }
                folders[target] = std::move(f);
            }
            continue;
        }
        const DocumentItem item = DocumentFiles::itemOf(to, DocumentFiles::TextFiles);
        if (!item.valid() || item.main() != to) {
            continue;  // (the PDF of a pair: the .xopp's move takes care of it)
        }
        // A document: from its folder's packs into those of the new folder. Same content at another place: new
        // paths, the stamps stay. A renamed or moved file keeps its size and time: nothing is read again. A pair's
        // .xopp is written again (the new path of its PDF): the next update reads only the .xopp, the PDF text
        // stays.
        load(from.parent_path());
        if (where.contains(to)) {
            load(to.parent_path());
        }
        std::lock_guard lock(mtx);
        const EntryPtr old = find(from);
        if (!old) {
            continue;  // not indexed yet: the next update reads it
        }
        erase(from);
        if (where.contains(to)) {
            auto e = std::make_shared<Entry>(*old);
            e->file = to;
            e->name = QString::fromStdString(item.name());
            e->pdf = remapAll(old->pdf);
            put(e);  // (with its PDF text, into the new folder's packs)
        }
    }
}

std::vector<LibraryIndex::Hit> LibraryIndex::search(const QString& query) const {
    const QString q = simplified(query).trimmed();
    const QString folded = textmatch::prepare(q);
    std::vector<Hit> hits;
    if (q.isEmpty()) {
        return hits;
    }
    std::vector<EntryPtr> snapshot;
    {
        std::lock_guard lock(mtx);
        for (const auto& [folder, f]: folders) {
            for (const auto& [name, e]: f.docs) {
                snapshot.push_back(e);
            }
        }
    }
    for (const auto& e: snapshot) {
        Hit h;
        h.file = e->file;
        h.inName = e->name.contains(q, Qt::CaseInsensitive);
        // Matches in a text (and the text around the first one), as the search of an open document matches them
        auto count = [&](const QString& text) {
            if (!h.snippet.isEmpty()) {
                return textmatch::count(text, folded);
            }
            const auto found = textmatch::find(text, folded);
            if (!found.empty()) {
                const qsizetype from = found.front().start;
                const qsizetype start = std::max<qsizetype>(0, from - 40);
                const qsizetype length = found.front().end - start + 60;
                h.snippet = (start > 0 ? QStringLiteral("…") : QString()) + text.mid(start, length) +
                            (start + length < text.size() ? QStringLiteral("…") : QString());
            }
            return static_cast<int>(found.size());
        };
        // A Markdown file: its passages, each with the headings above it
        std::vector<std::pair<int, QString>> headings;
        for (qsizetype b = 0; b < e->blockText.size(); ++b) {
            const int level = e->blockLevel[static_cast<size_t>(b)];
            if (const int n = count(e->blockText[b]); n > 0) {
                h.count += n;
                if (e->kind != QLatin1String("md")) {
                    continue;  // a text file: its hits and the text around the first one
                }
                ++h.pages;
                QStringList path;
                for (const auto& [l, text]: headings) {
                    path << (text.size() > 40 ? text.left(39) + QStringLiteral("…") : text);
                }
                h.blockHits.push_back({static_cast<int>(b), n, path.join(QStringLiteral(" › "))});
            }
            if (level > 0) {
                while (!headings.empty() && headings.back().first >= level) {
                    headings.pop_back();
                }
                headings.emplace_back(level, e->blockText[b]);
            }
        }
        for (int p = 0; p < e->pageCount(); ++p) {
            int n = 0;
            if (const int pdfNr = e->pdfPage[static_cast<size_t>(p)]; pdfNr >= 0) {
                if (auto it = e->pdfText.find(pdfNr); it != e->pdfText.end()) {
                    n += count(it->second);
                }
            }
            n += count(e->elementText[p]);
            if (n > 0) {
                h.count += n;
                ++h.pages;
                if (h.firstPage < 0) {
                    h.firstPage = p;
                }
                h.pageHits.push_back({p, n, e->aspects[static_cast<size_t>(p)]});
            }
        }
        if (h.count > 0 || h.inName) {
            hits.push_back(std::move(h));
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.inName != b.inName) {
            return a.inName;
        }
        return a.count > b.count;
    });
    return hits;
}

std::vector<LibraryIndex::Hit> LibraryIndex::search(const FuzzyQuery& query) const {
    if (!query.isValid()) {
        return search(query.source());
    }
    const auto& terms = query.terms();
    const std::vector<textmatch::Term> marks = query.markTerms();
    std::vector<textmatch::Term> termText;
    std::vector<char> counted;  // the hits of the terms that are not negated count
    for (size_t t = 0; t < terms.size(); ++t) {
        termText.push_back(terms[t].textTerm());
        counted.push_back(query.positive(t) ? 1 : 0);
    }
    std::vector<EntryPtr> snapshot;
    {
        std::lock_guard lock(mtx);
        for (const auto& [folder, f]: folders) {
            for (const auto& [name, e]: f.docs) {
                snapshot.push_back(e);
            }
        }
    }
    // Fuzzy terms match words: from the vocabularies of the documents, made first (then the words are matched at once)
    const bool fuzzy = std::any_of(termText.begin(), termText.end(),
                                   [](const textmatch::Term& t) { return (t.bounds & textmatch::Fuzzy) != 0; });
    std::vector<std::shared_ptr<const EntryWords>> vocabularies;
    if (fuzzy) {
        vocabularies = wordsOf(snapshot);
    }
    const words::Terms prepared(termText, counted);
    // A page, a passage of a Markdown file or a text file's text: which terms are on it, and the hits of those that
    // are not negated
    struct Unit {
        int index = 0;
        int count = 0;
        bool exact = false;
        std::vector<char> on;
    };
    std::vector<Hit> hits;
    for (size_t d = 0; d < snapshot.size(); ++d) {
        const EntryPtr& e = snapshot[d];
        const EntryWords* vocab = fuzzy ? vocabularies[d].get() : nullptr;
        Hit h;
        h.file = e->file;
        const fs::path dir = e->file.parent_path().lexically_relative(rootDir);
        const QString folder = dir.empty() || dir == "." ? QString() : QString::fromStdString(dir.generic_string());
        const FuzzyQuery::NameMatch name = query.matchName(e->name, folder);
        std::vector<char> inText(terms.size(), 0);
        std::vector<Unit> units;  // with hits
        const QString* snippetText = nullptr;
        auto examine = [&](int index, std::initializer_list<QStringView> texts) {
            const auto unit = static_cast<size_t>(index);
            const words::Terms::Found f =
                    prepared.examine(texts, vocab && unit < vocab->units.size() ? &vocab->units[unit] : nullptr);
            for (size_t t = 0; t < terms.size(); ++t) {
                inText[t] |= f.on[t];
            }
            return Unit{index, f.count, f.exact, f.on};
        };
        for (qsizetype b = 0; b < e->blockText.size(); ++b) {
            if (Unit u = examine(static_cast<int>(b), {e->blockText[b]}); u.count > 0) {
                if (!snippetText) {
                    snippetText = &e->blockText[b];
                }
                units.push_back(std::move(u));
            }
        }
        const bool pagesOf = e->blockText.isEmpty();
        for (int p = 0; pagesOf && p < e->pageCount(); ++p) {
            const QString* pdfText = nullptr;
            if (const int pdfNr = e->pdfPage[static_cast<size_t>(p)]; pdfNr >= 0) {
                if (auto it = e->pdfText.find(pdfNr); it != e->pdfText.end()) {
                    pdfText = &it->second;
                }
            }
            const QString& elements = e->elementText[p];
            if (Unit u = examine(p, {pdfText ? QStringView(*pdfText) : QStringView(), elements}); u.count > 0) {
                if (!snippetText) {
                    // (from the PDF text if that has hits, else from the text elements)
                    snippetText = pdfText && !textmatch::find(*pdfText, marks).empty() ? pdfText : &elements;
                }
                units.push_back(std::move(u));
            }
        }
        const bool matches = query.evaluate([&](size_t t) { return name.found[t] || inText[t]; });
        if (!matches) {
            continue;
        }
        h.nameScore = name.score;
        h.nameMarks = name.positions;
        h.inName = name.score > 0;
        // The pages on which the expression holds (else all with hits)
        std::vector<const Unit*> listed;
        bool exact = false;
        for (const Unit& u: units) {
            h.count += u.count;
            exact = exact || u.exact;
            if (query.evaluate([&](size_t t) { return name.found[t] || u.on[t]; })) {
                listed.push_back(&u);
            }
        }
        if (listed.empty()) {
            for (const Unit& u: units) {
                listed.push_back(&u);
            }
        }
        h.fuzzyOnly = h.count > 0 && !exact;
        if (snippetText) {
            const auto found = textmatch::find(*snippetText, marks);
            if (!found.empty()) {
                const qsizetype from = found.front().start;
                const qsizetype start = std::max<qsizetype>(0, from - 40);
                const qsizetype length = found.front().end - start + 60;
                h.snippet = (start > 0 ? QStringLiteral("…") : QString()) + snippetText->mid(start, length) +
                            (start + length < snippetText->size() ? QStringLiteral("…") : QString());
            }
        }
        if (e->kind == QLatin1String("md")) {
            // Each passage with the headings above it
            std::vector<std::pair<int, QString>> headings;
            size_t next = 0;
            for (qsizetype b = 0; b < e->blockText.size() && next < listed.size(); ++b) {
                if (listed[next]->index == b) {
                    QStringList path;
                    for (const auto& [l, text]: headings) {
                        path << (text.size() > 40 ? text.left(39) + QStringLiteral("…") : text);
                    }
                    h.blockHits.push_back({static_cast<int>(b), listed[next]->count, path.join(QStringLiteral(" › "))});
                    ++next;
                }
                if (const int level = e->blockLevel[static_cast<size_t>(b)]; level > 0) {
                    while (!headings.empty() && headings.back().first >= level) {
                        headings.pop_back();
                    }
                    headings.emplace_back(level, e->blockText[b]);
                }
            }
            h.pages = static_cast<int>(h.blockHits.size());
        } else if (pagesOf) {
            for (const Unit* u: listed) {
                h.pageHits.push_back({u->index, u->count, e->aspects[static_cast<size_t>(u->index)]});
            }
            h.pages = static_cast<int>(h.pageHits.size());
            h.firstPage = h.pageHits.empty() ? -1 : h.pageHits.front().page;
        }
        hits.push_back(std::move(h));
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.nameScore != b.nameScore) {
            return a.nameScore > b.nameScore;
        }
        if (a.fuzzyOnly != b.fuzzyOnly) {
            return b.fuzzyOnly;  // exact words before words that only match fuzzily
        }
        return a.count > b.count;
    });
    return hits;
}

std::vector<std::shared_ptr<const LibraryIndex::EntryWords>> LibraryIndex::wordsOf(
        const std::vector<EntryPtr>& entries) const {
    std::vector<std::shared_ptr<const EntryWords>> out(entries.size());
    std::vector<size_t> missing;
    {
        std::lock_guard lock(wordsMtx);
        for (size_t i = 0; i < entries.size(); ++i) {
            if (auto it = wordCache.find(entries[i].get()); it != wordCache.end() && it->second.first == entries[i]) {
                out[i] = it->second.second;
            } else {
                missing.push_back(i);
            }
        }
    }
    for (const size_t i: missing) {
        const Entry& e = *entries[i];
        auto w = std::make_shared<EntryWords>();
        if (!e.blockText.isEmpty()) {
            w->units.reserve(static_cast<size_t>(e.blockText.size()));
            for (const QString& b: e.blockText) {
                w->units.emplace_back(std::initializer_list<QStringView>{b});
            }
        } else {
            w->units.reserve(static_cast<size_t>(e.pageCount()));
            for (int p = 0; p < e.pageCount(); ++p) {
                QStringView pdfText;
                if (const int nr = e.pdfPage[static_cast<size_t>(p)]; nr >= 0) {
                    if (auto it = e.pdfText.find(nr); it != e.pdfText.end()) {
                        pdfText = it->second;
                    }
                }
                w->units.emplace_back(std::initializer_list<QStringView>{pdfText, e.elementText[p]});
            }
        }
        out[i] = std::move(w);
    }
    // Kept for the documents searched now (the entries of documents changed or gone are forgotten)
    std::lock_guard lock(wordsMtx);
    std::unordered_map<const Entry*, std::pair<EntryPtr, std::shared_ptr<const EntryWords>>> kept;
    kept.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        kept.emplace(entries[i].get(), std::pair{entries[i], out[i]});
    }
    wordCache = std::move(kept);
    return out;
}

void LibraryIndex::prepareWords() {
    if (discarded || wordsQueued.exchange(true)) {
        return;
    }
    pool->start([this] {
        wordsQueued = false;
        std::vector<EntryPtr> snapshot;
        {
            std::lock_guard lock(mtx);
            for (const auto& [folder, f]: folders) {
                for (const auto& [name, e]: f.docs) {
                    snapshot.push_back(e);
                }
            }
        }
        wordsOf(snapshot);
    });
}

size_t LibraryIndex::vocabularyBytes() const {
    std::lock_guard lock(wordsMtx);
    size_t bytes = 0;
    for (const auto& [e, w]: wordCache) {
        for (const words::Vocabulary& v: w.second->units) {
            bytes += v.bytes();
        }
    }
    return bytes;
}

std::map<int, QString> LibraryIndex::knownPdfText(const fs::path& pdf) const {
    std::map<int, QString> out;
    const QString stamp = fileStamp(pdf);
    if (stamp.isEmpty()) {
        return out;
    }
    const fs::path wanted = pdf.lexically_normal();
    std::lock_guard lock(mtx);
    for (const auto& [folder, f]: folders) {
        for (const auto& [name, e]: f.docs) {
            if (e->pdfStamp == stamp && e->pdf.lexically_normal() == wanted) {
                out.insert(e->pdfText.begin(), e->pdfText.end());  // (the texts are shared, not copied)
            }
        }
    }
    return out;
}

int LibraryIndex::pageCount(const fs::path& file) const {
    std::lock_guard lock(mtx);
    const EntryPtr e = find(file);
    return e && !pageless(e->kind) ? e->pageCount() : -1;
}

std::vector<fs::path> LibraryIndex::filesNamed(const QString& name, bool withoutExtension) const {
    std::vector<fs::path> found;
    const QString wanted = name.toCaseFolded();
    std::lock_guard lock(mtx);
    for (const auto& [folder, f]: folders) {
        for (const auto& [fileName, e]: f.docs) {
            const fs::path file(fileName);
            const QString have = QString::fromStdString((withoutExtension ? file.stem() : file).string());
            if (have.toCaseFolded() == wanted) {
                found.push_back(e->file);
            }
        }
    }
    return found;
}

std::vector<links::Page> LibraryIndex::linkPages(const fs::path& file) const {
    std::vector<links::Page> pages;
    std::lock_guard lock(mtx);
    const EntryPtr e = find(file);
    if (!e || pageless(e->kind)) {
        return pages;
    }
    for (int i = 0; i < e->pageCount(); ++i) {
        links::Page p;
        const auto at = static_cast<size_t>(i);
        p.pdfPage = at < e->pdfPage.size() && e->pdfPage[at] >= 0 ? e->pdfPage[at] + 1 : 0;
        p.text = e->elementText.value(i);
        pages.push_back(std::move(p));
    }
    return pages;
}

std::vector<LinkRewrite::Source> LibraryIndex::linkSources() const {
    std::vector<LinkRewrite::Source> sources;
    std::lock_guard lock(mtx);
    for (const auto& [folder, f]: folders) {
        for (const auto& [name, e]: f.docs) {
            if (!e->links.isEmpty() || !e->wikiLinks.isEmpty()) {
                sources.push_back({e->file, e->links, e->wikiLinks});
            }
        }
    }
    return sources;
}

std::vector<fs::path> LibraryIndex::filesWithPageText(const QString& fingerprint) const {
    std::vector<fs::path> found;
    const QString wanted = links::normalised(fingerprint);
    if (wanted.isEmpty()) {
        return found;
    }
    std::lock_guard lock(mtx);
    for (const auto& [folder, f]: folders) {
        for (const auto& [name, e]: f.docs) {
            for (const QString& page: e->elementText) {
                if (!page.isEmpty() && links::normalised(page).contains(wanted)) {
                    found.push_back(e->file);
                    break;
                }
            }
        }
    }
    return found;
}

QString LibraryIndex::simplified(const QString& text) { return text.simplified(); }

}  // namespace xqt
