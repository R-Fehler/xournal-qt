/*
 * xournal-qt: a document's annotations collected (qt/docs/annotations-md.md) - the highlighter over PDF text with the
 * text under it, a highlight annotation of the PDF itself, text and Markdown boxes, handwriting in the margin (not
 * the ink over the text), a link marker - and written as Markdown whose links lead back to their pages.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QRegularExpression>
#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <poppler.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentLink.h"
#include "session/DocumentSession.h"
#include "session/DocumentTextIndex.h"
#include "shell/Annotations.h"
#include "shell/DocumentLinks.h"

using namespace xqt;
namespace an = xqt::annotations;

namespace {
/// A two-page PDF with text and an outline ("Filtering" on page 1, "Update" on page 2); on page 2 a highlight
/// annotation made in another app over its first line, with the note "Check the gain".
fs::path makePdf(const QTemporaryDir& dir) {
    const fs::path plain = fs::path(dir.filePath("plain.pdf").toStdString());
    cairo_surface_t* surface = cairo_pdf_surface_create(plain.c_str(), 595, 842);
    cairo_t* cr = cairo_create(surface);
    cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);
    const char* lines[2][2] = {{"Kalman filters estimate the hidden state.", "Noise is modelled as Gaussian."},
                               {"The update step weighs the measurement.", "Covariance shrinks with each update."}};
    for (auto& page: lines) {
        cairo_move_to(cr, 60, 100);
        cairo_show_text(cr, page[0]);
        cairo_move_to(cr, 60, 130);
        cairo_show_text(cr, page[1]);
        cairo_show_page(cr);
    }
    cairo_pdf_surface_add_outline(surface, CAIRO_PDF_OUTLINE_ROOT, "Filtering", "page=1", CAIRO_PDF_OUTLINE_FLAG_OPEN);
    cairo_pdf_surface_add_outline(surface, CAIRO_PDF_OUTLINE_ROOT, "Update", "page=2", CAIRO_PDF_OUTLINE_FLAG_OPEN);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    // The highlight annotation (PDF space: y upwards; the first line's baseline is at 842 - 100)
    const fs::path file = fs::path(dir.filePath("lecture.pdf").toStdString());
    gchar* uri = g_filename_to_uri(plain.c_str(), nullptr, nullptr);
    PopplerDocument* doc = poppler_document_new_from_file(uri, nullptr, nullptr);
    g_free(uri);
    EXPECT_NE(doc, nullptr);
    PopplerPage* page = poppler_document_get_page(doc, 1);
    PopplerRectangle rect{55, 736, 420, 756};
    GArray* quads = g_array_new(FALSE, FALSE, sizeof(PopplerQuadrilateral));
    PopplerQuadrilateral q{{55, 756}, {420, 756}, {55, 736}, {420, 736}};
    g_array_append_val(quads, q);
    PopplerAnnot* annot = poppler_annot_text_markup_new_highlight(doc, &rect, quads);
    g_array_unref(quads);
    poppler_annot_set_contents(annot, "Check the gain");
    PopplerColor green{0, 0xffff, 0};
    poppler_annot_set_color(annot, &green);
    poppler_page_add_annot(page, annot);
    g_object_unref(annot);
    gchar* out = g_filename_to_uri(file.c_str(), nullptr, nullptr);
    EXPECT_TRUE(poppler_document_save(doc, out, nullptr));
    g_free(out);
    g_object_unref(page);
    g_object_unref(doc);
    return file;
}

Stroke* stroke(Layer* layer, StrokeTool tool, double width, std::initializer_list<QPointF> points,
               uint32_t color = 0x000000) {
    auto s = std::make_unique<Stroke>();
    s->setToolType(tool);
    s->setWidth(width);
    s->setColor(Color(color | 0xff000000U));
    for (const QPointF& p: points) {
        s->addPoint(Point(p.x(), p.y()));
    }
    Stroke* raw = s.get();
    layer->addElement(std::move(s));
    return raw;
}
void text(Layer* layer, const std::string& content, double x, double y) {
    auto t = std::make_unique<Text>();
    t->setText(content);
    t->setFont(XojFont("Sans", 10));
    t->move(x, y);
    layer->addElement(std::move(t));
}

/// The PDF annotated here: on page 1 the highlighter over the first line, a text box, two pen strokes in the right
/// margin (one note) and one through the second line (a mark, not a note); on page 2 a Markdown box and a link
/// marker.
std::unique_ptr<Document> makeDocument(const fs::path& pdf) {
    auto loaded = DocumentSession::loadFile(pdf);
    EXPECT_TRUE(loaded.document) << loaded.error;
    if (!loaded.document) {
        return nullptr;
    }
    Document& doc = *loaded.document;
    EXPECT_EQ(doc.getPageCount(), 2u);
    {
        PageRef p = doc.getPage(0);
        Layer* layer = p->getSelectedLayer();
        stroke(layer, StrokeTool::HIGHLIGHTER, 12, {{58, 96}, {420, 96}}, 0xffff00);
        text(layer, "Ask about the Q matrix", 60, 300);
        stroke(layer, StrokeTool::PEN, 1.4, {{520, 400}, {540, 410}, {560, 400}});
        stroke(layer, StrokeTool::PEN, 1.4, {{522, 418}, {570, 420}});
        stroke(layer, StrokeTool::PEN, 1.4, {{60, 126}, {200, 126}});
    }
    {
        PageRef p = doc.getPage(1);
        auto* md = new Layer();
        md->setName(std::string(xoj::markdown::LAYER_NAME));
        p->getLayers().push_back(md);  // (the page owns it)
        text(md, "**Summary**: the gain balances model and measurement.", 60, 300);
        text(md, "[Kalman notes](notes.xopp#page=2)", 60, 400);
    }
    return std::move(loaded.document);
}

std::vector<an::Item> ofKind(const std::vector<an::Item>& items, an::Kind kind) {
    std::vector<an::Item> out;
    for (const auto& i: items) {
        if (i.kind == kind) {
            out.push_back(i);
        }
    }
    return out;
}
}  // namespace

TEST(Annotations, collectsHighlightsBoxesHandwritingAndLinks) {
    QTemporaryDir tmp;
    const fs::path pdf = makePdf(tmp);
    auto doc = makeDocument(pdf);
    ASSERT_TRUE(doc);
    PdfLayoutReader reader(pdf);
    const auto items = an::collect(*doc, &reader);

    const auto highlights = ofKind(items, an::Kind::Highlight);
    ASSERT_EQ(highlights.size(), 1u);
    EXPECT_EQ(highlights[0].page, 0u);
    EXPECT_EQ(highlights[0].text, "Kalman filters estimate the hidden state.") << "the text under it, one line only";
    EXPECT_EQ(highlights[0].color, 0xffff00u);

    const auto pdfHighlights = ofKind(items, an::Kind::PdfHighlight);
    ASSERT_EQ(pdfHighlights.size(), 1u) << "the PDF's own highlight is read";
    EXPECT_EQ(pdfHighlights[0].page, 1u);
    EXPECT_EQ(pdfHighlights[0].text, "The update step weighs the measurement.");
    EXPECT_EQ(pdfHighlights[0].comment, "Check the gain");
    EXPECT_EQ(pdfHighlights[0].color, 0x00ff00u);

    const auto texts = ofKind(items, an::Kind::Text);
    ASSERT_EQ(texts.size(), 1u);
    EXPECT_EQ(texts[0].text, "Ask about the Q matrix");
    EXPECT_EQ(texts[0].page, 0u);

    const auto boxes = ofKind(items, an::Kind::Markdown);
    ASSERT_EQ(boxes.size(), 1u);
    EXPECT_EQ(boxes[0].text, "**Summary**: the gain balances model and measurement.");
    EXPECT_EQ(boxes[0].page, 1u);

    const auto links = ofKind(items, an::Kind::Link);
    ASSERT_EQ(links.size(), 1u) << "a Markdown box that is only a link is a link";
    EXPECT_EQ(links[0].text, "Kalman notes");
    EXPECT_EQ(links[0].target, "notes.xopp#page=2");

    const auto ink = ofKind(items, an::Kind::Ink);
    ASSERT_EQ(ink.size(), 1u) << "the margin note is one piece; the stroke through the text is a mark";
    EXPECT_EQ(ink[0].page, 0u);
    EXPECT_GE(ink[0].rect.left(), 515);
    EXPECT_LE(ink[0].rect.top(), 400);
    EXPECT_GE(ink[0].rect.bottom(), 419);

    // Reading order: page by page, top to bottom
    for (size_t i = 1; i < items.size(); ++i) {
        EXPECT_TRUE(items[i - 1].page < items[i].page ||
                    (items[i - 1].page == items[i].page && items[i - 1].rect.top() <= items[i].rect.top() + 4));
    }
}

TEST(Annotations, handwritingWrittenApartIsTwoPieces) {
    an::PageContent c;
    c.width = 595;
    c.height = 842;
    // Written one after the other: two strokes of a word, far away another word, then back to the first: a dot over it
    c.ink = {QRectF(500, 100, 20, 10), QRectF(525, 102, 20, 10), QRectF(500, 600, 30, 12),
             QRectF(540, 101, 10, 10)};
    const auto items = an::itemsOf(c, 0, nullptr);
    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0].rect, QRectF(500, 100, 50, 12)) << "a late stroke over the first word joins it";
    EXPECT_EQ(items[1].rect, QRectF(500, 600, 30, 12));
}

TEST(Annotations, markdownWithLinksBackToThePlaces) {
    QTemporaryDir tmp;
    const fs::path pdf = makePdf(tmp);
    auto doc = makeDocument(pdf);
    ASSERT_TRUE(doc);
    const fs::path xopp = fs::path(tmp.filePath("Lectures/lecture one.xopp").toStdString());
    fs::create_directories(xopp.parent_path());
    PdfLayoutReader reader(pdf);
    const auto items = an::collect(*doc, &reader);

    an::ExportInput input;
    input.document = xopp;
    input.title = "lecture one";
    input.chapters = an::chaptersOf(*doc);
    input.pages = DocumentLinks::pagesOf(*doc);
    ASSERT_EQ(input.chapters.size(), 2u) << "the PDF's outline";
    const fs::path mdFile = fs::path(tmp.filePath("Notes/lecture one.annotations.md").toStdString());
    const QString md = QString::fromStdString(an::markdown(items, input, mdFile));

    EXPECT_TRUE(md.startsWith("# Annotations: lecture one\n")) << md.toStdString();
    EXPECT_TRUE(md.contains("[lecture one.xopp](../Lectures/lecture%20one.xopp)")) << "the document, relative";
    EXPECT_TRUE(md.contains("\n## Filtering\n")) << md.toStdString();
    EXPECT_TRUE(md.contains("\n## Update\n"));
    EXPECT_LT(md.indexOf("## Filtering"), md.indexOf("Kalman filters estimate"));
    EXPECT_LT(md.indexOf("## Update"), md.indexOf("The update step"));
    EXPECT_TRUE(md.contains("> Kalman filters estimate the hidden state. ([p. 1]("));
    EXPECT_TRUE(md.contains(">\n> Check the gain\n")) << "the PDF highlight's note under it";
    EXPECT_TRUE(md.contains("- Ask about the Q matrix ([p. 1]("));
    EXPECT_TRUE(md.contains("> **Summary**: the gain balances model and measurement."));
    EXPECT_TRUE(md.contains("- [Kalman notes](../Lectures/notes.xopp#page=2)")) << "relinked from the Notes folder";
    EXPECT_TRUE(md.contains("(handwriting)")) << "no pictures asked for";

    // Every link leads back: it parses, points at the document, and its page is the item's
    const QRegularExpression placeLink(QStringLiteral(R"(\[p\. (\d+)\]\(([^)]+)\))"));
    int placeLinks = 0;
    for (auto it = placeLink.globalMatch(md); it.hasNext();) {
        const auto m = it.next();
        const auto link = links::parse(m.captured(2));
        ASSERT_TRUE(link.has_value()) << m.captured(2).toStdString();
        EXPECT_EQ(links::resolvePath(mdFile.parent_path(), link->path).lexically_normal(), xopp.lexically_normal());
        EXPECT_EQ(links::resolve(*link, {}, input.pages).page + 1, m.captured(1).toInt()) << m.captured(2).toStdString();
        ++placeLinks;
    }
    EXPECT_EQ(placeLinks, static_cast<int>(items.size())) << "one per item";

    // With pictures: the handwriting as an image in "<name>.assets/"
    input.inkImages = true;
    std::vector<an::Picture> pictures;
    const QString withPictures = QString::fromStdString(an::markdown(items, input, mdFile, &pictures));
    ASSERT_EQ(pictures.size(), 1u);
    EXPECT_EQ(pictures[0].file, "lecture one.annotations.assets/p1-01.png");
    EXPECT_EQ(items[pictures[0].item].kind, an::Kind::Ink);
    EXPECT_TRUE(withPictures.contains("](lecture%20one.annotations.assets/p1-01.png)")) << withPictures.toStdString();
    EXPECT_EQ(an::assetsFolder(mdFile).filename(), "lecture one.annotations.assets");
}

TEST(Annotations, markdownEscapesPlainText) {
    an::Item item;
    item.kind = an::Kind::Text;
    item.text = "# not a heading\n1. not a list * [x] $5";
    an::ExportInput input;
    input.document = "/tmp/d.xopp";
    input.title = "d";
    input.pages = {links::Page{}};
    const std::string md = an::markdown({item}, input, "/tmp/d.annotations.md");
    EXPECT_NE(md.find("- \\# not a heading\n  1\\. not a list \\* \\[x\\] \\$5 ([p. 1](d.xopp#page=1))"), std::string::npos)
            << md;
    EXPECT_NE(md.find("\n## Page 1\n"), std::string::npos) << "a heading per page without chapters";
}
