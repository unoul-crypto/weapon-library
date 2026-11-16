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
    range.set_aiming(true);
    range.advance(0.3);
    range.set_trigger(true);
    require(range.hits() == 0 && range.projectiles().size() == 1, "Shot still hits instantly");
    range.advance(0.01);
    require(range.hits() == 0 && range.projectiles().size() == 1, "Projectile reached target before its flight time");
    range.advance(0.99);
    require(range.shots() == 1 && range.hits() == 1 && range.score() == 10,
            "Held pistol did not fire exactly one center hit");
    require(range.weapon_state().rounds_remaining == 11 && range.targets()[1].hits == 1,
            "Pistol ammo and target counter disagree");
    require(range.last_result().outcome == weapon_range::ShotOutcome::Target && !range.last_result().passed_water,
            "Air target impact was not reported correctly");
    range.set_trigger(false);
    range.select_weapon(1);
    range.advance(0.3);
    range.set_trigger(true);
    range.advance(0.5);
    range.set_trigger(false);
    if (range.shots() != 7 || range.hits() != 6 || range.weapon_state().rounds_remaining != 24) {
        std::cerr << "Rifle at release: shots=" << range.shots() << " hits=" << range.hits()
                  << " ammo=" << range.weapon_state().rounds_remaining << '\n';
    }
    require(range.shots() == 7 && range.hits() == 6 && range.weapon_state().rounds_remaining == 24,
            "Rifle shot events did not match range hits and ammo");
    require(range.projectiles().size() == 1 && range.projectiles()[0].motion.state().age_seconds < 1e-8,
            "End-of-update shot was advanced before it was spawned");
    range.advance(1.0);
    require(range.shots() == 7 && range.hits() == 7, "Released rifle kept firing or its last bullet disappeared");
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
            !range.weapon_state().trigger_held && range.projectiles().empty() && range.recent_results().empty() &&
            range.last_result().outcome == weapon_range::ShotOutcome::None, "Session reset retained stale state");
}

void aim_movement_and_room_bounds() {
    weapon_range::RangeSimulation range;
    range.look(-std::atan2(3.0, 14.0), 0.0);
    range.set_trigger(true);
    range.advance(0.1);
    require(range.targets()[0].hits == 1, "Aim ray missed the left target center");
    range.set_trigger(false);
    range.advance(0.25);
    range.look(0.0, 0.8);
    range.set_trigger(true);
    range.advance(0.1);
    require(range.shots() == 2 && range.hits() == 1 && !range.traces().back().hit,
            "A shot into the ceiling was counted as a hit");
    require(std::abs(range.traces().back().to.y - 6.0) < 1e-8, "Shot did not stop at the ceiling");
    require(range.last_result().outcome == weapon_range::ShotOutcome::Ceiling, "Ceiling impact was mislabelled");
    range.advance(100.0, 1.0, 1.0);
    require(range.eye().x >= -5.5 && range.eye().x <= 5.5 &&
            range.eye().z >= 1.5 && range.eye().z <= 7.0, "Movement escaped the firing area");
}

void water_and_weapon_speed_affect_flight() {
    weapon_range::RangeSimulation air;
    air.set_trigger(true);
    const double pistol_speed = weapon_library::ballistics::length(air.projectiles()[0].motion.state().velocity);
    air.set_trigger(false);
    air.advance(0.1);
    require(air.last_flight_seconds() > 0.06 && air.last_flight_seconds() < 0.08 &&
            air.last_impact_speed() < pistol_speed, "Air flight time or resistance is incorrect");

    weapon_range::RangeSimulation water;
    water.select_weapon(2);
    water.look(std::atan2(3.0, 32.0), 0.0);
    water.set_trigger(true);
    require(weapon_library::ballistics::length(water.projectiles()[0].motion.state().velocity) > pistol_speed,
            "Weapon selection did not change launch speed");
    water.set_trigger(false);
    water.advance(1.0);
    require(water.water_entries() == 1 && water.targets()[2].hits == 1,
            "Water lane failed to cross the water volume and hit the target");
    require(water.last_impact_speed() < 200.0 && water.last_flight_seconds() > 0.07,
            "Water did not slow down the projectile");

    weapon_range::RangeSimulation moving;
    moving.advance(0.01, 0.0, 1.0);
    moving.set_trigger(true);
    const auto relative_velocity = moving.projectiles()[0].motion.state().velocity - weapon_range::Vec3{3.2, 0, 0};
    require(std::abs(weapon_library::math::length(relative_velocity) - weapon_range::weapon_specs()[0].muzzle_speed_m_s) < 1e-8,
            "Projectile did not inherit shooter velocity");
}

void water_results_match_actual_impacts_for_every_weapon() {
    for (unsigned index = 0; index < weapon_range::weapon_specs().size(); ++index) {
        weapon_range::RangeSimulation range;
        range.select_weapon(index);
        range.look(std::atan2(3.0, 32.0), 0.0);
        range.set_trigger(true);
        range.set_trigger(false);
        for (int tick = 0; tick < 720 && !range.projectiles().empty(); ++tick) range.advance(1.0 / 120.0);
        require(range.projectiles().empty() && range.water_entries() == 1 && range.recent_results().size() == 1,
                "Water shot did not produce exactly one terminal result");
        const auto& result = range.last_result();
        require(result.passed_water && result.time_seconds > 0.0 && result.speed_m_s > 0.0,
                "Terminal result lost its flight data or water history");
        const bool target_hit = index == 2;
        require(range.hits() == (target_hit ? 1u : 0u) && range.targets()[2].hits == range.hits(),
                "Water impact and target counters disagree");
        require(result.outcome == (target_hit ? weapon_range::ShotOutcome::Target : weapon_range::ShotOutcome::Floor),
                "Water shot was incorrectly classified as a hit or miss");
        if (!target_hit) {
            require(std::abs(result.position.y) < 1e-8 && result.position.z > range.targets()[2].center.z,
                    "Miss marker did not identify the floor impact before the target");
        }
        const auto& marker = range.recent_results()[0];
        require(weapon_library::ballistics::length(marker.position - result.position) < 1e-8,
                "Impact marker does not match the physical impact");
    }
}

void wall_results_and_marker_lifetime() {
    weapon_range::RangeSimulation range;
    range.look(3.141592653589793, 0.0);
    range.set_trigger(true);
    range.set_trigger(false);
    range.advance(0.05);
    require(range.hits() == 0 && range.last_result().outcome == weapon_range::ShotOutcome::Wall &&
            !range.last_result().passed_water && range.recent_results().size() == 1,
            "Wall miss was not exposed to presentation");
    range.advance(2.1);
    require(range.recent_results().empty() && range.last_result().outcome == weapon_range::ShotOutcome::Wall,
            "Marker cleanup erased the last result or retained stale marks");
}

void aiming_recoil_and_muzzle_are_connected() {
    weapon_range::RangeSimulation range;
    const double hip_spread = range.handling_state().spread_half_angle_rad;
    const auto hip_muzzle = range.weapon_pose().muzzle;
    range.set_aiming(true);
    range.advance(0.3);
    require(range.handling_state().aim_fraction == 1.0 && range.handling_state().spread_half_angle_rad < hip_spread,
            "Range ADS did not affect the handling module");
    const auto muzzle = range.weapon_pose().muzzle;
    require(weapon_library::math::length(muzzle - hip_muzzle) > 0.1, "ADS did not align the weapon pose");
    range.set_trigger(true);
    range.set_trigger(false);
    require(weapon_library::math::length(range.projectiles()[0].motion.state().position - muzzle) < 1e-9 &&
            weapon_library::math::length(muzzle - range.eye()) > 0.5, "Projectile still spawned at the camera");
    require(range.handling_state().recoil.pitch_rad > 0 && range.handling_state().bloom_rad > 0 && range.aim().y > 0,
            "Actual shot did not affect aim and spread");
    const double recoil = range.handling_state().recoil.pitch_rad;
    range.select_weapon(1);
    require(range.handling_state().recoil.pitch_rad == 0 && range.handling_state().bloom_rad == 0,
            "Weapon switch shared recoil between instances");
    range.advance(0.1);
    range.select_weapon(0);
    require(range.handling_state().recoil.pitch_rad > 0 && range.handling_state().recoil.pitch_rad < recoil,
            "Weapon switch reset recoil instead of letting it recover");
    range.reset();
    require(range.handling_state().aim_fraction == 0 && range.handling_state().bloom_rad == 0 &&
            range.handling_state().recoil.pitch_rad == 0, "Session reset retained handling state");
}

void cover_blocks_the_muzzle_even_when_the_eye_sees_above_it() {
    weapon_range::RangeSimulation range;
    range.select_weapon(1);
    range.advance(2.5 / 3.2, 0, 1);
    range.advance(3.5 / 3.2, 1, 0);
    range.set_movement(0, 0);
    require(range.eye().y > weapon_range::firing_cover().max.y &&
            range.weapon_pose().muzzle.y < weapon_range::firing_cover().max.y,
            "Cover test did not place the barrel below the visible sight line");
    range.set_trigger(true);
    range.set_trigger(false);
    range.advance(0.05);
    require(range.last_result().outcome == weapon_range::ShotOutcome::Cover && range.hits() == 0,
            "A camera-visible target allowed the muzzle to shoot through cover");
    range.set_aiming(true);
    range.advance(0.3);
    const auto muzzle = range.weapon_pose().muzzle;
    require(muzzle.y > weapon_range::firing_cover().max.y, "ADS did not raise the barrel above cover");
    range.set_trigger(true);
    range.set_trigger(false);
    require(weapon_library::math::length(range.projectiles()[0].motion.state().position - muzzle) < 1e-9,
            "Clear muzzle was unnecessarily clamped against cover");
    range.advance(0.01);
    require(!range.projectiles().empty() && range.projectiles()[0].motion.state().position.z < 0.0,
            "Raised muzzle failed to clear cover");
}

void range_handling_preserves_catch_up_timing_and_ignores_dry_fire() {
    weapon_range::RangeSimulation single, partitioned;
    for (auto* range : {&single, &partitioned}) {
        range->select_weapon(1);
        range->set_aiming(true);
        range->advance(0.3);
        range->set_trigger(true);
    }
    single.advance(0.5);
    for (int i = 0; i < 60; ++i) partitioned.advance(1.0 / 120.0);
    const auto a = single.handling_state(), b = partitioned.handling_state();
    require(single.shots() == partitioned.shots() && single.shots() == 6 &&
            std::abs(a.bloom_rad - b.bloom_rad) < 1e-10 &&
            std::abs(a.recoil.pitch_rad - b.recoil.pitch_rad) < 1e-10 &&
            single.projectiles().size() == partitioned.projectiles().size(), "Range catch-up changed handling state");
    for (std::size_t i = 0; i < single.projectiles().size(); ++i) {
        require(weapon_library::math::length(single.projectiles()[i].motion.state().position -
                                             partitioned.projectiles()[i].motion.state().position) < 1e-5,
                "Range catch-up changed the spawned trajectory");
    }
    single.advance(4.0);
    single.set_trigger(false);
    const auto before = single.handling_state();
    single.set_trigger(true);
    const auto after = single.handling_state();
    require(single.weapon_state().rounds_remaining == 0 && before.bloom_rad == after.bloom_rad &&
            before.recoil.pitch_rad == after.recoil.pitch_rad && before.recoil.yaw_rad == after.recoil.yaw_rad,
            "Dry firing created recoil or bloom");
}

void reload_preserves_scene_stats_and_ammunition() {
    weapon_range::RangeSimulation range;
    const auto reserve = range.reserve_rounds();
    require(range.request_reload() == weapon_library::ReloadStatus::MagazineFull, "Full scene magazine accepted reload");
    range.set_trigger(true); range.set_trigger(false);
    require(range.request_reload() == weapon_library::ReloadStatus::Accepted && range.weapon_state().reload.active,
            "Scene did not queue reload during shot recovery");
    range.select_weapon(0);
    require(range.weapon_state().reload.active, "Selecting the current weapon cancelled reload");
    range.set_aiming(true);
    require(!range.handling_state().aiming, "Reload did not lower aiming");
    range.advance(0.4);
    require(range.weapon_state().rounds_remaining == 11 && range.reserve_rounds() == reserve && range.shots() == 1,
            "Reload reset the scene or moved ammo early");
    range.advance(2.0);
    require(range.weapon_state().rounds_remaining == 12 && range.reserve_rounds() == reserve - 1 &&
            range.shots() == 1 && range.hits() == 1 && !range.weapon_state().reload.active,
            "Reload lost ammo, stats or created extra shots");
    range.set_trigger(true); range.set_trigger(false);
    range.request_reload(); range.advance(0.3);
    range.select_weapon(2); range.advance(3.0); range.select_weapon(0);
    require(!range.weapon_state().reload.active && range.weapon_state().rounds_remaining == 11 &&
            range.reserve_rounds() == reserve - 1, "Switching failed to cancel reload without spending reserve");
    range.request_reload(); range.advance(0.3);
    require(range.cancel_reload() == weapon_library::ReloadStatus::Accepted, "Scene reload cancellation failed");
    range.advance(2.0);
    require(range.weapon_state().rounds_remaining == 11 && range.reserve_rounds() == reserve - 1,
            "Cancelled scene reload completed later");
    range.reset();
    require(range.weapon_state().rounds_remaining == 12 && range.reserve_rounds() == reserve &&
            !range.weapon_state().reload.active && range.shots() == 0, "Reset retained reload state");
}

} // namespace

int main() {
    try {
        water_results_match_actual_impacts_for_every_weapon();
        wall_results_and_marker_lifetime();
        hits_use_library_shots();
        switch_preserves_magazines_and_releases_trigger();
        aim_movement_and_room_bounds();
        water_and_weapon_speed_affect_flight();
        aiming_recoil_and_muzzle_are_connected();
        cover_blocks_the_muzzle_even_when_the_eye_sees_above_it();
        range_handling_preserves_catch_up_timing_and_ignores_dry_fire();
        reload_preserves_scene_stats_and_ammunition();
        std::cout << "PASS: Range ballistics, handling, cover, firing, switching and reset\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: Range: " << error.what() << '\n';
        return 1;
    }
}
