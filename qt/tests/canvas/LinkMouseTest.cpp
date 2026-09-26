/*
 * xournal-qt: links with the mouse (qt/docs/links.md, "Links with the mouse"). A click follows a link as a tap does
 * (the same signal, so the same sheet), with the hand, the select tools and the tools that draw - without drawing a
 * dot; a drag that begins on a link still draws. In text being written a plain click puts the cursor, Ctrl + click
 * follows. The mouse looks up links on every move, so they come from what each page keeps: counted here.
 *
 * @license GNU GPLv2 or later
 */
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "model/Document.h"
#include "model/Font.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentLink.h"
#include "session/DocumentSession.h"
#include "session/TextFile.h"

#include "CanvasInput.h"
#include "CanvasView.h"
#include "MarkdownEditor.h"
#include "MarkdownFile.h"
#include "config-test.h"

using namespace xqt;

namespace {
/// A PDF of two pages 400 × 400: on the first an external link at (50..150, 50..80) and a link to page 2 at
/// (50..150, 200..230) (written by hand: cairo cannot write page links).
void writeLinkPdf(const std::string& path) {
    const std::vector<std::string> objects{
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 400] /Annots [5 0 R 6 0 R] >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 400] >>",
            "<< /Type /Annot /Subtype /Link /Rect [50 320 150 350] /Border [0 0 0] "
            "/A << /S /URI /URI (https://example.org/doc) >> >>",
            "<< /Type /Annot /Subtype /Link /Rect [50 170 150 200] /Border [0 0 0] /Dest [4 0 R /XYZ 0 400 0] >>"};
    std::string pdf = "%PDF-1.4\n";
    std::vector<size_t> offsets;
    for (size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t o: offsets) {
        char line[24];
        std::snprintf(line, sizeof(line), "%010zu 00000 n \n", o);
        pdf += line;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" +
           std::to_string(xref) + "\n%%EOF\n";
    std::ofstream(path, std::ios::binary) << pdf;
}

class LinkMouseTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        app->getToolHandler()->selectTool(TOOL_PEN);
    }
    void TearDown() override {
        input.reset();
        view.reset();
        session.reset();
        app.reset();
    }
    void openPdf() {
        const std::string pdf = tmp.filePath("links.pdf").toStdString();
        writeLinkPdf(pdf);
        auto loaded = DocumentSession::loadFile(pdf);
        ASSERT_TRUE(loaded.document) << loaded.error;
        session = std::make_unique<DocumentSession>(*app, std::move(loaded.document));
        makeView();
    }
    void makeView() {
        view = std::make_unique<CanvasView>(*session);
        view->getViewController().setViewSize(QSizeF(800, 1000));
        input = std::make_unique<CanvasInput>(*view);
        processEvents();
    }
    void processEvents(int ms = 30) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }
    QPointF at(size_t page, QPointF onPage) const {
        return view->pageViewRect(page).topLeft() + onPage * view->getViewController().zoom();
    }
    void mouse(QEvent::Type type, QPointF pos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods = {}) {
        const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        QMouseEvent e(type, pos, pos, button, buttons, mods, &mousePointer);
        e.setTimestamp(timestamp);
        timestamp += 8;
        input->mouseEvent(&e, pos);
    }
    void click(QPointF pos, Qt::KeyboardModifiers mods = {}) {
        mouse(QEvent::MouseButtonPress, pos, Qt::LeftButton, mods);
        mouse(QEvent::MouseMove, pos + QPointF(2, 1), Qt::LeftButton, mods);  // (a hand is never quite still)
        mouse(QEvent::MouseButtonRelease, pos + QPointF(2, 1), Qt::NoButton, mods);
        processEvents();
    }
    size_t elements() const {
        size_t n = 0;
        Document* doc = session->getDocument();
        for (size_t i = 0; i < doc->getPageCount(); ++i) {
            for (const Layer* l: doc->getPage(i)->getLayersView()) {
                n += l->getElementsView().size();
            }
        }
        return n;
    }
    /// The first place of a page where the mouse finds a link to `uri` (a search in steps of 3 pixels)
    std::optional<QPointF> linkPlace(size_t page, const QString& uri) {
        const QRectF r = view->pageViewRect(page);
        for (double y = r.top(); y < r.bottom(); y += 3) {
            for (double x = r.left(); x < r.right(); x += 3) {
                if (auto hover = view->hoverLinkAt(QPointF(x, y)); hover && hover->target.uri.contains(uri)) {
                    const QRectF spot = hover->target.viewRect;
                    return spot.center();
                }
            }
        }
        return std::nullopt;
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    std::unique_ptr<CanvasInput> input;
    QPointingDevice mousePointer{"test mouse", 1105, QInputDevice::DeviceType::Mouse,
                                 QPointingDevice::PointerType::Generic, QInputDevice::Capability::Position, 3, 3};
    ulong timestamp = 1000;
};
}  // namespace

TEST_F(LinkMouseTest, aClickWithThePenFollowsAPdfLinkWithoutADotAndADragStillDraws) {
    openPdf();
    QSignalSpy followed(view.get(), &CanvasView::linkTapped);
    click(at(0, QPointF(100, 65)));
    ASSERT_EQ(followed.count(), 1) << "the click follows the web link";
    EXPECT_EQ(followed.at(0).at(0).toString(), "https://example.org/doc");
    EXPECT_EQ(elements(), 0u) << "and draws no dot";

    click(at(0, QPointF(100, 215)));
    ASSERT_EQ(followed.count(), 2) << "the link to page 2";
    EXPECT_TRUE(followed.at(1).at(0).toString().isEmpty());
    EXPECT_EQ(followed.at(1).at(1).toInt(), 1);

    // A drag that begins on the link: a stroke, from where the mouse was pressed
    const QPointF from = at(0, QPointF(100, 65));
    mouse(QEvent::MouseButtonPress, from, Qt::LeftButton);
    for (int i = 1; i <= 10; ++i) {
        mouse(QEvent::MouseMove, from + QPointF(8 * i, 5 * i), Qt::LeftButton);
    }
    mouse(QEvent::MouseButtonRelease, from + QPointF(80, 50), Qt::NoButton);
    processEvents();
    EXPECT_EQ(followed.count(), 2) << "a drag follows nothing";
    ASSERT_EQ(elements(), 1u) << "it drew";
    const Layer* layer = session->getDocument()->getPage(0)->getSelectedLayer();
    const auto* stroke = static_cast<const Stroke*>(layer->getElementsView().front());
    ASSERT_EQ(stroke->getType(), ELEMENT_STROKE);
    const auto first = stroke->getPoint(0);
    EXPECT_NEAR(first.x, 100, 1.5) << "the stroke begins where the mouse was pressed";
    EXPECT_NEAR(first.y, 65, 1.5);
    EXPECT_GE(stroke->getPointCount(), 3u);

    // Away from links the click draws its dot as always
    click(at(0, QPointF(300, 300)));
    EXPECT_EQ(followed.count(), 2);
    EXPECT_EQ(elements(), 2u);
}

TEST_F(LinkMouseTest, whichToolsAClickFollowsWith) {
    openPdf();
    QSignalSpy followed(view.get(), &CanvasView::linkTapped);
    const QPointF link = at(0, QPointF(100, 65));
    ToolHandler* tools = app->getToolHandler();
    for (ToolType t: {TOOL_HAND, TOOL_SELECT_RECT, TOOL_SELECT_REGION, TOOL_SELECT_OBJECT, TOOL_SELECT_PDF_TEXT_LINEAR,
                      TOOL_HIGHLIGHTER, TOOL_ERASER}) {
        tools->selectTool(t);
        const int before = followed.count();
        click(link);
        EXPECT_EQ(followed.count(), before + 1) << "tool " << t;
    }
    EXPECT_EQ(elements(), 0u) << "the highlighter drew nothing";
    // A click held for a while is a click too (the mouse: no time limit)
    tools->selectTool(TOOL_HAND);
    mouse(QEvent::MouseButtonPress, link, Qt::LeftButton);
    processEvents(450);
    mouse(QEvent::MouseButtonRelease, link, Qt::NoButton);
    EXPECT_EQ(followed.count(), 8);

    // The text tool writes where it clicks; Ctrl + click follows with any tool
    tools->selectTool(TOOL_TEXT);
    EXPECT_FALSE(input->clickFollowsLink(false, Qt::NoModifier));
    EXPECT_TRUE(input->clickFollowsLink(false, Qt::ControlModifier));
    view->endTextEditing();
    click(link, Qt::ControlModifier);
    EXPECT_EQ(followed.count(), 9);
    EXPECT_EQ(view->getTextInput(), nullptr) << "Ctrl + click started no text";
}

TEST_F(LinkMouseTest, aClickFollowsADocumentLinkMarkerAndAWebAddressInAText) {
    session = std::make_unique<DocumentSession>(*app);
    makeView();
    // A link marker: a copied link pasted on the page (a Markdown box in the Markdown layer)
    auto* mime = new QMimeData;
    links::Link target;
    target.path = QString::fromStdString((fs::path(tmp.path().toStdString()) / "kalman.xopp").string());
    target.page = 3;
    links::toMime(*mime, "kalman, page 3", fs::path(target.path.toStdString()), target);
    QGuiApplication::clipboard()->setMimeData(mime);
    ASSERT_TRUE(view->pasteLinkMarker(at(0, QPointF(100, 100))));
    // A web address in an ordinary text
    auto text = std::make_unique<Text>();
    text->setText("see https://example.org/paper for more");
    text->setFont(XojFont("Sans", 12));
    text->move(100, 300);
    {
        auto page = session->getDocument()->getPage(0);
        std::unique_lock lock(*session->getDocument());
        page->getSelectedLayer()->addElement(std::move(text));
    }
    session->getDocument()->getPage(0)->firePageChanged();
    processEvents();

    const auto marker = linkPlace(0, "kalman.xopp");
    ASSERT_TRUE(marker) << "the marker's link is found";
    const auto web = linkPlace(0, "example.org/paper");
    ASSERT_TRUE(web);

    const size_t before = elements();
    QSignalSpy followed(view.get(), &CanvasView::linkTapped);
    click(*marker);
    ASSERT_EQ(followed.count(), 1) << "the pen's click follows the marker";
    EXPECT_NE(followed.at(0).at(0).toString().indexOf("kalman.xopp#page=3"), -1)
            << followed.at(0).at(0).toString().toStdString();
    click(*web);
    ASSERT_EQ(followed.count(), 2);
    EXPECT_EQ(followed.at(1).at(0).toString(), "https://example.org/paper");
    EXPECT_EQ(elements(), before) << "no dots";
}

TEST_F(LinkMouseTest, theMouseLooksUpTheLinksOfAPageOnceUntilItChanges) {
    openPdf();
    EXPECT_EQ(view->linkLookups(), 0);
    // Many moves over the page, on and off the links
    int hits = 0;
    for (int i = 0; i < 400; ++i) {
        hits += view->hoverLinkAt(at(0, QPointF(40 + (i % 40) * 3, 50 + (i / 40) * 20))).has_value() ? 1 : 0;
    }
    EXPECT_GT(hits, 0);
    EXPECT_EQ(view->linkLookups(), 1) << "the page's links were looked for once";
    // Clicks use the same (the tap's own lookup is for touch)
    click(at(0, QPointF(100, 65)));
    EXPECT_EQ(view->linkLookups(), 1);

    // The page changes (a stroke drawn away from the links): looked for once more, then kept again
    click(at(0, QPointF(300, 300)));
    for (int i = 0; i < 100; ++i) {
        view->hoverLinkAt(at(0, QPointF(40 + i, 65)));
    }
    EXPECT_EQ(view->linkLookups(), 2);
    // The second page has its own
    view->hoverLinkAt(at(1, QPointF(100, 65)));
    view->hoverLinkAt(at(1, QPointF(120, 65)));
    EXPECT_EQ(view->linkLookups(), 3);
}

TEST_F(LinkMouseTest, inTextBeingWrittenAClickPutsTheCursorAndCtrlClickFollows) {
    const fs::path md = fs::path(tmp.path().toStdString()) / "note.md";
    std::ofstream(md, std::ios::binary) << "# Note\n\nRead [the paper](https://example.org/md) today.\n";
    auto file = std::make_unique<TextFile>();
    std::string error;
    ASSERT_TRUE(file->load(md, TextFile::Kind::Markdown, error)) << error;
    session = std::make_unique<DocumentSession>(*app, MarkdownFile::textDocument(*file, false));
    session->setTextFile(std::move(file), false);
    makeView();
    ASSERT_TRUE(view->textMode());

    const auto link = linkPlace(0, "example.org/md");
    ASSERT_TRUE(link);
    const auto hover = view->hoverLinkAt(*link);
    ASSERT_TRUE(hover);
    EXPECT_TRUE(hover->editing) << "a text file is written";

    QSignalSpy followed(view.get(), &CanvasView::linkTapped);
    click(*link);
    EXPECT_EQ(followed.count(), 0) << "a plain click puts the cursor there";
    ASSERT_NE(view->getMarkdownEditor(), nullptr);
    const size_t cursor = view->getMarkdownEditor()->cursorPosition();
    const size_t linkText = session->currentText().find("the paper");
    EXPECT_GE(cursor, linkText) << "in the link's text";
    EXPECT_LE(cursor, linkText + 30);

    click(*link, Qt::ControlModifier);
    ASSERT_EQ(followed.count(), 1) << "Ctrl + click follows it";
    EXPECT_EQ(followed.at(0).at(0).toString(), "https://example.org/md");
}
