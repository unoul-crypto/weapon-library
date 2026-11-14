#pragma once

#include "RangeSimulation.h"

namespace weapon_range {

// Rendering only: consumes state and never changes the simulation.
void draw_range(const RangeSimulation& range, bool playing);

} // namespace weapon_range
