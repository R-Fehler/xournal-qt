/*
 * Xournal++
 *
 * Toolkit-independent PDF outline (table of contents) of a document.
 * xournal-qt: used instead of the GtkTreeModel when building without GTK (XOJ_NO_GTK).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>  // for string
#include <vector>  // for vector

#include "LinkDestination.h"  // for LinkDestination

struct DocumentOutlineEntry {
    std::string title;          ///< Plain text title (not escaped)
    LinkDestination dest;       ///< Target of the entry; dest.getExpand() tells if the entry is initially open
    std::string pageLabel;      ///< 1-based number of the document page showing the target PDF page, or ""
    std::vector<DocumentOutlineEntry> children;
};

using DocumentOutline = std::vector<DocumentOutlineEntry>;
