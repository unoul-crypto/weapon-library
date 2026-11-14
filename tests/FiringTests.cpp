#include <weapon_library/FiringMechanism.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

using namespace weapon_library;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool near(double a, double b) { return std::abs(a - b) < 1e-10; }

template <typename Function>
void rejects(Function function) {
    bool rejected = false;
    try {
        function();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "Invalid input was accepted");
}

auto weapon_definition(ActionDefinition action = {7, 0.0, 0.0, 0.0}) {
    return std::make_shared<const WeaponDefinition>(std::vector<ActionDefinition>{action});
}

FiringDefinition firing_definition(FireMode mode = FireMode::Automatic,
                                    double interval = 0.1,
                                    std::uint32_t capacity = 30,
                                    std::uint32_t shot_cost = 1) {
    return {7, mode, interval, capacity, shot_cost};
}

template <typename Event>
std::vector<Event> collect(const std::vector<FiringEvent>& events) {
    std::vector<Event> found;
    for (const auto& event : events) {
        if (const auto* value = std::get_if<Event>(&event)) {
            found.push_back(*value);
        }
    }
    return found;
}

void append(std::vector<FiringEvent>& output, const std::vector<FiringEvent>& events) {
    output.insert(output.end(), events.begin(), events.end());
}

void semi_auto_uses_press_edges() {
    FiringMechanism pistol(weapon_definition(), firing_definition(FireMode::SemiAutomatic), 10);
    const auto first = collect<ShotFiredEvent>(pistol.execute(TriggerCommand::Press));
    require(first.size() == 1 && near(first[0].time_seconds, 0.0), "First press did not fire immediately");
    require(first[0].rounds_consumed == 1 && first[0].rounds_remaining == 9,
            "Shot ammo event is incorrect");
    require(pistol.execute(TriggerCommand::Press).empty(), "Repeated press fired while held");
    require(collect<ShotFiredEvent>(pistol.advance(1.0)).empty(), "Semi-auto repeated while held");
    require(pistol.state().rounds_remaining == 9, "Holding semi-auto consumed extra ammo");
    pistol.execute(TriggerCommand::Release);
    require(pistol.execute(TriggerCommand::Release).empty(), "Repeated release emitted events");
    const auto second = collect<ShotFiredEvent>(pistol.execute(TriggerCommand::Press));
    require(second.size() == 1 && near(second[0].time_seconds, 1.0), "Fresh press did not rearm semi-auto");
}

void semi_auto_does_not_queue_or_bypass_cadence() {
    FiringMechanism pistol(weapon_definition(), firing_definition(FireMode::SemiAutomatic), 10);
    pistol.execute(TriggerCommand::Press);
    pistol.execute(TriggerCommand::Release);
    require(pistol.execute(TriggerCommand::Press).empty(), "Release/repress bypassed cadence");
    require(pistol.state().trigger_held, "Blocked shot lost trigger state");
    require(collect<ShotFiredEvent>(pistol.advance(1.0)).empty(), "Blocked press was queued");
    pistol.execute(TriggerCommand::Release);
    require(collect<ShotFiredEvent>(pistol.execute(TriggerCommand::Press)).size() == 1,
            "Semi-auto did not recover after blocked press");

    FiringMechanism busy(weapon_definition({7, 0.2, 0.1, 0.2}),
                         firing_definition(FireMode::SemiAutomatic), 10);
    busy.execute(TriggerCommand::Press);
    busy.advance(0.3);
    busy.execute(TriggerCommand::Release);
    require(busy.execute(TriggerCommand::Press).empty(), "Recovery was bypassed");
    require(collect<ShotFiredEvent>(busy.advance(1.0)).empty(), "Busy semi-auto press was queued");
    require(busy.state().rounds_remaining == 9, "Busy semi-auto consumed extra ammo");
}

void automatic_catches_up_at_boundaries() {
    FiringMechanism rifle(weapon_definition(), firing_definition(), 30);
    std::vector<FiringEvent> events = rifle.execute(TriggerCommand::Press);
    append(events, rifle.advance(1.0));
    const auto shots = collect<ShotFiredEvent>(events);
    require(shots.size() == 11, "Large update lost automatic shots or endpoint shot");
    for (std::size_t i = 0; i < shots.size(); ++i) {
        require(near(shots[i].time_seconds, static_cast<double>(i) * 0.1), "Automatic shot timestamp drifted");
        require(shots[i].rounds_remaining == 29 - i, "Automatic ammo count is wrong");
    }
    require(rifle.state().rounds_remaining == 19, "Automatic ammo conservation failed");
    require(near(rifle.state().weapon.time_seconds, 1.0), "Automatic clock did not advance fully");
    require(rifle.advance(0.0).empty(), "Zero update repeated an endpoint shot");
    rifle.execute(TriggerCommand::Release);
    require(collect<ShotFiredEvent>(rifle.advance(10.0)).empty(), "Automatic kept firing after release");
}

void automatic_waits_for_cooldown_and_recovery() {
    FiringMechanism rifle(weapon_definition(), firing_definition(), 10);
    rifle.execute(TriggerCommand::Press);
    rifle.advance(0.025);
    rifle.execute(TriggerCommand::Release);
    require(rifle.execute(TriggerCommand::Press).empty(), "Automatic repress bypassed cooldown");
    require(collect<ShotFiredEvent>(rifle.advance(0.05)).empty(), "Automatic fired before interval");
    const auto resumed = collect<ShotFiredEvent>(rifle.advance(0.025));
    require(resumed.size() == 1 && near(resumed[0].time_seconds, 0.1), "Held automatic did not resume at cooldown");

    FiringMechanism slow(weapon_definition({7, 0.05, 0.1, 0.25}), firing_definition(), 10);
    slow.execute(TriggerCommand::Press);
    const auto shots = collect<ShotFiredEvent>(slow.advance(1.0));
    require(shots.size() == 3, "Automatic skipped or bypassed slow action phases");
    require(near(shots[0].time_seconds, 0.05) && near(shots[1].time_seconds, 0.45) &&
            near(shots[2].time_seconds, 0.85), "Action-limited cadence is wrong");
}

void ammo_is_consumed_at_activation_in_event_order() {
    FiringMechanism gun(weapon_definition({7, 0.2, 0.0, 0.1}),
                        firing_definition(FireMode::SemiAutomatic), 5);
    const auto started = gun.execute(TriggerCommand::Press);
    require(started.size() == 1 && collect<ShotFiredEvent>(started).empty(), "Windup created a premature shot");
    require(gun.state().rounds_remaining == 5, "Windup prematurely consumed ammo");
    gun.execute(TriggerCommand::Release);
    const auto events = gun.advance(0.2);
    require(events.size() == 3, "Activation/shot/recovery event count is wrong");
    const auto* active = std::get_if<WeaponEvent>(&events[0]);
    const auto* shot = std::get_if<ShotFiredEvent>(&events[1]);
    const auto* recovery = std::get_if<WeaponEvent>(&events[2]);
    require(active && active->phase == Phase::Active, "Shot was not preceded by activation");
    require(shot && shot->action_id == 7 && shot->rounds_remaining == 4 && near(shot->time_seconds, 0.2),
            "Release incorrectly cancelled a committed shot");
    require(recovery && recovery->phase == Phase::Recovery, "Shot was not followed by recovery");
    require(gun.state().rounds_remaining == 4, "Active phase did not consume ammo exactly once");
    require(collect<ShotFiredEvent>(gun.advance(5.0)).empty(), "Single activation emitted duplicate shots");
}

void cancellation_preserves_ammo_and_cooldown() {
    FiringMechanism gun(weapon_definition({7, 0.1, 0.1, 0.1}),
                        firing_definition(FireMode::Automatic, 0.5), 5);
    gun.execute(TriggerCommand::Press);
    gun.advance(0.05);
    const auto cancelled = gun.cancel_action();
    require(cancelled.accepted() && cancelled.events.size() == 1, "Windup cancellation failed");
    require(!gun.state().trigger_held && gun.state().rounds_remaining == 5,
            "Cancellation lost ammo or kept firing enabled");
    require(collect<ShotFiredEvent>(gun.advance(0.05)).empty(), "Cancelled shot reappeared");
    require(gun.execute(TriggerCommand::Press).empty(), "Cancellation bypassed cooldown");
    require(collect<ShotFiredEvent>(gun.advance(0.4)).empty(), "Shot fired before resumed windup");
    require(gun.state().weapon.phase == Phase::Windup, "Automatic did not restart at cadence boundary");
    const auto shots = collect<ShotFiredEvent>(gun.advance(0.1));
    require(shots.size() == 1 && near(shots[0].time_seconds, 0.6), "Resumed shot has incorrect timing");
    require(gun.cancel_action().status == CommandStatus::CancellationBlocked,
            "Default active cancellation was allowed");
    require(gun.state().trigger_held && gun.state().rounds_remaining == 4,
            "Blocked cancellation changed trigger or ammo");

    FiringMechanism cancellable(weapon_definition({7, 0.0, 0.2, 0.2, {true, true, true}}),
                                firing_definition(), 5);
    cancellable.execute(TriggerCommand::Press);
    require(cancellable.cancel_action().accepted(), "Configured active cancellation was blocked");
    require(cancellable.state().rounds_remaining == 4, "Cancellation refunded an already fired round");
    require(collect<ShotFiredEvent>(cancellable.advance(1.0)).empty(), "Cancelled automatic kept firing");
    require(cancellable.cancel_action().status == CommandStatus::NoActiveAction,
            "Idle cancellation accepted");
}

void empty_magazine_has_no_phantom_shots_or_spam() {
    FiringMechanism rifle(weapon_definition(), firing_definition(), 3);
    auto events = rifle.execute(TriggerCommand::Press);
    append(events, rifle.advance(10.0));
    require(collect<ShotFiredEvent>(events).size() == 3, "Empty rifle emitted phantom shots");
    const auto dry = collect<DryFireEvent>(events);
    require(dry.size() == 1 && near(dry[0].time_seconds, 0.3), "Empty automatic clicked at wrong boundary");
    require(rifle.state().rounds_remaining == 0, "Empty rifle ammo underflowed");
    require(rifle.advance(10.0).empty(), "Held empty trigger spammed dry-fire events");
    require(rifle.execute(TriggerCommand::Press).empty(), "Repeated empty Press spammed events");
    rifle.execute(TriggerCommand::Release);
    require(collect<DryFireEvent>(rifle.execute(TriggerCommand::Press)).size() == 1,
            "Fresh empty trigger did not click");

    FiringMechanism empty(weapon_definition(), firing_definition(FireMode::SemiAutomatic), 0);
    const auto click = empty.execute(TriggerCommand::Press);
    require(click.size() == 1 && std::holds_alternative<DryFireEvent>(click[0]),
            "Empty pistol started an action");
    require(empty.state().weapon.phase == Phase::Ready, "Empty pistol became busy");
    require(empty.state().cooldown_remaining_seconds == 0.0, "Dry fire consumed firing cooldown");
}

void multi_round_shots_preserve_remainder() {
    FiringMechanism gun(weapon_definition(), firing_definition(FireMode::Automatic, 0.1, 8, 2), 5);
    auto events = gun.execute(TriggerCommand::Press);
    append(events, gun.advance(1.0));
    const auto shots = collect<ShotFiredEvent>(events);
    require(shots.size() == 2, "Insufficient rounds produced a partial shot");
    require(shots[0].rounds_consumed == 2 && shots[0].rounds_remaining == 3 &&
            shots[1].rounds_remaining == 1, "Multi-round shot accounting failed");
    require(gun.state().rounds_remaining == 1, "Unusable remainder was lost");
    require(collect<DryFireEvent>(events).size() == 1, "Insufficient rounds were not reported");
}

void equivalent_time_partitions_preserve_full_event_stream() {
    const auto shared = weapon_definition({7, 0.03, 0.02, 0.04});
    FiringMechanism single(shared, firing_definition(), 30);
    FiringMechanism partitioned(shared, firing_definition(), 30);
    auto expected = single.execute(TriggerCommand::Press);
    auto actual = partitioned.execute(TriggerCommand::Press);
    append(expected, single.advance(1.0));
    for (int i = 0; i < 100; ++i) {
        append(actual, partitioned.advance(0.01));
    }
    if (expected.size() != actual.size()) {
        std::cerr << "Expected events: " << expected.size() << ", actual events: " << actual.size()
                  << ", single phase: " << static_cast<int>(single.state().weapon.phase)
                  << ", partitioned phase: " << static_cast<int>(partitioned.state().weapon.phase)
                  << ", single cooldown: " << single.state().cooldown_remaining_seconds
                  << ", partitioned cooldown: " << partitioned.state().cooldown_remaining_seconds << '\n';
        throw std::runtime_error("Time partition changed event count");
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        require(expected[i].index() == actual[i].index(), "Time partition changed event order");
        if (const auto* event = std::get_if<WeaponEvent>(&expected[i])) {
            const auto& other = std::get<WeaponEvent>(actual[i]);
            require(event->type == other.type && event->phase == other.phase &&
                    event->action_id == other.action_id && near(event->time_seconds, other.time_seconds),
                    "Time partition changed action transition");
        } else if (const auto* shot = std::get_if<ShotFiredEvent>(&expected[i])) {
            const auto& other = std::get<ShotFiredEvent>(actual[i]);
            require(shot->rounds_remaining == other.rounds_remaining &&
                    near(shot->time_seconds, other.time_seconds), "Time partition changed shot");
        } else {
            throw std::runtime_error("Unexpected dry fire in partition test");
        }
    }
    require(single.state().weapon.phase == partitioned.state().weapon.phase,
            "Time partition changed final phase");
    require(single.state().rounds_remaining == partitioned.state().rounds_remaining,
            "Time partition changed ammo count");
    require(near(single.state().cooldown_remaining_seconds, partitioned.state().cooldown_remaining_seconds),
            "Time partition changed remaining cooldown");
}

void independent_weapons_and_no_idle_backlog() {
    const auto shared = weapon_definition();
    FiringMechanism rifle(shared, firing_definition(), 30);
    FiringMechanism turret(shared, firing_definition(), 30);
    require(&rifle.weapon_definition() == &turret.weapon_definition(), "Definitions were not shared");
    rifle.advance(100.0);
    const auto shot = collect<ShotFiredEvent>(rifle.execute(TriggerCommand::Press));
    require(shot.size() == 1 && near(shot[0].time_seconds, 100.0), "Idle time accumulated a shot backlog");
    require(turret.state().rounds_remaining == 30 && !turret.state().trigger_held &&
            turret.state().weapon.time_seconds == 0.0, "Instances shared runtime state");
    auto snapshot = rifle.state();
    snapshot.rounds_remaining = 0;
    snapshot.trigger_held = false;
    require(rifle.state().rounds_remaining == 29 && rifle.state().trigger_held,
            "Snapshot modification changed firing state");
}

void invalid_definitions_are_rejected() {
    auto config = firing_definition();
    config.action_id = 99;
    rejects([&] { FiringMechanism gun(weapon_definition(), config); });
    config = firing_definition();
    config.mode = static_cast<FireMode>(99);
    rejects([&] { FiringMechanism gun(weapon_definition(), config); });
    for (double interval : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()}) {
        rejects([&] { FiringMechanism gun(weapon_definition(), firing_definition(FireMode::Automatic, interval)); });
    }
    rejects([] { FiringMechanism gun(weapon_definition(), firing_definition(FireMode::Automatic, 0.1, 0)); });
    rejects([] { FiringMechanism gun(weapon_definition(), firing_definition(FireMode::Automatic, 0.1, 5, 0)); });
    rejects([] { FiringMechanism gun(weapon_definition(), firing_definition(FireMode::Automatic, 0.1, 5, 6)); });
    rejects([] { FiringMechanism gun(weapon_definition(), firing_definition(), 31); });
    rejects([] { FiringMechanism gun(nullptr, firing_definition()); });
}

void invalid_time_and_input_preserve_state() {
    FiringMechanism rifle(weapon_definition({7, 0.1, 0.1, 0.1}), firing_definition(), 30);
    rifle.execute(TriggerCommand::Press);
    for (double step : {-1.0, std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::quiet_NaN()}) {
        rejects([&] { rifle.advance(step); });
    }
    rejects([&] { rifle.execute(static_cast<TriggerCommand>(99)); });
    const auto state = rifle.state();
    require(state.trigger_held && state.rounds_remaining == 30 && state.weapon.phase == Phase::Windup &&
            near(state.weapon.phase_remaining_seconds, 0.1) && state.weapon.time_seconds == 0.0 &&
            near(state.cooldown_remaining_seconds, 0.1), "Invalid input mutated firing state");
    rifle.cancel_action();
    rifle.advance(std::numeric_limits<double>::max());
    rejects([&] { rifle.advance(std::numeric_limits<double>::max()); });
    require(std::isfinite(rifle.state().weapon.time_seconds), "Clock overflow was committed");
}

void positive_tiny_intervals_do_not_collapse() {
    FiringMechanism gun(weapon_definition(), firing_definition(FireMode::Automatic, 1e-12), 5);
    auto events = gun.execute(TriggerCommand::Press);
    append(events, gun.advance(0.5e-12));
    require(collect<ShotFiredEvent>(events).size() == 1, "Short positive interval was treated as zero");
    const auto second = collect<ShotFiredEvent>(gun.advance(0.5e-12));
    require(second.size() == 1 && second[0].time_seconds > 0.0, "Short positive interval was lost");
}

} // namespace

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"Semi-auto edges", semi_auto_uses_press_edges},
        {"Semi-auto cadence and rejected presses", semi_auto_does_not_queue_or_bypass_cadence},
        {"Automatic catch-up", automatic_catches_up_at_boundaries},
        {"Automatic cooldown and recovery", automatic_waits_for_cooldown_and_recovery},
        {"Activation and event order", ammo_is_consumed_at_activation_in_event_order},
        {"Cancellation and ammo", cancellation_preserves_ammo_and_cooldown},
        {"Empty magazine", empty_magazine_has_no_phantom_shots_or_spam},
        {"Multi-round shots", multi_round_shots_preserve_remainder},
        {"Equivalent time partitions", equivalent_time_partitions_preserve_full_event_stream},
        {"Independent instances and idle time", independent_weapons_and_no_idle_backlog},
        {"Invalid definitions", invalid_definitions_are_rejected},
        {"Invalid time and input", invalid_time_and_input_preserve_state},
        {"Tiny positive intervals", positive_tiny_intervals_do_not_collapse}
    };
    int failed = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "PASS: " << test.first << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL: " << test.first << ": " << error.what() << '\n';
        }
    }
    return failed == 0 ? 0 : 1;
}
