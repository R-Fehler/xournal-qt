#include "Library.h"

#include <algorithm>
#include <set>
#include <shared_mutex>

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

LibraryIndex::LibraryIndex(fs::path root, fs::path dir, QObject* parent):
        QObject(parent), rootDir(std::move(root)), indexDir(std::move(dir)), pool(std::make_unique<QThreadPool>()) {
    pool->setMaxThreadCount(1);  // in the background, one document after the other (also orders moves and updates)
}

LibraryIndex::~LibraryIndex() {
    ++generation;  // stops a running update
    pool->waitForDone();
}

void LibraryIndex::waitForDone() { pool->waitForDone(); }

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

fs::path LibraryIndex::indexFile(const fs::path& file) const {
    return indexDir / (hashOf(file.lexically_relative(rootDir).string(), 16).toStdString() + ".json");
}

bool LibraryIndex::Entry::upToDate(const DocumentItem& item) const {
    return file == item.main() && xoppStamp == (item.xopp.empty() ? QString() : fileStamp(item.xopp)) &&
           pdfStamp == fileStamp(pdf);
}

std::shared_ptr<const LibraryIndex::Entry> LibraryIndex::loadStored(const fs::path& file) const {
    QFile f(QString::fromStdString(indexFile(file).string()));
    if (!f.open(QIODevice::ReadOnly)) {
        return nullptr;
    }
    const QJsonObject json = QJsonDocument::fromJson(f.readAll()).object();
    if (json["format"].toInt() != FORMAT ||
        json["file"].toString() != QString::fromStdString(file.lexically_relative(rootDir).string())) {
        return nullptr;
    }
    auto e = std::make_shared<Entry>();
    e->file = file;
    e->name = json["name"].toString();
    e->xoppStamp = json["xoppStamp"].toString();
    const fs::path pdf(json["pdf"].toString().toStdString());
    e->pdf = pdf.empty() || pdf.is_absolute() ? pdf : rootDir / pdf;  // in the library: stored relative to it
    e->pdfStamp = json["pdfStamp"].toString();
    const QJsonObject pdfText = json["pdfText"].toObject();
    for (auto it = pdfText.begin(); it != pdfText.end(); ++it) {
        e->pdfText[it.key().toInt()] = it.value().toString();
    }
    for (const auto& page: json["pages"].toArray()) {
        const QJsonObject o = page.toObject();
        e->pdfPage.push_back(o["pdf"].toInt(-1));
        e->elementText << o["text"].toString();
        e->aspects.push_back(o["aspect"].toDouble());
    }
    return e;
}

void LibraryIndex::store(const Entry& e) const {
    QJsonObject pdfText;
    for (const auto& [page, text]: e.pdfText) {
        pdfText[QString::number(page)] = text;
    }
    QJsonArray pages;
    for (int i = 0; i < e.pageCount(); ++i) {
        pages.append(QJsonObject{{"pdf", e.pdfPage[static_cast<size_t>(i)]},
                                 {"text", e.elementText[i]},
                                 {"aspect", e.aspects[static_cast<size_t>(i)]}});
    }
    const bool inLibrary = !e.pdf.empty() && DocumentFiles::remap(e.pdf, rootDir, "/") != e.pdf;
    const QJsonObject json{{"format", FORMAT},
                           {"file", QString::fromStdString(e.file.lexically_relative(rootDir).string())},
                           {"name", e.name},
                           {"xoppStamp", e.xoppStamp},
                           {"pdf", QString::fromStdString(inLibrary ? e.pdf.lexically_relative(rootDir).string()
                                                                    : e.pdf.string())},
                           {"pdfStamp", e.pdfStamp},
                           {"pdfText", pdfText},
                           {"pages", pages}};
    std::error_code ec;
    fs::create_directories(indexDir, ec);
    // A cache: written next to it and renamed, without syncing to disk
    const fs::path stored = indexFile(e.file);
    const fs::path tmp = fs::path(stored) += ".part";
    QFile out(QString::fromStdString(tmp.string()));
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(json).toJson(QJsonDocument::Compact));
        out.close();
        fs::rename(tmp, stored, ec);
    }
}

std::shared_ptr<LibraryIndex::Entry> LibraryIndex::read(const DocumentItem& item,
                                                        const std::shared_ptr<const Entry>& previous) {
    auto e = std::make_shared<Entry>();
    e->file = item.main();
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
    std::shared_ptr<const Entry> donor;
    if (!e->pdfStamp.isEmpty()) {
        if (previous && previous->pdf == e->pdf && previous->pdfStamp == e->pdfStamp) {
            donor = previous;
        } else {
            std::lock_guard entriesLock(mtx);
            for (const auto& [f, other]: entries) {
                if (other->pdfStamp == e->pdfStamp && (other->pdf == e->pdf || !donor)) {
                    donor = other;  // the same size and time: the same file (renamed), preferably the same path
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

void LibraryIndex::run(std::vector<DocumentItem> items, quint64 gen) {
    auto notify = [this] { QMetaObject::invokeMethod(this, [this] { Q_EMIT progress(); }, Qt::QueuedConnection); };
    std::set<fs::path> alive;
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
        std::shared_ptr<const Entry> current;
        {
            std::lock_guard lock(mtx);
            if (auto it = entries.find(file); it != entries.end()) {
                current = it->second;
            }
        }
        if (!current) {
            current = loadStored(file);  // from the last run
        }
        if (current && current->upToDate(item)) {
            std::lock_guard lock(mtx);
            entries[file] = current;
        } else {
            auto fresh = read(item, current);
            store(*fresh);
            std::lock_guard lock(mtx);
            entries[file] = std::move(fresh);
        }
        doneCount = ++done;
        notify();
    }
    // Documents that are gone
    std::set<std::string> keep;
    {
        std::lock_guard lock(mtx);
        for (auto it = entries.begin(); it != entries.end();) {
            it = alive.count(it->first) ? std::next(it) : entries.erase(it);
        }
    }
    for (const auto& f: alive) {
        keep.insert(indexFile(f).filename().string());
    }
    std::error_code ec;
    for (auto it = fs::directory_iterator(indexDir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (it->path().extension() == ".json" && !keep.count(it->path().filename().string())) {
            std::error_code rec;
            fs::remove(it->path(), rec);
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
    // The documents at their new places (a moved folder: all documents in it) and where they were before.
    std::vector<std::pair<fs::path, fs::path>> documents;  // old main file, new main file
    for (const auto& [from, to]: moves) {
        std::error_code ec;
        if (fs::is_directory(to, ec)) {
            for (const auto& item: DocumentFiles::scanRecursive(to)) {
                documents.emplace_back(DocumentFiles::remap(item.main(), to, from), item.main());
            }
        } else if (const DocumentItem item = DocumentFiles::itemOf(to); item.valid() && item.main() == to) {
            documents.emplace_back(from, to);
        }
    }
    for (const auto& [oldFile, newFile]: documents) {
        std::shared_ptr<const Entry> old;
        {
            std::lock_guard lock(mtx);
            if (auto it = entries.find(oldFile); it != entries.end()) {
                old = it->second;
            }
        }
        if (!old) {
            old = loadStored(oldFile);
        }
        if (!old) {
            continue;  // not indexed yet: the next update reads it
        }
        // Same content at another place: new paths, the stamps stay. A renamed or moved file keeps its size and
        // time: nothing is read again. A pair's .xopp is written again (the new path of its PDF): the next update
        // reads only the .xopp, the PDF text stays. An entry that was not up to date is read again, too.
        auto e = std::make_shared<Entry>(*old);
        e->file = newFile;
        e->name = QString::fromStdString(DocumentFiles::itemOf(newFile).name());
        e->pdf = remapAll(old->pdf);
        store(*e);
        std::error_code ec;
        fs::remove(indexFile(oldFile), ec);
        std::lock_guard lock(mtx);
        entries.erase(oldFile);
        entries[newFile] = std::move(e);
    }
}

std::vector<LibraryIndex::Hit> LibraryIndex::search(const QString& query) const {
    const QString q = simplified(query).trimmed();
    std::vector<Hit> hits;
    if (q.isEmpty()) {
        return hits;
    }
    std::vector<std::shared_ptr<const Entry>> snapshot;
    {
        std::lock_guard lock(mtx);
        for (const auto& [file, e]: entries) {
            snapshot.push_back(e);
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
    auto it = entries.find(file);
    return it == entries.end() ? -1 : it->second->pageCount();
}

QString LibraryIndex::simplified(const QString& text) { return text.simplified(); }

}  // namespace xqt
