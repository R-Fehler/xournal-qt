/*
 * xournal-qt: the width of a Markdown text box, set with the handle on its right edge (MarkdownBoxResize;
 * qt/docs/markdown-boxes.md, "Size"): while it is written on the page and while it is selected, with the mouse, the
 * pen and a finger. The page's own Markdown text has no handle.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>
#include <string>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QTouchEvent>
#include <gtest/gtest.h>

#include "control/ToolHandler.h"
#include "control/settings/Settings.h"
#include "control/tools/EditSelection.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "undo/UndoRedoHandler.h"

#include "CanvasInput.h"
#include "CanvasPage.h"
#include "CanvasView.h"
#include "MarkdownBoxResize.h"
#include "MarkdownEditor.h"
#include "MdBox.h"
#include "TextFlow.h"

using namespace xqt;

namespace {
const std::string LONG_TEXT =
        "A paragraph long enough to fill several lines of a box, so that a narrower box needs more of them: the "
        "text flows anew when the handle on the right edge of the box is dragged, and the box is as high as its "
        "text.";

class MarkdownBoxResizeTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        view = std::make_unique<CanvasView>(*session);
        view->getViewController().setViewSize(QSizeF(900, 1400));
        input = std::make_unique<CanvasInput>(*view);
        view->setMarkdownText(true, 10, false);  // (the text tool writes Markdown text boxes, on the page)
        app->getToolHandler()->selectTool(TOOL_TEXT);
        processEvents();
    }
    void TearDown() override {
        input.reset();
        view.reset();
        session.reset();
        app.reset();
    }

    void processEvents(int ms = 30) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }

    PageRef page() const { return session->getDocument()->getPage(0); }
    double zoom() const { return view->getViewController().zoom(); }
    QPointF viewPos(QPointF onPage) const { return view->pageViewRect(0).topLeft() + onPage * zoom(); }

    void type(const std::string& s) {
        for (const char c: s) {
            QKeyEvent e(QEvent::KeyPress, static_cast<Qt::Key>(QChar(c).toUpper().unicode()), Qt::NoModifier,
                        QString(QChar(c)));
            bool finish = false;
            view->getMarkdownEditor()->keyPressed(&e, finish);
        }
    }

    /// A text box written at (x, y) with the text tool, still being written.
    MarkdownEditor& writeBox(double x, double y, const std::string& text) {
        view->startMarkdown(0, false, x, y);
        EXPECT_NE(view->getMarkdownEditor(), nullptr);
        type(text);
        processEvents();
        return *view->getMarkdownEditor();
    }
    /// The Markdown texts of the page (the page's own text and the boxes)
    std::vector<Text*> texts() const {
        std::vector<Text*> out;
        if (const Layer* layer = md::markdownLayer(page())) {
            for (const Element* e: layer->getElementsView()) {
                if (e->getType() == ELEMENT_TEXT) {
                    out.push_back(const_cast<Text*>(static_cast<const Text*>(e)));
                }
            }
        }
        return out;
    }

    void mouse(QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent e(type, pos, pos, button, buttons, Qt::NoModifier, &mousePointer);
        e.setTimestamp(timestamp);
        timestamp += 5;
        input->mouseEvent(&e, pos);
    }
    void mouseDrag(QPointF from, QPointF to) {
        mouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 10; ++i) {
            mouse(QEvent::MouseMove, from + (to - from) * (i / 10.0), Qt::NoButton, Qt::LeftButton);
        }
        mouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        processEvents();
    }
    void tablet(QEvent::Type type, QPointF pos, double pressure, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QTabletEvent e(type, &pen, pos, pos, pressure, 0.f, 0.f, 0.f, 0.0, 0.f, Qt::NoModifier, button, buttons);
        e.setTimestamp(timestamp);
        timestamp += 5;
        input->tabletEvent(&e, pos);
    }
    void penDrag(QPointF from, QPointF to) {
        tablet(QEvent::TabletPress, from, 0.5, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 10; ++i) {
            tablet(QEvent::TabletMove, from + (to - from) * (i / 10.0), 0.5, Qt::NoButton, Qt::LeftButton);
        }
        tablet(QEvent::TabletRelease, to, 0.0, Qt::LeftButton, Qt::NoButton);
        processEvents();
    }
    void touch(QEvent::Type type, QEventPoint::State state, QPointF pos) {
        QEventPoint p(1, state, pos, pos);
        QTouchEvent e(type, &touchscreen, Qt::NoModifier, {p});
        input->touchEvent(&e, [](QPointF scene) { return scene; });
    }
    void fingerDrag(QPointF from, QPointF to) {
        touch(QEvent::TouchBegin, QEventPoint::State::Pressed, from);
        for (int i = 1; i <= 10; ++i) {
            touch(QEvent::TouchUpdate, QEventPoint::State::Updated, from + (to - from) * (i / 10.0));
        }
        touch(QEvent::TouchEnd, QEventPoint::State::Released, to);
        processEvents();
    }

    /// The page as the canvas shows it (its picture and the overlays), around a point of the page
    QImage shownAround(QPointF onPage, double radius) {
        processEvents();
        CanvasPage* p = view->getPage(0);
        const auto info = p->bufferInfo();
        EXPECT_TRUE(info.valid);
        const double s = info.zoom * info.dpiScale;
        return p->composeTile(QRectF((onPage.x() - radius) * s, (onPage.y() - radius) * s, 2 * radius * s,
                                     2 * radius * s)
                                      .toRect());
    }
    /// Pixels of the selection's color (the handle's ring)
    int selectionColored(const QImage& img) const {
        const Color c = session->getSettings()->getSelectionColor();
        int n = 0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const QRgb p = img.pixel(x, y);
                n += std::abs(qRed(p) - c.red) < 30 && std::abs(qGreen(p) - c.green) < 30 &&
                     std::abs(qBlue(p) - c.blue) < 30;
            }
        }
        return n;
    }

    /// Select a box with a tap of the object select tool; returns the view position of its right knob.
    QPointF selectBox(const Text& box) {
        app->getToolHandler()->selectTool(TOOL_SELECT_OBJECT);
        const auto r = md::boxRect(box);
        const QPointF at = viewPos(QPointF(r.x + 10, r.y + 5));
        mouse(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
        processEvents();
        EditSelection* sel = view->getSelection();
        EXPECT_NE(sel, nullptr);
        if (!sel) {
            return {};
        }
        return viewPos(QPointF(sel->getXOnView() + sel->getWidth(), sel->getYOnView() + sel->getHeight() / 2));
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    std::unique_ptr<CanvasInput> input;
    QPointingDevice pen{"test pen", 1001, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3};
    QPointingDevice touchscreen{"test touchscreen", 1004, QInputDevice::DeviceType::TouchScreen,
                                QPointingDevice::PointerType::Finger, QInputDevice::Capability::Position, 10, 0};
    QPointingDevice mousePointer{"test mouse", 1005, QInputDevice::DeviceType::Mouse,
                                 QPointingDevice::PointerType::Generic, QInputDevice::Capability::Position, 3, 3};
    ulong timestamp = 1000;
};
}  // namespace

// A box written on the page has the handle in the middle of its right edge. Dragged (the mouse) to half the width,
// the text flows onto more lines and the box gets higher; it gets no narrower than 2 cm and no wider than the page.
// Each drag is an undo step of the text being written.
TEST_F(MarkdownBoxResizeTest, aBoxWrittenOnThePageIsResizedByItsHandle) {
    MarkdownEditor& editor = writeBox(100, 200, LONG_TEXT);
    ASSERT_EQ(texts().size(), 1u);
    Text* box = texts().front();
    const double width = box->getWrap();
    const double pageWidth = page()->getWidth();
    EXPECT_GT(width, 300) << "a new box goes to the right margin";
    const double height = md::contentHeight(*box);

    const auto handle = editor.widthHandle();
    ASSERT_TRUE(handle) << "a text box written on the page has the handle";
    EXPECT_NEAR(handle->y(), md::boxRect(*box).y + std::max(height, 13.0) / 2, 1) << "in the middle of its right edge";
    EXPECT_TRUE(view->boxResize().onHandle(viewPos(*handle)));
    EXPECT_FALSE(view->boxResize().onHandle(viewPos(*handle - QPointF(width / 2, 0))));
    const QImage shown = shownAround(*handle, 15 / zoom());
    EXPECT_GT(selectionColored(shown), 30) << "the handle is drawn";

    // Half the width: more lines, a higher box (the layout the box is drawn from)
    mouseDrag(viewPos(*handle), viewPos(*handle - QPointF(width / 2, 0)));
    ASSERT_EQ(view->getMarkdownEditor(), &editor) << "still written";
    EXPECT_NEAR(box->getWrap(), width / 2, 1);
    EXPECT_NEAR(editor.boxWidth(), width / 2, 1);
    EXPECT_GT(md::contentHeight(*box), height * 1.5) << "more lines";
    EXPECT_EQ(box->getText(), LONG_TEXT);

    // The narrowest: 2 cm; the widest: to the page's right edge
    mouseDrag(viewPos(*editor.widthHandle()), viewPos(QPointF(10, handle->y())));
    EXPECT_NEAR(box->getWrap(), MarkdownBoxResize::MIN_WIDTH, 1e-6);
    mouseDrag(viewPos(*editor.widthHandle()), viewPos(QPointF(pageWidth + 200, handle->y())));
    EXPECT_NEAR(box->getWrap(), pageWidth - 100, 1e-6);

    // Undo in the text being written: a drag at a time
    ASSERT_TRUE(editor.canUndo());
    editor.undo();
    EXPECT_NEAR(box->getWrap(), MarkdownBoxResize::MIN_WIDTH, 1e-6);
    editor.undo();
    EXPECT_NEAR(box->getWrap(), width / 2, 1);
    editor.undo();
    EXPECT_NEAR(box->getWrap(), width, 1e-6) << "undo restores the width";
    EXPECT_NEAR(md::contentHeight(*box), height, 1e-6);
    editor.redo();
    EXPECT_NEAR(box->getWrap(), width / 2, 1);
    EXPECT_EQ(box->getText(), LONG_TEXT) << "the text is not touched";

    view->endTextEditing();
    EXPECT_NEAR(texts().front()->getWrap(), width / 2, 1) << "kept when the writing is done";
}

// An existing box, written again: only its width changed, the edit's one undo step puts the width back.
TEST_F(MarkdownBoxResizeTest, theEditOfAnExistingBoxUndoesItsWidth) {
    writeBox(100, 200, LONG_TEXT);
    view->endTextEditing();
    Text* box = texts().front();
    const double width = box->getWrap();
    view->startMarkdown(0, false, 110, 205);  // (a tap on the box writes it again)
    MarkdownEditor* editor = view->getMarkdownEditor();
    ASSERT_NE(editor, nullptr);
    const auto handle = editor->widthHandle();
    ASSERT_TRUE(handle);
    penDrag(viewPos(*handle), viewPos(*handle - QPointF(150, 0)));  // (the pen)
    EXPECT_NEAR(box->getWrap(), width - 150, 1);
    view->endTextEditing();
    session->getUndoRedoHandler()->undo();
    ASSERT_EQ(texts().size(), 1u);
    EXPECT_NEAR(texts().front()->getWrap(), width, 1e-6);
    EXPECT_EQ(texts().front()->getText(), LONG_TEXT);
}

// The page's own Markdown text goes from margin to margin: no handle, while it is written or when it is selected.
TEST_F(MarkdownBoxResizeTest, thePageTextHasNoHandle) {
    const auto m = TextFlow::styleFor(page(), TextFlow::Style{});
    view->startMarkdown(0, true, m.leftMargin + 5, TextFlow::MARGIN + 5);
    MarkdownEditor* editor = view->getMarkdownEditor();
    ASSERT_NE(editor, nullptr);
    type(LONG_TEXT);
    EXPECT_FALSE(editor->widthHandle());
    ASSERT_EQ(texts().size(), 1u);
    Text* text = texts().front();
    const double width = text->getWrap();
    const auto r = md::boxRect(*text);
    const QPointF edge(r.x + r.width + 3, r.y + r.height / 2);
    EXPECT_FALSE(view->boxResize().onHandle(viewPos(edge)));
    mouseDrag(viewPos(edge), viewPos(edge - QPointF(width / 2, 0)));
    EXPECT_DOUBLE_EQ(text->getWrap(), width) << "a drag there does not change the width";
    view->endTextEditing();

    selectBox(*text);
    EXPECT_EQ(view->boxResize().selectedBox(), nullptr) << "selected, the page's text has no handle either";
}

// A box selected alone (the object select tool): its right knob sets the width (the pen), the text flows anew, it is
// selected again, and the drag is one undo step. The selection's other knobs are as they were: it still moves.
TEST_F(MarkdownBoxResizeTest, aSelectedBoxIsResizedByItsRightKnob) {
    writeBox(100, 200, LONG_TEXT);
    view->endTextEditing();
    Text* box = texts().front();
    const double width = box->getWrap();
    const double height = md::contentHeight(*box);

    const QPointF knob = selectBox(*box);
    ASSERT_EQ(view->boxResize().selectedBox(), box);
    EXPECT_TRUE(view->boxResize().onHandle(knob));
    EXPECT_FALSE(view->boxResize().onHandle(knob - QPointF(width / 2 * zoom(), 0))) << "inside: moving it";

    penDrag(knob, knob - QPointF(width / 2 * zoom(), 0));
    ASSERT_NE(view->getSelection(), nullptr) << "selected again";
    ASSERT_EQ(view->getSelection()->getElementsView().size(), 1u);
    ASSERT_EQ(view->getSelection()->getElementsView().front(), box);
    EXPECT_NEAR(box->getWrap(), width / 2, 1);
    EXPECT_GT(md::contentHeight(*box), height * 1.5) << "more lines";
    EXPECT_FALSE(box->isInEditing()) << "drawn by the renderer again";
    EXPECT_EQ(view->boxResize().selectedBox(), box);
    // (the selection has a margin around the box, the same on every side)
    EXPECT_NEAR(view->getSelection()->getHeight() - md::contentHeight(*box),
                view->getSelection()->getWidth() - box->getWrap(), 0.5)
            << "the selection is the new box";

    // Moving it: the middle of the selection, as before
    const auto before = box->getTransformation().shift;
    EditSelection* sel = view->getSelection();
    const QPointF middle = viewPos(QPointF(sel->getXOnView() + sel->getWidth() / 2,
                                           sel->getYOnView() + sel->getHeight() / 2));
    penDrag(middle, middle + QPointF(40, 60) * zoom());
    view->clearSelection();
    ASSERT_EQ(texts().size(), 1u);
    EXPECT_NEAR(texts().front()->getTransformation().shift.x, before.x + 40, 1);
    EXPECT_NEAR(texts().front()->getTransformation().shift.y, before.y + 60, 1);
    EXPECT_NEAR(texts().front()->getWrap(), width / 2, 1) << "moved, not resized";

    // One undo step each: the move, then the width
    session->getUndoRedoHandler()->undo();
    EXPECT_NEAR(texts().front()->getTransformation().shift.x, before.x, 1);
    session->getUndoRedoHandler()->undo();
    EXPECT_NEAR(texts().front()->getWrap(), width, 1e-6) << "undo restores the width";
    session->getUndoRedoHandler()->redo();
    EXPECT_NEAR(texts().front()->getWrap(), width / 2, 1);
}

// A finger on the selected box's right knob sets the width too; at most to the page's right edge.
TEST_F(MarkdownBoxResizeTest, aFingerDragsTheKnob) {
    writeBox(300, 200, "Some *words* in a box.");
    view->endTextEditing();
    Text* box = texts().front();
    const QPointF knob = selectBox(*box);
    fingerDrag(knob, knob + QPointF(2000, 0));
    EXPECT_NEAR(box->getWrap(), page()->getWidth() - 300, 1e-6);
    EXPECT_NE(view->getSelection(), nullptr);
}

// The width is upstream's wrap attribute of the text: saved and loaded again.
TEST_F(MarkdownBoxResizeTest, theWidthIsSavedAndLoaded) {
    MarkdownEditor& editor = writeBox(100, 200, LONG_TEXT);
    const auto handle = editor.widthHandle();
    ASSERT_TRUE(handle);
    mouseDrag(viewPos(*handle), viewPos(*handle - QPointF(173, 0)));
    view->endTextEditing();
    const double width = texts().front()->getWrap();
    ASSERT_LT(width, 400);
    const auto path = fs::path(tmp.filePath("box.xopp").toStdString());
    ASSERT_TRUE(session->saveAs(path).ok);
    auto loaded = DocumentSession::loadFile(path);
    ASSERT_TRUE(loaded.document) << loaded.error;
    const Layer* layer = md::markdownLayer(loaded.document->getPage(0));
    ASSERT_NE(layer, nullptr);
    const Text* box = md::boxOf(*layer);
    ASSERT_NE(box, nullptr);
    EXPECT_NEAR(box->getWrap(), width, 1e-3);
    EXPECT_EQ(box->getText(), LONG_TEXT);
}

// A box resized on a page with the page's own text: the page's text stays as it was (its flow is the page's).
TEST_F(MarkdownBoxResizeTest, aResizedBoxLeavesThePageTextAlone) {
    const auto m = TextFlow::styleFor(page(), TextFlow::Style{});
    view->startMarkdown(0, true, m.leftMargin + 5, TextFlow::MARGIN + 5);
    type("# Page\n" + LONG_TEXT);
    view->endTextEditing();
    ASSERT_EQ(texts().size(), 1u);
    const Text* pageText = texts().front();
    const std::string source = pageText->getText();
    const double pageWidth = pageText->getWrap();
    const size_t pages = session->getDocument()->getPageCount();

    MarkdownEditor& editor = writeBox(150, 500, LONG_TEXT);
    const auto handle = editor.widthHandle();
    ASSERT_TRUE(handle);
    mouseDrag(viewPos(*handle), viewPos(*handle - QPointF(200, 0)));
    view->endTextEditing();
    ASSERT_EQ(texts().size(), 2u);
    EXPECT_EQ(texts().front(), pageText);
    EXPECT_EQ(pageText->getText(), source);
    EXPECT_DOUBLE_EQ(pageText->getWrap(), pageWidth);
    EXPECT_EQ(session->getDocument()->getPageCount(), pages);
    EXPECT_EQ(md::pageBoxOf(*md::markdownLayer(page()), m.leftMargin, TextFlow::MARGIN), pageText);
}
