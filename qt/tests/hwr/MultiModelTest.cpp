/*
 * xournal-qt: two models read the same handwriting (MultiRecognizer): their readings put together per ink word, with
 * scripted English and German models.
 *
 * @license GNU GPLv2 or later
 */
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "hwr/FakeRecognizer.h"
#include "hwr/InkRecognitionService.h"
#include "hwr/InkTextIndexer.h"
#include "hwr/LanguagePlan.h"
#include "hwr/MultiRecognizer.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/FuzzyQuery.h"
#include "shell/InkTextStore.h"

#include "../SearchHits.h"

using namespace xqt;
using namespace xqt::hwr;

namespace {
std::unique_ptr<Stroke> written(double x, double y, int shape) {
    auto s = std::make_unique<Stroke>();
    s->setWidth(1.0);
    for (int i = 0; i <= 8; ++i) {
        s->addPoint(Point(x + i * 3.0, y + (i % 2 ? 10 : 0) + (i == 1 ? 0.3 * shape : 0)));
    }
    s->getBoundingBox();
    return s;
}

/// One page with `lines` lines of three words.
std::unique_ptr<Document> notes(int lines) {
    auto doc = std::make_unique<Document>(nullptr);
    auto page = std::make_shared<XojPage>(595, 842);
    page->setBackgroundType(PageType(PageTypeFormat::Plain));
    for (int l = 0; l < lines; ++l) {
        for (int w = 0; w < 3; ++w) {
            page->getSelectedLayer()->addElement(written(50 + 60 * w, 100 + 40 * l, l + 1));
        }
    }
    doc->addPage(std::move(page));
    return doc;
}

bool waitFor(const std::function<bool()>& done, int ms = 10000) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return done();
}

ink::Word wordOf(const QRectF& box, std::vector<std::pair<QString, float>> readings, float conf = 0.9f) {
    ink::Word w;
    w.box = box;
    w.conf = conf;
    w.text = readings.front().first;
    for (const auto& [text, p]: readings) {
        w.candidates.push_back(ink::candidate(text, p));
    }
    return w;
}

class MultiModelTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 1);
        InkTextIndexer::setDelay(0);
        english = std::make_shared<FakeRecognizer>(QStringLiteral("fake-en/1"), QStringList{QStringLiteral("en")});
        german = std::make_shared<FakeRecognizer>(QStringLiteral("fake-de/1"), QStringList{QStringLiteral("de")});
        english->setScript([](const LineInput&, size_t word) -> FakeRecognizer::Readings {
            switch (word) {
            case 0: return {{QStringLiteral("Kalman"), 0.6f}, {QStringLiteral("Kalmar"), 0.4f}};
            case 1: return {{QStringLiteral("dumb"), 0.9f}, {QStringLiteral("dump"), 0.1f}};
            default: return {{QStringLiteral("Strabe"), 0.5f}, {QStringLiteral("Stroke"), 0.5f}};
            }
        });
        german->setScript([](const LineInput&, size_t word) -> FakeRecognizer::Readings {
            switch (word) {
            case 0: return {{QStringLiteral("Kalman"), 0.5f}, {QStringLiteral("Kalmen"), 0.5f}};
            case 1: return {{QStringLiteral("dumm"), 0.8f}, {QStringLiteral("dumb"), 0.2f}};
            default: return {{QStringLiteral("Straße"), 0.9f}, {QStringLiteral("Strafe"), 0.1f}};
            }
        });
        both = std::make_shared<MultiRecognizer>(std::vector<std::shared_ptr<Recognizer>>{english, german});
        service = std::make_unique<InkRecognitionService>();
        service->setRecognizer(both);
        service->setActivityPause(50);
    }
    void TearDown() override { InkTextIndexer::setDelay(InkTextIndexer::DELAY_MS); }
    int hits(DocumentSession& s, const QString& query) {
        s.search().setQuery(query, false);
        test::waitForCounts(s.search());
        return s.search().hitCount();
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::shared_ptr<FakeRecognizer> english, german;
    std::shared_ptr<MultiRecognizer> both;
    std::unique_ptr<InkRecognitionService> service;
};
}  // namespace

// Per ink word one list of readings from both models; a reading both gave is there once, with the probability that one
// of them is right (noisy-OR); each keeps the models that read it
TEST(MultiRecognizerTest, readingsOfBothModelsArePutTogetherPerWord) {
    const QRectF a(0, 0, 20, 10), b(30, 0, 20, 10), c(60, 0, 20, 10);
    ink::LineResult en, de;
    en.words = {wordOf(a, {{"house", 0.7f}, {"horse", 0.3f}}, 0.8f), wordOf(b, {{"dumb", 1.0f}}, 0.9f)};
    de.words = {wordOf(a, {{"Haus", 0.6f}, {"house", 0.2f}}, 0.6f), wordOf(c, {{"Straße", 0.9f}}, 0.7f)};
    const ink::LineResult en0 = MultiRecognizer::tagged(en, 0), de1 = MultiRecognizer::tagged(de, 1);
    const ink::LineResult m = MultiRecognizer::merge({&en0, &de1}, 5);
    EXPECT_EQ(m.models, 3u);
    ASSERT_EQ(m.words.size(), 3u);  // (a word only one model read is there too)
    const ink::Word& w = m.words[0];
    EXPECT_EQ(w.box, a);
    EXPECT_FLOAT_EQ(w.conf, 0.8f);
    EXPECT_EQ(w.text, QStringLiteral("house"));
    ASSERT_EQ(w.candidates.size(), 3u);
    EXPECT_EQ(words::textOf(w.candidates[0].word), QStringLiteral("house"));
    EXPECT_NEAR(w.candidates[0].p, 1 - 0.3 * 0.8, 1e-6);
    EXPECT_EQ(w.candidates[0].models, 3);
    EXPECT_EQ(words::textOf(w.candidates[1].word), QStringLiteral("haus"));
    EXPECT_NEAR(w.candidates[1].p, 0.6, 1e-6);
    EXPECT_EQ(w.candidates[1].models, 2);
    EXPECT_EQ(w.candidates[2].models, 1);
    EXPECT_EQ(m.words[1].box, b);
    EXPECT_EQ(m.words[2].text, QStringLiteral("Straße"));
    EXPECT_EQ(m.words[2].candidates[0].models, 2);
    // Associative: German read later, onto what English read
    const ink::LineResult first = MultiRecognizer::merge({&en0}, 5);
    const ink::LineResult later = MultiRecognizer::merge({&first, &de1}, 5);
    ASSERT_EQ(later.words.size(), 3u);
    EXPECT_EQ(later.models, 3u);
    EXPECT_NEAR(later.words[0].candidates[0].p, w.candidates[0].p, 1e-6);
}

TEST_F(MultiModelTest, theSetHasTheIdsAndLanguagesOfItsModels) {
    const Capabilities c = both->capabilities();
    EXPECT_EQ(c.id, QStringLiteral("fake-en/1+fake-de/1"));
    EXPECT_EQ(c.languages, (QStringList{QStringLiteral("en"), QStringLiteral("de")}));
    EXPECT_EQ(c.languagesOf(1), QStringList{QStringLiteral("en")});
    EXPECT_EQ(c.languagesOf(2), QStringList{QStringLiteral("de")});
    EXPECT_EQ(c.languagesOf(3), (QStringList{QStringLiteral("en"), QStringLiteral("de")}));
    // Ready while one of them is
    german->setReady(false, QStringLiteral("no German model"));
    EXPECT_TRUE(both->ready());
    english->setReady(false, QStringLiteral("no English model"));
    QString why;
    EXPECT_FALSE(both->ready(&why));
    EXPECT_EQ(why, QStringLiteral("no English model"));
}

// Through the worker and the search of an open document: a German word is found through the German model, an English
// one through the English one, a word both read is one hit, ranked by the combined confidence
TEST_F(MultiModelTest, theSearchFindsWordsOfEitherModel) {
    auto s = std::make_unique<DocumentSession>(*app, notes(2));
    InkTextIndexer indexer(*s, *service);
    ASSERT_TRUE(waitFor([&] { return indexer.done() && indexer.pagesRead() == 1; }));
    EXPECT_EQ(english->calls(), 2);
    EXPECT_EQ(german->calls(), 2);
    EXPECT_EQ(hits(*s, QStringLiteral("Straße")), 2);  // German only
    EXPECT_EQ(hits(*s, QStringLiteral("dumm")), 2);    // German only
    EXPECT_EQ(hits(*s, QStringLiteral("stroke")), 2);  // English only
    EXPECT_EQ(hits(*s, QStringLiteral("kalman")), 2);  // both: one hit per ink word
    EXPECT_EQ(hits(*s, QStringLiteral("kalmen")), 2);
    const ink::PageText* ink = s->search().textIndex().inkOf(0);
    ASSERT_NE(ink, nullptr);
    ASSERT_EQ(ink->words.size(), 6u);
    // Ranking: "dumb" (English 0.9, German 0.2) as sure as both make it, above a word one model read with 0.5
    const auto dumb = ink::find(*ink, FuzzyQuery::textTerms(QStringLiteral("dumb"), false), 1);
    ASSERT_EQ(dumb.size(), 2u);
    EXPECT_NEAR(dumb[0].p, 1 - 0.1 * 0.8, 1e-6);
    EXPECT_TRUE(dumb[0].exact);
    const auto stroke = ink::find(*ink, FuzzyQuery::textTerms(QStringLiteral("stroke"), false), 1);
    ASSERT_EQ(stroke.size(), 2u);
    EXPECT_NEAR(stroke[0].p, 0.5, 1e-6);
    EXPECT_FALSE(stroke[0].exact);
    EXPECT_GT(dumb[0].p, stroke[0].p);
    // The readings keep their models; the line knows both read it
    const auto pages = indexer.pages();
    ASSERT_EQ(pages.size(), 1u);
    ASSERT_EQ(pages[0].lines.size(), 2u);
    ASSERT_TRUE(pages[0].lines[0].result);
    EXPECT_EQ(pages[0].lines[0].result->models, 3u);
    const ink::Word& w = ink->words[2];
    ASSERT_FALSE(w.candidates.empty());
    EXPECT_EQ(words::textOf(w.candidates[0].word), ink::folded(QStringLiteral("Straße")));
    EXPECT_EQ(both->capabilities().languagesOf(w.candidates[0].models), QStringList{QStringLiteral("de")});
    EXPECT_EQ(w.text, QStringLiteral("Straße"));
}

// The library's pack keeps which models read a line and each reading
TEST(MultiRecognizerTest, theLibrarysPackKeepsTheModels) {
    ink::LineResult en, de;
    en.words = {wordOf(QRectF(0, 0, 20, 10), {{"house", 0.7f}, {"horse", 0.3f}})};
    de.words = {wordOf(QRectF(0, 0, 20, 10), {{"Haus", 0.6f}, {"house", 0.2f}})};
    const ink::LineResult en0 = MultiRecognizer::tagged(en, 0), de1 = MultiRecognizer::tagged(de, 1);
    auto merged = std::make_shared<const ink::LineResult>(MultiRecognizer::merge({&en0, &de1}, 5));
    InkDoc doc;
    doc.stamp = QStringLiteral("1");
    doc.recognizer = QStringLiteral("a+b");
    doc.pages = {{hwr::LineRef{42, QPointF(10, 20), merged}}};
    const auto back = InkTextStore::decode(InkTextStore::encode(doc));
    ASSERT_TRUE(back);
    ASSERT_EQ(back->pages.size(), 1u);
    ASSERT_EQ(back->pages[0].size(), 1u);
    const auto& line = back->pages[0][0].result;
    ASSERT_TRUE(line);
    EXPECT_EQ(line->models, 3u);
    ASSERT_EQ(line->words.size(), 1u);
    ASSERT_EQ(line->words[0].candidates.size(), 3u);
    for (size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(line->words[0].candidates[i].models, merged->words[0].candidates[i].models);
        EXPECT_NEAR(line->words[0].candidates[i].p, merged->words[0].candidates[i].p, 1.0 / 255);
    }
    // A line of one model: as before (no models)
    InkDoc one;
    one.stamp = QStringLiteral("1");
    one.pages = {{hwr::LineRef{7, QPointF(), std::make_shared<const ink::LineResult>(en)}}};
    const auto plain = InkTextStore::decode(InkTextStore::encode(one));
    ASSERT_TRUE(plain && plain->pages[0][0].result);
    EXPECT_EQ(plain->pages[0][0].result->models, 0u);
    EXPECT_EQ(plain->pages[0][0].result->words[0].candidates[0].models, 0);
}

// --- which models read a document's lines (LanguagePlan) ---------------------------------------------------------

namespace {
/// One page of `lines` lines; German lines (`german(l)`) have four words, English ones three.
std::unique_ptr<Document> mixed(int lines, const std::function<bool(int)>& german) {
    auto doc = std::make_unique<Document>(nullptr);
    auto page = std::make_shared<XojPage>(595, 1100);
    page->setBackgroundType(PageType(PageTypeFormat::Plain));
    for (int l = 0; l < lines; ++l) {
        for (int w = 0; w < (german(l) ? 4 : 3); ++w) {
            page->getSelectedLayer()->addElement(written(50 + 60 * w, 60 + 40 * l, l + 1));
        }
    }
    doc->addPage(std::move(page));
    return doc;
}

bool germanLine(const LineInput& line) { return line.words.size() == 4; }

class LanguageDetectionTest: public MultiModelTest {
protected:
    void SetUp() override {
        MultiModelTest::SetUp();
        // Each model is sure of its language's lines and unsure of the other's
        english->setConfidence([](const LineInput& line, size_t) { return germanLine(line) ? 0.3f : 0.9f; });
        german->setConfidence([](const LineInput& line, size_t) { return germanLine(line) ? 0.9f : 0.3f; });
        german->setScript([](const LineInput&, size_t word) -> FakeRecognizer::Readings {
            switch (word) {
            case 0: return {{QStringLiteral("Kalman"), 0.5f}};
            case 1: return {{QStringLiteral("dumm"), 0.8f}};
            case 2: return {{QStringLiteral("Straße"), 0.9f}};
            default: return {{QStringLiteral("Verstärkung"), 0.9f}};
            }
        });
    }
    /// Reads the document; the number of lines it has
    std::unique_ptr<DocumentSession> read(int lines, const std::function<bool(int)>& german,
                                          std::shared_ptr<LanguagePlan> plan = nullptr) {
        auto s = std::make_unique<DocumentSession>(*app, mixed(lines, german));
        indexer = std::make_unique<InkTextIndexer>(*s, *service, nullptr, std::move(plan));
        EXPECT_TRUE(waitFor([&] { return indexer->done() && indexer->pagesRead() == 1; }));
        return s;
    }
    std::unique_ptr<InkTextIndexer> indexer;
};
}  // namespace

TEST(LanguagePlanTest, aClearlyBetterModelDecidesTheLanguage) {
    const std::vector<QStringList> models{{QStringLiteral("en")}, {QStringLiteral("de")}};
    LanguagePlan plan;
    EXPECT_EQ(plan.first(models), 3u);  // (both, until decided)
    for (int i = 0; i < LanguagePlan::PROBE_LINES - 1; ++i) {
        plan.observe({0.9f, 0.4f}, models);
    }
    EXPECT_EQ(plan.decided(), QString());
    plan.observe({0.9f, 0.4f}, models);
    EXPECT_EQ(plan.decided(), QStringLiteral("en"));
    EXPECT_EQ(plan.first(models), 1u);
    EXPECT_FALSE(plan.othersToo(0.8f));
    EXPECT_TRUE(plan.othersToo(0.3f));
    // Alike: no decision, both go on
    LanguagePlan alike;
    for (int i = 0; i < 2 * LanguagePlan::PROBE_LINES; ++i) {
        alike.observe({0.8f, 0.75f}, models);
    }
    EXPECT_EQ(alike.decided(), QString());
    EXPECT_EQ(alike.first(models), 3u);
    // The user's choice wins, and nothing is decided meanwhile
    LanguagePlan chosen(LanguagePlan::Choice::German, QStringLiteral("en"));
    EXPECT_EQ(chosen.first(models), 2u);
    EXPECT_FALSE(chosen.othersToo(0.0f));
    chosen.setChoice(LanguagePlan::Choice::Both);
    EXPECT_EQ(chosen.first(models), 3u);
    // A language no model reads: what there is
    LanguagePlan none(LanguagePlan::Choice::German);
    EXPECT_EQ(none.first({{QStringLiteral("en")}}), 1u);
    for (const auto c: {LanguagePlan::Choice::Automatic, LanguagePlan::Choice::English, LanguagePlan::Choice::German,
                        LanguagePlan::Choice::Both}) {
        EXPECT_EQ(LanguagePlan::choiceNamed(LanguagePlan::nameOf(c)), c);
    }
    EXPECT_EQ(LanguagePlan::choiceNamed(QString()), LanguagePlan::Choice::Automatic);
}

// An English document: both models read its first lines, then only the English one
TEST_F(LanguageDetectionTest, anEnglishDocumentStopsRunningTheGermanModel) {
    auto s = read(16, [](int) { return false; });
    EXPECT_EQ(english->calls(), 16);
    EXPECT_EQ(german->calls(), LanguagePlan::PROBE_LINES);
    EXPECT_EQ(indexer->plan()->decided(), QStringLiteral("en"));
    EXPECT_EQ(hits(*s, QStringLiteral("dumb")), 16);
    EXPECT_EQ(hits(*s, QStringLiteral("dumm")), LanguagePlan::PROBE_LINES);  // (German read only the first lines)
}

// A mixed document: decided for English by its first lines, the German model still reads the lines the English one is
// unsure of, so every German word is found
TEST_F(LanguageDetectionTest, aMixedDocumentKeepsBothOnUnsureLines) {
    auto german = [](int l) { return l >= LanguagePlan::PROBE_LINES && l % 2 == 1; };
    auto s = read(16, german);
    EXPECT_EQ(english->calls(), 16);
    EXPECT_EQ(this->german->calls(), LanguagePlan::PROBE_LINES + 5);
    EXPECT_EQ(indexer->plan()->decided(), QStringLiteral("en"));
    EXPECT_EQ(hits(*s, QStringLiteral("Verstärkung")), 5);  // every German line
}

// English first, then German: the English model's confidence drops, both read again, and German is decided
TEST_F(LanguageDetectionTest, theDecisionIsRevisitedWhenConfidenceDrops) {
    auto s = read(24, [](int l) { return l >= LanguagePlan::PROBE_LINES; });
    EXPECT_EQ(indexer->plan()->decided(), QStringLiteral("de"));
    EXPECT_EQ(german->calls(), 24);
    // 6 probing, 6 unsure ones (then both again), 6 probing again; none after German was decided
    EXPECT_EQ(english->calls(), 3 * LanguagePlan::PROBE_LINES);
    EXPECT_EQ(hits(*s, QStringLiteral("Verstärkung")), 18);
}

// The document's choice wins over the detection; a new choice reads only what it is missing
TEST_F(LanguageDetectionTest, theUsersChoiceWins) {
    auto s = read(8, [](int) { return false; }, std::make_shared<LanguagePlan>(LanguagePlan::Choice::German));
    EXPECT_EQ(english->calls(), 0);
    EXPECT_EQ(german->calls(), 8);
    EXPECT_EQ(hits(*s, QStringLiteral("dumb")), 0);
    indexer->setLanguageChoice(LanguagePlan::Choice::Both);
    ASSERT_TRUE(waitFor([&] { return english->calls() == 8 && indexer->done(); }));
    EXPECT_EQ(german->calls(), 8);  // (not again)
    EXPECT_EQ(hits(*s, QStringLiteral("dumb")), 8);
    EXPECT_EQ(hits(*s, QStringLiteral("dumm")), 8);
}

// Kept in the library's cache: the decision with the results, the user's choice whatever is put later
TEST(LanguagePlanTest, theLibrarysCacheKeepsTheLanguage) {
    QTemporaryDir dir;
    InkTextStore store(CacheLocation(fs::path(dir.path().toStdString())));
    const fs::path file = fs::path(dir.filePath(QStringLiteral("notes.xopp")).toStdString());
    store.setLanguageChoice(file, QStringLiteral("de"));
    auto entry = store.find(file);
    ASSERT_TRUE(entry);
    EXPECT_EQ(entry->languageChoice, QStringLiteral("de"));
    EXPECT_TRUE(entry->pages.empty());
    InkDoc doc;
    doc.stamp = QStringLiteral("7");
    doc.recognizer = QStringLiteral("a+b");
    doc.language = QStringLiteral("en");
    store.put(file, doc);
    entry = store.find(file);
    EXPECT_EQ(entry->languageChoice, QStringLiteral("de"));
    EXPECT_EQ(entry->language, QStringLiteral("en"));
    const auto back = InkTextStore::decode(InkTextStore::encode(*entry));
    ASSERT_TRUE(back);
    EXPECT_EQ(back->language, QStringLiteral("en"));
    EXPECT_EQ(back->languageChoice, QStringLiteral("de"));
    store.setLanguageChoice(file, QStringLiteral("auto"));
    EXPECT_EQ(store.find(file)->languageChoice, QString());
    EXPECT_EQ(store.find(file)->stamp, QStringLiteral("7"));
    store.discard();
}
