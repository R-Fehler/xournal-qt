/*
 * xournal-qt: printing a PDF through Qt's print engine, for systems without a spooler that takes PDF files (lp).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <chrono>
#include <string>

#include <QString>
#include <QStringList>

#include "filesystem.h"

class QObject;

class QPrinter;

namespace xqt {

class DocumentSession;

/// Write what is printed of a document into `file` (AppController::printDocument): with its annotations, the
/// document exported as a PDF (`range`: its pages, "" all); without, the PDF it annotates as it is. A protected
/// document (qt/docs/hybrid-pdf.md, "Encrypted PDFs"): drawn through poppler, which has its password, and without
/// annotations its PDF decrypted (the printer cannot open an encrypted one). Returns "" or why it failed.
std::string writePrintFile(DocumentSession& session, bool withAnnotations, const std::string& range,
                           const fs::path& file);

/// Hand a print file to the spooler: run `program` (lp) and remove `folder` (the file's temporary folder) as soon as
/// it has finished (lp returns once the job's data is in the spool), at the latest after `fallback`. False when it
/// could not be started (`folder` is removed then).
bool spoolAndRemove(const QString& program, const QStringList& arguments, const QString& folder,
                    std::chrono::milliseconds fallback, QObject* context);

/// Prints the PDF file on a printer that a QPrintDialog has set up: the pages of the printer's page range (all of
/// them without one), each drawn by poppler into an image of at most 300 dpi and fitted onto the paper's printable
/// area, turned a quarter when the page and the paper differ in orientation. Copies, duplex and colour are the
/// printer's settings. Used on Windows, where there is no `lp` (see AppController::printDocument).
/// @param error the reason when it returns false
bool printPdfAsImages(const QString& pdfFile, QPrinter& printer, QString* error = nullptr);

}  // namespace xqt
