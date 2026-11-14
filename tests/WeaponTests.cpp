#include <weapon_library/Weapon.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
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

auto definition(std::vector<ActionDefinition> actions = {{1, 0.25, 0.125, 0.5}}) {
    return std::make_shared<const WeaponDefinition>(std::move(actions));
}

void require_event(const WeaponEvent& event, EventType type, Phase phase,
                   double time, ActionId action_id = 1) {
    require(event.type == type, "Unexpected event type");
    require(event.phase == phase, "Unexpected event phase");
    require(event.action_id == action_id, "Unexpected event action ID");
    require(near(event.time_seconds, time), "Incorrect transition timestamp");
}

void action_lifecycle() {
    Weapon weapon(definition());
    require(weapon.state().phase == Phase::Ready, "New weapon is busy");
    require(!weapon.state().action_id, "New weapon has an active action");
    const auto start = weapon.execute(WeaponCommand::start(1));
    require(start.accepted() && start.events.size() == 1, "Start failed");
    require_event(start.events[0], EventType::ActionStarted, Phase::Windup, 0.0);
    require(weapon.advance(0.125).empty(), "Attack activated before windup elapsed");
    require(near(weapon.state().phase_remaining_seconds, 0.125), "Wrong remaining windup");
    auto events = weapon.advance(0.125);
    require(events.size() == 1, "Boundary did not activate attack exactly once");
    require_event(events[0], EventType::PhaseEntered, Phase::Active, 0.25);
    events = weapon.advance(0.125);
    require(events.size() == 1, "Recovery event missing");
    require_event(events[0], EventType::PhaseEntered, Phase::Recovery, 0.375);
    events = weapon.advance(0.5);
    require(events.size() == 1, "Completion event missing");
    require_event(events[0], EventType::ActionCompleted, Phase::Ready, 0.875);
    require(!weapon.state().action_id, "Completed action ID was retained");
    require(weapon.state().phase_remaining_seconds == 0.0, "Ready weapon has a timer");
    require(weapon.advance(1.0).empty(), "Completed action emitted duplicate events");
    require(weapon.execute(WeaponCommand::start(1)).accepted(), "Weapon cannot attack again");
}

void large_step_preserves_transitions() {
    Weapon weapon(definition());
    weapon.advance(2.0);
    const auto start = weapon.execute(WeaponCommand::start(1));
    require_event(start.events[0], EventType::ActionStarted, Phase::Windup, 2.0);
    const auto events = weapon.advance(10.0);
    require(events.size() == 3, "Large update skipped transitions");
    require_event(events[0], EventType::PhaseEntered, Phase::Active, 2.25);
    require_event(events[1], EventType::PhaseEntered, Phase::Recovery, 2.375);
    require_event(events[2], EventType::ActionCompleted, Phase::Ready, 2.875);
    require(weapon.state().phase == Phase::Ready, "Large update left action running");
    require(near(weapon.state().time_seconds, 12.0), "Unused update time was discarded");
}

void equivalent_time_partitions() {
    // Decimal durations intentionally exercise ordinary floating-point rounding.
    const auto shared = definition({{1, 0.1, 0.2, 0.3}});
    Weapon single(shared);
    Weapon partitioned(shared);
    single.execute(WeaponCommand::start(1));
    partitioned.execute(WeaponCommand::start(1));
    const auto expected = single.advance(0.75);
    std::vector<WeaponEvent> actual;
    for (int i = 0; i < 75; ++i) {
        const auto events = partitioned.advance(0.01);
        actual.insert(actual.end(), events.begin(), events.end());
    }
    require(actual.size() == expected.size(), "Time partition changed event count");
    for (std::size_t i = 0; i < actual.size(); ++i) {
        require_event(actual[i], expected[i].type, expected[i].phase,
                      expected[i].time_seconds);
    }
    require(single.state().phase == partitioned.state().phase, "Time partition changed phase");
    require(near(single.state().time_seconds, partitioned.state().time_seconds),
            "Time partition changed final clock");
}

void decimal_phase_boundaries_do_not_delay_completion() {
    Weapon single(definition({{1, 0.1, 0.2, 0.3}}));
    Weapon partitioned(definition({{1, 0.1, 0.2, 0.3}}));
    single.execute(WeaponCommand::start(1));
    partitioned.execute(WeaponCommand::start(1));
    const auto events = single.advance(0.6);
    require(events.size() == 3 && events.back().type == EventType::ActionCompleted,
            "Decimal boundary delayed completion by one tick");
    for (int i = 0; i < 60; ++i) {
        partitioned.advance(0.01);
    }
    require(partitioned.state().phase == Phase::Ready, "Partitioned decimal boundary delayed completion");

    Weapon tiny(definition({{1, 1e-12, 0.0, 0.0}}));
    tiny.execute(WeaponCommand::start(1));
    require(tiny.advance(0.0).empty(), "Positive tiny phase collapsed into an instant phase");
    require(tiny.advance(0.5e-12).empty(), "Tiny phase completed too early");
    require(tiny.advance(0.5e-12).size() == 3, "Tiny phase did not complete at its boundary");
}

void instant_actions_and_zero_updates() {
    Weapon instant(definition({{1, 0.0, 0.0, 0.0}}));
    const auto result = instant.execute(WeaponCommand::start(1));
    require(result.events.size() == 4, "Instant action lost transitions");
    require_event(result.events[0], EventType::ActionStarted, Phase::Windup, 0.0);
    require_event(result.events[1], EventType::PhaseEntered, Phase::Active, 0.0);
    require_event(result.events[2], EventType::PhaseEntered, Phase::Recovery, 0.0);
    require_event(result.events[3], EventType::ActionCompleted, Phase::Ready, 0.0);
    require(instant.advance(0.0).empty(), "Instant action repeated on zero update");
    require(instant.state().phase == Phase::Ready, "Instant action never completed");

    Weapon immediate(definition({{1, 0.0, 0.0, 0.25}}));
    require(immediate.execute(WeaponCommand::start(1)).events.size() == 3,
            "Zero windup/active phases were delayed");
    require(immediate.state().phase == Phase::Recovery, "Recovery not entered immediately");
    require(immediate.advance(0.0).empty(), "Zero step advanced nonzero recovery");
    require(immediate.advance(0.25).size() == 1, "Immediate action did not complete");

    Weapon trailing(definition({{1, 0.25, 0.0, 0.0}}));
    trailing.execute(WeaponCommand::start(1));
    require(trailing.advance(0.25).size() == 3, "Trailing zero phases were postponed");
    require(trailing.state().phase == Phase::Ready, "Trailing zero phases remain busy");
}

void rejected_commands_preserve_action() {
    Weapon weapon(definition({{1, 0.25, 0.125, 0.5}, {2, 1.0, 1.0, 1.0}}));
    auto result = weapon.execute(WeaponCommand::start(99));
    require(result.status == CommandStatus::UnknownAction && result.events.empty(),
            "Unknown action accepted");
    require(weapon.state().phase == Phase::Ready, "Unknown action made weapon busy");
    weapon.execute(WeaponCommand::start(1));
    weapon.advance(0.125);
    result = weapon.execute(WeaponCommand::start(2));
    require(result.status == CommandStatus::Busy && result.events.empty(),
            "Busy weapon accepted another action");
    require(weapon.state().action_id == 1, "Rejected command replaced active action");
    require(near(weapon.state().phase_remaining_seconds, 0.125), "Rejected command reset timer");
    require(weapon.advance(0.75).back().type == EventType::ActionCompleted,
            "Rejected command prevented original action from completing");
    require(weapon.execute(WeaponCommand::start(2)).accepted(), "Second action unavailable");
    const auto events = weapon.advance(3.0);
    require(events.back().action_id == 2, "Second action emitted wrong identity");
}

void cancellation_respects_phase_policy() {
    Weapon weapon(definition());
    require(weapon.execute(WeaponCommand::cancel()).status == CommandStatus::NoActiveAction,
            "Idle cancellation accepted");
    weapon.execute(WeaponCommand::start(1));
    weapon.advance(0.125);
    const auto cancelled = weapon.execute(WeaponCommand::cancel());
    require(cancelled.accepted() && cancelled.events.size() == 1, "Windup cancellation failed");
    require_event(cancelled.events[0], EventType::ActionCancelled, Phase::Ready, 0.125);
    require(weapon.advance(5.0).empty(), "Cancelled action emitted more events");
    weapon.execute(WeaponCommand::start(1));
    weapon.advance(0.25);
    require(weapon.execute(WeaponCommand::cancel()).status == CommandStatus::CancellationBlocked,
            "Default policy permits active cancellation");
    require(weapon.state().phase == Phase::Active, "Blocked cancellation changed state");
    weapon.advance(0.125);
    require(weapon.execute(WeaponCommand::cancel()).status == CommandStatus::CancellationBlocked,
            "Default policy permits skipping recovery");

    Weapon configurable(definition({{1, 0.25, 0.125, 0.5, {false, true, true}}}));
    configurable.execute(WeaponCommand::start(1));
    require(configurable.execute(WeaponCommand::cancel()).status == CommandStatus::CancellationBlocked,
            "Windup cancellation policy ignored");
    configurable.advance(0.25);
    require(configurable.execute(WeaponCommand::cancel()).accepted(), "Active cancellation ignored");
    configurable.execute(WeaponCommand::start(1));
    configurable.advance(0.375);
    require(configurable.execute(WeaponCommand::cancel()).accepted(), "Recovery cancellation ignored");
}

void instances_and_snapshots_are_independent() {
    const auto shared = definition();
    Weapon first(shared);
    Weapon second(shared);
    require(&first.definition() == &second.definition(), "Definition was copied per instance");
    first.execute(WeaponCommand::start(1));
    first.advance(0.25);
    require(second.state().phase == Phase::Ready && second.state().time_seconds == 0.0,
            "Shared definition also shared runtime state");
    auto snapshot = first.state();
    snapshot.phase = Phase::Ready;
    snapshot.phase_remaining_seconds = 100.0;
    require(first.state().phase == Phase::Active, "Snapshot modification changed weapon");
    require(near(first.state().phase_remaining_seconds, 0.125), "Snapshot changed live timer");
}

void invalid_configuration_is_rejected() {
    rejects([] { Weapon weapon(nullptr); });
    rejects([] { definition({{1}, {1}}); });
    const double invalid[] = {-0.01, std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()};
    for (double value : invalid) {
        rejects([&] { definition({{1, value, 0.0, 0.0}}); });
        rejects([&] { definition({{1, 0.0, value, 0.0}}); });
        rejects([&] { definition({{1, 0.0, 0.0, value}}); });
    }
    const double huge = std::numeric_limits<double>::max();
    rejects([&] { definition({{1, huge, huge, huge}}); });
    Weapon empty(definition({}));
    require(empty.execute(WeaponCommand::start(0)).status == CommandStatus::UnknownAction,
            "Empty definition invented an action");
    Weapon zero_id(definition({{0}}));
    require(zero_id.execute(WeaponCommand::start(0)).accepted(), "Zero ID is not supported");
}

void invalid_time_does_not_mutate_state() {
    Weapon weapon(definition());
    weapon.execute(WeaponCommand::start(1));
    const double invalid[] = {-1.0, std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()};
    for (double value : invalid) {
        rejects([&] { weapon.advance(value); });
        require(weapon.state().time_seconds == 0.0, "Invalid time changed clock");
        require(weapon.state().phase == Phase::Windup, "Invalid time changed phase");
        require(near(weapon.state().phase_remaining_seconds, 0.25), "Invalid time changed timer");
    }
    weapon.advance(std::numeric_limits<double>::max());
    rejects([&] { weapon.advance(std::numeric_limits<double>::max()); });
    require(std::isfinite(weapon.state().time_seconds), "Clock overflow was committed");
    require(weapon.state().phase == Phase::Ready, "Clock overflow changed ready state");
}

} // namespace

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"Action lifecycle", action_lifecycle},
        {"Large time step", large_step_preserves_transitions},
        {"Equivalent time partitions", equivalent_time_partitions},
        {"Decimal phase boundaries", decimal_phase_boundaries_do_not_delay_completion},
        {"Instant actions", instant_actions_and_zero_updates},
        {"Rejected commands", rejected_commands_preserve_action},
        {"Cancellation policy", cancellation_respects_phase_policy},
        {"Independent instances", instances_and_snapshots_are_independent},
        {"Invalid configuration", invalid_configuration_is_rejected},
        {"Invalid time", invalid_time_does_not_mutate_state}
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
