/*
 * xournal-qt: the snip tool in the real window (qt/docs/snip.md): from the select tools' list or the image button, a
 * rectangle or lasso dragged over a page puts its picture on the clipboard (PNG, with the fork's entry saying where it
 * came from), then the tool used before comes back. Pasted into a document, the picture has the size it had on its
 * page, and the window offers a link to that page next to it.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <functional>
#include <memory>
#include <shared_mutex>

#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

#include "control/tools/EditSelection.h"
#include "model/Document.h"
#include "model/Image.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "canvas/CanvasView.h"
#include "canvas/MarkdownEditor.h"
#include "canvas/Snip.h"
#include "markdown/MdBox.h"
#include "session/DocumentSession.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/Previews.h"
#include "shell/RecentFiles.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include "AppController.h"

namespace fs = std::filesystem;

namespace {
class SnipTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        controller = std::make_unique<AppController>();
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
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
        QTest::mouseMove(window, QPoint(-20, -20));  // (the pointer rests outside: nothing hovered, no tool tips)
        QGuiApplication::clipboard()->clear();
        wait(100);
    }
    void TearDown() override {
        controller->shutdown();
        engine.reset();
        controller.reset();
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
    static QObject* entryOf(QObject* menu, const char* name) {
        const int n = menu ? menu->property("count").toInt() : 0;
        for (int i = 0; i < n; ++i) {
            QQuickItem* it = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, it), Q_ARG(int, i));
            if (it && it->objectName() == name) {
                return it;
            }
        }
        return nullptr;
    }
    xqt::DocumentSession* current() const { return controller->tabManager().currentSession(); }
    xqt::CanvasView* view() const { return controller->tabManager().currentView(); }

    /// A new document with a thick black line across page 1 from (100, 200) to (300, 200); saved as `name` (under
    /// the temporary folder) unless it is empty.
    void makeSource(const std::string& name) {
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
        if (!name.empty()) {
            fs::create_directories((root / name).parent_path());
            ASSERT_TRUE(controller->saveAs(QUrl::fromLocalFile(QString::fromStdString((root / name).string()))));
        }
        view()->getViewController().scrollToPageRect(0, QRectF(0, 0, 400, 400));
        wait(200);
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
    }
    bool clipboardHasSnip() const {
        const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
        return mime && mime->hasFormat(xqt::snip::MIME);
    }
    QString snackbar() const {
        auto* bar = find<QQuickItem>("snackbar");
        return bar && bar->isVisible() ? find<QObject>("snackbarText")->property("text").toString() : QString();
    }
    /// The picture's pixel at a point of its page
    static QColor pixelAt(const QImage& image, const QRectF& area, double x, double y) {
        return image.pixelColor(static_cast<int>((x - area.x()) * image.width() / area.width()),
                                static_cast<int>((y - area.y()) * image.height() / area.height()));
    }
    /// A snip of the rectangle from (80, 150) to (320, 250) on page 1 of the current document
    void snipRectangle() {
        controller->startSnip("rect");
        drag({{80, 150}, {320, 250}});
        until([&] { return clipboardHasSnip(); });
        ASSERT_TRUE(clipboardHasSnip());
    }

    QTemporaryDir tmp;
    fs::path root;
    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
};
}  // namespace

TEST_F(SnipTest, aSnipFromTheSelectListCopiesThePictureWithItsSourceAndGivesTheToolBack) {
    makeSource("source.xopp");
    controller->selectTool("pen");
    auto* select = find<QQuickItem>("selectButton");
    auto* selects = find<QObject>("selectButtonVariants");
    ASSERT_NE(select, nullptr);
    ASSERT_NE(selects, nullptr);
    QMetaObject::invokeMethod(select, "pressAndHold");
    until([&] { return selects->property("visible").toBool(); });
    QObject* entry = entryOf(selects, "variant_snipRect");
    ASSERT_NE(entry, nullptr);
    QMetaObject::invokeMethod(entry, "triggered");
    until([&] { return !selects->property("visible").toBool(); });
    wait(300);  // (the menu's closing transition)
    EXPECT_EQ(controller->snipShape(), "rect");
    EXPECT_EQ(controller->tool(), "selectRect");
    EXPECT_TRUE(select->property("checked").toBool());
    EXPECT_EQ(select->property("currentKey").toString(), "snipRect");

    drag({{80, 150}, {320, 250}});
    until([&] { return clipboardHasSnip(); });
    ASSERT_TRUE(clipboardHasSnip());
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    EXPECT_TRUE(mime->hasImage());
    EXPECT_TRUE(mime->hasFormat("image/png"));
    const QImage png = QImage::fromData(mime->data("image/png"), "PNG");
    EXPECT_FALSE(png.isNull());

    const auto source = xqt::snip::decode(mime);
    ASSERT_TRUE(source);
    EXPECT_EQ(source->page, 0);
    EXPECT_EQ(source->title, "source, page 1");
    EXPECT_TRUE(source->link.startsWith(QString::fromStdString((root / "source.xopp").generic_string()) + "#page=1"))
            << source->link.toStdString();
    EXPECT_NEAR(source->area.x(), 80, 2);
    EXPECT_NEAR(source->area.y(), 150, 2);
    EXPECT_NEAR(source->area.width(), 240, 3);
    EXPECT_NEAR(source->area.height(), 100, 3);

    // The picture: at least 200 dpi, the line where it is on the page, the paper around it
    const QImage image = qvariant_cast<QImage>(mime->imageData());
    ASSERT_FALSE(image.isNull());
    EXPECT_GE(image.width(), static_cast<int>(source->area.width() * 200 / 72) - 1);
    EXPECT_EQ(image.size(), png.size());
    EXPECT_LT(pixelAt(image, source->area, 200, 200).lightness(), 30) << "the line";
    EXPECT_GT(pixelAt(image, source->area, 200, 170).lightness(), 240) << "the paper";
    EXPECT_EQ(pixelAt(image, source->area, 200, 170).alpha(), 255);

    // One snip: the tool before is back, and the window says so
    until([&] { return controller->tool() == "pen"; });
    EXPECT_EQ(controller->tool(), "pen");
    EXPECT_EQ(controller->snipShape(), "");
    EXPECT_EQ(snackbar(), "Copied picture");
    EXPECT_FALSE(view()->getSelection()) << "nothing selected";
}

TEST_F(SnipTest, theImageButtonSnipsWithTheLassoTransparentOutsideIt) {
    makeSource("source.xopp");
    controller->selectTool("eraser");
    auto* button = find<QQuickItem>("imageButton");
    auto* menu = find<QObject>("imageMenu");
    ASSERT_NE(button, nullptr);
    ASSERT_NE(menu, nullptr);
    QMetaObject::invokeMethod(button, "pressAndHold");
    until([&] { return menu->property("visible").toBool(); });
    ASSERT_NE(entryOf(menu, "imageFromFileItem"), nullptr);
    ASSERT_NE(entryOf(menu, "snipItem"), nullptr);
    QObject* lasso = entryOf(menu, "snipLassoItem");
    ASSERT_NE(lasso, nullptr);
    QMetaObject::invokeMethod(lasso, "triggered");
    until([&] { return !menu->property("visible").toBool(); });
    wait(300);  // (the menu's closing transition)
    EXPECT_EQ(controller->snipShape(), "lasso");
    EXPECT_EQ(controller->tool(), "selectRegion");

    // A triangle: its corner at the bottom right is outside
    drag({{80, 150}, {320, 150}, {80, 250}, {80, 150}});
    until([&] { return clipboardHasSnip(); });
    ASSERT_TRUE(clipboardHasSnip());
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    const auto source = xqt::snip::decode(mime);
    ASSERT_TRUE(source);
    const QImage image = qvariant_cast<QImage>(mime->imageData());
    EXPECT_EQ(pixelAt(image, source->area, 310, 245).alpha(), 0) << "outside the lasso";
    EXPECT_EQ(pixelAt(image, source->area, 110, 170).alpha(), 255) << "inside";
    EXPECT_LT(pixelAt(image, source->area, 120, 200).lightness(), 30) << "the line inside";
    EXPECT_EQ(pixelAt(image, source->area, 290, 200).alpha(), 0) << "the line outside";
    until([&] { return controller->tool() == "eraser"; });
    EXPECT_EQ(controller->tool(), "eraser");
}

TEST_F(SnipTest, anotherToolOrEscapeEndsTheSnip) {
    makeSource("");
    controller->selectTool("pen");
    controller->startSnip("rect");
    controller->selectTool("eraser");
    EXPECT_EQ(controller->snipShape(), "") << "another tool chosen: no snip, that tool stays";
    EXPECT_EQ(controller->tool(), "eraser");
    controller->startSnip("lasso");
    QTest::keyClick(window, Qt::Key_Escape);
    wait(50);
    EXPECT_EQ(controller->snipShape(), "");
    EXPECT_EQ(controller->tool(), "eraser") << "the tool before is back";
    // A tap is no snip: it stays armed
    controller->startSnip("rect");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, onPage(150, 150));
    wait(200);
    EXPECT_FALSE(clipboardHasSnip());
    EXPECT_EQ(controller->snipShape(), "rect");
    controller->cancelSnip();
}

TEST_F(SnipTest, aViewForReadingOnlySnipsToo) {
    makeSource("source.xopp");
    view()->setReadingOnly(true);
    controller->selectTool("pen");
    snipRectangle();
    EXPECT_EQ(controller->tool(), "pen");
    view()->setReadingOnly(false);
}

TEST_F(SnipTest, aPastedSnipHasItsSizeAndOffersALinkToItsPage) {
    makeSource("source.xopp");
    snipRectangle();
    const auto source = xqt::snip::decode(QGuiApplication::clipboard()->mimeData());
    ASSERT_TRUE(source);

    // Another document, in another folder
    controller->newDocument();
    fs::create_directories(root / "Notes");
    ASSERT_TRUE(controller->saveAs(QUrl::fromLocalFile(QString::fromStdString((root / "Notes" / "target.xopp").string()))));
    wait(100);
    QSignalSpy offered(controller.get(), &AppController::snipLinkOffered);
    ASSERT_TRUE(controller->pasteElements());
    EXPECT_EQ(offered.count(), 1);
    EditSelection* sel = view()->getSelection();
    ASSERT_NE(sel, nullptr) << "the picture, selected";
    ASSERT_EQ(sel->getElementsView().size(), 1u);
    const auto rect = sel->getElementsView().front()->getBoundingBox();
    EXPECT_NEAR(rect.width, source->area.width(), 2) << "the size it had on its page";
    EXPECT_NEAR(rect.height, source->area.height(), 2);
    EXPECT_EQ(snackbar(), "Add a link to the source page (source, page 1)?");
    auto* action = find<QQuickItem>("snackbarAction");
    ASSERT_NE(action, nullptr);
    EXPECT_TRUE(action->isVisible());
    EXPECT_EQ(action->property("text").toString(), "Add link");
    QMetaObject::invokeMethod(action, "clicked");
    wait(50);

    // A link marker under the picture, relative to this document, with the page
    PageRef page = current()->getDocument()->getPage(0);
    Layer* layer = xqt::md::markdownLayer(page);
    ASSERT_NE(layer, nullptr);
    const Text* marker = xqt::md::boxOf(*layer);
    ASSERT_NE(marker, nullptr);
    EXPECT_NE(marker->getText().find("](../source.xopp#page=1"), std::string::npos) << marker->getText();
    EXPECT_NE(marker->getText().find("source, page 1"), std::string::npos);
    const auto box = marker->getBoundingBox();
    EXPECT_GE(box.y, rect.y + rect.height - 1) << "under the picture";
    EXPECT_NEAR(box.x, rect.x, 2);
}

TEST_F(SnipTest, aSnipOfADocumentWithoutAFileOffersNoLink) {
    makeSource("");
    snipRectangle();
    const auto source = xqt::snip::decode(QGuiApplication::clipboard()->mimeData());
    ASSERT_TRUE(source);
    EXPECT_TRUE(source->link.isEmpty());
    QSignalSpy offered(controller.get(), &AppController::snipLinkOffered);
    ASSERT_TRUE(controller->pasteElements());
    EXPECT_EQ(offered.count(), 0);
    EXPECT_NE(view()->getSelection(), nullptr) << "the picture is pasted all the same";
}

TEST_F(SnipTest, inAMarkdownDocumentTheOfferIsAMarkdownLink) {
    makeSource("source.xopp");
    snipRectangle();
    {
        std::ofstream out(root / "note.md");
        out << "# Note\n\nText.\n";
    }
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "note.md").string())));
    wait(300);
    xqt::CanvasView* v = view();
    ASSERT_NE(v, nullptr);
    ASSERT_TRUE(v->ensureTextEditor());
    xqt::MarkdownEditor* editor = v->getMarkdownEditor();
    ASSERT_NE(editor, nullptr);
    QSignalSpy offered(controller.get(), &AppController::snipLinkOffered);
    QKeyEvent paste(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier, QStringLiteral("v"));
    bool finish = false;
    ASSERT_TRUE(v->textKeyPressed(&paste, finish));
    const std::string withPicture = editor->text();
    const size_t picture = withPicture.find("![](note.assets/");
    ASSERT_NE(picture, std::string::npos) << withPicture << " (the picture the Markdown way)";
    EXPECT_EQ(offered.count(), 1);
    ASSERT_TRUE(controller->addSnipLink());
    const std::string text = editor->text();
    const size_t link = text.find("[source, page 1](source.xopp#page=1");
    EXPECT_NE(link, std::string::npos) << text;
    EXPECT_GT(link, picture) << "after the picture";
}
