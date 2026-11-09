#include "thandor/fog_preview.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace thandor {
namespace {

constexpr uint32_t kFogBarrierMask = 0x88006000U;
constexpr uint32_t kMaximumScanStep = 0xFFU;
constexpr uint32_t kRadiusUnitsPerScanStep = 0x240U;

struct CellCoord {
    int32_t x{};
    int32_t y{};
};

constexpr std::array<CellCoord, 6> kDirections{{
    { 1,  0}, // Direction0
    { 1, -1}, // Direction1
    { 0, -1}, // Direction2
    {-1,  0}, // Direction3
    {-1,  1}, // Direction4
    { 0,  1}, // Direction5
}};

constexpr std::array<uint8_t, 24> kRuntimeClassVisibilityMarkers{{
    0x88U, 0xC0U, 0xC0U, 0xC0U, 0x90U, 0x88U,
    0x88U, 0x88U, 0x88U, 0x88U, 0x88U, 0x90U,
    0x88U, 0x90U, 0x90U, 0x90U, 0x90U, 0xC0U,
    0xA0U, 0xC0U, 0x90U, 0xC0U, 0x90U, 0x90U,
}};

bool inside(const FieldGrid& field, CellCoord coord) noexcept {
    return coord.x >= 0 && coord.y >= 0 &&
           static_cast<uint32_t>(coord.x) < field.width() &&
           static_cast<uint32_t>(coord.y) < field.height();
}

const FieldCell* cell_at(const FieldGrid& field, CellCoord coord) noexcept {
    if (!inside(field, coord)) return nullptr;
    return &field.at(static_cast<uint32_t>(coord.x), static_cast<uint32_t>(coord.y));
}

bool blocked(const FieldGrid& field, CellCoord coord) noexcept {
    const auto* cell = cell_at(field, coord);
    return cell == nullptr || (cell->flags_and_material & kFogBarrierMask) != 0U;
}

std::size_t flat_index(const FieldGrid& field, CellCoord coord) noexcept {
    return static_cast<std::size_t>(coord.y) * field.width() +
           static_cast<uint32_t>(coord.x);
}

int32_t raw_terrain_delta(const FieldGrid& field, CellCoord coord,
                          int32_t reference_height_q12) noexcept {
    const auto* cell = cell_at(field, coord);
    return cell == nullptr ? std::numeric_limits<int32_t>::min()
                           : cell->terrain_height_q12 - reference_height_q12;
}

int32_t projected_surface_delta(const FieldGrid& field, CellCoord coord, uint32_t step,
                                int32_t reference_height_q12) noexcept {
    const auto* cell = cell_at(field, coord);
    if (cell == nullptr) return std::numeric_limits<int32_t>::min();
    const int32_t surface_q12 = cell->terrain_height_q12 + std::max(cell->water_delta_q12, 0);
    const int64_t product = static_cast<int64_t>(surface_q12 - reference_height_q12) *
                            retail_fog_projection_scale_q12(step);
    return static_cast<int32_t>(product >> 12);
}

void mark_cell(const FieldGrid& field, CellCoord coord, uint64_t mask,
               std::vector<uint64_t>& occupancy) noexcept {
    if (!inside(field, coord)) return;
    occupancy[flat_index(field, coord)] |= mask;
}

void scan_direction(const FieldGrid& field, CellCoord coord, CellCoord direction,
                    uint32_t step, uint32_t step_limit, int32_t threshold,
                    int32_t reference_height_q12, uint64_t mask,
                    std::vector<uint64_t>& occupancy) noexcept {
    while (step < step_limit) {
        if (blocked(field, coord)) return;
        const int32_t projected =
            projected_surface_delta(field, coord, step, reference_height_q12);
        if (projected >= threshold) {
            mark_cell(field, coord, mask, occupancy);
            threshold = projected;
        }
        step += 4U;
        coord.x += direction.x;
        coord.y += direction.y;
    }
}

void trace_wedge(const FieldGrid& field, CellCoord center, uint32_t direction_index,
                 uint32_t step_limit, int32_t reference_height_q12, uint64_t mask,
                 std::vector<uint64_t>& occupancy) noexcept {
    const CellCoord direction_a = kDirections[direction_index % kDirections.size()];
    const CellCoord direction_b = kDirections[(direction_index + 1U) % kDirections.size()];
    CellCoord current{center.x + direction_a.x, center.y + direction_a.y};
    const CellCoord other_boundary{center.x + direction_b.x, center.y + direction_b.y};
    if (blocked(field, current)) return;

    int32_t comparison_threshold = raw_terrain_delta(field, current, reference_height_q12);
    int32_t threshold = comparison_threshold;
    uint32_t step = 0U;
    if (step < step_limit) {
        const int32_t other = raw_terrain_delta(field, other_boundary, reference_height_q12);
        // The retail wedge takes the signed arithmetic mean of the two adjacent raw terrain
        // deltas before projecting any surface/water height.
        threshold = static_cast<int32_t>(
            (static_cast<int64_t>(other) + comparison_threshold) >> 1);
    }

    while (step < step_limit && !blocked(field, current)) {
        int32_t projected =
            projected_surface_delta(field, current, step, reference_height_q12);
        if (projected >= comparison_threshold) {
            mark_cell(field, current, mask, occupancy);
            threshold = projected;
        }

        CellCoord ray_a{current.x + direction_a.x, current.y + direction_a.y};
        scan_direction(field, ray_a, direction_a, step + 4U, step_limit, threshold,
                       reference_height_q12, mask, occupancy);

        step += 4U;
        if (step >= step_limit) return;

        CellCoord second{current.x + direction_b.x, current.y + direction_b.y};
        if (blocked(field, second)) return;
        projected = projected_surface_delta(field, second, step, reference_height_q12);
        if (projected >= threshold) {
            mark_cell(field, second, mask, occupancy);
            threshold = projected;
        }

        CellCoord ray_b{second.x + direction_b.x, second.y + direction_b.y};
        scan_direction(field, ray_b, direction_b, step + 3U, step_limit, threshold,
                       reference_height_q12, mask, occupancy);

        step += 3U;
        comparison_threshold = threshold;
        current.x += direction_a.x + direction_b.x;
        current.y += direction_a.y + direction_b.y;
    }
}

int32_t signed_low32_shift(int64_t value, unsigned shift) noexcept {
    return static_cast<int32_t>(value >> shift);
}

} // namespace


int32_t retail_fog_projection_scale_q12(uint32_t scan_step) noexcept {
    const uint32_t step = std::min(scan_step, kMaximumScanStep);
    const uint32_t denominator = step + 4U;
    return static_cast<int32_t>((0x4000U + denominator / 2U) / denominator);
}

uint8_t retail_visibility_marker_for_model_class(uint32_t runtime_class_id) noexcept {
    return runtime_class_id < kRuntimeClassVisibilityMarkers.size()
        ? kRuntimeClassVisibilityMarkers[runtime_class_id]
        : 0x88U;
}

uint64_t retail_faction_visibility_mask(uint32_t packed_relation_states,
                                        uint8_t marker) noexcept {
    uint64_t result = 0U;
    for (uint32_t faction = 1U; faction <= 7U; ++faction) {
        const uint32_t relation_nibble = (packed_relation_states >> (faction * 4U)) & 0xFU;
        if ((relation_nibble & 0x8U) != 0U)
            result |= static_cast<uint64_t>(marker) << (faction * 8U);
    }
    return result;
}

uint64_t retail_self_faction_visibility_mask(uint32_t faction_index,
                                             uint8_t marker) noexcept {
    if (faction_index == 0U || faction_index > 7U) return 0U;
    return static_cast<uint64_t>(marker) << (faction_index * 8U);
}

std::optional<FogPreviewCell> retail_fog_world_to_cell(const FieldGrid& field,
                                                       int32_t world_x_q12,
                                                       int32_t world_y_q12) noexcept {
    // FieldGrid_WorldToGridQ12 at 0x004FEA50. These constants are the exact i386 multipliers.
    // The call site passes world Y as stack argument 0 and world X as stack argument 1.
    // WorldToGrid multiplies world X by +0x1C6E9C >>20 and world Y by -0x20C8CC >>21,
    // returns columnQ12 in EAX and rowQ12 in EDX.
    const int32_t diagonal_q12 = signed_low32_shift(
        static_cast<int64_t>(world_y_q12) * -0x20C8CCLL, 21U);
    const int32_t column_q12 = signed_low32_shift(
        static_cast<int64_t>(world_x_q12) * 0x1C6E9CLL, 20U) - diagonal_q12;
    const int32_t row_q12 = diagonal_q12 * 2;

    int32_t column = column_q12 >> 12;
    int32_t row = row_q12 >> 12;
    const uint32_t frac_column = static_cast<uint32_t>(column_q12) & 0xFFFU;
    const uint32_t frac_row = static_cast<uint32_t>(row_q12) & 0xFFFU;
    const uint32_t first = frac_row + frac_column * 2U;
    if (first < 0x1000U) {
        if (frac_column + frac_row * 2U >= 0x1000U) ++row;
    } else if (first <= 0x2000U) {
        ++column;
        if (frac_column < frac_row) {
            --column;
            ++row;
        }
    } else {
        ++column;
        if (frac_column + frac_row * 2U >= 0x2000U) ++row;
    }

    if (column < 0 || row < 0 || static_cast<uint32_t>(column) >= field.width() ||
        static_cast<uint32_t>(row) >= field.height())
        return std::nullopt;
    return FogPreviewCell{static_cast<uint32_t>(column), static_cast<uint32_t>(row)};
}

std::optional<FogPreviewCell> retail_nearest_world_to_cell(const FieldGrid& field,
                                                           int32_t world_x_q12,
                                                           int32_t world_y_q12) noexcept {
    // Same transformed nearest-cell path used by ArmyRuntime_UpdateTimedShotAndEffectEmitters and
    // ArmyRuntime_TryPlayMappedTerrainSoundAtWorldPoint. Physical world Y feeds -0x20C8CC and
    // physical world X feeds +0x1C6E9C.
    const int32_t diagonal_q12 = signed_low32_shift(
        static_cast<int64_t>(world_y_q12) * -0x20C8CCLL, 21U);
    const int32_t column_q12 = signed_low32_shift(
        static_cast<int64_t>(world_x_q12) * 0x1C6E9CLL, 20U) - diagonal_q12;
    const int32_t row_q12 = diagonal_q12 * 2;
    const int32_t column = (column_q12 + 0x800) >> 12;
    const int32_t row = (row_q12 + 0x800) >> 12;
    if (column < 0 || row < 0 || static_cast<uint32_t>(column) >= field.width() ||
        static_cast<uint32_t>(row) >= field.height()) return std::nullopt;
    return FogPreviewCell{static_cast<uint32_t>(column),static_cast<uint32_t>(row)};
}

int32_t retail_nearest_water_delta_q12(const FieldGrid& field, int32_t world_x_q12,
                                       int32_t world_y_q12) noexcept {
    const auto cell=retail_nearest_world_to_cell(field,world_x_q12,world_y_q12);
    return cell?field.at(cell->x,cell->y).water_delta_q12:0;
}

std::vector<uint64_t> build_projected_fog_preview_occupancy(
    const FieldGrid& field,
    std::span<const FogPreviewObserver> observers) {
    std::vector<uint64_t> occupancy(
        static_cast<std::size_t>(field.width()) * field.height(), 0U);
    for (const auto& observer : observers) {
        if (observer.faction_visibility_mask == 0U || observer.radius_world_units == 0U) continue;
        const auto center = retail_fog_world_to_cell(field, observer.world_x_q12,
                                                     observer.world_y_q12);
        if (!center) continue;
        const CellCoord center_coord{static_cast<int32_t>(center->x),
                                     static_cast<int32_t>(center->y)};
        if (blocked(field, center_coord)) continue;

        mark_cell(field, center_coord, observer.faction_visibility_mask, occupancy);
        const uint32_t step_limit = std::clamp(
            observer.radius_world_units / kRadiusUnitsPerScanStep, 1U, kMaximumScanStep);
        for (uint32_t wedge = 0U; wedge < 6U; ++wedge)
            trace_wedge(field, center_coord, wedge, step_limit, observer.reference_height_q12,
                        observer.faction_visibility_mask, occupancy);
    }
    return occupancy;
}

uint8_t retail_fog_runtime_visibility_byte(uint8_t occupancy_byte) noexcept {
    if ((occupancy_byte & 0x79U) != 0U) return 0xFFU;
    return (occupancy_byte & 0x80U) != 0U ? 0x87U : 0x00U;
}

uint8_t retail_fog_clamp_runtime_byte(uint8_t current_runtime_byte,
                                      uint8_t occupancy_byte) noexcept {
    // TerrainByteClampLookup_Initialize emits one 256-byte row per source-byte value. Rows
    // 0x00..0x7F alternate target 0/0xFF, row 0x80 targets 0x87, row 0x81 targets 0xFF,
    // row 0x82 targets 0x87, and rows 0x83..0xFF target 0xFF. Visibility occupancy normally uses
    // 0x00, 0x80, or one of the class markers 0x88/0x90/0xA0/0xC0 (and OR-combinations).
    uint8_t target = 0U;
    if (occupancy_byte < 0x80U) {
        target = (occupancy_byte & 1U) != 0U ? 0xFFU : 0U;
    } else if (occupancy_byte == 0x80U || occupancy_byte == 0x82U) {
        target = 0x87U;
    } else {
        target = 0xFFU;
    }
    constexpr uint8_t kDelta = 0x15U;
    if (current_runtime_byte < target) {
        const uint16_t advanced = static_cast<uint16_t>(current_runtime_byte) + kDelta;
        return static_cast<uint8_t>(std::min<uint16_t>(advanced, target));
    }
    if (current_runtime_byte > target) {
        return current_runtime_byte - target <= kDelta
            ? target
            : static_cast<uint8_t>(current_runtime_byte - kDelta);
    }
    return target;
}

uint8_t retail_fog_clamp_calls_for_simulation_tick(uint32_t simulation_tick,
                                                    bool has_runtime_owners) noexcept {
    switch (simulation_tick & 7U) {
    case 3U: return static_cast<uint8_t>(1U + (has_runtime_owners ? 1U : 0U));
    case 7U: return has_runtime_owners ? 1U : 0U;
    default: return 0U;
    }
}

bool retail_fog_currently_visible(uint8_t occupancy_byte) noexcept {
    return (occupancy_byte & 0x79U) != 0U;
}

bool retail_fog_explored(uint8_t occupancy_byte) noexcept {
    return (occupancy_byte & 0xF8U) != 0U;
}

uint8_t retail_model_fog_intensity(uint8_t occupancy_byte, uint32_t model_definition_flags) noexcept {
    if(retail_fog_currently_visible(occupancy_byte)) return 255U;
    if(retail_fog_explored(occupancy_byte)&&(model_definition_flags&0x10U)==0U) return 135U;
    return 0U;
}

uint8_t fog_occupancy_byte(uint64_t occupancy_mask, uint32_t faction_index) noexcept {
    if (faction_index > 7U) return 0U;
    return static_cast<uint8_t>((occupancy_mask >> (faction_index * 8U)) & 0xFFU);
}

uint32_t retail_model_neighborhood_mask(const FieldGrid& field,std::span<const uint64_t> occupancy,
    int32_t xq,int32_t yq,int32_t radius) {
    const uint32_t half=uint32_t((int64_t(yq)*-0x20C8CC)>>21);
    const uint32_t col=uint32_t((int64_t(xq)*0x1C6E9C)>>20)-half,row=half*2U;
    const uint32_t x=((col>>11)+1U)>>1,y=((row>>11)+1U)>>1;
    if(x>=field.width()||y>=field.height()||occupancy.size()!=size_t(field.width())*field.height()||
       (field.at(x,y).flags_and_material&0x88006000U))return 0;
    uint64_t mask=occupancy[size_t(y)*field.width()+x];
    const uint32_t steps=std::clamp((uint32_t(radius)+0x7FFU)/0x901U,1U,255U);
    constexpr std::array<std::array<int,2>,6> rays{{{1,0},{-1,0},{1,-1},{0,-1},{-1,1},{0,1}}};
    for(const auto& direction:rays){int32_t cx=int32_t(x),cy=int32_t(y);
        for(uint32_t n=0;n<steps;++n){cx+=direction[0];cy+=direction[1];
            if(cx<0||cy<0||uint32_t(cx)>=field.width()||uint32_t(cy)>=field.height())break;
            mask|=occupancy[size_t(cy)*field.width()+uint32_t(cx)];
            if(field.at(uint32_t(cx),uint32_t(cy)).flags_and_material&0x88006000U)break;
        }
    }
    uint32_t packed=0;
    for(uint32_t f=0;f<8;++f){const auto b=uint8_t(mask>>(f*8U))&0xF9U;
        if(b)packed|=(b==0x80U?1U:2U)<<(f*2U);
    }
    return packed;
}
RetailModelOccupancy retail_resolve_model_occupancy(uint32_t primary,uint32_t history,bool not_remembered){
    history|=((primary&0xAAAAAAAAU)>>1U)*3U;
    uint32_t combined=primary&history;
    if(!not_remembered)combined|=(combined*2U)&0xAAAAAAAAU;
    return {combined,history};
}

} // namespace thandor
