/*
 * xournal-qt: citations in the real window (qt/docs/citations.md): selected text is looked up - Google Scholar and a
 * translator in the browser, the address always shown before it opens; a reference finds its paper in the library.
 *
 * No test touches the network or starts a browser: the browser is a fake SystemApps.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <functional>
#include <shared_mutex>

#include <QBuffer>
#include <QClipboard>
#include <QImage>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
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
#include "markdown/MdBox.h"
#include "markdown/MdImages.h"
#include "model/Document.h"
#include "model/Text.h"
#include "session/TextDocument.h"
#include "session/DocumentSession.h"
#include "shell/Citations.h"
#include "shell/LibraryModel.h"
#include "shell/ReferenceMode.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/Previews.h"
#include "shell/RecentFiles.h"
#include "shell/SettingsModel.h"
#include "shell/SystemApps.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include "../ArxivSamples.h"
#include "../CitationPdfs.h"
#include "../FakeNet.h"
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
/// A second one, with an arXiv ID
constexpr const char* ARXIV_REFERENCE =
        "[2] T. B. Brown et al., Language models are few-shot learners, arXiv:2005.14165, 2020.";
constexpr double ARXIV_REF_Y = 230;

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
                           {{"References", 14, 150, REF_X}, {REFERENCE, 8, REF_Y, REF_X},
                            {ARXIV_REFERENCE, 8, ARXIV_REF_Y, REF_X}});
        // The papers, named by numbers as arXiv names them
        fs::create_directories(root / "Papers");
        xqt::test::makePaper((root / "Papers" / "1706.03762.pdf").string(), "Attention Is All You Need",
                             "Attention Is All You Need", "Ashish Vaswani, Noam Shazeer", "arXiv:1706.03762v7 [cs.CL]");
        xqt::test::makePaper((root / "Papers" / "2010.13154.pdf").string(), "",
                             "Attention is All You Need in Speech Separation", "Cem Subakan, Mirco Ravanelli");
        xqt::test::makePaper((root / "Papers" / "1512.03385.pdf").string(), "",
                             "Deep Residual Learning for Image Recognition", "Kaiming He, Xiangyu Zhang");
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
    /// By objectName: a QObject child of the window, else an item of the scene (delegates of lists have no QObject
    /// parent there)
    template <typename T = QObject>
    T* find(const char* name) const {
        if (T* o = window->findChild<T*>(name)) {
            return o;
        }
        std::function<QQuickItem*(QQuickItem*)> walk = [&](QQuickItem* item) -> QQuickItem* {
            if (item->objectName() == QLatin1String(name)) {
                return item;
            }
            for (QQuickItem* c: item->childItems()) {
                if (QQuickItem* f = walk(c)) {
                    return f;
                }
            }
            return nullptr;
        };
        QQuickItem* top = window->contentItem();
        while (top->parentItem()) {
            top = top->parentItem();
        }
        return qobject_cast<T*>(walk(top));
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
        // (where it is once laid out: a list's delegate, a Flow, are placed after they appear)
        QPointF was(-1e9, -1e9);
        until([&] {
            auto* i = find<QQuickItem>(name);
            const QPointF at = i ? i->mapToScene(QPointF(0, 0)) : QPointF();
            const bool still = at == was;
            was = at;
            if (!still) {
                wait(30);
            }
            return still;
        });
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
    void selectReference() { selectLine(REF_Y, "Attention is all you need"); }
    void selectLine(double y, const char* text) {
        ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "refs.pdf").string())));
        wait(200);
        xqt::CanvasView* v = view();
        ASSERT_NE(v, nullptr);
        const double zoom = v->getViewController().zoom();
        const QPointF onLine = v->pageViewRect(0).topLeft() + QPointF(REF_X + 120, y - 3) * zoom;
        ASSERT_TRUE(v->selectPdfTextAt(onLine, true));
        until([&] { return shown("pdfLookUpButton"); });
        ASSERT_TRUE(controller->pdfTextIsSelected());
        EXPECT_TRUE(controller->selectedText().contains(text)) << controller->selectedText().toStdString();
    }

    xqt::LibraryModel* library() const { return qobject_cast<xqt::LibraryModel*>(controller->libraryModel()); }
    void waitForTheIndex() {
        until([&] { return library()->indexTotal() > 0 && !library()->indexing(); }, 15000);
        ASSERT_FALSE(library()->indexing());
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

// A reference (selected in a PDF) finds its paper in the library by the paper's title - the file is named by its
// arXiv number -, and it opens beside the notes. Copy link puts a link to it on the clipboard.
TEST_F(CitationsTest, aReferenceFindsItsPaperInTheLibraryAndOpensItAsReference) {
    waitForTheIndex();
    selectReference();
    click("pdfLookUpButton");
    shot("3-lookup-menu-find");
    click("lookUpFindPaper");
    until([&] { return shown("findPaperSheet"); });
    ASSERT_TRUE(shown("findPaperSheet"));
    EXPECT_EQ(find("findPaperTitle")->property("text").toString(), "Attention is all you need") << "the guessed title";
    auto* citations = qobject_cast<xqt::Citations*>(controller->citationsObject());
    until([&] { return !citations->searchingPapers() && !citations->paperHits().isEmpty(); });
    const QVariantList hits = citations->paperHits();
    QStringList names;
    for (const QVariant& h: hits) {
        names << h.toMap()["fileName"].toString();
    }
    ASSERT_EQ(hits.size(), 2) << "the paper and the one with a longer title; not the others, not refs.pdf itself: "
                              << names.join(", ").toStdString();
    const QVariantMap first = hits.front().toMap();
    EXPECT_EQ(first["fileName"].toString(), "1706.03762.pdf");
    EXPECT_EQ(first["title"].toString(), "Attention Is All You Need");
    EXPECT_EQ(first["folder"].toString(), "Papers");
    EXPECT_EQ(first["score"].toInt(), 100);
    EXPECT_EQ(hits[1].toMap()["fileName"].toString(), "2010.13154.pdf");
    until([&] { return find("findPaperHitTitle") != nullptr; });
    ASSERT_NE(find("findPaperHitTitle"), nullptr);
    EXPECT_EQ(find("findPaperHitTitle")->property("text").toString(), "Attention Is All You Need") << "listed";
    EXPECT_TRUE(find("findPaperScholarUrl")->property("text").toString().startsWith("https://scholar.google.com/"))
            << "the web search's address is shown too";
    shot("4-find-paper");

    // A corrected title searches again
    auto* field = find<QQuickItem>("findPaperTitle");
    field->setProperty("text", "Deep residual learning");
    QMetaObject::invokeMethod(find("findPaperSheet"), "search");
    until([&] { return !citations->searchingPapers() && !citations->paperHits().isEmpty() &&
                       citations->paperHits().front().toMap()["fileName"] == "1512.03385.pdf"; });
    EXPECT_EQ(citations->paperHits().front().toMap()["fileName"].toString(), "1512.03385.pdf")
            << find("findPaperTitle")->property("text").toString().toStdString() << " / "
            << citations->paperHits().size();
    field->setProperty("text", "Attention is all you need");
    QMetaObject::invokeMethod(find("findPaperSheet"), "search");
    until([&] { return !citations->searchingPapers() && citations->paperHits().size() == 2; });

    // Open as reference: beside the notes
    click("findPaperReference");
    until([&] { return controller->reference().active(); });
    ASSERT_TRUE(controller->reference().active());
    EXPECT_EQ(controller->reference().title(), "1706.03762.pdf");
    until([&] { return !shown("findPaperSheet"); });
    EXPECT_FALSE(shown("findPaperSheet"));
    EXPECT_EQ(QString::fromStdString(controller->tabManager().currentSession()->documentFile().filename().string()),
              "refs.pdf") << "the notes stay";
    shot("5-reference");
}

TEST_F(CitationsTest, copyLinkOfAHitIsALinkToThePaper) {
    waitForTheIndex();
    selectReference();
    click("pdfLookUpButton");
    click("lookUpFindPaper");
    auto* citations = qobject_cast<xqt::Citations*>(controller->citationsObject());
    until([&] { return !citations->searchingPapers() && !citations->paperHits().isEmpty(); });
    click("findPaperCopyLink");
    until([&] { return !controller->clipboardLinkMarkdown().isEmpty(); });
    EXPECT_TRUE(controller->clipboardLinkMarkdown().contains("1706.03762.pdf"))
            << controller->clipboardLinkMarkdown().toStdString();
}

// Nothing in the library: the web search is offered, its address shown.
TEST_F(CitationsTest, withoutAHitGoogleScholarIsOffered) {
    waitForTheIndex();
    controller->newDocument();
    wait(100);
    QMetaObject::invokeMethod(find("findPaperSheet"), "openFor",
                              Q_ARG(QVariant, QVariant("[4] S. Hochreiter and J. Schmidhuber, \"Long short-term memory,\" "
                                                       "Neural Comput., vol. 9, no. 8, pp. 1735-1780, 1997.")));
    auto* citations = qobject_cast<xqt::Citations*>(controller->citationsObject());
    until([&] { return shown("findPaperSheet") && !citations->searchingPapers(); });
    EXPECT_TRUE(citations->paperHits().isEmpty());
    EXPECT_TRUE(find("findPaperStatus")->property("text").toString().startsWith("No document"));
    const QString shownUrl = find("findPaperScholarUrl")->property("text").toString();
    EXPECT_TRUE(shownUrl.contains("Long short-term memory")) << shownUrl.toStdString();
    click("findPaperScholar");
    until([&] { return shown("webConfirm"); });
    EXPECT_TRUE(shown("webConfirm")) << "asked with the address first";
    click("webConfirmOpen");
    until([&] { return !browser.opened.isEmpty(); });
    ASSERT_EQ(browser.opened.size(), 1);
    EXPECT_EQ(QUrlQuery(browser.opened.front()).queryItemValue("q", QUrl::FullyDecoded), "Long short-term memory");
}

// An arXiv ID in a reference: the menu offers the paper; the arXiv sheet shows the address it asks, asks once whether
// the app may connect (nothing is sent before), then lists arXiv's answer; "Download into the library" saves the PDF
// named by its title, and it opens beside the notes.
TEST_F(CitationsTest, anArxivPaperIsDownloadedAfterTheOptInAndOpensAsReference) {
    xqt::test::FakeNet net;
    QTemporaryDir answers;  // (outside the library)
    const std::string answerPdf = answers.filePath("answer.pdf").toStdString();
    xqt::test::makePaper(answerPdf, "Attention Is All You Need", "Attention Is All You Need", "Ashish Vaswani");
    QFile answerFile(QString::fromStdString(answerPdf));
    ASSERT_TRUE(answerFile.open(QIODevice::ReadOnly));
    const QByteArray pdfBytes = answerFile.readAll();
    net.answer = [&](const QUrl& url) {
        return xqt::test::FakeNet::ok(url.host() == "export.arxiv.org" ? QByteArray(xqt::test::ARXIV_SEARCH) : pdfBytes);
    };
    xqt::ArxivQueue::setInterval(0);
    settings()->set("networkAccess", "ask");
    waitForTheIndex();

    selectLine(ARXIV_REF_Y, "arXiv:2005.14165");
    click("pdfLookUpButton");
    until([&] { return shown("lookUpArxiv"); });
    ASSERT_TRUE(shown("lookUpArxiv")) << "the menu offers the paper of the ID";
    EXPECT_EQ(find("lookUpArxivUrl")->property("text").toString(),
              "https://export.arxiv.org/api/query?id_list=2005.14165") << "with the address it asks";
    EXPECT_TRUE(shown("lookUpArxivPage"));
    click("lookUpArxiv");
    until([&] { return shown("arxivSheet"); });
    ASSERT_TRUE(shown("arxivSheet"));
    EXPECT_EQ(find("arxivRequestUrl")->property("text").toString(),
              "https://export.arxiv.org/api/query?id_list=2005.14165") << "the address, next to its button";
    shot("6-arxiv-sheet");

    // The first request asks; "Not now" sends nothing
    click("arxivRequest");
    until([&] { return shown("networkOptIn"); });
    ASSERT_TRUE(shown("networkOptIn"));
    shot("7-network-opt-in");
    click("networkOptInNo");
    until([&] { return !shown("networkOptIn"); });
    wait(100);
    EXPECT_TRUE(net.calls.empty()) << "nothing sent";
    EXPECT_EQ(settings()->get("networkAccess").toString(), "ask");

    // Allow: the setting, then the request - exactly the address shown
    click("arxivRequest");
    until([&] { return shown("networkOptIn"); });
    click("networkOptInAllow");
    until([&] { return !net.calls.empty() && !shown("networkOptIn"); });
    ASSERT_EQ(net.calls.size(), 1u);
    EXPECT_EQ(net.calls[0].url.toString(QUrl::FullyEncoded), "https://export.arxiv.org/api/query?id_list=2005.14165");
    EXPECT_EQ(settings()->get("networkAccess").toString(), "on") << "not asked again";
    until([&] { return find("arxivResultTitle") != nullptr; });
    ASSERT_NE(find("arxivResultTitle"), nullptr);
    EXPECT_EQ(find("arxivResultTitle")->property("text").toString(), "Attention Is All You Need");
    EXPECT_EQ(find("arxivPdfUrl")->property("text").toString(), "https://arxiv.org/pdf/1706.03762v7")
            << "the PDF's address is shown before it is fetched";
    shot("8-arxiv-results");

    // Download: into the library's folder, named by the title
    click("arxivDownload");
    const fs::path saved = root / "Attention Is All You Need (1706.03762).pdf";
    until([&] { return fs::exists(saved) && shown("arxivOpenReference"); }, 15000);
    ASSERT_TRUE(fs::exists(saved)) << "calls: " << net.calls.size() << ", error: "
                                   << find("arxivStatus")->property("text").toString().toStdString();
    ASSERT_EQ(net.calls.size(), 2u);
    EXPECT_EQ(net.calls[1].url.toString(), "https://arxiv.org/pdf/1706.03762v7");
    ASSERT_TRUE(shown("arxivOpenReference")) << "then offered to open";
    shot("9-arxiv-downloaded");
    click("arxivOpenReference");
    until([&] { return controller->reference().active(); });
    ASSERT_TRUE(controller->reference().active());
    EXPECT_EQ(controller->reference().title(), "Attention Is All You Need (1706.03762).pdf");
    settings()->set("networkAccess", "ask");
    xqt::ArxivQueue::setInterval(3000);
}

// A web picture in a Markdown text (qt/docs/md-images.md): never fetched unasked. Its "Load image" shows the whole
// address (and, while connecting was not decided, what that means); Cancel sends nothing; Load fetches it (the opt-in
// then), keeps it in the app cache and the text shows it. Networking off: nothing is sent.
TEST_F(CitationsTest, aWebPictureIsLoadedOnlyWhenAskedWithItsAddressShown) {
    xqt::test::FakeNet net;
    QImage logo(24, 12, QImage::Format_RGB32);
    logo.fill(Qt::darkGreen);
    QByteArray png;
    {
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        logo.save(&b, "PNG");
    }
    net.answer = [png](const QUrl&) { return xqt::test::FakeNet::ok(png); };
    const QString url = "https://example.org/pics/logo.png?size=2";
    std::ofstream(root / "web.md") << "# Web\n\n![Logo](" << url.toStdString() << ")\n";
    settings()->set("networkAccess", "ask");
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "web.md").string())));
    wait(100);
    EXPECT_TRUE(net.calls.empty()) << "nothing is fetched when the text is shown";
    ASSERT_NE(view(), nullptr);

    // "Load image" tapped: the address is shown, and what connecting means
    Q_EMIT view()->imageLoadRequested(url);
    until([&] { return shown("webImageConfirm"); });
    ASSERT_TRUE(shown("webImageConfirm"));
    EXPECT_EQ(find("webImageUrl")->property("text").toString(), url);
    EXPECT_TRUE(find("webImageHost")->property("text").toString().contains("example.org"));
    EXPECT_TRUE(shown("webImageOptIn"));
    click("webImageCancel");
    until([&] { return !shown("webImageConfirm"); });
    wait(50);
    EXPECT_TRUE(net.calls.empty()) << "Cancel sends nothing";
    EXPECT_EQ(settings()->get("networkAccess").toString(), "ask");

    // Load: fetched once, kept in the app cache, shown
    Q_EMIT view()->imageLoadRequested(url);
    until([&] { return shown("webImageConfirm"); });
    click("webImageLoad");
    until([&] { return !net.calls.empty(); });
    ASSERT_EQ(net.calls.size(), 1u);
    EXPECT_EQ(net.calls[0].url.toString(), url);
    EXPECT_EQ(settings()->get("networkAccess").toString(), "on") << "loading was the opt-in";
    until([&] { return xqt::md::images::info(url.toStdString()).state == xqt::md::images::Info::State::Ok; });
    const auto info = xqt::md::images::info(url.toStdString());
    EXPECT_EQ(info.state, xqt::md::images::Info::State::Ok);
    EXPECT_EQ(info.width, 24);
    EXPECT_NE(info.path.find("web-images"), std::string::npos) << info.path;
    EXPECT_FALSE(fs::exists(root / "web.assets")) << "not next to the document";
    {
        // The text shows it now
        xqt::DocumentSession* s = controller->tabManager().currentSession();
        std::shared_lock lock(*s->getDocument());
        const Text* box = xqt::TextDocument::pageBoxOf(s->getDocument()->getPage(0));
        ASSERT_NE(box, nullptr);
        const auto pictures = xqt::md::imageRects(xqt::md::cachedLayout(box->getText(), xqt::md::styleOf(*box)));
        ASSERT_EQ(pictures.size(), 1u);
        EXPECT_TRUE(pictures[0].drawn);
    }

    // Networking off: the tap sends nothing (the window says it is off)
    until([&] { return !shown("webImageConfirm"); });
    settings()->set("networkAccess", "off");
    const QString other = "https://example.org/pics/other.png";
    Q_EMIT view()->imageLoadRequested(other);
    wait(100);
    EXPECT_FALSE(shown("webImageConfirm"));
    EXPECT_EQ(net.calls.size(), 1u);
    settings()->set("networkAccess", "ask");
}
