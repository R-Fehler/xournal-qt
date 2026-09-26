/*
 * xournal-qt: citations (qt/docs/citations.md): the look-up addresses of selected text.
 *
 * @license GNU GPLv2 or later
 */
#include <QUrlQuery>
#include <gtest/gtest.h>

#include "session/Citation.h"

using namespace xqt;

namespace {
QString queryItem(const QUrl& url, const QString& key) {
    return QUrlQuery(url).queryItemValue(key, QUrl::FullyDecoded);
}
}  // namespace

TEST(Citation, selectedTextIsCleanedForAQuery) {
    EXPECT_EQ(cite::cleanText("  Attention is all\n  you   need \n"), "Attention is all you need");
    EXPECT_EQ(cite::cleanText("recur-\nrent neural net-\n works"), "recurrent neural networks")
            << "a word broken at a line end is joined";
    EXPECT_EQ(cite::cleanText("Kalman-\nBucy filter"), "Kalman- Bucy filter") << "a capital after the break: a name";
    EXPECT_EQ(cite::cleanText("one two three four", 9), "one two") << "cut at a word";
    EXPECT_EQ(cite::cleanText(""), "");
}

TEST(Citation, googleScholarGetsTheTextAsItsQuery) {
    const QUrl url = cite::scholarUrl("Attention is all\nyou need");
    EXPECT_EQ(url.scheme(), "https");
    EXPECT_EQ(url.host(), "scholar.google.com");
    EXPECT_EQ(url.path(), "/scholar");
    EXPECT_EQ(queryItem(url, "q"), "Attention is all you need");
    const QUrl amp = cite::scholarUrl("R&D: a+b = c?");
    EXPECT_EQ(queryItem(amp, "q"), "R&D: a+b = c?") << "& + = ? are escaped, not taken for the address's own";
    EXPECT_LE(queryItem(cite::scholarUrl(QString(2000, QLatin1Char('x'))), "q").size(), cite::SEARCH_CHARS);
}

TEST(Citation, theTranslatorsAddressHasTheTextAndTheLanguage) {
    const QString google = cite::translatorPattern("google");
    ASSERT_FALSE(google.isEmpty());
    const QUrl url = cite::translateUrl(google, "Die Würde des Menschen & mehr", "en");
    EXPECT_EQ(url.host(), "translate.google.com");
    EXPECT_EQ(queryItem(url, "tl"), "en");
    EXPECT_EQ(queryItem(url, "sl"), "auto");
    EXPECT_EQ(queryItem(url, "text"), "Die Würde des Menschen & mehr");

    const QUrl deepl = cite::translateUrl(cite::translatorPattern("deepl"), "Hallo Welt", "fr");
    EXPECT_EQ(deepl.host(), "www.deepl.com");
    EXPECT_EQ(deepl.toString(QUrl::FullyEncoded), "https://www.deepl.com/translator#auto/fr/Hallo%20Welt");

    // Without a language: the system's
    EXPECT_EQ(queryItem(cite::translateUrl(google, "x", ""), "tl"), cite::systemLanguage());
    EXPECT_FALSE(cite::systemLanguage().isEmpty());
}

TEST(Citation, aCustomTranslatorMustBeAWebAddressWithTheText) {
    EXPECT_EQ(cite::translatorPattern("https://example.org/t?q={text}&to={lang}"),
              "https://example.org/t?q={text}&to={lang}");
    EXPECT_EQ(queryItem(cite::translateUrl("https://example.org/t?q={text}&to={lang}", "a b", "de"), "q"), "a b");
    EXPECT_TRUE(cite::translatorPattern("https://example.org/t?to={lang}").isEmpty()) << "no {text}";
    EXPECT_TRUE(cite::translatorPattern("file:///etc/passwd?{text}").isEmpty()) << "not a web address";
    EXPECT_TRUE(cite::translatorPattern("javascript:alert({text})").isEmpty());
    EXPECT_TRUE(cite::translatorPattern("").isEmpty());
    EXPECT_FALSE(cite::translateUrl("", "x", "de").isValid()) << "no translator: no address";
}

TEST(Citation, onlyWebAddressesAreOpened) {
    EXPECT_TRUE(cite::isWebAddress(QUrl("https://scholar.google.com/scholar?q=x")));
    EXPECT_TRUE(cite::isWebAddress(QUrl("http://export.arxiv.org/api/query")));
    EXPECT_FALSE(cite::isWebAddress(QUrl("file:///home/x.pdf")));
    EXPECT_FALSE(cite::isWebAddress(QUrl("mailto:a@b.org")));
    EXPECT_FALSE(cite::isWebAddress(QUrl("https://")));
}
