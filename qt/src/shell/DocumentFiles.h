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
 * Every other file can be listed too, when asked for (`Include`): a text or code file (.txt, .tex, .py, ...: shown as
 * plain text, read-only) or any other file (Office files and the rest: opened with the system app). Each is an item of
 * its own, known by its whole file name. Hidden files, backups ("~") and the files that belong to a .xopp never are.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "filesystem.h"

class QString;

namespace xqt {

struct DocumentItem {
    fs::path xopp;   ///< the .xopp / .xoj, or empty
    fs::path pdf;    ///< the PDF with the same name next to it, or empty
    fs::path md;     ///< a Markdown file (alone), or empty
    fs::path image;  ///< an image (alone, or with the .xopp of the same name that annotates it), or empty
    /// The PDF is a hybrid PDF and the .xopp its export for Xournal++, or the .xopp it was, kept (hybrid-pdf.md): the
    /// PDF is the document. A .xopp changed well after the PDF (edited in Xournal++) is a document of its own instead.
    bool hybrid = false;
    /// A text or code file, or any other file the library lists (alone), or empty.
    fs::path other;
    /// Conflict copies of its files that sync apps left next to it (SyncConflicts.h; found by scan()): they are shown
    /// on its card, not as documents of their own. Each is the main file of such a copy. Not part of the document
    /// (rename, move, trash leave them).
    std::vector<fs::path> conflicts;
    /// The file to open: the .xopp (not the export of a hybrid PDF), else the PDF, the Markdown file, the image, the
    /// other file.
    const fs::path& main() const {
        return !xopp.empty() && !hybrid ? xopp
               : !pdf.empty()           ? pdf
               : !md.empty()            ? md
               : !image.empty()         ? image
                                        : other;
    }
    fs::path folder() const { return main().parent_path(); }
    /// The name shown for it: the file name without extension (a text or other file: its whole file name).
    std::string name() const;
    bool valid() const { return !main().empty(); }
    /// What it is, for the library (a filter by kind): a PDF (with or without its .xopp), an image (with or without
    /// its .xopp), a Markdown file, notes (a .xopp alone), a text or code file, any other file.
    enum class Kind { Notes, Pdf, Markdown, Image, Text, Other };
    Kind kind() const;
    /// "notes", "pdf", "md", "image", "text", "other"
    const char* kindName() const;
    /// `file` is one of its files (the .xopp, the PDF, the Markdown file, the image, the other file).
    bool has(const fs::path& file) const;
    bool operator==(const DocumentItem& o) const {
        return xopp == o.xopp && pdf == o.pdf && md == o.md && image == o.image && other == o.other;
    }
};

/// What a PDF that is a document's file is, by what it carries (qt/docs/library.md, "Kinds of PDFs"): the library
/// index keeps it per PDF (LibraryIndex::pdfKind), so the cards and the "Show" filter never look into a PDF.
enum class PdfKind {
    Unknown,      ///< not indexed (yet), or not a PDF
    Plain,        ///< a PDF (without our marker)
    Notes,        ///< a PDF with notes (a hybrid PDF: hybrid-pdf.md)
    Text,         ///< a PDF text document: a PDF with notes whose page 1 starts the page's Markdown text (md-pdf.md)
    Archive,      ///< an archive PDF (PDF/A-3 with its notes)
    ArchiveText,  ///< an archive PDF of a text document
};
/// "plain", "notes", "text", "archive", "archive-text" ("" for Unknown): in the index and for QML
const char* pdfKindName(PdfKind kind);
PdfKind pdfKindNamed(const QString& name);
/// A PDF with notes (also a text document, an archive PDF)
inline bool hasNotes(PdfKind k) { return k != PdfKind::Unknown && k != PdfKind::Plain; }
inline bool isTextDocument(PdfKind k) { return k == PdfKind::Text || k == PdfKind::ArchiveText; }

/// Which kinds of files the library shows ("Show" in the library, a setting of each library). Hidden files and the
/// app's cache folders are never shown.
struct ShowFilter {
    bool notes = true;              ///< .xopp, .xoj alone
    bool pdfs = true;               ///< PDFs, alone or with their .xopp (also hybrid PDFs)
    bool onlyPdfsWithNotes = false; ///< of the PDFs only those with a .xopp next to them, and hybrid PDFs
    bool onlyTextDocuments = false; ///< of the PDFs only PDF text documents
    bool markdown = true;
    bool images = true;             ///< alone or with their .xopp
    bool text = false;              ///< text and code files
    bool other = false;             ///< all other files
    /// `pdfKind`: what the item's PDF is when it is the document's file (the library index knows it; a PDF not
    /// indexed yet is not one with notes). Only asked for when kindMatters().
    bool shows(const DocumentItem& item, PdfKind pdfKind = PdfKind::Unknown) const;
    /// Whether shows() needs the kind of a PDF
    bool kindMatters() const { return pdfs && (onlyPdfsWithNotes || onlyTextDocuments); }
    /// What a listing must include for it (DocumentFiles::Include).
    unsigned include() const;
    bool isDefault() const { return *this == ShowFilter(); }
    bool operator==(const ShowFilter& o) const {
        return notes == o.notes && pdfs == o.pdfs && onlyPdfsWithNotes == o.onlyPdfsWithNotes &&
               onlyTextDocuments == o.onlyTextDocuments &&
               markdown == o.markdown && images == o.images && text == o.text && other == o.other;
    }
};

namespace DocumentFiles {

/// The folder ".xournal_library" (library metadata) and other hidden entries are never shown.
constexpr const char* META_DIR = ".xournal_library";

/// What a listing holds besides the documents (flags).
enum Include : unsigned {
    Documents = 0,   ///< notes, PDFs, Markdown files, images
    TextFiles = 1,   ///< and text and code files
    OtherFiles = 2,  ///< and all other files
    AllFiles = TextFiles | OtherFiles,
};

struct Listing {
    std::vector<fs::path> folders;
    std::vector<DocumentItem> items;
};
/// Folders and documents in a folder, not recursive (with text and other files if included). Hidden entries,
/// backups, attached PDFs and background images are left out. Conflict copies of sync apps whose document is in the
/// folder are its `conflicts`, not items.
Listing scan(const fs::path& dir, unsigned include = Documents);
/// All documents in a folder and its subfolders.
std::vector<DocumentItem> scanRecursive(const fs::path& dir, unsigned include = Documents);
/// All subfolders (recursive), e.g. as targets for "Move to".
std::vector<fs::path> foldersRecursive(const fs::path& dir);
/// The item a document file belongs to (a .xopp with its PDF, a PDF with its .xopp); with `include`, also a text or
/// other file. Invalid if it is none.
DocumentItem itemOf(const fs::path& file, unsigned include = Documents);
bool isDocumentFile(const fs::path& file);
bool isMarkdownFile(const fs::path& file);
/// An image the library shows (not a background image stored with a .xopp).
bool isImageFile(const fs::path& file);
/// A text or code file (by its extension or name: .txt, .tex, .py, .json, Makefile, ...): shown as plain text.
bool isTextFile(const fs::path& file);
/// A file the library lists as "other" (not a document, not text; not hidden, a backup or a file of a .xopp). The
/// file is not looked at, only its name.
bool isOtherFile(const fs::path& file);
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

/// Rename a document: the .xopp and its PDF (or image) together. A text or other file: `newName` is its whole file
/// name. Every rename in the app goes through here (the library, the recent list, a tab, qt/docs/library.md
/// "Renaming"); what follows it (index, reading places, open tabs, links) is the caller's (Result::moved).
Result rename(const DocumentItem& item, const std::string& newName);
/// Why a document cannot get `newName`, as rename() checks it (the name fields show it while the name is typed).
/// None: it can, or it is its name already. ReadOnly: its file or its folder cannot be written.
enum class RenameProblem { None, Empty, Separator, Invalid, Taken, ReadOnly, Missing };
RenameProblem renameProblem(const DocumentItem& item, const std::string& newName);
/// The problems of the name alone (Empty, Separator, Invalid; None).
RenameProblem nameProblem(const std::string& newName);
/// Its files and its folder can be written (else renaming it is ReadOnly).
bool renamable(const DocumentItem& item);
/// The same for a folder (renameFolder).
RenameProblem folderRenameProblem(const fs::path& folder, const std::string& newName);
/// Move a document into another folder.
Result move(const DocumentItem& item, const fs::path& folder);
/// Copy a document file into a folder (under a free name): a .xopp with the PDF it uses (stored as "<name>.pdf" next
/// to it), a PDF with the .xopp next to it, an image with its .xopp, a Markdown file. A folder is copied with its
/// whole folder structure and all documents in it (text and other files only if included; hidden folders stay
/// behind). A single text or other file is copied if included. Problems with single documents do not stop the rest:
/// `ok` with the problems in `error`.
Result import(const fs::path& file, const fs::path& folder, unsigned include = Documents);
/// Move a document (all its files) to the trash.
Result trash(const DocumentItem& item);

Result createFolder(const fs::path& parent, const std::string& name);
Result renameFolder(const fs::path& folder, const std::string& newName);
/// Move a folder into another one.
Result moveFolder(const fs::path& folder, const fs::path& target);
Result trashFolder(const fs::path& folder);

/// `path` with its prefix `from` (a file or folder) replaced by `to`; `path` itself if it does not start with it.
fs::path remap(const fs::path& path, const fs::path& from, const fs::path& to);

/// The order of names in the library: natural ("2" before "10") and case-insensitive ("lecture" before "Makefile"),
/// by the language's rules (QCollator, e.g. accents) where the platform has them. Without a language (the C locale,
/// as in containers) or a collator that cannot do this (Android has no ICU), digits by their value and the rest by
/// case-folded code points. Equal names in another case are ordered case-sensitively, so the order is total.
int compareNames(const QString& a, const QString& b);
inline bool namesLess(const QString& a, const QString& b) { return compareNames(a, b) < 0; }

}  // namespace DocumentFiles

}  // namespace xqt
