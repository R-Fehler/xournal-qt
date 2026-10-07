/*
 * xournal-qt: version history in the real window (qt/docs/features/hybrid-pdf.md, "Version history"): the sidebar's
 * History button explains it and turns it on (off by default), ⋮ → Document → Version history… opens the same panel,
 * Ctrl+Alt+S saves with a message (a milestone), a version restored from its row's menu, and Share sends the PDF
 * without its versions unless chosen.
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <functional>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "session/PdfHistory.h"
#include "session/PdfRevisions.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/DocumentCovers.h"
#include "shell/RecentFiles.h"
#include "shell/SettingsModel.h"
#include "shell/SystemApps.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"
#include "shell/VersionsModel.h"
#include "shell/VersionCompare.h"
#include "shell/PagesModel.h"
#include "shell/ReferenceMode.h"
#include "CanvasView.h"
#include "ScrollLock.h"
#include "ViewController.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"

#include "AppController.h"
#include "UiFixture.h"

namespace fs = std::filesystem;

namespace {
void makeLecturePdf(const fs::path& file) {
    cairo_surface_t* s = cairo_pdf_surface_create(file.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    cairo_set_font_size(cr, 24);
    for (int i = 0; i < 2; ++i) {
        cairo_move_to(cr, 72, 100);
        cairo_show_text(cr, ("lecturepage" + std::to_string(i + 1)).c_str());
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
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

size_t strokesOf(Document& doc) {
    size_t n = 0;
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        for (const Layer* l: doc.getPage(i)->getLayers()) {
            for (auto it = l->getElementsView().begin(); it != l->getElementsView().end(); ++it) {
                ++n;
            }
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

class VersionHistoryTest: public xqt::test::UiFixture {
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
        xqt::PdfHistory::clock = nullptr;
        closeApp();
        xqt::SystemApps::setInstance(nullptr);
    }
    xqt::VersionsModel* versions() const { return qobject_cast<xqt::VersionsModel*>(controller->versionsModel()); }
    xqt::DocumentSession* session() const { return controller->tabManager().currentSession(); }
    void waitSaved() {
        until([&] { return !controller->anySaving(); }, 20000);
        until([&] { return !versions()->busy(); });
    }
    /// lecture.pdf, opened and saved as a PDF with notes without versions (as every PDF with notes is by default)
    fs::path openedNotes() {
        makeLecturePdf(dir / "lecture.pdf");
        EXPECT_TRUE(controller->openPath(QString::fromStdString((dir / "lecture.pdf").string())));
        drawStroke(*session(), 0, 100);
        const fs::path notes = dir / "lecture.notes.pdf";
        EXPECT_TRUE(controller->saveAsHybrid(QUrl::fromLocalFile(QString::fromStdString(notes.string()))));
        waitSaved();
        return notes;
    }
    QTemporaryDir tmp;
    fs::path dir;
    FakeApps apps;
};
}  // namespace

// Off by default, but found: the History button explains what it does and has the switch; turned on, the next save
// keeps the first versions
TEST_F(VersionHistoryTest, theHistoryButtonExplainsItAndTurnsItOn) {
    const fs::path notes = openedNotes();
    EXPECT_FALSE(xqt::HybridPdf::markerOf(notes).history) << "off by default";
    QObject* sidebar = find("sidebar");
    ASSERT_NE(sidebar, nullptr);
    click(findItem("sidebarHistoryButton"));
    EXPECT_EQ(sidebar->property("mode").toString(), "history");
    EXPECT_TRUE(versions()->active());
    auto* intro = findItem("historyIntro");
    ASSERT_NE(intro, nullptr);
    EXPECT_TRUE(intro->isVisible());
    auto* explanation = findItem("historyExplanation");
    ASSERT_NE(explanation, nullptr);
    EXPECT_TRUE(explanation->property("text").toString().contains("one for each day"));
    auto* sw = findItem("historySwitch");
    ASSERT_NE(sw, nullptr);
    EXPECT_FALSE(sw->property("checked").toBool());
    click(sw);
    EXPECT_TRUE(versions()->on());
    EXPECT_TRUE(versions()->pending());
    auto* pending = findItem("historyPending");
    ASSERT_NE(pending, nullptr);
    EXPECT_TRUE(pending->isVisible());
    click(findItem("historySaveNow"));
    waitSaved();
    EXPECT_FALSE(versions()->pending());
    EXPECT_TRUE(xqt::HybridPdf::markerOf(notes).history);
    auto* list = findItem("historyList");
    ASSERT_NE(list, nullptr);
    until([&] { return list->property("count").toInt() == 2; });
    EXPECT_EQ(list->property("count").toInt(), 2) << "the file as it was, and the version saved now";
    EXPECT_FALSE(intro->isVisible());
}

// ⋮ → Document → Version history… opens the panel; Ctrl+Alt+S saves with a message: a milestone at the top of the
// list (and the milestones filter shows it alone)
TEST_F(VersionHistoryTest, theMenuAndSaveWithAMessage) {
    const fs::path notes = openedNotes();
    QObject* item = find("versionHistoryItem");
    ASSERT_NE(item, nullptr);
    QMetaObject::invokeMethod(item, "triggered");
    wait(50);
    EXPECT_EQ(find("sidebar")->property("mode").toString(), "history");
    drawStroke(*session(), 1, 300);
    QTest::keyClick(window, Qt::Key_S, Qt::ControlModifier | Qt::AltModifier);
    QObject* dialog = find("versionMessageDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitOpened(dialog, true));
    auto* keep = findItem("versionMessageKeepVersions");
    ASSERT_NE(keep, nullptr);
    EXPECT_TRUE(keep->isVisible()) << "versions are not kept yet: offered";
    EXPECT_TRUE(keep->property("checked").toBool());
    auto* field = findItem("versionMessageField");
    ASSERT_NE(field, nullptr);
    field->setProperty("text", "Before the exam");
    click(findItem("versionMessageSave"));
    ASSERT_TRUE(waitOpened(dialog, false));
    waitSaved();
    const auto listed = xqt::PdfHistory::list(notes);
    ASSERT_EQ(listed.versions.size(), 2u);
    EXPECT_EQ(listed.versions.back().message, "Before the exam");
    auto* list = findItem("historyList");
    until([&] { return list && list->property("count").toInt() == 2; });
    auto* milestones = findItem("historyMilestonesOnly");
    ASSERT_NE(milestones, nullptr);
    milestones->setProperty("checked", true);
    QMetaObject::invokeMethod(milestones, "toggled");
    until([&] { return list->property("count").toInt() == 1; });
    EXPECT_EQ(list->property("count").toInt(), 1);
    EXPECT_EQ(versions()->data(versions()->index(0), xqt::VersionsModel::MessageRole).toString(), "Before the exam");
}

// A version restored from its row's menu: one undo step; the version's message changed from the menu
TEST_F(VersionHistoryTest, restoreAndMessageFromTheRowMenu) {
    const fs::path notes = openedNotes();
    session()->setKeepsVersions(true);
    drawStroke(*session(), 1, 300);
    ASSERT_TRUE(controller->saveWithMessage("Two strokes"));
    waitSaved();
    drawStroke(*session(), 1, 500);
    ASSERT_TRUE(controller->saveInBackground());
    waitSaved();
    ASSERT_EQ(strokesOf(*session()->getDocument()), 3u);
    click(findItem("sidebarHistoryButton"));
    auto* list = findItem("historyList");
    ASSERT_NE(list, nullptr);
    until([&] { return list->property("count").toInt() == 3; });
    ASSERT_EQ(list->property("count").toInt(), 3);
    // The row of version 0 (the oldest, one stroke): Restore…
    QQuickItem* oldest = nullptr;
    QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, oldest), Q_ARG(int, 2));
    ASSERT_NE(oldest, nullptr);
    click(oldest);
    QObject* menu = find("historyRowMenu");
    ASSERT_NE(menu, nullptr);
    QObject* restore = find("historyRestore");
    ASSERT_NE(restore, nullptr);
    QMetaObject::invokeMethod(restore, "triggered");
    QObject* confirm = find("historyRestoreDialog");
    ASSERT_TRUE(waitOpened(confirm, true));
    QSignalSpy restored(versions(), &xqt::VersionsModel::restored);
    click(findItem("historyRestoreConfirm"));
    until([&] { return restored.count() > 0; }, 20000);
    ASSERT_EQ(restored.count(), 1);
    EXPECT_TRUE(restored.first().at(0).toBool()) << restored.first().at(1).toString().toStdString();
    EXPECT_EQ(strokesOf(*session()->getDocument()), 1u);
    EXPECT_TRUE(controller->modified());
    controller->undo();
    EXPECT_EQ(strokesOf(*session()->getDocument()), 3u) << "one undo step";
    controller->redo();
    // A message for the version saved with none
    QQuickItem* newest = nullptr;
    QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, newest), Q_ARG(int, 1));
    ASSERT_NE(newest, nullptr);
    EXPECT_EQ(newest->property("kind").toString(), "version");
    const int id = newest->property("versionId").toInt();
    QObject* dialog = find("versionMessageDialog");
    QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant(id)));
    ASSERT_TRUE(waitOpened(dialog, true));
    findItem("versionMessageField")->setProperty("text", "Three strokes");
    click(findItem("versionMessageSave"));
    ASSERT_TRUE(waitOpened(dialog, false));
    const auto listed = xqt::PdfHistory::list(notes);
    ASSERT_EQ(listed.versions.size(), 3u);
    EXPECT_EQ(listed.versions.back().message, "Three strokes");
}

// Share sends the PDF without its versions (a copy written anew; the file keeps them), unless chosen
TEST_F(VersionHistoryTest, shareWithoutTheVersionsUnlessChosen) {
    const fs::path notes = openedNotes();
    session()->setKeepsVersions(true);
    drawStroke(*session(), 1, 300);
    ASSERT_TRUE(controller->saveInBackground());
    waitSaved();
    ASSERT_TRUE(controller->sharedKeepsVersions(QString()));
    const auto before = xqt::PdfHistory::list(notes).versions.size();
    QObject* dialog = find("shareDialog");
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant(QString())));
    ASSERT_TRUE(waitOpened(dialog, true));
    auto* withHistory = findItem("shareWithHistory");
    ASSERT_NE(withHistory, nullptr);
    EXPECT_TRUE(withHistory->isVisible());
    EXPECT_FALSE(withHistory->property("checked").toBool()) << "without, by default";
    click(findItem("sharePdfChoice"));
    until([&] { return apps.shared.size() == 1; }, 20000);
    ASSERT_EQ(apps.shared.size(), 1);
    const fs::path sent(apps.shared.first().toStdString());
    EXPECT_NE(sent, notes) << "a copy";
    EXPECT_FALSE(xqt::HybridPdf::markerOf(sent).history);
    EXPECT_EQ(xqt::PdfRevisions::read(sent).revisions.size(), 1u) << "no earlier revision goes along";
    EXPECT_EQ(xqt::PdfHistory::list(notes).versions.size(), before) << "the file keeps its versions";
    // With them: the file itself
    ASSERT_TRUE(controller->sharePdf(false, true));
    until([&] { return apps.shared.size() == 2; }, 20000);
    EXPECT_EQ(apps.shared.value(1).toStdString(), notes.string());
}

// Settings → Documents: "Keep versions of new PDFs with notes" (off); on, a PDF annotated and saved into itself keeps
// its versions from the first save, version 0 being the PDF as received
TEST_F(VersionHistoryTest, theSettingForNewPdfs) {
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    EXPECT_FALSE(settings->get("keepVersionsOfNewPdfs").toBool());
    settings->set("keepVersionsOfNewPdfs", true);
    makeLecturePdf(dir / "lecture.pdf");
    ASSERT_TRUE(controller->openPath(QString::fromStdString((dir / "lecture.pdf").string())));
    drawStroke(*session(), 0, 100);
    EXPECT_TRUE(versions()->on());
    ASSERT_TRUE(controller->saveAsHybrid(QUrl::fromLocalFile(QString::fromStdString((dir / "lecture.pdf").string()))));
    waitSaved();
    const auto listed = xqt::PdfHistory::list(dir / "lecture.pdf");
    ASSERT_EQ(listed.versions.size(), 2u);
    EXPECT_EQ(listed.versions[0].kind, xqt::PdfHistory::Kind::RECEIVED);
    settings->set("keepVersionsOfNewPdfs", false);
}

namespace {
/// The page at the point a locked view is kept by
size_t lockedPage(xqt::CanvasView* v) {
    const auto& vc = v->getViewController();
    return vc.placeAt(xqt::ScrollLock::anchorOf(vc))->page;
}
}  // namespace

// "Compare with now" in a row's menu: the version beside the document (read-only), both scrolled together, the pages
// that changed since marked in the page lists and counted in the comparison's bar, whose arrows go from change to
// change on both sides
TEST_F(VersionHistoryTest, compareWithNowMarksTheChangedPages) {
    const fs::path notes = openedNotes();  // (two pages, a stroke on the first)
    ASSERT_TRUE(controller->insertPages(2, 0, -1, false, 4));  // (six pages)
    session()->setKeepsVersions(true);
    ASSERT_TRUE(controller->saveInBackground());
    waitSaved();
    xqt::DocumentSession* now = session();
    ASSERT_GE(now->getDocument()->getPageCount(), 5u);
    drawStroke(*now, 3, 400);  // (not saved: "now" has it, the version not)
    click(findItem("sidebarHistoryButton"));
    auto* list = findItem("historyList");
    ASSERT_NE(list, nullptr);
    until([&] { return list->property("count").toInt() == 3; });
    ASSERT_EQ(list->property("count").toInt(), 3) << "unsaved changes, the version saved, the file as it was";
    QQuickItem* saved = nullptr;
    QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, saved), Q_ARG(int, 1));
    ASSERT_NE(saved, nullptr);
    click(saved);
    QObject* item = find("historyCompareNow");
    ASSERT_NE(item, nullptr);
    QMetaObject::invokeMethod(item, "triggered");
    QMetaObject::invokeMethod(find("historyRowMenu"), "close");
    ASSERT_TRUE(waitOpened(find("historyRowMenu"), false));

    auto& reference = controller->reference();
    auto& compare = controller->versionCompare();
    ASSERT_TRUE(reference.active());
    EXPECT_EQ(session(), now) << "the document stays in its tab";
    EXPECT_TRUE(reference.scrollLocked()) << "scrolled together";
    EXPECT_FALSE(reference.editable()) << "a version is read-only";
    EXPECT_FALSE(controller->viewingVersion());
    ASSERT_TRUE(compare.shown());
    until([&] { return !compare.busy() && compare.changeCount() > 0; });
    ASSERT_EQ(compare.changeCount(), 1);
    EXPECT_EQ(compare.summary(), "1 page changed");
    auto* pages = qobject_cast<QAbstractItemModel*>(controller->pagesModel());
    ASSERT_NE(pages, nullptr);
    EXPECT_FALSE(pages->data(pages->index(0, 0), xqt::PagesModel::DiffersRole).toBool());
    EXPECT_TRUE(pages->data(pages->index(3, 0), xqt::PagesModel::DiffersRole).toBool()) << "the page written on";
    auto* refPages = qobject_cast<QAbstractItemModel*>(reference.pagesModel());
    reference.setPagesShown(true);
    EXPECT_TRUE(refPages->data(refPages->index(3, 0), xqt::PagesModel::DiffersRole).toBool())
            << "marked on both sides";
    EXPECT_FALSE(refPages->data(refPages->index(2, 0), xqt::PagesModel::DiffersRole).toBool());
    reference.setPagesShown(false);
    auto* bar = findItem("compareBar");
    ASSERT_NE(bar, nullptr);
    EXPECT_TRUE(bar->isVisible());
    EXPECT_EQ(findItem("compareSummary")->property("text").toString(), "1 page changed");

    // The next change: page 4 on both sides
    xqt::CanvasView* main = controller->tabManager().currentView();
    xqt::CanvasView* version = reference.canvas();
    main->jumpToPage(0);
    wait(50);
    click(findItem("compareNextButton"));
    EXPECT_EQ(compare.currentChange(), 1);
    EXPECT_EQ(lockedPage(main), 3u);
    EXPECT_EQ(lockedPage(version), 3u);
    click(findItem("comparePreviousButton"));
    EXPECT_EQ(compare.currentChange(), 1) << "the first one stays";

    // Writing on: compared again
    drawStroke(*now, 0, 600);
    until([&] { return !compare.busy() && compare.changeCount() == 2; });
    EXPECT_EQ(compare.changeCount(), 2);
    EXPECT_TRUE(pages->data(pages->index(0, 0), xqt::PagesModel::DiffersRole).toBool());

    // Ended: no marks, no split
    click(findItem("compareCloseButton"));
    EXPECT_FALSE(compare.active());
    EXPECT_FALSE(reference.active());
    EXPECT_FALSE(pages->data(pages->index(3, 0), xqt::PagesModel::DiffersRole).toBool());
}

// "Compare with another version…" then a tap on the other one: the newer one read-only in a tab of its own, the older
// one beside it
TEST_F(VersionHistoryTest, compareTwoVersionsPickedInTheList) {
    const fs::path notes = openedNotes();
    session()->setKeepsVersions(true);
    ASSERT_TRUE(controller->saveWithMessage("First"));
    waitSaved();
    drawStroke(*session(), 1, 300);
    drawStroke(*session(), 0, 500);
    ASSERT_TRUE(controller->saveWithMessage("Second"));
    waitSaved();
    xqt::DocumentSession* document = session();
    click(findItem("sidebarHistoryButton"));
    auto* list = findItem("historyList");
    ASSERT_NE(list, nullptr);
    until([&] { return list->property("count").toInt() == 3; });
    ASSERT_EQ(list->property("count").toInt(), 3);
    QQuickItem* second = nullptr;
    QQuickItem* first = nullptr;
    QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, second), Q_ARG(int, 0));
    QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, first), Q_ARG(int, 1));
    ASSERT_NE(second, nullptr);
    ASSERT_NE(first, nullptr);
    click(second);
    QObject* item = find("historyCompareTwo");
    ASSERT_NE(item, nullptr);
    QMetaObject::invokeMethod(item, "triggered");
    QMetaObject::invokeMethod(find("historyRowMenu"), "close");
    ASSERT_TRUE(waitOpened(find("historyRowMenu"), false));
    auto* banner = findItem("historyPickBanner");
    ASSERT_NE(banner, nullptr);
    EXPECT_TRUE(banner->isVisible());
    click(first);
    wait(100);

    auto& compare = controller->versionCompare();
    ASSERT_TRUE(compare.shown()) << "active " << compare.active() << " reference " << controller->reference().active()
                                 << " tabs " << controller->tabCount() << " file "
                                 << session()->getFilePath().string();
    EXPECT_NE(session(), document) << "the newer version in a tab of its own";
    EXPECT_TRUE(controller->viewingVersion()) << "read-only";
    EXPECT_TRUE(session()->isReadOnly());
    EXPECT_TRUE(controller->reference().scrollLocked());
    until([&] { return !compare.busy() && compare.changeCount() > 0; });
    EXPECT_EQ(compare.changeCount(), 2) << "both pages were written on between the two";
    EXPECT_EQ(compare.olderTitle(), "First") << "a milestone by its message";
    EXPECT_EQ(compare.newerTitle(), "Second");
}
