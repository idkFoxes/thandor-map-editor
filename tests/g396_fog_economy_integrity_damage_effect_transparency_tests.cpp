#include "thandor/binary.hpp"
#include "thandor/catalog.hpp"
#include "thandor/model.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void write_ascii_utf16(std::span<std::byte> data, std::size_t offset,
                       std::string_view value) {
    for (const char character : value) {
        thandor::write_u16(data, offset,
            static_cast<uint16_t>(static_cast<unsigned char>(character)));
        offset += 2U;
    }
    thandor::write_u16(data, offset, 0U);
}

std::vector<std::byte> model_stat_fixture() {
    constexpr std::size_t record_offset = 0x200U;
    constexpr std::size_t record_size = 0x300U;
    constexpr std::size_t root_node_offset = 0x500U;
    std::vector<std::byte> data(0x600U, std::byte{});
    thandor::write_u32(data, 0x00U, thandor::fourcc("mdl"));
    thandor::write_u32(data, 0x04U, static_cast<uint32_t>(data.size()));
    thandor::write_u32(data, 0xB0U, 1U);
    thandor::write_u32(data, record_offset + 0x00U, static_cast<uint32_t>(record_size));
    thandor::write_u32(data, record_offset + 0x08U, 999U);
    thandor::write_i32(data, record_offset + 0x48U, 35000);
    thandor::write_i32(data, record_offset + 0x60U, 10500);
    thandor::write_u32(data, record_offset + 0x180U, 571U);
    thandor::write_u32(data, record_offset + 0x184U, 17600U);
    thandor::write_u32(data, record_offset + 0x18CU, 192U);
    thandor::write_u32(data, record_offset + 0x64U, static_cast<uint32_t>(root_node_offset));
    thandor::write_u32(data, root_node_offset + 0x00U, 0x80U);
    thandor::write_u32(data, root_node_offset + 0x04U, 0U);
    thandor::write_u32(data, root_node_offset + 0x0CU, 0x4000U);
    thandor::write_u32(data, root_node_offset + 0x14U, 0U);
    write_ascii_utf16(data, root_node_offset + 0x38U, "spr/test/stat.spr");
    return data;
}

const thandor::ArmyAssetPreset& preset(const thandor::AssetCatalog& catalog, uint32_t id) {
    const auto* value = catalog.army_preset(id);
    if (value == nullptr) throw std::runtime_error("missing ARM preset " + std::to_string(id));
    return *value;
}
} // namespace

int main(int argc, char** argv) try {
    const auto records = thandor::parse_model_visual_records(model_stat_fixture(), "mdl/test/stat.mdl");
    require(records.size() == 1U, "synthetic MDL did not produce one definition");
    require(records.front().simulation.fog_uncover_radius_q12 == 35000,
            "MDL +0x48 fog uncover radius was not parsed");
    require(records.front().simulation.maximum_integrity == 10500,
            "MDL +0x60 maximum integrity was not parsed");
    require(records.front().simulation.retail_build_time_q5 == 571U &&
            records.front().simulation.retail_xenite_cost_q4 == 17600U &&
            records.front().simulation.retail_energy_cost_q4 == 192U,
            "MDL +0x180/+0x184/+0x18C retail card values were not parsed");
    require(records.front().hierarchy_nodes.front().simulation.fog_uncover_radius_q12 == 35000 &&
            records.front().hierarchy_nodes.front().simulation.maximum_integrity == 10500 &&
            records.front().hierarchy_nodes.front().simulation.retail_build_time_q5 == 571U &&
            records.front().hierarchy_nodes.front().simulation.retail_xenite_cost_q4 == 17600U &&
            records.front().hierarchy_nodes.front().simulation.retail_energy_cost_q4 == 192U,
            "MDL statistics were not propagated to runtime hierarchy nodes");

    const auto root = std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto catalog_source = read_text(root / "src/core/catalog.cpp");
    const auto editor_source = read_text(root / "src/editor/editor_app.cpp");
    const auto renderer_source = read_text(root / "src/editor/terrain_renderer.cpp");
    const auto primitive_header = read_text(root / "src/editor/primitive_render.hpp");
    require(catalog_source.find("node.simulation.fog_uncover_radius_q12") != std::string::npos &&
            catalog_source.find("preset.fog_uncover_radius_q12 / 0x240") != std::string::npos,
            "resolved-hierarchy fog radius and scan-step aggregation are missing");
    require(catalog_source.find("visual->simulation.retail_build_time_q5") != std::string::npos &&
            catalog_source.find("visual->simulation.retail_xenite_cost_q4") != std::string::npos &&
            catalog_source.find("visual->simulation.retail_energy_cost_q4") != std::string::npos &&
            catalog_source.find("base_cost_components") == std::string::npos,
            "retail MDL hierarchy card values are missing or the superseded TEC-price inference returned");
    require(catalog_source.find("validate_effect") != std::string::npos &&
            catalog_source.find("linked_shot_present") != std::string::npos &&
            catalog_source.find("periodic_effect_id") != std::string::npos &&
            catalog_source.find("active_effects.contains") != std::string::npos,
            "recursive SHT/EFF/SPR validation is incomplete");
    require(editor_source.find("FogOverlayNoDepthTest") != std::string::npos &&
            editor_source.find("{base,base+3U,base+1U}") != std::string::npos &&
            editor_source.find("{base+3U,base+2U,base+1U}") != std::string::npos,
            "fog overlay does not use the exact terrain split and late no-depth pass");
    require(editor_source.find("range, browser kind") != std::string::npos &&
            editor_source.find("preset.fog_uncover_radius_q12") != std::string::npos,
            "simulated fog still uses guessed weapon/radar radii");
    require(primitive_header.find("FogOverlayNoDepthTest") != std::string::npos &&
            renderer_source.find("Animated water/lava writes depth before live models and effects") !=
                std::string::npos &&
            renderer_source.find("VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, false") != std::string::npos,
            "lava/smoke/fog render-stage correction is incomplete");
    require(editor_source.find("PCK gameplay values") != std::string::npos &&
            editor_source.find("target-class damage raw (+0xB0)") != std::string::npos &&
            editor_source.find("Effect pipeline: %s") != std::string::npos,
            "asset tooltip does not expose fog, integrity, costs, damage, and validation");

    if (argc < 2) {
        std::cout << "G396_FOG_ECONOMY_INTEGRITY_DAMAGE_EFFECT_TRANSPARENCY synthetic PASS\n";
        return 0;
    }

    const auto catalog = thandor::AssetCatalog::scan_game_root(argv[1]);
    const auto& headquarters = preset(catalog, 300U);
    const auto& radar = preset(catalog, 340U);
    const auto& laser_coil = preset(catalog, 345U);
    const auto& laser_turret = preset(catalog, 351U);
    const auto& rocket_turret = preset(catalog, 352U);
    const auto& weasel = preset(catalog, 1U);

    require(headquarters.fog_uncover_radius_q12 == 20000 &&
            headquarters.fog_uncover_scan_steps == 34U &&
            headquarters.root_maximum_integrity == 40000,
            "headquarters fog/integrity PCK values changed");
    require(radar.fog_uncover_radius_q12 == 35000 && radar.fog_uncover_scan_steps == 60U,
            "radar linked hierarchy is not max-aggregating MDL +0x48");
    require(laser_coil.fog_uncover_radius_q12 == 32000 &&
            laser_coil.root_maximum_integrity == 14000 &&
            laser_coil.retail_armour >= laser_coil.root_maximum_integrity &&
            laser_coil.retail_stat_arm_node_count > 0U,
            "laser coil PCK fog/integrity values changed");
    require(laser_turret.retail_xenite_cost_q4 > 0U &&
            laser_turret.retail_energy_cost_q4 > 0U &&
            laser_turret.effect_pipeline_validation.ok(),
            "large laser turret retail price or recursive pipeline validation changed");
    require(rocket_turret.retail_xenite_cost_q4 > 0U &&
            rocket_turret.retail_energy_cost_q4 > 0U &&
            rocket_turret.effect_pipeline_validation.ok() &&
            rocket_turret.effect_pipeline_validation.maximum_effect_depth >= 1U,
            "rocket turret base TEC price or recursive effect graph changed");
    require(weasel.fog_uncover_radius_q12 == 15000 &&
            weasel.root_maximum_integrity == 1500 &&
            weasel.effect_pipeline_validation.ok(),
            "Weasel PCK gameplay fields or weapon pipeline changed");

    std::size_t validated_visible_presets = 0U;
    for (const auto& candidate : catalog.army_presets()) {
        if (!candidate.browser_visible) continue;
        require(candidate.effect_pipeline_validation.ok(),
                "stock recursive effect validation failed for ARM " +
                    std::to_string(candidate.registry_id));
        ++validated_visible_presets;
    }
    require(validated_visible_presets >= 250U,
            "stock effect-pipeline census did not cover the full visible catalog");

    const auto* shot = catalog.shot_definition(18U);
    require(shot != nullptr && shot->target_impact_damage_q12[0] == 850 &&
            shot->target_impact_damage_q12[3] == 1450,
            "SHT 18 target-class damage table changed");

    std::cout << "G396_FOG_ECONOMY_INTEGRITY_DAMAGE_EFFECT_TRANSPARENCY "
              << "radarFog=" << radar.fog_uncover_radius_q12
              << " laserCostQ4=" << laser_turret.retail_xenite_cost_q4
              << " rocketEffects=" << rocket_turret.effect_pipeline_validation.effect_reference_count
              << " PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G396 checks failed: " << error.what() << '\n';
    return 1;
}
