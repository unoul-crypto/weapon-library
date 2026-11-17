#include <weapon_library/MeleeMechanism.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace weapon_library;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool near(double a, double b) { return std::abs(a - b) < 1e-10; }
std::shared_ptr<const WeaponDefinition> action(double windup = 0.2, double active = 0.4, double recovery = 0.3,
                                              CancellationPolicy policy = {}) {
    return std::make_shared<const WeaponDefinition>(std::vector<ActionDefinition>{{7, windup, active, recovery, policy}});
}
struct World final : MeleeWorld {
    mutable std::vector<MeleeSweep> queries;
    bool blocking = false;
    double threshold = 0.5;
    std::vector<MeleeContact> sweep(const MeleeSweep& q) const override {
        queries.push_back(q);
        require(q.from_progress >= 0 && q.from_progress <= q.to_progress && q.to_progress <= 1, "Invalid sweep progress");
        if (q.from_progress > threshold || q.to_progress < threshold) return {};
        const double fraction = q.to_progress == q.from_progress ? 0 : (threshold - q.from_progress) / (q.to_progress - q.from_progress);
        return {{9, fraction, {1, 2, 3}, {0, 0, 1}, blocking},
                {9, fraction, {1, 2, 3}, {0, 0, 1}, blocking}, {10, fraction, {}, {0, 0, 1}, false}};
    }
};
std::vector<MeleeHitEvent> hits(const std::vector<MeleeEvent>& events) {
    std::vector<MeleeHitEvent> result;
    for (const auto& event : events) if (const auto* hit = std::get_if<MeleeHitEvent>(&event)) result.push_back(*hit);
    return result;
}
template <typename Function> void rejects(Function function) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Invalid melee input was accepted");
}

void phases_deduplication_and_partitioning() {
    World world;
    MeleeMechanism m(action(), {7, 2.0, 0.1, 1.2});
    require(m.execute(MeleeCommand::Attack, world).accepted(), "Attack rejected");
    require(m.execute(MeleeCommand::Attack, world).status == CommandStatus::Busy && m.state().attack_id == 1,
            "Busy attack reset the current swing");
    m.advance(0.1, world);
    require(world.queries.empty(), "Windup queried collision");
    const auto all = m.advance(1.0, world);
    const auto contacts = hits(all);
    require(contacts.size() == 2 && contacts[0].target_id == 9 && contacts[1].target_id == 10 &&
            near(contacts[0].time_seconds, 0.4) && contacts[0].attack_id == 1, "Large update lost or duplicated contacts");
    require(m.state().weapon.phase == Phase::Ready && m.state().targets_hit == 2, "Attack did not finish");
    require(std::get<WeaponEvent>(all.front()).phase == Phase::Active &&
            std::get<WeaponEvent>(all.back()).type == EventType::ActionCompleted, "Melee event order failed");
    require(near(world.queries.back().to_time_seconds, 0.6) && near(world.queries.back().to_progress, 1) &&
            world.queries.back().reach_m == 2.0 && world.queries.back().radius_m == 0.1, "Query escaped the active phase");
    const auto count = world.queries.size();
    m.advance(1, world);
    require(world.queries.size() == count, "Ready state repeated a sweep");

    World split_world;
    MeleeMechanism split(action(), {7});
    split.execute(MeleeCommand::Attack, split_world);
    std::vector<MeleeHitEvent> split_hits;
    for (int i = 0; i < 110; ++i) {
        const auto part = hits(split.advance(0.01, split_world));
        split_hits.insert(split_hits.end(), part.begin(), part.end());
    }
    require(split_hits.size() == contacts.size() && near(split_hits[0].time_seconds, contacts[0].time_seconds),
            "Partitioned updates changed contact timing/deduplication");
    m.execute(MeleeCommand::Attack, world);
    const auto next = hits(m.advance(1, world));
    require(next.size() == 2 && next[0].attack_id == 2, "New attack did not clear entity deduplication");
}

void cancellation_and_limits() {
    World world;
    MeleeMechanism m(action(), {7});
    require(m.execute(MeleeCommand::Cancel, world).status == CommandStatus::NoActiveAction, "Idle cancellation accepted");
    m.execute(MeleeCommand::Attack, world);
    m.advance(0.1, world);
    require(m.execute(MeleeCommand::Cancel, world).accepted() && hits(m.advance(2, world)).empty() && world.queries.empty(),
            "Cancelled windup produced a hit later");
    m.execute(MeleeCommand::Attack, world);
    m.advance(0.2, world);
    require(m.execute(MeleeCommand::Cancel, world).status == CommandStatus::CancellationBlocked, "Active policy was bypassed");
    m.advance(0.4, world);
    require(m.execute(MeleeCommand::Cancel, world).status == CommandStatus::CancellationBlocked, "Recovery policy was bypassed");

    World blocking;
    blocking.blocking = true;
    MeleeMechanism stopped(action(), {7});
    stopped.execute(MeleeCommand::Attack, blocking);
    require(hits(stopped.advance(0.45, blocking)).size() == 1 && stopped.state().blocked, "Blocking contact did not stop the swing");
    const auto count = blocking.queries.size();
    stopped.advance(2, blocking);
    require(blocking.queries.size() == count, "Blade continued querying behind cover");

    World limited;
    MeleeMechanism single(action(), {7, 1.6, 0.08, 1.4, 1});
    single.execute(MeleeCommand::Attack, limited);
    require(hits(single.advance(1, limited)).size() == 1, "Max target count was ignored");
}

void zero_phases_and_sorted_contacts() {
    World world;
    MeleeMechanism instant(action(0, 0, 0), {7});
    const auto result = instant.execute(MeleeCommand::Attack, world);
    require(result.accepted() && hits(result.events).size() == 2 && world.queries.size() == 1 &&
            world.queries[0].from_progress == 0.5 && instant.state().weapon.phase == Phase::Ready,
            "Instant action did not query exactly once at the central pose");
    instant.advance(1, world);
    require(world.queries.size() == 1, "Instant action repeated its query");
    MeleeMechanism delayed(action(0.2, 0, 0.3), {7});
    delayed.execute(MeleeCommand::Attack, world);
    const auto hit = hits(delayed.advance(1, world));
    require(hit.size() == 2 && near(hit[0].time_seconds, 0.2), "Zero active phase was skipped by catch-up");

    struct Unsorted final : MeleeWorld {
        std::vector<MeleeContact> sweep(const MeleeSweep& q) const override {
            if (q.from_time_seconds == q.to_time_seconds) return {};
            return {{3, 0.8, {}, {}, false}, {2, 0.4, {}, {}, true}, {1, 0.1, {}, {}, false}};
        }
    } unsorted;
    MeleeMechanism ordered(action(), {7});
    ordered.execute(MeleeCommand::Attack, unsorted);
    const auto contacts = hits(ordered.advance(1, unsorted));
    require(contacts.size() == 2 && contacts[0].target_id == 1 && contacts[1].target_id == 2 &&
            contacts[0].time_seconds < contacts[1].time_seconds, "Contacts were not ordered before blocking");
}

void invalid_inputs() {
    rejects([] { MeleeMechanism m(action(), {42}); });
    rejects([] { MeleeMechanism m(nullptr, {7}); });
    rejects([] { MeleeMechanism m(action(), {7, 0}); });
    rejects([] { MeleeMechanism m(action(), {7, 1, -0.1}); });
    rejects([] { MeleeMechanism m(action(), {7, 1, 0.1, 7}); });
    rejects([] { MeleeMechanism m(action(), {7, std::numeric_limits<double>::infinity()}); });
    World world;
    MeleeMechanism m(action(), {7});
    m.execute(MeleeCommand::Attack, world);
    rejects([&] { m.advance(-1, world); });
    rejects([&] { m.advance(std::numeric_limits<double>::quiet_NaN(), world); });
    require(m.state().weapon.time_seconds == 0 && world.queries.empty(), "Invalid time mutated melee state");
}
} // namespace

int main() {
    try {
        phases_deduplication_and_partitioning();
        cancellation_and_limits();
        zero_phases_and_sorted_contacts();
        invalid_inputs();
        std::cout << "Melee tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
