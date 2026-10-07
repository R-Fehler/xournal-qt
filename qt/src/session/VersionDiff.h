/*
 * xournal-qt: which pages differ between two documents (comparing two versions, or a version and now;
 * qt/docs/features/reference-view.md "Comparing", qt/docs/features/hybrid-pdf.md "Version history").
 *
 * Each page gets a signature from what it holds, without drawing anything: its size, its background (kind, PDF page,
 * colour, image file) and each element of each layer as the .xopp holds it (upstream's SaveHandler: a stroke drawn
 * now and the same stroke read back from a saved version give the same text).
 * The two lists are then aligned (the longest common run of equal pages, after the equal pages at both ends), so a
 * page inserted or removed does not mark every page after it; a page that only moved is not a change. Where pages
 * differ, the n-th changed page of one side is paired with the n-th of the other between two equal pages; what is
 * left over was added (newer) or removed (older).
 *
 * Signed from the loaded documents rather than from the per-layer sigs the PDF's marker records: "now" has unsaved
 * changes the marker does not know, the marker's sigs change with the app's version, and a document without a marker
 * (a .xopp, any PDF) compares the same way.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class Document;
class XojPage;

namespace xqt::versiondiff {

struct PageSig {
    uint64_t whole = 0;               ///< the page: size, background, every layer and element in order
    std::vector<uint64_t> elements;   ///< each element (for the elements added and removed, later)
};

/// The signature of a page (the caller holds the document's lock, shared is enough).
PageSig sigOf(const XojPage& page);
/// Of every page (takes the document's shared lock).
std::vector<PageSig> sigsOf(Document& doc);
std::vector<uint64_t> wholeOf(const std::vector<PageSig>& sigs);

/// One change, in the order of the newer document. A page that is only on one side (added, removed) is shown on the
/// other side where it would be (the page after the last equal one, clamped).
struct Change {
    size_t newerPage = 0, olderPage = 0;  ///< where to show it on each side
    bool inNewer = true, inOlder = true;  ///< false: added (not in the older), removed (not in the newer)
};

struct Result {
    std::vector<char> newerChanged, olderChanged;  ///< per page: changed, added or removed
    std::vector<Change> changes;
    size_t added() const;
    size_t removed() const;
};

/// Compare the page signatures of an older and a newer document.
Result compare(const std::vector<uint64_t>& older, const std::vector<uint64_t>& newer);

}  // namespace xqt::versiondiff
