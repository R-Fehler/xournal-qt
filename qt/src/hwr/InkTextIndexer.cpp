#include "InkTextIndexer.h"

#include <algorithm>
#include <limits>
#include <shared_mutex>

#include "model/Document.h"
#include "model/XojPage.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/DocumentTextIndex.h"

namespace xqt::hwr {

namespace {
int delayMs = InkTextIndexer::DELAY_MS;
constexpr size_t AT_ONCE = 2;  ///< pages with the worker at a time
}  // namespace

void InkTextIndexer::setDelay(int ms) { delayMs = ms; }

InkTextIndexer::InkTextIndexer(DocumentSession& session, InkRecognitionService& service, QObject* parent):
        QObject(parent), session(session), service(service) {
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, this, [this] {
        started = true;
        scan();
        pump();
    });
    timer.start(delayMs);
    connect(&session, &DocumentSession::pageRevisionsChanged, this, [this] {
        this->service.noteActivity();  // (the user writes: the worker waits)
        timer.start(delayMs);
    });
    connect(&session, &DocumentSession::currentPageChanged, this, [this] { pump(); });
    connect(&service, &InkRecognitionService::recognizerChanged, this, [this] {
        if (started) {
            scan();
            pump();
        }
    });
    // The index read the document anew (another content): it lost the words
    connect(&session.search().textIndex(), &DocumentTextIndex::reset, this, [this] { reapply(); });
}

InkTextIndexer::~InkTextIndexer() {
    service.cancel(this);
}

void InkTextIndexer::start() {
    timer.stop();
    started = true;
    scan();
    pump();
}

void InkTextIndexer::setFocused(bool f) {
    focused = f;
}

double InkTextIndexer::priorityOf(size_t page) const {
    const size_t current = session.getCurrentPageNo();
    const double distance = page > current ? static_cast<double>(page - current) : static_cast<double>(current - page);
    return (focused ? 0.0 : 1e6) + distance;
}

void InkTextIndexer::scan() {
    Document* doc = session.getDocument();
    std::set<const XojPage*> present;
    {
        std::shared_lock lock(*doc);
        for (size_t i = 0; i < doc->getPageCount(); ++i) {
            const PageRef ref = doc->getPage(i);
            present.insert(ref.get());
            const quint64 revision = session.pageRevision(i);
            auto it = indexed.find(ref.get());
            const bool busy = std::any_of(outstanding.begin(), outstanding.end(), [&](const auto& o) {
                return o.second.ref == ref && o.second.revision == revision;
            });
            if (busy) {
                continue;
            }
            if (it == indexed.end() || it->second.revision != revision || (!it->second.complete && service.ready())) {
                dirty.insert(ref.get());
            }
        }
    }
    // Pages that are gone
    for (auto it = indexed.begin(); it != indexed.end();) {
        it = present.count(it->first) ? std::next(it) : indexed.erase(it);
    }
    for (auto it = dirty.begin(); it != dirty.end();) {
        it = present.count(*it) ? std::next(it) : dirty.erase(it);
    }
    Q_EMIT progress();
}

void InkTextIndexer::pump() {
    while (outstanding.size() < AT_ONCE && !dirty.empty()) {
        // The dirty page nearest to the one in view
        Document* doc = session.getDocument();
        PageRef ref;
        size_t index = 0;
        quint64 revision = 0;
        std::vector<InkStroke> strokes;
        {
            std::shared_lock lock(*doc);
            const size_t current = session.getCurrentPageNo();
            size_t best = std::numeric_limits<size_t>::max();
            for (size_t i = 0; i < doc->getPageCount(); ++i) {
                const PageRef p = doc->getPage(i);
                if (!dirty.count(p.get())) {
                    continue;
                }
                const size_t d = i > current ? i - current : current - i;
                if (d < best) {
                    best = d;
                    ref = p;
                    index = i;
                }
            }
            if (!ref) {
                dirty.clear();
                break;
            }
            revision = session.pageRevision(index);
            strokes = strokesOf(*ref);
        }
        dirty.erase(ref.get());
        if (strokes.empty()) {
            // (no handwriting: nothing to read)
            indexed[ref.get()] = Indexed{ref, revision, true, nullptr, {}};
            session.search().textIndex().setInk(index, nullptr);
            ++readCount;
            continue;
        }
        InkRecognitionService::Job job;
        job.priority = priorityOf(index);
        job.strokes = std::move(strokes);
        const quint64 id = service.submit(this, std::move(job), [this](PageResult r) {
            const quint64 jobId = r.id;
            received(jobId, std::move(r));
        });
        outstanding[id] = {ref, revision};
    }
    Q_EMIT progress();
}

void InkTextIndexer::received(quint64 id, PageResult result) {
    auto it = outstanding.find(id);
    if (it == outstanding.end()) {
        return;
    }
    const Outstanding o = it->second;
    outstanding.erase(it);
    // Where the page is now, and whether it is still as it was read
    Document* doc = session.getDocument();
    std::optional<size_t> index;
    {
        std::shared_lock lock(*doc);
        for (size_t i = 0; i < doc->getPageCount(); ++i) {
            if (doc->getPage(i) == o.ref) {
                index = i;
                break;
            }
        }
    }
    if (index) {
        if (session.pageRevision(*index) == o.revision) {
            auto text = result.text && !result.text->empty() ? result.text : nullptr;
            indexed[o.ref.get()] = Indexed{o.ref, o.revision, result.complete, text, std::move(result.lines)};
            session.search().textIndex().setInk(*index, text);
            ++readCount;
        } else {
            dirty.insert(o.ref.get());  // (changed meanwhile: read again)
        }
    }
    pump();
}

void InkTextIndexer::reapply() {
    Document* doc = session.getDocument();
    std::vector<std::pair<size_t, std::shared_ptr<const ink::PageText>>> texts;
    {
        std::shared_lock lock(*doc);
        for (size_t i = 0; i < doc->getPageCount(); ++i) {
            auto it = indexed.find(doc->getPage(i).get());
            if (it != indexed.end() && it->second.revision == session.pageRevision(i)) {
                texts.emplace_back(i, it->second.text);
            }
        }
    }
    for (auto& [i, text]: texts) {
        session.search().textIndex().setInk(i, text);
    }
    if (started) {
        scan();
        pump();
    }
}

std::vector<InkTextIndexer::PageLines> InkTextIndexer::pages() const {
    std::vector<PageLines> out;
    Document* doc = session.getDocument();
    std::shared_lock lock(*doc);
    out.resize(doc->getPageCount());
    for (size_t i = 0; i < out.size(); ++i) {
        auto it = indexed.find(doc->getPage(i).get());
        if (it != indexed.end() && it->second.revision == session.pageRevision(i)) {
            out[i] = {true, it->second.complete, it->second.lines};
        }
    }
    return out;
}

}  // namespace xqt::hwr
