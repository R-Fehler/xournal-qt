/*
 * xournal-qt: an incremental update of a PDF (ISO 32000-1, 7.5.6), the way Acrobat and Drawboard save.
 *
 * qpdf reads and changes the PDF but cannot write an incremental update: it always writes the whole file. This
 * appends only what changed. The file is opened with qpdf, an Update is started (it remembers the highest object
 * number), each object of the file is touch()ed before it is changed, then write() serialises through qpdf:
 * - the touched objects that are now different from what they were (same object numbers);
 * - the new objects they reach (numbered after the file's highest);
 * - a cross-reference section in the file's style: a cross-reference stream after one (PDF 1.5; the new objects that
 *   are not streams go into an object stream then), else a classic table; with /Prev, and a trailer with /Size,
 *   /Root, /Info and /ID (the file's first identifier, a new second one).
 * The bytes of the file stay as they are: readers take the last cross-reference section, and each earlier revision
 * stays readable.
 *
 * Writing is atomic: a copy of the file plus the update goes to a temporary file next to it, which is flushed to
 * the disk and renamed over the file. A crash or a failed write at any point leaves the previous revision as it was
 * (never a half-written update that readers would have to repair), and readers of the file meanwhile (the library,
 * previews, other apps) never see a partial update. See qt/docs/hybrid-pdf.md, "Saving: incremental updates".
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <qpdf/DLL.h>
#if QPDF_MAJOR_VERSION < 12
#error "qpdf 12 or newer is needed (see qt/cmake/XqtQpdf.cmake)"
#endif
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjGen.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include "filesystem.h"

namespace xqt::IncrementalPdf {

/// The end of a PDF file: where its last cross-reference section is, and whether it is a stream.
struct Tail {
    uint64_t size = 0;        ///< the file's length in bytes
    uint64_t startxref = 0;   ///< the offset of its last cross-reference section
    bool xrefStream = false;  ///< that section is a cross-reference stream (PDF 1.5 and later)
    bool eol = true;          ///< the file ends with an end of line
};

/// Read the end of `file` (the last "startxref"). False, with `error`, when it has none.
bool readTail(const fs::path& file, Tail& tail, std::string& error);

struct Stats {
    size_t changed = 0;   ///< objects of the file written again
    size_t added = 0;     ///< new objects
    size_t streams = 0;   ///< of both, streams
    uint64_t bytes = 0;   ///< the update's length
};

/// New objects are made through the Update (add, addStream, copy, copyStream), not with qpdf's makeIndirectObject,
/// newStream or copyForeignObject: the first new object of a QPDF makes qpdf read every object of the file to find a
/// free number (qpdf 12.4: 1.1 s for pgfmanual, 10.6 took 6.4 s; the whole save takes 0.2 s). The Update numbers them
/// itself after the file's highest; a new stream is a dictionary in `pdf` (so other objects can refer to it) whose
/// data the Update keeps: isStream() and streamDictionary() tell such streams apart.
class Update {
public:
    /// Before `pdf` (opened from a file) is changed.
    explicit Update(QPDF& pdf);

    /// This object of the file is about to be changed (a direct object: nothing; a new one: nothing).
    void touch(QPDFObjectHandle object);
    /// This stream's data is replaced.
    void touchData(QPDFObjectHandle stream);
    /// Whether the object was made after the update began.
    bool isNew(QPDFObjectHandle object) const;
    /// Whether the file has this object.
    bool has(QPDFObjGen id) const { return objects.count(id) > 0; }
    /// The highest object number of the file.
    int highest() const { return maxId; }

    /// A new indirect object with this (direct) value.
    QPDFObjectHandle add(QPDFObjectHandle value);
    /// A new stream: its dictionary (direct) and data (`encoded` as its /Filter says; else it is compressed when
    /// written).
    QPDFObjectHandle addStream(QPDFObjectHandle dictionary, std::string data);
    /// A new copy of a stream (of the file or new): its dictionary copied, the same data.
    QPDFObjectHandle copyStream(QPDFObjectHandle stream);
    /// A copy of an object of another PDF (with everything it refers to; not other pages, and not the /Parent of a
    /// page: that is set by the caller).
    QPDFObjectHandle copy(QPDFObjectHandle foreign);
    /// A stream: of the file, or new.
    bool isStream(QPDFObjectHandle object) const;
    /// Its dictionary (to change it).
    QPDFObjectHandle streamDictionary(QPDFObjectHandle stream);

    /// The update's bytes, to append to a file whose end is `tail` (they start at its end). Throws on failure.
    std::string serialize(const Tail& tail, Stats* stats = nullptr);

private:
    /// The next free number, taken (a null object until it is replaced).
    QPDFObjectHandle reserve();
    struct Before {
        std::string text;   ///< as it was (a stream: its dictionary)
        bool data = false;  ///< a stream whose data was replaced
    };
    QPDFObjectHandle copyValue(QPDFObjectHandle o, bool top);
    QPDF& pdf;
    int maxId = 0;
    int nextId = 0;
    std::set<QPDFObjGen> objects;
    std::map<QPDFObjGen, Before> touched;
    std::map<int, std::string> streamData;                     ///< new streams: their data
    std::map<std::pair<QPDF*, QPDFObjGen>, QPDFObjectHandle> copied;  ///< copy(): what was copied already
};

struct Result {
    bool ok = false;
    std::string error;
    uint64_t size = 0;  ///< the file's length now
};

/// Append `update` to `file`, whose end must still be `tail` (else nothing is written: another app changed it).
/// Atomic: see above. `update` starts right after the old end (Update::serialize puts an end of line first when the
/// file had none).
Result append(const fs::path& file, const Tail& tail, const std::string& update);

/// Tests: called while the update is written with the bytes written so far; returning true fails the write there, as
/// a full disk or a crash would.
extern std::function<bool(uint64_t)> failWriteAt;

}  // namespace xqt::IncrementalPdf
