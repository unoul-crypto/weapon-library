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
    if (definition_.reload_action_id) {
        const auto reload = std::find_if(actions.begin(), actions.end(), [&](const auto& value) {
            return value.id == *definition_.reload_action_id;
        });
        if (reload == actions.end() || reload->id == definition_.action_id) {
            throw std::invalid_argument("Reload action must exist and differ from the firing action");
        }
        reload_action_index_ = static_cast<std::size_t>(reload - actions.begin());
    }
}

const WeaponDefinition& FiringMechanism::weapon_definition() const noexcept {
    return weapon_.definition();
}

const FiringDefinition& FiringMechanism::definition() const noexcept { return definition_; }

FiringState FiringMechanism::state() const noexcept {
    ReloadState reload;
    const auto weapon = weapon_.state();
    if (reload_source_) {
        const auto& action = weapon_.definition().actions()[*reload_action_index_];
        reload.active = true;
        reload.waiting_for_action = !reload_running_;
        reload.duration_seconds = action.windup_seconds + action.active_seconds + action.recovery_seconds;
        reload.remaining_seconds = reload.duration_seconds;
        if (reload_running_) {
            reload.remaining_seconds = weapon.phase_remaining_seconds;
            if (weapon.phase == Phase::Windup) reload.remaining_seconds += action.active_seconds;
            if (weapon.phase == Phase::Windup || weapon.phase == Phase::Active)
                reload.remaining_seconds += action.recovery_seconds;
        }
    }
    return {weapon, trigger_held_, rounds_remaining_, cooldown_remaining_, reload, requires_trigger_release_};
}

std::vector<FiringEvent> FiringMechanism::execute(TriggerCommand command) {
    std::vector<FiringEvent> events;
    switch (command) {
    case TriggerCommand::Press:
        if (!trigger_held_) {
            trigger_held_ = true;
            if (reload_source_) requires_trigger_release_ = true;
            try_start(events);
        }
        return events;
    case TriggerCommand::Release:
        trigger_held_ = false;
        dry_fire_reported_ = false;
        requires_trigger_release_ = false;
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
        begin_reload_if_ready(events);
        if (trigger_held_ && definition_.mode == FireMode::Automatic) {
            try_start(events);
        }
    }
    return events;
}

FiringCancelResult FiringMechanism::cancel_action() {
    const bool had_reload = static_cast<bool>(reload_source_);
    const bool held_before = trigger_held_;
    const auto result = weapon_.execute(WeaponCommand::cancel());
    FiringCancelResult output{result.status, {}};
    append_events(result.events, output.events);
    if (result.accepted()) {
        if (reload_source_) {
            auto cancelled = cancel_reload(); // Also discard a queued reload intent.
            output.events.insert(output.events.end(), cancelled.events.begin(), cancelled.events.end());
        }
        trigger_held_ = false;
        dry_fire_reported_ = false;
        if (had_reload) requires_trigger_release_ = held_before;
    }
    return output;
}

void FiringMechanism::try_start(std::vector<FiringEvent>& events) {
    if (reload_source_ || requires_trigger_release_ || weapon_.state().phase != Phase::Ready || cooldown_remaining_ > 0.0) {
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
        if (event.action_id == definition_.action_id && event.type == EventType::PhaseEntered && event.phase == Phase::Active) {
            rounds_remaining_ -= definition_.rounds_per_shot;
            output.emplace_back(ShotFiredEvent{event.action_id, event.time_seconds,
                                               definition_.rounds_per_shot, rounds_remaining_});
        }
        if (!reload_running_ || event.action_id != *definition_.reload_action_id) continue;
        if (event.type == EventType::ActionStarted) {
            output.emplace_back(ReloadEvent{ReloadEventType::Started, event.time_seconds, 0, rounds_remaining_});
        } else if (event.type == EventType::ActionCompleted) {
            auto source = std::move(reload_source_);
            reload_running_ = false;
            const auto requested = definition_.magazine_capacity - rounds_remaining_;
            const auto transferred = source->take_up_to(requested);
            if (transferred > requested) throw std::logic_error("Ammunition source returned more rounds than requested");
            rounds_remaining_ += transferred;
            output.emplace_back(ReloadEvent{ReloadEventType::Completed, event.time_seconds, transferred, rounds_remaining_});
        } else if (event.type == EventType::ActionCancelled) {
            reload_source_.reset();
            reload_running_ = false;
            output.emplace_back(ReloadEvent{ReloadEventType::Cancelled, event.time_seconds, 0, rounds_remaining_});
        }
    }
}

ReloadCommandResult FiringMechanism::request_reload(std::shared_ptr<AmmunitionSource> source) {
    if (!reload_action_index_) return {ReloadStatus::Unsupported, {}};
    if (reload_source_) return {ReloadStatus::Busy, {}};
    if (!source) return {ReloadStatus::NoSource, {}};
    // A committed windup will spend its round before the queued reload starts.
    if (rounds_remaining_ == definition_.magazine_capacity && weapon_.state().phase != Phase::Windup)
        return {ReloadStatus::MagazineFull, {}};
    if (source->available_rounds() == 0) return {ReloadStatus::NoAmmunition, {}};
    reload_source_ = std::move(source);
    requires_trigger_release_ = trigger_held_;
    dry_fire_reported_ = false;
    ReloadCommandResult result{ReloadStatus::Accepted, {}};
    begin_reload_if_ready(result.events);
    return result;
}

void FiringMechanism::begin_reload_if_ready(std::vector<FiringEvent>& events) {
    if (!reload_source_ || reload_running_ || weapon_.state().phase != Phase::Ready) return;
    reload_running_ = true;
    const auto result = weapon_.execute(WeaponCommand::start(*definition_.reload_action_id));
    if (!result.accepted()) throw std::logic_error("Ready weapon could not start its reload action");
    append_events(result.events, events);
}

ReloadCommandResult FiringMechanism::cancel_reload() {
    if (!reload_source_) return {ReloadStatus::NotReloading, {}};
    ReloadCommandResult result{ReloadStatus::Accepted, {}};
    if (!reload_running_) {
        reload_source_.reset();
        result.events.emplace_back(ReloadEvent{ReloadEventType::Cancelled, weapon_.state().time_seconds, 0, rounds_remaining_});
    } else {
        const auto cancelled = weapon_.execute(WeaponCommand::cancel());
        if (!cancelled.accepted()) return {ReloadStatus::CancellationBlocked, {}};
        append_events(cancelled.events, result.events);
    }
    requires_trigger_release_ = trigger_held_;
    return result;
}

} // namespace weapon_library
