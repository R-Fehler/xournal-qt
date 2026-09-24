/*
 * xournal-qt: documents as files on disk (library, recent files).
 *
 * A document is one item even when it is two files: a .xopp with the PDF it annotates next to it under the same name
 * ("lecture.xopp" + "lecture.pdf"), or with the image it annotates ("photo.xopp" + "photo.jpg"; a .xopp next to a PDF
 * of its name belongs to the PDF). A lone .xopp (or .xoj), a lone PDF, a Markdown file (.md) and a lone image (.png,
 * .jpg, .jpeg, .webp, and .heic where Qt can read it) are items, too. Renaming, moving and
 * importing keep the two files together; the .xopp's reference to its PDF follows (it is rewritten with upstream's
 * LoadHandler / SaveHandler). Attached PDFs ("name.xopp.bg.pdf"), attached background images ("name.xopp.bg_1.png")
 * and the hidden merged PDF of pasted PDF pages (".name.pages.pdf", see session/MergedPdf.h) travel with their .xopp.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "filesystem.h"

namespace xqt {

struct DocumentItem {
    fs::path xopp;   ///< the .xopp / .xoj, or empty
    fs::path pdf;    ///< the PDF with the same name next to it, or empty
    fs::path md;     ///< a Markdown file (alone), or empty
    fs::path image;  ///< an image (alone, or with the .xopp of the same name that annotates it), or empty
    /// The file to open: the .xopp, else the PDF, the Markdown file, the image.
    const fs::path& main() const { return !xopp.empty() ? xopp : !pdf.empty() ? pdf : !md.empty() ? md : image; }
    fs::path folder() const { return main().parent_path(); }
    /// The name shown for it: the file name without extension.
    std::string name() const;
    bool valid() const { return !main().empty(); }
    /// What it is, for the library (a filter by kind): a PDF (with or without its .xopp), an image (with or without
    /// its .xopp), a Markdown file, or notes (a .xopp alone).
    enum class Kind { Notes, Pdf, Markdown, Image };
    Kind kind() const;
    /// "notes", "pdf", "md", "image"
    const char* kindName() const;
    /// `file` is one of its files (the .xopp, the PDF, the Markdown file, the image).
    bool has(const fs::path& file) const;
    bool operator==(const DocumentItem& o) const {
        return xopp == o.xopp && pdf == o.pdf && md == o.md && image == o.image;
    }
};

namespace DocumentFiles {

/// The folder ".xournal_library" (library metadata) and other hidden entries are never shown.
constexpr const char* META_DIR = ".xournal_library";

struct Listing {
    std::vector<fs::path> folders;
    std::vector<DocumentItem> items;
};
/// Folders and documents in a folder, not recursive. Hidden entries, backups, attached PDFs and background images are
/// left out.
Listing scan(const fs::path& dir);
/// All documents in a folder and its subfolders.
std::vector<DocumentItem> scanRecursive(const fs::path& dir);
/// All subfolders (recursive), e.g. as targets for "Move to".
std::vector<fs::path> foldersRecursive(const fs::path& dir);
/// The item a document file belongs to (a .xopp with its PDF, a PDF with its .xopp). Invalid if it is none.
DocumentItem itemOf(const fs::path& file);
bool isDocumentFile(const fs::path& file);
bool isMarkdownFile(const fs::path& file);
/// An image the library shows (not a background image stored with a .xopp).
bool isImageFile(const fs::path& file);
/// The attached background PDF of a .xopp (upstream: "name.xopp.bg.pdf").
fs::path attachmentOf(const fs::path& xopp);
/// The hidden PDF with the pages of a .xopp pasted from other PDFs (".name.pages.pdf").
fs::path pagesOf(const fs::path& xopp);
/// The images a .xopp keeps next to it as page backgrounds (upstream attaches them as "name.xopp.bg_1.png", ...).
std::vector<fs::path> imageAttachmentsOf(const fs::path& xopp);
/// The files of a document that exist: the .xopp, its attached PDF, background images and pages PDF, the PDF.
std::vector<fs::path> filesOf(const DocumentItem& item);

/// Outcome of a file operation.
struct Result {
    bool ok = false;
    std::string error;
    DocumentItem item;  ///< the document (or `folder`) at its new place
    fs::path folder;
    /// Every file or folder that has a new path now (old, new): open tabs and recent files follow.
    std::vector<std::pair<fs::path, fs::path>> moved;
    /// Import: documents copied (a folder: all documents in it and its subfolders)
    int documents = 0;
};

/// A name that is not a file or folder name in `folder` yet: `stem`, "stem (2)", ... (for a document: no .xopp,
/// .pdf, .md or image with that name, so it does not pair with another file).
std::string uniqueName(const fs::path& folder, const std::string& stem);
/// Whether a user-entered name can be a file name ("", "..", "a/b", ".hidden" cannot).
bool validName(const std::string& name);

/// Rename a document: the .xopp and its PDF (or image) together.
Result rename(const DocumentItem& item, const std::string& newName);
/// Move a document into another folder.
Result move(const DocumentItem& item, const fs::path& folder);
/// Copy a document file into a folder (under a free name): a .xopp with the PDF it uses (stored as "<name>.pdf" next
/// to it), a PDF with the .xopp next to it, an image with its .xopp, a Markdown file. A folder is copied with its
/// whole folder structure and all documents in it (other files and hidden folders stay behind). Problems with single documents do not stop the rest: `ok` with
/// the problems in `error`.
Result import(const fs::path& file, const fs::path& folder);
/// Move a document (all its files) to the trash.
Result trash(const DocumentItem& item);

Result createFolder(const fs::path& parent, const std::string& name);
Result renameFolder(const fs::path& folder, const std::string& newName);
/// Move a folder into another one.
Result moveFolder(const fs::path& folder, const fs::path& target);
Result trashFolder(const fs::path& folder);

/// `path` with its prefix `from` (a file or folder) replaced by `to`; `path` itself if it does not start with it.
fs::path remap(const fs::path& path, const fs::path& from, const fs::path& to);

}  // namespace DocumentFiles

}  // namespace xqt
