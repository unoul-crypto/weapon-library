#pragma once

#include "Math.h"
#include "Weapon.h"
#include "HitContact.h"

#include <unordered_set>
#include <variant>

namespace weapon_library {

using MeleeAttackId = std::uint64_t;

struct MeleeDefinition {
    ActionId action_id = 0;
    double reach_m = 1.6;
    double sweep_radius_m = 0.08;
    double arc_radians = 1.4; // Horizontal sweep from -arc/2 to +arc/2.
    std::uint32_t max_targets = 0; // Zero means unlimited; blocking contacts still stop the swing.
};

// The game supplies attacker transforms at these simulation times and sweeps its
// collision geometry over this portion of the active phase. No engine types enter the core.
struct MeleeSweep {
    ActionId action_id;
    MeleeAttackId attack_id;
    double from_time_seconds, to_time_seconds;
    double from_progress, to_progress;
    double reach_m, radius_m, arc_radians;
};

struct MeleeContact {
    std::uint64_t target_id;
    double fraction; // Time fraction within the requested sweep, in [0, 1].
    math::Vec3 position, normal;
    bool blocking = true;
};

class MeleeWorld {
public:
    virtual ~MeleeWorld() = default;
    // Return unoccluded contacts, including solid cover. Equal-time contacts must
    // be ordered near-to-far along the blade. Stable entity IDs deduplicate multiple
    // colliders/updates. Do not reenter the mechanism or mutate it from this callback.
    virtual std::vector<MeleeContact> sweep(const MeleeSweep& query) const = 0;
};

struct MeleeHitEvent {
    ActionId action_id;
    MeleeAttackId attack_id;
    std::uint64_t target_id;
    double time_seconds;
    math::Vec3 position, normal;
    bool blocking;

    HitContact hit_contact() const noexcept {
        return {HitKind::Melee, target_id, time_seconds, position, normal};
    }
};
using MeleeEvent = std::variant<WeaponEvent, MeleeHitEvent>;
enum class MeleeCommand { Attack, Cancel };
struct MeleeCommandResult {
    CommandStatus status;
    std::vector<MeleeEvent> events;
    bool accepted() const noexcept { return status == CommandStatus::Accepted; }
};
struct MeleeState {
    WeaponState weapon;
    MeleeAttackId attack_id = 0;
    std::uint32_t targets_hit = 0;
    bool blocked = false;
    double attack_progress = 0.0; // Entire action, including windup and recovery; zero when ready.
};

class MeleeMechanism {
public:
    MeleeMechanism(std::shared_ptr<const WeaponDefinition> actions, MeleeDefinition definition);
    const MeleeDefinition& definition() const noexcept { return definition_; }
    MeleeState state() const noexcept;
    MeleeCommandResult execute(MeleeCommand command, const MeleeWorld& world);
    std::vector<MeleeEvent> advance(double seconds, const MeleeWorld& world);

private:
    std::vector<MeleeEvent> consume(const std::vector<WeaponEvent>& events, WeaponState before, const MeleeWorld& world);
    void query(double from, double to, const MeleeWorld& world, std::vector<MeleeEvent>& events);

    Weapon weapon_;
    const MeleeDefinition definition_;
    double active_seconds_ = 0.0, total_seconds_ = 0.0;
    double started_at_ = 0.0, active_at_ = 0.0;
    MeleeAttackId attack_id_ = 0;
    std::unordered_set<std::uint64_t> targets_;
    bool blocked_ = false;
};

} // namespace weapon_library
