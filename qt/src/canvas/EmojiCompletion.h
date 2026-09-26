/*
 * xournal-qt: emoji shortcodes completed while typing on the page (text boxes, Markdown, the .md editor).
 *
 * ":" and at least two letters before the cursor (":smi") open a list of emoji (😄 smile, 😃 smiley, ...; the names are
 * GitHub's, EmojiData.h). Up / Down choose, Enter or Tab (or a tap on one) puts the emoji in place of the shortcode,
 * Escape closes the list until another shortcode is typed. Typing goes on as always.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <vector>

#include "EmojiData.h"

class QKeyEvent;

namespace xqt {

class CanvasTextInput;

class EmojiCompletion {
public:
    static constexpr size_t LIMIT = 8;

    /// The text or the cursor of `input` changed (nullptr: nothing is being written): the suggestions follow.
    /// Returns whether they changed (shown, closed, others).
    bool update(const CanvasTextInput* input);
    /// Whether suggestions are shown.
    bool active() const { return !items.empty(); }
    const std::vector<emoji::Completion>& suggestions() const { return items; }
    int selected() const { return current; }

    /// A key while the list is shown: Up / Down, Enter / Tab, Escape. Returns whether it took the key.
    bool keyPressed(const QKeyEvent* e, CanvasTextInput& input);
    /// Puts suggestion `index` in place of the shortcode typed.
    void choose(int index, CanvasTextInput& input);
    /// Closes the list until another shortcode is typed.
    void dismiss();

private:
    std::string query;      ///< the shortcode typed, with its ":"
    std::string dismissed;  ///< closed with Escape for this query
    std::vector<emoji::Completion> items;
    int current = 0;
};

}  // namespace xqt
