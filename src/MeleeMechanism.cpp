#include <weapon_library/MeleeMechanism.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace weapon_library {
namespace {
bool finite(math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
} // namespace

MeleeMechanism::MeleeMechanism(std::shared_ptr<const WeaponDefinition> actions, MeleeDefinition definition)
    : weapon_(std::move(actions)), definition_(definition) {
    if (!std::isfinite(definition.reach_m) || definition.reach_m <= 0.0 ||
        !std::isfinite(definition.sweep_radius_m) || definition.sweep_radius_m < 0.0 ||
        !std::isfinite(definition.arc_radians) || definition.arc_radians < 0.0 || definition.arc_radians > 6.283185307179586)
        throw std::invalid_argument("Melee reach must be positive; radius and arc must be finite and nonnegative (arc <= 2*pi)");
    const auto& list = weapon_.definition().actions();
    const auto action = std::find_if(list.begin(), list.end(), [&](const auto& value) { return value.id == definition.action_id; });
    if (action == list.end()) throw std::invalid_argument("Melee action ID is not defined");
    active_seconds_ = action->active_seconds;
    total_seconds_ = action->windup_seconds + action->active_seconds + action->recovery_seconds;
}

MeleeState MeleeMechanism::state() const noexcept {
    const auto weapon = weapon_.state();
    const double progress = weapon.phase == Phase::Ready ? 0.0 : total_seconds_ == 0.0 ? 1.0 :
        std::clamp((weapon.time_seconds - started_at_) / total_seconds_, 0.0, 1.0);
    return {weapon, attack_id_, static_cast<std::uint32_t>(targets_.size()), blocked_, progress};
}

MeleeCommandResult MeleeMechanism::execute(MeleeCommand command, const MeleeWorld& world) {
    if (command != MeleeCommand::Attack && command != MeleeCommand::Cancel)
        throw std::invalid_argument("Unknown melee command");
    const auto before = weapon_.state();
    if (command == MeleeCommand::Attack && before.phase == Phase::Ready && attack_id_ == (std::numeric_limits<MeleeAttackId>::max)())
        throw std::overflow_error("Melee attack ID overflow");
    const auto result = weapon_.execute(command == MeleeCommand::Attack ? WeaponCommand::start(definition_.action_id) : WeaponCommand::cancel());
    return {result.status, consume(result.events, before, world)};
}

std::vector<MeleeEvent> MeleeMechanism::advance(double seconds, const MeleeWorld& world) {
    const auto before = weapon_.state();
    const auto transitions = weapon_.advance(seconds);
    return consume(transitions, before, world);
}

std::vector<MeleeEvent> MeleeMechanism::consume(const std::vector<WeaponEvent>& transitions, WeaponState before, const MeleeWorld& world) {
    std::vector<MeleeEvent> events;
    auto phase = before.phase;
    double cursor = before.time_seconds;
    for (const auto& transition : transitions) {
        if (phase == Phase::Active && transition.time_seconds > cursor) query(cursor, transition.time_seconds, world, events);
        events.emplace_back(transition);
        if (transition.type == EventType::ActionStarted) {
            ++attack_id_;
            targets_.clear();
            blocked_ = false;
            started_at_ = transition.time_seconds;
        }
        phase = transition.phase;
        cursor = transition.time_seconds;
        if (transition.type == EventType::PhaseEntered && phase == Phase::Active) {
            active_at_ = cursor;
            query(cursor, cursor, world, events); // One overlap sample, even for zero-duration active phases.
        }
    }
    if (phase == Phase::Active && weapon_.state().time_seconds > cursor) query(cursor, weapon_.state().time_seconds, world, events);
    return events;
}

void MeleeMechanism::query(double from, double to, const MeleeWorld& world, std::vector<MeleeEvent>& events) {
    if (blocked_ || (definition_.max_targets && targets_.size() >= definition_.max_targets)) return;
    const auto progress = [&](double time) {
        return active_seconds_ == 0.0 ? 0.5 : std::clamp((time - active_at_) / active_seconds_, 0.0, 1.0);
    };
    const MeleeSweep request{definition_.action_id, attack_id_, from, to, progress(from), progress(to),
        definition_.reach_m, definition_.sweep_radius_m, definition_.arc_radians};
    auto contacts = world.sweep(request);
    for (const auto& contact : contacts) {
        if (!std::isfinite(contact.fraction) || contact.fraction < 0.0 || contact.fraction > 1.0 ||
            !finite(contact.position) || !finite(contact.normal))
            throw std::invalid_argument("Melee world returned an invalid contact");
    }
    std::stable_sort(contacts.begin(), contacts.end(), [](const auto& a, const auto& b) { return a.fraction < b.fraction; });
    for (const auto& contact : contacts) {
        if (targets_.insert(contact.target_id).second) {
            if (targets_.size() > (std::numeric_limits<std::uint32_t>::max)()) throw std::overflow_error("Melee hit count overflow");
            events.emplace_back(MeleeHitEvent{definition_.action_id, attack_id_, contact.target_id,
                from + (to - from) * contact.fraction, contact.position, contact.normal, contact.blocking});
        }
        if (contact.blocking) blocked_ = true;
        if (blocked_ || (definition_.max_targets && targets_.size() >= definition_.max_targets)) break;
    }
}

} // namespace weapon_library
