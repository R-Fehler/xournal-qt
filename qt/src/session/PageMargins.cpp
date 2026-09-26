#include "PageMargins.h"

#include <algorithm>

#include "model/BackgroundConfig.h"
#include "model/PageType.h"
#include "model/XojPage.h"

#include "MdBox.h"

namespace xqt::PageMargins {

double forSize(double width, double height) {
    const double shortSide = std::min(width, height);
    return std::clamp(shortSide * FULL / A5_SHORT_SIDE, MIN, FULL);
}

namespace {
Margins around(const PageRef& page, double m) {
    Margins out{m, m, m, m};
    const PageType bg = page->getBackgroundType();
    if (bg.format == PageTypeFormat::Lined) {
        double line = 72;  // (upstream's default: 1 inch; negative: on the right)
        BackgroundConfig(bg.config).loadValue(background_config_strings::CFG_MARGIN, line);
        if (line >= 0) {
            out.left = std::max(out.left, line + 10);
        } else {
            out.right = std::max(out.right, -line + 10);
        }
    }
    return out;
}
}  // namespace

Margins of(const PageRef& page) { return around(page, forSize(page->getWidth(), page->getHeight())); }

Margins unscaled(const PageRef& page) { return around(page, FULL); }

Text* pageBox(const Layer& layer, const PageRef& page) {
    const Margins m = of(page);
    if (Text* box = md::pageBoxOf(layer, m.left, m.top)) {
        return box;
    }
    if (m.top < FULL) {
        const Margins old = unscaled(page);
        return md::pageBoxOf(layer, old.left, old.top);
    }
    return nullptr;
}

}  // namespace xqt::PageMargins
