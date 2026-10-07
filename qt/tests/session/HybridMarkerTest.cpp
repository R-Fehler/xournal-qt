/*
 * xournal-qt: what a PDF with notes carries besides its pages (qt/docs/features/hybrid-pdf.md): the marker, the text layer of the
 * handwriting and the embedded files are the same whether the file was written in full or saved again as an
 * incremental update (both write them through one writer, HybridMarker.cpp). Read back from both and compared; for a
 * PDF with notes and for an archive PDF.
 *
 * @license GNU GPLv2 or later
 */
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <QCryptographicHash>
#include <QTemporaryDir>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtest/gtest.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>

#include "audio/AudioFiles.h"
#include "audio/DocumentAudio.h"
#include "model/BackgroundImage.h"
#include "model/Document.h"
#include "model/Font.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "session/InkText.h"

#include "support/TestSupport.h"

using namespace xqt;

namespace {

constexpr const char* RECORDING = "2026-10-07_09-00-00.ogg";
constexpr const char* LATER = "2026-10-07_10-00-00.ogg";  ///< recorded after the first save

using InkPages = std::vector<std::shared_ptr<const ink::PageText>>;

std::shared_ptr<const ink::PageText> handwriting(QPointF at, const std::vector<const char*>& words) {
    auto line = std::make_shared<ink::LineResult>();
    double x = 0;
    for (const char* text: words) {
        ink::Word w;
        w.box = QRectF(x, 0, 60, 14);
        w.conf = 0.9f;
        w.text = QString::fromUtf8(text);
        w.candidates.push_back(ink::candidate(w.text, 0.9f));
        line->words.push_back(std::move(w));
        x += 70;
    }
    return ink::PageText::assemble({{at, line}});
}

void addStroke(Layer* layer, double y, Color color = Color(0xff008000U)) {
    auto s = std::make_unique<Stroke>();
    s->setWidth(1.41);
    s->setColor(color);
    for (int j = 0; j < 10; ++j) {
        s->addPoint(Point(80 + j * 25, y + 5 * (j % 3), 1.0));
    }
    s->getBoundingBox();
    layer->addElement(std::move(s));
}

std::string md5Hex(const std::string& data) {
    return QCryptographicHash::hash(QByteArray::fromStdString(data), QCryptographicHash::Md5).toHex().toStdString();
}

std::string streamData(QPDFObjectHandle stream) {
    auto buffer = stream.getStreamData(qpdf_dl_all);
    return std::string(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
}

/// An object as text that does not depend on how it was written: indirect objects resolved (to `depth`), a stream as
/// its dictionary (without how it is encoded) and the MD5 of its data, a date as "<date>".
std::string canon(QPDFObjectHandle o, int depth = 6) {
    if (o.isIndirect() && depth <= 0) {
        return "<ref>";
    }
    if (o.isNull()) {
        return "null";
    }
    if (o.isBool()) {
        return o.getBoolValue() ? "true" : "false";
    }
    if (o.isInteger()) {
        return std::to_string(o.getIntValue());
    }
    if (o.isReal()) {
        return o.getRealValue();
    }
    if (o.isName()) {
        return o.getName();
    }
    if (o.isString()) {
        return "(" + o.getUTF8Value() + ")";
    }
    if (o.isArray()) {
        std::string out = "[";
        for (const auto& item: o.getArrayAsVector()) {
            out += canon(item, depth - 1) + " ";
        }
        return out + "]";
    }
    if (o.isStream()) {
        QPDFObjectHandle dict = o.getDict().shallowCopy();
        for (const char* key: {"/Length", "/Filter", "/DecodeParms"}) {
            if (dict.hasKey(key)) {
                dict.removeKey(key);
            }
        }
        return "stream" + canon(dict, depth - 1) + " md5 " + md5Hex(streamData(o));
    }
    if (o.isDictionary()) {
        std::string out = "<<";
        for (const auto& key: o.getKeys()) {
            const bool date = key == "/ModDate" || key == "/CreationDate";
            out += " " + key + " " + (date ? std::string("<date>") : canon(o.getKey(key), depth - 1));
        }
        return out + " >>";
    }
    return "?";
}

/// What a PDF with notes carries besides its pages, read back from the file, as lines to compare.
std::string carried(const fs::path& pdf) {
    QPDF q;
    q.setSuppressWarnings(true);
    q.processFile(pdf.string().c_str());
    std::ostringstream out;
    // The marker: every key, as written (what only an incremental save records, how often and since what size, aside)
    QPDFObjectHandle marker = q.getRoot().getKey("/XournalQt");
    EXPECT_TRUE(marker.isDictionary()) << pdf;
    if (!marker.isDictionary()) {
        return {};
    }
    for (const auto& key: marker.getKeys()) {
        if (key == "/Base" || key == "/Updates") {
            continue;
        }
        QPDFObjectHandle v = marker.getKey(key);
        if (key == "/Layers") {  // (a layer's record: what it shows; its drawing and annotation are objects)
            for (const auto& name: v.getKeys()) {
                QPDFObjectHandle r = v.getKey(name);
                out << "marker /Layers " << name << " " << canon(r.getArrayItem(0)) << " drawing "
                    << r.getArrayItem(1).isStream() << " annotation " << r.getArrayItem(2).isDictionary() << "\n";
            }
            continue;
        }
        out << "marker " << key << " " << canon(v) << "\n";
    }
    // The embedded files: their names, data and how they are described
    for (const auto& [name, spec]: QPDFEmbeddedFileDocumentHelper(q).getEmbeddedFiles()) {
        QPDFObjectHandle s = spec->getObjectHandle();
        QPDFObjectHandle ef = s.getKey("/EF");
        out << "file " << name << " spec";
        for (const char* key: {"/Type", "/F", "/UF", "/Desc", "/AFRelationship"}) {
            out << " " << key << " " << canon(s.getKey(key));
        }
        out << " /EF /F=/UF " << (ef.getKey("/F").getObjGen() == ef.getKey("/UF").getObjGen()) << " stream "
            << canon(ef.getKey("/F")) << "\n";
    }
    // The text layer of the handwriting on each page: its stream, and the font it uses
    int page = 0;
    for (auto& p: QPDFPageDocumentHelper(q).getAllPages()) {
        ++page;
        QPDFObjectHandle contents = p.getObjectHandle().getKey("/Contents");
        std::vector<QPDFObjectHandle> streams =
                contents.isArray() ? contents.getArrayAsVector() : std::vector<QPDFObjectHandle>{contents};
        for (const auto& c: streams) {
            QPDFObjectHandle mark = c.isStream() ? c.getDict().getKey("/XournalQt") : QPDFObjectHandle::newNull();
            if (mark.isDictionary() && mark.hasKey("/InkText")) {
                out << "page " << page << " ink text " << canon(mark) << " " << streamData(c).size() << " bytes, md5 "
                    << md5Hex(streamData(c)) << "\n";
            }
        }
        QPDFObjectHandle fonts = p.getObjectHandle().getKey("/Resources").getKey("/Font");
        if (fonts.isDictionary() && fonts.hasKey("/XqtInkText")) {
            out << "page " << page << " font " << canon(fonts.getKey("/XqtInkText")) << "\n";
        }
    }
    return out.str();
}

class HybridMarkerTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        HybridPdf::compactAbove = 1000;  // (a small test file: never written anew for growing)
        audio::setAppFolder(path("app-audio"));
        test::writeFile(audio::appFolder() / RECORDING, std::string(4000, 'r') + "a recording");
        test::writeFile(audio::appFolder() / LATER, std::string(3000, 'l') + "a later recording");
        makePng(path("photo.png"));
    }
    void TearDown() override {
        HybridPdf::compactAbove = 0.25;
        audio::setAppFolder({});
    }
    fs::path path(const std::string& name) const {
        return fs::path(tmp.filePath(QString::fromStdString(name)).toStdString());
    }

    static void makePng(const fs::path& png) {
        GdkPixbuf* pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, false, 8, 40, 40);
        gdk_pixbuf_fill(pixbuf, 0x3366ccffU);
        ASSERT_TRUE(gdk_pixbuf_save(pixbuf, png.string().c_str(), "png", nullptr, nullptr));
        g_object_unref(pixbuf);
    }

    /// A lecture of three PDF pages with all a PDF with notes carries: ink on a PDF page, a ruled page (a drawn base
    /// page), a page with an attached image (a file next to the .xopp), a recording, a link in a Markdown box, space
    /// for notes.
    std::unique_ptr<Document> lecture() {
        test::makeTextPdf(path("lecture.pdf"), {"lectureone", "lecturetwo", "lecturethree"});
        auto loaded = DocumentSession::loadFile(path("lecture.pdf"));
        EXPECT_TRUE(loaded.document) << loaded.error;
        auto doc = std::move(loaded.document);
        addStroke(doc->getPage(0)->getSelectedLayer(), 200);
        auto ruled = std::make_shared<XojPage>(595, 842);
        ruled->setBackgroundType(PageType(PageTypeFormat::Ruled));
        addStroke(ruled->getSelectedLayer(), 300, Color(0xff0000ffU));
        doc->insertPage(ruled, 1);
        auto image = std::make_shared<XojPage>(595, 842);
        BackgroundImage img;
        GError* error = nullptr;
        img.loadFile(path("photo.png"), &error);
        EXPECT_EQ(error, nullptr);
        img.setAttach(true);
        image->setBackgroundImage(img);
        image->setBackgroundType(PageType(PageTypeFormat::Image));
        addStroke(image->getSelectedLayer(), 400);
        doc->addPage(image);
        {  // a stroke written while the recording ran
            auto s = std::make_unique<Stroke>();
            s->setWidth(1.5);
            s->addPoint(Point(100, 600));
            s->addPoint(Point(200, 630));
            audio::stamp(*s, RECORDING, 700);
            s->getBoundingBox();
            doc->getPage(2)->getSelectedLayer()->addElement(std::move(s));
        }
        auto* markdown = new Layer();
        markdown->setName(std::string(xoj::markdown::LAYER_NAME));
        doc->getPage(3)->getLayers().insert(doc->getPage(3)->getLayers().begin(), markdown);
        auto box = std::make_unique<Text>();
        box->setText("Read [the paper](https://example.org/paper) first.");
        box->setFont(XojFont("Sans", 10));
        box->setWrap(400);
        box->setTransformation(xoj::util::Matrix::TRANSLATION(56, 56));
        box->getBoundingBox();
        markdown->addElement(std::move(box));
        doc->getPage(3)->setNoteSpace(NoteSpace{0, 0, 120, 0});
        return doc;
    }

    /// The handwriting recognised on pages 1 and 3.
    static InkPages ink(const char* page3) {
        return {handwriting({100, 200}, {"Kalman", "filter"}), nullptr, handwriting({80, 600}, {page3})};
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
};

/// The second save: a stroke added to a page, the handwriting of another one read anew (InkPages), and (`recorded`)
/// a stroke written while a new recording ran.
void edit(Document& doc, bool recorded) {
    std::unique_lock lock(doc);
    addStroke(doc.getPage(0)->getSelectedLayer(), 500);
    if (recorded) {
        auto s = std::make_unique<Stroke>();
        s->setWidth(1.5);
        s->addPoint(Point(100, 700));
        s->addPoint(Point(200, 730));
        audio::stamp(*s, LATER, 300);
        s->getBoundingBox();
        doc.getPage(0)->getSelectedLayer()->addElement(std::move(s));
    }
}

}  // namespace

// A PDF with notes saved again (appended) carries what a full write of the same document carries
TEST_F(HybridMarkerTest, aSaveAppendedCarriesWhatAFullWriteCarries) {
    const fs::path out = path("notes.pdf");
    {
        auto doc = lecture();
        const InkPages first = ink("before");
        HybridPdf::WriteOptions o;
        o.inkText = &first;
        const auto r = HybridPdf::write(*doc, out, {}, npos, path("notes.xopp"), o);
        ASSERT_TRUE(r.ok) << r.error;
        EXPECT_FALSE(r.incremental);
    }
    auto opened = HybridPdf::open(out);
    ASSERT_TRUE(opened.document) << opened.error;
    Document& doc = *opened.document;
    edit(doc, /*recorded=*/true);
    const InkPages second = ink("after");
    HybridPdf::Revision rev = HybridPdf::revisionOf(opened.base, out);
    ASSERT_TRUE(rev.valid());
    HybridPdf::WriteOptions o;
    o.revision = &rev;
    o.inkText = &second;
    const auto r = HybridPdf::write(doc, out, {}, npos, path("notes.xopp"), o);
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_TRUE(r.incremental) << r.whyFull;

    // The same document written in full (under the same name: the .xopp names its PDF)
    const fs::path full = path("full/notes.pdf");
    fs::create_directories(full.parent_path());
    HybridPdf::WriteOptions f;
    f.inkText = &second;
    const auto w = HybridPdf::write(doc, full, {}, npos, path("full/notes.xopp"), f);
    ASSERT_TRUE(w.ok) << w.error;
    ASSERT_FALSE(w.incremental);

    const std::string appended = carried(out);
    EXPECT_EQ(appended, carried(full));
    // (what the comparison covered)
    for (const char* what: {"marker /InkText", "marker /InkFont", "marker /Files [(document.xopp.bg_1.png) ]",
                            "marker /Audio [(audio-p003-2026-10-07_09-00-00.ogg)", "marker /XoppExport",
                            "marker /Spaces [3 ]", "marker /Drawn [1 4 ]", "-link1", "file document.xopp spec",
                            "file document.xopp.bg_1.png", "file audio-p003-2026-10-07_09-00-00.ogg",
                            "file audio-p001-2026-10-07_10-00-00.ogg",
                            "page 1 ink text", "page 3 ink text", "page 1 font"}) {
        EXPECT_NE(appended.find(what), std::string::npos) << what << " in\n" << appended;
    }
}

// The same for an archive PDF (its layers in the page content, its files associated with it)
TEST_F(HybridMarkerTest, anArchiveSavedAgainCarriesWhatAFullWriteCarries) {
    const fs::path out = path("notes.archive.pdf");
    {
        auto doc = lecture();
        const InkPages first = ink("before");
        const auto r = HybridPdf::writeArchive(*doc, out, {}, npos, {}, &first);
        ASSERT_TRUE(r.ok) << r.error;
    }
    auto opened = HybridPdf::open(out);
    ASSERT_TRUE(opened.document) << opened.error;
    Document& doc = *opened.document;
    edit(doc, /*recorded=*/false);  // (a new recording: an archive PDF is written in full then)
    const InkPages second = ink("after");
    HybridPdf::Revision rev = HybridPdf::revisionOf(opened.base, out);
    ASSERT_TRUE(rev.valid());
    HybridPdf::WriteOptions o;
    o.revision = &rev;
    o.inkText = &second;
    const auto r = HybridPdf::write(doc, out, {}, npos, {}, o);
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_TRUE(r.incremental) << r.whyFull;
    EXPECT_TRUE(r.pdfa);

    const fs::path full = path("full/notes.archive.pdf");
    fs::create_directories(full.parent_path());
    const auto w = HybridPdf::writeArchive(doc, full, {}, npos, {}, &second);
    ASSERT_TRUE(w.ok) << w.error;

    const std::string appended = carried(out);
    EXPECT_EQ(appended, carried(full));
    for (const char* what: {"marker /Archive true", "marker /Flattened", "marker /InkText", "/AFRelationship /Source",
                            "/AFRelationship /Supplement", "page 1 ink text"}) {
        EXPECT_NE(appended.find(what), std::string::npos) << what << " in\n" << appended;
    }
}
