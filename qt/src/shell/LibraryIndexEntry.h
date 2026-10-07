/*
 * xournal-qt: the inside of LibraryIndex, shared by its source files (LibraryIndex*.cpp) and by nobody else: the
 * entry of a document as the index keeps it, and the helpers the files share.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <map>
#include <memory>
#include <vector>

#include <QCborMap>
#include <QString>
#include <QStringList>

#include "FileStamps.h"
#include "LibraryIndex.h"
#include "session/Vocabulary.h"

namespace xqt {

struct LibraryIndex::Entry {
    fs::path file;                   ///< the document's main file
    QString kind;                    ///< "xopp" (also .xoj), "pdf", "md", "image", "text"
    QString name;
    QString xoppStamp;               ///< of the .xopp, the Markdown file, the image alone ("": a PDF alone)
    fs::path pdf;                    ///< the PDF it uses (next to it, elsewhere, attached; "": none)
    QString pdfStamp;
    QString sample;                  ///< a hash of the start and end of its main file ("": it could not be
                                     ///< read): tells two files with the same size and time apart
    /// Content hashes (contentHash) of its own file (the one of `xoppStamp`) and of its PDF ("": not computed
    /// yet; filled in the background after the documents are indexed): an entry whose files have another time
    /// but the same size and content is taken over instead of reading them again (adopt).
    QString sha, pdfSha;
    std::map<int, QString> pdfText;  ///< simplified text of the PDF pages it shows
    std::vector<int> pdfPage;        ///< per page: the PDF page it shows (-1: none)
    QStringList elementText;         ///< per page: the text of its text elements (simplified)
    std::vector<double> aspects;     ///< per page: height / width
    // A Markdown file (no pages): its text, read through md4c without the syntax; a text file: its text, one block
    QStringList blockText;           ///< per passage (MdPassages.h): its text (simplified)
    std::vector<int> blockLevel;     ///< per passage: a heading's level (0: not a heading)
    QStringList links;               ///< link targets (for backlinks)
    QStringList wikiLinks;           ///< [[wiki link]] targets
    /// Its bookmarks (qt/docs/features/bookmarks.md): page -> label ("": the automatic one). Stored in "notes" when
    /// there are any.
    std::map<int, QString> bookmarks;
    /// Its to-dos (task lines of its Markdown, qt/docs/features/todos.md), without their file. Stored in "notes".
    std::vector<Todo> todos;
    /// Its tags (qt/docs/features/tags.md): of its text (`#tag`), and the keywords of its PDF (kept with the PDF's
    /// stamp). Stored in "notes".
    QStringList textTags, pdfTags;
    /// Both, each once
    QStringList tags() const;
    /// Its PDF's title (PdfTitle.h): the /Title if it looks like one, and the largest text of the first page it
    /// shows. Kept with its PDF's stamp in "notes".
    QString title;
    QString heading;
    /// Its main file is a PDF: what it is (plain, with notes, a text document, an archive PDF). Unknown for other
    /// documents.
    PdfKind pdfKind = PdfKind::Unknown;
    /// A PDF with notes that keeps its versions (version history, PdfHistory.h): how many, read with its kind
    /// from the marker's /History (never the list itself); 0: it keeps none. Stored in "notes".
    int versions = 0;
    /// A PDF protected with a password (PdfEncryption.h): not read (no text, no pages), a lock on its card. Stored
    /// in "notes".
    bool locked = false;
    bool isPdf() const;
    int pageCount() const { return static_cast<int>(elementText.size()); }
    bool showsPdfPages() const;
    /// Nothing changed since it was read.
    bool upToDate(const DocumentItem& item) const;
};

/// The vocabularies of an entry: per passage (a Markdown or text file), else per page (Vocabulary.h).
struct LibraryIndex::EntryWords {
    std::vector<words::Vocabulary> units;
};

/// Helpers of the index's source files (they were one file before).
namespace library_index {
inline QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }
inline fs::path toPath(const QString& s) { return fs::path(s.toStdString()); }
/// The stamp of the file an entry reads itself (its "xopp" stamp): the .xopp, a Markdown file, a lone image; a lone
/// PDF has none (its PDF stamp).
QString ownStamp(const DocumentItem& item);
bool isPdfFile(const fs::path& p);
/// How many versions it keeps (version history: the marker's /History, read with the kind; 0: none).
int versionsOfPdf(const fs::path& pdf);
/// What a PDF is (qt/docs/features/library.md, "Kinds of PDFs"): by its marker (HybridPdf::markerOf: read when it was
/// opened and remembered, so no second read). A text document is a PDF with notes that carries its "name.md".
PdfKind kindOfPdf(const fs::path& pdf);
/// The kind of an entry: what it read ("xopp" also for .xoj, "pdf", "md", "image", "text").
QString entryKind(const DocumentItem& item);
/// Entries without pages: a Markdown file, a text file, an image
bool pageless(const QString& kind);
/// The "pdf-text" entry of a document: the text of its PDF's pages, with the PDF's stamp
QCborMap pdfTextOf(const QString& stamp, const std::map<int, QString>& pages);
/// The first PDF page an entry shows (-1: none)
int firstPdfPage(const std::vector<int>& pdfPage);
}  // namespace library_index

}  // namespace xqt
