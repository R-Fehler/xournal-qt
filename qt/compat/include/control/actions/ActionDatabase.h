/*
 * xournal-qt: shadow of upstream control/actions/ActionDatabase.h.
 *
 * Upstream's ActionDatabase wraps GActions. Reused upstream code only enables/disables actions and pushes action
 * states (e.g. the active layer). In the Qt build this is an abstract sink implemented by the Qt ActionRegistry;
 * states are converted to a small variant instead of GVariant.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>      // for int64_t
#include <string>       // for string
#include <type_traits>  // for is_enum_v
#include <variant>      // for variant

#include "enums/Action.enum.h"  // for Action

class ActionDatabase {
public:
    using State = std::variant<std::monostate, bool, int64_t, double, std::string>;

    virtual ~ActionDatabase() = default;

    virtual void enableAction(Action a, bool enable) = 0;
    virtual bool isActionEnabled(Action a) const = 0;

    /// Set the state of a stateful action without triggering its callback (the UI mirrors the state).
    template <typename state_type>
    void setActionState(Action a, state_type state) {
        setActionStateImpl(a, toState(state));
    }
    /// Change the state of a stateful action *and* trigger its callback.
    template <typename state_type>
    void fireChangeActionState(Action a, state_type state) {
        fireChangeActionStateImpl(a, toState(state));
    }
    void fireActivateAction(Action a) { fireActivateActionImpl(a, State{}); }
    template <typename param_type>
    void fireActivateAction(Action a, param_type param) {
        fireActivateActionImpl(a, toState(param));
    }

protected:
    virtual void setActionStateImpl(Action a, State state) = 0;
    virtual void fireChangeActionStateImpl(Action a, State state) = 0;
    virtual void fireActivateActionImpl(Action a, State param) = 0;

    template <typename T>
    static State toState(const T& v) {
        if constexpr (std::is_same_v<T, bool>) {
            return v;
        } else if constexpr (std::is_enum_v<T>) {
            return static_cast<int64_t>(v);
        } else if constexpr (std::is_integral_v<T>) {
            return static_cast<int64_t>(v);
        } else if constexpr (std::is_floating_point_v<T>) {
            return static_cast<double>(v);
        } else if constexpr (std::is_constructible_v<std::string, T>) {
            return std::string(v);
        } else {
            return static_cast<int64_t>(static_cast<uint32_t>(v));  // e.g. Color
        }
    }
};
