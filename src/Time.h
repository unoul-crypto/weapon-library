#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace weapon_library::detail {

inline bool valid_duration(double value) noexcept {
    return std::isfinite(value) && value >= 0.0;
}

// Treat differences of a few floating-point rounding units as the same boundary.
// A relative tolerance preserves genuinely short positive durations near zero.
inline bool duration_elapsed(double available, double required, double original_duration = 0.0) noexcept {
    if (available >= required) {
        return true;
    }
    return available > 0.0 && required - available <=
        (8.0 * std::numeric_limits<double>::epsilon()) *
        std::max({available, required, original_duration});
}

// Compensated subtraction prevents many short updates (or phase boundaries within
// one long update) from accumulating enough error to miss the next boundary.
inline void consume_duration(double& remaining, double elapsed, double& error,
                             double original_duration = 0.0) noexcept {
    if (duration_elapsed(elapsed, remaining, original_duration)) {
        remaining = 0.0;
        error = 0.0;
        return;
    }
    const double adjusted = -elapsed - error;
    const double updated = remaining + adjusted;
    error = (updated - remaining) - adjusted;
    remaining = updated;
}

} // namespace weapon_library::detail
