#pragma once

#include <weapon_library/FiringMechanism.h>
#include <weapon_library/Ballistics.h>
#include <weapon_library/WeaponHandling.h>

#include <array>
#include <memory>
#include <vector>

namespace weapon_range {

using Vec3 = weapon_library::ballistics::Vec3;
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
};

const std::array<WeaponSpec, 3>& weapon_specs();

// Demo game rules only. This module has no raylib or platform dependency.
class RangeSimulation {
public:
    RangeSimulation();
    void reset();
    void select_weapon(unsigned index);
    void set_trigger(bool held);
    void set_aiming(bool aiming);
    void set_movement(double forward, double strafe);
    void look(double yaw_delta, double pitch_delta);
    void advance(double seconds, double forward = 0.0, double strafe = 0.0);

    Vec3 eye() const noexcept { return eye_; }
    Vec3 aim() const;
    WeaponPose weapon_pose() const;
    weapon_library::HandlingState handling_state() const;
    double time() const noexcept { return time_; }
    unsigned selected_weapon() const noexcept { return selected_; }
    weapon_library::FiringState weapon_state() const;
    const std::array<Target, 3>& targets() const noexcept { return targets_; }
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
    void advance_projectiles(double seconds);
    void move(double seconds);
    void advance_handling(double seconds);
    WeaponPose weapon_pose(unsigned weapon_index) const;

    std::array<std::unique_ptr<weapon_library::FiringMechanism>, 3> weapons_;
    std::array<std::unique_ptr<weapon_library::WeaponHandling>, 3> handling_;
    std::array<Target, 3> targets_;
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
};

} // namespace weapon_range
