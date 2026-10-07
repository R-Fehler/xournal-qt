/*
 * xournal-qt: to-dos (qt/docs/features/todos.md): the task lines of the library's Markdown ("- [ ] call the lab")
 * listed in its To-dos view.
 *
 * The library's index reads every task line (LibraryIndex::todos); which of them are to-dos is a setting, "Collect
 * to-dos from": the lines marked as to-dos (the default: the marker "todo:" anywhere in the line, case ignored; the
 * list does not show it) or every check box. A check-box stamp (the tool for handwritten to-dos) is always one.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>
#include <string>

#include <QString>

#include "model/PageRef.h"

#include "LibraryIndex.h"

class Document;
class Layer;
class Settings;
class Text;

namespace xqt::todos {

/// The marker of a to-do when none is set
inline const QString DEFAULT_MARKER = QStringLiteral("todo:");

/// Which task lines are to-dos (the setting)
struct Rules {
    bool all = false;                ///< every check box (else: the lines with the marker, and stamps)
    QString marker = DEFAULT_MARKER;
    /// The setting ("todoSource": "marked" / "all", "todoMarker" in the xournalQt part of the settings)
    static Rules of(Settings& settings);
};

/// Whether a task line is a to-do by these rules
bool listed(const LibraryIndex::Todo& todo, const Rules& rules);
/// What the list shows of its text: without the marker and the due date, white space simplified
QString shownText(const QString& text, const QString& marker);

// --- a to-do found again in a document, and set done or open ----------------------------------------------------
/// Where a to-do's task line is in a document: its box (Markdown boxes and sticky notes' texts in the order the index
/// reads them; a Markdown file's pages: their boxes) and its mark there.
struct Place {
    PageRef page;
    size_t pageIndex = 0;
    Layer* layer = nullptr;
    Text* box = nullptr;
    size_t mark = 0;  ///< its mark's offset in the box's text
    bool done = false;
};
/// The task line with this text (LibraryIndex::Todo::text), the `occurrence`th one of the document's task lines with
/// it (0-based), or nothing. The caller holds the document's lock.
std::optional<Place> find(Document& doc, const QString& text, int occurrence);
/// The same in a Markdown text: its mark's offset.
std::optional<size_t> find(const std::string& markdown, const QString& text, int occurrence);

/// Why the to-dos of this file cannot be changed while it is not open ("": they can). `kind`: what the index knows of
/// a PDF.
QString whyNotWritable(const fs::path& file, PdfKind kind);
/// Set a to-do of a Markdown file that is not open (any thread): through its text, byte for byte elsewhere
/// (TextFile). False if it could not be (`error`); `found` false: it is not in the file (any more).
bool setInMarkdownFile(const fs::path& file, const QString& text, int occurrence, bool done, bool& found,
                       std::string& error);

}  // namespace xqt::todos
