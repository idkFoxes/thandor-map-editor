#include "thandor/fog_preview.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool visible_at(const thandor::FieldGrid& field, const std::vector<uint64_t>& occupancy,
                uint32_t x, uint32_t y, uint32_t faction = 1U) {
    const auto byte = thandor::fog_occupancy_byte(
        occupancy[static_cast<std::size_t>(y) * field.width() + x], faction);
    return thandor::retail_fog_currently_visible(byte);
}

thandor::FogPreviewObserver observer_for(const thandor::FieldGrid& field,
                                         int32_t reference_height_q12,
                                         uint32_t radius_steps = 12U) {
    const auto& center = field.at(29U, 29U);
    return {center.world_x_q12, center.world_y_q12, reference_height_q12,
            radius_steps * 0x240U,
            thandor::retail_self_faction_visibility_mask(
                1U, thandor::retail_visibility_marker_for_model_class(11U))};
}
} // namespace

int main() try {
    auto field = thandor::FieldGrid::create(59U, 59U);

    // FieldGrid_WorldToGridQ12 constants and the projected-occlusion hex rounding must map every
    // generated FLD lattice vertex back to its own persisted row/column.
    for (uint32_t y = 0; y < field.height(); ++y) {
        for (uint32_t x = 0; x < field.width(); ++x) {
            const auto& cell = field.at(x, y);
            const auto mapped = thandor::retail_fog_world_to_cell(
                field, cell.world_x_q12, cell.world_y_q12);
            require(mapped.has_value(), "FLD lattice point mapped outside retail fog grid");
            require(mapped->x == x && mapped->y == y,
                    "retail fog world-to-grid/hex rounding changed a lattice cell");
        }
    }

    constexpr std::array<uint8_t, 24> expected_markers{{
        0x88U,0xC0U,0xC0U,0xC0U,0x90U,0x88U,0x88U,0x88U,
        0x88U,0x88U,0x88U,0x90U,0x88U,0x90U,0x90U,0x90U,
        0x90U,0xC0U,0xA0U,0xC0U,0x90U,0xC0U,0x90U,0x90U}};
    for (uint32_t model_class = 0; model_class < expected_markers.size(); ++model_class)
        require(thandor::retail_visibility_marker_for_model_class(model_class) ==
                    expected_markers[model_class],
                "runtime-class visibility marker table differs from game.exe");

    uint32_t relations = 0U;
    relations |= 0x8U << (1U * 4U);
    relations |= 0x8U << (3U * 4U);
    relations |= 0x8U << (7U * 4U);
    const uint64_t relation_mask = thandor::retail_faction_visibility_mask(relations, 0x90U);
    require(thandor::fog_occupancy_byte(relation_mask, 1U) == 0x90U &&
            thandor::fog_occupancy_byte(relation_mask, 2U) == 0x00U &&
            thandor::fog_occupancy_byte(relation_mask, 3U) == 0x90U &&
            thandor::fog_occupancy_byte(relation_mask, 7U) == 0x90U,
            "packed four-bit faction relation sharing does not match retail bit-3 test");

    require(thandor::retail_fog_runtime_visibility_byte(0x00U) == 0x00U &&
            thandor::retail_fog_runtime_visibility_byte(0x80U) == 0x87U &&
            thandor::retail_fog_runtime_visibility_byte(0x88U) == 0xFFU &&
            thandor::retail_fog_runtime_visibility_byte(0x01U) == 0xFFU,
            "0x00/0x87/0xFF retail fog classification endpoints changed");

    // TerrainByteClampLookup_Initialize generates the transition table used on normal play ticks.
    // One application moves exactly 0x15 toward the source-selected target.
    require(thandor::retail_fog_clamp_runtime_byte(0x00U,0x88U)==0x15U &&
            thandor::retail_fog_clamp_runtime_byte(0x7EU,0x80U)==0x87U &&
            thandor::retail_fog_clamp_runtime_byte(0xFFU,0x80U)==0xEAU &&
            thandor::retail_fog_clamp_runtime_byte(0x10U,0x00U)==0x00U &&
            thandor::retail_fog_clamp_runtime_byte(0xF8U,0x88U)==0xFFU,
            "retail 0x15 visibility clamp differs from generated lookup table");
    for(uint32_t step=0U;step<256U;++step){
        const uint32_t denominator=step+4U;
        const int32_t expected=static_cast<int32_t>((0x4000U+denominator/2U)/denominator);
        require(thandor::retail_fog_projection_scale_q12(step)==expected,
                "retail 0x00505FA0 projection table relationship changed");
    }
    require(thandor::retail_fog_projection_scale_q12(999U)==
                thandor::retail_fog_projection_scale_q12(255U),
            "fog projection step did not clamp to the retail 0xFF scan limit");

    require(thandor::retail_fog_clamp_calls_for_simulation_tick(3U,true)==2U &&
            thandor::retail_fog_clamp_calls_for_simulation_tick(3U,false)==1U &&
            thandor::retail_fog_clamp_calls_for_simulation_tick(7U,true)==1U &&
            thandor::retail_fog_clamp_calls_for_simulation_tick(7U,false)==0U &&
            thandor::retail_fog_clamp_calls_for_simulation_tick(11U,true)==2U &&
            thandor::retail_fog_clamp_calls_for_simulation_tick(15U,true)==1U &&
            thandor::retail_fog_clamp_calls_for_simulation_tick(2U,true)==0U,
            "retail owner-list-dependent phase-3/phase-7 visibility clamp cadence changed");

    const auto flat_observer = observer_for(field, field.at(29U,29U).terrain_height_q12);
    const auto flat = thandor::build_projected_fog_preview_occupancy(field, {&flat_observer, 1U});
    require(visible_at(field, flat, 29U, 29U), "observer center is not visible");
    require(visible_at(field, flat, 31U, 29U),
            "flat projected direction-0 ray did not reach a near cell");

    constexpr std::array<std::array<int32_t,2>,6> hex_directions{{
        {{ 1, 0}},{{ 1,-1}},{{ 0,-1}},{{-1, 0}},{{-1, 1}},{{ 0, 1}}}};
    for(const auto& direction:hex_directions){
        const uint32_t x1=static_cast<uint32_t>(29+direction[0]);
        const uint32_t y1=static_cast<uint32_t>(29+direction[1]);
        const uint32_t x2=static_cast<uint32_t>(29+direction[0]*2);
        const uint32_t y2=static_cast<uint32_t>(29+direction[1]*2);
        require(visible_at(field,flat,x1,y1)&&visible_at(field,flat,x2,y2),
                "one of the six flat retail scan directions is not rotationally closed");
    }

    // A hard retail barrier terminates both the wedge boundary and directional ray.
    field.at(30U,29U).flags_and_material |= 0x2000U;
    const auto barrier = thandor::build_projected_fog_preview_occupancy(field, {&flat_observer, 1U});
    require(!visible_at(field, barrier, 30U,29U) && !visible_at(field, barrier,31U,29U),
            "0x88006000 fog barrier did not stop the projected ray");
    field.at(30U,29U).flags_and_material &= ~0x2000U;

    // Terrain elevation is projected into the ray threshold. A high first cell remains visible
    // itself but occludes the lower cell behind it.
    field.at(30U,29U).terrain_height_q12 = 0x10000;
    const auto ridge = thandor::build_projected_fog_preview_occupancy(field, {&flat_observer, 1U});
    require(visible_at(field, ridge,30U,29U) && !visible_at(field,ridge,31U,29U),
            "terrain ridge did not occlude the next direction-0 cell");

    // Raising the hierarchy-derived observer eye above that ridge exposes the same cell again.
    const auto high_observer = observer_for(field, 0x20000);
    const auto high_eye = thandor::build_projected_fog_preview_occupancy(field, {&high_observer,1U});
    require(visible_at(field, high_eye,31U,29U),
            "higher retail reference height did not see behind the test ridge");

    // Positive water delta contributes to the projected surface, while dry/negative delta does not.
    field.at(30U,29U).terrain_height_q12 = 0;
    field.at(30U,29U).water_delta_q12 = 0x10000;
    const auto water = thandor::build_projected_fog_preview_occupancy(field, {&flat_observer,1U});
    require(visible_at(field,water,30U,29U) && !visible_at(field,water,31U,29U),
            "positive water surface did not participate in retail projected occlusion");

    std::cout << "G407_RETAIL_PROJECTED_FOG_OF_WAR PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G407_RETAIL_PROJECTED_FOG_OF_WAR FAIL: " << error.what() << '\n';
    return 1;
}
