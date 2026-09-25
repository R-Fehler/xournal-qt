/*
 * xournal-qt: emoji while writing on the page (text boxes, Markdown): copy and paste, the cursor and deleting by
 * grapheme cluster (👩‍💻 is one character), and the shortcode completion (":smi" -> 😄).
 *
 * @license GNU GPLv2 or later
 */
#include <memory>
#include <string>

#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

#include "CanvasView.h"
#include "EmojiCompletion.h"
#include "MarkdownEditor.h"
#include "TextEditor.h"
#include "TextFlow.h"

using namespace xqt;

namespace {

const std::string SMILE = "\xf0\x9f\x98\x84";                                   // 😄
const std::string SMILEY = "\xf0\x9f\x98\x83";                                  // 😃
const std::string CODER = "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb";       // 👩‍💻 (ZWJ)
const std::string FLAG = "\xf0\x9f\x87\xa9\xf0\x9f\x87\xaa";                    // 🇩🇪
const std::string THUMB = "\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd";                   // 👍🏽 (skin tone)
const std::string PARTY = "\xf0\x9f\x8e\x89";                                   // 🎉

class EmojiEditingTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        view = std::make_unique<CanvasView>(*session);
        view->getViewController().setViewSize(QSizeF(900, 1400));
        processEvents();
    }
    void TearDown() override {
        view.reset();
        session.reset();
        app.reset();
    }
    void processEvents(int ms = 30) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }

    /// An ordinary text box (the text tool, not Markdown).
    TextEditor& startTextBox() {
        view->setMarkdownText(false, 10, false);
        view->startText(*view->getPage(0), 100, 100);
        EXPECT_NE(view->getTextEditor(), nullptr);
        return *view->getTextEditor();
    }
    /// The page's Markdown text, written on the page.
    MarkdownEditor& startMarkdown() {
        const auto m = TextFlow::styleFor(session->getDocument()->getPage(0), TextFlow::Style{});
        view->startMarkdown(0, true, m.leftMargin + 5, TextFlow::MARGIN + 5);
        EXPECT_NE(view->getMarkdownEditor(), nullptr);
        return *view->getMarkdownEditor();
    }

    /// A key as the canvas gives it (the suggestions first, then the editor).
    void key(Qt::Key k, const QString& text = {}, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QKeyEvent e(QEvent::KeyPress, k, mods, text);
        bool finish = false;
        view->textKeyPressed(&e, finish);
    }
    void type(const std::string& s) {
        for (const QChar c: QString::fromStdString(s)) {
            key(static_cast<Qt::Key>(c.toUpper().unicode()), QString(c));
        }
    }
    void paste(const std::string& s) {
        QGuiApplication::clipboard()->setText(QString::fromStdString(s));
        key(Qt::Key_V, {}, Qt::ControlModifier);
    }
    std::vector<std::string> suggested() const {
        std::vector<std::string> out;
        for (const auto& c: view->emojiCompletion().suggestions()) {
            out.emplace_back(c.name);
        }
        return out;
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
};

int cursorOf(const TextEditor& e) { return e.inputMethodQuery(Qt::ImCursorPosition).toInt(); }

}  // namespace

/// Pasted emoji sequences stay whole; the arrows go over each as one character, Backspace and Delete remove it
/// whole; copying gives them back as they were.
TEST_F(EmojiEditingTest, textBoxMovesAndDeletesByGraphemeCluster) {
    TextEditor& e = startTextBox();
    const std::string text = "a" + CODER + FLAG + THUMB + "b";
    paste(text);
    EXPECT_EQ(e.text().toStdString(), text);
    // (UTF-16: a 1, 👩‍💻 5, 🇩🇪 4, 👍🏽 4, b 1)
    EXPECT_EQ(cursorOf(e), 15);
    for (const int at: {14, 10, 6, 1, 0}) {
        key(Qt::Key_Left);
        EXPECT_EQ(cursorOf(e), at);
    }
    key(Qt::Key_Right);
    key(Qt::Key_Right);
    EXPECT_EQ(cursorOf(e), 6);
    key(Qt::Key_Backspace);
    EXPECT_EQ(e.text().toStdString(), "a" + FLAG + THUMB + "b");
    key(Qt::Key_Delete);
    EXPECT_EQ(e.text().toStdString(), "a" + THUMB + "b");

    key(Qt::Key_A, {}, Qt::ControlModifier);
    key(Qt::Key_C, {}, Qt::ControlModifier);
    EXPECT_EQ(QGuiApplication::clipboard()->text().toStdString(), "a" + THUMB + "b");
}

TEST_F(EmojiEditingTest, markdownMovesAndDeletesByGraphemeCluster) {
    MarkdownEditor& e = startMarkdown();
    const std::string text = "a" + CODER + FLAG + THUMB + "b";
    paste(text);
    EXPECT_EQ(e.text(), text);
    EXPECT_EQ(e.cursorPosition(), text.size());
    // (bytes: a 1, 👩‍💻 11, 🇩🇪 8, 👍🏽 8, b 1)
    for (const size_t at: {28u, 20u, 12u, 1u, 0u}) {
        key(Qt::Key_Left);
        EXPECT_EQ(e.cursorPosition(), at);
    }
    key(Qt::Key_Right);
    key(Qt::Key_Right);
    EXPECT_EQ(e.cursorPosition(), 12u);
    key(Qt::Key_Backspace);
    EXPECT_EQ(e.text(), "a" + FLAG + THUMB + "b");
    key(Qt::Key_Delete);
    EXPECT_EQ(e.text(), "a" + THUMB + "b");
    // (over a line break too)
    key(Qt::Key_End, {}, Qt::ControlModifier);
    key(Qt::Key_Return, {}, Qt::ShiftModifier);
    paste(FLAG);
    const size_t end = e.cursorPosition();
    key(Qt::Key_Left);
    EXPECT_EQ(e.cursorPosition(), end - FLAG.size());
    key(Qt::Key_Left);
    EXPECT_EQ(e.cursorPosition(), end - FLAG.size() - 1) << "before the line break";
    EXPECT_EQ(e.text()[e.cursorPosition()], '\n');

    key(Qt::Key_A, {}, Qt::ControlModifier);
    key(Qt::Key_C, {}, Qt::ControlModifier);
    EXPECT_EQ(QGuiApplication::clipboard()->text().toStdString(), e.text());
}

/// ":smi" in a text box: the suggestions; Down and Enter put the second in place of the shortcode.
TEST_F(EmojiEditingTest, textBoxCompletesShortcodes) {
    TextEditor& e = startTextBox();
    type("Hi :s");
    EXPECT_FALSE(view->emojiCompletion().active()) << "two letters first";
    type("mi");
    ASSERT_TRUE(view->emojiCompletion().active());
    const auto names = suggested();
    ASSERT_GE(names.size(), 3u);
    EXPECT_EQ(names[0], "smile");
    EXPECT_EQ(names[1], "smiley");
    EXPECT_LE(names.size(), EmojiCompletion::LIMIT);
    key(Qt::Key_Down);
    EXPECT_EQ(view->emojiCompletion().selected(), 1);
    key(Qt::Key_Return);
    EXPECT_EQ(e.text().toStdString(), "Hi " + SMILEY) << "no line break: Enter took the emoji";
    EXPECT_FALSE(view->emojiCompletion().active());

    // Escape closes the list for this shortcode, also while it is typed on; the next one opens it again
    type(" :hear");
    ASSERT_TRUE(view->emojiCompletion().active());
    key(Qt::Key_Escape);
    EXPECT_FALSE(view->emojiCompletion().active());
    EXPECT_NE(view->getTextEditor(), nullptr) << "Escape closed the list, not the text";
    type("t");
    EXPECT_FALSE(view->emojiCompletion().active());
    type(" :+1");
    EXPECT_TRUE(view->emojiCompletion().active());
    // No list in a time
    type(" 10:30");
    EXPECT_FALSE(view->emojiCompletion().active());
}

/// In Markdown, Enter or a tap puts the emoji itself into the source (typed ":smile:" stays text there).
TEST_F(EmojiEditingTest, markdownCompletesShortcodes) {
    MarkdownEditor& e = startMarkdown();
    type("Hi :smi");
    ASSERT_TRUE(view->emojiCompletion().active());
    key(Qt::Key_Return);
    EXPECT_EQ(e.text(), "Hi " + SMILE);
    type(" :smi");
    view->chooseEmojiCompletion(1);  // (a tap on the second)
    EXPECT_EQ(e.text(), "Hi " + SMILE + " " + SMILEY);
    EXPECT_FALSE(view->emojiCompletion().active());
    // A place elsewhere (a tap there) closes it
    type(" :smi");
    ASSERT_TRUE(view->emojiCompletion().active());
    e.setCursorPosition(0);
    view->refreshEmojiCompletion();
    EXPECT_FALSE(view->emojiCompletion().active());
    // Ending the writing closes it
    e.setCursorPosition(e.text().size());
    view->refreshEmojiCompletion();
    EXPECT_TRUE(view->emojiCompletion().active());
    view->endTextEditing();
    EXPECT_FALSE(view->emojiCompletion().active());
}

/// An on-screen keyboard sends the word being typed as its own text (preedit): the list follows it, and the emoji
/// takes its place.
TEST_F(EmojiEditingTest, completionOfTheWordAKeyboardIsTyping) {
    TextEditor& t = startTextBox();
    type("Hi ");
    QInputMethodEvent pre(QStringLiteral(":smi"), {});
    t.inputMethodEvent(&pre);
    view->refreshEmojiCompletion();
    ASSERT_TRUE(view->emojiCompletion().active());
    view->chooseEmojiCompletion(0);
    EXPECT_EQ(t.text().toStdString(), "Hi " + SMILE);
    view->endTextEditing();

    MarkdownEditor& m = startMarkdown();
    type("Yes ");
    m.inputMethodEvent(&pre);
    view->refreshEmojiCompletion();
    ASSERT_TRUE(view->emojiCompletion().active());
    view->chooseEmojiCompletion(0);
    EXPECT_EQ(m.text(), "Yes " + SMILE);
}

/// The picker: the emoji at the cursor, in place of the selection.
TEST_F(EmojiEditingTest, pickerInsertsAtTheCursor) {
    TextEditor& t = startTextBox();
    type("ab");
    key(Qt::Key_Left);
    EXPECT_TRUE(view->insertAtTextCursor(PARTY));
    EXPECT_EQ(t.text().toStdString(), "a" + PARTY + "b");
    view->endTextEditing();
    EXPECT_FALSE(view->insertAtTextCursor(PARTY)) << "nothing is being written";

    MarkdownEditor& m = startMarkdown();
    type("xyz");
    key(Qt::Key_Left, {}, Qt::ShiftModifier);
    EXPECT_TRUE(view->insertAtTextCursor(PARTY));
    EXPECT_EQ(m.text(), "xy" + PARTY);
}
