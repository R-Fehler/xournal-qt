/*
 * xournal-qt: a deep copy of a page, as a save writes it from (DocumentSave.cpp) and page files export it
 * (PageFiles.cpp): its layers and elements, which of its layers are visible and its background's name (upstream's copy
 * constructor leaves out the last two).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include "model/PageRef.h"

namespace xqt {

/// The caller holds the document's read lock.
PageRef deepCopyOf(const PageRef& page);

}  // namespace xqt
