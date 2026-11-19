#pragma once

#include <weapon_library/FiringMechanism.h>
#include <weapon_library/Ballistics.h>
#include <weapon_library/WeaponHandling.h>
#include <weapon_library/WeaponPresentation.h>
#include <weapon_library/MeleeMechanism.h>

#include <array>
#include <memory>
#include <vector>

namespace weapon_range {

using Vec3 = weapon_library::ballistics::Vec3;
constexpr unsigned firearm_count = 3, weapon_count = 4, knife_index = 3;
Vec3 view_direction(double yaw, double pitch);

struct WaterVolume { Vec3 min{1.7, 0.0, -25.0}, max{4.3, 3.2, -19.0}; };
const WaterVolume& water_volume();
struct SolidBox { Vec3 min{2.0, 0.0, 0.0}, max{4.0, 1.5, 0.8}; };
const SolidBox& firing_cover();

struct WeaponPose { Vec3 origin, muzzle, forward, right, up; };

struct Target {
    Vec3 center;
    double radius = 0.9;
    unsigned hits = 0;
    double last_hit_time = -10.0;
};

struct ShotTrace {
    Vec3 from, to;
    double time_seconds;
    bool hit;
    bool water;
};

struct RangeProjectile {
    weapon_library::ballistics::Projectile motion;
    double launched_at;
    bool passed_water = false;
};

enum class ShotOutcome { None, Target, Floor, Ceiling, Wall, Cover, Expired };

struct ShotResult {
    ShotOutcome outcome = ShotOutcome::None;
    Vec3 position, normal;
    double time_seconds = -10.0;
    double flight_seconds = 0.0;
    double speed_m_s = 0.0;
    bool passed_water = false;
    bool melee = false;
};

struct WeaponSpec {
    const char* name;
    weapon_library::FireMode mode;
    double interval_seconds;
    unsigned capacity;
    double muzzle_speed_m_s;
    double mass_kg;
    double cross_section_m2;
    weapon_library::HandlingDefinition handling;
    unsigned reserve_rounds = 0;
    double reload_seconds = 1.5;
};

const std::array<WeaponSpec, 3>& weapon_specs();

// Demo game rules only. This module has no raylib or platform dependency.
class RangeSimulation {
public:
    RangeSimulation();
    void reset();
    void select_weapon(unsigned index);
    void set_trigger(bool held);
    weapon_library::ReloadStatus request_reload();
    weapon_library::ReloadStatus cancel_reload();
    weapon_library::CommandStatus cancel_attack();
    void set_aiming(bool aiming);
    void set_movement(double forward, double strafe);
    void look(double yaw_delta, double pitch_delta);
    void advance(double seconds, double forward = 0.0, double strafe = 0.0);

    Vec3 eye() const noexcept { return eye_; }
    Vec3 aim() const;
    Vec3 shot_center_direction() const;
    WeaponPose weapon_pose() const;
    void set_presentation(unsigned index, weapon_library::WeaponPresentation presentation);
    void set_aim_sway(unsigned index, weapon_library::AimSwaySettings settings);
    const weapon_library::WeaponPresentation& presentation() const { return presentation_[selected_]; }
    weapon_library::PresentationPose sway_pose() const;
    weapon_library::HandlingState handling_state() const;
    double time() const noexcept { return time_; }
    unsigned selected_weapon() const noexcept { return selected_; }
    bool is_melee() const noexcept { return selected_ == knife_index; }
    weapon_library::MeleeState melee_state() const noexcept { return melee_->state(); }
    const weapon_library::MeleeDefinition& melee_definition() const noexcept { return melee_->definition(); }
    unsigned attacks() const noexcept { return attacks_; }
    unsigned melee_hits() const noexcept { return melee_hits_; }
    weapon_library::FiringState weapon_state() const;
    unsigned reserve_rounds() const noexcept { return is_melee() ? 0 : reserves_[selected_]->available_rounds(); }
    weapon_library::ReloadStatus last_reload_status() const noexcept { return last_reload_status_; }
    double last_reload_notice_time() const noexcept { return last_reload_notice_time_; }
    const std::array<Target, 4>& targets() const noexcept { return targets_; }
    const std::vector<ShotTrace>& traces() const noexcept { return traces_; }
    const std::vector<RangeProjectile>& projectiles() const noexcept { return projectiles_; }
    const std::vector<ShotResult>& recent_results() const noexcept { return recent_results_; }
    const ShotResult& last_result() const noexcept { return last_result_; }
    unsigned shots() const noexcept { return shots_; }
    unsigned hits() const noexcept { return hits_; }
    unsigned score() const noexcept { return score_; }
    double last_shot_time() const noexcept { return last_shot_time_; }
    double last_hit_time() const noexcept { return last_hit_time_; }
    double last_dry_time() const noexcept { return last_dry_time_; }
    double last_flight_seconds() const noexcept { return last_result_.flight_seconds; }
    double last_impact_speed() const noexcept { return last_result_.speed_m_s; }
    double last_impact_time() const noexcept { return last_result_.time_seconds; }
    unsigned water_entries() const noexcept { return water_entries_; }

private:
    struct PendingShot { double time; unsigned weapon_index; };
    void consume(const std::vector<weapon_library::FiringEvent>& events, unsigned weapon_index,
                 std::vector<PendingShot>* pending = nullptr);
    void shoot(double time, unsigned weapon_index);
    void consume_melee(const std::vector<weapon_library::MeleeEvent>& events);
    void advance_melee(double seconds);
    void advance_projectiles(double seconds);
    void move(double seconds);
    void advance_handling(double seconds);
    WeaponPose weapon_pose(unsigned weapon_index) const;

    std::array<std::unique_ptr<weapon_library::FiringMechanism>, 3> weapons_;
    std::array<std::unique_ptr<weapon_library::WeaponHandling>, weapon_count> handling_;
    std::array<std::shared_ptr<weapon_library::AmmunitionReserve>, 3> reserves_;
    std::array<weapon_library::WeaponPresentation, weapon_count> presentation_;
    std::array<weapon_library::AimSwaySettings, weapon_count> aim_sway_;
    weapon_library::WeaponSway sway_;
    std::unique_ptr<weapon_library::MeleeMechanism> melee_;
    std::array<Target, 4> targets_;
    std::vector<ShotTrace> traces_;
    std::vector<RangeProjectile> projectiles_;
    std::vector<ShotResult> recent_results_;
    ShotResult last_result_;
    Vec3 eye_;
    Vec3 movement_velocity_;
    double yaw_ = 0.0, pitch_ = 0.0, time_ = 0.0;
    unsigned selected_ = 0, shots_ = 0, hits_ = 0, score_ = 0;
    double last_shot_time_ = -10.0, last_hit_time_ = -10.0, last_dry_time_ = -10.0;
    unsigned water_entries_ = 0;
    bool aiming_ = false;
    bool melee_trigger_held_ = false;
    unsigned attacks_ = 0, melee_hits_ = 0;
    weapon_library::ReloadStatus last_reload_status_ = weapon_library::ReloadStatus::Accepted;
    double last_reload_notice_time_ = -10.0;
};

} // namespace weapon_range
