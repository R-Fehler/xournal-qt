/*
 * Xournal++
 *
 * Class for ruled backgrounds
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <atomic>  // xournal-qt: for ruledScale

#include <cairo.h>  // for cairo_t

#include "util/Color.h"  // for Color

#include "OneColorBackgroundView.h"  // for OneColorBackgroundView

class BackgroundConfig;

namespace xoj::view {
/// xournal-qt: a frontend may draw the ruling of small pages to scale (qt/src/session/PageMargins.cpp). The space
/// above and below the lines and the default margin line of a lined page are multiplied by what it returns for the
/// page's size; the lines stay where upstream's are (the first one moves up by whole lines). nullptr (upstream): as
/// upstream on every size.
using RuledScale = double (*)(double pageWidth, double pageHeight);
inline std::atomic<RuledScale> ruledScale{nullptr};

class RuledBackgroundView: public OneColorBackgroundView {
public:
    RuledBackgroundView(double pageWidth, double pageHeight, Color backgroundColor, const BackgroundConfig& config);
    virtual ~RuledBackgroundView() = default;

    virtual void draw(cairo_t* cr) const override;

protected:
    double lineSpacing = 24.0;  // Between two horizontal lines

    constexpr static Color DEFAULT_H_LINE_COLOR = Colors::xopp_dodgerblue;
    constexpr static Color ALT_DEFAULT_H_LINE_COLOR = Colors::xopp_darkslategray;
    constexpr static double DEFAULT_LINE_WIDTH = 0.5;

    constexpr static double HEADER_SIZE = 80.0;
    constexpr static double FOOTER_SIZE = 60.0;

    // xournal-qt: the header and footer of this page, and the scale (ruledScale)
    double scale = 1.0;
    double header = HEADER_SIZE;
    double footer = FOOTER_SIZE;
};
};  // namespace xoj::view
