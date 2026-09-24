/*
 * xournal-qt: how documents are kept (qt/docs/hybrid-pdf.md, "PDF-only mode"). Asked at the first start, changed in
 * Settings → Documents:
 * - Xournal++ files ("xopp"): .xopp notes next to their PDFs, as Xournal++ keeps them (the behaviour before the
 *   question existed);
 * - PDF files ("pdf"): every document is one PDF with notes (a hybrid PDF). New documents are "name.pdf", notes on a
 *   PDF are saved into that PDF, and nothing is written next to the files (autosaves go to the app cache).
 *
 * The setting is "documentMode" in the xournalQt part of settings.xml. While it is not set, the app works as
 * Xournal++ files and the window asks once. `XQT_DOCUMENT_MODE=xopp|pdf` (tests, scripts) stands in for a setting
 * that is not stored and keeps the question away; a stored setting wins.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QString>

class Settings;

namespace xqt::DocumentMode {

enum class Mode { Unset, Xopp, Pdf };

/// The stored setting (Unset: never chosen).
Mode stored(Settings& settings);
/// The mode in effect: the stored one, else XQT_DOCUMENT_MODE, else Xournal++ files.
Mode effective(Settings& settings);
/// Every document is one PDF (the mode in effect is "PDF files").
bool pdfOnly(Settings& settings);
/// The window asks which way to work: nothing stored, and XQT_DOCUMENT_MODE not set.
bool shouldAsk(Settings& settings);
/// Store the choice (saved at once).
void store(Settings& settings, Mode mode);

QString nameOf(Mode mode);  ///< "xopp", "pdf", "" (Unset)
Mode fromName(const QString& name);  ///< Unset for anything else

}  // namespace xqt::DocumentMode
