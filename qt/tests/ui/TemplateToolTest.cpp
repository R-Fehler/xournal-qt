/*
 * xournal-qt: page templates in the real window (qt/docs/features/templates.md): "Save page as template…" (⋮ › Page,
 * the dialog: into the library's Templates folder, with or without background and content), the add-page button's list
 * (the templates used last, all of them in the picker), the Insert pages dialog's "From a template", and a new
 * document that starts from a template.
 *
 * @license GNU GPLv2 or later
 */
#include <functional>
#include <memory>
#include <shared_mutex>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "canvas/CanvasView.h"
#include "session/DocumentSession.h"
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

namespace {
class TemplateToolTest: public xqt::test::UiFixture {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        library = root / "Library";
        fs::create_directories(library);
        xqt::stickers::setAppSet(root / "app-templates", xqt::stickers::Kind::Templates);
        makeController();
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        controller->setLibraryRoot(library);
        ASSERT_NO_FATAL_FAILURE(loadWindow({.size = QSize(1920, 1080)}));  // (the buttons in sight)
        wait(100);
    }
    void TearDown() override {
        closeApp();
        xqt::stickers::setAppSet({}, xqt::stickers::Kind::Templates);
    }

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
    xqt::DocumentSession* current() const { return controller->tabManager().currentSession(); }
    size_t pageCount() const { return current()->getDocument()->getPageCount(); }

    /// A new document of graph paper with a line on page 1
    void makeSource() {
        controller->newDocument();
        PageRef page = current()->getDocument()->getPage(0);
        auto stroke = std::make_unique<Stroke>();
        stroke->setWidth(4);
        stroke->setColor(Colors::black);
        stroke->addPoint(Point(100, 200, -1));
        stroke->addPoint(Point(300, 200, -1));
        {
            std::unique_lock lock(*current()->getDocument());
            page->getSelectedLayer()->addElement(std::move(stroke));
            page->setBackgroundType(PageType(PageTypeFormat::Graph));
        }
        page->firePageChanged();
        wait(100);
    }
    size_t strokesOn(size_t pageNo) const {
        Document* doc = current()->getDocument();
        std::shared_lock lock(*doc);
        size_t n = 0;
        for (const Layer* l: doc->getPage(pageNo)->getLayers()) {
            n += l->getElementsView().size();
        }
        return n;
    }
    QString saveTemplate(const QString& name, bool background = true, bool content = true) {
        QSignalSpy saved(controller.get(), &AppController::templateSaved);
        EXPECT_TRUE(controller->saveTemplate(0, name, {}, background, content, false));
        until([&] { return saved.count() > 0; });
        return saved.isEmpty() ? QString() : saved.first().at(0).toString();
    }

    QTemporaryDir tmp;
    fs::path root, library;
};
}  // namespace

TEST_F(TemplateToolTest, thePageMenusDialogSavesThePageIntoTheLibrarysTemplates) {
    makeSource();
    ASSERT_NE(find("saveTemplateItem"), nullptr) << "⋮ › Page";
    QMetaObject::invokeMethod(window->property("actions").value<QObject*>(), "openTemplateSave", Q_ARG(QVariant, 0));
    auto* dialog = find<QObject>("templateSaveDialog");
    ASSERT_NE(dialog, nullptr);
    until([&] { return dialog->property("opened").toBool(); });
    ASSERT_TRUE(dialog->property("opened").toBool());
    auto* name = find<QObject>("templateNameField");
    EXPECT_TRUE(name->property("text").toString().contains("page 1")) << name->property("text").toString().toStdString();
    name->setProperty("text", "Graph header");
    auto* background = find<QObject>("templateBackgroundBox");
    auto* content = find<QObject>("templateContentBox");
    EXPECT_TRUE(background->property("checked").toBool()) << "on by default";
    EXPECT_TRUE(content->property("checked").toBool()) << "on by default";
    background->setProperty("checked", false);
    content->setProperty("checked", false);
    EXPECT_FALSE(dialog->property("canSave").toBool()) << "nothing to save";
    content->setProperty("checked", true);
    EXPECT_TRUE(dialog->property("canSave").toBool());

    QSignalSpy saved(controller.get(), &AppController::templateSaved);
    QMetaObject::invokeMethod(dialog, "accept");
    until([&] { return saved.count() > 0; });
    ASSERT_EQ(saved.count(), 1);
    const fs::path file = library / "Templates" / "Graph header.xopp";
    EXPECT_EQ(saved.first().at(0).toString().toStdString(), file.string());
    auto loaded = xqt::DocumentSession::loadFile(file);
    ASSERT_TRUE(loaded.document);
    EXPECT_EQ(loaded.document->getPage(0)->getBackgroundType(), PageType(PageTypeFormat::Plain))
            << "without its background";
}

TEST_F(TemplateToolTest, theAddPageButtonsListOffersTheTemplatesUsedLast) {
    makeSource();
    const QString path = saveTemplate("Week");
    ASSERT_FALSE(path.isEmpty());
    QSignalSpy inserted(controller.get(), &AppController::templateInserted);
    ASSERT_TRUE(controller->insertTemplate(path));  // (used once: in the list now)
    until([&] { return inserted.count() > 0; });
    ASSERT_EQ(pageCount(), 2u);

    auto* button = find<QQuickItem>("addPageButton");
    ASSERT_NE(button, nullptr);
    QMetaObject::invokeMethod(button, "pressAndHold");
    auto* menu = find<QObject>("addPageMenu");
    ASSERT_NE(menu, nullptr);
    until([&] { return menu->property("opened").toBool(); });
    QObject* item = nullptr;
    until([&] { return (item = find<QObject>("addPageTemplate_Week")) != nullptr; });
    ASSERT_NE(item, nullptr) << "the template used last";
    controller->goToPage(0);
    QMetaObject::invokeMethod(item, "triggered");
    until([&] { return inserted.count() > 1; });
    ASSERT_EQ(pageCount(), 3u);
    EXPECT_EQ(current()->getCurrentPageNo(), 1u) << "after the current page";
    EXPECT_EQ(current()->getDocument()->getPage(1)->getBackgroundType(), PageType(PageTypeFormat::Graph));
    EXPECT_EQ(strokesOn(1), 1u);
    controller->undoPages();
    EXPECT_EQ(pageCount(), 2u) << "one undo step";

    // "From a template…": the picker, a tap on the card adds it
    ASSERT_NE(find("addPageFromTemplateItem"), nullptr);
    auto* picker = find<QObject>("templatePicker");
    ASSERT_NE(picker, nullptr);
    QMetaObject::invokeMethod(picker, "openToInsert", Q_ARG(QVariant, 0));
    until([&] { return picker->property("opened").toBool(); });
    QQuickItem* card = nullptr;
    until([&] {
        for (QQuickItem* it: delegates("templateGrid")) {
            if (it->objectName() == "templateCard_Week") {
                card = it;
            }
        }
        return card != nullptr;
    });
    ASSERT_NE(card, nullptr);
    QMetaObject::invokeMethod(card, "clicked");
    until([&] { return inserted.count() > 2; });
    ASSERT_EQ(pageCount(), 3u);
    EXPECT_EQ(strokesOn(0), 1u) << "before page 1";
    EXPECT_FALSE(picker->property("opened").toBool());
}

TEST_F(TemplateToolTest, theInsertPagesDialogAddsATemplateSeveralTimes) {
    makeSource();
    const QString path = saveTemplate("Lines", true, false);
    auto* dialog = find<QObject>("insertPagesDialog");
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(dialog, "openAt", Q_ARG(QVariant, 1));
    until([&] { return dialog->property("opened").toBool(); });
    ASSERT_NE(find("insertFromTemplate"), nullptr);
    dialog->setProperty("fromTemplate", true);
    dialog->setProperty("templatePath", path);
    find<QObject>("insertCount")->setProperty("value", 3);
    QSignalSpy inserted(controller.get(), &AppController::templateInserted);
    QMetaObject::invokeMethod(dialog, "insert");
    until([&] { return inserted.count() > 0; });
    ASSERT_EQ(pageCount(), 4u);
    for (size_t i: {1u, 2u, 3u}) {
        EXPECT_EQ(current()->getDocument()->getPage(i)->getBackgroundType(), PageType(PageTypeFormat::Graph));
        EXPECT_EQ(strokesOn(i), 0u) << "without its content";
    }
    controller->undoPages();
    EXPECT_EQ(pageCount(), 1u) << "one undo step";
}

TEST_F(TemplateToolTest, aNewDocumentFromATemplate) {
    makeSource();
    const QString path = saveTemplate("Start");
    controller->setHomeVisible(true);
    auto* dialog = find<QObject>("newDocumentDialog");
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(dialog, "open");
    until([&] { return dialog->property("opened").toBool(); });
    ASSERT_NE(find("newDocumentFromTemplate"), nullptr);
    find<QObject>("newDocumentName")->setProperty("text", "From start");
    dialog->setProperty("fromTemplate", true);
    dialog->setProperty("templatePath", path);
    const int tabs = controller->tabManager().count();
    QSignalSpy inserted(controller.get(), &AppController::templateInserted);
    QMetaObject::invokeMethod(dialog, "create");
    until([&] { return inserted.count() > 0; });
    ASSERT_EQ(controller->tabManager().count(), tabs + 1);
    ASSERT_EQ(pageCount(), 1u);
    EXPECT_EQ(current()->getDocument()->getPage(0)->getBackgroundType(), PageType(PageTypeFormat::Graph));
    EXPECT_EQ(strokesOn(0), 1u);
    EXPECT_EQ(current()->getFilePath(), library / "From start.xopp");
}
