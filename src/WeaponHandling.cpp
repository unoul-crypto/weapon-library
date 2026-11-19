#include <weapon_library/WeaponHandling.h>

#include "Time.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

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

Basis shot_basis(math::Vec3 aim, math::Vec3 up_hint, const HandlingState& state) {
    return kicked(kicked(basis(aim, up_hint), state.recoil), state.aim_sway);
}

bool multiplier(double value) { return std::isfinite(value) && value >= 0.0 && value <= 1.0; }
}

AimSwaySettings::AimSwaySettings(AngularOffset amplitude_rad, double frequency_hz)
    : amplitude_rad_(amplitude_rad), frequency_hz_(frequency_hz) {
    if (!detail::valid_duration(frequency_hz))
        throw std::invalid_argument("Aim sway frequency must be finite and nonnegative");
    if (!detail::valid_duration(amplitude_rad.yaw_rad) || amplitude_rad.yaw_rad >= pi * 0.5 ||
        !detail::valid_duration(amplitude_rad.pitch_rad) || amplitude_rad.pitch_rad >= pi * 0.5)
        throw std::invalid_argument("Aim sway amplitudes must be finite and in [0, pi/2) radians");
}

WeaponHandling::WeaponHandling(HandlingDefinition definition, std::uint32_t seed, SpreadModel spread_model)
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
    set_aim_sway({definition.aimed_sway_amplitude_rad, definition.aimed_sway_frequency_hz});
    set_spread_model(std::move(spread_model));
}

double WeaponHandling::spread(const SpreadModel& model) const {
    const auto& d = definition_;
    const double aimed = state_.aim_fraction;
    const double built_in = d.hip_spread_rad * (1.0 - aimed) + d.aimed_spread_rad * aimed + state_.bloom_rad +
        d.movement_spread_rad * state_.movement_factor * (1.0 + (d.aimed_movement_multiplier - 1.0) * aimed);
    if (!model) return built_in;
    auto snapshot = state_;
    snapshot.spread_half_angle_rad = built_in;
    const double result = model(SpreadContext{definition_, snapshot, built_in});
    if (!std::isfinite(result) || result < 0.0 || result >= pi * 0.5)
        throw std::invalid_argument("Spread model must return a finite half-angle in [0, pi/2)");
    return result;
}

void WeaponHandling::set_spread_model(SpreadModel model) {
    spread(model); // Validate before replacing the model; no clock, RNG or shot mutations.
    spread_model_ = std::move(model);
}

HandlingState WeaponHandling::state() const {
    auto snapshot = state_;
    snapshot.spread_half_angle_rad = spread(spread_model_);
    return snapshot;
}

void WeaponHandling::set_aiming(bool aiming) noexcept {
    state_.aiming = aiming;
    if (aiming && definition_.aim_in_seconds == 0.0) state_.aim_fraction = 1.0;
    if (!aiming && definition_.aim_out_seconds == 0.0) state_.aim_fraction = 0.0;
    update_aim_sway();
}

void WeaponHandling::set_aim_sway(AimSwaySettings settings) noexcept {
    definition_.aimed_sway_amplitude_rad = settings.amplitude_rad();
    definition_.aimed_sway_frequency_hz = settings.frequency_hz();
    update_aim_sway();
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
    update_aim_sway();
}

void WeaponHandling::update_aim_sway() noexcept {
    const auto amplitude = definition_.aimed_sway_amplitude_rad;
    const double frequency = definition_.aimed_sway_frequency_hz;
    if (state_.aim_fraction == 0.0 || frequency == 0.0 || (amplitude.yaw_rad == 0.0 && amplitude.pitch_rad == 0.0)) {
        state_.aim_sway = {};
        return;
    }
    // Wrap time before multiplying so long sessions and large finite steps keep bounded phases.
    // Pitch uses three cycles per two yaw cycles, so both axes are continuous at the wrap.
    const double phase = (std::fmod(state_.time_seconds, 2.0 / frequency) * frequency) * (2.0 * pi);
    state_.aim_sway = {
        amplitude.yaw_rad * std::sin(phase) * state_.aim_fraction,
        amplitude.pitch_rad * std::sin(1.5 * phase + pi / 4.0) * state_.aim_fraction
    };
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

math::Vec3 WeaponHandling::shot_center_direction(math::Vec3 aim, math::Vec3 up_hint) const {
    return shot_basis(aim, up_hint, state_).forward;
}

HandledShot WeaponHandling::sample_shot(math::Vec3 aim, math::Vec3 up_hint) {
    const auto b = shot_basis(aim, up_hint, state_); // Validate before state/RNG changes.
    const double cone = spread(spread_model_);
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
