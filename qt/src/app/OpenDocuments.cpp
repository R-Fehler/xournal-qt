#include "OpenDocuments.h"

#include <algorithm>
#include <mutex>
#include <shared_mutex>

#include "model/Document.h"
#include "session/DocumentSession.h"
#include "session/TextFile.h"
#include "shell/TabManager.h"

#include "AppController.h"

namespace xqt {

namespace {
bool sameFile(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    return !a.empty() && (a == b || fs::equivalent(a, b, ec));
}
}  // namespace

void OpenDocuments::add(AppController* window) {
    if (window && std::find(list.begin(), list.end(), window) == list.end()) {
        list.push_back(window);
    }
}

void OpenDocuments::remove(AppController* window) { list.erase(std::remove(list.begin(), list.end(), window), list.end()); }

OpenDocuments::Found OpenDocuments::find(const fs::path& file, const Match& match) const {
    Found found;
    for (AppController* w: list) {
        TabManager& tabs = w->tabManager();
        for (int i = 0; i < tabs.count(); ++i) {
            DocumentSession* t = tabs.session(i);
            if (!t || t == match.except) {
                continue;
            }
            bool shows = t->hasFilePath() && sameFile(t->getFilePath(), file);
            if (!shows && match.textFiles && t->textFile()) {
                shows = sameFile(t->textFile()->path(), file);
            }
            if (!shows && match.plainPdf && !t->hasFilePath() && !t->textFile()) {
                fs::path pdf;
                {
                    std::shared_lock lock(*t->getDocument());
                    pdf = t->getDocument()->getPdfFilepath();
                }
                shows = sameFile(pdf, file);
            }
            if (shows) {
                found.emplace_back(w, t);
            }
        }
    }
    return found;
}

std::vector<DocumentSession*> OpenDocuments::all() const {
    std::vector<DocumentSession*> sessions;
    for (AppController* w: list) {
        TabManager& tabs = w->tabManager();
        for (int i = 0; i < tabs.count(); ++i) {
            if (DocumentSession* s = tabs.session(i)) {
                sessions.push_back(s);
            }
        }
    }
    return sessions;
}

}  // namespace xqt
