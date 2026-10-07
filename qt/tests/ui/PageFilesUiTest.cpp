/*
 * xournal-qt: pages as files in the real window (qt/docs/features/page-files.md): the "Insert pages from a file" dialog
 * (pages ticked on their pictures, a range, a protected PDF's password), the page menus' entries, the extract, split
 * and picture dialogs, and "Copy page as image" (the menu entry and Ctrl+Shift+C: a high-resolution PNG on the
 * clipboard).
 *
 * @license GNU GPLv2 or later
 */
#include <functional>
#include <memory>

#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMimeData>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cairo-pdf.h>
#include <gtest/gtest.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include "model/Document.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/DocumentCovers.h"
#include "shell/RecentFiles.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include "AppController.h"
#include "UiFixture.h"
#include "support/TestSupport.h"

using xqt::test::makeTextPdf;

namespace fs = std::filesystem;

namespace {

class PageFilesUiTest: public xqt::test::UiFixture {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        makeTextPdf(root / "lecture.pdf", {"lectureone", "lecturetwo", "lecturethree"});
        makeController();
        untilMs = 15000;  // (extracting pages takes a while under load)
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        ASSERT_NO_FATAL_FAILURE(loadWindow({.size = QSize(1600, 1000)}));
        wait(100);
    }
    xqt::DocumentSession* current() const { return controller->tabManager().currentSession(); }
    size_t pageCount() const { return current()->getDocument()->getPageCount(); }
    QUrl url(const fs::path& p) const { return QUrl::fromLocalFile(QString::fromStdString(p.string())); }
    void openPageFiles(const char* what, const QVariantList& pages) {
        QMetaObject::invokeMethod(window->property("actions").value<QObject*>(), "openPageFiles", Q_ARG(QVariant, QString(what)), Q_ARG(QVariant, pages));
    }
    QObject* opened(const char* dialog) {
        auto* d = find<QObject>(dialog);
        EXPECT_NE(d, nullptr) << dialog;
        if (!d) {
            return nullptr;
        }
        until([&] { return d->property("opened").toBool(); });
        EXPECT_TRUE(d->property("opened").toBool()) << dialog;
        return d;
    }

    QTemporaryDir tmp;
    fs::path root;
};
}  // namespace

TEST_F(PageFilesUiTest, theInsertDialogInsertsThePagesTickedOnTheirPictures) {
    controller->newDocument();
    ASSERT_NE(find("addPageFromFileItem"), nullptr) << "in the add-page button's list";
    ASSERT_NE(find("insertFromFileItem"), nullptr) << "⋮ › Page";
    auto* files = find<QObject>("pageFiles");
    ASSERT_NE(files, nullptr);
    QMetaObject::invokeMethod(files, "openFile", Q_ARG(QVariant, url(root / "lecture.pdf")), Q_ARG(QVariant, 0),
                              Q_ARG(QVariant, true));
    QObject* dialog = opened("insertFromFileDialog");
    ASSERT_NE(dialog, nullptr);
    until([&] { return dialog->property("info").toMap().value("ok").toBool(); });
    ASSERT_EQ(dialog->property("pages").toInt(), 3);
    auto* grid = find<QObject>("insertFileGrid");
    ASSERT_NE(grid, nullptr);
    EXPECT_EQ(grid->property("count").toInt(), 3) << "a picture per page";
    EXPECT_TRUE(dialog->property("canInsert").toBool()) << "all pages at first";

    // A range that names no page of it: not offered
    find<QObject>("insertFileRange")->setProperty("text", "7");
    dialog->setProperty("mode", "range");
    EXPECT_FALSE(dialog->property("canInsert").toBool());
    EXPECT_FALSE(dialog->property("rangeError").toString().isEmpty());

    // Pages 3 and 1 ticked
    QMetaObject::invokeMethod(dialog, "toggle", Q_ARG(QVariant, 2));
    QMetaObject::invokeMethod(dialog, "toggle", Q_ARG(QVariant, 0));
    EXPECT_EQ(dialog->property("mode").toString(), "pick");
    EXPECT_TRUE(dialog->property("canInsert").toBool());
    QSignalSpy inserted(controller.get(), &AppController::pagesFromFileInserted);
    QMetaObject::invokeMethod(dialog, "insert");
    until([&] { return inserted.count() > 0; });
    ASSERT_EQ(inserted.count(), 1);
    ASSERT_EQ(pageCount(), 3u);
    current()->waitForSaves();
    EXPECT_FALSE(xqt::DocumentSearch::findOnPage(*current()->getDocument(), 1, "lectureone").empty());
    EXPECT_FALSE(xqt::DocumentSearch::findOnPage(*current()->getDocument(), 2, "lecturethree").empty());
    EXPECT_FALSE(dialog->property("opened").toBool());
}

TEST_F(PageFilesUiTest, aProtectedPdfAsksForItsPasswordInTheDialog) {
    {
        QPDF q;
        q.processFile((root / "lecture.pdf").string().c_str());
        const std::string wFile = (root / "locked.pdf").string();  // (QPDFWriter keeps the pointer)
        QPDFWriter w(q, wFile.c_str());
        w.setR6EncryptionParameters("sesame", "owner", true, true, true, true, true, true, qpdf_r3p_full, true);
        w.write();
    }
    controller->newDocument();
    QMetaObject::invokeMethod(find<QObject>("pageFiles"), "openFile", Q_ARG(QVariant, url(root / "locked.pdf")),
                              Q_ARG(QVariant, 0), Q_ARG(QVariant, true));
    QObject* dialog = opened("insertFromFileDialog");
    ASSERT_NE(dialog, nullptr);
    until([&] { return dialog->property("info").toMap().value("needsPassword").toBool(); });
    auto* password = find<QQuickItem>("insertFilePassword");
    ASSERT_NE(password, nullptr);
    EXPECT_TRUE(password->isVisible());
    password->setProperty("text", "sesame");
    QMetaObject::invokeMethod(dialog, "tryPassword");
    until([&] { return dialog->property("info").toMap().value("ok").toBool(); });
    EXPECT_EQ(dialog->property("pages").toInt(), 3);
    QSignalSpy inserted(controller.get(), &AppController::pagesFromFileInserted);
    QMetaObject::invokeMethod(dialog, "insert");
    until([&] { return inserted.count() > 0; });
    EXPECT_EQ(pageCount(), 4u);
}

TEST_F(PageFilesUiTest, thePageMenusOfferExtractSplitPicturesAndCopy) {
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "lecture.pdf").string())));
    for (const char* entry: {"pageMenuExtract", "pageMenuSplit", "pageMenuImages", "pageMenuInsertFile",
                             "pageMenuCopyImage", "pageGridFilesButton", "pageGridExtractItem", "pageGridSplitItem",
                             "pageGridImagesItem", "pageGridCopyImageItem", "extractPagesItem", "splitDocumentItem",
                             "copyPageImageItem", "exportImagesItem"}) {
        EXPECT_NE(find(entry), nullptr) << entry;
    }

    // Extract page 2, removed from the document: a new tab
    xqt::DocumentSession* source = current();
    openPageFiles("extract", {1});
    QObject* extract = opened("extractDialog");
    ASSERT_NE(extract, nullptr);
    EXPECT_EQ(find<QObject>("extractName")->property("text").toString(), "lecture (page 2)");
    EXPECT_TRUE(find<QObject>("extractAsPdf")->property("checked").toBool());
    find<QObject>("extractName")->setProperty("text", "Second");
    find<QObject>("extractRemove")->setProperty("checked", true);
    QSignalSpy extracted(controller.get(), &AppController::pagesExtracted);
    QMetaObject::invokeMethod(extract, "accept");
    until([&] { return extracted.count() > 0; });
    ASSERT_EQ(extracted.count(), 1);
    EXPECT_TRUE(fs::exists(root / "Second.pdf"));
    EXPECT_EQ(current()->getFilePath(), root / "Second.pdf");
    EXPECT_EQ(source->getDocument()->getPageCount(), 2u);

    // Split the source at its second page
    controller->tabManager().setCurrentIndex(controller->tabManager().indexOf(source));
    wait(50);
    openPageFiles("split", {1});
    QObject* split = opened("splitDialog");
    ASSERT_NE(split, nullptr);
    EXPECT_EQ(split->property("mode").toString(), "selected");
    EXPECT_EQ(split->property("plan").toMap().value("parts").toList().size(), 2);
    EXPECT_FALSE(find<QObject>("splitChaptersChoice")->property("enabled").toBool()) << "no chapters";
    extracted.clear();
    QMetaObject::invokeMethod(split, "accept");
    until([&] { return extracted.count() > 0; });
    ASSERT_EQ(extracted.count(), 1);
    EXPECT_EQ(extracted.first().at(0).toStringList().size(), 2);

    // Pictures of all pages into a folder
    openPageFiles("images", {});
    QObject* images = opened("imageExportDialog");
    ASSERT_NE(images, nullptr);
    images->setProperty("folder", url(root / "pictures"));
    images->setProperty("scope", "all");
    QSignalSpy exported(controller.get(), &AppController::pageImagesExported);
    QMetaObject::invokeMethod(images, "accept");
    until([&] { return exported.count() > 0; });
    ASSERT_EQ(exported.count(), 1);
    EXPECT_TRUE(fs::exists(root / "pictures" / "lecture-p001.png"));
    EXPECT_TRUE(fs::exists(root / "pictures" / "lecture-p002.png"));
}

TEST_F(PageFilesUiTest, copyPageAsImageFromTheMenuAndTheKeys) {
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "lecture.pdf").string())));
    controller->setPageImageDpi(150);
    QSignalSpy copied(controller.get(), &AppController::pageImageCopied);
    QMetaObject::invokeMethod(find<QObject>("copyPageImageItem"), "triggered");
    until([&] { return copied.count() > 0; });
    ASSERT_EQ(copied.count(), 1);
    EXPECT_EQ(copied.first().at(1).toSize(), QSize(1240, 1755)) << "595 × 842 pt at 150 dpi";
    const QImage png = QImage::fromData(QGuiApplication::clipboard()->mimeData()->data("image/png"), "PNG");
    EXPECT_EQ(png.size(), QSize(1240, 1755));

    // Ctrl+Shift+C
    QGuiApplication::clipboard()->clear();
    copied.clear();
    window->requestActivate();
    QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
    until([&] { return copied.count() > 0; });
    ASSERT_EQ(copied.count(), 1);
    EXPECT_FALSE(QGuiApplication::clipboard()->mimeData()->data("image/png").isEmpty());
    controller->setPageImageDpi(300);
}
