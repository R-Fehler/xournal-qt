/*
 * xournal-qt: the Annotations panel of the page sidebar in the real window (qt/docs/annotations-md.md): it lists the
 * document's notes by page, a tap goes to the page, it follows an edit (reading only the page that changed), the
 * filter chooses the kinds, and "Export as Markdown" writes the file next to the document.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <functional>
#include <iostream>

#include <QCoreApplication>
#include <QElapsedTimer>
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
#include "model/MarkdownText.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "shell/AnnotationsModel.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/Previews.h"
#include "shell/RecentFiles.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include "AppController.h"

namespace fs = std::filesystem;

namespace {
std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::unique_ptr<Text> textBox(const std::string& content, double x, double y) {
    auto t = std::make_unique<Text>();
    t->setText(content);
    t->setFont(XojFont("Sans", 10));
    t->move(x, y);
    return t;
}

/// notes.xopp, three pages: a text box on page 1, handwriting in the margin of page 2, a Markdown box on page 3.
void makeNotes(const fs::path& file) {
    Document doc(nullptr);
    for (int i = 0; i < 3; ++i) {
        auto page = std::make_shared<XojPage>(595.0, 842.0);
        page->getLayers().push_back(new Layer());  // (the page owns it)
        doc.addPage(page);
    }
    doc.getPage(0)->getLayers()[0]->addElement(textBox("First note", 60, 100));
    for (const auto& [a, b]: {std::pair{QPointF(500, 300), QPointF(540, 310)}, {QPointF(505, 318), QPointF(550, 320)}}) {
        auto s = std::make_unique<Stroke>();
        s->setWidth(1.4);
        s->addPoint(Point(a.x(), a.y()));
        s->addPoint(Point(b.x(), b.y()));
        doc.getPage(1)->getLayers()[0]->addElement(std::move(s));
    }
    auto* md = new Layer();
    md->setName(std::string(xoj::markdown::LAYER_NAME));
    doc.getPage(2)->getLayers().push_back(md);
    md->addElement(textBox("**Later** idea", 60, 500));
    ASSERT_TRUE(xqt::DocumentSession::writeDocument(doc, file).ok);
}

QQuickItem* itemAt(QQuickItem* view, int row) {
    QQuickItem* item = nullptr;
    QMetaObject::invokeMethod(view, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, row));
    return item;
}

class AnnotationsPanelTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        controller = std::make_unique<AppController>();
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->addImageProvider("thumbnail", new xqt::ThumbnailProvider);
        engine->addImageProvider("sketch", new xqt::SketchProvider);
        engine->addImageProvider("preview", new xqt::PreviewProvider);
        engine->addImageProvider("hitpage", new xqt::HitPageProvider);
        engine->addImageProvider("mdsnippet", new xqt::MdSnippetProvider);
        engine->addImageProvider("annotation", new xqt::AnnotationImageProvider);
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
        controller->shutdown();
        engine.reset();
        controller.reset();
    }
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
    void click(QQuickItem* item) {
        ASSERT_NE(item, nullptr);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        wait(50);
    }
    xqt::AnnotationsModel* model() const { return qobject_cast<xqt::AnnotationsModel*>(controller->annotationsModel()); }
    xqt::DocumentSession* current() const { return controller->tabManager().currentSession(); }

    QTemporaryDir tmp;
    fs::path root;
    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
};
}  // namespace

TEST_F(AnnotationsPanelTest, listsJumpsFollowsEditsFiltersAndExports) {
    const fs::path file = root / "notes.xopp";
    makeNotes(file);
    ASSERT_TRUE(controller->openPath(QString::fromStdString(file.string())));
    wait(100);
    window->setProperty("sidebarShown", true);
    wait(50);
    EXPECT_EQ(model()->pagesRead(), 0) << "nothing is read while the panel is not shown";

    click(find<QQuickItem>("sidebarAnnotationsButton"));
    auto* list = find<QQuickItem>("annotationList");
    ASSERT_NE(list, nullptr);
    until([&] { return list->isVisible() && list->property("count").toInt() == 3; });
    ASSERT_EQ(list->property("count").toInt(), 3);
    EXPECT_EQ(model()->pagesRead(), 3);
    const QModelIndex ink = model()->index(1);
    EXPECT_EQ(model()->data(ink, xqt::AnnotationsModel::KindRole).toString(), "ink");
    EXPECT_TRUE(model()->data(ink, xqt::AnnotationsModel::PictureRole).toString().startsWith("image://annotation/"));
    until([&] { return xqt::AnnotationImageProvider::renderCount() > 0; });
    EXPECT_GT(xqt::AnnotationImageProvider::renderCount(), 0) << "its picture is drawn";

    // A tap goes to the item's page
    until([&] { return itemAt(list, 2) != nullptr; });
    QQuickItem* later = itemAt(list, 2);
    ASSERT_NE(later, nullptr);
    EXPECT_EQ(later->property("itemText").toString(), "**Later** idea");
    click(later);
    until([&] { return controller->pageNumber() == 3; });
    EXPECT_EQ(controller->pageNumber(), 3);
    EXPECT_TRUE(controller->canGoBack()) << "Back returns to where the reader was";

    // An edit: the list follows once the writing pauses, reading only the page that changed
    {
        Document& doc = *current()->getDocument();
        doc.lock();
        doc.getPage(1)->getLayers()[0]->addElement(textBox("Added later", 60, 700));
        doc.unlock();
        current()->firePageChanged(1);
    }
    until([&] { return list->property("count").toInt() == 4; });
    ASSERT_EQ(list->property("count").toInt(), 4);
    EXPECT_EQ(model()->pagesRead(), 4) << "the other pages kept their items";
    EXPECT_EQ(model()->data(model()->index(2), xqt::AnnotationsModel::TextRole).toString(), "Added later");

    // The filter: handwriting only
    model()->setShownKinds({"ink"});
    until([&] { return list->property("count").toInt() == 1; });
    EXPECT_EQ(list->property("count").toInt(), 1);
    EXPECT_EQ(model()->total(), 4);
    model()->setShownKinds({"highlight", "text", "markdown", "ink", "link", "note"});
    until([&] { return list->property("count").toInt() == 4; });

    // Export: next to the document (Xournal++ files), without asking
    ASSERT_TRUE(current()->save().ok);
    const fs::path md = root / "notes.annotations.md";
    EXPECT_EQ(controller->annotationsFile().toLocalFile().toStdString(), md.string());
    QSignalSpy exported(controller.get(), &AppController::annotationsExported);
    click(find<QQuickItem>("annotationExport"));
    until([&] { return exported.count() == 1; });
    ASSERT_EQ(exported.count(), 1);
    EXPECT_EQ(exported[0][1].toString(), "") << exported[0][1].toString().toStdString();
    const std::string text = readFile(md);
    EXPECT_NE(text.find("- First note ([p. 1](notes.xopp#page=1&text="), std::string::npos) << text;
    EXPECT_NE(text.find("- Added later ([p. 2](notes.xopp#page=2"), std::string::npos) << text;
    EXPECT_NE(text.find("> **Later** idea"), std::string::npos) << text;
    EXPECT_NE(text.find("(handwriting)"), std::string::npos) << text;

    // Again: the file is there, so it asks before replacing it
    click(find<QQuickItem>("annotationExport"));
    auto* replace = find<QObject>("annotationReplaceDialog");
    ASSERT_NE(replace, nullptr);
    until([&] { return replace->property("visible").toBool(); });
    ASSERT_TRUE(replace->property("visible").toBool());
    EXPECT_EQ(exported.count(), 1) << "not written before the answer";
    auto* replaceButton = find<QQuickItem>("annotationReplace");
    until([&] { return replaceButton && replaceButton->isVisible() && replaceButton->height() > 0; });
    click(replaceButton);
    until([&] { return exported.count() == 2; });
    EXPECT_EQ(exported.count(), 2);
}

TEST_F(AnnotationsPanelTest, aNewDocumentIsSavedFirst) {
    controller->newDocument();
    wait(50);
    EXPECT_TRUE(controller->annotationsFile().isEmpty());
    EXPECT_TRUE(controller->suggestedAnnotationsFile().isEmpty());
    QSignalSpy exported(controller.get(), &AppController::annotationsExported);
    controller->exportAnnotations(QUrl());
    ASSERT_EQ(exported.count(), 1);
    EXPECT_FALSE(exported[0][1].toString().isEmpty()) << "it says why";
}

TEST_F(AnnotationsPanelTest, pdfFilesModeAsksWhereToExport) {
    const fs::path file = root / "notes.xopp";
    makeNotes(file);
    ASSERT_TRUE(controller->openPath(QString::fromStdString(file.string())));
    wait(100);
    EXPECT_FALSE(controller->annotationsFile().isEmpty());
    controller->setDocumentMode("pdf");
    EXPECT_TRUE(controller->annotationsFile().isEmpty()) << "nothing is written next to files in PDF files mode";
    EXPECT_EQ(controller->suggestedAnnotationsFile().toLocalFile().toStdString(), (root / "notes.annotations.md").string());
    controller->setDocumentMode("xopp");  // (the settings outlive the test)
}

// A big document: the pictures of handwriting are drawn for the rows in view only, not for the whole list, and
// scrolling through the list does not wait for them
TEST_F(AnnotationsPanelTest, aHundredPagesDrawOnlyThePicturesInView) {
    const int before = xqt::AnnotationImageProvider::renderCount();  // (of the tests before)
    const fs::path file = root / "many.xopp";
    {
        Document doc(nullptr);
        for (int i = 0; i < 100; ++i) {
            auto page = std::make_shared<XojPage>(595.0, 842.0);
            page->getLayers().push_back(new Layer());  // (the page owns it)
            auto s = std::make_unique<Stroke>();
            s->setWidth(1.4);
            s->addPoint(Point(500, 300));
            s->addPoint(Point(540, 310));
            s->addPoint(Point(560, 300));
            page->getLayers()[0]->addElement(std::move(s));
            doc.addPage(page);
        }
        ASSERT_TRUE(xqt::DocumentSession::writeDocument(doc, file).ok);
    }
    ASSERT_TRUE(controller->openPath(QString::fromStdString(file.string())));
    wait(100);
    window->setProperty("sidebarShown", true);
    wait(50);
    click(find<QQuickItem>("sidebarAnnotationsButton"));
    auto* list = find<QQuickItem>("annotationList");
    ASSERT_NE(list, nullptr);
    until([&] { return list->isVisible() && list->property("count").toInt() == 100; });
    ASSERT_EQ(list->property("count").toInt(), 100);
    wait(500);  // (the pictures of the first rows)
    const int first = xqt::AnnotationImageProvider::renderCount() - before;

    EXPECT_GT(first, 0);
    EXPECT_LT(first, 30) << "the rows in view";

    // To the end at once (the scroll bar dragged): the rows in between are not drawn
    const double end = list->property("contentHeight").toDouble() - list->height();
    ASSERT_GT(end, 2000) << "a long list";
    list->setProperty("contentY", end);
    wait(1000);
    const int atEnd = xqt::AnnotationImageProvider::renderCount() - before;
    EXPECT_LT(atEnd - first, 30) << "only the rows at the end";

    // Back to the top a step a frame, as a quick flick does it: it does not wait for the pictures, and no row is
    // drawn twice
    QElapsedTimer step;
    qint64 worst = 0;
    for (double y = end; y >= 0; y -= end / 40) {
        step.start();
        list->setProperty("contentY", y);
        wait(16);
        worst = std::max(worst, step.elapsed());
    }
    list->setProperty("contentY", 0.0);
    wait(1500);
    const int after = xqt::AnnotationImageProvider::renderCount() - before;
    EXPECT_LT(worst, 250) << "a step of the scrolling took " << worst << " ms";
    EXPECT_LE(after, 100) << "at most a picture per row, each drawn once";

    // Down again a little: the pictures drawn are kept, not drawn again
    list->setProperty("contentY", 300.0);
    wait(300);
    list->setProperty("contentY", 0.0);
    wait(800);
    EXPECT_EQ(xqt::AnnotationImageProvider::renderCount() - before, after) << "the pictures drawn were kept";
    std::cout << "pictures drawn: " << first << " at first, " << atEnd << " after the jump to the end, " << after
              << " after scrolling back; slowest step " << worst << " ms\n";
}
