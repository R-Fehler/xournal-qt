/*
 * xournal-qt: writing the version history of a PDF with notes (HybridInternal.h; PdfHistory.h reads it;
 * qt/docs/features/hybrid-pdf.md, "Version history").
 *
 * @license GNU GPLv2 or later
 */
#include "HybridInternal.h"

#include <algorithm>
#include <ctime>
#include <stdexcept>
#include <system_error>

#include <glib.h>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFNameTreeObjectHelper.hh>

#include "ByteDelta.h"
#include "PdfRevisions.h"

namespace xqt::HybridPdf {
using namespace detail;

namespace {

// --- version history (PdfHistory.h; qt/docs/features/hybrid-pdf.md, "Version history") -------------------------------

/// The embedded document.xopp of a PDF with notes as the file has it (gzipped); empty: none.
std::string embeddedXoppOf(QPDF& q) {
    QPDFObjectHandle marker = q.getRoot().getKey(MARKER);
    QPDFObjectHandle name = marker.isDictionary() ? marker.getKey("/Data") : QPDFObjectHandle::newNull();
    auto spec = QPDFEmbeddedFileDocumentHelper(q).getEmbeddedFile(name.isString() ? name.getUTF8Value()
                                                                                   : std::string(DATA_NAME));
    if (!spec) {
        return {};
    }
    auto buffer = spec->getEmbeddedFileStream().getStreamData();
    return std::string(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
}

/// The SHA-256 of a .xopp (of its XML, gunzipped).
std::string xoppSha(const std::string& gz) {
    bool ok = false;
    const std::string xml = fileio::gunzip(gz, ok);
    return PdfHistory::sha256(ok ? xml : gz);
}

/// The revisions of `target` after its first `from` bytes written again as one (the day's version replaced: the
/// earlier save of the day and this one become one revision; what only the earlier one used goes). `change`, when
/// given, changes the file's objects first (through the update).
bool rewriteFrom(const fs::path& target, uint64_t from, std::string& error,
                 const std::function<void(QPDF&, IncrementalPdf::Update&)>& change = {}) {
    const auto chain = PdfRevisions::read(target);
    size_t k = 0;
    while (k < chain.revisions.size() && chain.revisions[k].end != from) {
        ++k;
    }
    if (k + 1 >= chain.revisions.size() || chain.garbage) {
        error = "the file is not as expected";
        return false;
    }
    IncrementalPdf::Update::Over over;
    over.prefix = PdfRevisions::tailOf(target, chain.revisions[k]);
    for (size_t j = 0; j <= k; ++j) {
        over.highest = std::max(over.highest, static_cast<int>(chain.revisions[j].size) - 1);
        for (const auto& [num, gen]: chain.revisions[j].objects) {
            over.highest = std::max(over.highest, num);
        }
    }
    for (size_t j = k + 1; j < chain.revisions.size(); ++j) {
        const auto& objects = chain.revisions[j].objects;
        over.objects.insert(over.objects.end(), objects.begin(), objects.end());
    }
    IncrementalPdf::Tail whole;
    if (!IncrementalPdf::readTail(target, whole, error)) {
        return false;
    }
    std::string bytes;
    {
        QPDF q;
        q.setSuppressWarnings(true);
        PdfEncryption::openQpdf(q, target);
        IncrementalPdf::Update u(q);
        if (change) {
            change(q, u);
        }
        // (the marker says where its revision begins: version history tells our revisions from other apps' by it)
        QPDFObjectHandle marker = q.getRoot().getKey(MARKER);
        if (QPDFObjectHandle h = marker.isDictionary() ? marker.getKey("/History") : QPDFObjectHandle::newNull();
            h.isDictionary()) {
            u.touch(marker.isIndirect() ? marker : q.getRoot());
            QPDFObjectHandle copy = h.shallowCopy();
            copy.replaceKey("/Start", QPDFObjectHandle::newInteger(static_cast<long long>(from)));
            marker.replaceKey("/History", copy);
        }
        bytes = u.serializeOver(over);
    }
    const auto r = IncrementalPdf::append(target, whole, bytes, from);
    if (!r.ok) {
        error = r.error;
    }
    return r.ok;
}

/// The whole document appended to `target` as one update (instead of writing the file anew: its earlier versions
/// stay). Written in full to a file of its own first, then copied in; streams the file has already (the pages of the
/// PDF it annotates, their fonts and images) are referred to, not copied again.
Result appendWhole(const Prepared& prep, const fs::path& target, const std::string& exportName, const HistoryMark& mark,
                   Revision* written, const fs::path& work) {
    Steps step;
    const fs::path full = work / "whole.pdf";
    Result r = assemble(prep, target, Mode::Hybrid, exportName, titleOf(target), &mark, full);
    if (!r.ok) {
        return r;
    }
    PdfEncryption::derive(full, target);  // (encrypted like the file: read with its password below)
    step("the whole document written");
    IncrementalPdf::Tail tail;
    if (!IncrementalPdf::readTail(target, tail, r.error)) {
        r.ok = false;
        return r;
    }
    std::string bytes;
    {
        QPDF q;
        q.setSuppressWarnings(true);
        PdfEncryption::openQpdf(q, target);
        if (PdfEncryption::Encrypter enc(q); enc.encrypted() && !enc.supported()) {
            throw std::runtime_error("the file is encrypted: " + enc.why());
        }
        IncrementalPdf::Update u(q);
        u.indexReuse();
        step("the file's streams");
        QPDF f;
        f.setSuppressWarnings(true);
        PdfEncryption::openQpdf(f, full);
        QPDFObjectHandle root = q.getRoot();
        u.touch(root);
        replaceAll(root, u.copyAll(f.getRoot()));
        if (QPDFObjectHandle info = f.getTrailer().getKey("/Info"); info.isDictionary()) {
            QPDFObjectHandle own = q.getTrailer().getKey("/Info");
            if (own.isIndirect() && own.isDictionary()) {
                u.touch(own);
                replaceAll(own, u.copyAll(info));
            } else {
                q.getTrailer().replaceKey("/Info", u.copyAll(info));
            }
        }
        bytes = u.serialize(tail);
        step("copied");
    }
    const auto appended = IncrementalPdf::append(target, tail, bytes);
    if (!appended.ok) {
        r.ok = false;
        r.error = appended.error;
        return r;
    }
    r.incremental = true;
    r.appended = bytes.size();
    if (written) {
        *written = revisionAfterFull(target, prep);
    }
    return r;
}

/// Before a new version is appended: the last one, still the file's last revision and an ordinary day's version, is
/// written again with its .xopp as a delta against the version before it (its pages and drawings stay as they are, so
/// any PDF viewer still shows it; its embedded document.xopp goes). Milestones, every 30th version, a version after
/// another app's revision and one whose delta would be more than half of its .xopp stay whole. False (`versions`
/// unchanged) when it stays whole.
bool storeAsDelta(const fs::path& target, const PdfHistory::Listed& listed, std::vector<PdfHistory::Version>& versions) {
    using PdfHistory::Version;
    if (versions.size() < 2 || listed.chain.garbage || !listed.chain.ok()) {
        return false;
    }
    Version& cur = versions.back();
    const Version& prev = versions[versions.size() - 2];
    const auto& last = listed.chain.revisions.back();
    if (cur.kind != PdfHistory::Kind::FULL || cur.id <= 0 || cur.start == 0 || cur.milestone() ||
        cur.id % PdfHistory::KEYFRAME_EVERY == 0 || prev.kind == PdfHistory::Kind::RECEIVED || cur.sha.empty() ||
        !listed.lastIsOurs || last.end != cur.end) {
        return false;
    }
    Steps step;
    std::string error;
    const std::string base = PdfHistory::xmlOf(target, listed, prev.id, error);
    if (base.empty()) {
        g_warning("Version %d of %s stays whole: %s", cur.id, target.string().c_str(), error.c_str());
        return false;
    }
    std::string gz;
    {
        QPDF q;
        q.setSuppressWarnings(true);
        PdfEncryption::openQpdf(q, target);
        gz = embeddedXoppOf(q);
    }
    bool ok = false;
    const std::string xml = fileio::gunzip(gz, ok);
    if (!ok || PdfHistory::sha256(xml) != cur.sha) {
        return false;
    }
    const std::string delta = ByteDelta::encode(base, xml);
    std::string check;
    if (!ByteDelta::apply(base, delta, check) || check != xml) {
        g_warning("The delta of version %d of %s does not give it back: it stays whole", cur.id, target.string().c_str());
        return false;
    }
    if (fileio::gzip(delta).size() * 2 > gz.size()) {
        return false;  // (a keyframe: the delta would be more than half of it)
    }
    step("the last version's delta");
    const bool done = rewriteFrom(target, cur.start, error, [&](QPDF& q, IncrementalPdf::Update& u) {
        QPDFObjectHandle root = q.getRoot();
        QPDFObjectHandle marker = root.getKey(MARKER);
        QPDFObjectHandle name = marker.getKey("/Data");
        UpdateSink sink(u);
        touchNames(sink, root);
        QPDFObjectHandle names = root.getKey("/Names");
        QPDFNameTreeObjectHelper tree(names.getKey("/EmbeddedFiles"), q);
        tree.remove(name.isString() ? name.getUTF8Value() : std::string(DATA_NAME));
        u.touch(marker.isIndirect() ? marker : root);
        QPDFObjectHandle dict = QPDFObjectHandle::newDictionary();
        dict.replaceKey("/Type", QPDFObjectHandle::newName("/XournalQtDelta"));
        QPDFObjectHandle d = QPDFObjectHandle::newDictionary();
        d.replaceKey("/Data", u.addStream(dict, delta));
        d.replaceKey("/Base", QPDFObjectHandle::newInteger(prev.id));
        marker.replaceKey(PdfHistory::DELTA_KEY, d);
    });
    if (!done) {
        g_warning("Version %d of %s stays whole: %s", cur.id, target.string().c_str(), error.c_str());
        return false;
    }
    step("written again as a delta");
    std::error_code ec;
    cur.kind = PdfHistory::Kind::DELTA;
    cur.base = prev.id;
    cur.end = fs::file_size(target, ec);
    return true;
}

/// The marker's history written again into `q` through `sink` (an update of the file that begins at `start`): the
/// update is the current version's last revision, ours (PdfHistory::list tells our revisions from other apps' by
/// /Start).
void markHistoryIn(QPDF& q, ObjectSink& sink, std::vector<PdfHistory::Version> versions, uint64_t start) {
    versions.back().end = 0;  // (the current version: as its own list says it)
    QPDFObjectHandle marker = q.getRoot().getKey(MARKER);
    sink.touch(marker.isIndirect() ? marker : q.getRoot());
    HistoryMark mark{std::move(versions), start};
    putHistory(marker, &mark,
               [&](const std::string& data) { return sink.addStream(QPDFObjectHandle::newDictionary(), data); });
}

}  // namespace

namespace detail {

Result writeKeeping(Document& doc, const fs::path& target, const BasePageOf& baseOf, size_t pdfPageCount,
                    const std::string& exportName, const WriteOptions& options, const fs::path& work) {
    Steps step;
    const std::time_t when = PdfHistory::now();
    PdfHistory::Version v;
    v.date = PdfHistory::isoUtc(when);
    v.day = PdfHistory::localDay(when);
    v.message = options.history->message;
    auto prepared = [&](const Reuse* reuse) {
        PrepareOptions how = preparing(target, baseOf, pdfPageCount, options);
        how.reuse = reuse;
        Prepared prep = prepare(doc, target.filename().string(), work, how);
        if (!prep.error.empty()) {
            throw std::runtime_error(prep.error);
        }
        v.sha = xoppSha(prep.xopp);
        v.pages = static_cast<int>(prep.pages.size());
        return prep;
    };
    std::error_code ec;
    PdfRevisions::Chain chain;
    if (fs::exists(target, ec)) {
        chain = PdfRevisions::read(target);
    }
    if (!chain.ok() || chain.garbage) {
        // Nothing to build on (a new file, or one that does not read): the first version, written in full
        Prepared prep = prepared(nullptr);
        v.id = 1;
        HistoryMark mark{{v}, 0};
        Result r = assemble(prep, target, Mode::Hybrid, exportName, titleOf(target), &mark);
        r.version = v.id;
        if (r.ok && options.written) {
            *options.written = revisionAfterFull(target, prep);
        }
        return r;
    }
    if (!isHybrid(target)) {
        // A PDF of the user's: version 0 is the file as it was received, the document is appended on top of it
        PdfHistory::Version v0;
        v0.id = 0;
        v0.kind = PdfHistory::Kind::RECEIVED;
        v0.end = chain.end();
        v0.date = PdfRevisions::dateOf(target, v0.end);
        if (v0.date.empty()) {
            v0.date = v.date;
        }
        Prepared prep = prepared(nullptr);
        v.id = 1;
        v.start = chain.size;
        HistoryMark mark{{v0, v}, chain.size};
        Result r = appendWhole(prep, target, exportName, mark, options.written, work);
        r.version = v.id;
        return r;
    }
    const std::string stampBefore = stampOf(target);
    PdfHistory::Listed listed = PdfHistory::list(target);
    std::vector<PdfHistory::Version> versions = listed.versions;
    int last = -1;
    for (const auto& x: versions) {
        last = std::max(last, x.id);
    }
    if (versions.empty()) {
        // History begins now: version 0 is the file as it is
        PdfHistory::Version v0;
        v0.id = 0;
        v0.start = chain.revisions.back().start;
        v0.end = chain.end();
        v0.date = PdfRevisions::dateOf(target, v0.end);
        if (v0.date.empty()) {
            v0.date = v.date;
        }
        QPDF q;
        q.setSuppressWarnings(true);
        PdfEncryption::openQpdf(q, target);
        if (const std::string xopp = embeddedXoppOf(q); !xopp.empty()) {
            v0.sha = xoppSha(xopp);
        }
        versions.push_back(v0);
        last = 0;
    }
    const bool replace = !options.history->newVersion && PdfHistory::replacesLast(listed, v.day);
    uint64_t cut = 0;
    if (replace) {
        v.id = versions.back().id;
        v.start = cut = versions.back().start;
        versions.pop_back();
    } else {
        v.id = last + 1;
        v.start = chain.size;
    }
    step("the history");
    // A new version: the one before it becomes a delta (the file changes, its pages stay the same objects)
    Revision rev = options.revision ? *options.revision : Revision();
    if (!replace && storeAsDelta(target, listed, versions)) {
        v.start = versions.back().end;
        if (rev.valid() && rev.stamp == stampBefore) {
            rev.stamp = stampOf(target);
        }
    }
    auto finish = [&](Result r) {
        r.version = v.id;
        r.replaced = replace;
        if (r.ok && replace) {
            // The day's earlier save and this one: one revision
            std::string error;
            if (!rewriteFrom(target, cut, error)) {
                g_warning("Could not replace the day's version of %s: %s", target.string().c_str(), error.c_str());
            }
            if (options.written) {
                options.written->stamp = stampOf(target);
            }
            step("the day's version replaced");
        }
        return r;
    };
    std::string whyFull;
    if (rev.valid()) {
        AppendTry how;
        how.rev = &rev;
        how.cleanCopyOf = options.revision->stamp;
        how.prepared = prepared;
        how.markOf = [&](const Existing& existing) {
            HistoryMark mark{versions, existing.tail.size};
            mark.versions.push_back(v);
            return mark;
        };
        how.exportName = exportName;
        how.written = options.written;
        how.appended = finish;
        if (auto appended = appendIfPossible(target, how, whyFull, step)) {
            return *appended;
        }
    } else {
        whyFull = "no revision to build on";
    }
    if (step.on) {
        std::fprintf(stderr, "hybrid-pdf: the whole document appended: %s\n", whyFull.c_str());
    }
    // The fallback that keeps the versions: the whole document appended
    Prepared prep = prepared(nullptr);
    versions.push_back(v);
    std::error_code sec;
    Result r = appendWhole(prep, target, exportName, HistoryMark{versions, fs::file_size(target, sec)}, options.written,
                           work);
    r.whyFull = whyFull;
    return finish(r);
}

}  // namespace detail

bool writeVersion(const fs::path& pdf, int id, const fs::path& out, std::string& error) {
    try {
        const PdfHistory::Listed listed = PdfHistory::list(pdf);
        auto it = std::find_if(listed.versions.begin(), listed.versions.end(),
                               [&](const PdfHistory::Version& v) { return v.id == id; });
        if (it == listed.versions.end()) {
            error = listed.error.empty() ? "There is no such version in the file." : listed.error;
            return false;
        }
        std::string xopp;
        if (it->kind == PdfHistory::Kind::DELTA) {
            xopp = PdfHistory::xoppOf(pdf, listed, id, error);  // (rebuilt and checked first)
            if (xopp.empty()) {
                return false;
            }
        }
        if (!PdfRevisions::extract(pdf, it->end, out, error)) {
            return false;
        }
        PdfEncryption::derive(out, pdf);  // (a prefix of a protected file: encrypted, the same password)
        if (xopp.empty()) {
            return true;  // (the file as it was saved then: its own document.xopp, or the PDF as received)
        }
        // Its .xopp was a delta: the whole one added again, so that it opens as a PDF with notes of its own
        IncrementalPdf::Tail tail;
        if (!IncrementalPdf::readTail(out, tail, error)) {
            return false;
        }
        std::string bytes;
        {
            QPDF q;
            q.setSuppressWarnings(true);
            PdfEncryption::openQpdf(q, out);
            IncrementalPdf::Update u(q);
            QPDFObjectHandle marker = q.getRoot().getKey(MARKER);
            QPDFObjectHandle name = marker.getKey("/Data");
            UpdateSink sink(u);
            addDataSpec(q, sink, name.isString() ? name.getUTF8Value() : std::string(DATA_NAME), xopp);
            u.touch(marker.isIndirect() ? marker : q.getRoot());
            marker.removeKey(PdfHistory::DELTA_KEY);
            bytes = u.serialize(tail);
        }
        const auto r = IncrementalPdf::append(out, tail, bytes);
        if (!r.ok) {
            error = r.error;
        }
        return r.ok;
    } catch (const std::exception& e) {
        error = e.what();
    }
    return false;
}

void keepHistoryIn(const fs::path& pdf, QPDF& q, IncrementalPdf::Update& u, uint64_t start) {
    const QPDFObjectHandle marker = q.getRoot().getKey(MARKER);
    if (!marker.isDictionary() || !marker.getKey("/History").isDictionary()) {
        return;  // (not a PDF with notes, or one without history)
    }
    const PdfHistory::Listed listed = PdfHistory::list(pdf);
    if (listed.on && listed.lastIsOurs && !listed.versions.empty()) {
        UpdateSink sink(u);
        markHistoryIn(q, sink, listed.versions, start);
    }
}

bool setVersionMessage(const fs::path& pdf, int id, const std::string& message, std::string& error) {
    const fileio::FileWriteLock lock(pdf);
    try {
        PdfHistory::Listed listed = PdfHistory::list(pdf);
        if (!listed.on || !listed.lastIsOurs || listed.versions.empty()) {
            error = "The file was changed by another app since it was saved here: save it first.";
            return false;
        }
        bool found = false;
        for (auto& v: listed.versions) {
            if (v.id == id) {
                v.message = message;
                found = true;
            }
        }
        if (!found) {
            error = "There is no such version in the file.";
            return false;
        }
        IncrementalPdf::Tail tail;
        if (!IncrementalPdf::readTail(pdf, tail, error)) {
            return false;
        }
        std::string bytes;
        {
            QPDF q;
            q.setSuppressWarnings(true);
            PdfEncryption::openQpdf(q, pdf);
            IncrementalPdf::Update u(q);
            UpdateSink sink(u);
            markHistoryIn(q, sink, listed.versions, tail.size);
            bytes = u.serialize(tail);
        }
        const std::string was = stampOf(pdf);
        const auto r = IncrementalPdf::append(pdf, tail, bytes);
        if (!r.ok) {
            error = r.error;
            return false;
        }
        // (only the marker changed: the clean copy is still the clean copy of this version)
        keepCacheEntry(pdf, was);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
    }
    return false;
}

}  // namespace xqt::HybridPdf
