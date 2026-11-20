#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace weapon_library {

// Game/inventory adapter. take_up_to is one atomic operation: remove and return
// 0..requested rounds, or return zero without removing anything on failure.
// Implementations must not throw or re-enter the weapon. No reservation is needed:
// the weapon calls this only at a reload transfer boundary and can accept the whole result.
class AmmunitionSource {
public:
    virtual ~AmmunitionSource() = default;
    virtual std::uint32_t available_rounds() const noexcept = 0;
    virtual std::uint32_t take_up_to(std::uint32_t requested) noexcept = 0;
};

// Optional single-threaded counted source for games without an inventory adapter.
class AmmunitionReserve final : public AmmunitionSource {
public:
    explicit AmmunitionReserve(std::uint32_t rounds = 0) noexcept : rounds_(rounds) {}
    std::uint32_t available_rounds() const noexcept override { return rounds_; }
    std::uint32_t take_up_to(std::uint32_t requested) noexcept override {
        const auto taken = (std::min)(requested, rounds_);
        rounds_ -= taken;
        return taken;
    }
    // Returns accepted rounds; the caller retains any excess at the numeric limit.
    std::uint32_t add_rounds(std::uint32_t offered) noexcept {
        const auto accepted = (std::min)(offered, (std::numeric_limits<std::uint32_t>::max)() - rounds_);
        rounds_ += accepted;
        return accepted;
    }
private:
    std::uint32_t rounds_;
};

} // namespace weapon_library
