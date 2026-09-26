#include "PageMargins.h"

#include <algorithm>

#include "model/BackgroundConfig.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "view/background/RuledBackgroundView.h"

#include "MdBox.h"

namespace xqt::PageMargins {

double forSize(double width, double height) {
    const double shortSide = std::min(width, height);
    return std::clamp(shortSide * FULL / A5_SHORT_SIDE, MIN, FULL);
}

double rulingScale(double width, double height) { return forSize(width, height) / FULL; }

void installRuling() { xoj::view::ruledScale.store(&rulingScale, std::memory_order_release); }

namespace {
/// Margins of `m` on every side, after the margin line of a lined page (its default drawn at 1 inch * `lineScale`)
Margins around(const PageRef& page, double m, double lineScale) {
    Margins out{m, m, m, m};
    const PageType bg = page->getBackgroundType();
    if (bg.format == PageTypeFormat::Lined) {
        double line = 72;  // (upstream's default: 1 inch, on a small page to scale; negative: on the right)
        if (!BackgroundConfig(bg.config).loadValue(background_config_strings::CFG_MARGIN, line)) {
            line *= lineScale;
        }
        if (line >= 0) {
            out.left = std::max(out.left, line + 10);
        } else {
            out.right = std::max(out.right, -line + 10);
        }
    }
    return out;
}
}  // namespace

Margins of(const PageRef& page) {
    const double w = page->getWidth(), h = page->getHeight();
    return around(page, forSize(w, h), rulingScale(w, h));
}

Margins unscaled(const PageRef& page) { return around(page, FULL, 1); }

Text* pageBox(const Layer& layer, const PageRef& page) {
    const Margins m = of(page);
    if (Text* box = md::pageBoxOf(layer, m.left, m.top)) {
        return box;
    }
    if (m.top < FULL) {
        // (a lined page's text from before its line scaled: after the line at 1 inch)
        const Margins lined = around(page, m.top, 1);
        if (lined.left != m.left) {
            if (Text* box = md::pageBoxOf(layer, lined.left, lined.top)) {
                return box;
            }
        }
        const Margins old = unscaled(page);
        return md::pageBoxOf(layer, old.left, old.top);
    }
    return nullptr;
}

}  // namespace xqt::PageMargins
