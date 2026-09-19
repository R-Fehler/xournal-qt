#include "TextFlowEditor.h"

#include <algorithm>
#include <cmath>

#include <QFont>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>

namespace xqt {

namespace {
enum Kind { Paragraph = 0, Heading1 = 1, Heading2 = 2, Heading3 = 3, Bullet = 4, Numbered = 5 };
bool isHeading(int k) { return k >= Heading1 && k <= Heading3; }
bool isList(int k) { return k == Bullet || k == Numbered; }
double headingSize(int k) { return k == Heading1 ? 24 : k == Heading2 ? 18 : 15; }
bool isNumberedStyle(QTextListFormat::Style s) {
    return s == QTextListFormat::ListDecimal || s == QTextListFormat::ListLowerAlpha ||
           s == QTextListFormat::ListUpperAlpha || s == QTextListFormat::ListLowerRoman ||
           s == QTextListFormat::ListUpperRoman;
}
}  // namespace

TextFlowEditor::TextFlowEditor(QObject* parent): QObject(parent) {}

QTextDocument* TextFlowEditor::doc() const { return quickDocument ? quickDocument->textDocument() : nullptr; }

void TextFlowEditor::setDocument(QQuickTextDocument* d) {
    if (d == quickDocument) {
        return;
    }
    if (doc()) {
        disconnect(doc(), nullptr, this, nullptr);
    }
    quickDocument = d;
    if (QTextDocument* document = doc()) {
        document->setDefaultFont(QFont(fontFamily, static_cast<int>(std::lround(body))));
        connect(document, &QTextDocument::contentsChange, this, &TextFlowEditor::onContentsChange);
        connect(document, &QTextDocument::contentsChanged, this, [this] {
            if (!loading) {
                Q_EMIT contentChanged();
                Q_EMIT formatChanged();
            }
        });
    }
    Q_EMIT documentChanged();
}

void TextFlowEditor::setCursorPosition(int p) {
    if (p != cursor) {
        cursor = p;
        Q_EMIT formatChanged();
    }
}
void TextFlowEditor::setSelectionStart(int p) {
    if (p != selStart) {
        selStart = p;
        Q_EMIT formatChanged();
    }
}
void TextFlowEditor::setSelectionEnd(int p) {
    if (p != selEnd) {
        selEnd = p;
        Q_EMIT formatChanged();
    }
}

void TextFlowEditor::setFamily(const QString& f) {
    if (!f.isEmpty() && f != fontFamily) {
        fontFamily = f;
        Q_EMIT styleChanged();
    }
}

void TextFlowEditor::setBodySize(double s) {
    if (s > 0 && s != body) {
        body = s;
        Q_EMIT styleChanged();
    }
}

double TextFlowEditor::sizeFor(const Props& p) const {
    return isHeading(p.kind) ? headingSize(p.kind) : (p.size > 0 ? p.size : body);
}

auto TextFlowEditor::propsOf(const QTextBlock& block) const -> Props {
    Props p;
    if (QTextList* list = block.textList()) {
        p.kind = isNumberedStyle(list->format().style()) ? Numbered : Bullet;
        p.indent = std::max(0, list->format().indent() - 1);
    } else if (const int h = block.blockFormat().headingLevel(); h >= 1 && h <= 3) {
        p.kind = h;
    }
    QTextCharFormat cf = block.charFormat();
    if (auto it = block.begin(); !it.atEnd() && it.fragment().isValid()) {
        cf = it.fragment().charFormat();  // the first character's format (formats are per block)
    }
    if (!isHeading(p.kind)) {
        p.bold = cf.fontWeight() >= QFont::DemiBold;
        const double size = cf.fontPointSize();
        p.size = size <= 0 || std::abs(size - body) < 0.01 ? 0 : size;
    }
    p.italic = cf.fontItalic();
    if (cf.foreground().style() != Qt::NoBrush) {
        p.color = cf.foreground().color();
    }
    return p;
}

void TextFlowEditor::apply(QTextBlock block, const Props& p) {
    QTextDocument* d = doc();
    if (!d || !block.isValid()) {
        return;
    }
    QTextCursor c(d);
    c.beginEditBlock();
    c.setPosition(block.position());
    c.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
    QTextCharFormat cf;
    cf.setFontFamilies({fontFamily});
    cf.setFontPointSize(sizeFor(p));
    cf.setFontWeight(isHeading(p.kind) || p.bold ? QFont::Bold : QFont::Normal);
    cf.setFontItalic(p.italic);
    cf.setForeground(p.color);
    c.setCharFormat(cf);
    c.setBlockCharFormat(cf);  // (for what is typed into an empty block)

    // List membership
    QTextListFormat::Style style = p.kind == Numbered ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc;
    if (QTextList* list = block.textList()) {
        if (!isList(p.kind) || list->format().style() != style || list->format().indent() != p.indent + 1) {
            list->remove(block);
            QTextBlockFormat bf = block.blockFormat();
            bf.setIndent(0);
            c.setBlockFormat(bf);
        }
    }
    QTextBlockFormat bf = block.blockFormat();
    bf.setHeadingLevel(isHeading(p.kind) ? p.kind : 0);
    c.setBlockFormat(bf);
    if (isList(p.kind) && !block.textList()) {
        QTextList* before = block.previous().isValid() ? block.previous().textList() : nullptr;
        if (before && before->format().style() == style && before->format().indent() == p.indent + 1) {
            before->add(block);  // continues the list above
        } else {
            QTextListFormat lf;
            lf.setStyle(style);
            lf.setIndent(p.indent + 1);
            c.createList(lf);
        }
    }
    c.endEditBlock();
}

QList<QTextBlock> TextFlowEditor::selectedBlocks() const {
    QList<QTextBlock> list;
    QTextDocument* d = doc();
    if (!d) {
        return list;
    }
    const int from = selStart != selEnd ? std::min(selStart, selEnd) : cursor;
    const int to = selStart != selEnd ? std::max(selStart, selEnd) : cursor;
    for (QTextBlock b = d->findBlock(from); b.isValid() && b.position() <= to; b = b.next()) {
        list << b;
    }
    return list;
}

void TextFlowEditor::forSelection(const std::function<void(Props&)>& change) {
    for (const QTextBlock& b: selectedBlocks()) {
        Props p = propsOf(b);
        change(p);
        apply(b, p);
    }
    Q_EMIT formatChanged();
}

int TextFlowEditor::blockKind() const {
    return doc() ? propsOf(doc()->findBlock(cursor)).kind : Paragraph;
}
bool TextFlowEditor::bold() const {
    if (!doc()) {
        return false;
    }
    const Props p = propsOf(doc()->findBlock(cursor));
    return p.bold || isHeading(p.kind);
}
bool TextFlowEditor::italic() const { return doc() && propsOf(doc()->findBlock(cursor)).italic; }
double TextFlowEditor::fontSize() const { return doc() ? sizeFor(propsOf(doc()->findBlock(cursor))) : body; }
QColor TextFlowEditor::color() const { return doc() ? propsOf(doc()->findBlock(cursor)).color : QColor(Qt::black); }

void TextFlowEditor::setBlockKind(int kind) {
    forSelection([kind](Props& p) {
        p.kind = std::clamp(kind, 0, 5);
        if (!isList(p.kind)) {
            p.indent = 0;
        }
        if (isHeading(p.kind)) {
            p.size = 0;
            p.bold = false;
        }
    });
}

void TextFlowEditor::toggleBold() {
    const bool b = !bold();
    forSelection([b](Props& p) { p.bold = b; });
}

void TextFlowEditor::toggleItalic() {
    const bool i = !italic();
    forSelection([i](Props& p) { p.italic = i; });
}

void TextFlowEditor::changeSize(double step) {
    forSelection([this, step](Props& p) {
        if (!isHeading(p.kind)) {
            const double s = std::clamp((p.size > 0 ? p.size : body) + step, 6.0, 72.0);
            p.size = std::abs(s - body) < 0.01 ? 0 : s;
        }
    });
}

void TextFlowEditor::setColor(const QColor& c) {
    forSelection([c](Props& p) { p.color = c; });
}

void TextFlowEditor::indent(int delta) {
    forSelection([delta](Props& p) {
        if (isList(p.kind)) {
            p.indent = std::clamp(p.indent + delta, 0, 6);
        }
    });
}

bool TextFlowEditor::returnPressed() {
    QTextDocument* d = doc();
    if (!d) {
        return false;
    }
    const QTextBlock b = d->findBlock(cursor);
    const Props p = propsOf(b);
    if (isList(p.kind) && b.text().isEmpty()) {
        Props plain = p;  // Enter on an empty item ends the list
        plain.kind = Paragraph;
        plain.indent = 0;
        apply(b, plain);
        Q_EMIT formatChanged();
        return true;
    }
    if (isHeading(p.kind) && cursor == b.position() + b.length() - 1) {
        QTextCursor c(d);  // after a heading: a paragraph
        c.setPosition(cursor);
        c.insertBlock();
        Props plain;
        plain.color = p.color;
        apply(c.block(), plain);
        Q_EMIT formatChanged();
        return true;
    }
    return false;
}

void TextFlowEditor::onContentsChange(int position, int removed, int added) {
    if (loading || added != 1 || removed != 0 || !doc()) {
        return;
    }
    if (doc()->characterAt(position) != QLatin1Char(' ')) {
        return;
    }
    const QTextBlock b = doc()->findBlock(position);
    const QString head = b.text().left(position - b.position() + 1);
    static const QStringList prefixes{"# ", "## ", "### ", "- ", "* ", "1. ", "1) "};
    if (prefixes.contains(head)) {
        // Not inside this change: the document is still being edited
        const int number = b.blockNumber();
        QMetaObject::invokeMethod(this, [this, number] { markdownShortcut(number); }, Qt::QueuedConnection);
    }
}

void TextFlowEditor::markdownShortcut(int blockNumber) {
    QTextDocument* d = doc();
    if (!d) {
        return;
    }
    QTextBlock b = d->findBlockByNumber(blockNumber);
    const QString text = b.text();
    int kind = -1, length = 0;
    if (text.startsWith("### ")) {
        kind = Heading3, length = 4;
    } else if (text.startsWith("## ")) {
        kind = Heading2, length = 3;
    } else if (text.startsWith("# ")) {
        kind = Heading1, length = 2;
    } else if (text.startsWith("- ") || text.startsWith("* ")) {
        kind = Bullet, length = 2;
    } else if (text.startsWith("1. ") || text.startsWith("1) ")) {
        kind = Numbered, length = 3;
    }
    if (kind < 0) {
        return;
    }
    QTextCursor c(d);
    c.setPosition(b.position());
    c.setPosition(b.position() + length, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    Props p = propsOf(b);
    p.kind = kind;
    if (isHeading(kind)) {
        p.size = 0;
        p.bold = false;
    }
    apply(b, p);
    Q_EMIT formatChanged();
}

void TextFlowEditor::load(const QVariantList& blocks) {
    QTextDocument* d = doc();
    if (!d) {
        return;
    }
    loading = true;
    d->clear();
    d->setDefaultFont(QFont(fontFamily, static_cast<int>(std::lround(body))));
    QTextCursor c(d);
    for (int i = 0; i < blocks.size(); ++i) {
        const QVariantMap m = blocks[i].toMap();
        if (i > 0) {
            c.insertBlock();
        }
        c.insertText(m.value("text").toString());
        Props p;
        p.kind = m.value("kind").toInt();
        p.bold = m.value("bold").toBool();
        p.italic = m.value("italic").toBool();
        p.size = m.value("size").toDouble();
        p.color = m.value("color").value<QColor>();
        p.indent = m.value("indent").toInt();
        apply(c.block(), p);
    }
    if (blocks.isEmpty()) {
        apply(d->firstBlock(), Props{});
    }
    loading = false;
    Q_EMIT formatChanged();
}

QVariantList TextFlowEditor::blocks() const {
    QVariantList list;
    QTextDocument* d = doc();
    if (!d) {
        return list;
    }
    for (QTextBlock b = d->firstBlock(); b.isValid(); b = b.next()) {
        const Props p = propsOf(b);
        list.append(QVariantMap{{"kind", p.kind},
                                {"text", b.text()},
                                {"bold", p.bold},
                                {"italic", p.italic},
                                {"size", p.size},
                                {"color", p.color},
                                {"indent", p.indent}});
    }
    // No trailing empty blocks (the last Enter)
    while (!list.isEmpty() && list.last().toMap().value("text").toString().isEmpty()) {
        list.removeLast();
    }
    return list;
}

}  // namespace xqt
