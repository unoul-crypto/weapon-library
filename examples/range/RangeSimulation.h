#pragma once

#include <weapon_library/FiringMechanism.h>

#include <array>
#include <memory>
#include <vector>

namespace weapon_range {

struct Vec3 { double x = 0.0, y = 0.0, z = 0.0; };
Vec3 operator+(Vec3 a, Vec3 b);
Vec3 operator*(Vec3 value, double scale);
Vec3 view_direction(double yaw, double pitch);

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
};

struct WeaponSpec {
    const char* name;
    weapon_library::FireMode mode;
    double interval_seconds;
    unsigned capacity;
};

const std::array<WeaponSpec, 3>& weapon_specs();

// Demo game rules only. This module has no raylib or platform dependency.
class RangeSimulation {
public:
    RangeSimulation();
    void reset();
    void select_weapon(unsigned index);
    void set_trigger(bool held);
    void look(double yaw_delta, double pitch_delta);
    void advance(double seconds, double forward = 0.0, double strafe = 0.0);

    Vec3 eye() const noexcept { return eye_; }
    Vec3 aim() const noexcept { return view_direction(yaw_, pitch_); }
    double time() const noexcept { return time_; }
    unsigned selected_weapon() const noexcept { return selected_; }
    weapon_library::FiringState weapon_state() const;
    const std::array<Target, 3>& targets() const noexcept { return targets_; }
    const std::vector<ShotTrace>& traces() const noexcept { return traces_; }
    unsigned shots() const noexcept { return shots_; }
    unsigned hits() const noexcept { return hits_; }
    unsigned score() const noexcept { return score_; }
    double last_shot_time() const noexcept { return last_shot_time_; }
    double last_hit_time() const noexcept { return last_hit_time_; }
    double last_dry_time() const noexcept { return last_dry_time_; }

private:
    void consume(const std::vector<weapon_library::FiringEvent>& events);
    void shoot(double time);

    std::array<std::unique_ptr<weapon_library::FiringMechanism>, 3> weapons_;
    std::array<Target, 3> targets_;
    std::vector<ShotTrace> traces_;
    Vec3 eye_;
    double yaw_ = 0.0, pitch_ = 0.0, time_ = 0.0;
    unsigned selected_ = 0, shots_ = 0, hits_ = 0, score_ = 0;
    double last_shot_time_ = -10.0, last_hit_time_ = -10.0, last_dry_time_ = -10.0;
};

} // namespace weapon_range
