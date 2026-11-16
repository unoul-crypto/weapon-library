#include <weapon_library/FiringMechanism.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

using namespace weapon_library;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool near(double a, double b) { return std::abs(a - b) < 1e-9; }
template <typename Function> void rejects(Function function) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Invalid reload configuration accepted");
}
template <typename Event> std::vector<Event> collect(const std::vector<FiringEvent>& events) {
    std::vector<Event> result;
    for (const auto& event : events) if (const auto* value = std::get_if<Event>(&event)) result.push_back(*value);
    return result;
}
auto actions(ActionDefinition fire = {1}, ActionDefinition reload = {2, 0.5, 0.25, 0.25, {true, true, true}}) {
    return std::make_shared<const WeaponDefinition>(std::vector<ActionDefinition>{fire, reload});
}
FiringMechanism gun(unsigned loaded = 3, ActionDefinition fire = {1},
                    ActionDefinition reload = {2, 0.5, 0.25, 0.25, {true, true, true}},
                    FireMode mode = FireMode::Automatic, unsigned cost = 1) {
    return FiringMechanism(actions(fire, reload), {1, mode, 0.1, 10, cost, 2}, loaded);
}

void top_up_commits_only_at_completion() {
    auto weapon = gun(3);
    auto reserve = std::make_shared<AmmunitionReserve>(20);
    const auto start = weapon.request_reload(reserve);
    require(start.accepted() && weapon.state().reload.active && near(weapon.state().reload.remaining_seconds, 1.0),
            "Reload did not start with its full duration");
    require(collect<ReloadEvent>(start.events).size() == 1 && collect<ShotFiredEvent>(start.events).empty(),
            "Reload start emitted a shot or lost its event");
    auto middle = weapon.advance(0.5);
    require(weapon.state().weapon.phase == Phase::Active && weapon.state().rounds_remaining == 3 &&
            reserve->available_rounds() == 20 && collect<ShotFiredEvent>(middle).empty(),
            "Entering reload Active fired a phantom shot or transferred ammo early");
    weapon.advance(0.25);
    require(weapon.state().rounds_remaining == 3 && reserve->available_rounds() == 20,
            "Reload recovery transferred ammo early");
    const auto complete = collect<ReloadEvent>(weapon.advance(0.25));
    require(complete.size() == 1 && complete[0].type == ReloadEventType::Completed &&
            complete[0].rounds_transferred == 7 && near(complete[0].time_seconds, 1.0), "Reload completion event is wrong");
    require(weapon.state().rounds_remaining == 10 && reserve->available_rounds() == 13 && !weapon.state().reload.active,
            "Top-up discarded loaded rounds or duplicated reserve rounds");
    require(collect<ReloadEvent>(weapon.advance(10.0)).empty() && reserve->available_rounds() == 13,
            "Reload completion repeated the transfer");
}

void cancellation_never_spends_ammo_and_releases_source() {
    auto weapon = gun(2);
    auto reserve = std::make_shared<AmmunitionReserve>(8);
    std::weak_ptr<AmmunitionSource> weak = reserve;
    weapon.request_reload(reserve);
    weapon.advance(0.3);
    const auto cancel = weapon.cancel_reload();
    require(cancel.accepted() && !weapon.state().reload.active && weapon.state().rounds_remaining == 2 &&
            reserve->available_rounds() == 8, "Cancelled reload changed ammo");
    const auto events = collect<ReloadEvent>(cancel.events);
    require(events.size() == 1 && events[0].type == ReloadEventType::Cancelled && near(events[0].time_seconds, 0.3),
            "Cancellation event missing");
    weapon.request_reload(reserve);
    reserve.reset();
    require(!weak.expired(), "Reload did not keep its source alive");
    require(weapon.cancel_action().accepted() && weak.expired() && !weapon.state().reload.active,
            "Generic action cancellation leaked the reload source");
    weapon.advance(5.0);
    require(weapon.state().rounds_remaining == 2, "Cancelled reload completed later");
}

void reload_waits_for_committed_shot_and_blocks_held_trigger() {
    auto weapon = gun(10, {1, 0.2, 0.1, 0.2});
    auto reserve = std::make_shared<AmmunitionReserve>(10);
    weapon.execute(TriggerCommand::Press);
    require(weapon.request_reload(reserve).accepted() && weapon.state().reload.waiting_for_action,
            "Reload during a committed windup was not queued");
    const auto fire = collect<ShotFiredEvent>(weapon.advance(0.2));
    require(fire.size() == 1 && weapon.state().rounds_remaining == 9 && reserve->available_rounds() == 10,
            "Queued reload cancelled or refilled the committed shot early");
    const auto started = collect<ReloadEvent>(weapon.advance(0.3));
    require(started.size() == 1 && started[0].type == ReloadEventType::Started && near(started[0].time_seconds, 0.5) &&
            !weapon.state().reload.waiting_for_action, "Reload did not start at the shot completion boundary");
    const auto completed = collect<ReloadEvent>(weapon.advance(1.0));
    require(completed.size() == 1 && near(completed[0].time_seconds, 1.5) && reserve->available_rounds() == 9,
            "Queued reload used the wrong transfer time");
    require(collect<ShotFiredEvent>(weapon.advance(2.0)).empty() && weapon.state().rounds_remaining == 10,
            "Held trigger resumed automatically after reload");
    weapon.execute(TriggerCommand::Release);
    require(collect<ShotFiredEvent>(weapon.execute(TriggerCommand::Press)).empty(), "Windup fired immediately");
    require(collect<ShotFiredEvent>(weapon.advance(0.2)).size() == 1, "Fresh trigger did not rearm after reload");
}

void pressing_during_reload_is_not_a_queued_shot() {
    auto weapon = gun(0);
    auto reserve = std::make_shared<AmmunitionReserve>(10);
    weapon.request_reload(reserve);
    weapon.execute(TriggerCommand::Release);
    weapon.execute(TriggerCommand::Press);
    require(collect<ShotFiredEvent>(weapon.advance(3.0)).empty() && weapon.state().rounds_remaining == 10,
            "Press during reload became an automatic shot at completion");
    weapon.execute(TriggerCommand::Release);
    require(collect<ShotFiredEvent>(weapon.execute(TriggerCommand::Press)).size() == 1,
            "Fresh trigger press remained blocked after reload");
}

void partial_and_shared_reserves_preserve_every_round() {
    auto first = gun(0), second = gun(0);
    auto reserve = std::make_shared<AmmunitionReserve>(13);
    first.request_reload(reserve); second.request_reload(reserve);
    first.advance(1.0); second.advance(1.0);
    require(first.state().rounds_remaining == 10 && second.state().rounds_remaining == 3 && reserve->available_rounds() == 0,
            "Shared source duplicated or lost ammunition");

    auto partial = gun(2, {1}, {2, 0.5}, FireMode::Automatic, 3);
    auto few = std::make_shared<AmmunitionReserve>(3);
    partial.request_reload(few); partial.advance(0.5);
    require(partial.state().rounds_remaining == 5 && few->available_rounds() == 0, "Partial top-up lost existing rounds");
    auto events = partial.execute(TriggerCommand::Press);
    const auto later = partial.advance(0.2);
    events.insert(events.end(), later.begin(), later.end());
    require(collect<ShotFiredEvent>(events).size() == 1 && partial.state().rounds_remaining == 2,
            "Partial reserve allowed a partial-cost shot or ammo underflow");
}

void changed_inventory_is_queried_only_at_commit() {
    auto weapon = gun(0);
    auto reserve = std::make_shared<AmmunitionReserve>(5);
    weapon.request_reload(reserve);
    reserve->take_up_to(5); // Another inventory consumer spends the source while reloading.
    const auto events = collect<ReloadEvent>(weapon.advance(1.0));
    require(events.size() == 1 && events[0].rounds_transferred == 0 && weapon.state().rounds_remaining == 0,
            "Reload used an obsolete reservation after the source changed");
    require(!weapon.state().reload.active, "Zero-round completion remained stuck reloading");
}

void instant_reload_and_rejection_reasons() {
    auto weapon = gun(0, {1}, {2});
    auto reserve = std::make_shared<AmmunitionReserve>(15);
    const auto result = weapon.request_reload(reserve);
    const auto events = collect<ReloadEvent>(result.events);
    require(result.accepted() && events.size() == 2 && events[0].type == ReloadEventType::Started &&
            events[1].type == ReloadEventType::Completed && weapon.state().rounds_remaining == 10 &&
            reserve->available_rounds() == 5 && !weapon.state().reload.active,
            "Instant reload failed to commit exactly once");
    require(weapon.request_reload(reserve).status == ReloadStatus::MagazineFull, "Full magazine accepted reload");
    require(weapon.cancel_reload().status == ReloadStatus::NotReloading, "Idle cancellation accepted");
    auto empty = gun(0);
    require(empty.request_reload(nullptr).status == ReloadStatus::NoSource, "Null source accepted");
    require(empty.request_reload(std::make_shared<AmmunitionReserve>()).status == ReloadStatus::NoAmmunition,
            "Empty reserve accepted");
    empty.request_reload(reserve);
    require(empty.request_reload(reserve).status == ReloadStatus::Busy, "Duplicate reload accepted");
    FiringMechanism unsupported(actions(), {1, FireMode::Automatic, 0.1, 10}, 0);
    require(unsupported.request_reload(reserve).status == ReloadStatus::Unsupported, "Unconfigured reload accepted");
}

void cancellation_policy_and_queued_cancellation() {
    auto locked = gun(1, {1}, {2, 0.5, 0.25, 0.25, {false, false, false}});
    auto reserve = std::make_shared<AmmunitionReserve>(20);
    locked.request_reload(reserve);
    require(locked.cancel_reload().status == ReloadStatus::CancellationBlocked && !locked.cancel_action().accepted(),
            "Cancellation bypassed the reload action policy");
    locked.advance(0.5);
    require(locked.cancel_reload().status == ReloadStatus::CancellationBlocked, "Active cancellation policy ignored");
    locked.advance(0.25);
    require(locked.cancel_reload().status == ReloadStatus::CancellationBlocked, "Recovery cancellation policy ignored");
    locked.advance(0.25);
    require(locked.state().rounds_remaining == 10 && reserve->available_rounds() == 11,
            "Blocked cancellation damaged the eventual transfer");
    auto queued = gun(10, {1, 0.2, 0.0, 0.1});
    queued.execute(TriggerCommand::Press);
    queued.request_reload(reserve);
    require(queued.cancel_reload().accepted(), "Queued intent could not be cancelled");
    require(collect<ShotFiredEvent>(queued.advance(1.0)).size() == 1 && queued.state().rounds_remaining == 9 &&
            reserve->available_rounds() == 11, "Cancelling queued reload altered the committed shot or reserve");
    auto abort_both = gun(10, {1, 0.2});
    abort_both.execute(TriggerCommand::Press);
    abort_both.request_reload(reserve);
    require(abort_both.cancel_action().accepted() && !abort_both.state().reload.active &&
            collect<ShotFiredEvent>(abort_both.advance(2.0)).empty() && reserve->available_rounds() == 11,
            "Generic cancellation left a queued reload or spent its pending shot");
}

void reload_does_not_bypass_firing_cooldown() {
    FiringMechanism weapon(actions({1}, {2, 0.01}), {1, FireMode::SemiAutomatic, 0.5, 10, 1, 2}, 10);
    auto reserve = std::make_shared<AmmunitionReserve>(10);
    weapon.execute(TriggerCommand::Press); weapon.execute(TriggerCommand::Release);
    weapon.request_reload(reserve); weapon.advance(0.01);
    require(collect<ShotFiredEvent>(weapon.execute(TriggerCommand::Press)).empty(), "Reload bypassed firing cooldown");
    require(collect<ShotFiredEvent>(weapon.advance(1.0)).empty(), "Semi-auto queued an ineligible press");
    weapon.execute(TriggerCommand::Release);
    require(collect<ShotFiredEvent>(weapon.execute(TriggerCommand::Press)).size() == 1, "Reload left trigger permanently blocked");
}

std::vector<FiringEvent> timeline(bool partitioned) {
    auto weapon = gun(10, {1, 0.1, 0.0, 0.1}, {2, 0.2, 0.1, 0.1, {true, true, true}});
    auto reserve = std::make_shared<AmmunitionReserve>(10);
    weapon.execute(TriggerCommand::Press); weapon.request_reload(reserve);
    std::vector<FiringEvent> events;
    for (int i = 0; i < (partitioned ? 100 : 1); ++i) {
        const auto next = weapon.advance(partitioned ? 0.01 : 1.0);
        events.insert(events.end(), next.begin(), next.end());
    }
    require(weapon.state().rounds_remaining == 10 && reserve->available_rounds() == 9,
            "Timeline lost ammo while crossing multiple actions");
    return events;
}

void frame_partition_keeps_reload_event_times() {
    const auto one = timeline(false), many = timeline(true);
    require(one.size() == many.size(), "Time partition changed event count");
    for (std::size_t i = 0; i < one.size(); ++i) {
        require(one[i].index() == many[i].index(), "Time partition changed event order");
        if (const auto* a = std::get_if<WeaponEvent>(&one[i])) {
            const auto& b = std::get<WeaponEvent>(many[i]);
            require(a->type == b.type && a->phase == b.phase && near(a->time_seconds, b.time_seconds), "Action timing changed");
        } else if (const auto* a = std::get_if<ReloadEvent>(&one[i])) {
            const auto& b = std::get<ReloadEvent>(many[i]);
            require(a->type == b.type && a->rounds_transferred == b.rounds_transferred &&
                    near(a->time_seconds, b.time_seconds), "Reload timing changed");
        } else if (const auto* a = std::get_if<ShotFiredEvent>(&one[i])) {
            require(near(a->time_seconds, std::get<ShotFiredEvent>(many[i]).time_seconds), "Shot timing changed");
        }
    }
}

void invalid_configuration_and_reserve_limits() {
    rejects([] { FiringMechanism weapon(actions(), {1, FireMode::Automatic, 0.1, 10, 1, 1}); });
    rejects([] { FiringMechanism weapon(actions(), {1, FireMode::Automatic, 0.1, 10, 1, 99}); });
    AmmunitionReserve maximum(std::numeric_limits<std::uint32_t>::max());
    require(maximum.add_rounds(10) == 0 && maximum.available_rounds() == std::numeric_limits<std::uint32_t>::max(),
            "Reserve overflowed when adding ammo");
    require(maximum.take_up_to(0) == 0, "Zero transfer changed a reserve");
}
}

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"Completion-only transfer", top_up_commits_only_at_completion},
        {"Cancellation and source lifetime", cancellation_never_spends_ammo_and_releases_source},
        {"Committed shot and held trigger", reload_waits_for_committed_shot_and_blocks_held_trigger},
        {"Blocked reload input", pressing_during_reload_is_not_a_queued_shot},
        {"Partial and shared reserves", partial_and_shared_reserves_preserve_every_round},
        {"Changing inventory", changed_inventory_is_queried_only_at_commit},
        {"Instant reload and rejections", instant_reload_and_rejection_reasons},
        {"Cancellation policy", cancellation_policy_and_queued_cancellation},
        {"Firing cadence", reload_does_not_bypass_firing_cooldown},
        {"Frame partition", frame_partition_keeps_reload_event_times},
        {"Validation and reserve limits", invalid_configuration_and_reserve_limits}
    };
    int failed = 0;
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS: " << test.first << '\n'; }
        catch (const std::exception& e) { ++failed; std::cerr << "FAIL: " << test.first << ": " << e.what() << '\n'; }
    }
    return failed == 0 ? 0 : 1;
}
