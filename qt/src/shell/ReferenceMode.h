/*
 * xournal-qt: reference mode - a second document beside the current one, in the same tab, for reading while writing.
 *
 * The reference is another open tab (TabManager::Tab::reference, one per tab): the window shows its CanvasView in a
 * second canvas item, beside the current tab's, behind a movable divider. It is for reading only (the canvas item's
 * readingOnly): it scrolls and zooms, and its elements and PDF text can be selected and copied, to paste them into
 * the notes. Where the divider is (the share of the main document) and on which side the reference is are settings
 * of the application, the same for every tab.
 *
 * Keys: the window's shortcuts act on the main document; while the reference has the focus (a tap on it or on its
 * pill), copying, zooming, fitting the width and going back and forth act on the reference (AppController); while it
 * is also written in (the edit switch), undo, redo, cut, paste, delete and select all as well.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QMetaObject>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QUrl>

#include <memory>
#include <vector>

class Settings;

namespace xqt {

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
    Q_PROPERTY(int pageNumber READ pageNumber NOTIFY pageChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY pageChanged)
    Q_PROPERTY(int zoomPercent READ zoomPercent NOTIFY zoomChanged)
    /// The reference is written in (the edit switch of its pill, per tab; off: for reading only).
    Q_PROPERTY(bool editing READ editing WRITE setEditing NOTIFY changed)
    /// The reference has the keyboard focus (set by the window: a tap on it or on its pill).
    Q_PROPERTY(bool focused READ focused WRITE setFocused NOTIFY focusedChanged)
    /// Elements or PDF text are selected in the reference (to copy them).
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    /// PDF text is selected in the reference (the same name as AppController's: the pills of a canvas take either)
    Q_PROPERTY(bool pdfTextIsSelected READ pdfTextIsSelected NOTIFY pdfTextSelectionChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY navigationChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY navigationChanged)
    /// The pages of the reference, for its page grid (xqt::PagesModel; the page sidebar keeps the main document's).
    /// It follows the reference only while the grid is shown (pagesShown): its previews come first then.
    Q_PROPERTY(QObject* pages READ pagesModel CONSTANT)
    Q_PROPERTY(bool pagesShown READ pagesShown WRITE setPagesShown NOTIFY pagesShownChanged)
    /// The share of the width for the main document (the divider), 0.2 ... 0.8.
    Q_PROPERTY(double ratio READ ratio WRITE setRatio NOTIFY layoutChanged)
    /// The reference is on the left of the main document (else on the right).
    Q_PROPERTY(bool onLeft READ onLeft WRITE setOnLeft NOTIFY layoutChanged)
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
    bool active() const;
    int tab() const;
    QString title() const;
    int pageNumber() const;
    int pageCount() const;
    int zoomPercent() const;
    bool focused() const;
    bool editing() const;
    void setEditing(bool on);
    void setFocused(bool on);
    bool hasSelection() const;
    bool canGoBack() const;
    bool canGoForward() const;
    double ratio() const;
    void setRatio(double ratio);
    bool onLeft() const;
    void setOnLeft(bool left);

    /// Show the document of tab `index` beside the current tab's (the current tab itself: nothing).
    Q_INVOKABLE void showTab(int index);
    /// The current tab shows no reference any more (its tab stays open).
    Q_INVOKABLE void close();
    Q_INVOKABLE void swapSides() { setOnLeft(!onLeft()); }
    /// The reference becomes the main document of the tab, and the main document its reference.
    Q_INVOKABLE void swapRoles();
    /// "Show as a tab": the reference's tab moves right after the current tab (unless it is beside it already), the
    /// split closes (the pair is not kept), and the reference's tab becomes the current one. Ctrl+Tab and
    /// Ctrl+Shift+Tab then go between the two.
    Q_INVOKABLE void popOut();
    Q_INVOKABLE void fitWidth();
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    /// Go to a page of the reference (0-based), remembering the place for "back".
    Q_INVOKABLE void goToPage(int index);
    Q_INVOKABLE void navigateBack();
    Q_INVOKABLE void navigateForward();
    /// Follow a link tapped in the reference: a page of it, a link to a document (openDocumentLink), or an external
    /// link (openExternal).
    Q_INVOKABLE void followLink(const QString& uri, int page);
    /// Copy what is selected in the reference (PDF text, else elements). False if nothing is selected.
    Q_INVOKABLE bool copy();
    Q_INVOKABLE void clearSelection();

    // --- the selections of the reference: the API of AppController that the pills of a canvas use (CanvasPills) ---
    bool pdfTextIsSelected() const;
    Q_INVOKABLE QRectF pdfSelectionEnds() const;
    Q_INVOKABLE QRectF pdfSelectionBox() const;
    Q_INVOKABLE bool selectPdfTextAt(qreal x, qreal y);
    Q_INVOKABLE bool dragPdfSelection(qreal x, qreal y, bool startEnd);
    Q_INVOKABLE void showPdfSelection();
    /// Mark the selected PDF text ("highlight", "underline", "strikethrough"): only while the reference is written in.
    Q_INVOKABLE bool markPdfText(const QString& mode);
    Q_INVOKABLE bool copyPdfText();
    Q_INVOKABLE void clearPdfTextSelection();
    Q_INVOKABLE bool copySelection();
    /// Cut, delete, paste, insert: only while the reference is written in.
    Q_INVOKABLE bool cutSelection();
    Q_INVOKABLE void deleteSelection();
    Q_INVOKABLE bool pasteElements();
    Q_INVOKABLE bool pasteAt(qreal x, qreal y);
    Q_INVOKABLE bool canPaste() const;
    Q_INVOKABLE void selectAllOnPage();
    Q_INVOKABLE bool insertImage(const QUrl& file);
    /// Undo / redo in the reference (while it is written in).
    void undo();
    void redo();

Q_SIGNALS:
    void changed();
    void pageChanged();
    void zoomChanged();
    void focusedChanged();
    void selectionChanged();
    void navigationChanged();
    void layoutChanged();
    void pagesShownChanged();
    /// A link was tapped in the reference: uri (external) or page of the reference; rect in its canvas coordinates.
    void linkTapped(const QString& uri, int page, QRectF rect);
    /// An external link should be opened (AppController::openLink).
    void openExternal(const QString& uri);
    /// A link to a document was tapped in the reference, which holds it in `from` (AppController follows it).
    void openDocumentLink(const QString& uri, const QString& from);
    /// Something was copied from the reference (the window says so).
    void copied(const QString& what);
    /// PDF text was selected or unselected in the reference; selected: where (its canvas coordinates).
    void pdfTextSelectionChanged();
    void pdfTextSelected(QRectF rect);
    /// (not emitted: the pills of a canvas listen to it on AppController)
    void pdfTextModeChanged();
    /// A long press or right click on the reference (its canvas coordinates): the window offers what fits.
    void contextRequested(QPointF viewPos);
    /// The text tool on a Markdown text of the reference (while it is written in), as CanvasView's signals.
    void markdownRequested(int page);
    void markdownBoxRequested(int page, double x, double y);

private:
    /// The current tab or its reference changed: follow the reference's view.
    void update();

    TabManager& tabs;
    Settings* settings;
    QPointer<CanvasView> shownView;
    std::unique_ptr<PagesModel> pages;
    DocumentSession* shownSession = nullptr;
    std::vector<QMetaObject::Connection> connections;
    bool focus = false;
    bool gridShown = false;
};

}  // namespace xqt
