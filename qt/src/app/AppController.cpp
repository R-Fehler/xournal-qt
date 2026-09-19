#include "AppController.h"

#include <algorithm>

#include <QFileInfo>

#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "control/settings/Settings.h"
#include "gui/toolbarMenubar/model/ColorPalette.h"
#include "model/Document.h"
#include "undo/UndoRedoHandler.h"
#include "util/NamedColor.h"
#include "util/XojMsgBox.h"

#include "CanvasView.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

using namespace xqt;

namespace {
QColor toQColor(Color c) { return QColor(c.red, c.green, c.blue); }
Color toColor(const QColor& c) {
    return Color(static_cast<uint8_t>(c.red()), static_cast<uint8_t>(c.green()), static_cast<uint8_t>(c.blue()));
}
}  // namespace

AppController::AppController(QObject* parent): QObject(parent) {
    app = std::make_unique<AppContext>(AppContext::defaultResourceDir());
    colors = std::make_unique<Palette>(app->getResourceDir() / "palettes" / "xournal.gpl");
    try {
        colors->load();
    } catch (const std::exception& e) {
        colors->load_default();
    }
    // Messages from the reused core (XojMsgBox) are shown by the QML UI.
    xoj::compat::setMessageSink([this](xoj::compat::MessageRequest r, xoj::util::move_only_function<void(int)> done) {
        const bool error = r.kind == xoj::compat::MessageKind::Error;
        QMetaObject::invokeMethod(this, [this, title = QString::fromStdString(r.title),
                                         text = QString::fromStdString(r.text),
                                         error] { Q_EMIT message(title, text, error); });
        if (done) {
            done(r.buttons.empty() ? 0 : r.buttons.front().response);  // questions: first button until dialogs exist
        }
    });
    connect(app.get(), &AppContext::activeToolChanged, this, &AppController::toolChanged);
    connect(app.get(), &AppContext::toolPropertiesChanged, this, &AppController::toolChanged);
    newDocument();
}

AppController::~AppController() {
    xoj::compat::setMessageSink({});
    canvas.reset();
    session.reset();
}

void AppController::shutdown() {
    if (session) {
        session->deleteAutosaveFile();
    }
    app->getToolHandler()->saveSettings();
    app->getSettings()->save();
}

void AppController::setSession(std::unique_ptr<DocumentSession> s) {
    canvas.reset();
    if (session) {
        session->deleteAutosaveFile();
    }
    session = std::move(s);
    canvas = std::make_unique<CanvasView>(*session);
    connect(session.get(), &DocumentSession::modifiedChanged, this, &AppController::modifiedChanged);
    connect(session.get(), &DocumentSession::undoRedoStateChanged, this, &AppController::undoRedoChanged);
    connect(session.get(), &DocumentSession::filePathChanged, this, &AppController::titleChanged);
    connect(session.get(), &DocumentSession::currentPageChanged, this, &AppController::pageChanged);
    connect(canvas.get(), &CanvasView::pagesChanged, this, &AppController::pageChanged);
    connect(&canvas->getViewController(), &ViewController::zoomChanged, this, &AppController::zoomChanged);
    Q_EMIT documentChanged();
    Q_EMIT titleChanged();
    Q_EMIT modifiedChanged();
    Q_EMIT undoRedoChanged();
    Q_EMIT zoomChanged();
    Q_EMIT pageChanged();
}

QObject* AppController::view() const { return canvas.get(); }

QString AppController::title() const { return session ? QString::fromStdString(session->getDisplayName()) : QString(); }
bool AppController::modified() const { return session && session->isModified(); }
bool AppController::hasFilePath() const { return session && session->hasFilePath(); }
bool AppController::canUndo() const { return session && session->getUndoRedoHandler()->canUndo(); }
bool AppController::canRedo() const { return session && session->getUndoRedoHandler()->canRedo(); }

QString AppController::tool() const {
    switch (app->getToolHandler()->getToolType()) {
        case TOOL_PEN:
            return "pen";
        case TOOL_HIGHLIGHTER:
            return "highlighter";
        case TOOL_ERASER:
            return "eraser";
        case TOOL_HAND:
            return "hand";
        default:
            return "other";
    }
}

QColor AppController::color() const { return toQColor(app->getToolHandler()->getColor()); }
int AppController::size() const { return static_cast<int>(app->getToolHandler()->getSize()); }

QVariantList AppController::palette() const {
    QVariantList list;
    for (size_t i = 0; i < colors->size(); ++i) {
        list.append(toQColor(colors->getColorAt(i).getColor()));
    }
    return list;
}

int AppController::zoomPercent() const {
    if (!canvas) {
        return 100;
    }
    const auto& vc = canvas->getViewController();
    return static_cast<int>(std::lround(vc.zoom() / vc.zoom100() * 100.0));
}

int AppController::pageNumber() const { return session ? static_cast<int>(session->getCurrentPageNo()) + 1 : 0; }
int AppController::pageCount() const { return canvas ? static_cast<int>(canvas->pageCount()) : 0; }

void AppController::newDocument() { setSession(std::make_unique<DocumentSession>(*app)); }

bool AppController::openFile(const QUrl& url) { return openPath(url.toLocalFile()); }

bool AppController::openPath(const QString& path) {
    auto result = DocumentSession::loadFile(fs::path(path.toStdString()));
    if (!result.document) {
        Q_EMIT message(tr("Cannot open file"), QString::fromStdString(result.error), true);
        return false;
    }
    setSession(std::make_unique<DocumentSession>(*app, std::move(result.document)));
    if (!result.missingPdf.empty() || result.attachedPdfMissing) {
        Q_EMIT message(tr("PDF background missing"),
                       tr("The background PDF \"%1\" could not be found. The annotations are shown without it.")
                               .arg(QString::fromStdString(result.missingPdf.string())),
                       false);
    }
    if (!result.warnings.empty()) {
        QStringList w;
        for (const auto& s: result.warnings) {
            w << QString::fromStdString(s);
        }
        Q_EMIT message(tr("Problems while loading"),
                       tr("Some content might be lost. Do not overwrite the original file unless you are sure.\n\n") +
                               w.join('\n'),
                       false);
    }
    return true;
}

bool AppController::save() {
    if (!session || !session->hasFilePath()) {
        return false;
    }
    auto r = session->save();
    if (!r.ok) {
        Q_EMIT message(tr("Saving failed"), QString::fromStdString(r.error), true);
    }
    Q_EMIT titleChanged();
    return r.ok;
}

bool AppController::saveAs(const QUrl& url) {
    if (!session) {
        return false;
    }
    auto r = session->saveAs(fs::path(url.toLocalFile().toStdString()));
    if (!r.ok) {
        Q_EMIT message(tr("Saving failed"), QString::fromStdString(r.error), true);
    }
    Q_EMIT titleChanged();
    return r.ok;
}

void AppController::undo() {
    if (canUndo()) {
        session->clearSelectionEndText();
        session->getUndoRedoHandler()->undo();
    }
}

void AppController::redo() {
    if (canRedo()) {
        session->clearSelectionEndText();
        session->getUndoRedoHandler()->redo();
    }
}

void AppController::selectTool(const QString& name) {
    ToolType type = TOOL_PEN;
    if (name == "highlighter") {
        type = TOOL_HIGHLIGHTER;
    } else if (name == "eraser") {
        type = TOOL_ERASER;
    } else if (name == "hand") {
        type = TOOL_HAND;
    }
    ToolHandler* th = app->getToolHandler();
    th->selectTool(type);
    th->fireToolChanged();
}

void AppController::setColor(const QColor& c) {
    app->getToolHandler()->setColor(toColor(c), true);
    Q_EMIT toolChanged();
}

void AppController::setSize(int s) {
    app->getToolHandler()->setSize(static_cast<ToolSize>(std::clamp(s, 0, 4)));
    Q_EMIT toolChanged();
}

void AppController::fitWidth() {
    if (canvas) {
        canvas->getViewController().fitWidth();
    }
}

void AppController::zoomIn() {
    if (canvas) {
        auto& vc = canvas->getViewController();
        vc.zoomBy(1.2, QPointF(vc.viewSize().width() / 2, vc.viewSize().height() / 2));
    }
}

void AppController::zoomOut() {
    if (canvas) {
        auto& vc = canvas->getViewController();
        vc.zoomBy(1 / 1.2, QPointF(vc.viewSize().width() / 2, vc.viewSize().height() / 2));
    }
}

void AppController::addPageAfterCurrent() {
    if (session) {
        session->insertNewPage(session->getCurrentPageNo() + 1);
    }
}

QUrl AppController::iconUrl(const QString& name) const {
    return QUrl::fromLocalFile(QString::fromStdString((app->getResourceDir() / "icons" / name.toStdString()).string()) +
                               ".svg");
}

QUrl AppController::currentFolder() const {
    if (session && session->hasFilePath()) {
        return QUrl::fromLocalFile(QString::fromStdString(session->getFilePath().parent_path().string()));
    }
    return {};
}
