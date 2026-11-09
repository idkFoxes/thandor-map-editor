#include "thandor/binary.hpp"
#include "thandor/catalog.hpp"
#include "thandor/field.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(float first, float second, float epsilon = 0.001F) {
    return std::abs(first - second) <= epsilon;
}

thandor::Brush point_brush() {
    thandor::Brush brush;
    brush.radius_cells = 0.1F;
    brush.strength = 1.0F;
    brush.shape = thandor::BrushShape::Circle;
    brush.falloff = thandor::BrushFalloff::Constant;
    return brush;
}

} // namespace

int main() try {
    using namespace thandor;

    // V434 / game.exe 0x00532B60: exact editor-facing retail FLD save sanitization.
    auto field = FieldGrid::create(3U, 3U, float_to_q12(2.0F), 7U);
    auto& cell = field.at(1U, 1U);
    cell.flags_and_material = 0xFFFFFFFFU;
    cell.set_material(7U);
    const auto bytes = field.serialize();
    const std::size_t offset = 0x200U + (1U * 3U + 1U) * 0x80U;
    require(read_u32(bytes, offset + 0x08U) == FieldGrid::kRetailSerializedTriangle0NormalSeed,
            "FLD +0x08 does not match retail save seed");
    require(read_u32(bytes, offset + 0x50U) ==
                ((0xFFFFFF00U | 7U) & FieldGrid::kRetailPersistedFlagsAndMaterialMask),
            "FLD +0x50 does not apply retail 0xE80078FF mask");
    require(read_u32(bytes, offset + 0x54U) == 0U,
            "FLD +0x54 runtime direction lookup was serialized");

    // V434 / game.exe 0x005616D0: terrain and water delta are updated as a pair for every edited
    // cell, including a dry cell. The absolute water plane is therefore invariant.
    auto dry = FieldGrid::create(3U, 3U, 0, 0U);
    const float absolute_before = dry.at(1U, 1U).water_height();
    dry.raise_lower(1.0F, 1.0F, point_brush(), 2.0F,
                    TerrainWaterEditPolicy::PreserveAbsoluteSurface);
    require(near(dry.at(1U, 1U).terrain_height(), 2.0F),
            "dry terrain edit did not move terrain");
    require(near(dry.at(1U, 1U).water_height(), absolute_before),
            "dry terrain edit moved absolute water plane");
    require(dry.at(1U, 1U).water_delta_q12 == float_to_q12(-3.0F),
            "dry terrain edit did not preserve retail paired-delta arithmetic");

    // V434 / game.exe 0x00571DE6: preview color selection is registry-ID based, not browser-class
    // based. This specifically fixes the visible kulisse.arm registry 318 preview.
    ArmyAssetPreset kulisse;
    kulisse.registry_id = 318U;
    kulisse.source_path = "arm/kulisse.arm";
    kulisse.browser_kind = PlacementBrowserKind::Element;
    require(placement_uses_neutral_source_color(kulisse),
            "world-placement neutral element policy changed");
    require(!army_asset_preview_uses_neutral_source_color(kulisse),
            "registry 318 preview should use selected-faction color");
    kulisse.registry_id = 400U;
    require(army_asset_preview_uses_neutral_source_color(kulisse),
            "registry 400 preview should use neutral color");

    std::cout << "G406 retail FLD save / height-delta / preview-color checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    return 1;
}
