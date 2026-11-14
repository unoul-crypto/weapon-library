#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace weapon_library {

using ActionId = std::uint32_t;

enum class Phase { Ready, Windup, Active, Recovery };

struct CancellationPolicy {
    bool windup = true;
    bool active = false;
    bool recovery = false;
};

struct ActionDefinition {
    ActionId id = 0;
    double windup_seconds = 0.0;
    double active_seconds = 0.0;
    double recovery_seconds = 0.0;
    CancellationPolicy cancellation;
};

// Immutable configuration shared by independent weapon instances.
class WeaponDefinition {
public:
    // Rejects duplicate IDs, negative/non-finite durations and overflowing totals.
    explicit WeaponDefinition(std::vector<ActionDefinition> actions);
    const std::vector<ActionDefinition>& actions() const noexcept;

private:
    const std::vector<ActionDefinition> actions_;
};

enum class CommandType { StartAction, CancelAction };

struct WeaponCommand {
    CommandType type = CommandType::StartAction;
    ActionId action_id = 0; // Used only by StartAction.

    static WeaponCommand start(ActionId action_id) noexcept;
    static WeaponCommand cancel() noexcept;
};

enum class CommandStatus {
    Accepted,
    Busy,
    UnknownAction,
    NoActiveAction,
    CancellationBlocked
};

enum class EventType { ActionStarted, PhaseEntered, ActionCompleted, ActionCancelled };

struct WeaponEvent {
    EventType type;
    ActionId action_id;
    Phase phase;
    double time_seconds; // Simulation time of the transition, not delivery time.
};

struct CommandResult {
    CommandStatus status;
    std::vector<WeaponEvent> events;

    bool accepted() const noexcept { return status == CommandStatus::Accepted; }
};

struct WeaponState {
    Phase phase = Phase::Ready;
    std::optional<ActionId> action_id;
    double phase_remaining_seconds = 0.0;
    double time_seconds = 0.0;
};

class Weapon {
public:
    // Prefer make_shared<const WeaponDefinition>(...) to create shared definitions.
    explicit Weapon(std::shared_ptr<const WeaponDefinition> definition);

    const WeaponDefinition& definition() const noexcept;
    WeaponState state() const noexcept;

    // Commands execute at the current simulation time. Consume returned events once.
    CommandResult execute(WeaponCommand command);
    // Rejects negative/non-finite time and clock overflow before changing state.
    // Emits every crossed transition, including zero-duration phases.
    std::vector<WeaponEvent> advance(double seconds);

private:
    void enter_next_phase(std::vector<WeaponEvent>& events);
    void settle_instant_phases(std::vector<WeaponEvent>& events);
    void reset_action() noexcept;
    bool can_cancel() const noexcept;

    std::shared_ptr<const WeaponDefinition> definition_;
    std::optional<std::size_t> action_index_;
    WeaponState state_;
    double phase_rounding_error_ = 0.0;
    double phase_duration_seconds_ = 0.0;
};

} // namespace weapon_library
