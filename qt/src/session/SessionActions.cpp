#include "SessionActions.h"

namespace xqt {

SessionActions::SessionActions() { enabled.fill(true); }

void SessionActions::enableAction(Action a, bool enable) {
    auto& e = enabled[static_cast<size_t>(a)];
    if (e != enable) {
        e = enable;
        if (onChanged) {
            onChanged(a);
        }
    }
}

bool SessionActions::isActionEnabled(Action a) const { return enabled[static_cast<size_t>(a)]; }

auto SessionActions::getActionState(Action a) const -> const State& { return states[static_cast<size_t>(a)]; }

void SessionActions::setActionStateImpl(Action a, State state) {
    auto& s = states[static_cast<size_t>(a)];
    if (s != state) {
        s = std::move(state);
        if (onChanged) {
            onChanged(a);
        }
    }
}

void SessionActions::fireChangeActionStateImpl(Action a, State state) {
    setActionStateImpl(a, state);
    if (onChangeState) {
        onChangeState(a, states[static_cast<size_t>(a)]);
    }
}

void SessionActions::fireActivateActionImpl(Action a, State param) {
    if (onActivate) {
        onActivate(a, param);
    }
}

}  // namespace xqt
