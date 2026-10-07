/*
 * xournal-qt: encrypted PDFs (qt/docs/hybrid-pdf.md, "Encrypted PDFs"): opening with and without a password, an
 * owner password only, protecting, changing and removing a password, saving keeps the encryption, appended saves of
 * an encrypted PDF with notes (qpdf --check with the password, poppler, every earlier revision), version history, and
 * nothing of a protected document left unencrypted in the app's cache.
 *
 * The encrypted fixtures are made here with qpdf, in a temporary folder (never in test/files).
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFJob.hh>
#include <qpdf/QPDFLogger.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#include <zlib.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include "model/BackgroundImage.h"
#include "model/Document.h"
#include "model/PageType.h"
#include "model/Font.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfDocument.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "session/PdfEncryption.h"
#include "session/PdfHistory.h"
#include "session/PdfRevisions.h"
#include "util/Util.h"
#include "util/PathUtil.h"

#include "config.h"
#include "support/TestSupport.h"

using xqt::test::gunzip;

using xqt::test::readFile;

using xqt::test::makeTextPdf;
using xqt::test::numbered;

using namespace xqt;

namespace {

constexpr const char* PASSWORD = "s3cret pass";
constexpr const char* MARKER = "inkmarkerQ7Z";  ///< a text of the document: it must never be found unencrypted

/// Encrypt `in` as `out` with qpdf directly (as another app would): AES-256, a user password (may be empty) and an
/// owner password; `restrict`: no printing, no copying.
void encrypt(const fs::path& in, const fs::path& out, const std::string& user, const std::string& owner,
             bool restrict = false) {
    QPDF q;
    q.processFile(in.string().c_str());
    QPDFWriter w(q, out.string().c_str());
    w.setR6EncryptionParameters(user.c_str(), owner.c_str(), true, !restrict, true, true, true, true,
                                restrict ? qpdf_r3p_none : qpdf_r3p_full, true);
    w.write();
}

/// qpdf --check (with a password)
int qpdfCheck(const fs::path& pdf, const std::string& password, std::string* report = nullptr) {
    std::ostringstream out, err;
    QPDFJob job;
    auto logger = QPDFLogger::create();
    logger->setOutputStreams(&out, &err);
    job.setLogger(logger);
    const std::string file = pdf.string();
    const std::string pw = "--password=" + password;
    const char* argv[] = {"qpdf", "--check", pw.c_str(), file.c_str(), nullptr};
    job.initializeFromArgv(argv);
    job.run();
    if (report) {
        *report = out.str() + err.str();
    }
    return job.getExitCode();
}

bool popplerOpens(const fs::path& pdf, const std::string& password) {
    XojPdfDocument doc;
    GError* error = nullptr;
    const bool ok = doc.load(pdf, password, &error) && doc.getPageCount() > 0;
    if (error) {
        g_error_free(error);
    }
    if (ok) {  // (and draws its pages)
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 60, 80);
        cairo_t* cr = cairo_create(s);
        for (size_t i = 0; i < doc.getPageCount(); ++i) {
            doc.getPage(i)->render(cr);
        }
        cairo_destroy(cr);
        cairo_surface_destroy(s);
    }
    return ok;
}

/// Everything readable without a password in `pdf`: its strings and its streams decoded (empty when it needs one).
std::string readableText(const fs::path& pdf) {
    std::string all;
    try {
        QPDF q;
        q.setSuppressWarnings(true);
        q.processFile(pdf.string().c_str());
        for (QPDFObjectHandle o: q.getAllObjects()) {
            all += o.isStream() ? o.getDict().unparse() : o.unparseResolved();
            if (o.isStream()) {
                try {
                    auto b = o.getStreamData(qpdf_dl_all);
                    std::string data(reinterpret_cast<const char*>(b->getBuffer()), b->getSize());
                    all += gunzip(data);  // (an embedded .xopp)
                    all += data;
                } catch (const std::exception&) {
                }
            }
        }
    } catch (const std::exception&) {
    }
    return all;
}

/// The files under `dir` in which `marker` can be read without a password: as bytes, gunzipped, or in a PDF that
/// opens without one.
std::vector<fs::path> leaks(const fs::path& dir, const std::string& marker) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file()) {
            continue;
        }
        const std::string bytes = readFile(it->path());
        bool found = bytes.find(marker) != std::string::npos || gunzip(bytes).find(marker) != std::string::npos;
        if (!found && bytes.rfind("%PDF", 0) == 0) {
            found = readableText(it->path()).find(marker) != std::string::npos;
        }
        if (found) {
            out.push_back(it->path());
        }
    }
    return out;
}

void addStroke(Layer* layer, double y) {
    auto s = std::make_unique<Stroke>();
    s->setToolType(StrokeTool::PEN);
    s->setColor(Color(0xffcc0000U));
    s->setWidth(2);
    for (int j = 0; j < 10; ++j) {
        s->addPoint(Point(80 + j * 20, y + 5 * std::sin(j / 2.0), 1 + (j % 3) / 3.0));
    }
    s->getBoundingBox();
    layer->addElement(std::move(s));
}

void addText(Layer* layer, const std::string& text) {
    auto t = std::make_unique<Text>();
    t->setText(text);
    t->setFont(XojFont("Sans", 14));
    t->setColor(Color(0xff000080U));
    t->move(80, 400);
    t->getBoundingBox();
    layer->addElement(std::move(t));
}

size_t elementCount(Document& doc) {
    size_t n = 0;
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        for (const Layer* l: doc.getPage(i)->getLayers()) {
            n += l->getElementsView().size();
        }
    }
    return n;
}

class PdfEncryptionTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        HybridPdf::compactAbove = 1000;  // (the small files: appended to, never compacted)
    }
    void TearDown() override { HybridPdf::compactAbove = 0.25; }
    fs::path path(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    /// A plain PDF of two pages, protected with PASSWORD (the owner password another one).
    fs::path protectedPdf(const char* name) {
        makeTextPdf(path("plain.pdf"), numbered("page", 2));
        const fs::path out = path(name);
        encrypt(path("plain.pdf"), out, PASSWORD, "the owner");
        return out;
    }
    /// The protected PDF opened with its password, notes written on it (a stroke, the marker text), saved into it.
    std::unique_ptr<DocumentSession> protectedNotes(const fs::path& pdf) {
        auto loaded = DocumentSession::loadFile(pdf, false, PASSWORD);
        EXPECT_TRUE(loaded.document) << loaded.error;
        auto s = std::make_unique<DocumentSession>(*app, std::move(loaded.document));
        {
            std::unique_lock lock(*s->getDocument());
            addStroke(s->getDocument()->getPage(0)->getSelectedLayer(), 200);
            addText(s->getDocument()->getPage(1)->getSelectedLayer(), MARKER);
        }
        const auto r = s->saveAsHybrid(pdf);
        EXPECT_TRUE(r.ok) << r.error;
        return s;
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
};

}  // namespace

TEST_F(PdfEncryptionTest, aPasswordIsNeededAndChecked) {
    const fs::path pdf = protectedPdf("locked.pdf");
    auto st = PdfEncryption::probe(pdf);
    EXPECT_TRUE(st.encrypted);
    EXPECT_TRUE(st.needsPassword);
    EXPECT_FALSE(st.readable);
    st = PdfEncryption::probe(pdf, "wrong");
    EXPECT_TRUE(st.wrongPassword);
    EXPECT_FALSE(st.readable);
    st = PdfEncryption::probe(pdf, PASSWORD);
    EXPECT_TRUE(st.readable);
    EXPECT_TRUE(st.isProtected());
    EXPECT_TRUE(st.aes256);
    EXPECT_EQ(st.revision, 6);

    // Opening: without a password it is not opened, a wrong one is said so, the right one opens it
    auto none = DocumentSession::loadFile(pdf);
    EXPECT_FALSE(none.document);
    EXPECT_TRUE(none.needsPassword);
    EXPECT_FALSE(none.wrongPassword);
    EXPECT_EQ(none.passwordFile, pdf);
    auto wrong = DocumentSession::loadFile(pdf, false, "wrong");
    EXPECT_FALSE(wrong.document);
    EXPECT_TRUE(wrong.wrongPassword);
    EXPECT_FALSE(PdfEncryption::isKnown(pdf)) << "a wrong password is not remembered";
    {
        auto right = DocumentSession::loadFile(pdf, false, PASSWORD);
        ASSERT_TRUE(right.document) << right.error;
        EXPECT_TRUE(right.encrypted);
        EXPECT_EQ(right.document->getPageCount(), 2U);
        EXPECT_TRUE(PdfEncryption::isProtected(pdf));
        DocumentSession s(*app, std::move(right.document));
        EXPECT_TRUE(s.isProtected());
        right = {};
        EXPECT_TRUE(PdfEncryption::isKnown(pdf)) << "known while the document is open";
        EXPECT_TRUE(DocumentSession::loadFile(pdf).needsPassword) << "never opened without it (the library)";
    }
    EXPECT_FALSE(PdfEncryption::isKnown(pdf)) << "forgotten when it is closed";
}

TEST_F(PdfEncryptionTest, anOwnerPasswordOnlyOpensWithoutAskingAndTellsItsRestrictions) {
    makeTextPdf(path("plain.pdf"), numbered("page", 2));
    encrypt(path("plain.pdf"), path("restricted.pdf"), "", "the owner", /*restrict=*/true);
    const auto st = PdfEncryption::probe(path("restricted.pdf"));
    EXPECT_TRUE(st.readable);
    EXPECT_TRUE(st.encrypted);
    EXPECT_FALSE(st.needsPassword);
    EXPECT_FALSE(st.isProtected());
    EXPECT_FALSE(st.allowPrint);
    EXPECT_FALSE(st.allowCopy);
    auto loaded = DocumentSession::loadFile(path("restricted.pdf"));
    ASSERT_TRUE(loaded.document) << loaded.error;
    EXPECT_FALSE(loaded.needsPassword);
    EXPECT_FALSE(loaded.encrypted) << "anyone can read it: not treated as confidential";
    EXPECT_FALSE(loaded.allowPrint);
    EXPECT_FALSE(loaded.allowCopy);

    // Notes saved into it keep its encryption and its restrictions
    DocumentSession s(*app, std::move(loaded.document));
    {
        std::unique_lock lock(*s.getDocument());
        addStroke(s.getDocument()->getPage(0)->getSelectedLayer(), 300);
    }
    ASSERT_TRUE(s.saveAsHybrid(path("restricted.pdf")).ok);
    const auto after = PdfEncryption::probe(path("restricted.pdf"));
    EXPECT_TRUE(after.encrypted);
    EXPECT_FALSE(after.allowPrint);
    EXPECT_EQ(qpdfCheck(path("restricted.pdf"), ""), 0);
    auto again = DocumentSession::loadFile(path("restricted.pdf"));
    ASSERT_TRUE(again.document) << again.error;
    EXPECT_EQ(elementCount(*again.document), 1U);
}

// Notes kept as a .xopp beside a protected PDF (Xournal++ files): opening the .xopp asks for the PDF's password
TEST_F(PdfEncryptionTest, aXoppOnAProtectedPdfAsksForThePdfsPassword) {
    const fs::path pdf = protectedPdf("locked.pdf");
    {
        auto loaded = DocumentSession::loadFile(pdf, false, PASSWORD);
        ASSERT_TRUE(loaded.document) << loaded.error;
        DocumentSession s(*app, std::move(loaded.document));
        {
            std::unique_lock lock(*s.getDocument());
            addStroke(s.getDocument()->getPage(0)->getSelectedLayer(), 200);
        }
        ASSERT_TRUE(s.saveAs(path("locked.xopp")).ok);
    }
    auto none = DocumentSession::loadFile(path("locked.xopp"));
    EXPECT_FALSE(none.document);
    EXPECT_TRUE(none.needsPassword);
    EXPECT_EQ(none.passwordFile, pdf) << "the PDF's password";
    EXPECT_TRUE(DocumentSession::loadFile(path("locked.xopp"), false, "wrong").wrongPassword);
    auto right = DocumentSession::loadFile(path("locked.xopp"), false, PASSWORD);
    ASSERT_TRUE(right.document) << right.error;
    EXPECT_TRUE(right.warnings.empty());
    EXPECT_EQ(right.document->getPdfPageCount(), 2U);
    EXPECT_EQ(right.document->getFilepath(), path("locked.xopp"));
    EXPECT_EQ(elementCount(*right.document), 1U);
    DocumentSession s(*app, std::move(right.document));
    EXPECT_TRUE(s.isProtected()) << "its PDF is: its pages are not stored, it is not indexed";
}

TEST_F(PdfEncryptionTest, protectChangeAndRemoveThePassword) {
    makeTextPdf(path("doc.pdf"), numbered("page", 2));
    PdfEncryption::Protection p;
    p.password = "first";
    std::string error;
    ASSERT_TRUE(PdfEncryption::rewrite(path("doc.pdf"), "", path("doc.pdf"), &p, error)) << error;
    auto st = PdfEncryption::probe(path("doc.pdf"));
    EXPECT_TRUE(st.needsPassword);
    st = PdfEncryption::probe(path("doc.pdf"), "first");
    EXPECT_TRUE(st.readable);
    EXPECT_TRUE(st.aes256);
    EXPECT_EQ(st.revision, 6);
    EXPECT_FALSE(st.owner) << "a random owner password without restrictions";
    EXPECT_TRUE(st.allowPrint && st.allowCopy && st.allowModify);
    EXPECT_EQ(qpdfCheck(path("doc.pdf"), "first"), 0);
    EXPECT_TRUE(popplerOpens(path("doc.pdf"), "first"));
    EXPECT_FALSE(popplerOpens(path("doc.pdf"), ""));

    // Changed, with restrictions and an owner password
    PdfEncryption::Protection q;
    q.password = "second";
    q.allowPrint = false;
    q.allowCopy = false;
    EXPECT_FALSE(PdfEncryption::check(q).empty()) << "restrictions need an owner password";
    q.ownerPassword = "second";
    EXPECT_FALSE(PdfEncryption::check(q).empty()) << "and a different one";
    q.ownerPassword = "boss";
    ASSERT_TRUE(PdfEncryption::rewrite(path("doc.pdf"), "first", path("doc.pdf"), &q, error)) << error;
    EXPECT_TRUE(PdfEncryption::probe(path("doc.pdf"), "first").wrongPassword);
    st = PdfEncryption::probe(path("doc.pdf"), "second");
    EXPECT_TRUE(st.readable);
    EXPECT_FALSE(st.allowPrint);
    EXPECT_FALSE(st.allowCopy);
    EXPECT_TRUE(PdfEncryption::probe(path("doc.pdf"), "boss").owner);
    EXPECT_EQ(qpdfCheck(path("doc.pdf"), "second"), 0);
    EXPECT_TRUE(popplerOpens(path("doc.pdf"), "second"));

    // Removed
    ASSERT_TRUE(PdfEncryption::rewrite(path("doc.pdf"), "second", path("doc.pdf"), nullptr, error)) << error;
    st = PdfEncryption::probe(path("doc.pdf"));
    EXPECT_TRUE(st.readable);
    EXPECT_FALSE(st.encrypted);
    EXPECT_TRUE(popplerOpens(path("doc.pdf"), ""));
    EXPECT_EQ(qpdfCheck(path("doc.pdf"), ""), 0);
}

TEST_F(PdfEncryptionTest, notesSavedIntoAProtectedPdfStayEncryptedAndOpenWithThePassword) {
    const fs::path pdf = protectedPdf("lecture.pdf");
    auto s = protectedNotes(pdf);
    EXPECT_TRUE(HybridPdf::isHybrid(pdf));
    EXPECT_TRUE(PdfEncryption::probe(pdf).needsPassword) << "still protected";
    EXPECT_EQ(qpdfCheck(pdf, PASSWORD), 0);
    EXPECT_TRUE(popplerOpens(pdf, PASSWORD));
    EXPECT_TRUE(readableText(pdf).empty());
    EXPECT_EQ(readFile(pdf).find(MARKER), std::string::npos);
    const size_t elements = elementCount(*s->getDocument());
    s.reset();

    auto none = DocumentSession::loadFile(pdf);
    EXPECT_TRUE(none.needsPassword);
    auto again = DocumentSession::loadFile(pdf, false, PASSWORD);
    ASSERT_TRUE(again.document) << again.error;
    EXPECT_TRUE(again.hybrid);
    EXPECT_EQ(elementCount(*again.document), elements);
    EXPECT_TRUE(PdfEncryption::probe(again.document->getPdfFilepath()).needsPassword)
            << "the clean copy in the cache is encrypted too";
}

// A page whose background is an attached image: in a protected PDF with notes it is read from memory too
TEST_F(PdfEncryptionTest, anAttachedBackgroundImageOpensFromMemory) {
    const fs::path pdf = protectedPdf("photo.pdf");
    auto loaded = DocumentSession::loadFile(pdf, false, PASSWORD);
    ASSERT_TRUE(loaded.document) << loaded.error;
    {
        GdkPixbuf* pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, false, 8, 120, 90);
        gdk_pixbuf_fill(pixbuf, 0x3366ccff);
        ASSERT_TRUE(gdk_pixbuf_save(pixbuf, path("photo.png").string().c_str(), "png", nullptr, nullptr));
        g_object_unref(pixbuf);
        auto page = std::make_shared<XojPage>(595, 842);
        BackgroundImage img;
        GError* error = nullptr;
        img.loadFile(path("photo.png"), &error);
        ASSERT_EQ(error, nullptr);
        img.setAttach(true);
        page->setBackgroundImage(img);
        page->setBackgroundType(PageType(PageTypeFormat::Image));
        loaded.document->addPage(page);
    }
    {
        DocumentSession s(*app, std::move(loaded.document));
        ASSERT_TRUE(s.saveAsHybrid(pdf).ok);
    }
    fs::remove(path("photo.png"));
    auto again = DocumentSession::loadFile(pdf, false, PASSWORD);
    ASSERT_TRUE(again.document) << again.error;
    EXPECT_TRUE(again.warnings.empty()) << again.warnings.front();
    const PageRef last = again.document->getPage(again.document->getPageCount() - 1);
    ASSERT_TRUE(last->getBackgroundType().isImagePage());
    ASSERT_NE(last->getBackgroundImage().getPixbuf(), nullptr) << "read from the encrypted file, in memory";
    EXPECT_EQ(gdk_pixbuf_get_width(last->getBackgroundImage().getPixbuf()), 120);
}

TEST_F(PdfEncryptionTest, aProtectedPdfWithNotesIsAppendedToEncrypted) {
    const fs::path pdf = protectedPdf("lecture.pdf");
    protectedNotes(pdf).reset();
    auto loaded = DocumentSession::loadFile(pdf, false, PASSWORD);
    ASSERT_TRUE(loaded.document) << loaded.error;
    DocumentSession s(*app, std::move(loaded.document));
    const auto sizeBefore = fs::file_size(pdf);
    for (int i = 0; i < 3; ++i) {
        {
            std::unique_lock lock(*s.getDocument());
            addStroke(s.getDocument()->getPage(i % 2)->getSelectedLayer(), 500 + 20 * i);
        }
        const auto r = s.save();
        ASSERT_TRUE(r.ok) << r.error;
        EXPECT_TRUE(r.incremental) << "save " << i << ": only what changed is appended, encrypted";
        std::string report;
        EXPECT_EQ(qpdfCheck(pdf, PASSWORD, &report), 0) << report;
        EXPECT_TRUE(popplerOpens(pdf, PASSWORD));
        EXPECT_TRUE(readableText(pdf).empty());
    }
    EXPECT_GT(fs::file_size(pdf), sizeBefore);
    EXPECT_EQ(readFile(pdf).find("xopp:p"), std::string::npos) << "the names of our annotations are encrypted too";
    // Every earlier revision is a file that opens with the password
    const auto chain = PdfRevisions::read(pdf);
    ASSERT_GE(chain.revisions.size(), 4U);
    for (const auto& rev: chain.revisions) {
        const fs::path cut = path("cut.pdf");
        {
            const std::string bytes = readFile(pdf).substr(0, rev.end);
            std::ofstream out(cut, std::ios::binary);
            out << bytes;
        }
        std::string report;
        EXPECT_EQ(qpdfCheck(cut, PASSWORD, &report), 0) << "revision ending at " << rev.end << ": " << report;
        EXPECT_TRUE(popplerOpens(cut, PASSWORD));
    }
    // Opened again: the same notes
    const size_t elements = elementCount(*s.getDocument());
    auto again = DocumentSession::loadFile(pdf, false, PASSWORD);
    ASSERT_TRUE(again.document) << again.error;
    EXPECT_EQ(elementCount(*again.document), elements);
    EXPECT_TRUE(again.hybridChanged.empty()) << "the strings of the update decrypt to what was written";
}

TEST_F(PdfEncryptionTest, versionHistoryWorksEncrypted) {
    const fs::path pdf = protectedPdf("lecture.pdf");
    auto loaded = DocumentSession::loadFile(pdf, false, PASSWORD);
    ASSERT_TRUE(loaded.document) << loaded.error;
    DocumentSession s(*app, std::move(loaded.document));
    s.setKeepsVersions(true);
    {
        std::unique_lock lock(*s.getDocument());
        addStroke(s.getDocument()->getPage(0)->getSelectedLayer(), 200);
    }
    auto r = s.saveAsHybrid(pdf);
    ASSERT_TRUE(r.ok) << r.error;
    for (const char* message: {"first milestone", "second milestone"}) {
        {
            std::unique_lock lock(*s.getDocument());
            addStroke(s.getDocument()->getPage(1)->getSelectedLayer(), 300);
        }
        DocumentSession::SaveRequest req;
        req.message = message;
        s.saveInBackground(req);
        ASSERT_TRUE(s.waitForSaves());
        std::string report;
        EXPECT_EQ(qpdfCheck(pdf, PASSWORD, &report), 0) << report;
    }
    const auto listed = PdfHistory::list(pdf);
    ASSERT_GE(listed.versions.size(), 3U);
    std::string error;
    const fs::path v1 = path("v1.pdf");
    ASSERT_TRUE(HybridPdf::writeVersion(pdf, listed.versions[1].id, v1, error)) << error;
    EXPECT_TRUE(PdfEncryption::probe(v1).needsPassword) << "an earlier version stays encrypted";
    EXPECT_EQ(qpdfCheck(v1, PASSWORD), 0);
    auto opened = DocumentSession::loadFile(v1, false, PASSWORD);
    ASSERT_TRUE(opened.document) << opened.error;
}

TEST_F(PdfEncryptionTest, nothingOfAProtectedDocumentStaysUnencryptedInTheCache) {
    const fs::path cache = fs::path(qEnvironmentVariable("XDG_CACHE_HOME").toStdString());
    ASSERT_FALSE(cache.empty());
    const fs::path pdf = protectedPdf("secret.pdf");
    auto s = protectedNotes(pdf);
    // Edited: autosaved (an encrypted PDF in the cache), saved again (appended), a PDF copy to share
    {
        std::unique_lock lock(*s->getDocument());
        addText(s->getDocument()->getPage(0)->getSelectedLayer(), std::string(MARKER) + " again");
    }
    ASSERT_TRUE(s->autosave().ok);
    ASSERT_TRUE(s->waitForSaves());
    const fs::path autosave = DocumentSession::protectedAutosavePath(Util::getPid(), s->serial());
    ASSERT_TRUE(fs::exists(autosave));
    EXPECT_TRUE(PdfEncryption::probe(autosave).needsPassword) << "the autosave is encrypted";
    EXPECT_TRUE(PdfEncryption::probe(autosave, PASSWORD).readable) << "with the same password";
    EXPECT_FALSE(fs::exists(DocumentSession::unnamedAutosavePath(Util::getPid(), s->serial())));
    ASSERT_TRUE(s->save().ok);
    DocumentSession::SaveRequest copy;
    copy.kind = DocumentSession::SaveKind::ExportHybrid;
    copy.target = cache / "share" / "secret.pdf";
    fs::create_directories(copy.target.parent_path());
    s->saveInBackground(copy);
    ASSERT_TRUE(s->waitForSaves());
    EXPECT_TRUE(PdfEncryption::probe(copy.target).needsPassword) << "a copy stays protected";
    // Opened again (the clean copy), then closed
    s.reset();
    {
        auto again = DocumentSession::loadFile(pdf, false, PASSWORD);
        ASSERT_TRUE(again.document) << again.error;
        DocumentSession reopened(*app, std::move(again.document));
        const auto found = leaks(cache, MARKER);
        EXPECT_TRUE(found.empty()) << "readable in " << (found.empty() ? std::string() : found[0].string());
    }
    const auto found = leaks(cache, MARKER);
    EXPECT_TRUE(found.empty()) << "readable in " << (found.empty() ? std::string() : found[0].string());
    // The check finds it where it is readable (the test of the test)
    const fs::path plain = cache / "plain-check.xopp";
    {
        std::ofstream out(plain);
        out << MARKER;
    }
    EXPECT_EQ(leaks(cache, MARKER).size(), 1U);
    fs::remove(plain);
}

// What a protected PDF takes out into the cache while it is open (its pictures and recordings) is marked with the
// process, removed when it is closed, and what a crashed process left behind goes at the next start
TEST_F(PdfEncryptionTest, picturesAndRecordingsTakenOutDoNotOutliveTheProcess) {
    const fs::path pdf = protectedPdf("lecture.pdf");
    protectedNotes(pdf).reset();
    const std::string mark = "unpacked-" + std::to_string(Util::getPid());
    fs::path entry;
    {
        auto loaded = DocumentSession::loadFile(pdf, false, PASSWORD);
        ASSERT_TRUE(loaded.document) << loaded.error;
        DocumentSession s(*app, std::move(loaded.document));
        entry = s.getDocument()->getPdfFilepath().parent_path();
        EXPECT_TRUE(fs::exists(entry / mark)) << "marked with this process";
        EXPECT_TRUE(fs::exists(entry / "pictures"));
    }
    EXPECT_FALSE(fs::exists(entry / "pictures")) << "removed when it is closed";
    EXPECT_FALSE(fs::exists(entry / mark));
    EXPECT_TRUE(fs::exists(entry / "base.pdf")) << "the encrypted clean copy stays";

    // Left behind by a process that crashed (and one by a process that still runs)
    const fs::path cache = HybridPdf::cacheFolder();
    const fs::path work = Util::getCacheSubfolder("md-assets");
    auto leftover = [](const fs::path& dir, int64_t pid) {
        fs::create_directories(dir / "pictures");
        fs::create_directories(dir / "audio");
        std::ofstream(dir / "pictures" / "photo.png") << "picture";
        std::ofstream(dir / "audio" / "memo.ogg") << "sound";
        std::ofstream(dir / ("unpacked-" + std::to_string(pid)));
        std::ofstream(dir / "base.pdf") << "%PDF";
    };
    leftover(cache / "aaaa-crashed", 4999999);
    leftover(cache / "bbbb-running", 4999998);
    leftover(work / "cccc-crashed", 4999999);
    leftover(cache / "dddd-plain", 0);
    fs::remove(cache / "dddd-plain" / "unpacked-0");  // (not a protected PDF's: not marked)
    const int removed =
            HybridPdf::removeProtectedLeftovers([](int64_t pid) { return pid == 4999998 || pid == Util::getPid(); });
    EXPECT_EQ(removed, 2);
    EXPECT_FALSE(fs::exists(cache / "aaaa-crashed" / "pictures"));
    EXPECT_FALSE(fs::exists(cache / "aaaa-crashed" / "audio"));
    EXPECT_FALSE(fs::exists(cache / "aaaa-crashed" / "unpacked-4999999"));
    EXPECT_TRUE(fs::exists(cache / "aaaa-crashed" / "base.pdf")) << "(an encrypted clean copy: kept)";
    EXPECT_FALSE(fs::exists(work / "cccc-crashed")) << "a work folder of Markdown pictures: whole";
    EXPECT_TRUE(fs::exists(cache / "bbbb-running" / "pictures" / "photo.png")) << "its process still has it open";
    EXPECT_TRUE(fs::exists(cache / "dddd-plain" / "pictures" / "photo.png"));
    for (const char* d: {"aaaa-crashed", "bbbb-running", "dddd-plain"}) {
        fs::remove_all(cache / d);
    }
}

// Measurements (skipped unless XQT_BENCH_ENCRYPTED is set: a PDF to use, or "1" for a generated one of 1,321 pages):
// a protected PDF with notes on every 25th page, saved in full, then appended to (one stroke), then protected anew.
TEST_F(PdfEncryptionTest, benchLongProtectedPdf) {
    const QByteArray given = qgetenv("XQT_BENCH_ENCRYPTED");
    if (given.isEmpty()) {
        GTEST_SKIP() << "XQT_BENCH_ENCRYPTED not set";
    }
    fs::path source = given == "1" ? path("long.pdf") : fs::path(given.toStdString());
    if (given == "1") {
        makeTextPdf(source, numbered("page", 1321));
    }
    const fs::path pdf = path("long-protected.pdf");
    encrypt(source, pdf, PASSWORD, "the owner");
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point a) {
        return std::chrono::duration<double, std::milli>(Clock::now() - a).count();
    };
    auto t = Clock::now();
    auto loaded = DocumentSession::loadFile(pdf, false, PASSWORD);
    ASSERT_TRUE(loaded.document) << loaded.error;
    std::printf("open (plain, protected): %.0f ms, %zu pages\n", ms(t), loaded.document->getPageCount());
    DocumentSession s(*app, std::move(loaded.document));
    {
        std::unique_lock lock(*s.getDocument());
        for (size_t i = 0; i < s.getDocument()->getPageCount(); i += 25) {
            addStroke(s.getDocument()->getPage(i)->getSelectedLayer(), 300);
        }
    }
    t = Clock::now();
    ASSERT_TRUE(s.saveAsHybrid(pdf).ok);
    std::printf("first save, in full, encrypted: %.0f ms, %llu KB\n", ms(t),
                static_cast<unsigned long long>(fs::file_size(pdf) / 1024));
    for (int i = 0; i < 3; ++i) {
        {
            std::unique_lock lock(*s.getDocument());
            addStroke(s.getDocument()->getPage(50)->getSelectedLayer(), 400 + 10 * i);
        }
        t = Clock::now();
        const auto r = s.save();
        ASSERT_TRUE(r.ok) << r.error;
        std::printf("Ctrl+S after one stroke: %.0f ms, %s, %llu bytes appended\n", ms(t),
                    r.incremental ? "appended" : "in full", static_cast<unsigned long long>(r.appended));
    }
    t = Clock::now();
    auto again = DocumentSession::loadFile(pdf, false, PASSWORD);
    ASSERT_TRUE(again.document);
    std::printf("open the protected PDF with notes again: %.0f ms\n", ms(t));
    PdfEncryption::Protection p;
    p.password = "another";
    std::string error;
    t = Clock::now();
    ASSERT_TRUE(PdfEncryption::rewrite(pdf, PASSWORD, path("rewritten.pdf"), &p, error)) << error;
    std::printf("protected anew (the whole file): %.0f ms\n", ms(t));
}
