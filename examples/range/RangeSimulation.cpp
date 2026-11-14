#include "RangeSimulation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace weapon_range {

namespace {
using namespace weapon_library::ballistics;

bool in_water(Vec3 p) {
    const auto& box = water_volume();
    return p.x >= box.min.x && p.x <= box.max.x && p.y >= box.min.y &&
           p.y <= box.max.y && p.z >= box.min.z && p.z <= box.max.z;
}

class RangeWorld final : public ProjectileWorld {
public:
    explicit RangeWorld(const std::array<Target, 3>& targets) : targets_(targets) {}
    std::optional<SegmentInteraction> trace(const FlightSegment& segment, const Medium& medium) const override {
        const auto delta = segment.to - segment.from;
        std::optional<SegmentInteraction> nearest;
        const auto consider = [&](SegmentInteraction interaction) {
            if (interaction.fraction >= 0.0 && interaction.fraction <= 1.0 &&
                (!nearest || interaction.fraction < nearest->fraction)) nearest = interaction;
        };
        const auto wall = [&](double origin, double travel, double low, double high,
                              Vec3 normal, std::uint64_t id) {
            if (travel > 0.0 && origin + travel >= high) {
                consider({InteractionType::Impact, (high - origin) / travel, id, normal * -1.0});
            } else if (travel < 0.0 && origin + travel <= low) {
                consider({InteractionType::Impact, (low - origin) / travel, id, normal});
            }
        };
        wall(segment.from.x, delta.x, -7, 7, {1, 0, 0}, 100);
        wall(segment.from.y, delta.y, 0, 6, {0, 1, 0}, 101);
        wall(segment.from.z, delta.z, -33, 9, {0, 0, 1}, 102);
        for (std::size_t i = 0; i < targets_.size(); ++i) {
            if (delta.z == 0.0) continue;
            const auto& target = targets_[i];
            const double t = (target.center.z - segment.from.z) / delta.z;
            const auto p = segment.from + delta * t;
            if (t >= 0.0 && t <= 1.0 &&
                std::hypot(p.x - target.center.x, p.y - target.center.y) <= target.radius) {
                consider({InteractionType::Impact, t, i + 1, {0, 0, 1}});
            }
        }
        const auto& water = water_volume();
        double entry = -1e300, exit = 1e300;
        const auto slab = [&](double origin, double travel, double low, double high) {
            if (travel == 0.0) return origin >= low && origin <= high;
            const double a = (low - origin) / travel, b = (high - origin) / travel;
            entry = std::max(entry, std::min(a, b));
            exit = std::min(exit, std::max(a, b));
            return entry <= exit;
        };
        if (slab(segment.from.x, delta.x, water.min.x, water.max.x) &&
            slab(segment.from.y, delta.y, water.min.y, water.max.y) &&
            slab(segment.from.z, delta.z, water.min.z, water.max.z) && entry < exit) {
            const bool leaving = medium.id == Medium::water().id;
            const double t = leaving ? exit : entry;
            if (t >= 0.0 && t <= 1.0) {
                consider({InteractionType::MediumBoundary, t, 200, {}, leaving ? Medium::air() : Medium::water()});
            }
        }
        return nearest;
    }
private:
    const std::array<Target, 3>& targets_;
};
} // namespace

const WaterVolume& water_volume() { static const WaterVolume volume; return volume; }
Vec3 view_direction(double yaw, double pitch) {
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
}

const std::array<WeaponSpec, 3>& weapon_specs() {
    static const std::array<WeaponSpec, 3> specs{{
        {"PISTOL", weapon_library::FireMode::SemiAutomatic, 0.2, 12, 320.0, 0.008, 0.000064},
        {"RIFLE", weapon_library::FireMode::Automatic, 0.1, 30, 820.0, 0.004, 0.000025},
        {"TURRET", weapon_library::FireMode::Automatic, 0.15, 60, 650.0, 0.04, 0.00008}
    }};
    return specs;
}

RangeSimulation::RangeSimulation() { reset(); }

void RangeSimulation::reset() {
    using namespace weapon_library;
    const auto actions = std::make_shared<const WeaponDefinition>(
        std::vector<ActionDefinition>{{1, 0.0, 0.0, 0.045}});
    for (std::size_t i = 0; i < weapons_.size(); ++i) {
        const auto& spec = weapon_specs()[i];
        weapons_[i] = std::make_unique<FiringMechanism>(
            actions, FiringDefinition{1, spec.mode, spec.interval_seconds, spec.capacity}, spec.capacity);
    }
    targets_ = {{{{-3.0, 1.65, -9.0}}, {{0.0, 1.65, -17.0}}, {{3.0, 1.65, -27.0}}}};
    traces_.clear();
    projectiles_.clear();
    recent_results_.clear();
    last_result_ = {};
    movement_velocity_ = {};
    eye_ = {0.0, 1.65, 5.0};
    yaw_ = pitch_ = time_ = 0.0;
    selected_ = shots_ = hits_ = score_ = 0;
    last_shot_time_ = last_hit_time_ = last_dry_time_ = -10.0;
    water_entries_ = 0;
}

void RangeSimulation::select_weapon(unsigned index) {
    if (index >= weapons_.size()) {
        throw std::invalid_argument("Unknown range weapon");
    }
    set_trigger(false);
    selected_ = index;
}

void RangeSimulation::set_trigger(bool held) {
    consume(weapons_[selected_]->execute(held ? weapon_library::TriggerCommand::Press
                                            : weapon_library::TriggerCommand::Release), selected_);
}

void RangeSimulation::look(double yaw_delta, double pitch_delta) {
    if (!std::isfinite(yaw_delta) || !std::isfinite(pitch_delta)) {
        throw std::invalid_argument("Look input must be finite");
    }
    yaw_ = std::remainder(yaw_ + yaw_delta, 6.283185307179586);
    pitch_ = std::clamp(pitch_ + pitch_delta, -1.25, 1.25);
}

void RangeSimulation::advance(double seconds, double forward, double strafe) {
    if (!std::isfinite(seconds) || seconds < 0.0 || !std::isfinite(time_ + seconds) ||
        !std::isfinite(forward) || !std::isfinite(strafe)) {
        throw std::invalid_argument("Range time and movement must be finite");
    }
    forward = std::clamp(forward, -1.0, 1.0);
    strafe = std::clamp(strafe, -1.0, 1.0);
    const double length = std::max(1.0, std::hypot(forward, strafe));
    const Vec3 ahead = view_direction(yaw_, 0.0);
    const Vec3 right{std::cos(yaw_), 0.0, std::sin(yaw_)};
    movement_velocity_ = (ahead * forward + right * strafe) * (3.2 / length);
    double budget_error = 0.0;
    while (seconds > 0.0) {
        double step = std::min(seconds, 1.0 / 120.0);
        // Fold a rounding-sized final remainder into this tick, rather than
        // creating a second tick too short to advance the simulation clock.
        if (seconds - step <= step * 1e-12) step = seconds;
        const double end = time_ + step;
        if (end == time_) throw std::overflow_error("Range clock cannot advance at this precision");
        std::vector<PendingShot> pending;
        for (unsigned i = 0; i < weapons_.size(); ++i) consume(weapons_[i]->advance(step), i, &pending);
        std::stable_sort(pending.begin(), pending.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
        double cursor = time_;
        for (const auto& shot : pending) {
            const double at = std::clamp(shot.time, cursor, end);
            advance_projectiles(at - cursor);
            move(at - cursor);
            shoot(at, shot.weapon_index);
            cursor = at;
        }
        advance_projectiles(end - cursor);
        move(end - cursor);
        time_ = end;
        if (step == seconds) seconds = 0.0;
        else {
            const double adjusted = -step - budget_error;
            const double remaining = seconds + adjusted;
            budget_error = (remaining - seconds) - adjusted;
            seconds = std::max(0.0, remaining);
        }
    }
    traces_.erase(std::remove_if(traces_.begin(), traces_.end(), [&](const auto& trace) {
        return time_ - trace.time_seconds > 0.12;
    }), traces_.end());
    recent_results_.erase(std::remove_if(recent_results_.begin(), recent_results_.end(), [&](const auto& result) {
        return time_ - result.time_seconds > 2.0;
    }), recent_results_.end());
}

void RangeSimulation::move(double seconds) {
    eye_ = eye_ + movement_velocity_ * seconds;
    eye_.x = std::clamp(eye_.x, -5.5, 5.5);
    eye_.z = std::clamp(eye_.z, 1.5, 7.0);
}

weapon_library::FiringState RangeSimulation::weapon_state() const { return weapons_[selected_]->state(); }

void RangeSimulation::consume(const std::vector<weapon_library::FiringEvent>& events,
                              unsigned weapon_index, std::vector<PendingShot>* pending) {
    for (const auto& event : events) {
        if (const auto* shot = std::get_if<weapon_library::ShotFiredEvent>(&event)) {
            if (pending) pending->push_back({shot->time_seconds, weapon_index});
            else shoot(shot->time_seconds, weapon_index);
        } else if (const auto* dry = std::get_if<weapon_library::DryFireEvent>(&event)) {
            last_dry_time_ = dry->time_seconds;
        }
    }
}

void RangeSimulation::shoot(double timestamp, unsigned weapon_index) {
    using namespace weapon_library::ballistics;
    ++shots_;
    last_shot_time_ = timestamp;
    const auto& spec = weapon_specs()[weapon_index];
    ProjectileDefinition definition;
    definition.mass_kg = spec.mass_kg;
    definition.cross_section_m2 = spec.cross_section_m2;
    Vec3 inherited = movement_velocity_;
    if ((eye_.x <= -5.5 && inherited.x < 0) || (eye_.x >= 5.5 && inherited.x > 0)) inherited.x = 0;
    if ((eye_.z <= 1.5 && inherited.z < 0) || (eye_.z >= 7.0 && inherited.z > 0)) inherited.z = 0;
    projectiles_.push_back({Projectile(definition, eye_, aim() * spec.muzzle_speed_m_s + inherited,
                                      in_water(eye_) ? Medium::water() : Medium::air()), timestamp, in_water(eye_)});
}

void RangeSimulation::advance_projectiles(double seconds) {
    using namespace weapon_library::ballistics;
    if (seconds <= 0.0) return;
    const RangeWorld world(targets_);
    for (auto& projectile : projectiles_) {
        const auto before = projectile.motion.state();
        const auto events = projectile.motion.advance(seconds, world);
        const auto after = projectile.motion.state();
        bool hit_target = false;
        bool travelled_in_water = before.medium.id == Medium::water().id || after.medium.id == Medium::water().id;
        for (const auto& event : events) {
            if (event.type == ProjectileEventType::MediumChanged && event.state.medium.id == Medium::water().id) {
                ++water_entries_;
                travelled_in_water = true;
                projectile.passed_water = true;
            }
            if (event.type == ProjectileEventType::MediumChanged) continue;
            ShotResult result;
            result.position = event.state.position;
            result.normal = event.normal;
            result.time_seconds = projectile.launched_at + event.state.age_seconds;
            result.flight_seconds = event.state.age_seconds;
            result.speed_m_s = length(event.state.velocity);
            result.passed_water = projectile.passed_water;
            if (event.type == ProjectileEventType::Expired) result.outcome = ShotOutcome::Expired;
            else if (event.object_id >= 1 && event.object_id <= targets_.size()) {
                auto& target = targets_[static_cast<std::size_t>(event.object_id - 1)];
                const double radius = std::hypot(event.state.position.x - target.center.x,
                                                  event.state.position.y - target.center.y);
                ++target.hits;
                ++hits_;
                target.last_hit_time = std::max(target.last_hit_time, result.time_seconds);
                last_hit_time_ = std::max(last_hit_time_, result.time_seconds);
                score_ += radius < 0.18 ? 10 : radius < 0.45 ? 5 : 1;
                hit_target = true;
                result.outcome = ShotOutcome::Target;
            } else if (event.normal.y > 0.5) result.outcome = ShotOutcome::Floor;
            else if (event.normal.y < -0.5) result.outcome = ShotOutcome::Ceiling;
            else result.outcome = ShotOutcome::Wall;
            if (result.time_seconds >= last_result_.time_seconds) last_result_ = result;
            recent_results_.push_back(result);
        }
        traces_.push_back({before.position, after.position, projectile.launched_at + after.age_seconds,
                           hit_target, travelled_in_water});
    }
    projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(), [](const auto& projectile) {
        return projectile.motion.state().status != ProjectileStatus::Flying;
    }), projectiles_.end());
}

} // namespace weapon_range
