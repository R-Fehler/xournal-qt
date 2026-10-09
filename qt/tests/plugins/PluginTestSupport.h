/*
 * xournal-qt: what the plugin tests share: plugins written into a temporary folder, a host on it, and a window
 * stand-in (PluginUi) that answers questions and dialogs as the test says and keeps the notes it was shown.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <optional>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

#include "Operations.h"
#include "PluginHost.h"
#include "config-test.h"

namespace xqt::test {

class TestUi final: public plugins::PluginUi {
public:
    bool allow = true;
    int asked = 0;
    QStringList askedClasses;
    std::optional<QVariantMap> dialogAnswer;
    QVariantMap lastDialog;
    std::shared_ptr<plugins::LiveDialog> live;
    QStringList notes;
    QStringList undoNotes;
    bool askPermission(const plugins::PluginInfo&, const QString& opClass, const QString&) override {
        ++asked;
        askedClasses << opClass;
        return allow;
    }
    std::optional<QVariantMap> dialog(const plugins::PluginInfo&, const QVariantMap& spec) override {
        lastDialog = spec;
        return dialogAnswer;
    }
    bool openLive(const std::shared_ptr<plugins::LiveDialog>& d) override {
        live = d;
        return true;
    }
    void notify(const QString& text, bool undoable) override { (undoable ? undoNotes : notes) << text; }
};

class PluginFixture: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        QDir(tmp.path()).mkpath("bundled");
        QDir(tmp.path()).mkpath("user");
    }
    /// A plugin folder in `root` ("bundled" or "user") with these files ({"plugin.json", …}: name → content)
    void writePlugin(const QString& root, const QString& folder, const std::map<QString, QString>& files) {
        const QString dir = tmp.filePath(root + "/" + folder);
        for (const auto& [name, content]: files) {
            const QString path = dir + "/" + name;
            QDir().mkpath(QFileInfo(path).path());
            QFile f(path);
            ASSERT_TRUE(f.open(QIODevice::WriteOnly));
            f.write(content.toUtf8());
        }
    }
    /// A manifest with one command "run" (and more command ids), the permissions given
    static QString manifest(const QString& id, const QString& permissions = R"(["edit"])",
                            const QString& more = "", bool enabled = true) {
        return QStringLiteral(R"({"id": "%1", "name": "Test %1", "version": "1.0", "api": "1.0", "main": "main.mjs",
            "enabled": %4, "permissions": %2,
            "commands": [{"id": "run", "title": "Run it"}%3]})")
                .arg(id, permissions, more, enabled ? "true" : "false");
    }
    plugins::PluginHost& host() {
        if (!hostObject) {
            hostObject = std::make_unique<plugins::PluginHost>(
                    QStringList{tmp.filePath("bundled"), tmp.filePath("user")}, [this] { return stored; },
                    [this](const QString& s) { stored = s; });
        }
        return *hostObject;
    }
    plugins::Environment env() { return {&ops, session.get(), nullptr, &ui}; }
    size_t elements() {
        size_t n = 0;
        for (const Layer* l: session->getDocument()->getPage(0)->getLayers()) {
            n += l->getElementsView().size();
        }
        return n;
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    ops::Operations ops;
    TestUi ui;
    QString stored;
    std::unique_ptr<plugins::PluginHost> hostObject;
};

}  // namespace xqt::test
