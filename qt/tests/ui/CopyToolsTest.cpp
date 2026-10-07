/*
 * xournal-qt: the copy tools in the real window (qt/copy-tools): snip as a button of its own (qt/docs/snip.md), and
 * handwriting copied as text (qt/docs/handwriting-search.md, "Copy handwriting as text"): the text tools' second tool
 * (a sweep over ink, then the tool before), "Copy as text" of the selection's pill, the card with the text, and what
 * the window says when the handwriting search is off. The handwriting is read by a scripted recogniser.
 *
 * @license GNU GPLv2 or later
 */
#include <functional>
#include <memory>
#include <shared_mutex>

#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <gtest/gtest.h>

#include "hwr/FakeRecognizer.h"
#include "hwr/HandwritingSearch.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "canvas/CanvasView.h"
#include "session/DocumentSession.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/Previews.h"
#include "shell/RecentFiles.h"
#include "shell/SettingsModel.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include "AppController.h"
#include "UiFixture.h"

using namespace xqt;

namespace {
/// A written word: a zigzag of `letters` letters, 10 pt high, at x, y
std::unique_ptr<Stroke> written(double x, double y, int letters = 4) {
    auto s = std::make_unique<Stroke>();
    s->setWidth(1.0);
    s->setColor(Colors::black);
    for (int i = 0; i <= 2 * letters; ++i) {
        s->addPoint(Point(x + i * 3.0, y + (i % 2 ? 10 : 0)));
    }
    return s;
}

class CopyToolsTest: public xqt::test::UiFixture {
protected:
    void SetUp() override {
        // The handwriting is read by a scripted recogniser: a line of three words "Alpha beta gamma" (gamma unsure),
        // a line of four "delta epsilon zeta eta."
        fake = std::make_shared<hwr::FakeRecognizer>();
        fake->setScript([](const hwr::LineInput& line, size_t word) -> hwr::FakeRecognizer::Readings {
            static const QStringList three{"Alpha", "beta", "gamma"};
            static const QStringList four{"delta", "epsilon", "zeta", "eta."};
            const QStringList& words = line.words.size() == 3 ? three : four;
            return {{words.value(static_cast<int>(word), QStringLiteral("w")), 1.0f}};
        });
        fake->setConfidence([](const hwr::LineInput& line, size_t word) {
            return line.words.size() == 3 && word == 2 ? 0.3f : 0.9f;
        });
        hwr::HandwritingSearch::setFactory([f = fake](const QString&) { return f; });
        makeController();
        qobject_cast<RecentFiles*>(controller->recentModel())->clear();
        ASSERT_NO_FATAL_FAILURE(loadWindow({.size = QSize(1920, 1080)}));
        QGuiApplication::clipboard()->clear();
        wait(100);
    }
    void TearDown() override {
        handwriting(false);
        closeApp();
        hwr::HandwritingSearch::setFactory({});
    }

    SettingsModel* settings() const { return qobject_cast<SettingsModel*>(controller->settingsModel()); }
    bool isInside(QQuickItem* item, QQuickItem* in) const {
        const QRectF a(item->mapToScene(QPointF(0, 0)), item->size());
        const QRectF b(in->mapToScene(QPointF(0, 0)), in->size());
        return b.adjusted(-1, -1, 1, 1).contains(a);
    }
    DocumentSession* current() const { return controller->tabManager().currentSession(); }
    CanvasView* view() const { return controller->tabManager().currentView(); }
    void handwriting(bool on) {
        if (controller) {
            controller->handwritingSettings()->setProperty("enabled", on);
        }
    }
    /// A new document with two lines of handwriting on page 1: three words at y 100 (x 50, 110, 170), four at y 140
    void makeNotes() {
        controller->newDocument();
        PageRef page = current()->getDocument()->getPage(0);
        {
            std::unique_lock lock(*current()->getDocument());
            for (int w = 0; w < 3; ++w) {
                page->getSelectedLayer()->addElement(written(50 + 60 * w, 100));
            }
            for (int w = 0; w < 4; ++w) {
                page->getSelectedLayer()->addElement(written(50 + 60 * w, 140, 5));
            }
        }
        page->firePageChanged();
        view()->getViewController().scrollToPageRect(0, QRectF(0, 0, 400, 400));
        wait(200);
    }
    QPoint onPage(double x, double y) const {
        auto* canvas = find<QQuickItem>("canvas");
        const QPointF at = view()->pageViewRect(0).topLeft() + QPointF(x, y) * view()->getViewController().zoom();
        return canvas->mapToScene(at).toPoint();
    }
    void drag(const std::vector<QPointF>& points) {
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, onPage(points.front().x(), points.front().y()));
        for (size_t i = 1; i < points.size(); ++i) {
            const QPointF a = points[i - 1], b = points[i];
            for (int k = 1; k <= 6; ++k) {
                const QPointF p = a + (b - a) * k / 6.0;
                QTest::mouseMove(window, onPage(p.x(), p.y()));
                wait(5);
            }
        }
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, onPage(points.back().x(), points.back().y()));
    }
    QString clipboardText() const { return QGuiApplication::clipboard()->text(); }
    QString snackbar() const {
        auto* bar = find<QQuickItem>("snackbar");
        return bar && bar->isVisible() ? find<QObject>("snackbarText")->property("text").toString() : QString();
    }
    void openList(const char* button, const char* menu) {
        QMetaObject::invokeMethod(find<QQuickItem>(button), "pressAndHold");
        until([&] { return find<QObject>(menu)->property("visible").toBool(); });
    }

    std::shared_ptr<hwr::FakeRecognizer> fake;
};
}  // namespace

// Snip is one tap away: a fixed tool of the rail (a tap while armed: the other shape), and the select button's list
// holds the selections only (one place each)
TEST_F(CopyToolsTest, snipHasAButtonOfItsOwn) {
    makeNotes();
    controller->selectTool("pen");
    auto* snip = find<QQuickItem>("snipButton");
    ASSERT_NE(snip, nullptr);
    until([&] { return snip->isVisible(); });
    ASSERT_TRUE(snip->isVisible());
    EXPECT_TRUE(isInside(snip, find<QQuickItem>("toolbox"))) << "a fixed tool of the rail";
    openList("selectButton", "selectButtonVariants");
    QObject* selects = find<QObject>("selectButtonVariants");
    EXPECT_NE(entryOf(selects, "variant_selectRegion"), nullptr);
    EXPECT_EQ(entryOf(selects, "variant_snipRect"), nullptr) << "no snips in the select list";
    EXPECT_EQ(entryOf(selects, "variant_snipLasso"), nullptr);
    QMetaObject::invokeMethod(selects, "close");
    until([&] { return !selects->property("visible").toBool(); });

    QMetaObject::invokeMethod(snip, "clicked");
    const QString first = controller->snipShape();
    ASSERT_FALSE(first.isEmpty());
    EXPECT_TRUE(snip->property("checked").toBool());
    EXPECT_FALSE(find<QQuickItem>("selectButton")->property("checked").toBool()) << "the snip's button, not the selection's";
    QMetaObject::invokeMethod(snip, "clicked");
    EXPECT_EQ(controller->snipShape(), first == "rect" ? "lasso" : "rect") << "a tap while armed: the other shape";
    EXPECT_EQ(snip->property("currentKey").toString(), first == "rect" ? "snipLasso" : "snipRect");
    // Shift+S: the rectangle; the button follows
    QTest::keyClick(window, Qt::Key_S, Qt::ShiftModifier);
    wait(50);
    EXPECT_EQ(controller->snipShape(), "rect");
    EXPECT_EQ(snip->property("currentKey").toString(), "snipRect");
    QTest::keyClick(window, Qt::Key_Escape);
    wait(50);
    EXPECT_EQ(controller->snipShape(), "");
    EXPECT_EQ(controller->tool(), "pen");
}

// The text tools' second tool: a sweep over handwriting puts its words on the clipboard as text, the tool before is
// back at once, and a card near the words shows the text (an unsure word grey); the button remembers the tool
TEST_F(CopyToolsTest, aSweepCopiesTheHandwritingAsTextAndGivesThePenBack) {
    handwriting(true);
    makeNotes();
    controller->selectTool("pen");
    auto* button = find<QQuickItem>("pdfTextButton");
    ASSERT_NE(button, nullptr);
    openList("pdfTextButton", "pdfTextMenu");
    QObject* item = entryOf(find<QObject>("pdfTextMenu"), "copyInkTextItem");
    ASSERT_NE(item, nullptr);
    QMetaObject::invokeMethod(item, "triggered");
    until([&] { return !find<QObject>("pdfTextMenu")->property("visible").toBool(); });
    wait(300);
    EXPECT_TRUE(controller->inkCopyArmed());
    EXPECT_EQ(controller->snipShape(), "") << "not a snip of a picture";
    EXPECT_TRUE(button->property("checked").toBool());
    EXPECT_EQ(button->property("currentKey").toString(), "copyInkText");
    EXPECT_FALSE(find<QQuickItem>("selectButton")->property("checked").toBool());
    EXPECT_FALSE(find<QQuickItem>("snipButton")->property("checked").toBool());

    // Along the first line
    drag({{45, 105}, {120, 106}, {200, 104}});
    EXPECT_EQ(controller->tool(), "pen") << "the pen is back at once";
    EXPECT_FALSE(controller->inkCopyArmed());
    until([&] { return clipboardText() == "Alpha beta gamma"; });
    EXPECT_EQ(clipboardText(), "Alpha beta gamma");
    auto* toast = find<QQuickItem>("inkTextToast");
    until([&] { return toast->isVisible(); });
    ASSERT_TRUE(toast->isVisible());
    nextFrame();  // (it places itself once shown: Qt.callLater)
    EXPECT_EQ(find<QObject>("inkTextToastTitle")->property("text").toString(), "Copied as text");
    const QString html = toast->property("html").toString();
    EXPECT_TRUE(html.contains("Alpha beta")) << html.toStdString();
    EXPECT_TRUE(html.contains("color:#9aa0a6\">gamma")) << "the unsure word grey: " << html.toStdString();
    EXPECT_TRUE(find<QObject>("inkTextToastNote")->property("text").toString().contains("unsure"));
    // Near the words: above or below them, not across the window
    const QRectF card(toast->mapToScene(QPointF(0, 0)), QSizeF(toast->width(), toast->height()));
    const QPoint line = onPage(120, 105);
    EXPECT_LT(std::min(std::abs(card.bottom() - line.y()), std::abs(card.top() - line.y())), 120)
            << "near the words";
    // The button remembers it: a tap from the pen copies again (a loop around the second line)
    QMetaObject::invokeMethod(button, "clicked");
    EXPECT_TRUE(controller->inkCopyArmed());
    drag({{40, 135}, {290, 135}, {290, 158}, {40, 158}, {41, 136}});
    until([&] { return clipboardText() == "delta epsilon zeta eta."; });
    EXPECT_EQ(clipboardText(), "delta epsilon zeta eta.");
    EXPECT_EQ(controller->tool(), "pen");
    // A tap while it is in use: mark PDF text
    QMetaObject::invokeMethod(button, "clicked");
    QMetaObject::invokeMethod(button, "clicked");
    EXPECT_EQ(controller->tool(), "selectPdfTextLinear");
    // × closes the card
    QMetaObject::invokeMethod(find<QObject>("inkTextToastClose"), "clicked");
    EXPECT_FALSE(toast->isVisible());
}

// A selection with ink: its pill offers "Copy as text" beside Copy, with every word in reading order
TEST_F(CopyToolsTest, theSelectionPillCopiesTheHandwritingAsText) {
    handwriting(true);
    makeNotes();
    controller->selectAllOnPage();
    until([&] { return controller->hasSelection(); });
    auto* copyText = find<QQuickItem>("selectionCopyText");
    ASSERT_NE(copyText, nullptr);
    until([&] { return copyText->isVisible(); });
    ASSERT_TRUE(copyText->isVisible());
    EXPECT_TRUE(find<QQuickItem>("selectionCopy")->isVisible());
    QMetaObject::invokeMethod(copyText, "clicked");
    const QString both = "Alpha beta gamma\ndelta epsilon zeta eta.";
    until([&] { return clipboardText() == both; });
    EXPECT_EQ(clipboardText(), both);
    until([&] { return find<QQuickItem>("inkTextToast")->isVisible(); });
    EXPECT_TRUE(find<QQuickItem>("inkTextToast")->isVisible());
    EXPECT_TRUE(controller->hasSelection()) << "the selection stays";
    // Nothing but a picture selected: no "Copy as text"
    controller->clearSelection();
    until([&] { return !copyText->isVisible(); });
    EXPECT_FALSE(copyText->isVisible());
}

// Without the handwriting search (or a model) the window says so plainly, with the way to Settings
TEST_F(CopyToolsTest, withoutTheHandwritingSearchTheWindowPointsToSettings) {
    makeNotes();
    controller->selectTool("pen");
    QTest::keyClick(window, Qt::Key_T, Qt::ShiftModifier);  // (Copy handwriting as text)
    wait(50);
    EXPECT_FALSE(controller->inkCopyArmed()) << "nothing to sweep with";
    EXPECT_EQ(controller->tool(), "pen");
    until([&] { return !snackbar().isEmpty(); });
    EXPECT_TRUE(snackbar().contains("handwriting search")) << snackbar().toStdString();
    auto* action = find<QObject>("snackbarAction");
    ASSERT_TRUE(action->property("visible").toBool());
    EXPECT_EQ(action->property("text").toString(), "Settings");
    QMetaObject::invokeMethod(action, "clicked");
    auto* settings = find<QObject>("settingsPage");
    until([&] { return settings->property("opened").toBool(); });
    EXPECT_TRUE(settings->property("opened").toBool());
    EXPECT_EQ(find<QObject>("settingsSections")->property("currentIndex").toInt(), 5) << "Search";
    QMetaObject::invokeMethod(settings, "close");
    wait(300);

    // On, but no model: the sweep says so
    fake->setReady(false, "no model");
    handwriting(true);
    controller->startInkCopy();
    drag({{45, 105}, {200, 105}});
    until([&] { return snackbar().contains("model"); });
    EXPECT_TRUE(snackbar().contains("No handwriting model")) << snackbar().toStdString();
    EXPECT_EQ(controller->tool(), "pen");
    EXPECT_TRUE(clipboardText().isEmpty());
}
