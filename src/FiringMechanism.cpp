#include <weapon_library/FiringMechanism.h>

#include "Time.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace weapon_library {

FiringMechanism::FiringMechanism(std::shared_ptr<const WeaponDefinition> weapon_definition,
                               FiringDefinition firing_definition,
                               std::uint32_t initial_rounds)
    : weapon_(std::move(weapon_definition)), definition_(firing_definition),
      rounds_remaining_(initial_rounds) {
    if (definition_.mode != FireMode::SemiAutomatic && definition_.mode != FireMode::Automatic) {
        throw std::invalid_argument("Unknown firing mode");
    }
    if (!detail::valid_duration(definition_.shot_interval_seconds) ||
        definition_.shot_interval_seconds == 0.0) {
        throw std::invalid_argument("Shot interval must be finite and positive");
    }
    if (definition_.magazine_capacity == 0 || definition_.rounds_per_shot == 0 ||
        definition_.rounds_per_shot > definition_.magazine_capacity ||
        rounds_remaining_ > definition_.magazine_capacity) {
        throw std::invalid_argument("Invalid magazine capacity, shot cost or initial ammo");
    }
    const auto& actions = weapon_.definition().actions();
    const auto action = std::find_if(actions.begin(), actions.end(), [&](const auto& value) {
        return value.id == definition_.action_id;
    });
    if (action == actions.end()) {
        throw std::invalid_argument("Firing action must exist in the weapon definition");
    }
}

const WeaponDefinition& FiringMechanism::weapon_definition() const noexcept {
    return weapon_.definition();
}

const FiringDefinition& FiringMechanism::definition() const noexcept { return definition_; }

FiringState FiringMechanism::state() const noexcept {
    return {weapon_.state(), trigger_held_, rounds_remaining_, cooldown_remaining_};
}

std::vector<FiringEvent> FiringMechanism::execute(TriggerCommand command) {
    std::vector<FiringEvent> events;
    switch (command) {
    case TriggerCommand::Press:
        if (!trigger_held_) {
            trigger_held_ = true;
            try_start(events);
        }
        return events;
    case TriggerCommand::Release:
        trigger_held_ = false;
        dry_fire_reported_ = false;
        return events;
    }
    throw std::invalid_argument("Unknown trigger command");
}

std::vector<FiringEvent> FiringMechanism::advance(double seconds) {
    if (!detail::valid_duration(seconds) ||
        !std::isfinite(weapon_.state().time_seconds + seconds)) {
        throw std::invalid_argument("Time step must be finite, nonnegative and not overflow");
    }
    std::vector<FiringEvent> events;
    double step_rounding_error = 0.0;
    while (seconds > 0.0) {
        double step = seconds;
        const auto current = weapon_.state();
        if (current.phase != Phase::Ready) {
            step = std::min(step, current.phase_remaining_seconds);
        }
        if (cooldown_remaining_ > 0.0) {
            step = std::min(step, cooldown_remaining_);
        }
        const auto transitions = weapon_.advance(step);
        detail::consume_duration(cooldown_remaining_, step, cooldown_rounding_error_,
                                 definition_.shot_interval_seconds);
        detail::consume_duration(seconds, step, step_rounding_error);
        append_events(transitions, events);
        if (trigger_held_ && definition_.mode == FireMode::Automatic) {
            try_start(events);
        }
    }
    return events;
}

FiringCancelResult FiringMechanism::cancel_action() {
    const auto result = weapon_.execute(WeaponCommand::cancel());
    FiringCancelResult output{result.status, {}};
    append_events(result.events, output.events);
    if (result.accepted()) {
        trigger_held_ = false;
        dry_fire_reported_ = false;
    }
    return output;
}

void FiringMechanism::try_start(std::vector<FiringEvent>& events) {
    if (weapon_.state().phase != Phase::Ready || cooldown_remaining_ > 0.0) {
        return;
    }
    if (rounds_remaining_ < definition_.rounds_per_shot) {
        if (!dry_fire_reported_) {
            events.emplace_back(DryFireEvent{definition_.action_id, weapon_.state().time_seconds});
            dry_fire_reported_ = true;
        }
        return;
    }
    const auto result = weapon_.execute(WeaponCommand::start(definition_.action_id));
    if (!result.accepted()) {
        throw std::logic_error("Ready firing mechanism could not start its configured action");
    }
    cooldown_remaining_ = definition_.shot_interval_seconds;
    cooldown_rounding_error_ = 0.0;
    append_events(result.events, events);
}

void FiringMechanism::append_events(const std::vector<WeaponEvent>& events,
                                   std::vector<FiringEvent>& output) {
    for (const auto& event : events) {
        output.emplace_back(event);
        if (event.type == EventType::PhaseEntered && event.phase == Phase::Active) {
            rounds_remaining_ -= definition_.rounds_per_shot;
            output.emplace_back(ShotFiredEvent{event.action_id, event.time_seconds,
                                               definition_.rounds_per_shot, rounds_remaining_});
        }
    }
}

} // namespace weapon_library
