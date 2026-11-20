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

void water_results_match_actual_impacts_for_single_projectile_weapons() {
    for (unsigned index = 0; index < weapon_range::weapon_specs().size(); ++index) {
        weapon_range::RangeSimulation range;
        range.select_weapon(index);
        if (range.projectiles_per_shot() != 1) continue;
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

void ranged_shots_follow_the_swaying_reticle() {
    using weapon_library::math::normalized;
    using weapon_library::math::dot;
    weapon_range::RangeSimulation range;
    range.set_aiming(true);
    range.advance(0.4);
    const auto center = range.shot_center_direction();
    const auto camera = range.aim();
    const auto spread = range.handling_state().spread_half_angle_rad;
    require(dot(camera, center) < std::cos(2.0 * spread),
            "Test pose did not move the reticle beyond the unswayed cone");
    range.set_trigger(true);
    require(range.projectiles().size() == 1, "Aimed shot did not spawn");
    const auto launch = range.projectiles().front().motion.state();
    const double target_z = range.targets()[1].center.z;
    const double travel_time = (target_z - launch.position.z) / launch.velocity.z;
    const auto convergence = launch.position + launch.velocity * travel_time;
    const auto sight_ray = normalized(convergence - range.eye());
    require(dot(sight_ray, center) >= std::cos(spread) - 1e-12 &&
            dot(sight_ray, camera) < std::cos(spread),
            "Projectile converged on the camera instead of the pre-shot reticle cone");
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

void sprint_lowers_the_weapon_and_requires_a_fresh_trigger_press() {
    weapon_range::RangeSimulation range;
    range.select_weapon(1);
    range.set_trigger(true);
    const auto start_z = range.eye().z;
    range.set_stance_input(false, true);
    range.advance(0.2, 1.0);
    require(range.shots() == 1 && range.handling_state().lowered_fraction == 1.0 &&
            start_z - range.eye().z > 0.8 && !range.attack_allowed(),
            "Sprint did not lower the weapon, increase speed or stop continuing fire");
    require(range.request_reload() == weapon_library::ReloadStatus::Busy, "Reload started with the weapon lowered");
    range.set_stance_input(true, false);
    range.advance(0.16);
    range.set_trigger(true);
    require(!range.attack_allowed() && range.shots() == 1 && range.handling_state().aim_fraction > 0.0,
            "Weapon fired before raising to ADS");
    range.advance(0.16);
    range.set_trigger(true);
    require(range.attack_allowed() && range.handling_state().aim_fraction == 1.0 && range.shots() == 1,
            "Holding fire across the raise fired without a fresh press");
    range.set_trigger(false);
    range.set_trigger(true);
    require(range.shots() == 2, "Fresh trigger press failed after raising");
}

void lowering_interrupts_queued_and_active_reloads() {
    for (double reload_elapsed : {0.0, 0.4}) {
        weapon_range::RangeSimulation range;
        range.select_weapon(1);
        range.set_trigger(true);
        range.set_trigger(false);
        require(range.request_reload() == weapon_library::ReloadStatus::Accepted, "Test reload did not start");
        range.advance(reload_elapsed);
        const auto before = range.weapon_state();
        const auto reserve = range.reserve_rounds();
        require(before.reload.active && before.reload.waiting_for_action == (reload_elapsed == 0.0),
                "Test did not reach the requested reload phase");
        range.set_trigger(true);
        range.set_stance_input(false, true);
        require(!range.weapon_state().reload.active && range.last_reload_cancelled() &&
                range.handling_state().target_stance == weapon_library::HandlingStance::Lowered,
                "Lowering did not cancel the reload and update the stance");
        range.advance(2.0);
        require(range.weapon_state().rounds_remaining == before.rounds_remaining && range.reserve_rounds() == reserve,
                "Interrupted reload transferred ammo");

        range.set_stance_input(false, false);
        range.advance(0.3);
        range.set_trigger(true);
        require(range.shots() == 1, "Trigger held through the interrupted reload fired after raising");
        range.set_trigger(false);
        range.set_trigger(true);
        range.set_trigger(false);
        require(range.shots() == 2 &&
                range.request_reload() == weapon_library::ReloadStatus::Accepted, "Fresh shot or reload was blocked after raising");
        range.advance(2.0);
        require(range.weapon_state().rounds_remaining == 30 && range.reserve_rounds() == reserve - 2,
                "A new reload after interruption failed to transfer ammo once");
    }
}

void selectable_fire_modes_and_lowering_interrupt_a_burst() {
    using weapon_library::FireMode;
    weapon_range::RangeSimulation range;
    require(!range.cycle_fire_mode() && range.weapon_state().fire_mode == FireMode::SemiAutomatic,
            "Single-mode pistol exposed another mode");
    range.select_weapon(1);
    require(range.cycle_fire_mode() && range.weapon_state().fire_mode == FireMode::SemiAutomatic &&
            range.cycle_fire_mode() && range.weapon_state().fire_mode == FireMode::Burst,
            "Rifle did not cycle through its configured modes");
    range.set_trigger(true);
    range.set_trigger(false);
    range.advance(0.25);
    require(range.shots() == 3 && range.weapon_state().rounds_remaining == 27, "Tap did not fire a three-shot burst");
    range.advance(0.1);
    range.set_trigger(true);
    range.set_trigger(false);
    range.set_stance_input(false, true);
    range.advance(0.4);
    require(range.shots() == 4 && range.weapon_state().burst_shots_remaining == 0,
            "A released-trigger burst kept firing while the weapon lowered");
    require(range.cycle_fire_mode() && range.weapon_state().fire_mode == FireMode::Automatic,
            "Rifle did not cycle back to its first mode");
}

void common_contacts_preserve_impact_geometry_and_simulation_time() {
    using weapon_library::HitKind;
    using weapon_library::ballistics::ProjectileEvent;
    using weapon_library::ballistics::ProjectileEventType;
    weapon_library::ballistics::ProjectileState projectile;
    projectile.position = {1, 2, 3};
    projectile.age_seconds = 0.4;
    const auto impact = ProjectileEvent{ProjectileEventType::Impact, projectile, 42, {0, 0, 1}}.hit_contact(1.6);
    const auto melee = weapon_library::MeleeHitEvent{1, 2, 42, 2.0, {1, 2, 3}, {0, 0, 1}, true}.hit_contact();
    require(impact && impact->kind == HitKind::Projectile && melee.kind == HitKind::Melee &&
            impact->object_id == melee.object_id && std::abs(impact->time_seconds - melee.time_seconds) < 1e-10 &&
            weapon_library::math::length(impact->position - melee.position) < 1e-10 &&
            weapon_library::math::length(impact->normal - melee.normal) < 1e-10,
            "Projectile and melee contacts did not use the same time and geometry format");
    require(!ProjectileEvent{ProjectileEventType::Expired, projectile}.hit_contact(1.6) &&
            !ProjectileEvent{ProjectileEventType::MediumChanged, projectile}.hit_contact(1.6),
            "A non-impact projectile event became a hit contact");
}

void approach_melee_target(weapon_range::RangeSimulation& range) {
    range.advance(2.3 / 3.2, 0, -1);
    range.advance(0.28, 1, 0);
    range.select_weapon(weapon_range::knife_index);
}

void melee_reach_phases_hold_and_ammunition() {
    weapon_range::RangeSimulation range;
    range.select_weapon(weapon_range::knife_index);
    range.set_trigger(true); range.set_trigger(false);
    range.advance(1);
    require(range.attacks() == 1 && range.melee_hits() == 0 && range.shots() == 0, "Knife hit from outside its reach");
    range.reset();
    approach_melee_target(range);
    range.set_trigger(true);
    range.advance(0.15);
    require(range.melee_hits() == 0 && range.melee_state().weapon.phase == weapon_library::Phase::Windup,
            "Knife hit during windup");
    range.advance(0.11);
    require(range.melee_hits() == 1 && range.targets()[3].hits == 1 && range.last_result().melee &&
            range.last_result().outcome == weapon_range::ShotOutcome::Target, "Active knife swing missed the near target");
    range.advance(2);
    range.set_trigger(true);
    require(range.attacks() == 1 && range.melee_hits() == 1, "Holding the attack repeated swings/hits");
    require(range.weapon_state().rounds_remaining == 0 && range.reserve_rounds() == 0 && range.projectiles().empty(),
            "Knife exposed ammunition or spawned a projectile");
    require(range.request_reload() == weapon_library::ReloadStatus::Unsupported, "Knife attempted to reload");
    range.set_aiming(true); range.advance(0.3);
    require(range.handling_state().aim_fraction == 0, "Knife used firearm ADS");
    range.set_trigger(false); range.set_trigger(true); range.set_trigger(false);
    range.advance(1);
    require(range.attacks() == 2 && range.melee_hits() == 2 && range.targets()[3].hits == 2, "Fresh press did not start another swing");
    for (unsigned i = 0; i < weapon_range::firearm_count; ++i) {
        range.select_weapon(i);
        require(range.weapon_state().rounds_remaining == weapon_range::weapon_specs()[i].capacity &&
                range.reserve_rounds() == weapon_range::weapon_specs()[i].reserve_rounds, "Melee spent firearm ammunition");
    }
}

void melee_cancellation_direction_cover_and_movement() {
    weapon_range::RangeSimulation range;
    approach_melee_target(range);
    range.set_trigger(true); range.set_trigger(false); range.advance(0.1);
    require(range.cancel_attack() == weapon_library::CommandStatus::Accepted, "Knife windup could not be cancelled");
    range.advance(1);
    require(range.melee_hits() == 0, "Cancelled knife hit later");
    range.set_trigger(true); range.set_trigger(false); range.advance(0.1);
    range.select_weapon(0); range.advance(1);
    require(range.melee_hits() == 0 && range.melee_state().weapon.phase == weapon_library::Phase::Ready,
            "Switching left a hidden knife attack running");
    range.select_weapon(weapon_range::knife_index);
    range.look(3.141592653589793, 0);
    range.set_trigger(true); range.set_trigger(false); range.advance(1);
    require(range.melee_hits() == 0, "Knife hit a target behind the attacker");

    range.reset();
    range.advance(3.0 / 3.2, 0, 1);
    range.advance(3.5 / 3.2, 1, 0);
    range.look(0, -0.25);
    range.select_weapon(weapon_range::knife_index);
    range.set_trigger(true); range.set_trigger(false); range.advance(1);
    require(range.last_result().melee && range.last_result().outcome == weapon_range::ShotOutcome::Cover &&
            range.melee_hits() == 0 && range.melee_state().blocked, "Knife ignored solid cover");

    weapon_range::RangeSimulation large, partitioned;
    large.advance(2.3 / 3.2, 0, -1); partitioned.advance(2.3 / 3.2, 0, -1);
    large.select_weapon(weapon_range::knife_index); partitioned.select_weapon(weapon_range::knife_index);
    large.set_trigger(true); partitioned.set_trigger(true);
    large.advance(1, 1, 0);
    for (int i = 0; i < 100; ++i) partitioned.advance(0.01, 1, 0);
    require(large.melee_hits() == 1 && partitioned.melee_hits() == 1 && large.attacks() == 1 && partitioned.attacks() == 1,
            "Moving melee attacks changed with update partitioning");
}

void melee_contacts_follow_aim_and_lie_on_real_surfaces() {
    weapon_range::RangeSimulation centered;
    approach_melee_target(centered);
    centered.set_trigger(true); centered.set_trigger(false); centered.advance(0.3);
    const auto target = centered.targets()[3];
    const auto point = centered.last_result().position;
    require(centered.melee_hits() == 1 && weapon_library::math::length(point - target.center) < 1e-8,
            "Centered knife thrust left a lateral or padded-plane contact offset");

    for (double offset : {-0.5, -0.2, 0.2, 0.5}) {
        weapon_range::RangeSimulation edge;
        approach_melee_target(edge);
        edge.look(std::atan2(offset, edge.eye().z - target.center.z), 0);
        edge.set_trigger(true); edge.set_trigger(false); edge.advance(0.3);
        const auto contact = edge.last_result().position;
        require(edge.melee_hits() == 1 && std::abs(contact.z - target.center.z) < 1e-9 &&
                std::hypot(contact.x - target.center.x, contact.y - target.center.y) <= target.radius + 1e-9 &&
                (contact.x - target.center.x) * offset > 0, "Knife contact did not lie on the aimed target surface");
    }
    weapon_range::RangeSimulation miss;
    approach_melee_target(miss);
    miss.look(std::atan2(0.65, miss.eye().z - target.center.z), 0);
    miss.set_trigger(true); miss.set_trigger(false); miss.advance(1);
    require(miss.melee_hits() == 0, "A miss outside the blade radius was pulled toward the crosshair target");

    weapon_range::RangeSimulation cover;
    cover.advance(3.0 / 3.2, 0, 1); cover.advance(3.5 / 3.2, 1, 0); cover.look(0, -0.25);
    cover.select_weapon(weapon_range::knife_index); cover.set_trigger(true); cover.set_trigger(false); cover.advance(1);
    const auto p = cover.last_result().position;
    const auto box = weapon_range::firing_cover();
    require(cover.last_result().outcome == weapon_range::ShotOutcome::Cover &&
            p.x >= box.min.x && p.x <= box.max.x && p.y >= box.min.y && p.y <= box.max.y && p.z >= box.min.z && p.z <= box.max.z &&
            (std::abs(p.y - box.max.y) < 1e-8 || std::abs(p.z - box.max.z) < 1e-8), "Cover marker was left on an inflated collider");

    weapon_range::RangeSimulation wall;
    wall.advance(5.5 / 3.2, 0, -1); wall.look(-1.5707963267948966, 0);
    wall.select_weapon(weapon_range::knife_index); wall.set_trigger(true); wall.set_trigger(false); wall.advance(1);
    require(wall.last_result().outcome == weapon_range::ShotOutcome::Wall && std::abs(wall.last_result().position.x + 7) < 1e-8,
            "Wall marker was left on a padded plane");
}

} // namespace

int main() {
    try {
        water_results_match_actual_impacts_for_single_projectile_weapons();
        wall_results_and_marker_lifetime();
        hits_use_library_shots();
        switch_preserves_magazines_and_releases_trigger();
        aim_movement_and_room_bounds();
        water_and_weapon_speed_affect_flight();
        aiming_recoil_and_muzzle_are_connected();
        ranged_shots_follow_the_swaying_reticle();
        cover_blocks_the_muzzle_even_when_the_eye_sees_above_it();
        range_handling_preserves_catch_up_timing_and_ignores_dry_fire();
        reload_preserves_scene_stats_and_ammunition();
        sprint_lowers_the_weapon_and_requires_a_fresh_trigger_press();
        lowering_interrupts_queued_and_active_reloads();
        selectable_fire_modes_and_lowering_interrupt_a_burst();
        common_contacts_preserve_impact_geometry_and_simulation_time();
        melee_reach_phases_hold_and_ammunition();
        melee_cancellation_direction_cover_and_movement();
        melee_contacts_follow_aim_and_lie_on_real_surfaces();
        std::cout << "PASS: Range ballistics, handling, cover, firing, switching and reset\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: Range: " << error.what() << '\n';
        return 1;
    }
}
