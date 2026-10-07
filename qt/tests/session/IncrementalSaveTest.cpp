/*
 * xournal-qt: saving a PDF with notes again (qt/docs/features/hybrid-pdf.md, "Saving: incremental updates"): only what
 * changed is appended, the file looks and opens as a full write of it does, and the outline's bookmarks.
 *
 * @license GNU GPLv2 or later
 */
#include "HybridPdfTestSupport.h"

using namespace xqt;
using namespace xqt::test::hybrid;
using xqt::test::makeTextPdf;
using xqt::test::readFile;


// A crash or a full disk while the update is written leaves the file as it was, byte for byte
TEST_F(IncrementalSaveTest, aFailedAppendLeavesTheFileAsItWas) {
    auto s = savedLecture(path("notes.pdf"));
    const std::string before = readFile(path("notes.pdf"));
    drawOn(*s, 0, 500);
    // Before anything of the update is written, and when all of it is written but not yet in place
    for (const uint64_t at: {uint64_t(0), uint64_t(1)}) {
        IncrementalPdf::failWriteAt = [at](uint64_t written) { return written >= at; };
        const auto r = s->save();
        EXPECT_FALSE(r.ok) << "fails at " << at;
        EXPECT_EQ(readFile(path("notes.pdf")), before);
        for (auto& e: fs::directory_iterator(tmp.path().toStdString())) {
            EXPECT_NE(e.path().extension(), ".part") << "no temporary file left: " << e.path();
        }
    }
    IncrementalPdf::failWriteAt = nullptr;
    // A temporary file a crash left behind (the process died while it wrote it) goes with the next save
    const fs::path stale = path(".notes.pdf.12345-1.part");
    std::ofstream(stale) << before.substr(0, 1000);
    fs::last_write_time(stale, fs::file_time_type::clock::now() - std::chrono::hours(1));
    const auto r = s->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    EXPECT_EQ(readFile(path("notes.pdf")).substr(0, before.size()), before);
    EXPECT_FALSE(fs::exists(stale));
}

// Ctrl+S appends the changed layer (its annotation and drawing), the embedded document, the marker: nothing else
TEST_F(IncrementalSaveTest, ctrlSAppendsOnlyWhatChanged) {
    const fs::path out = path("lecture.notes.pdf");
    auto s = savedLecture(out);
    const std::string before = readFile(out);
    const auto page1 = annotIds(out, 0);
    const auto page3 = annotIds(out, 2);
    drawOn(*s, 2, 600);  // (page 3, the layer of its text: the ruled page is page 2)
    const auto r = s->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    const std::string after = readFile(out);
    EXPECT_EQ(after.substr(0, before.size()), before);
    EXPECT_EQ(r.appended, after.size() - before.size());
    EXPECT_LT(r.appended, 12000u) << "one layer, the .xopp, the marker";
    EXPECT_EQ(annotIds(out, 0), page1) << "the annotations of an unchanged page are the same objects";
    ASSERT_EQ(annotIds(out, 2).size(), 1u) << "the layer of the text and the new stroke";
    EXPECT_NE(annotIds(out, 2), page3) << "written again";
    int code = -1;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);
    EXPECT_TRUE(HybridPdf::hasEarlierRevisions(out));
    EXPECT_TRUE(s->hasEarlierRevisions());
    expectSamePages(out, writtenInFull(*s, "full.pdf"), 4, "after one incremental save");
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document);
    EXPECT_TRUE(loaded.hybridChanged.empty());
    EXPECT_EQ(describe(*loaded.document), describeAsXopp(*s->getDocument(), path("same.xopp")));
    // Saved again without a change: little more than the .xopp and the marker
    const auto again = s->save();
    ASSERT_TRUE(again.ok);
    EXPECT_TRUE(again.incremental);
    EXPECT_LT(again.appended, 8000u);
}

// The embedded .xopp is the same bytes when the same document is written again (version history stores older
// versions as byte deltas of it, qt/docs/features/hybrid-pdf.md "Version history"): written twice in full, and saved
// again without a change through the session (which puts a preview of the first page into it)
TEST_F(IncrementalSaveTest, theEmbeddedXoppIsTheSameBytesForTheSameDocument) {
    auto embedded = [](const fs::path& pdf) {
        QPDF q;
        q.processFile(pdf.string().c_str());
        auto spec = QPDFEmbeddedFileDocumentHelper(q).getEmbeddedFile(HybridPdf::DATA_NAME);
        EXPECT_TRUE(spec);
        if (!spec) {
            return std::string();
        }
        auto buffer = spec->getEmbeddedFileStream().getStreamData();
        return std::string(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
    };
    auto doc = annotated(path("lecture.pdf"));
    ASSERT_TRUE(HybridPdf::write(*doc, path("once.pdf")).ok);
    const std::string first = embedded(path("once.pdf"));
    ASSERT_GT(first.size(), 100u);
    ASSERT_TRUE(HybridPdf::write(*doc, path("once.pdf")).ok);
    EXPECT_EQ(embedded(path("once.pdf")), first) << "written in full twice";

    const fs::path out = path("notes.pdf");
    auto s = savedLecture(out);
    const std::string saved = embedded(out);
    const auto again = s->save();
    ASSERT_TRUE(again.ok) << again.error;
    EXPECT_TRUE(again.incremental);
    EXPECT_EQ(embedded(out), saved) << "saved again without a change, with the preview";
    drawOn(*s, 0, 500);
    ASSERT_TRUE(s->save().ok);
    EXPECT_NE(embedded(out), saved);
}

// An image attached as a page's background (document.xopp.bg_1.png) is not written again by a save that did not change
// it; a new image is
TEST_F(IncrementalSaveTest, attachedBackgroundImagesAreNotWrittenAgainWhenUnchanged) {
    auto makePng = [](const fs::path& png, unsigned seed) {
        GdkPixbuf* pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, false, 8, 160, 160);
        guchar* pixels = gdk_pixbuf_get_pixels(pixbuf);
        const int stride = gdk_pixbuf_get_rowstride(pixbuf);
        for (int y = 0; y < 160; ++y) {
            for (int x = 0; x < 160 * 3; ++x) {
                seed = seed * 1103515245u + 12345u;  // (noise: it does not compress)
                pixels[y * stride + x] = static_cast<guchar>(seed >> 16);
            }
        }
        ASSERT_TRUE(gdk_pixbuf_save(pixbuf, png.string().c_str(), "png", nullptr, nullptr));
        g_object_unref(pixbuf);
    };
    auto imagePage = [](const fs::path& png) {
        auto page = std::make_shared<XojPage>(595, 842);
        BackgroundImage img;
        GError* error = nullptr;
        img.loadFile(png, &error);
        EXPECT_EQ(error, nullptr);
        img.setAttach(true);
        page->setBackgroundImage(img);
        page->setBackgroundType(PageType(PageTypeFormat::Image));
        return page;
    };
    makePng(path("photo.png"), 1);
    const auto pngSize = fs::file_size(path("photo.png"));
    ASSERT_GT(pngSize, 50000u);
    auto doc = annotated(path("lecture.pdf"));
    doc->addPage(imagePage(path("photo.png")));
    const fs::path out = path("notes.pdf");
    auto s = std::make_unique<DocumentSession>(*app, std::move(doc));
    ASSERT_TRUE(s->saveAsHybrid(out).ok);
    drawOn(*s, 0, 500);
    auto r = s->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    EXPECT_LT(r.appended, pngSize / 2) << "the unchanged image is not written again";
    int code = -1;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);
    {
        auto loaded = DocumentSession::loadFile(out);
        ASSERT_TRUE(loaded.document) << loaded.error;
        const PageRef last = loaded.document->getPage(loaded.document->getPageCount() - 1);
        ASSERT_TRUE(last->getBackgroundType().isImagePage());
        ASSERT_NE(last->getBackgroundImage().getPixbuf(), nullptr) << "the image is still there";
        EXPECT_EQ(gdk_pixbuf_get_width(last->getBackgroundImage().getPixbuf()), 160);
    }
    // Another image on that page: written
    makePng(path("other.png"), 2);
    {
        auto* d = s->getDocument();
        d->lock();
        const PageRef last = d->getPage(d->getPageCount() - 1);
        BackgroundImage img;
        GError* error = nullptr;
        img.loadFile(path("other.png"), &error);
        img.setAttach(true);
        last->setBackgroundImage(img);
        d->unlock();
    }
    drawOn(*s, 0, 520);
    r = s->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_GT(r.appended, pngSize / 2) << "a new image is written";
}

// Many saves in a row, with every kind of change: each file is valid, looks like a full write, and opens as the
// document; the earlier revisions stay in it
TEST_F(IncrementalSaveTest, manySavesLookAndOpenLikeFullWrites) {
    const fs::path out = path("notes.pdf");
    auto s = savedLecture(out);
    Document& doc = *s->getDocument();
    const std::vector<std::pair<std::string, std::function<void()>>> edits = {
            {"a stroke", [&] { drawOn(*s, 0, 700); }},
            {"a stroke on the highlighter layer, erased",
             [&] {
                 std::unique_lock lock(doc);
                 Layer* l = doc.getPage(0)->getLayers().at(1);
                 l->removeElement(l->getElementsView().front());
             }},
            {"pages moved",
             [&] {
                 std::unique_lock lock(doc);
                 PageRef p = doc.getPage(3);
                 doc.deletePage(3);
                 doc.insertPage(p, 0);
             }},
            {"a ruled page added",
             [&] {
                 auto ruled = std::make_shared<XojPage>(595, 842);
                 ruled->setBackgroundType(PageType(PageTypeFormat::Ruled));
                 std::unique_lock lock(doc);
                 doc.insertPage(ruled, 2);
                 lock.unlock();
                 drawOn(*s, 2, 300);
             }},
            {"a background changed",
             [&] {
                 std::unique_lock lock(doc);
                 doc.getPage(2)->setBackgroundType(PageType(PageTypeFormat::Graph));
             }},
            {"a page deleted",
             [&] {
                 std::unique_lock lock(doc);
                 doc.deletePage(4);
             }},
            {"a text", [&] {
                 std::unique_lock lock(doc);
                 addText(doc.getPage(1)->getSelectedLayer(), "added later", 100, 300);
             }},
            {"the PDF page shown twice", [&] {
                 std::unique_lock lock(doc);
                 PageRef first = doc.getPage(1);  // (a PDF page)
                 auto twice = std::make_shared<XojPage>(first->getWidth(), first->getHeight());
                 twice->setBackgroundType(first->getBackgroundType());
                 twice->setBackgroundPdfPageNr(first->getPdfPageNr());
                 doc.insertPage(twice, 4);
             }},
    };
    size_t revisions = 1;
    for (const auto& [what, edit]: edits) {
        edit();
        const auto r = s->save();
        ASSERT_TRUE(r.ok) << what << ": " << r.error;
        EXPECT_TRUE(r.incremental) << what;
        revisions += r.incremental ? 1 : 0;
        int code = -1;
        const std::string check = qpdfCheck(out, code);
        EXPECT_EQ(code, 0) << what << "\n" << check;
        const fs::path full = writtenInFull(*s, "full.pdf");
        expectSamePages(out, full, doc.getPageCount(), what);
        auto loaded = DocumentSession::loadFile(out);
        ASSERT_TRUE(loaded.document) << what;
        EXPECT_TRUE(loaded.hybridChanged.empty()) << what;
        EXPECT_EQ(describe(*loaded.document), describeAsXopp(doc, path("same.xopp"))) << what;
    }
    EXPECT_EQ(countOf(readFile(out), "startxref"), revisions);
    if (const char* samples = std::getenv("XQT_INCREMENTAL_SAMPLES")) {  // (for other renderers)
        std::error_code ec;
        fs::create_directories(samples, ec);
        fs::copy_file(out, fs::path(samples) / "lecture-8-saves.pdf", fs::copy_options::overwrite_existing, ec);
        fs::copy_file(writtenInFull(*s, "full.pdf"), fs::path(samples) / "lecture-full.pdf",
                      fs::copy_options::overwrite_existing, ec);
    }
}

// The file is written anew in full when it grew too much, and when asked to (Save as, Share)
TEST_F(IncrementalSaveTest, compactsWhenItGrewTooMuchOrWhenAsked) {
    const fs::path out = path("notes.pdf");
    auto s = savedLecture(out);
    drawOn(*s, 0, 500);
    HybridPdf::compactAbove = 0.0001;
    auto r = s->save();
    ASSERT_TRUE(r.ok);
    EXPECT_FALSE(r.incremental) << "grew by more than the limit: written in full";
    EXPECT_FALSE(HybridPdf::hasEarlierRevisions(out));
    HybridPdf::compactAbove = 1000;
    drawOn(*s, 0, 550);
    r = s->save();
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(r.incremental) << "after a full write, appended again";
    EXPECT_TRUE(s->hasEarlierRevisions());
    DocumentSession::SaveRequest share;
    share.compact = true;
    r = s->saveNow(share);
    ASSERT_TRUE(r.ok);
    EXPECT_FALSE(r.incremental);
    EXPECT_FALSE(s->hasEarlierRevisions()) << "shared: no earlier revision goes along";
    r = s->saveAsHybrid(path("other.pdf"));
    ASSERT_TRUE(r.ok);
    EXPECT_FALSE(r.incremental) << "Save as";
    drawOn(*s, 1, 400);
    r = s->save();
    EXPECT_TRUE(r.incremental) << "then Ctrl+S appends to the new file";
}

// Share, Export and the archive export write fresh files: nothing of the earlier revisions (deleted ink) goes along
TEST_F(IncrementalSaveTest, whatIsSharedOrExportedHasNoEarlierRevisions) {
    const fs::path out = path("notes.pdf");
    auto s = savedLecture(out);
    {  // the highlighter goes: its annotation stays in the earlier revision
        std::unique_lock lock(*s->getDocument());
        s->getDocument()->getPage(0)->getLayers().at(1)->clearNoFree();
    }
    ASSERT_TRUE(s->save().incremental);
    auto hasHighlighter = [](const fs::path& pdf) {
        QPDF q;
        q.processFile(pdf.string().c_str());
        for (QPDFObjectHandle o: q.getAllObjects()) {
            if (o.isDictionary() && o.getKey("/NM").isString() && o.getKey("/NM").getUTF8Value() == "xopp:p1-l2") {
                return true;
            }
        }
        return false;
    };
    EXPECT_TRUE(hasHighlighter(out)) << "the earlier revision holds it";
    ASSERT_TRUE(s->saveNow({DocumentSession::SaveKind::ExportHybrid, path("copy.pdf"), {}, {}}).ok);
    ASSERT_TRUE(s->exportArchive(path("archive.pdf")).ok);
    ASSERT_TRUE(s->exportXopp(path("export.xopp")).ok);
    for (const char* f: {"copy.pdf", "archive.pdf", "export.pdf"}) {
        EXPECT_FALSE(HybridPdf::hasEarlierRevisions(path(f))) << f;
        EXPECT_FALSE(hasHighlighter(path(f))) << f;
    }
    // A PDF of the library shared as it is: written anew in one piece first
    fs::copy_file(out, path("card.pdf"));
    std::string error;
    ASSERT_TRUE(HybridPdf::compact(path("card.pdf"), error)) << error;
    EXPECT_FALSE(HybridPdf::hasEarlierRevisions(path("card.pdf")));
    EXPECT_FALSE(hasHighlighter(path("card.pdf")));
    int code = -1;
    EXPECT_EQ((qpdfCheck(path("card.pdf"), code), code), 0);
    auto loaded = DocumentSession::loadFile(path("card.pdf"));
    ASSERT_TRUE(loaded.document);
    EXPECT_TRUE(loaded.hybridChanged.empty());
    EXPECT_EQ(describe(*loaded.document), describeAsXopp(*s->getDocument(), path("same.xopp")));
}

// Opened again, the first Ctrl+S appends too; the clean copy is kept across incremental saves (not made again)
TEST_F(IncrementalSaveTest, theCleanCopyIsKeptAcrossIncrementalSaves) {
    const fs::path out = path("notes.pdf");
    savedLecture(out).reset();
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document);
    const fs::path base = loaded.document->getPdfFilepath();
    ASSERT_TRUE(HybridPdf::inCache(base));
    DocumentSession s(*app, std::move(loaded.document));
    drawOn(s, 0, 650);
    const auto r = s.save();
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(r.incremental) << "the first save after opening the file";
    auto again = DocumentSession::loadFile(out);
    ASSERT_TRUE(again.document);
    const fs::path next = again.document->getPdfFilepath();
    EXPECT_NE(next.parent_path(), base.parent_path()) << "the entry of the new version";
    EXPECT_TRUE(fs::equivalent(next, base)) << "the same clean copy (a hard link), not made again";
    EXPECT_EQ(describe(*again.document), describeAsXopp(*s.getDocument(), path("same.xopp")));
    // Pages moved: the next open makes a clean copy of that version
    {
        std::unique_lock lock(*s.getDocument());
        PageRef p = s.getDocument()->getPage(3);
        s.getDocument()->deletePage(3);
        s.getDocument()->insertPage(p, 0);
    }
    ASSERT_TRUE(s.save().incremental);
    auto moved = DocumentSession::loadFile(out);
    ASSERT_TRUE(moved.document);
    EXPECT_FALSE(fs::equivalent(moved.document->getPdfFilepath(), base));
    EXPECT_EQ(describe(*moved.document), describeAsXopp(*s.getDocument(), path("same.xopp")));
    // Opened from that version, the next save appends again
    DocumentSession reopened(*app, std::move(moved.document));
    drawOn(reopened, 1, 200);
    EXPECT_TRUE(reopened.save().incremental);
}

// Another app appends its own revision: a moved annotation of ours is noticed, and the next save writes the file anew;
// a comment it added is kept, and the next save appends again
TEST_F(IncrementalSaveTest, anotherAppsRevisionIsNoticedAndKept) {
    const fs::path out = path("notes.pdf");
    auto s = savedLecture(out);
    drawOn(*s, 0, 450);
    ASSERT_TRUE(s->save().incremental);
    s.reset();
    auto appendAsAnotherApp = [&](const std::function<void(QPDF&, IncrementalPdf::Update&)>& change) {
        IncrementalPdf::Tail tail;
        std::string error;
        ASSERT_TRUE(IncrementalPdf::readTail(out, tail, error));
        QPDF q;
        q.processFile(out.string().c_str());
        IncrementalPdf::Update u(q);
        change(q, u);
        ASSERT_TRUE(IncrementalPdf::append(out, tail, u.serialize(tail)).ok);
        fs::last_write_time(out, fs::last_write_time(out) + std::chrono::seconds(5));
    };
    appendAsAnotherApp([](QPDF& q, IncrementalPdf::Update& u) {  // a comment on page 3
        QPDFObjectHandle page = QPDFPageDocumentHelper(q).getAllPages().at(2).getObjectHandle();
        u.touch(page);
        QPDFObjectHandle note = q.makeIndirectObject(QPDFObjectHandle::parse(
                "<< /Type /Annot /Subtype /Text /Rect [300 300 320 320] /Contents (other app) /NM (other) >>"));
        std::vector<QPDFObjectHandle> annots = page.getKey("/Annots").getArrayAsVector();
        annots.push_back(note);
        page.replaceKey("/Annots", QPDFObjectHandle::newArray(annots));
    });
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document);
    EXPECT_TRUE(loaded.hybridChanged.empty()) << "ours are as they were";
    DocumentSession kept(*app, std::move(loaded.document));
    drawOn(kept, 0, 380);
    auto r = kept.save();
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(r.incremental) << "appended on top of the other app's revision";
    std::vector<std::string> names;
    {
        QPDF q;
        q.processFile(out.string().c_str());
        for (auto& a: QPDFPageDocumentHelper(q).getAllPages().at(2).getAnnotations()) {
            names.push_back(a.getObjectHandle().getKey("/NM").getUTF8Value());
        }
    }
    EXPECT_NE(std::find(names.begin(), names.end(), "other"), names.end()) << "its comment stays";
    int code = -1;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);
    // Now it moves our pen stroke
    appendAsAnotherApp([](QPDF& q, IncrementalPdf::Update& u) {
        QPDFObjectHandle a = ourAnnot(q, 0, HybridPdf::nameOf(0, 0));
        u.touch(a);
        a.replaceKey("/Rect", QPDFObjectHandle::parse("[10 10 50 50]"));
    });
    auto changed = DocumentSession::loadFile(out);
    ASSERT_TRUE(changed.document);
    EXPECT_EQ(changed.hybridChanged, std::vector<std::string>{HybridPdf::nameOf(0, 0)}) << "edited in another app";
    DocumentSession after(*app, std::move(changed.document));
    after.setHybridChanges({});  // "Keep the Xournal data"
    r = after.save();
    ASSERT_TRUE(r.ok);
    EXPECT_FALSE(r.incremental) << "written anew from the Xournal data";
    auto clean = DocumentSession::loadFile(out);
    ASSERT_TRUE(clean.document);
    EXPECT_TRUE(clean.hybridChanged.empty());
}

// An archive PDF stays PDF/A-3b after incremental saves: the dates of its information and XMP metadata agree
TEST_F(IncrementalSaveTest, anArchivePdfStaysPdfAAfterIncrementalSaves) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path out = path("lecture.archive.pdf");
    ASSERT_TRUE(HybridPdf::writeArchive(*doc, out).pdfa);
    doc.reset();
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document);
    DocumentSession s(*app, std::move(loaded.document));
    for (int i = 0; i < 3; ++i) {
        drawOn(s, static_cast<size_t>(i), 500 + 20 * i);
        if (i == 2) {  // and the pen stroke of page 1 goes
            std::unique_lock lock(*s.getDocument());
            s.getDocument()->getPage(0)->getLayers().at(0)->clearNoFree();
        }
        const auto r = s.save();
        ASSERT_TRUE(r.ok) << r.error;
        EXPECT_TRUE(r.incremental) << "save " << i + 1;
        EXPECT_TRUE(HybridPdf::isArchive(out));
        int code = -1;
        const std::string check = qpdfCheck(out, code);
        EXPECT_EQ(code, 0) << check;
        QPDF q;
        q.processFile(out.string().c_str());
        const std::string xmp = streamText(q.getRoot().getKey("/Metadata"));
        EXPECT_FALSE(q.getRoot().getKey("/Metadata").getDict().hasKey("/Filter"));
        EXPECT_NE(xmp.find("<pdfaid:part>3</pdfaid:part>"), std::string::npos);
        QPDFObjectHandle info = q.getTrailer().getKey("/Info");
        const std::string date = info.getKey("/ModDate").getUTF8Value();
        ASSERT_GE(date.size(), 16u);
        EXPECT_NE(xmp.find("<xmp:ModifyDate>" + date.substr(2, 4) + "-" + date.substr(6, 2) + "-" + date.substr(8, 2) +
                           "T" + date.substr(10, 2) + ":" + date.substr(12, 2) + ":" + date.substr(14, 2) + "+00:00"),
                  std::string::npos)
                << xmp;
        const std::string created = info.getKey("/CreationDate").getUTF8Value();
        EXPECT_NE(xmp.find("<xmp:CreateDate>" + created.substr(2, 4) + "-"), std::string::npos) << "kept";
        EXPECT_FALSE(q.isEncrypted());
    }
    const fs::path full = path("full.archive.pdf");
    ASSERT_TRUE(HybridPdf::writeArchive(*s.getDocument(), full).ok);
    expectSamePages(out, full, 4, "an archive PDF after three incremental saves");
    EXPECT_EQ(countOf(readFile(out), "startxref"), 4u);
    ArchivePdfTest::keepSample(out, "archive-incremental-3.pdf");
    // Reopened: the background is the original page, the ink comes from the data
    auto reopened = DocumentSession::loadFile(out);
    ASSERT_TRUE(reopened.document);
    EXPECT_TRUE(reopened.hybridChanged.empty());
    EXPECT_EQ(describe(*reopened.document), describeAsXopp(*s.getDocument(), path("same.xopp")));
    cairo_surface_t* bg = render(reopened.document->getPdfFilepath(), 0);
    cairo_surface_t* original = render(path("lecture.pdf"), 0);
    EXPECT_EQ(compare(bg, original).differ, 0.0) << "no ink in the background";
    cairo_surface_destroy(bg);
    cairo_surface_destroy(original);
}

// Pages pasted from another PDF are appended to the file (the pages it has stay as they are)
TEST_F(IncrementalSaveTest, pastedPdfPagesAreAppended) {
    makeTextPdf(path("other.pdf"), {"pastedone", "pastedtwo"});
    const std::string other = readFile(path("other.pdf"));
    const fs::path out = path("notes.pdf");
    auto paste = [&](DocumentSession& s, size_t at) {
        std::string error;
        const size_t first = s.addPdfPages(other, error);
        ASSERT_NE(first, npos) << error;
        s.waitForMerges();
        auto page = std::make_shared<XojPage>(595, 842);
        page->setBackgroundType(PageType(PageTypeFormat::Pdf));
        page->setBackgroundPdfPageNr(first + 1);
        std::unique_lock lock(*s.getDocument());
        s.getDocument()->insertPage(page, at);
    };
    auto s = savedLecture(out);  // (its background: the user's PDF)
    paste(*s, 1);
    auto r = s->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    expectSamePages(out, writtenInFull(*s, "full.pdf"), 5, "a page pasted");
    s.reset();
    auto loaded = DocumentSession::loadFile(out);  // (its background: the clean copy)
    ASSERT_TRUE(loaded.document);
    DocumentSession reopened(*app, std::move(loaded.document));
    paste(reopened, 0);
    drawOn(reopened, 0, 300);
    r = reopened.save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    int code = -1;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);
    expectSamePages(out, writtenInFull(reopened, "full.pdf"), 6, "pasted into the reopened file");
    auto again = DocumentSession::loadFile(out);
    ASSERT_TRUE(again.document);
    EXPECT_EQ(describe(*again.document), describeAsXopp(*reopened.getDocument(), path("same.xopp")));
    XojPdfDocument pdf;
    ASSERT_TRUE(pdf.load(out, "", nullptr));
    EXPECT_FALSE(pdf.getPage(0)->findText("pastedtwo").empty());
}

// A marker without the record of our layers (an earlier pre-release's file): the first Ctrl+S writes the file in full,
// the next ones append with the record it wrote
TEST_F(IncrementalSaveTest, aFileWithoutTheLayerRecordIsWrittenInFullOnce) {
    const fs::path out = path("notes.pdf");
    savedLecture(out).reset();
    editWithQpdf(out, [](QPDF& q) {
        QPDFObjectHandle marker = q.getRoot().getKey("/XournalQt");
        marker.removeKey("/Layers");
        marker.removeKey("/Drawn");
        for (auto& page: QPDFPageDocumentHelper(q).getAllPages()) {
            if (page.getObjectHandle().hasKey("/XournalQt")) {
                page.getObjectHandle().removeKey("/XournalQt");
            }
            for (auto& a: page.getAnnotations()) {
                QPDFObjectHandle mark = a.getObjectHandle().getKey("/XournalQt");
                if (mark.isDictionary() && mark.hasKey("/Sig")) {
                    mark.removeKey("/Sig");
                }
            }
        }
    });
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document);
    DocumentSession s(*app, std::move(loaded.document));
    for (int n = 0; n < 2; ++n) {
        drawOn(s, 2, 500 + 40 * n);
        const auto r = s.save();
        ASSERT_TRUE(r.ok) << r.error;
        EXPECT_EQ(r.incremental, n > 0) << "save " << n + 1;
        int code = -1;
        EXPECT_EQ((qpdfCheck(out, code), code), 0);
        expectSamePages(out, writtenInFull(s, "full.pdf"), 4, "a file without the record, save " + std::to_string(n + 1));
        auto again = DocumentSession::loadFile(out);
        ASSERT_TRUE(again.document);
        EXPECT_TRUE(again.hybridChanged.empty());
        EXPECT_EQ(describe(*again.document), describeAsXopp(*s.getDocument(), path("same.xopp")));
    }
}

namespace {
/// A PDF of three pages with an outline of its own ("Chapter 1" to page 1, "Chapter 2" to page 3).
void makeBookPdf(const fs::path& p) {
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    cairo_pdf_surface_add_outline(s, CAIRO_PDF_OUTLINE_ROOT, "Chapter 1", "page=1", CAIRO_PDF_OUTLINE_FLAG_OPEN);
    cairo_pdf_surface_add_outline(s, CAIRO_PDF_OUTLINE_ROOT, "Chapter 2", "page=3", CAIRO_PDF_OUTLINE_FLAG_OPEN);
    for (int i = 0; i < 3; ++i) {
        cairo_move_to(cr, 72, 100);
        cairo_show_text(cr, ("page " + std::to_string(i + 1)).c_str());
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

/// The top-level titles of the outline, and our item's entries (page index, title).
std::pair<std::vector<std::string>, std::vector<std::pair<int, std::string>>> outlineOf(const fs::path& pdf) {
    QPDF q;
    q.processFile(pdf.string().c_str());
    std::vector<std::string> titles;
    for (QPDFObjectHandle c = q.getRoot().getKey("/Outlines").getKey("/First"); c.isDictionary();
         c = c.getKey("/Next")) {
        titles.push_back(c.getKey("/Title").getUTF8Value());
    }
    const auto pages = QPDFPageDocumentHelper(q).getAllPages();
    std::vector<std::pair<int, std::string>> entries;
    for (const auto& e: PdfBookmarks::read(q)) {
        int index = -1;
        for (size_t i = 0; i < pages.size(); ++i) {
            if (pages[i].getObjectHandle().getObjGen() == e.page.getObjGen()) {
                index = static_cast<int>(i);
            }
        }
        entries.emplace_back(index, e.title);
    }
    return {titles, entries};
}
}  // namespace

// A PDF with notes lists the bookmarks in its own outline ("Bookmarks", after the document's table of contents, which
// stays): written with the whole file, then appended by Ctrl+S only when they changed; the embedded document keeps
// them too, so the file opens with them
TEST_F(IncrementalSaveTest, bookmarksGoIntoTheOutline) {
    using Outline = std::pair<std::vector<std::string>, std::vector<std::pair<int, std::string>>>;
    using Entries = std::vector<std::pair<int, std::string>>;
    makeBookPdf(path("book.pdf"));
    auto loaded = DocumentSession::loadFile(path("book.pdf"));
    ASSERT_TRUE(loaded.document);
    auto s = std::make_unique<DocumentSession>(*app, std::move(loaded.document));
    ASSERT_TRUE(s->setBookmark(0, std::string("Intro")));
    ASSERT_TRUE(s->setBookmark(2, std::string()));
    const fs::path out = path("book.notes.pdf");
    auto r = s->saveAsHybrid(out);
    ASSERT_TRUE(r.ok) << r.error;
    int code = -1;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);
    EXPECT_EQ(outlineOf(out), (Outline{{"Chapter 1", "Chapter 2", "Bookmarks"}, Entries{{0, "Intro"}, {2, "Page 3"}}}));
    {
        auto reopened = DocumentSession::loadFile(out);
        ASSERT_TRUE(reopened.document);
        const auto marks = PageBookmarks::of(*reopened.document);
        ASSERT_EQ(marks.size(), 2u);
        EXPECT_EQ(marks[0].label, "Intro");
        EXPECT_EQ(marks[1].page, 2u);
        EXPECT_EQ(marks[1].label, "") << "the automatic label stays automatic";
        size_t ours = 0;
        for (const auto& e: reopened.document->getOutline()) {
            ours += PageBookmarks::isOutlineItem(e) ? 1 : 0;
        }
        EXPECT_EQ(ours, 1u) << "poppler sees our item (the table of contents leaves it out)";
    }
    // A stroke only: the outline is not touched
    drawOn(*s, 1, 400);
    const std::string before = readFile(out);
    r = s->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    EXPECT_EQ(readFile(out).substr(before.size()).find("/Title"), std::string::npos) << "no outline item written";
    // Renamed, one more: appended
    ASSERT_TRUE(s->setBookmark(0, std::string("Introduction")));
    ASSERT_TRUE(s->setBookmark(1, std::string("Middle")));
    r = s->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    EXPECT_EQ((qpdfCheck(out, code), code), 0);
    EXPECT_EQ(outlineOf(out), (Outline{{"Chapter 1", "Chapter 2", "Bookmarks"},
                                       Entries{{0, "Introduction"}, {1, "Middle"}, {2, "Page 3"}}}));
    // Pages moved: the entries go to the pages where they are now (and "Page N" follows)
    ASSERT_TRUE(s->movePages({2}, 0));
    r = s->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);
    EXPECT_EQ(outlineOf(out).second, (Entries{{0, "Page 1"}, {1, "Introduction"}, {2, "Middle"}}));
    // None left: the item goes, the document's own outline stays
    for (int page: {0, 1, 2}) {
        s->setBookmark(static_cast<size_t>(page), std::nullopt);
    }
    r = s->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    EXPECT_EQ((qpdfCheck(out, code), code), 0);
    EXPECT_EQ(outlineOf(out), (Outline{{"Chapter 1", "Chapter 2"}, Entries{}}));
    auto reopened = DocumentSession::loadFile(out);
    ASSERT_TRUE(reopened.document);
    EXPECT_TRUE(PageBookmarks::of(*reopened.document).empty());
}
