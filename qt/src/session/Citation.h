/*
 * xournal-qt: citations (qt/docs/citations.md) - what selected text is looked up as: the query of a web search, the
 * address of a translator.
 *
 * Pure functions, no network: the addresses built here are shown to the user before anything opens them.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include <QString>
#include <QUrl>

namespace xqt::cite {

/// Selected text as a query: a word broken at a line end with a hyphen joined ("hyphen-\nation" → "hyphenation"),
/// whitespace runs to one space, trimmed; cut to at most `max` characters at a word (0: not cut).
QString cleanText(const QString& text, int max = 0);

/// The longest text sent to a web search and to a translator (addresses have a length limit).
constexpr int SEARCH_CHARS = 500;
constexpr int TRANSLATE_CHARS = 1500;

/// Google Scholar's search for the text.
QUrl scholarUrl(const QString& text);

/// A translator the settings offer: its key (the setting's value), its name, and its address with `{text}` and
/// `{lang}` in it.
struct Translator {
    QString key;
    QString name;
    QString pattern;
};
const std::vector<Translator>& translators();
/// The address pattern of the setting `translateService`: a translator's key, or a custom pattern. "" when it is
/// neither (a custom one must be http(s) and have `{text}`).
QString translatorPattern(const QString& setting);
/// The translator's address for the text into the language (a code: "de", "en", "pt-BR"; "" = auto).
QUrl translateUrl(const QString& pattern, const QString& text, const QString& language);
/// The system's language as a translator takes it: "de" for de_DE, "en" for en_US, "zh-CN" / "zh-TW", "pt".
QString systemLanguage();

/// A web address the app may hand to the browser: http or https with a host.
bool isWebAddress(const QUrl& url);

}  // namespace xqt::cite
