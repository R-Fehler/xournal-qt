/*
 * xournal-qt: the library's to-dos (qt/docs/todos.md) as the window offers them: the To-dos view of the library home,
 * ticking a to-do there (in its document when it is open, else in its file), opening one at its line.
 *
 * @license GNU GPLv2 or later
 */
#include <shared_mutex>

#include <QCoreApplication>
#include <QMetaObject>
#include <QFileInfo>
#include <QPointer>
#include <QTimer>

#include "AppController.h"
#include "AppServices.h"
#include "control/ScrollHandler.h"
#include "control/ToolHandler.h"
#include "model/Document.h"
#include "session/FileIo.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/TextFile.h"
#include "shell/DocumentFiles.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"
#include "shell/LibraryTodos.h"
#include "shell/SystemApps.h"
#include "shell/TabManager.h"
#include "shell/TodoCalendar.h"
#include "shell/Todos.h"
#include "undo/TextBoxUndoAction.h"
#include "undo/UndoRedoHandler.h"

#include "CanvasView.h"
#include "MdBox.h"
#include "MdTasks.h"
#include "TodoStamp.h"

using namespace xqt;

namespace {
fs::path pathOf(const QString& s) { return fs::path(s.toStdString()); }
}  // namespace

QObject* AppController::libraryTodosModel() const { return libraryTodos; }

bool AppController::setTodoIn(DocumentSession& s, const QString& rawText, int occurrence, bool done) {
    Document* doc = s.getDocument();
    std::optional<todos::Place> place;
    {
        std::shared_lock lock(*doc);
        place = todos::find(*doc, rawText, occurrence);
    }
    if (!place) {
        return false;
    }
    if (place->done == done) {
        return true;
    }
    if (place->box->isInEditing()) {
        return false;  // (being written: its text is the editor's)
    }
    const bool wasModified = s.isModified();
    // The box again with the task set (the same length: pages and other boxes stay as they are), as a tap on its
    // check box does (CanvasView::toggleMarkdownCheckBox)
    auto changed = place->box->cloneText();
    changed->setText(md::tasks::withTask(place->box->getText(), place->mark, done));
    Text* now = changed.get();
    ElementPtr before;
    {
        std::unique_lock lock(*doc);
        auto [old, index] = place->layer->removeElement(place->box);
        before = std::move(old);
        place->layer->insertElement(std::move(changed), index);
    }
    s.getUndoRedoHandler()->addUndoAction(
            std::make_unique<TextBoxUndoAction>(place->page, place->layer, now, std::move(before)));
    place->page->firePageChanged();
    s.firePageChanged(place->pageIndex);
    if (s.textFile()) {
        s.textEdited();  // (a text file: modified by its text)
    }
    // A document without other changes is saved (the library lists the to-do from its file)
    if (!wasModified && (s.hasFilePath() || s.isEditableText()) && !s.isSaving()) {
        startSave(SaveWay::Save, {}, [](bool) {}, &s);
    }
    return true;
}

bool AppController::setTodoDone(const QString& path, const QString& rawText, int occurrence, bool done) {
    const fs::path file = pathOf(path);
    const QString title = tr("To-do not changed");
    // Open in a tab: there, one undo step
    if (const auto open = appServices->openDocuments().find(file, {.textFiles = true}); !open.empty()) {
        auto [window, s] = open.front();
        if (s->isReadOnly()) {
            Q_EMIT message(title, tr("%1 is open read-only.").arg(QString::fromStdString(file.filename().string())),
                           false);
            return false;
        }
        if (!window->setTodoIn(*s, rawText, occurrence, done)) {
            Q_EMIT message(title, tr("The to-do is no longer in the document as the list has it."), false);
            libraryTodos->refresh();
            return false;
        }
        libraryTodos->setPending(path, rawText, occurrence, done);
        return true;
    }
    LibraryIndex* index = library->searchIndex();
    if (const QString why = todos::whyNotWritable(file, index ? index->pdfKind(file) : PdfKind::Unknown);
        !why.isEmpty()) {
        Q_EMIT message(title, why, false);
        return false;
    }
    libraryTodos->setPending(path, rawText, occurrence, done);
    QPointer<AppController> self(this);
    auto failed = [self, path, rawText, occurrence, title](const QString& why) {
        if (!self) {
            return;
        }
        Q_EMIT self->message(title, why, true);
        self->libraryTodos->clearPending(path, rawText, occurrence);
    };
    if (DocumentFiles::isMarkdownFile(file)) {
        // A Markdown file: through its text, in the background
        appServices->jobs().start([self, file, rawText, occurrence, done, failed] {
            bool found = false;
            std::string error;
            const bool ok = todos::setInMarkdownFile(file, rawText, occurrence, done, found, error);
            QMetaObject::invokeMethod(qApp, [self, ok, found, error, failed] {
                if (!self) {
                    return;
                }
                if (!ok) {
                    failed(QString::fromStdString(error));
                } else if (!found) {
                    failed(tr("The to-do is no longer in the file as the list has it."));
                } else {
                    self->library->refresh();  // (its index entry is read again)
                }
            });
        }, BackgroundJobs::Priority::Idle);
        return true;
    }
    // A .xopp or a PDF with notes: loaded in the background, then changed and saved as the app saves documents
    appServices->jobs().start([self, file, rawText, occurrence, done, failed] {
        auto loaded = std::make_shared<DocumentSession::LoadResult>(DocumentSession::loadFile(file));
        QMetaObject::invokeMethod(qApp, [self, file, rawText, occurrence, done, failed, loaded] {
            if (!self) {
                return;
            }
            if (!loaded->document) {
                failed(QString::fromStdString(loaded->error));
                return;
            }
            if (!loaded->hybridChanged.empty() || !loaded->warnings.empty() || !loaded->missingPdf.empty()) {
                failed(tr("%1 needs a look first (it was changed in another app, or not all of it could be read): "
                          "open it to tick the to-do.")
                               .arg(QString::fromStdString(file.filename().string())));
                return;
            }
            // (opened meanwhile: there)
            if (const auto open = self->appServices->openDocuments().find(file, {.textFiles = true}); !open.empty()) {
                if (!open.front().first->setTodoIn(*open.front().second, rawText, occurrence, done)) {
                    failed(tr("The to-do is no longer in the document as the list has it."));
                }
                return;
            }
            auto s = std::make_unique<DocumentSession>(*self->app, std::move(loaded->document));
            {
                Document* doc = s->getDocument();
                std::unique_lock lock(*doc);
                const auto place = todos::find(*doc, rawText, occurrence);
                if (!place) {
                    lock.unlock();
                    failed(tr("The to-do is no longer in the file as the list has it."));
                    return;
                }
                place->box->setText(md::tasks::withTask(place->box->getText(), place->mark, done));
            }
            DocumentSession* saving = s.get();
            self->todoSaves.push_back(std::move(s));
            const bool started = self->startSave(
                    SaveWay::Save, {},
                    [self, saving](bool) {
                        // (after its save is done: the session goes, from the event loop)
                        QTimer::singleShot(0, self, [self, saving] {
                            if (self) {
                                std::erase_if(self->todoSaves, [saving](const auto& t) { return t.get() == saving; });
                            }
                        });
                    },
                    saving);
            if (!started) {
                std::erase_if(self->todoSaves, [saving](const auto& t) { return t.get() == saving; });
                failed(tr("%1 could not be saved.").arg(QString::fromStdString(file.filename().string())));
            }
        });
    }, BackgroundJobs::Priority::Idle);
    return true;
}

bool AppController::openTodo(const QString& path, const QString& rawText, int occurrence, int page) {
    if (!openPath(path) || !session()) {
        return false;
    }
    DocumentSession* s = session();
    Document* doc = s->getDocument();
    std::optional<todos::Place> place;
    std::optional<md::Rect> box;
    {
        std::shared_lock lock(*doc);
        place = todos::find(*doc, rawText, occurrence);
        if (place) {
            box = md::checkBoxRect(*place->box, place->mark);
        }
    }
    const size_t count = doc->getPageCount();
    const size_t p = place ? place->pageIndex
                           : std::min<size_t>(static_cast<size_t>(std::max(0, page)), count > 0 ? count - 1 : 0);
    s->setCurrentPageNo(p);
    s->getScrollHandler()->scrollToPage(p);
    if (box) {
        // Its line in view, once the view has the page (from the event loop)
        const QRectF line(box->x, box->y, box->width, box->height);
        QPointer<DocumentSession> guard(s);
        QTimer::singleShot(0, this, [guard, p, line] {
            if (guard) {
                Q_EMIT guard->scrollToRectRequested(p, line.adjusted(-20, -40, 300, 40));
            }
        });
    }
    return true;
}

// --- the check-box stamp for handwritten to-dos (TodoStamp.h) ---

bool AppController::todoStampArmed() const { return todostamp::isArmed(); }

void AppController::startTodoStamp() {
    DocumentSession* s = session();
    if (!s || s->isReadOnly() || !canvas()) {
        return;
    }
    if (!todostamp::isArmed()) {
        stampPreviousTool = tool();
    }
    canvas()->clearSelection();  // (a tap on a selection would not reach the page)
    todostamp::disarm();  // (the tool changes: not the end of this stamp)
    ToolHandler* th = app->getToolHandler();
    th->selectTool(TOOL_HAND);  // (the tap writes nothing)
    th->fireToolChanged();
    QPointer<AppController> self(this);
    todostamp::arm([self] {
        if (self) {
            self->endTodoStamp(true);
        }
    });
    Q_EMIT todoStampChanged();
    Q_EMIT toolChanged();
    Q_EMIT pageActionDone(tr("Tap where the check box goes, then write the to-do beside it"), false);
}

void AppController::cancelTodoStamp() { endTodoStamp(true); }

void AppController::endTodoStamp(bool restore) {
    const QString previous = std::exchange(stampPreviousTool, QString());
    const bool wasArmed = todostamp::isArmed();
    todostamp::disarm();
    if (restore && !previous.isEmpty() && app->getToolHandler()->getToolType() == TOOL_HAND) {
        selectTool(previous);
    }
    if (wasArmed || restore) {
        Q_EMIT todoStampChanged();
    }
}

void AppController::followTodoStampTool() {
    if (todostamp::isArmed() && app->getToolHandler()->getToolType() != TOOL_HAND) {
        stampPreviousTool.clear();
        todostamp::disarm();
        Q_EMIT todoStampChanged();
    }
}

// --- to the calendar, one way (TodoCalendar.h) ---

bool AppController::addTodoToCalendar(const QVariantMap& row) {
    const todocal::Item item = todocal::itemOf(row);
    if (!item.due.isValid()) {
        Q_EMIT message(tr("Not added to the calendar"), tr("This to-do has no due date."), false);
        return false;
    }
    if (todocal::insertIntoCalendar(item)) {
        return true;  // (Android: the calendar app's new event, filled in)
    }
    const QString file = todocal::writeToCache(item);
    if (file.isEmpty()) {
        Q_EMIT message(tr("Not added to the calendar"), tr("The calendar file could not be written."), true);
        return false;
    }
    if (!SystemApps::instance().openWithSystemApp(file)) {
        Q_EMIT message(tr("No calendar app"),
                       tr("No app opened the calendar file. It is here, to import it by hand:\n%1").arg(file), false);
        return false;
    }
    return true;
}

bool AppController::exportTodos(const QUrl& target) {
    QString path = target.isLocalFile() ? target.toLocalFile() : target.toString();
    if (path.isEmpty()) {
        return false;
    }
    const bool calendar = path.endsWith(QLatin1String(".ics"), Qt::CaseInsensitive);
    if (!calendar && !path.endsWith(QLatin1String(".md"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".md");
    }
    std::vector<todocal::Item> items;
    for (const QVariant& row: libraryTodos->listed()) {
        if (!row.toMap().value("done").toBool()) {
            items.push_back(todocal::itemOf(row.toMap()));
        }
    }
    const QByteArray bytes = calendar ? todocal::ics(items)
                                      : todocal::markdown(items, tr("To-dos of %1").arg(library->name())).toUtf8();
    if (QString why; !fileio::writeFileAtomically(path, bytes, fileio::Sync::Durable, &why)) {
        Q_EMIT message(tr("Export failed"), why, true);
        return false;
    }
    Q_EMIT pageActionDone(tr("Exported to %1").arg(QFileInfo(path).fileName()), false);
    return true;
}
