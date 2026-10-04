/*
 * xournal-qt: to-dos (qt/docs/todos.md): the task lines of Markdown (boxes, sticky notes' texts, Markdown files, PDF
 * text documents) read into the library index's "notes" pack with their page, box, line, due date and whether they
 * are done; which of them are to-dos (the marker, or every check box; stamps always).
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>
#include <fstream>
#include <iterator>
#include <memory>

#include <QCborMap>
#include <QDate>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/StickyNote.h"
#include "shell/DocumentFiles.h"
#include "shell/Library.h"
#include "shell/LibraryCache.h"
#include "shell/LibraryModel.h"
#include "shell/LibraryTodos.h"
#include "shell/Todos.h"

#include "MarkdownFile.h"
#include "MdTasks.h"

using namespace xqt;
namespace tasks = xqt::md::tasks;

namespace {
void writeFile(const fs::path& p, const std::string& bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << bytes;
}

std::unique_ptr<Text> box(const std::string& markdown, double x = 40, double y = 60) {
    auto text = std::make_unique<Text>();
    text->setText(markdown);
    text->setFont(XojFont("Sans", 11));
    text->setWrap(300);
    text->move(x - text->getOrigin().x, y - text->getOrigin().y);
    return text;
}

/// A page with a Markdown layer holding these boxes
PageRef pageWith(std::vector<std::unique_ptr<Text>> boxes) {
    auto page = std::make_shared<XojPage>(595.0, 842.0);
    auto* layer = new Layer();
    layer->setName(std::string(xoj::markdown::LAYER_NAME));
    page->getLayers().push_back(layer);  // (the page owns it)
    page->getLayers().push_back(new Layer());
    for (auto& b: boxes) {
        layer->addElement(std::move(b));
    }
    return page;
}

void write(Document& doc, const fs::path& file) {
    fs::create_directories(file.parent_path());
    ASSERT_TRUE(DocumentSession::writeDocument(doc, file).ok);
}

std::vector<std::string> texts(const std::vector<LibraryIndex::Todo>& todos) {
    std::vector<std::string> out;
    for (const auto& t: todos) {
        out.push_back(t.text.toStdString());
    }
    return out;
}
}  // namespace

// The task lines of a text: bullets and numbers, open and done, nested, CRLF; none in code or in a plain text
TEST(Todos, theTaskLinesOfAMarkdownText) {
    const std::string text = "# List\r\n"
                             "- [ ] todo: call the lab\r\n"
                             "* [x] Done: send it\r\n"
                             "1. [ ] numbered\r\n"
                             "   - [X] nested\r\n"
                             "+ [ ]\r\n"
                             "- not a task [ ]\r\n"
                             "```\r\n- [ ] in code\r\n```\r\n";
    const auto found = tasks::find(text);
    ASSERT_EQ(found.size(), 5u);
    EXPECT_EQ(found[0].text, "todo: call the lab");
    EXPECT_FALSE(found[0].done);
    EXPECT_EQ(found[0].line, 1);
    EXPECT_EQ(text.substr(found[0].lineBegin, found[0].lineEnd - found[0].lineBegin), "- [ ] todo: call the lab\r");
    EXPECT_EQ(text[found[0].mark], ' ');
    EXPECT_EQ(found[1].text, "Done: send it");
    EXPECT_TRUE(found[1].done);
    EXPECT_EQ(found[2].text, "numbered");
    EXPECT_EQ(found[3].text, "nested");
    EXPECT_TRUE(found[3].done);
    EXPECT_EQ(found[3].line, 4);
    EXPECT_EQ(found[4].text, "");
    EXPECT_TRUE(tasks::find("<!-- xqt:plain -->\n- [ ] a line of a plain text\n").empty());
    EXPECT_TRUE(tasks::find("No tasks here.").empty());

    // Set done or open: only the mark changes
    const std::string done = tasks::withTask(text, found[0].mark, true);
    EXPECT_EQ(done.size(), text.size());
    EXPECT_EQ(tasks::find(done)[0].done, true);
    EXPECT_EQ(tasks::withTask(done, found[0].mark, false), text);
    EXPECT_EQ(tasks::withTask(text, 0, true), text) << "not a mark: unchanged";
}

// Due dates: Obsidian Tasks' "📅 2026-10-12" and "due:2026-10-12"; the text without them
TEST(Todos, dueDates) {
    EXPECT_EQ(tasks::dueDate("todo: hand in \xF0\x9F\x93\x85 2026-10-12"), "2026-10-12");
    EXPECT_EQ(tasks::dueDate("\xF0\x9F\x93\x85\xEF\xB8\x8F" "2026-01-31 todo: pay"), "2026-01-31");
    EXPECT_EQ(tasks::dueDate("todo: pay due:2026-10-12"), "2026-10-12");
    EXPECT_EQ(tasks::dueDate("todo: pay Due: 2026-10-12!"), "2026-10-12");
    EXPECT_EQ(tasks::dueDate("todo: overdue:2026-10-12"), "") << "a word ending in due";
    EXPECT_EQ(tasks::dueDate("todo: due:2026-13-01"), "") << "no month 13";
    EXPECT_EQ(tasks::dueDate("todo: due:2026-10-123"), "");
    EXPECT_EQ(tasks::dueDate("todo: nothing due"), "");
    EXPECT_EQ(tasks::withoutDueDate("todo: hand in \xF0\x9F\x93\x85 2026-10-12 to Anna"), "todo: hand in to Anna");
    EXPECT_EQ(tasks::withoutDueDate("due:2026-10-12 pay"), "pay");
}

// Stamps: a box that is one task without text
TEST(Todos, stamps) {
    EXPECT_TRUE(tasks::isStamp(std::string(tasks::STAMP)));
    EXPECT_TRUE(tasks::isStamp("- [x]"));
    EXPECT_TRUE(tasks::isStamp("\n* [ ]  \n"));
    EXPECT_FALSE(tasks::isStamp("- [ ] text"));
    EXPECT_FALSE(tasks::isStamp("- [ ]\n- [ ]"));
    EXPECT_FALSE(tasks::isStamp("[ ]"));
    ASSERT_EQ(tasks::find(tasks::STAMP).size(), 1u) << "the stamp's box draws a check box";
}

// Which task lines are to-dos: the marker anywhere (case ignored; not shown), every check box, stamps always
TEST(Todos, theMarkerSettingDecidesWhichLinesAreToDos) {
    LibraryIndex::Todo marked;
    marked.text = "Call the lab TODO: today \xF0\x9F\x93\x85 2026-10-12";
    LibraryIndex::Todo plain;
    plain.text = "an item of a shopping list";
    LibraryIndex::Todo stamp;
    stamp.stamp = true;
    todos::Rules rules;
    EXPECT_EQ(rules.marker, "todo:");
    EXPECT_TRUE(todos::listed(marked, rules));
    EXPECT_FALSE(todos::listed(plain, rules));
    EXPECT_TRUE(todos::listed(stamp, rules));
    rules.marker = "#task";
    EXPECT_FALSE(todos::listed(marked, rules));
    rules.all = true;
    EXPECT_TRUE(todos::listed(plain, rules));
    EXPECT_EQ(todos::shownText(marked.text, "todo:"), "Call the lab today");
    EXPECT_EQ(todos::shownText("todo:   pay  due:2026-10-12", "TODO:"), "pay");
}

// The index reads the task lines of Markdown boxes, sticky notes' texts and Markdown files into "notes": another
// index has them without opening a document; a changed file is read again
TEST(Todos, theIndexReadsTheToDos) {
    QTemporaryDir tmp;
    const fs::path root = fs::path(tmp.path().toStdString()) / "Library";
    sticky::installDrawer();  // (a note's text is a Markdown text)
    {
        Document doc(nullptr);
        std::vector<std::unique_ptr<Text>> boxes;
        boxes.push_back(box("Notes\n\n- [ ] todo: call the lab \xF0\x9F\x93\x85 2026-10-12\n- [x] todo: done one\n"));
        boxes.push_back(box(std::string(tasks::STAMP), 30, 400));
        doc.addPage(pageWith(std::move(boxes)));
        // Page 2: a sticky note with a task in its text, and the same text again in a box
        PageRef p2 = pageWith({});
        p2->getLayers().push_back(new Layer());
        sticky::Look look;
        look.rect = {100, 100, 200, 140};
        Layer* note = sticky::makeNote(look);
        auto text = box("- [ ] todo: call the lab", sticky::textOrigin(look).x, sticky::textOrigin(look).y);
        text->setWrap(sticky::textWidth(look));
        note->addElement(std::move(text));
        p2->getLayers().push_back(note);
        doc.addPage(p2);
        write(doc, root / "lecture.xopp");
    }
    writeFile(root / "Sub" / "plan.md", "# Plan\n\n- [ ] buy milk due:2026-10-05\n- [x] todo: read\n\n```\n- [ ] no\n```\n");
    writeFile(root / "Sub" / "empty.md", "Nothing to do.\n");
    {
        LibraryIndex index(root);
        index.update(DocumentFiles::scanRecursive(root));
        index.waitForDone();
        const auto all = index.todos();
        ASSERT_EQ(all.size(), 6u);
        // (by folder, then by file)
        EXPECT_EQ(all[0].file, root / "lecture.xopp");
        EXPECT_EQ(texts(all), (std::vector<std::string>{"todo: call the lab \xF0\x9F\x93\x85 2026-10-12", "todo: done one",
                                                        "", "todo: call the lab", "buy milk due:2026-10-05",
                                                        "todo: read"}));
        EXPECT_EQ(all[0].page, 0);
        EXPECT_EQ(all[0].box, 0);
        EXPECT_EQ(all[0].line, 2);
        EXPECT_EQ(all[0].due, "2026-10-12");
        EXPECT_FALSE(all[0].done);
        EXPECT_TRUE(all[1].done);
        EXPECT_TRUE(all[2].stamp);
        // (a stamp: where its check box is drawn, near its box's top left)
        EXPECT_NEAR(all[2].x, 30, 10);
        EXPECT_NEAR(all[2].y, 405, 15);
        EXPECT_GT(all[2].size, 4);
        EXPECT_LT(all[2].size, 15);
        EXPECT_DOUBLE_EQ(all[2].pageWidth, 595);
        EXPECT_EQ(all[3].page, 1) << "the sticky note's text";
        EXPECT_EQ(all[3].occurrence, 0) << "a different text (no due date)";
        EXPECT_EQ(all[4].file, root / "Sub" / "plan.md");
        EXPECT_EQ(all[4].page, -1);
        EXPECT_EQ(all[4].line, 2);
        EXPECT_EQ(all[4].due, "2026-10-05");
        EXPECT_TRUE(all[5].done);
        index.flush();
    }
    LibraryIndex again(root);
    const quint64 changes = again.todoChanges();
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_EQ(again.documentsRead(), 0) << "from the packs";
    ASSERT_EQ(again.todos().size(), 6u);
    EXPECT_TRUE(again.todos()[2].stamp);
    EXPECT_EQ(again.todos()[0].due, "2026-10-12");
    EXPECT_NE(again.todoChanges(), changes);
    // The Markdown file changes: read again
    writeFile(root / "Sub" / "plan.md", "- [ ] todo: one\n- [ ] todo: one\n");
    fs::last_write_time(root / "Sub" / "plan.md",
                        fs::last_write_time(root / "Sub" / "plan.md") + std::chrono::seconds(5));
    const quint64 before = again.todoChanges();
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    const auto now = again.todos();
    ASSERT_EQ(now.size(), 6u);
    EXPECT_EQ(now[4].occurrence, 0);
    EXPECT_EQ(now[5].occurrence, 1) << "the same text again";
    EXPECT_NE(again.todoChanges(), before);
}

// Entries of documents indexed before to-dos were have none stored: they are read once more (without their PDF text)
TEST(Todos, entriesFromBeforeAreReadAgainOnce) {
    QTemporaryDir tmp;
    const fs::path root = fs::path(tmp.path().toStdString()) / "Library";
    writeFile(root / "plan.md", "- [ ] todo: one\n");
    {
        LibraryIndex index(root);
        index.update(DocumentFiles::scanRecursive(root));
        index.waitForDone();
        index.flush();
    }
    // The pack as an older build wrote it: no "todos"
    {
        CacheLocation where(root);
        auto notes = Packs::read(where.dirOf(root), LibraryIndex::NOTES_PACK, LibraryIndex::FORMAT);
        ASSERT_TRUE(notes);
        QCborMap entry = notes->value(QStringLiteral("plan.md")).toMap();
        ASSERT_TRUE(entry.contains(QStringLiteral("todos")));
        entry.remove(QStringLiteral("todos"));
        notes->insert(QStringLiteral("plan.md"), entry);
        ASSERT_TRUE(Packs::write(where.dirOf(root), LibraryIndex::NOTES_PACK, LibraryIndex::FORMAT, *notes, true));
    }
    LibraryIndex again(root);
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_EQ(again.documentsRead(), 1);
    ASSERT_EQ(again.todos().size(), 1u);
    again.flush();
    LibraryIndex third(root);
    third.update(DocumentFiles::scanRecursive(root));
    third.waitForDone();
    EXPECT_EQ(third.documentsRead(), 0) << "once";
}

// The To-dos view: grouped by document with counts, sorted by due date (done ones last), filtered by state, due date,
// text and the library's current folder; the setting decides which lines are listed
TEST(Todos, theViewGroupsSortsAndFilters) {
    QTemporaryDir tmp;
    const fs::path root = fs::path(tmp.path().toStdString()) / "Library";
    writeFile(root / "alpha.md", "- [ ] todo: late due:2026-10-01\n- [ ] todo: no date\n- [x] todo: finished\n"
                                 "- [ ] plain item\n");
    writeFile(root / "Sub" / "beta.md", "- [ ] todo: soon \xF0\x9F\x93\x85 2026-10-06\n- [ ] todo: today due:2026-10-04\n");
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    model.searchIndex()->waitForDone();
    LibraryTodosModel view(&model);
    view.setToday(QDate(2026, 10, 4));  // (a Sunday: the week ends today where it begins on Monday)
    EXPECT_EQ(view.count(), 0) << "made only while shown";
    view.setActive(true);
    auto shown = [&] {
        std::vector<std::string> out;
        for (int i = 0; i < view.count(); ++i) {
            out.push_back(view.data(view.index(i), LibraryTodosModel::TextRole).toString().toStdString());
        }
        return out;
    };
    // By due date; grouped by document in the order the groups first come
    EXPECT_EQ(shown(), (std::vector<std::string>{"late", "no date", "today", "soon"}));
    EXPECT_EQ(view.total(), 5) << "the marked ones, also done";
    EXPECT_EQ(view.data(view.index(0), LibraryTodosModel::GroupCountRole).toInt(), 2);
    EXPECT_EQ(view.data(view.index(0), LibraryTodosModel::DueStateRole).toString(), "overdue");
    EXPECT_EQ(view.data(view.index(2), LibraryTodosModel::DueStateRole).toString(), "today");
    EXPECT_EQ(view.groupOf(QString::fromStdString((root / "Sub" / "beta.md").string())).value("folder").toString(),
              "Sub");
    view.setGrouping("none");
    EXPECT_EQ(shown(), (std::vector<std::string>{"late", "today", "soon", "no date"}));
    view.setSortBy("document");
    EXPECT_EQ(shown(), (std::vector<std::string>{"late", "no date", "soon", "today"}));
    view.setSortBy("due");
    view.setStatus("all");
    EXPECT_EQ(shown().back(), "finished");
    view.setStatus("done");
    EXPECT_EQ(shown(), (std::vector<std::string>{"finished"}));
    view.setStatus("open");
    view.setDue("overdue");
    EXPECT_EQ(shown(), (std::vector<std::string>{"late"}));
    view.setDue("week");
    EXPECT_EQ(shown(), (std::vector<std::string>{"today"})) << "the week ends today";
    view.setDue("none");
    EXPECT_EQ(shown(), (std::vector<std::string>{"no date"}));
    view.setDue("any");
    view.setQuery("SOO");
    EXPECT_EQ(shown(), (std::vector<std::string>{"soon"}));
    view.setQuery("");
    model.setFolder("Sub");
    view.setFolderOnly(true);
    EXPECT_EQ(shown(), (std::vector<std::string>{"today", "soon"}));
    view.setFolderOnly(false);
    todos::Rules all;
    all.all = true;
    view.setRules(all);
    EXPECT_EQ(view.count(), 5);
    // A to-do ticked: shown so at once, until the index has it
    view.setRules({});
    view.setPending(QString::fromStdString((root / "alpha.md").string()), "todo: late due:2026-10-01", 0, true);
    EXPECT_TRUE(view.data(view.index(0), LibraryTodosModel::DoneRole).toBool());
    EXPECT_TRUE(view.data(view.index(0), LibraryTodosModel::PendingRole).toBool());
    view.refresh();
    EXPECT_EQ(view.count(), 3) << "a pending done one is not open";
    view.clearPending(QString::fromStdString((root / "alpha.md").string()), "todo: late due:2026-10-01", 0);
    EXPECT_EQ(view.count(), 4);
}

// Written back: a Markdown file through its text (only the mark changes; the same text twice: the right one); a
// document's box found again by its text and occurrence; what is not changed while it is not open
TEST(Todos, theyAreFoundAgainAndWritten) {
    QTemporaryDir tmp;
    const fs::path root(tmp.path().toStdString());
    const std::string text = "\xEF\xBB\xBF- [ ] todo: one\r\n- [ ] todo: one\r\n";
    writeFile(root / "a.md", text);
    bool found = false;
    std::string error;
    ASSERT_TRUE(todos::setInMarkdownFile(root / "a.md", "todo: one", 1, true, found, error)) << error;
    EXPECT_TRUE(found);
    std::ifstream in(root / "a.md", std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()),
              "\xEF\xBB\xBF- [ ] todo: one\r\n- [x] todo: one\r\n");
    ASSERT_TRUE(todos::setInMarkdownFile(root / "a.md", "todo: two", 0, true, found, error));
    EXPECT_FALSE(found);

    sticky::installDrawer();
    Document doc(nullptr);
    std::vector<std::unique_ptr<Text>> boxes;
    boxes.push_back(box("- [ ] todo: a"));
    boxes.push_back(box("- [x] todo: b\n- [ ] todo: a"));
    doc.addPage(pageWith({}));
    doc.addPage(pageWith(std::move(boxes)));
    write(doc, root / "n.xopp");
    auto loaded = DocumentSession::loadFile(root / "n.xopp");
    ASSERT_TRUE(loaded.document);
    const auto place = todos::find(*loaded.document, "todo: a", 1);
    ASSERT_TRUE(place);
    EXPECT_EQ(place->pageIndex, 1u);
    EXPECT_EQ(place->box->getText(), "- [x] todo: b\n- [ ] todo: a");
    EXPECT_EQ(place->box->getText()[place->mark], ' ');
    EXPECT_FALSE(place->done);
    EXPECT_TRUE(todos::find(*loaded.document, "todo: b", 0)->done);
    EXPECT_FALSE(todos::find(*loaded.document, "todo: a", 2));

    EXPECT_EQ(todos::whyNotWritable(root / "a.md", PdfKind::Unknown), "");
    EXPECT_EQ(todos::whyNotWritable(root / "n.xopp", PdfKind::Unknown), "");
    writeFile(root / "x.pdf", "%PDF-1.4");
    EXPECT_EQ(todos::whyNotWritable(root / "x.pdf", PdfKind::Notes), "");
    EXPECT_NE(todos::whyNotWritable(root / "x.pdf", PdfKind::Archive), "");
    EXPECT_NE(todos::whyNotWritable(root / "x.pdf", PdfKind::Plain), "");
    writeFile(root / "old.xoj", "x");
    EXPECT_NE(todos::whyNotWritable(root / "old.xoj", PdfKind::Unknown), "");
    EXPECT_NE(todos::whyNotWritable(root / "gone.md", PdfKind::Unknown), "");
}
