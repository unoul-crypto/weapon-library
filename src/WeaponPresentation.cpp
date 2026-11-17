#include <weapon_library/WeaponPresentation.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace weapon_library {
namespace {
bool finite(math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
double fraction(double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("Presentation progress must be finite");
    return std::clamp(value, 0.0, 1.0);
}
math::Vec3 blend(math::Vec3 a, math::Vec3 b, double t) { return a * (1.0 - t) + b * t; }
} // namespace

WeaponPresentation::WeaponPresentation(PresentationDefinition definition) : definition_(std::move(definition)) {
    if (!finite(definition_.hip_offset) || !finite(definition_.aimed_offset) || !finite(definition_.muzzle_offset))
        throw std::invalid_argument("Presentation offsets must be finite");
    if (definition_.interpolation != PoseInterpolation::Linear && definition_.interpolation != PoseInterpolation::Smooth)
        throw std::invalid_argument("Unknown presentation interpolation");
    const auto& keys = definition_.reload_keyframes;
    if (!keys.empty() && (keys.size() < 2 || keys.front().progress != 0.0 || keys.back().progress != 1.0))
        throw std::invalid_argument("Reload keyframes must cover progress 0 to 1");
    double previous = -1.0;
    for (const auto& key : keys) {
        if (!std::isfinite(key.progress) || key.progress < 0.0 || key.progress > 1.0 || key.progress <= previous ||
            !finite(key.pose.offset) || !finite(key.pose.rotation_degrees))
            throw std::invalid_argument("Reload keyframes must be finite and strictly ordered");
        previous = key.progress;
    }
}

math::Vec3 WeaponPresentation::mount_offset(double aim_fraction) const {
    return blend(definition_.hip_offset, definition_.aimed_offset, fraction(aim_fraction));
}

PresentationPose WeaponPresentation::sample_reload(double progress) const {
    const double t = fraction(progress);
    const auto& keys = definition_.reload_keyframes;
    if (keys.empty()) return {};
    if (t <= keys.front().progress) return keys.front().pose;
    if (t >= keys.back().progress) return keys.back().pose;
    const auto upper = std::upper_bound(keys.begin(), keys.end(), t,
        [](double value, const ReloadKeyframe& key) { return value < key.progress; });
    const auto& a = *(upper - 1);
    const auto& b = *upper;
    double weight = (t - a.progress) / (b.progress - a.progress);
    if (definition_.interpolation == PoseInterpolation::Smooth) weight = weight * weight * (3.0 - 2.0 * weight);
    return {blend(a.pose.offset, b.pose.offset, weight), blend(a.pose.rotation_degrees, b.pose.rotation_degrees, weight)};
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
