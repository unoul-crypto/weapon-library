#pragma once

#include <cmath>
#include <stdexcept>

namespace weapon_library::math {

struct Vec3 { double x = 0.0, y = 0.0, z = 0.0; };
inline Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 v, double s) noexcept { return {v.x * s, v.y * s, v.z * s}; }
inline double dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double length(Vec3 v) noexcept { return std::hypot(v.x, v.y, v.z); }
inline Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 normalized(Vec3 v) {
    const double size = length(v);
    if (!std::isfinite(size) || size == 0.0) throw std::invalid_argument("Direction must be finite and nonzero");
    return {v.x / size, v.y / size, v.z / size};
}

} // namespace weapon_library::math
