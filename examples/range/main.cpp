#include "RangeRenderer.h"

#include <raylib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace {

struct KeyPresses {
    std::vector<int> keys;
    KeyPresses() {
        for (int key = GetKeyPressed(); key != 0; key = GetKeyPressed()) keys.push_back(key);
    }
    bool contains(int key) const { return std::find(keys.begin(), keys.end(), key) != keys.end(); }
};

int smoke(const char* screenshot) {
    weapon_range::RangeSimulation range;
    range.select_weapon(1);
    range.set_trigger(true);
    for (int i = 0; i < 42; ++i) range.advance(1.0 / 120.0);
    range.set_trigger(false);
    for (int frame = 0; frame < 4; ++frame) weapon_range::draw_range(range, true);
    const auto path = std::filesystem::path(screenshot);
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    Image image = LoadImageFromScreen();
    const bool exported = ExportImage(image, screenshot);
    UnloadImage(image);
    const bool okay = exported && range.shots() == 4 && range.hits() == 4 &&
                      range.weapon_state().rounds_remaining == 26;
    std::printf("Range smoke: shots=%u hits=%u ammo=%u screenshot=%s\n", range.shots(),
                range.hits(), range.weapon_state().rounds_remaining, exported ? "saved" : "failed");
    return okay ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const bool testing = argc >= 2 && std::strcmp(argv[1], "--smoke") == 0;
    SetConfigFlags(FLAG_MSAA_4X_HINT | (testing ? FLAG_WINDOW_HIDDEN : FLAG_WINDOW_RESIZABLE));
    InitWindow(1280, 800, "Weapon Library - Firing Range");
    if (!IsWindowReady()) return 1;
    SetWindowMinSize(1040, 680);
    SetExitKey(KEY_NULL);
    SetTargetFPS(120);
    if (testing) {
        const int result = smoke(argc >= 3 ? argv[2] : "build/qa/range.png");
        CloseWindow();
        return result;
    }

    weapon_range::RangeSimulation range;
    bool playing = false, armed = false;
    double accumulated = 0.0;
    constexpr double tick = 1.0 / 120.0;
    const auto pause = [&] {
        range.set_trigger(false);
        playing = armed = false;
        accumulated = 0.0;
        EnableCursor();
    };
    while (!WindowShouldClose()) {
        // GLFW can deliver key-down and key-up in one polling pass. The key
        // queue preserves such taps, unlike checking only the final key state.
        const KeyPresses pressed;
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
            for (int index = 0; index < 3; ++index) {
                if (pressed.contains(KEY_ONE + index)) {
                    range.select_weapon(static_cast<unsigned>(index));
                    changed = true;
                }
            }
            if (pressed.contains(KEY_R)) { range.reset(); changed = true; }
            if (changed) armed = false;
            const auto mouse = GetMouseDelta();
            range.look(mouse.x * 0.002, -mouse.y * 0.002);
            if (armed) {
                // Preserve a short tap even if press and release share a frame.
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || pressed.contains(KEY_F)) range.set_trigger(true);
                range.set_trigger(fire_down);
            } else if (!fire_down) armed = true;
            accumulated += std::min(static_cast<double>(GetFrameTime()), 0.1);
            const double forward = (IsKeyDown(KEY_W) ? 1.0 : 0.0) - (IsKeyDown(KEY_S) ? 1.0 : 0.0);
            const double strafe = (IsKeyDown(KEY_D) ? 1.0 : 0.0) - (IsKeyDown(KEY_A) ? 1.0 : 0.0);
            while (accumulated >= tick) {
                range.advance(tick, forward, strafe);
                accumulated -= tick;
            }
        }
        weapon_range::draw_range(range, playing);
    }
    EnableCursor();
    CloseWindow();
    return 0;
}
