#include <weapon_library/FiringMechanism.h>

#include "Time.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace weapon_library {
namespace {
bool known_fire_mode(FireMode mode) noexcept {
    return mode == FireMode::SemiAutomatic || mode == FireMode::Automatic || mode == FireMode::Burst;
}
} // namespace

FireModeSettings::FireModeSettings(FireMode initial, std::vector<FireMode> available, std::uint32_t burst_size)
    : initial_(initial), available_(std::move(available)), burst_size_(burst_size) {
    if (!known_fire_mode(initial)) throw std::invalid_argument("Unknown initial fire mode");
    if (available_.empty()) available_.push_back(initial);
    for (auto it = available_.begin(); it != available_.end(); ++it) {
        if (!known_fire_mode(*it) || std::find(available_.begin(), it, *it) != it)
            throw std::invalid_argument("Fire modes must be known and unique");
    }
    if (!supports(initial)) throw std::invalid_argument("Initial fire mode must be available");
    if (supports(FireMode::Burst) && burst_size_ < 2)
        throw std::invalid_argument("Burst mode requires at least two shots");
}

bool FireModeSettings::supports(FireMode mode) const noexcept {
    return std::find(available_.begin(), available_.end(), mode) != available_.end();
}

FiringMechanism::FiringMechanism(std::shared_ptr<const WeaponDefinition> weapon_definition,
                               FiringDefinition firing_definition,
                               std::uint32_t initial_rounds)
    : weapon_(std::move(weapon_definition)), definition_(std::move(firing_definition)),
      fire_mode_settings_(definition_.mode, definition_.available_modes, definition_.burst_size),
      fire_mode_(definition_.mode), rounds_remaining_(initial_rounds) {
    if (!detail::valid_duration(definition_.shot_interval_seconds) ||
        definition_.shot_interval_seconds == 0.0) {
        throw std::invalid_argument("Shot interval must be finite and positive");
    }
    if (definition_.magazine_capacity == 0 || definition_.rounds_per_shot == 0 ||
        definition_.rounds_per_shot > definition_.magazine_capacity ||
        rounds_remaining_ > definition_.magazine_capacity) {
        throw std::invalid_argument("Invalid magazine capacity, shot cost or initial ammo");
    }
    set_projectiles_per_shot(definition_.projectiles_per_shot);
    set_reload_mode(definition_.reload_mode);
    const auto& actions = weapon_.definition().actions();
    const auto action = std::find_if(actions.begin(), actions.end(), [&](const auto& value) {
        return value.id == definition_.action_id;
    });
    if (action == actions.end()) {
        throw std::invalid_argument("Firing action must exist in the weapon definition");
    }
    const auto reload_index = [&](std::optional<ActionId> id) -> std::optional<std::size_t> {
        if (!id) return std::nullopt;
        const auto reload = std::find_if(actions.begin(), actions.end(), [&](const auto& value) {
            return value.id == *id;
        });
        if (reload == actions.end() || reload->id == definition_.action_id) {
            throw std::invalid_argument("Reload action must exist and differ from the firing action");
        }
        return static_cast<std::size_t>(reload - actions.begin());
    };
    reload_action_index_ = reload_index(definition_.reload_action_id);
    round_reload_action_index_ = reload_index(definition_.round_reload_action_id);
}

const WeaponDefinition& FiringMechanism::weapon_definition() const noexcept {
    return weapon_.definition();
}

const FiringDefinition& FiringMechanism::definition() const noexcept { return definition_; }

void FiringMechanism::set_projectiles_per_shot(std::uint32_t count) {
    if (count == 0) throw std::invalid_argument("Projectile count must be positive");
    projectiles_per_shot_ = count;
}

bool FiringMechanism::set_fire_mode(FireMode mode) noexcept {
    if (!fire_mode_settings_.supports(mode)) return false;
    if (fire_mode_ != mode) {
        interrupt_trigger();
        fire_mode_ = mode;
    }
    return true;
}

void FiringMechanism::set_fire_mode_settings(FireModeSettings settings) noexcept {
    if (!settings.supports(fire_mode_)) {
        interrupt_trigger();
        fire_mode_ = settings.initial_mode();
    } else if (fire_mode_ == FireMode::Burst && settings.burst_size() != fire_mode_settings_.burst_size()) {
        interrupt_trigger();
    }
    fire_mode_settings_ = std::move(settings);
}

void FiringMechanism::interrupt_trigger() noexcept {
    burst_shots_remaining_ = 0;
    if (trigger_held_) requires_trigger_release_ = true;
}

void FiringMechanism::set_reload_mode(ReloadMode mode) {
    if (mode != ReloadMode::Magazine && mode != ReloadMode::PerRound)
        throw std::invalid_argument("Unknown reload mode");
    reload_mode_ = mode;
}

FiringState FiringMechanism::state() const noexcept {
    ReloadState reload;
    const auto weapon = weapon_.state();
    reload.mode = reload_mode_;
    if (reload_source_) {
        const auto& action = weapon_.definition().actions()[active_reload_action_index_];
        reload.mode = active_reload_mode_;
        reload.rounds_inserted = rounds_inserted_;
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
    return {weapon, trigger_held_, rounds_remaining_, cooldown_remaining_, reload, requires_trigger_release_,
            fire_mode_, burst_shots_remaining_};
}

std::vector<FiringEvent> FiringMechanism::execute(TriggerCommand command) {
    std::vector<FiringEvent> events;
    switch (command) {
    case TriggerCommand::Press:
        if (!trigger_held_) {
            trigger_held_ = true;
            if (reload_source_) requires_trigger_release_ = true;
            if (fire_mode_ != FireMode::Burst) {
                try_start(events);
            } else if (burst_shots_remaining_ == 0) {
                const auto count = std::min(fire_mode_settings_.burst_size(), rounds_remaining_ / definition_.rounds_per_shot);
                if (try_start(events)) burst_shots_remaining_ = count - 1;
            }
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
        if (trigger_held_ && fire_mode_ == FireMode::Automatic) {
            try_start(events);
        } else if (burst_shots_remaining_ > 0 && try_start(events)) {
            --burst_shots_remaining_;
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
        burst_shots_remaining_ = 0;
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

bool FiringMechanism::try_start(std::vector<FiringEvent>& events) {
    if (reload_source_ || requires_trigger_release_ || weapon_.state().phase != Phase::Ready || cooldown_remaining_ > 0.0) {
        return false;
    }
    if (rounds_remaining_ < definition_.rounds_per_shot) {
        if (!dry_fire_reported_) {
            events.emplace_back(DryFireEvent{definition_.action_id, weapon_.state().time_seconds});
            dry_fire_reported_ = true;
        }
        return false;
    }
    const auto result = weapon_.execute(WeaponCommand::start(definition_.action_id));
    if (!result.accepted()) {
        throw std::logic_error("Ready firing mechanism could not start its configured action");
    }
    cooldown_remaining_ = definition_.shot_interval_seconds;
    cooldown_rounding_error_ = 0.0;
    append_events(result.events, events);
    return true;
}

void FiringMechanism::append_events(const std::vector<WeaponEvent>& events,
                                   std::vector<FiringEvent>& output) {
    for (const auto& event : events) {
        output.emplace_back(event);
        if (event.action_id == definition_.action_id && event.type == EventType::PhaseEntered && event.phase == Phase::Active) {
            rounds_remaining_ -= definition_.rounds_per_shot;
            output.emplace_back(ShotFiredEvent{event.action_id, event.time_seconds,
                                               definition_.rounds_per_shot, rounds_remaining_, projectiles_per_shot_});
        }
        if (!reload_running_ || event.action_id != weapon_.definition().actions()[active_reload_action_index_].id) continue;
        if (event.type == EventType::ActionStarted && !reload_started_) {
            reload_started_ = true;
            output.emplace_back(ReloadEvent{ReloadEventType::Started, event.time_seconds, 0, rounds_remaining_});
        } else if (event.type == EventType::ActionCompleted) {
            complete_reload_step(event.time_seconds, output);
        } else if (event.type == EventType::ActionCancelled) {
            reload_source_.reset();
            reload_running_ = false;
            output.emplace_back(ReloadEvent{ReloadEventType::Cancelled, event.time_seconds, 0, rounds_remaining_});
        }
    }
}

void FiringMechanism::complete_reload_step(double time_seconds, std::vector<FiringEvent>& events) {
    reload_running_ = false;
    const bool per_round = active_reload_mode_ == ReloadMode::PerRound;
    const auto requested = per_round ? 1u : definition_.magazine_capacity - rounds_remaining_;
    const auto transferred = reload_source_->take_up_to(requested);
    if (transferred > requested) {
        reload_source_.reset();
        throw std::logic_error("Ammunition source returned more rounds than requested");
    }
    rounds_remaining_ += transferred;
    if (per_round && transferred != 0) {
        ++rounds_inserted_;
        events.emplace_back(ReloadEvent{ReloadEventType::RoundInserted, time_seconds, transferred, rounds_remaining_});
        if (rounds_remaining_ < definition_.magazine_capacity && reload_source_->available_rounds() > 0) return;
    }
    reload_source_.reset();
    events.emplace_back(ReloadEvent{ReloadEventType::Completed, time_seconds, per_round ? 0u : transferred, rounds_remaining_});
}

ReloadCommandResult FiringMechanism::request_reload(std::shared_ptr<AmmunitionSource> source) {
    const auto index = reload_mode_ == ReloadMode::PerRound && round_reload_action_index_
        ? round_reload_action_index_ : reload_action_index_;
    if (!index) return {ReloadStatus::Unsupported, {}};
    if (reload_source_) return {ReloadStatus::Busy, {}};
    if (!source) return {ReloadStatus::NoSource, {}};
    // A committed windup will spend its round before the queued reload starts.
    if (rounds_remaining_ == definition_.magazine_capacity && weapon_.state().phase != Phase::Windup)
        return {ReloadStatus::MagazineFull, {}};
    if (source->available_rounds() == 0) return {ReloadStatus::NoAmmunition, {}};
    active_reload_action_index_ = *index;
    active_reload_mode_ = reload_mode_;
    reload_started_ = false;
    rounds_inserted_ = 0;
    reload_source_ = std::move(source);
    burst_shots_remaining_ = 0;
    requires_trigger_release_ = trigger_held_;
    dry_fire_reported_ = false;
    ReloadCommandResult result{ReloadStatus::Accepted, {}};
    begin_reload_if_ready(result.events);
    return result;
}

void FiringMechanism::begin_reload_if_ready(std::vector<FiringEvent>& events) {
    while (reload_source_ && !reload_running_ && weapon_.state().phase == Phase::Ready) {
        reload_running_ = true;
        const auto action_id = weapon_.definition().actions()[active_reload_action_index_].id;
        const auto result = weapon_.execute(WeaponCommand::start(action_id));
        if (!result.accepted()) throw std::logic_error("Ready weapon could not start its reload action");
        append_events(result.events, events);
    }
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
