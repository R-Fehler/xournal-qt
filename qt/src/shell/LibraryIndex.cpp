#include "LibraryIndex.h"
#include "LibraryIndexEntry.h"

#include <algorithm>
#include <optional>
#include <set>
#include <shared_mutex>

#include <QMetaObject>
#include <QThreadPool>

#include "model/Document.h"
#include "model/Text.h"
#include "session/HybridPdf.h"

#include "Tags.h"
#include "DocumentCovers.h"

namespace xqt {

using namespace library_index;

const QString LibraryIndex::NOTES_PACK = QStringLiteral("notes");
const QString LibraryIndex::PDF_TEXT_PACK = QStringLiteral("pdf-text");

namespace library_index {
QString ownStamp(const DocumentItem& item) {
    if (!item.xopp.empty()) {
        return fileStamp(item.xopp);
    }
    return item.pdf.empty() ? fileStamp(item.main()) : QString();
}
bool isPdfFile(const fs::path& p) {
    return QString::fromStdString(p.extension().string()).compare(QLatin1String(".pdf"), Qt::CaseInsensitive) == 0;
}
int versionsOfPdf(const fs::path& pdf) {
    const HybridPdf::Marker m = HybridPdf::markerOf(pdf);
    return m.history ? std::max(1, m.versions) : 0;
}
PdfKind kindOfPdf(const fs::path& pdf) {
    const HybridPdf::Marker m = HybridPdf::markerOf(pdf);
    if (!m.hybrid) {
        return PdfKind::Plain;
    }
    const bool text = m.markdown;
    return m.archive ? (text ? PdfKind::ArchiveText : PdfKind::Archive) : (text ? PdfKind::Text : PdfKind::Notes);
}
QString entryKind(const DocumentItem& item) {
    if (!item.xopp.empty()) {
        return QStringLiteral("xopp");
    }
    return !item.pdf.empty()   ? QStringLiteral("pdf")
           : !item.md.empty()  ? QStringLiteral("md")
           : !item.other.empty() ? QStringLiteral("text")
                                 : QStringLiteral("image");
}
bool pageless(const QString& kind) {
    return kind == QLatin1String("md") || kind == QLatin1String("text") || kind == QLatin1String("image");
}
}  // namespace library_index

LibraryIndex::LibraryIndex(fs::path root, CacheLocation location, QObject* parent):
        QObject(parent),
        rootDir(std::move(root)),
        where(location.valid() ? std::move(location) : CacheLocation(rootDir)),
        pool(std::make_unique<QThreadPool>()),
        writer(std::make_unique<QThreadPool>()) {
    pool->setMaxThreadCount(1);  // in the background, one document after the other (also orders moves and updates)
    writer->setMaxThreadCount(1);
    scheduler = std::make_unique<WriteScheduler>([this] { writer->start([this] { writeChanged(); }); });
    inks = std::make_unique<InkTextStore>(where);
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
    inks->flush();
}

void LibraryIndex::setWriteDelays(int quietMs, int maxDelayMs) { scheduler->setDelays(quietMs, maxDelayMs); }

void LibraryIndex::discard() {
    discarded = true;
    ++generation;  // stops a running update
    pool->waitForDone();
    writer->waitForDone();
    scheduler->cancel();
    inks->discard();
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
    for (const auto& [from, to]: moves) {
        inks->moved(from, to);  // (a document's handwriting follows it; a folder's goes with its cache)
    }
    if (!moves.empty() && !discarded) {
        pool->start([this, moves] { applyMoves(moves); });
    }
}

bool LibraryIndex::Entry::showsPdfPages() const {
    return std::any_of(pdfPage.begin(), pdfPage.end(), [](int p) { return p >= 0; });
}

bool LibraryIndex::Entry::isPdf() const { return isPdfFile(file); }

bool LibraryIndex::Entry::upToDate(const DocumentItem& item) const {
    return file == item.main() && xoppStamp == ownStamp(item) && pdfStamp == fileStamp(pdf);
}

QStringList LibraryIndex::Entry::tags() const {
    QStringList all = textTags;
    tags::merge(all, pdfTags);
    return all;
}

bool LibraryIndex::documentProtected(const fs::path& file) {
    const DocumentItem item = DocumentFiles::itemOf(file);
    if (discarded || !item.valid() || !where.contains(item.main())) {
        return false;
    }
    inks->erase(item.main());
    auto e = std::make_shared<Entry>();
    e->file = item.main();
    e->kind = entryKind(item);
    e->name = QString::fromStdString(item.name());
    e->xoppStamp = ownStamp(item);
    e->sample = contentSample(item.main());
    e->locked = true;
    if (e->isPdf()) {
        e->pdfKind = kindOfPdf(e->file);  // (what it is: not read again for it)
    }
    std::lock_guard lock(mtx);
    auto f = folders.find(item.main().parent_path());
    if (f == folders.end() || !f->second.loaded) {
        return false;
    }
    put(e);
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
    // The keywords of its PDF: as before when it did not change (else read now: only its trailer and information)
    fillPdfTags(*e, previous);
    // The title of its PDF as before (else the next update reads the document)
    if (e->showsPdfPages()) {
        if (!previous || previous->pdf != e->pdf || previous->pdfStamp != e->pdfStamp ||
            firstPdfPage(previous->pdfPage) != firstPdfPage(e->pdfPage)) {
            return false;
        }
        e->title = previous->title;
        e->heading = previous->heading;
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
    // The same size and time and the same content (two different files can have the same size and time), the one
    // with the same name first
    const auto [from, to] = orphans.equal_range(key);
    auto match = to;
    QString sample;
    for (auto it = from; it != to; ++it) {
        const EntryPtr& old = it->second;
        const bool sameName = old->file.filename() == file.filename();
        if (old->sample.isEmpty()) {
            continue;
        }
        if (sample.isEmpty()) {
            sample = contentSample(file);
        }
        if (old->sample != sample) {
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
        if (current && !taken && !current->upToDate(item)) {
            // Its files with another time but the same size and content (copied, unzipped, synced): taken over
            if (EntryPtr adopted = adopt(item, current)) {
                std::lock_guard lock(mtx);
                if (find(file) == current) {
                    put(adopted);
                    current = adopted;
                }
            }
        }
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
    fillHashes(items, gen);
    if (generation != gen) {
        return;  // a newer update takes over
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

namespace {
/// The size in a stamp ("size:time"; -1: none).
qint64 sizeOfStamp(const QString& stamp) {
    const qsizetype colon = stamp.indexOf(QLatin1Char(':'));
    bool ok = false;
    const qint64 size = colon > 0 ? stamp.left(colon).toLongLong(&ok) : -1;
    return ok ? size : -1;
}
/// The content hash of a file whose stamp is `stamp`, if it still is after it was read ("" if not).
QString hashOfStamped(const fs::path& file, const QString& stamp) {
    const QString hash = contentHash(file);
    return !hash.isEmpty() && fileStamp(file) == stamp ? hash : QString();
}
}  // namespace

LibraryIndex::EntryPtr LibraryIndex::adopt(const DocumentItem& item, const EntryPtr& e) {
    if (!e || e->file != item.main()) {
        return nullptr;
    }
    const QString own = ownStamp(item), pdfNow = fileStamp(e->pdf);
    const bool ownDiffers = e->xoppStamp != own, pdfDiffers = e->pdfStamp != pdfNow;
    if (!ownDiffers && !pdfDiffers) {
        return nullptr;  // (something else is missing: read as usual)
    }
    // Never on name and size alone: the content must be the one the entry was read from
    auto same = [](const fs::path& file, const QString& was, const QString& now, const QString& hash) {
        return !hash.isEmpty() && !was.isEmpty() && !now.isEmpty() && sizeOfStamp(was) == sizeOfStamp(now) &&
               sizeOfStamp(now) >= 0 && hashOfStamped(file, now) == hash;
    };
    if (ownDiffers && !same(ownFileOf(item), e->xoppStamp, own, e->sha)) {
        return nullptr;
    }
    if (pdfDiffers && !same(e->pdf, e->pdfStamp, pdfNow, e->pdfSha)) {
        return nullptr;
    }
    auto adopted = std::make_shared<Entry>(*e);
    adopted->xoppStamp = own;
    adopted->pdfStamp = pdfNow;
    ++adoptions;
    // Its handwriting was read from the same content, and its cover shows it
    const QString inkWas = e->xoppStamp.isEmpty() ? e->pdfStamp : e->xoppStamp;
    const QString inkNow = own.isEmpty() ? pdfNow : own;
    if (inkWas != inkNow) {
        inks->restamp(e->file, inkWas, inkNow);
    }
    std::vector<std::pair<QString, QString>> changes;
    if (ownDiffers) {
        changes.emplace_back(e->xoppStamp, own);
    }
    if (pdfDiffers) {
        changes.emplace_back(e->pdfStamp, pdfNow);
    }
    DocumentCovers::adopt(item, changes);
    return adopted;
}

void LibraryIndex::fillHashes(const std::vector<DocumentItem>& items, quint64 gen) {
    for (const DocumentItem& item: items) {
        if (generation != gen || discarded) {
            return;
        }
        EntryPtr e;
        {
            std::lock_guard lock(mtx);
            e = find(item.main());
        }
        if (!e || !e->upToDate(item)) {
            continue;
        }
        const fs::path own = ownFileOf(item);
        const bool needOwn = e->sha.isEmpty() && !e->xoppStamp.isEmpty() && !own.empty();
        const bool needPdf = e->pdfSha.isEmpty() && !e->pdfStamp.isEmpty() && !e->pdf.empty();
        if (!needOwn && !needPdf) {
            continue;
        }
        auto hashed = std::make_shared<Entry>(*e);
        if (needOwn) {
            hashed->sha = hashOfStamped(own, e->xoppStamp);
            ++hashCount;
        }
        if (needPdf) {
            hashed->pdfSha = hashOfStamped(e->pdf, e->pdfStamp);
            ++hashCount;
        }
        if (hashed->sha == e->sha && hashed->pdfSha == e->pdfSha) {
            continue;  // (changed while it was read: the next update)
        }
        std::lock_guard lock(mtx);
        if (find(item.main()) == e) {
            put(hashed);
        }
    }
}

std::optional<LibraryIndex::Snapshot> LibraryIndex::snapshot(const DocumentItem& item) {
    if (!item.valid() || !where.contains(item.main())) {
        return std::nullopt;
    }
    load(item.main().parent_path());
    EntryPtr e;
    {
        std::lock_guard lock(mtx);
        e = find(item.main());
    }
    if (!e || e->locked || !e->upToDate(item)) {
        return std::nullopt;
    }
    Snapshot s;
    s.notes = notesOf(*e);
    if (!e->pdfText.empty()) {
        s.pdfText = pdfTextOf(e->pdfStamp, e->pdfText);
    }
    s.ownStamp = e->xoppStamp;
    s.pdfStamp = e->pdfStamp;
    s.sha = e->sha;
    s.pdfSha = e->pdfSha;
    s.pdf = e->pdf;
    s.ink = inkOf(*e);
    return s;
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
            ++kindChanges;  // (known under their new paths)
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

}  // namespace xqt
