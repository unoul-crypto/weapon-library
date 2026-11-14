#include "RangeSimulation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace weapon_range {

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator*(Vec3 value, double scale) { return {value.x * scale, value.y * scale, value.z * scale}; }
Vec3 view_direction(double yaw, double pitch) {
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
}

const std::array<WeaponSpec, 3>& weapon_specs() {
    static const std::array<WeaponSpec, 3> specs{{
        {"PISTOL", weapon_library::FireMode::SemiAutomatic, 0.2, 12},
        {"RIFLE", weapon_library::FireMode::Automatic, 0.1, 30},
        {"TURRET", weapon_library::FireMode::Automatic, 0.15, 60}
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
    eye_ = {0.0, 1.65, 5.0};
    yaw_ = pitch_ = time_ = 0.0;
    selected_ = shots_ = hits_ = score_ = 0;
    last_shot_time_ = last_hit_time_ = last_dry_time_ = -10.0;
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
                                            : weapon_library::TriggerCommand::Release));
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
    // Keep the pose fixed while processing this interval's shot events. In the
    // live app these are 1/120-second intervals, not an end-of-frame world pose.
    for (auto& weapon : weapons_) {
        consume(weapon->advance(seconds));
    }
    const Vec3 ahead = view_direction(yaw_, 0.0);
    const Vec3 right{std::cos(yaw_), 0.0, std::sin(yaw_)};
    eye_ = eye_ + (ahead * forward + right * strafe) * (3.2 * seconds / length);
    eye_.x = std::clamp(eye_.x, -5.5, 5.5);
    eye_.z = std::clamp(eye_.z, 1.5, 7.0);
    time_ += seconds;
    traces_.erase(std::remove_if(traces_.begin(), traces_.end(), [&](const auto& trace) {
        return time_ - trace.time_seconds > 0.18;
    }), traces_.end());
}

weapon_library::FiringState RangeSimulation::weapon_state() const { return weapons_[selected_]->state(); }

void RangeSimulation::consume(const std::vector<weapon_library::FiringEvent>& events) {
    for (const auto& event : events) {
        if (const auto* shot = std::get_if<weapon_library::ShotFiredEvent>(&event)) {
            shoot(shot->time_seconds);
        } else if (const auto* dry = std::get_if<weapon_library::DryFireEvent>(&event)) {
            last_dry_time_ = dry->time_seconds;
        }
    }
}

void RangeSimulation::shoot(double timestamp) {
    ++shots_;
    last_shot_time_ = timestamp;
    const auto direction = aim();
    double distance = 80.0;
    // Clip against the same room surfaces drawn by the renderer.
    const auto clip = [&](double origin, double component, double low, double high) {
        if (component > 1e-12) distance = std::min(distance, (high - origin) / component);
        if (component < -1e-12) distance = std::min(distance, (low - origin) / component);
    };
    clip(eye_.x, direction.x, -7.0, 7.0);
    clip(eye_.y, direction.y, 0.0, 6.0);
    clip(eye_.z, direction.z, -33.0, 9.0);
    Target* hit = nullptr;
    double hit_radius = 0.0;
    if (direction.z < -1e-12) {
        for (auto& target : targets_) {
            const double candidate = (target.center.z - eye_.z) / direction.z;
            const auto point = eye_ + direction * candidate;
            const double radius = std::hypot(point.x - target.center.x, point.y - target.center.y);
            if (candidate > 0.0 && candidate < distance && radius <= target.radius) {
                distance = candidate;
                hit = &target;
                hit_radius = radius;
            }
        }
    }
    if (hit) {
        ++hits_;
        ++hit->hits;
        hit->last_hit_time = last_hit_time_ = timestamp;
        score_ += hit_radius < 0.18 ? 10 : hit_radius < 0.45 ? 5 : 1;
    }
    traces_.push_back({eye_, eye_ + direction * distance, timestamp, hit != nullptr});
}

} // namespace weapon_range
