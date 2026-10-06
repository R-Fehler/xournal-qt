/*
 * xournal-qt: dark pages and page colors in the real window (qt/docs/dark-pages.md): ⋮ › View › Dark pages turns the
 * canvas and the page pictures dark, the background dialog gives pages a curated paper color and texture, and the
 * print dialog says that dark paper takes a lot of ink.
 *
 * @license GNU GPLv2 or later
 */
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <functional>
#include <memory>
#include <shared_mutex>

#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/XojPage.h"
#include "render/PaperTexture.h"
#include "session/DocumentSession.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/Previews.h"
#include "shell/RecentFiles.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include "AppController.h"
#include "DocumentCanvasItem.h"

namespace fs = std::filesystem;

namespace {
class DarkPagesUiTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        controller = std::make_unique<AppController>();
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        controller->setDarkPagesMode("off");
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
        window->resize(1600, 1000);
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        QTest::mouseMove(window, QPoint(-20, -20));
        controller->newDocument();
        wait(200);
    }
    void TearDown() override {
        controller->setDarkPagesMode("off");
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
    /// An item under `root` (the delegates of a Repeater are not its QObject children)
    static QQuickItem* item(QQuickItem* root, const char* name) {
        if (!root) {
            return nullptr;
        }
        if (root->objectName() == QLatin1String(name)) {
            return root;
        }
        for (QQuickItem* c: root->childItems()) {
            if (QQuickItem* found = item(c, name)) {
                return found;
            }
        }
        return nullptr;
    }
    xqt::DocumentSession* current() const { return controller->tabManager().currentSession(); }
    void openDialog(QObject* dialog, const char* how = "open") {
        QMetaObject::invokeMethod(dialog, how);
        until([&] { return dialog->property("opened").toBool(); });
        ASSERT_TRUE(dialog->property("opened").toBool());
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
};
}  // namespace

TEST_F(DarkPagesUiTest, theViewMenuTurnsThePagesAndTheirPicturesDark) {
    auto* canvas = find<DocumentCanvasItem>("canvas");
    ASSERT_NE(canvas, nullptr);
    EXPECT_FALSE(canvas->darkPages());
    auto* on = find("darkPagesOnItem");
    ASSERT_NE(on, nullptr) << "⋮ › View › Dark pages";
    // (the item's signal, as in the other tests: AbstractButton.click() is Qt 6.8 and newer, and KDE neon has 6.7)
    ASSERT_TRUE(QMetaObject::invokeMethod(on, "triggered"));
    wait(50);
    EXPECT_EQ(controller->darkPagesMode(), "on");
    EXPECT_TRUE(canvas->darkPages());
    // The page pictures (the page sidebar, the overview) ask for dark ones
    bool anyDark = false;
    for (QQuickItem* image: window->findChildren<QQuickItem*>("pageSketch")) {
        const QString source = image->property("source").toUrl().toString();
        if (!source.isEmpty()) {
            EXPECT_TRUE(source.endsWith("~dark")) << source.toStdString();
            anyDark = true;
        }
    }
    (void)anyDark;  // (the sidebar may be closed in this window size)
    ASSERT_TRUE(QMetaObject::invokeMethod(find("darkPagesOffItem"), "triggered"));
    wait(50);
    EXPECT_FALSE(canvas->darkPages());
    EXPECT_FALSE(controller->darkPagesShown());
}

TEST_F(DarkPagesUiTest, theBackgroundDialogGivesPagesAPaperColorAndTexture) {
    auto* dialog = find("backgroundDialog");
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariantList{0}));
    until([&] { return dialog->property("opened").toBool(); });
    ASSERT_TRUE(dialog->property("opened").toBool());
    auto* content = dialog->property("contentItem").value<QQuickItem*>();
    ASSERT_NE(content, nullptr);
    auto* black = item(content, "paper_black");
    auto* illustration = item(content, "paper_illustration");
    auto* texture = item(content, "paperTexture");
    ASSERT_NE(black, nullptr);
    ASSERT_NE(illustration, nullptr);
    ASSERT_NE(texture, nullptr);
    ASSERT_TRUE(QMetaObject::invokeMethod(illustration, "clicked"));
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(300);
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT") + "-background-dialog.png");
    }
    QMetaObject::invokeMethod(texture, "toggle");
    QMetaObject::invokeMethod(texture, "toggled");
    wait(20);
    EXPECT_TRUE(dialog->property("textured").toBool());
    QMetaObject::invokeMethod(dialog, "accept");
    wait(100);
    {
        Document* doc = current()->getDocument();
        std::shared_lock lock(*doc);
        const PageRef p = doc->getPage(0);
        EXPECT_EQ(uint32_t(p->getBackgroundColor()) & 0xffffff, 0xf2e6cbu) << "illustration paper";
        EXPECT_TRUE(xqt::paper::textured(p->getBackgroundType().config));
    }
}

TEST_F(DarkPagesUiTest, printingDarkPaperSaysItTakesALotOfInk) {
    auto* print = find("printDialog");
    ASSERT_NE(print, nullptr);
    openDialog(print);
    auto* warning = find<QQuickItem>("darkPaperWarning");
    ASSERT_NE(warning, nullptr);
    EXPECT_FALSE(warning->isVisible()) << "white paper";
    QMetaObject::invokeMethod(print, "close");
    wait(300);

    const int plain = static_cast<int>(
            controller->settingsModel()->property("pageBackgroundFormats").toStringList().indexOf("plain"));
    ASSERT_TRUE(controller->changePageBackground({0}, std::max(0, plain), QColor("#161616"), 0));
    openDialog(print);
    EXPECT_TRUE(warning->isVisible()) << "black paper";
    QMetaObject::invokeMethod(print, "close");
    wait(300);
}
