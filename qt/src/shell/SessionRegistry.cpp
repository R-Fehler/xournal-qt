#include "SessionRegistry.h"

#include <condition_variable>
#include <map>
#include <mutex>

#include "PageSketches.h"
#include "Thumbnails.h"

namespace xqt::SessionRegistry {

namespace {
struct Registry {
    std::mutex mtx;
    std::condition_variable idle;
    quint64 nextId = 1;
    std::map<quint64, DocumentSession*> sessions;
    std::map<quint64, int> busy;  ///< workers holding each session
};
Registry& registry() {
    static Registry r;
    return r;
}
}  // namespace

quint64 add(DocumentSession* session) {
    auto& r = registry();
    std::lock_guard lock(r.mtx);
    for (auto& [id, s]: r.sessions) {
        if (s == session) {
            return id;
        }
    }
    const quint64 id = r.nextId++;
    r.sessions[id] = session;
    PageSketches::instance().add(id, session);
    return id;
}

void remove(DocumentSession* session) {
    auto& r = registry();
    std::unique_lock lock(r.mtx);
    for (auto it = r.sessions.begin(); it != r.sessions.end(); ++it) {
        if (it->second == session) {
            const quint64 id = it->first;
            r.sessions.erase(it);
            r.idle.wait(lock, [&] { return r.busy[id] == 0; });
            r.busy.erase(id);
            lock.unlock();
            ThumbnailProvider::dropSession(id);
            PageSketches::instance().remove(id);
            return;
        }
    }
}

quint64 idOf(const DocumentSession* session) {
    auto& r = registry();
    std::lock_guard lock(r.mtx);
    for (auto& [id, s]: r.sessions) {
        if (s == session) {
            return id;
        }
    }
    return 0;
}

DocumentSession* acquire(quint64 id) {
    auto& r = registry();
    std::lock_guard lock(r.mtx);
    auto it = r.sessions.find(id);
    if (it == r.sessions.end()) {
        return nullptr;
    }
    ++r.busy[id];
    return it->second;
}

void release(quint64 id) {
    auto& r = registry();
    std::lock_guard lock(r.mtx);
    --r.busy[id];
    r.idle.notify_all();
}

}  // namespace xqt::SessionRegistry
