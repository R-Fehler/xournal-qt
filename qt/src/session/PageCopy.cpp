/*
 * xournal-qt: a deep copy of a page (PageCopy.h).
 *
 * @license GNU GPLv2 or later
 */
#include "PageCopy.h"

#include <memory>

#include "model/Layer.h"
#include "model/XojPage.h"

namespace xqt {

PageRef deepCopyOf(const PageRef& page) {
    struct Access: XojPage {
        using XojPage::setLayerVisible;  // (for the LayerController only)
    };
    constexpr auto setLayerVisible = &Access::setLayerVisible;
    auto copy = std::make_shared<XojPage>(*page);
    for (Layer::Index i = 0; i <= page->getLayerCount(); ++i) {  // (0: the background)
        ((*copy).*setLayerVisible)(i, page->isLayerVisible(i));
    }
    if (page->backgroundHasName()) {
        copy->setBackgroundName(page->getBackgroundName());
    }
    return copy;
}

}  // namespace xqt
