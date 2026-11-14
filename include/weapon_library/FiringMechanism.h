#pragma once

#include <weapon_library/Weapon.h>

#include <variant>

namespace weapon_library {

enum class FireMode { SemiAutomatic, Automatic };

struct FiringDefinition {
    ActionId action_id = 0;
    FireMode mode = FireMode::SemiAutomatic;
    // Minimum interval between accepted firing action starts, in seconds.
    double shot_interval_seconds = 0.1;
    std::uint32_t magazine_capacity = 1;
    std::uint32_t rounds_per_shot = 1;
};

enum class TriggerCommand { Press, Release };

struct ShotFiredEvent {
    ActionId action_id;
    double time_seconds;
    std::uint32_t rounds_consumed;
    std::uint32_t rounds_remaining;
};

struct DryFireEvent {
    ActionId action_id;
    double time_seconds;
};

// One ordered stream: activation precedes its shot, which precedes recovery.
using FiringEvent = std::variant<WeaponEvent, ShotFiredEvent, DryFireEvent>;

struct FiringState {
    WeaponState weapon;
    bool trigger_held;
    std::uint32_t rounds_remaining;
    double cooldown_remaining_seconds;
};

struct FiringCancelResult {
    CommandStatus status;
    std::vector<FiringEvent> events;

    bool accepted() const noexcept { return status == CommandStatus::Accepted; }
};

// Owns its action runtime so no outside command can bypass cadence or ammo rules.
class FiringMechanism {
public:
    FiringMechanism(std::shared_ptr<const WeaponDefinition> weapon_definition,
                    FiringDefinition firing_definition,
                    std::uint32_t initial_rounds = 0);

    const WeaponDefinition& weapon_definition() const noexcept;
    const FiringDefinition& definition() const noexcept;
    FiringState state() const noexcept;

    // Press/Release are edges. Repeated Press while held is harmless.
    // Semi-auto does not queue presses during cooldown or a running action.
    std::vector<FiringEvent> execute(TriggerCommand command);
    // Automatic mode fires at every eligible boundary, including the end of a step.
    std::vector<FiringEvent> advance(double seconds);
    // An accepted cancellation releases the trigger; blocked cancellation changes nothing.
    FiringCancelResult cancel_action();

private:
    void try_start(std::vector<FiringEvent>& events);
    void append_events(const std::vector<WeaponEvent>& events,
                       std::vector<FiringEvent>& output);

    Weapon weapon_;
    const FiringDefinition definition_;
    bool trigger_held_ = false;
    bool dry_fire_reported_ = false;
    std::uint32_t rounds_remaining_;
    double cooldown_remaining_ = 0.0;
    double cooldown_rounding_error_ = 0.0;
};

} // namespace weapon_library
