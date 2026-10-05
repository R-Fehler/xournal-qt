/*
 * xournal-qt: page templates (qt/docs/templates.md): the sets on disk (the Templates folder, the app-wide set, the
 * attached PDF going along), saving a page as a template with and without its background and content, and adding a
 * template's page as a pasted copy of that page: a PDF page shows that PDF page (its text searchable) and is saved
 * right in a .xopp and in a PDF with notes; one undo step; a new document that starts with it.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <cairo-pdf.h>
#include <gtest/gtest.h>

#include "control/settings/Settings.h"
#include "control/xojfile/LoadHandler.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentMode.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "session/MergedPdf.h"
#include "session/TemplateFile.h"
#include "shell/Stickers.h"
#include "shell/TabManager.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"

#include "AppController.h"

using namespace xqt;

namespace {

void makeTextPdf(const fs::path& p, const std::vector<std::string>& words) {
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    cairo_set_font_size(cr, 24);
    for (const auto& w: words) {
        cairo_move_to(cr, 72, 100);
        cairo_show_text(cr, w.c_str());
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

void touch(const fs::path& file, const std::string& content = "x") {
    fs::create_directories(file.parent_path());
    std::ofstream(file) << content;
}

/// A stroke on a page, through the undo stack (the document is changed).
void drawStroke(DocumentSession& s, size_t pageNo) {
    auto page = s.getDocument()->getPage(pageNo);
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(2);
    stroke->setColor(Color(0xffcc0000U));
    stroke->addPoint(Point(100, 100, 1.0));
    stroke->addPoint(Point(200, 180, 3.0));
    const Stroke* raw = stroke.get();
    Layer* layer = page->getSelectedLayer();
    s.getDocument()->lock();
    layer->addElement(std::move(stroke));
    s.getDocument()->unlock();
    s.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
}

size_t strokesOn(const XojPage& page) {
    size_t n = 0;
    for (const Layer* l: page.getLayersView()) {
        for (const auto& e: l->getElementsView()) {
            n += e->getType() == ELEMENT_STROKE;
        }
    }
    return n;
}

bool waitFor(const std::function<bool()>& done, int ms = 20000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

QUrl url(const fs::path& p) { return QUrl::fromLocalFile(QString::fromStdString(p.string())); }

class TemplatesTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        appSet = root / "app-templates";
        stickers::setAppSet(appSet, stickers::Kind::Templates);
        makeTextPdf(root / "lecture.pdf", {"lectureone", "lecturetwo", "lecturethree"});
    }
    void TearDown() override { stickers::setAppSet({}, stickers::Kind::Templates); }

    struct Mode {
        Mode(AppController& c, DocumentMode::Mode m): settings(*c.context().getSettings()) { set(m); }
        ~Mode() { DocumentMode::store(settings, DocumentMode::Mode::Unset); }
        void set(DocumentMode::Mode m) { DocumentMode::store(settings, m); }
        Settings& settings;
    };
    bool open(AppController& c, const fs::path& p) { return c.openPath(QString::fromStdString(p.string())); }
    DocumentSession& current(AppController& c) { return *c.tabManager().currentSession(); }
    /// Save page `page` of the current document as a template of the app-wide set; its file
    fs::path save(AppController& c, int page, const QString& name, bool background, bool content,
                  const QString& folder = {}) {
        QSignalSpy saved(&c, &AppController::templateSaved);
        EXPECT_TRUE(c.saveTemplate(page, name, folder, background, content, true));
        EXPECT_TRUE(waitFor([&] { return saved.count() > 0; }));
        if (saved.isEmpty()) {
            return {};
        }
        EXPECT_EQ(saved.first().at(1).toString(), QString());
        return fs::path(saved.first().at(0).toString().toStdString());
    }
    /// Add a template's page to the current document; the pages added
    int insert(AppController& c, const fs::path& file, int position = -1, int count = 1) {
        QSignalSpy done(&c, &AppController::templateInserted);
        EXPECT_TRUE(c.insertTemplate(QString::fromStdString(file.string()), position, count));
        EXPECT_TRUE(waitFor([&] { return done.count() > 0; }));
        if (done.isEmpty()) {
            return 0;
        }
        EXPECT_EQ(done.first().at(2).toString(), QString());
        return done.first().at(1).toInt();
    }
    bool pageHasText(DocumentSession& s, size_t page, const char* text) {
        s.waitForSaves();
        return !DocumentSearch::findOnPage(*s.getDocument(), page, text).empty();
    }

    QTemporaryDir tmp;
    fs::path root, appSet;
};

}  // namespace

// --- the sets on disk -------------------------------------------------------------------------------------------

TEST_F(TemplatesTest, theTemplatesFolderHoldsXoppFilesWithTheirPdf) {
    const fs::path set = stickers::librarySet(root / "Library", stickers::Kind::Templates);
    EXPECT_EQ(set, root / "Library" / "Templates");
    EXPECT_EQ(stickers::appSet(stickers::Kind::Templates), appSet);
    touch(set / "Week.xopp");
    touch(set / "Slide.xopp");
    touch(set / "Slide.xopp.bg.pdf");  // (its PDF page: part of it)
    touch(set / "Logo.png");           // (a picture is a sticker, not a template)
    touch(set / "Lecture 3" / "Exercise.xopp");
    std::set<std::string> found;
    for (const auto& e: stickers::list(set, stickers::Kind::Templates)) {
        found.insert(e.folder + "|" + e.name);
    }
    EXPECT_EQ(found, (std::set<std::string>{"|Week", "|Slide", "Lecture 3|Exercise"}));
    EXPECT_EQ(stickers::uniqueTarget(set, "Slide"), set / "Slide (2).xopp");
    touch(set / "Gone.xopp.bg.pdf");
    EXPECT_EQ(stickers::uniqueTarget(set, "Gone"), set / "Gone (2).xopp") << "never another template's PDF";

    // The own order has a file of its own; renamed, moved, copied and deleted, the PDF goes along
    ASSERT_TRUE(stickers::moveInOrder(set / "Week.xopp", -1, stickers::Kind::Templates));
    EXPECT_TRUE(fs::exists(set / stickers::TEMPLATE_ORDER_FILE));
    EXPECT_FALSE(fs::exists(set / stickers::ORDER_FILE));
    const auto renamed = stickers::rename(set / "Slide.xopp", "Title slide", stickers::Kind::Templates);
    ASSERT_TRUE(renamed);
    EXPECT_TRUE(fs::exists(set / "Title slide.xopp.bg.pdf"));
    EXPECT_FALSE(fs::exists(set / "Slide.xopp.bg.pdf"));
    const auto moved = stickers::moveTo(*renamed, set / "Lecture 3", stickers::Kind::Templates);
    ASSERT_TRUE(moved);
    EXPECT_TRUE(fs::exists(set / "Lecture 3" / "Title slide.xopp.bg.pdf"));
    const auto copied = stickers::copyTo(*moved, appSet);
    ASSERT_TRUE(copied);
    EXPECT_TRUE(fs::exists(appSet / "Title slide.xopp.bg.pdf"));
    EXPECT_TRUE(fs::exists(set / "Lecture 3" / "Title slide.xopp.bg.pdf"));
    ASSERT_TRUE(stickers::trash(*copied, stickers::Kind::Templates));
    EXPECT_FALSE(fs::exists(appSet / "Title slide.xopp"));
    EXPECT_FALSE(fs::exists(appSet / "Title slide.xopp.bg.pdf"));
}

TEST_F(TemplatesTest, theModelListsTheTemplatesAndTheLastUsed) {
    AppController c;
    c.setLibraryRoot(root);
    auto* model = qobject_cast<StickersModel*>(c.templatesModel());
    ASSERT_TRUE(model);
    EXPECT_EQ(model->kindName(), "templates");
    EXPECT_EQ(model->libraryFolder().toStdString(), (root / "Templates").string());
    touch(root / "Templates" / "Week.xopp");
    touch(appSet / "Grid.xopp");
    model->refresh();
    EXPECT_EQ(model->rowCount(), 1);
    model->markUsed(QString::fromStdString((appSet / "Grid.xopp").string()));
    model->markUsed(QString::fromStdString((root / "Templates" / "Week.xopp").string()));
    const QVariantList recent = model->recent(5);
    ASSERT_EQ(recent.size(), 2);
    EXPECT_EQ(recent[0].toMap().value("name").toString(), "Week");
    EXPECT_EQ(recent[1].toMap().value("name").toString(), "Grid");
    // (the stickers keep their own list)
    auto* stickerModel = qobject_cast<StickersModel*>(c.stickersModel());
    EXPECT_EQ(stickerModel->libraryFolder().toStdString(), (root / "Stickers").string());
    EXPECT_TRUE(stickerModel->recent(5).isEmpty());
}

// --- saving -----------------------------------------------------------------------------------------------------

TEST_F(TemplatesTest, aPageSavedWithAndWithoutItsBackgroundAndContent) {
    AppController c;
    c.newDocument();
    DocumentSession& s = current(c);
    {
        std::unique_lock lock(*s.getDocument());
        s.getDocument()->getPage(0)->setBackgroundType(PageType(PageTypeFormat::Graph));
    }
    drawStroke(s, 0);
    const QVariantMap draft = c.templateDraft(0, true);
    EXPECT_TRUE(draft.value("offered").toBool());
    EXPECT_EQ(draft.value("background").toString(), "paper");
    EXPECT_FALSE(draft.value("name").toString().isEmpty());

    const fs::path both = save(c, 0, "Both", true, true, "Lecture 3");
    EXPECT_EQ(both, appSet / "Lecture 3" / "Both.xopp");
    const fs::path paper = save(c, 0, "Paper", true, false);
    const fs::path ink = save(c, 0, "Ink", false, true);
    EXPECT_FALSE(c.saveTemplate(0, "Nothing", {}, false, false, true)) << "nothing to save";

    // Upstream's loader reads them
    for (const auto& [file, graph, strokes]: std::vector<std::tuple<fs::path, bool, size_t>>{
                 {both, true, 1}, {paper, true, 0}, {ink, false, 1}}) {
        std::vector<std::string> warnings;
        LoadHandler handler(&warnings);
        auto loaded = handler.loadDocument(file);
        ASSERT_TRUE(loaded) << file;
        EXPECT_TRUE(warnings.empty()) << file;
        ASSERT_EQ(loaded->getPageCount(), 1u);
        EXPECT_EQ(loaded->getPage(0)->getBackgroundType() == PageType(PageTypeFormat::Graph), graph) << file;
        EXPECT_EQ(strokesOn(*loaded->getPage(0)), strokes) << file;
        EXPECT_FALSE(fs::exists(templates::attachedPdfOf(file))) << "no PDF page, no PDF";
    }
    EXPECT_EQ(qobject_cast<StickersModel*>(c.templatesModel())->rowCount(), 3);
}

// --- using one --------------------------------------------------------------------------------------------------

TEST_F(TemplatesTest, aTemplateWithoutItsBackgroundGetsTheBackgroundOfNewPagesThere) {
    AppController c;
    c.newDocument();
    drawStroke(current(c), 0);
    const fs::path ink = save(c, 0, "Ink", false, true);

    c.newDocument();
    DocumentSession& s = current(c);
    {
        std::unique_lock lock(*s.getDocument());
        s.getDocument()->getPage(0)->setBackgroundType(PageType(PageTypeFormat::Lined));
        s.getDocument()->getPage(0)->setBackgroundColor(Color(0xffeeffeeU));
    }
    ASSERT_EQ(insert(c, ink), 1);
    ASSERT_EQ(s.getDocument()->getPageCount(), 2u);
    const PageRef added = s.getDocument()->getPage(1);
    EXPECT_EQ(added->getBackgroundType(), PageType(PageTypeFormat::Lined));
    EXPECT_EQ(added->getBackgroundColor(), Color(0xffeeffeeU));
    EXPECT_FALSE(templates::withoutBackground(*added));
    EXPECT_EQ(strokesOn(*added), 1u);
    EXPECT_EQ(s.getCurrentPageNo(), 1u) << "after the current page, shown";
}

TEST_F(TemplatesTest, aPdfPageTemplateIsThatPdfPageInAnotherDocument) {
    AppController c;
    ASSERT_TRUE(open(c, root / "lecture.pdf"));
    drawStroke(current(c), 1);
    EXPECT_EQ(c.templateDraft(1, true).value("background").toString(), "pdf");
    const fs::path slide = save(c, 1, "Slide", true, true);
    ASSERT_TRUE(fs::exists(slide));
    ASSERT_TRUE(fs::exists(templates::attachedPdfOf(slide))) << "the PDF page goes along";
    {
        // Upstream's loader: the PDF page of the template
        std::vector<std::string> warnings;
        LoadHandler handler(&warnings);
        auto loaded = handler.loadDocument(slide);
        ASSERT_TRUE(loaded);
        EXPECT_TRUE(warnings.empty());
        EXPECT_EQ(loaded->getPdfPageCount(), 1u);
        EXPECT_FALSE(DocumentSearch::findOnPage(*loaded, 0, "lecturetwo").empty());
    }

    // Into a .xopp without PDF: its own paired PDF, as a pasted copy of that page
    c.newDocument();
    ASSERT_TRUE(c.saveAs(url(root / "notes.xopp")));
    DocumentSession& s = current(c);
    ASSERT_EQ(insert(c, slide, 0, 2), 2);
    ASSERT_EQ(s.getDocument()->getPageCount(), 3u);
    for (size_t i: {0u, 1u}) {
        const PageRef page = s.getDocument()->getPage(i);
        ASSERT_TRUE(page->getBackgroundType().isPdfPage()) << "a PDF page, not a picture";
        EXPECT_TRUE(pageHasText(s, i, "lecturetwo"));
        EXPECT_EQ(strokesOn(*page), 1u);
    }
    EXPECT_EQ(s.getDocument()->getPage(0)->getPdfPageNr(), s.getDocument()->getPage(1)->getPdfPageNr())
            << "the PDF page added once";
    ASSERT_TRUE(c.save());
    s.waitForSaves();
    EXPECT_EQ(s.getDocument()->getPdfFilepath(), root / "notes.pdf");

    // One undo step
    c.undoPages();
    EXPECT_EQ(s.getDocument()->getPageCount(), 1u);
    c.redoPages();
    ASSERT_EQ(s.getDocument()->getPageCount(), 3u);
    ASSERT_TRUE(c.save());

    // Saved right: opened again, the PDF page is there
    auto loaded = DocumentSession::loadFile(root / "notes.xopp");
    ASSERT_TRUE(loaded.document) << loaded.error;
    ASSERT_EQ(loaded.document->getPageCount(), 3u);
    EXPECT_FALSE(DocumentSearch::findOnPage(*loaded.document, 1, "lecturetwo").empty());

    // The template's file stays as it was
    EXPECT_TRUE(fs::exists(templates::attachedPdfOf(slide)));
}

TEST_F(TemplatesTest, aPdfPageTemplateInAPdfWithNotes) {
    AppController c;
    ASSERT_TRUE(open(c, root / "lecture.pdf"));
    const fs::path slide = save(c, 2, "Third", true, true);
    c.closeTab(0);

    Mode mode(c, DocumentMode::Mode::Pdf);
    makeTextPdf(root / "paper.pdf", {"paperone"});
    ASSERT_TRUE(open(c, root / "paper.pdf"));
    DocumentSession& s = current(c);
    ASSERT_EQ(insert(c, slide), 1);
    ASSERT_EQ(s.getDocument()->getPageCount(), 2u);
    EXPECT_TRUE(pageHasText(s, 1, "lecturethree"));
    ASSERT_TRUE(c.save());
    EXPECT_TRUE(HybridPdf::isHybrid(root / "paper.pdf"));
    std::vector<std::string> next;
    for (const auto& e: fs::directory_iterator(root)) {
        next.push_back(e.path().filename().string());
    }
    std::sort(next.begin(), next.end());
    EXPECT_EQ(next, (std::vector<std::string>{"app-templates", "lecture.pdf", "paper.pdf"})) << "nothing next to it";

    c.closeTab(c.tabManager().indexOf(&s));
    ASSERT_TRUE(open(c, root / "paper.pdf"));
    DocumentSession& again = current(c);
    ASSERT_EQ(again.getDocument()->getPageCount(), 2u);
    EXPECT_TRUE(again.getDocument()->getPage(1)->getBackgroundType().isPdfPage());
    EXPECT_TRUE(pageHasText(again, 1, "lecturethree"));
    EXPECT_TRUE(pageHasText(again, 0, "paperone"));
}

TEST_F(TemplatesTest, aNewDocumentStartsWithTheTemplatesPage) {
    AppController c;
    ASSERT_TRUE(open(c, root / "lecture.pdf"));
    const fs::path slide = save(c, 0, "First", true, true);
    fs::create_directories(root / "Library");
    c.setLibraryRoot(root / "Library");

    QSignalSpy done(&c, &AppController::templateInserted);
    ASSERT_TRUE(c.createDocumentFromTemplate("Week 1", true, QString::fromStdString(slide.string())));
    ASSERT_TRUE(waitFor([&] { return done.count() > 0; }));
    EXPECT_EQ(done.first().at(2).toString(), QString());
    DocumentSession& s = current(c);
    ASSERT_EQ(s.getDocument()->getPageCount(), 1u);
    EXPECT_TRUE(s.getDocument()->getPage(0)->getBackgroundType().isPdfPage());
    EXPECT_TRUE(pageHasText(s, 0, "lectureone"));
    EXPECT_FALSE(c.canUndoPages()) << "a new document: nothing to undo";
    EXPECT_EQ(s.getFilePath(), root / "Library" / "Week 1.xopp");
    auto loaded = DocumentSession::loadFile(root / "Library" / "Week 1.xopp");
    ASSERT_TRUE(loaded.document) << loaded.error;
    ASSERT_EQ(loaded.document->getPageCount(), 1u);
    EXPECT_FALSE(DocumentSearch::findOnPage(*loaded.document, 0, "lectureone").empty());
}

TEST_F(TemplatesTest, aMissingTemplateSaysSo) {
    AppController c;
    c.newDocument();
    QSignalSpy done(&c, &AppController::templateInserted);
    ASSERT_TRUE(c.insertTemplate(QString::fromStdString((root / "missing.xopp").string())));
    ASSERT_TRUE(waitFor([&] { return done.count() > 0; }));
    EXPECT_FALSE(done.first().at(2).toString().isEmpty());
    EXPECT_EQ(current(c).getDocument()->getPageCount(), 1u);
}
