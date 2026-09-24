/*
 * xournal-qt: printing a PDF through Qt's print engine, for systems without a spooler that takes PDF files (lp).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QString>

class QPrinter;

namespace xqt {

/// Prints the PDF file on a printer that a QPrintDialog has set up: the pages of the printer's page range (all of
/// them without one), each drawn by poppler into an image of at most 300 dpi and fitted onto the paper's printable
/// area, turned a quarter when the page and the paper differ in orientation. Copies, duplex and colour are the
/// printer's settings. Used on Windows, where there is no `lp` (see AppController::printDocument).
/// @param error the reason when it returns false
bool printPdfAsImages(const QString& pdfFile, QPrinter& printer, QString* error = nullptr);

}  // namespace xqt
