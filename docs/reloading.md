# Reloading and ammunition sources

`FiringMechanism` supports optional interruptible top-up reloads using the common
`Weapon` action runtime. There is no inventory, renderer or platform dependency.

## Configuration

Select a distinct existing action through `FiringDefinition::reload_action_id`.
Its phase durations determine reload time, and its phase cancellation policy
controls interruption. A simple magazine reload can put all time in windup.

```cpp
#include <weapon_library/FiringMechanism.h>
using namespace weapon_library;
auto actions = std::make_shared<const WeaponDefinition>(std::vector<ActionDefinition>{
    {1, 0.0, 0.0, 0.045},
    {2, 1.5, 0.0, 0.0, {true, true, true}}
});
FiringMechanism pistol(actions, {1, FireMode::SemiAutomatic, 0.2, 12, 1, 2}, 6);
auto reserve = std::make_shared<AmmunitionReserve>(24);
auto started = pistol.request_reload(reserve);
auto events = pistol.advance(1.5);
// Magazine: 12. Reserve: 18. Consume both returned event vectors once.
```

Omitting both reload action IDs preserves firing-only behavior. An unknown reload ID or
one equal to the firing action ID is rejected at construction. Common action
validation and zero-duration phase behavior still apply.

## Atomic ammo transfer

The mechanism owns a shared reference to the source until completion or cancellation.
Each transfer occurs at an action completion. It requests the magazine's available
space in Magazine mode or one round for each insertion in PerRound mode. Existing
rounds stay loaded.

Implement `AmmunitionSource` for the game's inventory:

- `available_rounds()` is a non-throwing query for early rejection.
- `take_up_to(requested)` atomically removes and returns 0..requested rounds. On
  failure, return zero without removing anything. Do not throw or re-enter the weapon.

The weapon has room for the entire request and cannot fire during reload. A source
depleted by another consumer produces a partial or zero-round completion instead
of duplication. Returning more than requested violates the contract and raises a
logic error; the library cannot repair an adapter that removed the wrong items.

`AmmunitionReserve` is an optional bounded counter implementation. Multiple weapons
can share it in one simulation thread. Threaded inventory adapters provide their
own atomic transfer. A storage-library adapter can preserve item IDs and metadata
without making that library a mandatory dependency here.

## Timing and cancellation

A request during firing waits for the committed shot and recovery to finish, then
starts reload before automatic repetition. The shot still spends its normal cost.
`reload.active` includes this pending intent; `waiting_for_action` distinguishes it
from the running reload. During the wait, the reload timer reports the full
duration of the first reload action. The current shot timer is available in
`state().weapon`.

`cancel_reload()` cancels a queued intent immediately, or respects the running
reload action's cancellation policy. It keeps earlier transfers and clears the pending
insertion or magazine fill. The committed shot can still finish after cancelling
the queued reload. A successful generic
`cancel_action()` also discards a reload queued behind the cancelled shot.

Holding through reload or pressing during it requires release and a fresh press
before firing again. Presses during reload are not queued shots. Cooldowns continue
and cannot be bypassed by reload/cancellation. Pause by not advancing simulation.

An accepted queued request may return no events. `ReloadEvent` adds `Started`,
`RoundInserted`, `Completed` and `Cancelled` notifications to the ordered
`FiringEvent` stream. `rounds_transferred` is the amount moved by that event.
The firing action emits `ShotFiredEvent` on entry to its Active phase.
A zero-duration reload commits immediately at its start.
Large updates preserve event times and commit each transfer once.

`ReloadStatus` reports unsupported reloads, full magazines, missing/empty sources,
duplicate requests and blocked cancellation.

## Per-round reloads

Set `FiringDefinition::reload_mode` to `ReloadMode::PerRound`. The reload action
runs once per insertion. Configure `round_reload_action_id` to use a separate
action with its own timing and cancellation policy. When omitted, each
insertion uses `reload_action_id`. Use `set_reload_mode` to change the mode for
the next accepted reload. The current request keeps its mode and action.

Each completed insertion asks the source for one round. Successful transfers
emit `RoundInserted` with `rounds_transferred = 1`. The reload stops at magazine
capacity, when the reserve empties, or when an insertion transfers zero rounds.
One `Started` event covers the whole session. In PerRound mode `Completed` has
`rounds_transferred = 0` because the preceding insertion events already report
the transfers. `ReloadState` reports the current insertion's timing and the
number of rounds inserted so far. An instant insertion follows the same event
order, including when a whole reload completes at one timestamp.

## Range integration

R reloads and Q cancels. Shift attempts to cancel the reload before lowering
the weapon. Switching cancels the old weapon's reload. Successful cancellation
preserves the magazine and reserve counts. Home resets the session.
The pistol, rifle and turret use magazine reloads by default. Their reload
times are 1.2, 1.8 and 2.4 simulation seconds, respectively. The HUD shows
the reserve and reload progress.
The shotgun starts with six shells loaded, 30 in reserve, and per-round
reloading enabled. Select it with 5. Fire three shots, press V and start
reloading with R. Cancel with Q after the magazine reaches 5 of 6.
The magazine should stay at five and the reserve should stay at 28.

Set `reload_mode = per_round` in another firearm's `.weapon` file and press F5.
Each insertion in the range takes 0.45 simulation seconds. The HUD shows
the current insertion and the reload animation repeats for each round.
Use `reload_mode = magazine` for a magazine reload.
Reload lowers aim and moves the model; those are scene choices. Previously fired
projectiles keep flying and statistics are retained. Escape/focus loss pauses the
operation, and resume continues it without advancing paused time.

Weapon models, reload keyframes and skeletal clips are configurable independently
of these mechanics. See [presentation customization](presentation.md).
