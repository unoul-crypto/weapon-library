#include "RangeRenderer.h"
#include "RangeVisuals.h"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <utility>

namespace weapon_range {
namespace {

constexpr Color ink{13, 21, 29, 255}, panel{21, 32, 43, 245};
constexpr Color muted{141, 162, 176, 255}, white{232, 239, 239, 255};
constexpr Color accent{255, 170, 78, 255}, teal{100, 225, 199, 255};
constexpr Color miss{255, 112, 112, 255};
Vector3 rv(Vec3 v) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }

void text(const char* value, int x, int y, int size, Color color) { DrawText(value, x, y, size, color); }
void centered(const char* value, int x, int y, int size, Color color) {
    text(value, x - MeasureText(value, size) / 2, y, size, color);
}
void cube(Vector3 pos, Vector3 size, Color color) { DrawCubeV(pos, size, color); }

struct WeaponAsset {
    RangeVisualDefinition definition;
    Model model{};
    ModelAnimation* animations = nullptr;
    int animation_count = 0;
    int reload_clip = -1;
    int attack_clip = -1;
    int last_frame = -1;
    int last_clip = -1;

    ~WeaponAsset() {
        if (animations) UnloadModelAnimations(animations, animation_count);
        if (model.meshes || model.materials) {
            // LoadModel owns its textures, but UnloadModel only frees material maps.
            std::set<unsigned> textures;
            for (int i = 0; i < model.materialCount; ++i) for (int j = 0; j <= MATERIAL_MAP_BRDF; ++j) {
                const auto texture = model.materials[i].maps[j].texture;
                if (texture.id != 0 && texture.id != rlGetTextureIdDefault() && textures.insert(texture.id).second)
                    UnloadTexture(texture);
            }
            UnloadModel(model);
        }
    }

    void load() {
        if (definition.model_file.empty()) return;
        const auto path = definition.model_file.string();
        if (!std::filesystem::is_regular_file(definition.model_file))
            throw std::runtime_error("Model file not found: " + definition.model_file.filename().string());
        if (!IsFileExtension(path.c_str(), ".obj;.glb;.gltf"))
            throw std::runtime_error("Supported model formats: OBJ, GLB, GLTF");
        model = LoadModel(path.c_str());
        if (!IsModelValid(model)) throw std::runtime_error("Cannot load model: " + definition.model_file.filename().string());
        if (definition.reload_clip.empty() && definition.attack_clip.empty()) return;
        if (!std::filesystem::is_regular_file(definition.animation_file))
            throw std::runtime_error("Animation file not found");
        animations = LoadModelAnimations(definition.animation_file.string().c_str(), &animation_count);
        reload_clip = find_clip(definition.reload_clip);
        attack_clip = find_clip(definition.attack_clip);
    }

    int find_clip(const std::string& name) const {
        if (name.empty()) return -1;
        int index = -1;
        for (int i = 0; i < animation_count; ++i) {
            if (name == animations[i].name) { index = i; break; }
        }
        if (index < 0) throw std::runtime_error("Animation clip not found: " + name);
        const auto& clip = animations[index];
        if (model.boneCount == 0 || clip.frameCount < 1 || !IsModelAnimationValid(model, clip))
            throw std::runtime_error("Animation clip skeleton does not match the model");
        for (int i = 0; i < model.boneCount; ++i) {
            if (std::string(model.bones[i].name) != clip.bones[i].name)
                throw std::runtime_error("Animation clip bone names do not match the model");
        }
        return index;
    }

    void animate(const RangeSimulation& range) {
        const int index = range.is_melee() ? attack_clip : reload_clip;
        if (index < 0) return;
        const auto& clip = animations[index];
        const double progress = range.is_melee() ? range.melee_state().attack_progress :
            weapon_library::WeaponPresentation::reload_progress(range.weapon_state().reload);
        const int frame = static_cast<int>(std::round(progress * (clip.frameCount - 1)));
        if (index != last_clip || frame != last_frame) {
            UpdateModelAnimation(model, clip, frame);
            last_frame = frame;
            last_clip = index;
        }
    }
};

void transform_pose(const weapon_library::PresentationPose& pose) {
    rlTranslatef(static_cast<float>(pose.offset.x), static_cast<float>(pose.offset.y), static_cast<float>(pose.offset.z));
    // Local vertices are rotated X, then Y, then Z, then translated.
    rlRotatef(static_cast<float>(pose.rotation_degrees.z), 0, 0, 1);
    rlRotatef(static_cast<float>(pose.rotation_degrees.y), 0, 1, 0);
    rlRotatef(static_cast<float>(pose.rotation_degrees.x), 1, 0, 0);
}

const char* result_label(ShotOutcome outcome) {
    switch (outcome) {
    case ShotOutcome::Target: return "TARGET HIT";
    case ShotOutcome::Floor: return "FLOOR HIT";
    case ShotOutcome::Ceiling: return "CEILING HIT";
    case ShotOutcome::Wall: return "WALL HIT";
    case ShotOutcome::Cover: return "COVER HIT";
    case ShotOutcome::Expired: return "SHOT EXPIRED";
    case ShotOutcome::None: return "RESULT  --";
    }
    return "RESULT  --";
}

void draw_impact_marks(const RangeSimulation& range) {
    for (const auto& result : range.recent_results()) {
        if (result.outcome == ShotOutcome::Expired) continue;
        const auto center = rv(result.position + result.normal * 0.025);
        const float alpha = static_cast<float>(std::clamp((2.0 - (range.time() - result.time_seconds)) / 0.5, 0.0, 1.0));
        DrawSphere(center, result.melee ? 0.025f : 0.09f, Fade(result.outcome == ShotOutcome::Target ? teal : miss, alpha));
    }
}

void draw_last_miss_label(const RangeSimulation& range, Camera3D camera) {
    const auto& result = range.last_result();
    if (result.outcome == ShotOutcome::None || result.outcome == ShotOutcome::Target ||
        result.outcome == ShotOutcome::Expired || range.time() - result.time_seconds > 2.0 ||
        weapon_library::ballistics::dot(result.position - range.eye(), range.aim()) <= 0.0) return;
    const auto point = GetWorldToScreen(rv(result.position), camera);
    const int x = static_cast<int>(point.x), y = static_cast<int>(point.y) + 14;
    if (x < 0 || x > GetScreenWidth() || y < 90 || y > GetScreenHeight() - 180) return;
    const auto label = result_label(result.outcome);
    const int width = MeasureText(label, 15);
    DrawRectangle(x - width / 2 - 6, y - 4, width + 12, 23, panel);
    centered(label, x, y, 15, miss);
}

void draw_room() {
    cube({0, -0.15f, -12}, {14, 0.3f, 48}, {33, 44, 53, 255});
    cube({0, 3, -33.2f}, {14.4f, 6, 0.4f}, {43, 54, 63, 255});
    cube({-7.2f, 3, -12}, {0.4f, 6, 48}, {27, 39, 49, 255});
    cube({7.2f, 3, -12}, {0.4f, 6, 48}, {27, 39, 49, 255});
    cube({0, 6.15f, -12}, {14, 0.3f, 48}, {19, 29, 38, 255});
    for (int z = -32; z <= 8; z += 2) {
        DrawLine3D({-7, 0.008f, static_cast<float>(z)}, {7, 0.008f, static_cast<float>(z)}, {50, 62, 70, 255});
    }
    for (int x = -6; x <= 6; x += 2) {
        DrawLine3D({static_cast<float>(x), 0.008f, 9}, {static_cast<float>(x), 0.008f, -33}, {50, 62, 70, 255});
    }
    cube({0, 0.013f, 0.6f}, {14, 0.014f, 0.08f}, accent);
    const auto& cover = firing_cover();
    auto cover_body_max = cover.max;
    cover_body_max.y -= 0.04;
    cube(rv((cover.min + cover_body_max) * 0.5), rv(cover_body_max - cover.min), {67, 80, 87, 255});
    cube(rv({(cover.min.x + cover.max.x) * 0.5, cover.max.y - 0.02, (cover.min.z + cover.max.z) * 0.5}),
         rv({cover.max.x - cover.min.x, 0.04, cover.max.z - cover.min.z}), {100, 111, 110, 255});
    for (float x : {-5.0f, -1.5f, 1.5f, 5.0f}) {
        cube({x, 0.014f, -15}, {0.025f, 0.015f, 32}, {90, 100, 95, 255});
    }
    for (int z = -30; z <= 6; z += 6) {
        const float depth = static_cast<float>(z);
        for (float x : {-6.95f, 6.95f}) {
            cube({x, 3, depth}, {0.12f, 6, 0.22f}, {57, 70, 80, 255});
            cube({x * 0.995f, 3.3f, depth}, {0.1f, 1.5f, 0.08f}, teal);
        }
        cube({0, 5.95f, depth}, {14, 0.2f, 0.22f}, {48, 62, 72, 255});
        cube({0, 5.8f, depth}, {5, 0.04f, 0.12f}, {220, 236, 228, 255});
    }
    for (int x = -6; x <= 6; x += 2) {
        cube({static_cast<float>(x), 2.5f, -32.95f}, {0.035f, 4.8f, 0.08f}, {64, 78, 85, 255});
    }
}

void draw_targets(const RangeSimulation& range) {
    for (const auto& target : range.targets()) {
        const auto p = rv(target.center);
        const bool recent = range.time() - target.last_hit_time < 0.16;
        cube({p.x, 0.42f, p.z - 0.1f}, {0.09f, 0.84f, 0.12f}, {90, 104, 112, 255});
        cube({p.x, 0.045f, p.z - 0.1f}, {1.1f, 0.09f, 0.75f}, {58, 73, 84, 255});
        const Color rings[] = {recent ? teal : white, {41, 61, 73, 255}, white, {242, 135, 65, 255}, ink};
        const float radius = static_cast<float>(target.radius);
        const float radii[] = {radius, radius * 0.8f, radius * 0.59f, radius * 0.38f, radius * 0.18f};
        for (int ring = 0; ring < 5; ++ring) {
            const float depth = p.z + static_cast<float>(ring) * 0.006f;
            DrawCylinderEx({p.x, p.y, depth - 0.008f}, {p.x, p.y, depth}, radii[ring], radii[ring], 48, rings[ring]);
        }
    }
}

void draw_water() {
    const auto& volume = water_volume();
    const auto center = rv((volume.min + volume.max) * 0.5);
    const auto size = rv(volume.max - volume.min);
    // The volume is a fluid region, with no solid glass collider.
    rlDisableDepthMask();
    DrawCubeV(center, size, {38, 167, 203, 32});
    DrawCubeWiresV(center, size, {76, 208, 225, 190});
    rlEnableDepthMask();
}

void draw_target_labels(const RangeSimulation& range, Camera3D camera) {
    unsigned index = 0;
    for (const auto& target : range.targets()) {
        const auto p = rv(target.center);
        char label[64];
        ++index;
        if (index == 4) std::snprintf(label, sizeof(label), "MELEE PRACTICE");
        else std::snprintf(label, sizeof(label), "%02u", index);
        const Vec3 relative{target.center.x - range.eye().x, target.center.y - range.eye().y,
                            target.center.z - range.eye().z};
        const auto aim = range.aim();
        if (relative.x * aim.x + relative.y * aim.y + relative.z * aim.z <= 0) continue;
        const auto screen = GetWorldToScreen({p.x, p.y + 1.15f, p.z}, camera);
        if (screen.x > 0 && screen.x < GetScreenWidth() && screen.y > 100 && screen.y < GetScreenHeight() - 170) {
            centered(label, static_cast<int>(screen.x), static_cast<int>(screen.y), 16, muted);
        }
    }
    const auto water_label = GetWorldToScreen({3.0f, 3.5f, -19.0f}, camera);
    const auto delta = Vec3{3.0, 3.5, -19.0} - range.eye();
    if (weapon_library::ballistics::dot(delta, range.aim()) > 0 && water_label.x > 0 && water_label.x < GetScreenWidth()) {
        centered("WATER / 6 M", static_cast<int>(water_label.x), static_cast<int>(water_label.y), 16, teal);
    }
}

void draw_held_weapon(const RangeSimulation& range, WeaponAsset& asset) {
    const auto pose = range.weapon_pose();
    const auto aim = pose.forward, right = pose.right, up = pose.up;
    const double age = range.time() - range.last_shot_time();
    const double kick = !range.is_melee() && age >= 0.0 ? std::exp(-age * 35.0) : 0.0;
    const Vec3 origin = pose.origin + aim * (-kick * 0.065) + up * (kick * 0.02);
    // Orient the visual in the view frame, independently of collision and firing rules.
    const float matrix[16] = {
        static_cast<float>(right.x), static_cast<float>(right.y), static_cast<float>(right.z), 0,
        static_cast<float>(up.x), static_cast<float>(up.y), static_cast<float>(up.z), 0,
        static_cast<float>(aim.x), static_cast<float>(aim.y), static_cast<float>(aim.z), 0,
        static_cast<float>(origin.x), static_cast<float>(origin.y), static_cast<float>(origin.z), 1};
    rlPushMatrix();
    rlDisableBackfaceCulling();
    rlMultMatrixf(matrix);
    transform_pose(range.sway_pose());
    transform_pose(asset.definition.presentation.sample_lowered(range.handling_state().lowered_fraction));
    const auto reload = range.weapon_state().reload;
    transform_pose(asset.definition.presentation.reload_pose(reload));
    if (range.is_melee() && range.melee_state().weapon.phase != weapon_library::Phase::Ready)
        transform_pose(asset.definition.presentation.sample_attack(range.melee_state().attack_progress));
    asset.animate(range);
    const bool pistol = range.selected_weapon() == 0;
    if (asset.model.meshCount) {
        rlPushMatrix();
        const auto& transform = asset.definition.model_transform;
        transform_pose({transform.offset, transform.rotation_degrees});
        rlScalef(static_cast<float>(transform.scale.x), static_cast<float>(transform.scale.y), static_cast<float>(transform.scale.z));
        DrawModel(asset.model, {0, 0, 0}, 1.0f, WHITE);
        rlPopMatrix();
    } else if (range.is_melee()) {
        cube({0, -0.055f, 0}, {0.045f, 0.15f, 0.05f}, {36, 49, 57, 255});
        for (float y : {-0.1f, -0.07f, -0.04f}) cube({0, y, 0}, {0.048f, 0.009f, 0.053f}, {65, 79, 87, 255});
        cube({0, 0.03f, 0}, {0.10f, 0.025f, 0.025f}, accent);
        cube({0, 0.135f, 0}, {0.045f, 0.19f, 0.016f}, {171, 195, 204, 255});
        DrawTriangle3D({-0.0225f, 0.23f, -0.008f}, {0.0225f, 0.23f, -0.008f}, {0, 0.29f, 0}, white);
        DrawTriangle3D({-0.0225f, 0.23f, 0.008f}, {0, 0.29f, 0}, {0.0225f, 0.23f, 0.008f}, muted);
    } else {
        cube({0, 0, 0}, {0.11f, 0.1f, pistol ? 0.24f : 0.4f}, {65, 79, 87, 255});
        cube({0, -0.1f, -0.06f}, {0.09f, 0.17f, 0.1f}, {29, 40, 49, 255});
        cube({0, 0.04f, 0.02f}, {0.085f, 0.025f, pistol ? 0.23f : 0.42f}, {108, 127, 135, 255});
        cube({0, 0, pistol ? 0.15f : 0.27f}, {0.055f, 0.055f, pistol ? 0.1f : 0.2f}, {25, 35, 44, 255});
        cube({0, 0.06f, 0.1f}, {0.004f, 0.01f, 0.01f}, accent);
        if (!pistol) cube({0, -0.1f, 0.06f}, {0.07f, 0.19f, 0.1f}, {36, 49, 57, 255});
    }
    if (!range.is_melee() && age >= 0 && age < 0.045) {
        DrawSphere(rv(asset.definition.presentation.definition().muzzle_offset), 0.05f, {255, 218, 136, 255});
    }
    rlPopMatrix();
    rlEnableBackfaceCulling();
}

void draw_crosshair(const RangeSimulation& range, Camera3D camera, double spread_rad) {
    const auto sight = GetWorldToScreen(rv(range.eye() + range.shot_center_direction()), camera);
    if (!std::isfinite(sight.x) || !std::isfinite(sight.y) ||
        sight.x < 0.0f || sight.y < 0.0f || sight.x >= GetScreenWidth() || sight.y >= GetScreenHeight())
        return;
    const int sight_x = static_cast<int>(std::lround(sight.x)), sight_y = static_cast<int>(std::lround(sight.y));
    const bool hit = range.time() - range.last_hit_time() < 0.16;
    const Color cross = hit ? accent : teal;
    const double half_fov_rad = camera.fovy * 0.008726646259971648;
    const float radius = static_cast<float>(GetScreenHeight() * 0.5 * std::tan(spread_rad) / std::tan(half_fov_rad));
    const int gap = static_cast<int>(std::ceil(std::max(4.0f, radius)));
    if (radius > 2.0f) DrawCircleLines(sight_x, sight_y, radius, Fade(cross, 0.35f));
    DrawLine(sight_x - gap - 8, sight_y, sight_x - gap, sight_y, cross);
    DrawLine(sight_x + gap, sight_y, sight_x + gap + 8, sight_y, cross);
    DrawLine(sight_x, sight_y - gap - 8, sight_x, sight_y - gap, cross);
    DrawLine(sight_x, sight_y + gap, sight_x, sight_y + gap + 8, cross);
    DrawCircle(sight_x, sight_y, 1.5f, cross);
    if (hit) {
        for (int x : {-1, 1}) for (int y : {-1, 1})
            DrawLine(sight_x + x * 18, sight_y + y * 18, sight_x + x * 24, sight_y + y * 24, accent);
    }
}

void draw_hud(const RangeSimulation& range, Camera3D camera, bool playing, bool slow_motion, bool show_crosshair) {
    const int width = GetScreenWidth(), height = GetScreenHeight();
    const auto state = range.weapon_state();
    DrawRectangle(0, 0, width, 4, accent);
    text("WEAPON LAB", 28, 24, 26, white);
    text("LIVE RANGE  /  " WEAPON_RANGE_VERSION, 29, 55, 15, muted);
    const auto handling = range.handling_state();
    DrawRectangle(24, 90, 300, 73, panel);
    char accuracy[96];
    constexpr double degrees = 57.29577951308232;
    if (range.is_melee()) std::snprintf(accuracy, sizeof(accuracy), "MELEE  /  REACH %.1f M", range.melee_definition().reach_m);
    else std::snprintf(accuracy, sizeof(accuracy), "AIM %03.0f%%   SPREAD %.2f DEG", handling.aim_fraction * 100.0,
                      handling.spread_half_angle_rad * degrees);
    text(accuracy, 36, 104, 15, teal);
    if (!range.attack_allowed()) {
        const bool lowering = handling.target_stance == weapon_library::HandlingStance::Lowered;
        if (!lowering)
            std::snprintf(accuracy, sizeof(accuracy), "RAISING  %.2f S", handling.stance_remaining_seconds);
        else if (handling.stance_remaining_seconds > 0.0)
            std::snprintf(accuracy, sizeof(accuracy), "LOWERING  %.2f S", handling.stance_remaining_seconds);
        else
            std::snprintf(accuracy, sizeof(accuracy), "WEAPON LOWERED");
    } else if (range.is_melee()) std::snprintf(accuracy, sizeof(accuracy), "APPROACH THE NEAR TARGET");
    else std::snprintf(accuracy, sizeof(accuracy), "RECOIL %.2f / %.2f DEG", handling.recoil.yaw_rad * degrees,
                      handling.recoil.pitch_rad * degrees);
    text(accuracy, 36, 134, 15, muted);
    text(playing ? (slow_motion ? "SLOW / 0.1X" : "LIVE / 1.0X") : "PAUSED", width - 186, 28, 18, playing ? teal : accent);
    DrawRectangle(width - 260, 66, 232, 262, panel);
    char buffer[128];
    const auto attempts = range.is_melee() ? range.attacks() : range.shots();
    const auto hits = range.is_melee() ? range.melee_hits() : range.hits();
    std::snprintf(buffer, sizeof(buffer), "%s  %03u    HITS  %03u", range.is_melee() ? "STABS" : "SHOTS", attempts, hits);
    text(buffer, width - 246, 82, 17, white);
    std::snprintf(buffer, sizeof(buffer), "ACCURACY  %03.0f%%", attempts ? 100.0 * hits / attempts : 0.0);
    text(buffer, width - 246, 111, 17, muted);
    std::snprintf(buffer, sizeof(buffer), "SCORE  %04u", range.score());
    text(buffer, width - 246, 140, 18, accent);
    std::snprintf(buffer, sizeof(buffer), "IN FLIGHT  %02u", static_cast<unsigned>(range.projectiles().size()));
    text(buffer, width - 246, 174, 16, teal);
    if (range.last_result().melee) {
        text("CONTACT  /  MELEE", width - 246, 201, 16, white);
    } else if (range.last_impact_time() >= 0.0) {
        std::snprintf(buffer, sizeof(buffer), "FLIGHT  %.1f MS", range.last_flight_seconds() * 1000.0);
        text(buffer, width - 246, 201, 16, white);
        std::snprintf(buffer, sizeof(buffer), "IMPACT  %.1f M/S", range.last_impact_speed());
        text(buffer, width - 246, 228, 16, muted);
    } else text("FLIGHT  --", width - 246, 201, 16, muted);
    const auto& result = range.last_result();
    text(result_label(result.outcome), width - 246, 266, 18,
         result.outcome == ShotOutcome::None ? muted : result.outcome == ShotOutcome::Target ? teal : miss);
    if (result.outcome != ShotOutcome::None) {
        text(result.melee ? "BLADE CONTACT" : result.passed_water ? "PATH: WATER" : "PATH: AIR", width - 246, 295, 15, muted);
    }

    const int bottom = height - 195;
    DrawRectangle(24, bottom, 310, 145, panel);
    if (range.is_melee()) {
        text("KNIFE", 40, bottom + 12, 21, white);
        const auto phase = range.melee_state().weapon.phase;
        const char* label = phase == weapon_library::Phase::Ready ? "READY" : phase == weapon_library::Phase::Windup ?
            "WINDUP" : phase == weapon_library::Phase::Active ? "THRUST" : "RECOVERY";
        text(label, 40, bottom + 46, 25, phase == weapon_library::Phase::Active ? accent : teal);
        text("LMB / F  /  ONE STAB PER PRESS", 40, bottom + 84, 14, muted);
        text("Q CANCEL  /  NO AMMO REQUIRED", 40, bottom + 112, 14, muted);
        DrawRectangle(24, bottom - 10, static_cast<int>(310 * range.melee_state().attack_progress), 3, accent);
    } else {
        const auto& spec = weapon_specs()[range.selected_weapon()];
        text(spec.name, 40, bottom + 12, 21, white);
        std::snprintf(buffer, sizeof(buffer), "%02u", state.rounds_remaining);
        text(buffer, 40, bottom + 39, 42, state.rounds_remaining ? white : accent);
        std::snprintf(buffer, sizeof(buffer), "/ %02u", spec.capacity);
        text(buffer, 106, bottom + 56, 20, muted);
        text(spec.mode == weapon_library::FireMode::SemiAutomatic ? "SEMI AUTO" : "FULL AUTO", 172, bottom + 45, 15, teal);
        std::snprintf(buffer, sizeof(buffer), "%03.0f RPM", 60.0 / spec.interval_seconds);
        text(buffer, 172, bottom + 68, 15, muted);
        std::snprintf(buffer, sizeof(buffer), "MUZZLE  %.0f M/S", spec.muzzle_speed_m_s);
        text(buffer, 40, bottom + 121, 15, muted);
        std::snprintf(buffer, sizeof(buffer), "RESERVE  %03u", range.reserve_rounds());
        text(buffer, 40, bottom + 98, 16, teal);
    }
    if (state.reload.active) {
        DrawRectangle(24, bottom - 45, 310, 37, panel);
        const double progress = weapon_library::WeaponPresentation::reload_progress(state.reload);
        std::snprintf(buffer, sizeof(buffer), "RELOADING %03.0f%% / Q OR SHIFT", progress * 100.0);
        text(state.reload.waiting_for_action ? "QUEUED / Q OR SHIFT TO CANCEL" : buffer, 36, bottom - 35, 16, accent);
        DrawRectangle(24, bottom - 11, static_cast<int>(310 * progress), 3, accent);
    } else if (range.time() - range.last_reload_notice_time() < 1.5) {
        const char* notice = nullptr;
        switch (range.last_reload_status()) {
        case weapon_library::ReloadStatus::Accepted:
            if (range.last_reload_cancelled()) notice = "RELOAD CANCELLED";
            break;
        case weapon_library::ReloadStatus::MagazineFull: notice = "MAGAZINE FULL"; break;
        case weapon_library::ReloadStatus::NoAmmunition: notice = "NO RESERVE AMMO"; break;
        case weapon_library::ReloadStatus::CancellationBlocked: notice = "CANNOT CANCEL YET"; break;
        default: break;
        }
        if (notice) text(notice, 36, bottom - 30, 18, accent);
    }
    for (unsigned i = 0; i < weapon_count; ++i) {
        const int x = width - 482 + static_cast<int>(i) * 114;
        const bool selected = i == range.selected_weapon();
        DrawRectangle(x, bottom + 89, 106, 56, selected ? Color{71, 62, 46, 245} : panel);
        if (selected) DrawRectangle(x, bottom + 89, 106, 2, accent);
        std::snprintf(buffer, sizeof(buffer), "%u", i + 1);
        text(buffer, x + 10, bottom + 96, 14, muted);
        centered(i == knife_index ? "KNIFE" : weapon_specs()[i].name, x + 53, bottom + 120, 16, selected ? accent : white);
    }
    DrawRectangle(0, height - 34, width, 34, ink);
    centered("WASD MOVE  SHIFT SPRINT  RMB AIM  LMB/F ATTACK  R RELOAD  Q CANCEL  1-4 WEAPON  C CROSSHAIR  V SLOW  F5 CONFIG  HOME RESET  ESC PAUSE", width / 2, height - 23, width < 1200 ? 11 : 13, muted);
    const int cx = width / 2, cy = height / 2;
    if (show_crosshair && range.attack_allowed()) draw_crosshair(range, camera, handling.spread_half_angle_rad);
    if (!range.is_melee() && !state.reload.active && range.time() - range.last_dry_time() < 0.9)
        centered("EMPTY  /  R TO RELOAD", cx, cy + 48, 18, accent);
    if (!playing) {
        DrawRectangle(0, 0, width, height, {6, 13, 20, 195});
        DrawRectangle(cx - 244, cy - 144, 488, 272, panel);
        DrawRectangle(cx - 244, cy - 144, 488, 3, accent);
        centered("FIRING RANGE", cx, cy - 108, 34, white);
        centered("Aim, control recoil and watch the trajectory.", cx, cy - 54, 18, muted);
        centered("Click or press ENTER to enter", cx, cy + 1, 24, accent);
        centered("Hold RMB to aim. Fire with LMB or F.", cx, cy + 53, 17, white);
        centered("4 selects the knife. Approach the near target.", cx, cy + 83, 16, muted);
        centered("R reloads. Q cancels. Home resets the session.", cx, cy + 108, 15, muted);
    }
}

} // namespace

struct RangeRenderer::Impl {
    std::array<std::unique_ptr<WeaponAsset>, weapon_count> assets;
    explicit Impl(const std::filesystem::path* directory = nullptr) {
        const char* files[] = {"pistol.weapon", "rifle.weapon", "turret.weapon", "knife.weapon"};
        for (unsigned i = 0; i < assets.size(); ++i) {
            assets[i] = std::make_unique<WeaponAsset>();
            assets[i]->definition = directory ? load_visual(*directory / files[i], i) : default_visual(i);
            assets[i]->load();
        }
    }
};

RangeRenderer::RangeRenderer(std::filesystem::path directory)
    : config_directory_(std::move(directory)), impl_(std::make_unique<Impl>()) {}
RangeRenderer::~RangeRenderer() = default;

bool RangeRenderer::reload_visuals(RangeSimulation& range) {
    try {
        auto replacement = std::make_unique<Impl>(&config_directory_);
        for (unsigned i = 0; i < replacement->assets.size(); ++i) {
            const auto& config = replacement->assets[i]->definition;
            range.set_presentation(i, config.presentation);
            range.set_aim_sway(i, config.aim_sway);
            range.set_lowered_stance_timing(i, config.stance_timing);
        }
        impl_ = std::move(replacement);
        status_ = "WEAPON CONFIG LOADED / F5 TO REFRESH";
        config_error_ = false;
        return true;
    } catch (const std::exception& error) {
        status_ = std::string("VISUAL CONFIG ERROR: ") + error.what();
        config_error_ = true;
        std::fprintf(stderr, "%s\n", status_.c_str());
        return false;
    }
}

void RangeRenderer::draw(const RangeSimulation& range, bool playing, bool slow_motion, bool show_crosshair) {
    Camera3D camera{};
    camera.position = rv(range.eye());
    camera.target = rv(range.eye() + range.aim());
    camera.up = {0, 1, 0};
    camera.fovy = static_cast<float>(65.0 - 20.0 * range.handling_state().aim_fraction);
    camera.projection = CAMERA_PERSPECTIVE;
    BeginDrawing();
    ClearBackground(ink);
    BeginMode3D(camera);
    draw_room();
    draw_targets(range);
    draw_water();
    for (const auto& trace : range.traces()) {
        const float alpha = static_cast<float>(std::clamp(1.0 - (range.time() - trace.time_seconds) / 0.12, 0.0, 1.0));
        DrawLine3D(rv(trace.from), rv(trace.to), Fade(trace.water ? teal : accent, alpha * 0.55f));
    }
    for (const auto& projectile : range.projectiles()) {
        const auto state = projectile.motion.state();
        const bool water = state.medium.id == weapon_library::ballistics::Medium::water().id;
        DrawSphere(rv(state.position), 0.045f, water ? teal : Color{255, 231, 164, 255});
    }
    draw_impact_marks(range);
    draw_held_weapon(range, *impl_->assets[range.selected_weapon()]);
    EndMode3D();
    draw_target_labels(range, camera);
    draw_last_miss_label(range, camera);
    draw_hud(range, camera, playing, slow_motion, show_crosshair);
    DrawText(status_.c_str(), 29, 174, 12, config_error_ ? miss : muted);
    EndDrawing();
}

} // namespace weapon_range
