#include <weapon_library/WeaponHandling.h>

#include "Time.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace weapon_library {
namespace {
constexpr double pi = 3.14159265358979323846;
struct Basis { math::Vec3 forward, right, up; };

Basis basis(math::Vec3 aim, math::Vec3 hint) {
    const auto forward = math::normalized(aim);
    hint = math::normalized(hint);
    auto right = math::cross(forward, hint);
    if (math::length(right) < 1e-8) {
        right = math::cross(forward, std::abs(forward.x) < 0.9 ? math::Vec3{1, 0, 0} : math::Vec3{0, 0, 1});
    }
    right = math::normalized(right);
    return {forward, right, math::cross(right, forward)};
}

Basis kicked(Basis b, AngularOffset offset) {
    const auto forward = b.forward * std::cos(offset.yaw_rad) + b.right * std::sin(offset.yaw_rad);
    const auto right = b.right * std::cos(offset.yaw_rad) - b.forward * std::sin(offset.yaw_rad);
    return {forward * std::cos(offset.pitch_rad) + b.up * std::sin(offset.pitch_rad), right,
            b.up * std::cos(offset.pitch_rad) - forward * std::sin(offset.pitch_rad)};
}
bool multiplier(double value) { return std::isfinite(value) && value >= 0.0 && value <= 1.0; }
}

WeaponHandling::WeaponHandling(HandlingDefinition definition, std::uint32_t seed)
    : definition_(definition), random_state_(seed == 0 ? 0x6d2b79f5u : seed) {
    const double nonnegative[] = {definition.aim_in_seconds, definition.aim_out_seconds,
        definition.hip_spread_rad, definition.aimed_spread_rad, definition.movement_spread_rad,
        definition.bloom_per_shot_rad, definition.max_bloom_rad, definition.bloom_recovery_rad_per_second,
        definition.recoil_yaw_jitter_rad, definition.max_recoil_rad.yaw_rad,
        definition.max_recoil_rad.pitch_rad, definition.recoil_return_rate};
    for (double value : nonnegative) {
        if (!detail::valid_duration(value)) throw std::invalid_argument("Handling values must be finite and nonnegative");
    }
    if (!multiplier(definition.aimed_movement_multiplier) || !multiplier(definition.aimed_recoil_multiplier) ||
        !std::isfinite(definition.recoil_per_shot.yaw_rad) || !std::isfinite(definition.recoil_per_shot.pitch_rad) ||
        std::max(definition.hip_spread_rad, definition.aimed_spread_rad) +
            definition.movement_spread_rad + definition.max_bloom_rad >= pi * 0.5 ||
        definition.max_recoil_rad.yaw_rad >= pi * 0.5 || definition.max_recoil_rad.pitch_rad >= pi * 0.5) {
        throw std::invalid_argument("Invalid handling multipliers, recoil or spread cone");
    }
}

double WeaponHandling::spread() const noexcept {
    const auto& d = definition_;
    const double aimed = state_.aim_fraction;
    return d.hip_spread_rad * (1.0 - aimed) + d.aimed_spread_rad * aimed + state_.bloom_rad +
        d.movement_spread_rad * state_.movement_factor * (1.0 + (d.aimed_movement_multiplier - 1.0) * aimed);
}

HandlingState WeaponHandling::state() const noexcept {
    auto snapshot = state_;
    snapshot.spread_half_angle_rad = spread();
    return snapshot;
}

void WeaponHandling::set_aiming(bool aiming) noexcept {
    state_.aiming = aiming;
    if (aiming && definition_.aim_in_seconds == 0.0) state_.aim_fraction = 1.0;
    if (!aiming && definition_.aim_out_seconds == 0.0) state_.aim_fraction = 0.0;
}

void WeaponHandling::set_movement_factor(double factor) {
    if (!multiplier(factor)) throw std::invalid_argument("Movement factor must be in [0, 1]");
    state_.movement_factor = factor;
}

void WeaponHandling::advance(double seconds) {
    if (!detail::valid_duration(seconds) || !std::isfinite(state_.time_seconds + seconds)) {
        throw std::invalid_argument("Handling time must be finite, nonnegative and not overflow");
    }
    if (state_.aiming) {
        state_.aim_fraction = definition_.aim_in_seconds == 0.0 ? 1.0 :
            std::min(1.0, state_.aim_fraction + seconds / definition_.aim_in_seconds);
    } else {
        state_.aim_fraction = definition_.aim_out_seconds == 0.0 ? 0.0 :
            std::max(0.0, state_.aim_fraction - seconds / definition_.aim_out_seconds);
    }
    state_.bloom_rad = std::max(0.0, state_.bloom_rad - definition_.bloom_recovery_rad_per_second * seconds);
    const double decay = std::exp(-definition_.recoil_return_rate * seconds);
    state_.recoil.yaw_rad *= decay;
    state_.recoil.pitch_rad *= decay;
    state_.time_seconds += seconds;
}

double WeaponHandling::random_unit() noexcept {
    // Fixed xorshift32 and explicit 24-bit conversion; no STL distribution or frame RNG.
    random_state_ ^= random_state_ << 13;
    random_state_ ^= random_state_ >> 17;
    random_state_ ^= random_state_ << 5;
    return static_cast<double>(random_state_ >> 8) / 16777216.0;
}

math::Vec3 WeaponHandling::recoiled_direction(math::Vec3 aim, math::Vec3 up_hint) const {
    return kicked(basis(aim, up_hint), state_.recoil).forward;
}

HandledShot WeaponHandling::sample_shot(math::Vec3 aim, math::Vec3 up_hint) {
    const auto b = kicked(basis(aim, up_hint), state_.recoil); // Validate before state/RNG changes.
    const double cone = spread();
    // Uniform solid-angle cone around the current aim, independent of the renderer.
    const double sin_half_angle = std::sqrt(random_unit()) * std::sin(cone * 0.5);
    const double cos_angle = 1.0 - 2.0 * sin_half_angle * sin_half_angle;
    const double sin_angle = 2.0 * sin_half_angle * std::sqrt(std::max(0.0, 1.0 - sin_half_angle * sin_half_angle));
    const double azimuth = 2.0 * pi * random_unit();
    const auto direction = math::normalized(b.forward * cos_angle +
        (b.right * std::cos(azimuth) + b.up * std::sin(azimuth)) * sin_angle);
    const double recoil_scale = 1.0 + (definition_.aimed_recoil_multiplier - 1.0) * state_.aim_fraction;
    const auto before = state_.recoil;
    const double yaw_kick = definition_.recoil_per_shot.yaw_rad +
        (random_unit() * 2.0 - 1.0) * definition_.recoil_yaw_jitter_rad;
    state_.recoil.yaw_rad = std::clamp(state_.recoil.yaw_rad + yaw_kick * recoil_scale,
                                     -definition_.max_recoil_rad.yaw_rad, definition_.max_recoil_rad.yaw_rad);
    state_.recoil.pitch_rad = std::clamp(state_.recoil.pitch_rad + definition_.recoil_per_shot.pitch_rad * recoil_scale,
                                       -definition_.max_recoil_rad.pitch_rad, definition_.max_recoil_rad.pitch_rad);
    state_.bloom_rad = std::min(definition_.max_bloom_rad, state_.bloom_rad + definition_.bloom_per_shot_rad);
    return {direction, state_.time_seconds, cone, before, state_.recoil};
}

} // namespace weapon_library
