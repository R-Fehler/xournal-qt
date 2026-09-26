#include "PdfBookmarks.h"

#include <algorithm>
#include <set>

#include "IncrementalPdf.h"

namespace xqt::PdfBookmarks {

namespace {
using OH = QPDFObjectHandle;

constexpr const char* TITLE = "Bookmarks";
constexpr const char* KEY = "/XournalQt";
constexpr int MAX_ITEMS = 100000;  ///< (a broken /Next chain that loops)

/// The page an outline entry goes to (a null handle: none that we recognise).
OH pageOf(OH item) {
    OH dest = item.getKey("/Dest");
    if (!dest.isArray()) {
        OH action = item.getKey("/A");
        if (action.isDictionary() && action.getKey("/S").isNameAndEquals("/GoTo")) {
            dest = action.getKey("/D");
        }
    }
    if (dest.isArray() && dest.getArrayNItems() > 0) {
        OH page = dest.getArrayItem(0);
        if (page.isDictionary() && page.isIndirect()) {
            return page;
        }
    }
    return OH::newNull();
}

std::string titleOf(OH item) {
    OH t = item.getKey("/Title");
    return t.isString() ? t.getUTF8Value() : std::string();
}

/// The children of an outline item, in order.
std::vector<OH> childrenOf(OH item) {
    std::vector<OH> out;
    std::set<QPDFObjGen> seen;
    for (OH c = item.getKey("/First"); c.isDictionary() && out.size() < MAX_ITEMS; c = c.getKey("/Next")) {
        if (c.isIndirect() && !seen.insert(c.getObjGen()).second) {
            break;
        }
        out.push_back(c);
    }
    return out;
}

bool isOurs(OH item) {
    if (!item.isDictionary()) {
        return false;
    }
    if (item.hasKey(KEY)) {
        return true;
    }
    if (titleOf(item) != TITLE) {
        return false;
    }
    const std::vector<OH> children = childrenOf(item);
    if (children.empty()) {
        return false;
    }
    for (OH c: children) {
        if (c.hasKey("/First") || pageOf(c).isNull()) {
            return false;
        }
    }
    return true;
}

/// Our item among the top-level entries (the last one that is ours), or null.
OH findItem(OH outlines) {
    OH found = OH::newNull();
    if (!outlines.isDictionary()) {
        return found;
    }
    for (OH c: childrenOf(outlines)) {
        if (isOurs(c)) {
            found = c;
        }
    }
    return found;
}

std::vector<Entry> entriesOf(OH item) {
    std::vector<Entry> out;
    for (OH c: childrenOf(item)) {
        out.push_back({pageOf(c), titleOf(c)});
    }
    return out;
}

bool same(const std::vector<Entry>& a, const std::vector<Entry>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].title != b[i].title || a[i].page.isNull() || b[i].page.isNull() ||
            a[i].page.getObjGen() != b[i].page.getObjGen()) {
            return false;
        }
    }
    return true;
}

/// Visible entries an item adds to its parent's /Count: itself and, when open, its children.
long long visibleOf(OH item) {
    if (!item.isDictionary()) {
        return 0;
    }
    OH count = item.getKey("/Count");
    return 1 + (count.isInteger() && count.getIntValue() > 0 ? count.getIntValue() : 0);
}

class Writer {
public:
    Writer(QPDF& pdf, IncrementalPdf::Update* update): pdf(pdf), u(update) {}

    void touch(OH o) {
        if (u) {
            u->touch(o);
        }
    }
    OH add(OH value) { return u ? u->add(value) : pdf.makeIndirectObject(value); }

    /// New children of `item`, linked to each other.
    void fill(OH item, const std::vector<Entry>& entries) {
        OH prev = OH::newNull();
        for (const Entry& e: entries) {
            OH dest = OH::newArray();
            dest.appendItem(e.page);
            dest.appendItem(OH::newName("/XYZ"));
            dest.appendItem(OH::newNull());
            dest.appendItem(OH::newNull());
            dest.appendItem(OH::newNull());
            OH child = OH::newDictionary();
            child.replaceKey("/Title", OH::newUnicodeString(e.title));
            child.replaceKey("/Parent", item);
            child.replaceKey("/Dest", dest);
            child = add(child);
            if (prev.isNull()) {
                item.replaceKey("/First", child);
            } else {
                prev.replaceKey("/Next", child);
                child.replaceKey("/Prev", prev);
            }
            prev = child;
        }
        item.replaceKey("/Last", prev);
        item.replaceKey("/Count", OH::newInteger(static_cast<long long>(entries.size())));
    }

    void addToCount(OH outlines, long long delta) {
        OH count = outlines.getKey("/Count");
        if (delta != 0 && count.isInteger()) {
            outlines.replaceKey("/Count", OH::newInteger(std::max(0LL, count.getIntValue() + delta)));
        }
    }

    QPDF& pdf;
    IncrementalPdf::Update* u;
};
}  // namespace

std::vector<Entry> read(QPDF& pdf) {
    OH item = findItem(pdf.getRoot().getKey("/Outlines"));
    return item.isNull() ? std::vector<Entry>() : entriesOf(item);
}

bool write(QPDF& pdf, const std::vector<Entry>& entries, IncrementalPdf::Update* update) {
    Writer w(pdf, update);
    OH root = pdf.getRoot();
    OH outlines = root.getKey("/Outlines");
    auto touchOutlines = [&] { w.touch(outlines.isIndirect() ? outlines : root); };
    OH item = findItem(outlines);
    if (item.isNull() && entries.empty()) {
        return false;
    }
    if (!item.isNull() && item.hasKey(KEY) && same(entriesOf(item), entries)) {
        return false;
    }
    if (!item.isNull() && entries.empty()) {
        // Removed: its neighbours and the outline dictionary close the gap
        const long long before = visibleOf(item);
        OH prev = item.getKey("/Prev");
        OH next = item.getKey("/Next");
        touchOutlines();
        if (prev.isDictionary()) {
            w.touch(prev);
            if (next.isDictionary()) {
                prev.replaceKey("/Next", next);
            } else {
                prev.removeKey("/Next");
            }
        } else if (next.isDictionary()) {
            outlines.replaceKey("/First", next);
        } else {
            outlines.removeKey("/First");
        }
        if (next.isDictionary()) {
            w.touch(next);
            if (prev.isDictionary()) {
                next.replaceKey("/Prev", prev);
            } else {
                next.removeKey("/Prev");
            }
        } else if (prev.isDictionary()) {
            outlines.replaceKey("/Last", prev);
        } else {
            outlines.removeKey("/Last");
        }
        w.addToCount(outlines, -before);
        return true;
    }
    if (!item.isNull()) {
        // Written again in its place (its old children are left to the next full write)
        const long long before = visibleOf(item);
        w.touch(item);
        item.replaceKey(KEY, OH::newBool(true));
        item.replaceKey("/Title", OH::newUnicodeString(TITLE));
        w.fill(item, entries);
        const long long delta = visibleOf(item) - before;
        if (delta != 0 && outlines.getKey("/Count").isInteger()) {
            touchOutlines();
            w.addToCount(outlines, delta);
        }
        return true;
    }
    // New: the last top-level item
    if (!outlines.isDictionary()) {
        w.touch(root);
        OH dict = OH::newDictionary();
        dict.replaceKey("/Type", OH::newName("/Outlines"));
        dict.replaceKey("/Count", OH::newInteger(0));
        outlines = w.add(dict);
        root.replaceKey("/Outlines", outlines);
    } else {
        touchOutlines();
    }
    OH dict = OH::newDictionary();
    dict.replaceKey("/Title", OH::newUnicodeString(TITLE));
    dict.replaceKey("/Parent", outlines);
    dict.replaceKey(KEY, OH::newBool(true));
    item = w.add(dict);
    w.fill(item, entries);
    OH last = outlines.getKey("/Last");
    if (last.isDictionary()) {
        w.touch(last);
        last.replaceKey("/Next", item);
        item.replaceKey("/Prev", last);
    } else {
        outlines.replaceKey("/First", item);
    }
    outlines.replaceKey("/Last", item);
    if (!outlines.getKey("/Count").isInteger()) {
        // (optional while nothing is open; now something is: count what is visible)
        long long visible = 0;
        for (OH c: childrenOf(outlines)) {
            visible += visibleOf(c);
        }
        outlines.replaceKey("/Count", OH::newInteger(visible));
    } else {
        w.addToCount(outlines, visibleOf(item));
    }
    return true;
}

}  // namespace xqt::PdfBookmarks
