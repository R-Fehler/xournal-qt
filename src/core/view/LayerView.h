/*
 * Xournal++
 *
 * Displays a Layer
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <atomic>  // xournal-qt: for layerDrawer

class Layer;
namespace xoj::view {
class Context;

/// xournal-qt: a frontend may draw some layers itself (sticky notes: qt/src/session/StickyNote.cpp). It returns true
/// when it drew the layer; false leaves it to LayerView. nullptr (upstream): every layer is drawn by LayerView.
using LayerDrawer = bool (*)(const Layer& layer, const Context& ctx);
inline std::atomic<LayerDrawer> layerDrawer{nullptr};

class LayerView {
public:
    LayerView(const Layer* layer);

    /**
     * @brief Draws the entire Layer
     */
    void draw(const Context& ctx) const;

    const Layer* getLayer() const;

private:
    const Layer* layer;
};
};  // namespace xoj::view
