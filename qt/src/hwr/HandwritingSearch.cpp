#include "HandwritingSearch.h"

#include <QStandardPaths>

#include "control/settings/Settings.h"
#include "model/Document.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/DocumentTextIndex.h"

namespace xqt::hwr {

namespace {
HandwritingSearch::Factory& factory() {
    static HandwritingSearch::Factory f;
    return f;
}
}  // namespace

void HandwritingSearch::setFactory(Factory f) { factory() = std::move(f); }

namespace {
HandwritingSearch::Seeder& seeder() {
    static HandwritingSearch::Seeder s;
    return s;
}
}  // namespace

void HandwritingSearch::setSeeder(Seeder s) { seeder() = std::move(s); }

bool HandwritingSearch::enabledIn(Settings& s) {
    bool on = false;
    s.getCustomElement("xournalQt").getBool("handwritingSearch", on);
    return on;
}

void HandwritingSearch::setEnabledIn(Settings& s, bool on) {
    s.getCustomElement("xournalQt").setBool("handwritingSearch", on);
    s.customSettingsChanged();
}

QString HandwritingSearch::defaultModelDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
           QStringLiteral("/xournal-qt/models/") + QLatin1String(MODEL_NAME);
}

QString HandwritingSearch::modelDir(Settings& s) {
    std::string set;
    s.getCustomElement("xournalQt").getString("handwritingModel", set);
    if (!set.empty()) {
        return QString::fromStdString(set);
    }
    if (const QString env = qEnvironmentVariable("XQT_HWR_MODEL"); !env.isEmpty()) {
        return env;
    }
    return defaultModelDir();
}

HandwritingSearch::HandwritingSearch(AppContext& app, QObject* parent): QObject(parent), app(app) {
    connect(&app, &AppContext::settingsChanged, this, &HandwritingSearch::applySettings);
    applySettings();
}

HandwritingSearch::~HandwritingSearch() { dropIndexers(); }

void HandwritingSearch::applySettings() {
    Settings& s = *app.getSettings();
    const bool wanted = enabledIn(s);
    const QString folder = modelDir(s);
    if (wanted && (!worker.recognizer() || folder != modelFolder)) {
        modelFolder = folder;
        worker.setRecognizer(factory() ? factory()(folder) : nullptr);
    }
    if (wanted == on) {
        return;
    }
    on = wanted;
    if (!on) {
        dropIndexers();
        worker.setRecognizer(nullptr);  // (its model goes)
        modelFolder.clear();
    }
    update();
    Q_EMIT enabledChanged();
}

void HandwritingSearch::reloadModel() {
    if (on) {
        modelFolder = modelDir(*app.getSettings());
        worker.setRecognizer(factory() ? factory()(modelFolder) : nullptr);
    }
    Q_EMIT enabledChanged();
}

QString HandwritingSearch::modelFolderInUse() const { return modelDir(*app.getSettings()); }

void HandwritingSearch::dropIndexers() {
    for (auto& [session, indexer]: indexers) {
        delete indexer.data();
    }
    indexers.clear();
    // The open documents forget the words (they are searched no more)
    for (const auto& [key, w]: windows) {
        for (const auto& s: w.open) {
            if (!s) {
                continue;
            }
            DocumentTextIndex& index = s->search().textIndex();
            for (size_t i = 0; i < index.pageCount(); ++i) {
                index.setInk(i, nullptr);
            }
        }
    }
}

void HandwritingSearch::setSessions(const QObject* window, const std::vector<DocumentSession*>& sessions,
                                    DocumentSession* current) {
    if (sessions.empty()) {
        windows.erase(window);
    } else {
        Window& w = windows[window];
        w.open.assign(sessions.begin(), sessions.end());
        w.front = current;
    }
    update();
}

void HandwritingSearch::update() {
    std::vector<std::pair<DocumentSession*, bool>> all;  ///< with: in front
    for (const auto& [key, w]: windows) {
        for (const auto& s: w.open) {
            if (s) {
                all.emplace_back(s.data(), s == w.front);
            }
        }
    }
    // Gone ones (their indexers went with them: children of the session)
    for (auto it = indexers.begin(); it != indexers.end();) {
        const bool still =
                std::any_of(all.begin(), all.end(), [&](const auto& e) { return e.first == it->first; });
        if (!still || !it->second) {
            delete it->second.data();
            it = indexers.erase(it);
        } else {
            ++it;
        }
    }
    if (!on) {
        return;
    }
    for (const auto& [s, inFront]: all) {
        auto& indexer = indexers[s];
        if (!indexer) {
            if (seeder() && s->hasFilePath()) {
                for (auto& [hash, result]: seeder()(s->getFilePath(), worker.recognizerId())) {
                    worker.remember(hash, std::move(result));
                }
            }
            indexer = new InkTextIndexer(*s, worker, s);  // (goes with its document)
            connect(indexer, &InkTextIndexer::progress, this, &HandwritingSearch::progress);
        }
        indexer->setFocused(inFront);
    }
}

int HandwritingSearch::pagesWaiting() const {
    int n = 0;
    for (const auto& [session, indexer]: indexers) {
        n += indexer ? indexer->pagesWaiting() : 0;
    }
    return n;
}

InkTextIndexer* HandwritingSearch::indexerOf(const DocumentSession* session) const {
    auto it = indexers.find(session);
    return it != indexers.end() ? it->second.data() : nullptr;
}

}  // namespace xqt::hwr
