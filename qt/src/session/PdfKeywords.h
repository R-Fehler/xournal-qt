/*
 * xournal-qt: the keywords of a PDF as tags (qt/docs/tags.md): its document information's /Keywords and its XMP
 * metadata's dc:subject, as Zotero, Acrobat and LaTeX's hyperref write them.
 *
 * Reads with qpdf (the trailer, the document information and the catalog's /Metadata; no page is read), on any thread.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

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

/// The keywords of `pdf`.
Keywords read(const fs::path& pdf);
/// Its keywords as tags ({} when it has none or cannot be read).
QStringList tagsOf(const fs::path& pdf);

}  // namespace xqt::pdfkeywords
