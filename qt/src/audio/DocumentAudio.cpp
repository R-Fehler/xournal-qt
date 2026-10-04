#include "DocumentAudio.h"

#include <algorithm>
#include <map>

#include <QCoreApplication>
#include <QDateTime>

#include "model/AudioContent.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"

namespace xqt::audio {

std::vector<std::string> parseMemos(std::string_view attribute) {
    std::vector<std::string> names;
    size_t from = 0;
    while (from <= attribute.size()) {
        const size_t to = std::min(attribute.find(MEMO_SEPARATOR, from), attribute.size());
        if (to > from) {
            names.emplace_back(attribute.substr(from, to - from));
        }
        from = to + 1;
    }
    return names;
}

std::string formatMemos(const std::vector<std::string>& names) {
    std::string out;
    for (const auto& n: names) {
        if (n.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += MEMO_SEPARATOR;
        }
        out += n;
    }
    return out;
}

std::vector<std::string> memosOf(const XojPage& page) { return parseMemos(page.getAudioMemos()); }

std::string newRecordingName(const QDateTime& now, const std::function<bool(const std::string&)>& taken) {
    const std::string stem = now.toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")).toStdString();
    std::string name = stem + ".ogg";
    for (int i = 2; taken && taken(name); ++i) {
        name = stem + "-" + std::to_string(i) + ".ogg";
    }
    return name;
}

const AudioContent* audioOf(const Element* e) {
    if (!e) {
        return nullptr;
    }
    const AudioContent* a = nullptr;
    if (e->getType() == ELEMENT_STROKE) {
        a = static_cast<const Stroke*>(e);
    } else if (e->getType() == ELEMENT_TEXT) {
        a = static_cast<const Text*>(e);
    }
    return a && !a->getAudioFilename().empty() ? a : nullptr;
}

std::string nameOf(const AudioContent& audio) {
    const std::u8string s = audio.getAudioFilename().generic_u8string();
    return {s.begin(), s.end()};
}

void stamp(AudioContent& audio, const std::string& name, size_t ts) {
    audio.setAudioFilename(fs::path(std::u8string(name.begin(), name.end())));
    audio.setTimestamp(ts);
}

namespace {
/// Every element with a recording on every page (`f(pageIndex, element, audio)`).
template <typename F>
void forEachStamp(const Document& doc, F&& f) {
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        const PageRef p = doc.getPage(i);
        for (const Layer* l: p->getLayersView()) {
            for (const Element* e: l->getElementsView()) {
                if (const AudioContent* a = audioOf(e)) {
                    f(i, e, *a);
                }
            }
        }
    }
}

void addPage(std::vector<size_t>& pages, size_t page) {
    if (auto it = std::lower_bound(pages.begin(), pages.end(), page); it == pages.end() || *it != page) {
        pages.insert(it, page);
    }
}
}  // namespace

std::vector<Recording> recordingsOf(const Document& doc) {
    std::map<std::string, Recording> byName;
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        for (const auto& n: memosOf(*doc.getPage(i))) {
            Recording& r = byName[n];
            r.name = n;
            addPage(r.pages, i);
            addPage(r.memoPages, i);
        }
    }
    forEachStamp(doc, [&](size_t page, const Element*, const AudioContent& a) {
        const std::string n = nameOf(a);
        Recording& r = byName[n];
        r.name = n;
        addPage(r.pages, page);
        const size_t ts = a.getTimestamp();
        r.firstTs = r.elements == 0 ? ts : std::min(r.firstTs, ts);
        r.lastTs = std::max(r.lastTs, ts);
        ++r.elements;
    });
    std::vector<Recording> out;
    out.reserve(byName.size());
    for (auto& [n, r]: byName) {
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<Moment> momentsOf(const Document& doc, const std::string& name) {
    std::vector<Moment> out;
    forEachStamp(doc, [&](size_t page, const Element* e, const AudioContent& a) {
        if (nameOf(a) == name) {
            out.push_back({page, a.getTimestamp(), e});
        }
    });
    std::stable_sort(out.begin(), out.end(), [](const Moment& a, const Moment& b) { return a.ts < b.ts; });
    return out;
}

std::optional<Hit> hitAt(const XojPage& page, double x, double y, double radius) {
    std::optional<Hit> best;
    double bestDistance = radius;
    for (const Layer* l: page.getLayersView()) {
        if (!l->isVisible()) {
            continue;
        }
        for (const Element* e: l->getElementsView()) {
            const AudioContent* a = audioOf(e);
            if (!a || !e->intersectsArea(x - radius, y - radius, 2 * radius, 2 * radius)) {
                continue;  // (the rough check first, as upstream: distanceTo is expensive on long strokes)
            }
            if (const double d = e->distanceTo(x, y); d < bestDistance || (d == 0 && !best)) {
                bestDistance = d;
                best = Hit{nameOf(*a), a->getTimestamp(), e};
            }
        }
    }
    return best;
}

// --- undo ------------------------------------------------------------------------------------------------------------

MemoUndoAction::MemoUndoAction(PageRef page, std::string before, std::string after, std::string text, Apply apply):
        UndoAction("MemoUndoAction"),
        target(std::move(page)),
        before(std::move(before)),
        after(std::move(after)),
        text(std::move(text)),
        apply(std::move(apply)) {}

bool MemoUndoAction::undo(Control*) {
    apply(target, before);
    this->undone = true;
    return true;
}

bool MemoUndoAction::redo(Control*) {
    apply(target, after);
    this->undone = false;
    return true;
}

Removed removeRecording(Document& doc, const std::string& name) {
    Removed r;
    r.name = name;
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        const PageRef p = doc.getPage(i);
        auto memos = memosOf(*p);
        if (std::erase(memos, name) > 0) {
            r.memos.emplace_back(p, p->getAudioMemos());
            p->setAudioMemos(formatMemos(memos));
        }
        for (const Layer* l: p->getLayersView()) {
            for (const Element* e: l->getElementsView()) {
                if (const AudioContent* a = audioOf(e); a && nameOf(*a) == name) {
                    auto* audio = const_cast<AudioContent*>(a);
                    r.stamps.push_back({audio, nameOf(*a), a->getTimestamp()});
                    audio->setAudioFilename({});
                    audio->setTimestamp(0);
                }
            }
        }
    }
    return r;
}

void restore(const Removed& removed) {
    for (const auto& s: removed.stamps) {
        stamp(*s.audio, s.fn, s.ts);
    }
    for (const auto& [page, memos]: removed.memos) {
        page->setAudioMemos(memos);
    }
}

RemoveRecordingUndoAction::RemoveRecordingUndoAction(Removed removed, Locked locked):
        UndoAction("RemoveRecordingUndoAction"), removed(std::move(removed)), locked(std::move(locked)) {}

bool RemoveRecordingUndoAction::undo(Control*) {
    locked([this] { restore(removed); });
    this->undone = true;
    return true;
}

bool RemoveRecordingUndoAction::redo(Control*) {
    locked([this] {
        for (const auto& s: removed.stamps) {
            s.audio->setAudioFilename({});
            s.audio->setTimestamp(0);
        }
        for (const auto& [page, before]: removed.memos) {
            auto memos = parseMemos(before);
            std::erase(memos, removed.name);
            page->setAudioMemos(formatMemos(memos));
        }
    });
    this->undone = false;
    return true;
}

std::string RemoveRecordingUndoAction::getText() {
    return QCoreApplication::translate("DocumentAudio", "Remove recording").toStdString();
}

}  // namespace xqt::audio
