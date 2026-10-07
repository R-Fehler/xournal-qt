/*
 * xournal-qt: groups of elements (qt/docs/features/groups.md).
 *
 * A group is a number on the elements of one layer (Element::getGroup, an upstream seam; 0: in no group), saved in a
 * .xopp as the element attribute xqt-group="n". Upstream Xournal++ ignores the attribute (and drops it when it saves:
 * the elements stay, ungrouped). Groups are flat: an element is in one group or in none.
 *
 * What makes a group is its number within its layer. New numbers are larger than every number in the document and
 * every number handed out before while the app runs, so a group that comes back (undo) never meets a new one with its
 * number. Elements that arrive in a layer from elsewhere (pasted, dropped on another page, moved into a note) get new
 * numbers where another group of that layer has theirs (separate), so two groups never merge by accident.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "model/ElementInsertionPosition.h"
#include "model/PageRef.h"
#include "undo/UndoAction.h"

class Document;
class Element;
class Layer;

namespace xqt::groups {

using Id = uint32_t;

/// A group number that no element of `doc` and none of `also` has, larger than any handed out before (the caller holds
/// the document's lock, shared is enough)
Id fresh(const Document& doc, const std::vector<const Element*>& also = {});

/// The groups of `elements` get new numbers (one new number for each old one; 0 stays 0). For elements that are not
/// in the document (a paste) or are all of their groups. The caller holds the document's lock.
void renumber(const std::vector<Element*>& elements, const Document& doc);

/// The groups of `elements` that another element of `layer` (not one of them) has too get new numbers. For elements
/// put into (or about to be put into) `layer`. Returns whether any changed. The caller holds the document's lock.
bool separate(const std::vector<Element*>& elements, const Layer& layer, const Document& doc);

/// The elements of `layer` at `refs` (sorted by their place in the layer) and the other members of their groups in
/// the layer, sorted by place. The caller holds the document's lock.
InsertionOrderRef withMembers(const Layer& layer, const InsertionOrderRef& refs);
/// The same for elements of a layer (the members added after them, in their order in the layer)
std::vector<Element*> withMembers(const Layer& layer, const std::vector<Element*>& elements);

/// The clipboard: beside upstream's "application/xournal" data (which has no groups, so Xournal++ pastes it as it is),
/// the group number of each element in the same order, as text ("3 3 0 7")
inline constexpr const char* CLIPBOARD_MIME = "application/x-xournal-qt-groups";
std::string clipboardNumbers(const std::vector<const Element*>& elements);
/// The numbers of clipboardNumbers (empty: not `count` numbers)
std::vector<Id> fromClipboard(const std::string& text, size_t count);

/// What a selection of elements can do with groups
struct State {
    bool canGroup = false;    ///< two or more elements that are not one whole group already
    bool canUngroup = false;  ///< one of them is in a group
    bool oneGroup = false;    ///< they are all of one group (its pill button says "Ungroup")
};
State stateOf(const std::vector<const Element*>& elements);

/// One undo step: the groups of some elements changed (grouped, ungrouped)
class GroupChangeUndoAction final: public UndoAction {
public:
    struct Change {
        Element* element;
        Id before;
        Id after;
    };
    GroupChangeUndoAction(PageRef page, std::vector<Change> changes, std::string text);
    bool undo(Control* control) override;
    bool redo(Control* control) override;
    std::string getText() override { return text; }
    std::vector<PageRef> getPages() override { return {}; }  // (no picture changes)

private:
    std::vector<Change> changes;
    std::string text;
};

/// The elements become one group (a new number): the undo step, nullptr if they were one already or are fewer than
/// two. The caller holds the document's lock.
std::unique_ptr<UndoAction> group(const std::vector<Element*>& elements, const PageRef& page, const Document& doc);
/// The elements leave their groups: the undo step, nullptr if none was in one
std::unique_ptr<UndoAction> ungroup(const std::vector<Element*>& elements, const PageRef& page);

}  // namespace xqt::groups
