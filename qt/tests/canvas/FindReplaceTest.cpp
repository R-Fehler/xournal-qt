/*
 * xournal-qt: find and replace in a document (FindReplace.h, qt/docs/md-editor.md "Find and replace"): a .md and a .txt
 * edited, a PDF text document with Markdown text boxes and a sticky note's text; "Replace all" is one undo step (the
 * text being written: one of its own), "Replace" replaces the current hit of the search and goes to the next one,
 * and what cannot be written (PDF text, plain text elements, hidden layers, read-only files) is never changed.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <memory>
#include <shared_mutex>
#include <string>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/StickyNote.h"
#include "session/TextDocument.h"
#include "session/TextFile.h"
#include "undo/UndoRedoHandler.h"
#include "util/Matrix.h"

#include "support/SearchHits.h"
#include "CanvasTime.h"
#include "CanvasView.h"
#include "FindReplace.h"
#include "MarkdownEditor.h"
#include "MarkdownFile.h"
#include "MarkdownSession.h"
#include "MdBox.h"

using namespace xqt;
using xqt::test::waitForCounts;

namespace {

std::string flowOf(Document& doc) {
    std::shared_lock lock(doc);
    return TextDocument::flowText(doc);
}

/// A Markdown text of several pages with "cat" in many places
std::string catText() {
    std::string s = "# The cat\n\n";
    for (int i = 0; i < 40; ++i) {
        s += "Paragraph " + std::to_string(i) + " has a **cat** and a concatenation, long enough to wrap onto a "
             "second line of the page when it is laid out.\n\n";
    }
    return s + "Last cat.\n";
}

size_t occurrences(const std::string& s, const std::string& what) {
    size_t n = 0;
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + what.size())) {
        ++n;
    }
    return n;
}

class FindReplaceTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
    }
    void TearDown() override {
        view.reset();
        session.reset();
        app.reset();
    }
    void openText(const std::string& name, const std::string& bytes, TextFile::Kind kind) {
        const fs::path p = fs::path(tmp.path().toStdString()) / name;
        std::ofstream(p, std::ios::binary) << bytes;
        auto text = std::make_unique<TextFile>();
        std::string error;
        ASSERT_TRUE(text->load(p, kind, error)) << error;
        view.reset();
        session = std::make_unique<DocumentSession>(*app, MarkdownFile::textDocument(*text));
        session->setTextFile(std::move(text), false);
        view = std::make_unique<CanvasView>(*session);
        view->setClock(clock);
        view->getViewController().setViewSize(QSizeF(900, 1400));
        processEvents();
    }
    void show(std::unique_ptr<Document> doc) {
        view.reset();
        session = std::make_unique<DocumentSession>(*app, std::move(doc));
        view = std::make_unique<CanvasView>(*session);
        view->setClock(clock);
        view->getViewController().setViewSize(QSizeF(900, 1400));
        processEvents();
    }
    /// `ms` pass on the canvas's clock, the event loop and the renders run meanwhile (no real time passes)
    void processEvents(int ms = 20) { test::passTime(clock, *app, ms); }
    /// A Markdown text box at (x, y) of a page with `text` (as the text tool writes one: an undo step)
    const Text* addBox(size_t page, double x, double y, const std::string& text) {
        MarkdownSession md(*session);
        md.beginBox(page, md::Style{}, x, y);
        md.update(text);
        const auto parts = md.parts();
        const Text* box = parts.empty() ? nullptr : parts.front().box;
        md.finish();
        return box;
    }
    void search(const QString& text, replace::Options options = {}) {
        session->search().setQuery(text, true, false, options);
        ASSERT_TRUE(waitForCounts(session->search()));
    }
    int replaceAll(const QString& query, const QString& with, replace::Options options = {}) {
        return replace::replaceAll(*session, view.get(), query, with, options);
    }

    ManualClock clock;  ///< the canvas's time (CanvasTime.h): the tests move it on
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
};
}  // namespace

// A .md: every match over all its pages, as one undo step; undone, the text is the file's again
TEST_F(FindReplaceTest, replaceAllInAMarkdownFileIsOneUndoStep) {
    const std::string text = catText();
    openText("cats.md", text, TextFile::Kind::Markdown);
    ASSERT_GT(session->getDocument()->getPageCount(), 2u);
    EXPECT_TRUE(replace::canReplace(*session));
    ASSERT_EQ(replace::targets(*session).size(), 1u) << "one text over its pages";

    EXPECT_EQ(replaceAll("cat", "dog", {false, true, false}), 42) << "whole words: not \"concatenation\"";
    const std::string after = session->currentText();
    EXPECT_EQ(occurrences(after, "dog"), 42u);
    EXPECT_EQ(occurrences(after, "concatenation"), 40u);
    EXPECT_NE(after.find("**dog**"), std::string::npos) << "the marks stay";
    EXPECT_TRUE(session->isModified());

    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(session->currentText(), text) << "one step";
    EXPECT_FALSE(session->isModified());
    session->getUndoRedoHandler()->redo();
    EXPECT_EQ(session->currentText(), after);

    EXPECT_EQ(replaceAll("nothing like this", "x"), 0);
    EXPECT_EQ(session->currentText(), after);
}

// While the text is written on the page, "Replace all" is a step of that text (Ctrl+Z in it undoes it)
TEST_F(FindReplaceTest, replaceAllWhileWritingIsAStepOfTheText) {
    const std::string text = "# Notes\n\nA cat, a Cat and a CAT.\n";
    openText("write.md", text, TextFile::Kind::Markdown);
    ASSERT_TRUE(view->ensureTextEditor());
    MarkdownEditor* editor = view->getMarkdownEditor();
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(replaceAll("cat", "dog", {true, false, false}), 1) << "case-sensitive";
    ASSERT_EQ(view->getMarkdownEditor(), editor) << "still writing";
    EXPECT_EQ(editor->text(), "# Notes\n\nA dog, a Cat and a CAT.\n");
    EXPECT_EQ(replaceAll("cat", "dog"), 2);
    EXPECT_EQ(editor->text(), "# Notes\n\nA dog, a dog and a dog.\n");
    editor->undo();
    EXPECT_EQ(editor->text(), "# Notes\n\nA dog, a Cat and a CAT.\n");
    editor->undo();
    EXPECT_EQ(editor->text(), text);
}

// A .txt: plain text, a regular expression with groups
TEST_F(FindReplaceTest, aPlainTextFileAndARegularExpression) {
    openText("dates.txt", "due 2026-10-05\n\tand 2026-11-30\n", TextFile::Kind::Plain);
    EXPECT_TRUE(replace::canReplace(*session));
    EXPECT_EQ(replaceAll("(\\d+)-(\\d+)-(\\d+)", "$3.$2.$1", {false, false, true}), 2);
    EXPECT_EQ(session->currentText(), "due 05.10.2026\n\tand 30.11.2026\n");
}

// A PDF text document with a text box, a sticky note's text, a plain text element and a box on a hidden layer: the
// Markdown that is shown is replaced (one undo step for all), the rest stays
TEST_F(FindReplaceTest, theMarkdownOfNotesBoxesAndStickyNotes) {
    auto doc = MarkdownFile::notesDocument("# Cat facts\n\nThe cat sleeps.\n");
    auto second = std::make_shared<XojPage>(MarkdownFile::PAGE_WIDTH, MarkdownFile::PAGE_HEIGHT);
    auto* ink = new Layer();
    second->getLayers().push_back(ink);  // (the page owns it)
    doc->addPage(second);
    show(std::move(doc));
    const Text* box = addBox(1, 100, 100, "A *cat* in a box");
    ASSERT_NE(box, nullptr);
    // A sticky note with a Markdown text
    Layer* note = sticky::makeNote({{100, 400, 200, 150}, sticky::presetColors()[0], false});
    {
        std::unique_lock lock(*session->getDocument());
        second->getLayers().push_back(note);  // (the page owns it)
    }
    {
        MarkdownSession md(*session);
        md.beginBox(1, md::Style{}, 150, 450);
        md.update("cat note");
        md.finish();
    }
    ASSERT_NE(sticky::textOf(*note), nullptr);
    EXPECT_EQ(sticky::textOf(*note)->getText(), "cat note");
    // A plain text element (the text tool of Xournal++) and a box on a hidden layer
    auto plain = std::make_unique<Text>();
    plain->setText("plain cat");
    plain->setTransformation(xoj::util::Matrix::TRANSLATION(100, 700));
    const Text* plainRaw = plain.get();
    auto* hidden = new Layer();
    hidden->setName("Markdown");
    hidden->setVisible(false);
    auto hiddenBox = std::make_unique<Text>();
    hiddenBox->setText("hidden cat");
    hiddenBox->setTransformation(xoj::util::Matrix::TRANSLATION(300, 700));
    const Text* hiddenRaw = hiddenBox.get();
    {
        std::unique_lock lock(*session->getDocument());
        ink->addElement(std::move(plain));
        hidden->addElement(std::move(hiddenBox));
        second->getLayers().push_back(hidden);
    }

    EXPECT_TRUE(replace::canReplace(*session));
    EXPECT_EQ(replace::targets(*session).size(), 3u) << "the page's text, the box, the note's text";
    EXPECT_EQ(replaceAll("cat", "dog"), 4);
    EXPECT_EQ(flowOf(*session->getDocument()), "# dog facts\n\nThe dog sleeps.\n");
    EXPECT_EQ(box->getText(), "A *dog* in a box");
    EXPECT_EQ(sticky::textOf(*note)->getText(), "dog note");
    EXPECT_EQ(plainRaw->getText(), "plain cat") << "not a Markdown text";
    EXPECT_EQ(hiddenRaw->getText(), "hidden cat") << "not shown";

    session->getUndoRedoHandler()->undo();  // (one step for all)
    EXPECT_EQ(flowOf(*session->getDocument()), "# Cat facts\n\nThe cat sleeps.\n");
    EXPECT_EQ(md::boxOf(*md::markdownLayer(session->getDocument()->getPage(1)))->getText(), "A *cat* in a box");
    EXPECT_EQ(sticky::textOf(*note)->getText(), "cat note");
}

// A text that gets longer flows onto more pages; the texts after it are replaced too, and undo puts the pages back
TEST_F(FindReplaceTest, textsThatGrowMoveThePagesAfterThem) {
    show(MarkdownFile::notesDocument(catText()));
    const size_t pages = session->getDocument()->getPageCount();
    auto last = std::make_shared<XojPage>(MarkdownFile::PAGE_WIDTH, MarkdownFile::PAGE_HEIGHT);
    last->getLayers().push_back(new Layer());
    {
        std::unique_lock lock(*session->getDocument());
        session->getDocument()->addPage(last);
    }
    session->firePageInserted(pages);
    const Text* box = addBox(pages, 100, 100, "the last cat");
    ASSERT_NE(box, nullptr);
    const std::string longer(300, 'x');
    EXPECT_EQ(replaceAll("cat", QString::fromStdString(longer), {false, true, false}), 43);
    EXPECT_GT(session->getDocument()->getPageCount(), pages + 1) << "the text got longer";
    EXPECT_EQ(box->getText(), "the last " + longer);
    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(session->getDocument()->getPageCount(), pages + 1);
    EXPECT_EQ(flowOf(*session->getDocument()), catText());
    EXPECT_EQ(session->getDocument()->getPage(pages), last);
    // (undo puts the box from before back: another element)
    EXPECT_EQ(md::boxOf(*md::markdownLayer(last))->getText(), "the last cat");
}

// "Replace": the current hit, then the next; a hit that cannot be replaced is passed over
TEST_F(FindReplaceTest, replaceTheCurrentHitThenTheNext) {
    openText("three.md", "# Cats\n\nOne cat, **two cats** and a `cat`.\n", TextFile::Kind::Markdown);
    search("cat");
    ASSERT_EQ(session->search().hitCount(), 4);
    ASSERT_EQ(session->search().currentHit(), 0);
    EXPECT_EQ(replace::replaceCurrent(*session, view.get(), "cat", "dog", {}), replace::Step::Replaced);
    EXPECT_EQ(session->currentText(), "# dogs\n\nOne cat, **two cats** and a `cat`.\n");
    ASSERT_TRUE(waitForCounts(session->search()));
    EXPECT_EQ(session->search().hitCount(), 3);
    EXPECT_EQ(session->search().currentHit(), 0) << "the next one";
    EXPECT_EQ(replace::replaceCurrent(*session, view.get(), "cat", "dog", {}), replace::Step::Replaced);
    EXPECT_EQ(session->currentText(), "# dogs\n\nOne dog, **two cats** and a `cat`.\n");
    // The next is "cats" in bold: replaced inside its marks
    EXPECT_EQ(replace::replaceCurrent(*session, view.get(), "cat", "dog", {}), replace::Step::Replaced);
    EXPECT_EQ(session->currentText(), "# dogs\n\nOne dog, **two dogs** and a `cat`.\n");
    EXPECT_EQ(replace::replaceCurrent(*session, view.get(), "cat", "a cat", {}), replace::Step::Replaced);
    EXPECT_EQ(session->currentText(), "# dogs\n\nOne dog, **two dogs** and a `a cat`.\n");
    ASSERT_TRUE(waitForCounts(session->search()));
    EXPECT_EQ(session->search().hitCount(), 1) << "the replacement has a hit itself";
    // Each one undo step
    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(session->currentText(), "# dogs\n\nOne dog, **two dogs** and a `cat`.\n");

    // No current hit: the first is shown first
    session->search().setQuery("dogs", false);
    ASSERT_TRUE(waitForCounts(session->search()));
    ASSERT_EQ(session->search().currentHit(), -1);
    EXPECT_EQ(replace::replaceCurrent(*session, view.get(), "dogs", "cats", {}), replace::Step::Shown);
    EXPECT_EQ(session->search().currentHit(), 0);
    search("nothing");
    EXPECT_EQ(replace::replaceCurrent(*session, view.get(), "nothing", "x", {}), replace::Step::None);
}

// A hit in text that is not Markdown (a plain text element) is passed over, the next one is current
TEST_F(FindReplaceTest, hitsThatCannotBeReplacedArePassedOver) {
    show(MarkdownFile::notesDocument("The cat.\n"));
    auto plain = std::make_unique<Text>();
    plain->setText("plain cat");
    plain->setTransformation(xoj::util::Matrix::TRANSLATION(100, 30));  // (above the page's text: the first hit)
    const Text* plainRaw = plain.get();
    {
        std::unique_lock lock(*session->getDocument());
        auto* ink = new Layer();
        ink->addElement(std::move(plain));
        session->getDocument()->getPage(0)->getLayers().push_back(ink);  // (the page owns it)
    }
    search("cat");
    ASSERT_EQ(session->search().hitCount(), 2);
    ASSERT_EQ(session->search().currentHit(), 0);
    EXPECT_EQ(replace::replaceCurrent(*session, view.get(), "cat", "dog", {}), replace::Step::Skipped);
    EXPECT_EQ(plainRaw->getText(), "plain cat");
    EXPECT_EQ(session->search().currentHit(), 1);
    EXPECT_EQ(replace::replaceCurrent(*session, view.get(), "cat", "dog", {}), replace::Step::Replaced);
    EXPECT_EQ(flowOf(*session->getDocument()), "The dog.\n");
}

// Documents without Markdown text, and text files shown read-only: nothing to replace
TEST_F(FindReplaceTest, nothingToReplaceWhereNothingCanBeWritten) {
    show(MarkdownFile::document("", MarkdownFile::style()));
    EXPECT_FALSE(replace::canReplace(*session));
    EXPECT_EQ(replaceAll("x", "y"), 0);
}
