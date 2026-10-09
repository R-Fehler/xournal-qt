/*
 * xournal-qt-cli plugin …: plugin commands without a window (qt/docs/features/plugins.md, "From the command line").
 *
 *   xournal-qt-cli plugin list
 *   xournal-qt-cli plugin run <plugin-id> <command> [FILE…] [--set key=value]… [--allow class]… [--page N]
 *                  [--frame x,y,width,height] (-o OUT | --out-dir DIR | --in-place)
 *
 * Each FILE (a .xopp or .xoj; none: a new document of one page) is opened, the command runs on it as in the app (one
 * transaction: on a failure nothing of it is saved), and the result is written. No window: permissions are only those
 * given by --allow (edit, pages, tools, files; never asked), a dialog's fields take their values from --set (others
 * keep the plugin's), and a live dialog (the function plotter's) is answered at once: its preview computed with the
 * values and its frame (--frame, else the plugin's size in the middle of the page), then Insert. The window's
 * operations (the selection, the tool, files) are not there.
 *
 * @license GNU GPLv2 or later
 */
#include <iostream>
#include <memory>
#include <optional>
#include <shared_mutex>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "model/Document.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

#include "MdBox.h"
#include "Operations.h"
#include "PluginHost.h"

namespace {

using namespace xqt;

/// A --set value: a number, true/false, or text
QVariant valueOf(const QString& text) {
    bool ok = false;
    const double d = QString(text).replace(',', '.').toDouble(&ok);
    if (ok && !text.trimmed().isEmpty() && !text.contains(QRegularExpression("[^0-9.,eE+-]"))) {
        return d;
    }
    if (text == QLatin1String("true") || text == QLatin1String("false")) {
        return text == QLatin1String("true");
    }
    return text;
}

class CliUi final: public plugins::PluginUi {
public:
    QStringList allowed;
    QVariantMap set;
    std::optional<size_t> page;
    std::optional<QRectF> frameRect;
    DocumentSession* session = nullptr;
    bool failed = false;

    bool askPermission(const plugins::PluginInfo& plugin, const QString& opClass, const QString& what) override {
        const bool yes = allowed.contains(opClass);
        if (!yes) {
            std::cerr << plugin.manifest.name.toStdString() << " wants to " << what.toStdString()
                      << ": not allowed (--allow " << opClass.toStdString() << ")\n";
        }
        return yes;
    }
    QVariantMap withSet(QVariantMap values) const {
        for (auto it = set.begin(); it != set.end(); ++it) {
            values.insert(it.key(), it.value());
        }
        return values;
    }
    std::optional<QVariantMap> dialog(const plugins::PluginInfo&, const QVariantMap& spec) override {
        QVariantMap values = spec.value("values").toMap();
        for (const QVariant& f: spec.value("fields").toList()) {
            const QVariantMap field = f.toMap();
            if (field.contains("id") && field.contains("value") && !values.contains(field.value("id").toString())) {
                values.insert(field.value("id").toString(), field.value("value"));
            }
        }
        return withSet(values);
    }
    /// A live dialog opened by the command: answered once the command has returned (its insert is a transaction of
    /// its own)
    std::shared_ptr<plugins::LiveDialog> pending;
    bool openLive(const std::shared_ptr<plugins::LiveDialog>& d) override {
        pending = d;
        return true;
    }
    void answerLive() {
        const std::shared_ptr<plugins::LiveDialog> d = std::move(pending);
        pending.reset();
        if (!d) {
            return;
        }
        // The frame: given, or the plugin's size in the middle of the page
        const QVariantMap wanted = d->spec().value("frame").toMap();
        const size_t p = page.value_or(0);
        double pw = 595, ph = 842;
        {
            std::shared_lock lock(*session->getDocument());
            if (p < session->getDocument()->getPageCount()) {
                pw = session->getDocument()->getPage(p)->getWidth();
                ph = session->getDocument()->getPage(p)->getHeight();
            }
        }
        QRectF r = frameRect.value_or(QRectF());
        if (!frameRect) {
            const double w = wanted.value("width", 240).toDouble(), h = wanted.value("height", 180).toDouble();
            r = QRectF(wanted.value("x", (pw - w) / 2).toDouble(), wanted.value("y", (ph - h) / 2).toDouble(), w, h);
        }
        QVariantMap frame{{"page", static_cast<int>(p)}, {"x", r.x()}, {"y", r.y()}, {"width", r.width()},
                          {"height", r.height()}};
        const QVariantMap values = withSet(d->spec().value("values").toMap());
        // The preview once (it may ask for another frame size: the plotter's exact scale)
        const QVariantMap preview = d->change(values, frame, QString());
        if (preview.contains("error")) {
            std::cerr << preview.value("error").toString().toStdString() << "\n";
            failed = true;
            return;
        }
        const QVariantMap errors = preview.value("errors").toMap();
        for (auto it = errors.begin(); it != errors.end(); ++it) {
            std::cerr << it.key().toStdString() << ": " << it.value().toString().toStdString() << "\n";
        }
        QVariantMap merged = values;
        const QVariantMap more = preview.value("values").toMap();
        for (auto it = more.begin(); it != more.end(); ++it) {
            if (!set.contains(it.key())) {
                merged.insert(it.key(), it.value());
            }
        }
        const QVariantMap sized = preview.value("frame").toMap();
        for (const char* k: {"width", "height"}) {
            if (sized.contains(k)) {
                frame.insert(k, sized.value(k));
            }
        }
        QString error;
        if (!d->insert(merged, frame, &error)) {
            std::cerr << error.toStdString() << "\n";
            failed = true;
        }
    }
    void notify(const QString& text, bool undoable) override {
        if (!undoable) {
            std::cerr << text.toStdString() << "\n";
        }
    }
};

int usage() {
    std::cerr << "usage: xournal-qt-cli plugin list\n"
                 "       xournal-qt-cli plugin run <plugin-id> <command> [FILE...] [--set key=value]...\n"
                 "              [--allow edit|pages|tools|files]... [--page N] [--frame x,y,width,height]\n"
                 "              (-o OUT.xopp | --out-dir DIR | --in-place)\n"
                 "  Runs a plugin's command on each FILE (.xopp, .xoj; none: a new document) and saves the result.\n"
                 "  Permissions only by --allow; dialog fields by --set (a live dialog is inserted at once).\n";
    return 1;
}

}  // namespace

int pluginCommand(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("xournal-qt");  // (the user's plugins: the app's data folder)
    const QStringList args = QCoreApplication::arguments().mid(2);
    if (args.isEmpty()) {
        return usage();
    }
    // Settings of its own (never the user's): defaults, thrown away
    QTemporaryDir settingsDir;
    xqt::AppContext context(xqt::AppContext::defaultResourceDir(),
                            fs::path(settingsDir.filePath("settings.xml").toStdString()), 1);
    xqt::md::installRenderer();
    QStringList folders{QString::fromStdU16String((context.getResourceDir() / "plugins").u16string()),
                        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("plugins")};
    plugins::PluginHost host(folders, [] { return QString(); }, [](const QString&) {});

    if (args[0] == QLatin1String("list")) {
        for (const plugins::PluginInfo& p: host.plugins()) {
            std::cout << p.manifest.id.toStdString() << "  " << p.manifest.name.toStdString()
                      << (p.error.isEmpty() ? "" : "  (" + p.error.toStdString() + ")") << "\n";
            for (const plugins::PluginCommand& c: p.manifest.commands) {
                std::cout << "    " << c.id.toStdString() << "  " << c.title.toStdString() << "\n";
            }
            if (!p.manifest.permissions.isEmpty()) {
                std::cout << "    permissions: " << p.manifest.permissions.join(", ").toStdString() << "\n";
            }
        }
        return 0;
    }
    if (args[0] != QLatin1String("run") || args.size() < 3) {
        return usage();
    }
    const QString id = args[1];
    const QString command = args[2];
    CliUi ui;
    QStringList files;
    QString out, outDir;
    bool inPlace = false;
    for (int i = 3; i < args.size(); ++i) {
        const QString a = args[i];
        auto next = [&]() -> QString { return i + 1 < args.size() ? args[++i] : QString(); };
        if (a == QLatin1String("--set")) {
            const QString kv = next();
            const int eq = kv.indexOf('=');
            if (eq <= 0) {
                std::cerr << "--set takes key=value\n";
                return usage();
            }
            ui.set.insert(kv.left(eq), valueOf(kv.mid(eq + 1)));
        } else if (a == QLatin1String("--allow")) {
            ui.allowed << next().split(',', Qt::SkipEmptyParts);
        } else if (a == QLatin1String("--page")) {
            ui.page = static_cast<size_t>(std::max(1, next().toInt()) - 1);
        } else if (a == QLatin1String("--frame")) {
            const QStringList v = next().split(',');
            if (v.size() != 4) {
                std::cerr << "--frame takes x,y,width,height (points)\n";
                return usage();
            }
            ui.frameRect = QRectF(v[0].toDouble(), v[1].toDouble(), v[2].toDouble(), v[3].toDouble());
        } else if (a == QLatin1String("-o") || a == QLatin1String("--out")) {
            out = next();
        } else if (a == QLatin1String("--out-dir")) {
            outDir = next();
        } else if (a == QLatin1String("--in-place")) {
            inPlace = true;
        } else if (a.startsWith('-')) {
            std::cerr << "unknown argument: " << a.toStdString() << "\n";
            return usage();
        } else {
            files << a;
        }
    }
    const plugins::PluginInfo* info = host.plugin(id);
    if (!info) {
        std::cerr << "no plugin \"" << id.toStdString() << "\" (xournal-qt-cli plugin list)\n";
        return 1;
    }
    if (!info->enabled) {
        host.setEnabled(id, true);  // (asked for by name: on for this run)
    }
    const int outputs = !out.isEmpty() + !outDir.isEmpty() + inPlace;
    if (outputs != 1 || (!out.isEmpty() && files.size() > 1) || (inPlace && files.isEmpty())) {
        std::cerr << "say where the result goes: -o OUT (one document), --out-dir DIR, or --in-place\n";
        return usage();
    }
    if (files.isEmpty()) {
        files << QString();  // (a new document)
    }
    int failures = 0;
    for (const QString& file: files) {
        std::unique_ptr<DocumentSession> session;
        if (file.isEmpty()) {
            session = std::make_unique<DocumentSession>(context);
        } else {
            auto loaded = DocumentSession::loadFile(fs::path(file.toStdString()));
            if (!loaded.document) {
                std::cerr << file.toStdString() << ": cannot be opened\n";
                ++failures;
                continue;
            }
            session = std::make_unique<DocumentSession>(context, std::move(loaded.document));
        }
        if (ui.page) {
            session->setCurrentPageNo(*ui.page);
        }
        ops::Operations operations;  // (the document's only: no window)
        ui.session = session.get();
        ui.failed = false;
        const auto r = host.run(id, command, {&operations, session.get(), nullptr, &ui});
        if (r.ok) {
            ui.answerLive();
        }
        if (!r.ok || ui.failed) {
            if (!r.ok) {
                std::cerr << (file.isEmpty() ? std::string("(new document)") : file.toStdString()) << ": "
                          << r.error.toStdString() << "\n";
            }
            ++failures;
            continue;
        }
        const QString target = !out.isEmpty() ? out
                               : inPlace      ? file
                                              : QDir(outDir).filePath(file.isEmpty() ? QStringLiteral("new.xopp")
                                                                                     : QFileInfo(file).completeBaseName() +
                                                                                               ".xopp");
        if (!outDir.isEmpty()) {
            QDir().mkpath(outDir);
        }
        if (!DocumentSession::writeDocument(*session->getDocument(), fs::path(target.toStdString())).ok) {
            std::cerr << target.toStdString() << ": cannot be written\n";
            ++failures;
            continue;
        }
        std::cout << target.toStdString() << "\n";
    }
    return failures == 0 ? 0 : -3;
}
