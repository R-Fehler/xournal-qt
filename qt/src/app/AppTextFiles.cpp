/*
 * xournal-qt: the window's side of text files edited as documents (qt/docs/md-editor.md): opening them, and their
 * changes on disk by other programs.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <cctype>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
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
#include "util/PathUtil.h"
#include "control/ScrollHandler.h"

using namespace xqt;

namespace {
/// A plain text file edited without asking: .txt (other text and code files only after a warning).
bool isPlainTextFile(const fs::path& file) {
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".txt";
}

/// The other text files the user chose to edit as plain text ("Edit anyway", after its warning): they open for
/// editing from then on. In the config folder, the newest last (at most MAX_ACCEPTED).
constexpr int MAX_ACCEPTED = 500;
fs::path acceptedStore() { return Util::getConfigFile("edit-as-text.json"); }
QStringList acceptedFiles() {
    QFile f(QString::fromStdString(acceptedStore().string()));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QStringList list;
    for (const QJsonValue& v: QJsonDocument::fromJson(f.readAll()).array()) {
        list << v.toString();
    }
    return list;
}
bool isAccepted(const fs::path& file) {
    return acceptedFiles().contains(QFileInfo(QString::fromStdString(file.string())).absoluteFilePath());
}
void accept(const fs::path& file) {
    QStringList list = acceptedFiles();
    const QString path = QFileInfo(QString::fromStdString(file.string())).absoluteFilePath();
    list.removeAll(path);
    list << path;
    while (list.size() > MAX_ACCEPTED) {
        list.removeFirst();
    }
    QSaveFile f(QString::fromStdString(acceptedStore().string()));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonArray::fromStringList(list)).toJson(QJsonDocument::Compact));
        f.commit();
    }
}
/// An "other" text file (code, LaTeX, JSON, ...): not a .md, not a .txt.
bool isOtherTextFile(const fs::path& file) {
    return DocumentFiles::isTextFile(file) && !DocumentFiles::isMarkdownFile(file) && !isPlainTextFile(file);
}
}  // namespace

std::unique_ptr<DocumentSession> AppController::openTextFile(const fs::path& file, std::string& error) {
    const bool markdown = DocumentFiles::isMarkdownFile(file);
    if (!markdown && !isPlainTextFile(file) && !(isOtherTextFile(file) && isAccepted(file))) {
        return nullptr;  // (another text file: read-only until "Edit anyway")
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

bool AppController::canEditAnyway() const {
    const DocumentSession* s = session();
    return s && !s->textFile() && !s->hasFilePath() && isOtherTextFile(s->shownFile());
}

bool AppController::editAnyway(bool confirmed) {
    if (!canEditAnyway()) {
        return false;
    }
    const fs::path file = session()->shownFile();
    const QString name = QString::fromStdString(file.filename().string());
    if (!confirmed && !isAccepted(file)) {
        Q_EMIT editAnywayWarning(name);  // (asked once per file; "OK" calls this again, confirmed)
        return false;
    }
    auto text = std::make_unique<TextFile>();
    std::string error;
    if (!text->load(file, TextFile::Kind::Plain, error)) {
        Q_EMIT message(tr("Cannot edit %1").arg(name), tr("It cannot be read."), true);
        return false;
    }
    if (!text->editable() || !QFileInfo(QString::fromStdString(file.string())).isWritable()) {
        const QString why = text->isTooBig() ? tr("It is bigger than %1 MB.").arg(TextFile::MAX_EDIT_BYTES / (1024 * 1024))
                            : !text->isUtf8() ? tr("It is not UTF-8 text.")
                                              : tr("The file cannot be written.");
        Q_EMIT message(tr("Cannot edit %1").arg(name), why, true);
        return false;
    }
    accept(file);
    // The tab shows it as a plain text to edit now (in the place of the read-only one)
    const size_t page = session()->getCurrentPageNo();
    auto doc = MarkdownFile::textDocument(*text);
    auto edited = std::make_unique<DocumentSession>(*app, std::move(doc));
    edited->setTextFile(std::move(text), false);
    const int old = tabs->currentIndex();
    tabs->addTab(std::move(edited));
    tabs->closeTab(old);
    if (DocumentSession* s = session(); s && page < s->getDocument()->getPageCount()) {
        s->setCurrentPageNo(page);
        s->getScrollHandler()->scrollToPage(page);
    }
    watchTextFiles();
    Q_EMIT titleChanged();
    return true;
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
