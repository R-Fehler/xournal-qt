#include "MdPassages.h"

#include <algorithm>

namespace xqt::md {

namespace {

bool isList(BlockKind k) { return k == BlockKind::BulletList || k == BlockKind::OrderedList; }

void appendRuns(Passage& p, const std::vector<Run>& runs) {
    for (const Run& r: runs) {
        // The same length as its source: byte for byte; else (an entity, "&amp;") each byte shows all of it
        const bool exact = r.source != NO_SOURCE && r.text.size() == r.sourceLength;
        for (size_t i = 0; i < r.text.size(); ++i) {
            p.from.push_back(r.source == NO_SOURCE ? NO_SOURCE : exact ? r.source + i : r.source);
            p.to.push_back(r.source == NO_SOURCE ? NO_SOURCE : exact ? r.source + i + 1 : r.source + r.sourceLength);
        }
        p.text += r.text;
    }
}

/// A part of a passage: '\n' after the part before (never matched by the search: a hit does not run into the next
/// part)
void appendPart(Passage& p, const std::vector<Run>& runs) {
    if (!p.text.empty() && !runs.empty()) {
        p.text += '\n';
        p.from.push_back(NO_SOURCE);
        p.to.push_back(NO_SOURCE);
    }
    appendRuns(p, runs);
}

/// The text of a block and everything in it, except lists (a list item's own text)
void appendBlock(Passage& p, const Block& b) {
    if (isList(b.kind)) {
        return;
    }
    appendPart(p, b.runs);
    for (const Block& c: b.children) {
        appendBlock(p, c);
    }
}

void add(std::vector<Passage>& out, Passage p, const Block& b, const std::vector<size_t>& path) {
    if (p.text.empty()) {
        return;
    }
    p.begin = b.textBegin;
    p.end = b.textEnd;
    p.path = path;
    out.push_back(std::move(p));
}

void walk(const Block& b, std::vector<size_t>& path, std::vector<Passage>& out, bool inHead) {
    Passage p;
    switch (b.kind) {
        case BlockKind::Heading:
            p.kind = Passage::Kind::Heading;
            p.level = b.level;
            appendRuns(p, b.runs);
            add(out, std::move(p), b, path);
            return;
        case BlockKind::Paragraph:
        case BlockKind::Html:
            appendRuns(p, b.runs);
            add(out, std::move(p), b, path);
            return;
        case BlockKind::CodeBlock:
            p.kind = Passage::Kind::Code;
            appendRuns(p, b.runs);
            add(out, std::move(p), b, path);
            break;  // (no children)
        case BlockKind::ListItem:
            p.kind = Passage::Kind::ListItem;
            for (const Block& c: b.children) {
                appendBlock(p, c);
            }
            add(out, std::move(p), b, path);
            break;  // then the items of the lists in it
        case BlockKind::TableRow:
            p.kind = Passage::Kind::TableRow;
            p.header = inHead;
            for (const Block& cell: b.children) {
                appendPart(p, cell.runs);
            }
            add(out, std::move(p), b, path);
            return;
        default:
            break;
    }
    for (size_t i = 0; i < b.children.size(); ++i) {
        const Block& c = b.children[i];
        if (b.kind == BlockKind::ListItem && !isList(c.kind)) {
            continue;  // (its own text: in its passage)
        }
        path.push_back(i);
        walk(c, path, out, inHead || b.kind == BlockKind::TableHead);
        path.pop_back();
    }
}

/// The block at a path, and the blocks above it (outermost first; the root not included)
std::vector<const Block*> chainOf(const Document& doc, const std::vector<size_t>& path) {
    std::vector<const Block*> chain;
    const Block* b = &doc.root;
    for (size_t i: path) {
        if (i >= b->children.size()) {
            return {};
        }
        b = &b->children[i];
        chain.push_back(b);
    }
    return chain;
}

Block withoutChildren(const Block& b) {
    Block copy;
    copy.kind = b.kind;
    copy.level = b.level;
    copy.tight = b.tight;
    copy.mark = b.mark;
    copy.start = b.start;
    copy.align = b.align;
    copy.textBegin = b.textBegin;
    copy.textEnd = b.textEnd;
    return copy;
}

}  // namespace

std::vector<Passage> passages(const Document& doc) {
    std::vector<Passage> out;
    std::vector<size_t> path;
    walk(doc.root, path, out, false);
    return out;
}

std::pair<size_t, size_t> sourceRange(const Passage& p, size_t a, size_t b) {
    size_t begin = NO_SOURCE, end = NO_SOURCE;
    b = std::min(b, p.from.size());
    for (size_t i = a; i < b; ++i) {
        if (p.from[i] != NO_SOURCE) {
            begin = begin == NO_SOURCE ? p.from[i] : std::min(begin, p.from[i]);
            end = end == NO_SOURCE ? p.to[i] : std::max(end, p.to[i]);
        }
    }
    return {begin, end};
}

Document snippet(const Document& doc, const Passage& p) {
    Document out;
    out.links = doc.links;
    out.wikiLinks = doc.wikiLinks;
    const std::vector<const Block*> chain = chainOf(doc, p.path);
    if (chain.empty()) {
        return out;
    }
    Block inner = *chain.back();
    if (inner.kind == BlockKind::ListItem) {
        std::erase_if(inner.children, [](const Block& c) { return isList(c.kind); });
    }
    // Outwards: the list of an item (numbered on as it is), a table (with its header), quotes. Not the list item
    // a list is in: a nested item is shown alone.
    for (size_t k = chain.size() - 1; k-- > 0;) {
        const Block& a = *chain[k];
        if (a.kind == BlockKind::ListItem) {
            break;
        }
        if (isList(a.kind)) {
            Block list = withoutChildren(a);
            list.start = a.start + static_cast<unsigned>(p.path[k + 1]);
            list.children.push_back(std::move(inner));
            inner = std::move(list);
        } else if (a.kind == BlockKind::Quote) {
            Block quote = withoutChildren(a);
            quote.children.push_back(std::move(inner));
            inner = std::move(quote);
        } else if (a.kind == BlockKind::Table && inner.kind == BlockKind::TableRow) {
            Block table = withoutChildren(a);
            Block head;
            head.kind = BlockKind::TableHead;
            if (p.header) {
                head.children.push_back(std::move(inner));
                table.children.push_back(std::move(head));
            } else {
                // The header row above it
                for (const Block& part: a.children) {
                    if (part.kind == BlockKind::TableHead && !part.children.empty()) {
                        head.children.push_back(part.children.front());
                    }
                }
                if (!head.children.empty()) {
                    table.children.push_back(std::move(head));
                }
                Block body;
                body.kind = BlockKind::TableBody;
                body.children.push_back(std::move(inner));
                table.children.push_back(std::move(body));
            }
            inner = std::move(table);
        }
    }
    out.root.children.push_back(std::move(inner));
    out.root.textBegin = out.root.children.front().textBegin;
    out.root.textEnd = out.root.children.front().textEnd;
    return out;
}

std::vector<LinkTarget> linksOf(const Document& doc) {
    std::vector<LinkTarget> out;
    std::vector<const Block*> todo{&doc.root};
    while (!todo.empty()) {
        const Block* b = todo.back();
        todo.pop_back();
        for (const Run& r: b->runs) {
            const auto link = static_cast<size_t>(r.link);
            if ((r.flags & Link) && !(r.flags & Image) && r.link >= 0 && link < doc.links.size()) {
                LinkTarget t{doc.links[link], link < doc.wikiLinks.size() && doc.wikiLinks[link]};
                if (!t.target.empty() && std::find(out.begin(), out.end(), t) == out.end()) {
                    out.push_back(std::move(t));
                }
            }
        }
        for (auto it = b->children.rbegin(); it != b->children.rend(); ++it) {
            todo.push_back(&*it);  // (in order)
        }
    }
    return out;
}

}  // namespace xqt::md
