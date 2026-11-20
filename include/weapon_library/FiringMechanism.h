#pragma once

#include <weapon_library/Weapon.h>
#include <weapon_library/Ammunition.h>

#include <variant>
#include <vector>

namespace weapon_library {

enum class FireMode { SemiAutomatic, Automatic, Burst };
enum class ReloadMode { Magazine, PerRound };

class FireModeSettings {
public:
    explicit FireModeSettings(FireMode initial = FireMode::SemiAutomatic,
                              std::vector<FireMode> available = {}, std::uint32_t burst_size = 3);
    FireMode initial_mode() const noexcept { return initial_; }
    const std::vector<FireMode>& available_modes() const noexcept { return available_; }
    std::uint32_t burst_size() const noexcept { return burst_size_; }
    bool supports(FireMode mode) const noexcept;

private:
    FireMode initial_;
    std::vector<FireMode> available_;
    std::uint32_t burst_size_;
};

struct FiringDefinition {
    ActionId action_id = 0;
    FireMode mode = FireMode::SemiAutomatic;
    // Minimum interval between accepted firing action starts, in seconds.
    double shot_interval_seconds = 0.1;
    std::uint32_t magazine_capacity = 1;
    std::uint32_t rounds_per_shot = 1;
    std::optional<ActionId> reload_action_id; // Must differ from the firing action.
    std::uint32_t projectiles_per_shot = 1; // Independent of magazine cost.
    std::vector<FireMode> available_modes; // Empty means only mode is available.
    std::uint32_t burst_size = 3;
    ReloadMode reload_mode = ReloadMode::Magazine;
    std::optional<ActionId> round_reload_action_id; // PerRound uses reload_action_id when omitted.
};

enum class TriggerCommand { Press, Release };

struct ShotFiredEvent {
    ActionId action_id;
    double time_seconds;
    std::uint32_t rounds_consumed;
    std::uint32_t rounds_remaining;
    std::uint32_t projectile_count = 1;
};

struct DryFireEvent {
    ActionId action_id;
    double time_seconds;
};

enum class ReloadEventType { Started, Completed, Cancelled, RoundInserted };

struct ReloadEvent {
    ReloadEventType type;
    double time_seconds;
    std::uint32_t rounds_transferred = 0;
    std::uint32_t rounds_remaining = 0;
};

enum class ReloadStatus {
    Accepted, Unsupported, Busy, MagazineFull, NoAmmunition, NoSource,
    NotReloading, CancellationBlocked
};

struct ReloadState {
    bool active = false; // Includes a request waiting for the current shot to finish.
    bool waiting_for_action = false;
    double duration_seconds = 0.0; // In PerRound mode, both times refer to the current insertion.
    double remaining_seconds = 0.0;
    ReloadMode mode = ReloadMode::Magazine;
    std::uint32_t rounds_inserted = 0;
};

// One ordered stream: activation precedes its shot, which precedes recovery.
using FiringEvent = std::variant<WeaponEvent, ShotFiredEvent, DryFireEvent, ReloadEvent>;

struct FiringState {
    WeaponState weapon;
    bool trigger_held;
    std::uint32_t rounds_remaining;
    double cooldown_remaining_seconds;
    ReloadState reload;
    bool requires_trigger_release = false;
    FireMode fire_mode = FireMode::SemiAutomatic;
    std::uint32_t burst_shots_remaining = 0; // Excludes any action already started.
};

struct ReloadCommandResult {
    ReloadStatus status;
    std::vector<FiringEvent> events;
    bool accepted() const noexcept { return status == ReloadStatus::Accepted; }
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
    std::uint32_t projectiles_per_shot() const noexcept { return projectiles_per_shot_; }
    // Updates the next emitted shot. The immutable definition remains the initial preset.
    void set_projectiles_per_shot(std::uint32_t count);
    const FireModeSettings& fire_mode_settings() const noexcept { return fire_mode_settings_; }
    bool set_fire_mode(FireMode mode) noexcept; // Returns false for an unavailable mode.
    // Keeps the active mode when supported. The initial mode is used as fallback.
    void set_fire_mode_settings(FireModeSettings settings) noexcept;
    // Stops unstarted shots and requires release if the trigger is still held.
    void interrupt_trigger() noexcept;
    ReloadMode reload_mode() const noexcept { return reload_mode_; }
    // Configures the next accepted reload request.
    void set_reload_mode(ReloadMode mode);

    // Press/Release are edges. Repeated Press while held is harmless.
    // Semi and burst do not queue presses during cooldown or a running action.
    // Burst finishes the accepted group after Release.
    std::vector<FiringEvent> execute(TriggerCommand command);
    // Automatic and pending burst shots fire at eligible boundaries, including the end of a step.
    std::vector<FiringEvent> advance(double seconds);
    // An accepted cancellation releases the trigger; blocked cancellation changes nothing.
    FiringCancelResult cancel_action();
    // May wait for the committed shot/recovery. Source stays owned until completion
    // or cancellation. Rounds transfer at completion of the reload action or insertion action.
    ReloadCommandResult request_reload(std::shared_ptr<AmmunitionSource> source);
    ReloadCommandResult cancel_reload();

private:
    bool try_start(std::vector<FiringEvent>& events);
    void append_events(const std::vector<WeaponEvent>& events,
                       std::vector<FiringEvent>& output);
    void begin_reload_if_ready(std::vector<FiringEvent>& events);
    void complete_reload_step(double time_seconds, std::vector<FiringEvent>& events);

    Weapon weapon_;
    const FiringDefinition definition_;
    FireModeSettings fire_mode_settings_;
    FireMode fire_mode_;
    std::uint32_t burst_shots_remaining_ = 0;
    bool trigger_held_ = false;
    bool dry_fire_reported_ = false;
    std::uint32_t rounds_remaining_;
    std::uint32_t projectiles_per_shot_ = 1;
    double cooldown_remaining_ = 0.0;
    double cooldown_rounding_error_ = 0.0;
    std::shared_ptr<AmmunitionSource> reload_source_;
    bool reload_running_ = false;
    bool requires_trigger_release_ = false;
    std::optional<std::size_t> reload_action_index_;
    std::optional<std::size_t> round_reload_action_index_;
    std::size_t active_reload_action_index_ = 0;
    ReloadMode reload_mode_ = ReloadMode::Magazine;
    ReloadMode active_reload_mode_ = ReloadMode::Magazine;
    bool reload_started_ = false;
    std::uint32_t rounds_inserted_ = 0;
};

} // namespace weapon_library
