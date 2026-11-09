#include "thandor/catalog.hpp"
#include "thandor/field.hpp"
#include "thandor/level.hpp"
#include "thandor/model.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path resolve_game_root(int argc, char** argv) {
    if (argc >= 2 && argv[1] != nullptr && argv[1][0] != '\0') return argv[1];
    if (const char* value = std::getenv("THANDOR_GAME_ROOT"); value != nullptr && value[0] != '\0')
        return value;
    return {};
}

} // namespace

int main(int argc, char** argv) try {
    using namespace thandor;

    auto field = FieldGrid::create(59U, 59U, 0, 0U);
    auto flat_dry = field.terrain_compatibility(20U, 20U);
    require(flat_dry.terrain_normal_elevation_turn16 == 0x4000U,
            "flat terrain normal must use the binary's 0x4000 elevation convention");
    require(flat_dry.water_normal_elevation_turn16 == 0x4000U,
            "flat water normal must use the binary's 0x4000 elevation convention");
    require(flat_dry.classification_bits == 0x01000000U,
            "flat dry cell must set only recovered terrain class bit 24");
    require(field.terrain_forbidden_for_traversal_band(20U, 20U, 0U),
            "dry terrain must reject traversal band 0");
    require(!field.terrain_forbidden_for_traversal_band(20U, 20U, 6U),
            "flat dry terrain must allow the player-start vehicle's high band 6");

    for (auto& cell : field.cells()) cell.water_delta_q12 = float_to_q12(0.05F);
    const auto flat_shallow = field.terrain_compatibility(20U, 20U);
    require((flat_shallow.classification_bits & 0x7F000000U) == 0U,
            "flat shallow-water cell must not fabricate a steep/deep terrain-class bit");
    require(!field.terrain_forbidden_for_traversal_band(20U, 20U, 0U),
            "flat shallow water must allow traversal band 0");
    for (auto& cell : field.cells()) cell.water_delta_q12 = float_to_q12(1.0F);
    const auto flat_deep = field.terrain_compatibility(20U, 20U);
    require((flat_deep.classification_bits & 0x70000000U) == 0x70000000U,
            "water delta >= 500 Q12 must set recovered deep-water classes 28..30");
    require(field.terrain_forbidden_for_traversal_band(20U, 20U, 6U),
            "deep water must reject the player-start vehicle's band 6");

    require(field.terrain_rejects_placement_occupancy_class(20U, 20U, 1U),
            "missing MDL occupancy class 1 support bit must reject placement");
    field.at(20U, 20U).flags_and_material |= 0x00001000U;
    require(!field.terrain_rejects_placement_occupancy_class(20U, 20U, 1U),
            "MDL occupancy class 1 support bit 0x1000 must accept placement");
    require(field.terrain_rejects_placement_occupancy_class(20U, 20U, 0U),
            "class 1 support must not satisfy class 0");
    field.at(20U, 20U).flags_and_material |= 0x00002000U;
    require(field.terrain_forbidden_for_traversal_band(20U, 20U, 6U),
            "hard FLD boundary/blocker bit must reject every traversal profile");

    const auto game_root = resolve_game_root(argc, argv);
    if (game_root.empty()) {
        std::cout << "G334_TERRAIN_COMPATIBILITY_FIXTURE PASS (stock PCK scan skipped)\n";
        return 0;
    }

    const auto catalog = AssetCatalog::scan_game_root(game_root);
    require(catalog.map_groups().size() == 56U, "expected complete 56-map stock catalog");

    const auto* start = catalog.army_preset(50U);
    require(start != nullptr && start->visual_kind == PlacementVisualKind::PlayerStart,
            "ARM 50 player-start vehicle classification missing");
    require(start->has_traversal_profile && start->traversal_high_band_index == 6U,
            "ARM 50 MDL traversal profile is not the recovered band-6 profile");
    require(!start->has_placement_occupancy_class,
            "movable ARM 50 must not expose its unrelated +0xC0 dword as a structure occupancy class");

    std::size_t visible_assets = 0U;
    std::size_t visible_elements = 0U;
    std::size_t decoded_visible_sprites = 0U;
    std::size_t stock_placements = 0U;
    std::size_t campaign_variant_checks = 0U;
    std::set<uint32_t> visible_ids;
    for (const auto& preset : catalog.army_presets()) {
        if (!preset.browser_visible) continue;
        ++visible_assets;
        visible_ids.insert(preset.registry_id);
        require(preset.game_placeable, "visible card is not game-placeable: " + std::to_string(preset.registry_id));
        require(preset.visual_chain_complete,
                "visible card has incomplete ARM->MDL->SPR chain: " + std::to_string(preset.registry_id));
        require(!preset.compatible_arm_paths.empty() && !preset.compatible_model_paths.empty(),
                "visible card lost dependency alternatives: " + std::to_string(preset.registry_id));
        if (preset.visual_kind == PlacementVisualKind::PlayerStart) continue;
        const auto sprite = SpriteAsset::parse(catalog.load(preset.sprite_path));
        require(!sprite.mesh().vertices.empty() && !sprite.mesh().indices.empty(),
                "visible card resolves to an empty SPR: " + std::to_string(preset.registry_id));
        ++decoded_visible_sprites;
        if (preset.browser_kind == PlacementBrowserKind::Element) {
            ++visible_elements;
            require(preset.stock_placement_count != 0U,
                    "Element card lacks direct stock LEV placement proof: " + std::to_string(preset.registry_id));
            require(preset.source_path != "arm/kulisse.arm",
                    "marker-only kulisse helper leaked into game-ready Elements");
        }
    }
    for (uint32_t marker_id : {357U, 358U, 359U})
        require(!visible_ids.contains(marker_id), "nonvisual kulisse helper is still placeable");

    for (const auto& group : catalog.map_groups()) {
        const auto level = LevelAsset::parse(catalog.load(group.level_path));
        stock_placements += level.placements().size();
        const auto issues = catalog.validate_level_placements(level);
        require(issues.empty(), group.level_path + " contains a placement whose mounted ARM/MDL chain was not resolved");
        for (const auto& placement : level.placements()) {
            const auto* preset = catalog.army_preset(placement.army_asset_id);
            require(preset != nullptr, "stock placement ID missing from ARM registry");
            require(catalog.placement_available_in_level(*preset, level),
                    group.level_path + " cannot resolve stock ARM " + std::to_string(placement.army_asset_id));
        }
        if (group.level_path == "level/hansolo/s01_insel.lev") {
            const auto* campaign_building = catalog.army_preset(370U);
            require(campaign_building != nullptr &&
                    catalog.placement_available_in_level(*campaign_building, level),
                    "campaign building01 / mdl/01 alternative chain was not recognized");
            ++campaign_variant_checks;
        }
        if (group.level_path == "level/hansolo/s05_trigelb.lev") {
            const auto* cross_numbered = catalog.army_preset(310U);
            require(cross_numbered != nullptr &&
                    catalog.placement_available_in_level(*cross_numbered, level),
                    "building04 ARM with mdl/05 model family was not resolved by registry ID");
            ++campaign_variant_checks;
        }
    }

    require(campaign_variant_checks == 2U, "campaign variant regression maps were not scanned");
    require(visible_assets >= 250U && visible_elements >= 80U,
            "game-ready browser census unexpectedly collapsed");
    require(decoded_visible_sprites + 1U == visible_assets,
            "every visible non-start card must decode a nonempty SPR");

    std::cout << "G334_GAME_READY_OBJECT_TERRAIN_COMPATIBILITY PASS"
              << " maps=" << catalog.map_groups().size()
              << " stock_placements=" << stock_placements
              << " visible_assets=" << visible_assets
              << " visible_elements=" << visible_elements
              << " decoded_sprites=" << decoded_visible_sprites
              << " campaign_variants=" << campaign_variant_checks << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G334_GAME_READY_OBJECT_TERRAIN_COMPATIBILITY FAIL: " << error.what() << '\n';
    return 1;
}
