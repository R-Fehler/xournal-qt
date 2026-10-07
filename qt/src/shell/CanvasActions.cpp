#include "CanvasActions.h"

#include <cmath>

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QMimeData>

#include "control/ToolHandler.h"

#include "CanvasView.h"
#include "ContentFiles.h"
#include "MixedSelection.h"
#include "StickyNotes.h"
#include "ViewController.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

namespace xqt {

namespace {
CanvasView::PdfTextMode pdfModeFrom(const QString& m) {
    return m == "underline"       ? CanvasView::PdfTextMode::Underline
           : m == "strikethrough" ? CanvasView::PdfTextMode::Strikethrough
           : m == "select"        ? CanvasView::PdfTextMode::Select
                                  : CanvasView::PdfTextMode::Highlight;
}
QPointF middleOf(const ViewController& vc) { return {vc.viewSize().width() / 2, vc.viewSize().height() / 2}; }
}  // namespace

CanvasActions::CanvasActions(Policy policy, QObject* parent): QObject(parent), rules(std::move(policy)) {}

CanvasView* CanvasActions::view() const { return shown.data(); }

CanvasActions::~CanvasActions() {
    for (auto& c: connections) {
        disconnect(c);
    }
}

void CanvasActions::setView(CanvasView* v) {
    if (v == shown) {
        return;
    }
    for (auto& c: connections) {
        disconnect(c);
    }
    connections.clear();
    shown = v;
    if (v) {
        auto relay = [this](auto sender, auto signal, auto to) { connections.push_back(connect(sender, signal, this, to)); };
        relay(v, &CanvasView::selectionChanged, &CanvasActions::selectionChanged);
        relay(v, &CanvasView::noteSelectionChanged, &CanvasActions::noteSelectionChanged);
        // (select more: available, on or off, the count)
        relay(v, &CanvasView::selectMoreChanged, &CanvasActions::selectMoreChanged);
        relay(v, &CanvasView::selectionChanged, &CanvasActions::selectMoreChanged);
        relay(v, &CanvasView::noteSelectionChanged, &CanvasActions::selectMoreChanged);
        relay(&v->getSession().getApp(), &AppContext::activeToolChanged, &CanvasActions::selectMoreChanged);
        // Whoever changes the PDF text selection (a press on the page, copying, marking, a page change): the knobs
        // and the pill follow it
        relay(v, &CanvasView::pdfTextSelected, &CanvasActions::pdfTextSelected);
        relay(v, &CanvasView::pdfTextSelected, &CanvasActions::pdfTextSelectionChanged);
        relay(v, &CanvasView::pdfTextSelectionCleared, &CanvasActions::pdfTextSelectionChanged);
        relay(v, &CanvasView::navigationChanged, &CanvasActions::navigationChanged);
        relay(v, &CanvasView::currentPageChanged, &CanvasActions::pageChanged);
        relay(v, &CanvasView::pagesChanged, &CanvasActions::pageChanged);
        relay(&v->getViewController(), &ViewController::zoomChanged, &CanvasActions::zoomChanged);
        relay(&v->getViewController(), &ViewController::zoom100Changed, &CanvasActions::zoomChanged);
    }
    Q_EMIT selectionChanged();
    Q_EMIT noteSelectionChanged();
    Q_EMIT selectMoreChanged();
    Q_EMIT pdfTextSelectionChanged();
    Q_EMIT navigationChanged();
    Q_EMIT pageChanged();
    Q_EMIT zoomChanged();
}

bool CanvasActions::readingOnly() const {
    return (rules.readingOnly && rules.readingOnly()) || (shown && shown->getSession().isReadOnly());
}

bool CanvasActions::pagesFixed() const {
    const DocumentSession* s = shown ? &shown->getSession() : nullptr;
    return s && s->textFile() && !s->hasFilePath();
}

void CanvasActions::chooseSelectTool(ToolType type) {
    ToolHandler* tools = shown->getSession().getToolHandler();
    if (tools->getToolType() == TOOL_SELECT_RECT || tools->getToolType() == TOOL_SELECT_REGION) {
        return;  // (a select tool in hand already)
    }
    if (rules.selectTool) {
        rules.selectTool(type);
    } else {
        tools->selectTool(type);
        tools->fireToolChanged();
    }
}

bool CanvasActions::copiedIf(bool ok) {
    if (ok) {
        Q_EMIT copied();
        if (!rules.copiedText.isEmpty()) {
            Q_EMIT notice(rules.copiedText);
        }
    }
    return ok;
}

// --- selection ------------------------------------------------------------------------------------------------------

bool CanvasActions::hasSelection() const { return shown && (shown->getSelection() || shown->mixed().active()); }
bool CanvasActions::selectMoreOffered() const { return shown && shown->offersSelectMore(); }
bool CanvasActions::selectMoreAvailable() const { return shown && shown->canSelectMore(); }
bool CanvasActions::selectingMore() const { return shown && shown->selectingMore(); }
void CanvasActions::setSelectingMore(bool on) {
    if (shown) {
        shown->setSelectingMore(on);  // (only where it may be written in: CanvasView::canSelectMore)
    }
}
int CanvasActions::selectedCount() const { return shown ? shown->selectedCount() : 0; }
bool CanvasActions::canGroup() const { return shown && !readingOnly() && shown->groupState().canGroup; }
bool CanvasActions::canUngroup() const { return shown && !readingOnly() && shown->groupState().canUngroup; }
bool CanvasActions::groupSelection() { return shown && !readingOnly() && shown->groupSelection(); }
bool CanvasActions::ungroupSelection() { return shown && !readingOnly() && shown->ungroupSelection(); }

bool CanvasActions::copySelection() { return copiedIf(shown && shown->copySelection()); }  // (also a selected note)

bool CanvasActions::cutSelection() {
    if (!shown || readingOnly()) {
        return false;
    }
    const bool ok = shown->cutSelection();
    if (ok) {
        Q_EMIT copied();
    }
    return ok;
}

void CanvasActions::deleteSelection() {
    if (shown && !readingOnly()) {
        shown->deleteSelection();
    }
}

bool CanvasActions::pasteElements() { return shown && !readingOnly() && !pagesFixed() && shown->pasteElements(); }

bool CanvasActions::pasteAt(qreal x, qreal y) {
    return shown && !readingOnly() && !pagesFixed() &&
           shown->pasteElements(shown->getViewController().screenToView(QPointF(x, y)));
}

bool CanvasActions::canPaste() const {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    return mime && (mime->hasImage() || mime->hasText() || mime->hasFormat("application/xournal") ||
                    StickyNotes::clipboardHasNote() || MixedSelection::clipboardHas());
}

void CanvasActions::selectAllOnPage() {
    if (shown) {
        chooseSelectTool(TOOL_SELECT_REGION);  // (so that the selection can be moved right away)
        shown->selectAllOnPage();
    }
}

bool CanvasActions::insertImage(const QUrl& url) {
    if (!shown || readingOnly() || pagesFixed()) {
        return false;
    }
    QFile f(ContentFiles::sourceOf(url));  // (Android's picker: a content:// URI, which Qt reads too)
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    chooseSelectTool(TOOL_SELECT_RECT);  // (so that the image can be moved and resized right away)
    if (!shown->insertImage(f.readAll())) {
        Q_EMIT message(tr("Insert image"), tr("\"%1\" is not an image that can be read.").arg(url.fileName()), true);
        return false;
    }
    return true;
}

void CanvasActions::clearSelection() {
    if (shown) {
        shown->clearPdfTextSelection();
        if (shown->hasAnySelection()) {
            shown->clearSelection();  // (elements, a note, several notes)
        }
    }
}

// --- sticky notes ---------------------------------------------------------------------------------------------------

bool CanvasActions::noteSelected() const { return shown && shown->notes().hasSelection(); }

QColor CanvasActions::noteColor() const {
    if (const auto look = shown ? shown->notes().selectedLook() : std::nullopt) {
        return QColor(look->color.red, look->color.green, look->color.blue);
    }
    return {};
}

void CanvasActions::setNoteColor(const QColor& color) {
    if (shown && !readingOnly()) {
        shown->notes().setColor(Color(static_cast<uint8_t>(color.red()), static_cast<uint8_t>(color.green()),
                                      static_cast<uint8_t>(color.blue())));
    }
}

bool CanvasActions::noteCovers() const {
    const auto look = shown ? shown->notes().selectedLook() : std::nullopt;
    return look && look->cover;
}

void CanvasActions::setNoteCovers(bool covers) {
    if (shown && !readingOnly()) {
        shown->notes().setCover(covers);
    }
}

QRectF CanvasActions::noteBox() const {
    return shown ? shown->getViewController().viewToScreen(shown->notes().selectedViewBox()) : QRectF();
}

bool CanvasActions::writeNoteText() {
    if (!shown || readingOnly() || pagesFixed()) {
        return false;
    }
    if (rules.beforeWritingNote) {
        rules.beforeWritingNote();  // (the Markdown written beside the page is done first)
    }
    const bool writing = shown->writeNoteText();
    Q_EMIT noteTextStarted();
    return writing;
}

bool CanvasActions::copyStickyNote() {
    return copiedIf(shown && (shown->mixed().active() ? shown->mixed().copy() : shown->notes().copySelected()));
}

bool CanvasActions::cutStickyNote() {
    if (!shown || readingOnly() || pagesFixed()) {
        return false;
    }
    const bool ok = shown->mixed().active() ? shown->mixed().cut() : shown->notes().cutSelected();
    if (ok) {
        Q_EMIT copied();
    }
    return ok;
}

void CanvasActions::deleteStickyNote() {
    if (shown && !readingOnly()) {
        shown->notes().deleteSelected();
    }
}

// --- PDF text -------------------------------------------------------------------------------------------------------

bool CanvasActions::pdfTextIsSelected() const { return shown && shown->hasPdfTextSelection(); }

// (on the screen: the canvas item's coordinates, the canvas may be turned)
QRectF CanvasActions::pdfSelectionEnds() const {
    return shown ? shown->getViewController().viewToScreenEnds(shown->pdfSelectionEnds()) : QRectF();
}

QRectF CanvasActions::pdfSelectionBox() const {
    return shown ? shown->getViewController().viewToScreen(shown->pdfSelectionBox()) : QRectF();
}

bool CanvasActions::selectPdfTextAt(qreal x, qreal y) {
    if (!shown) {
        return false;
    }
    // The same word again: its whole line (like a phone widens the selection)
    const QPointF where = shown->getViewController().screenToView(QPointF(x, y));
    const bool again =
            shown->hasPdfTextSelection() && shown->pdfSelectionEnds().adjusted(-8, -8, 8, 8).contains(where);
    const bool selected = shown->selectPdfTextAt(where, again);
    Q_EMIT pdfTextSelectionChanged();
    return selected;
}

bool CanvasActions::dragPdfSelection(qreal x, qreal y, bool startEnd) {
    const bool moved =
            shown && shown->dragPdfSelection(shown->getViewController().screenToView(QPointF(x, y)), startEnd);
    if (moved) {
        Q_EMIT pdfTextSelectionChanged();
    }
    return moved;
}

void CanvasActions::showPdfSelection() {
    if (shown) {
        shown->scrollToPdfSelection();
    }
}

bool CanvasActions::markPdfText(const QString& mode) {
    return shown && !readingOnly() && shown->markPdfText(pdfModeFrom(mode));
}

bool CanvasActions::copyPdfText() {
    if (!shown) {
        return false;
    }
    if (!shown->getSession().allowsCopying()) {
        // A PDF whose owner does not allow copying its text (opened without its owner password): honoured
        Q_EMIT notice(tr("The author of this PDF does not allow copying its text"));
        return false;
    }
    if (!shown->copyPdfText()) {
        return false;
    }
    shown->clearPdfTextSelection();
    Q_EMIT copied();
    if (!rules.textCopiedText.isEmpty()) {
        Q_EMIT notice(rules.textCopiedText);
    }
    return true;
}

void CanvasActions::clearPdfTextSelection() {
    if (shown) {
        shown->clearPdfTextSelection();
    }
}

QString CanvasActions::selectedText() const { return shown ? shown->selectedText() : QString(); }

// --- the view -------------------------------------------------------------------------------------------------------

bool CanvasActions::canGoBack() const { return shown && shown->canGoBack(); }
bool CanvasActions::canGoForward() const { return shown && shown->canGoForward(); }

void CanvasActions::navigateBack() {
    if (shown) {
        shown->navigateBack();
    }
}

void CanvasActions::navigateForward() {
    if (shown) {
        shown->navigateForward();
    }
}

int CanvasActions::pageNumber() const { return shown ? static_cast<int>(shown->currentPageNo()) + 1 : 0; }
int CanvasActions::pageCount() const { return shown ? static_cast<int>(shown->pageCount()) : 0; }

void CanvasActions::goToPage(int index) {
    if (shown && index >= 0 && static_cast<size_t>(index) < shown->pageCount()) {
        shown->jumpToPage(static_cast<size_t>(index));
    }
}

int CanvasActions::zoomPercent() const {
    if (!shown) {
        return 100;
    }
    const auto& vc = shown->getViewController();
    return static_cast<int>(std::lround(vc.zoom() / vc.zoom100() * 100.0));
}

void CanvasActions::zoomIn() {
    if (shown) {
        auto& vc = shown->getViewController();
        vc.zoomBy(1.2, middleOf(vc));
    }
}

void CanvasActions::zoomOut() {
    if (shown) {
        auto& vc = shown->getViewController();
        vc.zoomBy(1 / 1.2, middleOf(vc));
    }
}

void CanvasActions::zoomToRealSize() {
    if (shown) {
        auto& vc = shown->getViewController();
        vc.setZoom(vc.zoom100(), middleOf(vc));
    }
}

void CanvasActions::fitWidth() {
    if (shown) {
        shown->resetRotation();
        // The width of its current page (ViewController::fitWidthZoom), not of its widest one
        shown->getViewController().fitWidth(shown->currentPageNo());
    }
}

}  // namespace xqt
