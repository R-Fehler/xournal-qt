/*
 * xournal-qt: citations, the QML side (see Citations.h).
 *
 * @license GNU GPLv2 or later
 */
#include "Citations.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QPointer>
#include <QThreadPool>
#include <QVariantMap>

#include "control/settings/Settings.h"
#include "session/Citation.h"
#include "session/FuzzyQuery.h"

#include "Library.h"
#include "LibraryModel.h"
#include "SystemApps.h"

namespace xqt {

namespace {
QString customString(Settings& settings, const char* key, const QString& fallback) {
    std::string v;
    return settings.getCustomElement("xournalQt").getString(key, v) ? QString::fromStdString(v) : fallback;
}
}  // namespace

Citations::Citations(Settings& settings, LibraryModel* library, QObject* parent):
        QObject(parent), settings(settings), library(library) {}

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
