#include "thandor/field_raycast.hpp"
#include "thandor/gfx.hpp"
#include "thandor/model.hpp"
#include "thandor/semantic_types.hpp"
#include "viewport_picking.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open source file: " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    using namespace thandor;
    static_assert(sizeof(GraphicsSubresourceIndex) == 4U);
    static_assert(sizeof(PaletteBankIndex) == 4U);
    static_assert(sizeof(ModelLookupKeyIndex) == 4U);
    static_assert(sizeof(ModelLookupKeyClass) == 4U);
    static_assert(sizeof(AngleTurn32) == 4U);
    static_assert(!std::is_convertible_v<PaletteBankIndex, GraphicsSubresourceIndex>);
    static_assert(!std::is_convertible_v<uint32_t, GraphicsSubresourceIndex>);

    require(pack_model_lookup_key(ModelLookupKeyIndex{7U}, ModelLookupKeyClass{6U}) == 0x76U,
            "SPR lookup key did not preserve class | (index << 4)");
    const SpriteLookupRecord lookup{0x43U, {1.0F, 2.0F, 3.0F}};
    require(lookup.key_class().value() == 3U && lookup.key_index().value() == 4U,
            "SPR lookup axes were reversed");
    require(runtime_angle_from_serialized_turn16(0x12345678U).value() == 0x5678U,
            "serialized turn16 conversion did not mask at the runtime boundary");

    auto field = FieldGrid::create(64U, 64U, 0, 0U);
    const auto quad_center = [&](uint32_t x, uint32_t y, float z) {
        const auto& a = field.at(x, y);
        const auto& b = field.at(x + 1U, y + 1U);
        return Vec3f{(q12_to_float(a.world_x_q12) + q12_to_float(b.world_x_q12)) * 0.5F,
                     (q12_to_float(a.world_y_q12) + q12_to_float(b.world_y_q12)) * 0.5F, z};
    };
    const Vec3f vertical_origin = quad_center(20U, 21U, 12.0F);
    const auto flat_hit = raycast_field_surface_compatibility(
        field, vertical_origin, {0.0F, 0.0F, -1.0F}, 40.0F,
        FieldRaycastSurface::Terrain);
    require(flat_hit.has_value(), "vertical compatibility field ray missed flat terrain");
    require(std::abs(flat_hit->position.z) <= 1.0e-4F,
            "flat terrain ray returned the wrong height");
    require(flat_hit->visited_cells == 1U,
            "vertical compatibility ray should test exactly one terrain quad");

    const Vec3f dry_secondary_origin = quad_center(10U, 10U, -2.0F);
    const auto dry_secondary_hit = raycast_field_surface_compatibility(
        field, dry_secondary_origin, {0.0F, 0.0F, 1.0F}, 8.0F,
        FieldRaycastSurface::Secondary);
    require(dry_secondary_hit.has_value() && dry_secondary_hit->secondary_surface,
            "secondary ray did not preserve the signed dry-surface contract");
    require(std::abs(dry_secondary_hit->position.z + 1.0F) <= 1.0e-4F,
            "secondary ray clamped the dry sentinel instead of adding it");

    field.at(20U, 21U).water_delta_q12 = 0x2000;
    field.at(20U, 22U).water_delta_q12 = 0x2000;
    field.at(21U, 21U).water_delta_q12 = 0x2000;
    field.at(21U, 22U).water_delta_q12 = 0x2000;
    const auto top_hit = raycast_field_surface_compatibility(
        field, vertical_origin, {0.0F, 0.0F, -1.0F}, 40.0F,
        FieldRaycastSurface::Topmost);
    require(top_hit.has_value() && top_hit->secondary_surface,
            "topmost compatibility ray did not select the water surface");
    require(std::abs(top_hit->position.z - 2.0F) <= 1.0e-4F,
            "water/top-surface compatibility ray returned the wrong height");

    const Vec3f diagonal_target = quad_center(48U, 48U, 0.0F);
    const Vec3f diagonal_origin{diagonal_target.x - 14.0F,
                                diagonal_target.y + 9.0F, 15.0F};
    const Vec3f diagonal_direction{diagonal_target.x - diagonal_origin.x,
                                   diagonal_target.y - diagonal_origin.y,
                                   diagonal_target.z - diagonal_origin.z};
    const auto diagonal_hit = raycast_field_surface_compatibility(
        field, diagonal_origin, diagonal_direction, 200.0F,
        FieldRaycastSurface::Terrain);
    require(diagonal_hit.has_value(), "bounded grid traversal missed a diagonal terrain ray");
    require(diagonal_hit->visited_cells <= 0x400U,
            "compatibility field ray exceeded the binary-proven traversal ceiling");
    require(diagonal_hit->visited_cells < field.width() * field.height(),
            "compatibility field ray regressed to a complete-grid scan");

    auto large_field = FieldGrid::create(139U, 139U, 0, 0U);
    const auto& camera_a = large_field.at(69U, 69U);
    const auto& camera_b = large_field.at(70U, 70U);
    const Vec3f benchmark_origin{
        (q12_to_float(camera_a.world_x_q12) + q12_to_float(camera_b.world_x_q12)) * 0.5F + 0.013F,
        (q12_to_float(camera_a.world_y_q12) + q12_to_float(camera_b.world_y_q12)) * 0.5F + 0.019F,
        80.0F};
    uint64_t benchmark_visited = 0U;
    uint32_t benchmark_max = 0U;
    uint32_t benchmark_rays = 0U;
    for (uint32_t y = 10U; y + 1U < 139U; y += 16U) {
        for (uint32_t x = 10U; x + 1U < 139U; x += 16U) {
            const auto& target_a = large_field.at(x, y);
            const auto& target_b = large_field.at(x + 1U, y + 1U);
            const Vec3f target{(q12_to_float(target_a.world_x_q12) +
                                q12_to_float(target_b.world_x_q12)) * 0.5F,
                               (q12_to_float(target_a.world_y_q12) +
                                q12_to_float(target_b.world_y_q12)) * 0.5F, -10.0F};
            const Vec3f direction{target.x - benchmark_origin.x,
                                  target.y - benchmark_origin.y,
                                  target.z - benchmark_origin.z};
            const auto hit = raycast_field_surface_compatibility(
                large_field, benchmark_origin, direction, 1000.0F,
                FieldRaycastSurface::Terrain);
            if (!hit) throw std::runtime_error(
                "large-field compatibility ray missed flat target x=" + std::to_string(x) +
                " y=" + std::to_string(y));
            benchmark_visited += hit->visited_cells;
            benchmark_max = std::max(benchmark_max, hit->visited_cells);
            ++benchmark_rays;
        }
    }
    require(benchmark_max <= 0x400U, "large-field census exceeded the traversal ceiling");
    const uint64_t full_grid_quads =
        static_cast<uint64_t>(large_field.width() - 1U) * (large_field.height() - 1U);
    require(benchmark_visited < full_grid_quads * benchmark_rays,
            "large-field census did not reduce the complete-grid candidate count");
    std::cout << "G389_RAYCAST_CENSUS rays=" << benchmark_rays
              << " visitedCells=" << benchmark_visited
              << " averageVisited="
              << static_cast<double>(benchmark_visited) / static_cast<double>(benchmark_rays)
              << " maximumVisited=" << benchmark_max
              << " oldFullGridQuadsPerRay=" << full_grid_quads << "\n";

    editor::AxisAlignedBounds left;
    left.include({-2.0F, -1.0F, -1.0F});
    left.include({-1.0F, 1.0F, 1.0F});
    editor::AxisAlignedBounds right;
    right.include({1.0F, -1.0F, -1.0F});
    right.include({2.0F, 1.0F, 1.0F});
    editor::AxisAlignedBounds aggregate;
    aggregate.include(left.minimum);
    aggregate.include(right.maximum);
    const editor::ViewRay gap_ray{{0.0F, -5.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};
    require(editor::ray_bounds_distance(gap_ray, aggregate).has_value(),
            "test aggregate AABB should be hit through its empty center");
    require(!editor::ray_transformed_bounds_distance(
                gap_ray, editor::TransformedBoundsHull::from_axis_aligned(left)) &&
            !editor::ray_transformed_bounds_distance(
                gap_ray, editor::TransformedBoundsHull::from_axis_aligned(right)),
            "per-node hull picking selected empty space inside an aggregate AABB");

#ifdef THANDOR_SOURCE_DIR
    const std::filesystem::path source_root = THANDOR_SOURCE_DIR;
    const auto viewport = read_text(source_root / "src/editor/viewport_picking.hpp");
    const auto editor_source = read_text(source_root / "src/editor/editor_app.cpp");
    const auto shadow_source = read_text(source_root / "src/core/terrain_shadow.cpp");
    const auto model_header = read_text(source_root / "include/thandor/model.hpp");
    require(viewport.find("raycast_field_surface_compatibility") != std::string::npos &&
            viewport.find("for (uint32_t y = 0; y + 1U < field.height(); ++y)") == std::string::npos,
            "viewport picking did not replace the complete-grid triangle scan");
    require(editor_source.find("placement_pick_hulls_") != std::string::npos &&
            editor_source.find("ray_transformed_bounds_distance") != std::string::npos,
            "placement selection did not add hierarchy-node hull testing");
    require(editor_source.find("simulation_line_of_fire_clear") != std::string::npos &&
            editor_source.find("constexpr uint32_t kSamples=64U") == std::string::npos &&
            editor_source.find("FieldRaycastSurface::Terrain,0x400U") != std::string::npos,
            "simulation line of fire still uses the fixed 64-sample terrain approximation");
    require(shadow_source.find("raycast_field_surface_compatibility") != std::string::npos &&
            shadow_source.find("coarse_steps = 48U") == std::string::npos,
            "generated-shadow receivers still use the coarse sample/binary-search ray");
    require(model_header.find("struct SpriteLookupRecord") != std::string::npos &&
            model_header.find("find_lookup_record(ModelLookupKeyIndex") != std::string::npos,
            "SPR +0xE4/+0xE8 records remain attachment-only in the public API");
#endif

    std::cout << "G389 V338 raycast, lookup-key, and semantic-domain regression PASS\n";
    return 0;
}
