/*
 * xournal-qt: the hybrid PDF (qt/docs/features/hybrid-pdf.md): the file is valid, other apps see our drawing as we draw
 * it, and it opens again as the same document.
 *
 * @license GNU GPLv2 or later
 */
#include "HybridPdfTestSupport.h"

using namespace xqt;
using namespace xqt::test::hybrid;
using xqt::test::makeTextPdf;
using xqt::test::readFile;

TEST_F(HybridPdfTest, writesAValidPdfWithOurAnnotationsDataAndMarker) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path out = path("lecture.notes.pdf");
    const auto r = HybridPdf::write(*doc, out);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.pages, 4u);
    EXPECT_EQ(r.annotations, 4u) << "one per layer with content: pen, highlighter, text, ruled page";

    if (const char* sample = std::getenv("XQT_HYBRID_SAMPLE")) {  // a sample for other PDF apps
        std::error_code ec;
        fs::copy_file(out, sample, fs::copy_options::overwrite_existing, ec);
    }
    int code = -1;
    const std::string report = qpdfCheck(out, code);
    EXPECT_EQ(code, 0) << report;
    EXPECT_NE(report.find("No syntax or stream encoding errors"), std::string::npos) << report;

    QPDF q;
    q.processFile(out.string().c_str());
    QPDFObjectHandle marker = q.getRoot().getKey("/XournalQt");
    ASSERT_TRUE(marker.isDictionary());
    EXPECT_EQ(marker.getKey("/Version").getIntValue(), HybridPdf::FORMAT_VERSION);
    EXPECT_EQ(marker.getKey("/Annots").getKeys().size(), 4u);
    EXPECT_TRUE(QPDFEmbeddedFileDocumentHelper(q).getEmbeddedFile(HybridPdf::DATA_NAME));
    EXPECT_FALSE(q.getTrailer().getKey("/Info").hasKey("/XournalQtPages")) << "never a merged PDF";

    auto pages = QPDFPageDocumentHelper(q).getAllPages();
    ASSERT_EQ(pages.size(), 4u);
    std::vector<std::string> kinds;
    for (auto& page: pages) {
        std::string k;
        for (auto& a: page.getAnnotations()) {
            QPDFObjectHandle o = a.getObjectHandle();
            k += o.getKey("/Subtype").getName() + " ";
            EXPECT_EQ(o.getKey("/NM").getUTF8Value().rfind(HybridPdf::NAME_PREFIX, 0), 0u);
            EXPECT_TRUE(o.getKey("/AP").getKey("/N").isStream());
            EXPECT_TRUE(o.hasKey("/XournalQt"));
        }
        kinds.push_back(k);
    }
    EXPECT_EQ(kinds, (std::vector<std::string>{"/Ink /Ink ", "/Ink ", "/Stamp ", ""}));
    // The pen stroke's points, in PDF space (y up)
    QPDFObjectHandle ink = pages[0].getAnnotations()[0].getObjectHandle().getKey("/InkList").getArrayItem(0);
    ASSERT_EQ(ink.getArrayNItems(), 8);
    EXPECT_NEAR(ink.getArrayItem(0).getNumericValue(), 100, 0.05);
    EXPECT_NEAR(ink.getArrayItem(1).getNumericValue(), 842 - 200, 0.05);
    EXPECT_EQ(pages[2].getAnnotations()[0].getObjectHandle().getKey("/Contents").getUTF8Value(),
              "a note in the margin");
    // The text of the PDF is still there, on the right pages
    XojPdfDocument pdf;
    ASSERT_TRUE(pdf.load(out, "", nullptr));
    EXPECT_FALSE(pdf.getPage(0)->findText("lectureone").empty());
    EXPECT_TRUE(pdf.getPage(1)->findText("lecturetwo").empty()) << "the ruled page";
    EXPECT_FALSE(pdf.getPage(2)->findText("lecturetwo").empty());
    EXPECT_FALSE(pdf.getPage(3)->findText("lecturethree").empty());
}

TEST_F(HybridPdfTest, popplerShowsItLikeOurPdfExport) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path hybrid = path("hybrid.pdf"), exported = path("export.pdf");
    ASSERT_TRUE(HybridPdf::write(*doc, hybrid).ok);
    ExportHelper::exportPdf(doc.get(), exported, nullptr, nullptr, EXPORT_BACKGROUND_ALL, false);
    for (size_t i = 0; i < 4; ++i) {
        cairo_surface_t* a = render(hybrid, i);
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

TEST_F(HybridPdfTest, theEmbeddedDocumentOpensWithTheCleanCopyAsBackground) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path out = path("lecture.notes.pdf");
    ASSERT_TRUE(HybridPdf::write(*doc, out).ok);
    EXPECT_TRUE(HybridPdf::isHybrid(out));
    EXPECT_FALSE(HybridPdf::isHybrid(path("lecture.pdf")));

    auto opened = HybridPdf::open(out);
    ASSERT_TRUE(opened.document) << opened.error;
    EXPECT_TRUE(opened.changed.empty());
    Document& back = *opened.document;
    EXPECT_EQ(back.getFilepath(), out);
    EXPECT_EQ(back.getPdfFilepath(), opened.base);
    EXPECT_TRUE(HybridPdf::inCache(opened.base));
    ASSERT_EQ(back.getPageCount(), 4u);
    EXPECT_EQ(back.getPdfPageCount(), 4u);
    // The clean copy: no annotations of ours, no data, no marker; valid
    QPDF clean;
    clean.processFile(opened.base.string().c_str());
    EXPECT_FALSE(clean.getRoot().hasKey("/XournalQt"));
    EXPECT_FALSE(QPDFEmbeddedFileDocumentHelper(clean).hasEmbeddedFiles());
    for (auto& page: QPDFPageDocumentHelper(clean).getAllPages()) {
        EXPECT_TRUE(page.getAnnotations().empty());
    }
    int code = -1;
    EXPECT_EQ((qpdfCheck(opened.base, code), code), 0);
}

// --- reading ------------------------------------------------------------------------------------------------------

TEST_F(HybridPdfTest, opensAsTheSameDocument) {
    auto doc = annotated(path("lecture.pdf"));
    const std::string before = describeAsXopp(*doc, path("reference.xopp"));
    const fs::path out = path("lecture.notes.pdf");
    ASSERT_TRUE(HybridPdf::write(*doc, out).ok);

    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document) << loaded.error;
    EXPECT_TRUE(loaded.hybrid);
    EXPECT_TRUE(loaded.hybridChanged.empty());
    EXPECT_TRUE(loaded.warnings.empty());
    EXPECT_EQ(describe(*loaded.document), before);
    EXPECT_EQ(loaded.document->getFilepath(), out);

    // Opened in a tab, changed and saved: a hybrid PDF again, with both strokes
    DocumentSession session(*app, std::move(loaded.document));
    EXPECT_TRUE(session.isHybrid());
    {
        std::unique_lock lock(*session.getDocument());
        addStroke(session.getDocument()->getPage(3)->getSelectedLayer(), StrokeTool::PEN, Color(0xff008000U), 1.41,
                  {Point(10, 10, 1), Point(90, 90, 2)});
    }
    const auto r = session.save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(HybridPdf::isHybrid(out));
    auto again = DocumentSession::loadFile(out);
    ASSERT_TRUE(again.document);
    EXPECT_EQ(describe(*again.document), describeAsXopp(*session.getDocument(), path("reference2.xopp")));
    EXPECT_NE(describe(*again.document), before);
}

TEST_F(HybridPdfTest, plainPdfsAndXoppFilesOpenAsBefore) {
    makeTextPdf(path("plain.pdf"), {"alpha", "beta"});
    auto loaded = DocumentSession::loadFile(path("plain.pdf"));
    ASSERT_TRUE(loaded.document);
    EXPECT_FALSE(loaded.hybrid);
    EXPECT_TRUE(loaded.document->getFilepath().empty());
    EXPECT_EQ(loaded.document->getPdfFilepath(), path("plain.pdf"));
    EXPECT_EQ(loaded.document->getPageCount(), 2u);

    DocumentSession session(*app, std::move(loaded.document));
    EXPECT_FALSE(session.isHybrid());
    ASSERT_TRUE(session.saveAs(path("plain.xopp")).ok);
    EXPECT_FALSE(session.isHybrid()) << ".xopp stays the format of Save";
    EXPECT_FALSE(HybridPdf::isHybrid(path("plain.pdf")));
}

TEST_F(HybridPdfTest, annotationsOfOtherAppsStay) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path out = path("lecture.notes.pdf");
    ASSERT_TRUE(HybridPdf::write(*doc, out).ok);
    editWithQpdf(out, [](QPDF& q) {  // comments on page 4 and on the ruled page 2, as another app adds them
        for (int n: {3, 1}) {
            QPDFObjectHandle page = QPDFPageDocumentHelper(q).getAllPages().at(n).getObjectHandle();
            QPDFObjectHandle note = q.makeIndirectObject(QPDFObjectHandle::parse(
                    "<< /Type /Annot /Subtype /Text /Rect [100 100 120 120] /Contents (from another app) /NM (other-" +
                    std::to_string(n) + ") >>"));
            QPDFObjectHandle annots = QPDFObjectHandle::newArray();
            if (page.getKey("/Annots").isArray()) {
                for (int i = 0; i < page.getKey("/Annots").getArrayNItems(); ++i) {
                    annots.appendItem(page.getKey("/Annots").getArrayItem(i));
                }
            }
            annots.appendItem(note);
            page.replaceKey("/Annots", annots);
        }
    });
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document);
    EXPECT_TRUE(loaded.hybridChanged.empty()) << "ours are unchanged";
    {
        QPDF clean;
        clean.processFile(loaded.document->getPdfFilepath().string().c_str());
        auto annots = QPDFPageDocumentHelper(clean).getAllPages().at(3).getAnnotations();
        ASSERT_EQ(annots.size(), 1u) << "the clean copy keeps it (shown by the background)";
        EXPECT_EQ(annots[0].getObjectHandle().getKey("/NM").getUTF8Value(), "other-3");
    }
    DocumentSession session(*app, std::move(loaded.document));
    {  // the ruled page moves to the end: its comment goes with it
        std::unique_lock lock(*session.getDocument());
        PageRef ruled = session.getDocument()->getPage(1);
        session.getDocument()->deletePage(1);
        session.getDocument()->insertPage(ruled, 3);
    }
    ASSERT_TRUE(session.save().ok);
    QPDF saved;
    saved.processFile(out.string().c_str());
    auto pages = QPDFPageDocumentHelper(saved).getAllPages();
    ASSERT_EQ(pages.at(2).getAnnotations().size(), 1u) << "the last PDF page, now third";
    EXPECT_EQ(pages.at(2).getAnnotations()[0].getObjectHandle().getKey("/Contents").getUTF8Value(),
              "from another app");
    EXPECT_EQ(pages.at(0).getAnnotations().size(), 2u) << "ours, written once";
    std::vector<std::string> onRuled;
    for (auto& a: pages.at(3).getAnnotations()) {
        onRuled.push_back(a.getObjectHandle().getKey("/NM").getUTF8Value());
    }
    EXPECT_EQ(onRuled, (std::vector<std::string>{"other-1", HybridPdf::nameOf(3, 0)}))
            << "a page with a generated background keeps the other app's comment too";
    // (a copy made by a full write points at its page; appended, the other app's annotation stays as it was, on the
    // same page object)
    QPDFObjectHandle onPage = pages.at(3).getAnnotations()[0].getObjectHandle().getKey("/P");
    if (!onPage.isNull()) {
        EXPECT_EQ(onPage.getObjGen(), pages.at(3).getObjectHandle().getObjGen());
    }
    int code = -1;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);

    // "Save as" .xopp: its pages go next to it, not a reference into the cache
    ASSERT_TRUE(session.saveAs(path("notes.xopp")).ok);
    EXPECT_FALSE(session.isHybrid());
    EXPECT_EQ(session.getDocument()->getPdfFilepath(), path("notes.pdf"));
    auto xopp = DocumentSession::loadFile(path("notes.xopp"));
    ASSERT_TRUE(xopp.document);
    EXPECT_EQ(describe(*xopp.document), describe(*session.getDocument()));
}

TEST_F(HybridPdfTest, inkChangedInAnotherAppIsReportedAndCanBeImported) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path out = path("lecture.notes.pdf");
    ASSERT_TRUE(HybridPdf::write(*doc, out).ok);
    const std::string pen = HybridPdf::nameOf(0, 0), ruled = HybridPdf::nameOf(1, 0);
    editWithQpdf(out, [&](QPDF& q) {
        // The pen stroke moved 50 pt to the right
        QPDFObjectHandle a = ourAnnot(q, 0, pen);
        ASSERT_TRUE(a.isDictionary());
        QPDFObjectHandle rect = QPDFObjectHandle::newArray();
        for (int i = 0; i < 4; ++i) {
            rect.appendItem(QPDFObjectHandle::newReal(a.getKey("/Rect").getArrayItem(i).getNumericValue() +
                                                              (i % 2 == 0 ? 50 : 0),
                                                      1));
        }
        a.replaceKey("/Rect", rect);
        // The stroke on the ruled page deleted
        QPDFObjectHandle page = QPDFPageDocumentHelper(q).getAllPages().at(1).getObjectHandle();
        page.replaceKey("/Annots", QPDFObjectHandle::newArray());
    });
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document);
    EXPECT_EQ(loaded.hybridChanged, (std::vector<std::string>{pen, ruled}));
    const std::string before = describe(*loaded.document);

    DocumentSession session(*app, std::move(loaded.document));
    session.setHybridChanges(loaded.hybridChanged);
    std::string error;
    ASSERT_TRUE(session.importHybridChanges(error)) << error;
    EXPECT_TRUE(session.isModified());
    Document& d = *session.getDocument();
    EXPECT_FALSE(d.getPage(0)->getLayers()[0]->getElementsView().begin() != d.getPage(0)->getLayers()[0]->getElementsView().end())
            << "the layer the other app changed is its annotation now";
    EXPECT_TRUE(d.getPage(0)->getLayers()[1]->getElementsView().begin() != d.getPage(0)->getLayers()[1]->getElementsView().end())
            << "the highlighter was not changed";
    {
        QPDF clean;
        clean.processFile(d.getPdfFilepath().string().c_str());
        auto annots = QPDFPageDocumentHelper(clean).getAllPages().at(0).getAnnotations();
        ASSERT_EQ(annots.size(), 1u);
        EXPECT_EQ(annots[0].getObjectHandle().getKey("/NM").getUTF8Value(), "imported:" + pen);
        EXPECT_FALSE(annots[0].getObjectHandle().hasKey("/XournalQt"));
    }
    session.getUndoRedoHandler()->undo();
    session.getUndoRedoHandler()->undo();
    EXPECT_EQ(describe(d), before) << "undone: the Xournal data is back";
    session.getUndoRedoHandler()->redo();
    session.getUndoRedoHandler()->redo();

    // Saved, it is ours again (a plain annotation kept, and a new one of ours for what is left)
    ASSERT_TRUE(session.save().ok);
    auto again = DocumentSession::loadFile(out);
    ASSERT_TRUE(again.document);
    EXPECT_TRUE(again.hybridChanged.empty());
}

TEST_F(HybridPdfTest, notesSavedIntoThePdfItselfKeepTheOriginalOnce) {
    makeTextPdf(path("lecture.pdf"), {"lectureone", "lecturetwo"});
    const std::string original = [&] {
        std::ifstream in(path("lecture.pdf"), std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }();
    auto loaded = DocumentSession::loadFile(path("lecture.pdf"));
    DocumentSession session(*app, std::move(loaded.document));
    {
        std::unique_lock lock(*session.getDocument());
        addStroke(session.getDocument()->getPage(1)->getSelectedLayer(), StrokeTool::PEN, Color(0xffff0000U), 2,
                  {Point(10, 10), Point(200, 300)});
    }
    ASSERT_TRUE(session.saveAsHybrid(path("lecture.pdf")).ok);
    EXPECT_TRUE(session.isHybrid());
    EXPECT_TRUE(HybridPdf::isHybrid(path("lecture.pdf")));
    ASSERT_TRUE(fs::exists(path("lecture.original.pdf")));
    {
        std::ifstream in(path("lecture.original.pdf"), std::ios::binary);
        EXPECT_EQ(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()), original);
    }
    EXPECT_NE(session.getDocument()->getPdfFilepath(), path("lecture.pdf")) << "the pages come from a copy now";
    // Saved again: still right, the original untouched
    {
        std::unique_lock lock(*session.getDocument());
        addStroke(session.getDocument()->getPage(0)->getSelectedLayer(), StrokeTool::PEN, Color(0xffff0000U), 2,
                  {Point(10, 10), Point(200, 300)});
    }
    ASSERT_TRUE(session.save().ok);
    auto again = DocumentSession::loadFile(path("lecture.pdf"));
    ASSERT_TRUE(again.document);
    EXPECT_EQ(describe(*again.document), describe(*session.getDocument()));
    std::ifstream in(path("lecture.original.pdf"), std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()), original);
}

// Posters and flashcards (qt/page-sizes): an A0 page and an A7 card keep their size in the PDF export (vector: the
// strokes stay strokes at any size) and in a PDF with our annotations (the page's /MediaBox), and open again so.
TEST_F(HybridPdfTest, postersAndFlashcardsKeepTheirSizeInPdfs) {
    constexpr double MM = 72.0 / 25.4;
    const QSizeF a0(841 * MM, 1189 * MM), a7(74 * MM, 105 * MM);
    DocumentSession session(*app);
    Document* doc = session.getDocument();
    doc->getPage(0)->setSize(a0.width(), a0.height());
    doc->getPage(0)->setBackgroundType(PageType(PageTypeFormat::Graph));
    addStroke(doc->getPage(0)->getSelectedLayer(), StrokeTool::PEN, Color(0xff0000ffU), 2,
              {Point(100, 100), Point(2200, 3200)});
    auto card = std::make_shared<XojPage>(a7.height(), a7.width());  // (landscape)
    card->setBackgroundType(PageType(PageTypeFormat::Ruled));
    addStroke(card->getSelectedLayer(), StrokeTool::PEN, Color(0xffcc0000U), 1, {Point(20, 20), Point(250, 180)});
    doc->insertPage(card, 1);
    auto mediaBoxes = [](const fs::path& pdf) {
        QPDF q;
        q.processFile(pdf.string().c_str());
        std::vector<QSizeF> sizes;
        for (auto& page: QPDFPageDocumentHelper(q).getAllPages()) {
            const auto box = page.getMediaBox().getArrayAsRectangle();
            sizes.emplace_back(box.urx - box.llx, box.ury - box.lly);
        }
        return sizes;
    };
    auto near = [](QSizeF a, QSizeF b) { return std::abs(a.width() - b.width()) < 0.01 && std::abs(a.height() - b.height()) < 0.01; };

    const fs::path exported = path("poster.pdf");
    ExportHelper::exportPdf(doc, exported, nullptr, nullptr, EXPORT_BACKGROUND_ALL, false);
    auto sizes = mediaBoxes(exported);
    ASSERT_EQ(sizes.size(), 2u);
    EXPECT_TRUE(near(sizes[0], a0)) << sizes[0].width() << " x " << sizes[0].height();
    EXPECT_TRUE(near(sizes[1], a7.transposed())) << sizes[1].width() << " x " << sizes[1].height();
    EXPECT_LT(fs::file_size(exported), 200'000u) << "vector, not a picture of the poster";

    const fs::path hybrid = path("poster.notes.pdf");
    const auto r = HybridPdf::write(*doc, hybrid);
    ASSERT_TRUE(r.ok) << r.error;
    sizes = mediaBoxes(hybrid);
    ASSERT_EQ(sizes.size(), 2u);
    EXPECT_TRUE(near(sizes[0], a0));
    EXPECT_TRUE(near(sizes[1], a7.transposed()));
    auto loaded = DocumentSession::loadFile(hybrid);
    ASSERT_TRUE(loaded.document) << loaded.error;
    ASSERT_EQ(loaded.document->getPageCount(), 2u);
    EXPECT_NEAR(loaded.document->getPage(0)->getWidth(), a0.width(), 0.01);
    EXPECT_NEAR(loaded.document->getPage(0)->getHeight(), a0.height(), 0.01);
    EXPECT_NEAR(loaded.document->getPage(1)->getWidth(), a7.height(), 0.01);
    EXPECT_NEAR(loaded.document->getPage(1)->getHeight(), a7.width(), 0.01);
}

TEST_F(HybridPdfTest, exportsAPlainXoppForXournalpp) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path out = path("lecture.notes.pdf");
    ASSERT_TRUE(HybridPdf::write(*doc, out).ok);
    auto loaded = DocumentSession::loadFile(out);
    DocumentSession session(*app, std::move(loaded.document));
    const fs::path xopp = path("lecture.notes.xopp");
    EXPECT_EQ(DocumentSession::exportPdfFor(xopp), path(".lecture.notes.pages.pdf")) << "the hybrid PDF has the name";
    ASSERT_TRUE(session.exportXopp(xopp).ok);
    EXPECT_EQ(session.getFilePath(), out) << "the document stays the hybrid PDF";
    auto exported = DocumentSession::loadFile(xopp);
    ASSERT_TRUE(exported.document) << exported.error;
    EXPECT_FALSE(exported.hybrid);
    EXPECT_EQ(exported.document->getPdfFilepath(), path(".lecture.notes.pages.pdf"));
    EXPECT_EQ(describe(*exported.document), describe(*session.getDocument()));
    QPDF base;
    base.processFile(path(".lecture.notes.pages.pdf").string().c_str());
    EXPECT_FALSE(base.getRoot().hasKey("/XournalQt"));
    for (auto& page: QPDFPageDocumentHelper(base).getAllPages()) {
        EXPECT_TRUE(page.getAnnotations().empty()) << "no ink twice in Xournal++";
    }
    // A notes document without PDF of its own: "name.pdf" next to it
    EXPECT_EQ(DocumentSession::exportPdfFor(path("other.xopp")), path("other.pdf"));
}

// "Keep it updated for Xournal++": the hybrid PDF records the .xopp it keeps (relative to it), a session reads that
// from the file, and a save without it drops it.
TEST_F(HybridPdfTest, recordsTheXoppItKeepsForXournalpp) {
    auto doc = annotated(path("lecture.pdf"));
    DocumentSession session(*app, std::move(doc));
    const fs::path out = path("lecture.notes.pdf"), xopp = path("lecture.xopp");
    DocumentSession::SaveRequest request;
    request.kind = DocumentSession::SaveKind::Hybrid;
    request.target = out;
    request.exportXopp = xopp;
    request.recordExport = xopp;
    ASSERT_TRUE(session.saveNow(request).ok);
    EXPECT_TRUE(fs::exists(xopp));
    EXPECT_EQ(HybridPdf::xoppExportOf(out), xopp);
    EXPECT_EQ(session.xoppExport(), xopp);
    QPDF q;
    q.processFile(out.string().c_str());
    EXPECT_EQ(q.getRoot().getKey("/XournalQt").getKey("/XoppExport").getUTF8Value(), "lecture.xopp")
            << "relative: it follows the PDF";

    auto reopened = DocumentSession::loadFile(out);
    ASSERT_TRUE(reopened.document);
    DocumentSession again(*app, std::move(reopened.document));
    EXPECT_EQ(again.xoppExport(), xopp) << "read from the file";
    ASSERT_TRUE(again.save().ok);
    EXPECT_EQ(again.xoppExport(), fs::path()) << "a save that does not record it drops it";
    EXPECT_EQ(HybridPdf::xoppExportOf(out), fs::path());
}

// The .xopp a document was goes to the trash (or is written over): the document takes its pages from a copy in the
// cache first, the same pages under the same numbers.
TEST_F(HybridPdfTest, aDocumentLetsGoOfTheFileItShowsPagesFrom) {
    makeTextPdf(path("pages.pdf"), {"pageone", "pagetwo"});
    auto loaded = DocumentSession::loadFile(path("pages.pdf"));
    DocumentSession session(*app, std::move(loaded.document));
    std::string error;
    ASSERT_TRUE(session.detachBackground({path("other.pdf")}, error));
    EXPECT_EQ(session.getDocument()->getPdfFilepath(), path("pages.pdf")) << "not one of them: stays";
    ASSERT_TRUE(session.detachBackground({path("pages.pdf")}, error)) << error;
    const fs::path copy = session.getDocument()->getPdfFilepath();
    EXPECT_TRUE(HybridPdf::inCache(copy)) << copy;
    fs::remove(path("pages.pdf"));
    {
        std::unique_lock lock(*session.getDocument());
        addStroke(session.getDocument()->getPage(1)->getSelectedLayer(), StrokeTool::PEN, Color(0xffff0000U), 2,
                  {Point(10, 10), Point(200, 300)});
    }
    ASSERT_TRUE(session.saveAsHybrid(path("pages.notes.pdf")).ok) << "the pages are still there";
    auto again = DocumentSession::loadFile(path("pages.notes.pdf"));
    ASSERT_TRUE(again.document);
    EXPECT_EQ(describe(*again.document), describe(*session.getDocument()));
}

// Share → For Xournal++: "name.xopp" with upstream's attached PDF "name.xopp.bg.pdf" (domain "attach"), which upstream's
// LoadHandler opens with the pages right, also after the two were moved elsewhere together.
TEST_F(HybridPdfTest, aCopyForXournalppTakesItsPdfAlongAsAttachment) {
    auto doc = annotated(path("lecture.pdf"));
    const std::string expected = describeAsXopp(*doc, path("roundtrip.xopp"));  // (as a .xopp gives it back)
    DocumentSession session(*app, std::move(doc));
    fs::create_directories(path("out"));
    const fs::path xopp = path("out") / "lecture.xopp";
    DocumentSession::SaveRequest request;
    request.kind = DocumentSession::SaveKind::ExportXopp;
    request.target = xopp;
    request.attachedPdf = true;
    ASSERT_TRUE(session.saveNow(request).ok);
    EXPECT_TRUE(fs::exists(path("out") / "lecture.xopp.bg.pdf"));
    EXPECT_FALSE(fs::exists(path("out") / "lecture.pdf"));
    EXPECT_FALSE(fs::exists(path("out") / ".lecture.pages.pdf"));
    auto check = [&](const fs::path& file) {
        auto loaded = DocumentSession::loadFile(file);  // (upstream's LoadHandler)
        ASSERT_TRUE(loaded.document) << loaded.error;
        Document& d = *loaded.document;
        EXPECT_TRUE(d.isAttachPdf());
        fs::path bg = file;
        bg += ".bg.pdf";
        EXPECT_EQ(d.getPdfFilepath(), bg);
        EXPECT_EQ(describe(d), expected);
        for (size_t i = 0; i < d.getPageCount(); ++i) {
            if (d.getPage(i)->getBackgroundType().isPdfPage()) {
                EXPECT_EQ(d.getPage(i)->getPdfPageNr(), i) << "base page i";
            }
        }
    };
    check(xopp);
    fs::rename(path("out"), path("moved"));
    check(path("moved") / "lecture.xopp");

    // Notes without a PDF: the .xopp alone
    DocumentSession notes(*app);
    request.target = path("moved") / "notes.xopp";
    ASSERT_TRUE(notes.saveNow(request).ok);
    EXPECT_TRUE(fs::exists(path("moved") / "notes.xopp"));
    EXPECT_FALSE(fs::exists(path("moved") / "notes.xopp.bg.pdf"));
}

// Share → a PDF copy of a .xopp: the document keeps its file, format and unsaved changes. Never over its own PDF.
TEST_F(HybridPdfTest, aPdfCopyLeavesTheDocumentAsItIs) {
    auto doc = annotated(path("lecture.pdf"));
    DocumentSession session(*app, std::move(doc));
    ASSERT_TRUE(session.saveAs(path("lecture.xopp")).ok);
    {
        std::unique_lock lock(*session.getDocument());
        addStroke(session.getDocument()->getPage(0)->getSelectedLayer(), StrokeTool::PEN, Color(0xff00ff00U), 2,
                  {Point(10, 10), Point(300, 300)});
    }
    const bool modified = session.isModified();
    DocumentSession::SaveRequest request;
    request.kind = DocumentSession::SaveKind::ExportHybrid;
    request.target = path("copy");
    ASSERT_TRUE(session.saveNow(request).ok);
    EXPECT_EQ(session.getFilePath(), path("lecture.xopp"));
    EXPECT_FALSE(session.isHybrid());
    EXPECT_EQ(session.isModified(), modified);
    ASSERT_TRUE(HybridPdf::isHybrid(path("copy.pdf")));
    auto copy = DocumentSession::loadFile(path("copy.pdf"));
    ASSERT_TRUE(copy.document);
    EXPECT_EQ(describe(*copy.document), describeAsXopp(*session.getDocument(), path("roundtrip.xopp")));
    request.target = path("lecture.pdf");
    EXPECT_FALSE(session.saveNow(request).ok) << "not over the PDF it shows";
    EXPECT_FALSE(HybridPdf::isHybrid(path("lecture.pdf")));
}

// A PDF page shown twice (a duplicated page) is two pages in the file, each with its own annotations.
TEST_F(HybridPdfTest, aPageShownTwiceHasItsOwnAnnotations) {
    auto doc = annotated(path("lecture.pdf"));
    auto copy = std::make_shared<XojPage>(*doc->getPage(0));  // (the pen stroke and the highlighter too)
    doc->insertPage(copy, 1);
    for (const Layer* l: copy->getLayers()) {
        for (const auto& e: l->getElementsView()) {
            e->getBoundingBox();
        }
    }
    const fs::path out = path("twice.pdf");
    const auto r = HybridPdf::write(*doc, out);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.pages, 5u);
    int code = -1;
    const std::string report = qpdfCheck(out, code);
    EXPECT_EQ(code, 0) << report;
    QPDF q;
    q.processFile(out.string().c_str());
    auto pages = QPDFPageDocumentHelper(q).getAllPages();
    ASSERT_EQ(pages.size(), 5u);
    EXPECT_EQ(pages[0].getAnnotations().size(), 2u);
    EXPECT_EQ(pages[1].getAnnotations().size(), 2u);
    EXPECT_NE(pages[0].getObjectHandle().getObjGen(), pages[1].getObjectHandle().getObjGen());
    EXPECT_EQ(pages[1].getAnnotations()[0].getObjectHandle().getKey("/P").getObjGen(),
              pages[1].getObjectHandle().getObjGen());
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document);
    EXPECT_EQ(describe(*loaded.document), describeAsXopp(*doc, path("reference.xopp")));
}

TEST_F(HybridPdfTest, linksOfMarkdownBoxesBecomeLinksOtherViewersFollow) {
    // A lecture PDF with its notes, and a document of notes that links to it (a Markdown box on page 1)
    makeTextPdf(path("lecture.pdf"), {"lectureone", "lecturetwo", "lecturethree"});
    {
        auto loaded = DocumentSession::loadFile(path("lecture.pdf"));
        ASSERT_TRUE(loaded.document);
        ASSERT_TRUE(DocumentSession::writeDocument(*loaded.document, path("lecture.xopp")).ok);
    }
    Document doc(nullptr);
    auto page = std::make_shared<XojPage>(595, 842);
    auto* markdown = new Layer();
    markdown->setName(std::string(xoj::markdown::LAYER_NAME));
    page->getLayers().insert(page->getLayers().begin(), markdown);
    auto box = std::make_unique<Text>();
    box->setText("See [the lecture](lecture.xopp#page=3&pdfpage=2), [the PDF](lecture.pdf#page=3), "
                 "[the web](https://example.org/x) and [notes](other.md#heading=a).");
    box->setFont(XojFont("Sans", 10));
    box->setWrap(400);
    box->setTransformation(xoj::util::Matrix::TRANSLATION(56, 56));
    markdown->addElement(std::move(box));
    doc.addPage(page);
    const fs::path out = path("notes.pdf");
    const auto r = HybridPdf::write(doc, out);
    ASSERT_TRUE(r.ok) << r.error;

    QPDF pdf;
    pdf.processFile(out.string().c_str());
    std::vector<std::string> actions;
    for (auto& p: QPDFPageDocumentHelper(pdf).getAllPages()) {
        QPDFObjectHandle annots = p.getObjectHandle().getKey("/Annots");
        for (int i = 0; annots.isArray() && i < annots.getArrayNItems(); ++i) {
            QPDFObjectHandle a = annots.getArrayItem(i);
            if (a.getKey("/Subtype").getName() != "/Link") {
                continue;
            }
            QPDFObjectHandle act = a.getKey("/A");
            const QPDFObjectHandle::Rectangle rect = a.getKey("/Rect").getArrayAsRectangle();
            EXPECT_GT(rect.urx, rect.llx);
            EXPECT_GT(rect.lly, 700) << "near the top of the page (PDF space: y up)";
            if (act.getKey("/S").getName() == "/URI") {
                actions.push_back("URI " + act.getKey("/URI").getStringValue());
            } else {
                actions.push_back(act.getKey("/S").getName() + " " + act.getKey("/F").getUTF8Value() + " " +
                                  std::to_string(act.getKey("/D").getArrayItem(0).getIntValue()));
            }
        }
    }
    EXPECT_EQ(actions, (std::vector<std::string>{"/GoToR lecture.pdf 1", "/GoToR lecture.pdf 2",
                                                 "URI https://example.org/x"}))
            << "the .xopp's PDF at its PDF page; the PDF at its page; not the .md";

    // They are ours: not reported as changed, and written again (not twice) on the next save
    auto opened = HybridPdf::open(out);
    ASSERT_TRUE(opened.document) << opened.error;
    EXPECT_TRUE(opened.changed.empty());
    ASSERT_TRUE(HybridPdf::write(*opened.document, out).ok);
    QPDF again;
    again.processFile(out.string().c_str());
    int count = 0;
    for (auto& p: QPDFPageDocumentHelper(again).getAllPages()) {
        QPDFObjectHandle annots = p.getObjectHandle().getKey("/Annots");
        for (int i = 0; annots.isArray() && i < annots.getArrayNItems(); ++i) {
            count += annots.getArrayItem(i).getKey("/Subtype").getName() == "/Link";
        }
    }
    EXPECT_EQ(count, 3);
}
