/*
 * xournal-qt: stickers in the real window (qt/docs/features/stickers.md): what is selected saved as a sticker (the
 * selection's pill and its dialog, into the library's Stickers folder or a folder of it, onto the clipboard too), and a
 * sticker pasted from the picker: at its size in the middle of the visible part of the page, selected, one undo step,
 * smaller only when it is larger than the page; not into a document opened for reading only. The built-in collections
 * (BuiltinStickerToolTest): "Built in", found in English and German, pasted as one group, the card's menu only copying,
 * a collection hidden from its chip or in the settings and shown again, the pen's colour.
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

#include "control/Tool.h"
#include "control/ToolHandler.h"
#include "control/settings/Settings.h"
#include "control/tools/EditSelection.h"
#include "model/Document.h"
#include "model/Image.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "canvas/CanvasView.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/StickerFile.h"
#include "session/StickyNote.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/DocumentCovers.h"
#include "shell/RecentFiles.h"
#include "shell/Stickers.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include "AppController.h"
#include "UiFixture.h"

namespace fs = std::filesystem;
using xoj::util::Rectangle;

namespace {
class StickerToolTest: public xqt::test::UiFixture {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        library = root / "Library";
        fs::create_directories(library);
        xqt::stickers::setAppSet(root / "app-stickers");
        makeController();
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        controller->setLibraryRoot(library);
        ASSERT_NO_FATAL_FAILURE(loadWindow({.size = QSize(1920, 1080)}));  // (the buttons in sight)
        QGuiApplication::clipboard()->clear();
        wait(100);
    }
    void TearDown() override {
        closeApp();
        xqt::stickers::setAppSet({});
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

// --- in the library -------------------------------------------------------------------------------------------------

TEST_F(StickerToolTest, theLibrarysStickersFolderHasItsMark) {
    fs::create_directories(library / "Stickers");
    fs::create_directories(library / "Other");
    controller->setLibraryRoot(library);  // (read again)
    controller->setHomeVisible(true);
    const auto marked = [&] {
        std::vector<std::string> names;
        for (QQuickItem* card: delegates("libraryGrid")) {
            auto* m = card->findChild<QQuickItem*>("stickersMark");
            if (m && m->isVisible()) {
                names.push_back(card->property("name").toString().toStdString());
            }
        }
        return names;
    };
    until([&] { return !marked().empty(); });
    EXPECT_EQ(marked(), std::vector<std::string>{"Stickers"}) << "the Stickers folder's card, and only it";
}

// --- built in (the collections that come with the app) -------------------------------------------------------------

class BuiltinStickerToolTest: public StickerToolTest {
protected:
    /// The picker of the first sticker button, opened on "Built in"
    QObject* openBuiltin() {
        auto* button = find<QQuickItem>("stickerButton");
        picker = button->property("picker").value<QObject*>();
        EXPECT_NE(picker, nullptr);
        QMetaObject::invokeMethod(button, "clicked");
        until([&] { return picker->property("opened").toBool(); });
        auto* tab = in("stickerScopeBuiltin");
        EXPECT_NE(tab, nullptr);
        if (tab) {
            QMetaObject::invokeMethod(tab, "clicked");
        }
        until([&] { return model()->scope() == "builtin"; });
        return picker;
    }
    /// An object of the picker (a menu, a field) or an item in its view (a chip)
    QObject* in(const QString& name) const {
        if (auto* o = picker->findChild<QObject*>(name)) {
            return o;
        }
        std::function<QQuickItem*(QQuickItem*)> walk = [&](QQuickItem* item) -> QQuickItem* {
            if (!item) {
                return nullptr;
            }
            if (item->objectName() == name) {
                return item;
            }
            for (QQuickItem* child: item->childItems()) {
                if (auto* found = walk(child)) {
                    return found;
                }
            }
            return nullptr;
        };
        return walk(picker->property("contentItem").value<QQuickItem*>());
    }
    xqt::StickersModel* model() const { return qobject_cast<xqt::StickersModel*>(controller->stickersModel()); }
    void search(const QString& text) {
        auto* field = in("stickerSearch");
        ASSERT_NE(field, nullptr);
        field->setProperty("text", text);
        wait(50);
    }
    /// An entry of a menu, by its object name ("" when it is not offered)
    QQuickItem* menuEntry(QObject* menu, const QString& name) const {
        const int n = menu->property("count").toInt();
        for (int i = 0; i < n; ++i) {
            QQuickItem* it = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, it), Q_ARG(int, i));
            if (it && it->objectName() == name) {
                return it;
            }
        }
        return nullptr;
    }
    /// The groups of the elements of page 1 (the selection let go first)
    std::vector<int> groupsOnPage() const {
        std::vector<int> out;
        Document* doc = current()->getDocument();
        std::shared_lock lock(*doc);
        for (const Layer* l: doc->getPage(0)->getLayers()) {
            for (const Element* e: l->getElementsView()) {
                out.push_back(static_cast<int>(e->getGroup()));
            }
        }
        return out;
    }
    QObject* picker = nullptr;
};

// "Built in": the collections as chips, found by an English or a German name, a tap pastes it as one group
TEST_F(BuiltinStickerToolTest, theBuiltInScopeFindsInBothLanguagesAndPastesOneGroup) {
    controller->newDocument();
    wait(200);
    openBuiltin();
    EXPECT_NE(in("stickerFolderChip_circuits-iec"), nullptr);
    auto* solids = in("stickerFolderChip_solids");
    ASSERT_NE(solids, nullptr);
    EXPECT_EQ(solids->property("text").toString(), "3D solids");
    search("Widerstand");
    QQuickItem* card = nullptr;
    until([&] { return (card = stickerCard("Resistor")) != nullptr; });
    ASSERT_NE(card, nullptr) << "the German name finds it, shown by its English one";
    search("beaker");
    until([&] { return (card = stickerCard("Beaker")) != nullptr; });
    ASSERT_NE(card, nullptr);

    QSignalSpy pasted(controller.get(), &AppController::stickerPasted);
    QMetaObject::invokeMethod(card, "clicked");
    until([&] { return pasted.count() > 0; });
    ASSERT_EQ(pasted.count(), 1);
    EXPECT_EQ(pasted.first().at(1).toString(), "");
    EXPECT_TRUE(view()->getSelection());
    view()->clearSelection();
    const auto groups = groupsOnPage();
    ASSERT_GE(groups.size(), 2u);
    for (const int g: groups) {
        EXPECT_EQ(g, groups.front()) << "one group";
    }
    EXPECT_NE(groups.front(), 0);
    // Black, as drawn
    {
        std::shared_lock lock(*current()->getDocument());
        for (const Element* e: current()->getDocument()->getPage(0)->getLayers()[0]->getElementsView()) {
            EXPECT_EQ(e->getColor(), Colors::black);
        }
    }
}

// Read-only: the card's menu only copies it to the user's stickers
TEST_F(BuiltinStickerToolTest, theCardsMenuOnlyCopiesToMyStickers) {
    controller->newDocument();
    wait(100);
    openBuiltin();
    search("Resistor");
    QQuickItem* card = nullptr;
    until([&] { return (card = stickerCard("Resistor")) != nullptr; });
    ASSERT_NE(card, nullptr);
    auto* menu = in("stickerCardMenu");
    ASSERT_NE(menu, nullptr);
    QMetaObject::invokeMethod(card, "pressAndHold");
    until([&] { return menu->property("visible").toBool(); });
    for (const char* entry: {"stickerRename", "stickerMoveUp", "stickerMoveDown", "stickerMoveToFolder", "stickerOpen",
                             "stickerCopyOther", "stickerCopyToLibrary", "stickerDelete"}) {
        auto* it = menuEntry(menu, entry);
        ASSERT_NE(it, nullptr) << entry;
        EXPECT_FALSE(it->property("offered").toBool()) << entry;
    }
    auto* copy = menuEntry(menu, "stickerCopyToMine");
    ASSERT_NE(copy, nullptr);
    EXPECT_TRUE(copy->property("offered").toBool());
    QMetaObject::invokeMethod(copy, "triggered");
    until([&] { return fs::exists(library / "Stickers" / "Resistor.xopp"); });
    EXPECT_TRUE(fs::exists(library / "Stickers" / "Resistor.xopp")) << "a copy of the user's to change";
    until([&] { return !snackbar().isEmpty(); });
    EXPECT_EQ(snackbar(), "Copied to this library's stickers");
}

// A collection hidden from its chip's menu: not listed, not searched, kept in the settings; "Show" brings it back
TEST_F(BuiltinStickerToolTest, aCollectionHiddenFromItsChipAndShownAgain) {
    controller->newDocument();
    wait(100);
    openBuiltin();
    auto* chip = in("stickerFolderChip_solids");
    ASSERT_NE(chip, nullptr);
    auto* menu = in("stickerChipMenu");
    ASSERT_NE(menu, nullptr);
    QMetaObject::invokeMethod(chip, "pressAndHold");
    until([&] { return menu->property("visible").toBool(); });
    auto* hide = menuEntry(menu, "stickerHideCollection");
    ASSERT_NE(hide, nullptr);
    QMetaObject::invokeMethod(hide, "triggered");
    until([&] { return model()->hiddenCount() == 1; });
    EXPECT_FALSE(model()->folderList().contains("solids"));
    EXPECT_EQ(xqt::stickers::hiddenCollections(*controller->context().getSettings()), QStringList{"solids"});
    search("Cube");
    wait(100);
    EXPECT_EQ(model()->rowCount(), 0);
    search("");
    auto* note = qobject_cast<QQuickItem*>(in("stickerHiddenNote"));
    ASSERT_NE(note, nullptr);
    until([&] { return note->isVisible(); });
    EXPECT_EQ(note->property("text").toString(), "1 collection hidden");
    QMetaObject::invokeMethod(in("stickerShowHidden"), "clicked");
    until([&] { return model()->hiddenCount() == 0; });
    EXPECT_TRUE(model()->folderList().contains("solids"));
    EXPECT_TRUE(xqt::stickers::hiddenCollections(*controller->context().getSettings()).isEmpty());
}

// Settings → Documents → Built-in stickers: a switch per collection, "Restore hidden collections"
TEST_F(BuiltinStickerToolTest, hiddenAndRestoredInTheSettings) {
    QObject* sheet = find("settingsPage");
    key(Qt::Key_Comma, Qt::ControlModifier);
    ASSERT_TRUE(waitOpened(sheet, true));
    click(findItem("documentsTab"));
    auto* lab = findItem("stickerCollectionSwitch_lab");
    ASSERT_NE(lab, nullptr);
    until([&] { return lab->isVisible(); });
    EXPECT_TRUE(lab->property("checked").toBool());
    scrollIntoView(lab);
    click(lab);
    until([&] { return model()->hiddenCount() == 1; });
    EXPECT_EQ(xqt::stickers::hiddenCollections(*controller->context().getSettings()), QStringList{"lab"});
    auto* restore = findItem("stickerRestoreCollections");
    ASSERT_NE(restore, nullptr);
    until([&] { return restore->isVisible(); });
    scrollIntoView(restore);
    click(restore);
    until([&] { return model()->hiddenCount() == 0; });
    lab = findItem("stickerCollectionSwitch_lab");  // (the rows are made anew when the list changes)
    ASSERT_NE(lab, nullptr);
    EXPECT_TRUE(lab->property("checked").toBool());
    key(Qt::Key_Escape);
    ASSERT_TRUE(waitOpened(sheet, false));
}

// "In the pen's colour": a built-in sticker takes the pen's colour; the user's own stickers stay as they are
TEST_F(BuiltinStickerToolTest, inThePensColour) {
    controller->newDocument();
    wait(100);
    openBuiltin();
    auto* box = in("stickerPenColour");
    ASSERT_NE(box, nullptr);
    EXPECT_FALSE(box->property("checked").toBool()) << "black by default";
    box->setProperty("checked", true);
    QMetaObject::invokeMethod(box, "toggled");
    EXPECT_TRUE(model()->penColour());
    QMetaObject::invokeMethod(picker, "close");
    controller->context().getToolHandler()->getTool(TOOL_PEN).setColor(Color(0x2060c0U));
    ASSERT_TRUE(paste(QString::fromStdString(
            (xqt::stickers::builtinSet() / "circuits-iec" / "Resistor.xopp").string())));
    view()->clearSelection();
    std::shared_lock lock(*current()->getDocument());
    size_t n = 0;
    for (const Element* e: current()->getDocument()->getPage(0)->getLayers()[0]->getElementsView()) {
        EXPECT_EQ(e->getColor(), Color(0x2060c0U));
        ++n;
    }
    EXPECT_EQ(n, 3u);
}
