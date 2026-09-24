/*
 * xournal-qt: how the search compares text - one matcher for counting and for marking.
 *
 * The search of an open document counts hits in the text of its pages (DocumentTextIndex) and marks them on the
 * pages it shows. Both run this matcher over the same text, so the count and the marks agree:
 *  - the text is kept simplified, as the library index keeps it: whitespace runs are one space (the PDF text has
 *    line breaks where the page has them, so a phrase across a line break is found);
 *  - case-insensitive (Unicode case folding);
 *  - a ligature (U+FB00..U+FB06, "ﬁ") matches its letters;
 *  - a word broken at a line end, "hyphen- ated" (a letter, '-', the space of the line break, a lower-case
 *    letter), is found as "hyphenated", and as "hyphen-ated" too; typed with the hyphen and the space it is found as
 *    well. A soft hyphen (U+00AD) is skipped;
 *  - matches do not overlap; a '\n' is never matched (the pieces of a page's text are joined with it: a hit does not
 *    run from one text box into the next).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include <QString>
#include <QStringView>

namespace xqt::textmatch {

/// The query as it is compared: whitespace runs to one space, trimmed, case folded, ligatures written out.
QString prepare(const QString& query);

/// A match of a query in a text: [start, end) of the text.
struct Span {
    qsizetype start = 0;
    qsizetype end = 0;
};
/// The matches of `query` (prepare()d) in `text`, in order.
std::vector<Span> find(QStringView text, QStringView query);
/// Their number (without collecting them).
int count(QStringView text, QStringView query);

/// A text simplified (whitespace runs to one space, trimmed: QString::simplified), with where each of its characters
/// came from: `origin[i]` is the index in the original text of character i (and origin[size] the end of the last one).
struct Simplified {
    QString text;
    std::vector<qsizetype> origin;
};
Simplified simplify(QStringView original);

}  // namespace xqt::textmatch
