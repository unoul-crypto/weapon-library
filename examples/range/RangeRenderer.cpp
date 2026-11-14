#include "RangeRenderer.h"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

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

const char* result_label(ShotOutcome outcome) {
    switch (outcome) {
    case ShotOutcome::Target: return "TARGET HIT";
    case ShotOutcome::Floor: return "FLOOR HIT";
    case ShotOutcome::Ceiling: return "CEILING HIT";
    case ShotOutcome::Wall: return "WALL HIT";
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
        DrawSphere(center, 0.09f, Fade(result.outcome == ShotOutcome::Target ? teal : miss, alpha));
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
        const float radii[] = {0.9f, 0.72f, 0.53f, 0.34f, 0.16f};
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
        std::snprintf(label, sizeof(label), "%02u", ++index);
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

void draw_held_weapon(const RangeSimulation& range) {
    const auto eye = range.eye(), aim = range.aim();
    const double planar = std::hypot(aim.x, aim.z);
    const Vec3 right{-aim.z / planar, 0, aim.x / planar};
    const Vec3 up{-right.z * aim.y, planar, right.x * aim.y};
    const double age = range.time() - range.last_shot_time();
    const double kick = age >= 0.0 ? std::exp(-age * 35.0) : 0.0;
    const Vec3 origin = eye + aim * (0.58 - kick * 0.065) + right * 0.24 + up * (-0.22 + kick * 0.02);
    // Orient the visual in the view frame, independently of collision and firing rules.
    const float matrix[16] = {
        static_cast<float>(right.x), static_cast<float>(right.y), static_cast<float>(right.z), 0,
        static_cast<float>(up.x), static_cast<float>(up.y), static_cast<float>(up.z), 0,
        static_cast<float>(aim.x), static_cast<float>(aim.y), static_cast<float>(aim.z), 0,
        static_cast<float>(origin.x), static_cast<float>(origin.y), static_cast<float>(origin.z), 1};
    rlPushMatrix();
    rlDisableBackfaceCulling();
    rlMultMatrixf(matrix);
    const bool pistol = range.selected_weapon() == 0;
    cube({0, 0, 0}, {0.11f, 0.1f, pistol ? 0.24f : 0.4f}, {65, 79, 87, 255});
    cube({0, -0.1f, -0.06f}, {0.09f, 0.17f, 0.1f}, {29, 40, 49, 255});
    cube({0, 0.04f, 0.02f}, {0.085f, 0.025f, pistol ? 0.23f : 0.42f}, {108, 127, 135, 255});
    cube({0, 0, pistol ? 0.15f : 0.27f}, {0.055f, 0.055f, pistol ? 0.1f : 0.2f}, {25, 35, 44, 255});
    cube({0, 0.065f, 0.1f}, {0.018f, 0.023f, 0.018f}, accent);
    if (!pistol) cube({0, -0.1f, 0.06f}, {0.07f, 0.19f, 0.1f}, {36, 49, 57, 255});
    if (age >= 0 && age < 0.045) {
        DrawSphere({0, 0, pistol ? 0.22f : 0.4f}, 0.05f, {255, 218, 136, 255});
    }
    rlPopMatrix();
    rlEnableBackfaceCulling();
}

void draw_hud(const RangeSimulation& range, bool playing, bool slow_motion) {
    const int width = GetScreenWidth(), height = GetScreenHeight();
    const auto state = range.weapon_state();
    const auto& spec = weapon_specs()[range.selected_weapon()];
    DrawRectangle(0, 0, width, 4, accent);
    text("WEAPON LAB", 28, 24, 26, white);
    text("LIVE RANGE  /  " WEAPON_RANGE_VERSION, 29, 55, 15, muted);
    text(playing ? (slow_motion ? "SLOW / 0.1X" : "LIVE / 1.0X") : "PAUSED", width - 186, 28, 18, playing ? teal : accent);
    DrawRectangle(width - 260, 66, 232, 262, panel);
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "SHOTS  %03u    HITS  %03u", range.shots(), range.hits());
    text(buffer, width - 246, 82, 17, white);
    std::snprintf(buffer, sizeof(buffer), "ACCURACY  %03.0f%%", range.shots() ? 100.0 * range.hits() / range.shots() : 0.0);
    text(buffer, width - 246, 111, 17, muted);
    std::snprintf(buffer, sizeof(buffer), "SCORE  %04u", range.score());
    text(buffer, width - 246, 140, 18, accent);
    std::snprintf(buffer, sizeof(buffer), "IN FLIGHT  %02u", static_cast<unsigned>(range.projectiles().size()));
    text(buffer, width - 246, 174, 16, teal);
    if (range.last_impact_time() >= 0.0) {
        std::snprintf(buffer, sizeof(buffer), "FLIGHT  %.1f MS", range.last_flight_seconds() * 1000.0);
        text(buffer, width - 246, 201, 16, white);
        std::snprintf(buffer, sizeof(buffer), "IMPACT  %.1f M/S", range.last_impact_speed());
        text(buffer, width - 246, 228, 16, muted);
    } else text("FLIGHT  --", width - 246, 201, 16, muted);
    const auto& result = range.last_result();
    text(result_label(result.outcome), width - 246, 266, 18,
         result.outcome == ShotOutcome::None ? muted : result.outcome == ShotOutcome::Target ? teal : miss);
    if (result.outcome != ShotOutcome::None) {
        text(result.passed_water ? "PATH: WATER" : "PATH: AIR", width - 246, 295, 15, muted);
    }

    const int bottom = height - 172;
    DrawRectangle(24, bottom, 276, 122, panel);
    text(spec.name, 40, bottom + 12, 21, white);
    std::snprintf(buffer, sizeof(buffer), "%02u", state.rounds_remaining);
    text(buffer, 40, bottom + 39, 42, state.rounds_remaining ? white : accent);
    std::snprintf(buffer, sizeof(buffer), "/ %02u", spec.capacity);
    text(buffer, 106, bottom + 56, 20, muted);
    text(spec.mode == weapon_library::FireMode::SemiAutomatic ? "SEMI AUTO" : "FULL AUTO", 172, bottom + 45, 15, teal);
    std::snprintf(buffer, sizeof(buffer), "%03.0f RPM", 60.0 / spec.interval_seconds);
    text(buffer, 172, bottom + 68, 15, muted);
    std::snprintf(buffer, sizeof(buffer), "MUZZLE  %.0f M/S", spec.muzzle_speed_m_s);
    text(buffer, 40, bottom + 98, 15, muted);
    for (unsigned i = 0; i < weapon_specs().size(); ++i) {
        const int x = width - 368 + static_cast<int>(i) * 114;
        const bool selected = i == range.selected_weapon();
        DrawRectangle(x, bottom + 66, 106, 56, selected ? Color{71, 62, 46, 245} : panel);
        if (selected) DrawRectangle(x, bottom + 66, 106, 2, accent);
        std::snprintf(buffer, sizeof(buffer), "%u", i + 1);
        text(buffer, x + 10, bottom + 73, 14, muted);
        centered(weapon_specs()[i].name, x + 53, bottom + 97, 16, selected ? accent : white);
    }
    DrawRectangle(0, height - 34, width, 34, ink);
    centered("WASD MOVE   MOUSE AIM   LMB / F FIRE   1-3 WEAPON   V SLOW MOTION   R RESET   ESC PAUSE", width / 2, height - 23, width < 1200 ? 12 : 14, muted);
    const int cx = width / 2, cy = height / 2;
    const bool hit = range.time() - range.last_hit_time() < 0.16;
    const Color cross = hit ? accent : teal;
    DrawLine(cx - 13, cy, cx - 5, cy, cross); DrawLine(cx + 5, cy, cx + 13, cy, cross);
    DrawLine(cx, cy - 13, cx, cy - 5, cross); DrawLine(cx, cy + 5, cx, cy + 13, cross);
    DrawCircle(cx, cy, 1.5f, cross);
    if (hit) {
        for (int x : {-1, 1}) for (int y : {-1, 1}) DrawLine(cx + x * 18, cy + y * 18, cx + x * 24, cy + y * 24, accent);
    }
    if (range.time() - range.last_dry_time() < 0.9) centered("EMPTY  /  R TO RESET SESSION", cx, cy + 48, 18, accent);
    if (!playing) {
        DrawRectangle(0, 0, width, height, {6, 13, 20, 195});
        DrawRectangle(cx - 244, cy - 144, 488, 272, panel);
        DrawRectangle(cx - 244, cy - 144, 488, 3, accent);
        centered("FIRING RANGE", cx, cy - 108, 34, white);
        centered("Gravity. Air resistance. Water resistance.", cx, cy - 54, 18, muted);
        centered("Click or press ENTER to enter", cx, cy + 1, 24, accent);
        centered("Hold the rifle trigger. Tap the pistol trigger.", cx, cy + 53, 17, white);
        centered("V slows time. The blue volume is water.", cx, cy + 83, 16, muted);
    }
}

} // namespace

void draw_range(const RangeSimulation& range, bool playing, bool slow_motion) {
    Camera3D camera{};
    camera.position = rv(range.eye());
    camera.target = rv(range.eye() + range.aim());
    camera.up = {0, 1, 0};
    camera.fovy = 65;
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
    draw_held_weapon(range);
    EndMode3D();
    draw_target_labels(range, camera);
    draw_last_miss_label(range, camera);
    draw_hud(range, playing, slow_motion);
    EndDrawing();
}

} // namespace weapon_range
