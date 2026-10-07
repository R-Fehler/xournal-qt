/*
 * xournal-qt: the keywords of a PDF as tags (qt/docs/tags.md): its document information's /Keywords and its XMP
 * metadata's dc:subject, as Zotero, Acrobat and LaTeX's hyperref write them.
 *
 * Reads with qpdf (the trailer, the document information and the catalog's /Metadata; no page is read), on any thread.
 * Writes them as an incremental update, the way the app saves PDFs with notes (qt/docs/tags.md, "Tags in files"),
 * holding the file's fileio::FileWriteLock (a save of the same file waits); a PDF with notes keeps its version history.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>

#include <QString>
#include <QStringList>

#include "filesystem.h"

namespace xqt::pdfkeywords {

/// What a PDF says its keywords are.
struct Keywords {
    QString info;             ///< its document information's /Keywords, as written
    QStringList subject;      ///< its XMP metadata's dc:subject entries, as written
    bool read = false;        ///< the file could be read
    /// As tags (Tags.h): of both, each once
    QStringList tags() const;
};

/// The /Keywords that stand for `tags` when the PDF had `old`: the keywords of `old` whose tag is still wanted stay
/// as they were written ("Machine learning" for the tag Machine-learning), the new tags follow; ", " between them.
QString keywordsFor(const QString& old, const QStringList& tags);

/// Give `pdf` these tags as its keywords, without touching anything else: an incremental update (IncrementalPdf.h)
/// with its document information's /Keywords (removed when there are none) and, where it has XMP metadata, its
/// dc:subject and pdf:Keywords (an archive PDF's metadata is written anew from the document information, so it stays
/// PDF/A: ArchivePdf::update). Atomic. False with `error` (an encrypted PDF is not changed).
bool write(const fs::path& pdf, const QStringList& tags, std::string& error);
/// For tests: called by write() between reading the file and appending the update.
extern std::function<void()> beforeAppend;

/// The keywords of `pdf`.
/// `session`: the file of an open document (a protected PDF is read with its password, PdfEncryption.h); else a
/// protected PDF has none (the library never reads one).
Keywords read(const fs::path& pdf, bool session = false);
/// Its keywords as tags ({} when it has none or cannot be read).
QStringList tagsOf(const fs::path& pdf);

}  // namespace xqt::pdfkeywords
