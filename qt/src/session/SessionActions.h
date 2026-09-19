/*
 * xournal-qt: per-session implementation of the (shadow) ActionDatabase.
 *
 * Reused upstream code enables/disables actions and pushes action states (e.g. the active layer) through
 * `control->getActionDatabase()`. This class stores them per document session; the Qt UI binds to the active
 * session's actions. User-triggered activations (fireActivateAction / fireChangeActionState) are forwarded to a
 * handler installed by the application (the ActionRegistry).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <array>
#include <functional>

#include "control/actions/ActionDatabase.h"

namespace xqt {

class SessionActions final: public ActionDatabase {
public:
    SessionActions();

    void enableAction(Action a, bool enable) override;
    bool isActionEnabled(Action a) const override;
    const State& getActionState(Action a) const;

    /// Called whenever the enabled flag or the state of an action changed.
    std::function<void(Action)> onChanged;
    /// Called for actions triggered by reused upstream code (activation with optional parameter / state change).
    std::function<void(Action, const State&)> onActivate;
    std::function<void(Action, const State&)> onChangeState;

protected:
    void setActionStateImpl(Action a, State state) override;
    void fireChangeActionStateImpl(Action a, State state) override;
    void fireActivateActionImpl(Action a, State param) override;

private:
    static constexpr size_t COUNT = static_cast<size_t>(Action::ENUMERATOR_COUNT);
    std::array<bool, COUNT> enabled;
    std::array<State, COUNT> states;
};

}  // namespace xqt
