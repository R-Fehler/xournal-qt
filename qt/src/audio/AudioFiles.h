/*
 * xournal-qt: where recordings are kept and found (qt/docs/audio.md, "Storage").
 *
 * As in Xournal++, a .xopp names its recordings bare ("2026-10-04_14-03-22.ogg") and the files are in the app's
 * audio folder (here <app data>/audio, never the cache: they are the user's data). A recording is found, in this
 * order:
 * 1. the name itself when it is an absolute path that exists ("Export for Xournal++" writes those);
 * 2. next to the document, and in its "name.audio" folder (an exported copy moved with its recordings);
 * 3. the app's audio folder;
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
/// Recordings found only in such a folder are copied into the app's audio folder (a PDF with notes saved as a .xopp:
/// the .xopp names them bare, as upstream). Returns how many were copied.
size_t adoptExtracted(const std::vector<std::string>& names, const fs::path& documentFile);

/// The file of the recording `name` (as an element or a memo names it) for the document saved as `documentFile`
/// (empty: not saved); empty when it is nowhere.
fs::path find(const std::string& name, const fs::path& documentFile = {});

/// The folder "Export for Xournal++" copies the recordings into: "name.audio" next to "name.xopp".
fs::path exportFolderOf(const fs::path& xopp);

/// The name of a recording's attachment in a PDF with notes: "audio-p012-2026-10-04_14-03-22.ogg", with the first and
/// the last page it is on ("audio-p012-p015-…"; 1-based, three digits at least), so it is found without the app.
std::string attachmentName(const std::string& name, const std::vector<size_t>& pages);
/// What a PDF app shows for it.
std::string attachmentDescription(const std::string& name, const std::vector<size_t>& pages);
/// The MIME type of a recording.
constexpr const char* MIME = "audio/ogg";

}  // namespace xqt::audio
