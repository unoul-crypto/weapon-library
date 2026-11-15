#include <weapon_library/Ballistics.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace weapon_library::ballistics {

Medium Medium::air(Vec3 flow) noexcept { return {0, 1.225, flow}; }
Medium Medium::water(Vec3 flow) noexcept { return {1, 1000.0, flow}; }

namespace {

bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool positive(double v) { return std::isfinite(v) && v > 0.0; }
bool nonnegative(double v) { return std::isfinite(v) && v >= 0.0; }
void validate_medium(const Medium& medium) {
    if (!nonnegative(medium.density_kg_m3) || !finite(medium.flow_velocity)) {
        throw std::invalid_argument("Medium density and flow must be finite; density must be nonnegative");
    }
}
bool same_medium(const Medium& a, const Medium& b) {
    return a.id == b.id && a.density_kg_m3 == b.density_kg_m3 &&
           a.flow_velocity.x == b.flow_velocity.x && a.flow_velocity.y == b.flow_velocity.y &&
           a.flow_velocity.z == b.flow_velocity.z;
}

struct Motion { Vec3 position, velocity; };

Motion integrate(const ProjectileState& state, const ProjectileDefinition& def, double seconds) {
    // Symmetric gravity kicks around the exact quadratic-drag solution in a
    // constant medium. Drag alone cannot reverse velocity relative to the flow.
    const Vec3 half_gravity = def.gravity * (seconds * 0.5);
    const Vec3 relative = state.velocity + half_gravity - state.medium.flow_velocity;
    const double speed = length(relative);
    const double k = 0.5 * state.medium.density_kg_m3 * def.drag_coefficient *
                     def.cross_section_m2 / def.mass_kg;
    const double rate = k * speed;
    const double drag_time = rate * seconds;
    if (!std::isfinite(rate) || !std::isfinite(drag_time)) {
        throw std::overflow_error("Ballistic drag exceeds numeric range");
    }
    const double travel_time = drag_time == 0.0 ? seconds : std::log1p(drag_time) / rate;
    const Vec3 position = state.position + state.medium.flow_velocity * seconds + relative * travel_time;
    const Vec3 velocity = state.medium.flow_velocity + relative * (1.0 / (1.0 + drag_time)) + half_gravity;
    if (!finite(position) || !finite(velocity)) {
        throw std::overflow_error("Ballistic motion exceeds numeric range");
    }
    return {position, velocity};
}

double interaction_time(const ProjectileState& state, const ProjectileDefinition& def,
                        const Motion& end, double seconds, double fraction) {
    if (fraction == 0.0 || fraction == 1.0) return seconds * fraction;
    const Vec3 chord = end.position - state.position;
    const double chord_length = length(chord);
    if (chord_length == 0.0) return seconds * fraction;
    const Vec3 direction{chord.x / chord_length, chord.y / chord_length, chord.z / chord_length};
    // A chord fraction is a spatial fraction, not a time fraction under drag.
    // Solve the projected travel time before applying the new medium.
    double low = 0.0, high = seconds;
    for (int i = 0; i < 42; ++i) {
        const double middle = (low + high) * 0.5;
        const Vec3 travelled = integrate(state, def, middle).position - state.position;
        if (dot(travelled, direction) / chord_length < fraction) low = middle;
        else high = middle;
    }
    return (low + high) * 0.5;
}

} // namespace

Projectile::Projectile(ProjectileDefinition definition, Vec3 position, Vec3 velocity, Medium medium)
    : definition_(definition) {
    if (!positive(definition.mass_kg) || !nonnegative(definition.cross_section_m2) ||
        !nonnegative(definition.drag_coefficient) || !finite(definition.gravity) ||
        !positive(definition.lifetime_seconds) || !positive(definition.max_step_seconds) ||
        !positive(definition.max_segment_m) || !finite(position) || !finite(velocity)) {
        throw std::invalid_argument("Invalid projectile definition or launch state");
    }
    validate_medium(medium);
    state_.position = position;
    state_.velocity = velocity;
    state_.medium = medium;
}

std::vector<ProjectileEvent> Projectile::advance(double seconds, const ProjectileWorld& world) {
    if (!nonnegative(seconds) || !std::isfinite(state_.age_seconds + seconds)) {
        throw std::invalid_argument("Projectile time step must be finite, nonnegative and not overflow");
    }
    std::vector<ProjectileEvent> events;
    if (state_.status != ProjectileStatus::Flying || seconds == 0.0) return events;
    const double target_age = std::min(state_.age_seconds + seconds, definition_.lifetime_seconds);
    unsigned zero_time_boundaries = 0;
    while (state_.age_seconds < target_age) {
        double step = std::min(definition_.max_step_seconds, target_age - state_.age_seconds);
        auto next = integrate(state_, definition_, step);
        double distance = length(next.position - state_.position);
        while (distance > definition_.max_segment_m) {
            step *= std::max(0.01, 0.9 * definition_.max_segment_m / distance);
            if (step == 0.0 || state_.age_seconds + step == state_.age_seconds) {
                throw std::overflow_error("Projectile step is below available time precision");
            }
            next = integrate(state_, definition_, step);
            distance = length(next.position - state_.position);
        }
        if (state_.age_seconds + step == state_.age_seconds) {
            throw std::overflow_error("Projectile clock cannot advance at this precision");
        }
        const auto interaction = world.trace({state_.position, next.position, state_.age_seconds, step}, state_.medium);
        if (interaction) {
            if (!nonnegative(interaction->fraction) || interaction->fraction > 1.0 ||
                !finite(interaction->normal)) {
                throw std::invalid_argument("World returned an invalid segment interaction");
            }
            if (interaction->type == InteractionType::MediumBoundary) {
                validate_medium(interaction->next_medium);
                if (same_medium(state_.medium, interaction->next_medium)) {
                    throw std::invalid_argument("Medium boundary must change the medium");
                }
            } else if (interaction->type != InteractionType::Impact) {
                throw std::invalid_argument("World returned an unknown interaction type");
            }
            const double used = interaction_time(state_, definition_, next, step, interaction->fraction);
            if (state_.age_seconds + used == state_.age_seconds &&
                interaction->type == InteractionType::MediumBoundary) {
                if (++zero_time_boundaries > 8) {
                    throw std::runtime_error("World returned repeated zero-time medium boundaries");
                }
            } else zero_time_boundaries = 0;
            const auto at_contact = integrate(state_, definition_, used);
            state_.position = state_.position + (next.position - state_.position) * interaction->fraction;
            state_.velocity = at_contact.velocity;
            state_.age_seconds = std::min(target_age, state_.age_seconds + used);
            state_.distance_m += distance * interaction->fraction;
            if (interaction->type == InteractionType::Impact) {
                state_.status = ProjectileStatus::Impacted;
                events.push_back({ProjectileEventType::Impact, state_, interaction->object_id, interaction->normal});
                return events;
            }
            state_.medium = interaction->next_medium;
            events.push_back({ProjectileEventType::MediumChanged, state_, interaction->object_id, interaction->normal});
        } else {
            state_.position = next.position;
            state_.velocity = next.velocity;
            state_.age_seconds = std::min(target_age, state_.age_seconds + step);
            state_.distance_m += distance;
            zero_time_boundaries = 0;
        }
    }
    if (state_.age_seconds >= definition_.lifetime_seconds) {
        state_.status = ProjectileStatus::Expired;
        events.push_back({ProjectileEventType::Expired, state_});
    }
    return events;
}

} // namespace weapon_library::ballistics
