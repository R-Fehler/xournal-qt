#include "LibraryIndex.h"
#include "LibraryIndexEntry.h"

#include <set>

#include <QCborArray>

#include "DocumentCovers.h"

namespace xqt {

using namespace library_index;

namespace library_index {
QCborMap pdfTextOf(const QString& stamp, const std::map<int, QString>& pages) {
    QCborMap text;
    for (const auto& [page, t]: pages) {
        text.insert(page, t);
    }
    return QCborMap{{QStringLiteral("stamp"), stamp}, {QStringLiteral("pages"), text}};
}
}  // namespace library_index

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
    if (!e.sha.isEmpty()) {
        notes.insert(QStringLiteral("sha"), e.sha);
    }
    if (!e.pdfSha.isEmpty()) {
        notes.insert(QStringLiteral("pdfSha"), e.pdfSha);
    }
    if (e.showsPdfPages()) {
        notes.insert(QStringLiteral("title"), e.title);
        notes.insert(QStringLiteral("heading"), e.heading);
    }
    if (e.pdfKind != PdfKind::Unknown) {
        notes.insert(QStringLiteral("pdfKind"), QLatin1String(pdfKindName(e.pdfKind)));
    }
    if (e.versions > 0) {
        notes.insert(QStringLiteral("versions"), e.versions);
    }
    if (e.locked) {
        notes.insert(QStringLiteral("locked"), true);
    }
    if (!e.bookmarks.empty()) {
        QCborMap marks;
        for (const auto& [page, label]: e.bookmarks) {
            marks.insert(page, label);
        }
        notes.insert(QStringLiteral("bookmarks"), marks);
    }
    if (e.kind != QLatin1String("image") && e.kind != QLatin1String("text")) {
        // Its to-dos (also none: they were read)
        QCborArray todos;
        for (const Todo& t: e.todos) {
            QCborMap m{{QStringLiteral("p"), t.page},  {QStringLiteral("b"), t.box},
                       {QStringLiteral("l"), t.line},  {QStringLiteral("t"), t.text},
                       {QStringLiteral("d"), t.done},  {QStringLiteral("x"), t.x},
                       {QStringLiteral("y"), t.y},     {QStringLiteral("f"), t.size},
                       {QStringLiteral("w"), t.pageWidth}};
            if (t.occurrence > 0) {
                m.insert(QStringLiteral("o"), t.occurrence);
            }
            if (!t.due.isEmpty()) {
                m.insert(QStringLiteral("due"), t.due);
            }
            if (t.stamp) {
                m.insert(QStringLiteral("s"), true);
            }
            todos.append(m);
        }
        notes.insert(QStringLiteral("todos"), todos);
    }
    if (e.kind != QLatin1String("image") && e.kind != QLatin1String("text")) {
        // Its tags (also none: they were read); those of its PDF's keywords go with its PDF's stamp
        notes.insert(QStringLiteral("tags"), QCborArray::fromStringList(e.textTags));
        if (!e.pdfTags.isEmpty()) {
            notes.insert(QStringLiteral("pdfTags"), QCborArray::fromStringList(e.pdfTags));
        }
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
    e->sha = notes.value(QStringLiteral("sha")).toString();
    e->pdfSha = notes.value(QStringLiteral("pdfSha")).toString();
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
        for (const auto& l: notes.value(QStringLiteral("links")).toArray()) {
            e->links << l.toString();
        }
        for (const auto& l: notes.value(QStringLiteral("wikiLinks")).toArray()) {
            e->wikiLinks << l.toString();
        }
    }
    // Its bookmarks
    const QCborMap marks = notes.value(QStringLiteral("bookmarks")).toMap();
    for (auto it = marks.cbegin(); it != marks.cend(); ++it) {
        // (a Markdown file has no pages in the index: its bookmarks' pages are those it is laid out on)
        if (const qint64 page = it.key().toInteger(-1);
            page >= 0 && (page < texts.size() || e->kind == QLatin1String("md"))) {
            e->bookmarks[static_cast<int>(page)] = it.value().toString();
        }
    }
    // What its PDF is
    e->pdfKind = e->isPdf() ? pdfKindNamed(notes.value(QStringLiteral("pdfKind")).toString()) : PdfKind::Unknown;
    e->versions = e->isPdf() ? static_cast<int>(notes.value(QStringLiteral("versions")).toInteger(0)) : 0;
    e->locked = e->isPdf() && notes.value(QStringLiteral("locked")).toBool();
    // Its to-dos
    for (const auto& v: notes.value(QStringLiteral("todos")).toArray()) {
        const QCborMap m = v.toMap();
        Todo t;
        t.page = static_cast<int>(m.value(QStringLiteral("p")).toInteger(-1));
        t.box = static_cast<int>(m.value(QStringLiteral("b")).toInteger(-1));
        t.line = static_cast<int>(m.value(QStringLiteral("l")).toInteger());
        t.occurrence = static_cast<int>(m.value(QStringLiteral("o")).toInteger());
        t.text = m.value(QStringLiteral("t")).toString();
        t.done = m.value(QStringLiteral("d")).toBool();
        t.due = m.value(QStringLiteral("due")).toString();
        t.stamp = m.value(QStringLiteral("s")).toBool();
        t.x = m.value(QStringLiteral("x")).toDouble();
        t.y = m.value(QStringLiteral("y")).toDouble();
        t.size = m.value(QStringLiteral("f")).toDouble();
        t.pageWidth = m.value(QStringLiteral("w")).toDouble();
        if (t.page < texts.size()) {
            e->todos.push_back(std::move(t));
        }
    }
    // Its tags
    for (const auto& v: notes.value(QStringLiteral("tags")).toArray()) {
        e->textTags << v.toString();
    }
    for (const auto& v: notes.value(QStringLiteral("pdfTags")).toArray()) {
        e->pdfTags << v.toString();
    }
    if (e->showsPdfPages()) {
        // Its PDF's title
        e->title = notes.value(QStringLiteral("title")).toString();
        e->heading = notes.value(QStringLiteral("heading")).toString();
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
    inks->load(folder);  // (the handwriting read in its documents)
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
        ++kindChanges;  // (the kinds stored in its packs are known now)
        ++markChanges;  // (and their bookmarks)
        ++todoChangeCount;  // (and their to-dos)
        ++tagChangeCount;   // (and their tags)
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
    if ((slot ? slot->pdfKind : PdfKind::Unknown) != e->pdfKind || (slot ? slot->versions : 0) != e->versions ||
        (slot ? slot->locked : false) != e->locked) {
        ++kindChanges;  // (the cards show both)
    }
    if ((slot ? slot->bookmarks : std::map<int, QString>()) != e->bookmarks) {
        ++markChanges;
    }
    if ((slot ? slot->todos : std::vector<Todo>()) != e->todos) {
        ++todoChangeCount;
    }
    if ((slot ? slot->tags() : QStringList()) != e->tags()) {
        ++tagChangeCount;
    }
    slot = e;
    scheduler->changed();
}

void LibraryIndex::erase(const fs::path& file) {
    inks->erase(file);
    auto f = folders.find(file.parent_path());
    if (f != folders.end() && f->second.docs.erase(file.filename().string())) {
        ++kindChanges;
        ++markChanges;
        ++todoChangeCount;
        ++tagChangeCount;
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
            for (const QString& pack: {NOTES_PACK, PDF_TEXT_PACK, DocumentCovers::PACK, DocumentCovers::STAMPS_PACK}) {
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

}  // namespace xqt
