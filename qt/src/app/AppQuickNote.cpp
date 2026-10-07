/*
 * xournal-qt: Quick note (qt/docs/features/quick-note.md). One action makes a note to write on at once: a new document
 * in the library's "Inbox" named by the date and time, or a line in today's Markdown note there (the setting
 * "quickNote").
 *
 * @license GNU GPLv2 or later
 */
#include <QFile>

#include "AppController.h"
#include "CanvasView.h"
#include "MarkdownEditor.h"
#include "session/DocumentSession.h"
#include "session/FileIo.h"
#include "shell/DocumentFiles.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"
#include "shell/SettingsModel.h"
#include "shell/TabManager.h"

using namespace xqt;

namespace {
QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }
}  // namespace

bool AppController::quickNote() { return quickNoteAt(QDateTime::currentDateTime()); }

bool AppController::quickNoteAt(const QDateTime& when) {
    const Library* lib = library->library();
    if (!lib) {
        newDocument();  // (no library to keep it in: a new document, saved where the user says)
        return true;
    }
    const fs::path inbox = lib->root() / QUICK_NOTE_FOLDER;
    std::error_code ec;
    fs::create_directories(inbox, ec);
    if (!fs::is_directory(inbox, ec)) {
        Q_EMIT message(tr("Cannot create the file"), tr("The folder \"%1\" cannot be made.").arg(qstr(inbox)), true);
        return false;
    }

    if (settingsView->get("quickNote").toString() != QLatin1String("daily")) {
        // A new note named by the date and time (sorted by name it is in order), in the format of new documents
        const std::string stem = DocumentFiles::uniqueName(inbox, when.toString("yyyy-MM-dd HH-mm").toStdString());
        if (!createDocumentAt(inbox / (stem + (pdfOnly() ? ".pdf" : ".xopp")))) {
            return false;
        }
        // Ready for the pen (the toolbox's pen used last; the pen in hand stays)
        if (tool() != QLatin1String("pen")) {
            takeToolOfType("pen");
        }
        return true;
    }

    // Today's Markdown note: a line "- 21:30 " at its end, the cursor after it
    const fs::path md = inbox / (when.toString("yyyy-MM-dd").toStdString() + ".md");
    const std::string line = "- " + when.toString("HH:mm").toStdString() + " ";
    const bool open = tabs->indexOfFile(md) >= 0;
    if (!open) {
        // (not open: the line goes into the file, so it is there however the app ends)
        QByteArray bytes;
        if (QFile f(qstr(md)); f.exists()) {
            if (!f.open(QIODevice::ReadOnly)) {
                Q_EMIT message(tr("Cannot open the file"), tr("\"%1\" cannot be read.").arg(qstr(md)), true);
                return false;
            }
            bytes = f.readAll();
        }
        if (!bytes.isEmpty() && !bytes.endsWith('\n')) {
            bytes += '\n';
        }
        bytes += QByteArray::fromStdString(line);
        if (!fileio::writeFileAtomically(qstr(md), bytes)) {
            Q_EMIT message(tr("Cannot create the file"), tr("\"%1\" cannot be written.").arg(qstr(md)), true);
            return false;
        }
        library->refresh();
    }
    if (!openPath(qstr(md))) {  // (open already: its tab)
        return false;
    }
    CanvasView* v = canvas();
    if (!v || !v->ensureTextEditor() || !v->getMarkdownEditor()) {
        return false;
    }
    MarkdownEditor* editor = v->getMarkdownEditor();
    if (open) {
        // (open in a tab: added to the text there, which may hold changes not saved yet; one undo step)
        const std::string& text = editor->text();
        md::format::Edit add;
        add.from = add.to = text.size();
        add.with = (text.empty() || text.back() == '\n' ? "" : "\n") + line;
        add.anchor = add.caret = text.size() + add.with.size();
        editor->applyEdit(add);
    }
    editor->setCursorPosition(editor->text().size());
    return true;
}
