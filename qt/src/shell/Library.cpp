#include "Library.h"

#include <algorithm>
#include <set>
#include <shared_mutex>

#include <QCborArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QStandardPaths>
#include <QThreadPool>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfPage.h"
#include "session/DocumentSession.h"
#include "util/PathUtil.h"

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
        n = fs::absolute(p).lexically_normal();
    }
    if (!n.has_filename() && n.has_parent_path() && n != n.root_path()) {
        n = n.parent_path();
    }
    return n;
}
}  // namespace

Library::Library(const fs::path& root): rootDir(normalized(root)) {}

fs::path Library::librariesFolder() {
    return fs::path(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation).toStdString()) /
           "Xournal_Libraries";
}

fs::path Library::defaultRoot() { return librariesFolder() / "Default"; }

fs::path Library::downloadsFolder() {
    return fs::path(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation).toStdString());
}

bool Library::isTemporary() const {
    const fs::path downloads = normalized(downloadsFolder());
    return !downloads.empty() && DocumentFiles::remap(rootDir, downloads, "/") != rootDir;
}

QString Library::name() const { return QString::fromStdString(rootDir.filename().string()); }

bool Library::isDefault() const { return rootDir == normalized(defaultRoot()); }

std::string Library::key() const { return hashOf(rootDir.string(), 12).toStdString(); }

fs::path Library::metaDir() const {
    // Every library keeps its index and previews in its own folder (also Downloads: starting is then as fast as
    // anywhere else, and the data goes away with the folder). Only for folders that cannot be written: the cache.
    const fs::path dir = rootDir / DocumentFiles::META_DIR;
    std::error_code ec;
    if ((fs::is_directory(dir, ec) || fs::create_directories(dir, ec)) &&
        QFileInfo(QString::fromStdString(dir.string())).isWritable()) {
        return dir;
    }
    return Util::getCacheSubfolder(fs::path("libraries") / key());
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
    for (const fs::path& f: {item.xopp, item.pdf, item.xopp.empty() ? fs::path() : DocumentFiles::attachmentOf(item.xopp)}) {
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

void LibraryIndex::update(std::vector<DocumentItem> items) {
    const quint64 gen = ++generation;
    running = true;
    doneCount = 0;
    totalCount = static_cast<int>(items.size());
    Q_EMIT progress();
    pool->start([this, items = std::move(items), gen]() mutable { run(std::move(items), gen); });
}

void LibraryIndex::moved(const std::vector<std::pair<fs::path, fs::path>>& moves) {
    if (!moves.empty()) {
        pool->start([this, moves] { applyMoves(moves); });
    }
}

bool LibraryIndex::Entry::showsPdfPages() const {
    return std::any_of(pdfPage.begin(), pdfPage.end(), [](int p) { return p >= 0; });
}

bool LibraryIndex::Entry::upToDate(const DocumentItem& item) const {
    return file == item.main() && xoppStamp == (item.xopp.empty() ? QString() : fileStamp(item.xopp)) &&
           pdfStamp == fileStamp(pdf);
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
    return QCborMap{{QStringLiteral("kind"), e.kind},       {QStringLiteral("name"), e.name},
                    {QStringLiteral("xopp"), e.xoppStamp},  {QStringLiteral("pdf"), pdf},
                    {QStringLiteral("pdfStamp"), e.pdfStamp}, {QStringLiteral("pdfPages"), pdfPages},
                    {QStringLiteral("text"), text},         {QStringLiteral("aspects"), aspects}};
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

void LibraryIndex::writeChanged() {
    std::lock_guard writeLock(writeMtx);
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
    for (const Job& job: jobs) {
        const fs::path dir = where.dirOf(job.folder);
        if (job.docs.empty()) {
            // Its last document is gone: its cache folder goes too (unless something else is in it)
            for (const QString& pack: {NOTES_PACK, PDF_TEXT_PACK, QStringLiteral("previews")}) {
                Packs::remove(dir, pack);
            }
            Packs::removeIfOnlyOurs(dir);
            continue;
        }
        if (job.notes) {
            QCborMap notes;
            for (const auto& e: job.docs) {
                notes.insert(qstr(e->file.filename()), notesOf(*e));
            }
            Packs::write(dir, NOTES_PACK, FORMAT, notes, true);
            ++packWrites;
        }
        if (job.text) {
            QCborMap text;
            for (const auto& e: job.docs) {
                if (!e->pdfText.empty()) {
                    text.insert(qstr(e->file.filename()), pdfTextOf(e->pdfStamp, e->pdfText));
                }
            }
            Packs::write(dir, PDF_TEXT_PACK, FORMAT, text, true, &job.changedText);
            ++packWrites;
        }
    }
}

// --- reading documents

std::shared_ptr<LibraryIndex::Entry> LibraryIndex::read(const DocumentItem& item, const EntryPtr& previous) {
    auto e = std::make_shared<Entry>();
    e->file = item.main();
    e->kind = item.xopp.empty() ? QStringLiteral("pdf") : QStringLiteral("xopp");
    e->name = QString::fromStdString(item.name());
    e->xoppStamp = item.xopp.empty() ? QString() : fileStamp(item.xopp);
    auto loaded = DocumentSession::loadFile(item.main());
    ++docsRead;
    if (!loaded.document) {
        return e;  // unreadable: empty, not read again until it changes
    }
    Document& doc = *loaded.document;
    std::shared_lock lock(doc);
    e->pdf = doc.getPdfFilepath();
    e->pdfStamp = fileStamp(e->pdf);
    // PDF text read before: from this document's last entry, or another one with this PDF (e.g. the PDF of a
    // document that just got its .xopp, or that was moved by another program).
    EntryPtr donor;
    if (!e->pdfStamp.isEmpty()) {
        if (previous && previous->pdf == e->pdf && previous->pdfStamp == e->pdfStamp) {
            donor = previous;
        } else {
            std::lock_guard entriesLock(mtx);
            for (const auto& [folder, f]: folders) {
                for (const auto& [name, other]: f.docs) {
                    if (other->pdfStamp == e->pdfStamp && (other->pdf == e->pdf || !donor)) {
                        donor = other;  // the same size and time: the same file (renamed), preferably the same path
                    }
                }
            }
        }
    }
    const size_t pdfPages = doc.getPdfPageCount();
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        PageRef page = doc.getPage(i);
        const bool pdfPage = page->getBackgroundType().isPdfPage() && page->getPdfPageNr() < pdfPages;
        const int pdfNr = pdfPage ? static_cast<int>(page->getPdfPageNr()) : -1;  // (a shorter new PDF version)
        e->pdfPage.push_back(pdfNr);
        if (pdfNr >= 0 && !e->pdfText.count(pdfNr)) {
            if (donor && donor->pdfText.count(pdfNr)) {
                e->pdfText[pdfNr] = donor->pdfText.at(pdfNr);
            } else if (XojPdfPageSPtr pdf = doc.getPdfPage(static_cast<size_t>(pdfNr))) {
                const XojPdfRectangle all(0, 0, pdf->getWidth(), pdf->getHeight());
                e->pdfText[pdfNr] = simplified(QString::fromStdString(pdf->selectText(all, XojPdfPageSelectionStyle::Linear)));
                ++pdfRead;
            }
        }
        QString elements;
        for (const Layer* layer: page->getLayers()) {
            for (const auto& el: layer->getElementsView()) {
                if (el->getType() == ELEMENT_TEXT) {
                    elements += ' ' + QString::fromStdString(static_cast<const Text*>(el)->getText());
                }
            }
        }
        e->elementText << simplified(elements);
        e->aspects.push_back(page->getWidth() > 0 ? page->getHeight() / page->getWidth() : 0);
    }
    return e;
}

LibraryIndex::EntryPtr LibraryIndex::movedHere(const DocumentItem& item, std::multimap<std::string, EntryPtr>& orphans,
                                               bool& collected) {
    if (!collected) {
        // Entries whose file is gone (once per update, when a document without an entry is found)
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
                orphans.emplace(e->file.filename().string(), e);
            }
        }
    }
    const fs::path file = item.main();
    const auto [from, to] = orphans.equal_range(file.filename().string());
    for (auto it = from; it != to; ++it) {
        const EntryPtr& old = it->second;
        // The same file: the same size and time
        const bool same = item.xopp.empty() ? old->xoppStamp.isEmpty() && old->pdfStamp == fileStamp(item.pdf)
                                            : old->xoppStamp == fileStamp(item.xopp);
        if (!same) {
            continue;
        }
        auto e = std::make_shared<Entry>(*old);
        e->file = file;
        if (e->pdf.parent_path() == old->file.parent_path()) {
            e->pdf = file.parent_path() / old->pdf.filename();  // its PDF (or attachment) came along
        }
        orphans.erase(it);
        return e;
    }
    return nullptr;
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
    std::multimap<std::string, EntryPtr> orphans;
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
        } else {
            auto fresh = read(item, current);
            std::lock_guard lock(mtx);
            put(std::move(fresh));
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
        const DocumentItem item = DocumentFiles::itemOf(to);
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
        // Matches in a text (and the text around the first one)
        auto count = [&](const QString& text) {
            int n = 0;
            for (qsizetype from = text.indexOf(q, 0, Qt::CaseInsensitive); from >= 0;
                 from = text.indexOf(q, from + q.size(), Qt::CaseInsensitive)) {
                if (h.snippet.isEmpty()) {
                    const qsizetype start = std::max<qsizetype>(0, from - 40);
                    const qsizetype length = from - start + q.size() + 60;
                    h.snippet = (start > 0 ? QStringLiteral("…") : QString()) + text.mid(start, length) +
                                (start + length < text.size() ? QStringLiteral("…") : QString());
                }
                ++n;
            }
            return n;
        };
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

int LibraryIndex::pageCount(const fs::path& file) const {
    std::lock_guard lock(mtx);
    const EntryPtr e = find(file);
    return e ? e->pageCount() : -1;
}

QString LibraryIndex::simplified(const QString& text) { return text.simplified(); }

}  // namespace xqt
