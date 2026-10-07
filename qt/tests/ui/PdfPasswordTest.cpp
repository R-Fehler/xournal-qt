/*
 * xournal-qt: encrypted PDFs in the real window (qt/docs/hybrid-pdf.md, "Encrypted PDFs"): opening a protected PDF
 * asks for its password (a wrong one is said so and asked again, Cancel leaves it closed), ⋮ → Document protects the
 * document's PDF and changes or removes its password, and Share protects a copy.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <functional>
#include <iterator>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <zlib.h>
#include <qpdf/QPDFWriter.hh>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Font.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/PdfEncryption.h"
#include "shell/DocumentFiles.h"
#include "shell/HitPages.h"
#include "shell/LibraryIndex.h"
#include "shell/LibraryModel.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/Previews.h"
#include "shell/RecentFiles.h"
#include "shell/SettingsModel.h"
#include "shell/SystemApps.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"

#include "AppController.h"
#include "UiFixture.h"
#include "support/TestSupport.h"

using xqt::test::readFile;
using xqt::test::gunzip;

namespace fs = std::filesystem;

namespace {
void makePdf(const fs::path& file) {
    cairo_surface_t* s = cairo_pdf_surface_create(file.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    cairo_set_font_size(cr, 24);
    for (int i = 0; i < 2; ++i) {
        cairo_move_to(cr, 72, 100);
        cairo_show_text(cr, ("page" + std::to_string(i + 1)).c_str());
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

void protect(const fs::path& in, const fs::path& out, const char* password) {
    QPDF q;
    q.processFile(in.string().c_str());
    QPDFWriter w(q, out.string().c_str());
    w.setR6EncryptionParameters(password, "owner of it", true, true, true, true, true, true, qpdf_r3p_full, true);
    w.write();
}

void drawStroke(xqt::DocumentSession& s, size_t pageNo, double y) {
    auto page = s.getDocument()->getPage(pageNo);
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(2);
    stroke->setColor(Color(0xffcc0000U));
    stroke->addPoint(Point(100, y, 1.0));
    stroke->addPoint(Point(200, y + 80, 3.0));
    const Stroke* raw = stroke.get();
    Layer* layer = page->getSelectedLayer();
    s.getDocument()->lock();
    layer->addElement(std::move(stroke));
    s.getDocument()->unlock();
    s.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
}

void addText(xqt::DocumentSession& s, size_t pageNo, const std::string& text) {
    auto page = s.getDocument()->getPage(pageNo);
    auto t = std::make_unique<Text>();
    t->setText(text);
    t->setFont(XojFont("Sans", 14));
    t->setColor(Color(0xff000080U));
    t->move(80, 400);
    t->getBoundingBox();
    const Text* raw = t.get();
    Layer* layer = page->getSelectedLayer();
    s.getDocument()->lock();
    layer->addElement(std::move(t));
    s.getDocument()->unlock();
    s.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
}

/// What can be read of a file without a password: its bytes, gunzipped, a library pack uncompressed, a PDF's strings
/// and streams (decoded, an embedded .xopp gunzipped).
std::string readable(const fs::path& file) {
    const std::string bytes = readFile(file);
    std::string all = bytes + gunzip(bytes);
    if (bytes.rfind("XQPK", 0) == 0 && bytes.size() > 6) {
        const QByteArray body = QByteArray::fromStdString(bytes.substr(6));
        all += (bytes[5] & 1) ? qUncompress(body).toStdString() : body.toStdString();
    }
    if (bytes.rfind("%PDF", 0) == 0) {
        try {
            QPDF q;
            q.setSuppressWarnings(true);
            q.processFile(file.string().c_str());
            for (QPDFObjectHandle o: q.getAllObjects()) {
                all += o.isStream() ? o.getDict().unparse() : o.unparseResolved();
                if (o.isStream()) {
                    try {
                        auto b = o.getStreamData(qpdf_dl_all);
                        const std::string data(reinterpret_cast<const char*>(b->getBuffer()), b->getSize());
                        all += data + gunzip(data);
                    } catch (const std::exception&) {
                    }
                }
            }
        } catch (const std::exception&) {
        }
    }
    return all;
}

/// The files under these folders in which `marker` can be read without a password
std::vector<fs::path> leaks(const std::vector<fs::path>& dirs, const std::string& marker) {
    std::vector<fs::path> out;
    for (const fs::path& dir: dirs) {
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (it->is_regular_file() && readable(it->path()).find(marker) != std::string::npos) {
                out.push_back(it->path());
            }
        }
    }
    return out;
}

/// The files in a folder and below
size_t filesIn(const fs::path& dir) {
    size_t n = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        n += it->is_regular_file() ? 1 : 0;
    }
    return n;
}

/// The pictures of a document's pages stored on disk (PageSketches: its folders are named after its path first)
size_t storedPagesOf(const fs::path& cache, const fs::path& file) {
    const std::string prefix =
            QCryptographicHash::hash(QByteArray::fromStdString(file.lexically_normal().string()), QCryptographicHash::Sha1)
                    .toHex()
                    .left(12)
                    .toStdString() +
            "-";
    size_t n = 0;
    std::error_code ec;
    for (auto it = fs::directory_iterator(cache / "pages", ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (it->path().filename().string().rfind(prefix, 0) == 0) {
            n += filesIn(it->path());
        }
    }
    return n;
}

struct FakeApps: xqt::SystemApps {
    QStringList shared;
    bool share(const QStringList& files) override {
        shared << files;
        return true;
    }
};

class PdfPasswordTest: public xqt::test::UiFixture {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        dir = fs::path(tmp.path().toStdString());
        xqt::SystemApps::setInstance(&apps);
        makeController();
        untilMs = 10000;
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        QMetaObject::invokeMethod(controller->settingsModel(), "resetLayoutChoices");
        ASSERT_NO_FATAL_FAILURE(loadWindow());
        wait(100);
    }
    void TearDown() override {
        closeApp();
        xqt::SystemApps::setInstance(nullptr);
    }
    xqt::DocumentSession* session() const { return controller->tabManager().currentSession(); }
    int tabCount() const { return controller->tabManager().count(); }
    void waitSaved() { until([&] { return !controller->anySaving(); }, 20000); }
    QTemporaryDir tmp;
    fs::path dir;
    FakeApps apps;
};
}  // namespace

// A protected PDF: the dialog asks for its password; a wrong one is said plainly and asked again; the right one opens
// it. Cancel leaves another one closed.
TEST_F(PdfPasswordTest, openingAProtectedPdfAsksForItsPassword) {
    makePdf(dir / "plain.pdf");
    protect(dir / "plain.pdf", dir / "locked.pdf", "open sesame");
    const int before = tabCount();
    EXPECT_FALSE(controller->openPath(QString::fromStdString((dir / "locked.pdf").string())));
    QObject* dialog = find("pdfPasswordDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitOpened(dialog, true));
    EXPECT_EQ(tabCount(), before) << "not opened yet";
    auto* text = findItem("pdfPasswordText");
    ASSERT_NE(text, nullptr);
    EXPECT_TRUE(text->property("text").toString().contains("locked.pdf"));
    auto* wrong = findItem("pdfPasswordWrong");
    ASSERT_NE(wrong, nullptr);
    EXPECT_FALSE(wrong->isVisible());
    auto* field = findItem("pdfPasswordField");
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->property("echoMode").toInt(), 2) << "the password is not shown";

    field->setProperty("text", "wrong");
    click(findItem("pdfPasswordOpen"));
    until([&] { return wrong->isVisible(); });
    EXPECT_TRUE(dialog->property("visible").toBool()) << "asked again";
    EXPECT_TRUE(wrong->isVisible()) << "a wrong password is said so";
    EXPECT_EQ(field->property("text").toString(), "") << "not kept in the window";
    EXPECT_EQ(tabCount(), before);

    field->setProperty("text", "open sesame");
    click(findItem("pdfPasswordOpen"));
    ASSERT_TRUE(waitOpened(dialog, false));
    ASSERT_EQ(tabCount(), before + 1);
    EXPECT_EQ(session()->documentFile(), dir / "locked.pdf");
    EXPECT_TRUE(controller->protectedDocument());
    EXPECT_EQ(session()->getDocument()->getPageCount(), 2u);

    // Cancel: not opened
    protect(dir / "plain.pdf", dir / "other.pdf", "another");
    EXPECT_FALSE(controller->openPath(QString::fromStdString((dir / "other.pdf").string())));
    ASSERT_TRUE(waitOpened(dialog, true));
    EXPECT_FALSE(wrong->isVisible());
    click(findItem("pdfPasswordCancel"));
    ASSERT_TRUE(waitOpened(dialog, false));
    EXPECT_EQ(tabCount(), before + 1);
    EXPECT_FALSE(xqt::PdfEncryption::isKnown(dir / "other.pdf"));
}

// ⋮ → Document → Protect with a password…: the PDF (with the unsaved notes) needs the password from then on; the
// document stays open, protected. Then Change or remove the password… → Remove: an ordinary PDF again.
TEST_F(PdfPasswordTest, protectAndRemoveThePasswordFromTheMenu) {
    makePdf(dir / "notes.pdf");
    ASSERT_TRUE(controller->openPath(QString::fromStdString((dir / "notes.pdf").string())));
    drawStroke(*session(), 0, 200);
    EXPECT_TRUE(controller->canProtect());
    EXPECT_FALSE(controller->protectedDocument());
    QObject* item = find("protectDocumentItem");
    ASSERT_NE(item, nullptr);
    QMetaObject::invokeMethod(item, "triggered");
    QObject* dialog = find("protectDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitOpened(dialog, true));
    auto* password = findItem("protectPassword");
    auto* confirm = findItem("protectConfirm");
    auto* accept = findItem("protectAccept");
    ASSERT_TRUE(password && confirm && accept);
    password->setProperty("text", "kept secret");
    confirm->setProperty("text", "kept secrex");
    wait(50);
    EXPECT_FALSE(accept->property("enabled").toBool()) << "the two differ";
    EXPECT_TRUE(findItem("protectProblem")->isVisible());
    confirm->setProperty("text", "kept secret");
    wait(50);
    EXPECT_TRUE(accept->property("enabled").toBool());
    click(accept);
    ASSERT_TRUE(waitOpened(dialog, false));
    waitSaved();
    until([&] { return controller->protectedDocument(); });
    EXPECT_TRUE(controller->protectedDocument());
    EXPECT_FALSE(session()->isModified());
    const auto st = xqt::PdfEncryption::probe(dir / "notes.pdf");
    EXPECT_TRUE(st.needsPassword);
    EXPECT_TRUE(xqt::PdfEncryption::probe(dir / "notes.pdf", "kept secret").readable);
    auto reopened = xqt::DocumentSession::loadFile(dir / "notes.pdf", false, "kept secret");
    ASSERT_TRUE(reopened.document) << reopened.error;
    size_t strokes = 0;
    for (const Layer* l: reopened.document->getPage(0)->getLayers()) {
        strokes += l->getElementsView().size();
    }
    EXPECT_EQ(strokes, 1u) << "the unsaved stroke went into the protected PDF";
    EXPECT_FALSE(find("protectDocumentItem")->property("offered").toBool());
    QObject* change = find("changePasswordItem");
    ASSERT_NE(change, nullptr);
    EXPECT_TRUE(change->property("offered").toBool());

    QMetaObject::invokeMethod(change, "triggered");
    ASSERT_TRUE(waitOpened(dialog, true));
    auto* remove = findItem("protectRemove");
    ASSERT_NE(remove, nullptr);
    EXPECT_TRUE(remove->isVisible());
    click(remove);
    ASSERT_TRUE(waitOpened(dialog, false));
    until([&] { return !controller->protectedDocument(); });
    EXPECT_FALSE(xqt::PdfEncryption::probe(dir / "notes.pdf").encrypted);
}

// Share → "Protect with a password": the PDF with notes goes as a copy that needs the password
TEST_F(PdfPasswordTest, shareProtectsACopy) {
    makePdf(dir / "lecture.pdf");
    ASSERT_TRUE(controller->openPath(QString::fromStdString((dir / "lecture.pdf").string())));
    drawStroke(*session(), 1, 300);
    QObject* dialog = find("shareDialog");
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QString()));
    ASSERT_TRUE(waitOpened(dialog, true));
    auto* box = findItem("shareProtect");
    ASSERT_NE(box, nullptr);
    EXPECT_TRUE(box->isVisible());
    click(box);
    auto* field = findItem("sharePassword");
    ASSERT_NE(field, nullptr);
    until([&] { return field->isVisible(); });
    auto* choice = findItem("sharePdfChoice");
    ASSERT_NE(choice, nullptr);
    EXPECT_FALSE(choice->property("enabled").toBool()) << "a password first";
    field->setProperty("text", "for the class");
    wait(50);
    click(choice);
    waitSaved();
    until([&] { return !apps.shared.isEmpty(); });
    ASSERT_EQ(apps.shared.size(), 1);
    const fs::path copy(apps.shared.front().toStdString());
    EXPECT_TRUE(xqt::PdfEncryption::probe(copy).needsPassword);
    EXPECT_TRUE(xqt::PdfEncryption::probe(copy, "for the class").readable);
    EXPECT_FALSE(xqt::PdfEncryption::probe(dir / "lecture.pdf").encrypted) << "the document's own file is not";
    EXPECT_TRUE(session()->isModified()) << "the document keeps its unsaved changes";
}

// A document of the library that was not protected (its text indexed, its card's picture and its pages' pictures
// stored, a clean copy, a copy shared), then protected: nothing of it stays readable in the caches, and no picture of
// its pages is kept
TEST_F(PdfPasswordTest, protectingRemovesEverythingTheCachesKeptOfIt) {
    const std::string marker = "cachemarkerK4W";
    const fs::path lib = dir / "lib";
    fs::create_directories(lib);
    const fs::path pdf = lib / "lecture.pdf";
    makePdf(pdf);
    controller->setLibraryRoot(lib);
    auto* library = qobject_cast<xqt::LibraryModel*>(controller->libraryModel());
    ASSERT_NE(library, nullptr);
    ASSERT_TRUE(controller->openPath(QString::fromStdString(pdf.string())));
    addText(*session(), 0, marker);
    ASSERT_TRUE(controller->saveAsHybrid(QUrl::fromLocalFile(QString::fromStdString(pdf.string()))));
    waitSaved();
    drawStroke(*session(), 1, 300);  // (a second version of the file)
    ASSERT_TRUE(controller->saveInBackground());
    waitSaved();
    // What the caches keep of it while it has no password
    const fs::path cache = fs::path(qEnvironmentVariable("XDG_CACHE_HOME").toStdString()) / "xournal-qt";
    const xqt::DocumentItem item = xqt::DocumentFiles::itemOf(pdf);
    library->refresh();
    xqt::LibraryIndex* index = library->searchIndex();
    ASSERT_NE(index, nullptr);
    until([&] { return !index->search(QString::fromStdString(marker)).empty(); }, 20000);
    ASSERT_FALSE(index->search(QString::fromStdString(marker)).empty()) << "indexed";
    index->flush();
    EXPECT_FALSE(xqt::PreviewCache::preview(item).isNull());
    xqt::PreviewCache::flush();
    until([&] { return storedPagesOf(cache, pdf) > 0; }, 20000);
    EXPECT_GT(storedPagesOf(cache, pdf), 0u) << "pictures of its pages stored";
    ASSERT_TRUE(controller->sharePdfCopy(QUrl(), false));
    waitSaved();
    until([&] { return !apps.shared.isEmpty(); });
    const fs::path packs = index->location().dirOf(lib);  // (the library's packs of that folder)
    const std::vector<fs::path> dirs{cache, packs};
    ASSERT_FALSE(leaks(dirs, marker).empty()) << "the check finds what the caches keep";
    ASSERT_NE(readable(packs / "previews.pack").find("lecture.pdf"), std::string::npos) << "its card's picture";

    ASSERT_TRUE(controller->protectDocument("kept secret", "", true, true, true));
    waitSaved();
    until([&] { return controller->protectedDocument(); });
    ASSERT_TRUE(controller->protectedDocument());
    index->flush();
    xqt::PreviewCache::flush();
    const auto found = leaks(dirs, marker);
    EXPECT_TRUE(found.empty()) << "readable in " << (found.empty() ? std::string() : found.front().string());
    EXPECT_EQ(storedPagesOf(cache, pdf), 0u) << "no picture of its pages";
    EXPECT_TRUE(xqt::PreviewCache::stored(item).isNull()) << "no picture of its card";
    EXPECT_EQ(readable(packs / "previews.pack").find("lecture.pdf"), std::string::npos) << "not in the pack either";
    EXPECT_TRUE(index->lockedOf(pdf));
    EXPECT_TRUE(index->search(QString::fromStdString(marker)).empty());
}

// A start removes what a crashed process had taken out of protected PDFs (pictures, recordings): the app's
// constructor cleans up before anything is opened
TEST_F(PdfPasswordTest, aStartRemovesWhatACrashLeftUnpacked) {
    const fs::path cache = fs::path(qEnvironmentVariable("XDG_CACHE_HOME").toStdString()) / "xournal-qt";
    const fs::path entry = cache / "hybrid-pdf" / "0123456789abcdef-1-2";
    const fs::path work = cache / "md-assets" / "fedcba9876543210";
    for (const fs::path& d: {entry / "pictures", entry / "audio", work}) {
        fs::create_directories(d);
    }
    std::ofstream(entry / "pictures" / "photo.png") << "picture";
    std::ofstream(entry / "audio" / "memo.ogg") << "sound";
    std::ofstream(entry / "unpacked-4999997");
    std::ofstream(work / "photo.png") << "picture";
    std::ofstream(work / "unpacked-4999997");
    TearDown();  // (the app quits: a crash would leave the same)
    SetUp();     // (and starts again)
    EXPECT_FALSE(fs::exists(entry / "pictures"));
    EXPECT_FALSE(fs::exists(entry / "audio"));
    EXPECT_FALSE(fs::exists(work));
    fs::remove_all(entry);
}
