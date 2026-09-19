/*
 * xournal-qt: shadow of upstream control/ScrollHandler.h.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>  // for size_t

#include "model/PageRef.h"        // for PageRef
#include "pdf/base/XojPdfPage.h"  // for XojPdfRectangle

class LinkDestination;

class ScrollHandler {
public:
    virtual ~ScrollHandler() = default;
    virtual void scrollToPage(const PageRef& page, XojPdfRectangle rect = {0, 0, -1, -1}) = 0;
    virtual void scrollToPage(size_t page, XojPdfRectangle rect = {0, 0, -1, -1}) = 0;
    virtual void scrollToLinkDest(const LinkDestination& dest) = 0;
};
