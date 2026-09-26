#include "EmojiCompletion.h"

#include <QKeyEvent>

#include "CanvasTextInput.h"

namespace xqt {

bool EmojiCompletion::update(const CanvasTextInput* input) {
    std::string typed;
    if (input) {
        const std::string before = input->textBeforeCursor();
        if (const std::string_view name = emoji::typedShortcode(before); !name.empty()) {
            typed = ":" + std::string(name);
        }
    }
    if (typed == query) {
        return false;
    }
    query = typed;
    if (query.rfind(dismissed, 0) != 0) {
        dismissed.clear();  // (another shortcode; the one closed with Escape stays closed while it is typed on)
    }
    const bool was = active();
    items = query.empty() || !dismissed.empty() ? std::vector<emoji::Completion>{}
                                                : emoji::complete(std::string_view(query).substr(1), LIMIT);
    current = 0;
    return was || active();
}

bool EmojiCompletion::keyPressed(const QKeyEvent* e, CanvasTextInput& input) {
    if (!active() || (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        return false;
    }
    const int n = static_cast<int>(items.size());
    switch (e->key()) {
        case Qt::Key_Up:
            current = (current + n - 1) % n;
            return true;
        case Qt::Key_Down:
            current = (current + 1) % n;
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Tab:
            choose(current, input);
            return true;
        case Qt::Key_Escape:
            dismiss();
            return true;
        default:
            return false;
    }
}

void EmojiCompletion::choose(int index, CanvasTextInput& input) {
    if (index < 0 || index >= static_cast<int>(items.size())) {
        return;
    }
    const std::string emoji = items[static_cast<size_t>(index)].emoji->emoji;
    const size_t bytes = query.size();
    items.clear();
    query.clear();
    input.replaceBeforeCursor(bytes, emoji);
}

void EmojiCompletion::dismiss() {
    dismissed = query;
    items.clear();
}

}  // namespace xqt
