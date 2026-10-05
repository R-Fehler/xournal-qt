/*
 * xournal-qt: tags in the library home's real window (qt/docs/tags.md): the Tags tab with its tags and counts (nested
 * ones folded), a tap that shows the documents with the tag (with the "Show" filter and the folder), the chip that takes
 * the filter away, tags on the cards ("+N" when there are more).
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <functional>
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/PdfKeywords.h"
#include "shell/HitPages.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"
#include "shell/LibraryTags.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/Previews.h"
#include "shell/RecentFiles.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"

#include "AppController.h"

namespace fs = std::filesystem;

namespace {
void writeFile(const fs::path& p, const std::string& bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << bytes;
}

/// A .xopp with one typed text
void writeNotes(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    Document doc(nullptr);
    auto page = std::make_shared<XojPage>(595.0, 842.0);
    auto* layer = new Layer();
    page->getLayers().push_back(layer);
    auto t = std::make_unique<Text>();
    t->setText(text);
    t->setFont(XojFont("Sans", 11));
    t->move(60, 300);
    layer->addElement(std::move(t));
    doc.addPage(page);
    ASSERT_TRUE(xqt::DocumentSession::writeDocument(doc, file).ok);
}

void writePdf(const fs::path& p, const char* keywords) {
    fs::create_directories(p.parent_path());
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    if (*keywords) {
        cairo_pdf_surface_set_metadata(s, CAIRO_PDF_METADATA_KEYWORDS, keywords);
    }
    cairo_t* cr = cairo_create(s);
    cairo_move_to(cr, 72, 100);
    cairo_show_text(cr, "a paper");
    cairo_show_page(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

class TagsUiTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString()) / "Library";
        writeNotes(root / "lecture.xopp", "Kalman #course/math #exam");
        writeFile(root / "Sub" / "plan.md", "# Plan\n\nFor #course, also #one #two #three\n");
        writePdf(root / "Sub" / "paper.pdf", "exam");
        writePdf(root / "plain.pdf", "");
        controller = std::make_unique<AppController>();
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
        window->resize(1400, 900);
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        QTest::mouseMove(window, QPoint(-20, -20));
        library = qobject_cast<xqt::LibraryModel*>(controller->libraryModel());
        tags = qobject_cast<xqt::LibraryTagsModel*>(controller->libraryTagsModel());
        ASSERT_NE(tags, nullptr);
        until([&] { return !library->indexing() && library->searchIndex()->tagged().size() == 3; });
        ASSERT_EQ(library->searchIndex()->tagged().size(), 3u);
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
    std::vector<QQuickItem*> findAll(const QString& name, QQuickItem* in = nullptr) const {
        std::vector<QQuickItem*> out;
        walk(in ? in : window->contentItem(), [&](QQuickItem* i) {
            if (i->objectName() == name && i->isVisible()) {
                out.push_back(i);
            }
        });
        return out;
    }
    void click(QQuickItem* item) {
        ASSERT_NE(item, nullptr);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        wait(50);
    }
    /// The row of the Tags list for this tag (nullptr: none shown)
    QQuickItem* tagRow(const QString& tag) const {
        QQuickItem* found = nullptr;
        walk(window->contentItem(), [&](QQuickItem* i) {
            if (!found && i->objectName() == "tagRow" && i->isVisible() && i->property("tag").toString() == tag) {
                found = i;
            }
        });
        return found;
    }
    QQuickItem* childNamed(QQuickItem* in, const QString& name) const {
        QQuickItem* found = nullptr;
        walk(in, [&](QQuickItem* c) {
            if (!found && c->objectName() == name && c->isVisible()) {
                found = c;
            }
        });
        return found;
    }
    std::vector<std::string> rowNames() const {
        std::vector<std::string> out;
        for (int i = 0; i < library->rowCount(); ++i) {
            out.push_back(library->data(library->index(i), xqt::LibraryModel::NameRole).toString().toStdString());
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    void showTags() {
        click(findItem("tagsPageButton"));
        until([&] { return findItem("tagsList") != nullptr; });
        ASSERT_NE(findItem("tagsList"), nullptr);
    }

    QTemporaryDir tmp;
    fs::path root;
    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
    xqt::LibraryModel* library = nullptr;
    xqt::LibraryTagsModel* tags = nullptr;
};
}  // namespace

// The Tags tab: each tag with how many documents have it, nested ones folded under their parent; a tap lists the
// documents with the tag in the library (a chip says so; a tap on it shows all again)
TEST_F(TagsUiTest, theTagsTabListsTagsAndATapFiltersTheLibrary) {
    showTags();
    until([&] { return tagRow("course") != nullptr; });
    QQuickItem* course = tagRow("course");
    ASSERT_NE(course, nullptr);
    EXPECT_EQ(childNamed(course, "tagCount")->property("text").toString(), "2 documents");
    EXPECT_EQ(tags->countOf("exam"), 2) << "typed, and a PDF's keyword";
    EXPECT_EQ(tags->countOf("course/math"), 1);
    EXPECT_EQ(tagRow("course/math"), nullptr) << "folded";
    click(childNamed(course, "tagFold"));
    until([&] { return tagRow("course/math") != nullptr; });
    ASSERT_NE(tagRow("course/math"), nullptr);
    EXPECT_EQ(childNamed(tagRow("course/math"), "tagName")->property("text").toString(), "#math");
    EXPECT_EQ(tags->total(), 6);

    // The "Show" filter: without PDFs, only the typed #exam counts
    library->setShown("pdfs", false);
    until([&] { return tags->countOf("exam") == 1; });
    EXPECT_EQ(tags->countOf("exam"), 1);
    library->setShown("pdfs", true);
    until([&] { return tags->countOf("exam") == 2; });

    // A tap: the library with the documents tagged #exam
    click(tagRow("exam"));
    until([&] { return library->tagFilter() == "exam"; });
    EXPECT_EQ(library->tagFilter(), "exam");
    until([&] { return findItem("tagFilterChip") != nullptr; });
    ASSERT_NE(findItem("tagFilterChip"), nullptr);
    EXPECT_EQ(findItem("tagFilterLabel")->property("text").toString(), "#exam");
    EXPECT_EQ(rowNames(), (std::vector<std::string>{"lecture", "paper"})) << "no folders, also from subfolders";
    // In a folder: only those inside it
    library->setFolder("Sub");
    EXPECT_EQ(rowNames(), (std::vector<std::string>{"paper"}));
    library->setFolder("");
    library->setTagFilter("course");
    EXPECT_EQ(rowNames(), (std::vector<std::string>{"lecture", "plan"})) << "with the tags inside it";
    click(findItem("tagFilterChip"));
    EXPECT_EQ(library->tagFilter(), "");
    EXPECT_EQ(rowNames().size(), 3u) << "lecture, plain and the folder Sub";
}

// Cards show a few tags, then "+N"
TEST_F(TagsUiTest, cardsShowTheirTags) {
    library->setFlat(true);
    until([&] { return findAll("cardTags").size() == 3; });
    ASSERT_EQ(findAll("cardTags").size(), 3u) << "the plain PDF has none";
    QQuickItem* more = findItem("cardTagsMore");
    ASSERT_NE(more, nullptr) << "plan.md has 4 tags";
    bool plusShown = false;
    walk(more, [&](QQuickItem* i) {
        plusShown = plusShown || i->property("text").toString().startsWith("+");
    });
    EXPECT_TRUE(plusShown);
    EXPECT_FALSE(findAll("cardTag").empty());
}

// "Tags…" of a card: a PDF gets tags as keywords (Save writes the file, the index and the card follow); a Xournal++ file
// shows its typed tags and says how to add one
TEST_F(TagsUiTest, theTagsDialogWritesAPdfsKeywords) {
    const QString paper = QString::fromStdString((root / "plain.pdf").string());
    QVariantMap info = controller->documentTags(QString::fromStdString((root / "lecture.xopp").string()));
    EXPECT_FALSE(info.value("editable").toBool()) << "a .xopp: typed tags only";
    EXPECT_FALSE(info.value("why").toString().isEmpty());
    EXPECT_EQ(info.value("typed").toStringList(), (QStringList{"course/math", "exam"}));
    info = controller->documentTags(paper);
    EXPECT_TRUE(info.value("editable").toBool());
    EXPECT_TRUE(info.value("file").toStringList().isEmpty());
    EXPECT_TRUE(info.value("suggestions").toStringList().contains("exam"));

    // The card's menu → Tags…: a tag typed, one of the suggestions, Save
    library->setFlat(true);
    QObject* dialog = nullptr;
    for (QObject* o: window->findChildren<QObject*>("tagsDialog")) {
        if (o->parent() && QString(o->parent()->metaObject()->className()).contains("HomeView")) {
            dialog = o;
        }
    }
    if (!dialog) {
        dialog = window->findChild<QObject*>("tagsDialog");
    }
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, paper));
    until([&] { return dialog->property("opened").toBool(); });
    wait(300);  // (its transition: the field where it stays)
    QQuickItem* field = findItem("tagsDialogField");
    ASSERT_NE(field, nullptr);
    click(field);
    for (char c: std::string("#reading")) {
        QTest::keyClick(window, c);
    }
    QTest::keyClick(window, Qt::Key_Return);
    until([&] { return !findAll("tagsDialogChip").empty(); });
    EXPECT_EQ(findAll("tagsDialogChip").size(), 1u);
    QQuickItem* exam = nullptr;
    for (QQuickItem* s: findAll("tagsDialogSuggestion")) {
        if (s->property("tag").toString() == "exam") {
            exam = s;
        }
    }
    ASSERT_NE(exam, nullptr);
    // (the chip just added lays the suggestions out anew: click where "exam" stays, not where it was a moment ago)
    QPointF last(-1, -1);
    int still = 0;
    until([&] {
        const QPointF at = exam->mapToScene(QPointF(exam->width() / 2, exam->height() / 2));
        still = at == last ? still + 1 : 0;
        last = at;
        return still >= 5;  // (unmoved for 100 ms)
    });
    click(exam);
    EXPECT_EQ(findAll("tagsDialogChip").size(), 2u);
    QMetaObject::invokeMethod(dialog, "accept");
    until([&] { return xqt::pdfkeywords::tagsOf(root / "plain.pdf").size() == 2; });
    EXPECT_EQ(xqt::pdfkeywords::tagsOf(root / "plain.pdf"), (QStringList{"reading", "exam"}));
    until([&] { return library->searchIndex()->tagsOf(root / "plain.pdf").size() == 2; });
    EXPECT_EQ(library->searchIndex()->tagsOf(root / "plain.pdf"), (QStringList{"reading", "exam"}));
}

// A PDF open in a tab: without changes it gets the tags and the tab reads it again; with unsaved changes it is refused
TEST_F(TagsUiTest, aPdfOpenInATab) {
    const QString paper = QString::fromStdString((root / "plain.pdf").string());
    ASSERT_TRUE(controller->openPath(paper));
    until([&] { return controller->tabCount() == 1; });
    ASSERT_TRUE(controller->setDocumentTags(paper, {"open"}));
    until([&] { return xqt::pdfkeywords::tagsOf(root / "plain.pdf") == QStringList{"open"}; });
    EXPECT_EQ(xqt::pdfkeywords::tagsOf(root / "plain.pdf"), QStringList{"open"});
    wait(100);
    EXPECT_EQ(controller->tabCount(), 1) << "read again in its place";
    xqt::DocumentSession* s = controller->tabManager().currentSession();
    ASSERT_NE(s, nullptr);
    {
        Document& doc = *s->getDocument();
        auto page = doc.getPage(0);
        auto stroke = std::make_unique<Stroke>();
        stroke->setWidth(2);
        stroke->addPoint(Point(100, 100, 1.0));
        stroke->addPoint(Point(200, 180, 1.0));
        const Stroke* raw = stroke.get();
        Layer* layer = page->getSelectedLayer();
        doc.lock();
        layer->addElement(std::move(stroke));
        doc.unlock();
        s->getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
    }
    ASSERT_TRUE(s->isModified());
    EXPECT_FALSE(controller->setDocumentTags(paper, {"other"})) << "unsaved changes: saved first";
    wait(100);
    EXPECT_EQ(xqt::pdfkeywords::tagsOf(root / "plain.pdf"), QStringList{"open"});
    s->getUndoRedoHandler()->undo();
}
