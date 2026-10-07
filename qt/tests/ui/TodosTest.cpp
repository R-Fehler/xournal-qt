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
#include "shell/LibraryIndex.h"
#include "shell/LibraryModel.h"
#include "shell/LibraryTodos.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/DocumentCovers.h"
#include "shell/RecentFiles.h"
#include "shell/SystemApps.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"
#include "undo/UndoRedoHandler.h"

#include "AppController.h"
#include "UiFixture.h"
#include "CanvasView.h"
#include "MdBox.h"
#include "MdTasks.h"
#include "support/TestSupport.h"

using xqt::test::readFile;
using xqt::test::writeFile;

namespace fs = std::filesystem;

namespace {

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

class TodosUiTest: public xqt::test::UiFixture {
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
        makeController();
        controller->setLibraryRoot(root);
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        QMetaObject::invokeMethod(controller->settingsModel(), "set", Q_ARG(QString, "todoSource"),
                                  Q_ARG(QVariant, "marked"));
        QMetaObject::invokeMethod(controller->settingsModel(), "set", Q_ARG(QString, "todoMarker"),
                                  Q_ARG(QVariant, "todo:"));
        ASSERT_NO_FATAL_FAILURE(loadWindow({.size = QSize(1400, 900)}));
        library = qobject_cast<xqt::LibraryModel*>(controller->libraryModel());
        todos = qobject_cast<xqt::LibraryTodosModel*>(controller->libraryTodosModel());
        ASSERT_NE(todos, nullptr);
        until([&] { return !library->indexing() && library->searchIndex()->todos().size() == 5; });
        ASSERT_EQ(library->searchIndex()->todos().size(), 5u);
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
    void showTodos() {
        click(findItem("todosPageButton"));
        until([&] { return findItem("todosList") != nullptr; });
        ASSERT_NE(findItem("todosList"), nullptr);
    }

    QTemporaryDir tmp;
    fs::path root;
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

namespace {
/// Box texts of page 1 of a document
std::vector<const Text*> boxesOnPage(Document& doc, size_t page) {
    std::vector<const Text*> out;
    for (const Layer* l: doc.getPage(page)->getLayers()) {
        for (const Element* e: l->getElementsView()) {
            if (e->getType() == ELEMENT_TEXT && static_cast<const Text*>(e)->isMarkdown()) {
                out.push_back(static_cast<const Text*>(e));
            }
        }
    }
    return out;
}
}  // namespace

// The check-box stamp (the image button's list): the next tap puts a tiny Markdown box "- [ ] " with its check box where
// the tap was, then the pen is back. The To-dos tab lists it whatever the marker, with the handwriting beside it as a
// picture; ticking it there ticks the box in the document
TEST_F(TodosUiTest, theCheckBoxStampForHandwrittenToDos) {
    controller->newDocument();
    ASSERT_TRUE(controller->saveAs(QUrl::fromLocalFile(QString::fromStdString((root / "ink.xopp").string()))));
    view()->getViewController().scrollToPageRect(0, QRectF(0, 0, 400, 400));
    wait(200);
    controller->selectTool("pen");
    auto* imageMenu = window->findChild<QObject*>("imageMenu");
    ASSERT_NE(imageMenu, nullptr);
    QObject* entry = entryOf(imageMenu, "todoStampItem");
    ASSERT_NE(entry, nullptr) << "in the image button's list";
    QMetaObject::invokeMethod(entry, "triggered");
    until([&] { return controller->todoStampArmed(); });
    ASSERT_TRUE(controller->todoStampArmed());
    EXPECT_EQ(controller->tool(), "hand") << "the tap writes nothing";
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, onPage(100, 200));
    until([&] { return !controller->todoStampArmed(); });
    EXPECT_FALSE(controller->todoStampArmed());
    EXPECT_EQ(controller->tool(), "pen") << "the pen is back to write the to-do";
    Document* doc = current()->getDocument();
    {
        std::shared_lock lock(*doc);
        const auto boxes = boxesOnPage(*doc, 0);
        ASSERT_EQ(boxes.size(), 1u);
        EXPECT_EQ(boxes[0]->getText(), std::string(xqt::md::tasks::STAMP));
        const auto box = xqt::md::checkBoxRect(*boxes[0], 3);
        ASSERT_TRUE(box);
        EXPECT_NEAR(box->x + box->width / 2, 100, 2) << "its check box where the tap was";
        EXPECT_NEAR(box->y + box->height / 2, 200, 2);
    }
    EXPECT_TRUE(current()->getUndoRedoHandler()->canUndo());
    // The to-do written beside it (a stroke), saved
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(2);
    stroke->setColor(Colors::black);
    for (int x = 120; x <= 260; x += 10) {
        stroke->addPoint(Point(x, 195 + (x / 10) % 2 * 8, -1));
    }
    {
        std::unique_lock lock(*doc);
        doc->getPage(0)->getSelectedLayer()->addElement(std::move(stroke));
    }
    doc->getPage(0)->firePageChanged();
    ASSERT_TRUE(controller->save());
    library->refresh();
    until([&] { return !library->indexing() && library->searchIndex()->todos().size() == 6; });

    controller->setHomeVisible(true);
    showTodos();
    until([&] { return todos->count() == 4; });
    ASSERT_EQ(todos->count(), 4) << "the stamp is listed although it has no marker";
    int row = -1;
    for (int i = 0; i < todos->count(); ++i) {
        if (todos->data(todos->index(i), xqt::LibraryTodosModel::StampRole).toBool()) {
            row = i;
        }
    }
    ASSERT_GE(row, 0);
    const QString picture = todos->data(todos->index(row), xqt::LibraryTodosModel::PictureRole).toString();
    EXPECT_TRUE(picture.startsWith("image://hitpage/")) << picture.toStdString();
    EXPECT_TRUE(picture.contains("/0/area/"));
    until([&] { return findItem("todoInk") != nullptr; });
    EXPECT_NE(findItem("todoInk"), nullptr);
    // The picture: the handwriting beside the check box
    xqt::LibraryIndex::Todo stamp;
    for (const auto& t: library->searchIndex()->todos()) {
        if (t.stamp) {
            stamp = t;
        }
    }
    const QRectF area = xqt::LibraryTodosModel::stampArea(stamp);
    EXPECT_TRUE(area.contains(QPointF(200, 200)));
    const QImage img = xqt::HitPageProvider::renderArea(root / "ink.xopp", 0, area, 400);
    ASSERT_FALSE(img.isNull());
    int dark = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = img.width() / 4; x < img.width(); ++x) {
            dark += qGray(img.pixel(x, y)) < 100 ? 1 : 0;
        }
    }
    EXPECT_GT(dark, 20) << "the stroke is in it";

    // Ticked in the list: in the open document, one undo step
    todos->setStatus("all");
    for (int i = 0; i < todos->count(); ++i) {
        if (todos->data(todos->index(i), xqt::LibraryTodosModel::StampRole).toBool()) {
            row = i;
        }
    }
    ASSERT_TRUE(controller->setTodoDone(todos->data(todos->index(row), xqt::LibraryTodosModel::PathRole).toString(),
                                        "", 0, true));
    std::shared_lock lock(*doc);
    EXPECT_EQ(boxesOnPage(*doc, 0)[0]->getText(), "- [x] ");
}

namespace {
class FakeApps final: public xqt::SystemApps {
public:
    bool openWithSystemApp(const QString& path) override {
        opened << path;
        return true;
    }
    QStringList opened;
};
}  // namespace

// "Add to calendar": an .ics of an all-day event on the due date handed to the system's app (on the desktop); the
// open to-dos exported as .ics and as Markdown
TEST_F(TodosUiTest, toTheCalendarOneWay) {
    FakeApps apps;
    xqt::SystemApps::setInstance(&apps);
    showTodos();
    until([&] { return todos->count() == 3; });
    ASSERT_EQ(todos->data(todos->index(0), xqt::LibraryTodosModel::TextRole).toString(), "call the lab");
    EXPECT_TRUE(controller->addTodoToCalendar(todos->get(0)));
    ASSERT_EQ(apps.opened.size(), 1);
    EXPECT_TRUE(apps.opened[0].endsWith(".ics"));
    {
        QFile f(apps.opened[0]);
        ASSERT_TRUE(f.open(QIODevice::ReadOnly));
        const QByteArray ics = f.readAll();
        EXPECT_TRUE(ics.contains("DTSTART;VALUE=DATE:" +
                                 QDate::currentDate().addDays(1).toString("yyyyMMdd").toLatin1()));
        EXPECT_TRUE(ics.contains("SUMMARY:call the lab"));
    }
    EXPECT_FALSE(controller->addTodoToCalendar(todos->get(1))) << "no due date";
    EXPECT_EQ(apps.opened.size(), 1);
    // Exports of the open to-dos listed
    const fs::path out = fs::path(tmp.path().toStdString()) / "out";
    fs::create_directories(out);
    ASSERT_TRUE(controller->exportTodos(QUrl::fromLocalFile(QString::fromStdString((out / "todos.ics").string()))));
    EXPECT_EQ(QString::fromStdString(readFile(out / "todos.ics")).count("BEGIN:VEVENT"), 1) << "the one with a date";
    ASSERT_TRUE(controller->exportTodos(QUrl::fromLocalFile(QString::fromStdString((out / "todos").string()))));
    const std::string md = readFile(out / "todos.md");
    EXPECT_NE(md.find("- [ ] write the report"), std::string::npos);
    EXPECT_NE(md.find("- [ ] buy milk"), std::string::npos);
    EXPECT_EQ(md.find("done one"), std::string::npos) << "open ones only";
    EXPECT_NE(findItem("todosMoreButton"), nullptr);
    xqt::SystemApps::setInstance(nullptr);
}
