/*
 * xournal-qt: sticky notes (qt/docs/sticky-notes.md): the file format (a layer per note, its paper first), a save and
 * load round trip, upstream Xournal++ opening the file, and the note drawn with its content clipped to it in every
 * export (PDF, print, hybrid PDF, archive, thumbnails), without the marks that only the screen shows.
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <cairo-pdf.h>
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
#include "session/DocumentTextIndex.h"
#include "session/StickyNote.h"
#include "util/Matrix.h"
#include "MdBox.h"
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

/// A page of a PDF as poppler draws it (annotations too), white behind, `scale` px per point.
cairo_surface_t* renderPdf(const fs::path& pdf, size_t page, double scale = 1) {
    XojPdfDocument doc;
    GError* error = nullptr;
    EXPECT_TRUE(doc.load(pdf, "", &error)) << pdf;
    if (error) {
        g_error_free(error);
    }
    XojPdfPageSPtr p = doc.getPage(page);
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                    static_cast<int>(std::ceil(p->getWidth() * scale)),
                                                    static_cast<int>(std::ceil(p->getHeight() * scale)));
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_scale(cr, scale, scale);
    p->render(cr);
    cairo_destroy(cr);
    return s;
}

/// A page's picture as thumbnails and previews draw it (upstream's DocumentView), white behind, `scale` px per point
cairo_surface_t* renderPage(Document& doc, size_t page, double scale) {
    std::shared_lock lock(doc);
    PageRef p = doc.getPage(page);
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, static_cast<int>(p->getWidth() * scale),
                                                    static_cast<int>(p->getHeight() * scale));
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_scale(cr, scale, scale);
    DocumentView().drawPage(p, cr, true);
    cairo_destroy(cr);
    return s;
}

/// A note of each color of the palette in a row, with a stroke on each; their rectangles
std::vector<xoj::util::Rectangle<double>> addPalette(XojPage& page) {
    std::vector<xoj::util::Rectangle<double>> rects;
    const auto& colors = sticky::presetColors();
    for (size_t i = 0; i < colors.size(); ++i) {
        const xoj::util::Rectangle<double> r(40 + 105.0 * i, 60, 90, 70);
        Layer* n = sticky::makeNote({r, colors[i], false});
        addStroke(n, BLUE, 1.5, {Point(r.x + 10, r.y + 20), Point(r.x + 60, r.y + 30), Point(r.x + 80, r.y + 20)});
        page.getLayers().push_back(n);
        rects.push_back(r);
    }
    return rects;
}

/// The palette's notes in a picture at 4 px per point: each paper has its color, its edge is a darker shade of it
/// (not black), and below each note lies a light shade (the page there: `ground`).
void expectPaperLook(cairo_surface_t* s, const std::vector<xoj::util::Rectangle<double>>& rects, Color ground,
                     const char* what) {
    constexpr int SCALE = 4;
    const auto& colors = sticky::presetColors();
    for (size_t i = 0; i < rects.size(); ++i) {
        const auto& r = rects[i];
        const Color paper = colors[i];
        const Color edge = sticky::edgeColor(paper);
        const std::string at = std::string(what) + ", note " + std::to_string(i) + ": ";
        const int midY = static_cast<int>((r.y + r.height / 2) * SCALE);
        const int midX = static_cast<int>((r.x + r.width / 2) * SCALE);
        EXPECT_TRUE(near(pixel(s, static_cast<int>((r.x + 5) * SCALE), midY), paper)) << at << "the paper";
        // The edge lies on the rectangle's border (0.8 pt wide: 3 px here); the left, right, top and bottom ones
        const std::vector<std::pair<int, int>> onEdge{{static_cast<int>(r.x * SCALE), midY},
                                                      {static_cast<int>((r.x + r.width) * SCALE), midY},
                                                      {midX, static_cast<int>(r.y * SCALE)},
                                                      {midX, static_cast<int>((r.y + r.height) * SCALE)}};
        for (const auto& [x, y]: onEdge) {
            const Rgb e = pixel(s, x, y);
            EXPECT_TRUE(near(e, edge, 14)) << at << "the edge's color at " << x << "," << y << ": " << str(e);
            EXPECT_TRUE(e.r <= paper.red && e.g <= paper.green && e.b <= paper.blue &&
                        e.r + e.g + e.b < paper.red + paper.green + paper.blue - 60)
                    << at << "the edge is darker than the paper " << str(e);
            EXPECT_GT(e.r + e.g + e.b, 3 * 90) << at << "and not black " << str(e);
        }
        // The shade: below the note (a little), not above it
        const Rgb below = pixel(s, midX, static_cast<int>((r.y + r.height + 1.5) * SCALE));
        const Rgb above = pixel(s, midX, static_cast<int>((r.y - 2) * SCALE));
        EXPECT_TRUE(below.r < ground.red - 8 && below.r > ground.red - 60) << at << "a light shade below " << str(below);
        EXPECT_TRUE(near(above, ground, 3)) << at << "nothing above " << str(above);
    }
}

/// Where the pictures of the look test go when XQT_STICKY_SHOTS is set (to look at them)
void saveShot(cairo_surface_t* s, const char* name) {
    if (const char* dir = std::getenv("XQT_STICKY_SHOTS")) {
        cairo_surface_write_to_png(s, (fs::path(dir) / name).string().c_str());
    }
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
    // (qt/sticky-containers) the note's Markdown text: Xournal++ shows its source, wrapped at the note's width
    {
        const auto look = *sticky::lookOf(*note);
        auto t = std::make_unique<Text>();
        t->setText("Upstream **shows** this");
        t->setFont(XojFont("Sans", 10));
        t->setColor(INK);
        t->setWrap(sticky::textWidth(look));
        t->setTransformation(xoj::util::Matrix::TRANSLATION(sticky::textOrigin(look).x, sticky::textOrigin(look).y));
        note->addElement(std::move(t));
        ASSERT_NE(sticky::textOf(*note), nullptr);
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

// qt/sticky-containers: the note's one Markdown text (qt/docs/sticky-notes.md, "Notes as containers")
TEST_F(StickyNoteTest, aNotesMarkdownTextIsToldByItsPlaceAndFlowsInTheNotesWidth) {
    const auto look = *sticky::lookOf(*note);
    const auto origin = sticky::textOrigin(look);
    EXPECT_EQ(sticky::textOf(*note), nullptr) << "the plain text \"Answer?\" is no Markdown text (no wrap width)";
    auto t = std::make_unique<Text>();
    t->setText("# Title\n\n**Keys** are the words that a sentence needs to wrap over several lines of this note");
    t->setFont(XojFont("Sans", 10));
    t->setColor(INK);
    t->setWrap(sticky::textWidth(look));
    t->setTransformation(xoj::util::Matrix::TRANSLATION(origin.x, origin.y));
    Text* text = t.get();
    note->addElement(std::move(t));
    EXPECT_TRUE(text->isMarkdown()) << "flagged when it comes into the note's layer";
    EXPECT_EQ(sticky::textOf(*note), text);
    EXPECT_FALSE(static_cast<const Text*>(note->getElementsView()[2])->isMarkdown()) << "the plain text stays plain";
    // Elsewhere on the note, or on an ordinary layer, a text with a wrap width is plain
    auto other = text->cloneText();
    other->setTransformation(xoj::util::Matrix::TRANSLATION(origin.x + 30, origin.y + 40));
    EXPECT_FALSE(sticky::isNoteText(*note, *other));
    Layer plain;
    auto third = text->cloneText();
    Text* onPlain = third.get();
    plain.addElement(std::move(third));
    EXPECT_FALSE(onPlain->isMarkdown());

    // Narrower: its width follows (more lines); moved: it goes along and stays the note's text; renamed (cover): too
    const double height = md::contentHeight(*text);
    sticky::Look narrow = look;
    narrow.rect.width = 120;
    sticky::applyLook(*note, look, narrow);
    EXPECT_NEAR(text->getWrap(), 120 - 2 * sticky::TEXT_PADDING, 1e-9);
    EXPECT_GT(md::contentHeight(*text), height + 5);
    sticky::Look moved = narrow;
    moved.rect.x += 40;
    moved.rect.y += 30;
    moved.cover = true;
    sticky::applyLook(*note, narrow, moved);
    EXPECT_TRUE(text->isMarkdown()) << "renamed to a covering note: still its text";
    EXPECT_EQ(sticky::textOf(*note), text);
    EXPECT_NEAR(text->getTransformation().shift.x, origin.x + 40, 1e-9);
    sticky::applyLook(*note, moved, look);
    EXPECT_NEAR(text->getWrap(), sticky::textWidth(look), 1e-9);

    // Saved and loaded: the note's text again (a Markdown text, its width); the text index shows it as drawn
    ASSERT_TRUE(session->saveAs(path("text.xopp")).ok);
    auto loaded = DocumentSession::loadFile(path("text.xopp"));
    ASSERT_TRUE(loaded.document) << loaded.error;
    const Layer* n = loaded.document->getPage(0)->getLayers()[1];
    const Text* again = sticky::textOf(*n);
    ASSERT_NE(again, nullptr);
    EXPECT_TRUE(again->isMarkdown());
    EXPECT_EQ(again->getText(), text->getText());
    EXPECT_NEAR(again->getWrap(), sticky::textWidth(look), 1e-6);
    bool shown = false;
    for (const ElementText& piece: elementTexts(*loaded.document->getPage(0))) {
        shown = shown || (piece.element == again && piece.shown.startsWith("Keys are"));
    }
    EXPECT_TRUE(shown) << "searched as it is shown";
    // Pictures in it are carried with the file (qt/md-images) like those of the page's Markdown texts
    EXPECT_EQ(md::boxesOf(*loaded.document->getPage(0)).size(), 1u);
    EXPECT_TRUE(md::holdsBoxes(*n));
    EXPECT_FALSE(md::holdsBoxes(*loaded.document->getPage(0)->getLayers()[0]));
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
                // (and its shadow: sticky::DRAWN_MARGIN, a few points around it)
                EXPECT_NEAR(rect.urx - rect.llx, 200, 14);
                EXPECT_NEAR(rect.ury - rect.lly, 150, 14);
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

// --- the look: a darker edge and a soft shade (qt/sticky-look) -----------------------------------------------------

TEST_F(StickyNoteTest, everyColorHasADarkerEdgeAndAShadeInEveryPicture) {
    // On a white page: the PDF export (print, the archive: the same drawing), and a thumbnail
    auto page = std::make_shared<XojPage>(595, 300, true);
    page->setBackgroundType(PageType(PageTypeFormat::Plain));
    const auto rects = addPalette(*page);
    session->getDocument()->addPage(page);
    const size_t last = session->getDocument()->getPageCount() - 1;
    ExportHelper::exportPdf(session->getDocument(), path("palette.pdf"), nullptr, nullptr, EXPORT_BACKGROUND_ALL,
                            false);
    cairo_surface_t* s = renderPdf(path("palette.pdf"), last, 4);
    saveShot(s, "palette-white-export.png");
    expectPaperLook(s, rects, Color(0xff, 0xff, 0xff), "PDF export");
    cairo_surface_destroy(s);
    s = renderPage(*session->getDocument(), last, 4);
    saveShot(s, "palette-white-thumbnail.png");
    expectPaperLook(s, rects, Color(0xff, 0xff, 0xff), "thumbnail");
    cairo_surface_destroy(s);

    // The hybrid PDF (the note is its annotation's appearance)
    ASSERT_TRUE(HybridPdf::write(*session->getDocument(), path("palette.notes.pdf")).ok);
    s = renderPdf(path("palette.notes.pdf"), last, 4);
    saveShot(s, "palette-white-hybrid.png");
    expectPaperLook(s, rects, Color(0xff, 0xff, 0xff), "hybrid PDF");
    cairo_surface_destroy(s);

    // On a page of a PDF (light gray there, with lines of "text")
    const fs::path pdf = path("base.pdf");
    {
        cairo_surface_t* base = cairo_pdf_surface_create(pdf.string().c_str(), 595, 300);
        cairo_t* cr = cairo_create(base);
        cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
        cairo_paint(cr);
        cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
        for (double y = 20; y < 300; y += 14) {
            cairo_rectangle(cr, 20, y, 555, 3);
        }
        cairo_fill(cr);
        cairo_destroy(cr);
        cairo_surface_destroy(base);
    }
    auto loaded = DocumentSession::loadFile(pdf);
    ASSERT_TRUE(loaded.document) << loaded.error;
    DocumentSession onPdf(*app, std::move(loaded.document));
    std::vector<xoj::util::Rectangle<double>> onPdfRects;
    {
        std::unique_lock lock(*onPdf.getDocument());
        onPdfRects = addPalette(*onPdf.getDocument()->getPage(0));
    }
    ExportHelper::exportPdf(onPdf.getDocument(), path("palette-pdf.pdf"), nullptr, nullptr, EXPORT_BACKGROUND_ALL,
                            false);
    s = renderPdf(path("palette-pdf.pdf"), 0, 4);
    saveShot(s, "palette-pdf-export.png");
    // (the shade is checked between the lines of text: 1.5 pt below each note is gray paper there)
    expectPaperLook(s, onPdfRects, Color(0xe6, 0xe6, 0xe6), "on a PDF page");
    cairo_surface_destroy(s);
}

TEST_F(StickyNoteTest, theLookIsDrawnNotSaved) {
    // Drawn (everywhere), saved, loaded: the note is still its paper and its content, the paper as it was
    cairo_surface_destroy(renderPage(*session->getDocument(), 0, 1));
    ASSERT_TRUE(HybridPdf::write(*session->getDocument(), path("look.notes.pdf")).ok);
    ASSERT_TRUE(session->saveAs(path("look.xopp")).ok);
    for (const char* file: {"look.xopp", "look.notes.pdf"}) {
        auto loaded = DocumentSession::loadFile(path(file));
        ASSERT_TRUE(loaded.document) << file << ": " << loaded.error;
        PageRef page = loaded.document->getPage(0);
        ASSERT_EQ(page->getLayerCount(), 3u) << file;
        const Layer* n = page->getLayers()[1];
        ASSERT_EQ(n->getElementsView().size(), 3u) << file << ": the paper, the stroke, the text: nothing added";
        const auto* paper = static_cast<const Stroke*>(n->getElementsView()[0]);
        EXPECT_EQ(paper->getColor(), YELLOW) << file;
        EXPECT_DOUBLE_EQ(paper->getWidth(), sticky::PAPER_WIDTH) << file << ": the outline as upstream draws it";
        EXPECT_EQ(paper->getFill(), 255) << file;
        EXPECT_EQ(paper->getPointCount(), 5u) << file;
    }
}

/// What the edge and the shadow cost (XQT_BENCH_STICKY=1): a page with 1, 5 and 20 notes, each with 30 strokes of
/// handwriting, drawn flat (the paper as upstream draws it), with the edge, and with the edge and the shadow: the
/// whole page at 2 px per point (a screen), a thumbnail (0.25 px per point), and the area of a stroke being written
/// on a note (20 x 20 pt: what is drawn again while writing).
TEST_F(StickyNoteTest, benchmarkTheLook) {
    if (!std::getenv("XQT_BENCH_STICKY")) {
        GTEST_SKIP() << "a benchmark: set XQT_BENCH_STICKY=1";
    }
    struct Case {
        const char* name;
        double scale;
        double x, y, w, h;  // (the area drawn, points)
        int reps;
    };
    const std::vector<Case> cases{{"screen page", 2, 0, 0, 595, 842, 10},
                                  {"thumbnail", 0.25, 0, 0, 595, 842, 60},
                                  {"stroke area", 2, 60, 60, 20, 20, 2000}};
    const std::vector<std::pair<const char*, sticky::Finish>> finishes{
            {"flat", sticky::Finish::Flat}, {"edge", sticky::Finish::Edge}, {"edge+shadow", sticky::Finish::Full}};
    for (const int count: {1, 5, 20}) {
        auto page = std::make_shared<XojPage>(595, 842, true);
        page->setBackgroundType(PageType(PageTypeFormat::Plain));
        for (int i = 0; i < count; ++i) {
            const double x = 20 + (i % 4) * 142.0;
            const double y = 20 + (i / 4) * 162.0;
            Layer* n = sticky::makeNote({{x, y, 130, 150}, sticky::presetColors()[i % 5], i % 3 == 2});
            for (int k = 0; k < 30; ++k) {
                std::vector<Point> pts;
                for (int j = 0; j < 40; ++j) {
                    pts.emplace_back(x + 5 + j * 3, y + 10 + k * 4.5 + 2 * std::sin(j * 0.8 + k), 0.5);
                }
                addStroke(n, BLUE, 1.2, pts);
            }
            page->getLayers().push_back(n);
        }
        for (const Case& c: cases) {
            cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, static_cast<int>(c.w * c.scale),
                                                            static_cast<int>(c.h * c.scale));
            cairo_t* cr = cairo_create(s);
            cairo_scale(cr, c.scale, c.scale);
            cairo_translate(cr, -c.x, -c.y);
            cairo_rectangle(cr, c.x, c.y, c.w, c.h);
            cairo_clip(cr);
            std::vector<double> best(finishes.size(), 1e9);
            for (int round = 0; round < 5; ++round) {
                for (size_t f = 0; f < finishes.size(); ++f) {
                    sticky::setFinish(finishes[f].second);
                    const auto t0 = std::chrono::steady_clock::now();
                    for (int r = 0; r < c.reps; ++r) {
                        DocumentView().drawPage(page, cr, true);
                    }
                    const double ms =
                            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() /
                            c.reps;
                    best[f] = std::min(best[f], ms);
                }
            }
            cairo_destroy(cr);
            cairo_surface_destroy(s);
            std::printf("%2d notes, %-11s:", count, c.name);
            for (size_t f = 0; f < finishes.size(); ++f) {
                std::printf("  %s %.3f ms (%+.1f %%)", finishes[f].first, best[f], 100 * (best[f] / best[0] - 1));
            }
            std::printf("\n");
        }
    }
    sticky::setFinish(sticky::Finish::Full);
}
