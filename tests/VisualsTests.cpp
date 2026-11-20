#include "RangeSimulation.h"
#include "RangeVisuals.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool same(weapon_range::Vec3 a, weapon_range::Vec3 b) { return weapon_library::math::length(a - b) < 1e-10; }
struct ConfigFixture {
    std::filesystem::path directory;
    ConfigFixture() {
        directory = std::filesystem::temp_directory_path() / "weapon_library_visual_tests";
        std::filesystem::create_directories(directory);
    }
    ~ConfigFixture() { std::filesystem::remove(directory / "test.weapon"); std::filesystem::remove(directory); }
    std::filesystem::path write(const char* contents) {
        const auto file = directory / "test.weapon";
        std::ofstream out(file);
        out << contents;
        out.close();
        if (!out) throw std::runtime_error("Cannot write test config");
        return file;
    }
};
void configs_and_muzzle(const std::filesystem::path& assets) {
    for (unsigned i = 0; i < weapon_range::weapon_count; ++i) {
        const char* names[] = {"pistol.weapon", "rifle.weapon", "turret.weapon", "knife.weapon"};
        const auto visual = weapon_range::load_visual(assets / names[i], i);
        if (i == weapon_range::knife_index)
            require(!visual.presentation.definition().attack_keyframes.empty(), "Bundled knife motion is missing");
        else require(!visual.presentation.definition().reload_keyframes.empty(), "Bundled reload motion is missing");
        if (i == 1) require(std::filesystem::is_regular_file(visual.model_file), "Bundled model path failed");
    }
    ConfigFixture fixture;
    const auto visual = weapon_range::load_visual(fixture.write(
        "model = models/custom.glb\nreload_clip = Reload\n"
        "hip_offset = 0.3 -0.1 0.6\naimed_offset = 0 0 0.5\nmuzzle_offset = 0.1 0.02 0.7\n"
        "reload_interpolation = linear\nreload_key = 0 0 0 0 0 0 0\nreload_key = 1 0 -0.2 0 0 0 40\n"), 0);
    require(visual.model_file == fixture.directory / "models/custom.glb" && visual.animation_file == visual.model_file,
            "Asset paths were not resolved relative to the config");
    require(visual.reload_clip == "Reload", "Reload clip name was lost");
    weapon_range::RangeSimulation range;
    range.set_presentation(0, visual.presentation);
    auto pose = range.weapon_pose();
    require(same(pose.origin, range.eye() + weapon_range::Vec3{0.3, -0.1, -0.6}) &&
            same(pose.muzzle, pose.origin + weapon_range::Vec3{0.1, 0.02, -0.7}), "Custom muzzle was not used by the simulation");
    range.set_trigger(true);
    require(same(range.projectiles().front().motion.state().position, pose.muzzle), "Projectile ignored the configured socket");
    range.set_trigger(false);
    range.reset();
    require(same(range.weapon_pose().muzzle, pose.muzzle), "Reset lost visual configuration");
    range.set_aiming(true); range.advance(0.3);
    require(same(range.weapon_pose().origin, range.eye() + weapon_range::Vec3{0, 0, -0.5}), "Custom ADS mount was ignored");
    for (const auto* invalid : {"misspelled = 1\n", "model_scale = 1 0 1\n", "hip_offset = 1 2 3 junk\n",
            "reload_key = 0.5 0 0 0 0 0 0\n", "reload_clip = Reload\n", "model = a.obj\nmodel = b.obj\n",
            "reload_motion = none\nreload_key = 0 0 0 0 0 0 0\n", "mass_kg = -1\n",
            "walk_sway_multiplier = -1\n", "turn_sway_multiplier = -1\n", "aimed_sway_multiplier = -1\n",
            "aimed_sway_amplitude_rad = 0.02\n", "aimed_sway_amplitude_rad = 0.02 2\n",
            "aimed_sway_frequency_hz = -0.5\n", "raise_to_aim_seconds = -0.1\n",
            "lower_key = 0.5 0 0 0 0 0 0\n", "projectiles_per_shot = 0\n",
            "projectiles_per_shot = 3.5\n", "projectiles_per_shot = 65\n", "fire_modes =\n",
            "fire_modes = single\n", "fire_modes = burst burst\n", "burst_size = 1\n"}) {
        bool rejected = false;
        try { weapon_range::load_visual(fixture.write(invalid), 0); }
        catch (const std::runtime_error& error) {
            rejected = std::string(error.what()).find("test.weapon:") != std::string::npos;
        }
        require(rejected, "Malformed visual config lacked a useful error");
    }
    const auto disabled = weapon_range::load_visual(fixture.write("reload_motion = none\n"), 0);
    require(disabled.presentation.definition().reload_keyframes.empty(), "Disabling keyframe motion failed");
    const auto knife = weapon_range::load_visual(fixture.write(
        "model = models/knife.glb\nattack_clip = Slash\nattack_interpolation = linear\n"
        "attack_key = 0 0 0 0 0 0 0\nattack_key = 1 1 0 0 0 30 0\n"), weapon_range::knife_index);
    require(knife.attack_clip == "Slash" && knife.animation_file == knife.model_file &&
            knife.presentation.definition().interpolation == weapon_library::PoseInterpolation::Smooth &&
            same(knife.presentation.sample_attack(0.5).offset, {0.5, 0, 0}), "Attack animation configuration was ignored");

    weapon_range::RangeSimulation light, heavy;
    heavy.set_presentation(0, weapon_range::load_visual(fixture.write("mass_kg = 10\n"), 0).presentation);
    light.look(0.1, 0.0);
    heavy.look(0.1, 0.0);
    require(heavy.sway_pose().rotation_degrees.y < light.sway_pose().rotation_degrees.y &&
            same(light.aim(), heavy.aim()) && same(light.weapon_pose().muzzle, heavy.weapon_pose().muzzle),
            "Configured mass did not change the model independently of aim and the muzzle socket");
    light.set_trigger(true);
    heavy.set_trigger(true);
    require(same(light.projectiles().front().motion.state().velocity, heavy.projectiles().front().motion.state().velocity),
            "Visual sway changed the shot direction");
}

void sway_settings_update_existing_motion() {
    ConfigFixture fixture;
    weapon_range::RangeSimulation range;
    range.look(0.1, 0.05);
    range.advance(0.13, 1.0);
    const auto original = range.sway_pose();
    const auto original_mass = range.presentation().definition().mass_kg;
    const auto visual = weapon_range::load_visual(fixture.write(
        "walk_sway_multiplier = 0.5\nturn_sway_multiplier = 0\naimed_sway_multiplier = 1\n"), 0);
    range.set_presentation(0, visual.presentation);
    const auto tuned = range.sway_pose();
    require(range.presentation().definition().mass_kg == original_mass &&
            std::abs(tuned.rotation_degrees.z - original.rotation_degrees.z * 0.5) < 1e-10 &&
            tuned.rotation_degrees.y == 0.0, "Reloading sway settings lost the mass or current motion");
    range.set_aiming(true);
    range.advance(0.3, 1.0);
    const auto aimed = range.sway_pose();
    auto weaker = visual.presentation.definition();
    weaker.sway_strength.aimed_multiplier = 0.25;
    range.set_presentation(0, weapon_library::WeaponPresentation(weaker));
    require(range.handling_state().aim_fraction == 1.0 && std::abs(aimed.rotation_degrees.z) > 0.01 &&
            same(range.sway_pose().rotation_degrees, aimed.rotation_degrees * 0.25),
            "The configured aimed multiplier was not applied to current movement");
}
void aim_sway_config_applies_per_weapon_and_survives_reset() {
    ConfigFixture fixture;
    weapon_range::RangeSimulation range;
    range.set_aiming(true);
    range.advance(0.5);
    range.set_trigger(true);
    range.set_trigger(false);
    const auto before = range.handling_state();
    const auto ammo = range.weapon_state().rounds_remaining;
    const auto config = weapon_range::load_visual(fixture.write(
        "aimed_sway_amplitude_rad = 0.02 0\naimed_sway_frequency_hz = 0.5\n"), 0);
    range.set_aim_sway(0, config.aim_sway);
    const auto after = range.handling_state();
    require(std::abs(after.aim_sway.yaw_rad - 0.02) < 1e-10 && after.aim_sway.pitch_rad == 0.0 &&
            after.time_seconds == before.time_seconds && after.aim_fraction == before.aim_fraction &&
            after.recoil.pitch_rad == before.recoil.pitch_rad && after.bloom_rad == before.bloom_rad &&
            range.weapon_state().rounds_remaining == ammo && range.shots() == 1,
            "Reloaded aim sway did not update the active gun in place");

    range.select_weapon(1);
    range.advance(0.23);
    require(std::abs(range.handling_state().aim_sway.yaw_rad) <= 0.0035,
            "Pistol aim sway settings affected the rifle");
    range.reset();
    range.set_aiming(true);
    range.advance(0.5);
    require(std::abs(range.handling_state().aim_sway.yaw_rad - 0.02) < 1e-10,
            "Reset restored the old aim sway preset");
}
void lowered_animation_and_timing_load_from_the_weapon_file() {
    ConfigFixture fixture;
    const auto config = weapon_range::load_visual(fixture.write(
        "lower_seconds = 0\nraise_to_ready_seconds = 0.4\nraise_to_aim_seconds = 0.8\n"
        "lower_interpolation = linear\nlower_key = 0 0 0 0 0 0 0\nlower_key = 1 0 -0.2 0 30 0 0\n"), 0);
    weapon_range::RangeSimulation range;
    range.set_presentation(0, config.presentation);
    range.set_lowered_stance_timing(0, config.stance_timing);
    range.set_stance_input(false, true);
    require(range.handling_state().lowered_fraction == 1.0, "Configured instant lowering was ignored");
    range.set_stance_input(false, false);
    range.advance(0.2);
    const auto pose = range.presentation().sample_lowered(range.handling_state().lowered_fraction);
    require(same(pose.offset, {0, -0.1, 0}) && same(pose.rotation_degrees, {15, 0, 0}),
            "Configured pose or ready raise duration did not reach halfway");
    range.reset();
    range.set_stance_input(false, true);
    range.set_stance_input(true, false);
    range.advance(0.4);
    require(std::abs(range.handling_state().aim_fraction - 0.5) < 1e-10,
            "Configured aim raise duration was lost after reset");
}
void configured_projectile_count_spawns_a_group_and_survives_reset() {
    ConfigFixture fixture;
    weapon_range::RangeSimulation range;
    const auto config = weapon_range::load_visual(fixture.write("projectiles_per_shot = 8\n"), 0);

    range.set_trigger(true);
    range.set_trigger(false);
    range.set_projectiles_per_shot(0, config.projectiles_per_shot);
    require(range.weapon_state().rounds_remaining == 11 && range.projectiles().size() == 1,
            "Reloading the config changed ammo or an already emitted shot");
    range.advance(0.2);
    range.set_trigger(true);
    require(range.projectiles_per_shot() == 8 && range.projectiles().size() == 8 &&
            range.shots() == 2 && range.projectiles_fired() == 9 && range.weapon_state().rounds_remaining == 10,
            "Range did not spawn eight projectiles for the cost of one round");
    const auto& first = range.projectiles().front();
    const auto& last = range.projectiles().back();
    require(first.launched_at == last.launched_at && same(first.motion.state().position, last.motion.state().position) &&
            !same(first.motion.state().velocity, last.motion.state().velocity),
            "The group did not share a muzzle and timestamp with independent trajectories");
    range.select_weapon(1);
    require(range.projectiles_per_shot() == 1, "Live projectile tuning affected another firearm");
    range.reset();
    require(range.projectiles_per_shot() == 8 && range.projectiles_fired() == 0,
            "Range reset lost the configured group or retained shot statistics");
}
void fire_mode_configuration_limits_selection_and_sets_burst_size() {
    using weapon_library::FireMode;
    ConfigFixture fixture;
    const auto config = weapon_range::load_visual(fixture.write("fire_modes = semi burst\nburst_size = 4\n"), 1);
    weapon_range::RangeSimulation range;
    range.select_weapon(1);
    range.set_fire_mode_settings(1, config.fire_modes);
    require(range.weapon_state().fire_mode == FireMode::SemiAutomatic && range.cycle_fire_mode() &&
            range.weapon_state().fire_mode == FireMode::Burst, "Config did not fall back from the removed automatic mode");
    range.set_trigger(true);
    range.set_trigger(false);
    range.advance(0.4);
    require(range.shots() == 4 && range.cycle_fire_mode() && range.weapon_state().fire_mode == FireMode::SemiAutomatic,
            "Config did not set a four-shot burst or restrict the selector");
    range.reset();
    range.select_weapon(1);
    require(range.weapon_state().fire_mode == FireMode::SemiAutomatic, "Reset lost the configured initial mode");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected bundled visual directory");
        configs_and_muzzle(argv[1]);
        sway_settings_update_existing_motion();
        aim_sway_config_applies_per_weapon_and_survives_reset();
        lowered_animation_and_timing_load_from_the_weapon_file();
        configured_projectile_count_spawns_a_group_and_survives_reset();
        fire_mode_configuration_limits_selection_and_sets_burst_size();
        std::cout << "Visual config and socket tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
