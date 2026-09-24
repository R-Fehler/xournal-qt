#include "ReferenceMode.h"

#include <algorithm>
#include <cmath>

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QMimeData>
#include <QUrl>

#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "control/settings/Settings.h"

#include "CanvasView.h"
#include "PageSketches.h"
#include "PagesModel.h"
#include "Thumbnails.h"
#include "TabManager.h"
#include "session/DocumentSession.h"
#include "undo/UndoRedoHandler.h"

namespace xqt {

namespace {
const char* const CUSTOM = "xournalQt";  // our settings (in upstream's settings file)
}  // namespace

ReferenceMode::ReferenceMode(TabManager& tabs, Settings* settings, QObject* parent):
        QObject(parent), tabs(tabs), settings(settings), pages(std::make_unique<PagesModel>()) {
    connect(&tabs, &TabManager::currentTabChanged, this, &ReferenceMode::update);
    connect(&tabs, &TabManager::referencesChanged, this, &ReferenceMode::update);
    connect(&tabs, &TabManager::countChanged, this, &ReferenceMode::update);
    // (the tab of the reference may be at another place now)
    connect(&tabs, &TabManager::currentIndexChanged, this, &ReferenceMode::update);
    connect(&tabs, &QAbstractItemModel::rowsMoved, this, &ReferenceMode::changed);
    update();
}

ReferenceMode::~ReferenceMode() {
    for (auto& c: connections) {
        disconnect(c);
    }
    pages->setSession(nullptr);
}

QObject* ReferenceMode::pagesModel() const { return pages.get(); }

void ReferenceMode::setPagesShown(bool shown) {
    shown = shown && shownSession;
    if (shown == gridShown) {
        return;
    }
    gridShown = shown;
    pages->setSession(shown ? shownSession : nullptr);
    if (!shown && tabs.currentSession()) {
        // (the grid made the reference's previews come first; the main document's again)
        PageSketches::instance().focus(ThumbnailProvider::idOf(tabs.currentSession()));
    }
    Q_EMIT pagesShownChanged();
}

void ReferenceMode::update() {
    const int index = tabs.referenceOf(tabs.currentIndex());
    CanvasView* v = index >= 0 ? tabs.view(index) : nullptr;
    DocumentSession* s = index >= 0 ? tabs.session(index) : nullptr;
    if (v == shownView && s == shownSession) {
        Q_EMIT changed();  // (the same document; its tab may have another index now)
        return;
    }
    for (auto& c: connections) {
        disconnect(c);
    }
    connections.clear();
    if (shownView) {
        shownView->clearPdfTextSelection();  // (what was selected to copy; the selection of elements stays)
    }
    shownView = v;
    shownSession = s;
    if (!s) {
        setPagesShown(false);
    } else if (gridShown) {
        pages->setSession(s);
    }
    if (v && s) {
        connections.push_back(connect(s, &DocumentSession::currentPageChanged, this, &ReferenceMode::pageChanged));
        connections.push_back(connect(s, &DocumentSession::filePathChanged, this, &ReferenceMode::changed));
        connections.push_back(connect(v, &CanvasView::pagesChanged, this, &ReferenceMode::pageChanged));
        connections.push_back(connect(&v->getViewController(), &ViewController::zoomChanged, this,
                                      &ReferenceMode::zoomChanged));
        connections.push_back(connect(v, &CanvasView::selectionChanged, this, &ReferenceMode::selectionChanged));
        connections.push_back(connect(v, &CanvasView::pdfTextSelected, this, &ReferenceMode::selectionChanged));
        connections.push_back(connect(v, &CanvasView::pdfTextSelectionCleared, this, &ReferenceMode::selectionChanged));
        connections.push_back(connect(v, &CanvasView::pdfTextSelected, this, &ReferenceMode::pdfTextSelected));
        connections.push_back(connect(v, &CanvasView::pdfTextSelected, this, &ReferenceMode::pdfTextSelectionChanged));
        connections.push_back(
                connect(v, &CanvasView::pdfTextSelectionCleared, this, &ReferenceMode::pdfTextSelectionChanged));
        connections.push_back(connect(v, &CanvasView::navigationChanged, this, &ReferenceMode::navigationChanged));
        // Links: offered as on the main canvas (a tap can be a mistake); followLink goes there
        connections.push_back(connect(v, &CanvasView::linkTapped, this, &ReferenceMode::linkTapped));
        // A long press or right click: the window offers what can be done there
        connections.push_back(connect(v, &CanvasView::contextRequested, this, &ReferenceMode::contextRequested));
        // The text tool on a Markdown text (only while the reference is written in: else the tool scrolls)
        connections.push_back(connect(v, &CanvasView::markdownRequested, this, &ReferenceMode::markdownRequested));
        connections.push_back(connect(v, &CanvasView::markdownBoxRequested, this, &ReferenceMode::markdownBoxRequested));
    }
    Q_EMIT changed();
    Q_EMIT pageChanged();
    Q_EMIT zoomChanged();
    Q_EMIT selectionChanged();
    Q_EMIT pdfTextSelectionChanged();
    Q_EMIT navigationChanged();
    if (!active()) {
        setFocused(false);
    }
}

QObject* ReferenceMode::view() const { return shownView.data(); }
CanvasView* ReferenceMode::canvas() const { return shownView.data(); }
bool ReferenceMode::active() const { return !shownView.isNull(); }
bool ReferenceMode::focused() const { return focus && active(); }
bool ReferenceMode::editing() const { return active() && tabs.referenceEditable(tabs.currentIndex()); }

void ReferenceMode::setEditing(bool on) {
    if (active()) {
        tabs.setReferenceEditable(tabs.currentIndex(), on);  // (update() tells the window)
    }
}

void ReferenceMode::undo() {
    if (shownSession && editing() && shownSession->getUndoRedoHandler()->canUndo()) {
        shownSession->clearSelectionEndText();  // first: finishing a text edit is itself an undo step
        shownSession->getUndoRedoHandler()->undo();
    }
}

void ReferenceMode::redo() {
    if (shownSession && editing() && shownSession->getUndoRedoHandler()->canRedo()) {
        shownSession->clearSelectionEndText();
        shownSession->getUndoRedoHandler()->redo();
    }
}

int ReferenceMode::tab() const { return shownSession ? tabs.indexOf(shownSession) : -1; }

QString ReferenceMode::title() const {
    return shownSession ? QString::fromStdString(shownSession->getDisplayName()) : QString();
}

int ReferenceMode::pageNumber() const {
    return shownSession && shownView ? static_cast<int>(shownSession->getCurrentPageNo()) + 1 : 0;
}

int ReferenceMode::pageCount() const { return shownView ? static_cast<int>(shownView->pageCount()) : 0; }

int ReferenceMode::zoomPercent() const {
    if (!shownView) {
        return 100;
    }
    const auto& vc = shownView->getViewController();
    return static_cast<int>(std::lround(vc.zoom() / vc.zoom100() * 100.0));
}

void ReferenceMode::setFocused(bool on) {
    if (on != focus) {
        focus = on;
        Q_EMIT focusedChanged();
    }
}

bool ReferenceMode::hasSelection() const {
    return shownView && (shownView->getSelection() || shownView->hasPdfTextSelection());
}

bool ReferenceMode::canGoBack() const { return shownView && shownView->canGoBack(); }
bool ReferenceMode::canGoForward() const { return shownView && shownView->canGoForward(); }

double ReferenceMode::ratio() const {
    double r = 0.5;
    settings->getCustomElement(CUSTOM).getDouble("referenceRatio", r);
    return std::isfinite(r) ? std::clamp(r, MIN_RATIO, MAX_RATIO) : 0.5;
}

void ReferenceMode::setRatio(double r) {
    if (!std::isfinite(r)) {
        return;
    }
    r = std::clamp(r, MIN_RATIO, MAX_RATIO);
    if (r != ratio()) {
        settings->getCustomElement(CUSTOM).setDouble("referenceRatio", r);
        settings->customSettingsChanged();
        Q_EMIT layoutChanged();
    }
}

bool ReferenceMode::onLeft() const {
    // Right-handed by default: the notes on the right, near the writing hand, the reference on the left (the arm
    // does not cover it)
    std::string side = "left";
    settings->getCustomElement(CUSTOM).getString("referenceSide", side);
    return side != "right";
}

void ReferenceMode::setOnLeft(bool left) {
    if (left != onLeft()) {
        settings->getCustomElement(CUSTOM).setString("referenceSide", left ? "left" : "right");
        settings->customSettingsChanged();
        Q_EMIT layoutChanged();
    }
}

void ReferenceMode::showTab(int index) {
    const int current = tabs.currentIndex();
    if (index < 0 || index >= tabs.count() || index == current || current < 0) {
        return;
    }
    const bool anew = tabs.referenceOf(current) != index;
    tabs.setReference(current, index);
    // Shown anew in the narrower half: its width fits (the window gave the canvas its size meanwhile). Switching
    // tabs or roles keeps the zoom (and the rendered pages).
    if (anew && shownView) {
        // (sideways: the height; else the width of its page in view)
        shownView->getViewController().fitDefault(shownView->getSession().getCurrentPageNo());
    }
}

void ReferenceMode::close() {
    if (tabs.currentIndex() >= 0) {
        tabs.setReference(tabs.currentIndex(), -1);
    }
}

void ReferenceMode::swapRoles() {
    if (active()) {
        setFocused(false);  // (the keys are for the main document, now the other one)
        tabs.swapReference();
    }
}

void ReferenceMode::fitWidth() {
    if (shownView) {
        // The width of its current page (ViewController::fitWidthZoom), not of its widest one
        shownView->getViewController().fitWidth(shownView->getSession().getCurrentPageNo());
    }
}

void ReferenceMode::zoomIn() {
    if (shownView) {
        auto& vc = shownView->getViewController();
        vc.zoomBy(1.2, QPointF(vc.viewSize().width() / 2, vc.viewSize().height() / 2));
    }
}

void ReferenceMode::zoomOut() {
    if (shownView) {
        auto& vc = shownView->getViewController();
        vc.zoomBy(1 / 1.2, QPointF(vc.viewSize().width() / 2, vc.viewSize().height() / 2));
    }
}

void ReferenceMode::goToPage(int index) {
    if (shownView && index >= 0 && static_cast<size_t>(index) < shownView->pageCount()) {
        shownView->jumpToPage(static_cast<size_t>(index));
    }
}

void ReferenceMode::navigateBack() {
    if (shownView) {
        shownView->navigateBack();
    }
}

void ReferenceMode::navigateForward() {
    if (shownView) {
        shownView->navigateForward();
    }
}

void ReferenceMode::followLink(const QString& uri, int page) {
    if (!uri.isEmpty()) {
        Q_EMIT openExternal(uri);
    } else {
        goToPage(page);
    }
}

bool ReferenceMode::copy() {
    if (!shownView) {
        return false;
    }
    if (shownView->copyPdfText()) {
        shownView->clearPdfTextSelection();
        Q_EMIT copied(tr("Text copied from the reference"));
        return true;
    }
    if (shownView->copySelection()) {
        shownView->clearSelection();  // (copied: the elements go back where they were)
        Q_EMIT copied(tr("Copied from the reference"));
        return true;
    }
    return false;
}

void ReferenceMode::clearSelection() {
    if (shownView) {
        shownView->clearPdfTextSelection();
        if (shownView->getSelection()) {
            shownView->clearSelection();
        }
    }
}

// --- the selections of the reference, for the same pills as the notes' (the actions of AppController) ------------

bool ReferenceMode::pdfTextIsSelected() const { return shownView && shownView->hasPdfTextSelection(); }

QRectF ReferenceMode::pdfSelectionEnds() const { return shownView ? shownView->pdfSelectionEnds() : QRectF(); }

QRectF ReferenceMode::pdfSelectionBox() const { return shownView ? shownView->pdfSelectionBox() : QRectF(); }

bool ReferenceMode::selectPdfTextAt(qreal x, qreal y) {
    if (!shownView) {
        return false;
    }
    // The same word again: its whole line (as on the notes)
    const QPointF where(x, y);
    const bool again = shownView->hasPdfTextSelection() &&
                       shownView->pdfSelectionEnds().adjusted(-8, -8, 8, 8).contains(where);
    const bool selected = shownView->selectPdfTextAt(where, again);
    Q_EMIT pdfTextSelectionChanged();
    return selected;
}

bool ReferenceMode::dragPdfSelection(qreal x, qreal y, bool startEnd) {
    const bool moved = shownView && shownView->dragPdfSelection(QPointF(x, y), startEnd);
    if (moved) {
        Q_EMIT pdfTextSelectionChanged();
    }
    return moved;
}

void ReferenceMode::showPdfSelection() {
    if (shownView) {
        shownView->scrollToPdfSelection();
    }
}

bool ReferenceMode::markPdfText(const QString& mode) {
    // (for reading only the view marks nothing)
    const CanvasView::PdfTextMode m = mode == "underline"       ? CanvasView::PdfTextMode::Underline
                                      : mode == "strikethrough" ? CanvasView::PdfTextMode::Strikethrough
                                                                : CanvasView::PdfTextMode::Highlight;
    return shownView && editing() && shownView->markPdfText(m);
}

bool ReferenceMode::copyPdfText() {
    const bool ok = shownView && shownView->copyPdfText();
    if (ok) {
        shownView->clearPdfTextSelection();
        Q_EMIT copied(tr("Text copied from the reference"));
    }
    return ok;
}

void ReferenceMode::clearPdfTextSelection() {
    if (shownView) {
        shownView->clearPdfTextSelection();
    }
}

bool ReferenceMode::copySelection() {
    const bool ok = shownView && shownView->copySelection();
    if (ok) {
        Q_EMIT copied(tr("Copied from the reference"));
    }
    return ok;
}

bool ReferenceMode::cutSelection() { return shownView && editing() && shownView->cutSelection(); }

void ReferenceMode::deleteSelection() {
    if (shownView && editing()) {
        shownView->deleteSelection();
    }
}

bool ReferenceMode::pasteElements() {
    return shownView && editing() && !shownSession->isReadOnly() && shownView->pasteElements();
}

bool ReferenceMode::pasteAt(qreal x, qreal y) {
    return shownView && editing() && !shownSession->isReadOnly() && shownView->pasteElements(QPointF(x, y));
}

bool ReferenceMode::canPaste() const {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    return mime && (mime->hasImage() || mime->hasText() || mime->hasFormat("application/xournal"));
}

void ReferenceMode::selectAllOnPage() {
    if (!shownView) {
        return;
    }
    ToolHandler* tools = shownSession->getToolHandler();
    if (tools->getToolType() != TOOL_SELECT_RECT && tools->getToolType() != TOOL_SELECT_REGION) {
        tools->selectTool(TOOL_SELECT_REGION);  // (as on the notes: the selection is at hand)
        tools->fireToolChanged();
    }
    shownView->selectAllOnPage();
}

bool ReferenceMode::insertImage(const QUrl& file) {
    QFile f(file.toLocalFile());
    if (!shownView || !editing() || !f.open(QIODevice::ReadOnly)) {
        return false;
    }
    ToolHandler* tools = shownSession->getToolHandler();
    if (tools->getToolType() != TOOL_SELECT_RECT && tools->getToolType() != TOOL_SELECT_REGION) {
        tools->selectTool(TOOL_SELECT_RECT);  // (so that the image can be moved and resized right away)
        tools->fireToolChanged();
    }
    return shownView->insertImage(f.readAll());
}

}  // namespace xqt
