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

QString Library::name() const { return QString::fromStdString(rootDir.filename().string()); }

bool Library::isDefault() const { return rootDir == normalized(defaultRoot()); }

std::string Library::key() const { return hashOf(rootDir.string(), 12).toStdString(); }

fs::path Library::metaDir() const {
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

QString documentStamp(const DocumentItem& item) {
    QString stamp;
    for (const fs::path& f: {item.xopp, item.pdf}) {
        if (!f.empty()) {
            const QFileInfo info(QString::fromStdString(f.string()));
            stamp += QString::number(info.size()) + ':' +
                     QString::number(info.lastModified().toMSecsSinceEpoch()) + ';';
        }
    }
    return stamp;
}

// --- LibraryIndex ---------------------------------------------------------------------------------------------

LibraryIndex::LibraryIndex(fs::path root, fs::path dir, QObject* parent):
        QObject(parent), rootDir(std::move(root)), indexDir(std::move(dir)), pool(std::make_unique<QThreadPool>()) {
    pool->setMaxThreadCount(1);  // in the background, one document after the other
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

fs::path LibraryIndex::indexFile(const fs::path& file) const {
    return indexDir / (hashOf(file.lexically_relative(rootDir).string(), 16).toStdString() + ".json");
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
        const QString stamp = documentStamp(item);
        {
            std::lock_guard lock(mtx);
            if (auto it = entries.find(file); it != entries.end() && it->second->stamp == stamp) {
                doneCount = ++done;
                continue;
            }
        }
        auto entry = std::make_shared<Entry>();
        entry->file = file;
        entry->stamp = stamp;
        entry->name = QString::fromStdString(item.name());
        const QString rel = QString::fromStdString(file.lexically_relative(rootDir).string());
        const fs::path stored = indexFile(file);

        bool loaded = false;
        if (QFile f(QString::fromStdString(stored.string())); f.open(QIODevice::ReadOnly)) {
            const QJsonObject json = QJsonDocument::fromJson(f.readAll()).object();
            if (json["format"].toInt() == FORMAT && json["stamp"].toString() == stamp && json["file"].toString() == rel) {
                for (const auto& page: json["pages"].toArray()) {
                    entry->pages << page.toString();
                }
                for (const auto& a: json["aspects"].toArray()) {
                    entry->aspects.push_back(a.toDouble());
                }
                loaded = true;
            }
        }
        if (!loaded) {
            auto result = DocumentSession::loadFile(file);
            if (result.document) {
                for (const QString& text: extractText(*result.document)) {
                    entry->pages << simplified(text);
                }
                std::shared_lock lock(*result.document);
                for (size_t i = 0; i < result.document->getPageCount(); ++i) {
                    const PageRef p = result.document->getPage(i);
                    entry->aspects.push_back(p->getWidth() > 0 ? p->getHeight() / p->getWidth() : 0);
                }
            }
            std::error_code ec;
            fs::create_directories(indexDir, ec);
            // A cache: written next to it and renamed, without syncing to disk
            const fs::path tmp = fs::path(stored) += ".part";
            QFile out(QString::fromStdString(tmp.string()));
            if (out.open(QIODevice::WriteOnly)) {
                QJsonArray aspects;
                for (double a: entry->aspects) {
                    aspects.append(a);
                }
                QJsonObject json{{"format", FORMAT},
                                 {"file", rel},
                                 {"stamp", stamp},
                                 {"pages", QJsonArray::fromStringList(entry->pages)},
                                 {"aspects", aspects}};
                out.write(QJsonDocument(json).toJson(QJsonDocument::Compact));
                out.close();
                fs::rename(tmp, stored, ec);
            }
        }
        {
            std::lock_guard lock(mtx);
            entries[file] = std::move(entry);
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
        for (int p = 0; p < e->pages.size(); ++p) {
            const QString& text = e->pages[p];
            int n = 0;
            for (qsizetype from = text.indexOf(q, 0, Qt::CaseInsensitive); from >= 0;
                 from = text.indexOf(q, from + q.size(), Qt::CaseInsensitive)) {
                if (h.snippet.isEmpty()) {
                    const qsizetype start = std::max<qsizetype>(0, from - 40);
                    h.snippet = (start > 0 ? QStringLiteral("…") : QString()) + text.mid(start, from - start + q.size() + 60);
                    if (start + (from - start + q.size() + 60) < text.size()) {
                        h.snippet += QStringLiteral("…");
                    }
                }
                ++n;
            }
            if (n > 0) {
                h.count += n;
                ++h.pages;
                if (h.firstPage < 0) {
                    h.firstPage = p;
                }
                h.pageHits.push_back({p, n, p < static_cast<int>(e->aspects.size()) ? e->aspects[static_cast<size_t>(p)] : 0});
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
    return it == entries.end() ? -1 : static_cast<int>(it->second->pages.size());
}

QStringList LibraryIndex::extractText(Document& doc) {
    QStringList pages;
    std::shared_lock lock(doc);
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        PageRef page = doc.getPage(i);
        QString text;
        if (page->getBackgroundType().isPdfPage()) {
            if (XojPdfPageSPtr pdf = doc.getPdfPage(page->getPdfPageNr())) {
                const XojPdfRectangle all(0, 0, pdf->getWidth(), pdf->getHeight());
                text = QString::fromStdString(pdf->selectText(all, XojPdfPageSelectionStyle::Linear));
            }
        }
        for (const Layer* layer: page->getLayers()) {
            for (const auto& e: layer->getElementsView()) {
                if (e->getType() == ELEMENT_TEXT) {
                    text += '\n' + QString::fromStdString(static_cast<const Text*>(e)->getText());
                }
            }
        }
        pages << text;
    }
    return pages;
}

QString LibraryIndex::simplified(const QString& text) { return text.simplified(); }

}  // namespace xqt
