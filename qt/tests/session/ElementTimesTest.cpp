/*
 * xournal-qt: when an element was made (qt/docs/timeline.md, "Creation times"): the element attribute xqt-created
 * (written only when known, read back, ignored by upstream's loader without a message), kept by copies and by the
 * pieces the eraser leaves, not in upstream's clipboard data; the .xopp inside a PDF with notes; its cost in bytes.
 *
 * @license GNU GPLv2 or later
 */
#include <cstdio>
#include <memory>
#include <string>

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <cairo.h>
#include <gtest/gtest.h>

#include "control/xojfile/DocumentBuilderInterface.h"
#include "control/xojfile/LoadHandler.h"
#include "control/xojfile/XmlParser.h"
#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/Font.h"
#include "model/Image.h"
#include "model/Layer.h"
#include "model/LineStyle.h"
#include "model/Link.h"
#include "model/PageType.h"
#include "model/PathParameter.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/StrokeStyle.h"
#include "model/Text.h"
#include "model/TextAlignment.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/ElementTimes.h"
#include "util/Matrix.h"
#include "util/serializing/BinObjectEncoding.h"
#include "util/serializing/ObjectOutputStream.h"

#include "config-test.h"
#include "support/TestSupport.h"

using xqt::test::gunzipFile;

using namespace xqt;

namespace {

constexpr int64_t T0 = 1791100800000;  // 2026-10-04 08:00:00 UTC

std::unique_ptr<Stroke> stroke(double x0, double y0, double x1, double y1, int64_t created) {
    auto s = std::make_unique<Stroke>();
    s->setToolType(StrokeTool::PEN);
    s->setColor(Color(0x10, 0x20, 0xc0));
    s->setWidth(2);
    for (int i = 0; i <= 10; ++i) {
        s->addPoint(Point(x0 + (x1 - x0) * i / 10, y0 + (y1 - y0) * i / 10, 0.5 + 0.01 * i));
    }
    s->setCreated(created);
    return s;
}

DocumentHandler& handler() {
    static DocumentHandler h;
    return h;
}

std::string png(int w, int h) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, 0, 0.5, 0);
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

/// One page: a stroke, a text, a link, an image (all with times), a stroke without one (an older element)
std::unique_ptr<Document> timed() {
    auto doc = std::make_unique<Document>(&handler());
    auto page = std::make_shared<XojPage>(400, 300);
    Layer* layer = page->getSelectedLayer();
    layer->addElement(stroke(10, 10, 60, 40, T0));
    auto text = std::make_unique<Text>();
    text->setText("made later");
    text->setFont(XojFont("Sans", 12));
    text->move(20, 50);
    text->setCreated(T0 + 1500);
    layer->addElement(std::move(text));
    auto link = std::make_unique<Link>();
    link->setText("a link");
    link->setUrl("https://example.org");
    link->setTransformation({1, 0, 0, 1, {150, 150}});
    link->setCreated(T0 + 2000);
    layer->addElement(std::move(link));
    auto image = std::make_unique<Image>();
    image->setImage(png(20, 10));
    image->setTransformation({1, 0, 0, 1, {200, 200}});
    image->setCreated(T0 + 2500);
    layer->addElement(std::move(image));
    layer->addElement(stroke(200, 10, 260, 40, 0));
    doc->addPage(page);
    return doc;
}

std::vector<int64_t> timesOf(const Document& doc) {
    std::vector<int64_t> times;
    for (const Element* e: doc.getPage(0)->getSelectedLayer()->getElementsView()) {
        times.push_back(e->getCreated());
    }
    return times;
}

class ElementTimesTest: public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(tmp.isValid()); }
    void TearDown() override { timeline::setClock({}); }
    fs::path file(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    QTemporaryDir tmp;
};

}  // namespace

// The time is the element attribute xqt-created (ms since 1970 UTC), written only when known, read back as it was
TEST_F(ElementTimesTest, aXoppKeepsThemAsAnElementAttribute) {
    auto doc = timed();
    ASSERT_TRUE(DocumentSession::writeDocument(*doc, file("timed.xopp")).ok);
    const QString xml = QString::fromStdString(gunzipFile(file("timed.xopp")));
    EXPECT_EQ(xml.count("xqt-created="), 4) << xml.toStdString();
    EXPECT_TRUE(xml.contains("xqt-created=\"1791100800000\""));
    EXPECT_TRUE(xml.contains("xqt-created=\"1791100802500\""));

    std::vector<std::string> warnings;
    LoadHandler loader(&warnings);
    auto loaded = loader.loadDocument(file("timed.xopp"));
    ASSERT_TRUE(loaded);
    EXPECT_TRUE(warnings.empty()) << warnings.front();
    EXPECT_EQ(timesOf(*loaded), (std::vector<int64_t>{T0, T0 + 1500, T0 + 2000, T0 + 2500, 0}));
}

// Upstream's builder has no setElementCreated: a builder with only upstream's methods reads the file through the parser
// without an error and gets every element (the parser's call goes to the empty default)
TEST_F(ElementTimesTest, aLoaderWithoutTimesReadsTheFileWithoutAnError) {
    auto doc = timed();
    ASSERT_TRUE(DocumentSession::writeDocument(*doc, file("timed.xopp")).ok);
    const QByteArray xml = QByteArray::fromStdString(gunzipFile(file("timed.xopp")));
    ASSERT_TRUE(xml.contains("xqt-created="));

    struct UpstreamBuilder final: DocumentBuilderInterface {
        int elements = 0;
        std::vector<std::string> errors;
        void addDocument(std::u8string, int) override {}
        void finalizeDocument() override {}
        void addPage(double, double) override {}
        void finalizePage() override {}
        void addAudioAttachment(const fs::path&) override {}
        void setBgName(const std::string&) override {}
        void setBgSolid(const PageType&, Color) override {}
        void setBgPixmap(bool, const fs::path&) override {}
        void setBgPixmapCloned(size_t) override {}
        void setBgPdf(size_t) override {}
        void loadBgPdf(bool, const fs::path&) override {}
        void addLayer(const std::optional<std::string_view>&) override {}
        void finalizeLayer() override {}
        void addStroke(StrokeTool, Color, double, int, StrokeCapStyle, const LineStyle&, fs::path, size_t) override {}
        void setStrokePoints(std::vector<Point>, bool) override {}
        void finalizeStroke() override { ++elements; }
        void addText(std::string, double, xoj::util::Matrix, Color, std::optional<double>, std::optional<TextAlignment>,
                     bool, fs::path, size_t) override {}
        void setTextContents(std::string) override {}
        void finalizeText() override { ++elements; }
        void addImageLegacy(double, double, double, double, std::optional<xoj::util::Size<double>>) override {}
        void addImage(xoj::util::Matrix) override {}
        void setImageData(std::string) override {}
        void setImageAttachment(const fs::path&) override {}
        void finalizeImage() override { ++elements; }
        void addTexImageLegacy(double, double, double, double, std::string,
                               std::optional<xoj::util::Size<double>>) override {}
        void addTexImage(xoj::util::Matrix, std::string) override {}
        void setTexImageData(std::string) override {}
        void setTexImageAttachment(const fs::path&) override {}
        void finalizeTexImage() override { ++elements; }
        void addLink(TextAlignment, std::string, double, xoj::util::Matrix, Color, std::string) override {}
        void setLinkContent(std::string) override {}
        void finalizeLink() override { ++elements; }
        void logError(const std::string& error) override { errors.push_back(error); }
    } builder;
    XmlParser parser(builder);
    GMarkupParseContext* context = g_markup_parse_context_new(&XmlParser::interface, static_cast<GMarkupParseFlags>(0),
                                                              &parser, nullptr);
    GError* error = nullptr;
    EXPECT_TRUE(g_markup_parse_context_parse(context, xml.constData(), xml.size(), &error));
    EXPECT_TRUE(g_markup_parse_context_end_parse(context, &error));
    EXPECT_EQ(error, nullptr);
    g_markup_parse_context_free(context);
    EXPECT_TRUE(builder.errors.empty()) << builder.errors.front();
    EXPECT_EQ(builder.elements, 5);
}

// A value that is no time is ignored (the element has none), as the file is still read
TEST_F(ElementTimesTest, aBrokenValueIsIgnored) {
    auto doc = timed();
    ASSERT_TRUE(DocumentSession::writeDocument(*doc, file("timed.xopp")).ok);
    QByteArray xml = QByteArray::fromStdString(gunzipFile(file("timed.xopp")));
    xml.replace("xqt-created=\"1791100800000\"", "xqt-created=\"soon\"");
    xml.replace("xqt-created=\"1791100801500\"", "xqt-created=\"-5\"");
    QFile out(tmp.filePath("broken.xml"));
    ASSERT_TRUE(out.open(QIODevice::WriteOnly));
    out.write(xml);
    out.close();
    QProcess gz;
    gz.start("sh", {"-c", "gzip -c '" + tmp.filePath("broken.xml") + "' > '" + tmp.filePath("broken.xopp") + "'"});
    ASSERT_TRUE(gz.waitForFinished());
    LoadHandler loader;
    auto loaded = loader.loadDocument(file("broken.xopp"));
    ASSERT_TRUE(loaded);
    EXPECT_EQ(timesOf(*loaded), (std::vector<int64_t>{0, 0, T0 + 2000, T0 + 2500, 0}));
}

// The .xopp inside a PDF with notes has them too
TEST_F(ElementTimesTest, aPdfWithNotesKeepsThem) {
    AppContext app(fs::path(XQT_BUILD_RESOURCE_DIR), file("settings.xml"), 1);
    DocumentSession s(app);
    {
        Document* doc = s.getDocument();
        std::unique_lock lock(*doc);
        Layer* layer = doc->getPage(0)->getSelectedLayer();
        layer->addElement(stroke(10, 10, 60, 40, T0));
        layer->addElement(stroke(70, 10, 90, 40, T0 + 700));
        layer->addElement(stroke(100, 10, 120, 40, 0));
    }
    const auto r = s.saveAsHybrid(file("notes.pdf"));
    ASSERT_TRUE(r.ok) << r.error;
    auto opened = DocumentSession::loadFile(file("notes.pdf"));
    ASSERT_TRUE(opened.document) << opened.error;
    EXPECT_EQ(timesOf(*opened.document), (std::vector<int64_t>{T0, T0 + 700, 0}));
}

// A copy of an element has its time; so have the pieces the eraser leaves and the shape a stroke becomes. Upstream's
// serialization (the clipboard) has no time: a pasted element is a new one.
TEST_F(ElementTimesTest, copiesAndErasedPiecesKeepTheirTime) {
    auto doc = timed();
    for (const Element* e: doc->getPage(0)->getSelectedLayer()->getElementsView()) {
        EXPECT_EQ(e->clone()->getCreated(), e->getCreated());
    }
    auto s = stroke(0, 0, 100, 0, T0 + 42);
    auto piece = s->cloneSection(PathParameter(0, 0.2), PathParameter(3, 0.5));
    EXPECT_EQ(piece->getCreated(), T0 + 42);
    Stroke shape;
    shape.applyStyleFrom(s.get());
    EXPECT_EQ(shape.getCreated(), T0 + 42);
    auto older = stroke(0, 0, 100, 0, 0);
    ObjectOutputStream a(new BinObjectEncoding());
    s->serialize(a);
    ObjectOutputStream b(new BinObjectEncoding());
    older->serialize(b);
    GString* da = a.stealData();
    GString* db = b.stealData();
    EXPECT_EQ(std::string(da->str, da->len), std::string(db->str, db->len));
    g_string_free(da, TRUE);
    g_string_free(db, TRUE);
}

// The clock: the system's (now), or a test's; elements stamped at once share the time
TEST_F(ElementTimesTest, stampingUsesTheClock) {
    const int64_t before = timeline::now();
    EXPECT_GT(before, T0 - 365LL * 24 * 3600 * 1000) << "ms since 1970";
    Stroke a;
    timeline::stampNew(a);
    EXPECT_GE(a.getCreated(), before);
    EXPECT_LE(a.getCreated(), timeline::now());

    int64_t fake = T0;
    timeline::setClock([&] { return fake; });
    Stroke b;
    Stroke c;
    timeline::stampNew({&b, &c});
    EXPECT_EQ(b.getCreated(), T0);
    EXPECT_EQ(c.getCreated(), T0);
    fake += 10;
    auto page = std::make_shared<XojPage>(100, 100);
    page->getSelectedLayer()->addElement(stroke(0, 0, 10, 10, 5));
    page->getSelectedLayer()->addElement(stroke(0, 0, 10, 10, 0));
    timeline::stampPage(*page);
    for (const Element* e: page->getSelectedLayer()->getElementsView()) {
        EXPECT_EQ(e->getCreated(), T0 + 10);
    }
}

// What the times cost in the file: measured on 2000 strokes of 11 points (a short stroke), each made 0.2 to 3 s after
// the one before. Written down in qt/docs/timeline.md.
TEST_F(ElementTimesTest, theirCostInTheFile) {
    constexpr int N = 2000;
    auto make = [&](bool withTimes) {
        auto doc = std::make_unique<Document>(&handler());
        auto page = std::make_shared<XojPage>(595, 842);
        uint32_t random = 12345;  // (pauses between strokes from 0.2 to 3 s, as handwriting has them)
        int64_t t = T0;
        for (int i = 0; i < N; ++i) {
            const double x = 20 + (i % 40) * 13.7;
            const double y = 20 + (i / 40) * 15.3;
            random = random * 1103515245u + 12345u;
            t += 200 + (random >> 8) % 2800;
            page->getSelectedLayer()->addElement(stroke(x, y, x + 9.1, y + 6.3, withTimes ? t : 0));
        }
        doc->addPage(page);
        return doc;
    };
    auto without = make(false);
    auto with = make(true);
    ASSERT_TRUE(DocumentSession::writeDocument(*without, file("without.xopp")).ok);
    ASSERT_TRUE(DocumentSession::writeDocument(*with, file("with.xopp")).ok);
    const double gz = static_cast<double>(QFileInfo(tmp.filePath("with.xopp")).size() -
                                          QFileInfo(tmp.filePath("without.xopp")).size()) /
                      N;
    const double plain = (static_cast<double>(gunzipFile(file("with.xopp")).size()) -
                          static_cast<double>(gunzipFile(file("without.xopp")).size())) /
                         N;
    const double perStroke = static_cast<double>(QFileInfo(tmp.filePath("without.xopp")).size()) / N;
    std::printf("[ cost     ] xqt-created: %.1f bytes a stroke in the .xopp (gzip), %.1f uncompressed; a stroke of 11 "
                "points is %.1f bytes (gzip)\n",
                gz, plain, perStroke);
    EXPECT_NEAR(plain, 28, 0.5) << " xqt-created=\"1791100800000\"";
    EXPECT_LT(gz, 8) << "compressed, most digits repeat";
}
