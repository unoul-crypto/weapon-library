#include "RangeSimulation.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void hits_use_library_shots() {
    weapon_range::RangeSimulation range;
    range.set_trigger(true);
    range.advance(1.0);
    require(range.shots() == 1 && range.hits() == 1 && range.score() == 10,
            "Held pistol did not fire exactly one center hit");
    require(range.weapon_state().rounds_remaining == 11 && range.targets()[1].hits == 1,
            "Pistol ammo and target counter disagree");
    range.set_trigger(false);
    range.select_weapon(1);
    range.set_trigger(true);
    range.advance(0.5);
    range.set_trigger(false);
    require(range.shots() == 7 && range.hits() == 7 && range.weapon_state().rounds_remaining == 24,
            "Rifle shot events did not match range hits and ammo");
    range.advance(1.0);
    require(range.shots() == 7, "Released rifle kept firing");
}

void switch_preserves_magazines_and_releases_trigger() {
    weapon_range::RangeSimulation range;
    range.select_weapon(1);
    range.set_trigger(true);
    range.select_weapon(2);
    range.advance(1.0);
    require(range.shots() == 1 && range.weapon_state().rounds_remaining == 60,
            "Switching fired the new weapon or left the old trigger held");
    range.select_weapon(1);
    require(range.weapon_state().rounds_remaining == 29, "Switching refilled a magazine");
    range.set_trigger(true);
    range.advance(5.0);
    require(range.shots() == 30 && range.weapon_state().rounds_remaining == 0,
            "Magazine depletion produced lost or phantom shots");
    range.reset();
    require(range.shots() == 0 && range.hits() == 0 && range.score() == 0 &&
            range.targets()[1].hits == 0 && range.weapon_state().rounds_remaining == 12 &&
            !range.weapon_state().trigger_held, "Session reset retained stale state");
}

void aim_movement_and_room_bounds() {
    weapon_range::RangeSimulation range;
    range.look(-std::atan2(3.0, 14.0), 0.0);
    range.set_trigger(true);
    require(range.targets()[0].hits == 1, "Aim ray missed the left target center");
    range.set_trigger(false);
    range.advance(0.25);
    range.look(0.0, 0.8);
    range.set_trigger(true);
    require(range.shots() == 2 && range.hits() == 1 && !range.traces().back().hit,
            "A shot into the ceiling was counted as a hit");
    require(std::abs(range.traces().back().to.y - 6.0) < 1e-8, "Shot did not stop at the ceiling");
    range.advance(100.0, 1.0, 1.0);
    require(range.eye().x >= -5.5 && range.eye().x <= 5.5 &&
            range.eye().z >= 1.5 && range.eye().z <= 7.0, "Movement escaped the firing area");
}

} // namespace

int main() {
    try {
        hits_use_library_shots();
        switch_preserves_magazines_and_releases_trigger();
        aim_movement_and_room_bounds();
        std::cout << "PASS: Range hits, switching, reset, aiming and bounds\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: Range: " << error.what() << '\n';
        return 1;
    }
}
