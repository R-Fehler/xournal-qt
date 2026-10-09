/*
 * xournal-qt: the operations layer (qt/src/ops, ADR 0008): operations checked against the principal (a refused one
 * changes nothing, asking happens once per class), transactions as one undo step undone last first, rollback on
 * failure, and the document operations themselves (shapes, groups, data, Markdown boxes, layers, pages).
 *
 * @license GNU GPLv2 or later
 */
#include <memory>
#include <shared_mutex>

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "undo/UndoRedoHandler.h"

#include "MdBox.h"
#include "Operations.h"
#include "Shapes.h"
#include "config-test.h"

using namespace xqt;
using namespace xqt::ops;

namespace {

/// A plugin's authority as a test sets it: allowed classes, classes that are asked (with the answer), the rest denied
class TestAuthority final: public Authority {
public:
    QStringList allowed;
    QStringList askable;
    bool answer = true;
    int asked = 0;
    Decision decide(const Principal&, const QString&, const QString& opClass) override {
        if (isFreeClass(opClass) || allowed.contains(opClass)) {
            return Decision::Allow;
        }
        return askable.contains(opClass) ? Decision::Ask : Decision::Deny;
    }
    bool ask(const Principal&, const QString&, const QString& opClass) override {
        ++asked;
        askable.removeAll(opClass);
        if (answer) {
            allowed << opClass;
        }
        return answer;
    }
};

QVariantMap stroke(double x0, double y0, double x1, double y1, const QString& color = "#ff0000") {
    return {{"type", "stroke"}, {"points", QVariantList{x0, y0, x1, y1}}, {"color", color}, {"width", 2}};
}

class OperationsTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        authority.allowed = {"edit", "pages"};
    }
    std::unique_ptr<Context> context(Authority& a) {
        return std::make_unique<Context>(ops, Principal::plugin("org.example.test", "Test plugin"), a, session.get());
    }
    size_t elements(size_t page = 0) {
        size_t n = 0;
        for (const Layer* l: session->getDocument()->getPage(page)->getLayers()) {
            n += l->getElementsView().size();
        }
        return n;
    }
    size_t layers() { return session->getDocument()->getPage(0)->getLayerCount(); }
    UndoRedoHandler& undo() { return *session->getUndoRedoHandler(); }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    Operations ops;
    TestAuthority authority;
};

}  // namespace

TEST_F(OperationsTest, aDeniedOperationIsRejectedWithoutSideEffects) {
    TestAuthority none;  // (no class allowed, none asked)
    auto c = context(none);
    try {
        c->apply("element.insert", {{"shapes", QVariantList{stroke(10, 10, 50, 50)}}});
        FAIL() << "inserted without permission";
    } catch (const Error& e) {
        EXPECT_EQ(e.kind, Error::Kind::Denied);
        EXPECT_EQ(e.name(), "PermissionError");
    }
    EXPECT_EQ(elements(), 0u);
    EXPECT_FALSE(undo().canUndo());
    // Reading needs no permission
    EXPECT_EQ(c->apply("document.read").toMap().value("pageCount").toInt(), 1);
    // An operation of a class the plugin may not: also its parts (a stroke inside an insert)
    none.allowed = {"pages"};
    EXPECT_THROW(c->apply("element.insert", {{"shapes", QVariantList{stroke(1, 1, 2, 2)}}}), Error);
    EXPECT_EQ(elements(), 0u);
}

TEST_F(OperationsTest, aClassIsAskedOnFirstUseAndTheAnswerKept) {
    TestAuthority a;
    a.askable = {"edit"};
    a.answer = false;
    auto c = context(a);
    EXPECT_THROW(c->apply("element.insert", {{"shapes", QVariantList{stroke(10, 10, 50, 50)}}}), Error);
    EXPECT_EQ(a.asked, 1);
    EXPECT_EQ(elements(), 0u);
    EXPECT_THROW(c->apply("element.insert", {{"shapes", QVariantList{stroke(10, 10, 50, 50)}}}), Error);
    EXPECT_EQ(a.asked, 1);  // (no is kept: not asked again)

    TestAuthority b;
    b.askable = {"edit"};
    auto d = context(b);
    d->apply("element.insert", {{"shapes", QVariantList{stroke(10, 10, 50, 50), stroke(20, 20, 60, 60)}}});
    d->apply("element.insert", {{"shapes", QVariantList{stroke(30, 10, 50, 50)}}});
    EXPECT_EQ(b.asked, 1);
    EXPECT_EQ(elements(), 3u);
}

TEST_F(OperationsTest, aTransactionIsOneUndoStepUndoneLastFirst) {
    auto c = context(authority);
    const size_t before = layers();
    c->begin("Plot");
    // A new layer (selected), then strokes into it: undone in reverse, or the strokes would be lost with it
    c->apply("layer.add", {{"name", "Plot"}});
    c->apply("element.insert", {{"shapes", QVariantList{stroke(10, 10, 50, 50), stroke(60, 10, 90, 50)}}});
    c->apply("background.set", {{"type", "graph"}});
    EXPECT_TRUE(c->changed());
    EXPECT_TRUE(c->commit());
    EXPECT_EQ(undo().undoDescription(), "Undo: Plot");
    EXPECT_EQ(layers(), before + 1);
    EXPECT_EQ(session->getDocument()->getPage(0)->getLayers().back()->getElementsView().size(), 2u);

    undo().undo();
    EXPECT_EQ(layers(), before);
    EXPECT_EQ(elements(), 0u);
    EXPECT_EQ(session->getDocument()->getPage(0)->getBackgroundType().format, PageTypeFormat::Lined);
    EXPECT_FALSE(undo().canUndo());
    undo().redo();
    EXPECT_EQ(layers(), before + 1);
    EXPECT_EQ(elements(), 2u);
    EXPECT_EQ(session->getDocument()->getPage(0)->getBackgroundType().format, PageTypeFormat::Graph);
}

TEST_F(OperationsTest, aFailureRollsTheTransactionBack) {
    auto c = context(authority);
    c->apply("element.insert", {{"shapes", QVariantList{stroke(1, 1, 5, 5)}}});  // (a step of its own before)
    c->begin("Plot");
    c->apply("layer.add", {});
    c->apply("element.insert", {{"shapes", QVariantList{stroke(10, 10, 50, 50)}}});
    // A bad shape: refused before anything of it is done
    EXPECT_THROW(c->apply("element.insert", {{"shapes", QVariantList{stroke(1, 1, 2, 2), QVariantMap{{"type", "blob"}}}}}),
                 Error);
    EXPECT_EQ(elements(), 2u);  // (the bad insert added nothing)
    c->rollback();
    EXPECT_EQ(elements(), 1u);
    EXPECT_EQ(layers(), 1u);
    EXPECT_EQ(undo().undoDescription(), "Undo: element.insert");
    // A context that ends in a transaction rolls it back
    {
        auto d = context(authority);
        d->begin("Unfinished");
        d->apply("element.insert", {{"shapes", QVariantList{stroke(10, 10, 50, 50)}}});
    }
    EXPECT_EQ(elements(), 1u);
}

TEST_F(OperationsTest, groupedShapesAreOneGroupInOneLayerWithTheirData) {
    auto c = context(authority);
    const QVariantList refs =
            c->apply("element.insert",
                     {{"shapes", QVariantList{stroke(10, 10, 100, 10), stroke(10, 10, 10, 100),
                                              QVariantMap{{"type", "markdown"}, {"text", "$x$"}, {"x", 100},
                                                          {"y", 20}, {"anchor", "top"}, {"color", "#0000ff"}},
                                              QVariantMap{{"type", "markdown"}, {"text", "$y$"}, {"x", 20}, {"y", 5},
                                                          {"anchor", "left"}}}},
                      {"group", true},
                      {"data", QVariantMap{{"id", "p1"}, {"f", "x^2"}}}})
                    .toList();
    ASSERT_EQ(refs.size(), 4);
    XojPage& page = *session->getDocument()->getPage(0);
    // A group lies in one layer: the boxes stay with the ink (no Markdown layer made), marked as Markdown texts
    EXPECT_EQ(md::markdownLayer(session->getDocument()->getPage(0)), nullptr);
    Layer* ink = page.getSelectedLayer();
    ASSERT_EQ(ink->getElementsView().size(), 4u);
    const auto all = ink->getElementsView();
    const Element* s0 = *all.begin();
    const auto* x = static_cast<const Text*>(*std::next(all.begin(), 2));
    EXPECT_NE(s0->getGroup(), 0u);
    for (const Element* e: all) {
        EXPECT_EQ(e->getGroup(), s0->getGroup());
    }
    EXPECT_EQ(s0->getColor(), Color(0xff, 0, 0));
    EXPECT_TRUE(x->isMarkdown());
    EXPECT_EQ(x->getColor(), Color(0, 0, 0xff));
    // The data on the first element, under the plugin's id
    EXPECT_NE(s0->getData().find("org.example.test"), std::string::npos);
    EXPECT_TRUE((*std::next(all.begin()))->getData().empty());
    // "top": the box's top middle at (100, 20); as wide as its formula
    const auto box = md::boxRect(*x);
    EXPECT_NEAR(box.y, 20, 0.01);
    EXPECT_NEAR(box.x + box.width / 2, 100, 1.0);
    EXPECT_GT(box.width, 2);
    EXPECT_LT(box.width, 20);
    // Saved and read back: still a Markdown text in that layer
    ASSERT_TRUE(DocumentSession::writeDocument(*session->getDocument(), fs::path(tmp.filePath("g.xopp").toStdString())).ok);
    auto loaded = DocumentSession::loadFile(fs::path(tmp.filePath("g.xopp").toStdString()));
    ASSERT_TRUE(loaded.document);
    const auto back = loaded.document->getPage(0)->getSelectedLayer()->getElementsView();
    ASSERT_EQ(back.size(), 4u);
    EXPECT_TRUE(static_cast<const Text*>(*std::next(back.begin(), 2))->isMarkdown());

    // Listed with its data (only the plugin's own), and deleted with the rest of its group
    const QVariantList listed = c->apply("element.list", {{"onlyWithData", true}}).toList();
    ASSERT_EQ(listed.size(), 1);
    EXPECT_EQ(listed[0].toMap().value("data").toMap().value("f").toString(), "x^2");
    EXPECT_EQ(c->apply("element.delete", {{"refs", QVariantList{listed[0].toMap().value("ref")}}, {"withGroups", true}})
                      .toInt(),
              4);
    EXPECT_EQ(elements(), 0u);
    EXPECT_THROW(c->apply("element.delete", {{"refs", QVariantList{refs[0]}}}), Error);  // (gone: stale)
    undo().undo();
    EXPECT_EQ(elements(), 4u);
}

TEST_F(OperationsTest, aMarkdownBoxOnItsOwnGoesToTheMarkdownLayer) {
    auto c = context(authority);
    c->apply("element.insert", {{"shapes", QVariantList{QVariantMap{{"type", "markdown"}, {"text", "**Note**"},
                                                                     {"x", 50}, {"y", 50}}}}});
    Layer* markdown = md::markdownLayer(session->getDocument()->getPage(0));
    ASSERT_NE(markdown, nullptr);
    ASSERT_EQ(markdown->getElementsView().size(), 1u);
    EXPECT_TRUE(static_cast<const Text*>(markdown->getElementsView().front())->isMarkdown());
    undo().undo();  // (the box and its new layer: one step)
    EXPECT_EQ(md::markdownLayer(session->getDocument()->getPage(0)), nullptr);
}

TEST_F(OperationsTest, dataLayersAndReadOnly) {
    auto c = context(authority);
    const QString ref = c->apply("element.insert", {{"shapes", QVariantList{stroke(10, 10, 50, 50)}}}).toList()[0].toString();
    EXPECT_TRUE(c->apply("element.data", {{"ref", ref}, {"value", QVariantMap{{"n", 3}}}}).toBool());
    const Element* e = session->getDocument()->getPage(0)->getSelectedLayer()->getElementsView().front();
    EXPECT_EQ(e->getData(), R"({"org.example.test":{"n":3}})");
    undo().undo();
    EXPECT_TRUE(e->getData().empty());

    c->apply("layer.rename", {{"layer", 0}, {"name", "Ink"}});
    EXPECT_EQ(session->getDocument()->getPage(0)->getLayers()[0]->getName(), "Ink");
    c->apply("layer.visible", {{"layer", 0}, {"visible", false}});
    EXPECT_FALSE(session->getDocument()->getPage(0)->getLayers()[0]->isVisible());
    undo().undo();
    EXPECT_TRUE(session->getDocument()->getPage(0)->getLayers()[0]->isVisible());

    EXPECT_EQ(c->apply("page.insert", {{"count", 2}, {"background", "graph"}}).toInt(), 1);
    EXPECT_EQ(session->getDocument()->getPageCount(), 3u);
    EXPECT_EQ(session->getDocument()->getPage(2)->getBackgroundType().format, PageTypeFormat::Graph);
    const QVariantMap page = c->apply("page.read", {{"page", 2}}).toMap();
    EXPECT_EQ(page.value("background").toMap().value("type").toString(), "graph");
    EXPECT_THROW(c->apply("background.set", {{"type", "pdf"}}), Error);
    EXPECT_THROW(c->apply("page.read", {{"page", 9}}), Error);
    EXPECT_THROW(c->apply("no.such"), Error);

    session->setReplaying(true);  // (the timeline replays it: no changes)
    try {
        c->apply("element.insert", {{"shapes", QVariantList{stroke(10, 10, 50, 50)}}});
        FAIL();
    } catch (const Error& err) {
        EXPECT_EQ(err.kind, Error::Kind::ReadOnly);
    }
}

TEST_F(OperationsTest, patternsAndClasses) {
    EXPECT_TRUE(patternsOfClass("edit").matches("stroke.insert"));
    EXPECT_TRUE(patternsOfClass("edit").matches("element.delete"));
    EXPECT_FALSE(patternsOfClass("edit").matches("page.insert"));
    EXPECT_TRUE(patternsOfClass("pages").matches("background.set"));
    EXPECT_TRUE(Patterns({"*"}).matches("anything"));
    EXPECT_FALSE(Patterns({"layer.add"}).matches("layer.addx"));
    EXPECT_TRUE(grantableClasses().contains("files"));
    EXPECT_FALSE(grantableClasses().contains("read"));
    // Every operation of the table belongs to the patterns of its class (so a capability by patterns grants it)
    for (const QString& name: ops.names()) {
        const auto* info = ops.find(name);
        EXPECT_TRUE(patternsOfClass(info->opClass).matches(name)) << name.toStdString();
    }
}
