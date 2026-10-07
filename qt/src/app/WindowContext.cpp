#include "WindowContext.h"

#include "shell/TabManager.h"

namespace xqt {

DocumentSession* WindowContext::session() const { return tabs.currentSession(); }
CanvasView* WindowContext::view() const { return tabs.currentView(); }

}  // namespace xqt
