#pragma once

#include "RangeSimulation.h"

#include <filesystem>
#include <memory>
#include <string>

namespace weapon_range {

// Owns graphics resources. Construct and destroy while the graphics context exists.
class RangeRenderer {
public:
    explicit RangeRenderer(std::filesystem::path config_directory);
    ~RangeRenderer();
    RangeRenderer(const RangeRenderer&) = delete;
    RangeRenderer& operator=(const RangeRenderer&) = delete;
    bool reload_visuals(RangeSimulation& range);
    void draw(const RangeSimulation& range, bool playing, bool slow_motion = false, bool show_crosshair = true);

private:
    struct Impl;
    std::filesystem::path config_directory_;
    std::unique_ptr<Impl> impl_;
    std::string status_;
    bool config_error_ = false;
};

} // namespace weapon_range
