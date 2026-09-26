/*
 * xournal-qt: citations in the real window (qt/docs/citations.md): selected text is looked up - Google Scholar and a
 * translator in the browser, the address always shown before it opens.
 *
 * No test touches the network or starts a browser: the browser is a fake SystemApps.
 *
 * @license GNU GPLv2 or later
 */
#include <functional>

#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>
#include <gtest/gtest.h>

#include "canvas/CanvasView.h"
#include "canvas/MarkdownEditor.h"
#include "canvas/ViewController.h"
#include "session/DocumentSession.h"
#include "shell/Citations.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/Previews.h"
#include "shell/RecentFiles.h"
#include "shell/SettingsModel.h"
#include "shell/SystemApps.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include "../CitationPdfs.h"
#include "AppController.h"

namespace fs = std::filesystem;

namespace {
/// The browser: records what it was asked to open
struct FakeBrowser: xqt::SystemApps {
    QList<QUrl> opened;
    bool openWebAddress(const QUrl& url) override {
        opened << url;
        return true;
    }
};

/// The reference line of refs.pdf, and where it is on its page (points)
constexpr const char* REFERENCE =
        "[1] A. Vaswani, N. Shazeer, et al., \"Attention is all you need,\" in Proc. NeurIPS, 2017, pp. 5998-6008.";
constexpr double REF_X = 40, REF_Y = 200;

class CitationsTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        xqt::SystemApps::setInstance(&browser);
        controller = std::make_unique<AppController>();
        settings()->set("webConfirm", true);
        settings()->set("translateService", "google");
        settings()->set("translateLanguage", "de");
        xqt::test::makePdf((root / "refs.pdf").string(), "",
                           {{"References", 14, 150, REF_X}, {REFERENCE, 8, REF_Y, REF_X}});
        controller->setLibraryRoot(root);
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->addImageProvider("thumbnail", new xqt::ThumbnailProvider);
        engine->addImageProvider("sketch", new xqt::SketchProvider);
        engine->addImageProvider("preview", new xqt::PreviewProvider);
        engine->addImageProvider("hitpage", new xqt::HitPageProvider);
        engine->addImageProvider("mdsnippet", new xqt::MdSnippetProvider);
        engine->rootContext()->setContextProperty("app", controller.get());
        engine->loadFromModule("XournalQt", "Main");
        ASSERT_FALSE(engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
        ASSERT_NE(window, nullptr);
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        QTest::mouseMove(window, QPoint(-20, -20));
        wait(100);
    }
    void TearDown() override {
        settings()->set("webConfirm", true);
        settings()->set("translateLanguage", "");
        controller->shutdown();
        engine.reset();
        controller.reset();
        xqt::SystemApps::setInstance(nullptr);
    }
    xqt::SettingsModel* settings() const { return qobject_cast<xqt::SettingsModel*>(controller->settingsModel()); }

    static void wait(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
    }
    void until(const std::function<bool()>& done, int ms = 5000) {
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < ms) {
            wait(20);
        }
    }
    template <typename T = QObject>
    T* find(const char* name) const {
        return window->findChild<T*>(name);
    }
    bool shown(const char* name) const {
        auto* o = find(name);
        return o && o->property("visible").toBool();
    }
    void click(QQuickItem* item) {
        ASSERT_NE(item, nullptr);
        ASSERT_TRUE(item->isVisible());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        wait(50);
    }
    void click(const char* name) {
        until([&] { auto* i = find<QQuickItem>(name); return i && i->isVisible() && i->width() > 0; });
        // (in a menu or a dialog: once it is fully open, not while it grows into place)
        for (QObject* p = find(name); p; p = p->parent()) {
            if (p->inherits("QQuickPopup")) {
                until([&] { return p->property("opened").toBool(); });
                break;
            }
        }
        SCOPED_TRACE(name);
        click(find<QQuickItem>(name));
    }
    /// XQT_CITE_SHOTS=<folder>: pictures of the window at the steps (to look at them; nothing is compared)
    void shot(const char* name) const {
        if (const QString folder = qEnvironmentVariable("XQT_CITE_SHOTS"); !folder.isEmpty()) {
            wait(300);
            window->grabWindow().save(folder + "/" + name + ".png");
        }
    }
    xqt::CanvasView* view() const {
        auto* canvasItem = find<QQuickItem>("canvas");
        return canvasItem ? qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>()) : nullptr;
    }
    /// Open refs.pdf and select its reference line (a long press on a line with the PDF text tool selects the line)
    void selectReference() {
        ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "refs.pdf").string())));
        wait(200);
        xqt::CanvasView* v = view();
        ASSERT_NE(v, nullptr);
        const double zoom = v->getViewController().zoom();
        const QPointF onLine = v->pageViewRect(0).topLeft() + QPointF(REF_X + 120, REF_Y - 3) * zoom;
        ASSERT_TRUE(v->selectPdfTextAt(onLine, true));
        until([&] { return shown("pdfLookUpButton"); });
        ASSERT_TRUE(controller->pdfTextIsSelected());
        EXPECT_TRUE(controller->selectedText().contains("Attention is all you need")) << controller->selectedText().toStdString();
    }

    QTemporaryDir tmp;
    fs::path root;
    FakeBrowser browser;
    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
};
}  // namespace

// Search in Google Scholar: the menu shows the address, the window asks with the whole address, and only Open opens
// it. "Don't ask again" turns the question off (the menu still shows the address), and Settings turns it on again.
TEST_F(CitationsTest, googleScholarOpensOnlyAfterItsAddressWasShown) {
    selectReference();
    click("pdfLookUpButton");
    until([&] { return shown("lookUpScholar"); });
    ASSERT_TRUE(shown("lookUpScholar"));
    auto* shownUrl = find("lookUpScholarUrl");
    ASSERT_NE(shownUrl, nullptr);
    EXPECT_TRUE(shownUrl->property("text").toString().startsWith("https://scholar.google.com/scholar?q="))
            << "the menu shows the address: " << shownUrl->property("text").toString().toStdString();
    shot("1-lookup-menu");
    click("lookUpScholar");
    until([&] { return shown("webConfirm"); });
    ASSERT_TRUE(shown("webConfirm")) << "asked first";
    shot("2-web-confirm");
    EXPECT_TRUE(browser.opened.isEmpty()) << "nothing opened before the answer";
    const QString url = find("webConfirmUrl")->property("text").toString();
    const QUrl asked(url);
    EXPECT_EQ(asked.host(), "scholar.google.com");
    EXPECT_TRUE(QUrlQuery(asked).queryItemValue("q", QUrl::FullyDecoded).contains("Attention is all you need"));

    // Cancel: nothing
    click("webConfirmCancel");
    until([&] { return !shown("webConfirm"); });
    EXPECT_TRUE(browser.opened.isEmpty());

    // Again, Open: exactly that address
    click("pdfLookUpButton");
    click("lookUpScholar");
    until([&] { return shown("webConfirm"); });
    click("webConfirmOpen");
    until([&] { return !browser.opened.isEmpty() && !shown("webConfirm"); });
    ASSERT_EQ(browser.opened.size(), 1);
    EXPECT_EQ(browser.opened.front().toString(QUrl::FullyEncoded), url) << "the address shown is the one opened";

    // Don't ask again: the next one opens from the menu, which showed its address
    auto* citations = qobject_cast<xqt::Citations*>(controller->citationsObject());
    EXPECT_FALSE(citations->translateUrl("x").isEmpty());
    click("pdfLookUpButton");
    until([&] { return shown("lookUpScholar"); });
    EXPECT_TRUE(shown("lookUpTranslate")) << find("lookUpTranslateUrl")->property("text").toString().toStdString();
    click("lookUpTranslate");
    until([&] { return shown("webConfirm"); });
    click("webConfirmDontAsk");
    click("webConfirmOpen");
    until([&] { return browser.opened.size() == 2 && !shown("webConfirm"); });
    ASSERT_EQ(browser.opened.size(), 2);
    EXPECT_EQ(browser.opened.back().host(), "translate.google.com");
    EXPECT_EQ(QUrlQuery(browser.opened.back()).queryItemValue("tl"), "de") << "the language of Settings";
    EXPECT_FALSE(settings()->get("webConfirm").toBool());
    click("pdfLookUpButton");
    until([&] { return shown("lookUpScholar"); });
    click("lookUpScholar");
    until([&] { return browser.opened.size() == 3 && !shown("lookUpMenu"); });
    EXPECT_EQ(browser.opened.size(), 3);
    EXPECT_FALSE(shown("webConfirm")) << "not asked any more";

    // On again (Settings): asked again
    settings()->set("webConfirm", true);
    click("pdfLookUpButton");
    click("lookUpScholar");
    until([&] { return shown("webConfirm"); });
    EXPECT_TRUE(shown("webConfirm"));
    click("webConfirmCancel");
    EXPECT_EQ(browser.opened.size(), 3);
}

// Our own text: the selection of the text being written is looked up the same way (the context pill).
TEST_F(CitationsTest, textBeingWrittenIsLookedUpToo) {
    controller->newDocument();
    wait(100);
    controller->setMarkdownInPanel(false);
    controller->selectTool("text");
    xqt::CanvasView* v = view();
    ASSERT_NE(v, nullptr);
    v->startMarkdown(0, true, 100, 100);
    xqt::MarkdownEditor* editor = v->getMarkdownEditor();
    ASSERT_NE(editor, nullptr);
    QGuiApplication::clipboard()->setText("Kalman filter");
    QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
    wait(50);
    EXPECT_EQ(controller->selectedText(), "") << "nothing selected yet";
    QTest::keyClick(window, Qt::Key_Home, Qt::ShiftModifier);
    wait(50);
    EXPECT_EQ(controller->selectedText(), "Kalman filter");
    auto* citations = qobject_cast<xqt::Citations*>(controller->citationsObject());
    ASSERT_NE(citations, nullptr);
    EXPECT_EQ(QUrlQuery(QUrl(citations->scholarUrl(controller->selectedText()))).queryItemValue("q"), "Kalman filter");

    // The context pill (right click) offers "Look up…" on it
    const QPointF at = v->pageViewRect(0).topLeft() + QPointF(110, 105) * v->getViewController().zoom();
    Q_EMIT controller->contextRequested(at);
    until([&] { return shown("contextLookUp"); });
    EXPECT_TRUE(shown("contextLookUp"));
    QTest::keyClick(window, Qt::Key_Escape);
}
