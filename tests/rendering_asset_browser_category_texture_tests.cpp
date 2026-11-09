#include "asset_preview.hpp"
#include "thandor/catalog.hpp"
#include "thandor/model.hpp"

#include <array>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <string>

namespace {

thandor::ArmyAssetPreset preset(std::string source,
                                thandor::PlacementVisualKind visual = thandor::PlacementVisualKind::Standard) {
    thandor::ArmyAssetPreset result;
    result.source_path = std::move(source);
    result.visual_kind = visual;
    result.browser_kind = thandor::classify_placement_browser_kind(result);
    return result;
}

void check_barycentric(editor::PreviewBarycentric weights) {
    const float sum = weights[0] + weights[1] + weights[2];
    assert(sum > 0.9999F && sum < 1.0001F);
    for (const float weight : weights) assert(weight >= -0.0001F && weight <= 1.0001F);
}

} // namespace

int main(int argc, char** argv) {
    const auto start = preset("arm/unit.arm", thandor::PlacementVisualKind::PlayerStart);
    const auto power = preset("arm/rohstoff.arm", thandor::PlacementVisualKind::PowerResource);
    const auto resource_scenery = preset("arm/rohstoff.arm", thandor::PlacementVisualKind::ResourceScenery);
    const auto unit = preset("arm/unit.arm");
    const auto building = preset("arm/building.arm");
    const auto ruin = preset("arm/ruinen.arm");
    const auto tree = preset("arm/nbaum.arm", thandor::PlacementVisualKind::Tree);

    assert(start.browser_kind == thandor::PlacementBrowserKind::Unit);
    assert(power.browser_kind == thandor::PlacementBrowserKind::General);
    assert(resource_scenery.browser_kind == thandor::PlacementBrowserKind::Element);
    assert(!thandor::placement_is_resource(resource_scenery));
    assert(thandor::placement_is_resource_scenery(resource_scenery));
    assert(unit.browser_kind == thandor::PlacementBrowserKind::Unit);
    assert(building.browser_kind == thandor::PlacementBrowserKind::Building);
    assert(ruin.browser_kind == thandor::PlacementBrowserKind::Element);
    assert(tree.browser_kind == thandor::PlacementBrowserKind::Element);
    assert(thandor::placement_uses_neutral_source_color(ruin));
    assert(thandor::placement_uses_neutral_source_color(tree));
    assert(!thandor::placement_uses_neutral_source_color(unit));
    assert(!thandor::placement_uses_neutral_source_color(building));

    // World-placement source-color policy remains separate from the retail asset-card preview
    // resolver. game.exe 0x00571DE6 uses faction color for registry IDs < 400 and neutral for 400+.
    assert(!thandor::army_asset_preview_uses_neutral_source_color(318U));
    assert(!thandor::army_asset_preview_uses_neutral_source_color(399U));
    assert(thandor::army_asset_preview_uses_neutral_source_color(400U));
    assert(thandor::army_asset_preview_uses_neutral_source_color(429U));

    assert(editor::asset_preview_texture_subdivision_count(100U, true) == 6U);
    assert(editor::asset_preview_texture_subdivision_count(1400U, true) == 3U);
    assert(editor::asset_preview_texture_subdivision_count(1400U, false) == 2U);
    for (uint32_t divisions = 1U; divisions <= 6U; ++divisions) {
        const auto triangles = editor::subdivide_asset_preview_triangle(divisions);
        assert(triangles.size() == static_cast<std::size_t>(divisions) * divisions);
        for (const auto& triangle : triangles) for (const auto corner : triangle.corners) {
            check_barycentric(corner);
        }
    }
    const std::array<editor::Vec3f, 3> positions{{
        {0.0F, 0.0F, 0.0F}, {3.0F, 0.0F, 0.0F}, {0.0F, 3.0F, 0.0F}}};
    const auto centroid = editor::interpolate_preview_position(positions, {1.0F / 3.0F, 1.0F / 3.0F, 1.0F / 3.0F});
    assert(centroid.x > 0.999F && centroid.x < 1.001F);
    assert(centroid.y > 0.999F && centroid.y < 1.001F);

    if (argc < 2) return 0;
    const auto catalog = thandor::AssetCatalog::scan_game_root(std::filesystem::path(argv[1]));
    std::map<thandor::PlacementBrowserKind, std::size_t> category_counts;
    std::size_t element_visuals = 0U;
    std::size_t element_marker_only = 0U;
    std::size_t element_textured_source_triangles = 0U;
    std::size_t element_untextured_source_triangles = 0U;
    std::size_t element_flat_material_visuals = 0U;
    for (const auto& item : catalog.army_presets()) {
        ++category_counts[item.browser_kind];
        assert(item.browser_kind == thandor::classify_placement_browser_kind(item));
        if (item.browser_kind != thandor::PlacementBrowserKind::Element) continue;
        assert(thandor::placement_uses_neutral_source_color(item));
        if (item.sprite_path.empty()) {
            ++element_marker_only;
            assert(item.source_path == "arm/kulisse.arm");
            assert(item.registry_id == 357U || item.registry_id == 358U || item.registry_id == 359U);
            continue;
        }
        ++element_visuals;
        const auto sprite = thandor::SpriteAsset::parse(catalog.load(item.sprite_path));
        const auto& mesh = sprite.mesh();
        assert(mesh.indices.size() % 3U == 0U);
        std::size_t textured = 0U;
        std::size_t untextured = 0U;
        for (std::size_t triangle = 0U; triangle < mesh.indices.size(); triangle += 3U) {
            const auto& first = mesh.vertices.at(mesh.indices[triangle]);
            const auto& second = mesh.vertices.at(mesh.indices[triangle + 1U]);
            const auto& third = mesh.vertices.at(mesh.indices[triangle + 2U]);
            assert(first.texture_subresource == second.texture_subresource);
            assert(first.texture_subresource == third.texture_subresource);
            if (first.texture_subresource == std::numeric_limits<uint32_t>::max()) ++untextured;
            else ++textured;
        }
        if (textured == 0U) {
            ++element_flat_material_visuals;
            assert(item.sprite_path == "spr/extras/ffkte0.spr" ||
                   item.sprite_path == "spr/extras/feffa0.spr");
            assert(item.registry_id == 429U || item.registry_id == 647U ||
                   item.registry_id == 854U || item.registry_id == 855U);
            assert(untextured == 12U);
        }
        element_textured_source_triangles += textured;
        element_untextured_source_triangles += untextured;
    }

    assert(category_counts[thandor::PlacementBrowserKind::General] == 4U);
    assert(category_counts[thandor::PlacementBrowserKind::Element] == 148U);
    assert(category_counts[thandor::PlacementBrowserKind::Unit] == 131U);
    assert(category_counts[thandor::PlacementBrowserKind::Building] == 43U);
    assert(element_visuals == 145U);
    assert(element_marker_only == 3U);
    assert(element_flat_material_visuals == 4U);
    assert(element_textured_source_triangles == 3298U);
    assert(element_untextured_source_triangles == 48U);

    std::cout << "G324_ASSET_CATEGORY_TEXTURE_CENSUS PASS"
              << " general=" << category_counts[thandor::PlacementBrowserKind::General]
              << " elements=" << category_counts[thandor::PlacementBrowserKind::Element]
              << " units=" << category_counts[thandor::PlacementBrowserKind::Unit]
              << " buildings=" << category_counts[thandor::PlacementBrowserKind::Building]
              << " element_visuals=" << element_visuals
              << " marker_only=" << element_marker_only
              << " flat_material_visuals=" << element_flat_material_visuals
              << " textured_source_triangles=" << element_textured_source_triangles
              << " untextured_source_triangles=" << element_untextured_source_triangles << '\n';
    return 0;
}
