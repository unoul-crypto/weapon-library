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

struct BoxInterval {
    double entry = -1e300, exit = 1e300;
    Vec3 entry_normal, exit_normal;
};

std::optional<BoxInterval> intersect_box(Vec3 from, Vec3 delta, Vec3 low, Vec3 high) {
    BoxInterval hit;
    const auto slab = [&](double origin, double travel, double minimum, double maximum, Vec3 axis) {
        if (travel == 0.0) return origin >= minimum && origin <= maximum;
        double a = (minimum - origin) / travel, b = (maximum - origin) / travel;
        Vec3 first_normal = axis * -1.0, last_normal = axis;
        if (a > b) { std::swap(a, b); std::swap(first_normal, last_normal); }
        if (a > hit.entry) { hit.entry = a; hit.entry_normal = first_normal; }
        if (b < hit.exit) { hit.exit = b; hit.exit_normal = last_normal; }
        return hit.entry <= hit.exit;
    };
    if (!slab(from.x, delta.x, low.x, high.x, {1, 0, 0}) ||
        !slab(from.y, delta.y, low.y, high.y, {0, 1, 0}) ||
        !slab(from.z, delta.z, low.z, high.z, {0, 0, 1})) return std::nullopt;
    return hit;
}

class RangeWorld final : public ProjectileWorld {
public:
    explicit RangeWorld(const std::array<Target, 3>& targets, bool include_media = true)
        : targets_(targets), include_media_(include_media) {}
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
        const auto& cover = firing_cover();
        if (const auto hit = intersect_box(segment.from, delta, cover.min, cover.max)) {
            if (hit->entry >= 0.0) consider({InteractionType::Impact, hit->entry, 300, hit->entry_normal});
            else if (hit->exit >= 0.0) consider({InteractionType::Impact, 0.0, 300, hit->exit_normal * -1.0});
        }
        if (!include_media_) return nearest;
        const auto& water = water_volume();
        if (const auto hit = intersect_box(segment.from, delta, water.min, water.max); hit && hit->entry < hit->exit) {
            const bool leaving = medium.id == Medium::water().id;
            const double t = leaving ? hit->exit : hit->entry;
            if (t >= 0.0 && t <= 1.0) {
                consider({InteractionType::MediumBoundary, t, 200, {}, leaving ? Medium::air() : Medium::water()});
            }
        }
        return nearest;
    }
private:
    const std::array<Target, 3>& targets_;
    bool include_media_;
};
} // namespace

const WaterVolume& water_volume() { static const WaterVolume volume; return volume; }
const SolidBox& firing_cover() { static const SolidBox box; return box; }
Vec3 view_direction(double yaw, double pitch) {
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
}

const std::array<WeaponSpec, 3>& weapon_specs() {
    static const auto specs = [] {
        std::array<WeaponSpec, 3> values{{
        {"PISTOL", weapon_library::FireMode::SemiAutomatic, 0.2, 12, 320.0, 0.008, 0.000064},
        {"RIFLE", weapon_library::FireMode::Automatic, 0.1, 30, 820.0, 0.004, 0.000025},
        {"TURRET", weapon_library::FireMode::Automatic, 0.15, 60, 650.0, 0.04, 0.00008}
        }};
        values[0].handling.recoil_per_shot.pitch_rad = 0.012;
        values[0].handling.bloom_per_shot_rad = 0.004;
        values[1].handling.hip_spread_rad = 0.025;
        values[1].handling.recoil_per_shot.pitch_rad = 0.006;
        values[1].handling.recoil_yaw_jitter_rad = 0.0025;
        values[1].handling.bloom_per_shot_rad = 0.0022;
        values[1].handling.recoil_return_rate = 5.0;
        values[2].handling.hip_spread_rad = 0.014;
        values[2].handling.aimed_spread_rad = 0.0015;
        values[2].handling.recoil_per_shot.pitch_rad = 0.004;
        values[2].handling.recoil_yaw_jitter_rad = 0.001;
        values[2].handling.bloom_per_shot_rad = 0.0015;
        values[2].handling.recoil_return_rate = 4.0;
        return values;
    }();
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
        handling_[i] = std::make_unique<WeaponHandling>(spec.handling, 0x92d68ca2u + static_cast<std::uint32_t>(i) * 7919u);
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
    aiming_ = false;
}

void RangeSimulation::select_weapon(unsigned index) {
    if (index >= weapons_.size()) {
        throw std::invalid_argument("Unknown range weapon");
    }
    set_trigger(false);
    handling_[selected_]->set_aiming(false);
    selected_ = index;
    handling_[selected_]->set_aiming(aiming_);
}

void RangeSimulation::set_aiming(bool aiming) {
    aiming_ = aiming;
    handling_[selected_]->set_aiming(aiming);
}

Vec3 RangeSimulation::aim() const {
    return handling_[selected_]->recoiled_direction(view_direction(yaw_, pitch_));
}

weapon_library::HandlingState RangeSimulation::handling_state() const { return handling_[selected_]->state(); }

WeaponPose RangeSimulation::weapon_pose() const {
    return weapon_pose(selected_);
}

WeaponPose RangeSimulation::weapon_pose(unsigned weapon_index) const {
    const auto forward = handling_[weapon_index]->recoiled_direction(view_direction(yaw_, pitch_));
    const auto right = weapon_library::math::normalized(weapon_library::math::cross(forward, {0, 1, 0}));
    const auto up = weapon_library::math::cross(right, forward);
    const double ads = handling_[weapon_index]->state().aim_fraction;
    const auto origin = eye_ + forward * (0.58 - 0.08 * ads) + right * (0.24 * (1.0 - ads)) +
                        up * (-0.22 + 0.155 * ads);
    return {origin, origin + forward * (weapon_index == 0 ? 0.22 : 0.4), forward, right, up};
}

void RangeSimulation::set_movement(double forward, double strafe) {
    if (!std::isfinite(forward) || !std::isfinite(strafe)) throw std::invalid_argument("Movement must be finite");
    forward = std::clamp(forward, -1.0, 1.0);
    strafe = std::clamp(strafe, -1.0, 1.0);
    const double magnitude = std::hypot(forward, strafe);
    movement_velocity_ = (view_direction(yaw_, 0.0) * forward +
                         Vec3{std::cos(yaw_), 0, std::sin(yaw_)} * strafe) * (3.2 / std::max(1.0, magnitude));
    for (auto& handling : handling_) handling->set_movement_factor(std::min(1.0, magnitude));
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
    set_movement(forward, strafe);
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
            advance_handling(at - cursor);
            move(at - cursor);
            shoot(at, shot.weapon_index);
            cursor = at;
        }
        advance_projectiles(end - cursor);
        advance_handling(end - cursor);
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

void RangeSimulation::advance_handling(double seconds) {
    for (auto& handling : handling_) handling->advance(seconds);
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
    // Capture the pre-shot muzzle. Recoil from this shot affects subsequent aim.
    const auto pose = weapon_pose(weapon_index);
    const auto shot = handling_[weapon_index]->sample_shot(view_direction(yaw_, pitch_));
    const RangeWorld solids(targets_, false);
    const auto far_point = eye_ + shot.direction * 100.0;
    const auto sight_hit = solids.trace({eye_, far_point, 0.0, 0.0}, Medium::air());
    const auto aim_point = sight_hit ? eye_ + (far_point - eye_) * sight_hit->fraction : far_point;
    auto muzzle = pose.muzzle;
    if (const auto blocked = solids.trace({eye_, muzzle, 0.0, 0.0}, Medium::air())) {
        muzzle = eye_ + (muzzle - eye_) * blocked->fraction + blocked->normal * 0.0001;
    }
    const auto direction = weapon_library::math::normalized(aim_point - muzzle);
    ProjectileDefinition definition;
    definition.mass_kg = spec.mass_kg;
    definition.cross_section_m2 = spec.cross_section_m2;
    Vec3 inherited = movement_velocity_;
    if ((eye_.x <= -5.5 && inherited.x < 0) || (eye_.x >= 5.5 && inherited.x > 0)) inherited.x = 0;
    if ((eye_.z <= 1.5 && inherited.z < 0) || (eye_.z >= 7.0 && inherited.z > 0)) inherited.z = 0;
    projectiles_.push_back({Projectile(definition, muzzle, direction * spec.muzzle_speed_m_s + inherited,
                                      in_water(muzzle) ? Medium::water() : Medium::air()), timestamp, in_water(muzzle)});
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
            } else if (event.object_id == 300) result.outcome = ShotOutcome::Cover;
            else if (event.normal.y > 0.5) result.outcome = ShotOutcome::Floor;
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
