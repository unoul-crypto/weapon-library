#include <weapon_library/Ballistics.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

using namespace weapon_library::ballistics;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(double a, double b, double tolerance = 1e-8) { return std::abs(a - b) < tolerance; }
template <typename Function> void rejects(Function function) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Invalid input was accepted");
}

struct EmptyWorld : ProjectileWorld {
    mutable unsigned queries = 0;
    mutable double largest_segment = 0.0;
    std::optional<SegmentInteraction> trace(const FlightSegment& s, const Medium&) const override {
        ++queries;
        largest_segment = std::max(largest_segment, length(s.to - s.from));
        return std::nullopt;
    }
};

ProjectileDefinition simple() {
    ProjectileDefinition d;
    d.mass_kg = d.cross_section_m2 = d.drag_coefficient = 1.0;
    d.gravity = {};
    d.max_step_seconds = 1.0;
    d.max_segment_m = 1000.0;
    return d;
}

void vacuum_has_exact_gravity_and_finite_flight() {
    EmptyWorld world;
    auto d = simple();
    d.gravity = {0, -9.81, 0};
    Projectile p(d, {0, 2, 0}, {100, 5, 0}, {0, 0.0, {}});
    require(p.advance(0.0, world).empty() && world.queries == 0, "Zero update queried the world");
    p.advance(1.0, world);
    const auto state = p.state();
    require(near(state.position.x, 100.0) && near(state.position.y, 2.095), "Vacuum trajectory is incorrect");
    require(near(state.velocity.x, 100.0) && near(state.velocity.y, -4.81), "Gravity did not change velocity correctly");
    require(state.status == ProjectileStatus::Flying && near(state.age_seconds, 1.0), "Projectile ended early");
}

void quadratic_drag_matches_analytic_solution() {
    EmptyWorld world;
    const Medium medium{2, 2.0, {}}; // k = rho * Cd * A / (2m) = 1.
    Projectile p(simple(), {}, {10, 0, 0}, medium);
    p.advance(1.0, world);
    require(near(p.state().position.x, std::log(11.0)) && near(p.state().velocity.x, 10.0 / 11.0),
            "Quadratic drag disagrees with the analytic solution");
    Projectile split(simple(), {}, {10, 0, 0}, medium);
    for (int i = 0; i < 100; ++i) split.advance(0.01, world);
    require(near(split.state().position.x, p.state().position.x) &&
            near(split.state().velocity.x, p.state().velocity.x), "Drag changed with update partition");
}

void drag_is_relative_to_flow_and_stable_in_water() {
    EmptyWorld world;
    Projectile moving(simple(), {}, {20, 0, 0}, {2, 2.0, {10, 0, 0}});
    moving.advance(1.0, world);
    require(near(moving.state().velocity.x, 10.0 + 10.0 / 11.0) &&
            near(moving.state().position.x, 10.0 + std::log(11.0)), "Flow-relative drag is incorrect");
    Projectile carried(simple(), {}, {}, {2, 2.0, {10, 0, 0}});
    carried.advance(1.0, world);
    require(carried.state().velocity.x > 0 && carried.state().velocity.x < 10.0,
            "Flow failed to accelerate a stationary projectile");

    ProjectileDefinition d;
    d.gravity = {};
    Projectile air(d, {}, {800, 0, 0}, Medium::air());
    Projectile water(d, {}, {800, 0, 0}, Medium::water());
    air.advance(0.2, world);
    water.advance(0.2, world);
    require(water.state().velocity.x > 0 && water.state().velocity.x < air.state().velocity.x * 0.05,
            "Dense water failed to slow the projectile or reversed its direction");
    require(water.state().position.x < air.state().position.x, "Water did not reduce travel distance");
}

struct LayerWorld : ProjectileWorld {
    std::optional<SegmentInteraction> trace(const FlightSegment& s, const Medium& medium) const override {
        const double dx = s.to.x - s.from.x;
        if (dx == 0) return std::nullopt;
        const bool water = medium.id == 1;
        double boundary;
        if (water) boundary = dx > 0 ? 10.0 : 5.0;
        else if (dx > 0 && s.from.x <= 5.0) boundary = 5.0;
        else if (dx < 0 && s.from.x >= 10.0) boundary = 10.0;
        else return std::nullopt;
        const double t = (boundary - s.from.x) / dx;
        if (t < 0 || t > 1) return std::nullopt;
        return SegmentInteraction{InteractionType::MediumBoundary, t, 42, {},
                                  water ? Medium{0, 0.0, {}} : Medium{1, 2.0, {}}};
    }
};

void medium_boundaries_split_the_same_update() {
    const LayerWorld world;
    Projectile p(simple(), {}, {100, 0, 0}, {0, 0.0, {}});
    const auto events = p.advance(2.0, world);
    require(events.size() == 2 && events[0].type == ProjectileEventType::MediumChanged &&
            events[1].type == ProjectileEventType::MediumChanged, "Air/water/air transition events missing");
    const double water_time = (std::exp(5.0) - 1.0) / 100.0;
    const double exit_speed = 100.0 * std::exp(-5.0);
    const double expected_x = 10.0 + exit_speed * (2.0 - 0.05 - water_time);
    require(near(events[0].state.age_seconds, 0.05) && near(events[0].state.position.x, 5.0),
            "Water entry was delayed until end of update");
    require(near(events[1].state.age_seconds, 0.05 + water_time) && near(events[1].state.velocity.x, exit_speed),
            "Water exit used distance fraction as time fraction");
    require(near(p.state().position.x, expected_x) && p.state().medium.id == 0,
            "Unused time after medium exit was lost");
    Projectile split(simple(), {}, {100, 0, 0}, {0, 0.0, {}});
    for (int i = 0; i < 200; ++i) split.advance(0.01, world);
    require(near(split.state().position.x, expected_x), "Medium transition changed with frame rate");
}

void launch_on_boundary_and_reverse_crossing() {
    const LayerWorld world;
    Projectile entering(simple(), {5, 0, 0}, {10, 0, 0}, {0, 0.0, {}});
    const auto entry = entering.advance(0.1, world);
    require(entry.size() == 1 && entry[0].state.age_seconds == 0.0 && entering.state().medium.id == 1,
            "Start-on-boundary entry was lost or looped");
    Projectile leaving(simple(), {5, 0, 0}, {-10, 0, 0}, {1, 2.0, {}});
    const auto exit = leaving.advance(0.1, world);
    require(exit.size() == 1 && leaving.state().medium.id == 0 && near(leaving.state().position.x, 4.0),
            "Reverse boundary crossing remained in water");
}

struct ThinWall : ProjectileWorld {
    std::optional<SegmentInteraction> trace(const FlightSegment& s, const Medium&) const override {
        const double dx = s.to.x - s.from.x;
        if (dx <= 0 || s.from.x > 0.015 || s.to.x < 0.015) return std::nullopt;
        return SegmentInteraction{InteractionType::Impact, (0.015 - s.from.x) / dx, 99, {-1, 0, 0}};
    }
};

void swept_collision_stops_at_contact_once() {
    auto d = simple();
    Projectile p(d, {}, {600, 0, 0}, {0, 0.0, {}});
    const auto hit = p.advance(1.0, ThinWall{});
    require(hit.size() == 1 && hit[0].object_id == 99 && hit[0].type == ProjectileEventType::Impact,
            "Fast projectile tunnelled through a thin wall");
    require(near(p.state().position.x, 0.015) && near(p.state().age_seconds, 0.015 / 600.0),
            "Impact position or flight time is incorrect");
    require(p.state().status == ProjectileStatus::Impacted && p.advance(1.0, ThinWall{}).empty(),
            "Impacted projectile moved or repeated its impact");
}

void lifetime_and_substep_bounds_are_enforced() {
    EmptyWorld world;
    auto d = simple();
    d.max_segment_m = 0.1;
    d.max_step_seconds = 0.05;
    d.lifetime_seconds = 0.1;
    Projectile p(d, {}, {1000, 0, 0}, {0, 0.0, {}});
    const auto events = p.advance(100.0, world);
    require(world.queries > 100 && world.largest_segment <= 0.100000001, "Segment length was not bounded");
    require(events.size() == 1 && events[0].type == ProjectileEventType::Expired &&
            near(p.state().position.x, 100.0) && near(p.state().age_seconds, 0.1), "Lifetime cutoff is incorrect");
    require(p.advance(1.0, world).empty(), "Expiration event was repeated");
}

void invalid_inputs_and_world_results_are_rejected() {
    EmptyWorld world;
    auto d = simple();
    d.mass_kg = 0;
    rejects([&] { Projectile p(d, {}, {}); });
    d = simple(); d.max_step_seconds = 0;
    rejects([&] { Projectile p(d, {}, {}); });
    d = simple(); d.cross_section_m2 = -1;
    rejects([&] { Projectile p(d, {}, {}); });
    rejects([&] { Projectile p(simple(), {}, {}, {0, -1, {}}); });
    const double nan = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { Projectile p(simple(), {nan, 0, 0}, {}); });
    Projectile p(simple(), {}, {10, 0, 0});
    for (double seconds : {-1.0, nan, std::numeric_limits<double>::infinity()}) {
        rejects([&] { p.advance(seconds, world); });
    }
    require(p.state().age_seconds == 0 && p.state().position.x == 0 && world.queries == 0,
            "Invalid time mutated state or called the world");
    struct BadWorld : ProjectileWorld {
        std::optional<SegmentInteraction> trace(const FlightSegment&, const Medium&) const override {
            return SegmentInteraction{InteractionType::Impact, 1.5};
        }
    } bad;
    rejects([&] { p.advance(0.1, bad); });
    require(p.state().age_seconds == 0, "Invalid first world response mutated state");
}

void zero_time_boundary_cycles_are_reported() {
    struct CyclingWorld : ProjectileWorld {
        std::optional<SegmentInteraction> trace(const FlightSegment&, const Medium& m) const override {
            return SegmentInteraction{InteractionType::MediumBoundary, 0.0, 0, {},
                                      m.id == 0 ? Medium::water() : Medium::air()};
        }
    } world;
    Projectile p(simple(), {}, {1, 0, 0});
    bool reported = false;
    try { p.advance(0.1, world); } catch (const std::runtime_error&) { reported = true; }
    require(reported, "Faulty adapter caused an unbounded zero-time boundary loop");
    require(p.state().age_seconds == 0.0, "Zero-time boundaries advanced the clock");
}

} // namespace

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"Vacuum and gravity", vacuum_has_exact_gravity_and_finite_flight},
        {"Analytic quadratic drag", quadratic_drag_matches_analytic_solution},
        {"Flow and dense water", drag_is_relative_to_flow_and_stable_in_water},
        {"Medium boundaries", medium_boundaries_split_the_same_update},
        {"Launch on a boundary", launch_on_boundary_and_reverse_crossing},
        {"Swept collision", swept_collision_stops_at_contact_once},
        {"Lifetime and substeps", lifetime_and_substep_bounds_are_enforced},
        {"Invalid inputs", invalid_inputs_and_world_results_are_rejected},
        {"Faulty boundary adapter", zero_time_boundary_cycles_are_reported}
    };
    int failed = 0;
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS: " << test.first << '\n'; }
        catch (const std::exception& error) {
            ++failed; std::cerr << "FAIL: " << test.first << ": " << error.what() << '\n';
        }
    }
    return failed == 0 ? 0 : 1;
}
