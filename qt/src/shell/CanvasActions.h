/*
 * xournal-qt: what acts on one canvas - its selection, sticky notes, PDF text, the clipboard, the zoom and the way
 * back - for the pills of a canvas (SelectionPill, NotePill, PdfTextPill, PdfTextHandles, ContextPill: their
 * `target`) and for the keys. A window has one for its current document (`app.edit`, AppController) and one for the
 * reference beside it (`app.reference.edit`, ReferenceMode): the same code for both, so that what one allows the
 * other cannot forget (qt/docs/reference-view.md). It follows a view (setView); what may be done in it is its Policy.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <vector>

#include <QColor>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QUrl>

#include "control/ToolEnums.h"

namespace xqt {

class CanvasView;

class CanvasActions final: public QObject {
    Q_OBJECT
    /// Elements, or several sticky notes with elements, are selected (the selection's pill; a single note has its own)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    /// Select more (qt/touch-multiselect): offered by the tool, available now, on, and the count of what is selected
    Q_PROPERTY(bool selectMoreOffered READ selectMoreOffered NOTIFY selectMoreChanged)
    Q_PROPERTY(bool selectMoreAvailable READ selectMoreAvailable NOTIFY selectMoreChanged)
    Q_PROPERTY(bool selectingMore READ selectingMore WRITE setSelectingMore NOTIFY selectMoreChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectMoreChanged)
    /// Groups (qt/docs/groups.md)
    Q_PROPERTY(bool canGroup READ canGroup NOTIFY selectionChanged)
    Q_PROPERTY(bool canUngroup READ canUngroup NOTIFY selectionChanged)
    /// A sticky note is selected: its pill (qt/docs/sticky-notes.md)
    Q_PROPERTY(bool noteSelected READ noteSelected NOTIFY noteSelectionChanged)
    Q_PROPERTY(QColor noteColor READ noteColor WRITE setNoteColor NOTIFY noteSelectionChanged)
    Q_PROPERTY(bool noteCovers READ noteCovers WRITE setNoteCovers NOTIFY noteSelectionChanged)
    /// PDF text is selected (to copy or mark it)
    Q_PROPERTY(bool pdfTextIsSelected READ pdfTextIsSelected NOTIFY pdfTextSelectionChanged)
    /// The view's own places (Back / Forward)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY navigationChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY navigationChanged)
    /// The page in view (1-based; 0: no view) and how many pages it shows
    Q_PROPERTY(int pageNumber READ pageNumber NOTIFY pageChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY pageChanged)
    /// The zoom, 100: as large as the paper (ScreenCalibration.h)
    Q_PROPERTY(int zoomPercent READ zoomPercent NOTIFY zoomChanged)
public:
    /// What may be done in the canvas, and the window's ways of doing what is not the canvas's own business.
    struct Policy {
        /// Nothing in it may be changed (a reference for reading only): copy, select, look; no cut, paste, delete,
        /// marks or notes. (A read-only document is always for reading only.)
        std::function<bool()> readingOnly;
        /// Chooses a tool (the select tools, before "select all" and an inserted image). Without one: the tool
        /// handler of the view's session.
        std::function<void(ToolType)> selectTool;
        /// Before a sticky note's text is written: what is being written elsewhere is done first.
        std::function<void()> beforeWritingNote;
        /// What is said when elements or notes, or PDF text, were copied ("": nothing).
        QString copiedText;
        QString textCopiedText;
    };

    explicit CanvasActions(Policy policy, QObject* parent = nullptr);
    ~CanvasActions() override;

    /// The view acted on (nullptr: none); follows its signals and tells that everything may have changed.
    void setView(CanvasView* view);
    CanvasView* view() const;
    Policy& policy() { return rules; }

    // --- selection ---
    bool hasSelection() const;
    bool selectMoreOffered() const;
    bool selectMoreAvailable() const;
    bool selectingMore() const;
    void setSelectingMore(bool on);
    int selectedCount() const;
    bool canGroup() const;
    bool canUngroup() const;
    Q_INVOKABLE bool groupSelection();
    Q_INVOKABLE bool ungroupSelection();
    Q_INVOKABLE bool copySelection();
    Q_INVOKABLE bool cutSelection();
    Q_INVOKABLE void deleteSelection();
    /// The clipboard's elements (also a note, a picture, text) onto the page in view
    Q_INVOKABLE bool pasteElements();
    /// ... at a place of the canvas item (its coordinates: the canvas may be turned)
    Q_INVOKABLE bool pasteAt(qreal x, qreal y);
    /// The clipboard holds something to paste (it cannot be watched: asked when a pill opens)
    Q_INVOKABLE bool canPaste() const;
    /// Everything on the page in view (with a select tool, so that it can be moved right away)
    Q_INVOKABLE void selectAllOnPage();
    /// A picture file (also Android's content:// URIs) onto the page in view, selected
    Q_INVOKABLE bool insertImage(const QUrl& file);
    /// Nothing selected any more (elements, notes, PDF text)
    Q_INVOKABLE void clearSelection();

    // --- sticky notes ---
    bool noteSelected() const;
    QColor noteColor() const;
    void setNoteColor(const QColor& color);
    bool noteCovers() const;
    void setNoteCovers(bool covers);
    /// The selected note on the screen (the canvas item's coordinates; empty: none)
    Q_INVOKABLE QRectF noteBox() const;
    /// Write the selected note's text (Markdown, on the note)
    Q_INVOKABLE bool writeNoteText();
    Q_INVOKABLE bool copyStickyNote();
    Q_INVOKABLE bool cutStickyNote();
    Q_INVOKABLE void deleteStickyNote();

    // --- PDF text ---
    bool pdfTextIsSelected() const;
    /// Where the selection begins (x, y) and ends (x + width, y + height), on the screen
    Q_INVOKABLE QRectF pdfSelectionEnds() const;
    Q_INVOKABLE QRectF pdfSelectionBox() const;
    /// Select the word at a place of the canvas item (the same word again: its whole line)
    Q_INVOKABLE bool selectPdfTextAt(qreal x, qreal y);
    Q_INVOKABLE bool dragPdfSelection(qreal x, qreal y, bool startEnd);
    /// Scroll the selected text into view
    Q_INVOKABLE void showPdfSelection();
    /// Mark the selected text: "highlight", "underline", "strikethrough"
    Q_INVOKABLE bool markPdfText(const QString& mode);
    /// Copy the selected text, unless the PDF's author does not allow copying its text (refused, and said so)
    Q_INVOKABLE bool copyPdfText();
    Q_INVOKABLE void clearPdfTextSelection();
    /// The selected text (PDF text, or of the text being written): the look-up actions
    Q_INVOKABLE QString selectedText() const;

    // --- the view ---
    bool canGoBack() const;
    bool canGoForward() const;
    Q_INVOKABLE void navigateBack();
    Q_INVOKABLE void navigateForward();
    int pageNumber() const;
    int pageCount() const;
    /// Go to a page (0-based), remembering the place for Back
    Q_INVOKABLE void goToPage(int index);
    int zoomPercent() const;
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void zoomToRealSize();
    /// The width of the page in view (and not turned)
    Q_INVOKABLE void fitWidth();

    /// Nothing may be changed now (Policy::readingOnly, a read-only document)
    bool readingOnly() const;

Q_SIGNALS:
    void selectionChanged();
    void noteSelectionChanged();
    void selectMoreChanged();
    void pdfTextSelectionChanged();
    /// PDF text was selected: where (the view's coordinates)
    void pdfTextSelected(QRectF rect);
    /// How the PDF text tool marks (the window's choice changed; the handles look again)
    void pdfTextModeChanged();
    void navigationChanged();
    void pageChanged();
    void zoomChanged();
    /// Elements, notes or text were put on the clipboard from here
    void copied();
    /// A note's text is being written (the window's Markdown state follows)
    void noteTextStarted();
    /// Something to tell in passing (a toast)
    void notice(const QString& text);
    /// Something went wrong (a message box)
    void message(const QString& title, const QString& text, bool error);

private:
    /// Pages that may not change: a text file's pages are its text (until it is saved as a document)
    bool pagesFixed() const;
    void chooseSelectTool(ToolType type);
    bool copiedIf(bool ok);

    Policy rules;
    QPointer<CanvasView> shown;
    std::vector<QMetaObject::Connection> connections;
};

}  // namespace xqt
