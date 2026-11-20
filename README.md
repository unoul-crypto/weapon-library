# Weapon Library v0.15.0

A small C++17 foundation for reusable weapon mechanics. The core has no external
dependencies, graphics, physics engine, platform calls or wall-clock access.

## Current scope

- Shared immutable weapon definitions and independent runtime instances.
- Multiple named-by-ID actions per weapon.
- Windup, active and recovery phases, followed by readiness.
- Explicit start/cancel commands with rejection reasons.
- Configurable cancellation permission for each phase.
- Ordered transition events stamped with simulation time.
- Read-only state snapshots and headless behavioral tests.
- A firing mechanism with semi-automatic and automatic trigger modes.
- Cadence, magazine capacity, configurable ammo cost and dry-fire events.
- Configurable projectile counts with independent spread directions for each shot.
- One ordered stream of action transitions and shot events.
- Independent aiming, seeded spread, shot bloom and accumulating/recovering recoil.
- Optional per-instance spread models for nonlinear game-owned accuracy factors,
  shared by HUD snapshots and shot sampling.
- Interruptible reload actions, finite reserves and an atomic ammunition-source contract.
- Engine-independent weapon mounts and configurable reload keyframe poses.
- Weight-based model inertia with separate walk, turn and ADS sway multipliers.
- Optional ADS aim sway that moves the reticle and the center of the shot cone.
- Lowered weapon stance with independent raise times and a configurable pose track.
- Range adapter for custom OBJ/GLB/GLTF models and named skeletal reload clips.
- A melee mechanism with active-phase sweeps, per-attack entity deduplication,
  blocking contacts and configurable target limits through a game-owned world adapter.
- An optional interactive 3D firing range, with separate game logic and rendering.
- An independent ballistics module: finite-speed projectiles, gravity, flow-relative
  air/water resistance, swept queries and medium transitions within an update.

The action core remains usable on its own. `FiringMechanism` composes it with
trigger, cadence and ammunition rules. `weapon_library::ballistics` provides
projectile motion through a game-supplied world adapter. The game owns collision
geometry and damage; inventory-backed reloading connects through `AmmunitionSource`.
`MeleeMechanism` reuses the action phases and queries a `MeleeWorld` only during
the active swing. See [the melee contract and knife example](docs/melee.md).

## Build and test

With CMake and a C++17 toolchain:

```sh
cmake -S . -B build/native -DWEAPON_LIBRARY_BUILD_TESTS=ON -DWEAPON_LIBRARY_BUILD_EXAMPLES=ON
cmake --build build/native --config Release
ctest --test-dir build/native -C Release --output-on-failure
```

On Windows, the helper also discovers CMake bundled with Visual Studio:

```powershell
./scripts/build.ps1 -Test
```

Tests use runtime checks in all build configurations, including Release. The
headless action example prints activation/completion times. The firing example
uses the same mechanism for a pistol, a rifle and a turret.

## Play the firing range

On Windows, double-click `play-windows.cmd`, or run:

```bat
scripts\build-windows.cmd --run
```

The launcher builds the demo using CMake directly and opens its window. It does
not run a PowerShell script or change execution policies. Enter with a click or Enter.
The scene has three ranged targets, a near melee target, four weapon configurations, hit feedback, short
projectile trails, ammo counters, accuracy and scoring. Every shot comes from the
library's `ShotFiredEvent`; hits are counted when its flying projectile impacts.
The blue region in front of target 03 contains water. The HUD shows flight time,
impact speed and the number of projectiles still flying. Hold RMB to aim: zoom,
weapon alignment, tighter spread and reduced recoil transition together. The
spread ring grows while moving/firing and settles while resting. Recoil changes
the camera's aim; the model also has a separate visual kick.

The side cover block can obstruct the barrel even when the eye sees the target.
Projectiles now spawn at the muzzle, with a solid query preventing shots through
nearby cover. The model and simulation share the weapon pose.

The result panel distinguishes target, floor, ceiling, cover and wall impacts, and
expiration. Impact marks remain visible for two simulation seconds; misses also
label the actual impact point. `PATH: WATER` records whether that projectile
travelled through water, including impacts after it left the water region.

The water section is six metres thick. With the demo presets, firing horizontally
at target 03 from the starting position causes pistol and rifle rounds to drop
to the floor before the target. The heavier turret round reaches it. Select 3 to
check a registered hit through water; floor impacts are genuine misses and do not
increase the target hit counter. Use V to inspect the drop.

| Control | Action |
| --- | --- |
| Mouse | Aim |
| Right mouse | Hold to aim down sights |
| WASD | Move within the firing area |
| Left mouse / F | Attack; hold for rifle/turret, tap for pistol/knife |
| 1 / 2 / 3 | Select pistol / rifle / turret firing configuration |
| 4 | Select knife; approach the near target to the left of the starting position |
| R | Reload from this weapon's reserve |
| Q | Cancel reload or the current knife swing |
| C | Toggle the crosshair and spread ring |
| F5 | Reload weapon settings without resetting the session |
| Shift | Lower the weapon and interrupt a cancellable reload |
| Shift+W | Sprint forward with the weapon lowered |
| Home | Reset the entire practice session: ammo, reserves, pose, targets and statistics |
| V | Toggle 0.1x slow motion to inspect trajectories and medium transitions |
| Escape | Pause and release the mouse |
| Click / Enter while paused | Resume |
| Window close button | Quit |

Switching preserves each magazine and releases the old weapon's trigger. Resume,
switch and reset require a fresh trigger press, preventing accidental firing.
Focus loss pauses the simulation and releases the mouse. Reload transfers happen
only on completion; switching cancels reload and preserves loaded/reserve rounds.
The HUD shows reserve ammo and reload progress. The turret preset
tests automatic firing with a larger magazine; it uses the same handheld demo view.

Release Shift to raise the weapon to its ready position. Hold RMB while releasing
Shift to raise a firearm directly to ADS. Each weapon can specify separate
lowering, ready raise and ADS raise durations. See [lowered pose settings](docs/presentation.md#lowered-pose)
and [reload interruption](docs/reloading.md#range-integration).

Edit `examples/range/visuals/pistol.weapon`, `rifle.weapon` or `turret.weapon`
to replace the model, adjust its transform, align the muzzle and customize reload
keyframes or a skeletal clip. The rifle includes an original glTF model with an
animated magazine. The launchers read these source configs; press F5 to apply edits.
See [model and animation customization](docs/presentation.md) for the file format
and reusable library API.

Set `projectiles_per_shot = 8` in a firearm's `.weapon` file and press F5 to
try a multi-projectile shot. The range spends one round and tracks each
projectile's hit separately. See [projectile groups](docs/firing.md#projectile-groups).

The knife has a 0.16 s windup, a 0.18 s active thrust, 0.28 s recovery and 1.6 m reach.
Its directed attack follows the aim line; contact markers lie on real surfaces.
It consumes no ammo and hits each entity at most once per swing. Holding attack
does not repeat it, and pressing during a busy action does not queue a new swing.
Q or switching cancels the demo knife; phase-specific cancellation remains
configurable in the library. `knife.weapon` configures its model, attack keyframes
and optional skeletal clip. F5 preserves a swing's current progress while reloading assets.

The demo is OFF by default. Enabling it fetches pinned raylib 5.5; the library and
headless tests still build without graphics or downloads. To reuse an existing
raylib 5.5 source checkout, pass `-RaylibSource <directory>` to the helper.

With another native CMake toolchain:

```sh
cmake -S . -B build/range -DWEAPON_LIBRARY_BUILD_DEMO=ON -DWEAPON_LIBRARY_BUILD_TESTS=ON
cmake --build build/range --config Release
ctest --test-dir build/range -C Release --output-on-failure
```

Run the generated `weapon_library_range` executable. A graphics smoke check with a
real OpenGL context renders a hidden window, verifies four rifle hits and exports
a screenshot:

```powershell
./build/native/Release/weapon_library_range.exe --smoke build/qa/range.png
./build/native/Release/weapon_library_range.exe --smoke-water build/qa/water.png
./build/native/Release/weapon_library_range.exe --smoke-reload build/qa/reload.png
./build/native/Release/weapon_library_range.exe --smoke-melee build/qa/melee.png
```

`examples/range/RangeSimulation` owns movement, room/target hit queries and counters,
and links only the weapon library. `RangeRenderer` reads this state; `main.cpp`
owns input, pause and simulation scheduling (120 Hz normally, 1200 Hz in simulation
time during slow motion). Shot timestamps determine projectile spawn times within
each update. Targets are static; aim is sampled from input rather than rewound.

See [the ballistics model and adapter contract](docs/ballistics.md), including how
to link the separate module. Use `WEAPON_LIBRARY_BUILD_BALLISTICS=OFF` to build
only the action/firing/handling systems without the range.

See [the aiming, spread and recoil contract](docs/handling.md) for independent
configuration, seeded sampling and integration at each shot's timestamp.

## Embed in a game

```cmake
add_subdirectory(path/to/weapon-library)
target_link_libraries(your_game PRIVATE weapon_library::weapon_library)
```

Tests and examples default to OFF so embedding does not add executables to the game.

```cpp
#include <weapon_library/Weapon.h>

using namespace weapon_library;

constexpr ActionId primary_attack = 1;
auto definition = std::make_shared<const WeaponDefinition>(
    std::vector<ActionDefinition>{{primary_attack, 0.125, 0.125, 0.25}});
Weapon weapon(definition);

auto result = weapon.execute(WeaponCommand::start(primary_attack));
// Consume result.events, including immediate activation for zero windup.
auto events = weapon.advance(1.0 / 60.0);
// Consume events and use weapon.state() to construct the presentation snapshot.
```

See [the complete example](examples/Headless.cpp).

## Firing mechanism

```cpp
#include <weapon_library/FiringMechanism.h>

using namespace weapon_library;

constexpr ActionId fire = 1;
auto actions = std::make_shared<const WeaponDefinition>(
    std::vector<ActionDefinition>{{fire, 0.0, 0.0, 0.05}});
FiringMechanism rifle(actions, {fire, FireMode::Automatic, 0.1, 30}, 30);

auto immediate = rifle.execute(TriggerCommand::Press); // First shot at t = 0.
auto continued = rifle.advance(0.5);                  // Shots at 0.1 through 0.5.
rifle.execute(TriggerCommand::Release);

for (const auto& event : continued) {
    if (const auto* shot = std::get_if<ShotFiredEvent>(&event)) {
        // Resolve the muzzle pose at shot->time_seconds and perform the game's shot.
    }
}
```

Consume both command and update events. `ShotFiredEvent` is the authoritative
request for one shot, with its timestamp, projectile count, ammo cost and remaining rounds. The
wrapped active-phase event is a lifecycle notification, not a second shot request.

See the [firing contract](docs/firing.md) and the
[pistol, rifle and turret example](examples/Firing.cpp).

## Simulation contract

- All durations and time steps are finite nonnegative seconds. Invalid input throws
  `std::invalid_argument`; invalid time does not change the weapon state.
- Action IDs are unique within a definition. Zero is a valid ID. Empty definitions
  are allowed and reject every start command.
- One action runs at a time. There is no hidden command queue or automatic repeat.
  Unknown IDs are rejected before checking whether the weapon is busy.
  The separate firing mechanism implements repeat by starting actions at eligible
  boundaries; a raw zero-duration `Weapon` action does not enforce firing cadence.
- Commands take effect at the current weapon clock. The host chooses their order.
- Starting emits `ActionStarted` in `Windup`; entering `Active` and `Recovery`
  emits `PhaseEntered`; completion emits `ActionCompleted` in `Ready`.
  Cancellation emits `ActionCancelled` in `Ready` without a completion event.
- Zero-length phases emit events immediately, even within `execute()`. A positive
  time step emits all crossed transitions in order, with their transition times.
- Event vectors are returned by value and never replayed internally. Consume each
  vector once. Rejected commands produce no events and preserve the current action.
- Cancellation defaults to windup only. Enabling recovery cancellation intentionally
  permits an earlier return to readiness; only enable it when game rules allow it.
- The clock advances while idle as well. Pause by not calling `advance()`.
- Use a consistent simulation step. Event times use double precision; equivalent
  time partitions may differ by floating-point rounding. Bitwise determinism across
  platforms is not promised. Countdown subtraction compensates accumulated rounding
  error and treats differences within a few rounding units of the original duration
  as the same boundary; zero updates never consume positive durations.
- Instances are single-thread owned. Separate instances can share one immutable
  definition. Copy state values for presentation; do not mutate simulation in rendering.

## Integration boundaries

The game maps input and AI to commands and synchronizes the weapon clock with its
simulation. It owns inventory, entities, transforms, physics queries and damage.
For a raw action, an active-phase event can request an effect or begin a melee hit
window. For `FiringMechanism`, use `ShotFiredEvent` for the shot. For melee,
the host must also evaluate the swept path during that window; a single activation
event does not perform collision detection or deduplicate hits.

Transition timestamps are local simulation seconds, not world snapshots. When a
large step crosses a whole hit window, the integration must evaluate the matching
world interval or substep the world. Querying only the final world pose loses motion.

Rendering, animation, sound and UI consume snapshots/events. No animation callback
is needed for the core timing rules. Engine, storage-library and eventpp adapters
can be added independently without becoming mandatory core dependencies.

## Next milestones

1. Melee attack instances and per-attack target tracking.
2. Game adapters for hit queries/projectiles and storage-library, plus integration
   examples for handheld weapons and a turret using the same firing mechanism.

Keep each milestone driven by a working example. Add interfaces when a concrete
integration needs them, and keep engine-specific types outside the core.

See [reloading and ammunition sources](docs/reloading.md) for configuration,
completion-only transfers, interruption and inventory integration.
