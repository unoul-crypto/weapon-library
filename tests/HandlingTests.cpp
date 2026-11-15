#include <weapon_library/WeaponHandling.h>
#include <weapon_library/FiringMechanism.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace weapon_library;
using math::Vec3;
namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(double a, double b, double tolerance = 1e-11) { return std::abs(a - b) < tolerance; }
bool same(Vec3 a, Vec3 b) { return math::length(a - b) < 1e-11; }
template <typename Function> void rejects(Function function) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Invalid handling input was accepted");
}

HandlingDefinition no_kick() {
    HandlingDefinition d;
    d.recoil_per_shot = {};
    d.recoil_yaw_jitter_rad = 0;
    d.bloom_per_shot_rad = 0;
    return d;
}

void aiming_is_smooth_interruptible_and_can_be_instant() {
    WeaponHandling h;
    const double hip = h.state().spread_half_angle_rad;
    h.set_aiming(true);
    require(h.state().aim_fraction == 0, "Nonzero ADS transition was instantaneous");
    h.advance(0.1);
    require(near(h.state().aim_fraction, 0.5) && h.state().spread_half_angle_rad < hip,
            "ADS failed to narrow spread during transition");
    h.set_aiming(false);
    h.advance(0.075);
    require(near(h.state().aim_fraction, 0), "Interrupted ADS did not return from its current fraction");
    h.set_aiming(true);
    h.advance(1.0);
    require(h.state().aim_fraction == 1.0, "ADS transition exceeded its bound");
    HandlingDefinition instant;
    instant.aim_in_seconds = instant.aim_out_seconds = 0;
    WeaponHandling quick(instant);
    quick.set_aiming(true);
    require(quick.state().aim_fraction == 1.0, "Zero-duration aim-in was delayed");
    quick.set_aiming(false);
    require(quick.state().aim_fraction == 0.0, "Zero-duration aim-out was delayed");
}

void spread_is_seeded_bounded_and_centered() {
    const auto d = no_kick();
    WeaponHandling first(d, 12345), replay(d, 12345), other(d, 54321);
    const auto sample = first.sample_shot({0, 0, -1});
    require(same(sample.direction, replay.sample_shot({0, 0, -1}).direction), "Same seed did not replay a shot");
    require(!same(sample.direction, other.sample_shot({0, 0, -1}).direction), "Different seeds produced the same shot");
    double horizontal = 0, vertical = 0, squared_angle = 0;
    for (int i = 0; i < 2048; ++i) {
        const auto shot = first.sample_shot({0, 0, -1});
        require(near(math::length(shot.direction), 1.0), "Spread produced a non-unit direction");
        require(-shot.direction.z >= std::cos(shot.spread_half_angle_rad) - 1e-12, "Shot escaped its spread cone");
        horizontal += shot.direction.x;
        vertical += shot.direction.y;
        const double angle = std::acos(std::clamp(-shot.direction.z, -1.0, 1.0));
        squared_angle += angle * angle;
        require(same(shot.direction, replay.sample_shot({0, 0, -1}).direction), "Shot sequence did not replay");
    }
    require(std::abs(horizontal / 2048) < 0.001 && std::abs(vertical / 2048) < 0.001,
            "Spread is strongly biased away from the center");
    require(squared_angle / 2048 > d.hip_spread_rad * d.hip_spread_rad * 0.4 &&
            squared_angle / 2048 < d.hip_spread_rad * d.hip_spread_rad * 0.6,
            "Spread samples do not cover the cone");
}

void aimed_and_moving_shots_have_different_accuracy() {
    const auto d = no_kick();
    WeaponHandling hip(d, 71), aimed(d, 71), moving(d, 71);
    aimed.set_aiming(true); aimed.advance(1.0);
    moving.set_movement_factor(1.0);
    const auto h = hip.sample_shot({0, 0, -1});
    const auto a = aimed.sample_shot({0, 0, -1});
    const auto m = moving.sample_shot({0, 0, -1});
    require(a.spread_half_angle_rad < h.spread_half_angle_rad && m.spread_half_angle_rad > h.spread_half_angle_rad,
            "Aim and movement factors did not affect accuracy");
    require(a.direction.z < h.direction.z && m.direction.z > h.direction.z,
            "Actual shot directions did not reflect the different accuracy");
}

void bloom_and_recoil_apply_after_the_shot_and_recover() {
    HandlingDefinition d;
    d.hip_spread_rad = d.aimed_spread_rad = 0;
    d.recoil_yaw_jitter_rad = 0;
    WeaponHandling h(d), aimed(d);
    const auto first = h.sample_shot({0, 0, -1});
    require(same(first.direction, {0, 0, -1}) && first.recoil_before.pitch_rad == 0,
            "The current shot incorrectly included its own recoil impulse");
    require(h.state().recoil.pitch_rad > 0 && h.state().bloom_rad > 0 &&
            h.recoiled_direction({0, 0, -1}).y > 0, "Shot did not kick subsequent aim or increase spread");
    const auto second = h.sample_shot({0, 0, -1});
    require(second.recoil_before.pitch_rad > 0 && second.spread_half_angle_rad > first.spread_half_angle_rad,
            "Successive shot lost accumulated recoil or bloom");
    for (int i = 0; i < 100; ++i) h.sample_shot({0, 0, -1});
    require(h.state().bloom_rad <= d.max_bloom_rad && h.state().recoil.pitch_rad <= d.max_recoil_rad.pitch_rad,
            "Accumulated shot effects exceeded their caps");
    h.advance(5.0);
    require(h.state().bloom_rad == 0 && std::abs(h.state().recoil.pitch_rad) < 1e-10,
            "Shot effects did not recover with elapsed time");
    aimed.set_aiming(true); aimed.advance(1.0);
    const auto steady = aimed.sample_shot({0, 0, -1});
    require(steady.recoil_after.pitch_rad < first.recoil_after.pitch_rad, "ADS did not reduce recoil impulse");
}

void recovery_and_randomness_are_independent_of_frame_partition() {
    WeaponHandling one({}, 58), many({}, 58);
    one.sample_shot({0, 0, -1}); many.sample_shot({0, 0, -1});
    one.set_aiming(true); many.set_aiming(true);
    one.advance(0.4);
    for (int i = 0; i < 40; ++i) many.advance(0.01);
    const auto a = one.state(), b = many.state();
    require(near(a.time_seconds, b.time_seconds) && near(a.aim_fraction, b.aim_fraction) &&
            near(a.bloom_rad, b.bloom_rad) && near(a.recoil.pitch_rad, b.recoil.pitch_rad) &&
            near(a.recoil.yaw_rad, b.recoil.yaw_rad), "Recovery changed with frame partition");
    require(same(one.sample_shot({0, 0, -1}).direction, many.sample_shot({0, 0, -1}).direction),
            "Advancing more frames consumed extra random numbers");
}

std::vector<HandledShot> simulate_fire(bool partitioned) {
    auto actions = std::make_shared<const WeaponDefinition>(std::vector<ActionDefinition>{{1, 0.03, 0.0, 0.04}});
    FiringMechanism firing(actions, {1, FireMode::Automatic, 0.1, 30}, 30);
    WeaponHandling handling({}, 444);
    handling.set_aiming(true);
    std::vector<HandledShot> result;
    const auto consume = [&](const std::vector<FiringEvent>& events) {
        for (const auto& event : events) if (const auto* shot = std::get_if<ShotFiredEvent>(&event)) {
            handling.advance(std::max(0.0, shot->time_seconds - handling.state().time_seconds));
            result.push_back(handling.sample_shot({0, 0, -1}));
        }
    };
    consume(firing.execute(TriggerCommand::Press));
    const int steps = partitioned ? 100 : 1;
    for (int i = 0; i < steps; ++i) {
        consume(firing.advance(1.0 / steps));
        handling.advance(std::max(0.0, firing.state().weapon.time_seconds - handling.state().time_seconds));
    }
    return result;
}

void catch_up_shots_use_their_individual_times() {
    const auto one = simulate_fire(false), many = simulate_fire(true);
    require(one.size() == 10 && one.size() == many.size(), "Catch-up lost shot samples");
    for (std::size_t i = 0; i < one.size(); ++i) {
        require(near(one[i].time_seconds, 0.03 + i * 0.1) && near(one[i].time_seconds, many[i].time_seconds) &&
                near(one[i].spread_half_angle_rad, many[i].spread_half_angle_rad) &&
                same(one[i].direction, many[i].direction), "Catch-up shot used end-of-frame handling state");
    }
}

void vertical_aim_and_tiny_cones_remain_valid() {
    auto d = no_kick();
    d.hip_spread_rad = 1e-12;
    WeaponHandling h(d, 12);
    const auto shot = h.sample_shot({0, 1, 0});
    require(near(math::length(shot.direction), 1.0) && (shot.direction.x != 0 || shot.direction.z != 0),
            "Vertical aim or a tiny positive cone collapsed");
}

void invalid_inputs_preserve_runtime_and_rng() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    auto d = no_kick(); d.aim_in_seconds = -1;
    rejects([&] { WeaponHandling h(d); });
    d = no_kick(); d.hip_spread_rad = 2.0;
    rejects([&] { WeaponHandling h(d); });
    d = no_kick(); d.aimed_recoil_multiplier = nan;
    rejects([&] { WeaponHandling h(d); });
    WeaponHandling h({}, 24), reference({}, 24);
    rejects([&] { h.advance(-1); });
    rejects([&] { h.advance(nan); });
    rejects([&] { h.set_movement_factor(1.1); });
    rejects([&] { h.sample_shot({0, 0, 0}); });
    rejects([&] { h.sample_shot({0, 0, -1}, {nan, 0, 0}); });
    require(h.state().time_seconds == 0 && h.state().bloom_rad == 0 && h.state().movement_factor == 0,
            "Invalid input changed handling state");
    require(same(h.sample_shot({0, 0, -1}).direction, reference.sample_shot({0, 0, -1}).direction),
            "Invalid input consumed random samples");
}
}

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"ADS transitions", aiming_is_smooth_interruptible_and_can_be_instant},
        {"Seeded cone sampling", spread_is_seeded_bounded_and_centered},
        {"Aim and movement accuracy", aimed_and_moving_shots_have_different_accuracy},
        {"Bloom and recoil", bloom_and_recoil_apply_after_the_shot_and_recover},
        {"Frame partition", recovery_and_randomness_are_independent_of_frame_partition},
        {"Shot timestamps", catch_up_shots_use_their_individual_times},
        {"Vertical and tiny cones", vertical_aim_and_tiny_cones_remain_valid},
        {"Invalid inputs", invalid_inputs_preserve_runtime_and_rng}
    };
    int failed = 0;
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS: " << test.first << '\n'; }
        catch (const std::exception& e) { ++failed; std::cerr << "FAIL: " << test.first << ": " << e.what() << '\n'; }
    }
    return failed == 0 ? 0 : 1;
}
