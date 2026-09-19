/*
 * xournal-qt: the editor of the text mode (a QML TextArea's document, word-processor style).
 *
 * Formatting is per block, as the page can store it (see TextFlow): the kind (paragraph, heading 1-3, bullet or
 * numbered list item, with a list level), bold, italic, size and color of the whole block. Markdown shortcuts at the
 * start of a block: "# ", "## ", "### " (headings), "- " or "* " (bullets), "1. " (numbers). Enter after a heading
 * starts a paragraph; Enter on an empty list item ends the list.
 * Blocks go to and come from the controller as QVariantMaps (TextFlow::toVariant / fromVariant).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QVariantList>

class QTextDocument;

namespace xqt {

class TextFlowEditor: public QObject {  // (not final: QML derives from it)
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(int cursorPosition READ cursorPosition WRITE setCursorPosition NOTIFY formatChanged)
    Q_PROPERTY(int selectionStart READ selectionStart WRITE setSelectionStart NOTIFY formatChanged)
    Q_PROPERTY(int selectionEnd READ selectionEnd WRITE setSelectionEnd NOTIFY formatChanged)
    /// Of the block at the cursor: 0 paragraph, 1-3 heading, 4 bullet, 5 numbered
    Q_PROPERTY(int blockKind READ blockKind NOTIFY formatChanged)
    Q_PROPERTY(bool bold READ bold NOTIFY formatChanged)
    Q_PROPERTY(bool italic READ italic NOTIFY formatChanged)
    Q_PROPERTY(double fontSize READ fontSize NOTIFY formatChanged)
    Q_PROPERTY(QColor color READ color NOTIFY formatChanged)
    /// The text font (the text tool's) and the size of paragraphs
    Q_PROPERTY(QString family READ family WRITE setFamily NOTIFY styleChanged)
    Q_PROPERTY(double bodySize READ bodySize WRITE setBodySize NOTIFY styleChanged)
public:
    explicit TextFlowEditor(QObject* parent = nullptr);

    QQuickTextDocument* document() const { return quickDocument; }
    void setDocument(QQuickTextDocument* d);
    int cursorPosition() const { return cursor; }
    void setCursorPosition(int p);
    int selectionStart() const { return selStart; }
    void setSelectionStart(int p);
    int selectionEnd() const { return selEnd; }
    void setSelectionEnd(int p);
    int blockKind() const;
    bool bold() const;
    bool italic() const;
    double fontSize() const;
    QColor color() const;
    QString family() const { return fontFamily; }
    void setFamily(const QString& f);
    double bodySize() const { return body; }
    void setBodySize(double s);

    /// Show these blocks (from the page).
    Q_INVOKABLE void load(const QVariantList& blocks);
    /// The blocks as typed (for the page).
    Q_INVOKABLE QVariantList blocks() const;

    /// For the blocks of the selection (or the cursor's):
    Q_INVOKABLE void setBlockKind(int kind);
    Q_INVOKABLE void toggleBold();
    Q_INVOKABLE void toggleItalic();
    Q_INVOKABLE void changeSize(double step);
    Q_INVOKABLE void setColor(const QColor& color);
    /// List level +1 / -1
    Q_INVOKABLE void indent(int delta);
    /// Enter: true if the editor handled it (paragraph after a heading, end of a list).
    Q_INVOKABLE bool returnPressed();

Q_SIGNALS:
    void documentChanged();
    void formatChanged();
    void styleChanged();
    /// The text or a format changed (the page should follow).
    void contentChanged();

private:
    struct Props {
        int kind = 0;
        bool bold = false, italic = false;
        double size = 0;  ///< 0: body size
        QColor color = Qt::black;
        int indent = 0;
    };
    QTextDocument* doc() const;
    Props propsOf(const QTextBlock& block) const;
    /// Give a block these properties (formats of the whole block, list membership).
    void apply(QTextBlock block, const Props& p);
    /// The blocks the selection touches (or the cursor's).
    QList<QTextBlock> selectedBlocks() const;
    void forSelection(const std::function<void(Props&)>& change);
    void onContentsChange(int position, int removed, int added);
    void markdownShortcut(int blockNumber);
    double sizeFor(const Props& p) const;

    QPointer<QQuickTextDocument> quickDocument;
    int cursor = 0, selStart = 0, selEnd = 0;
    QString fontFamily = "Sans";
    double body = 12;
    bool loading = false;
};

}  // namespace xqt
