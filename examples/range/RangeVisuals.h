#pragma once

#include <weapon_library/WeaponPresentation.h>
#include <weapon_library/WeaponHandling.h>

#include <filesystem>
#include <string>

namespace weapon_range {

struct ModelTransform {
    weapon_library::math::Vec3 offset;
    weapon_library::math::Vec3 rotation_degrees;
    weapon_library::math::Vec3 scale{1.0, 1.0, 1.0};
};

// Asset loading belongs to the demo adapter; the library only evaluates poses.
struct RangeVisualDefinition {
    weapon_library::WeaponPresentation presentation;
    weapon_library::AimSwaySettings aim_sway;
    weapon_library::LoweredStanceTiming stance_timing;
    std::uint32_t projectiles_per_shot = 1;
    std::filesystem::path model_file;
    ModelTransform model_transform;
    std::filesystem::path animation_file;
    std::string reload_clip;
    std::string attack_clip;
};

RangeVisualDefinition default_visual(unsigned weapon_index);
RangeVisualDefinition load_visual(const std::filesystem::path& file, unsigned weapon_index);

} // namespace weapon_range
