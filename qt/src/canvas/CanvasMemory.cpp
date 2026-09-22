#include "CanvasMemory.h"

#include <algorithm>

#include <unistd.h>

#include "CanvasView.h"

namespace xqt {

CanvasMemory& CanvasMemory::instance() {
    static auto* memory = new CanvasMemory;  // (never destroyed: views may go after statics)
    return *memory;
}

qint64 CanvasMemory::systemMemory() {
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long size = sysconf(_SC_PAGE_SIZE);
    if (pages <= 0 || size <= 0) {
        return qint64(8) * 1024 * 1024 * 1024;
    }
    return static_cast<qint64>(pages) * size;
}

qint64 CanvasMemory::defaultLimit() { return systemMemory() / 4; }

qint64 CanvasMemory::maxLimit() { return systemMemory() / 3; }

CanvasMemory::CanvasMemory(): max(defaultLimit()) {
    planTimer.setSingleShot(true);
    planTimer.setInterval(250);
    connect(&planTimer, &QTimer::timeout, this, &CanvasMemory::plan);
}

void CanvasMemory::setLimit(qint64 bytes) {
    max = std::max<qint64>(0, bytes);
    planTimer.start(0);
    Q_EMIT limitChanged();
}

void CanvasMemory::add(CanvasView* view) {
    views.push_back(View{view, 0});
    planTimer.start();
}

void CanvasMemory::remove(CanvasView* view) {
    views.erase(std::remove_if(views.begin(), views.end(), [view](const View& v) { return v.view == view; }),
                views.end());
    planTimer.start();
}

void CanvasMemory::used(CanvasView* view) {
    for (auto& v: views) {
        if (v.view == view) {
            v.used = ++counter;
        }
    }
    planTimer.start();  // (again: once the reader paused)
}

void CanvasMemory::planNow() {
    planTimer.stop();
    plan();
}

qint64 CanvasMemory::bytes() const {
    qint64 sum = 0;
    for (const auto& v: views) {
        sum += v.view->bufferBytes();
    }
    return sum;
}

void CanvasMemory::plan() {
    if (views.empty()) {
        return;
    }
    std::vector<View> order = views;
    std::stable_sort(order.begin(), order.end(), [](const View& a, const View& b) { return a.used > b.used; });
    const qint64 pages = pagesLimit();
    const auto share = static_cast<qint64>(static_cast<double>(pages) * (order.size() > 1 ? CURRENT_SHARE : 1.0));
    qint64 left = pages - order.front().view->planCache(share);
    for (size_t i = 1; i < order.size(); ++i) {
        left -= order[i].view->trimTo(std::max<qint64>(0, left));
    }
}

}  // namespace xqt
