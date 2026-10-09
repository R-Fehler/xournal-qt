/*
 * xournal-qt: the plugins' data on elements on the clipboard (qt/docs/features/plugins.md, "Data on elements";
 * qt/docs/features/groups.md, "The clipboard").
 *
 * An element's data (Element::getData, the attribute xqt-data: a JSON object by plugin id) is not in upstream's
 * clipboard data (Element::serialize stays upstream's, so Xournal++ pastes what is copied here). Beside it the fork
 * writes the data of each copied element in the same order, so a copied plot is a plot again where it is pasted. The
 * pasted copy is a thing of its own through its groups: pasting gives every copied group a new number
 * (groups::renumber), and plugins tell their things apart by their group, not by an id inside their data.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <vector>

class Element;

namespace xqt::elementdata {

/// On the clipboard beside upstream's data: a JSON array with each copied element's data (a string, "" for none)
inline constexpr const char* CLIPBOARD_MIME = "application/x-xournal-qt-data";

/// The clipboard text of these elements' data ("": none of them has data)
std::string clipboardData(const std::vector<const Element*>& elements);
/// The data of clipboardData, one per element (empty: not `count` of them, or not that format)
std::vector<std::string> fromClipboard(const std::string& text, size_t count);

}  // namespace xqt::elementdata
