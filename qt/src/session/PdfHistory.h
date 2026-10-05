/*
 * xournal-qt: version history inside a PDF with notes (qt/docs/hybrid-pdf.md, "Version history"; the plan:
 * qt/docs/research/version-history.md, "Confirmed by the author").
 *
 * A version is a revision of the file that our save wrote while history is on: the file cut after that revision's
 * "%%EOF" is the file as it was saved then (PdfRevisions.h). One version per local calendar day: the first save of a
 * day appends a new one, the further saves of that day replace it (the file is cut back to where it began and the
 * day's version appended again). A save with a message is a milestone: never replaced; the saves after it start a new
 * version. Version 0 is the file as it was when history began (a plain PDF "as received", or a PDF with notes as it
 * was). Another app's revision after ours is never cut away.
 *
 * The list lives in the file, so it travels with it: our marker gets /History << /On true /Count n /Latest (date) >>
 * and /Versions, a compressed stream with one JSON line per version (the latest revision's list is the truth; each
 * list covers the versions before it and the version it belongs to). The file is checked, not trusted: listed ends
 * that are no revision ends of the file are versions another app removed (it wrote the file anew), and revisions of
 * the file after the first version that are not listed were added by another app.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

#include "PdfRevisions.h"
#include "filesystem.h"

class QPDF;

namespace xqt::PdfHistory {

/// What a version's .xopp is stored as.
namespace Kind {
constexpr const char* RECEIVED = "received";  ///< a plain PDF as it was received: no .xopp
constexpr const char* FULL = "full";          ///< its revision has the whole embedded document.xopp
constexpr const char* DELTA = "delta";        ///< a byte delta against the version before it (ByteDelta.h)
}  // namespace Kind

struct Version {
    int id = 0;               ///< 0: the file as it was when history began; then 1, 2, … (never reused)
    std::string date;         ///< when it was saved, UTC ("2026-10-04T12:30:00Z")
    std::string day;          ///< the local calendar day of that save ("2026-10-04")
    std::string message;      ///< a milestone's message (empty: an ordinary day's version)
    uint64_t start = 0;       ///< where its revision begins in the file
    uint64_t end = 0;         ///< after its revision's "%%EOF" (0 in the list of its own revision: not known then)
    std::string sha;          ///< SHA-256 of its uncompressed .xopp (hex; empty: received)
    std::string kind = Kind::FULL;
    int base = -1;            ///< a delta: the version it is against
    int pages = 0;            ///< its page count
    bool milestone() const { return !message.empty(); }
};

/// The JSON lines of a list of versions, and back (lines that do not read are left out).
std::string toJsonLines(const std::vector<Version>& versions);
std::vector<Version> fromJsonLines(const std::string& text);

/// What our marker in a PDF with notes says about its history (the latest revision; `q` opened on the file or a
/// prefix of it).
struct State {
    bool on = false;
    int count = 0;  ///< /History /Count (what the library shows, without reading the list)
    long long start = -1;  ///< /History /Start: where the revision with this marker begins (-1: not said)
    std::vector<Version> versions;
};
State stateOf(QPDF& q);

/// Another app's revision in the history (listed with the versions, by its place in the file).
struct Other {
    uint64_t start = 0, end = 0;
    std::string date;  ///< its /ModDate as UTC; empty: none
};

/// The history of a file, checked against its revisions (reads the whole file: when the panel is shown, not while
/// listing).
struct Listed {
    bool on = false;
    std::vector<Version> versions;  ///< oldest first, each with its end; versions no longer in the file left out
    std::vector<Other> others;      ///< revisions of other apps after the first version
    int removed = 0;                ///< listed versions that are not in the file any more
    bool lastIsOurs = false;        ///< the file's last revision has the marker the list was read from
    uint64_t size = 0;              ///< the file's length
    PdfRevisions::Chain chain;
    std::string error;
};
Listed list(const fs::path& pdf);

/// The version shown as "now" when a document is saved at `when`: whether that save replaces the last version (the
/// same local day, not a milestone, not version 0, and still the last revision of the file).
bool replacesLast(const Listed& listed, const std::string& today);

/// The .xopp of version `id` as its XML: the embedded document of its revision, or rebuilt from the deltas back to the
/// last version stored whole, and checked against the version's SHA-256. Empty, with `error`, when it cannot.
std::string xmlOf(const fs::path& pdf, const Listed& listed, int id, std::string& error);
/// The same as a .xopp file's bytes (gzipped).
std::string xoppOf(const fs::path& pdf, const Listed& listed, int id, std::string& error);

/// Write the .xopp of version `id` (-1: the latest) of `pdf` to `out` (xournal-qt-cli export-xopp). Its PDF background
/// is the PDF by its name, as embedded.
bool exportXopp(const fs::path& pdf, int id, const fs::path& out, std::string& error);

/// The marker key of a version whose .xopp is a delta: << /Data stream /Base id >>.
constexpr const char* DELTA_KEY = "/XoppDelta";
/// Every 30th version is stored whole (a delta chain is at most this long).
constexpr int KEYFRAME_EVERY = 30;

// --- small helpers -------------------------------------------------------------------------------------------------

/// The clock of version history (tests set another one; nullptr: the system clock).
extern std::function<std::time_t()> clock;
std::time_t now();
std::string isoUtc(std::time_t t);
std::string localDay(std::time_t t);
std::string sha256(const std::string& data);
/// A gzip file's content (an embedded .xopp); `ok` false when it does not read.
std::string gunzip(const std::string& data, bool& ok);
std::string gzip(const std::string& data);

}  // namespace xqt::PdfHistory
