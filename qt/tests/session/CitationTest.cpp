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

// --- the title of a bibliography entry ----------------------------------------------------------------------------

namespace {
struct Entry {
    const char* style;
    const char* text;
    const char* title;
};
}  // namespace

// Real bibliography entries of the common styles, as they are selected in a PDF (line breaks included)
TEST(Citation, theTitleOfABibliographyEntryIsFound) {
    const Entry entries[] = {
            {"IEEE, straight quotes",
             "[1] A. Vaswani, N. Shazeer, N. Parmar, J. Uszkoreit, L. Jones, A. N. Gomez, Ł. Kaiser, and I. Polosukhin,\n"
             "\"Attention is all you need,\" in Advances in Neural Information Processing Systems, 2017, pp. 5998–6008.",
             "Attention is all you need"},
            {"IEEE, curly quotes",
             "[12] K. He, X. Zhang, S. Ren, and J. Sun, “Deep residual learning for image recognition,” in Proc. "
             "IEEE Conf. Comput. Vis. Pattern Recognit. (CVPR), Jun. 2016, pp. 770–778.",
             "Deep residual learning for image recognition"},
            {"IEEE, a word broken at the line end",
             "[7] G. Hinton, O. Vinyals, and J. Dean, “Distilling the knowl-\nedge in a neural network,” arXiv "
             "preprint arXiv:1503.02531, 2015.",
             "Distilling the knowledge in a neural network"},
            {"APA, proceedings",
             "Devlin, J., Chang, M.-W., Lee, K., & Toutanova, K. (2019). BERT: Pre-training of deep bidirectional "
             "transformers for language understanding. In Proceedings of NAACL-HLT (pp. 4171–4186).",
             "BERT: Pre-training of deep bidirectional transformers for language understanding"},
            {"APA, journal",
             "Kalman, R. E. (1960). A new approach to linear filtering and prediction problems. Journal of Basic "
             "Engineering, 82(1), 35–45. https://doi.org/10.1115/1.3662552",
             "A new approach to linear filtering and prediction problems"},
            {"ACM",
             "Ashish Vaswani, Noam Shazeer, Niki Parmar, Jakob Uszkoreit, Llion Jones, Aidan N. Gomez, Łukasz Kaiser, and "
             "Illia Polosukhin. 2017. Attention is all you need. In Proceedings of the 31st International Conference on "
             "Neural Information Processing Systems (NIPS'17). Curran Associates Inc., Red Hook, NY, USA, 6000–6010.",
             "Attention is all you need"},
            {"ACM, a title with commas",
             "Leslie Lamport. 1978. Time, clocks, and the ordering of events in a distributed system. Commun. ACM 21, 7 "
             "(July 1978), 558–565. https://doi.org/10.1145/359545.359563",
             "Time, clocks, and the ordering of events in a distributed system"},
            {"arXiv (natbib)",
             "Tom B. Brown, Benjamin Mann, Nick Ryder, et al. Language models are few-shot learners. arXiv preprint "
             "arXiv:2005.14165, 2020.",
             "Language models are few-shot learners"},
            {"arXiv, initials before a capitalised title",
             "Kingma, D. P. and Ba, J. Adam: A method for stochastic optimization. arXiv:1412.6980, 2014.",
             "Adam: A method for stochastic optimization"},
            {"LNCS (Springer)",
             "Vaswani, A., Shazeer, N., Parmar, N., et al.: Attention is all you need. In: Advances in Neural "
             "Information Processing Systems, pp. 5998–6008 (2017)",
             "Attention is all you need"},
            {"Nature",
             "3. LeCun, Y., Bengio, Y. & Hinton, G. Deep learning. Nature 521, 436–444 (2015).", "Deep learning"},
            {"Chicago, the period inside the quotes",
             "Shannon, Claude E. 1948. “A Mathematical Theory of Communication.” Bell System Technical Journal "
             "27 (3): 379–423.",
             "A Mathematical Theory of Communication"},
            {"German, low quotes",
             "Einstein, A. (1905): „Zur Elektrodynamik bewegter Körper“. In: Annalen der Physik 17, "
             "S. 891–921.",
             "Zur Elektrodynamik bewegter Körper"},
            {"German, DIN 1505 (the authors' colon)",
             "Schmidt, Peter ; Müller, Hans: Grundlagen der Regelungstechnik. 2. Aufl. Berlin : Springer, 2010",
             "Grundlagen der Regelungstechnik"},
            {"German, author-year with u. a.",
             "Müller, H. u. a. (2018): Maschinelles Lernen in der Praxis. München: Hanser.",
             "Maschinelles Lernen in der Praxis"},
    };
    for (const Entry& e: entries) {
        const cite::TitleGuess g = cite::guessTitle(QString::fromUtf8(e.text));
        EXPECT_EQ(g.title.toStdString(), e.title) << e.style << " (found as \"" << g.how.toStdString() << "\")";
        EXPECT_FALSE(g.raw.startsWith(QLatin1Char('['))) << e.style << ": the label is dropped from the raw text";
    }
}

TEST(Citation, withoutABetterGuessTheTitleIsTheWholeEntry) {
    const auto g = cite::guessTitle("Kalman filter tutorial");
    EXPECT_EQ(g.title, "Kalman filter tutorial");
    EXPECT_EQ(g.raw, "Kalman filter tutorial");
    EXPECT_EQ(cite::guessTitle("  ").title, "");
}

// --- matching titles ------------------------------------------------------------------------------------------------

TEST(Citation, titleWordsAreFoldedWithoutStopWords) {
    EXPECT_EQ(cite::titleWords("Attention Is All You Need"), QStringList({"attention", "all", "you", "need"}));
    EXPECT_EQ(cite::titleWords("Zur Elektrodynamik der bewegten Körper"),
              QStringList({"elektrodynamik", "bewegten", "körper"}));
    EXPECT_EQ(cite::titleWords("ﬁnite-element analysis"), QStringList({"finite", "element", "analysis"}))
            << "ligatures written out";
}

TEST(Citation, wordsAreAlikeWithEndingsAndTypos) {
    EXPECT_EQ(cite::wordLikeness("network", "network", 1), 1.0);
    EXPECT_EQ(cite::wordLikeness("network", "networks", 1), 0.9);
    EXPECT_EQ(cite::wordLikeness("learners", "learner", 1), 0.9);
    EXPECT_EQ(cite::wordLikeness("attention", "attentoin", 1), 0.7) << "a swap";
    EXPECT_EQ(cite::wordLikeness("attention", "attentoin", 0), 0.0) << "no typos allowed";
    EXPECT_EQ(cite::wordLikeness("net", "internet", 1), 0.0);
    EXPECT_EQ(cite::wordLikeness("all", "ale", 1), 0.0) << "short words have no typos";
}

TEST(Citation, aTitleMatchesItsDocumentsTitleBestAndToleratesTypos) {
    const QStringList query = cite::titleWords("Attention is all you need");
    EXPECT_DOUBLE_EQ(cite::titleMatch(query, cite::titleWords("Attention Is All You Need"), true, 1), 1.0);
    EXPECT_GT(cite::titleMatch(cite::titleWords("Attenton is all you need"), cite::titleWords("Attention Is All You Need"), true, 1), 0.85);
    const double longer = cite::titleMatch(query, cite::titleWords("Attention is all you need in speech separation "
                                                                    "with transformers for noisy rooms"), true, 1);
    EXPECT_LT(longer, 0.9) << "another paper with those words in a longer title ranks lower";
    EXPECT_GE(longer, 0.75);
    EXPECT_LT(cite::titleMatch(query, cite::titleWords("Deep residual learning for image recognition"), true, 1), 0.2);
    // A page of text: the share of the title's words in it
    EXPECT_NEAR(cite::titleMatch(query, cite::titleWords("the attention we need is not all of it, you see"), false, 1),
                0.9, 1e-9);

    // The whole entry holds a document's title: found whatever the style
    const QStringList entry = cite::titleWords(
            "[3] D. P. Kingma and J. Ba, Adam: A method for stochastic optimization, in Proc. ICLR, 2015.");
    const QSet<QString> entryWords(entry.begin(), entry.end());
    EXPECT_DOUBLE_EQ(cite::titleInText(cite::titleWords("Adam: A Method for Stochastic Optimization"), entryWords, 1), 1.0);
    EXPECT_LT(cite::titleInText(cite::titleWords("Attention Is All You Need"), entryWords, 1), 0.3);
    EXPECT_EQ(cite::titleInText(cite::titleWords("Adam"), entryWords, 1), 0.0) << "too short to say";
}
