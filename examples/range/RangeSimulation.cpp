#include "RangeSimulation.h"
#include "RangeVisuals.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace weapon_range {

namespace {
using namespace weapon_library::ballistics;
constexpr double movement_speed = 3.2;

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
    explicit RangeWorld(const std::array<Target, 4>& targets, bool include_media = true)
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
                consider({InteractionType::Impact, t, i + 1, {0, 0, delta.z < 0.0 ? 1.0 : -1.0}});
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
    const std::array<Target, 4>& targets_;
    bool include_media_;
};

struct SurfaceContact {
    std::uint64_t object_id;
    double fraction; // Distance fraction along the blade, not time.
    Vec3 position, normal;
};

// Sweep a sphere along the blade against a convex shape. The bounding box only
// rejects candidates: contact positions come from the actual shape's surface.
template <typename ClosestPoint>
std::optional<SurfaceContact> sweep_convex(Vec3 from, Vec3 delta, double radius, Vec3 low, Vec3 high,
    std::uint64_t id, Vec3 fallback_normal, ClosestPoint closest) {
    const Vec3 padding{radius, radius, radius};
    const auto bounds = intersect_box(from, delta, low - padding, high + padding);
    if (!bounds || bounds->exit < 0 || bounds->entry > 1) return std::nullopt;
    const auto distance_squared = [&](double t) {
        const auto p = from + delta * t;
        const auto offset = p - closest(p);
        return dot(offset, offset);
    };
    const double start = std::max(0.0, bounds->entry), end = std::min(1.0, bounds->exit);
    double left = start, right = end;
    // Distance to a convex shape along a segment is convex; find its minimum,
    // then the first radius crossing. This also handles rounded disk/box edges.
    for (int i = 0; i < 64; ++i) {
        const double a = left + (right - left) / 3.0, b = right - (right - left) / 3.0;
        if (distance_squared(a) <= distance_squared(b)) right = b;
        else left = a;
    }
    const double minimum = (left + right) * 0.5;
    const double radius_squared = radius * radius;
    if (distance_squared(minimum) > radius_squared + 1e-12) return std::nullopt;
    left = start; right = minimum;
    if (distance_squared(start) > radius_squared) {
        for (int i = 0; i < 48; ++i) {
            const double middle = (left + right) * 0.5;
            if (distance_squared(middle) <= radius_squared) right = middle;
            else left = middle;
        }
    } else right = start;
    const auto center = from + delta * right;
    const auto point = closest(center);
    const auto offset = center - point;
    const auto normal = length(offset) > 1e-10 ? weapon_library::math::normalized(offset) : fallback_normal;
    return SurfaceContact{id, right, point, normal};
}

std::optional<SurfaceContact> trace_melee_volume(const std::array<Target, 4>& targets, Vec3 origin, Vec3 tip, double radius) {
    const auto delta = tip - origin;
    std::optional<SurfaceContact> nearest;
    const auto consider = [&](const std::optional<SurfaceContact>& hit) {
        if (hit && (!nearest || hit->fraction < nearest->fraction)) nearest = hit;
    };
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const auto& target = targets[i];
        const Vec3 size{target.radius, target.radius, 0};
        consider(sweep_convex(origin, delta, radius, target.center - size, target.center + size, i + 1,
            Vec3{0, 0, delta.z <= 0 ? 1.0 : -1.0}, [&](Vec3 p) {
                const double dx = p.x - target.center.x, dy = p.y - target.center.y;
                const double distance = std::hypot(dx, dy);
                const double scale = distance > target.radius ? target.radius / distance : 1.0;
                return Vec3{target.center.x + dx * scale, target.center.y + dy * scale, target.center.z};
            }));
    }
    const auto& box = firing_cover();
    consider(sweep_convex(origin, delta, radius, box.min, box.max, 300, Vec3{0, 0, 1}, [&](Vec3 p) {
        return Vec3{std::clamp(p.x, box.min.x, box.max.x), std::clamp(p.y, box.min.y, box.max.y), std::clamp(p.z, box.min.z, box.max.z)};
    }));
    const auto plane = [&](double value, double travel, double low, double high, Vec3 axis, std::uint64_t id) {
        for (int side : {-1, 1}) {
            const double boundary = side < 0 ? low : high;
            const double padded = boundary - side * radius;
            if (travel * side <= 0 || (value + travel - padded) * side < 0) continue;
            const double t = std::max(0.0, (padded - value) / travel);
            if (t > 1.0) continue;
            const auto point = origin + delta * t + axis * (boundary - (value + travel * t));
            consider(SurfaceContact{id, t, point, axis * -side});
        }
    };
    plane(origin.x, delta.x, -7, 7, {1, 0, 0}, 100);
    plane(origin.y, delta.y, 0, 6, {0, 1, 0}, 101);
    plane(origin.z, delta.z, -33, 9, {0, 0, 1}, 102);
    return nearest;
}

// Demo adapter: sweep a blade capsule through small angular/movement samples.
// Production games can replace this with their physics engine's continuous sweep.
class RangeMeleeWorld final : public weapon_library::MeleeWorld {
public:
    RangeMeleeWorld(const std::array<Target, 4>& targets, Vec3 eye, Vec3 velocity, Vec3 forward, double time)
        : targets_(targets), eye_(eye), velocity_(velocity), forward_(forward), time_(time) {}
    std::vector<weapon_library::MeleeContact> sweep(const weapon_library::MeleeSweep& query) const override {
        const auto right = weapon_library::math::normalized(weapon_library::math::cross(forward_, {0, 1, 0}));
        const double duration = query.to_time_seconds - query.from_time_seconds;
        const int steps = duration == 0.0 ? 0 : std::max(1, static_cast<int>(std::ceil(std::max(
            (query.to_progress - query.from_progress) * query.arc_radians / 0.01,
            duration * length(velocity_) / 0.01))));
        for (int i = 0; i <= steps; ++i) {
            const double fraction = steps == 0 ? 0.0 : static_cast<double>(i) / steps;
            const double progress = query.from_progress + (query.to_progress - query.from_progress) * fraction;
            const double angle = (progress - 0.5) * query.arc_radians;
            const auto direction = forward_ * std::cos(angle) + right * std::sin(angle);
            auto origin = eye_ + velocity_ * (query.from_time_seconds + duration * fraction - time_);
            origin.x = std::clamp(origin.x, -5.5, 5.5);
            origin.z = std::clamp(origin.z, 1.5, 7.0);
            const auto tip = origin + direction * query.reach_m;
            if (const auto hit = trace_melee_volume(targets_, origin, tip, query.radius_m))
                return {{hit->object_id, fraction, hit->position, hit->normal, true}};
        }
        return {};
    }
private:
    const std::array<Target, 4>& targets_;
    Vec3 eye_, velocity_, forward_;
    double time_;
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
        values[0].handling.aimed_sway_amplitude_rad = {0.0045, 0.003};
        values[0].reserve_rounds = 60;
        values[0].reload_seconds = 1.2;
        values[1].handling.hip_spread_rad = 0.025;
        values[1].handling.recoil_per_shot.pitch_rad = 0.006;
        values[1].handling.recoil_yaw_jitter_rad = 0.0025;
        values[1].handling.bloom_per_shot_rad = 0.0022;
        values[1].handling.recoil_return_rate = 5.0;
        values[1].handling.aimed_sway_amplitude_rad = {0.0035, 0.0025};
        values[1].reserve_rounds = 150;
        values[1].reload_seconds = 1.8;
        values[2].handling.hip_spread_rad = 0.014;
        values[2].handling.aimed_spread_rad = 0.0015;
        values[2].handling.recoil_per_shot.pitch_rad = 0.004;
        values[2].handling.recoil_yaw_jitter_rad = 0.001;
        values[2].handling.bloom_per_shot_rad = 0.0015;
        values[2].handling.recoil_return_rate = 4.0;
        values[2].handling.aimed_sway_amplitude_rad = {0.0018, 0.0012};
        values[2].reserve_rounds = 180;
        values[2].reload_seconds = 2.4;
        return values;
    }();
    return specs;
}

RangeSimulation::RangeSimulation() {
    for (unsigned i = 0; i < weapon_count; ++i) {
        auto config = default_visual(i);
        presentation_[i] = std::move(config.presentation);
        aim_sway_[i] = config.aim_sway;
    }
    reset();
}

void RangeSimulation::reset() {
    using namespace weapon_library;
    for (std::size_t i = 0; i < weapons_.size(); ++i) {
        const auto& spec = weapon_specs()[i];
        const auto actions = std::make_shared<const WeaponDefinition>(std::vector<ActionDefinition>{
            {1, 0.0, 0.0, 0.045}, {2, spec.reload_seconds, 0.0, 0.0, {true, true, true}}});
        weapons_[i] = std::make_unique<FiringMechanism>(
            actions, FiringDefinition{1, spec.mode, spec.interval_seconds, spec.capacity, 1, 2}, spec.capacity);
        handling_[i] = std::make_unique<WeaponHandling>(spec.handling, 0x92d68ca2u + static_cast<std::uint32_t>(i) * 7919u);
        reserves_[i] = std::make_shared<AmmunitionReserve>(spec.reserve_rounds);
    }
    handling_[knife_index] = std::make_unique<WeaponHandling>(HandlingDefinition{});
    for (unsigned i = 0; i < weapon_count; ++i) handling_[i]->set_aim_sway(aim_sway_[i]);
    const auto knife_actions = std::make_shared<const WeaponDefinition>(std::vector<ActionDefinition>{
        {3, 0.16, 0.18, 0.28, {true, true, true}}});
    melee_ = std::make_unique<MeleeMechanism>(knife_actions, MeleeDefinition{3, 1.6, 0.08, 0.0});
    targets_ = {{{{-3.0, 1.65, -9.0}}, {{0.0, 1.65, -17.0}}, {{3.0, 1.65, -27.0}}, {{-2.3, 1.65, 2.8}, 0.45}}};
    traces_.clear();
    projectiles_.clear();
    recent_results_.clear();
    last_result_ = {};
    movement_velocity_ = {};
    eye_ = {0.0, 1.65, 5.0};
    yaw_ = pitch_ = time_ = 0.0;
    selected_ = shots_ = hits_ = score_ = 0;
    sway_ = weapon_library::WeaponSway(presentation().definition().mass_kg);
    last_shot_time_ = last_hit_time_ = last_dry_time_ = -10.0;
    water_entries_ = 0;
    aiming_ = false;
    melee_trigger_held_ = false;
    attacks_ = melee_hits_ = 0;
    last_reload_status_ = ReloadStatus::Accepted;
    last_reload_notice_time_ = -10.0;
}

void RangeSimulation::select_weapon(unsigned index) {
    if (index >= weapon_count) {
        throw std::invalid_argument("Unknown range weapon");
    }
    if (index == selected_) return;
    set_trigger(false);
    if (is_melee()) cancel_attack();
    else if (weapons_[selected_]->state().reload.active) cancel_reload();
    handling_[selected_]->set_aiming(false);
    selected_ = index;
    sway_ = weapon_library::WeaponSway(presentation().definition().mass_kg);
    handling_[selected_]->set_aiming(!is_melee() && aiming_ && !weapon_state().reload.active);
    last_reload_notice_time_ = -10.0;
}

void RangeSimulation::set_aiming(bool aiming) {
    aiming_ = aiming;
    handling_[selected_]->set_aiming(!is_melee() && aiming && !weapon_state().reload.active);
}

weapon_library::ReloadStatus RangeSimulation::request_reload() {
    if (is_melee()) return weapon_library::ReloadStatus::Unsupported;
    const auto result = weapons_[selected_]->request_reload(reserves_[selected_]);
    consume(result.events, selected_);
    if (result.accepted()) {
        set_trigger(false);
        handling_[selected_]->set_aiming(false);
    }
    last_reload_status_ = result.status;
    last_reload_notice_time_ = time_;
    return result.status;
}

weapon_library::ReloadStatus RangeSimulation::cancel_reload() {
    if (is_melee()) return weapon_library::ReloadStatus::Unsupported;
    const auto result = weapons_[selected_]->cancel_reload();
    consume(result.events, selected_);
    last_reload_status_ = result.status;
    last_reload_notice_time_ = time_;
    return result.status;
}

Vec3 RangeSimulation::aim() const {
    return handling_[selected_]->recoiled_direction(view_direction(yaw_, pitch_));
}

Vec3 RangeSimulation::shot_center_direction() const {
    return handling_[selected_]->shot_center_direction(view_direction(yaw_, pitch_));
}

weapon_library::HandlingState RangeSimulation::handling_state() const { return handling_[selected_]->state(); }

WeaponPose RangeSimulation::weapon_pose() const {
    return weapon_pose(selected_);
}

weapon_library::PresentationPose RangeSimulation::sway_pose() const {
    return sway_.pose(handling_state().aim_fraction, presentation().definition().sway_strength);
}

void RangeSimulation::set_presentation(unsigned index, weapon_library::WeaponPresentation presentation) {
    if (index >= presentation_.size()) throw std::invalid_argument("Unknown range weapon");
    if (index == selected_) sway_.set_mass_kg(presentation.definition().mass_kg);
    presentation_[index] = std::move(presentation);
}

void RangeSimulation::set_aim_sway(unsigned index, weapon_library::AimSwaySettings settings) {
    if (index >= aim_sway_.size()) throw std::invalid_argument("Unknown range weapon");
    aim_sway_[index] = settings;
    handling_[index]->set_aim_sway(settings);
}

WeaponPose RangeSimulation::weapon_pose(unsigned weapon_index) const {
    const auto base = view_direction(yaw_, pitch_);
    const auto camera_forward = handling_[weapon_index]->recoiled_direction(base);
    const auto camera_right = weapon_library::math::normalized(weapon_library::math::cross(camera_forward, {0, 1, 0}));
    const auto camera_up = weapon_library::math::cross(camera_right, camera_forward);
    const auto forward = handling_[weapon_index]->shot_center_direction(base);
    const auto right = weapon_library::math::normalized(weapon_library::math::cross(forward, {0, 1, 0}));
    const auto up = weapon_library::math::cross(right, forward);
    const double ads = handling_[weapon_index]->state().aim_fraction;
    const auto& presentation = presentation_[weapon_index];
    const auto offset = presentation.mount_offset(ads);
    const auto muzzle = presentation.definition().muzzle_offset;
    const auto origin = eye_ + camera_right * offset.x + camera_up * offset.y + camera_forward * offset.z;
    return {origin, origin + right * muzzle.x + up * muzzle.y + forward * muzzle.z, forward, right, up};
}

void RangeSimulation::set_movement(double forward, double strafe) {
    if (!std::isfinite(forward) || !std::isfinite(strafe)) throw std::invalid_argument("Movement must be finite");
    forward = std::clamp(forward, -1.0, 1.0);
    strafe = std::clamp(strafe, -1.0, 1.0);
    const double magnitude = std::hypot(forward, strafe);
    movement_velocity_ = (view_direction(yaw_, 0.0) * forward +
                         Vec3{std::cos(yaw_), 0, std::sin(yaw_)} * strafe) * (movement_speed / std::max(1.0, magnitude));
    for (auto& handling : handling_) handling->set_movement_factor(std::min(1.0, magnitude));
}

void RangeSimulation::set_trigger(bool held) {
    if (is_melee()) {
        const bool pressed = held && !melee_trigger_held_;
        melee_trigger_held_ = held;
        if (pressed) {
            const RangeMeleeWorld world(targets_, eye_, movement_velocity_, aim(), time_);
            consume_melee(melee_->execute(weapon_library::MeleeCommand::Attack, world).events);
        }
        return;
    }
    consume(weapons_[selected_]->execute(held ? weapon_library::TriggerCommand::Press
                                            : weapon_library::TriggerCommand::Release), selected_);
}

void RangeSimulation::look(double yaw_delta, double pitch_delta) {
    if (!std::isfinite(yaw_delta) || !std::isfinite(pitch_delta)) {
        throw std::invalid_argument("Look input must be finite");
    }
    constexpr double full_turn = 6.283185307179586;
    const double turn = std::remainder(yaw_delta, full_turn);
    const double next_pitch = std::clamp(pitch_ + pitch_delta, -1.25, 1.25);
    sway_.add_look_delta(turn, next_pitch - pitch_);
    yaw_ = std::remainder(yaw_ + turn, full_turn);
    pitch_ = next_pitch;
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
            advance_melee(at - cursor);
            advance_projectiles(at - cursor);
            advance_handling(at - cursor);
            move(at - cursor);
            shoot(at, shot.weapon_index);
            cursor = at;
        }
        advance_melee(end - cursor);
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
    if (seconds <= 0.0) return;
    const auto old_eye = eye_;
    eye_ = eye_ + movement_velocity_ * seconds;
    eye_.x = std::clamp(eye_.x, -5.5, 5.5);
    eye_.z = std::clamp(eye_.z, 1.5, 7.0);
    const double movement = std::min(1.0, weapon_library::math::length(eye_ - old_eye) / (movement_speed * seconds));
    sway_.advance(seconds, movement);
}

weapon_library::FiringState RangeSimulation::weapon_state() const {
    if (!is_melee()) return weapons_[selected_]->state();
    weapon_library::FiringState state{};
    state.weapon = melee_->state().weapon;
    return state;
}

weapon_library::CommandStatus RangeSimulation::cancel_attack() {
    const RangeMeleeWorld world(targets_, eye_, movement_velocity_, view_direction(yaw_, pitch_), time_);
    const auto result = melee_->execute(weapon_library::MeleeCommand::Cancel, world);
    consume_melee(result.events);
    return result.status;
}

void RangeSimulation::advance_melee(double seconds) {
    const RangeMeleeWorld world(targets_, eye_, movement_velocity_, view_direction(yaw_, pitch_), melee_->state().weapon.time_seconds);
    consume_melee(melee_->advance(seconds, world));
}

void RangeSimulation::consume_melee(const std::vector<weapon_library::MeleeEvent>& events) {
    for (const auto& event : events) {
        if (const auto* action = std::get_if<weapon_library::WeaponEvent>(&event)) {
            if (action->type == weapon_library::EventType::ActionStarted) ++attacks_;
        } else if (const auto* hit = std::get_if<weapon_library::MeleeHitEvent>(&event)) {
            ShotResult result;
            result.position = hit->position;
            result.normal = hit->normal;
            result.time_seconds = hit->time_seconds;
            result.melee = true;
            if (hit->target_id >= 1 && hit->target_id <= targets_.size()) {
                auto& target = targets_[static_cast<std::size_t>(hit->target_id - 1)];
                ++target.hits;
                ++melee_hits_;
                target.last_hit_time = hit->time_seconds;
                last_hit_time_ = std::max(last_hit_time_, hit->time_seconds);
                score_ += 5;
                result.outcome = ShotOutcome::Target;
            } else if (hit->target_id == 300) result.outcome = ShotOutcome::Cover;
            else if (hit->normal.y > 0.5) result.outcome = ShotOutcome::Floor;
            else if (hit->normal.y < -0.5) result.outcome = ShotOutcome::Ceiling;
            else result.outcome = ShotOutcome::Wall;
            if (result.time_seconds >= last_result_.time_seconds) last_result_ = result;
            recent_results_.push_back(result);
        }
    }
}

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
