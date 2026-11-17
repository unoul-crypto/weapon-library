#include "RangeVisuals.h"

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
    if (weapon_index >= 3) throw std::invalid_argument("Unknown range weapon");
    weapon_library::PresentationDefinition definition;
    if (weapon_index == 0) definition.muzzle_offset.z = 0.22;
    RangeVisualDefinition result;
    result.presentation = weapon_library::WeaponPresentation(std::move(definition));
    return result;
}

RangeVisualDefinition load_visual(const std::filesystem::path& file, unsigned weapon_index) {
    auto result = default_visual(weapon_index);
    auto definition = result.presentation.definition();
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
            if (!first && key != "reload_key") throw std::invalid_argument("Duplicate setting: " + key);
            if (key == "model") result.model_file = value;
            else if (key == "animation") result.animation_file = value;
            else if (key == "reload_clip") result.reload_clip = value;
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
            else if (key == "reload_interpolation") {
                if (value == "smooth") definition.interpolation = weapon_library::PoseInterpolation::Smooth;
                else if (value == "linear") definition.interpolation = weapon_library::PoseInterpolation::Linear;
                else throw std::invalid_argument("Reload interpolation must be smooth or linear");
            }
            else if (key == "reload_key") {
                if (first) definition.reload_keyframes.clear();
                weapon_library::ReloadKeyframe keyframe{};
                auto& p = keyframe.pose;
                numbers(value, keyframe.progress, p.offset.x, p.offset.y, p.offset.z,
                        p.rotation_degrees.x, p.rotation_degrees.y, p.rotation_degrees.z);
                definition.reload_keyframes.push_back(keyframe);
            }
            else if (key == "reload_motion") {
                if (value != "none") throw std::invalid_argument("Use reload_motion = none to disable keyframe motion");
                definition.reload_keyframes.clear();
            }
            else throw std::invalid_argument("Unknown visual setting: " + key);
        }
        if (input.bad()) throw std::runtime_error("Failed to read visual config");
        if (seen.count("reload_motion") && seen.count("reload_key"))
            throw std::invalid_argument("Use reload_motion or reload_key, not both");
        if (!result.animation_file.empty() && result.reload_clip.empty())
            throw std::invalid_argument("Animation file requires reload_clip");
        if (!result.reload_clip.empty() && result.model_file.empty())
            throw std::invalid_argument("Reload clip requires a model");
        result.presentation = weapon_library::WeaponPresentation(std::move(definition));
        if (!result.model_file.empty()) result.model_file = (file.parent_path() / result.model_file).lexically_normal();
        if (!result.animation_file.empty()) result.animation_file = (file.parent_path() / result.animation_file).lexically_normal();
        else if (!result.reload_clip.empty()) result.animation_file = result.model_file;
        return result;
    } catch (const std::exception& error) {
        throw std::runtime_error(file.filename().string() + ":" + std::to_string(line_number) + ": " + error.what());
    }
}

} // namespace weapon_range
