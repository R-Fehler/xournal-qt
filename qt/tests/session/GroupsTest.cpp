/*
 * xournal-qt: groups of elements (qt/docs/features/groups.md): the element attribute xqt-group (written only on grouped
 * elements, read back, ignored by upstream's loader without a message), groups kept by copies of elements and by the
 * pieces the eraser leaves.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>
#include <string>

#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <cairo.h>
#include <gtest/gtest.h>

#include "control/xojfile/DocumentBuilderInterface.h"
#include "control/xojfile/LoadHandler.h"
#include "control/xojfile/XmlParser.h"
#include "model/Document.h"
#include "model/ElementInsertionPosition.h"
#include "model/DocumentHandler.h"
#include "model/Font.h"
#include "model/Image.h"
#include "model/LineStyle.h"
#include "model/PageType.h"
#include "model/StrokeStyle.h"
#include "model/TextAlignment.h"
#include "util/serializing/BinObjectEncoding.h"
#include "util/serializing/ObjectOutputStream.h"
#include "util/Matrix.h"
#include "model/Layer.h"
#include "model/Link.h"
#include "model/PathParameter.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/ElementGroups.h"

#include "config-test.h"

using namespace xqt;

namespace {

std::unique_ptr<Stroke> stroke(double x0, double y0, double x1, double y1, uint32_t group) {
    auto s = std::make_unique<Stroke>();
    s->setToolType(StrokeTool::PEN);
    s->setColor(Color(0x10, 0x20, 0xc0));
    s->setWidth(2);
    s->addPoint(Point(x0, y0));
    s->addPoint(Point((x0 + x1) / 2, (y0 + y1) / 2));
    s->addPoint(Point(x1, y1));
    s->setGroup(group);
    return s;
}

std::unique_ptr<Text> text(const std::string& content, double x, double y, uint32_t group) {
    auto t = std::make_unique<Text>();
    t->setText(content);
    t->setFont(XojFont("Sans", 12));
    t->move(x, y);
    t->setGroup(group);
    return t;
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

DocumentHandler& handler() {
    static DocumentHandler h;
    return h;
}

/// One page: group 3 (a stroke, a text, an image, a link), a loose stroke, group 7 (two strokes)
std::unique_ptr<Document> grouped() {
    auto doc = std::make_unique<Document>(&handler());
    auto page = std::make_shared<XojPage>(400, 300);
    Layer* layer = page->getSelectedLayer();
    layer->addElement(stroke(10, 10, 60, 40, 3));
    layer->addElement(text("in a group", 20, 50, 3));
    auto image = std::make_unique<Image>();
    image->setImage(png(20, 10));
    image->setTransformation({1, 0, 0, 1, {100, 100}});
    image->setGroup(3);
    layer->addElement(std::move(image));
    auto link = std::make_unique<Link>();
    link->setText("a link");
    link->setUrl("https://example.org");
    link->setTransformation({1, 0, 0, 1, {150, 150}});
    link->setGroup(3);
    layer->addElement(std::move(link));
    layer->addElement(stroke(200, 10, 260, 40, 0));
    layer->addElement(stroke(10, 200, 60, 240, 7));
    layer->addElement(stroke(70, 200, 90, 240, 7));
    doc->addPage(page);
    return doc;
}

std::vector<uint32_t> groupsOf(const Document& doc) {
    std::vector<uint32_t> groups;
    for (const Element* e: doc.getPage(0)->getSelectedLayer()->getElementsView()) {
        groups.push_back(e->getGroup());
    }
    return groups;
}

QString xmlOf(const QString& file) {
    QProcess gz;
    gz.start("gzip", {"-dc", file});
    gz.waitForFinished();
    return QString::fromUtf8(gz.readAllStandardOutput());
}

class GroupsTest: public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(tmp.isValid()); }
    fs::path file(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    QTemporaryDir tmp;
};

}  // namespace

// The group is the element attribute xqt-group, written only on elements in a group (strokes, texts, images, links),
// read back as it was
TEST_F(GroupsTest, aXoppKeepsThemAsAnElementAttribute) {
    auto doc = grouped();
    ASSERT_TRUE(DocumentSession::writeDocument(*doc, file("grouped.xopp")).ok);
    const QString xml = xmlOf(tmp.filePath("grouped.xopp"));
    EXPECT_EQ(xml.count("xqt-group=\"3\""), 4) << xml.toStdString();
    EXPECT_EQ(xml.count("xqt-group=\"7\""), 2);
    EXPECT_EQ(xml.count("xqt-group="), 6);

    std::vector<std::string> warnings;
    LoadHandler loader(&warnings);
    auto loaded = loader.loadDocument(file("grouped.xopp"));
    ASSERT_TRUE(loaded);
    EXPECT_TRUE(warnings.empty()) << warnings.front();
    EXPECT_EQ(groupsOf(*loaded), (std::vector<uint32_t>{3, 3, 3, 3, 0, 7, 7}));
}

// Upstream's builder (LoadHandler) has no setElementGroup: a builder with only upstream's methods reads a grouped
// file through the parser without an error and gets every element (the parser's call goes to the empty default)
TEST_F(GroupsTest, aLoaderWithoutGroupsReadsTheFileWithoutAnError) {
    auto doc = grouped();
    ASSERT_TRUE(DocumentSession::writeDocument(*doc, file("grouped.xopp")).ok);
    const QByteArray xml = xmlOf(tmp.filePath("grouped.xopp")).toUtf8();
    ASSERT_TRUE(xml.contains("xqt-group="));

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
    EXPECT_EQ(builder.elements, 7);
}

// Upstream Xournal++ looks attributes up by name: it opens (and exports) a grouped file without any message
TEST_F(GroupsTest, upstreamXournalppOpensItWithoutAMessage) {
    const QString upstream = qEnvironmentVariableIsSet("XOJ_UPSTREAM_BIN") ? qEnvironmentVariable("XOJ_UPSTREAM_BIN")
                                                                          : QString(XQT_UPSTREAM_BIN);
    if (!QFileInfo(upstream).isExecutable()) {
        GTEST_SKIP() << "no upstream xournalpp at " << upstream.toStdString() << " (set XOJ_UPSTREAM_BIN)";
    }
    auto doc = grouped();
    ASSERT_TRUE(DocumentSession::writeDocument(*doc, file("grouped.xopp")).ok);
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();  // (its own configuration: AGENTS.md)
    for (const char* var: {"XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME"}) {
        env.insert(var, tmp.filePath(QString("upstream-") + var));
    }
    p.setProcessEnvironment(env);
    p.start(upstream, {tmp.filePath("grouped.xopp"), "--create-pdf=" + tmp.filePath("upstream.pdf")});
    ASSERT_TRUE(p.waitForFinished(60000)) << "upstream did not finish";
    const QString output = QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError());
    ASSERT_EQ(p.exitCode(), 0) << output.toStdString();
    EXPECT_FALSE(output.contains("error", Qt::CaseInsensitive)) << output.toStdString();
    EXPECT_FALSE(output.contains("xqt-group")) << output.toStdString();
    EXPECT_TRUE(QFileInfo(tmp.filePath("upstream.pdf")).size() > 0);
}

// A copy of an element is in the same group; so are the pieces the eraser leaves of a stroke
TEST_F(GroupsTest, copiesAndErasedPiecesStayInTheGroup) {
    auto doc = grouped();
    for (const Element* e: doc->getPage(0)->getSelectedLayer()->getElementsView()) {
        EXPECT_EQ(e->clone()->getGroup(), e->getGroup());
    }
    auto s = stroke(0, 0, 100, 0, 5);
    auto piece = s->cloneSection(PathParameter(0, 0.2), PathParameter(1, 0.5));
    EXPECT_EQ(piece->getGroup(), 5u);
    // Not in upstream's serialization (its clipboard data stays as it is)
    auto loose = stroke(0, 0, 100, 0, 0);
    ObjectOutputStream a(new BinObjectEncoding());
    s->serialize(a);
    ObjectOutputStream b(new BinObjectEncoding());
    loose->serialize(b);
    GString* da = a.stealData();
    GString* db = b.stealData();
    EXPECT_EQ(std::string(da->str, da->len), std::string(db->str, db->len));
    g_string_free(da, TRUE);
    g_string_free(db, TRUE);
}

// --- the model's helpers (session/ElementGroups) -------------------------------------------------------------------

// A new number is larger than any in the document and than any handed out before
TEST_F(GroupsTest, aNewNumberIsOneNoGroupHas) {
    auto doc = grouped();
    const groups::Id a = groups::fresh(*doc);
    EXPECT_GT(a, 7u);
    const groups::Id b = groups::fresh(*doc);
    EXPECT_GT(b, a) << "never the same twice";
    auto loose = stroke(0, 0, 10, 10, 1000);
    EXPECT_GT(groups::fresh(*doc, {loose.get()}), 1000u);
}

// The members of a group in the layer are added to what is found, in the layer's order
TEST_F(GroupsTest, membersAreAddedInTheLayersOrder) {
    auto doc = grouped();
    Layer* layer = doc->getPage(0)->getSelectedLayer();
    const auto all = layer->getElementsView();
    std::vector<const Element*> elements(all.begin(), all.end());
    const InsertionOrderRef found{InsertionPositionRef(elements[2], 2), InsertionPositionRef(elements[6], 6)};
    const InsertionOrderRef with = groups::withMembers(*layer, found);
    ASSERT_EQ(with.size(), 6u);
    for (size_t i = 0; i < with.size(); ++i) {
        const Element::Index expected[] = {0, 1, 2, 3, 5, 6};
        EXPECT_EQ(with[i].pos, expected[i]);
        EXPECT_EQ(with[i].e, elements[static_cast<size_t>(expected[i])]);
    }
    // A loose element alone stays alone
    EXPECT_EQ(groups::withMembers(*layer, InsertionOrderRef{InsertionPositionRef(elements[4], 4)}).size(), 1u);
}

// Pasted copies get new numbers (each group one); elements coming into a layer only where a group there has theirs
TEST_F(GroupsTest, renumberAndSeparate) {
    auto doc = grouped();
    Layer* layer = doc->getPage(0)->getSelectedLayer();
    auto a = stroke(0, 0, 10, 10, 3);
    auto b = stroke(0, 0, 10, 10, 3);
    auto c = stroke(0, 0, 10, 10, 7);
    auto d = stroke(0, 0, 10, 10, 0);
    groups::renumber({a.get(), b.get(), c.get(), d.get()}, *doc);
    EXPECT_EQ(a->getGroup(), b->getGroup());
    EXPECT_GT(a->getGroup(), 7u);
    EXPECT_GT(c->getGroup(), 7u);
    EXPECT_NE(c->getGroup(), a->getGroup());
    EXPECT_EQ(d->getGroup(), 0u);

    auto e = stroke(0, 0, 10, 10, 3);   // (3 is taken in the layer)
    auto f = stroke(0, 0, 10, 10, 12);  // (12 is not)
    EXPECT_TRUE(groups::separate({e.get(), f.get()}, *layer, *doc));
    EXPECT_NE(e->getGroup(), 3u);
    EXPECT_EQ(f->getGroup(), 12u);
    EXPECT_FALSE(groups::separate({f.get()}, *layer, *doc));
    // Elements already in the layer are not counted against themselves
    std::vector<Element*> group3;
    for (const Element* g: layer->getElementsView()) {
        if (g->getGroup() == 3) {
            group3.push_back(const_cast<Element*>(g));
        }
    }
    EXPECT_FALSE(groups::separate(group3, *layer, *doc));
}

TEST_F(GroupsTest, whatASelectionCanDo) {
    auto a = stroke(0, 0, 10, 10, 3);
    auto b = stroke(0, 0, 10, 10, 3);
    auto c = stroke(0, 0, 10, 10, 0);
    auto s = groups::stateOf({a.get(), b.get()});
    EXPECT_TRUE(s.oneGroup);
    EXPECT_FALSE(s.canGroup);
    EXPECT_TRUE(s.canUngroup);
    s = groups::stateOf({a.get(), b.get(), c.get()});
    EXPECT_FALSE(s.oneGroup);
    EXPECT_TRUE(s.canGroup);
    EXPECT_TRUE(s.canUngroup);
    s = groups::stateOf({c.get()});
    EXPECT_FALSE(s.canGroup);
    EXPECT_FALSE(s.canUngroup);
    s = groups::stateOf({a.get()});  // (a group's last member)
    EXPECT_FALSE(s.canGroup);
    EXPECT_TRUE(s.canUngroup);
    // The clipboard's numbers
    EXPECT_EQ(groups::clipboardNumbers({a.get(), c.get(), b.get()}), "3 0 3");
    EXPECT_EQ(groups::fromClipboard("3 0 3", 3), (std::vector<groups::Id>{3, 0, 3}));
    EXPECT_TRUE(groups::fromClipboard("3 0 3", 2).empty());
    EXPECT_TRUE(groups::fromClipboard("3 x 3", 3).empty());
}

// Plugin data on elements (ADR 0008): the attribute xqt-data, written only where there is some (escaped: JSON has
// quotes), read back as it was; copied with the element and with the eraser's pieces (applyStyleFrom)
TEST_F(GroupsTest, pluginDataIsTheElementAttributeXqtData) {
    auto doc = grouped();
    const std::string json = R"({"org.example.plot":{"f":"x<2 && y>\"1\"","n":3}})";
    auto elements = doc->getPage(0)->getSelectedLayer()->getElementsView();
    const_cast<Element*>(elements.front())->setData(json);           // a stroke
    const_cast<Element*>(*std::next(elements.begin()))->setData("t");  // a text
    ASSERT_TRUE(DocumentSession::writeDocument(*doc, file("data.xopp")).ok);
    const QString xml = xmlOf(tmp.filePath("data.xopp"));
    EXPECT_EQ(xml.count("xqt-data="), 2) << xml.toStdString();

    std::vector<std::string> warnings;
    LoadHandler loader(&warnings);
    auto loaded = loader.loadDocument(file("data.xopp"));
    ASSERT_TRUE(loaded);
    EXPECT_TRUE(warnings.empty());
    const auto back = loaded->getPage(0)->getSelectedLayer()->getElementsView();
    EXPECT_EQ(back.front()->getData(), json);
    EXPECT_EQ((*std::next(back.begin()))->getData(), "t");
    EXPECT_EQ((*std::next(back.begin(), 2))->getData(), "");

    auto copy = back.front()->clone();
    EXPECT_EQ(copy->getData(), json);
    Stroke piece;
    piece.applyStyleFrom(dynamic_cast<const Stroke*>(back.front()));
    EXPECT_EQ(piece.getData(), json);
}
