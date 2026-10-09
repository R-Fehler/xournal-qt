/*
 * xournal-qt: the operations on a document (Operations.h): reading it, its elements, layers, pages and backgrounds.
 * Each change goes through upstream's (or the fork's) undo actions, so it is part of the transaction it runs in.
 *
 *   document.read                       → {pageCount, currentPage, readOnly, file}
 *   page.read {page}                    → {index, width, height, background: {type, color}, layers: [{name, visible,
 *                                          elements, markdown}], selectedLayer}
 *   element.list {page, layer?, withData?} → [{ref, type, layer, group, x, y, width, height, color, data}]
 *   element.insert {page, layer?, shapes, group?, data?} → [ref, …]  (checks stroke.insert, text.insert,
 *                                          markdown.insert per shape; Markdown boxes go to the page's Markdown layer)
 *   element.delete {refs, withGroups?}  → the number deleted
 *   element.data {ref, value}           the principal's own data on an element (null: none)
 *   layer.add {page, name?, above?}     → its index;  layer.rename {page, layer, name};
 *   layer.visible {page, layer, visible};  layer.select {page, layer}
 *   page.insert {at?, count?, background?} → the index of the first;  page.delete {pages}
 *   background.set {pages?, type, color?}   type: plain, lined, ruled, graph, dotted, staves, isodotted, isograph
 *
 * `page` is a 0-based index (default: the current page); `layer` an index into the page's layers, 0 at the bottom
 * (default: the selected one). `data` is kept under the principal's id (Element::getData, the attribute xqt-data).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QVariant>

class Element;

namespace xqt::ops {

class Operations;
struct Principal;

void addDocumentOperations(Operations& ops);
/// A principal's value in an element's data (invalid: none)
QVariant dataOf(const Element& element, const Principal& principal);

}  // namespace xqt::ops
