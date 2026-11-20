#pragma once

#include <weapon_library/Math.h>

#include <cstdint>

namespace weapon_library {

enum class HitKind { Projectile, Melee };

// World-space contact. IDs and the simulation clock are owned by the game.
struct HitContact {
    HitKind kind;
    std::uint64_t object_id;
    double time_seconds;
    math::Vec3 position;
    math::Vec3 normal;
};

} // namespace weapon_library
