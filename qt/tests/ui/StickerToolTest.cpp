/*
 * xournal-qt: stickers in the real window (qt/docs/stickers.md): what is selected saved as a sticker (the selection's
 * pill and its dialog, into the library's Stickers folder or a folder of it, onto the clipboard too), and a sticker
 * pasted from the picker: at its size in the middle of the visible part of the page, selected, one undo step, smaller
 * only when it is larger than the page; not into a document opened for reading only.
 *
 * @license GNU GPLv2 or later
 */
#include <functional>
#include <memory>
#include <shared_mutex>

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
#include <cairo.h>
#include <gtest/gtest.h>

#include "control/tools/EditSelection.h"
#include "model/Document.h"
#include "model/Image.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "canvas/CanvasView.h"
#include "session/DocumentSession.h"
#include "session/StickerFile.h"
#include "session/StickyNote.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/Previews.h"
#include "shell/RecentFiles.h"
#include "shell/Stickers.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include "AppController.h"

namespace fs = std::filesystem;
using xoj::util::Rectangle;

namespace {
class StickerToolTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        library = root / "Library";
        fs::create_directories(library);
        xqt::stickers::setAppSet(root / "app-stickers");
        controller = std::make_unique<AppController>();
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        controller->setLibraryRoot(library);
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
        window->resize(1920, 1080);  // (the buttons in sight)
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        QTest::mouseMove(window, QPoint(-20, -20));
        QGuiApplication::clipboard()->clear();
        wait(100);
    }
    void TearDown() override {
        controller->shutdown();
        engine.reset();
        controller.reset();
        xqt::stickers::setAppSet({});
    }

    static void wait(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
    }
    void until(const std::function<bool()>& done, int ms = 5000) {
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
    /// The delegates of a view (they have no QObject parent: findChild does not see them)
    std::vector<QQuickItem*> delegates(const char* view) const {
        std::vector<QQuickItem*> items;
        auto* v = find<QQuickItem>(view);
        const int n = v ? v->property("count").toInt() : 0;
        for (int i = 0; i < n; ++i) {
            QQuickItem* it = nullptr;
            QMetaObject::invokeMethod(v, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, it), Q_ARG(int, i));
            if (it) {
                items.push_back(it);
            }
        }
        return items;
    }
    QQuickItem* stickerCard(const QString& name) const {
        for (QQuickItem* it: delegates("stickerGrid")) {
            if (it->objectName() == "stickerCard_" + name && it->isVisible()) {
                return it;
            }
        }
        return nullptr;
    }
    xqt::DocumentSession* current() const { return controller->tabManager().currentSession(); }
    xqt::CanvasView* view() const { return controller->tabManager().currentView(); }
    QString snackbar() const {
        auto* bar = find<QQuickItem>("snackbar");
        return bar && bar->isVisible() ? find<QObject>("snackbarText")->property("text").toString() : QString();
    }

    /// A new document with a thick line on page 1 from (100, 200) to (300, 200)
    void makeSource() {
        controller->newDocument();
        PageRef page = current()->getDocument()->getPage(0);
        auto stroke = std::make_unique<Stroke>();
        stroke->setWidth(12);
        stroke->setColor(Colors::black);
        stroke->addPoint(Point(100, 200, -1));
        stroke->addPoint(Point(300, 200, -1));
        {
            std::unique_lock lock(*current()->getDocument());
            page->getSelectedLayer()->addElement(std::move(stroke));
        }
        page->firePageChanged();
        view()->getViewController().scrollToPageRect(0, QRectF(0, 0, 400, 400));
        wait(200);
    }
    /// A PDF of one A4 page with a red rectangle from (80, 150) to (320, 250), opened, with the thick line of
    /// makeSource on it
    void makePdfSource() {
        const fs::path pdf = root / "paper.pdf";
        cairo_surface_t* surface = cairo_pdf_surface_create(pdf.string().c_str(), 595, 842);
        cairo_t* cr = cairo_create(surface);
        cairo_set_source_rgb(cr, 1, 0, 0);
        cairo_rectangle(cr, 80, 150, 240, 100);
        cairo_fill(cr);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        ASSERT_TRUE(controller->openPath(QString::fromStdString(pdf.string())));
        until([&] { return current() && current()->getDocument()->getPageCount() == 1; });
        PageRef page = current()->getDocument()->getPage(0);
        auto stroke = std::make_unique<Stroke>();
        stroke->setWidth(12);
        stroke->setColor(Colors::black);
        stroke->addPoint(Point(100, 200, -1));
        stroke->addPoint(Point(300, 200, -1));
        {
            std::unique_lock lock(*current()->getDocument());
            page->getSelectedLayer()->addElement(std::move(stroke));
        }
        page->firePageChanged();
        view()->getViewController().scrollToPageRect(0, QRectF(0, 0, 400, 400));
        wait(300);
    }
    /// Window coordinates of a point of page 1 (points)
    QPoint onPage(double x, double y) const {
        auto* canvas = find<QQuickItem>("canvas");
        const QPointF at = view()->pageViewRect(0).topLeft() + QPointF(x, y) * view()->getViewController().zoom();
        return canvas->mapToScene(at).toPoint();
    }
    /// A mouse drag through these points of page 1
    void drag(const std::vector<QPointF>& points) {
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, onPage(points.front().x(), points.front().y()));
        for (size_t i = 1; i < points.size(); ++i) {
            const QPointF a = points[i - 1], b = points[i];
            for (int k = 1; k <= 6; ++k) {
                const QPointF p = a + (b - a) * k / 6.0;
                QTest::mouseMove(window, onPage(p.x(), p.y()));
                wait(5);
            }
        }
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, onPage(points.back().x(), points.back().y()));
        wait(100);
    }
    /// The picture of the background in a sticker's file (its "Sticker picture" layer): its pixels and where it lies
    static QImage pictureOf(const fs::path& file, Rectangle<double>* where = nullptr) {
        auto loaded = xqt::DocumentSession::loadFile(file);
        if (!loaded.document) {
            return {};
        }
        for (const Layer* l: loaded.document->getPage(0)->getLayers()) {
            if (l->getName() != xqt::stickers::PICTURE_LAYER || l->getElementsView().size() == 0) {
                continue;
            }
            const auto* image = static_cast<const Image*>(l->getElementsView().front());
            if (where) {
                *where = image->getBoundingBox();
            }
            const std::string& data = image->getBinaryData();
            return QImage::fromData(QByteArray(data.data(), static_cast<qsizetype>(data.size())), "PNG");
        }
        return {};
    }
    /// Save what is selected as a sticker, waiting for it
    QString save(const QString& name, const QString& folder = {}, bool picture = false, bool appWide = false) {
        QSignalSpy saved(controller.get(), &AppController::stickerSaved);
        EXPECT_TRUE(controller->saveSticker(name, folder, picture, appWide));
        until([&] { return saved.count() > 0; });
        EXPECT_EQ(saved.count(), 1);
        if (saved.isEmpty()) {
            return {};
        }
        EXPECT_EQ(saved.first().at(1).toString(), "") << saved.first().at(1).toString().toStdString();
        return saved.first().at(0).toString();
    }
    /// Paste a sticker, waiting for it
    bool paste(const QString& path) {
        QSignalSpy pasted(controller.get(), &AppController::stickerPasted);
        if (!controller->pasteSticker(path)) {
            return false;
        }
        until([&] { return pasted.count() > 0; });
        return !pasted.isEmpty() && pasted.first().at(1).toString().isEmpty();
    }
    /// The elements of the selected layer of page 1 and their bounds
    std::optional<Rectangle<double>> inkBounds(size_t* count = nullptr) const {
        Document* doc = current()->getDocument();
        std::shared_lock lock(*doc);
        std::optional<Rectangle<double>> b;
        size_t n = 0;
        for (const Layer* l: doc->getPage(0)->getLayers()) {
            for (const Element* e: l->getElementsView()) {
                ++n;
                const auto r = e->getBoundingBox();
                b ? b->unite(r) : void(b = r);
            }
        }
        if (count) {
            *count = n;
        }
        return b;
    }

    QTemporaryDir tmp;
    fs::path root, library;
    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
};
}  // namespace

TEST_F(StickerToolTest, theSelectionBecomesAStickerOfTheLibraryAndIsOnTheClipboard) {
    makeSource();
    controller->selectAllOnPage();
    ASSERT_TRUE(view()->getSelection());
    const QVariantMap draft = controller->stickerDraft();
    EXPECT_TRUE(draft.value("offered").toBool());
    EXPECT_TRUE(draft.value("name").toString().startsWith("Sticker ")) << "no text: the date";
    EXPECT_FALSE(draft.value("picture").toBool()) << "plain paper: no picture offered";

    const QString path = save("Line");
    const fs::path file = library / "Stickers" / "Line.xopp";
    EXPECT_EQ(path.toStdString(), file.string());
    ASSERT_TRUE(fs::exists(file));
    EXPECT_EQ(snackbar(), "Saved sticker “Line”");
    // On the clipboard as a copied selection
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    ASSERT_NE(mime, nullptr);
    EXPECT_TRUE(mime->hasFormat(xqt::sticky::GROUP_CLIPBOARD_MIME));
    // The file: the line at its size, on a page of its size plus the margin
    auto content = xqt::stickers::read(file);
    ASSERT_TRUE(content);
    ASSERT_EQ(content->elements.size(), 1u);
    EXPECT_NEAR(content->bounds.width, 200 + 12, 1);
    // The selection is still there, unchanged
    EXPECT_TRUE(view()->getSelection());
    // A second one of that name: "Line (2)"; one in a new folder
    EXPECT_EQ(save("Line"), QString::fromStdString((library / "Stickers" / "Line (2).xopp").string()));
    EXPECT_EQ(save("Line", "Lecture 3"), QString::fromStdString((library / "Stickers" / "Lecture 3" / "Line.xopp").string()));
    // In all libraries: the app-wide set
    EXPECT_EQ(save("Everywhere", {}, false, true),
              QString::fromStdString((xqt::stickers::appSet() / "Everywhere.xopp").string()));
}

TEST_F(StickerToolTest, theSelectionPillOpensTheDialog) {
    makeSource();
    controller->selectAllOnPage();
    auto* button = find<QQuickItem>("selectionSticker");
    ASSERT_NE(button, nullptr);
    until([&] { return button->isVisible(); });
    ASSERT_TRUE(button->isVisible());
    QMetaObject::invokeMethod(button, "clicked");
    auto* dialog = find<QObject>("stickerSaveDialog");
    ASSERT_NE(dialog, nullptr);
    until([&] { return dialog->property("opened").toBool(); });
    ASSERT_TRUE(dialog->property("opened").toBool());
    auto* name = find<QObject>("stickerNameField");
    ASSERT_NE(name, nullptr);
    EXPECT_TRUE(name->property("text").toString().startsWith("Sticker "));
    name->setProperty("text", "From the pill");
    auto* folder = find<QObject>("stickerFolderBox");
    ASSERT_NE(folder, nullptr);
    folder->setProperty("editText", "Topic");
    EXPECT_FALSE(find<QQuickItem>("stickerPictureBox")->isVisible());
    QSignalSpy saved(controller.get(), &AppController::stickerSaved);
    QMetaObject::invokeMethod(dialog, "accept");
    until([&] { return saved.count() > 0; });
    EXPECT_TRUE(fs::exists(library / "Stickers" / "Topic" / "From the pill.xopp"));
}

// --- the picture of the PDF behind it ---------------------------------------------------------------------------

TEST_F(StickerToolTest, withThePdfBehindItAPictureOfTheBackgroundOnly) {
    makePdfSource();
    controller->selectAllOnPage();
    ASSERT_TRUE(view()->getSelection());
    EXPECT_TRUE(controller->stickerDraft().value("picture").toBool()) << "a PDF page: the picture is offered";
    const QString path = save("On paper", {}, true);
    Rectangle<double> where;
    const QImage picture = pictureOf(path.toStdString(), &where);
    ASSERT_FALSE(picture.isNull()) << "a picture layer";
    // Where the content is (the margin around it on the sticker's page), at least 200 dpi
    EXPECT_NEAR(where.x, xqt::stickers::MARGIN, 0.5);
    EXPECT_NEAR(where.width, 212, 1);
    EXPECT_GE(picture.width(), static_cast<int>(212 * 200 / 72) - 2);
    // The PDF's red, and not the ink (the line is in the sticker itself)
    const QColor middle = picture.pixelColor(picture.width() / 2, picture.height() / 2);
    EXPECT_GT(middle.red(), 200) << "the PDF behind the line, not the line";
    EXPECT_LT(middle.green(), 60);
    EXPECT_EQ(middle.alpha(), 255);
    // Pasted back: the picture first (below the line)
    auto content = xqt::stickers::read(path.toStdString());
    ASSERT_TRUE(content);
    ASSERT_EQ(content->elements.size(), 2u);
    EXPECT_EQ(content->elements[0]->getType(), ELEMENT_IMAGE);
    EXPECT_EQ(content->elements[1]->getType(), ELEMENT_STROKE);
}

TEST_F(StickerToolTest, aLassoSelectionCutsThePictureToItsShape) {
    makePdfSource();
    controller->selectTool("selectRegion");
    // A diamond around the line: its corners lie outside it
    drag({{90, 200}, {200, 150}, {310, 200}, {200, 250}, {90, 200}});
    ASSERT_TRUE(view()->getSelection());
    const QString path = save("Diamond", {}, true);
    const QImage picture = pictureOf(path.toStdString());
    ASSERT_FALSE(picture.isNull());
    EXPECT_EQ(picture.pixelColor(picture.width() / 2, picture.height() / 2).alpha(), 255) << "inside the lasso";
    EXPECT_EQ(picture.pixelColor(1, 1).alpha(), 0) << "outside the lasso: transparent";
    // Moved since: the rectangle around the content (nothing transparent)
    drag({{200, 200}, {200, 400}});
    const QString moved = save("Moved", {}, true);
    const QImage whole = pictureOf(moved.toStdString());
    ASSERT_FALSE(whole.isNull());
    EXPECT_EQ(whole.pixelColor(1, 1).alpha(), 255);
}

// --- the picker and pasting ------------------------------------------------------------------------------------

TEST_F(StickerToolTest, aTapInThePickerPastesTheStickerSelectedInTheMiddleOfThePage) {
    makeSource();
    controller->selectAllOnPage();
    save("Line");
    controller->newDocument();
    view()->getViewController().scrollToPageRect(0, QRectF(0, 0, 595, 842));
    wait(200);
    controller->selectTool("pen");

    auto* button = find<QQuickItem>("stickerButton");
    ASSERT_NE(button, nullptr);
    EXPECT_TRUE(button->isVisible()) << "in the tool bar";
    QMetaObject::invokeMethod(button, "clicked");
    auto* picker = find<QObject>("stickerPicker");
    ASSERT_NE(picker, nullptr);
    until([&] { return picker->property("opened").toBool(); });
    QQuickItem* card = nullptr;
    until([&] { return (card = stickerCard("Line")) != nullptr; });
    ASSERT_NE(card, nullptr);
    QSignalSpy pasted(controller.get(), &AppController::stickerPasted);
    QMetaObject::invokeMethod(card, "clicked");
    until([&] { return pasted.count() > 0; });
    ASSERT_EQ(pasted.count(), 1);
    EXPECT_FALSE(picker->property("opened").toBool());

    // Selected (to move and resize), with a select tool
    EXPECT_TRUE(view()->getSelection());
    EXPECT_EQ(controller->tool(), "selectRect");
    view()->clearSelection();  // (a selection holds its elements out of their layer)
    size_t n = 0;
    const auto b = inkBounds(&n);
    ASSERT_TRUE(b);
    EXPECT_EQ(n, 1u);
    EXPECT_NEAR(b->width, 212, 1) << "its own size";
    // In the middle of the visible part of the page
    auto* canvas = find<QQuickItem>("canvas");
    const QRectF visible = view()->pageViewRect(0).intersected(QRectF(0, 0, canvas->width(), canvas->height()));
    const double zoom = view()->getViewController().zoom();
    const QPointF middle = (visible.center() - view()->pageViewRect(0).topLeft()) / zoom;
    EXPECT_NEAR(b->x + b->width / 2, middle.x(), 2);
    EXPECT_NEAR(b->y + b->height / 2, middle.y(), 2);
    // On the clipboard
    EXPECT_TRUE(QGuiApplication::clipboard()->mimeData()->hasFormat(xqt::sticky::GROUP_CLIPBOARD_MIME));
    // One undo step
    controller->undo();
    wait(50);
    inkBounds(&n);
    EXPECT_EQ(n, 0u);
}

TEST_F(StickerToolTest, aStickerLargerThanThePageIsMadeSmaller) {
    // A sticker 1000 points wide (an A4 page is 595)
    xqt::sticky::Group big;
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(4);
    stroke->addPoint(Point(0, 0, -1));
    stroke->addPoint(Point(1000, 100, -1));
    big.bounds = stroke->getBoundingBox();
    big.elements.push_back(std::move(stroke));
    big.markdown.push_back(false);
    auto doc = xqt::stickers::makeDocument(std::move(big), Colors::white);
    const fs::path file = library / "Stickers" / "Big.xopp";
    fs::create_directories(file.parent_path());
    ASSERT_TRUE(xqt::stickers::write(*doc, file));

    controller->newDocument();
    wait(200);
    ASSERT_TRUE(paste(QString::fromStdString(file.string())));
    view()->clearSelection();
    const auto b = inkBounds();
    ASSERT_TRUE(b);
    double pageWidth = 0;
    {
        std::shared_lock lock(*current()->getDocument());
        pageWidth = current()->getDocument()->getPage(0)->getWidth();
    }
    EXPECT_LE(b->width, pageWidth + 0.5);
    EXPECT_GE(b->width, pageWidth - 8) << "as large as fits";
    EXPECT_GE(b->x, -0.5);
}

TEST_F(StickerToolTest, notIntoADocumentForReadingOnly) {
    makeSource();
    controller->selectAllOnPage();
    const QString path = save("Line");
    view()->setReadingOnly(true);  // (a document opened for reading)
    EXPECT_FALSE(controller->canPasteSticker());
    EXPECT_FALSE(controller->pasteSticker(path));
}

// --- the card's menu ------------------------------------------------------------------------------------------------

TEST_F(StickerToolTest, theCardsMenuReordersAndDeletes) {
    makeSource();
    controller->selectAllOnPage();
    save("A");
    save("B");
    controller->newDocument();
    wait(100);
    QMetaObject::invokeMethod(find<QQuickItem>("stickerButton"), "clicked");
    auto* picker = find<QObject>("stickerPicker");
    until([&] { return picker->property("opened").toBool(); });
    QQuickItem* card = nullptr;
    auto* menu = find<QObject>("stickerCardMenu");
    ASSERT_NE(menu, nullptr);
    const auto trigger = [&](const char* entry) {
        // (the cards are made anew when the list changes)
        card = nullptr;
        until([&] { return (card = stickerCard("B")) != nullptr; });
        ASSERT_NE(card, nullptr);
        QMetaObject::invokeMethod(card, "pressAndHold");
        until([&] { return menu->property("visible").toBool(); });
        ASSERT_TRUE(menu->property("visible").toBool());
        QObject* item = nullptr;
        const int n = menu->property("count").toInt();
        for (int i = 0; i < n; ++i) {
            QQuickItem* it = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, it), Q_ARG(int, i));
            if (it && it->objectName() == entry) {
                item = it;
            }
        }
        ASSERT_NE(item, nullptr) << entry;
        QMetaObject::invokeMethod(item, "triggered");
        until([&] { return !menu->property("visible").toBool(); });
        wait(300);  // (the menu's closing transition)
    };
    // B before A: the own order, written into the folder's hidden file
    trigger("stickerMoveUp");
    auto* model = qobject_cast<xqt::StickersModel*>(controller->stickersModel());
    EXPECT_EQ(model->sort(), "own");
    EXPECT_EQ(model->pathAt(0).toStdString(), (library / "Stickers" / "B.xopp").string());
    EXPECT_TRUE(fs::exists(library / "Stickers" / ".sticker-order.json"));
    // Copied to all libraries
    trigger("stickerCopyOther");
    EXPECT_TRUE(fs::exists(xqt::stickers::appSet() / "B.xopp"));
    // Deleted
    trigger("stickerDelete");
    EXPECT_FALSE(fs::exists(library / "Stickers" / "B.xopp"));
    EXPECT_EQ(model->rowCount(), 1);
}

