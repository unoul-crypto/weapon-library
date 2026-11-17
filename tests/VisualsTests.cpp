#include "RangeSimulation.h"
#include "RangeVisuals.h"

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
            "reload_motion = none\nreload_key = 0 0 0 0 0 0 0\n"}) {
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
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected bundled visual directory");
        configs_and_muzzle(argv[1]);
        std::cout << "Visual config and socket tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
