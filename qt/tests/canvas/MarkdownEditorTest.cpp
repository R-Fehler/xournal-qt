/*
 * xournal-qt: Markdown written on the page (MarkdownEditor): what is shown while typing.
 *
 * The canvas shows a page as tiles that are composed again only where the page says it changed (dirty regions).
 * These tests keep such a picture of the page, update it the same way, and compare it with the page composed
 * anew: what was typed must be on the screen at once, not after the next key.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>
#include <string>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QPainter>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

#include "CanvasPage.h"
#include "CanvasView.h"
#include "MarkdownEditor.h"
#include "TextFlow.h"

using namespace xqt;

namespace {
class MarkdownEditorTest: public ::testing::Test {
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

    /// Start writing the page's text on page 0.
    MarkdownEditor& start() {
        const auto m = TextFlow::styleFor(session->getDocument()->getPage(0), TextFlow::Style{});
        view->startMarkdown(0, true, m.leftMargin + 5, TextFlow::MARGIN + 5);
        EXPECT_NE(view->getMarkdownEditor(), nullptr);
        processEvents();
        return *view->getMarkdownEditor();
    }

    void key(Qt::Key k, const QString& text = {}, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QKeyEvent e(QEvent::KeyPress, k, mods, text);
        bool finish = false;
        view->getMarkdownEditor()->keyPressed(&e, finish);
    }
    void type(const std::string& s) {
        for (const char c: s) {
            if (c == '\n') {
                key(Qt::Key_Return);
            } else {
                key(static_cast<Qt::Key>(QChar(c).toUpper().unicode()), QString(QChar(c)));
            }
        }
    }

    // --- the page as the canvas shows it ---------------------------------------------------------------------------
    /// Compose the whole page anew (buffer and overlays).
    QImage composed() {
        CanvasPage* page = view->getPage(0);
        const auto info = page->bufferInfo();
        return page->composeTile(QRect(QPoint(0, 0), info.pixelSize));
    }
    /// The picture the canvas shows: composed where the page said it changed (as DocumentCanvasItem does).
    void updateShown() {
        processEvents();
        CanvasPage* page = view->getPage(0);
        const auto info = page->bufferInfo();
        ASSERT_TRUE(info.valid);
        bool all = false;
        const auto dirty = page->takeDirty(info, all);
        if (all || shown.size() != info.pixelSize) {
            shown = composed();
            return;
        }
        QPainter p(&shown);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        for (const QRect& r: dirty) {
            p.drawImage(r.topLeft(), page->composeTile(r));
        }
    }
    /// Pixels that differ between what is shown and the page as it is.
    int stalePixels() {
        const QImage now = composed();
        if (now.size() != shown.size()) {
            return now.width() * now.height();
        }
        int n = 0;
        for (int y = 0; y < now.height(); ++y) {
            const auto* a = reinterpret_cast<const QRgb*>(now.constScanLine(y));
            const auto* b = reinterpret_cast<const QRgb*>(shown.constScanLine(y));
            for (int x = 0; x < now.width(); ++x) {
                n += a[x] != b[x];
            }
        }
        return n;
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    QImage shown;
};
}  // namespace

// Enter in a code block that ends the text is a new line of the code, as anywhere else in code (not a blank line
// and a new paragraph); after its closing fence it starts a new paragraph.
TEST_F(MarkdownEditorTest, enterInACodeBlockAtTheEndIsALineOfCode) {
    MarkdownEditor& editor = start();
    type("```py\nsome code\n    #stuff\n");
    EXPECT_EQ(editor.text(), "```py\nsome code\n    #stuff\n    ") << "one line each, indented as the line before";
    key(Qt::Key_Backspace);
    key(Qt::Key_Backspace);
    key(Qt::Key_Backspace);
    key(Qt::Key_Backspace);
    type("```\nafter");
    EXPECT_EQ(editor.text(), "```py\nsome code\n    #stuff\n```\n\nafter");
}

// A code block at the end of the text, closed, then Enter and more text: what is typed is shown at once, where it is
// typed (the cursor goes along), below the code block.
TEST_F(MarkdownEditorTest, writingAfterACodeBlockAtTheEndIsShownAtOnce) {
    MarkdownEditor& editor = start();
    updateShown();
    type("Intro\n```py\nsome code\n#stuff\n```\n");
    updateShown();
    EXPECT_EQ(stalePixels(), 0) << "after the closing fence and Enter";
    double lastX = editor.cursorRectOnPage().x();
    const double lineY = editor.cursorRectOnPage().y();
    for (const char c: std::string("after")) {
        type(std::string(1, c));
        updateShown();
        EXPECT_EQ(stalePixels(), 0) << "\"" << c << "\" is shown at once (text: \"" << editor.text() << "\")";
        const QRectF cursor = editor.cursorRectOnPage();
        EXPECT_GT(cursor.x(), lastX + 1) << "the cursor goes along after \"" << c << "\"";
        EXPECT_NEAR(cursor.y(), lineY, 2) << "on the line after the code block";
        lastX = cursor.x();
    }
    // The paragraph is drawn: ink right of the text's left edge on the cursor's line
    const auto info = view->getPage(0)->bufferInfo();
    const double s = info.zoom * info.dpiScale;
    const QRectF cursor = editor.cursorRectOnPage();
    const QRect line(static_cast<int>((cursor.x() - 30) * s), static_cast<int>(cursor.y() * s),
                     static_cast<int>(28 * s), static_cast<int>(cursor.height() * s));
    const QImage img = composed().copy(line);
    int ink = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            ink += qGray(img.pixel(x, y)) < 128;
        }
    }
    EXPECT_GT(ink, 20) << "the typed word is drawn before the cursor";
}
