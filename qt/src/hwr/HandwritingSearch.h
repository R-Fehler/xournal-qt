/*
 * xournal-qt: the handwriting search of the app (qt/docs/handwriting-search.md): the setting, the recogniser and its
 * model, the worker, and an indexer per open document.
 *
 * Off until switched on in Settings ("handwritingSearch" in the xournalQt part of the settings). While on, every open
 * document gets an InkTextIndexer (the one in front first), all sharing one InkRecognitionService. Off again: the
 * indexers go and the open documents forget their recognised words (the library's results stay on disk).
 *
 * The model is a folder with the model's files and its manifest (model.json, TrocrRecognizer.h). Which folder: the
 * setting "handwritingModel" if set, else the environment variable XQT_HWR_MODEL, else the app's data folder,
 * "~/.local/share/xournal-qt/models/trocr-small-hw-int8/" (where qt/scripts/hwr-model.sh and the download in Settings
 * put it). The recogniser is made by a factory the app sets (TrocrRecognizer; the tests' FakeRecognizer).
 *
 * UI thread.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <QObject>
#include <QPointer>
#include <QString>

#include "InkRecognitionService.h"
#include "InkTextIndexer.h"
#include "filesystem.h"

class Settings;

namespace xqt {
class AppContext;
class DocumentSession;
}  // namespace xqt

namespace xqt::hwr {

class HandwritingSearch final: public QObject {
    Q_OBJECT
public:
    /// The model's folder name in the app's data folder.
    static constexpr const char* MODEL_NAME = "trocr-small-hw-int8";

    explicit HandwritingSearch(AppContext& app, QObject* parent = nullptr);
    ~HandwritingSearch() override;

    /// The setting (off by default).
    static bool enabledIn(Settings& settings);
    static void setEnabledIn(Settings& settings, bool on);
    /// The model's folder: the setting, else XQT_HWR_MODEL, else the app's data folder (see above).
    static QString modelDir(Settings& settings);
    /// "~/.local/share/xournal-qt/models/<MODEL_NAME>" (where downloads go).
    static QString defaultModelDir();

    /// Makes the recogniser for a model folder (null: none). The app sets TrocrRecognizer's.
    using Factory = std::function<std::shared_ptr<Recognizer>(const QString& modelDir)>;
    static void setFactory(Factory factory);

    /// What was read before in a document's lines (the library's cache), for the recogniser with this id: the worker
    /// takes them, so opening a document reads none of it again.
    using LineResults = std::vector<std::pair<quint64, std::shared_ptr<const ink::LineResult>>>;
    using Seeder = std::function<LineResults(const fs::path& file, const QString& recognizer)>;
    static void setSeeder(Seeder seeder);

    /// Read the settings again (switched on or off, another model).
    void applySettings();
    bool enabled() const { return on; }
    /// Make the recogniser anew (the model was downloaded or removed).
    void reloadModel();
    /// The model's folder in use.
    QString modelFolderInUse() const;
    InkRecognitionService& service() { return worker; }

    /// The open documents of a window (`window`: any key; several windows share the search), and the one in front.
    void setSessions(const QObject* window, const std::vector<DocumentSession*>& sessions, DocumentSession* current);
    /// Pages of the open documents waiting to be read.
    int pagesWaiting() const;
    /// The indexer of an open document (null: none, the search is off).
    InkTextIndexer* indexerOf(const DocumentSession* session) const;

Q_SIGNALS:
    void enabledChanged();
    /// Pages read or waiting changed in an open document.
    void progress();

private:
    void dropIndexers();
    void update();

    AppContext& app;
    InkRecognitionService worker;
    bool on = false;
    QString modelFolder;
    std::map<const DocumentSession*, QPointer<InkTextIndexer>> indexers;
    struct Window {
        std::vector<QPointer<DocumentSession>> open;
        QPointer<DocumentSession> front;
    };
    std::map<const QObject*, Window> windows;
};

}  // namespace xqt::hwr
