/*
 * xournal-qt: the window's side of text files edited as documents (qt/docs/md-editor.md): opening them, and their
 * changes on disk by other programs.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <cctype>

#include <QFileInfo>
#include <QGuiApplication>

#include "AppController.h"
#include "model/Document.h"
#include "CanvasView.h"
#include "MarkdownEditor.h"
#include "MarkdownFile.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/TextFile.h"
#include "shell/DocumentFiles.h"
#include "shell/TabManager.h"

using namespace xqt;

namespace {
/// A plain text file edited without asking: .txt (other text and code files only after a warning).
bool isPlainTextFile(const fs::path& file) {
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".txt";
}
}  // namespace

std::unique_ptr<DocumentSession> AppController::openTextFile(const fs::path& file, std::string& error) {
    const bool markdown = DocumentFiles::isMarkdownFile(file);
    if (!markdown && !isPlainTextFile(file)) {
        return nullptr;
    }
    auto text = std::make_unique<TextFile>();
    if (!text->load(file, markdown ? TextFile::Kind::Markdown : TextFile::Kind::Plain, error)) {
        error = tr("\"%1\" cannot be read.").arg(QString::fromStdString(file.string())).toStdString();
        return nullptr;
    }
    // Edited when it can be written back as it was: UTF-8, not too big, a file we may write
    const bool editable = text->editable() && QFileInfo(QString::fromStdString(file.string())).isWritable();
    std::unique_ptr<Document> doc;
    if (editable) {
        doc = MarkdownFile::textDocument(*text);
    } else {
        doc = MarkdownFile::document(markdown ? MarkdownFile::read(file) : MarkdownFile::readAsPlainText(file));
    }
    auto session = std::make_unique<DocumentSession>(*app, std::move(doc));
    session->setTextFile(std::move(text), !editable);
    return session;
}

QString AppController::textDocument() const {
    const DocumentSession* s = session();
    if (!s || !s->textFile() || s->hasFilePath()) {
        return {};
    }
    return s->textFile()->kind() == TextFile::Kind::Markdown ? QStringLiteral("markdown") : QStringLiteral("plain");
}

bool AppController::textEditable() const { return session() && session()->isEditableText(); }

void AppController::watchTextFiles() {
    if (!textWatcher) {
        textWatcher = std::make_unique<QFileSystemWatcher>();
        textCheckTimer.setSingleShot(true);
        textCheckTimer.setInterval(300);
        connect(&textCheckTimer, &QTimer::timeout, this, &AppController::checkTextFiles);
        // (a file replaced by another program is a new file: watched again by watchTextFiles)
        connect(textWatcher.get(), &QFileSystemWatcher::fileChanged, this, [this] { textCheckTimer.start(); });
        connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) {
                textCheckTimer.start();  // (back from another app: what did it do to the files?)
            }
        });
    }
    QStringList wanted;
    for (int i = 0; i < tabs->count(); ++i) {
        const DocumentSession* s = tabs->session(i);
        if (s->textFile() && !s->hasFilePath()) {
            wanted << QString::fromStdString(s->textFile()->path().string());
        }
    }
    const QStringList watched = textWatcher->files();
    for (const QString& f: watched) {
        if (!wanted.contains(f)) {
            textWatcher->removePath(f);
        }
    }
    for (const QString& f: wanted) {
        if (!watched.contains(f) && QFileInfo::exists(f)) {
            textWatcher->addPath(f);
        }
    }
}

void AppController::checkTextFiles() {
    for (int i = 0; i < tabs->count(); ++i) {
        checkTextFile(tabs->session(i));
    }
    watchTextFiles();
}

void AppController::checkTextFile(DocumentSession* s) {
    if (!s || !s->textFile() || s->hasFilePath() || s == askingTextChange) {
        return;
    }
    std::string bytes;
    if (!s->textChangedOnDisk(bytes)) {
        return;
    }
    const QString name = QString::fromStdString(s->textFile()->path().filename().string());
    if (!s->isModified()) {
        reloadText(s, std::move(bytes));  // nothing to lose: the file as it is now
        Q_EMIT pageActionDone(tr("%1 was changed by another app: shown as it is now").arg(name), false);
        return;
    }
    // Changes here and there: the window asks (with that tab shown)
    askingTextChange = s;
    tabs->setCurrentIndex(tabs->indexOf(s));
    setHomeVisible(false);
    Q_EMIT textChangedOnDisk(name);
}

void AppController::resolveTextChange(bool reload) {
    DocumentSession* s = askingTextChange;
    askingTextChange = nullptr;
    if (!s) {
        return;
    }
    if (!reload) {
        s->keepTextOverDisk();  // (the next save writes over it; not asked again for this version)
        return;
    }
    std::string bytes;
    if (s->textChangedOnDisk(bytes)) {  // (as it is now)
        reloadText(s, std::move(bytes));
    }
}

void AppController::reloadText(DocumentSession* s, std::string bytes) {
    const int index = tabs->indexOf(s);
    CanvasView* view = index >= 0 ? tabs->view(index) : nullptr;
    const MarkdownEditor* editor = view ? view->getMarkdownEditor() : nullptr;
    const size_t cursor = editor ? editor->cursorPosition() : std::string::npos;
    MarkdownFile::setText(*s, TextFile::normalized(bytes));  // (one undo step: undo brings the text before back)
    s->textReloaded(std::move(bytes));
    if (view && cursor != std::string::npos && view->ensureTextEditor()) {
        view->getMarkdownEditor()->setCursorPosition(cursor);  // (as Ghostwriter does)
    }
}
