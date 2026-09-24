#include "LibraryArchive.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <map>
#include <set>

#include <QCoreApplication>
#include <QDate>
#include <QPointer>
#include <QStringList>
#include <QThread>
#include <QThreadPool>

#include "model/Document.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "util/Util.h"

#include "DocumentFiles.h"
#include "config.h"

namespace xqt {

namespace {

QString tr(const char* text) { return QCoreApplication::translate("LibraryArchive", text); }

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

fs::path canonicalOf(const fs::path& p) {
    std::error_code ec;
    const fs::path c = fs::weakly_canonical(p, ec);
    return ec ? p.lexically_normal() : c;
}

/// `inner` is `outer` or inside it.
bool within(const fs::path& inner, const fs::path& outer) {
    const fs::path rel = canonicalOf(inner).lexically_relative(canonicalOf(outer));
    return !rel.empty() && *rel.begin() != "..";
}

fs::path fromU8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string u8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::string readme(const LibraryArchive::Plan& plan, const LibraryArchive::Summary& s) {
    std::string t;
    t += tr("Archive of \"%1\", made with %2 on %3.")
                 .arg(QString::fromStdString(plan.name), QString::fromUtf8(PROJECT_STRING),
                      QDate::currentDate().toString(Qt::ISODate))
                 .toStdString() +
         "\n\n";
    t += tr("The PDF files are archive PDFs, made for keeping (PDF/A-3). They stay readable for decades in any PDF "
            "viewer. The ink and notes are merged into the pages, so every viewer shows them and none can hide or "
            "lose them.")
                 .toStdString() +
         "\n\n";
    t += tr("Each PDF also carries its full Xournal document as an attachment (document.xopp). To edit the notes "
            "again, open the PDF in xournal-qt: strokes, text and layers are editable as before. Other PDF apps show "
            "document.xopp in their list of attachments.")
                 .toStdString() +
         "\n\n";
    t += tr("Files that are not documents (images, Markdown, text and all others) are copied as they were. The "
            "folders are those of the library.")
                 .toStdString() +
         "\n\n";
    t += tr("%1 archive PDFs (%2 of them PDF/A-3b), %3 other files.")
                 .arg(s.archived)
                 .arg(s.pdfa)
                 .arg(s.copied)
                 .toStdString() +
         "\n";
    if (!s.notPdfA.empty()) {
        t += "\n" + tr("Not PDF/A (written all the same, and readable; only the PDF/A label is missing):").toStdString() +
             "\n";
        for (const auto& [file, reasons]: s.notPdfA) {
            std::string why;
            for (const auto& r: reasons) {
                why += (why.empty() ? "" : "; ") + r;
            }
            t += "- " + u8(file) + ": " + why + "\n";
        }
    }
    if (!s.failed.empty()) {
        t += "\n" + tr("Not archived:").toStdString() + "\n";
        for (const auto& [file, why]: s.failed) {
            t += "- " + u8(file) + ": " + why + "\n";
        }
    }
    if (s.cancelled) {
        t += "\n" + tr("This archive is incomplete: the export was cancelled.").toStdString() + "\n";
    }
    return t;
}

}  // namespace

// --- planning and running ------------------------------------------------------------------------------------------

LibraryArchive::Plan LibraryArchive::plan(const fs::path& source, const fs::path& into, const fs::path& library,
                                          const std::string& name, std::string& error) {
    Plan p;
    std::error_code ec;
    if (source.empty() || !fs::is_directory(source, ec)) {
        error = tr("The library folder cannot be read.").toStdString();
        return p;
    }
    if (into.empty() || !fs::is_directory(into, ec)) {
        error = tr("Choose an existing folder for the archive.").toStdString();
        return p;
    }
    if (within(into, library.empty() ? source : library) || within(into, source)) {
        error = tr("The archive never goes into the library itself. Choose a folder outside it.").toStdString();
        return p;
    }
    p.source = canonicalOf(source);
    p.name = name.empty() ? u8(p.source.filename()) : name;
    const std::string base = p.name + " archive " + QDate::currentDate().toString(Qt::ISODate).toStdString();
    fs::path target = canonicalOf(into) / fromU8(base);
    for (int n = 2; fs::exists(target, ec) && n < 10000; ++n) {
        target = canonicalOf(into) / fromU8(base + " (" + std::to_string(n) + ")");
    }
    p.target = target;

    const std::vector<DocumentItem> items = DocumentFiles::scanRecursive(p.source, DocumentFiles::AllFiles);
    std::map<fs::path, std::set<std::string>> taken;  // folder (relative) -> names used (lower case)
    // Other files keep their names; the documents' PDFs get a free one next to them
    for (const DocumentItem& item: items) {
        const bool document = !item.xopp.empty() || !item.pdf.empty();
        std::vector<fs::path> files;
        if (!document) {
            files.push_back(item.main());
        } else if (!item.image.empty()) {
            files.push_back(item.image);  // (the picture a .xopp annotates: the user's file, as it is)
        }
        for (const fs::path& f: files) {
            const fs::path rel = f.lexically_relative(p.source);
            taken[rel.parent_path()].insert(lower(u8(rel.filename())));
            p.copies.emplace_back(f, p.target / rel);
        }
    }
    for (const DocumentItem& item: items) {
        if (item.xopp.empty() && item.pdf.empty()) {
            continue;
        }
        Plan::Document d;
        d.file = item.main();
        for (const fs::path& f: {item.xopp, item.pdf}) {
            if (!f.empty()) {
                d.sources.push_back(f);
            }
        }
        const fs::path relFolder = d.file.parent_path().lexically_relative(p.source);
        std::string stem = u8(d.file.stem());
        std::string file = stem + ".pdf";
        auto& names = taken[relFolder];
        for (int n = 2; names.count(lower(file)) && n < 10000; ++n) {
            file = stem + " (" + std::to_string(n) + ").pdf";
        }
        names.insert(lower(file));
        d.archive = (p.target / relFolder / fromU8(file)).lexically_normal();
        p.documents.push_back(std::move(d));
    }
    return p;
}

LibraryArchive::Summary LibraryArchive::run(const Plan& plan, const std::atomic<bool>& cancel,
                                            const std::function<void(int, int, const fs::path&)>& progress) {
    Summary s;
    s.target = plan.target;
    std::error_code ec;
    fs::create_directories(plan.target, ec);
    const int total = static_cast<int>(plan.documents.size() + plan.copies.size()) + 1;
    int done = 0;
    auto step = [&](const fs::path& current) {
        ++done;
        if (progress) {
            progress(done, total, current);
        }
    };
    // Links to a document of the export lead to its archive PDF
    std::map<fs::path, fs::path> archiveOf;
    for (const auto& d: plan.documents) {
        for (const fs::path& f: d.sources) {
            archiveOf[canonicalOf(f)] = d.archive;
        }
    }
    auto archived = [&archiveOf](const fs::path& f) {
        auto it = archiveOf.find(canonicalOf(f));
        return it != archiveOf.end() ? it->second : fs::path();
    };
    for (const auto& d: plan.documents) {
        if (cancel) {
            s.cancelled = true;
            break;
        }
        const fs::path rel = d.file.lexically_relative(plan.source);
        const fs::path relArchive = d.archive.lexically_relative(plan.target);
        auto loaded = DocumentSession::loadFile(d.file);
        if (!loaded.document) {
            s.failed.emplace_back(rel, loaded.error);
        } else if (!loaded.missingPdf.empty()) {
            s.failed.emplace_back(rel, tr("its PDF is missing: %1")
                                               .arg(QString::fromStdString(u8(loaded.missingPdf.filename())))
                                               .toStdString());
        } else {
            fs::create_directories(d.archive.parent_path(), ec);
            HybridPdf::LinkMap links;
            links.from = d.file.parent_path();
            links.archived = archived;
            const auto r = HybridPdf::writeArchive(*loaded.document, d.archive, {}, npos, links);
            if (!r.ok) {
                s.failed.emplace_back(rel, r.error);
            } else {
                ++s.archived;
                if (r.pdfa) {
                    ++s.pdfa;
                } else {
                    s.notPdfA.emplace_back(relArchive, r.notPdfA);
                }
            }
        }
        step(rel);
    }
    for (const auto& [from, to]: plan.copies) {
        if (cancel || s.cancelled) {
            s.cancelled = true;
            break;
        }
        const fs::path rel = from.lexically_relative(plan.source);
        std::error_code cec;
        fs::create_directories(to.parent_path(), cec);
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, cec);
        if (cec) {
            s.failed.emplace_back(rel, cec.message());
        } else {
            ++s.copied;
            std::error_code tec;
            fs::last_write_time(to, fs::last_write_time(from, tec), tec);  // (as it was)
        }
        step(rel);
    }
    std::ofstream(plan.target / "README.txt", std::ios::binary) << readme(plan, s);
    step("README.txt");
    return s;
}

// --- in the background -----------------------------------------------------------------------------------------------

struct LibraryArchive::State {
    std::atomic<bool> cancel{false};
};

LibraryArchive::LibraryArchive(QObject* parent): QObject(parent) {}

LibraryArchive::~LibraryArchive() { cancel(); }

void LibraryArchive::cancel() {
    if (state) {
        state->cancel = true;
    }
}

bool LibraryArchive::start(const fs::path& source, const fs::path& into, const fs::path& library,
                           const std::string& name, std::string& error) {
    if (state) {
        error = tr("An archive is being written already.").toStdString();
        return false;
    }
    error.clear();
    Plan p = plan(source, into, library, name, error);
    if (!error.empty()) {
        return false;
    }
    state = std::make_shared<State>();
    doneCount = 0;
    totalCount = static_cast<int>(p.documents.size() + p.copies.size()) + 1;
    currentName.clear();
    Q_EMIT runningChanged();
    Q_EMIT progressChanged();
    QPointer<LibraryArchive> self(this);
    std::shared_ptr<State> st = state;
    QThreadPool::globalInstance()->start([self, st, p = std::move(p)] {
        QThread::currentThread()->setPriority(QThread::LowPriority);  // (not for right now)
        const Summary s = run(p, st->cancel, [self, st](int done, int total, const fs::path& current) {
            const QString name = QString::fromStdString(u8(current));
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, st, done, total, name] {
                if (self && self->state == st) {
                    self->doneCount = done;
                    self->totalCount = total;
                    self->currentName = name;
                    Q_EMIT self->progressChanged();
                }
            });
        });
        QThread::currentThread()->setPriority(QThread::NormalPriority);
        QVariantMap map;
        map["target"] = QString::fromStdString(u8(s.target));
        map["archived"] = s.archived;
        map["pdfa"] = s.pdfa;
        map["copied"] = s.copied;
        QStringList notPdfA, failed;
        for (const auto& [file, reasons]: s.notPdfA) {
            QStringList why;
            for (const auto& r: reasons) {
                why << QString::fromStdString(r);
            }
            notPdfA << QString::fromStdString(u8(file)) + ": " + why.join("; ");
        }
        for (const auto& [file, why]: s.failed) {
            failed << QString::fromStdString(u8(file)) + ": " + QString::fromStdString(why);
        }
        map["notPdfA"] = notPdfA;
        map["failed"] = failed;
        map["cancelled"] = s.cancelled;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, st, map] {
            if (!self || self->state != st) {
                return;
            }
            self->state.reset();
            Q_EMIT self->runningChanged();
            Q_EMIT self->finished(map);
        });
    });
    return true;
}

}  // namespace xqt
