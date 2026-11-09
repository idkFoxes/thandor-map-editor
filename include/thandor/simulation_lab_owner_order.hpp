#pragma once

#include <cstddef>
#include <cstdint>

namespace thandor {

// Compact representation of one MODEL owner at the moment an owner-list job
// captures its head. Retail links newly-created owners at the head: synthetic
// owners therefore precede authored LEV owners, and authored owners are visited
// in descending placement order. The stable index resolves equal-time fixtures
// without changing any distinct native key.
struct SimulationLabOwnerOrderKey {
    bool synthetic{};
    std::uint32_t creation_tick{};
    std::size_t source_placement_index{};
    std::size_t stable_index{};
};

constexpr bool simulation_lab_owner_newer_first(
    const SimulationLabOwnerOrderKey& left,
    const SimulationLabOwnerOrderKey& right) noexcept {
    if (left.synthetic != right.synthetic) return left.synthetic;
    if (left.synthetic && left.creation_tick != right.creation_tick)
        return left.creation_tick > right.creation_tick;
    if (left.source_placement_index != right.source_placement_index)
        return left.source_placement_index > right.source_placement_index;
    return left.stable_index > right.stable_index;
}

} // namespace thandor
