#include "LibraryInkJob.h"

#include <map>
#include <optional>
#include <shared_mutex>

#include <QThreadPool>
#include <QTimer>

#include "model/Document.h"
#include "model/XojPage.h"
#include "hwr/LanguagePlan.h"
#include "session/DocumentSession.h"

#include "FileStamps.h"
#include "LibraryIndex.h"

namespace xqt {

namespace {
constexpr size_t AT_ONCE = 2;
constexpr double LIBRARY_PRIORITY = 2e6;  ///< after every open document (InkTextIndexer: 0 and 1e6 + distance)
}  // namespace

struct LibraryInkJob::Current {
    fs::path file;
    QString stamp;
    std::shared_ptr<Document> doc;
    size_t pages = 0;
    size_t nextPage = 0;
    std::vector<std::vector<hwr::LineRef>> lines;
    std::map<quint64, size_t> jobs;  ///< job id -> page
    bool complete = true;
    std::shared_ptr<hwr::LanguagePlan> plan = std::make_shared<hwr::LanguagePlan>();
};

LibraryInkJob::LibraryInkJob(hwr::InkRecognitionService& service, QObject* parent):
        QObject(parent), service(service), loader(std::make_unique<QThreadPool>()) {
    loader->setMaxThreadCount(1);
    loader->setThreadPriority(QThread::LowestPriority);
    connect(&service, &hwr::InkRecognitionService::recognizerChanged, this, &LibraryInkJob::check);
}

LibraryInkJob::~LibraryInkJob() {
    stop();
    loader->waitForDone();
}

void LibraryInkJob::setIndex(LibraryIndex* i) {
    if (index == i) {
        return;
    }
    stop();
    index = i;
    check();
}

void LibraryInkJob::setEnabled(bool on) {
    enabled = on;
    check();
}

bool LibraryInkJob::allowed() const { return enabled && index && service.ready(); }

void LibraryInkJob::check() {
    if (!allowed()) {
        if (running()) {
            stop();
            Q_EMIT progress();
        }
        return;
    }
    if (!current) {
        waiting.clear();  // (the candidates anew: what changed since)
        next();
    }
}

void LibraryInkJob::stop() {
    ++generation;
    service.cancel(this);
    current.reset();
    waiting.clear();
}

void LibraryInkJob::next() {
    if (current || !allowed()) {
        return;
    }
    if (waiting.empty()) {
        for (fs::path& f: index->inkCandidates(service.recognizerId(), true)) {
            auto it = tried.find(f);
            if (it == tried.end() || it->second != fileStamp(f)) {  // (one that failed is not read again and again)
                waiting.push_back(std::move(f));
            }
        }
    }
    if (waiting.empty()) {
        Q_EMIT progress();
        return;
    }
    current = std::make_shared<Current>();
    current->file = waiting.front();
    waiting.pop_front();
    current->stamp = fileStamp(current->file);
    tried[current->file] = current->stamp;
    Q_EMIT progress();
    loader->start([this, doc = current, gen = generation] {
        auto r = DocumentSession::loadFile(doc->file);
        doc->doc = std::shared_ptr<Document>(std::move(r.document));
        QMetaObject::invokeMethod(
                this,
                [this, doc, gen] {
                    if (gen == generation && doc == current) {
                        loaded(doc);
                    }
                },
                Qt::QueuedConnection);
    });
}

void LibraryInkJob::loaded(std::shared_ptr<Current> doc) {
    if (!doc->doc || !index) {
        current.reset();
        QTimer::singleShot(0, this, &LibraryInkJob::next);
        return;
    }
    {
        std::shared_lock lock(*doc->doc);
        doc->pages = doc->doc->getPageCount();
    }
    doc->lines.resize(doc->pages);
    // Lines read before (an older version of the file) are not read again; its language as chosen, or as decided
    if (auto before = index->inkText().find(doc->file)) {
        const bool same = before->recognizer == service.recognizerId();
        doc->plan = std::make_shared<hwr::LanguagePlan>(hwr::LanguagePlan::choiceNamed(before->languageChoice),
                                                        same ? before->language : QString());
        for (const auto& page: before->pages) {
            for (const hwr::LineRef& l: page) {
                if (l.result && same) {
                    service.remember(l.hash, l.result);
                }
            }
        }
    }
    pump();
}

void LibraryInkJob::pump() {
    if (!current || !current->doc) {
        return;
    }
    Current& c = *current;
    while (c.jobs.size() < AT_ONCE && c.nextPage < c.pages) {
        const size_t page = c.nextPage++;
        std::vector<hwr::InkStroke> strokes;
        {
            std::shared_lock lock(*c.doc);
            strokes = hwr::strokesOf(*c.doc->getPage(page));
        }
        if (strokes.empty()) {
            continue;
        }
        hwr::InkRecognitionService::Job job;
        job.priority = LIBRARY_PRIORITY + static_cast<double>(page);
        job.strokes = std::move(strokes);
        job.plan = c.plan;
        const quint64 id = service.submit(this, std::move(job), [this, doc = current](hwr::PageResult r) {
            if (doc != current) {
                return;
            }
            auto it = doc->jobs.find(r.id);
            if (it == doc->jobs.end()) {
                return;
            }
            doc->lines[it->second] = std::move(r.lines);
            doc->complete = doc->complete && r.complete;
            doc->jobs.erase(it);
            ++pagesDone;
            Q_EMIT progress();
            pump();
        });
        c.jobs[id] = page;
    }
    if (c.jobs.empty() && c.nextPage >= c.pages) {
        finish();
    }
}

void LibraryInkJob::finish() {
    Current& c = *current;
    if (index && fileStamp(c.file) == c.stamp) {  // (not changed while it was read)
        InkDoc doc;
        doc.stamp = c.stamp;
        doc.recognizer = service.recognizerId();
        doc.complete = c.complete;
        doc.language = c.plan->decided();
        doc.pages = std::move(c.lines);
        index->inkText().put(c.file, std::move(doc));
    }
    current.reset();
    Q_EMIT progress();
    QTimer::singleShot(0, this, &LibraryInkJob::next);
}

}  // namespace xqt
