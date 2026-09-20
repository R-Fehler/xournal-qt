/*
 * xournal-qt: chapters of a document that has no PDF outline.
 *
 * They come from the document itself, so nothing is stored beside it and Xournal++ keeps everything:
 *  - a text that begins with "# ", "## " or "### " (the level is the number of marks), and
 *  - the headings of the text mode (bold, 24 / 18 / 15 pt).
 * The contents sidebar and the contents overview show them when the document has no PDF table of contents.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <vector>

class Document;

namespace xqt::DocumentChapters {

struct Chapter {
    std::string title;
    int level = 0;   ///< 0: chapter, 1: section, 2: subsection
    size_t page = 0;  ///< 0-based
};

/// The chapters of the document, in the order of the pages (the document is locked while reading).
std::vector<Chapter> find(Document& document);

/// The text of a heading for the given level, as `find` recognizes it again.
std::string headingText(const std::string& title, int level);
/// Size of a heading of that level (the text mode uses the same ones).
double headingSize(int level);

}  // namespace xqt::DocumentChapters
