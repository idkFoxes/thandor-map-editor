#include "thandor/path_diagnostics.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace thandor {
namespace {

int32_t signed_low32_shift(int64_t value, unsigned shift) noexcept {
    // The i386 sequences in V519 form the low 32 bits of an arithmetic 64-bit shift from EDX:EAX.
    return static_cast<int32_t>(value >> shift);
}

int32_t clamp_inner(int32_t value, uint32_t scratch_extent) noexcept {
    if (scratch_extent < 3U) return 0;
    const int32_t maximum = static_cast<int32_t>(std::min<uint32_t>(
        scratch_extent - 2U, static_cast<uint32_t>(std::numeric_limits<int32_t>::max())));
    return std::clamp(value, 1, maximum);
}

} // namespace

V519PathScratchCoordinate v519_path_world_to_scratch(int32_t world_x_q12,
                                                      int32_t world_y_q12) noexcept {
    // Same immutable physical transform already used by retail_fog_world_to_cell. V519 pathing
    // rounds the Q12 grid coordinates to the 4x GridScratch lattice with +0x800 >> 10.
    const int32_t diagonal_q12 = signed_low32_shift(
        static_cast<int64_t>(world_y_q12) * -0x20C8CCLL, 21U);
    const int32_t column_q12 = signed_low32_shift(
        static_cast<int64_t>(world_x_q12) * 0x1C6E9CLL, 20U) - diagonal_q12;
    const int32_t row_q12 = diagonal_q12 * 2;
    return {(column_q12 + 0x800) >> 10, (row_q12 + 0x800) >> 10};
}

V519PathScratchCoordinate v519_path_clip_to_inner_scratch(
    const FieldGrid& field, V519PathScratchCoordinate coordinate) noexcept {
    const uint32_t scratch_width = field.width() <= std::numeric_limits<uint32_t>::max() / 4U
        ? field.width() * 4U : std::numeric_limits<uint32_t>::max();
    const uint32_t scratch_height = field.height() <= std::numeric_limits<uint32_t>::max() / 4U
        ? field.height() * 4U : std::numeric_limits<uint32_t>::max();
    coordinate.column = clamp_inner(coordinate.column, scratch_width);
    coordinate.row = clamp_inner(coordinate.row, scratch_height);
    return coordinate;
}

V519PathWorldPointQ12 v519_path_scratch_center_to_world_q12(
    V519PathScratchCoordinate coordinate) noexcept {
    // V519 route reconstruction uses (index * 0x400 - 0x600) before applying the field basis.
    const int64_t row_offset_q12 = static_cast<int64_t>(coordinate.row) * 0x400LL - 0x600LL;
    const int64_t column_offset_q12 = static_cast<int64_t>(coordinate.column) * 0x400LL - 0x600LL;
    const int64_t x_product = (row_offset_q12 + column_offset_q12 * 2LL) * 0x901LL;
    const int64_t y_product = row_offset_q12 * -1999LL;
    return {signed_low32_shift(x_product, 13U), signed_low32_shift(y_product, 12U)};
}

V519PathMasks v519_path_masks(uint32_t faction_index, uint32_t traversal_low_band_index,
                              uint32_t traversal_high_band_index) noexcept {
    V519PathMasks result;
    result.faction_presence_mask = 1U << (faction_index & 0x1FU);
    result.traversal_bands_valid = traversal_low_band_index < 8U && traversal_high_band_index < 8U;
    if (!result.traversal_bands_valid) return result;
    result.high_cost_mask = 0x00010000U << traversal_low_band_index;
    result.blocking_mask = (0x00000100U << traversal_low_band_index) |
                           (0x01000000U << traversal_high_band_index);
    return result;
}

} // namespace thandor
