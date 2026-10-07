/*
 * xournal-qt: when an element was made (qt/docs/features/timeline.md, "Creation times").
 *
 * Every element the user makes gets the time it was made: milliseconds since 1970-01-01 UTC, saved as the element
 * attribute xqt-created (Element::getCreated, an upstream seam like xqt-group, ADR 0002). The document's timeline
 * (Timeline.h) orders its elements by it.
 *
 * - New elements are stamped where the fork makes them: a stroke when the pen touches (pen, highlighter, shapes,
 *   whiteout), a text when its box opens, an image when it is inserted, marks over PDF text, link markers, chapters,
 *   to-do stamps, sticky notes, Markdown text boxes, the elements of a template's page.
 * - Pasted elements and stickers are new: they get the time they were pasted (upstream's clipboard data has no time;
 *   copies made by the fork, as stickers, are stamped again).
 * - What changes an element keeps its time: moving, resizing, recolouring, editing a text, the pieces the eraser
 *   leaves and the shape a stroke becomes (Stroke::applyStyleFrom), undo and redo.
 * - Elements of files written before (or by Xournal++) have none (0); they stay so.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "model/PageRef.h"

class Element;
class Layer;

namespace xqt::timeline {

/// Now, in milliseconds since 1970-01-01 UTC (the system's clock, or the one a test set)
int64_t now();
/// A clock for tests (empty: the system's again)
void setClock(std::function<int64_t()> clock);

/// The element is new: it gets the time now (or `at`)
void stampNew(Element& e);
void stampNew(Element& e, int64_t at);
/// Every element of these is new (one time for all: they came at once)
void stampNew(const std::vector<Element*>& elements);
/// Every element of the page is new (a page made from a template); also those of its sticky notes
void stampPage(XojPage& page);

}  // namespace xqt::timeline
