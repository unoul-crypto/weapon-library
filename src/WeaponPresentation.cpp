#include <weapon_library/WeaponPresentation.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace weapon_library {
namespace {
constexpr double radians_to_degrees = 57.29577951308232;
constexpr double full_turn = 6.283185307179586;
constexpr double step_cycle_seconds = 1.0 / 1.7;

bool finite(math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
void validate_mass(double mass_kg) {
    if (!std::isfinite(mass_kg) || mass_kg <= 0.0)
        throw std::invalid_argument("Weapon mass must be finite and positive");
}

void validate_sway_strength(const SwayStrength& strength) {
    const double multipliers[] = {strength.walk_multiplier, strength.turn_multiplier, strength.aimed_multiplier};
    for (const double value : multipliers) {
        if (!std::isfinite(value) || value < 0.0)
            throw std::invalid_argument("Sway multipliers must be finite and nonnegative");
    }
}

double fraction(double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("Presentation progress must be finite");
    return std::clamp(value, 0.0, 1.0);
}
math::Vec3 blend(math::Vec3 a, math::Vec3 b, double t) { return a * (1.0 - t) + b * t; }
void validate_track(const std::vector<PoseKeyframe>& keys) {
    if (!keys.empty() && (keys.size() < 2 || keys.front().progress != 0.0 || keys.back().progress != 1.0))
        throw std::invalid_argument("Pose keyframes must cover progress 0 to 1");
    double previous = -1.0;
    for (const auto& key : keys) {
        if (!std::isfinite(key.progress) || key.progress < 0.0 || key.progress > 1.0 || key.progress <= previous ||
            !finite(key.pose.offset) || !finite(key.pose.rotation_degrees))
            throw std::invalid_argument("Pose keyframes must be finite and strictly ordered");
        previous = key.progress;
    }
}

PresentationPose sample_track(const std::vector<PoseKeyframe>& keys, PoseInterpolation interpolation, double progress) {
    const double t = fraction(progress);
    if (keys.empty()) return {};
    if (t <= keys.front().progress) return keys.front().pose;
    if (t >= keys.back().progress) return keys.back().pose;
    const auto upper = std::upper_bound(keys.begin(), keys.end(), t,
        [](double value, const PoseKeyframe& key) { return value < key.progress; });
    const auto& a = *(upper - 1);
    const auto& b = *upper;
    double weight = (t - a.progress) / (b.progress - a.progress);
    if (interpolation == PoseInterpolation::Smooth) weight = weight * weight * (3.0 - 2.0 * weight);
    return {blend(a.pose.offset, b.pose.offset, weight), blend(a.pose.rotation_degrees, b.pose.rotation_degrees, weight)};
}
} // namespace

WeaponPresentation::WeaponPresentation(PresentationDefinition definition) : definition_(std::move(definition)) {
    validate_mass(definition_.mass_kg);
    validate_sway_strength(definition_.sway_strength);
    if (!finite(definition_.hip_offset) || !finite(definition_.aimed_offset) || !finite(definition_.muzzle_offset))
        throw std::invalid_argument("Presentation offsets must be finite");
    if ((definition_.interpolation != PoseInterpolation::Linear && definition_.interpolation != PoseInterpolation::Smooth) ||
        (definition_.attack_interpolation != PoseInterpolation::Linear && definition_.attack_interpolation != PoseInterpolation::Smooth))
        throw std::invalid_argument("Unknown presentation interpolation");
    validate_track(definition_.reload_keyframes);
    validate_track(definition_.attack_keyframes);
}

WeaponSway::WeaponSway(double mass_kg) { set_mass_kg(mass_kg); }

void WeaponSway::set_mass_kg(double mass_kg) {
    validate_mass(mass_kg);
    // A bounded response keeps very heavy models controllable on screen.
    inertia_ = mass_kg / (mass_kg + 2.0);
}

void WeaponSway::add_look_delta(double yaw_rad, double pitch_rad) {
    if (!std::isfinite(yaw_rad) || !std::isfinite(pitch_rad))
        throw std::invalid_argument("Look delta must be finite");
    constexpr double max_lag = 0.13; // About 7.5 degrees.
    const double gain = 0.18 + 0.55 * inertia_;
    // Local +Y rotation turns the model right, and local +X pitches it down.
    yaw_lag_ = std::clamp(yaw_lag_ - yaw_rad * gain, -max_lag, max_lag);
    pitch_lag_ = std::clamp(pitch_lag_ + pitch_rad * gain, -max_lag, max_lag);
}

void WeaponSway::advance(double seconds, double movement_factor) {
    if (!std::isfinite(seconds) || seconds < 0.0 ||
        !std::isfinite(movement_factor) || movement_factor < 0.0 || movement_factor > 1.0)
        throw std::invalid_argument("Sway time must be finite and nonnegative and movement in [0, 1]");
    const double response_seconds = 0.065 + 0.17 * inertia_;
    const double exponent = -seconds / response_seconds;
    const double decay = std::exp(exponent);
    yaw_lag_ *= decay;
    pitch_lag_ *= decay;
    const double step_target = movement_factor * (0.75 + 0.6 * inertia_);
    const double blend_weight = -std::expm1(exponent);
    step_weight_ += (step_target - step_weight_) * blend_weight;
    const double walk_time = std::fmod(seconds * movement_factor, step_cycle_seconds);
    step_phase_ = std::fmod(step_phase_ + walk_time / step_cycle_seconds * full_turn, full_turn);
}

PresentationPose WeaponSway::pose(double aim_fraction, const SwayStrength& strength) const {
    validate_sway_strength(strength);
    const double aim = fraction(aim_fraction);
    const double aim_amount = (1.0 - aim) + strength.aimed_multiplier * aim;
    // Bound the additional motion even if the configured multipliers are extreme.
    constexpr double max_boost = 4.0;
    const double walk_amount = std::min(max_boost, strength.walk_multiplier * aim_amount);
    const double turn_amount = std::min(max_boost, strength.turn_multiplier * aim_amount);
    const double side = std::sin(step_phase_) * step_weight_ * walk_amount;
    const double rise = std::sin(2.0 * step_phase_) * step_weight_ * walk_amount;
    const double yaw = yaw_lag_ * turn_amount;
    const double pitch = pitch_lag_ * turn_amount;
    return {
        {yaw * 0.2 + side * 0.012, -pitch * 0.2 + rise * 0.012, 0.0},
        {pitch * radians_to_degrees, yaw * radians_to_degrees, side * 1.5}
    };
}

math::Vec3 WeaponPresentation::mount_offset(double aim_fraction) const {
    return blend(definition_.hip_offset, definition_.aimed_offset, fraction(aim_fraction));
}

PresentationPose WeaponPresentation::sample_reload(double progress) const {
    return sample_track(definition_.reload_keyframes, definition_.interpolation, progress);
}

PresentationPose WeaponPresentation::sample_attack(double progress) const {
    return sample_track(definition_.attack_keyframes, definition_.attack_interpolation, progress);
}

double WeaponPresentation::reload_progress(const ReloadState& reload) {
    if (!reload.active || reload.waiting_for_action) return 0.0;
    if (!std::isfinite(reload.duration_seconds) || !std::isfinite(reload.remaining_seconds) ||
        reload.duration_seconds < 0.0 || reload.remaining_seconds < 0.0)
        throw std::invalid_argument("Reload presentation timing must be finite and nonnegative");
    return reload.duration_seconds == 0.0 ? 1.0 : fraction(1.0 - reload.remaining_seconds / reload.duration_seconds);
}

PresentationPose WeaponPresentation::reload_pose(const ReloadState& reload) const {
    if (!reload.active || reload.waiting_for_action) return {};
    return sample_reload(reload_progress(reload));
}

} // namespace weapon_library
