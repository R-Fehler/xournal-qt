/*
 * xournal-qt: which elements a drawing shows (the replay of the timeline, qt/docs/timeline.md, "Replay").
 *
 * A filter is set for the drawing of a thread (FilterScope; PageRaster sets the one its host gives, RasterHost::
 * rasterFilter). Layers are then drawn without the elements it hides: upstream's LayerView asks the frontend's layer
 * drawer first (xoj::view::layerDrawer, ADR 0002); the fork's drawer (sticky notes, session/StickyNote.cpp) leaves
 * them out of the notes it draws and draws other layers with drawLayer below. Without a filter nothing changes.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

class Element;
class Layer;

namespace xoj::view {
class Context;
}

namespace xqt::render {

class ElementFilter {
public:
    virtual ~ElementFilter() = default;
    /// Whether the element is drawn (any thread)
    virtual bool shows(const Element* e) const = 0;
};

/// The filter of the drawing on this thread (nullptr: everything is drawn)
const ElementFilter* currentFilter();

/// While it lives, this thread draws with `filter` (nullptr: everything)
class FilterScope {
public:
    explicit FilterScope(const ElementFilter* filter);
    ~FilterScope();
    FilterScope(const FilterScope&) = delete;
    FilterScope& operator=(const FilterScope&) = delete;

private:
    const ElementFilter* before;
};

/// Draws a layer as upstream's LayerView does (the elements in the area being drawn), without those `filter` hides
void drawLayer(const Layer& layer, const xoj::view::Context& ctx, const ElementFilter& filter);

/// A layer drawer (xoj::view::layerDrawer) for when the frontend has none: the layer with the current filter, or left
/// to LayerView without one. FilterScope installs it when no drawer is installed.
bool drawFiltered(const Layer& layer, const xoj::view::Context& ctx);

}  // namespace xqt::render
