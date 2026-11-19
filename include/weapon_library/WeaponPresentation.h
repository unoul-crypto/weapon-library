#pragma once

#include "FiringMechanism.h"
#include "Math.h"

#include <vector>

namespace weapon_library {

// Local weapon space: +X right, +Y up, +Z forward. Distances are metres.
struct PresentationPose {
    math::Vec3 offset;
    math::Vec3 rotation_degrees;
};

struct PoseKeyframe {
    double progress;
    PresentationPose pose;
};
using ReloadKeyframe = PoseKeyframe; // Compatibility with existing reload definitions.

enum class PoseInterpolation { Linear, Smooth };

struct SwayStrength {
    double walk_multiplier = 1.0;
    double turn_multiplier = 1.0;
    double aimed_multiplier = 0.25;
};

struct PresentationDefinition {
    math::Vec3 hip_offset{0.24, -0.22, 0.58};
    math::Vec3 aimed_offset{0.0, -0.065, 0.5};
    math::Vec3 muzzle_offset{0.0, 0.0, 0.4};
    std::vector<ReloadKeyframe> reload_keyframes{
        {0.0, {}}, {0.5, {{0.0, -0.14, -0.1}, {0.0, 0.0, 25.0}}}, {1.0, {}}};
    PoseInterpolation interpolation = PoseInterpolation::Smooth;
    std::vector<PoseKeyframe> attack_keyframes;
    PoseInterpolation attack_interpolation = PoseInterpolation::Smooth;
    double mass_kg = 1.0; // Held model inertia.
    SwayStrength sway_strength;
};

// Runtime motion of a held model, in the same local space as PresentationPose.
class WeaponSway {
public:
    explicit WeaponSway(double mass_kg = 1.0);
    void set_mass_kg(double mass_kg); // Retains the current pose when tuning mass.
    void add_look_delta(double yaw_rad, double pitch_rad);
    void advance(double seconds, double movement_factor); // Actual movement speed as a fraction in [0, 1].
    PresentationPose pose(double aim_fraction = 0.0, const SwayStrength& strength = {}) const;

private:
    double inertia_ = 0.0;
    double yaw_lag_ = 0.0;
    double pitch_lag_ = 0.0;
    double step_phase_ = 0.0;
    double step_weight_ = 0.0;
};

// Immutable, engine-independent presentation data. No clock, assets or gameplay mutations.
class WeaponPresentation {
public:
    explicit WeaponPresentation(PresentationDefinition definition = {});
    const PresentationDefinition& definition() const noexcept { return definition_; }
    math::Vec3 mount_offset(double aim_fraction) const;
    PresentationPose sample_reload(double progress) const;
    PresentationPose sample_attack(double progress) const;
    PresentationPose reload_pose(const ReloadState& reload) const;
    static double reload_progress(const ReloadState& reload);

private:
    PresentationDefinition definition_;
};

} // namespace weapon_library
