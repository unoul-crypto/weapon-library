# Weapon models and animation

The library provides `WeaponPresentation` (in the normal `weapon_library` target):
validated, immutable mount offsets, reload and attack keyframes with no graphics dependency.
The game owns model files, textures, skeletons and rendering. The range demonstrates
this boundary through `RangeVisuals` (file parsing) and `RangeRenderer` (raylib assets).
Another engine can consume the same presentation poses and use its own asset loader.

## Edit the range

Launch using `play-windows.cmd`, `scripts/build-windows.cmd --run`, or the PowerShell
helper. Edit one of the files in `examples/range/visuals`:

- `pistol.weapon`
- `rifle.weapon`
- `turret.weapon`
- `knife.weapon`

Press F5 in the range to load changes, including during a reload. Ammo, target hits,
reload/attack timing and simulation time are preserved. All four configs and their assets
must load successfully before replacement. Errors appear in the HUD and console;
failed updates retain the previous working setup. At startup, a configuration error
leaves the built-in shapes available.

When running the executable directly, it reads `visuals` beside the executable.
The build copies bundled files there when building the range. To use your own directory:

```sh
weapon_library_range --visuals path/to/visuals
```

Copy all four configs to that directory. Model and animation paths are relative
to their respective `.weapon` file, independent of the process working directory.
Paths may contain spaces; do not quote values inside the config. Blank lines and
whole-line `#` comments are allowed. Unknown keys, duplicate settings (except
`reload_key` and `attack_key`), invalid numbers and malformed tracks are errors.

## Model and muzzle

```ini
model = models/my-rifle.glb
model_scale = 0.01 0.01 0.01
model_offset = 0 0 0
model_rotation = 0 180 0
hip_offset = 0.24 -0.22 0.58
aimed_offset = 0 -0.065 0.5
muzzle_offset = 0 0 0.4
```

`model` supports OBJ, GLB and GLTF through raylib 5.5. Keep external materials,
textures and buffers beside the model at the relative paths it references. A blank
`model =` selects the built-in shape. Scale components must be positive.

Weapon local space is +X right, +Y up, +Z forward, with distances in metres.
Mount offsets are relative to the eye, blended by the library's ADS fraction.
`model_scale`, `model_rotation` and `model_offset` convert asset coordinates into
this weapon space: scale first, rotate X then Y then Z (degrees), then translate.
They affect only the imported mesh. The old ADS width distortion is removed.

`muzzle_offset` is in weapon space **after** that asset conversion; it is not
automatically scaled/rotated with the mesh. Set it to the resulting barrel tip.
The scene uses this same offset for projectile spawning and the muzzle flash;
its existing eye-to-muzzle obstruction check still prevents shooting through cover.
Cosmetic recoil kick and reload poses do not change the collision socket.

## Keyframe animation

```ini
reload_interpolation = smooth
# progress / offset XYZ in metres / rotation XYZ in degrees
reload_key = 0    0 0 0          0 0 0
reload_key = 0.2  -0.04 0.12 0.04  5 -12 28
reload_key = 0.7  -0.04 0.12 0.04  5 -12 28
reload_key = 1    0 0 0          0 0 0
```

The first explicit `reload_key` replaces the default track. Keys must have unique,
strictly increasing progress values from 0 to 1, including both endpoints. Smooth
interpolation uses smoothstep per segment; `linear` is also supported. Equal poses
at different progress values hold a pose. Rotations interpolate component-wise,
allowing deliberate full turns; this is not quaternion shortest-path interpolation.
Use identity first/last poses for a continuous transition to the ready pose.

Keyframes animate the whole weapon and work with both built-in and imported models.
For skeletal-only animation, set `reload_motion = none` and omit `reload_key` lines.

## Skeletal reload clips

```ini
model = models/my-rifle.glb
reload_clip = Reload
# Optional: a different GLB/GLTF file with exactly the same skeleton.
animation = animations/rifle.glb
```

Omit `animation` to load clips from the model file. `reload_clip` must match the
imported name exactly (raylib exposes names up to 31 characters). The adapter checks
bone count, hierarchy and names. This path supports skeletal transform animation,
not morph targets, vertex caches or material animation. Use a skinned mesh and
export moving weapon parts as bones. OBJ supports keyframe motion of the whole
model, but has no skeleton.

The entire chosen clip is mapped to normalized reload progress, so its export
duration does not set gameplay reload duration. Frame zero is also the ready pose:
export the clip with the weapon assembled at frame zero and at the end. The clip
and whole-weapon keyframes may be combined. The bundled `rifle.gltf` demonstrates
a magazine bone; `scripts/generate-range-model.py` can regenerate this original
asset with the Python standard library. Python is not required to build or play.

## Integrate in another game

For the knife, `attack_key` uses the same seven-number format and validation as
`reload_key`. Its progress covers the **entire** action (windup, active, recovery).
`attack_interpolation` selects `smooth` or `linear` independently of the reload track.
Use `attack_motion = none` to disable whole-weapon motion. `attack_clip = Slash`
selects a skeletal clip from the model or shared `animation` file; it is mapped to
`MeleeState::attack_progress` and restores frame zero when ready or cancelled.
The clip must include its own windup and recovery. These visual settings do not
change collision timing, reach or arc. See [melee integration](melee.md).

```cpp
#include <weapon_library/WeaponPresentation.h>

weapon_library::PresentationDefinition visual;
visual.muzzle_offset = {0, 0, 0.22};
const weapon_library::WeaponPresentation presentation(visual);

const auto offset = presentation.mount_offset(handling.state().aim_fraction);
const auto reload_pose = presentation.reload_pose(firing.state().reload);
const auto progress = presentation.reload_progress(firing.state().reload);
// Apply offset/pose through your engine; sample a skeletal clip using progress.
```

The presentation has no clock or runtime advancement. It samples the authoritative
`ReloadState`: queued reloads retain the ready pose; pause and slow motion follow
simulation timing; cancellation/completion restore the ready pose. Zero-duration
active reloads sample progress 1 without division by zero. Large updates simply
sample their resulting state and cannot transfer ammunition a second time.
The core validates finite offsets, rotations and progress; sampling clamps finite
progress/ADS values to [0, 1]. An empty keyframe vector disables whole-weapon motion.

The renderer owns and releases models, animations and textures while the graphics
context exists. Loading, file formats and graphics resources stay outside the core.
