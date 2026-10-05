/*
 * xournal-qt: a byte delta (copy ranges of the base and inserted bytes), xdelta-like, for the older versions of a PDF
 * with notes (PdfHistory.h): an older version's .xopp is kept as the delta against the version before it.
 *
 * Small and self-contained: the base is indexed in blocks of 16 bytes by a rolling hash; the target is scanned with
 * the same hash at every position, a match is checked byte for byte and extended both ways. The format:
 *   "XQD1", base size, target size (varints), then operations until the target is complete:
 *   0x01 offset length  (copy that range of the base)
 *   0x02 length bytes   (insert these bytes)
 * apply() checks every bound; the version's SHA-256 is checked by the caller.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>

namespace xqt::ByteDelta {

/// The delta that makes `target` from `base` (deterministic: the same inputs give the same bytes).
std::string encode(const std::string& base, const std::string& target);

/// `target` made from `base` and `delta`. False when the delta does not read or is not for a base of that size.
bool apply(const std::string& base, const std::string& delta, std::string& target);

}  // namespace xqt::ByteDelta
