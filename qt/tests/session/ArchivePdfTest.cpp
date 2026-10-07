/*
 * xournal-qt: the archive PDF (PDF/A-3b; qt/docs/features/hybrid-pdf.md, "Archive PDF"): the ink in the pages, the
 * document as its source, and it opens again editable.
 *
 * @license GNU GPLv2 or later
 */
#include "HybridPdfTestSupport.h"

using namespace xqt;
using namespace xqt::test::hybrid;
using xqt::test::makeTextPdf;
using xqt::test::readFile;

namespace {
/// A PDF made by hand with qpdf: one page per (content, resources); `extra` changes it before it is written.
void makeRawPdf(const fs::path& p, const std::vector<std::pair<std::string, std::string>>& pages,
                const std::function<void(QPDF&)>& extra = {}) {
    QPDF q;
    q.emptyPDF();
    for (const auto& [content, resources]: pages) {
        QPDFObjectHandle page = q.makeIndirectObject(
                QPDFObjectHandle::parse("<< /Type /Page /MediaBox [0 0 595 842] /Resources " + resources + " >>"));
        page.replaceKey("/Contents", QPDFObjectHandle::newStream(&q, content));
        QPDFPageDocumentHelper(q).addPage(page, false);
    }
    if (extra) {
        extra(q);
    }
    QPDFWriter w(q, p.string().c_str());
    w.write();
}

/// The colour of a pixel (r, g, b).
std::array<int, 3> pixel(cairo_surface_t* s, int x, int y) {
    const unsigned char* p = cairo_image_surface_get_data(s) + y * cairo_image_surface_get_stride(s) + 4 * x;
    return {p[2], p[1], p[0]};
}

int elementsOf(Document& doc, size_t page, size_t layer) {
    const auto& v = doc.getPage(page)->getLayers()[layer]->getElementsView();
    return static_cast<int>(std::distance(v.begin(), v.end()));
}
}  // namespace

TEST_F(ArchivePdfTest, writesAPdfA3bWithTheInkInThePagesAndTheDataAsItsSource) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path out = path("lecture.archive.pdf");
    const auto r = HybridPdf::writeArchive(*doc, out);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.pdfa);
    EXPECT_TRUE(r.notPdfA.empty()) << r.notPdfA.front();
    EXPECT_EQ(r.pages, 4u);
    EXPECT_EQ(r.flattened, 4u) << "pen, highlighter, text, ruled page";
    EXPECT_EQ(r.annotations, 0u);
    keepSample(out, "archive-lecture.pdf");
    EXPECT_TRUE(HybridPdf::isHybrid(out));
    EXPECT_TRUE(HybridPdf::isArchive(out));
    EXPECT_FALSE(HybridPdf::isArchive(path("lecture.pdf")));

    int code = -1;
    const std::string check = qpdfCheck(out, code);
    EXPECT_EQ(code, 0) << check;
    EXPECT_NE(check.find("No syntax or stream encoding errors"), std::string::npos) << check;

    QPDF q;
    q.processFile(out.string().c_str());
    EXPECT_GE(q.getPDFVersion(), std::string("1.7"));
    EXPECT_FALSE(q.isEncrypted());
    EXPECT_TRUE(q.getTrailer().getKey("/ID").isArray());
    QPDFObjectHandle root = q.getRoot();
    // The ink is page content: no annotations, the layers drawn by a marked stream after the page's own
    auto pages = QPDFPageDocumentHelper(q).getAllPages();
    ASSERT_EQ(pages.size(), 4u);
    for (size_t i = 0; i < 3; ++i) {  // (page 4 has no notes)
        EXPECT_TRUE(pages[i].getAnnotations().empty());
        QPDFObjectHandle contents = pages[i].getObjectHandle().getKey("/Contents");
        ASSERT_TRUE(contents.isArray());
        ASSERT_GE(contents.getArrayNItems(), 3);
        EXPECT_TRUE(contents.getArrayItem(0).getDict().getKey("/XournalQt").isDictionary());
        QPDFObjectHandle last = contents.getArrayItem(contents.getArrayNItems() - 1);
        ASSERT_TRUE(last.getDict().getKey("/XournalQt").isDictionary());
        EXPECT_NE(streamText(last).find("/XqtInk1 Do"), std::string::npos);
    }
    EXPECT_FALSE(pages[3].getObjectHandle().getKey("/Contents").isArray()) << "a page without notes is as it was";
    // The marker, and the data as the PDF's source (an associated file)
    QPDFObjectHandle marker = root.getKey("/XournalQt");
    ASSERT_TRUE(marker.isDictionary());
    EXPECT_EQ(marker.getKey("/Version").getIntValue(), HybridPdf::ARCHIVE_FORMAT_VERSION);
    EXPECT_TRUE(marker.getKey("/Archive").getBoolValue());
    EXPECT_EQ(marker.getKey("/Flattened").getArrayNItems(), 4);
    QPDFObjectHandle af = root.getKey("/AF");
    ASSERT_TRUE(af.isArray());
    ASSERT_EQ(af.getArrayNItems(), 1);
    QPDFObjectHandle spec = af.getArrayItem(0);
    EXPECT_EQ(spec.getKey("/UF").getUTF8Value(), HybridPdf::DATA_NAME);
    EXPECT_EQ(spec.getKey("/F").getUTF8Value(), HybridPdf::DATA_NAME);
    EXPECT_EQ(spec.getKey("/AFRelationship").getName(), "/Source");
    QPDFObjectHandle ef = spec.getKey("/EF").getKey("/F");
    EXPECT_EQ(ef.getDict().getKey("/Subtype").getName(), "/application/x-xopp");
    EXPECT_TRUE(ef.getDict().getKey("/Params").getKey("/ModDate").isString());
    EXPECT_TRUE(QPDFEmbeddedFileDocumentHelper(q).getEmbeddedFile(HybridPdf::DATA_NAME));
    // The output intent: sRGB with its profile
    QPDFObjectHandle intents = root.getKey("/OutputIntents");
    ASSERT_TRUE(intents.isArray());
    ASSERT_EQ(intents.getArrayNItems(), 1);
    QPDFObjectHandle intent = intents.getArrayItem(0);
    EXPECT_EQ(intent.getKey("/S").getName(), "/GTS_PDFA1");
    QPDFObjectHandle icc = intent.getKey("/DestOutputProfile");
    EXPECT_EQ(icc.getDict().getKey("/N").getIntValue(), 3);
    EXPECT_EQ(streamText(icc), ArchivePdf::srgbProfile());
    EXPECT_EQ(ArchivePdf::srgbProfile().size(), 3268u);
    EXPECT_EQ(ArchivePdf::srgbProfile().substr(36, 4), "acsp");
    // The metadata: unfiltered XMP that says PDF/A-3b, with the title of the document information
    QPDFObjectHandle meta = root.getKey("/Metadata");
    ASSERT_TRUE(meta.isStream());
    EXPECT_FALSE(meta.getDict().hasKey("/Filter")) << "PDF/A: the metadata stream is not compressed";
    const std::string xmp = streamText(meta);
    EXPECT_NE(xmp.find("<pdfaid:part>3</pdfaid:part>"), std::string::npos) << xmp;
    EXPECT_NE(xmp.find("<pdfaid:conformance>B</pdfaid:conformance>"), std::string::npos);
    QPDFObjectHandle info = q.getTrailer().getKey("/Info");
    EXPECT_EQ(info.getKey("/Title").getUTF8Value(), "lecture");
    EXPECT_NE(xmp.find("<rdf:li xml:lang=\"x-default\">lecture</rdf:li>"), std::string::npos);
    const std::string date = info.getKey("/ModDate").getUTF8Value();  // D:YYYYMMDDHHmmSS+00'00'
    ASSERT_GE(date.size(), 16u);
    EXPECT_NE(xmp.find("<xmp:ModifyDate>" + date.substr(2, 4) + "-" + date.substr(6, 2) + "-" + date.substr(8, 2) +
                       "T" + date.substr(10, 2) + ":" + date.substr(12, 2) + ":" + date.substr(14, 2) + "+00:00"),
              std::string::npos);
    EXPECT_EQ(info.getKey("/Creator").getUTF8Value(), std::string(PROJECT_STRING));
    EXPECT_NE(xmp.find("<xmp:CreatorTool>" + std::string(PROJECT_STRING) + "</xmp:CreatorTool>"), std::string::npos);
    // The text of the PDF is still there
    XojPdfDocument pdf;
    ASSERT_TRUE(pdf.load(out, "", nullptr));
    EXPECT_FALSE(pdf.getPage(0)->findText("lectureone").empty());
    EXPECT_FALSE(pdf.getPage(2)->findText("lecturetwo").empty());
}

TEST_F(ArchivePdfTest, popplerShowsItLikeOurPdfExport) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path archive = path("lecture.archive.pdf"), exported = path("export.pdf");
    ASSERT_TRUE(HybridPdf::writeArchive(*doc, archive).ok);
    ExportHelper::exportPdf(doc.get(), exported, nullptr, nullptr, EXPORT_BACKGROUND_ALL, false);
    for (size_t i = 0; i < 4; ++i) {
        cairo_surface_t* a = render(archive, i);
        cairo_surface_t* b = render(exported, i);
        const Diff d = compare(a, b);
        EXPECT_LT(d.mean, 0.5) << "page " << i + 1;
        EXPECT_LT(d.differ, 0.002) << "page " << i + 1;
        if (i == 0) {
            EXPECT_TRUE(inked(a, 140, 220, 160, 240)) << "the pen stroke is drawn";
        }
        cairo_surface_destroy(a);
        cairo_surface_destroy(b);
    }
}

TEST_F(ArchivePdfTest, reopensEditableWithoutTheInkInItsBackground) {
    auto doc = annotated(path("lecture.pdf"));
    const std::string before = describeAsXopp(*doc, path("reference.xopp"));
    const fs::path out = path("lecture.archive.pdf");
    ASSERT_TRUE(HybridPdf::writeArchive(*doc, out).ok);

    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document) << loaded.error;
    EXPECT_TRUE(loaded.hybrid);
    EXPECT_TRUE(loaded.hybridChanged.empty());
    EXPECT_EQ(describe(*loaded.document), before) << "the embedded .xopp round-trips";
    // The background: the original pages, pixel for pixel (no trace of the ink)
    const fs::path base = loaded.document->getPdfFilepath();
    for (auto [basePage, original]: {std::pair<size_t, size_t>{0, 0}, {2, 1}, {3, 2}}) {
        cairo_surface_t* a = render(base, basePage);
        cairo_surface_t* b = render(path("lecture.pdf"), original);
        const Diff d = compare(a, b);
        EXPECT_EQ(d.mean, 0) << "page " << basePage + 1;
        cairo_surface_destroy(a);
        cairo_surface_destroy(b);
    }
    {  // the ruled page: its lines, not the blue stroke
        cairo_surface_t* ruled = render(base, 1);
        const auto c = pixel(ruled, 350, 325);
        EXPECT_FALSE(c[2] > 150 && c[0] < 100) << "no stroke at 350,325";
        cairo_surface_destroy(ruled);
    }
    {  // nothing of ours left in the clean copy
        QPDF clean;
        clean.processFile(base.string().c_str());
        EXPECT_FALSE(clean.getRoot().hasKey("/XournalQt"));
        EXPECT_FALSE(clean.getRoot().hasKey("/AF"));
        EXPECT_FALSE(clean.getRoot().hasKey("/OutputIntents"));
        EXPECT_FALSE(clean.getRoot().hasKey("/Metadata")) << "(it would still say PDF/A)";
        for (auto& page: QPDFPageDocumentHelper(clean).getAllPages()) {
            QPDFObjectHandle c = page.getObjectHandle().getKey("/Contents");
            for (int i = 0; c.isArray() && i < c.getArrayNItems(); ++i) {
                EXPECT_FALSE(c.getArrayItem(i).getDict().hasKey("/XournalQt"));
            }
            QPDFObjectHandle x = page.getObjectHandle().getKey("/Resources").getKey("/XObject");
            EXPECT_FALSE(x.isDictionary() && x.hasKey("/XqtInk1"));
        }
    }

    // Erase the pen stroke and save (Ctrl+S): still an archive PDF, without the stroke in its pages and its data
    DocumentSession session(*app, std::move(loaded.document));
    {
        std::unique_lock lock(*session.getDocument());
        Layer* layer = session.getDocument()->getPage(0)->getLayers()[0];
        ASSERT_EQ(layer->getElementsView().size(), 1u);
        layer->removeElement(*layer->getElementsView().begin());
    }
    const auto saved = session.save();
    ASSERT_TRUE(saved.ok) << saved.error;
    EXPECT_TRUE(HybridPdf::isArchive(out));
    cairo_surface_t* page = render(out, 0);
    EXPECT_FALSE(inked(page, 140, 220, 160, 240)) << "the stroke is gone from the page";
    EXPECT_TRUE(inked(page, 60, 842 - 100, 220, 842 - 90) || inked(page, 60, 90, 220, 100)) << "the highlighter stays";
    cairo_surface_destroy(page);
    auto again = DocumentSession::loadFile(out);
    ASSERT_TRUE(again.document) << again.error;
    EXPECT_EQ(elementsOf(*again.document, 0, 0), 0) << "and from the embedded .xopp";
    EXPECT_EQ(elementsOf(*again.document, 0, 1), 1);
    EXPECT_TRUE(again.hybridChanged.empty());
    int code = -1;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);
}

TEST_F(ArchivePdfTest, contentAnotherAppAddedStaysInTheBackground) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path out = path("lecture.archive.pdf");
    ASSERT_TRUE(HybridPdf::writeArchive(*doc, out).ok);
    editWithQpdf(out, [](QPDF& q) {  // a black square appended to page 1's content, as another app would
        QPDFObjectHandle page = QPDFPageDocumentHelper(q).getAllPages().at(0).getObjectHandle();
        QPDFObjectHandle contents = QPDFObjectHandle::newArray();
        QPDFObjectHandle old = page.getKey("/Contents");
        for (int i = 0; i < old.getArrayNItems(); ++i) {
            contents.appendItem(old.getArrayItem(i));
        }
        contents.appendItem(QPDFObjectHandle::newStream(&q, "q 0 0 0 rg 500 700 40 40 re f Q\n"));
        page.replaceKey("/Contents", contents);
    });
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document) << loaded.error;
    EXPECT_TRUE(loaded.hybridChanged.empty());
    cairo_surface_t* bg = render(loaded.document->getPdfFilepath(), 0);
    EXPECT_TRUE(inked(bg, 510, 842 - 730, 530, 842 - 710)) << "the other app's square stays";
    EXPECT_FALSE(inked(bg, 140, 220, 160, 240)) << "our stroke does not";
    cairo_surface_destroy(bg);

    // A page whose content another app rewrote as one stream: our marked streams are gone, that is reported
    editWithQpdf(out, [](QPDF& q) {
        QPDFObjectHandle page = QPDFPageDocumentHelper(q).getAllPages().at(1).getObjectHandle();
        page.replaceKey("/Contents", QPDFObjectHandle::newStream(&q, "0 0 1 RG 300 542 m 400 492 l S\n"));
    });
    auto changed = DocumentSession::loadFile(out);
    ASSERT_TRUE(changed.document);
    EXPECT_EQ(changed.hybridChanged, (std::vector<std::string>{HybridPdf::nameOf(1, 0)}));
}

TEST_F(ArchivePdfTest, notesWithoutAPdfTextAndLinksArePdfA) {
    makeTextPdf(path("lecture.pdf"), {"lectureone", "lecturetwo", "lecturethree"});
    Document doc(nullptr);
    auto page = std::make_shared<XojPage>(595, 842);
    page->setBackgroundType(PageType(PageTypeFormat::Graph));
    addStroke(page->getSelectedLayer(), StrokeTool::HIGHLIGHTER, Color(0xffffff00U), 12, {Point(60, 300), Point(220, 300)});
    addText(page->getSelectedLayer(), "Grüße, «archive»", 80, 400);
    auto* markdown = new Layer();
    markdown->setName(std::string(xoj::markdown::LAYER_NAME));
    page->getLayers().insert(page->getLayers().begin(), markdown);
    auto box = std::make_unique<Text>();
    box->setText("See [the PDF](lecture.pdf#page=3) and [the web](https://example.org/x).");
    box->setFont(XojFont("Sans", 10));
    box->setWrap(400);
    box->setTransformation(xoj::util::Matrix::TRANSLATION(56, 56));
    markdown->addElement(std::move(box));
    doc.addPage(page);
    for (const Layer* l: page->getLayers()) {
        for (const auto& e: l->getElementsView()) {
            e->getBoundingBox();
        }
    }
    fs::create_directories(path("Archive"));
    const fs::path out = path("Archive/notes.pdf");
    // Linked from its own folder, into the archive: the PDF is archived as "Archive/lecture.pdf"
    HybridPdf::LinkMap links;
    links.from = path("");
    links.archived = [&](const fs::path& f) {
        return f.filename() == "lecture.pdf" ? path("Archive/lecture.pdf") : fs::path();
    };
    const auto r = HybridPdf::writeArchive(doc, out, {}, npos, links);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.pdfa) << (r.notPdfA.empty() ? "" : r.notPdfA.front());
    keepSample(out, "archive-notes.pdf");
    int code = -1;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);

    QPDF q;
    q.processFile(out.string().c_str());
    std::vector<std::string> actions;
    for (auto& a: QPDFPageDocumentHelper(q).getAllPages().at(0).getAnnotations()) {
        QPDFObjectHandle o = a.getObjectHandle();
        ASSERT_EQ(o.getKey("/Subtype").getName(), "/Link") << "the only annotations are links";
        EXPECT_EQ(o.getKey("/F").getIntValue() & 4, 4) << "printed (PDF/A)";
        QPDFObjectHandle act = o.getKey("/A");
        actions.push_back(act.getKey("/S").getName() == "/URI"
                                  ? "URI " + act.getKey("/URI").getStringValue()
                                  : "GoToR " + act.getKey("/F").getUTF8Value() + " " +
                                            std::to_string(act.getKey("/D").getArrayItem(0).getIntValue()));
    }
    EXPECT_EQ(actions, (std::vector<std::string>{"GoToR lecture.pdf 2", "URI https://example.org/x"}));
    auto opened = DocumentSession::loadFile(out);
    ASSERT_TRUE(opened.document) << opened.error;
    EXPECT_EQ(elementsOf(*opened.document, 0, 1), 2);
}

TEST_F(ArchivePdfTest, aSourcePdfThatCannotConformIsWrittenButNotCalledPdfA) {
    // Page 1: Helvetica, not embedded; page 2: CMYK colours
    makeRawPdf(path("old.pdf"),
               {{"BT /F1 24 Tf 72 700 Td (Hello) Tj ET\n",
                 "<< /Font << /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> >>"},
                {"0 1 1 0 k 100 100 200 200 re f\n", "<< >>"}});
    auto loaded = DocumentSession::loadFile(path("old.pdf"));
    ASSERT_TRUE(loaded.document) << loaded.error;
    addStroke(loaded.document->getPage(0)->getSelectedLayer(), StrokeTool::PEN, Color(0xffcc0000U), 2,
              {Point(100, 200), Point(200, 250)});
    loaded.document->getPage(0)->getSelectedLayer()->getElementsView().front()->getBoundingBox();
    const fs::path out = path("old.archive.pdf");
    const auto r = HybridPdf::writeArchive(*loaded.document, out);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_FALSE(r.pdfa);
    ASSERT_EQ(r.notPdfA.size(), 2u);
    EXPECT_NE(r.notPdfA[0].find("not embedded: Helvetica"), std::string::npos) << r.notPdfA[0];
    EXPECT_NE(r.notPdfA[1].find("CMYK"), std::string::npos) << r.notPdfA[1];
    EXPECT_NE(r.notPdfA[1].find("(page 2)"), std::string::npos) << r.notPdfA[1];
    int code = -1;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);
    QPDF q;
    q.processFile(out.string().c_str());
    const std::string xmp = streamText(q.getRoot().getKey("/Metadata"));
    EXPECT_EQ(xmp.find("pdfaid:part"), std::string::npos) << "no claim of PDF/A";
    EXPECT_TRUE(q.getRoot().getKey("/OutputIntents").isArray());
    EXPECT_TRUE(HybridPdf::isArchive(out)) << "still an archive PDF the app opens for editing";
    auto opened = DocumentSession::loadFile(out);
    ASSERT_TRUE(opened.document) << opened.error;
    EXPECT_EQ(elementsOf(*opened.document, 0, 0), 1);
}

TEST_F(ArchivePdfTest, whatCanBeRepairedIsRepaired) {
    makeRawPdf(path("scripted.pdf"),
               {{"q 0 0 1 rg 100 100 50 50 re f Q q 20 0 0 20 300 300 cm /Im1 Do Q\n",
                 "<< /XObject << /Im1 << /Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceRGB "
                 "/BitsPerComponent 8 /Interpolate true /Length 3 >> >> >>"}},
               [](QPDF& q) {
                   QPDFObjectHandle root = q.getRoot();
                   root.replaceKey("/OpenAction", QPDFObjectHandle::parse("<< /S /JavaScript /JS (app.alert(1)) >>"));
                   QPDFObjectHandle page = QPDFPageDocumentHelper(q).getAllPages().at(0).getObjectHandle();
                   page.replaceKey("/Annots",
                                   QPDFObjectHandle::parse("[ << /Type /Annot /Subtype /Link /Rect [100 100 150 150] "
                                                           "/A << /S /URI /URI (https://example.org) >> >> ]"));
                   // the image's pixel
                   QPDFObjectHandle im = page.getKey("/Resources").getKey("/XObject").getKey("/Im1");
                   QPDFObjectHandle image = QPDFObjectHandle::newStream(&q, std::string("\xff\x00\x00", 3));
                   image.replaceDict(im);
                   image.getDict().removeKey("/Length");
                   page.getKey("/Resources").getKey("/XObject").replaceKey("/Im1", image);
               });
    auto loaded = DocumentSession::loadFile(path("scripted.pdf"));
    ASSERT_TRUE(loaded.document) << loaded.error;
    const fs::path out = path("scripted.archive.pdf");
    const auto r = HybridPdf::writeArchive(*loaded.document, out);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.pdfa) << (r.notPdfA.empty() ? "" : r.notPdfA.front());
    ASSERT_EQ(r.adjusted.size(), 1u);
    EXPECT_NE(r.adjusted[0].find("JavaScript"), std::string::npos);
    keepSample(out, "archive-repaired.pdf");
    QPDF q;
    q.processFile(out.string().c_str());
    EXPECT_FALSE(q.getRoot().hasKey("/OpenAction"));
    QPDFObjectHandle page = QPDFPageDocumentHelper(q).getAllPages().at(0).getObjectHandle();
    EXPECT_FALSE(page.getKey("/Resources").getKey("/XObject").getKey("/Im1").getDict().getKey("/Interpolate").getBoolValue());
    QPDFObjectHandle link = page.getKey("/Annots").getArrayItem(0);
    EXPECT_EQ(link.getKey("/F").getIntValue(), 4) << "printed";
    EXPECT_EQ(link.getKey("/A").getKey("/URI").getStringValue(), "https://example.org") << "a web link stays";
}

/// XQT_ARCHIVE_SOURCE=<pdf> [XQT_ARCHIVE_SAMPLES=<folder>]: that PDF with a stroke on its first page as an archive PDF,
/// with the report on stdout (to check the checks against veraPDF on real PDFs).
TEST_F(ArchivePdfTest, archiveOfAGivenPdf) {
    const char* source = std::getenv("XQT_ARCHIVE_SOURCE");
    if (!source) {
        GTEST_SKIP() << "XQT_ARCHIVE_SOURCE not set";
    }
    auto loaded = DocumentSession::loadFile(source);
    ASSERT_TRUE(loaded.document) << loaded.error;
    addStroke(loaded.document->getPage(0)->getSelectedLayer(), StrokeTool::PEN, Color(0xffcc0000U), 2,
              {Point(100, 200), Point(200, 250)});
    loaded.document->getPage(0)->getSelectedLayer()->getElementsView().front()->getBoundingBox();
    const fs::path out = path("given.archive.pdf");
    const auto start = std::chrono::steady_clock::now();
    const auto r = HybridPdf::writeArchive(*loaded.document, out);
    ASSERT_TRUE(r.ok) << r.error;
    std::cout << "archive: " << (r.pdfa ? "PDF/A-3b" : "not PDF/A") << ", "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << " s\n";
    for (const auto& p: r.notPdfA) {
        std::cout << "  not PDF/A: " << p << "\n";
    }
    for (const auto& a: r.adjusted) {
        std::cout << "  adjusted: " << a << "\n";
    }
    keepSample(out, (fs::path(source).stem().string() + ".archive.pdf").c_str());
}

// A hybrid PDF is never PDF/A (its annotations and attachment break it): a PDF/A source's identification is dropped
// from the metadata it keeps; an archive of the same source is PDF/A again.
TEST_F(ArchivePdfTest, aHybridPdfOfAPdfASourceDoesNotClaimPdfA) {
    makeTextPdf(path("source.pdf"), {"lectureone"});
    {  // the source says it is PDF/A-2b (in both forms XMP allows), with an output intent
        auto loaded = DocumentSession::loadFile(path("source.pdf"));
        ASSERT_TRUE(loaded.document);
        ASSERT_TRUE(HybridPdf::writeArchive(*loaded.document, path("pdfa.pdf")).ok);
        editWithQpdf(path("pdfa.pdf"), [](QPDF& q) {
            QPDFObjectHandle root = q.getRoot();
            for (const char* key: {"/XournalQt", "/AF", "/Names"}) {
                root.removeKey(key);  // (a plain PDF/A, not ours)
            }
            QPDFObjectHandle meta = root.getKey("/Metadata");
            std::string xmp = streamText(meta);
            const auto at = xmp.find("<pdfaid:part>3</pdfaid:part>");
            ASSERT_NE(at, std::string::npos);
            xmp.replace(at, std::string("<pdfaid:part>3</pdfaid:part>").size(),
                        "<pdfaid:part>2</pdfaid:part><dc:rights>kept</dc:rights>");
            const auto d = xmp.find("<rdf:Description rdf:about=\"\"");
            xmp.insert(d + std::string("<rdf:Description").size(), " pdfaid:amd=\"2005\"");
            meta.replaceStreamData(xmp, QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull());
        });
    }
    auto loaded = DocumentSession::loadFile(path("pdfa.pdf"));
    ASSERT_TRUE(loaded.document) << loaded.error;
    EXPECT_FALSE(loaded.hybrid);
    addStroke(loaded.document->getPage(0)->getSelectedLayer(), StrokeTool::PEN, Color(0xffcc0000U), 2,
              {Point(100, 200), Point(200, 250)});
    loaded.document->getPage(0)->getSelectedLayer()->getElementsView().front()->getBoundingBox();

    ASSERT_TRUE(HybridPdf::write(*loaded.document, path("hybrid.pdf")).ok);
    QPDF hybrid;
    hybrid.processFile(path("hybrid.pdf").string().c_str());
    const std::string xmp = streamText(hybrid.getRoot().getKey("/Metadata"));
    EXPECT_EQ(xmp.find("pdfaid:part"), std::string::npos) << xmp;
    EXPECT_EQ(xmp.find("pdfaid:conformance"), std::string::npos);
    EXPECT_EQ(xmp.find("pdfaid:amd"), std::string::npos);
    EXPECT_NE(xmp.find("<dc:rights>kept</dc:rights>"), std::string::npos) << "the rest of the metadata stays";

    const auto archive = HybridPdf::writeArchive(*loaded.document, path("again.archive.pdf"));
    ASSERT_TRUE(archive.ok) << archive.error;
    EXPECT_TRUE(archive.pdfa);
    QPDF a;
    a.processFile(path("again.archive.pdf").string().c_str());
    EXPECT_NE(streamText(a.getRoot().getKey("/Metadata")).find("<pdfaid:part>3</pdfaid:part>"), std::string::npos);
}
