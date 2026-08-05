#include "thandor/catalog.hpp"
#include "thandor/field.hpp"
#include "thandor/level.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() try {
    using namespace thandor;
    require(resource_field_occupancy_class(810U) == 1U &&
            resource_field_occupancy_class(811U) == 1U,
            "energy resource aliases must map to occupancy class 1 / bit 0x1000");
    require(resource_field_occupancy_class(814U) == 0U &&
            resource_field_occupancy_class(815U) == 0U,
            "building-resource aliases must map to occupancy class 0 / bit 0x0800");
    require(!resource_field_occupancy_class(50U),
            "player-start ARM 50 must not fabricate a resource-field class");

    auto field = FieldGrid::create(59U, 59U, 0, 0U);
    Brush brush;
    brush.radius_cells = 2.0F;
    brush.strength = 1.0F;
    brush.shape = BrushShape::Square;
    brush.falloff = BrushFalloff::Constant;
    require(field.paint_placement_occupancy_class(20.0F, 20.0F, brush, 1U, true) == 25U,
            "stock resource stamp must write an exact 5x5 support patch");
    for (uint32_t y = 18U; y <= 22U; ++y) for (uint32_t x = 18U; x <= 22U; ++x) {
        require(field.placement_occupancy_class_present(x, y, 1U),
                "energy support bit missing inside 5x5 stamp");
        require(!field.terrain_rejects_placement_occupancy_class(x, y, 1U),
                "energy generator must be accepted inside its support stamp");
        require(field.terrain_rejects_placement_occupancy_class(x, y, 0U),
                "energy support must not satisfy building-material extractor");
    }

    LevelPlacement energy{};
    energy.army_asset_id = 811U;
    const auto& energy_cell = field.at(30U, 30U);
    energy.set_render_position_q12(energy_cell.world_x_q12, energy_cell.world_y_q12);
    LevelPlacement building{};
    building.army_asset_id = 815U;
    const auto& building_cell = field.at(40U, 40U);
    building.set_render_position_q12(building_cell.world_x_q12, building_cell.world_y_q12);
    const std::array<LevelPlacement, 2> placements{{energy, building}};
    require(stamp_resource_field_masks(field, placements) == 50U,
            "export synchronization must stamp both independent 5x5 resource fields");
    require(!field.terrain_rejects_placement_occupancy_class(30U, 30U, 1U),
            "ARM 811 export synchronization did not create energy support");
    require(!field.terrain_rejects_placement_occupancy_class(40U, 40U, 0U),
            "ARM 815 export synchronization did not create building-resource support");

    require(field.paint_placement_occupancy_class(20.0F, 20.0F, brush, 1U, false) == 25U,
            "resource-field erase must clear the exact painted patch");
    require(field.terrain_rejects_placement_occupancy_class(20U, 20U, 1U),
            "cleared support field must reject the generator again");

    std::cout << "G336_RESOURCE_FIELD_OCCUPANCY_CONTRACT PASS"
              << " auto_stamp_cells=25 energy_bit=0x1000 building_bit=0x0800\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G336_RESOURCE_FIELD_OCCUPANCY_CONTRACT FAIL: " << error.what() << '\n';
    return 1;
}
