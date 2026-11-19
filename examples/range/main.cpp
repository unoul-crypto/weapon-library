#include "RangeRenderer.h"

#include <raylib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <cmath>
#include <vector>

namespace {

struct KeyPresses {
    std::vector<int> keys;
    KeyPresses() {
        for (int key = GetKeyPressed(); key != 0; key = GetKeyPressed()) keys.push_back(key);
    }
    bool contains(int key) const { return std::find(keys.begin(), keys.end(), key) != keys.end(); }
};

int smoke(const char* screenshot, bool water, bool reload, bool melee, weapon_range::RangeRenderer& renderer) {
    weapon_range::RangeSimulation range;
    const bool visuals_loaded = renderer.reload_visuals(range);
    bool okay;
    if (melee) {
        range.advance(2.3 / 3.2, 0, -1);
        range.advance(0.28, 1, 0);
        range.select_weapon(weapon_range::knife_index);
        range.set_trigger(true);
        range.advance(0.26);
        okay = range.attacks() == 1 && range.melee_hits() == 1 && range.targets()[3].hits == 1 &&
               range.shots() == 0 && range.projectiles().empty() && range.is_melee();
    } else if (reload) {
        range.select_weapon(1);
        range.set_trigger(true);
        range.advance(0.25);
        range.set_trigger(false);
        okay = range.request_reload() == weapon_library::ReloadStatus::Accepted;
        range.advance(0.9);
        okay = okay && range.weapon_state().reload.active && !range.weapon_state().reload.waiting_for_action &&
               range.weapon_state().rounds_remaining == 27 && range.reserve_rounds() == 150;
    } else if (water) {
        range.select_weapon(2);
        range.look(std::atan2(3.0, 32.0), 0.0);
        range.set_trigger(true);
        range.set_trigger(false);
        range.advance(0.3);
        okay = range.shots() == 1 && range.hits() == 1 && range.water_entries() == 1 &&
               range.last_flight_seconds() > 0.07 && range.last_impact_speed() < 200.0;
        range.set_trigger(true);
        range.set_trigger(false);
        range.advance(0.055);
        okay = okay && range.projectiles().size() == 1 &&
               range.projectiles()[0].motion.state().medium.id == weapon_library::ballistics::Medium::water().id;
    } else {
        range.select_weapon(1);
        range.set_aiming(true);
        range.advance(0.3);
        range.set_trigger(true);
        for (int i = 0; i < 42; ++i) range.advance(1.0 / 120.0);
        range.set_trigger(false);
        okay = range.shots() == 4 && range.hits() == 4 && range.weapon_state().rounds_remaining == 26;
    }
    if (reload || melee) {
        // Refresh assets midway through reload, as with F5: no gameplay reset.
        const auto attack_progress = range.melee_state().attack_progress;
        okay = renderer.reload_visuals(range) && okay;
        if (reload) okay = okay && range.weapon_state().reload.active &&
            range.weapon_state().rounds_remaining == 27 && range.reserve_rounds() == 150;
        if (melee) okay = okay && range.melee_state().attack_progress == attack_progress &&
            range.melee_state().weapon.phase == weapon_library::Phase::Active && range.attacks() == 1;
    }
    for (int frame = 0; frame < 4; ++frame) renderer.draw(range, true, water);
    const auto path = std::filesystem::path(screenshot);
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    Image image = LoadImageFromScreen();
    const bool exported = ExportImage(image, screenshot);
    UnloadImage(image);
    if (reload) {
        range.advance(1.0);
        renderer.draw(range, true, false); // Completed reload restores the clip's ready frame.
        okay = okay && !range.weapon_state().reload.active && range.weapon_state().rounds_remaining == 30 &&
               range.reserve_rounds() == 147 && range.shots() == 3;
    }
    if (melee) {
        range.advance(1);
        range.set_trigger(true);
        okay = okay && range.attacks() == 1 && range.melee_hits() == 1;
        range.set_trigger(false); range.set_trigger(true); range.set_trigger(false);
        range.advance(1);
        okay = okay && range.attacks() == 2 && range.melee_hits() == 2 && range.shots() == 0;
    }
    if (melee) std::printf("Range smoke (melee): swings=%u hits=%u screenshot=%s\n",
                          range.attacks(), range.melee_hits(), exported ? "saved" : "failed");
    else std::printf("Range smoke (%s): shots=%u hits=%u ammo=%u flight=%.2fms impact=%.2fm/s screenshot=%s\n",
                     reload ? "reload" : water ? "water" : "air", range.shots(), range.hits(), range.weapon_state().rounds_remaining,
                     range.last_flight_seconds() * 1000.0, range.last_impact_speed(), exported ? "saved" : "failed");
    return okay && exported && visuals_loaded ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const bool melee_test = argc >= 2 && std::strcmp(argv[1], "--smoke-melee") == 0;
    const bool water_test = argc >= 2 && std::strcmp(argv[1], "--smoke-water") == 0;
    const bool reload_test = argc >= 2 && std::strcmp(argv[1], "--smoke-reload") == 0;
    const bool testing = melee_test || water_test || reload_test || (argc >= 2 && std::strcmp(argv[1], "--smoke") == 0);
    SetConfigFlags(FLAG_MSAA_4X_HINT | (testing ? FLAG_WINDOW_HIDDEN : FLAG_WINDOW_RESIZABLE));
    InitWindow(1280, 800, "Weapon Library - Firing Range");
    if (!IsWindowReady()) return 1;
    SetWindowMinSize(1040, 680);
    SetExitKey(KEY_NULL);
    SetTargetFPS(120);
    auto visual_directory = std::filesystem::path(GetApplicationDirectory()) / "visuals";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--visuals") == 0 && i + 1 < argc) visual_directory = argv[++i];
    }
    auto renderer = std::make_unique<weapon_range::RangeRenderer>(visual_directory);
    if (testing) {
        const int result = smoke(argc >= 3 ? argv[2] : "build/qa/range.png", water_test, reload_test, melee_test, *renderer);
        renderer.reset();
        CloseWindow();
        return result;
    }

    weapon_range::RangeSimulation range;
    renderer->reload_visuals(range);
    bool playing = false, armed = false, slow_motion = false;
    bool show_crosshair = true;
    double accumulated = 0.0;
    const auto pause = [&] {
        range.set_trigger(false);
        range.set_aiming(false);
        playing = armed = false;
        accumulated = 0.0;
        EnableCursor();
    };
    while (!WindowShouldClose()) {
        // GLFW can deliver key-down and key-up in one polling pass. The key
        // queue preserves such taps, unlike checking only the final key state.
        const KeyPresses pressed;
        if (pressed.contains(KEY_F5)) renderer->reload_visuals(range);
        if (pressed.contains(KEY_C)) show_crosshair = !show_crosshair;
        const bool fire_down = IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsKeyDown(KEY_F);
        if (!IsWindowFocused()) pause();
        if (pressed.contains(KEY_ESCAPE)) pause();
        if (!playing) {
            if (IsWindowFocused() && (pressed.contains(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
                playing = true;
                armed = false; // The entry click cannot also fire a shot.
                accumulated = 0.0;
                DisableCursor();
                GetMouseDelta();
            }
        } else {
            bool changed = false;
            for (int index = 0; index < static_cast<int>(weapon_range::weapon_count); ++index) {
                if (pressed.contains(KEY_ONE + index)) {
                    range.select_weapon(static_cast<unsigned>(index));
                    changed = true;
                }
            }
            if (pressed.contains(KEY_HOME)) { range.reset(); changed = true; }
            if (pressed.contains(KEY_R)) {
                if (range.request_reload() == weapon_library::ReloadStatus::Accepted) changed = true;
            }
            if (pressed.contains(KEY_Q)) {
                if (range.is_melee()) {
                    if (range.cancel_attack() == weapon_library::CommandStatus::Accepted) changed = true;
                } else if (range.cancel_reload() == weapon_library::ReloadStatus::Accepted) changed = true;
            }
            if (pressed.contains(KEY_V)) { slow_motion = !slow_motion; accumulated = 0.0; }
            if (changed) armed = false;
            const auto mouse = GetMouseDelta();
            const double sensitivity = 0.002 * (1.0 - 0.35 * range.handling_state().aim_fraction);
            range.look(mouse.x * sensitivity, -mouse.y * sensitivity);
            range.set_aiming(IsMouseButtonDown(MOUSE_BUTTON_RIGHT));
            const double forward = (IsKeyDown(KEY_W) ? 1.0 : 0.0) - (IsKeyDown(KEY_S) ? 1.0 : 0.0);
            const double strafe = (IsKeyDown(KEY_D) ? 1.0 : 0.0) - (IsKeyDown(KEY_A) ? 1.0 : 0.0);
            range.set_movement(forward, strafe);
            if (armed) {
                // Preserve a short tap even if press and release share a frame.
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || pressed.contains(KEY_F)) range.set_trigger(true);
                range.set_trigger(fire_down);
            } else if (!fire_down) armed = true;
            accumulated += std::min(static_cast<double>(GetFrameTime()), 0.1) * (slow_motion ? 0.1 : 1.0);
            const double tick = slow_motion ? 1.0 / 1200.0 : 1.0 / 120.0;
            while (accumulated >= tick) {
                range.advance(tick, forward, strafe);
                accumulated -= tick;
            }
        }
        renderer->draw(range, playing, slow_motion, show_crosshair);
    }
    EnableCursor();
    renderer.reset();
    CloseWindow();
    return 0;
}
