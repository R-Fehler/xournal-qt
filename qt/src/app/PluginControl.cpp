#include "PluginControl.h"

#include <algorithm>
#include <cmath>
#include <shared_mutex>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include "control/tools/EditSelection.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"

#include "AppController.h"
#include "AppServices.h"
#include "CanvasView.h"
#include "CurrentDocument.h"
#include "DocumentOps.h"
#include "Shapes.h"

namespace xqt {

namespace {
constexpr qint64 MAX_FILE = 16 * 1024 * 1024;  ///< what a plugin may read or write as text at once

QString keyOf(const QString& plugin, const QString& command) {
    return QStringLiteral("plugin:%1/%2").arg(plugin, command);
}
bool splitKey(const QString& key, QString& plugin, QString& command) {
    if (!key.startsWith(QLatin1String("plugin:"))) {
        return false;
    }
    const QString rest = key.mid(7);
    const int slash = rest.lastIndexOf('/');
    if (slash <= 0) {
        return false;
    }
    plugin = rest.left(slash);
    command = rest.mid(slash + 1);
    return true;
}
const char* typeName(const Element& e) {
    switch (e.getType()) {
        case ELEMENT_STROKE:
            return "stroke";
        case ELEMENT_TEXT:
            return "text";
        case ELEMENT_IMAGE:
            return "image";
        case ELEMENT_TEXIMAGE:
            return "teximage";
        case ELEMENT_LINK:
            return "link";
    }
    return "element";
}
}  // namespace

PluginControl::PluginControl(const WindowContext& window, AppController& controller, QObject* parent):
        QObject(parent), window(window), app(controller) {
    addWindowOperations();
    connect(&host(), &plugins::PluginHost::pluginsChanged, this, &PluginControl::changed);
    connect(&host(), &plugins::PluginHost::logChanged, this, [this] {
        ++logRev;
        Q_EMIT logChanged();
    });
    connect(&host(), &plugins::PluginHost::liveDialogsClosed, this, [this](const QString& id) {
        if (live && live->pluginId() == id) {
            closeLive();
        }
    });
}

PluginControl::~PluginControl() {
    if (loop) {
        loop->quit();
    }
    closeLive();
}

plugins::PluginHost& PluginControl::host() const { return window.services.plugins(); }

plugins::Environment PluginControl::environment() {
    return {&operations, app.homeVisible() ? nullptr : window.session(), this, this};
}

bool PluginControl::busy() const { return host().busy(); }

QString PluginControl::userFolder() const { return host().folders().value(1); }

QVariantList PluginControl::commands() const {
    QVariantList out;
    for (const plugins::PluginInfo& p: host().plugins()) {
        if (!p.enabled || !p.error.isEmpty()) {
            continue;
        }
        for (const plugins::PluginCommand& c: p.manifest.commands) {
            const QString key = keyOf(p.manifest.id, c.id);
            out << QVariantMap{{"key", key},
                               {"plugin", p.manifest.id},
                               {"pluginName", p.manifest.name},
                               {"command", c.id},
                               {"title", c.title},
                               {"icon", iconOf(key)},
                               {"iconName", c.icon.startsWith(QLatin1String("xqt-")) || c.icon.startsWith(QLatin1String("xopp-"))
                                                    ? c.icon
                                                    : QStringLiteral("xqt-plugin")},
                               {"menu", c.menu},
                               {"toolbox", c.toolbox},
                               {"place", c.place},
                               {"when", c.when}};
        }
    }
    return out;
}

QVariantList PluginControl::pluginList() const {
    QVariantList out;
    for (const plugins::PluginInfo& p: host().plugins()) {
        QVariantList permissions;
        for (const QString& c: p.manifest.permissions) {
            permissions << QVariantMap{
                    {"class", c}, {"text", ops::describeClass(c)}, {"answer", host().grant(p.manifest.id, c)}};
        }
        QStringList titles;
        for (const plugins::PluginCommand& c: p.manifest.commands) {
            titles << c.title;
        }
        out << QVariantMap{{"id", p.manifest.id},
                           {"name", p.manifest.name},
                           {"version", p.manifest.version},
                           {"author", p.manifest.author},
                           {"description", p.manifest.description},
                           {"enabled", p.enabled},
                           {"bundled", p.bundled},
                           {"error", p.error},
                           {"folder", QDir::toNativeSeparators(p.folder)},
                           {"permissions", permissions},
                           {"commands", titles}};
    }
    return out;
}

QUrl PluginControl::iconOf(const QString& key) const {
    QString plugin, command;
    const plugins::PluginInfo* p = splitKey(key, plugin, command) ? host().plugin(plugin) : nullptr;
    const plugins::PluginCommand* c = p ? p->manifest.command(command) : nullptr;
    if (!c || c->icon.isEmpty()) {
        return app.iconUrl(QStringLiteral("xqt-plugin"));
    }
    if (c->icon.startsWith(QLatin1String("xqt-")) || c->icon.startsWith(QLatin1String("xopp-"))) {
        return app.iconUrl(c->icon);
    }
    const QString file = QDir(p->folder).filePath(c->icon);
    return QFileInfo::exists(file) ? QUrl::fromLocalFile(file) : app.iconUrl(QStringLiteral("xqt-plugin"));
}

bool PluginControl::run(const QString& key) {
    QString plugin, command;
    if (!splitKey(key, plugin, command) || host().busy()) {
        return false;
    }
    Q_EMIT busyChanged();
    const auto result = host().run(plugin, command, environment());
    Q_EMIT busyChanged();
    return result.ok;
}

void PluginControl::setEnabled(const QString& id, bool on) { host().setEnabled(id, on); }

void PluginControl::setGrant(const QString& id, const QString& opClass, const QString& answer) {
    host().setGrant(id, opClass, answer);
}

QString PluginControl::logText(const QString& id) const {
    QStringList lines;
    for (const plugins::LogLine& l: host().log(id)) {
        lines << QStringLiteral("%1 %2%3")
                         .arg(l.time.toString(QStringLiteral("HH:mm:ss")),
                              l.level == 2 ? QStringLiteral("error: ") : l.level == 1 ? QStringLiteral("warning: ") : "",
                              l.text);
    }
    return lines.join('\n');
}

void PluginControl::clearLog(const QString& id) { host().clearLog(id); }

void PluginControl::reload() { host().reload(); }

// --- questions and dialogs ------------------------------------------------------------------------------------------

void PluginControl::wait(bool& answered) {
    answered = false;
    QEventLoop l;
    QEventLoop* outer = loop;
    loop = &l;
    QPointer<PluginControl> self(this);
    while (!answered) {
        l.exec();
        if (!self) {
            return;
        }
        if (loop != &l) {
            break;
        }
    }
    loop = outer;
}

bool PluginControl::askPermission(const plugins::PluginInfo& plugin, const QString& opClass, const QString& what) {
    asking = {{"plugin", plugin.manifest.name}, {"class", opClass}, {"what", what}};
    Q_EMIT questionChanged();
    QPointer<PluginControl> self(this);
    wait(askAnswered);
    if (!self) {
        return false;
    }
    asking.clear();
    Q_EMIT questionChanged();
    return askAllowed;
}

void PluginControl::answerQuestion(bool allow) {
    askAllowed = allow;
    askAnswered = true;
    if (loop) {
        loop->quit();
    }
}

std::optional<QVariantMap> PluginControl::dialog(const plugins::PluginInfo& plugin, const QVariantMap& spec) {
    dialogShown = spec;
    dialogShown.insert("plugin", plugin.manifest.name);
    dialogValues.reset();
    Q_EMIT dialogChanged();
    QPointer<PluginControl> self(this);
    wait(dialogAnswered);
    if (!self) {
        return std::nullopt;
    }
    dialogShown.clear();
    Q_EMIT dialogChanged();
    return dialogValues;
}

void PluginControl::answerDialog(const QVariant& values) {
    if (values.isValid() && !values.isNull()) {
        dialogValues = values.toMap();
    } else {
        dialogValues.reset();
    }
    dialogAnswered = true;
    if (loop) {
        loop->quit();
    }
}

void PluginControl::answerFile(const QUrl& file) {
    fileChosen = file;
    fileAnswered = true;
    if (loop) {
        loop->quit();
    }
}

void PluginControl::notify(const QString& text, bool undoable) { Q_EMIT note(text, undoable); }

QVariantList PluginControl::palette() const {
    const QString chosen = app.colorPalette();
    for (const QVariant& v: app.colorPalettes()) {
        const QVariantMap p = v.toMap();
        if (p.value("id").toString() != chosen) {
            continue;
        }
        QVariantList out;
        for (const QVariant& r: p.value("roles").toList()) {
            const QVariantMap role = r.toMap();
            out << QVariantMap{{"key", role.value("key")},
                               {"name", role.value("name")},
                               {"color", role.value("ink").value<QColor>().name()}};
        }
        return out;
    }
    return PluginUi::palette();
}

// --- the live dialog --------------------------------------------------------------------------------------------------

QVariantMap PluginControl::liveSpec() const {
    if (!live) {
        return {};
    }
    QVariantMap s = live->spec();
    QString plugin = live->pluginId();
    if (const plugins::PluginInfo* p = host().plugin(plugin)) {
        s.insert("plugin", p->manifest.name);
    }
    return s;
}

QVariantMap PluginControl::defaultFrame(const QVariantMap& wanted) const {
    DocumentSession* s = window.session();
    CanvasView* v = window.view();
    QVariantMap f;
    if (!s) {
        return f;
    }
    Document* doc = s->getDocument();
    size_t page = s->getCurrentPageNo();
    double pw = 0, ph = 0;
    {
        std::shared_lock lock(*doc);
        if (wanted.contains("page")) {
            page = static_cast<size_t>(std::max(0, wanted.value("page").toInt()));
        }
        page = std::min(page, doc->getPageCount() - 1);
        pw = doc->getPage(page)->getWidth();
        ph = doc->getPage(page)->getHeight();
    }
    const double w = std::clamp(wanted.value("width", 240).toDouble(), 20.0, pw);
    const double h = std::clamp(wanted.value("height", 180).toDouble(), 20.0, ph);
    // The middle of what of the page is in view (else the page's middle)
    QPointF middle(pw / 2, ph / 2);
    double seenLeft = 0, seenRight = pw;
    if (v && page < v->pageCount()) {
        const double zoom = v->getViewController().zoom();
        const QRectF r = v->pageViewRect(page);
        const QSizeF view = v->getViewController().viewSize();
        // (what the live dialog leaves free of the canvas, when that is still a good part of it)
        QSizeF free(view.width() - reservedRight, view.height() - reservedBottom);
        if (free.width() < view.width() * 0.4 || free.height() < view.height() * 0.3) {
            free = view;
        }
        const QRectF seen = r.intersected(QRectF(QPointF(0, 0), free));
        if (!seen.isEmpty() && zoom > 0) {
            middle = (seen.center() - r.topLeft()) / zoom;
            seenLeft = (seen.left() - r.left()) / zoom;
            seenRight = (seen.right() - r.left()) / zoom;
        }
    }
    // (in the middle of what is free; where that is narrower than the frame, at its right end, clear of the dialog)
    double x = middle.x() - w / 2;
    x = std::max(seenLeft + 8, std::min(x, seenRight - w - 8));
    x = wanted.contains("x") ? wanted.value("x").toDouble() : x;
    const double y = wanted.contains("y") ? wanted.value("y").toDouble() : middle.y() - h / 2;
    f.insert("page", static_cast<int>(page));
    f.insert("x", std::clamp(x, 0.0, std::max(0.0, pw - w)));
    f.insert("y", std::clamp(y, 0.0, std::max(0.0, ph - h)));
    f.insert("width", w);
    f.insert("height", h);
    f.insert("resizable", wanted.value("resizable", true).toBool());
    return f;
}

bool PluginControl::openLive(const std::shared_ptr<plugins::LiveDialog>& dialog) {
    if (!window.session()) {
        return false;
    }
    closeLive();
    live = dialog;
    frame = defaultFrame(dialog->spec().value("frame").toMap());
    lastValues = dialog->spec().value("values").toMap();
    state.clear();
    Q_EMIT liveChanged();
    Q_EMIT liveFrameChanged();
    Q_EMIT liveStateChanged();
    // (the first preview once the command that opened it has returned)
    QTimer::singleShot(0, this, [this, d = std::weak_ptr<plugins::LiveDialog>(dialog)] {
        if (live && live == d.lock()) {
            liveEdited(lastValues);
        }
    });
    return true;
}

void PluginControl::liveEdited(const QVariantMap& edited, const QString& action) {
    if (!live) {
        return;
    }
    const QVariantMap values = edited;  // (it may be lastValues itself)
    for (auto it = values.begin(); it != values.end(); ++it) {
        lastValues.insert(it.key(), it.value());
    }
    applyLiveResult(live->change(lastValues, frame, action));
}

void PluginControl::moveLiveFrame(double x, double y, double width, double height) {
    if (!live || frame.isEmpty()) {
        return;
    }
    DocumentSession* s = window.session();
    double pw = 1e6, ph = 1e6;
    if (s) {
        std::shared_lock lock(*s->getDocument());
        const auto index = static_cast<size_t>(frame.value("page").toInt());
        if (index < s->getDocument()->getPageCount()) {
            pw = s->getDocument()->getPage(index)->getWidth();
            ph = s->getDocument()->getPage(index)->getHeight();
        }
    }
    const double w = std::clamp(width, 20.0, pw);
    const double h = std::clamp(height, 20.0, ph);
    frame.insert("x", std::clamp(x, 0.0, std::max(0.0, pw - w)));
    frame.insert("y", std::clamp(y, 0.0, std::max(0.0, ph - h)));
    frame.insert("width", w);
    frame.insert("height", h);
    Q_EMIT liveFrameChanged();
    applyLiveResult(live->change(lastValues, frame, QStringLiteral("frame")));
}

void PluginControl::applyLiveResult(const QVariantMap& result) {
    QVariantMap next;
    if (result.contains("error")) {
        next.insert("error", result.value("error"));
        next.insert("errors", state.value("errors"));
        next.insert("fields", state.value("fields"));
        state = next;
        Q_EMIT liveStateChanged();
        return;
    }
    if (result.contains("values")) {
        const QVariantMap v = result.value("values").toMap();
        for (auto it = v.begin(); it != v.end(); ++it) {
            lastValues.insert(it.key(), it.value());
        }
        next.insert("values", lastValues);
    }
    if (result.contains("frame")) {
        const QVariantMap f = result.value("frame").toMap();
        for (const char* k: {"width", "height", "x", "y"}) {
            if (f.contains(k)) {
                frame.insert(k, f.value(k).toDouble());
            }
        }
        if (f.contains("resizable")) {
            frame.insert("resizable", f.value("resizable").toBool());
        }
        Q_EMIT liveFrameChanged();
    }
    next.insert("errors", result.value("errors").toMap());
    next.insert("message", result.value("message").toString());
    next.insert("fields", result.contains("fields") ? result.value("fields") : state.value("fields"));
    state = next;
    showPreview(result.value("shapes").toList());
    Q_EMIT liveStateChanged();
}

void PluginControl::showPreview(const QVariantList& shapes) {
    CanvasView* v = window.view();
    if (!v) {
        return;
    }
    std::vector<ElementPtr> elements;
    try {
        for (const QVariant& shape: shapes) {
            elements.push_back(std::move(ops::makeElement(shape.toMap()).element));
        }
    } catch (const ops::Error& e) {
        state.insert("error", e.text);
        v->clearPluginPreview();
        return;
    }
    v->setPluginPreview(static_cast<size_t>(frame.value("page").toInt()), std::move(elements));
}

void PluginControl::clearPreview() {
    if (CanvasView* v = window.view()) {
        v->clearPluginPreview();
    }
}

bool PluginControl::liveInsert(const QVariantMap& edited) {
    if (!live) {
        return false;
    }
    const QVariantMap values = edited;
    for (auto it = values.begin(); it != values.end(); ++it) {
        lastValues.insert(it.key(), it.value());
    }
    QString error;
    const auto dialog = live;
    if (dialog->insert(lastValues, frame, &error)) {
        closeLive();
        return true;
    }
    if (live) {
        state.insert("error", error);
        Q_EMIT liveStateChanged();
    }
    return false;
}

void PluginControl::liveCancel() { closeLive(); }

void PluginControl::setReserved(double right, double bottom) {
    reservedRight = std::max(0.0, right);
    reservedBottom = std::max(0.0, bottom);
}

void PluginControl::closeLive() {
    if (!live) {
        return;
    }
    live->close();
    live.reset();
    clearPreview();
    frame.clear();
    state.clear();
    lastValues.clear();
    Q_EMIT liveChanged();
    Q_EMIT liveFrameChanged();
    Q_EMIT liveStateChanged();
}

void PluginControl::currentChanged() {
    if (live && live->document() != window.session()) {
        closeLive();
    }
}

// --- the window's operations --------------------------------------------------------------------------------------

void PluginControl::addWindowOperations() {
    using ops::Context;
    using ops::Error;
    operations.add("selection.read", "read", false, [this](Context& c, const QVariantMap&) -> QVariant {
        CanvasView* v = window.view();
        EditSelection* sel = v ? v->getSelection() : nullptr;
        if (!sel || !c.document) {
            return {};
        }
        Document* doc = c.document->getDocument();
        std::shared_lock lock(*doc);
        QVariantList list;
        for (const Element* e: sel->getElementsView()) {
            const auto& b = e->getBoundingBox();
            QVariantMap m{{"type", QString::fromLatin1(typeName(*e))},
                          {"group", static_cast<int>(e->getGroup())},
                          {"x", b.x},
                          {"y", b.y},
                          {"width", b.width},
                          {"height", b.height},
                          {"color", ops::colorName(e->getColor())}};
            if (const QVariant d = ops::dataOf(*e, c.principal); d.isValid()) {
                m.insert("data", d);
            }
            list << m;
        }
        return QVariantMap{{"page", static_cast<int>(doc->indexOf(sel->getSourcePage()))}, {"elements", list}};
    });
    operations.add("selection.clear", "read", false, [this](Context&, const QVariantMap&) -> QVariant {
        if (CanvasView* v = window.view()) {
            v->clearSelection();
        }
        return true;
    });
    operations.add("tool.read", "read", false, [this](Context&, const QVariantMap&) -> QVariant {
        return QVariantMap{{"type", app.tool()}, {"color", app.color().name()}, {"width", app.customWidth()}};
    });
    operations.add("tool.select", "tools", false, [this](Context&, const QVariantMap& a) -> QVariant {
        static const QStringList known{"pen", "highlighter", "eraser", "text", "hand", "selectRect", "selectRegion",
                                       "selectObject"};
        const QString type = ops::string(a, "type");
        if (!known.contains(type)) {
            throw Error(Error::Kind::Invalid, QStringLiteral("no tool \"%1\" (%2)").arg(type, known.join(", ")));
        }
        app.selectTool(type);
        return true;
    });
    operations.add("tool.color", "tools", false, [this](Context&, const QVariantMap& a) -> QVariant {
        const auto c = ops::parseColor(ops::string(a, "color"));
        if (!c) {
            throw Error(Error::Kind::Invalid, QStringLiteral("\"color\" is no color (#rrggbb)"));
        }
        app.setColor(QColor(c->red, c->green, c->blue));
        return true;
    });
    operations.add("tool.width", "tools", false, [this](Context&, const QVariantMap& a) -> QVariant {
        const double w = ops::number(a, "width");
        if (w < 0.1 || w > 150) {
            throw Error(Error::Kind::Invalid, QStringLiteral("\"width\" must be 0.1 to 150 points"));
        }
        app.setCustomWidth(w);
        return true;
    });
    operations.add("view.read", "read", false, [this](Context& c, const QVariantMap&) -> QVariant {
        QVariantMap out{{"zoom", app.zoomPercent()}};
        if (!c.document) {
            return out;
        }
        out.insert("page", static_cast<int>(c.document->getCurrentPageNo()));
        CanvasView* v = window.view();
        const size_t page = c.document->getCurrentPageNo();
        if (v && page < v->pageCount()) {
            const double zoom = v->getViewController().zoom();
            const QRectF r = v->pageViewRect(page);
            const QRectF seen = r.intersected(QRectF(QPointF(0, 0), v->getViewController().viewSize()));
            if (!seen.isEmpty() && zoom > 0) {
                const QRectF onPage((seen.topLeft() - r.topLeft()) / zoom, seen.size() / zoom);
                out.insert("visible", QVariantMap{{"page", static_cast<int>(page)},
                                                  {"x", onPage.x()},
                                                  {"y", onPage.y()},
                                                  {"width", onPage.width()},
                                                  {"height", onPage.height()}});
            }
        }
        return out;
    });
    operations.add("view.goto", "view", false, [this](Context& c, const QVariantMap& a) -> QVariant {
        app.jumpToPage(static_cast<int>(ops::pageIndex(c, a)));
        return true;
    });
    operations.add("view.zoom", "view", false, [this](Context&, const QVariantMap& a) -> QVariant {
        app.setZoomPercent(std::clamp(ops::integer(a, "percent"), 10, 1000));
        return true;
    });
    // Files: only what the user chooses in a dialog, as handles
    auto choose = [this](bool save, const QVariantMap& a) -> QVariant {
        QStringList filters;
        for (const QVariant& f: a.value("filters").toList()) {
            filters << f.toString();
        }
        if (filters.isEmpty()) {
            filters << tr("All files (*)");
        }
        fileChosen = QUrl();
        host().pauseWatch();
        Q_EMIT fileRequested(save, a.value("title").toString(), a.value("name").toString(), filters);
        QPointer<PluginControl> self(this);
        wait(fileAnswered);
        if (!self) {
            return {};
        }
        host().resumeWatch();
        if (fileChosen.isEmpty()) {
            return {};
        }
        const QString id = QStringLiteral("f%1").arg(fileHandles.size() + 1);
        fileHandles[id] = fileChosen;
        return QVariantMap{{"id", id}, {"name", fileChosen.fileName()}};
    };
    operations.add("file.chooseOpen", "files", false,
                   [choose](Context&, const QVariantMap& a) -> QVariant { return choose(false, a); });
    operations.add("file.chooseSave", "files", false,
                   [choose](Context&, const QVariantMap& a) -> QVariant { return choose(true, a); });
    auto fileOf = [this](const QVariantMap& a) {
        const auto it = fileHandles.find(a.value("handle").toString());
        if (it == fileHandles.end()) {
            throw Error(Error::Kind::Invalid, QStringLiteral("not a file the user chose"));
        }
        return it->second;
    };
    operations.add("file.read", "files", false, [fileOf](Context&, const QVariantMap& a) -> QVariant {
        const QUrl url = fileOf(a);
        QFile f(url.isLocalFile() ? url.toLocalFile() : url.toString());  // (Android: content:// through Qt)
        if (!f.open(QIODevice::ReadOnly)) {
            throw Error(Error::Kind::Failed, QStringLiteral("%1 cannot be read").arg(url.fileName()));
        }
        if (f.size() > MAX_FILE) {
            throw Error(Error::Kind::Failed, QStringLiteral("%1 is too big").arg(url.fileName()));
        }
        return QString::fromUtf8(f.readAll());
    });
    operations.add("file.write", "files", false, [fileOf](Context&, const QVariantMap& a) -> QVariant {
        const QUrl url = fileOf(a);
        const QByteArray bytes = ops::string(a, "text").toUtf8();
        if (bytes.size() > MAX_FILE) {
            throw Error(Error::Kind::Failed, QStringLiteral("the text is too big"));
        }
        QFile f(url.isLocalFile() ? url.toLocalFile() : url.toString());
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(bytes) != bytes.size()) {
            throw Error(Error::Kind::Failed, QStringLiteral("%1 cannot be written").arg(url.fileName()));
        }
        return true;
    });
    operations.add("file.export", "files", false, [this, fileOf](Context&, const QVariantMap& a) -> QVariant {
        const QUrl url = fileOf(a);
        if (a.value("format", "pdf").toString() != QLatin1String("pdf")) {
            throw Error(Error::Kind::Invalid, QStringLiteral("only \"pdf\" can be exported"));
        }
        if (!app.exportPdf(url)) {
            throw Error(Error::Kind::Failed, QStringLiteral("the export to %1 failed").arg(url.fileName()));
        }
        return true;
    });
}

}  // namespace xqt
