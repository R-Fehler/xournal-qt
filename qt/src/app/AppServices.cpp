#include "AppServices.h"

#include <map>

#include <QObject>
#include <QPointer>

#include "control/settings/Settings.h"
#include "gui/toolbarMenubar/model/ColorPalette.h"
#include "hwr/HandwritingSearch.h"
#include "session/AppContext.h"
#include "session/DocumentImages.h"
#include "session/DocumentTextIndex.h"
#include "session/HybridPdf.h"
#include "shell/HandwritingSettings.h"
#include "shell/Library.h"
#include "shell/LibraryBookmarks.h"
#include "shell/LibraryInkJob.h"
#include "shell/LibraryModel.h"
#include "shell/LibraryTags.h"
#include "shell/LibraryTodos.h"
#include "shell/PageClipboard.h"
#include "shell/RecentFiles.h"
#include "shell/SessionRecovery.h"
#include "shell/SettingsModel.h"
#include "shell/ShortcutsModel.h"
#include "shell/Todos.h"
#include "shell/ToolboxModel.h"
#include "util/Util.h"

#include "MdImageDecoder.h"

namespace xqt {

namespace {
const char* const CUSTOM = "xournalQt";  // our settings (in upstream's settings file)
}  // namespace

AppServices::AppServices() {
    app = std::make_shared<AppContext>(AppContext::defaultResourceDir());
    MdImageDecoder::install();  // the pictures of Markdown texts, read with Qt (qt/docs/features/md-images.md)
    DocumentImages::pruneWorkFolders();  // (work folders of documents not opened for 60 days; before any opens)
    // What protected PDFs had taken out into the cache in a process that crashed (qt/docs/features/hybrid-pdf.md)
    HybridPdf::removeProtectedLeftovers([](int64_t pid) {
        return pid == Util::getPid() || SessionRecovery::processAlive(static_cast<qint64>(pid));
    });
    palette = std::make_shared<Palette>(app->getResourceDir() / "palettes" / "xournal.gpl");
    try {
        palette->load();
    } catch (const std::exception& e) {
        palette->load_default();
    }
    Settings& stored = *app->getSettings();
    SettingsModel::applyPreviewMemory(stored);
    SettingsModel::applyCanvasMemory(stored);
    SettingsModel::applyFuzzyTypos(stored);

    pages = std::make_unique<PageClipboard>();
    settings = std::make_unique<SettingsModel>(*app);
    // The toolbox's tools (qt/docs/features/toolbox.md): stored in the settings; without them, the first layout
    AppContext* context = app.get();
    tools = std::make_unique<ToolboxModel>(
            [context] {
                std::string json;
                context->getSettings()->getCustomElement(CUSTOM).getString("toolbox", json);
                return QString::fromStdString(json);
            },
            [context](const QString& json) {
                context->getSettings()->getCustomElement(CUSTOM).setString("toolbox", json.toStdString());
                context->getSettings()->customSettingsChanged();
            });
    keys = std::make_unique<ShortcutsModel>(stored);
    ink = std::make_unique<hwr::HandwritingSearch>(*app);

    lib = std::make_unique<LibraryModel>();
    bookmarks = std::make_unique<LibraryBookmarksModel>(lib.get());
    tags = std::make_unique<LibraryTagsModel>(lib.get());
    todos = std::make_unique<LibraryTodosModel>(lib.get());
    todos->setRules(todos::Rules::of(stored));
    QObject::connect(context, &AppContext::settingsChanged, todos.get(),
                     [this, context] { todos->setRules(todos::Rules::of(*context->getSettings())); });
    // Open documents take the PDF text the library index read before (their search has all counts at once)
    DocumentTextIndex::setSeeder([lib = QPointer<LibraryModel>(lib.get())](const fs::path& pdf) {
        LibraryIndex* index = lib ? lib->searchIndex() : nullptr;
        return index ? index->knownPdfText(pdf) : std::map<int, QString>();
    });
    // ... and the handwriting it read before (opening a document reads none of it again)
    // (and its handwriting language: the user's choice, and the language decided for these models)
    hwr::HandwritingSearch::setSeeder([lib = QPointer<LibraryModel>(lib.get())](const fs::path& file,
                                                                                const QString& recognizer) {
        hwr::HandwritingSearch::Seeded out;
        LibraryIndex* index = lib ? lib->searchIndex() : nullptr;
        if (auto entry = index ? index->inkText().find(file) : nullptr) {
            out.choice = entry->languageChoice;
            out.decided = entry->recognizer == recognizer ? entry->language : QString();
        }
        if (auto doc = index ? index->inkOf(file) : nullptr; doc && doc->recognizer == recognizer) {
            for (const auto& page: doc->pages) {
                for (const hwr::LineRef& l: page) {
                    if (l.result) {
                        out.lines.emplace_back(l.hash, l.result);
                    }
                }
            }
        }
        return out;
    });
    // The library's handwriting: read in the background while the search is on (and on mains power)
    libraryInkJob = std::make_unique<LibraryInkJob>(ink->service());
    libraryInkJob->setIndex(lib->searchIndex());
    libraryInkJob->setEnabled(ink->enabled());
    QObject::connect(ink.get(), &hwr::HandwritingSearch::enabledChanged, libraryInkJob.get(),
                     [this] { libraryInkJob->setEnabled(ink->enabled()); });
    inkSettings = std::make_unique<HandwritingSettings>(*app, *ink, libraryInkJob.get());
    QObject::connect(lib.get(), &LibraryModel::indexChanged, libraryInkJob.get(), [this] {
        libraryInkJob->setIndex(lib->searchIndex());
        if (!lib->indexing()) {
            libraryInkJob->check();  // (documents changed or came)
        }
    });
    // The fuzzy search's toggle is an app-wide setting (shared by all windows through the library model)
    {
        bool fuzzy = false;
        stored.getCustomElement(CUSTOM).getBool("fuzzySearch", fuzzy);
        lib->setFuzzySearch(fuzzy);
        // (with it on, open documents make the vocabularies of their text in the background: the first fuzzy search
        // of a long document does not make them on the UI thread)
        DocumentTextIndex::setWordsInBackground(fuzzy);
        QObject::connect(lib.get(), &LibraryModel::fuzzySearchChanged, lib.get(), [this, context] {
            context->getSettings()->getCustomElement(CUSTOM).setBool("fuzzySearch", lib->fuzzySearch());
            context->getSettings()->customSettingsChanged();
            DocumentTextIndex::setWordsInBackground(lib->fuzzySearch());
        });
    }
    recentFiles = std::make_unique<RecentFiles>(RecentFiles::defaultStoreFile());
    // The Recent cards show what a PDF of the library is, as its cards do (the index knows it)
    recentFiles->setPdfKinds([lib = QPointer<LibraryModel>(lib.get())](const fs::path& file) {
        LibraryIndex* index = lib ? lib->searchIndex() : nullptr;
        return index ? index->pdfKind(file) : PdfKind::Unknown;
    });
    recentFiles->setVersionCounts([lib = QPointer<LibraryModel>(lib.get())](const fs::path& file) {
        LibraryIndex* index = lib ? lib->searchIndex() : nullptr;
        return index ? index->versionsOf(file) : 0;
    });
    QObject::connect(lib.get(), &LibraryModel::indexChanged, recentFiles.get(), [this] {
        if (!lib->indexing()) {
            recentFiles->pdfKindsChanged();
        }
    });
}

AppServices::~AppServices() {
    background.waitForDone();  // (a document being written is finished before the library and the settings go)
}

void AppServices::makeWindow(AppController* window) const {
    if (windowFactory) {
        windowFactory(window);
    }
}

}  // namespace xqt
