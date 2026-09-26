/*
 * xournal-qt: the title of a PDF, for finding the paper of a reference in the library (qt/docs/citations.md).
 *
 * Papers are often named by numbers (arXiv: 1706.03762.pdf), so the library matches their titles, from two places:
 *  - the PDF's /Title (its document information), when it looks like a title: not empty, not a file name
 *    ("paper.dvi", "Microsoft Word - x.docx"), not "untitled", not only digits or an arXiv number;
 *  - the text in the largest font on its first page: the runs of poppler's text attributes with the largest font
 *    size, in reading order, at most 300 characters. Text that runs up or down the page (the arXiv stamp in the margin,
 *    "arXiv:1706.03762v7 [cs.CL] 2 Aug 2023", often the largest text of the page) does not count.
 *
 * Reads with a poppler instance of its own (the library index's worker thread).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QString>

#include "filesystem.h"

namespace xqt::pdftitle {

struct Titles {
    QString meta;     ///< the /Title, if it looks like one ("" otherwise)
    QString heading;  ///< the largest text of the page ("": none)
};

/// The /Title of `pdf` and the largest text of its page `page` (0-based).
Titles read(const fs::path& pdf, int page = 0);

/// A /Title that looks like a title (`fileName`: the PDF's name, which a /Title often only repeats).
bool plausibleMeta(const QString& title, const QString& fileName = {});

/// The longest a heading is kept.
constexpr int HEADING_CHARS = 300;

}  // namespace xqt::pdftitle
