/*
 * xournal-qt: citations: queries and addresses (see Citation.h).
 *
 * @license GNU GPLv2 or later
 */
#include "Citation.h"

#include <QLocale>
#include <QRegularExpression>

namespace xqt::cite {

QString cleanText(const QString& text, int max) {
    QString t = text;
    // A word broken at a line end: "hyphen-\nation" (a lower-case letter after the break: not "Kalman-\nBucy")
    static const QRegularExpression broken(QStringLiteral("(\\p{L})[-\\x{00AD}\\x{2010}]\\s*\\n\\s*(\\p{Ll})"));
    t.replace(broken, QStringLiteral("\\1\\2"));
    t.remove(QChar(0x00AD));  // soft hyphens elsewhere
    t = t.simplified();
    if (max > 0 && t.size() > max) {
        qsizetype cut = t.lastIndexOf(QLatin1Char(' '), max);
        t = t.left(cut > max / 2 ? cut : max);
    }
    return t;
}

QUrl scholarUrl(const QString& text) {
    QUrl url(QStringLiteral("https://scholar.google.com/scholar"));
    const QString q = cleanText(text, SEARCH_CHARS);
    url.setQuery(QStringLiteral("q=") + QString::fromLatin1(QUrl::toPercentEncoding(q)), QUrl::StrictMode);
    return url;
}

const std::vector<Translator>& translators() {
    static const std::vector<Translator> list{
            {QStringLiteral("google"), QStringLiteral("Google Translate"),
             QStringLiteral("https://translate.google.com/?sl=auto&tl={lang}&text={text}&op=translate")},
            {QStringLiteral("deepl"), QStringLiteral("DeepL"),
             QStringLiteral("https://www.deepl.com/translator#auto/{lang}/{text}")},
            {QStringLiteral("bing"), QStringLiteral("Bing Translator"),
             QStringLiteral("https://www.bing.com/translator?from=auto-detect&to={lang}&text={text}")},
    };
    return list;
}

QString translatorPattern(const QString& setting) {
    const QString s = setting.trimmed();
    for (const Translator& t: translators()) {
        if (t.key == s) {
            return t.pattern;
        }
    }
    const QUrl url(QString(s).replace(QStringLiteral("{text}"), QStringLiteral("x"))
                           .replace(QStringLiteral("{lang}"), QStringLiteral("en")));
    return s.contains(QStringLiteral("{text}")) && isWebAddress(url) ? s : QString();
}

QUrl translateUrl(const QString& pattern, const QString& text, const QString& language) {
    if (pattern.isEmpty()) {
        return {};
    }
    const QString lang = language.isEmpty() ? systemLanguage() : language;
    QString address = pattern;
    address.replace(QStringLiteral("{lang}"), QString::fromLatin1(QUrl::toPercentEncoding(lang)));
    address.replace(QStringLiteral("{text}"),
                    QString::fromLatin1(QUrl::toPercentEncoding(cleanText(text, TRANSLATE_CHARS))));
    return QUrl::fromEncoded(address.toUtf8(), QUrl::StrictMode);
}

QString systemLanguage() {
    const QLocale locale = QLocale::system();
    const QString name = locale.name();  // "de_DE", "zh_CN", "C"
    QString lang = name.section(QLatin1Char('_'), 0, 0);
    if (lang.isEmpty() || lang == QLatin1String("C")) {
        return QStringLiteral("en");
    }
    if (lang == QLatin1String("zh")) {
        // Translators tell the two scripts apart
        return locale.script() == QLocale::TraditionalChineseScript ? QStringLiteral("zh-TW") : QStringLiteral("zh-CN");
    }
    return lang;
}

bool isWebAddress(const QUrl& url) {
    const QString scheme = url.scheme().toLower();
    return url.isValid() && (scheme == QLatin1String("http") || scheme == QLatin1String("https")) &&
           !url.host().isEmpty();
}

}  // namespace xqt::cite
