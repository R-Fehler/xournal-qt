/*
 * xournal-qt: emoji for the QML UI (`import XournalQt.Canvas`, the singleton `Emoji`): the completion of shortcodes
 * in the Markdown editor beside the page, the emoji picker's search, and stepping over a text by grapheme cluster
 * (a TextArea of Qt 6.7 splits flags). The canvas has its own completion (EmojiCompletion).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace xqt {

class EmojiNames: public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    /// The shortcode being typed at the end of `before` (the text before the cursor): its name without ":" (at least
    /// two characters), or "" (EmojiData.h, typedShortcode).
    Q_INVOKABLE QString typedShortcode(const QString& before) const;
    /// Suggestions for a shortcode's beginning: {emoji, name} each (at most `limit`).
    Q_INVOKABLE QVariantList completions(const QString& prefix, int limit = 8) const;
    /// The picker: emoji whose names, tags or description contain `query` ("" : all), {emoji, name, category}
    /// each (category: an index into categories()).
    Q_INVOKABLE QVariantList search(const QString& query, int limit = 5000) const;
    Q_INVOKABLE QStringList categories() const;
    /// The grapheme cluster boundary after (`forward`) or before the UTF-16 position `pos` of `text`.
    Q_INVOKABLE int graphemeStep(const QString& text, int pos, bool forward) const;
};

}  // namespace xqt
