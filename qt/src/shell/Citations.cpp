/*
 * xournal-qt: citations, the QML side (see Citations.h).
 *
 * @license GNU GPLv2 or later
 */
#include "Citations.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QGuiApplication>
#include <QPointer>
#include <QThreadPool>
#include <QVariantMap>

#include "control/settings/Settings.h"
#include "session/Citation.h"
#include "session/FuzzyQuery.h"

#include "Library.h"
#include "LibraryModel.h"
#include "NetFetch.h"
#include "SystemApps.h"

namespace xqt {

namespace {
QString customString(Settings& settings, const char* key, const QString& fallback) {
    std::string v;
    return settings.getCustomElement("xournalQt").getString(key, v) ? QString::fromStdString(v) : fallback;
}
}  // namespace

Citations::Citations(Settings& settings, LibraryModel* library, QObject* parent):
        QObject(parent), settings(settings), library(library), queue(new ArxivQueue(this)) {
    connect(queue, &ArxivQueue::waitingChanged, this, &Citations::arxivChanged);
}

Citations::~Citations() = default;

QString Citations::scholarUrl(const QString& text) const {
    return cite::cleanText(text).isEmpty() ? QString() : cite::scholarUrl(text).toString(QUrl::FullyEncoded);
}

QString Citations::translateUrl(const QString& text) const {
    if (cite::cleanText(text).isEmpty()) {
        return {};
    }
    const QString pattern = cite::translatorPattern(customString(settings, "translateService", QStringLiteral("google")));
    const QUrl url = cite::translateUrl(pattern, text, customString(settings, "translateLanguage", QString()).trimmed());
    return url.isValid() ? url.toString(QUrl::FullyEncoded) : QString();
}

bool Citations::openWeb(const QString& url) {
    const QUrl u = QUrl::fromEncoded(url.toUtf8(), QUrl::StrictMode);
    if (!cite::isWebAddress(u) || !SystemApps::instance().openWebAddress(u)) {
        return false;
    }
    Q_EMIT webOpened(url);
    return true;
}

QString Citations::displayUrl(const QString& url) const {
    // The escapes of the query and the fragment decoded, so the text reads as text ("q=Attention, is all")
    const QUrl u = QUrl::fromEncoded(url.toUtf8());
    QString shown = u.toString(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::PrettyDecoded);
    if (u.hasQuery()) {
        shown += QLatin1Char('?') + u.query(QUrl::FullyDecoded);
    }
    if (u.hasFragment()) {
        shown += QLatin1Char('#') + u.fragment(QUrl::FullyDecoded);
    }
    return shown;
}

QString Citations::hostOf(const QString& url) const { return QUrl::fromEncoded(url.toUtf8()).host(); }

void Citations::copyText(const QString& text) const {
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

QVariantList Citations::translators() const {
    QVariantList list;
    for (const cite::Translator& t: cite::translators()) {
        list << QVariantMap{{QStringLiteral("key"), t.key}, {QStringLiteral("name"), t.name}};
    }
    return list;
}

QString Citations::systemLanguage() const { return cite::systemLanguage(); }

QString Citations::cleanText(const QString& text) const { return cite::cleanText(text); }

QVariantMap Citations::guessTitle(const QString& entry) const {
    const cite::TitleGuess g = cite::guessTitle(entry);
    return {{QStringLiteral("title"), g.title}, {QStringLiteral("raw"), g.raw}, {QStringLiteral("how"), g.how}};
}

void Citations::findPapers(const QString& title, const QString& raw, const QStringList& exclude) {
    const quint64 generation = ++searchGeneration;
    LibraryIndex* index = library ? library->searchIndex() : nullptr;
    if (!index || !library->library() || title.trimmed().isEmpty()) {
        hits.clear();
        searching = false;
        Q_EMIT papersChanged();
        return;
    }
    const fs::path root = library->library()->root();
    std::set<fs::path> excluded;
    for (const QString& f: exclude) {
        excluded.insert(fs::path(f.toStdString()));
    }
    auto search = index->titleSearch(title, raw, FuzzyQuery::typoTolerance(), 0.5, 20, excluded);
    searching = true;
    Q_EMIT papersChanged();
    QThreadPool::globalInstance()->start([self = QPointer<Citations>(this), search = std::move(search), root,
                                          generation] {
        QVariantList found;
        for (const LibraryIndex::TitleHit& h: search()) {
            const fs::path folder = h.file.parent_path().lexically_relative(root);
            found << QVariantMap{{QStringLiteral("path"), QString::fromStdString(h.file.string())},
                                 {QStringLiteral("title"), h.title},
                                 {QStringLiteral("folder"),
                                  folder == "." ? QString() : QString::fromStdString(folder.generic_string())},
                                 {QStringLiteral("fileName"), QString::fromStdString(h.file.filename().string())},
                                 {QStringLiteral("score"), static_cast<int>(h.score * 100 + 0.5)},
                                 {QStringLiteral("matched"), h.matched}};
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, found = std::move(found), generation] {
            if (!self || self->searchGeneration != generation) {
                return;  // (a newer search, or gone)
            }
            self->hits = found;
            self->searching = false;
            Q_EMIT self->papersChanged();
        });
    });
}

}  // namespace xqt

namespace xqt {

// --- arXiv -------------------------------------------------------------------------------------------------------

namespace {
constexpr int API_TIMEOUT_MS = 20000;
constexpr int PDF_TIMEOUT_MS = 120000;
constexpr qint64 API_MAX_BYTES = 4 * 1024 * 1024;
constexpr qint64 PDF_MAX_BYTES = 300 * 1024 * 1024;

QVariantMap paperMap(const cite::ArxivPaper& p) {
    QString authors = p.authors.mid(0, 3).join(QStringLiteral(", "));
    if (p.authors.size() > 3) {
        authors += QStringLiteral(" et al.");
    }
    return {{QStringLiteral("id"), p.id.id},
            {QStringLiteral("full"), p.id.full()},
            {QStringLiteral("title"), p.title},
            {QStringLiteral("authors"), authors},
            {QStringLiteral("year"), p.year},
            {QStringLiteral("absUrl"), cite::arxivAbsUrl(p.id).toString(QUrl::FullyEncoded)},
            {QStringLiteral("pdfUrl"), p.pdf.toString(QUrl::FullyEncoded)},
            {QStringLiteral("fileName"), cite::downloadName(p.title, p.id)}};
}
}  // namespace

QVariantList Citations::arxivIdsIn(const QString& text) const {
    QVariantList list;
    for (const cite::ArxivId& id: cite::arxivIds(text)) {
        list << QVariantMap{{QStringLiteral("id"), id.id},
                            {QStringLiteral("full"), id.full()},
                            {QStringLiteral("absUrl"), cite::arxivAbsUrl(id).toString(QUrl::FullyEncoded)},
                            {QStringLiteral("pdfUrl"), cite::arxivPdfUrl(id).toString(QUrl::FullyEncoded)},
                            {QStringLiteral("lookUpUrl"), cite::arxivIdUrl(id).toString(QUrl::FullyEncoded)}};
    }
    return list;
}

QString Citations::arxivSearchUrl(const QString& title) const {
    return cite::arxivSearchUrl(title).toString(QUrl::FullyEncoded);
}

QString Citations::arxivLookUpUrl(const QString& fullId) const {
    const std::vector<cite::ArxivId> ids = cite::arxivIds(QStringLiteral("arXiv:") + fullId);
    return ids.empty() ? QString() : cite::arxivIdUrl(ids.front()).toString(QUrl::FullyEncoded);
}

QString Citations::networkAccess() const {
    const QString v = customString(settings, "networkAccess", QStringLiteral("ask"));
    return v == QLatin1String("on") || v == QLatin1String("off") ? v : QStringLiteral("ask");
}

bool Citations::networkOn() {
    if (networkAccess() == QLatin1String("on")) {
        return true;
    }
    error = networkAccess() == QLatin1String("off")
                    ? tr("Connecting to arXiv is turned off (Settings → Documents → Web and citations).")
                    : tr("Connecting to arXiv was not allowed yet.");
    Q_EMIT arxivChanged();
    return false;
}

bool Citations::arxivWaiting() const { return queue->waiting(); }

void Citations::fetchFeed(const QUrl& url) {
    const quint64 generation = ++arxivGeneration;
    results.clear();
    error.clear();
    ++busy;
    Q_EMIT arxivChanged();
    queue->get(url, API_TIMEOUT_MS, API_MAX_BYTES, [self = QPointer<Citations>(this), generation](const NetFetch::Reply& r) {
        if (!self) {
            return;
        }
        --self->busy;
        if (generation != self->arxivGeneration) {
            Q_EMIT self->arxivChanged();
            return;  // (a newer search)
        }
        if (!r.error.isEmpty()) {
            self->error = r.error;
        } else {
            QString problem;
            for (const cite::ArxivPaper& p: cite::parseArxivFeed(r.body, &problem)) {
                self->results << paperMap(p);
            }
            self->error = problem;
        }
        Q_EMIT self->arxivChanged();
    });
}

bool Citations::arxivSearch(const QString& title) {
    const QUrl url = cite::arxivSearchUrl(title);
    if (!url.isValid() || !networkOn()) {
        return false;
    }
    fetchFeed(url);
    return true;
}

bool Citations::arxivLookUp(const QString& fullId) {
    const QString address = arxivLookUpUrl(fullId);
    if (address.isEmpty() || !networkOn()) {
        return false;
    }
    fetchFeed(QUrl::fromEncoded(address.toUtf8()));
    return true;
}

QStringList Citations::libraryFolders() const {
    QStringList folders{QString()};
    if (library && library->library()) {
        const fs::path root = library->library()->root();
        for (const fs::path& f: DocumentFiles::foldersRecursive(root)) {
            folders << QString::fromStdString(f.lexically_relative(root).generic_string());
        }
    }
    return folders;
}

QString Citations::currentFolder() const { return library ? library->folder() : QString(); }

QString Citations::downloadPath(int index, const QString& folder) const {
    if (index < 0 || index >= results.size() || !library || !library->library()) {
        return {};
    }
    const QString name = results[index].toMap().value(QStringLiteral("fileName")).toString();
    fs::path dir = library->library()->root();
    if (!folder.isEmpty()) {
        dir /= fs::path(folder.toStdString());
    }
    return QString::fromStdString((dir / fs::path(name.toStdString())).lexically_normal().string());
}

bool Citations::downloadExists(int index, const QString& folder) const {
    const QString path = downloadPath(index, folder);
    return !path.isEmpty() && QFileInfo::exists(path);
}

bool Citations::arxivDownload(int index, const QString& folder) {
    const QString path = downloadPath(index, folder);
    if (path.isEmpty()) {
        return false;
    }
    if (QFileInfo::exists(path)) {
        downloaded = path;  // (named by the paper's ID: the paper is there)
        error.clear();
        Q_EMIT arxivChanged();
        Q_EMIT paperDownloaded(path);
        return true;
    }
    if (!networkOn()) {
        return false;
    }
    const QUrl pdf = QUrl::fromEncoded(results[index].toMap().value(QStringLiteral("pdfUrl")).toString().toUtf8());
    if (!cite::isWebAddress(pdf)) {
        return false;
    }
    error.clear();
    downloaded.clear();
    ++busy;
    Q_EMIT arxivChanged();
    queue->get(pdf, PDF_TIMEOUT_MS, PDF_MAX_BYTES, [self = QPointer<Citations>(this), path](const NetFetch::Reply& r) {
        if (!self) {
            return;
        }
        auto fail = [&](const QString& why) {
            --self->busy;
            self->error = why;
            Q_EMIT self->arxivChanged();
        };
        if (!r.error.isEmpty()) {
            fail(r.error);
            return;
        }
        if (!r.body.startsWith("%PDF-")) {
            fail(tr("arXiv did not send a PDF (the paper may be withdrawn, or only its source is there)."));
            return;
        }
        // Written on a worker thread (a PDF of some MB), under another name first
        QThreadPool::globalInstance()->start([self, path, body = r.body] {
            QString problem;
            QDir().mkpath(QFileInfo(path).absolutePath());
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(body) != body.size() || !file.commit()) {
                problem = tr("The PDF could not be saved: %1").arg(file.errorString());
            }
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, path, problem] {
                if (!self) {
                    return;
                }
                --self->busy;
                self->error = problem;
                if (problem.isEmpty()) {
                    self->downloaded = path;
                    if (self->library) {
                        self->library->refresh();  // (indexed like any new document)
                    }
                }
                Q_EMIT self->arxivChanged();
                if (problem.isEmpty()) {
                    Q_EMIT self->paperDownloaded(path);
                }
            });
        });
    });
    return true;
}

}  // namespace xqt
