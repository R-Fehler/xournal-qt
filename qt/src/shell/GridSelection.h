/*
 * xournal-qt: selected items of a grid of files (library, recent documents), kept by path so that the selection
 * survives the list changing (new files, sorting).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <algorithm>
#include <functional>
#include <set>

#include <Qt>

#include "filesystem.h"

namespace xqt {

struct GridSelection {
    std::set<fs::path> paths;
    fs::path anchor;  ///< where a Shift+click range starts

    /// A click on a row: alone selects only it, Ctrl toggles it, Shift selects the range from the anchor (with Ctrl:
    /// added to the selection). `pathAt(i)` for the rows 0 .. count-1.
    void click(int row, Qt::KeyboardModifiers mods, int count, const std::function<fs::path(int)>& pathAt) {
        if (row < 0 || row >= count) {
            return;
        }
        const fs::path p = pathAt(row);
        if (mods & Qt::ShiftModifier) {
            int from = row;
            for (int i = 0; i < count; ++i) {
                if (pathAt(i) == anchor) {
                    from = i;
                    break;
                }
            }
            if (!(mods & Qt::ControlModifier)) {
                paths.clear();
            }
            for (int i = std::min(from, row); i <= std::max(from, row); ++i) {
                paths.insert(pathAt(i));
            }
            return;  // the anchor stays
        }
        if (mods & Qt::ControlModifier) {
            toggle(p);
        } else {
            paths = {p};
        }
        anchor = p;
    }
    void toggle(const fs::path& p) {
        if (!paths.erase(p)) {
            paths.insert(p);
        }
        anchor = p;
    }
    bool contains(const fs::path& p) const { return paths.count(p) > 0; }
    /// Drop what is not shown any more. Returns true if something was dropped.
    bool keepOnly(const std::set<fs::path>& shown) {
        const size_t before = paths.size();
        std::erase_if(paths, [&](const fs::path& p) { return !shown.count(p); });
        return paths.size() != before;
    }
};

}  // namespace xqt
