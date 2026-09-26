#include "TextView.h"

#include <algorithm>  // for max
#include <cstddef>    // for size_t

#include "model/MarkdownText.h"   // xournal-qt: for renderer
#include "model/Text.h"           // for Text
#include "util/Color.h"           // for cairo_set_source_rgbi
#include "util/Matrix.h"          // for Matrix
#include "util/StringUtils.h"     // for StringUtils
#include "util/TextLinks.h"        // for findLinks
#include "util/raii/CairoWrappers.h"
#include "util/raii/GObjectSPtr.h"
#include "view/View.h"            // for Context, OPACITY_NO_AUDIO, view

#include "filesystem.h"  // for path

using namespace xoj::view;

TextView::TextView(const Text* text): text(text) {}

TextView::~TextView() = default;

auto TextView::initPango(cairo_t* cr, const Text* t) -> xoj::util::GObjectSPtr<PangoLayout> {
    auto layout = t->createPangoLayout();
    pango_cairo_update_layout(cr, layout.get());
    pango_context_set_matrix(pango_layout_get_context(layout.get()), nullptr);

    return layout;
}

void TextView::draw(const Context& ctx) const {
    if (text->isInEditing()) {
        // The drawing is handled by gui/TextEditor
        return;
    }

    // xournal-qt: a Markdown text is drawn formatted by the frontend's renderer (model/MarkdownText.h)
    if (text->isMarkdown()) {
        if (auto renderer = xoj::markdown::renderer.load(std::memory_order_acquire)) {
            renderer(*text, ctx.cr);
            return;
        }
    }

    xoj::util::CairoSaveGuard saveGuard(ctx.cr);

    // make elements without audio translucent when highlighting elements with audio
    if (ctx.fadeOutNonAudio && text->getAudioFilename().empty()) {
        cairo_set_operator(ctx.cr, CAIRO_OPERATOR_OVER);
        Util::cairo_set_source_rgbi(ctx.cr, text->getColor(), OPACITY_NO_AUDIO);
    } else {
        cairo_set_operator(ctx.cr, CAIRO_OPERATOR_SOURCE);
        Util::cairo_set_source_rgbi(ctx.cr, text->getColor());
    }

    text->getTransformation().transformCairo(ctx.cr);

    auto layout = initPango(ctx.cr, text);
    const std::string& content = text->getText();
    pango_layout_set_text(layout.get(), content.c_str(), static_cast<int>(content.length()));

    // xournal-qt: web addresses in the text are underlined and coloured like links (the text itself stays plain)
    if (const auto links = xoj::util::findLinks(content); !links.empty()) {
        PangoAttrList* attributes = pango_attr_list_new();
        for (const auto& link: links) {
            PangoAttribute* underline = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
            underline->start_index = static_cast<guint>(link.start);
            underline->end_index = static_cast<guint>(link.start + link.length);
            pango_attr_list_insert(attributes, underline);
            PangoAttribute* colour = pango_attr_foreground_new(0x1a1a, 0x5f5f, 0xd8d8);  // a link blue
            colour->start_index = underline->start_index;
            colour->end_index = underline->end_index;
            pango_attr_list_insert(attributes, colour);
        }
        pango_layout_set_attributes(layout.get(), attributes);
        pango_attr_list_unref(attributes);
    }

    // xournal-qt: through the frontend's painter when it has one (model/MarkdownText.h)
    if (auto painter = xoj::markdown::layoutPainter.load(std::memory_order_acquire)) {
        painter(ctx.cr, layout.get());
    } else {
        pango_cairo_show_layout(ctx.cr, layout.get());
    }
}
