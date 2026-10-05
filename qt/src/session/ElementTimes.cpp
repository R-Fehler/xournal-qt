#include "ElementTimes.h"

#include <chrono>
#include <mutex>
#include <utility>

#include "model/Element.h"
#include "model/Layer.h"
#include "model/XojPage.h"

namespace xqt::timeline {

namespace {
std::mutex clockMutex;
std::function<int64_t()>& testClock() {
    static std::function<int64_t()> clock;
    return clock;
}
}  // namespace

int64_t now() {
    {
        std::lock_guard lock(clockMutex);
        if (testClock()) {
            return testClock()();
        }
    }
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void setClock(std::function<int64_t()> clock) {
    std::lock_guard lock(clockMutex);
    testClock() = std::move(clock);
}

void stampNew(Element& e) { e.setCreated(now()); }

void stampNew(Element& e, int64_t at) { e.setCreated(at); }

void stampNew(const std::vector<Element*>& elements) {
    const int64_t at = now();
    for (Element* e: elements) {
        if (e) {
            e->setCreated(at);
        }
    }
}

void stampPage(XojPage& page) {
    const int64_t at = now();
    for (Layer* layer: page.getLayers()) {
        for (auto& e: layer->getElements()) {
            e->setCreated(at);
        }
    }
}

}  // namespace xqt::timeline
