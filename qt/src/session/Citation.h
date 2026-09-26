/*
 * xournal-qt: citations (qt/docs/citations.md) - what selected text is looked up as: the query of a web search, the
 * address of a translator; the title of a bibliography entry, and how well a document's title matches it.
 *
 * Pure functions, no network: the addresses built here are shown to the user before anything opens them.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include <QSet>
#include <QStringList>

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

// --- the title of a bibliography entry --------------------------------------------------------------------------

/// What an entry was read as.
struct TitleGuess {
    QString title;  ///< the likely title; without a better guess the cleaned entry
    QString raw;    ///< the cleaned entry (a leading label such as "[12]" dropped)
    /// How it was found: "quoted", "year" (APA, ACM: after the year), "colon" (LNCS: after the authors' colon),
    /// "sentence" (the first sentence after the authors), "" (none: the whole entry)
    QString how;
};
/// The likely title of a bibliography entry (IEEE, APA, ACM, LNCS, Nature, arXiv styles, English and German).
TitleGuess guessTitle(const QString& entry);

// --- matching titles -----------------------------------------------------------------------------------------------

/// The words of a text that count when titles are compared: case folded as the search folds them (TextMatch), without
/// stop words (English, German) and one-letter words.
QStringList titleWords(const QString& text);
/// How alike two words (titleWords) are: 1 the same; 0.9 one starts with the other, which has at most 3 letters more
/// ("network", "networks"; 4+ letters); 0.7 a typo (WordMatch's edit distance, with the typo tolerance `typos`); 0.
double wordLikeness(const QString& a, const QString& b, int typos);
/// How well a document's candidate matches the words of a guessed title (0..1): the share of the query's words found
/// in it; for a title-like candidate (`titleLike`: its /Title, its largest text, its name) 0.75 of that plus 0.25 of
/// the share of its own words found in the query (a long heading that happens to contain the words ranks lower);
/// else 0.9 of it (a page of text says nothing by its length).
double titleMatch(const QStringList& query, const QStringList& candidate, bool titleLike, int typos);
/// The share of a title's words that are in a text's words (the whole entry: a title all of whose words are in the
/// entry is its title, whatever the entry's style). 0 for a title of fewer than 3 words.
double titleInText(const QStringList& title, const QSet<QString>& text, int typos);

// --- arXiv ---------------------------------------------------------------------------------------------------

/// An arXiv identifier: "1706.03762" (new style) or "hep-th/9901001" (old style), with its version if one was given.
struct ArxivId {
    QString id;       ///< without the version
    QString version;  ///< "v2", or ""
    QString full() const { return id + version; }
    bool operator==(const ArxivId& o) const { return id == o.id && version == o.version; }
};
/// The arXiv IDs in a text, in order, each once: "arXiv:2401.12345", "arXiv:2401.12345v2", "arXiv:hep-th/9901001",
/// arxiv.org/abs/… and /pdf/… addresses; a bare number ("2401.12345v2", "hep-th/9901001") only when the text says
/// "arXiv" (a page range "1234.5678" in a reference is no ID), a bare old-style ID always.
std::vector<ArxivId> arxivIds(const QString& text);

/// arXiv's export API: a search by title words (the title's words without stop words, at most 8, AND-ed), and the
/// entry of one ID. Atom feeds (parseArxivFeed).
QUrl arxivSearchUrl(const QString& title, int maxResults = 10);
QUrl arxivIdUrl(const ArxivId& id);
/// A paper's page and its PDF.
QUrl arxivAbsUrl(const ArxivId& id);
QUrl arxivPdfUrl(const ArxivId& id);

/// An entry of arXiv's Atom feed.
struct ArxivPaper {
    ArxivId id;
    QString title;       ///< whitespace collapsed
    QStringList authors;
    QString year;        ///< of its first version ("2017")
    QString summary;     ///< the abstract, whitespace collapsed
    QUrl pdf;            ///< its PDF (from the feed's link, else arxivPdfUrl)
};
/// The papers of an Atom feed of the export API; `error` says why there are none when the feed is an error or not a
/// feed at all ("" when it simply has no entries).
std::vector<ArxivPaper> parseArxivFeed(const QByteArray& atom, QString* error = nullptr);

/// The file name of a downloaded paper: its title with what file systems do not allow (/ \ : * ? " < > | and control
/// characters) left out, whitespace collapsed, at most 120 characters (cut at a word), without dots and spaces at
/// its end, then the ID without its version ("/" as "_"): "Attention Is All You Need (1706.03762).pdf". Without a
/// title: "arXiv 1706.03762.pdf".
QString downloadName(const QString& title, const ArxivId& id);

}  // namespace xqt::cite
