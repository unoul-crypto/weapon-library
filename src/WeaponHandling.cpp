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

LoweredStanceTiming::LoweredStanceTiming(double lower_seconds, double raise_to_ready_seconds, double raise_to_aim_seconds)
    : lower_seconds_(lower_seconds), raise_to_ready_seconds_(raise_to_ready_seconds), raise_to_aim_seconds_(raise_to_aim_seconds) {
    if (!detail::valid_duration(lower_seconds) || !detail::valid_duration(raise_to_ready_seconds) ||
        !detail::valid_duration(raise_to_aim_seconds))
        throw std::invalid_argument("Lowered stance durations must be finite and nonnegative");
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
    begin_stance(aiming ? HandlingStance::Aimed : HandlingStance::Ready);
}

void WeaponHandling::set_stance(HandlingStance target) {
    switch (target) {
    case HandlingStance::Ready:
    case HandlingStance::Aimed:
    case HandlingStance::Lowered:
        begin_stance(target);
        return;
    }
    throw std::invalid_argument("Unknown handling stance");
}

void WeaponHandling::set_lowered_stance_timing(LoweredStanceTiming timing) noexcept {
    definition_.lowered_stance = timing;
}

bool WeaponHandling::attack_allowed() const noexcept {
    return state_.target_stance != HandlingStance::Lowered && state_.lowered_fraction == 0.0;
}

void WeaponHandling::begin_stance(HandlingStance target) noexcept {
    if (target == state_.target_stance) return;
    state_.target_stance = target;
    state_.aiming = target == HandlingStance::Aimed;
    stance_start_aim_ = state_.aim_fraction;
    stance_start_lowered_ = state_.lowered_fraction;
    const double ready = std::max(0.0, (1.0 - stance_start_aim_) - stance_start_lowered_);
    const auto& timing = definition_.lowered_stance;
    switch (target) {
    case HandlingStance::Lowered:
        stance_duration_ = (1.0 - stance_start_lowered_) * timing.lower_seconds();
        break;
    case HandlingStance::Ready:
        stance_duration_ = stance_start_lowered_ * timing.raise_to_ready_seconds() +
                           stance_start_aim_ * definition_.aim_out_seconds;
        break;
    case HandlingStance::Aimed:
        stance_duration_ = stance_start_lowered_ * timing.raise_to_aim_seconds() +
                           ready * definition_.aim_in_seconds;
        break;
    }
    state_.stance_remaining_seconds = stance_duration_;
    stance_rounding_error_ = 0.0;
    advance_stance(0.0);
    update_aim_sway();
}

void WeaponHandling::advance_stance(double seconds) noexcept {
    detail::consume_duration(state_.stance_remaining_seconds, seconds, stance_rounding_error_, stance_duration_);
    const double target_aim = state_.target_stance == HandlingStance::Aimed ? 1.0 : 0.0;
    const double target_lowered = state_.target_stance == HandlingStance::Lowered ? 1.0 : 0.0;
    if (state_.stance_remaining_seconds == 0.0) {
        state_.aim_fraction = target_aim;
        state_.lowered_fraction = target_lowered;
        return;
    }
    const double remaining = state_.stance_remaining_seconds / stance_duration_;
    state_.aim_fraction = stance_start_aim_ * remaining + target_aim * (1.0 - remaining);
    state_.lowered_fraction = stance_start_lowered_ * remaining + target_lowered * (1.0 - remaining);
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
    advance_stance(seconds);
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
