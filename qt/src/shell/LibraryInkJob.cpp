#include "LibraryInkJob.h"

#include <map>
#include <optional>
#include <shared_mutex>

#include <QDir>
#include <QFile>
#include <QThreadPool>

#include "model/Document.h"
#include "model/XojPage.h"
#include "hwr/LanguagePlan.h"
#include "session/DocumentSession.h"

#include "Library.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace xqt {

namespace {
std::function<bool()>& powerSource() {
    static std::function<bool()> f;
    return f;
}
constexpr size_t AT_ONCE = 2;
constexpr double LIBRARY_PRIORITY = 2e6;  ///< after every open document (InkTextIndexer: 0 and 1e6 + distance)

bool systemOnMains() {
#ifdef _WIN32
    SYSTEM_POWER_STATUS status;
    return !GetSystemPowerStatus(&status) || status.ACLineStatus != 0;
#elif defined(__linux__) && !defined(__ANDROID__)
    const QDir dir(QStringLiteral("/sys/class/power_supply"));
    bool battery = false;
    for (const QString& name: dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        auto read = [&](const char* what) {
            QFile f(dir.filePath(name) + u'/' + QLatin1String(what));
            return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readAll()).trimmed() : QString();
        };
        const QString type = read("type");
        if (type == QLatin1String("Battery")) {
            battery = true;
        } else if ((type == QLatin1String("Mains") || type.startsWith(QLatin1String("USB"))) &&
                   read("online") == QLatin1String("1")) {
            return true;
        }
    }
    return !battery;  // (a computer without a battery is on mains)
#else
    return false;  // (phones: not yet)
#endif
}
}  // namespace

bool LibraryInkJob::onMains() { return powerSource() ? powerSource()() : systemOnMains(); }

void LibraryInkJob::setPowerSource(std::function<bool()> source) { powerSource() = std::move(source); }

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
    power.setInterval(60000);
    connect(&power, &QTimer::timeout, this, &LibraryInkJob::check);
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
    if (on) {
        power.start();
    } else {
        power.stop();
    }
    check();
}

bool LibraryInkJob::allowed() const { return enabled && index && service.ready() && onMains(); }

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
        ++docsDone;
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
    ++docsDone;
    current.reset();
    Q_EMIT progress();
    QTimer::singleShot(0, this, &LibraryInkJob::next);
}

}  // namespace xqt
