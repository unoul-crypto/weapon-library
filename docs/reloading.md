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

Omitting the action ID preserves firing-only behavior. An unknown reload ID or
one equal to the firing action ID is rejected at construction. Common action
validation and zero-duration phase behavior still apply.

## Atomic ammo transfer

The mechanism owns a shared reference to the source until completion/cancellation.
It reserves/removes no rounds early. On ActionCompleted it requests the magazine's
available space, then adds the returned count. Existing rounds stay loaded; this
top-up model does not discard a partial magazine.

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
from the running reload. During the wait, remaining reload time is its full duration;
the current shot timer is separately available in `state().weapon`.

`cancel_reload()` cancels a queued intent immediately, or respects the running
reload action's cancellation policy. It never spends reserve rounds. Cancelling
the queued intent does not cancel the committed shot. A successful generic
`cancel_action()` also discards a reload queued behind the cancelled shot.

Holding through reload or pressing during it requires release and a fresh press
before firing again. Presses during reload are not queued shots. Cooldowns continue
and cannot be bypassed by reload/cancellation. Pause by not advancing simulation.

An accepted queued request may return no events. `ReloadEvent` adds Started,
Completed and Cancelled notifications to the ordered `FiringEvent` stream, alongside
common action events. Completed includes transferred and loaded counts. Only the
firing action's Active phase emits ShotFiredEvent; reload phases never spend ammo
as shots or add recoil. A zero-duration reload commits immediately at its start.
Large updates preserve event times and transfer once.

`ReloadStatus` reports unsupported reloads, full magazines, missing/empty sources,
duplicate requests and blocked cancellation.

## Range integration

R reloads, Q cancels, and switching cancels the old weapon's reload. Home resets
the session. Initial reserves are pistol 60, rifle 150 and turret 180; reload times
are 1.2, 1.8 and 2.4 simulation seconds. The HUD shows reserve and progress.
Reload lowers aim and moves the model; those are scene choices. Previously fired
projectiles keep flying and statistics are retained. Escape/focus loss pauses the
operation, and resume continues it without advancing paused time.
