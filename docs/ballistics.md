# Ballistics

The independent C++17 target `weapon_library::ballistics` provides point-projectile
motion without a dependency on the weapon runtime, a renderer or a physics engine.
It is built by default and can be omitted with `WEAPON_LIBRARY_BUILD_BALLISTICS=OFF`.
The interactive demo requires it; the action and firing tests work without it.

## Motion model

Use metres, seconds and kilograms in a consistent coordinate frame. The launch
velocity is `aim_direction * muzzle_speed + platform_velocity`; normalize the aim
in the game. The library receives the resulting velocity rather than deciding
which weapon, cartridge or moving mount produced it.

`ProjectileDefinition` contains mass, reference cross-section, drag coefficient,
gravity vector, lifetime, maximum integration time step and maximum segment length.
`Medium` contains an application-defined ID, density and flow velocity. Convenience
presets use density 1.225 for air and 1000 for water; games can supply other values.

For velocity relative to the medium `u = velocity - flow_velocity`, the model uses
quadratic drag acceleration:

```text
k = density * drag_coefficient * cross_section / (2 * mass)
drag_acceleration = -k * length(u) * u
```

The force relation follows the [NASA Glenn drag equation](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/drag-equation/).
The implementation applies half a gravity step, the exact constant-medium drag
solution, then the other half gravity step. In particular, drag alone does not
reverse velocity relative to the flow, even in a dense medium. With no drag the
position and velocity follow the constant-gravity solution.

This is a configurable game model with constant coefficients. It does not model
spin, changing orientation, Mach-dependent coefficients, cavitation, buoyancy,
penetration or ricochet. Demo presets illustrate behavior rather than calibrating
a particular real firearm or ammunition. With gravity and drag combined, the
solution is numerical; smaller integration limits improve accuracy.

## World adapter

Implement `ProjectileWorld::trace(segment, current_medium)` in the game. Return the
nearest solid impact or medium boundary anywhere on the queried segment, or
`std::nullopt`. Query geometry, collision filters, ignored owners and moving-world
history belong to this adapter. A host can map its world clock using launch time
plus `segment.start_age_seconds`.

- `fraction` is a spatial fraction along `from` to `to`, in `[0, 1]`.
- An impact returns the game's object ID and optional normal. The library stops
  the projectile there and emits `Impact` exactly once.
- A medium boundary returns the medium on the far side. The library advances to
  the boundary, preserves velocity, changes medium, and uses the remaining time in
  the same call. It solves the travel time to a spatial fraction, since distance
  and time fractions differ under drag.
- A zero-fraction boundary is allowed for launches exactly on a surface. Use
  `current_medium` and travel direction to return the appropriate crossing once;
  do not repeatedly return the same boundary after the medium has changed.
- An unchanged medium is rejected. More than eight consecutive boundaries without
  clock progress report a faulty adapter instead of hanging.
- Collision checks use bounded straight chords approximating the curved path.
  Defaults are at most 1/480 second and 0.25 m per chord. Adapt these limits to the
  precision required by the game's geometry. This is a point projectile; sweep a
  shape in the adapter if a projectile should have radius.

The initial medium is explicit at construction (default air). Determine it from
the launch point in the game, especially when firing underwater. Any later medium
change must be returned by the adapter. Media are homogeneous between crossings;
continuous spatial/time-varying flow fields are outside this initial adapter.

## Minimal use

```cmake
target_link_libraries(your_game PRIVATE
    weapon_library::weapon_library weapon_library::ballistics)
```

```cpp
#include <weapon_library/Ballistics.h>
using namespace weapon_library::ballistics;

struct EmptyWorld : ProjectileWorld {
    std::optional<SegmentInteraction> trace(const FlightSegment&, const Medium&) const override {
        return std::nullopt;
    }
};

EmptyWorld world;
ProjectileDefinition definition;
Projectile bullet(definition, {0, 1.65, 0}, {0, 0, -320}, Medium::air());
auto events = bullet.advance(1.0 / 120.0, world);
auto snapshot = bullet.state();
// Render snapshot.position. Apply damage only when events report Impact.
```

The game owns projectile instances, launch timestamps and object identity.
`ProjectileState` exposes position, velocity, medium, age, travelled chord distance
and status. `advance()` returns only medium-change and terminal events; movement
is read from snapshots. Impacted/expired instances remain stationary and never
repeat their terminal event. Remove them from the game's active list.

All scalar/vector inputs must be finite. Mass, lifetime and integration limits
are positive; area, drag coefficient and density may be zero. Invalid time is
rejected before mutation. Invalid world responses throw; earlier substeps in that
same update may already have completed. Numerical overflow also reports an error.
Equivalent time partitions can differ by integration and floating-point error;
bitwise cross-platform determinism is not promised.

## Firing range integration

`RangeSimulation` creates a projectile for each `ShotFiredEvent`, uses a segment
adapter for the room, circular targets and water volume, and increments hits only
on impact. Within each simulation tick it advances existing projectiles to the
next firing event before spawning that shot. A shot at the end of a tick therefore
has zero flight age rather than receiving an entire tick of movement early.

The three presets have different muzzle speeds, masses and cross-sections. The
right-hand lane contains a six-metre water region with no solid glass walls. The
HUD shows muzzle speed, active projectile count, last impact flight time and speed.
The result summary records the actual surface and whether the projectile passed
through water. Floor, ceiling and wall impacts remain misses; only target impacts
increment target counters. Impact markers persist for two simulation seconds.
In the six-metre water lane, horizontal center aim with the starting pistol/rifle
presets ends at the floor; the turret preset reaches target 03. This is caused by
deceleration and drop, not a disabled water hit counter.
`V` toggles 0.1x simulation speed; slow motion uses finer steps to keep flying dots
smooth. Gravity, firing cadence and all projectile clocks slow together.

The simulation subdivides long updates, advances the shooter to each spawn time
and adds shooter velocity to the launch. Aim is constant during one input update;
the scene has static targets and does not implement moving-target rewind.
