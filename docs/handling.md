# Aiming, spread and recoil

`WeaponHandling` is part of `weapon_library::weapon_library`. It has no camera,
physics, renderer, input or ballistics dependency. Its configuration is separate
from instance state and can be reused by handheld weapons, AI and mounted weapons.
All angles are radians, and time is simulation seconds.

## Configuration and state

`HandlingDefinition` configures aim-in/out durations, hip and aimed cone half-angles,
movement penalties, bloom per shot, bloom cap/recovery, recoil impulse/jitter,
aimed recoil scaling, recoil caps and an exponential recoil return rate.

The game sends `set_aiming(bool)` and a normalized movement factor in `[0, 1]`.
The library does not interpret speed, character pose or controller buttons.
Aim fraction moves linearly between 0 and 1; interrupted transitions continue from
the current fraction. Zero-duration transitions settle immediately on the command.

Spread combines the interpolated base cone, current bloom and movement penalty.
Bloom increases on actual shots and recovers linearly with elapsed time. Recoil
offsets recover exponentially, including while the weapon is holstered if the game
continues advancing it. Both accumulated effects have explicit caps.

`state()` returns a value snapshot for the camera, model, crosshair and HUD. The
camera's zoom and model position remain presentation choices. `recoiled_direction`
returns the center direction including recoil, without spread. It does not change
state or consume random numbers.

## Sampling an actual shot

Call `sample_shot(base_direction, up_hint)` exactly once per `ShotFiredEvent`, at
that event's simulation time. Pass the raw player/AI aim, not a direction already
modified by `recoiled_direction`. The function:

1. Applies the existing recoil to the aim's local basis.
2. Samples a direction uniformly over the current cone's solid angle.
3. Returns its direction, time, cone angle and recoil before/after the shot.
4. Adds recoil and bloom for subsequent shots.

The shot does not include its own new recoil impulse. Dry fire, rejected actions
and cancellation before firing must not call the sampler. If a logical shot
creates multiple pellets, use this result as the cluster's axis and apply a
separate pellet pattern; repeated sampler calls would add repeated impulses.

The finite, nonzero up hint defines local yaw and pitch. A parallel hint receives
a stable fallback axis, including for vertical aim. Direction inputs are validated
before mutation or random sampling.

The constructor accepts a 32-bit seed. A fixed xorshift32 sequence supplies three
24-bit uniform values per shot; seed zero maps to `0x6d2b79f5`. Aim commands,
snapshots, rendering and time updates do not consume random numbers. The same seed,
shot times and input history reproduce the shot sequence within numerical error.
Trigonometric functions and floating-point arithmetic are not promised bitwise
identical across all platforms.

## Timing with the firing mechanism

Firing and handling remain independent components. Advance handling chronologically
to each shot event before sampling; do not evaluate an entire automatic burst at
the end-of-frame state. For example, in a host that owns both clocks:

```cpp
#include <weapon_library/FiringMechanism.h>
#include <weapon_library/WeaponHandling.h>

void consume_shots(weapon_library::WeaponHandling& handling,
                   const std::vector<weapon_library::FiringEvent>& events) {
    for (const auto& event : events) {
        if (const auto* shot = std::get_if<weapon_library::ShotFiredEvent>(&event)) {
            const double elapsed = shot->time_seconds - handling.state().time_seconds;
            handling.advance(elapsed); // Host clocks must already be synchronized.
            const auto result = handling.sample_shot({0, 0, -1});
            // Game resolves muzzle pose and spawns a projectile using result.direction.
        }
    }
}
```

After consuming events, advance handling through any remaining interval. Process
aim/movement commands at their actual times as well. Pause by not advancing the
clock. Invalid/non-finite time and clock overflow are rejected before mutation.

## Scene integration

Hold RMB to aim. The scene narrows the FOV from 65 to 45 degrees, lowers mouse
sensitivity, moves the model toward the sight line and uses the library's reduced
cone/recoil. The spread ring represents the current cone projected through that
FOV; movement and repeated shots enlarge it. Camera direction follows the physical
recoil center. Model kick is a separate cosmetic translation.

Each weapon retains its own seeded handling state. Switching releases the previous
trigger and its aim request, while recoil and bloom keep recovering. Home resets
the session, including random sequences. R reloads and Q cancels reload; no reload
phase samples a shot or adds recoil/bloom. Pause/focus loss release firing and aiming.

The game queries the sampled view ray for a convergence point, then launches from
the muzzle toward it. A separate eye-to-muzzle solid query keeps a barrel inside
nearby cover from spawning a projectile through it. Fluid boundaries do not block
this sight query; the ballistics adapter handles those during flight. The side
cover block demonstrates a visible target with an obstructed hip-fire barrel;
raising the barrel by aiming can clear it. Its collider and renderer share bounds.

This convergence establishes the initial sight line, not automatic compensation
for gravity or water resistance. The projectile still follows its ballistic path.
