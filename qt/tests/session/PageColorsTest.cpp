/*
 * xournal-qt: page colors and textured paper (qt/docs/dark-pages.md, "Page colors"): the color is upstream's background
 * color and the texture a key of upstream's page type config, both kept through a .xopp and a PDF with notes; the
 * texture is drawn wherever a page is (deterministic, subtle); ruling stays visible on dark paper; a highlighter on
 * dark paper lightens instead of disappearing.
 *
 * @license GNU GPLv2 or later
 */
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <memory>

#include <cairo.h>
#include <gtest/gtest.h>
#include <zlib.h>

#include "model/BackgroundConfig.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "render/PaperTexture.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "view/DocumentView.h"

using namespace xqt;

namespace {

constexpr uint32_t BLACK_PAPER = 0xff161616, CREAM = 0xfff2e6cb;

struct Picture {
    std::vector<uint32_t> px;
    int w = 0, h = 0;
    uint32_t at(int x, int y) const { return px[static_cast<size_t>(y * w + x)]; }
};

/// A page drawn as every view draws it (DocumentView), at `scale` pixels a point (XQT_TEST_SHOT: also into a PNG
/// file, `<XQT_TEST_SHOT>-<name>.png`)
Picture draw(const PageRef& page, double scale = 1, const char* name = nullptr) {
    Picture p;
    p.w = static_cast<int>(page->getWidth() * scale);
    p.h = static_cast<int>(page->getHeight() * scale);
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, p.w, p.h);
    cairo_t* cr = cairo_create(s);
    cairo_scale(cr, scale, scale);
    DocumentView view;
    view.drawPage(page, cr, true);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    if (name && qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        cairo_surface_write_to_png(s,
                                   (qEnvironmentVariable("XQT_TEST_SHOT").toStdString() + "-" + name + ".png").c_str());
    }
    const int stride = cairo_image_surface_get_stride(s);
    const unsigned char* data = cairo_image_surface_get_data(s);
    for (int y = 0; y < p.h; ++y) {
        for (int x = 0; x < p.w; ++x) {
            p.px.push_back(*reinterpret_cast<const uint32_t*>(data + y * stride + x * 4) & 0xffffff);
        }
    }
    cairo_surface_destroy(s);
    return p;
}

int grey(uint32_t c) {
    return static_cast<int>(((c >> 16) & 0xff) * 0.3 + ((c >> 8) & 0xff) * 0.59 + (c & 0xff) * 0.11);
}

std::string gunzip(const QString& file) {
    gzFile f = gzopen(file.toStdString().c_str(), "rb");
    std::string out;
    char buf[4096];
    int n;
    while ((n = gzread(f, buf, sizeof buf)) > 0) {
        out.append(buf, static_cast<size_t>(n));
    }
    gzclose(f);
    return out;
}

class PageColorsTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
    }
    fs::path path(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    PageRef page(PageTypeFormat format, uint32_t color, bool textured, double w = 200, double h = 160) {
        auto p = std::make_shared<XojPage>(w, h);
        PageType type(format);
        type.config = paper::withLineColors(paper::withTexture(type.config, textured), Color(color));
        p->setBackgroundType(type);
        p->setBackgroundColor(Color(color));
        return p;
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
};
}  // namespace

TEST_F(PageColorsTest, colorAndTextureGoThroughAXoppAndAPdfWithNotes) {
    auto s = std::make_unique<DocumentSession>(*app);
    Document* doc = s->getDocument();
    doc->lock();
    doc->getPage(0)->setBackgroundType(page(PageTypeFormat::Graph, BLACK_PAPER, true)->getBackgroundType());
    doc->getPage(0)->setBackgroundColor(Color(BLACK_PAPER));
    doc->unlock();
    s->insertNewPage(1);  // (a new page takes the paper of the one before)
    doc->lock();
    doc->getPage(1)->setBackgroundType(page(PageTypeFormat::Plain, CREAM, false)->getBackgroundType());
    doc->getPage(1)->setBackgroundColor(Color(CREAM));
    doc->unlock();

    const auto check = [](const Document& d, const std::string& what) {
        ASSERT_EQ(d.getPageCount(), 2u) << what;
        const PageRef dark = d.getPage(0), cream = d.getPage(1);
        EXPECT_EQ(uint32_t(dark->getBackgroundColor()), BLACK_PAPER) << what;
        EXPECT_EQ(dark->getBackgroundType().format, PageTypeFormat::Graph) << what;
        EXPECT_TRUE(paper::textured(dark->getBackgroundType().config)) << what;
        EXPECT_EQ(uint32_t(cream->getBackgroundColor()), CREAM) << what;
        EXPECT_FALSE(paper::textured(cream->getBackgroundType().config)) << what;
        // The ruling's colors are upstream's own keys: Xournal++ reads them
        uint32_t line = 0;
        EXPECT_TRUE(BackgroundConfig(dark->getBackgroundType().config).loadValueHex("af1", line)) << what;
        EXPECT_GT(grey(line), grey(BLACK_PAPER & 0xffffff) + 25) << what;
    };
    ASSERT_TRUE(s->saveAs(path("paper.xopp")).ok);
    const std::string xml = gunzip(tmp.filePath("paper.xopp"));
    EXPECT_NE(xml.find("color=\"#161616ff\""), std::string::npos) << "upstream's background color";
    EXPECT_NE(xml.find("xqt-texture=paper"), std::string::npos) << "the texture in upstream's config";
    {
        auto loaded = DocumentSession::loadFile(path("paper.xopp"));
        ASSERT_TRUE(loaded.document) << loaded.error;
        check(*loaded.document, ".xopp");
    }
    ASSERT_TRUE(s->saveAsHybrid(path("paper.pdf")).ok);
    {
        auto loaded = DocumentSession::loadFile(path("paper.pdf"));
        ASSERT_TRUE(loaded.document) << loaded.error;
        check(*loaded.document, "PDF with notes");
    }
}

TEST_F(PageColorsTest, textureIsSubtleAndAlwaysTheSame) {
    const Picture plain = draw(page(PageTypeFormat::Plain, CREAM, false));
    const Picture textured = draw(page(PageTypeFormat::Plain, CREAM, true));
    const Picture again = draw(page(PageTypeFormat::Plain, CREAM, true));
    EXPECT_EQ(textured.px, again.px) << "the same page, the same grain";
    long diff = 0;
    int differing = 0;
    int most = 0;
    for (size_t i = 0; i < plain.px.size(); ++i) {
        const int d = std::abs(grey(plain.px[i]) - grey(textured.px[i]));
        diff += d;
        differing += d > 0;
        most = std::max(most, d);
    }
    EXPECT_GT(differing, static_cast<int>(plain.px.size() / 4)) << "a grain all over the page";
    EXPECT_LT(static_cast<double>(diff) / plain.px.size(), 6.0) << "subtle";
    EXPECT_LE(most, 40);
    // Dark paper: a lighter grain
    const Picture darkPlain = draw(page(PageTypeFormat::Plain, BLACK_PAPER, false));
    const Picture darkTextured = draw(page(PageTypeFormat::Plain, BLACK_PAPER, true));
    draw(page(PageTypeFormat::Ruled, CREAM, true), 3, "cream-ruled-textured");
    draw(page(PageTypeFormat::Graph, BLACK_PAPER, true), 3, "black-graph-textured");
    draw(page(PageTypeFormat::Lined, 0xffc9a77cu, true), 3, "kraft-lined-textured");
    long darkDiff = 0;
    for (size_t i = 0; i < darkPlain.px.size(); ++i) {
        darkDiff += grey(darkTextured.px[i]) - grey(darkPlain.px[i]);
    }
    EXPECT_GT(darkDiff, 0);
}

TEST_F(PageColorsTest, rulingStaysVisibleOnDarkPaper) {
    for (uint32_t c: {0xff161616u, 0xff2b2d31u, 0xff5c5f66u, 0xffc9a77cu}) {
        const Picture p = draw(page(PageTypeFormat::Graph, c, false));
        int lightest = 0, darkest = 255;
        for (uint32_t px: p.px) {
            lightest = std::max(lightest, grey(px));
            darkest = std::min(darkest, grey(px));
        }
        // the lines differ from the paper by a good step (one way or the other)
        EXPECT_GT(std::max(lightest - grey(c & 0xffffff), grey(c & 0xffffff) - darkest), 25) << std::hex << c;
    }
}

TEST_F(PageColorsTest, aHighlighterOnDarkPaperLightensIt) {
    for (const uint32_t paperColor: {0xffffffffu, BLACK_PAPER}) {
        PageRef p = page(PageTypeFormat::Plain, paperColor, false);
        auto s = std::make_unique<Stroke>();
        s->setToolType(StrokeTool::HIGHLIGHTER);
        s->setColor(Color(0xffffe066u));
        s->setWidth(20);
        s->addPoint(Point(20, 80, -1));
        s->addPoint(Point(180, 80, -1));
        p->getSelectedLayer()->addElement(std::move(s));
        const Picture pic = draw(p);
        const uint32_t marked = pic.at(100, 80);
        if (paperColor == 0xffffffffu) {
            // upstream's: multiplied at 0.47 (white paper: the color at 0.47)
            EXPECT_NEAR(static_cast<int>((marked >> 8) & 0xff),
                        static_cast<int>(std::lround(255 - 0.47 * (255 - 0xe0))), 2);
        } else {
            EXPECT_GT(grey(marked), grey(BLACK_PAPER & 0xffffff) + 60) << std::hex << marked << ": the marker shows";
            EXPECT_GT((marked >> 16) & 0xff, marked & 0xff) << "yellow, not grey";
        }
    }
}
