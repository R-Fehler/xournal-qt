/*
 * xournal-qt: the To-dos tab of the library home in the real window (qt/docs/todos.md): the to-dos of the library's
 * documents grouped by document with their counts, the filters (state, due date, text, the setting "Collect to-dos
 * from"), a tap that opens the document at the to-do's page, and the check box that ticks the to-do in its file (a
 * Markdown file that is not open, written in the background) or in the open document (one undo step there).
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <shared_mutex>

#include <QCoreApplication>
#include <QDate>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "shell/HitPages.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"
#include "shell/LibraryTodos.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/Previews.h"
#include "shell/RecentFiles.h"
#include "shell/SystemApps.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"
#include "undo/UndoRedoHandler.h"

#include "AppController.h"
#include "CanvasView.h"
#include "MdBox.h"
#include "MdTasks.h"

namespace fs = std::filesystem;

namespace {
std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void writeFile(const fs::path& p, const std::string& bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << bytes;
}

/// A .xopp whose pages each have one Markdown box with this text
void writeNotes(const fs::path& file, const std::vector<std::string>& pages) {
    fs::create_directories(file.parent_path());
    Document doc(nullptr);
    for (const std::string& markdown: pages) {
        auto page = std::make_shared<XojPage>(595.0, 842.0);
        auto* layer = new Layer();
        layer->setName(std::string(xoj::markdown::LAYER_NAME));
        page->getLayers().push_back(layer);  // (the page owns it)
        page->getLayers().push_back(new Layer());
        auto text = std::make_unique<Text>();
        text->setText(markdown);
        text->setFont(XojFont("Sans", 11));
        text->setWrap(400);
        text->move(60, 300);
        layer->addElement(std::move(text));
        doc.addPage(page);
    }
    ASSERT_TRUE(xqt::DocumentSession::writeDocument(doc, file).ok);
}

/// The texts of a document's Markdown boxes, joined
std::string boxesOf(Document& doc) {
    std::shared_lock lock(doc);
    std::string all;
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        for (const Layer* l: doc.getPage(i)->getLayers()) {
            for (const Element* e: l->getElementsView()) {
                if (e->getType() == ELEMENT_TEXT) {
                    all += static_cast<const Text*>(e)->getText();
                }
            }
        }
    }
    return all;
}

class TodosUiTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString()) / "Library";
        const QString tomorrow = QDate::currentDate().addDays(1).toString(Qt::ISODate);
        writeNotes(root / "lecture.xopp",
                   {"# Lab\n\n- [ ] todo: call the lab due:" + tomorrow.toStdString() +
                            "\n- [ ] a shopping item\n- [x] todo: done one\n",
                    "- [ ] todo: write the report\n"});
        writeFile(root / "Sub" / "plan.md", "# Plan\n\n- [ ] TODO: buy milk\n");
        controller = std::make_unique<AppController>();
        controller->setLibraryRoot(root);
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        QMetaObject::invokeMethod(controller->settingsModel(), "set", Q_ARG(QString, "todoSource"),
                                  Q_ARG(QVariant, "marked"));
        QMetaObject::invokeMethod(controller->settingsModel(), "set", Q_ARG(QString, "todoMarker"),
                                  Q_ARG(QVariant, "todo:"));
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
        window->resize(1400, 900);
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        QTest::mouseMove(window, QPoint(-20, -20));
        library = qobject_cast<xqt::LibraryModel*>(controller->libraryModel());
        todos = qobject_cast<xqt::LibraryTodosModel*>(controller->libraryTodosModel());
        ASSERT_NE(todos, nullptr);
        until([&] { return !library->indexing() && library->searchIndex()->todos().size() == 5; });
        ASSERT_EQ(library->searchIndex()->todos().size(), 5u);
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
    void walk(QQuickItem* i, const std::function<void(QQuickItem*)>& f) const {
        f(i);
        for (QQuickItem* c: i->childItems()) {
            walk(c, f);
        }
    }
    QQuickItem* findItem(const QString& name) const {
        QQuickItem* found = nullptr;
        walk(window->contentItem(), [&](QQuickItem* i) {
            if (!found && i->objectName() == name && i->isVisible()) {
                found = i;
            }
        });
        return found;
    }
    void click(QQuickItem* item) {
        ASSERT_NE(item, nullptr);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        wait(50);
    }
    /// The row of the list showing this text (nullptr: none)
    QQuickItem* rowWith(const QString& text) const {
        QQuickItem* found = nullptr;
        walk(window->contentItem(), [&](QQuickItem* i) {
            if (!found && i->objectName() == "todoRow" && i->isVisible()) {
                walk(i, [&](QQuickItem* c) {
                    if (c->objectName() == "todoText" && c->property("text").toString() == text) {
                        found = i;
                    }
                });
            }
        });
        return found;
    }
    QQuickItem* childNamed(QQuickItem* in, const QString& name) const {
        QQuickItem* found = nullptr;
        walk(in, [&](QQuickItem* c) {
            if (!found && c->objectName() == name) {
                found = c;
            }
        });
        return found;
    }
    std::vector<std::string> shownTexts() const {
        std::vector<std::string> out;
        for (int i = 0; i < todos->count(); ++i) {
            out.push_back(todos->data(todos->index(i), xqt::LibraryTodosModel::TextRole).toString().toStdString());
        }
        return out;
    }
    xqt::DocumentSession* current() const { return controller->tabManager().currentSession(); }
    xqt::CanvasView* view() const { return controller->tabManager().currentView(); }
    /// Window coordinates of a point of page 1 (points)
    QPoint onPage(double x, double y) const {
        QQuickItem* canvas = window->findChild<QQuickItem*>("canvas");
        const QPointF at = view()->pageViewRect(0).topLeft() + QPointF(x, y) * view()->getViewController().zoom();
        return canvas->mapToScene(at).toPoint();
    }
    static QObject* entryOf(QObject* menu, const char* name) {
        const int n = menu ? menu->property("count").toInt() : 0;
        for (int i = 0; i < n; ++i) {
            QQuickItem* it = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, it), Q_ARG(int, i));
            if (it && it->objectName() == name) {
                return it;
            }
        }
        return nullptr;
    }
    void showTodos() {
        click(findItem("todosPageButton"));
        until([&] { return findItem("todosList") != nullptr; });
        ASSERT_NE(findItem("todosList"), nullptr);
    }

    QTemporaryDir tmp;
    fs::path root;
    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
    xqt::LibraryModel* library = nullptr;
    xqt::LibraryTodosModel* todos = nullptr;
};
}  // namespace

// The tab lists the marked to-dos grouped by document with their counts (soonest due first); the filters and the
// setting change what it lists
TEST_F(TodosUiTest, theToDosTabListsGroupsAndFilters) {
    showTodos();
    auto* list = findItem("todosList");
    until([&] { return list->property("count").toInt() == 3; });
    ASSERT_EQ(list->property("count").toInt(), 3) << "the open, marked ones";
    EXPECT_EQ(shownTexts(), (std::vector<std::string>{"call the lab", "write the report", "buy milk"}))
            << "the one due first; the marker is not shown";
    const QVariantMap lecture = todos->groupOf(QString::fromStdString((root / "lecture.xopp").string()));
    EXPECT_EQ(lecture.value("label").toString(), "lecture");
    EXPECT_EQ(lecture.value("count").toInt(), 2);
    until([&] { return rowWith("call the lab") != nullptr; });
    QQuickItem* row = rowWith("call the lab");
    ASSERT_NE(row, nullptr);
    auto* due = childNamed(row, "todoDue");
    ASSERT_NE(due, nullptr);
    EXPECT_TRUE(due->isVisible());
    EXPECT_EQ(due->property("text").toString(), "Tomorrow");
    EXPECT_NE(findItem("todoGroupCount"), nullptr);

    // State, due date and text
    todos->setStatus("all");
    EXPECT_EQ(todos->count(), 4);
    EXPECT_EQ(shownTexts(), (std::vector<std::string>{"call the lab", "write the report", "done one", "buy milk"}))
            << "done ones last in their group";
    todos->setStatus("open");
    todos->setDue("none");
    EXPECT_EQ(shownTexts(), (std::vector<std::string>{"write the report", "buy milk"}));
    todos->setDue("any");
    QQuickItem* field = findItem("todosQueryField");
    ASSERT_NE(field, nullptr);
    click(field);
    for (char c: std::string("MILK")) {
        QTest::keyClick(window, c, Qt::ShiftModifier);
    }
    until([&] { return todos->count() == 1; });
    EXPECT_EQ(shownTexts(), (std::vector<std::string>{"buy milk"}));
    field->setProperty("text", "");
    todos->setQuery("");
    // Grouped by folder: the library's top and "Sub"
    todos->setGrouping("folder");
    EXPECT_EQ(todos->groupOf("").value("count").toInt(), 2);
    EXPECT_EQ(todos->groupOf("Sub").value("count").toInt(), 1);
    todos->setGrouping("document");

    // Every check box (the setting)
    QMetaObject::invokeMethod(controller->settingsModel(), "set", Q_ARG(QString, "todoSource"), Q_ARG(QVariant, "all"));
    until([&] { return todos->count() == 4; });
    EXPECT_EQ(todos->count(), 4);
    EXPECT_TRUE(todos->collectAll());
    QMetaObject::invokeMethod(controller->settingsModel(), "set", Q_ARG(QString, "todoSource"),
                              Q_ARG(QVariant, "marked"));
    until([&] { return todos->count() == 3; });
    EXPECT_EQ(todos->count(), 3);
}

// The check box of a to-do of a Markdown file that is not open: written into the file (only its mark), and the list
// follows
TEST_F(TodosUiTest, tickingAToDoOfAClosedFileWritesTheFile) {
    showTodos();
    until([&] { return rowWith("buy milk") != nullptr; });
    QQuickItem* row = rowWith("buy milk");
    ASSERT_NE(row, nullptr);
    click(childNamed(row, "todoCheck"));
    until([&] { return readFile(root / "Sub" / "plan.md") == "# Plan\n\n- [x] TODO: buy milk\n"; });
    EXPECT_EQ(readFile(root / "Sub" / "plan.md"), "# Plan\n\n- [x] TODO: buy milk\n");
    EXPECT_EQ(controller->tabCount(), 0) << "not opened";
    until([&] { return todos->count() == 2; });
    EXPECT_EQ(shownTexts(), (std::vector<std::string>{"call the lab", "write the report"}));
    // And a .xopp that is not open: loaded, changed and saved in the background
    until([&] { return rowWith("write the report") != nullptr; });
    click(childNamed(rowWith("write the report"), "todoCheck"));
    until([&] { return readFile(root / "lecture.xopp") != std::string() && todos->count() == 1; }, 10000);
    EXPECT_EQ(todos->count(), 1);
    auto loaded = xqt::DocumentSession::loadFile(root / "lecture.xopp");
    ASSERT_TRUE(loaded.document);
    EXPECT_NE(boxesOf(*loaded.document).find("- [x] todo: write the report"), std::string::npos);
    EXPECT_EQ(controller->tabCount(), 0);
}

// In an open document the to-do is ticked there, as one undo step; a tap on a row opens the document at its page
TEST_F(TodosUiTest, tickingAToDoOfAnOpenDocumentIsOneUndoStep) {
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "lecture.xopp").string())));
    xqt::DocumentSession* s = controller->tabManager().currentSession();
    ASSERT_NE(s, nullptr);
    controller->setHomeVisible(true);
    showTodos();
    until([&] { return rowWith("write the report") != nullptr; });
    click(childNamed(rowWith("write the report"), "todoCheck"));
    EXPECT_NE(boxesOf(*s->getDocument()).find("- [x] todo: write the report"), std::string::npos);
    until([&] { return todos->count() == 2; });
    EXPECT_EQ(todos->count(), 2) << "the list follows (saved: it had no other changes)";
    ASSERT_TRUE(s->getUndoRedoHandler()->canUndo());
    s->getUndoRedoHandler()->undo();
    EXPECT_NE(boxesOf(*s->getDocument()).find("- [ ] todo: write the report"), std::string::npos);

    // A tap on a row: the document at the to-do's page
    controller->jumpToPage(0);
    todos->setStatus("all");
    until([&] { return rowWith("write the report") != nullptr; });
    click(rowWith("write the report"));
    until([&] { return !controller->homeVisible() && controller->pageNumber() == 2; });
    EXPECT_FALSE(controller->homeVisible());
    EXPECT_EQ(controller->pageNumber(), 2);
}

// On a phone the filters scroll sideways in one row above the list, and the rows are tall enough for a finger
TEST_F(TodosUiTest, onAPhoneTheListFitsTheWidth) {
    window->resize(400, 800);
    wait(200);
    showTodos();
    auto* view = findItem("todosView");
    ASSERT_NE(view, nullptr);
    until([&] { return view->property("phone").toBool(); });
    EXPECT_TRUE(view->property("phone").toBool());
    until([&] { return rowWith("buy milk") != nullptr; });
    QQuickItem* row = rowWith("buy milk");
    ASSERT_NE(row, nullptr);
    EXPECT_GE(row->height(), 48);
    EXPECT_LE(row->mapToScene(QPointF(row->width(), 0)).x(), window->width()) << "no wider than the window";
    auto* field = findItem("todosQueryField");
    ASSERT_NE(field, nullptr);
    EXPECT_GT(field->width(), 100);
}
