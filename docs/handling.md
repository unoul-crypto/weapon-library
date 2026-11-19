# Aiming, spread and recoil

`WeaponHandling` is part of `weapon_library::weapon_library`. It has no camera,
physics, renderer, input or ballistics dependency. Its configuration is separate
from instance state and can be reused by handheld weapons, AI and mounted weapons.
All angles are radians, and time is simulation seconds.

## Configuration and state

`HandlingDefinition` configures aim-in/out durations, hip and aimed cone half-angles,
movement penalties, bloom per shot, bloom cap/recovery, recoil impulse/jitter,
aimed recoil scaling, recoil caps and an exponential recoil return rate.

ADS spread is configurable, including values larger than the hip cone. For example:

```cpp
weapon_library::HandlingDefinition settings;
settings.aimed_spread_rad = 0.01; // Half-angle in radians (about 0.57 degrees).
weapon_library::WeaponHandling handling(settings);
```

This only changes the base aimed cone; movement and current bloom still contribute.
Set their respective settings to zero if they are not wanted. The default aimed
half-angle is 0.002 radians; the range keeps its existing weapon presets.

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

## ADS aim sway

Set `HandlingDefinition::aimed_sway_amplitude_rad` to the desired peak yaw and
pitch angles for fully aimed shots. Each amplitude is nonnegative and smaller
than pi/2 radians. Both default to zero. `aimed_sway_frequency_hz` controls yaw
cycles per simulation second. Pitch runs at 1.5 times that frequency. A zero
frequency disables the effect. The smooth oscillation is scaled by aim fraction
and continues while the character stands still. The current angular offset is
available as `HandlingState::aim_sway`.

Use `shot_center_direction` to place the reticle. The shot sampler applies the same
aim offset before sampling the spread cone. In the range, the camera uses
`recoiled_direction` and the reticle moves within that view. The reticle marks
the center of the launch cone before spread, flight and collisions. The visual
model sway multipliers in the `.weapon` file apply to the held model.

Set the following keys in a weapon's `.weapon` file in `examples/range/visuals`
and press F5 in the range to apply them. This example uses the pistol preset.

```ini
aimed_sway_amplitude_rad = 0.0045 0.003
aimed_sway_frequency_hz = 0.55
```

The amplitude values give horizontal yaw and vertical pitch in that order. Set
both to zero to disable aim sway. The knife preset has no ADS. Press C to hide
or show the crosshair and its spread ring. Gameplay uses the same shot center
regardless of crosshair visibility.

The library also supports live updates through
`WeaponHandling::set_aim_sway(AimSwaySettings{amplitude, frequency})`. It
recalculates the offset at the current simulation time and preserves handling
state, including recoil, bloom and the random sequence.

## Custom spread logic

`SpreadModel` is an optional `std::function<double(const SpreadContext&)>`. Supply
it as the third constructor argument or replace it using `set_spread_model`.
It may combine the built-in result with game-specific factors or calculate the
entire cone itself. No fixed list of character/environment factors enters the core:
the game owns those and captures them in its callable.

```cpp
struct AccuracyFactors {
    double stamina = 1.0; // Game maintains values in its chosen units/ranges.
    double heat = 0.0;
    bool underwater = false;
} factors;

weapon_library::WeaponHandling handling({}, 42,
    [&factors](const weapon_library::SpreadContext& context) {
        const double fatigue = 1.0 - factors.stamina;
        return context.default_half_angle_rad + 0.03 * fatigue * fatigue
            + 0.01 * factors.heat + (factors.underwater ? 0.02 : 0.0);
    });
// Later, game commands update factors without reconstructing the handling state.
factors.stamina = 0.5;
const double displayed_cone = handling.state().spread_half_angle_rad;
// handling.set_spread_model({}); // Restore the built-in formula without resetting state/RNG.
```

The context includes the weapon's definition, a pre-shot handling snapshot
(simulation time, aim fraction, movement, bloom and recoil), and the built-in cone.
The snapshot's `spread_half_angle_rad` also holds that built-in cone. Context
references are valid only during the call; captured objects must outlive their use.
Models are per instance and may share game-owned read-only data as needed.

Return a **finite half-angle in [0, pi/2)**, in radians. The core validates the
model at installation and each evaluation. An invalid replacement preserves the
previous model; invalid shot-time output or a throwing callback fails before RNG,
recoil or bloom mutations. `state()` and `sample_shot()` can therefore throw for a
failing model. Definition values retain their original validation even when a
custom model replaces the formula.

Use a pure evaluator: do not advance game state, consume random values, reenter
the handling instance or retain context references. The same model is evaluated
by both the HUD snapshot and the physical shot sampler, so queries must have no
side effects. Time updates do not evaluate it. The direction distribution remains
the library's seeded uniform solid-angle cone, with the same three RNG values per
shot. Current-shot sampling uses pre-shot bloom/recoil; their impulses follow it.

For replay or catch-up, supply external factors at **each shot's simulation time**
rather than the end of a rendered frame. Context-based curves can use
`context.handling.time_seconds` directly; historical character/world state belongs
to the host. With identical factors, shot times and seed, custom models preserve
the existing frame-independent sampling behavior.

## Sampling an actual shot

Call `sample_shot(base_direction, up_hint)` exactly once per `ShotFiredEvent`, at
that event's simulation time. Pass the raw player/AI aim, not a direction already
modified by `recoiled_direction`. The function:

1. Applies the existing recoil and ADS sway to the aim's local basis.
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
