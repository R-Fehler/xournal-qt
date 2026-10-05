#include "HandwritingSearch.h"

#include <QDir>
#include <QStandardPaths>

#include "control/settings/Settings.h"
#include "MultiRecognizer.h"
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

const std::vector<HandwritingSearch::Slot>& HandwritingSearch::slots() {
    static const std::vector<Slot> all{
            {QStringLiteral("en"), QStringLiteral("trocr-small-hw-int8"), QStringLiteral("handwritingModel"),
             QStringLiteral("XQT_HWR_MODEL")},
            {QStringLiteral("de"), QStringLiteral("crnn-de"), QStringLiteral("handwritingModelDe"),
             QStringLiteral("XQT_HWR_MODEL_DE")}};
    return all;
}

const HandwritingSearch::Slot* HandwritingSearch::slotOf(const QString& language) {
    for (const Slot& s: slots()) {
        if (s.language == language) {
            return &s;
        }
    }
    return nullptr;
}

QStringList HandwritingSearch::languagesIn(Settings& s) {
    std::string set;
    s.getCustomElement("xournalQt").getString("handwritingLanguages", set);
    QStringList out;
    for (const QString& l: QString::fromStdString(set).split(u'+', Qt::SkipEmptyParts)) {
        if (slotOf(l) && !out.contains(l)) {
            out << l;
        }
    }
    if (out.isEmpty()) {
        out = {QStringLiteral("en"), QStringLiteral("de")};  // (English and German by default)
    }
    // In the slots' order (English first): the members of the model set, and its id, do not depend on the clicks
    QStringList ordered;
    for (const Slot& slot: slots()) {
        if (out.contains(slot.language)) {
            ordered << slot.language;
        }
    }
    return ordered;
}

void HandwritingSearch::setLanguagesIn(Settings& s, const QStringList& languages) {
    s.getCustomElement("xournalQt").setString("handwritingLanguages", languages.join(u'+').toStdString());
    s.customSettingsChanged();
}

QString HandwritingSearch::modelsDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/xournal-qt/models");
}

QString HandwritingSearch::defaultModelDir(const QString& language) {
    const Slot* slot = slotOf(language);
    return modelsDir() + u'/' + (slot ? slot->name : language);
}

QString HandwritingSearch::modelDir(Settings& s, const QString& language) {
    const Slot* slot = slotOf(language);
    if (!slot) {
        return defaultModelDir(language);
    }
    std::string set;
    s.getCustomElement("xournalQt").getString(slot->setting.toStdString().c_str(), set);
    if (!set.empty()) {
        return QString::fromStdString(set);
    }
    if (const QString env = qEnvironmentVariable(slot->env.toUtf8().constData()); !env.isEmpty()) {
        return env;
    }
    return defaultModelDir(language);
}

void HandwritingSearch::setModelDirIn(Settings& s, const QString& language, const QString& folder) {
    if (const Slot* slot = slotOf(language)) {
        s.getCustomElement("xournalQt").setString(slot->setting.toStdString().c_str(), folder.toStdString());
        s.customSettingsChanged();
    }
}

HandwritingSearch::Choice HandwritingSearch::choose(Settings& s) {
    Choice c;
    // The models in the app's models folder (by name: the same choice every time)
    std::vector<ModelInfo> found;
    const QDir models(modelsDir());
    for (const QString& name: models.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (name.endsWith(QLatin1String(".part"))) {
            continue;  // (a download not finished)
        }
        if (ModelInfo info = ModelInfo::read(models.filePath(name)); info.valid()) {
            found.push_back(std::move(info));
        }
    }
    for (const QString& language: languagesIn(s)) {
        if (std::any_of(c.models.begin(), c.models.end(), [&](const ModelInfo& m) { return m.reads(language); })) {
            continue;  // (a model that reads both)
        }
        const QString folder = modelDir(s, language);
        ModelInfo info = ModelInfo::read(folder);
        if (!info.valid() || !info.reads(language)) {
            const bool own = QDir::cleanPath(folder) == QDir::cleanPath(defaultModelDir(language));
            auto other = std::find_if(found.begin(), found.end(), [&](const ModelInfo& m) { return m.reads(language); });
            if (!own || other == found.end()) {
                c.missing << language;  // (a folder the user chose is not replaced by another)
                continue;
            }
            info = *other;
        }
        if (std::none_of(c.models.begin(), c.models.end(), [&](const ModelInfo& m) {
                return QDir::cleanPath(m.folder) == QDir::cleanPath(info.folder);
            })) {
            c.models.push_back(std::move(info));
        }
    }
    return c;
}

HandwritingSearch::HandwritingSearch(AppContext& app, QObject* parent): QObject(parent), app(app) {
    connect(&app, &AppContext::settingsChanged, this, &HandwritingSearch::applySettings);
    applySettings();
}

HandwritingSearch::~HandwritingSearch() { dropIndexers(); }

namespace {
QStringList foldersOf(Settings& s) {
    const HandwritingSearch::Choice c = HandwritingSearch::choose(s);
    QStringList folders;
    for (const ModelInfo& m: c.models) {
        folders << m.folder;
    }
    if (folders.isEmpty()) {
        // (none there: the first language's, whose recogniser says why it cannot read)
        folders << HandwritingSearch::modelDir(s, HandwritingSearch::languagesIn(s).value(0, QStringLiteral("en")));
    }
    return folders;
}
}  // namespace

void HandwritingSearch::makeRecognizer(const QStringList& folders) {
    modelFolders = folders;
    std::vector<std::shared_ptr<Recognizer>> members;
    for (const QString& folder: folders) {
        if (auto r = factory() ? factory()(folder) : nullptr) {
            members.push_back(std::move(r));
        }
    }
    if (members.size() == 1) {
        worker.setRecognizer(std::move(members.front()));  // (one model: its id names the results, as before)
    } else if (members.empty()) {
        worker.setRecognizer(nullptr);
    } else {
        worker.setRecognizer(std::make_shared<MultiRecognizer>(std::move(members)));
    }
}

void HandwritingSearch::applySettings() {
    Settings& s = *app.getSettings();
    const bool wanted = enabledIn(s);
    if (wanted) {
        const QStringList folders = foldersOf(s);
        if (!worker.recognizer() || folders != modelFolders) {
            makeRecognizer(folders);
        }
    }
    if (wanted == on) {
        return;
    }
    on = wanted;
    if (!on) {
        dropIndexers();
        worker.setRecognizer(nullptr);  // (its models go)
        modelFolders.clear();
    }
    update();
    Q_EMIT enabledChanged();
}

void HandwritingSearch::reloadModel() {
    if (on) {
        makeRecognizer(foldersOf(*app.getSettings()));
    }
    Q_EMIT enabledChanged();
}

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
