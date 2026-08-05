#include "thandor/catalog.hpp"
#include "thandor/model.hpp"

#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: g351_stock_linked_army_model_census GAME_ROOT\n";
        return 2;
    }
    const auto catalog = thandor::AssetCatalog::scan_game_root(argv[1]);
    std::size_t linked_unit_or_building = 0U;
    for (const auto& preset : catalog.army_presets()) {
        if ((preset.browser_kind == thandor::PlacementBrowserKind::Unit ||
             preset.browser_kind == thandor::PlacementBrowserKind::Building) &&
            preset.linked_model_runtime_count != 0U) ++linked_unit_or_building;
    }
    if (linked_unit_or_building < 100U)
        throw std::runtime_error("stock linked ARM model census unexpectedly small");
    const auto* kraftwerk = catalog.army_preset(310U);
    if (kraftwerk == nullptr) throw std::runtime_error("stock Power plant asset 310 missing");
    if (kraftwerk->linked_model_runtime_count != 4U ||
        kraftwerk->deferred_model_attachment_count != 4U ||
        kraftwerk->resolved_model_hierarchy.size() != 5U)
        throw std::runtime_error("Power plant linked generator hierarchy was not restored");
    std::size_t triangle_count = 0U;
    std::size_t generator_count = 0U;
    for (const auto& node : kraftwerk->resolved_model_hierarchy) {
        const auto sprite = thandor::SpriteAsset::parse(catalog.load(node.sprite_path));
        triangle_count += sprite.mesh().indices.size() / 3U;
        if (node.sprite_path == "spr/gebaeude/gkgen0.spr") ++generator_count;
    }
    if (triangle_count != 204U || generator_count != 4U)
        throw std::runtime_error("Power plant complete stock geometry census mismatch");
    std::cout << "linked unit/building presets=" << linked_unit_or_building
              << " power_plant_nodes=" << kraftwerk->resolved_model_hierarchy.size()
              << " power_plant_triangles=" << triangle_count << " PASS\n";
}
