/*
 * xournal-qt: citations (qt/docs/citations.md): the look-up addresses of selected text, the title of a bibliography
 * entry and how titles match, arXiv IDs, arXiv's answers (saved ones: no network) and the names of downloads.
 *
 * @license GNU GPLv2 or later
 */
#include <QUrlQuery>
#include <gtest/gtest.h>

#include "session/Citation.h"

#include "../ArxivSamples.h"

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

// --- arXiv ------------------------------------------------------------------------------------------------------

namespace {
QStringList ids(const QString& text) {
    QStringList out;
    for (const cite::ArxivId& id: cite::arxivIds(text)) {
        out << id.full();
    }
    return out;
}
}  // namespace

TEST(Citation, arxivIdsAreRecognised) {
    EXPECT_EQ(ids("arXiv:2401.12345"), QStringList{"2401.12345"});
    EXPECT_EQ(ids("arXiv:2401.12345v2"), QStringList{"2401.12345v2"});
    EXPECT_EQ(ids("arXiv: 1706.03762"), QStringList{"1706.03762"}) << "a space after the colon (as PDFs break it)";
    EXPECT_EQ(ids("A. Vaswani et al. Attention is all you need. arXiv preprint arXiv:1706.03762, 2017."),
              QStringList{"1706.03762"});
    EXPECT_EQ(ids("CoRR abs/1412.6980 (arXiv), 2014"), QStringList{"1412.6980"}) << "four digits until 2014";
    EXPECT_EQ(ids("https://arxiv.org/abs/2005.14165v4"), QStringList{"2005.14165v4"});
    EXPECT_EQ(ids("see arxiv.org/pdf/1512.03385.pdf"), QStringList{"1512.03385"});
    EXPECT_EQ(ids("2401.12345v2 (arXiv)"), QStringList{"2401.12345v2"}) << "bare, with arXiv in the text";
    // Old style, also bare
    EXPECT_EQ(ids("E. Witten, hep-th/9802150"), QStringList{"hep-th/9802150"});
    EXPECT_EQ(ids("arXiv:math.AG/0601001v2"), QStringList{"math.AG/0601001v2"});
    EXPECT_EQ(ids("https://arxiv.org/abs/cond-mat/0102536"), QStringList{"cond-mat/0102536"});
    // Not IDs
    EXPECT_EQ(ids("J. Basic Eng. 82(1), pp. 1234.5678, 1960"), QStringList{}) << "no arXiv in the text";
    EXPECT_EQ(ids("Proc. 2017, 1706.03762"), QStringList{}) << "a bare number without arXiv";
    EXPECT_EQ(ids("arXiv:1713.01234"), QStringList{}) << "no month 13";
    EXPECT_EQ(ids("arXiv:1401.12345"), QStringList{}) << "five digits only since 2015";
    EXPECT_EQ(ids("arXiv:1501.1234"), QStringList{}) << "four digits only until 2014";
    EXPECT_EQ(ids("foo/9901001"), QStringList{}) << "not an archive";
    // Each once, in order
    EXPECT_EQ(ids("arXiv:1706.03762 and arxiv.org/abs/1706.03762v7, then hep-th/9901001 and arXiv:2005.14165"),
              QStringList({"1706.03762", "hep-th/9901001", "2005.14165"}));
}

TEST(Citation, arxivAddressesAreItsApisAndPages) {
    const QUrl search = cite::arxivSearchUrl("Attention is all you need");
    EXPECT_EQ(search.toString(QUrl::FullyEncoded),
              "https://export.arxiv.org/api/query?search_query=ti:attention+AND+ti:all+AND+ti:you+AND+ti:need"
              "&start=0&max_results=10");
    EXPECT_EQ(cite::arxivSearchUrl("Über die Wärmeleitung in Metallen").toString(QUrl::FullyEncoded),
              "https://export.arxiv.org/api/query?search_query=ti:w%C3%A4rmeleitung+AND+ti:metallen"
              "&start=0&max_results=10")
            << "stop words left out, letters beyond ASCII escaped";
    const QUrl longTitle = cite::arxivSearchUrl("one two three four five six seven eight nine ten");
    EXPECT_EQ(longTitle.toString(QUrl::FullyEncoded).count("ti:"), 8) << "at most 8 words";
    EXPECT_FALSE(cite::arxivSearchUrl("the of and").isValid()) << "nothing to search";

    const cite::ArxivId id{"1706.03762", "v7"};
    EXPECT_EQ(cite::arxivIdUrl(id).toString(), "https://export.arxiv.org/api/query?id_list=1706.03762v7");
    EXPECT_EQ(cite::arxivAbsUrl(id).toString(), "https://arxiv.org/abs/1706.03762v7");
    EXPECT_EQ(cite::arxivPdfUrl(id).toString(), "https://arxiv.org/pdf/1706.03762v7");
    EXPECT_EQ(cite::arxivPdfUrl({"hep-th/9901001", ""}).toString(), "https://arxiv.org/pdf/hep-th/9901001");
}

TEST(Citation, arxivsAtomAnswerIsRead) {
    QString error;
    const auto papers = cite::parseArxivFeed(test::ARXIV_SEARCH, &error);
    EXPECT_EQ(error, "");
    ASSERT_EQ(papers.size(), 3u);
    const cite::ArxivPaper& p = papers[0];
    EXPECT_EQ(p.id.id, "1706.03762");
    EXPECT_EQ(p.id.version, "v7");
    EXPECT_EQ(p.title, "Attention Is All You Need");
    EXPECT_EQ(p.authors, QStringList({"Ashish Vaswani", "Noam Shazeer", "Niki Parmar", "Jakob Uszkoreit", "Llion Jones"}));
    EXPECT_EQ(p.year, "2017");
    EXPECT_TRUE(p.summary.startsWith("The dominant sequence transduction models"));
    EXPECT_EQ(p.pdf.toString(), "https://arxiv.org/pdf/1706.03762v7") << "https, not the feed's http";
    EXPECT_EQ(papers[1].title, "Attention is All You Need in Speech Separation: a Study of Transformers")
            << "a title broken over lines is one line";
    EXPECT_EQ(papers[2].id.id, "hep-th/9901001");
    EXPECT_EQ(papers[2].id.version, "v1");

    // An error of the API: no papers, and why
    EXPECT_TRUE(cite::parseArxivFeed(test::ARXIV_ERROR, &error).empty());
    EXPECT_EQ(error, "incorrect id format for 1234.5");
    // No results: no papers, no error
    EXPECT_TRUE(cite::parseArxivFeed(test::ARXIV_EMPTY, &error).empty());
    EXPECT_EQ(error, "");
    // Not a feed (a proxy's page, a broken answer)
    EXPECT_TRUE(cite::parseArxivFeed("<html><body>Service unavailable</body></html>", &error).empty());
    EXPECT_FALSE(error.isEmpty());
    EXPECT_TRUE(cite::parseArxivFeed("", &error).empty());
    EXPECT_FALSE(error.isEmpty());
}

TEST(Citation, aDownloadIsNamedByItsTitleWithTheId) {
    EXPECT_EQ(cite::downloadName("Attention Is All You Need", {"1706.03762", "v7"}),
              "Attention Is All You Need (1706.03762).pdf") << "the ID without its version";
    EXPECT_EQ(cite::downloadName("Adam: A Method for Stochastic Optimization", {"1412.6980", ""}),
              "Adam - A Method for Stochastic Optimization (1412.6980).pdf");
    EXPECT_EQ(cite::downloadName("What/Why? A \"study\" <of> |pipes|*\\", {"2401.12345", ""}),
              "What Why A study of pipes (2401.12345).pdf") << "what file systems do not allow is left out";
    EXPECT_EQ(cite::downloadName("$O(n \\log n)$ sorting for {TeX}", {"2401.12345", ""}),
              "O(n log n) sorting for TeX (2401.12345).pdf") << "TeX marks left out";
    EXPECT_EQ(cite::downloadName("Line\nbreaks\tand   spaces...", {"2401.12345", ""}),
              "Line breaks and spaces (2401.12345).pdf") << "no dots at its end";
    EXPECT_EQ(cite::downloadName("An old one", {"hep-th/9901001", "v1"}), "An old one (hep-th_9901001).pdf");
    EXPECT_EQ(cite::downloadName("", {"1706.03762", ""}), "arXiv 1706.03762.pdf");
    EXPECT_EQ(cite::downloadName("...", {"1706.03762", ""}), "arXiv 1706.03762.pdf");
    const QString longName = cite::downloadName(QString("word ").repeated(60), {"1706.03762", ""});
    EXPECT_LE(longName.size(), 120 + QString(" (1706.03762).pdf").size());
    EXPECT_TRUE(longName.endsWith("word (1706.03762).pdf")) << "cut at a word: " << longName.toStdString();
}
