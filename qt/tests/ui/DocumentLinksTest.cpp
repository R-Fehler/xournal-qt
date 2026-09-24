/*
 * xournal-qt: links between documents in the real window (qt/docs/links.md): a tapped link asks where to open the
 * document (a new tab, the reference, here), remembers the choice if asked to, goes to the chapter, heading or page,
 * and Back and Forward go across documents.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <functional>

#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMimeData>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "canvas/CanvasView.h"
#include "markdown/MdBox.h"
#include "session/DocumentSession.h"
#include "shell/LibraryModel.h"
#include "shell/PageSketches.h"
#include "shell/Previews.h"
#include "shell/RecentFiles.h"
#include "shell/ReferenceMode.h"
#include "shell/SettingsModel.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"

#include "AppController.h"

namespace fs = std::filesystem;

namespace {
void writeFile(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << text;
}

class DocumentLinksTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        controller = std::make_unique<AppController>();
        settings()->set("linkOpening", "ask");
        makeDocuments();
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
        wait(100);
    }
    void TearDown() override {
        settings()->set("linkOpening", "ask");
        controller->shutdown();
        engine.reset();
        controller.reset();
    }
    xqt::SettingsModel* settings() const { return qobject_cast<xqt::SettingsModel*>(controller->settingsModel()); }

    /// Lectures/kalman.xopp: 5 pages, the chapter "Prediction step" on page 4; Notes/a.md links to it and to b.md,
    /// whose "Part two" is on its second page.
    void makeDocuments() {
        controller->newDocument();
        ASSERT_TRUE(controller->insertPages(1, 0, -1, false, 4));
        ASSERT_TRUE(controller->addChapter(3, "Prediction step", 0));
        fs::create_directories(root / "Lectures");
        ASSERT_TRUE(controller->saveAs(QUrl::fromLocalFile(QString::fromStdString((root / "Lectures" / "kalman.xopp").string()))));
        controller->closeTab(0);
        writeFile(root / "Notes" / "a.md",
                  "# Notes\n\nSee [Kalman](../Lectures/kalman.xopp#chapter=Prediction%20step&page=2) first.\n\n"
                  "Then [[b#Part two]] and [gone](../Lectures/kalman.xopp#chapter=Gone&page=5).\n");
        std::string b = "# Part one\n\n";
        for (int i = 0; i < 80; ++i) {
            b += "A line of the first part, number " + std::to_string(i) + ".\n\n";
        }
        b += "## Part two\n\nThe second part.\n";
        writeFile(root / "Notes" / "b.md", b);
    }

    static void wait(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
    }
    void until(const std::function<bool()>& done, int ms = 2000) {
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
    void click(QQuickItem* item) {
        ASSERT_NE(item, nullptr);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        wait(50);
    }
    xqt::DocumentSession* current() const { return controller->tabManager().currentSession(); }
    std::string currentFile() const { return current() ? current()->documentFile().filename().string() : ""; }

    /// Ctrl + click on the words `text` of the current text document's page (text mode follows links so).
    void tapText(const std::string& text) {
        auto* canvasItem = find<QQuickItem>("canvas");
        auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
        ASSERT_NE(view, nullptr);
        const size_t page = current()->getCurrentPageNo();
        const Text* box = nullptr;
        {
            Layer* layer = xqt::md::markdownLayer(current()->getDocument()->getPage(page));
            ASSERT_NE(layer, nullptr);
            box = xqt::md::boxOf(*layer);
        }
        ASSERT_NE(box, nullptr);
        const auto rects = xqt::md::findText(*box, text);
        ASSERT_FALSE(rects.empty()) << text;
        const auto& r = rects.front();
        view->getViewController().scrollToPageRect(page, QRectF(r.x, r.y, r.width, r.height));
        wait(100);
        const QPointF at = view->pageViewRect(page).topLeft() +
                           QPointF(r.x + r.width / 2, r.y + r.height / 2) * view->getViewController().zoom();
        QTest::mouseClick(window, Qt::LeftButton, Qt::ControlModifier, canvasItem->mapToScene(at).toPoint());
        wait(80);
    }
    bool popupOpen() const { return find("linkPopup")->property("opened").toBool(); }

    QTemporaryDir tmp;
    fs::path root;
    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
};
}  // namespace

TEST_F(DocumentLinksTest, aTappedLinkAsksWhereAndBackAndForwardGoAcrossDocuments) {
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "Notes" / "a.md").string())));
    wait(200);
    ASSERT_EQ(currentFile(), "a.md");
    tapText("Kalman");
    until([&] { return popupOpen(); });
    ASSERT_TRUE(popupOpen()) << "a link to a document asks where to open it";
    EXPECT_NE(find("linkLabel")->property("text").toString().indexOf("kalman.xopp"), -1)
            << find("linkLabel")->property("text").toString().toStdString();
    click(find<QQuickItem>("linkNewTab"));
    until([&] { return currentFile() == "kalman.xopp"; });
    ASSERT_EQ(currentFile(), "kalman.xopp");
    EXPECT_EQ(controller->tabCount(), 2);
    EXPECT_EQ(controller->pageNumber(), 4) << "the chapter's page, not the saved one";

    // Back to the notes, forward to the lecture: across the documents
    ASSERT_TRUE(controller->canGoBack());
    controller->navigateBack();
    until([&] { return currentFile() == "a.md"; });
    EXPECT_EQ(currentFile(), "a.md");
    EXPECT_EQ(controller->tabCount(), 2) << "the lecture stays open";
    ASSERT_TRUE(controller->canGoForward());
    controller->navigateForward();
    until([&] { return currentFile() == "kalman.xopp"; });
    EXPECT_EQ(currentFile(), "kalman.xopp");
    EXPECT_EQ(controller->pageNumber(), 4);

    // Back, then the same link again: the open lecture is switched to, not opened twice
    controller->navigateBack();
    until([&] { return currentFile() == "a.md"; });
    controller->goToPage(0);
    tapText("Kalman");
    until([&] { return popupOpen(); });
    click(find<QQuickItem>("linkNewTab"));
    until([&] { return currentFile() == "kalman.xopp"; });
    EXPECT_EQ(controller->tabCount(), 2);
}

TEST_F(DocumentLinksTest, openHereReplacesTheDocumentAndTheChoiceIsRemembered) {
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "Notes" / "a.md").string())));
    wait(200);
    tapText("Kalman");
    until([&] { return popupOpen(); });
    ASSERT_TRUE(popupOpen());
    click(find<QQuickItem>("linkRemember"));
    click(find<QQuickItem>("linkHere"));
    until([&] { return currentFile() == "kalman.xopp"; });
    EXPECT_EQ(currentFile(), "kalman.xopp");
    EXPECT_EQ(controller->tabCount(), 1) << "the notes had no changes: they went";
    EXPECT_EQ(settings()->get("linkOpening").toString(), "here");

    // Back opens the notes again, in place of the lecture
    ASSERT_TRUE(controller->canGoBack());
    controller->navigateBack();
    until([&] { return currentFile() == "a.md"; });
    EXPECT_EQ(currentFile(), "a.md");
    EXPECT_EQ(controller->tabCount(), 1);

    // Remembered: no question any more
    wait(500);  // (opened again: the canvas takes it over first)
    controller->goToPage(0);
    QSignalSpy tapped(controller.get(), &AppController::linkTapped);
    tapText("Kalman");
    EXPECT_EQ(tapped.count(), 1);
    until([&] { return currentFile() == "kalman.xopp"; });
    EXPECT_EQ(currentFile(), "kalman.xopp");
    EXPECT_FALSE(popupOpen());
}

TEST_F(DocumentLinksTest, wikiLinksHeadingsReferenceAndWhatWasNotFound) {
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "Notes" / "a.md").string())));
    wait(200);
    // [[b#Part two]]: the file of that name next to it, at the heading (on its second page)
    tapText("b#Part two");
    until([&] { return popupOpen(); });
    ASSERT_TRUE(popupOpen());
    click(find<QQuickItem>("linkNewTab"));
    until([&] { return currentFile() == "b.md"; });
    ASSERT_EQ(currentFile(), "b.md");
    EXPECT_GT(controller->pageNumber(), 1) << "at the heading";
    controller->navigateBack();
    until([&] { return currentFile() == "a.md"; });

    // A chapter that is gone: the saved page, and a note says so
    QSignalSpy notes(controller.get(), &AppController::pageActionDone);
    ASSERT_TRUE(controller->followDocumentLink("../Lectures/kalman.xopp#chapter=Gone&page=5", "tab"));
    EXPECT_EQ(currentFile(), "kalman.xopp");
    EXPECT_EQ(controller->pageNumber(), 5);
    ASSERT_FALSE(notes.isEmpty());
    EXPECT_EQ(notes.last().at(0).toString(), "Chapter \"Gone\" not found, opened page 5");

    // As the reference, beside the notes
    controller->navigateBack();
    until([&] { return currentFile() == "a.md"; });
    ASSERT_TRUE(controller->followDocumentLink("../Lectures/kalman.xopp#page=2", "reference"));
    EXPECT_EQ(currentFile(), "a.md");
    EXPECT_TRUE(controller->reference().active());
    EXPECT_EQ(controller->reference().pageNumber(), 2);

    // A file that is not there: said, nothing opens
    QSignalSpy messages(controller.get(), &AppController::message);
    const int tabs = controller->tabCount();
    EXPECT_FALSE(controller->followDocumentLink("../Lectures/nothing.xopp#page=2", "tab"));
    EXPECT_EQ(controller->tabCount(), tabs);
    EXPECT_FALSE(messages.isEmpty());
    // Web addresses are not links to documents
    EXPECT_FALSE(controller->documentLink("https://example.org/a.pdf").value("document").toBool());
    EXPECT_TRUE(controller->documentLink("../Lectures/kalman.xopp#page=2").value("found").toBool());
}
