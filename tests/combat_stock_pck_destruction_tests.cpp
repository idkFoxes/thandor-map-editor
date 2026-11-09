#include "thandor/catalog.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

using namespace thandor;
namespace {
void require(bool valid, const char* why) {
    if (!valid) throw std::runtime_error(why);
}
const ModelVisualNode* root(const ArmyAssetPreset& preset) {
    const auto& hierarchy = preset.resolved_model_hierarchy.empty()
        ? preset.model_hierarchy : preset.resolved_model_hierarchy;
    for (const auto& node : hierarchy)
        if (node.parent_index == std::numeric_limits<uint32_t>::max()) return &node;
    return nullptr;
}
}

int main(int argc, char** argv) try {
    if (argc != 2) {
        std::cerr << "usage: combat_stock_pck_destruction_tests STOCK_PCK_DIRECTORY\n";
        return 2;
    }
    const auto catalog = AssetCatalog::scan_archive_root(argv[1]);
    std::size_t buildings = 0, replacement_buildings = 0;
    std::size_t buildings_with_terminal_effects = 0, four_plus_effects = 0;
    std::set<int32_t> wreck_ids;
    for (const auto& preset : catalog.army_presets()) {
        if (preset.browser_kind != PlacementBrowserKind::Building) continue;
        ++buildings;
        const auto* model = root(preset);
        require(model != nullptr, "stock building lacks a resolved root MDL");
        std::size_t effect_channels = 0;
        for (const auto effect : model->simulation.terminal_attachment_effect_ids)
            if (effect != 0U) ++effect_channels;
        if (effect_channels != 0U) ++buildings_with_terminal_effects;
        if (effect_channels >= 4U) ++four_plus_effects;
        const auto wreck = model->simulation.destroyed_replacement_army_asset_id;
        if (wreck < 0) continue;
        ++replacement_buildings;
        wreck_ids.insert(wreck);
        const auto* replacement = catalog.army_preset(static_cast<uint32_t>(wreck));
        require(replacement != nullptr, "stock destroyed-replacement ARM cannot be resolved");
        const auto* replacement_root = root(*replacement);
        require(replacement_root != nullptr && replacement_root->runtime_class_id == 12U,
                "stock destruction replacement does not use ruin class 12");
    }
    require(buildings == 43U, "unexpected stock building census");
    require(replacement_buildings == 32U, "stock replacement mapping changed");
    require(buildings_with_terminal_effects == 43U && four_plus_effects == 8U,
            "stock terminal-effect channel census changed");
    require(wreck_ids == std::set<int32_t>{400, 401, 402, 403},
            "stock wreck ARM IDs changed; investigate before inventing four-stage variants");
    for (const auto [building, wreck] : std::array<std::pair<uint32_t, int32_t>, 4>{
             std::pair{300U, 402}, {345U, 403}, {331U, 401}, {390U, 400}}) {
        const auto* preset = catalog.army_preset(building);
        require(preset && root(*preset) &&
                    root(*preset)->simulation.destroyed_replacement_army_asset_id == wreck,
                "named stock building -> wreck mapping changed");
    }
    for (uint32_t wreck = 400; wreck <= 403; ++wreck) {
        const auto* preset = catalog.army_preset(wreck);
        require(preset != nullptr && root(*preset) != nullptr,
                "stock class-12 ruin definition missing");
        require(root(*preset)->simulation.class_step_q12 == -1,
                "stock ruin vertical sinking step changed");
    }
    std::cout << "V720_STOCK_DESTRUCTION_PCK PASS buildings=" << buildings
              << " replacement_buildings=" << replacement_buildings
              << " wreck_ids=400..403 four_plus_effect_channels=" << four_plus_effects << '\n';
} catch (const std::exception& error) {
    std::cerr << "V720_STOCK_DESTRUCTION_PCK FAIL: " << error.what() << '\n';
    return 1;
}
