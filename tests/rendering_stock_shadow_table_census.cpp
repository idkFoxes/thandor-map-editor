#include "thandor/catalog.hpp"
#include "thandor/model.hpp"

#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::cout << "G356 stock shadow-table census: SKIP (no game root)\n";
            return 0;
        }
        auto catalog = thandor::AssetCatalog::scan_archive_root(argv[1]);
        std::set<std::string> tree_paths;
        std::size_t tree_assets = 0U;
        std::size_t shadow_triangles = 0U;
        std::size_t soft_triangles = 0U;
        std::size_t hard_triangles = 0U;
        std::size_t mixed_assets = 0U;
        std::size_t missing_shadow_tables = 0U;
        for (const auto& preset : catalog.army_presets()) {
            if (preset.visual_kind != thandor::PlacementVisualKind::Tree ||
                preset.sprite_path.empty() || !tree_paths.insert(preset.sprite_path).second) continue;
            const auto sprite = thandor::SpriteAsset::parse(catalog.load(preset.sprite_path));
            const std::size_t soft = sprite.generated_shadow_soft_triangle_count();
            const std::size_t hard = sprite.generated_shadow_hard_triangle_count();
            ++tree_assets;
            shadow_triangles += soft + hard;
            soft_triangles += soft;
            hard_triangles += hard;
            if (soft != 0U && hard != 0U) ++mixed_assets;
            if (!sprite.has_generated_shadow_mesh()) ++missing_shadow_tables;
        }
        check(tree_assets == 54U, "stock unique tree SPR count changed");
        check(shadow_triangles == 867U, "stock dedicated tree-shadow triangle count changed");
        check(soft_triangles == 414U, "stock dedicated soft-shadow triangle count changed");
        check(hard_triangles == 453U, "stock dedicated hard-shadow triangle count changed");
        check(mixed_assets == 31U, "stock mixed soft/hard tree-shadow count changed");
        check(missing_shadow_tables == 9U,
              "stock tree assets without a dedicated +0xEC shadow table changed");
        std::cout << "G356_STOCK_SHADOW_TABLE_CENSUS PASS tree_assets=" << tree_assets
                  << " shadow_triangles=" << shadow_triangles
                  << " soft=" << soft_triangles << " hard=" << hard_triangles
                  << " mixed=" << mixed_assets
                  << " missing=" << missing_shadow_tables << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "G356 stock census failure: " << error.what() << '\n';
        return 1;
    }
}
