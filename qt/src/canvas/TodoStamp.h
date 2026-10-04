/*
 * xournal-qt: the check-box stamp for handwritten to-dos (qt/docs/todos.md, "Handwritten to-dos").
 *
 * Armed from the image button's list, the next tap on a page puts a tiny Markdown box there whose whole text is an
 * empty task, "- [ ] " (md::tasks::STAMP): its check box lands where the tap was, and the to-do is written by hand to
 * its right. Ticking it works as for any Markdown task (a tap on its check box); the library's To-dos view lists it
 * whatever the marker setting, with a picture of the handwriting beside it. Then the tool used before comes back.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>

namespace xqt::todostamp {

/// The next tap on a page places a stamp; `placed` is called after it was placed (on the UI thread).
void arm(std::function<void()> placed);
void disarm();
bool isArmed();
/// A stamp was placed (by the canvas): disarmed, then `placed` of arm() is called.
void stamped();

}  // namespace xqt::todostamp
