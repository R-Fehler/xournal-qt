/*
 * xournal-qt: the task lines of a Markdown text (qt/docs/features/todos.md): "- [ ] call the lab", "* [x] done", "1. [
 * ] …".
 *
 * They are found by the parser (md4c's task lists, as the boxes draw them), so an example in a code block is none. A
 * task's due date is written in the Obsidian Tasks way ("📅 2026-10-12") or as "due:2026-10-12". A check-box stamp
 * (the tool for handwritten to-dos) is a box whose whole text is one task without text, "- [ ] ".
 *
 * Qt-free, like the rest of the engine.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace xqt::md::tasks {

/// A task line of a text.
struct Task {
    size_t mark = 0;       ///< the offset of its mark (the " " or "x" between the brackets)
    size_t lineBegin = 0;  ///< its line: [lineBegin, lineEnd), without the line break
    size_t lineEnd = 0;
    int line = 0;          ///< the line's number (0-based)
    bool done = false;
    std::string text;      ///< what follows the check box on its line, trimmed (Markdown as written)
};

/// The task lines of a Markdown text, in the order of the text (none in a plain text).
std::vector<Task> find(std::string_view source);

/// The due date a task's text names ("📅 2026-10-12", "due:2026-10-12", "due: 2026-10-12"): "2026-10-12", or "" when
/// it names none (or no valid date).
std::string dueDate(std::string_view text);
/// The text without its due date (and the white space around it).
std::string withoutDueDate(std::string_view text);

/// What a check-box stamp writes into its box.
constexpr std::string_view STAMP = "- [ ] ";
/// Whether a box's text is a check-box stamp: one task without text, nothing else ("- [ ]", "- [x]", "* [ ]", white
/// space around it).
bool isStamp(std::string_view source);

/// The text with the task whose mark is at `mark` set to done or open (the same length: nothing else moves).
std::string withTask(std::string_view source, size_t mark, bool done);

}  // namespace xqt::md::tasks
