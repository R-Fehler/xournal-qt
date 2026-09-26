/*
 * xournal-qt: sticky notes (qt/docs/sticky-notes.md): the file format (a layer per note, its paper first), a save and
 * load round trip, upstream Xournal++ opening the file, and the note drawn with its content clipped to it in every
 * export (PDF, print, hybrid PDF, archive, thumbnails), without the marks that only the screen shows.
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <cstdlib>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <cairo.h>
#include <gtest/gtest.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include "control/ExportHelper.h"
#include "model/Document.h"
#include "model/Font.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfDocument.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "session/StickyNote.h"
#include "undo/UndoRedoHandler.h"
#include "view/DocumentView.h"

using namespace xqt;

namespace {

const Color YELLOW = sticky::presetColors()[0];
const Color PINK = sticky::presetColors()[1];
const Color INK(0x00, 0x00, 0x00);
const Color BLUE(0x10, 0x20, 0xc0);

Stroke* addStroke(Layer* layer, Color color, double width, std::vector<Point> points) {
    auto s = std::make_unique<Stroke>();
    s->setToolType(StrokeTool::PEN);
    s->setColor(color);
    s->setWidth(width);
    for (const auto& p: points) {
        s->addPoint(p);
    }
    Stroke* raw = s.get();
    layer->addElement(std::move(s));
    return raw;
}

Text* addText(Layer* layer, const std::string& text, double x, double y) {
    auto t = std::make_unique<Text>();
    t->setText(text);
    t->setFont(XojFont("Sans", 12));
    t->setColor(INK);
    t->move(x, y);
    Text* raw = t.get();
    layer->addElement(std::move(t));
    return raw;
}

struct Rgb {
    int r = 0, g = 0, b = 0;
};
Rgb pixel(cairo_surface_t* s, int x, int y) {
    cairo_surface_flush(s);
    const unsigned char* p = cairo_image_surface_get_data(s) + y * cairo_image_surface_get_stride(s) + 4 * x;
    return {p[2], p[1], p[0]};  // (ARGB32, little endian: B G R A)
}
bool near(Rgb a, Color c, int tolerance = 12) {
    return std::abs(a.r - c.red) <= tolerance && std::abs(a.g - c.green) <= tolerance &&
           std::abs(a.b - c.blue) <= tolerance;
}
std::string str(Rgb a) {
    return "rgb(" + std::to_string(a.r) + ", " + std::to_string(a.g) + ", " + std::to_string(a.b) + ")";
}

/// A page of a PDF as poppler draws it (annotations too), white behind, 1 px per point.
cairo_surface_t* renderPdf(const fs::path& pdf, size_t page) {
    XojPdfDocument doc;
    GError* error = nullptr;
    EXPECT_TRUE(doc.load(pdf, "", &error)) << pdf;
    if (error) {
        g_error_free(error);
    }
    XojPdfPageSPtr p = doc.getPage(page);
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, static_cast<int>(std::ceil(p->getWidth())),
                                                    static_cast<int>(std::ceil(p->getHeight())));
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    p->render(cr);
    cairo_destroy(cr);
    return s;
}

class StickyNoteTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        Document* doc = session->getDocument();
        PageRef page = doc->getPage(0);
        page->setBackgroundType(PageType(PageTypeFormat::Plain));
        // The page's ink: a line through where the note goes
        addStroke(page->getSelectedLayer(), INK, 3, {Point(50, 150), Point(400, 150)});
        // A note with a stroke that goes beyond its right edge and a text
        note = sticky::makeNote({{100, 100, 200, 150}, YELLOW, false});
        addStroke(note, BLUE, 2, {Point(120, 200), Point(350, 200)});
        addText(note, "Answer?", 110, 110);
        page->getLayers().push_back(note);  // (as the LayerController does, without its events)
        // A covering note further down
        cover = sticky::makeNote({{350, 400, 120, 80}, PINK, true});
        addStroke(cover, INK, 2, {Point(360, 420), Point(460, 470)});
        page->getLayers().push_back(cover);
        page->setSelectedLayerId(1);
    }
    fs::path path(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }

    /// The picture of the note document (any of the exports): the note hides the page's ink, its content is on it
    /// and clipped to it, the covering note has no folded corner (the screen only).
    void expectNotesDrawn(cairo_surface_t* s, const char* what) {
        EXPECT_TRUE(near(pixel(s, 70, 150), INK, 60)) << what << ": the page's ink beside the note " << str(pixel(s, 70, 150));
        EXPECT_TRUE(near(pixel(s, 200, 150), YELLOW)) << what << ": the note hides the ink below it "
                                                      << str(pixel(s, 200, 150));
        EXPECT_TRUE(near(pixel(s, 200, 130), YELLOW)) << what << ": the paper " << str(pixel(s, 200, 130));
        EXPECT_TRUE(near(pixel(s, 200, 200), BLUE, 60)) << what << ": the stroke on the note " << str(pixel(s, 200, 200));
        EXPECT_TRUE(near(pixel(s, 330, 200), Color(0xff, 0xff, 0xff), 8))
                << what << ": the stroke is clipped at the note's edge " << str(pixel(s, 330, 200));
        EXPECT_TRUE(near(pixel(s, 465, 404), PINK)) << what << ": no folded corner outside the screen "
                                                    << str(pixel(s, 465, 404));
        EXPECT_TRUE(near(pixel(s, 380, 460), PINK)) << what << ": the covering note " << str(pixel(s, 380, 460));
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    Layer* note = nullptr;
    Layer* cover = nullptr;
};
}  // namespace

TEST_F(StickyNoteTest, aNoteIsALayerWithItsPaperFirst) {
    ASSERT_TRUE(sticky::isNote(*note));
    const auto look = sticky::lookOf(*note);
    ASSERT_TRUE(look);
    EXPECT_EQ(look->rect.x, 100);
    EXPECT_EQ(look->rect.y, 100);
    EXPECT_EQ(look->rect.width, 200);
    EXPECT_EQ(look->rect.height, 150);
    EXPECT_EQ(look->color, YELLOW);
    EXPECT_FALSE(look->cover);
    EXPECT_TRUE(sticky::lookOf(*cover)->cover);
    EXPECT_EQ(note->getName(), "Sticky note");
    EXPECT_EQ(cover->getName(), "Sticky note (cover)");

    // A layer of that name without a paper is an ordinary layer; so is a paper without the name
    Layer plain;
    plain.setName("Sticky note");
    addStroke(&plain, INK, 1, {Point(0, 0), Point(10, 10)});
    EXPECT_FALSE(sticky::isNote(plain));
    std::unique_ptr<Layer> unnamed(sticky::makeNote({{0, 0, 50, 50}, YELLOW, false}));
    unnamed->setName("Layer 3");
    EXPECT_FALSE(sticky::isNote(*unnamed));

    PageRef page = session->getDocument()->getPage(0);
    EXPECT_EQ(sticky::noteAt(*page, 150, 120), note);
    EXPECT_EQ(sticky::noteAt(*page, 400, 450), cover);
    EXPECT_EQ(sticky::noteAt(*page, 50, 50), nullptr);
    cover->setVisible(false);
    EXPECT_EQ(sticky::noteAt(*page, 400, 450), nullptr) << "a hidden note is not there";
    EXPECT_TRUE(sticky::hasNotes(*page));
}

TEST_F(StickyNoteTest, savedAndLoadedTheNotesStayNotes) {
    ASSERT_TRUE(session->saveAs(path("notes.xopp")).ok);
    auto loaded = DocumentSession::loadFile(path("notes.xopp"));
    ASSERT_TRUE(loaded.document) << loaded.error;
    PageRef page = loaded.document->getPage(0);
    ASSERT_EQ(page->getLayerCount(), 3u);
    const Layer* n = page->getLayers()[1];
    const Layer* c = page->getLayers()[2];
    ASSERT_TRUE(sticky::isNote(*n));
    ASSERT_TRUE(sticky::isNote(*c));
    EXPECT_EQ(*sticky::lookOf(*n), *sticky::lookOf(*note));
    EXPECT_EQ(*sticky::lookOf(*c), *sticky::lookOf(*cover));
    ASSERT_EQ(n->getElementsView().size(), 3u) << "the paper, the stroke, the text";
    EXPECT_EQ(n->getElementsView()[1]->getType(), ELEMENT_STROKE);
    EXPECT_EQ(n->getElementsView()[2]->getType(), ELEMENT_TEXT);
    EXPECT_EQ(static_cast<const Text*>(n->getElementsView()[2])->getText(), "Answer?");
    EXPECT_FALSE(sticky::isNote(*page->getLayers()[0]));
}

TEST_F(StickyNoteTest, upstreamXournalppOpensTheFileAndShowsTheNotes) {
    const QString upstream = qEnvironmentVariableIsSet("XOJ_UPSTREAM_BIN") ? qEnvironmentVariable("XOJ_UPSTREAM_BIN")
                                                                          : QString(XQT_UPSTREAM_BIN);
    if (!QFileInfo(upstream).isExecutable()) {
        GTEST_SKIP() << "no upstream xournalpp at " << upstream.toStdString() << " (set XOJ_UPSTREAM_BIN)";
    }
    ASSERT_TRUE(session->saveAs(path("notes.xopp")).ok);
    QProcess p;
    // Its own configuration and caches: never the user's (AGENTS.md)
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char* var: {"XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME"}) {
        env.insert(var, tmp.filePath(QString("upstream-") + var));
    }
    p.setProcessEnvironment(env);
    p.start(upstream, {tmp.filePath("notes.xopp"), "--create-img=" + tmp.filePath("upstream.png"),
                       "--export-png-dpi=72"});
    ASSERT_TRUE(p.waitForFinished(60000)) << "upstream did not finish";
    const QString output = QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError());
    ASSERT_EQ(p.exitCode(), 0) << output.toStdString();
    EXPECT_FALSE(output.contains("error", Qt::CaseInsensitive)) << output.toStdString();
    cairo_surface_t* s = cairo_image_surface_create_from_png(tmp.filePath("upstream.png").toStdString().c_str());
    ASSERT_EQ(cairo_surface_status(s), CAIRO_STATUS_SUCCESS);
    EXPECT_TRUE(near(pixel(s, 70, 150), INK, 60)) << str(pixel(s, 70, 150));
    EXPECT_TRUE(near(pixel(s, 200, 150), YELLOW)) << "the note hides the ink below it there too " << str(pixel(s, 200, 150));
    EXPECT_TRUE(near(pixel(s, 200, 200), BLUE, 60)) << "the ink on the note " << str(pixel(s, 200, 200));
    EXPECT_TRUE(near(pixel(s, 380, 460), PINK)) << str(pixel(s, 380, 460));
    // (Upstream does not clip: the stroke goes on beyond the note's edge, as documented)
    EXPECT_TRUE(near(pixel(s, 330, 200), BLUE, 60)) << str(pixel(s, 330, 200));
    cairo_surface_destroy(s);
}

TEST_F(StickyNoteTest, pdfExportAndPrintDrawTheNotesClipped) {
    // (Printing prints this export)
    ExportHelper::exportPdf(session->getDocument(), path("export.pdf"), nullptr, nullptr, EXPORT_BACKGROUND_ALL, false);
    cairo_surface_t* s = renderPdf(path("export.pdf"), 0);
    expectNotesDrawn(s, "PDF export");
    cairo_surface_destroy(s);
}

TEST_F(StickyNoteTest, theHybridPdfShowsANoteAsAStampWithItsLook) {
    const auto r = HybridPdf::write(*session->getDocument(), path("notes.pdf"));
    ASSERT_TRUE(r.ok) << r.error;
    cairo_surface_t* s = renderPdf(path("notes.pdf"), 0);
    expectNotesDrawn(s, "hybrid PDF");
    cairo_surface_destroy(s);

    QPDF q;
    q.processFile(path("notes.pdf").string().c_str());
    auto pages = QPDFPageDocumentHelper(q).getAllPages();
    ASSERT_EQ(pages.size(), 1u);
    int stamps = 0;
    int ink = 0;
    for (auto& annot: pages[0].getObjectHandle().getKey("/Annots").aitems()) {
        const std::string subtype = annot.getKey("/Subtype").getName();
        const std::string name = annot.getKey("/NM").getUTF8Value();
        EXPECT_NE(subtype, "/Text") << "never a popup note";
        if (name == "xopp:p1-l2" || name == "xopp:p1-l3") {
            EXPECT_EQ(subtype, "/Stamp") << name;
            stamps++;
            if (name == "xopp:p1-l2") {
                // As big as the note (the stroke beyond its edge is not drawn)
                auto rect = annot.getKey("/Rect").getArrayAsRectangle();
                EXPECT_NEAR(rect.urx - rect.llx, 200, 6);
                EXPECT_NEAR(rect.ury - rect.lly, 150, 6);
            }
        } else if (subtype == "/Ink") {
            ink++;
        }
    }
    EXPECT_EQ(stamps, 2);
    EXPECT_EQ(ink, 1) << "the page's own layer";

    // And it opens as the same notes
    auto loaded = DocumentSession::loadFile(path("notes.pdf"));
    ASSERT_TRUE(loaded.document) << loaded.error;
    PageRef page = loaded.document->getPage(0);
    ASSERT_EQ(page->getLayerCount(), 3u);
    EXPECT_EQ(*sticky::lookOf(*page->getLayers()[1]), *sticky::lookOf(*note));
    EXPECT_EQ(*sticky::lookOf(*page->getLayers()[2]), *sticky::lookOf(*cover));
}

TEST_F(StickyNoteTest, theArchiveExportDrawsTheNotes) {
    const auto r = session->exportArchive(path("archive.pdf"));
    ASSERT_TRUE(r.ok) << r.error;
    cairo_surface_t* s = renderPdf(path("archive.pdf"), 0);
    expectNotesDrawn(s, "archive");
    cairo_surface_destroy(s);
}

TEST_F(StickyNoteTest, thumbnailsDrawTheNotesAsCovering) {
    // Thumbnails and previews draw with upstream's DocumentView (not for the screen): a peeking note covers there
    sticky::setPeeking(cover, true);
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 596, 842);
    cairo_t* cr = cairo_create(s);
    {
        DocumentView view;
        std::shared_lock lock(*session->getDocument());
        view.drawPage(session->getDocument()->getPage(0), cr, true);
    }
    cairo_destroy(cr);
    expectNotesDrawn(s, "thumbnail");
    cairo_surface_destroy(s);
    sticky::setPeeking(cover, false);
}

TEST_F(StickyNoteTest, aChangedLookMovesTheContentAndIsUndone) {
    Document* doc = session->getDocument();
    PageRef page = doc->getPage(0);
    const auto before = *sticky::lookOf(*note);
    auto after = before;
    after.rect.x += 50;
    after.rect.y += 20;
    const auto* content = static_cast<const Stroke*>(note->getElementsView()[1]);
    const double x0 = content->getPoint(0).x;
    sticky::changeLook(*doc, page, *note, before, after);
    session->getUndoRedoHandler()->addUndoAction(
            std::make_unique<sticky::NoteUndoAction>(page, note, before, after, "Move sticky note"));
    EXPECT_EQ(sticky::lookOf(*note)->rect.x, 150);
    EXPECT_EQ(content->getPoint(0).x, x0 + 50) << "the content went along";

    // Resized: the content stays where it is
    auto larger = after;
    larger.rect.width += 100;
    sticky::changeLook(*doc, page, *note, after, larger);
    EXPECT_EQ(content->getPoint(0).x, x0 + 50);
    sticky::changeLook(*doc, page, *note, larger, after);

    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(*sticky::lookOf(*note), before);
    EXPECT_EQ(content->getPoint(0).x, x0);
    session->getUndoRedoHandler()->redo();
    EXPECT_EQ(*sticky::lookOf(*note), after);

    // Color and cover
    auto pinkCover = after;
    pinkCover.color = PINK;
    pinkCover.cover = true;
    sticky::changeLook(*doc, page, *note, after, pinkCover);
    EXPECT_EQ(note->getName(), "Sticky note (cover)");
    EXPECT_EQ(sticky::lookOf(*note)->color, PINK);
}

TEST_F(StickyNoteTest, theSelectedLayerIsNeverANote) {
    Document* doc = session->getDocument();
    PageRef page = doc->getPage(0);
    page->setSelectedLayerId(3);  // (as Xournal++ and the undo of a layer leave it: the top one)
    EXPECT_TRUE(sticky::leaveNoteLayer(*doc, page));
    EXPECT_EQ(page->getSelectedLayerId(), 1u);
    EXPECT_FALSE(sticky::leaveNoteLayer(*doc, page)) << "nothing to change";

    // A page with nothing but a note: a layer for the page's ink comes below it
    auto only = std::make_shared<XojPage>(595, 842, true);
    only->getLayers().push_back(sticky::makeNote({{10, 10, 100, 100}, YELLOW, false}));
    only->setSelectedLayerId(1);
    EXPECT_TRUE(sticky::leaveNoteLayer(*doc, only));
    ASSERT_EQ(only->getLayerCount(), 2u);
    EXPECT_EQ(only->getSelectedLayerId(), 1u);
    EXPECT_FALSE(sticky::isNote(*only->getSelectedLayer()));
    EXPECT_TRUE(sticky::isNote(*only->getLayers()[1]));
}

TEST_F(StickyNoteTest, aNoteGoesThroughTheClipboardWhole) {
    std::string bytes;
    {
        std::shared_lock lock(*session->getDocument());
        bytes = sticky::serialize(*cover);
        EXPECT_TRUE(sticky::serialize(*session->getDocument()->getPage(0)->getLayers()[0]).empty())
                << "the page's own layer is no note";
    }
    ASSERT_FALSE(bytes.empty());
    std::unique_ptr<Layer> copy = sticky::deserialize(bytes.data(), bytes.size());
    ASSERT_TRUE(copy);
    EXPECT_EQ(*sticky::lookOf(*copy), *sticky::lookOf(*cover)) << "its place, color and cover";
    EXPECT_EQ(copy->getName(), sticky::COVER_LAYER_NAME);
    ASSERT_EQ(copy->getElementsView().size(), cover->getElementsView().size());
    const auto* ink = static_cast<const Stroke*>(copy->getElementsView()[1]);
    const auto* original = static_cast<const Stroke*>(cover->getElementsView()[1]);
    EXPECT_EQ(ink->getPointVector().size(), original->getPointVector().size());
    EXPECT_EQ(ink->getColor(), original->getColor());
    EXPECT_EQ(ink->getWidth(), original->getWidth());

    {
        std::shared_lock lock(*session->getDocument());
        bytes = sticky::serialize(*note);
    }
    copy = sticky::deserialize(bytes.data(), bytes.size());
    ASSERT_TRUE(copy);
    ASSERT_EQ(copy->getElementsView().size(), 3u);
    ASSERT_EQ(copy->getElementsView()[2]->getType(), ELEMENT_TEXT);
    EXPECT_EQ(static_cast<const Text*>(copy->getElementsView()[2])->getText(), "Answer?");

    EXPECT_FALSE(sticky::deserialize("nonsense", 8)) << "not a note";
    EXPECT_FALSE(sticky::deserialize(bytes.data(), bytes.size() / 2)) << "cut short";
}

TEST_F(StickyNoteTest, aPastedNoteStaysWhereItWasWhenItFitsOnThePage) {
    using R = xoj::util::Rectangle<double>;
    const auto same = [](const R& a, const R& b) {
        return std::abs(a.x - b.x) < 1e-9 && std::abs(a.y - b.y) < 1e-9 && std::abs(a.width - b.width) < 1e-9 &&
               std::abs(a.height - b.height) < 1e-9;
    };
    // Fits: the same place
    EXPECT_TRUE(same(sticky::pastePlace({100, 100, 200, 150}, 595, 842, {}), R(100, 100, 200, 150)));
    // Beyond a smaller page: moved inside it, its size kept
    EXPECT_TRUE(same(sticky::pastePlace({500, 800, 200, 150}, 595, 842, {}), R(395, 692, 200, 150)));
    // Larger than the page: as large as the page
    EXPECT_TRUE(same(sticky::pastePlace({10, 10, 700, 150}, 595, 842, {}), R(0, 10, 595, 150)));
    // Exactly on a note there (its original): a little further down and right, and further for the next copy
    const R first = sticky::pastePlace({100, 100, 200, 150}, 595, 842, {R(100, 100, 200, 150)});
    EXPECT_TRUE(same(first, R(116, 116, 200, 150)));
    EXPECT_TRUE(same(sticky::pastePlace({100, 100, 200, 150}, 595, 842, {R(100, 100, 200, 150), first}),
                     R(132, 132, 200, 150)));
    // In the bottom right corner: up and left instead
    EXPECT_TRUE(same(sticky::pastePlace({395, 692, 200, 150}, 595, 842, {R(395, 692, 200, 150)}),
                     R(379, 676, 200, 150)));
}
