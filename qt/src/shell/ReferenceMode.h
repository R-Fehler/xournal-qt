/*
 * xournal-qt: reference mode - a second document beside the current one, in the same tab, for reading while writing.
 *
 * The reference is another open tab (TabManager::Tab::reference, one per tab): the window shows its CanvasView in a
 * second canvas item, beside the current tab's, behind a movable divider. It may be the tab's own document
 * (qt/self-reference): then a second CanvasView of the same DocumentSession (TabManager::Tab::selfView) with its own
 * page, zoom, selection and way back; edits show on both sides, there is one undo history, and the rendered pages of
 * both views share CanvasMemory's limit. See qt/docs/reference-view.md. It is for reading only (the canvas item's
 * readingOnly): it scrolls and zooms, and its elements and PDF text can be selected and copied, to paste them into
 * the notes. Where the divider is (the share of the main document) and on which side the reference is are settings
 * of the application, the same for every tab.
 *
 * What acts on the reference's canvas (its selection, notes, PDF text, the clipboard, its page and zoom) is `edit`,
 * the same CanvasActions the notes have (`app.edit`): the pills of a canvas take either as their target.
 *
 * Keys: the window's shortcuts act on the main document; while the reference has the focus (a tap on it or on its
 * pill), copying, zooming, fitting the width and going back and forth act on the reference (AppController); while it
 * is also written in (the edit switch), undo, redo, cut, paste, delete and select all as well.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QColor>
#include <QImage>
#include <QMetaObject>
#include <QObject>
#include <QPointF>
#include <QPolygonF>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QUrl>

#include <memory>
#include <utility>
#include <vector>

#include "ScrollLock.h"

class Settings;

namespace xqt {

class CanvasActions;
class CanvasView;
class DocumentSession;
class PagesModel;
class TabManager;

class ReferenceMode final: public QObject {
    Q_OBJECT
    /// The CanvasView of the current tab's reference (nullptr: none).
    Q_PROPERTY(QObject* view READ view NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    /// Its tab (-1: none)
    Q_PROPERTY(int tab READ tab NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    /// The reference is the current tab's own document, in a second view (qt/self-reference)
    Q_PROPERTY(bool self READ isSelf NOTIFY changed)
    /// What acts on the reference's canvas: selection, notes, PDF text, clipboard, page, zoom, Back (CanvasActions,
    /// the same as the notes' `app.edit`; changes only while the reference is written in)
    Q_PROPERTY(QObject* edit READ editObject CONSTANT)
    /// The reference is written in (the edit switch of its pill, per tab; off: for reading only).
    Q_PROPERTY(bool editing READ editing WRITE setEditing NOTIFY changed)
    /// The reference has the keyboard focus (set by the window: a tap on it or on its pill).
    Q_PROPERTY(bool focused READ focused WRITE setFocused NOTIFY focusedChanged)
    /// Something to copy is selected in the reference: elements, notes or PDF text (its pill's Copy).
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    /// The pages of the reference, for its page grid (xqt::PagesModel; the page sidebar keeps the main document's).
    /// It follows the reference only while the grid is shown (pagesShown): its sketches come first then.
    Q_PROPERTY(QObject* pages READ pagesModel CONSTANT)
    Q_PROPERTY(bool pagesShown READ pagesShown WRITE setPagesShown NOTIFY pagesShownChanged)
    /// The share of the width for the main document (the divider), 0.2 ... 0.8.
    Q_PROPERTY(double ratio READ ratio WRITE setRatio NOTIFY layoutChanged)
    /// The reference is on the left of the main document (else on the right).
    Q_PROPERTY(bool onLeft READ onLeft WRITE setOnLeft NOTIFY layoutChanged)
    /// Locked scrolling (ScrollLock): scrolling, paging and zooming either side moves the other, by page with the
    /// offset of when it was locked. Remembered per pair of documents for the session (also a document beside itself).
    Q_PROPERTY(bool scrollLocked READ scrollLocked WRITE setScrollLocked NOTIFY scrollLockChanged)
    /// The reference can be written in (not a version of a document cut out of its file, nothing read-only): the edit
    /// switch is offered
    Q_PROPERTY(bool editable READ editable NOTIFY changed)
public:
    static constexpr double MIN_RATIO = 0.2;
    static constexpr double MAX_RATIO = 0.8;

    ReferenceMode(TabManager& tabs, Settings* settings, QObject* parent = nullptr);
    ~ReferenceMode() override;

    QObject* view() const;
    QObject* pagesModel() const;
    bool pagesShown() const { return gridShown; }
    void setPagesShown(bool shown);
    CanvasView* canvas() const;
    /// What acts on the reference's canvas (CanvasActions)
    CanvasActions& actions() const { return *edits; }
    QObject* editObject() const;
    bool active() const;
    bool isSelf() const;
    int tab() const;
    QString title() const;
    bool focused() const;
    bool editing() const;
    void setEditing(bool on);
    void setFocused(bool on);
    bool hasSelection() const;
    double ratio() const;
    void setRatio(double ratio);
    bool onLeft() const;
    void setOnLeft(bool left);
    bool scrollLocked() const;
    void setScrollLocked(bool on);
    Q_INVOKABLE void toggleScrollLock() { setScrollLocked(!scrollLocked()); }
    /// The two views kept together (the main view and the reference's), while locked
    ScrollLock& scrollLock() { return lock; }
    bool editable() const;

    /// Show the document of tab `index` beside the current tab's. The current tab itself: its own document, in a
    /// second view with a page, a zoom and a selection of its own (qt/self-reference), starting where the tab is.
    Q_INVOKABLE void showTab(int index);
    /// "Show this document beside" (the page menu, a link to a page of it): the current tab's own document as its
    /// reference (if it is not already), at `page` (-1: where the tab is).
    Q_INVOKABLE void showBeside(int page = -1);
    /// The current tab shows no reference any more (its tab stays open).
    Q_INVOKABLE void close();
    Q_INVOKABLE void swapSides() { setOnLeft(!onLeft()); }
    /// The reference becomes the main document of the tab, and the main document its reference.
    Q_INVOKABLE void swapRoles();
    /// "Show as a tab": the reference's tab moves right after the current tab (unless it is beside it already), the
    /// split closes (the pair is not kept), and the reference's tab becomes the current one. Ctrl+Tab and
    /// Ctrl+Shift+Tab then go between the two. The tab's own document (it has one tab): the tab goes to the place of
    /// the reference (Back returns), and the split closes.
    Q_INVOKABLE void popOut();
    /// Follow a link tapped in the reference: a page of it, a link to a document (openDocumentLink), or an external
    /// link (openExternal).
    Q_INVOKABLE void followLink(const QString& uri, int page);
    /// Copy what is selected in the reference (PDF text, else elements; then unselected). False if nothing is
    /// selected (or the PDF does not allow copying its text).
    Q_INVOKABLE bool copy();
    /// The file of the document shown ("" without one).
    QString shownFile() const;
    /// Undo / redo in the reference (while it is written in).
    void undo();
    void redo();

Q_SIGNALS:
    void changed();
    void focusedChanged();
    /// What hasSelection says may be different
    void selectionChanged();
    void layoutChanged();
    void pagesShownChanged();
    void scrollLockChanged();
    /// What undo and redo can do in the reference changed (its document's history, or the Markdown written in it).
    void undoRedoChanged();
    /// A link was tapped in the reference: uri (external) or page of the reference; rect in its canvas coordinates.
    void linkTapped(const QString& uri, int page, QRectF rect);
    /// An external link should be opened (AppController::openLink).
    void openExternal(const QString& uri);
    /// A link to a document was tapped in the reference, which holds it in `from` (AppController follows it).
    void openDocumentLink(const QString& uri, const QString& from);
    /// Something was copied from the reference, or could not be (the window says so).
    void copied(const QString& what);
    /// A long press or right click on the reference (its canvas coordinates): the window offers what fits.
    void contextRequested(QPointF viewPos);
    /// The text tool on a Markdown text of the reference (while it is written in), as CanvasView's signals.
    void markdownRequested(int page);
    void markdownBoxRequested(int page, double x, double y);
    /// The snip tool in the reference (CanvasView::snipped, snipLinkOffered), with the view
    void snipped(xqt::CanvasView* view, const QImage& image, int page, const QRectF& area, bool capped);
    void snipLinkOffered(xqt::CanvasView* view, const QString& title);
    /// "Copy handwriting as text" swept over the reference (CanvasView::inkSwept), with the view
    void inkSwept(xqt::CanvasView* view, int page, const QPolygonF& path);

private:
    /// The current tab or its reference changed: follow the reference's view.
    void update();
    /// Lock the main view and the reference's when their pair is locked, else unlock (nothing while showTab places
    /// a new reference)
    void relock();
    bool pairLocked(const DocumentSession* x, const DocumentSession* y) const;

    TabManager& tabs;
    Settings* settings;
    QPointer<CanvasView> shownView;
    std::unique_ptr<PagesModel> pages;
    std::unique_ptr<CanvasActions> edits;
    DocumentSession* shownSession = nullptr;
    std::vector<QMetaObject::Connection> connections;
    bool focus = false;
    bool gridShown = false;
    ScrollLock lock;
    /// The pairs of documents locked this session (in either order; a document with itself: beside itself)
    std::vector<std::pair<QPointer<DocumentSession>, QPointer<DocumentSession>>> lockedPairs;
    bool placing = false;  ///< showTab places a new reference (not locked yet)
};

}  // namespace xqt
