#include <weapon_library/WeaponPresentation.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace weapon_library;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool same(math::Vec3 a, math::Vec3 b) { return math::length(a - b) < 1e-11; }
template <typename Function> void rejects(Function function) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Invalid presentation data was accepted");
}

void poses_follow_authoritative_reload_state() {
    PresentationDefinition d;
    d.reload_keyframes = {{0, {}}, {0.5, {{0, -2, 0}, {0, 0, 60}}}, {1, {}}};
    WeaponPresentation p(d);
    const auto middle = p.reload_pose({true, false, 2, 1});
    require(same(p.reload_pose({false, false, 2, 1}).offset, {}) &&
            same(p.reload_pose({true, true, 2, 1}).offset, {}), "Ready and queued reload poses must be identity");
    require(same(middle.offset, {0, -2, 0}) && same(middle.rotation_degrees, {0, 0, 60}),
            "Reload midpoint did not sample the keyframe");
    require(same(p.reload_pose({true, false, 2, 1.5}).offset, {0, -1, 0}), "Interpolation failed");
    require(same(p.reload_pose({true, true, 2, 1}).offset, {}), "Queued reload moved the weapon");
    require(same(p.reload_pose({false, false, 2, 1}).offset, {}), "Cancelled reload retained a pose");
    require(same(p.reload_pose({true, false, 0, 0}).offset, {}), "Instant reload did not sample its endpoint");
    require(WeaponPresentation::reload_progress({true, false, 0, 0}) == 1, "Instant reload divided by zero");
    require(same(p.sample_reload(-1).offset, {}) && same(p.sample_reload(2).offset, {}), "Progress did not clamp");
    require(same(p.mount_offset(0), d.hip_offset) && same(p.mount_offset(1), d.aimed_offset) &&
            same(p.mount_offset(0.5), (d.hip_offset + d.aimed_offset) * 0.5), "ADS mount interpolation failed");
    d.interpolation = PoseInterpolation::Linear;
    WeaponPresentation linear(d);
    require(same(linear.sample_reload(0.125).offset, {0, -0.5, 0}), "Linear interpolation failed");
    require(same(p.sample_reload(0.125).offset, {0, -0.3125, 0}), "Smooth interpolation failed");
    d.reload_keyframes.clear();
    require(same(WeaponPresentation(d).sample_reload(0.5).offset, {}), "Disabled motion was not identity");
}

void attack_tracks_are_independent() {
    PresentationDefinition d;
    d.attack_keyframes = {{0, {}}, {1, {{1, 0, 0}, {0, 90, 0}}}};
    d.attack_interpolation = PoseInterpolation::Linear;
    WeaponPresentation presentation(d);
    require(same(presentation.sample_attack(0.25).offset, {0.25, 0, 0}) &&
            same(presentation.sample_attack(0.25).rotation_degrees, {0, 22.5, 0}) &&
            same(presentation.sample_reload(0.5).offset, {0, -0.14, -0.1}), "Attack and reload tracks interfered");
    d.attack_keyframes[1].progress = 0;
    rejects([&] { WeaponPresentation invalid(d); });
}

void invalid_definitions_are_rejected() {
    PresentationDefinition d;
    d.reload_keyframes[1].progress = 0;
    rejects([&] { WeaponPresentation p(d); });
    d = {};
    d.reload_keyframes.back().progress = 0.9;
    rejects([&] { WeaponPresentation p(d); });
    d = {};
    d.reload_keyframes[1].pose.rotation_degrees.x = std::numeric_limits<double>::infinity();
    rejects([&] { WeaponPresentation p(d); });
    d = {};
    d.muzzle_offset.x = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { WeaponPresentation p(d); });
    WeaponPresentation p;
    rejects([&] { p.sample_reload(std::numeric_limits<double>::infinity()); });
    rejects([&] { p.mount_offset(std::numeric_limits<double>::quiet_NaN()); });
}
} // namespace

int main() {
    try {
        poses_follow_authoritative_reload_state();
        invalid_definitions_are_rejected();
        attack_tracks_are_independent();
        std::cout << "Presentation tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
