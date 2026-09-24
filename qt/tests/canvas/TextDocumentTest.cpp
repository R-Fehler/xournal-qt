/*
 * xournal-qt: a .md file edited as a document (DocumentSession::setTextFile, MarkdownFile, CanvasView's text mode):
 * the pages hold its text, saving writes the text back byte for byte where it did not change, the modified state
 * follows the text, and a change on disk can be taken over.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <memory>
#include <string>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/TextFile.h"
#include "undo/UndoRedoHandler.h"
#include "util/Util.h"

#include "CanvasView.h"
#include "MarkdownEditor.h"
#include "MarkdownFile.h"
#include "MdBox.h"
#include "MdDocument.h"
#include "TextFlow.h"
#include "model/Layer.h"
#include "model/Text.h"

using namespace xqt;

namespace {
std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void writeFile(const fs::path& p, const std::string& bytes) {
    std::ofstream out(p, std::ios::binary);
    out << bytes;
}

/// A Markdown text of several pages with all kinds of blocks, lines ending in `eol`.
std::string longMarkdown(const std::string& eol) {
    std::string s = "# A long file" + eol + eol;
    for (int i = 0; i < 40; ++i) {
        s += "## Section " + std::to_string(i) + eol + eol;
        s += "Paragraph " + std::to_string(i) + " with **bold**, *italic* and `code`, long enough to wrap onto a "
             "second line of the page, and a [link](https://example.org)." + eol + eol;
        if (i % 5 == 0) {
            s += "- item one" + eol + "- item two" + eol + "  - nested" + eol + eol;
            s += "```cpp" + eol + "int main() {" + eol + "    return 0;" + eol + "}" + eol + "```" + eol + eol;
            s += "| a | b |" + eol + "|---|---|" + eol + "| 1 | 2 |" + eol + "| 3 | 4 |" + eol + eol;
        }
    }
    return s + "The end." + eol;
}

class TextDocumentTest: public ::testing::Test {
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
    fs::path file(const std::string& name, const std::string& bytes) {
        fs::path p = fs::path(tmp.path().toStdString()) / name;
        writeFile(p, bytes);
        return p;
    }
    void open(const fs::path& p, TextFile::Kind kind = TextFile::Kind::Markdown) {
        auto text = std::make_unique<TextFile>();
        std::string error;
        ASSERT_TRUE(text->load(p, kind, error)) << error;
        session = std::make_unique<DocumentSession>(*app, MarkdownFile::textDocument(*text));
        session->setTextFile(std::move(text), false);
        view = std::make_unique<CanvasView>(*session);
        view->getViewController().setViewSize(QSizeF(900, 1400));
        processEvents();
    }
    void processEvents(int ms = 20) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }
    void key(Qt::Key k, const QString& text = {}, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        ASSERT_NE(view->getMarkdownEditor(), nullptr);
        QKeyEvent e(QEvent::KeyPress, k, mods, text);
        bool finish = false;
        view->getMarkdownEditor()->keyPressed(&e, finish);
    }
    void type(const std::string& s) {
        for (const char c: s) {
            key(Qt::Key_A, QString(QChar(c)));
        }
    }
    /// Put the cursor right before `what` (its first place in the text).
    void cursorBefore(const std::string& what) {
        ASSERT_TRUE(view->ensureTextEditor());
        const size_t at = session->currentText().find(what);
        ASSERT_NE(at, std::string::npos) << what;
        view->getMarkdownEditor()->setCursorPosition(at);
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
};
}  // namespace

TEST_F(TextDocumentTest, theFileFlowsOverPagesAndSavedUnchangedIsTheSameBytes) {
    const std::string bytes = longMarkdown("\r\n");
    const fs::path p = file("long.md", bytes);
    open(p);
    EXPECT_GT(session->getDocument()->getPageCount(), 3u);
    EXPECT_EQ(session->currentText(), TextFile::normalized(bytes)) << "the pages give the text again";
    EXPECT_FALSE(session->isModified());
    EXPECT_TRUE(session->isEditableText());
    EXPECT_FALSE(session->isReadOnly());
    EXPECT_EQ(session->getDisplayName(), "long.md");
    const auto r = session->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(readFile(p), bytes);
    // Never a .xopp
    EXPECT_FALSE(fs::exists(fs::path(p).replace_extension(".xopp")));
    EXPECT_FALSE(session->hasFilePath());
}

TEST_F(TextDocumentTest, anEditIsWrittenBackAndTheRestStaysByteForByte) {
    const std::string bytes = longMarkdown("\r\n");
    const fs::path p = file("edit.md", bytes);
    open(p);
    cursorBefore("Paragraph 25 ");
    type("NEW ");
    EXPECT_TRUE(session->isModified());
    ASSERT_TRUE(session->save().ok);
    std::string expected = bytes;
    expected.insert(expected.find("Paragraph 25 "), "NEW ");
    EXPECT_EQ(readFile(p), expected);
    EXPECT_FALSE(session->isModified());
    // A new line in a Windows file ends in "\r\n"
    cursorBefore("The end.");
    key(Qt::Key_Return, "\r", Qt::ShiftModifier);  // (a line of the same paragraph)
    ASSERT_TRUE(session->save().ok);
    expected.insert(expected.find("The end."), "\r\n");
    EXPECT_EQ(readFile(p), expected);
    // Undone in the editor (the line, the space, the word): modified again, and saved it is the file from before
    view->getMarkdownEditor()->undo();
    view->getMarkdownEditor()->undo();
    view->getMarkdownEditor()->undo();
    EXPECT_TRUE(session->isModified());
    EXPECT_EQ(session->currentText(), TextFile::normalized(bytes));
    ASSERT_TRUE(session->save().ok);
    EXPECT_EQ(readFile(p), bytes);
    EXPECT_FALSE(session->isModified()) << "the text is the file's again";
}

TEST_F(TextDocumentTest, aFileWithoutNewlineAtTheEndKeepsItsEnd) {
    const fs::path p = file("end.md", "# Title\n\nLast line");
    open(p);
    cursorBefore("Title");
    type("My ");
    ASSERT_TRUE(session->save().ok);
    EXPECT_EQ(readFile(p), "# My Title\n\nLast line");
}

TEST_F(TextDocumentTest, typingWithoutACursorStartsAtThePageAndEscapeKeepsTheText) {
    const fs::path p = file("empty.md", "");
    open(p);
    EXPECT_TRUE(view->textMode());
    EXPECT_EQ(view->getMarkdownEditor(), nullptr);
    ASSERT_TRUE(view->ensureTextEditor());
    type("Hello");
    EXPECT_EQ(session->currentText(), "Hello");
    view->endTextEditing();
    EXPECT_EQ(session->currentText(), "Hello");
    EXPECT_TRUE(session->isModified());
    ASSERT_TRUE(session->save().ok);
    EXPECT_EQ(readFile(p), "Hello");
}

TEST_F(TextDocumentTest, aChangeOnDiskIsSeenAndReadAgain) {
    const fs::path p = file("disk.md", "# One\n\nText.\n");
    open(p);
    std::string bytes;
    EXPECT_FALSE(session->textChangedOnDisk(bytes));
    ASSERT_TRUE(session->save().ok);
    EXPECT_FALSE(session->textChangedOnDisk(bytes)) << "our own save";
    writeFile(p, "# One\n\nText changed elsewhere.\n");
    ASSERT_TRUE(session->textChangedOnDisk(bytes));
    // Not modified here: read again (as the window does, AppController::reloadText)
    ASSERT_FALSE(session->isModified());
    MarkdownFile::setText(*session, TextFile::normalized(bytes));
    session->textReloaded(bytes);
    EXPECT_EQ(session->currentText(), "# One\n\nText changed elsewhere.\n");
    EXPECT_FALSE(session->isModified());
    EXPECT_FALSE(session->textChangedOnDisk(bytes));
    // Undo brings the text before back (modified: the file has the other one)
    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(session->currentText(), "# One\n\nText.\n");
    EXPECT_TRUE(session->isModified());
    // Kept over the file's version: not asked about it again, and saving writes over it
    writeFile(p, "# One\n\nThird version.\n");
    ASSERT_TRUE(session->textChangedOnDisk(bytes));
    session->keepTextOverDisk();
    EXPECT_FALSE(session->textChangedOnDisk(bytes));
    ASSERT_TRUE(session->save().ok);
    EXPECT_EQ(readFile(p), "# One\n\nText.\n");
}

TEST_F(TextDocumentTest, theAutosaveHoldsTheText) {
    const fs::path p = file("auto.md", "Start.\n");
    open(p);
    ASSERT_TRUE(session->autosaveText().ok);
    const fs::path autosave = DocumentSession::textAutosavePath(Util::getPid(), session->serial());
    EXPECT_FALSE(fs::exists(autosave)) << "nothing to keep";
    cursorBefore("Start.");
    type("A ");
    ASSERT_TRUE(session->autosaveText().ok);
    EXPECT_EQ(readFile(autosave), "A Start.\n");
    EXPECT_EQ(readFile(p), "Start.\n") << "the file is written only by saving";
    ASSERT_TRUE(session->save().ok);
    EXPECT_FALSE(fs::exists(autosave)) << "saved: the autosave goes";
}

TEST_F(TextDocumentTest, aTapAnywhereOnAPagePutsTheCursorIntoTheText) {
    const fs::path p = file("tap.md", longMarkdown("\n"));
    open(p);
    ASSERT_GT(session->getDocument()->getPageCount(), 2u);
    // Far below the text of page 2: the cursor goes to the end of that page's part
    CanvasPage* page = view->canvasPageOf(session->getDocument()->getPage(1).get());
    ASSERT_NE(page, nullptr);
    view->textPress(*page, 300, 835);
    ASSERT_NE(view->getMarkdownEditor(), nullptr);
    const auto starts = MarkdownFile::pageStarts(*session->getDocument());
    const size_t cursor = view->getMarkdownEditor()->cursorPosition();
    EXPECT_GE(cursor, starts[1]);
    EXPECT_LE(cursor, starts[2]);
    // Another tap on the first page moves it there
    view->textPress(*view->canvasPageOf(session->getDocument()->getPage(0).get()), TextFlow::MARGIN + 1,
                    TextFlow::MARGIN + 1);
    EXPECT_LT(view->getMarkdownEditor()->cursorPosition(), starts[1]);
}

TEST_F(TextDocumentTest, aPlainTextFileIsWrittenAsItIsWithoutMarkdown) {
    std::string bytes;
    for (int i = 0; i < 120; ++i) {
        bytes += "    line " + std::to_string(i) + ": **not bold**, # not a heading, - not a list\r\n";
    }
    const fs::path p = file("notes.txt", bytes);
    open(p, TextFile::Kind::Plain);
    EXPECT_GT(session->getDocument()->getPageCount(), 1u);
    EXPECT_EQ(session->currentText(), TextFile::normalized(bytes));
    // Nothing is formatted: every page's box is plain text, drawn as it is
    const Text* box = nullptr;
    {
        const Layer* layer = md::markdownLayer(session->getDocument()->getPage(0));
        ASSERT_NE(layer, nullptr);
        box = md::pageBoxOf(*layer, TextFlow::MARGIN, TextFlow::MARGIN);
    }
    ASSERT_NE(box, nullptr);
    EXPECT_TRUE(md::isPlain(box->getText()));
    EXPECT_EQ(md::shownTexts(*box).at(0), "    line 0: **not bold**, # not a heading, - not a list");
    // Enter keeps the indentation (no list, no new paragraph), Ctrl+B adds no marks, Tab is a tab
    cursorBefore("line 5:");
    key(Qt::Key_End);
    key(Qt::Key_Return, "\r");
    type("next");
    key(Qt::Key_B, "", Qt::ControlModifier);
    key(Qt::Key_Tab, "\t");
    type("x");
    ASSERT_TRUE(session->save().ok);
    std::string expected = bytes;
    const std::string line5 = "    line 5: **not bold**, # not a heading, - not a list";
    expected.insert(expected.find(line5) + line5.size(), "\r\n    next\tx");
    EXPECT_EQ(readFile(p), expected);
}
