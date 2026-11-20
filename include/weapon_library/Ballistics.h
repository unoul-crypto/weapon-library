#pragma once

#include <weapon_library/Math.h>
#include <weapon_library/HitContact.h>
#include <cstdint>
#include <optional>
#include <vector>

namespace weapon_library::ballistics {

// Compatibility aliases; geometry is also usable without the ballistics target.
using math::Vec3;
using math::operator+;
using math::operator-;
using math::operator*;
using math::dot;
using math::length;

struct Medium {
    std::uint32_t id = 0; // Application-defined; presets use 0 for air, 1 for water.
    double density_kg_m3 = 1.225;
    Vec3 flow_velocity;

    static Medium air(Vec3 flow = {}) noexcept;
    static Medium water(Vec3 flow = {}) noexcept;
};

struct ProjectileDefinition {
    double mass_kg = 0.008;
    double cross_section_m2 = 0.000064;
    double drag_coefficient = 0.3;
    Vec3 gravity = {0.0, -9.81, 0.0};
    double lifetime_seconds = 5.0;
    double max_step_seconds = 1.0 / 480.0;
    double max_segment_m = 0.25;
};

enum class ProjectileStatus { Flying, Impacted, Expired };

struct ProjectileState {
    Vec3 position, velocity;
    Medium medium;
    double age_seconds = 0.0;
    double distance_m = 0.0;
    ProjectileStatus status = ProjectileStatus::Flying;
};

struct FlightSegment {
    Vec3 from, to;
    double start_age_seconds;
    double duration_seconds;
};

enum class InteractionType { Impact, MediumBoundary };

struct SegmentInteraction {
    InteractionType type = InteractionType::Impact;
    double fraction = 1.0; // Position along the queried chord, in [0, 1].
    std::uint64_t object_id = 0;
    Vec3 normal;
    Medium next_medium; // Used only by MediumBoundary.
};

class ProjectileWorld {
public:
    virtual ~ProjectileWorld() = default;
    // Return the nearest impact or medium boundary along the ENTIRE segment.
    // current_medium resolves start-on-boundary cases without position nudges.
    virtual std::optional<SegmentInteraction> trace(
        const FlightSegment& segment, const Medium& current_medium) const = 0;
};

enum class ProjectileEventType { Impact, MediumChanged, Expired };

struct ProjectileEvent {
    ProjectileEventType type;
    ProjectileState state;
    std::uint64_t object_id = 0;
    Vec3 normal;

    // Only impacts are contacts. The host supplies the launch time in its simulation clock.
    std::optional<HitContact> hit_contact(double launch_time_seconds) const noexcept {
        if (type != ProjectileEventType::Impact) return std::nullopt;
        return HitContact{HitKind::Projectile, object_id, launch_time_seconds + state.age_seconds, state.position, normal};
    }
};

// Pure simulation, in metres, seconds and kilograms. The host owns entity IDs,
// world time and launch velocity (aim * muzzle speed + inherited platform velocity).
class Projectile {
public:
    Projectile(ProjectileDefinition definition, Vec3 position, Vec3 velocity,
               Medium initial_medium = Medium::air());
    const ProjectileDefinition& definition() const noexcept { return definition_; }
    ProjectileState state() const noexcept { return state_; }
    // Terminal projectiles never move or emit their terminal event again.
    // Input time is validated before mutation; invalid world responses throw.
    std::vector<ProjectileEvent> advance(double seconds, const ProjectileWorld& world);

private:
    ProjectileDefinition definition_;
    ProjectileState state_;
};

} // namespace weapon_library::ballistics
