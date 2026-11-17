#pragma once

#include <weapon_library/Math.h>

#include <cstdint>
#include <functional>

namespace weapon_library {

struct AngularOffset {
    double yaw_rad = 0.0;
    double pitch_rad = 0.0; // Positive pitches the aim upwards.
};

struct HandlingDefinition {
    double aim_in_seconds = 0.2;
    double aim_out_seconds = 0.15;
    double hip_spread_rad = 0.018;
    double aimed_spread_rad = 0.002;
    double movement_spread_rad = 0.012;
    double aimed_movement_multiplier = 0.5;
    double bloom_per_shot_rad = 0.003;
    double max_bloom_rad = 0.035;
    double bloom_recovery_rad_per_second = 0.025;
    AngularOffset recoil_per_shot{0.0, 0.008};
    double recoil_yaw_jitter_rad = 0.002;
    double aimed_recoil_multiplier = 0.65;
    AngularOffset max_recoil_rad{0.06, 0.14};
    double recoil_return_rate = 6.0; // Exponential decay rate, per second.
};

struct HandlingState {
    double time_seconds = 0.0;
    bool aiming = false;
    double aim_fraction = 0.0;
    double movement_factor = 0.0; // Host-defined, in [0, 1].
    double bloom_rad = 0.0;
    AngularOffset recoil;
    double spread_half_angle_rad = 0.0;
};

struct SpreadContext {
    const HandlingDefinition& definition;
    HandlingState handling; // Pre-shot snapshot; spread_half_angle_rad contains the built-in result.
    double default_half_angle_rad;
};

// Pure evaluation: return a finite cone half-angle in [0, pi/2), in radians.
// Capture game-owned factors; do not mutate/reenter the handling instance or consume RNG here.
using SpreadModel = std::function<double(const SpreadContext&)>;

struct HandledShot {
    math::Vec3 direction; // Unit direction, including pre-shot recoil and spread.
    double time_seconds;
    double spread_half_angle_rad;
    AngularOffset recoil_before;
    AngularOffset recoil_after;
};

// All angles are radians. Independent of input devices, cameras and ballistics.
// The host calls sample_shot exactly once per actual ShotFiredEvent, at its time.
class WeaponHandling {
public:
    explicit WeaponHandling(HandlingDefinition definition = {}, std::uint32_t seed = 1, SpreadModel spread_model = {});
    const HandlingDefinition& definition() const noexcept { return definition_; }
    HandlingState state() const;
    // An empty model restores the built-in formula. Invalid replacements leave the previous model installed.
    void set_spread_model(SpreadModel model);
    void set_aiming(bool aiming) noexcept;
    void set_movement_factor(double factor);
    void advance(double seconds);

    // A finite nonzero up hint establishes local yaw/pitch; parallel hints get
    // a stable fallback basis. Invalid directions do not consume RNG or kick.
    math::Vec3 recoiled_direction(math::Vec3 aim, math::Vec3 up_hint = {0, 1, 0}) const;
    HandledShot sample_shot(math::Vec3 aim, math::Vec3 up_hint = {0, 1, 0});

private:
    double spread(const SpreadModel& model) const;
    double random_unit() noexcept;
    HandlingDefinition definition_;
    HandlingState state_;
    std::uint32_t random_state_;
    SpreadModel spread_model_;
};

} // namespace weapon_library
