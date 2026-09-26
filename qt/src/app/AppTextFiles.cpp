/*
 * xournal-qt: the window's side of text files edited as documents (qt/docs/md-editor.md): opening them, and their
 * changes on disk by other programs.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <cctype>
#include <shared_mutex>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QGuiApplication>

#include "AppController.h"
#include "control/settings/Settings.h"
#include "model/Document.h"
#include "CanvasView.h"
#include "MarkdownEditor.h"
#include "MarkdownFile.h"
#include "session/AppContext.h"
#include "session/DocumentMode.h"
#include "session/DocumentSession.h"
#include "session/TextDocument.h"
#include "session/TextFile.h"
#include "shell/DocumentFiles.h"
#include "shell/LibraryModel.h"
#include "shell/LocalUrl.h"
#include "shell/RecentFiles.h"
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
        if (f.flush()) {  // (see LibraryCache.cpp, writeFile)
            f.commit();
        }
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
    bool continuous = false;
    app->getSettings()->getCustomElement("xournalQt").getBool("textContinuous", continuous);
    continuous = continuous && editable;
    if (editable) {
        doc = MarkdownFile::textDocument(*text, continuous);
    } else {
        doc = MarkdownFile::document(markdown ? MarkdownFile::read(file) : MarkdownFile::readAsPlainText(file));
    }
    auto session = std::make_unique<DocumentSession>(*app, std::move(doc));
    session->setTextContinuous(continuous);
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
    bool continuous = false;
    app->getSettings()->getCustomElement("xournalQt").getBool("textContinuous", continuous);
    auto doc = MarkdownFile::textDocument(*text, continuous);
    auto edited = std::make_unique<DocumentSession>(*app, std::move(doc));
    edited->setTextContinuous(continuous);
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

bool AppController::textContinuous() const {
    if (const DocumentSession* s = session(); s && s->isEditableText()) {
        return s->isTextContinuous();  // (the current text document as it is laid out)
    }
    bool on = false;
    app->getSettings()->getCustomElement("xournalQt").getBool("textContinuous", on);
    return on;
}

void AppController::setTextContinuous(bool on) {
    bool stored = false;
    app->getSettings()->getCustomElement("xournalQt").getBool("textContinuous", stored);
    if (on != stored) {
        app->getSettings()->getCustomElement("xournalQt").setBool("textContinuous", on);
        app->getSettings()->customSettingsChanged();
    }
    // The current text document follows (the others when they are opened again)
    if (DocumentSession* s = session(); s && s->isEditableText() && s->isTextContinuous() != on) {
        MarkdownFile::relayout(*s, on);
    }
    Q_EMIT textLayoutChanged();
}

QString AppController::sharedTextFile(const QString& path) const {
    if (!path.isEmpty()) {
        const fs::path file(path.toStdString());
        return DocumentFiles::isMarkdownFile(file) || DocumentFiles::isTextFile(file) ? path : QString();
    }
    const DocumentSession* s = session();
    if (!s || s->hasFilePath() || !(s->textFile() || DocumentFiles::isTextFile(s->shownFile()))) {
        return {};
    }
    return QString::fromStdString(s->shownFile().string());
}

bool AppController::canOpenExternally() const {
    const DocumentSession* s = session();
    return s && !s->hasFilePath() && !s->shownFile().empty();
}

bool AppController::openExternally() {
    if (!canOpenExternally()) {
        return false;
    }
    textCheckTimer.stop();
    return openWithSystemApp(QString::fromStdString(session()->shownFile().string()));
}

bool AppController::createTextFile(const QString& name, const QString& extension) {
    if ((extension != QLatin1String(".md") && extension != QLatin1String(".txt")) || !library->available()) {
        return false;
    }
    const QString path = library->newTextFilePath(name, extension);
    QSaveFile f(path);
    if (path.isEmpty() || !f.open(QIODevice::WriteOnly) || !f.commit()) {
        Q_EMIT message(tr("Cannot create the file"), tr("\"%1\" cannot be written.").arg(path), true);
        return false;
    }
    library->refresh();
    if (!openPath(path)) {
        return false;
    }
    if (CanvasView* v = canvas()) {
        v->ensureTextEditor();  // (write at once)
    }
    return true;
}

bool AppController::editAsNotes() {
    DocumentSession* s = session();
    if (!s || !s->textFile() || s->hasFilePath() || s->textFile()->kind() != TextFile::Kind::Markdown) {
        return false;
    }
    const fs::path md = s->textFile()->path();
    const std::string text = s->currentText();  // (as it is here, saved or not)
    // (a text document of notes: typing goes into its text, also when the .md is empty; qt/docs/md-pdf.md)
    auto notes = std::make_unique<DocumentSession>(*app, MarkdownFile::notesDocument(text));
    fs::path xopp = md;
    xopp.replace_extension(".xopp");
    notes->setMadeFrom(xopp);
    tabs->addTab(std::move(notes));  // (next to the .md)
    setHomeVisible(false);
    Q_EMIT titleChanged();
    fs::path suggested = xopp;
    if (pdfOnly()) {
        suggested = fs::path(suggestedHybridFile().toLocalFile().toStdString());  // (a PDF with notes)
    }
    Q_EMIT pageActionDone(tr("Notes made from %1: write on them; saving suggests %2 next to it")
                                  .arg(QString::fromStdString(md.filename().string()),
                                       QString::fromStdString(suggested.filename().string())),
                          false);
    return true;
}

// --- text documents as PDF (qt/docs/md-pdf.md) ------------------------------------------------------------------------

bool AppController::newTextAsPdf() const {
    return DocumentMode::newTextDocuments(*app->getSettings()) == DocumentMode::TextKind::Pdf;
}

bool AppController::makeTextPdf(const std::string& text, const fs::path& pdf) {
    auto made = std::make_unique<DocumentSession>(*app, MarkdownFile::notesDocument(text));
    DocumentSession* created = made.get();
    tabs->addTab(std::move(made));
    setHomeVisible(false);
    const DocumentSession::SaveResult r = created->saveAsHybrid(pdf);
    if (!r.ok) {
        Q_EMIT titleChanged();
        Q_EMIT message(tr("Saving failed"), QString::fromStdString(r.error), true);
        return false;  // (the tab stays, unsaved: Save as… writes it elsewhere)
    }
    recent->add(created->getFilePath());
    afterHybridSave(*created);  // (with the library's refresh)
    Q_EMIT titleChanged();
    if (CanvasView* v = canvas(); v && tabs->currentSession() == created) {
        v->ensureTextEditor();  // (write at once)
    }
    return true;
}

bool AppController::createTextDocument(const QString& name) {
    if (!newTextAsPdf()) {
        return createTextFile(name, QStringLiteral(".md"));
    }
    if (!library->available()) {
        return false;
    }
    fs::path pdf(library->newDocumentPath(name).toStdString());  // (a name free for every kind of document)
    pdf.replace_extension(".pdf");
    return makeTextPdf(std::string(), pdf);
}

bool AppController::openAsPdfDocument() {
    DocumentSession* s = session();
    if (!s || !s->textFile() || s->hasFilePath() || s->textFile()->kind() != TextFile::Kind::Markdown) {
        return false;
    }
    const fs::path md = s->textFile()->path();
    const std::string text = s->currentText();  // (as it is here, saved or not)
    fs::path pdf = md.parent_path() / (md.stem().string() + ".pdf");
    std::error_code ec;
    for (int i = 2; fs::exists(pdf, ec); ++i) {
        pdf = md.parent_path() / (md.stem().string() + " (" + std::to_string(i) + ").pdf");
    }
    if (!makeTextPdf(text, pdf)) {
        return false;
    }
    Q_EMIT pageActionDone(tr("%1 made from %2, which stays as it is")
                                  .arg(QString::fromStdString(pdf.filename().string()),
                                       QString::fromStdString(md.filename().string())),
                          false);
    return true;
}

bool AppController::textNotes() const {
    const CanvasView* v = canvas();
    return v && v->typesIntoFlow();
}

bool AppController::hasMarkdownText() const {
    DocumentSession* s = session();
    if (!s || s->textFile()) {
        return false;
    }
    std::shared_lock lock(*s->getDocument());
    return TextDocument::isTextDocument(*s->getDocument()) || TextDocument::hasMarkdownText(*s->getDocument());
}

QUrl AppController::markdownExportFile() const {
    const DocumentSession* s = session();
    if (!s || s->documentFile().empty() || pdfOnly()) {
        return {};  // (PDF files: nothing is written next to files, the window asks)
    }
    const fs::path doc = s->documentFile();
    return QUrl::fromLocalFile(QString::fromStdString((doc.parent_path() / (doc.stem().string() + ".md")).string()));
}

QUrl AppController::suggestedMarkdownExport() const {
    const DocumentSession* s = session();
    if (!s) {
        return {};
    }
    fs::path target = s->documentFile();
    if (target.empty()) {
        target = fs::path(suggestedSaveFile().toLocalFile().toStdString());
    }
    target.replace_extension(".md");
    return QUrl::fromLocalFile(QString::fromStdString(target.string()));
}

bool AppController::exportMarkdown(const QUrl& file) {
    DocumentSession* s = session();
    fs::path target(localPathOf(file).toStdString());
    if (!s || s->textFile() || target.empty()) {
        return false;
    }
    if (target.extension().empty()) {
        target += ".md";
    }
    std::string text;
    {
        std::shared_lock lock(*s->getDocument());
        text = TextDocument::markdown(*s->getDocument());
    }
    // (the images of the text go into "name.assets/" once qt/md-images is there)
    const QString path = QString::fromStdString(target.string());
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(text.data(), static_cast<qint64>(text.size())) < 0 ||
        !out.commit()) {
        Q_EMIT message(tr("Export as Markdown failed"), tr("\"%1\" cannot be written.").arg(path), true);
        return false;
    }
    library->refresh();
    Q_EMIT pageActionDone(tr("Markdown written: %1").arg(QString::fromStdString(target.filename().string())), false);
    return true;
}

QString AppController::externalFileOf(const QString& path) const {
    const DocumentItem item = DocumentFiles::itemOf(fs::path(path.toStdString()), DocumentFiles::AllFiles);
    if (!item.valid() || !item.pdf.empty()) {
        return {};  // (a PDF, with its notes or not: opened here)
    }
    const fs::path& file = !item.image.empty() ? item.image : !item.md.empty() ? item.md : item.other;
    return file.empty() ? QString() : QString::fromStdString(file.string());
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
        // After the app's own saves (a file renamed over the old one is watched anew; nothing counts as changed)
        connect(tabs.get(), &TabManager::savingChanged, this, [this] {
            if (!tabs->anySaving()) {
                textCheckTimer.start();
            }
        });
    }
    QStringList wanted;
    for (int i = 0; i < tabs->count(); ++i) {
        const DocumentSession* s = tabs->session(i);
        if (s->textFile() && !s->hasFilePath()) {
            wanted << QString::fromStdString(s->textFile()->path().string());
        }
        for (const fs::path& f: s->filesOnDisk()) {  // (a .xopp, a PDF)
            wanted << QString::fromStdString(f.string());
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
    // (a reload replaces a tab: the list is taken first)
    std::vector<QPointer<DocumentSession>> sessions;
    for (int i = 0; i < tabs->count(); ++i) {
        sessions.emplace_back(tabs->session(i));
    }
    for (const auto& s: sessions) {
        if (s) {
            checkTextFile(s);
        }
        if (s) {
            checkDocumentFiles(s);
        }
    }
    watchTextFiles();
}

void AppController::checkDocumentFiles(DocumentSession* s) {
    if (!s || s->textFile() || s == askingTextChange || !s->filesChangedOnDisk()) {
        return;
    }
    const QString name = QString::fromStdString(s->documentFile().filename().string());
    if (!s->isModified()) {
        if (reloadDocument(s)) {  // nothing to lose: the files as they are now
            Q_EMIT pageActionDone(tr("%1 was changed by another app: shown as it is now").arg(name), false);
        }
        return;
    }
    // Changes here and there: the window asks (with that tab shown)
    askingTextChange = s;
    tabs->setCurrentIndex(tabs->indexOf(s));
    setHomeVisible(false);
    Q_EMIT documentChangedOnDisk(name);
}

bool AppController::reloadDocument(DocumentSession* s) {
    const int index = tabs->indexOf(s);
    if (index < 0) {
        return false;
    }
    const fs::path file = s->documentFile();
    auto result = DocumentSession::loadFile(file);
    if (!result.document) {
        s->stampFiles();  // (not asked again about this version)
        Q_EMIT message(tr("Cannot reload"),
                       tr("%1 was changed by another app, but it cannot be read: %2")
                               .arg(QString::fromStdString(file.filename().string()),
                                    QString::fromStdString(result.error)),
                       true);
        return false;
    }
    const int current = tabs->currentIndex();
    const size_t page = s->getCurrentPageNo();
    const std::vector<std::string> hybridChanged = result.hybridChanged;
    // The new one right after it, then the old one closed: the new one is in its place
    tabs->setCurrentIndex(index);
    tabs->addTab(std::make_unique<DocumentSession>(*app, std::move(result.document)));
    DocumentSession* fresh = tabs->currentSession();
    closeTab(index);
    if (current != index) {
        tabs->setCurrentIndex(current);
    }
    const size_t p = std::min(page, fresh->getDocument()->getPageCount() - 1);
    fresh->setCurrentPageNo(p);
    fresh->getScrollHandler()->scrollToPage(p);
    if (!hybridChanged.empty()) {
        fresh->setHybridChanges(hybridChanged);
    }
    watchTextFiles();
    return true;
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
    if (!s->textFile()) {  // a document
        if (reload) {
            reloadDocument(s);
        } else {
            s->stampFiles();  // (the next save writes over it; not asked again for this version)
        }
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
