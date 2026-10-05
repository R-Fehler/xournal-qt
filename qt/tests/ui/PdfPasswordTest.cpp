/*
 * xournal-qt: encrypted PDFs in the real window (qt/docs/hybrid-pdf.md, "Encrypted PDFs"): opening a protected PDF
 * asks for its password (a wrong one is said so and asked again, Cancel leaves it closed), ⋮ → Document protects the
 * document's PDF and changes or removes its password, and Share protects a copy.
 *
 * @license GNU GPLv2 or later
 */
#include <functional>

#include <QCoreApplication>
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
#include <qpdf/QPDFWriter.hh>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/PdfEncryption.h"
#include "shell/HitPages.h"
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

struct FakeApps: xqt::SystemApps {
    QStringList shared;
    bool share(const QStringList& files) override {
        shared << files;
        return true;
    }
};

class PdfPasswordTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        dir = fs::path(tmp.path().toStdString());
        xqt::SystemApps::setInstance(&apps);
        controller = std::make_unique<AppController>();
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        QMetaObject::invokeMethod(controller->settingsModel(), "resetLayoutChoices");
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->addImageProvider("thumbnail", new xqt::ThumbnailProvider);
        engine->addImageProvider("sketch", new xqt::SketchProvider);
        engine->addImageProvider("preview", new xqt::PreviewProvider);
        engine->addImageProvider("hitpage", new xqt::HitPageProvider);
        engine->addImageProvider("mdsnippet", new xqt::MdSnippetProvider);
        engine->rootContext()->setContextProperty("app", controller.get());
        engine->loadFromModule("XournalQt", "Main");
        ASSERT_FALSE(engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
        ASSERT_NE(window, nullptr);
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        QTest::mouseMove(window, QPoint(-20, -20));
        wait(100);
    }
    void TearDown() override {
        controller->shutdown();
        engine.reset();
        controller.reset();
        xqt::SystemApps::setInstance(nullptr);
    }
    static void wait(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
    }
    void until(const std::function<bool()>& done, int ms = 10000) {
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < ms) {
            wait(20);
        }
    }
    template <typename T = QObject>
    T* find(const char* name) const {
        return window->findChild<T*>(name);
    }
    QQuickItem* findItem(const char* name) const {
        std::function<QQuickItem*(QQuickItem*)> walk = [&](QQuickItem* i) -> QQuickItem* {
            if (i->objectName() == name) {
                return i;
            }
            for (QQuickItem* c: i->childItems()) {
                if (QQuickItem* f = walk(c)) {
                    return f;
                }
            }
            return nullptr;
        };
        return walk(window->contentItem());
    }
    void click(QQuickItem* item) {
        ASSERT_NE(item, nullptr);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        wait(50);
    }
    static bool waitOpened(QObject* popup, bool opened, int timeoutMs = 5000) {
        auto done = [&] {
            return popup->property("opened").toBool() == opened && popup->property("visible").toBool() == opened;
        };
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        return done();
    }
    xqt::DocumentSession* session() const { return controller->tabManager().currentSession(); }
    int tabCount() const { return controller->tabManager().count(); }
    void waitSaved() { until([&] { return !controller->anySaving(); }, 20000); }
    QTemporaryDir tmp;
    fs::path dir;
    FakeApps apps;
    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
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
