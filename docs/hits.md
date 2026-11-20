# Shared hit contacts

`weapon_library::HitContact` in `<weapon_library/HitContact.h>` carries the kind
of hit, the hit object's ID, its world-space point and normal, and the simulation
timestamp. The host assigns object IDs. Bullets and pellets each produce one
contact per projectile impact. Melee uses the same type for each reported hit.

```cpp
void apply_hit(const weapon_library::HitContact& contact);

// ProjectileEvent stores age since launch. Use the host's launch timestamp.
if (const auto contact = projectile_event.hit_contact(launch_time_seconds)) {
    apply_hit(*contact);
}

// MeleeHitEvent already carries its simulation timestamp.
apply_hit(melee_event.hit_contact());
```

`ProjectileEvent::hit_contact` returns an empty optional for expiry and
medium changes. The original typed events retain their source-specific data
such as projectile velocity and melee attack ID. Games can use those values
alongside the shared contact to implement their own damage rules and effects.

The range sends both types of contact through one handler for targets, cover
and room surfaces. Its scoring rules distinguish melee and projectile hits.
