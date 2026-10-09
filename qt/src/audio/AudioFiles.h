/*
 * xournal-qt: where recordings are kept and found (qt/docs/features/audio.md, "Storage").
 *
 * A .xopp (or .xoj) keeps its recordings next to it, in "<name>.audio/" (its sidecar), named bare in the file
 * ("2026-10-04_14-03-22.ogg") as Xournal++ names them, so the document and its recordings move together. A document
 * without a file (not saved yet) and a PDF with notes record into the app's audio folder (<app data>/audio, never the
 * cache: they are the user's data); the first save as a .xopp, and Save as, take them into the sidecar (gather). A
 * PDF with notes carries its recordings inside (HybridPdf). A recording is found, in this order:
 * 1. the name itself when it is an absolute path that exists ("Export for Xournal++" writes those);
 * 2. in the document's sidecar "name.audio", and next to the document;
 * 3. the app's audio folder: only for a document that keeps no sidecar (not saved, a PDF with notes);
 * 4. the recordings a PDF with notes carries (taken out of the file into the cache when it is opened,
 *    HybridPdf::Opened::audio), for that PDF;
 * 5. other folders: Xournal++'s audio folder when the user set it in the settings (read only), folders added with
 *    addFolder, and last the recordings taken out of any PDF in this session.
 *
 * Thread-safe.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "filesystem.h"

namespace xqt::audio {

/// The app's audio folder (made when asked for). Tests: setAppFolder.
fs::path appFolder();
void setAppFolder(const fs::path& folder);  ///< empty: the default again

/// Folders searched after the app's (Xournal++'s audio folder from the settings).
void setExtraFolders(std::vector<fs::path> folders);

/// A folder searched while the handle lives (the recordings an open document brought).
class FolderHandle {
public:
    explicit FolderHandle(fs::path folder);
    ~FolderHandle();
    FolderHandle(const FolderHandle&) = delete;
    FolderHandle& operator=(const FolderHandle&) = delete;

private:
    fs::path folder;
};
std::unique_ptr<FolderHandle> addFolder(const fs::path& folder);

/// The recordings a PDF with notes carries, taken out of it into `folder` (HybridPdf::open): searched for that
/// document (by its path), and after all others for any document (a copy saved as .xopp in this session).
void setExtractedFolder(const fs::path& document, const fs::path& folder);

/// The document file keeps its recordings in a sidecar: a .xopp or .xoj (not a PDF with notes, not an unsaved one).
bool keepsSidecar(const fs::path& documentFile);
/// The sidecar of a document: "name.audio" next to "name.xopp" (also where "Export for Xournal++" copies them).
fs::path sidecarOf(const fs::path& documentFile);
/// Where a recording of the document `documentFile` (empty: not saved) is written: its sidecar when it keeps one, else
/// the app's audio folder. Made when asked for.
fs::path recordingFolderFor(const fs::path& documentFile);

/// A recording being written (the recorder's file): gather() leaves it where it is until it is done.
void setBusy(const fs::path& file, bool busy);

/// The document that was `from` (empty: not saved) is saved as the .xopp `to`: the recordings `names` it refers to
/// are put into `to`'s sidecar. Those that were in the app's audio folder of a document not saved before are moved
/// (renamed; on another disk copied, checked by size, then deleted); all others (another document's sidecar after
/// Save as, a PDF's recordings, the app's folder for a PDF with notes) are copied, so the document they came from
/// keeps them. Absolute names, recordings already there, missing and busy ones are left. Returns how many were put
/// there. Runs on any thread (the save's worker).
size_t gather(const std::vector<std::string>& names, const fs::path& from, const fs::path& to);
/// Moves one recording `file` into the folder `into` (rename, or copy, check, delete). False: left where it is.
bool moveInto(const fs::path& file, const fs::path& into);

/// The file of the recording `name` (as an element or a memo names it) for the document saved as `documentFile`
/// (empty: not saved); empty when it is nowhere.
fs::path find(const std::string& name, const fs::path& documentFile = {});

/// The folder "Export for Xournal++" copies the recordings into: "name.audio" next to "name.xopp" (the sidecar).
inline fs::path exportFolderOf(const fs::path& xopp) { return sidecarOf(xopp); }

/// The name of a recording's attachment in a PDF with notes: "audio-p012-2026-10-04_14-03-22.ogg", with the first and
/// the last page it is on ("audio-p012-p015-…"; 1-based, three digits at least), so it is found without the app.
std::string attachmentName(const std::string& name, const std::vector<size_t>& pages);
/// What a PDF app shows for it.
std::string attachmentDescription(const std::string& name, const std::vector<size_t>& pages);
/// The MIME type of a recording.
constexpr const char* MIME = "audio/ogg";

}  // namespace xqt::audio
