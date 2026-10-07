#include "ReferenceMode.h"

#include "session/DocumentLink.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <QUrl>

#include "control/settings/Settings.h"

#include "CanvasActions.h"
#include "CanvasView.h"
#include "session/AppContext.h"
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
    // What acts on its canvas: as on the notes, changes only while it is written in
    CanvasActions::Policy policy;
    policy.readingOnly = [this] { return !editing(); };
    policy.copiedText = tr("Copied from the reference");
    policy.textCopiedText = tr("Text copied from the reference");
    edits = std::make_unique<CanvasActions>(std::move(policy));
    connect(edits.get(), &CanvasActions::notice, this, &ReferenceMode::copied);
    // (hasSelection: elements, notes, PDF text)
    connect(edits.get(), &CanvasActions::selectionChanged, this, &ReferenceMode::selectionChanged);
    connect(edits.get(), &CanvasActions::pdfTextSelectionChanged, this, &ReferenceMode::selectionChanged);
    // (written in or not: what may be done with the selection)
    connect(this, &ReferenceMode::changed, edits.get(), &CanvasActions::selectionChanged);
    connect(&lock, &ScrollLock::lockedChanged, this, &ReferenceMode::scrollLockChanged);
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
    edits->setView(nullptr);
    pages->setSession(nullptr);
}

QObject* ReferenceMode::pagesModel() const { return pages.get(); }
QObject* ReferenceMode::editObject() const { return edits.get(); }

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
    CanvasView* v = tabs.referenceView(tabs.currentIndex());
    DocumentSession* s = index >= 0 ? tabs.session(index) : nullptr;
    if (v == shownView && s == shownSession) {
        relock();          // (the main view may be another one)
        Q_EMIT changed();  // (the same document; its tab may have another index now)
        return;
    }
    for (auto& c: connections) {
        disconnect(c);
    }
    connections.clear();
    if (shownView) {
        shownView->clearPdfTextSelection();  // (what was selected to copy; the selection of elements stays)
        shownView->setSelectingMore(false);  // (select more ends with the view shown)
    }
    shownView = v;
    shownSession = s;
    if (!s) {
        setPagesShown(false);
    } else if (gridShown) {
        pages->setSession(s);
    }
    if (v && s) {
        connections.push_back(connect(s, &DocumentSession::filePathChanged, this, &ReferenceMode::changed));
        connections.push_back(
                connect(s, &DocumentSession::undoRedoStateChanged, this, &ReferenceMode::undoRedoChanged));
        connections.push_back(connect(v, &CanvasView::markdownUndoChanged, this, &ReferenceMode::undoRedoChanged));
        connections.push_back(connect(v, &CanvasView::textEditingChanged, this, &ReferenceMode::undoRedoChanged));
        // Links: offered as on the main canvas (a tap can be a mistake); followLink goes there
        connections.push_back(connect(v, &CanvasView::linkTapped, this, &ReferenceMode::linkTapped));
        // A long press or right click: the window offers what can be done there
        connections.push_back(connect(v, &CanvasView::contextRequested, this, &ReferenceMode::contextRequested));
        // The text tool on a Markdown text (only while the reference is written in: else the tool scrolls)
        connections.push_back(connect(v, &CanvasView::markdownRequested, this, &ReferenceMode::markdownRequested));
        connections.push_back(connect(v, &CanvasView::markdownBoxRequested, this, &ReferenceMode::markdownBoxRequested));
        connections.push_back(connect(v, &CanvasView::snipped, this,
                                      [this, v](const QImage& image, int page, const QRectF& area, bool capped) {
                                          Q_EMIT snipped(v, image, page, area, capped);
                                      }));
        connections.push_back(connect(v, &CanvasView::snipLinkOffered, this,
                                      [this, v](const QString& title) { Q_EMIT snipLinkOffered(v, title); }));
        connections.push_back(connect(v, &CanvasView::inkSwept, this,
                                      [this, v](int page, const QPolygonF& path) { Q_EMIT inkSwept(v, page, path); }));
    }
    edits->setView(v && s ? v : nullptr);
    relock();
    Q_EMIT changed();
    Q_EMIT selectionChanged();
    if (!active()) {
        setFocused(false);
    }
}

// --- locked scrolling ----------------------------------------------------------------------------------------------

bool ReferenceMode::pairLocked(const DocumentSession* x, const DocumentSession* y) const {
    return std::any_of(lockedPairs.begin(), lockedPairs.end(), [x, y](const auto& p) {
        return (p.first == x && p.second == y) || (p.first == y && p.second == x);
    });
}

void ReferenceMode::relock() {
    if (placing) {
        return;
    }
    CanvasView* main = tabs.currentView();
    DocumentSession* mainSession = tabs.currentSession();
    if (!main || !shownView || !shownSession || !pairLocked(mainSession, shownSession)) {
        lock.unlock();
        return;
    }
    if (lock.first() != &main->getViewController() || lock.second() != &shownView->getViewController()) {
        // (the offset: where the two are now; they were kept together while shown)
        lock.lock(&main->getViewController(), &shownView->getViewController());
    }
}

bool ReferenceMode::scrollLocked() const { return lock.locked(); }

void ReferenceMode::setScrollLocked(bool on) {
    DocumentSession* mainSession = tabs.currentSession();
    if (!active() || !mainSession || !shownSession || on == scrollLocked()) {
        return;
    }
    // (forget the pairs of documents that were closed)
    lockedPairs.erase(std::remove_if(lockedPairs.begin(), lockedPairs.end(),
                                     [](const auto& p) { return !p.first || !p.second; }),
                      lockedPairs.end());
    if (on) {
        lockedPairs.emplace_back(mainSession, shownSession);
    } else {
        lockedPairs.erase(std::remove_if(lockedPairs.begin(), lockedPairs.end(),
                                         [&](const auto& p) {
                                             return (p.first == mainSession && p.second == shownSession) ||
                                                    (p.first == shownSession && p.second == mainSession);
                                         }),
                          lockedPairs.end());
    }
    relock();
}

bool ReferenceMode::editable() const { return shownSession && !shownSession->isReadOnly(); }

QObject* ReferenceMode::view() const { return shownView.data(); }
CanvasView* ReferenceMode::canvas() const { return shownView.data(); }
bool ReferenceMode::active() const { return !shownView.isNull(); }
bool ReferenceMode::focused() const { return focus && active(); }
bool ReferenceMode::editing() const {
    return active() && tabs.referenceEditable(tabs.currentIndex()) && editable();
}

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

void ReferenceMode::setFocused(bool on) {
    if (on != focus) {
        focus = on;
        Q_EMIT focusedChanged();
    }
}

bool ReferenceMode::hasSelection() const {
    // (elements, or several notes with elements, or PDF text; a single note has its own pill, as on the notes)
    return edits->hasSelection() || edits->pdfTextIsSelected();
}

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
    if (index < 0 || index >= tabs.count() || current < 0) {
        return;
    }
    const bool anew = tabs.referenceOf(current) != index;
    placing = true;  // (a pair locked before is locked again once it is placed: its fit does not zoom the notes)
    tabs.setReference(current, index);
    // Shown anew in the narrower half: its width fits (the window gave the canvas its size meanwhile). Switching
    // tabs or roles keeps the zoom (and the rendered pages).
    if (anew && shownView) {
        // (sideways: the height; else the width of its page in view)
        shownView->getViewController().fitDefault(shownView->currentPageNo());
    }
    placing = false;
    relock();
}

void ReferenceMode::close() {
    if (tabs.currentIndex() >= 0) {
        tabs.setReference(tabs.currentIndex(), -1);
    }
}

void ReferenceMode::swapRoles() {
    if (active()) {
        setFocused(false);  // (the keys are for the main document, now the other one)
        // (the views of a document beside itself exchange their places: not followed by each other meanwhile; the
        // pair is taken again afterwards)
        lock.unlock();
        tabs.swapReference();
        relock();
    }
}

void ReferenceMode::popOut() {
    const int notes = tabs.currentIndex();
    const int reference = tabs.referenceOf(notes);
    if (reference < 0) {
        return;
    }
    if (reference == notes) {
        // The same document: it has one tab. The tab goes to the place of the reference (Back returns), and the
        // split closes.
        setFocused(false);
        lock.unlock();  // (the place of the reference goes to the tab: not followed by the reference)
        if (CanvasView* main = tabs.view(notes); main && shownView) {
            main->swapPlacesWith(*shownView);
        }
        tabs.setReference(notes, -1);
        return;
    }
    DocumentSession* shown = tabs.session(reference);
    setFocused(false);
    tabs.setReference(notes, -1);
    // Right after the notes; beside them already (before or after): one step away as it is
    if (std::abs(reference - notes) != 1) {
        tabs.moveTab(reference, reference > notes ? notes + 1 : notes);
    }
    tabs.setCurrentIndex(tabs.indexOf(shown));
}

bool ReferenceMode::isSelf() const { return active() && tabs.isSelfReference(tabs.currentIndex()); }

void ReferenceMode::showBeside(int page) {
    const int current = tabs.currentIndex();
    if (current < 0) {
        return;
    }
    if (tabs.isSelfReference(current)) {
        edits->goToPage(page);  // (Back returns to where it was)
        return;
    }
    showTab(current);  // (where the tab is)
    if (shownView && page >= 0 && static_cast<size_t>(page) < shownView->pageCount()) {
        // A new view opens there
        shownView->setCurrentPageNo(static_cast<size_t>(page));
        shownView->getViewController().scrollToPage(static_cast<size_t>(page));
    }
}

void ReferenceMode::followLink(const QString& uri, int page) {
    const bool wiki = uri.startsWith(QLatin1String("[[")) && uri.endsWith(QLatin1String("]]"));
    if (!uri.isEmpty() && (wiki || links::isDocumentLink(uri))) {
        Q_EMIT openDocumentLink(uri, shownSession ? QString::fromStdString(shownSession->documentFile().string())
                                                  : QString());
    } else if (!uri.isEmpty()) {
        Q_EMIT openExternal(uri);
    } else {
        edits->goToPage(page);
    }
}

bool ReferenceMode::copy() {
    if (edits->pdfTextIsSelected()) {
        return edits->copyPdfText();  // (unselected once copied; refused where the PDF does not allow it)
    }
    if (edits->copySelection()) {
        shownView->clearSelection();  // (copied: the elements go back where they were)
        return true;
    }
    return false;
}

QString ReferenceMode::shownFile() const {
    return shownSession ? QString::fromStdString(shownSession->documentFile().string()) : QString();
}

}  // namespace xqt
