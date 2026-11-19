#include "RangeVisuals.h"
#include "RangeSimulation.h"

#include <cmath>
#include <fstream>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace weapon_range {
namespace {
std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

template <typename... Values> void numbers(const std::string& value, Values&... values) {
    std::istringstream input(value);
    input.imbue(std::locale::classic());
    if (!(input >> ... >> values) || !(std::isfinite(values) && ...))
        throw std::invalid_argument("Expected finite numbers separated by spaces");
    input >> std::ws;
    if (!input.eof()) throw std::invalid_argument("Unexpected text after numbers");
}
void vector(const std::string& value, weapon_library::math::Vec3& v) { numbers(value, v.x, v.y, v.z); }
} // namespace

RangeVisualDefinition default_visual(unsigned weapon_index) {
    if (weapon_index >= weapon_count) throw std::invalid_argument("Unknown range weapon");
    weapon_library::PresentationDefinition definition;
    constexpr double masses[weapon_count] = {0.9, 3.5, 12.0, 0.35};
    definition.mass_kg = masses[weapon_index];
    if (weapon_index == 0) definition.muzzle_offset.z = 0.22;
    if (weapon_index == knife_index) {
        definition.hip_offset = {0.23, -0.2, 0.55};
        definition.aimed_offset = definition.hip_offset;
        definition.reload_keyframes.clear();
        definition.attack_keyframes = {{0, {}}, {0.258, {{0, 0.02, -0.08}, {70, 0, -10}}},
            {0.548, {{-0.23, 0.18, 0.18}, {85, 0, 0}}}, {1, {}}};
    }
    RangeVisualDefinition result;
    result.presentation = weapon_library::WeaponPresentation(std::move(definition));
    if (weapon_index < firearm_count) {
        const auto& handling = weapon_specs()[weapon_index].handling;
        result.aim_sway = {handling.aimed_sway_amplitude_rad, handling.aimed_sway_frequency_hz};
    }
    return result;
}

RangeVisualDefinition load_visual(const std::filesystem::path& file, unsigned weapon_index) {
    auto result = default_visual(weapon_index);
    auto definition = result.presentation.definition();
    auto aim_sway_amplitude = result.aim_sway.amplitude_rad();
    auto aim_sway_frequency = result.aim_sway.frequency_hz();
    std::ifstream input(file);
    if (!input) throw std::runtime_error("Cannot open visual config: " + file.filename().string());
    std::set<std::string> seen;
    unsigned line_number = 0;
    try {
        for (std::string line; std::getline(input, line);) {
            ++line_number;
            line = trim(line);
            if (line.empty() || line.front() == '#') continue;
            const auto equal = line.find('=');
            if (equal == std::string::npos) throw std::invalid_argument("Expected key = value");
            const auto key = trim(line.substr(0, equal));
            const auto value = trim(line.substr(equal + 1));
            const bool first = seen.insert(key).second;
            if (!first && key != "reload_key" && key != "attack_key") throw std::invalid_argument("Duplicate setting: " + key);
            if (key == "model") result.model_file = value;
            else if (key == "animation") result.animation_file = value;
            else if (key == "reload_clip") result.reload_clip = value;
            else if (key == "attack_clip") result.attack_clip = value;
            else if (key == "model_offset") vector(value, result.model_transform.offset);
            else if (key == "model_rotation") vector(value, result.model_transform.rotation_degrees);
            else if (key == "model_scale") {
                vector(value, result.model_transform.scale);
                const auto scale = result.model_transform.scale;
                if (scale.x <= 0 || scale.y <= 0 || scale.z <= 0)
                    throw std::invalid_argument("Model scale must be positive");
            }
            else if (key == "hip_offset") vector(value, definition.hip_offset);
            else if (key == "aimed_offset") vector(value, definition.aimed_offset);
            else if (key == "muzzle_offset") vector(value, definition.muzzle_offset);
            else if (key == "mass_kg") numbers(value, definition.mass_kg);
            else if (key == "walk_sway_multiplier") numbers(value, definition.sway_strength.walk_multiplier);
            else if (key == "turn_sway_multiplier") numbers(value, definition.sway_strength.turn_multiplier);
            else if (key == "aimed_sway_multiplier") numbers(value, definition.sway_strength.aimed_multiplier);
            else if (key == "aimed_sway_amplitude_rad")
                numbers(value, aim_sway_amplitude.yaw_rad, aim_sway_amplitude.pitch_rad);
            else if (key == "aimed_sway_frequency_hz") numbers(value, aim_sway_frequency);
            else if (key == "reload_interpolation" || key == "attack_interpolation") {
                auto& interpolation = key == "reload_interpolation" ? definition.interpolation : definition.attack_interpolation;
                if (value == "smooth") interpolation = weapon_library::PoseInterpolation::Smooth;
                else if (value == "linear") interpolation = weapon_library::PoseInterpolation::Linear;
                else throw std::invalid_argument("Pose interpolation must be smooth or linear");
            }
            else if (key == "reload_key" || key == "attack_key") {
                auto& keys = key == "reload_key" ? definition.reload_keyframes : definition.attack_keyframes;
                if (first) keys.clear();
                weapon_library::PoseKeyframe keyframe{};
                auto& p = keyframe.pose;
                numbers(value, keyframe.progress, p.offset.x, p.offset.y, p.offset.z,
                        p.rotation_degrees.x, p.rotation_degrees.y, p.rotation_degrees.z);
                keys.push_back(keyframe);
            }
            else if (key == "reload_motion" || key == "attack_motion") {
                if (value != "none") throw std::invalid_argument("Use motion = none to disable keyframe motion");
                (key == "reload_motion" ? definition.reload_keyframes : definition.attack_keyframes).clear();
            }
            else throw std::invalid_argument("Unknown visual setting: " + key);
        }
        if (input.bad()) throw std::runtime_error("Failed to read visual config");
        if ((seen.count("reload_motion") && seen.count("reload_key")) || (seen.count("attack_motion") && seen.count("attack_key")))
            throw std::invalid_argument("Use motion or keyframes for a track, not both");
        const bool has_clip = !result.reload_clip.empty() || !result.attack_clip.empty();
        if (!result.animation_file.empty() && !has_clip) throw std::invalid_argument("Animation file requires a clip name");
        if (has_clip && result.model_file.empty()) throw std::invalid_argument("Animation clip requires a model");
        result.presentation = weapon_library::WeaponPresentation(std::move(definition));
        result.aim_sway = {aim_sway_amplitude, aim_sway_frequency};
        if (!result.model_file.empty()) result.model_file = (file.parent_path() / result.model_file).lexically_normal();
        if (!result.animation_file.empty()) result.animation_file = (file.parent_path() / result.animation_file).lexically_normal();
        else if (has_clip) result.animation_file = result.model_file;
        return result;
    } catch (const std::exception& error) {
        throw std::runtime_error(file.filename().string() + ":" + std::to_string(line_number) + ": " + error.what());
    }
}

} // namespace weapon_range
