/*
 * xournal-qt: the handwriting search of the app (qt/docs/handwriting-search.md): the setting, the recogniser and its
 * model, the worker, and an indexer per open document.
 *
 * Off until switched on in Settings ("handwritingSearch" in the xournalQt part of the settings). While on, every open
 * document gets an InkTextIndexer (the one in front first), all sharing one InkRecognitionService. Off again: the
 * indexers go and the open documents forget their recognised words (the library's results stay on disk).
 *
 * The models (qt/docs/handwriting-search.md, "Languages and models"): the setting "handwritingLanguages" says which
 * languages are read ("en", "de" or "en+de", the default); each language has a slot (slots(): English, German) whose
 * model is a folder with a manifest (model.json, ModelInfo.h): the folder chosen in Settings for it
 * ("handwritingModel", "handwritingModelDe"), else its environment variable (XQT_HWR_MODEL, XQT_HWR_MODEL_DE), else
 * the app's own in its data folder, "~/.local/share/xournal-qt/models/<slot's name>/" (where qt/scripts/hwr-model.sh
 * and the download in Settings put it). Without a model there, any model in the app's models folder that reads the
 * language is taken; one model that reads both languages serves both. The recogniser of each model is made by a
 * factory the app sets (by the manifest's kind: TrocrRecognizer, CtcRecognizer; the tests' FakeRecognizer); several
 * models run together (MultiRecognizer), and their ids make the recogniser's id that names the results.
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
#include "ModelInfo.h"
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
    /// A language's model: its folder name in the app's models folder (and the name of the model downloaded for it),
    /// and where another folder may be chosen for it.
    struct Slot {
        QString language;  ///< "en", "de"
        QString name;      ///< "trocr-small-hw-int8"
        QString setting;   ///< "handwritingModel"
        QString env;       ///< "XQT_HWR_MODEL"
    };
    static const std::vector<Slot>& slots();
    static const Slot* slotOf(const QString& language);

    explicit HandwritingSearch(AppContext& app, QObject* parent = nullptr);
    ~HandwritingSearch() override;

    /// The setting (off by default).
    static bool enabledIn(Settings& settings);
    static void setEnabledIn(Settings& settings, bool on);
    /// The languages read ("en", "de"; English and German by default).
    static QStringList languagesIn(Settings& settings);
    static void setLanguagesIn(Settings& settings, const QStringList& languages);
    /// A language's model folder: the one chosen for it, else its environment variable, else the app's own.
    static QString modelDir(Settings& settings, const QString& language = QStringLiteral("en"));
    /// The folder chosen for a language's model ("": the app's own again).
    static void setModelDirIn(Settings& settings, const QString& language, const QString& folder);
    /// "~/.local/share/xournal-qt/models" and "<that>/<slot's name>" (where downloads go).
    static QString modelsDir();
    static QString defaultModelDir(const QString& language = QStringLiteral("en"));

    /// The models the languages need: the installed ones chosen for them (in the languages' order, each once), and
    /// the languages without one.
    struct Choice {
        std::vector<ModelInfo> models;
        QStringList missing;
    };
    static Choice choose(Settings& settings);

    /// Makes the recogniser for a model folder (null: none). The app sets one by the manifest's kind.
    using Factory = std::function<std::shared_ptr<Recognizer>(const QString& modelDir)>;
    static void setFactory(Factory factory);

    /// What was read before in a document's lines (the library's cache), for the recogniser with this id: the worker
    /// takes them, so opening a document reads none of it again; and the document's language (LanguagePlan.h): the
    /// user's choice ("auto", "en", "de", "both") and the language decided for this recogniser ("": none yet).
    using LineResults = std::vector<std::pair<quint64, std::shared_ptr<const ink::LineResult>>>;
    struct Seeded {
        LineResults lines;
        QString choice;
        QString decided;
    };
    using Seeder = std::function<Seeded(const fs::path& file, const QString& recognizer)>;
    static void setSeeder(Seeder seeder);

    /// Read the settings again (switched on or off, another model).
    void applySettings();
    bool enabled() const { return on; }
    /// Make the recogniser anew (a model was downloaded or removed).
    void reloadModel();
    /// The model folders in use (one per model; those of the models found, else the first language's).
    QStringList modelFoldersInUse() const { return modelFolders; }
    InkRecognitionService& service() { return worker; }

    /// The open documents of a window (`window`: any key; several windows share the search), and the one in front.
    void setSessions(const QObject* window, const std::vector<DocumentSession*>& sessions, DocumentSession* current);
    /// Pages of the open documents waiting to be read.
    int pagesWaiting() const;
    /// The indexer of an open document (null: none, the search is off).
    InkTextIndexer* indexerOf(const DocumentSession* session) const;
    /// The handwriting language chosen for an open document (Automatic while the search is off).
    LanguagePlan::Choice languageChoiceOf(const DocumentSession* session) const;
    /// Choose it (the caller keeps it in the library's cache).
    void setLanguageChoice(const DocumentSession* session, LanguagePlan::Choice choice);

Q_SIGNALS:
    void enabledChanged();
    /// Pages read or waiting changed in an open document.
    void progress();

private:
    void dropIndexers();
    void update();

    AppContext& app;
    InkRecognitionService worker;
    void makeRecognizer(const QStringList& folders);

    bool on = false;
    QStringList modelFolders;
    std::map<const DocumentSession*, QPointer<InkTextIndexer>> indexers;
    struct Window {
        std::vector<QPointer<DocumentSession>> open;
        QPointer<DocumentSession> front;
    };
    std::map<const QObject*, Window> windows;
};

}  // namespace xqt::hwr
