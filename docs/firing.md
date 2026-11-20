# Firing mechanism

`FiringMechanism` owns a `Weapon` runtime and immutable firing settings. A pistol,
rifle and turret differ in configuration and the source of their input commands;
they use the same firing code. No entity, muzzle, inventory or renderer types enter
this module.

## Configuration and state

`FiringDefinition` selects an existing action ID, trigger mode, minimum firing
interval, magazine capacity and rounds consumed per shot. All phase durations
come from the shared `WeaponDefinition`.

The interval is a finite positive number of seconds between accepted firing action
starts. For example, 600 rounds/minute with one round per shot is `60.0 / 600.0`
seconds. An action must also complete before the next firing action can start.
Consequently, with fixed phases and uninterrupted automatic fire, the firing cycle
is the longer of the interval and the total action duration.

Construct with the initially loaded round count, which defaults to zero. Capacity
and shot cost must be positive, cost must fit capacity, and initial rounds cannot
exceed capacity. The configured action and mode must be valid. Invalid definitions
throw `std::invalid_argument`.

Each mechanism has its own trigger state, ammo, clock, action state and cooldown.
Weapon definitions can be shared. `state()` returns a value snapshot. The
mechanism owns its action runtime and enforces cadence and ammo accounting.

## Trigger input

Send `TriggerCommand::Press` when the logical trigger changes from released to
pressed, and `Release` when it changes back. Repeated Press while held does
nothing. Input can come from a mouse, gamepad, AI or turret controller.

- **SemiAutomatic:** a fresh press makes one attempt at the current simulation
  time. Holding does not repeat. A press while busy or cooling down is not queued;
  release and press again when ready. Release/repress cannot bypass the cooldown.
- **Automatic:** a fresh press tries immediately. While held, `advance()` attempts
  subsequent actions as soon as both the previous action and cooldown are done.
  Holding through a blocked press therefore resumes fire when eligible.

Release prevents subsequent automatic actions. It does not cancel an action that
has already started: a pending windup can still produce its committed shot.
The host should send Release when disabling the controlling input, switching
equipment or leaving a turret. Pause the simulation by not advancing time.

## Shots, ammo and cancellation

An action starts only if enough rounds are available for its full shot cost.
Rounds are consumed exactly once, when the action enters Active. A positive windup
does not consume rounds early; a zero windup can fire inside the Press command.

`cancel_action()` uses the action's existing phase cancellation policy. An accepted
cancellation releases the trigger, emits the cancellation notification and keeps
the original firing cooldown. Cancelling before activation saves the rounds;
cancelling after activation never refunds a shot. A blocked or idle cancellation
preserves trigger and ammo state. Use Release to stop a held trigger regardless
of cancellation permissions.

When ready to attempt a shot with insufficient ammo, emit one `DryFireEvent` per
trigger press. Holding an empty automatic trigger does not spam events. Dry fire
does not start an action, spend ammo or start a cooldown. If a magazine has fewer
rounds than the configured shot cost, the unusable remainder is preserved.

Optional reload actions now top up loaded ammo from an `AmmunitionSource`, with
transfer only at completion. See [the reload contract](reloading.md). There is no
mutable loaded-ammo setter or implicit infinite reserve.

## Projectile groups

Set `FiringDefinition::projectiles_per_shot` to a positive count. It defaults
to one. For example, configure `projectiles_per_shot = 8` and
`rounds_per_shot = 1` for an eight-pellet shot that costs one round. Each shot
emits one `ShotFiredEvent`, carrying the count in `projectile_count`. The
fire mode, cooldown and reload still operate per shot.

`FiringMechanism::set_projectiles_per_shot` changes the count used by the next
emitted shot. Use `projectiles_per_shot()` to read the current count. The
initial setting is available through `definition()`. Already emitted events
retain their count.

The range accepts an integer from 1 to 64 in each firearm's `.weapon` file.
Press F5 to apply the value. The range launches the group from the same
muzzle, samples each direction independently and tracks hits per projectile.
Its accuracy is hits divided by projectiles launched.

## Events and integration

Commands and updates return an ordered `std::vector<FiringEvent>`. Each item is
one of:

| Event | Purpose |
| --- | --- |
| `WeaponEvent` | Existing action lifecycle transitions |
| `ShotFiredEvent` | One shot request with action ID, time, projectile count, cost and remaining rounds |
| `DryFireEvent` | One eligible attempt with insufficient rounds |
| `ReloadEvent` | Reload start, completion/transfer, or cancellation |

At activation, `PhaseEntered(Active)` precedes `ShotFiredEvent`. Recovery and
completion follow. Even completely instant actions preserve this order. Use
`ShotFiredEvent` to spawn the shot; consuming the active-phase event as another
shot would double-fire. The host can spawn every projectile using the event's count.

`advance()` processes every crossed phase and eligible automatic shot, including
a shot exactly at the end of the update. Calling `advance(0)` does not repeat that
shot. All events keep their transition times instead of using the end-of-frame
time. Idle time creates no firing backlog. Negative/non-finite steps and clock
overflow are rejected before advancing any part of the mechanism.

The event stream contains every shot rather than a capped catch-up count, so its
size scales with shots fired. Use the normal fixed simulation step in a game.
Round counts are finite and stop firing on exhaustion.

Physics and projectile integration must resolve the world and muzzle at the event
time, or advance the world in matching substeps. Replaying a long update's shots
against only its final world pose cannot reconstruct intervening movement. The
library emits logical requests; the game owns aim, recoil presentation, hit
queries and damage application. For finite-speed projectiles, the game can use
the independent [ballistics module](ballistics.md). The firing range now does so.
For aim transitions, seeded spread and recoil, combine firing events with
[WeaponHandling](handling.md), sampling once at each actual shot time.

See [the runnable example](../examples/Firing.cpp).
