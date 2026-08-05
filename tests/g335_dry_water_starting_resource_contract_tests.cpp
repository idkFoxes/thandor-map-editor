#include "thandor/binary.hpp"
#include "thandor/field.hpp"
#include "thandor/level.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
std::vector<std::byte> make_level_fixture() {
    std::vector<std::byte> bytes(0x340U);
    thandor::write_u32(bytes, 0x00U, thandor::fourcc("lev"));
    thandor::write_u32(bytes, 0x04U, static_cast<uint32_t>(bytes.size()));
    thandor::write_u32(bytes, 0xD8U, 0U);
    thandor::write_u32(bytes, 0xDCU, static_cast<uint32_t>(bytes.size()));
    for (std::size_t index = 0U; index < 10U; ++index)
        thandor::write_u32(bytes, 0xB0U + index * 4U, static_cast<uint32_t>(0x318U + index * 4U));
    for (uint32_t faction = 1U; faction <= 7U; ++faction) {
        const std::size_t base = 0x200U + static_cast<std::size_t>(faction - 1U) * 0x20U;
        thandor::write_u32(bytes, base + 0x1CU, 0x100U + faction);
    }
    return bytes;
}
}

int main() {
    auto field = thandor::FieldGrid::create(59U, 59U, thandor::float_to_q12(2.0F), 0U);
    for (const auto& cell : field.cells()) assert(cell.water_delta_q12 < 0);

    thandor::Brush brush;
    brush.radius_cells = 0.1F;
    brush.falloff = thandor::BrushFalloff::Constant;
    field.set_water(1.0F, 1.0F, brush, 4.0F, true);
    assert(field.at(1U, 1U).water_delta_q12 > 0);
    field.set_water(1.0F, 1.0F, brush, 4.0F, false);
    assert(field.at(1U, 1U).water_delta_q12 == thandor::FieldGrid::kDryWaterDeltaQ12);
    field.at(2U, 2U).water_delta_q12 = 0;
    field.normalize_for_game_export();
    assert(field.at(2U, 2U).water_delta_q12 == thandor::FieldGrid::kDryWaterDeltaQ12);

    auto level = thandor::LevelAsset::parse(make_level_fixture());
    const uint32_t preserved_mode = thandor::read_u32(level.serialize(), 0x21CU);
    level.set_faction_starting_resources_q4(
        1U, thandor::LevelAsset::kDefaultBuildingResourceQ4,
        thandor::LevelAsset::kDefaultEnergyResourceQ4);
    assert(level.faction_building_resource_q4(1U) == 6500U * 16U);
    assert(level.faction_energy_resource_q4(1U) == 3000U * 16U);
    const auto serialized = level.serialize();
    assert(thandor::read_u32(serialized, 0x21CU) == preserved_mode);

    std::cout << "G335_DRY_WATER_STARTING_RESOURCE_CONTRACT PASS\n";
    return 0;
}
