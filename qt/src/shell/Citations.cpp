/*
 * xournal-qt: citations, the QML side (see Citations.h).
 *
 * @license GNU GPLv2 or later
 */
#include "Citations.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QVariantMap>

#include "control/settings/Settings.h"
#include "session/Citation.h"

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

}  // namespace xqt
