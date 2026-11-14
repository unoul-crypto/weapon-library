#include <weapon_library/Weapon.h>

#include "Time.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace weapon_library {
WeaponDefinition::WeaponDefinition(std::vector<ActionDefinition> actions)
    : actions_(std::move(actions)) {
    for (std::size_t i = 0; i < actions_.size(); ++i) {
        const auto& action = actions_[i];
        if (!detail::valid_duration(action.windup_seconds) ||
            !detail::valid_duration(action.active_seconds) ||
            !detail::valid_duration(action.recovery_seconds) ||
            !std::isfinite(action.windup_seconds + action.active_seconds +
                           action.recovery_seconds)) {
            throw std::invalid_argument("Action durations must be finite and nonnegative");
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (actions_[j].id == action.id) {
                throw std::invalid_argument("Action IDs must be unique");
            }
        }
    }
}

const std::vector<ActionDefinition>& WeaponDefinition::actions() const noexcept {
    return actions_;
}

WeaponCommand WeaponCommand::start(ActionId action_id) noexcept {
    return {CommandType::StartAction, action_id};
}

WeaponCommand WeaponCommand::cancel() noexcept {
    return {CommandType::CancelAction, 0};
}

Weapon::Weapon(std::shared_ptr<const WeaponDefinition> definition)
    : definition_(std::move(definition)) {
    if (!definition_) {
        throw std::invalid_argument("Weapon definition must not be null");
    }
}

const WeaponDefinition& Weapon::definition() const noexcept { return *definition_; }

WeaponState Weapon::state() const noexcept { return state_; }

CommandResult Weapon::execute(WeaponCommand command) {
    // At most four events: start, active, recovery, completion.
    CommandResult result{CommandStatus::Accepted, {}};
    switch (command.type) {
    case CommandType::StartAction: {
        const auto& actions = definition_->actions();
        std::size_t index = 0;
        while (index < actions.size() && actions[index].id != command.action_id) {
            ++index;
        }
        if (index == actions.size()) {
            result.status = CommandStatus::UnknownAction;
            return result;
        }
        if (action_index_) {
            result.status = CommandStatus::Busy;
            return result;
        }
        result.events.reserve(4);
        action_index_ = index;
        state_.action_id = command.action_id;
        state_.phase = Phase::Windup;
        phase_rounding_error_ = 0.0;
        state_.phase_remaining_seconds = actions[index].windup_seconds;
        phase_duration_seconds_ = state_.phase_remaining_seconds;
        result.events.push_back({EventType::ActionStarted, command.action_id,
                                 Phase::Windup, state_.time_seconds});
        settle_instant_phases(result.events);
        return result;
    }
    case CommandType::CancelAction:
        if (!action_index_) {
            result.status = CommandStatus::NoActiveAction;
            return result;
        }
        if (!can_cancel()) {
            result.status = CommandStatus::CancellationBlocked;
            return result;
        }
        result.events.push_back({EventType::ActionCancelled, *state_.action_id,
                                 Phase::Ready, state_.time_seconds});
        reset_action();
        return result;
    }
    throw std::invalid_argument("Unknown weapon command type");
}

std::vector<WeaponEvent> Weapon::advance(double seconds) {
    if (!detail::valid_duration(seconds) || !std::isfinite(state_.time_seconds + seconds)) {
        throw std::invalid_argument("Time step must be finite, nonnegative and not overflow");
    }
    const double target_time = state_.time_seconds + seconds;
    double step_rounding_error = 0.0;
    std::vector<WeaponEvent> events;
    if (action_index_) {
        events.reserve(3);
    }
    while (action_index_) {
        if (!detail::duration_elapsed(seconds, state_.phase_remaining_seconds, phase_duration_seconds_)) {
            detail::consume_duration(state_.phase_remaining_seconds, seconds, phase_rounding_error_,
                                     phase_duration_seconds_);
            break;
        }
        const double elapsed = std::min(seconds, state_.phase_remaining_seconds);
        detail::consume_duration(seconds, elapsed, step_rounding_error);
        state_.time_seconds = std::min(target_time, state_.time_seconds + elapsed);
        enter_next_phase(events);
    }
    state_.time_seconds = target_time;
    return events;
}

void Weapon::enter_next_phase(std::vector<WeaponEvent>& events) {
    phase_rounding_error_ = 0.0;
    const auto& action = definition_->actions()[*action_index_];
    switch (state_.phase) {
    case Phase::Windup:
        state_.phase = Phase::Active;
        state_.phase_remaining_seconds = action.active_seconds;
        break;
    case Phase::Active:
        state_.phase = Phase::Recovery;
        state_.phase_remaining_seconds = action.recovery_seconds;
        break;
    case Phase::Recovery:
        events.push_back({EventType::ActionCompleted, action.id,
                          Phase::Ready, state_.time_seconds});
        reset_action();
        return;
    case Phase::Ready:
        throw std::logic_error("Ready weapon cannot have an active action");
    }
    phase_duration_seconds_ = state_.phase_remaining_seconds;
    events.push_back({EventType::PhaseEntered, action.id,
                      state_.phase, state_.time_seconds});
}

void Weapon::settle_instant_phases(std::vector<WeaponEvent>& events) {
    while (action_index_ && state_.phase_remaining_seconds == 0.0) {
        enter_next_phase(events);
    }
}

void Weapon::reset_action() noexcept {
    action_index_.reset();
    state_.action_id.reset();
    state_.phase = Phase::Ready;
    state_.phase_remaining_seconds = 0.0;
    phase_rounding_error_ = 0.0;
    phase_duration_seconds_ = 0.0;
}

bool Weapon::can_cancel() const noexcept {
    const auto& policy = definition_->actions()[*action_index_].cancellation;
    switch (state_.phase) {
    case Phase::Windup: return policy.windup;
    case Phase::Active: return policy.active;
    case Phase::Recovery: return policy.recovery;
    case Phase::Ready: return false;
    }
    return false;
}

} // namespace weapon_library
