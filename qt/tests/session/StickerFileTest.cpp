/*
 * xournal-qt: a sticker's file (qt/docs/features/stickers.md): one page of the content's size plus the margin, on plain
 * paper of the source page's colour, the layers as the app keeps them (the picture, Markdown, the ink, the notes), read
 * back as the content to paste, opened by upstream's loader without a warning; the name suggested for it.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>
#include <string>

#include <QTemporaryDir>
#include <cairo.h>
#include <gtest/gtest.h>

#include "control/xojfile/LoadHandler.h"
#include "model/Document.h"
#include "model/Font.h"
#include "model/Image.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/StickerFile.h"
#include "session/StickyNote.h"

using namespace xqt;
using xoj::util::Rectangle;

namespace {

ElementPtr stroke(double x0, double y0, double x1, double y1) {
    auto s = std::make_unique<Stroke>();
    s->setToolType(StrokeTool::PEN);
    s->setColor(Color(0x10, 0x20, 0xc0));
    s->setWidth(2);
    s->addPoint(Point(x0, y0));
    s->addPoint(Point(x1, y1));
    return s;
}

ElementPtr text(const std::string& content, double x, double y, bool markdown) {
    auto t = std::make_unique<Text>();
    t->setText(content);
    t->setFont(XojFont("Sans", 12));
    t->move(x, y);
    if (markdown) {
        t->setWrap(200);
        t->setMarkdown(true);
    }
    return t;
}

/// Content as the canvas copies it: a stroke, a text, a Markdown box and a note, around (100, 200)
sticky::Group sample() {
    sticky::Group g;
    g.elements.push_back(stroke(100, 200, 180, 260));
    g.markdown.push_back(false);
    g.elements.push_back(text("Hello there", 110, 270, false));
    g.markdown.push_back(false);
    g.elements.push_back(text("# Heading\nbody", 120, 300, true));
    g.markdown.push_back(true);
    g.notes.emplace_back(sticky::makeNote({{200, 210, 80, 60}, sticky::presetColors()[0], false}));
    std::optional<Rectangle<double>> b;
    for (const auto& e: g.elements) {
        const auto r = e->getBoundingBox();
        b ? b->unite(r) : void(b = r);
    }
    b->unite(Rectangle<double>(200, 210, 80, 60));
    g.bounds = *b;
    return g;
}

std::string png(int w, int h, double r, double g, double b) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_paint(cr);
    cairo_destroy(cr);
    std::string bytes;
    cairo_surface_write_to_png_stream(
            s,
            [](void* closure, const unsigned char* data, unsigned int length) {
                static_cast<std::string*>(closure)->append(reinterpret_cast<const char*>(data), length);
                return CAIRO_STATUS_SUCCESS;
            },
            &bytes);
    cairo_surface_destroy(s);
    return bytes;
}

class StickerFileTest: public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(tmp.isValid()); }
    fs::path file(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    QTemporaryDir tmp;
};

TEST_F(StickerFileTest, onePageOfTheContentsSizeOnPlainPaper) {
    sticky::Group g = sample();
    const Rectangle<double> bounds = g.bounds;
    const Color paper(0xfa, 0xf0, 0xd0);
    auto doc = stickers::makeDocument(std::move(g), paper);
    ASSERT_EQ(doc->getPageCount(), 1u);
    PageRef page = doc->getPage(0);
    EXPECT_NEAR(page->getWidth(), bounds.width + 2 * stickers::MARGIN, 1e-6);
    EXPECT_NEAR(page->getHeight(), bounds.height + 2 * stickers::MARGIN, 1e-6);
    EXPECT_EQ(page->getBackgroundType().format, PageTypeFormat::Plain);
    EXPECT_EQ(page->getBackgroundColor(), paper);
    // Bottom up: Markdown, the ink (unnamed: "Layer 1" in Xournal++), the note
    const auto& layers = page->getLayers();
    ASSERT_EQ(layers.size(), 3u);
    EXPECT_EQ(layers[0]->getName(), std::string(xoj::markdown::LAYER_NAME));
    EXPECT_EQ(layers[0]->getElementsView().size(), 1u);
    EXPECT_FALSE(layers[1]->hasName());
    EXPECT_EQ(layers[1]->getElementsView().size(), 2u);
    EXPECT_TRUE(sticky::isNote(*layers[2]));
    EXPECT_EQ(page->getSelectedLayerId(), 2u);  // (what Xournal++ writes into: the ink's layer)
    // Moved so the content begins at the margin
    std::optional<Rectangle<double>> all;
    for (const Layer* l: layers) {
        for (const Element* e: l->getElementsView()) {
            const auto r = e->getBoundingBox();
            all ? all->unite(r) : void(all = r);
        }
    }
    ASSERT_TRUE(all);
    EXPECT_NEAR(all->x, stickers::MARGIN, 0.01);
    EXPECT_NEAR(all->y, stickers::MARGIN, 0.01);
}

TEST_F(StickerFileTest, writtenAndReadBackAsTheContent) {
    sticky::Group g = sample();
    const Rectangle<double> bounds = g.bounds;
    auto doc = stickers::makeDocument(std::move(g), Color(0xff, 0xff, 0xff));
    const fs::path target = file("Sticker.xopp");
    std::string error;
    ASSERT_TRUE(stickers::write(*doc, target, &error)) << error;
    ASSERT_TRUE(fs::exists(target));

    auto read = stickers::read(target, &error);
    ASSERT_TRUE(read) << error;
    ASSERT_EQ(read->elements.size(), 3u);
    ASSERT_EQ(read->notes.size(), 1u);
    // The Markdown box first (its layer is at the bottom), marked as one; then the stroke and the text
    EXPECT_TRUE(read->markdown[0]);
    EXPECT_EQ(read->elements[0]->getType(), ELEMENT_TEXT);
    EXPECT_FALSE(read->markdown[1]);
    EXPECT_EQ(read->elements[1]->getType(), ELEMENT_STROKE);
    EXPECT_EQ(read->elements[2]->getType(), ELEMENT_TEXT);
    // Of the original size, at the margin
    EXPECT_NEAR(read->bounds.x, stickers::MARGIN, 0.01);
    EXPECT_NEAR(read->bounds.y, stickers::MARGIN, 0.01);
    EXPECT_NEAR(read->bounds.width, bounds.width, 0.01);
    EXPECT_NEAR(read->bounds.height, bounds.height, 0.01);
    const auto look = sticky::lookOf(*read->notes[0]);
    ASSERT_TRUE(look);
    EXPECT_NEAR(look->rect.width, 80, 0.01);
    EXPECT_EQ(look->color, sticky::presetColors()[0]);

    // On the clipboard as a copied selection of notes and elements, the Markdown box still one
    const std::string bytes = stickers::clipboardBytes(*read);
    auto group = sticky::deserializeGroup(bytes.data(), bytes.size());
    ASSERT_TRUE(group);
    EXPECT_EQ(group->elements.size(), 3u);
    EXPECT_EQ(group->notes.size(), 1u);
    EXPECT_TRUE(group->markdown[0]);
    EXPECT_FALSE(group->markdown[1]);
    EXPECT_NEAR(group->bounds.width, bounds.width, 0.01);
}

TEST_F(StickerFileTest, upstreamLoadsItWithoutAWarning) {
    auto doc = stickers::makeDocument(sample(), Color(0xff, 0xff, 0xff),
                                      stickers::Picture{png(40, 30, 1, 0, 0), Rectangle<double>(100, 200, 40, 30)});
    const fs::path target = file("Upstream.xopp");
    ASSERT_TRUE(stickers::write(*doc, target));
    std::vector<std::string> warnings;
    LoadHandler handler(&warnings);
    auto loaded = handler.loadDocument(target);
    ASSERT_TRUE(loaded);
    EXPECT_TRUE(warnings.empty()) << warnings.front();
    EXPECT_EQ(loaded->getPageCount(), 1u);
    EXPECT_EQ(loaded->getPage(0)->getLayerCount(), 4u);  // (the picture, Markdown, the ink, the note)
}

TEST_F(StickerFileTest, thePictureBehindLiesAtTheBottom) {
    const Rectangle<double> area(90, 190, 200, 140);
    auto doc = stickers::makeDocument(sample(), Color(0xff, 0xff, 0xff),
                                      stickers::Picture{png(400, 280, 0, 1, 0), area});
    PageRef page = doc->getPage(0);
    const auto& layers = page->getLayers();
    ASSERT_EQ(layers.size(), 4u);
    EXPECT_EQ(layers[0]->getName(), stickers::PICTURE_LAYER);
    ASSERT_EQ(layers[0]->getElementsView().size(), 1u);
    const Element* image = layers[0]->getElementsView().front();
    ASSERT_EQ(image->getType(), ELEMENT_IMAGE);
    // As large as its area (here larger than the content: the page takes it in), at the margin
    EXPECT_NEAR(image->getBoundingBox().width, 200, 0.01);
    EXPECT_NEAR(image->getBoundingBox().height, 140, 0.01);
    EXPECT_NEAR(image->getBoundingBox().x, stickers::MARGIN, 0.01);
    EXPECT_NEAR(page->getWidth(), 200 + 2 * stickers::MARGIN, 0.01);

    const fs::path target = file("Picture.xopp");
    ASSERT_TRUE(stickers::write(*doc, target));
    auto read = stickers::read(target);
    ASSERT_TRUE(read);
    ASSERT_EQ(read->elements.size(), 4u);
    EXPECT_EQ(read->elements[0]->getType(), ELEMENT_IMAGE);  // (pasted first: below the rest)
    EXPECT_FALSE(read->markdown[0]);
}

TEST_F(StickerFileTest, notesAloneKeepAnOwnLayer) {
    sticky::Group g;
    g.notes.emplace_back(sticky::makeNote({{10, 10, 100, 80}, sticky::presetColors()[1], true}));
    g.bounds = Rectangle<double>(10, 10, 100, 80);
    auto doc = stickers::makeDocument(std::move(g), Color(0xff, 0xff, 0xff));
    const auto& layers = doc->getPage(0)->getLayers();
    ASSERT_EQ(layers.size(), 2u);
    EXPECT_FALSE(sticky::isNote(*layers[0]));
    EXPECT_TRUE(sticky::isNote(*layers[1]));
    EXPECT_EQ(doc->getPage(0)->getSelectedLayerId(), 1u);
}

TEST_F(StickerFileTest, nothingReadFromAnEmptyOrMissingFile) {
    std::string error;
    EXPECT_FALSE(stickers::read(file("missing.xopp"), &error));
    EXPECT_FALSE(error.empty());
    auto doc = stickers::makeDocument(sticky::Group{}, Color(0xff, 0xff, 0xff));
    const fs::path target = file("Empty.xopp");
    ASSERT_TRUE(stickers::write(*doc, target));
    EXPECT_FALSE(stickers::read(target, &error));
}

TEST_F(StickerFileTest, theSuggestedName) {
    sticky::Group g;
    g.elements.push_back(stroke(0, 0, 10, 10));
    g.markdown.push_back(false);
    EXPECT_EQ(stickers::suggestedName(g), "");
    g.elements.push_back(text("\n## *Ideal* gas law: p/V = nRT\nmore", 0, 0, true));
    g.markdown.push_back(true);
    EXPECT_EQ(stickers::suggestedName(g), "Ideal gas law p V = nRT");
    sticky::Group task;
    task.elements.push_back(text("- [ ] Read chapter 4 of the book about thermodynamics and its laws", 0, 0, true));
    task.markdown.push_back(true);
    EXPECT_EQ(stickers::suggestedName(task), "Read chapter 4 of the book about");  // (a word boundary, 40 bytes at most)

    EXPECT_EQ(stickers::fileNameOf("  a/b:c  "), "a b c");
    EXPECT_EQ(stickers::fileNameOf("..hidden."), "hidden");
    EXPECT_EQ(stickers::fileNameOf("Größe ✓"), "Größe ✓");
    EXPECT_EQ(stickers::fileNameOf("???"), "");
}

}  // namespace
