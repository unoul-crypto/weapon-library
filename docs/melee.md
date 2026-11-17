# Melee mechanism

`MeleeMechanism` is part of the C++17 `weapon_library` target. It composes `Weapon`
and adds active-phase collision queries and per-attack hit filtering. It does not
depend on raylib, ballistics, a physics engine, health, inventories or animation.

## Configuration and commands

```cpp
#include <weapon_library/MeleeMechanism.h>

using namespace weapon_library;
auto actions = std::make_shared<const WeaponDefinition>(
    std::vector<ActionDefinition>{{3, 0.16, 0.18, 0.28}});
MeleeMechanism knife(actions, MeleeDefinition{3, 1.6, 0.08, 0.0}); // Thrust; use a positive arc for a slash.

// world implements MeleeWorld using the game's collision engine.
auto command = knife.execute(MeleeCommand::Attack, world);
auto events = knife.advance(delta_seconds, world);
auto cancelled = knife.execute(MeleeCommand::Cancel, world);
```

The action ID must exist in the shared immutable definition. Action phase durations
and cancellation policy use the existing `Weapon` rules. Reach is positive; sweep
radius is nonnegative; arc is in [0, 2*pi] radians. All values must be finite. Zero
arc describes a thrust along the attacker's forward direction. The game maps the
horizontal arc from -arc/2 to +arc/2 in its own attacker frame.

Each accepted Attack begins one action with a new nonzero per-instance `MeleeAttackId`, clears
the per-attack entity set, and emits the common action transitions. Busy commands
are rejected without buffering or resetting the current attack. The core has no
held-button or auto-repeat rule: the game sends Attack for each desired swing.
Cancel reports the common `CommandStatus` and follows the current phase's policy.
The default policy permits cancellation during windup only; the range explicitly
permits it in all knife phases, including when switching weapons.

## World adapter

Implement `MeleeWorld::sweep(const MeleeSweep&) const`:

- Supply attacker transforms/geometry at the query's simulation timestamps.
- Sweep the blade/capsule/volume over `from_progress` to `to_progress`, using the
  supplied reach, radius and arc. This is active-phase progress, distinct from the
  whole-action animation progress.
- Return `MeleeContact` values: a stable entity ID, a time fraction in [0, 1],
  position, normal and whether the contact blocks the rest of the swing.
- Include cover and walls. Return unoccluded contacts; order simultaneous contacts
  near-to-far along the blade. Collision geometry and line-of-sight filtering
  belong to the game. The core cannot infer spatial occlusion from entity IDs.
- Use one ID for all colliders of the same entity. Nonblocking contacts can hit
  multiple entities; blocking contacts stop further queries for this attack.

The mechanism orders contacts by time (stable for ties), emits one `MeleeHitEvent`
per unique entity per attack, and stops after a blocker or `max_targets`. Zero
`max_targets` is unlimited. A repeated blocking contact still blocks. Hits carry
the action ID, attack ID, target ID, position, normal, blocking flag and exact
reported simulation time. Only consume events returned by each call once.

`MeleeState` exposes the common weapon state, attack ID, hit count, blocking state
and whole-action `attack_progress`. The last attack's hit count/ID remain available
while ready; the animation progress becomes zero. Another attack clears the set.
The game decides which contact IDs are damageable and how damage, armor, effects
or hit reactions work. A wall contact is not a character hit merely because it
emits an event.

World callbacks must not reenter/mutate the mechanism. They must return finite,
valid contacts and should not throw. The core rejects invalid contacts, but does
not roll back action time or external world changes after a failing adapter.
Use the query timestamps, not a snapshot of the mechanism inside the callback:
the underlying action may already have advanced past several phases.

## Timing

No queries occur during windup, recovery or readiness. Entering a nonzero active
phase performs an instantaneous query at progress zero, then subsequent updates
sweep every elapsed active interval. This catches contact at the start as well as
targets encountered later in the swing.

Large updates clip collision queries to the active interval and emit contacts
between the corresponding phase events, even if the final state is already ready.
The game must sweep the whole requested interval, rather than only checking its
end pose. Pause by not advancing; scale simulation time for slow motion.

A zero-duration active phase queries exactly once at its central pose (progress
0.5) at the transition timestamp. This also applies when windup/recovery are zero
and the entire action completes inside the Attack command. Negative/non-finite
time and clock overflow follow the common action validation.

## Range example

Press 4 to select the knife. The small near target is left of the starting point;
move close with WASD, aim at it and tap LMB or F. Each press starts one swing. The
HUD shows windup, swing and recovery, with separate swing/hit counters. R has no
effect on a knife; Q cancels, Escape pauses, Home resets. Switching cancels the
knife without generating hidden hits. Firearm magazines/reserves are unaffected.
The near target also accepts bullets through the existing collision adapter.

The knife preset is a directed thrust (zero arc), so aiming at a target's center
produces a centered contact. Other weapons can still configure sweeping arcs.
Its visual animation follows that thrust, including windup and recovery.

The demonstration adapter sweeps a blade capsule sampled at at most 0.01 radians
and 0.01 m of movement. Expanded bounds only reject collision candidates; a convex
distance query finds contact on the actual target disk or cover box. Wall contacts
also use the real plane. Marker positions are surface contacts, not blade-center
positions on inflated colliders. Off-center and grazing strikes retain their
actual contact positions instead of being moved to the crosshair.
The adapter clips against room solids and uses timestamped attacker movement. This is a simple static-world demo,
not a replacement for a production engine's continuous collision/pose history.

Edit `examples/range/visuals/knife.weapon` to change the mesh or attack animation,
then press F5. The same `WeaponPresentation` track sampler used for reload motion
evaluates attack keyframes. Models and optional skeletal clips stay in the renderer;
the core drives their progress. See [presentation customization](presentation.md).

Headless tests cover phase timing, catch-up/partitioning, zero phases, duplicate
colliders, cancellation, blockers, target limits, reach, movement, switching and
firearm ammunition isolation. The hidden graphics smoke is:

```sh
weapon_library_range --smoke-melee build/qa/melee.png
```
