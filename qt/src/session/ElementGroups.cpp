#include "ElementGroups.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <sstream>
#include <utility>

#include <QCoreApplication>

#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/XojPage.h"

namespace xqt::groups {

namespace {
/// The largest number handed out while the app runs (all documents: a group never meets one handed out before)
std::atomic<Id> lastIssued{0};
}  // namespace

Id fresh(const Document& doc, const std::vector<const Element*>& also) {
    Id largest = lastIssued.load();
    for (size_t p = 0; p < doc.getPageCount(); ++p) {
        const PageRef page = doc.getPage(p);
        for (const Layer* l: page->getLayersView()) {
            for (const Element* e: l->getElementsView()) {
                largest = std::max(largest, e->getGroup());
            }
        }
    }
    for (const Element* e: also) {
        largest = std::max(largest, e->getGroup());
    }
    const Id id = largest + 1;
    Id seen = lastIssued.load();
    while (seen < id && !lastIssued.compare_exchange_weak(seen, id)) {}
    return id;
}

namespace {
void renumberIds(const std::vector<Element*>& elements, const std::set<Id>& ids, const Document& doc) {
    if (ids.empty()) {
        return;
    }
    const std::vector<const Element*> mine(elements.begin(), elements.end());
    std::map<Id, Id> next;
    for (Element* e: elements) {
        const Id g = e->getGroup();
        if (g == 0 || !ids.count(g)) {
            continue;
        }
        auto it = next.find(g);
        if (it == next.end()) {
            it = next.emplace(g, fresh(doc, mine)).first;
        }
        e->setGroup(it->second);
    }
}
}  // namespace

void renumber(const std::vector<Element*>& elements, const Document& doc) {
    std::set<Id> ids;
    for (const Element* e: elements) {
        if (e->getGroup() != 0) {
            ids.insert(e->getGroup());
        }
    }
    renumberIds(elements, ids, doc);
}

bool separate(const std::vector<Element*>& elements, const Layer& layer, const Document& doc) {
    std::set<Id> ids;
    for (const Element* e: elements) {
        if (e->getGroup() != 0) {
            ids.insert(e->getGroup());
        }
    }
    if (ids.empty()) {
        return false;
    }
    const std::set<const Element*> mine(elements.begin(), elements.end());
    std::set<Id> taken;
    for (const Element* e: layer.getElementsView()) {
        if (e->getGroup() != 0 && ids.count(e->getGroup()) && !mine.count(e)) {
            taken.insert(e->getGroup());
        }
    }
    renumberIds(elements, taken, doc);
    return !taken.empty();
}

InsertionOrderRef withMembers(const Layer& layer, const InsertionOrderRef& refs) {
    std::set<Id> ids;
    std::set<const Element*> in;
    for (const auto& r: refs) {
        in.insert(r.e);
        if (r.e->getGroup() != 0) {
            ids.insert(r.e->getGroup());
        }
    }
    if (ids.empty()) {
        return refs;
    }
    InsertionOrderRef result = refs;
    Element::Index pos = 0;
    for (const Element* e: layer.getElementsView()) {
        if (e->getGroup() != 0 && ids.count(e->getGroup()) && !in.count(e)) {
            result.emplace_back(e, pos);
        }
        ++pos;
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<Element*> withMembers(const Layer& layer, const std::vector<Element*>& elements) {
    std::set<Id> ids;
    for (const Element* e: elements) {
        if (e->getGroup() != 0) {
            ids.insert(e->getGroup());
        }
    }
    std::vector<Element*> result = elements;
    if (ids.empty()) {
        return result;
    }
    const std::set<const Element*> in(elements.begin(), elements.end());
    for (const Element* e: layer.getElementsView()) {
        if (e->getGroup() != 0 && ids.count(e->getGroup()) && !in.count(e)) {
            result.push_back(const_cast<Element*>(e));  // (the layer's own: the caller may change them)
        }
    }
    return result;
}

std::string clipboardNumbers(const std::vector<const Element*>& elements) {
    std::string text;
    for (const Element* e: elements) {
        text += (text.empty() ? "" : " ") + std::to_string(e->getGroup());
    }
    return text;
}

std::vector<Id> fromClipboard(const std::string& text, size_t count) {
    std::vector<Id> ids;
    std::istringstream in(text);
    unsigned long long n = 0;
    while (in >> n) {
        ids.push_back(static_cast<Id>(n));
    }
    if (!in.eof() || ids.size() != count) {
        return {};
    }
    return ids;
}

State stateOf(const std::vector<const Element*>& elements) {
    State s;
    if (elements.empty()) {
        return s;
    }
    const Id first = elements.front()->getGroup();
    s.oneGroup = first != 0 && elements.size() >= 2 &&
                 std::all_of(elements.begin(), elements.end(), [&](const Element* e) { return e->getGroup() == first; });
    s.canUngroup = std::any_of(elements.begin(), elements.end(), [](const Element* e) { return e->getGroup() != 0; });
    s.canGroup = elements.size() >= 2 && !s.oneGroup;
    return s;
}

GroupChangeUndoAction::GroupChangeUndoAction(PageRef page, std::vector<Change> changes, std::string text):
        UndoAction("GroupChangeUndoAction"), changes(std::move(changes)), text(std::move(text)) {
    this->page = std::move(page);
}

bool GroupChangeUndoAction::undo(Control*) {
    for (const Change& c: changes) {
        c.element->setGroup(c.before);
    }
    this->undone = true;
    return true;
}

bool GroupChangeUndoAction::redo(Control*) {
    for (const Change& c: changes) {
        c.element->setGroup(c.after);
    }
    this->undone = false;
    return true;
}

std::unique_ptr<UndoAction> group(const std::vector<Element*>& elements, const PageRef& page, const Document& doc) {
    const std::vector<const Element*> mine(elements.begin(), elements.end());
    if (!stateOf(mine).canGroup) {
        return nullptr;
    }
    const Id id = fresh(doc, mine);
    std::vector<GroupChangeUndoAction::Change> changes;
    for (Element* e: elements) {
        changes.push_back({e, e->getGroup(), id});
        e->setGroup(id);
    }
    return std::make_unique<GroupChangeUndoAction>(
            page, std::move(changes), QCoreApplication::translate("ElementGroups", "Group").toStdString());
}

std::unique_ptr<UndoAction> ungroup(const std::vector<Element*>& elements, const PageRef& page) {
    std::vector<GroupChangeUndoAction::Change> changes;
    for (Element* e: elements) {
        if (e->getGroup() != 0) {
            changes.push_back({e, e->getGroup(), 0});
            e->setGroup(0);
        }
    }
    if (changes.empty()) {
        return nullptr;
    }
    return std::make_unique<GroupChangeUndoAction>(
            page, std::move(changes), QCoreApplication::translate("ElementGroups", "Ungroup").toStdString());
}

}  // namespace xqt::groups
