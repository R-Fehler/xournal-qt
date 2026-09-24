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
#include "canvas/MarkdownEditor.h"
#include "control/tools/EditSelection.h"
#include "session/DocumentLink.h"
#include "markdown/MdBox.h"
#include "session/DocumentSession.h"
#include "shell/Library.h"
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
        QTest::mouseMove(window, QPoint(-20, -20));  // (the pointer rests outside: nothing hovered, no tool tips)
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
                  "Then [[b#Part two]] and [gone](../Lectures/kalman.xopp#chapter=Gone&page=5).\n\n"
                  "And [lost](../Lectures/lost.xopp#page=1).\n");
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

    // A file that is not there: said (the window offers to locate it), nothing opens
    QSignalSpy messages(controller.get(), &AppController::linkTargetMissing);
    const int tabs = controller->tabCount();
    EXPECT_FALSE(controller->followDocumentLink("../Lectures/nothing.xopp#page=2", "tab"));
    EXPECT_EQ(controller->tabCount(), tabs);
    EXPECT_FALSE(messages.isEmpty());
    // Web addresses are not links to documents
    EXPECT_FALSE(controller->documentLink("https://example.org/a.pdf").value("document").toBool());
    EXPECT_TRUE(controller->documentLink("../Lectures/kalman.xopp#page=2").value("found").toBool());
}

TEST_F(DocumentLinksTest, copiedLinksArePastedAsMarkdownAndAsMarkers) {
    const QString kalman = QString::fromStdString((root / "Lectures" / "kalman.xopp").string());
    ASSERT_TRUE(controller->openPath(kalman));
    wait(200);
    // A page: on the clipboard as the app's link, as Markdown and as HTML
    controller->copyPageLink(2);
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    ASSERT_TRUE(mime->hasFormat(xqt::links::MIME));
    EXPECT_EQ(mime->text(), "[kalman, page 3](" + kalman + "#page=3)");
    EXPECT_NE(mime->html().indexOf("file://"), -1);
    // A chapter
    controller->copyChapterLink(3, "Prediction step");
    EXPECT_EQ(QGuiApplication::clipboard()->text(), "[kalman, Prediction step](" + kalman + "#chapter=Prediction%20step&page=4)");

    // Pasted into a .md being written: relative to it
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "Notes" / "a.md").string())));
    wait(300);
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(view->ensureTextEditor());
    xqt::MarkdownEditor* editor = view->getMarkdownEditor();
    ASSERT_NE(editor, nullptr);
    QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
    wait(50);
    EXPECT_NE(editor->text().find("[kalman, Prediction step](../Lectures/kalman.xopp#chapter=Prediction%20step&page=4)"),
              std::string::npos)
            << editor->text();
    view->endTextEditing();
    controller->undo();

    // A library card
    ASSERT_TRUE(controller->copyDocumentLink(QString::fromStdString((root / "Notes" / "b.md").string())));
    EXPECT_EQ(QGuiApplication::clipboard()->text(),
              "[b](" + QString::fromStdString((root / "Notes" / "b.md").string()) + ")");

    // On a page of notes: a link marker in the Markdown layer, readable as Markdown; undo takes it away
    controller->newDocument();
    ASSERT_TRUE(controller->saveAs(QUrl::fromLocalFile(QString::fromStdString((root / "Notes" / "sketch.xopp").string()))));
    controller->copyDocumentLink(kalman, 3);
    view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_TRUE(controller->pasteElements());
    PageRef page = current()->getDocument()->getPage(0);
    Layer* layer = xqt::md::markdownLayer(page);
    ASSERT_NE(layer, nullptr);
    const Text* marker = xqt::md::boxOf(*layer);
    ASSERT_NE(marker, nullptr);
    EXPECT_EQ(marker->getText(),
              "[\xF0\x9F\x94\x97 kalman, page 4](../Lectures/kalman.xopp#page=4&text=prediction%20step)")
            << "a page without a PDF page: the page's first words";
    EXPECT_LT(marker->getWrap(), 250) << "as wide as its text";

    // A tap on it asks where to open the lecture
    controller->selectTool("hand");
    const auto r = xqt::md::boxRect(*marker);
    view->getViewController().scrollToPageRect(0, QRectF(r.x, r.y, r.width, r.height));
    wait(100);
    const QPointF at = view->pageViewRect(0).topLeft() +
                       QPointF(r.x + r.width / 2, r.y + r.height / 2) * view->getViewController().zoom();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, canvasItem->mapToScene(at).toPoint());
    until([&] { return popupOpen(); });
    ASSERT_TRUE(popupOpen());
    EXPECT_NE(find("linkLabel")->property("text").toString().indexOf("kalman.xopp"), -1);
    QMetaObject::invokeMethod(find("linkPopup"), "close");
    wait(100);
    controller->undo();
    EXPECT_EQ(xqt::md::boxOf(*layer), nullptr) << "undone";

    // With something selected: the marker goes next to it
    ASSERT_TRUE(view->pasteText("A sketch", QPointF(view->pageViewRect(0).topLeft() + QPointF(60, 60))));
    controller->selectTool("selectRect");
    view->selectAllOnPage();
    ASSERT_NE(view->getSelection(), nullptr);
    const auto selected = view->getSelection()->getRect();
    ASSERT_TRUE(controller->pasteElements());
    const Text* next = xqt::md::boxOf(*layer);
    ASSERT_NE(next, nullptr);
    EXPECT_NEAR(next->getTransformation().shift.x, selected.x + selected.width + 4, 1);
    EXPECT_NEAR(next->getTransformation().shift.y, selected.y, 1);
}

namespace {
std::string readText(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
}  // namespace

TEST_F(DocumentLinksTest, renamingInTheLibraryUpdatesTheLinksAndBacklinksAreListed) {
    auto* library = qobject_cast<xqt::LibraryModel*>(controller->libraryModel());
    const fs::path kalman = root / "Lectures" / "kalman.xopp";
    // A sketch with a link marker to the lecture (closed), and the note with links to it (open)
    controller->newDocument();
    ASSERT_TRUE(controller->saveAs(QUrl::fromLocalFile(QString::fromStdString((root / "Notes" / "sketch.xopp").string()))));
    ASSERT_TRUE(controller->copyDocumentLink(QString::fromStdString(kalman.string()), 1));
    ASSERT_TRUE(controller->pasteElements());
    ASSERT_TRUE(controller->save());
    controller->closeTab(controller->currentTab());
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "Notes" / "a.md").string())));
    library->refresh();
    library->searchIndex()->waitForDone();

    // Linked from
    ASSERT_TRUE(controller->openPath(QString::fromStdString(kalman.string())));
    const QVariantList from = controller->backlinks();
    ASSERT_EQ(from.size(), 2);
    QStringList names;
    for (const QVariant& v: from) {
        names << v.toMap().value("name").toString();
    }
    names.sort();
    EXPECT_EQ(names, (QStringList{"a", "sketch"}));

    // Renamed in the library: the open note is changed through itself (undo), the closed sketch in the background
    QSignalSpy notes(controller.get(), &AppController::pageActionDone);
    library->setFolder("Lectures");
    const int row = library->rowOf(QString::fromStdString(kalman.string()));
    ASSERT_GE(row, 0);
    ASSERT_TRUE(library->rename(row, "Kalman filter"));
    until([&] {
        auto loaded = xqt::DocumentSession::loadFile(root / "Notes" / "sketch.xopp");
        Layer* layer = loaded.document ? xqt::md::markdownLayer(loaded.document->getPage(0)) : nullptr;
        return layer && xqt::md::boxOf(*layer) &&
               xqt::md::boxOf(*layer)->getText().find("Kalman%20filter.xopp#page=2") != std::string::npos;
    }, 5000);
    auto sketch = xqt::DocumentSession::loadFile(root / "Notes" / "sketch.xopp");
    ASSERT_TRUE(sketch.document);
    Layer* layer = xqt::md::markdownLayer(sketch.document->getPage(0));
    ASSERT_NE(layer, nullptr);
    EXPECT_EQ(xqt::md::boxOf(*layer)->getText(),
              "[\xF0\x9F\x94\x97 kalman, page 2](../Lectures/Kalman%20filter.xopp#page=2)");
    until([&] { return readText(root / "Notes" / "a.md").find("Kalman%20filter.xopp") != std::string::npos; }, 3000);
    const std::string note = readText(root / "Notes" / "a.md");
    EXPECT_NE(note.find("[Kalman](../Lectures/Kalman%20filter.xopp#chapter=Prediction%20step&page=2)"), std::string::npos)
            << note;
    EXPECT_NE(note.find("[gone](../Lectures/Kalman%20filter.xopp#chapter=Gone&page=5)"), std::string::npos) << note;
    EXPECT_NE(note.find("[[b#Part two]]"), std::string::npos) << "the rest as it was";
    until([&] {
        for (const auto& args: notes) {
            if (args.at(0).toString().startsWith("Updated")) {
                return true;
            }
        }
        return false;
    }, 3000);
    bool said = false;
    for (const auto& args: notes) {
        said = said || args.at(0).toString().startsWith("Updated ");
    }
    EXPECT_TRUE(said) << "a note says so";
}

TEST_F(DocumentLinksTest, aLinkWhoseFileWasMovedElsewhereIsFoundOrLocated) {
    auto* library = qobject_cast<xqt::LibraryModel*>(controller->libraryModel());
    // Moved by another program
    fs::create_directories(root / "Archive");
    fs::rename(root / "Lectures" / "kalman.xopp", root / "Archive" / "kalman.xopp");
    library->refresh();
    library->searchIndex()->waitForDone();
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "Notes" / "a.md").string())));
    wait(200);
    QSignalSpy found(controller.get(), &AppController::linkTargetFound);
    ASSERT_TRUE(controller->followDocumentLink("../Lectures/kalman.xopp#chapter=Prediction%20step&page=2", "tab"));
    EXPECT_EQ(currentFile(), "kalman.xopp");
    EXPECT_EQ(controller->pageNumber(), 4);
    ASSERT_EQ(found.count(), 1);
    EXPECT_EQ(found.first().at(1).toString(), "Archive");
    until([&] { return find("linkFoundDialog")->property("opened").toBool(); });
    EXPECT_TRUE(find("linkFoundDialog")->property("opened").toBool());
    QMetaObject::invokeMethod(find("linkFoundDialog"), "close");
    ASSERT_TRUE(controller->updateFoundLink());
    xqt::DocumentSession* note = controller->tabManager().session(0);
    EXPECT_NE(note->currentText().find("[Kalman](../Archive/kalman.xopp#chapter=Prediction%20step&page=2)"),
              std::string::npos)
            << note->currentText();

    // Nothing like it: the reader locates it
    controller->setCurrentTab(0);
    QSignalSpy missing(controller.get(), &AppController::linkTargetMissing);
    EXPECT_FALSE(controller->followDocumentLink("../Lectures/lost.xopp#page=1", "tab"));
    ASSERT_EQ(missing.count(), 1);
    EXPECT_EQ(missing.first().at(0).toString(), "lost.xopp");
    until([&] { return find("linkMissingDialog")->property("opened").toBool(); });
    EXPECT_TRUE(find("linkMissingDialog")->property("opened").toBool());
    QMetaObject::invokeMethod(find("linkMissingDialog"), "close");
    ASSERT_TRUE(controller->relinkTo(QUrl::fromLocalFile(QString::fromStdString((root / "Archive" / "kalman.xopp").string()))));
    EXPECT_EQ(currentFile(), "kalman.xopp") << "followed";
    EXPECT_NE(note->currentText().find("[lost](../Archive/kalman.xopp#page=1)"), std::string::npos) << note->currentText();
}
