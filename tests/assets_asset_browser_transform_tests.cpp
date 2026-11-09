#include "asset_preview.hpp"
#include "thandor/field.hpp"

#include <cassert>
#include <cmath>
#include <numbers>

namespace {
bool close(float left, float right, float epsilon = 0.001F) {
    return std::abs(left - right) <= epsilon;
}
}

int main() {
    // Fixed card sizing must produce deterministic equal-width rows while the enclosing child owns
    // vertical scrolling.
    assert(editor::asset_card_column_count(360.0F, 174.0F, 8.0F) == 2);
    assert(editor::asset_card_column_count(170.0F, 174.0F, 8.0F) == 1);

    editor::AssetPreviewMesh mesh{};
    editor::AssetPreviewTriangle triangle{{{
        {-1.0F, -1.0F, 0.0F}, {1.0F, -1.0F, 0.0F}, {0.0F, 1.0F, 1.0F}}}, 0xFF80C0FFU};
    mesh.triangles.push_back(triangle);
    for (const auto point : triangle.positions) editor::include_preview_bounds(mesh, point);
    const auto projected = editor::project_asset_preview(mesh, 160.0F, 120.0F);
    assert(projected.size() == 1U);
    for (const auto point : projected.front().points) {
        assert(point.x >= 0.0F && point.x <= 160.0F);
        assert(point.y >= 0.0F && point.y <= 120.0F);
    }

    assert(close(editor::point_segment_distance({5.0F, 3.0F}, {0.0F, 0.0F}, {10.0F, 0.0F}), 3.0F));
    assert(close(editor::axis_drag_world_delta({0.0F, 0.0F}, {20.0F, 0.0F},
                                               {0.0F, 0.0F}, {100.0F, 0.0F}, 5.0F), 1.0F));
    assert(editor::add_turn16_delta(0U, std::numbers::pi_v<float> * 0.5F, false) == 0x4000U);
    const float fourteen_degrees = 14.0F * std::numbers::pi_v<float> / 180.0F;
    assert(editor::add_turn16_delta(0U, fourteen_degrees, true) == 0x0AABU);

    const auto gizmo = editor::project_transform_gizmo({0.0F, 0.0F, 0.5F}, editor::identity(),
                                                        1000.0F, 800.0F, 0.1F);
    assert(gizmo.visible);
    assert(gizmo.x_axis.x > gizmo.origin.x);
    assert(gizmo.y_axis.y > gizmo.origin.y);
    assert(editor::rotation_ring_distance(gizmo.rotation_ring[10], gizmo.rotation_ring) < 0.01F);

    // LEV records have no independent object Z field. The blue Z handle therefore moves the four
    // supporting FLD vertices and preserves the selected water policy so the change survives stock
    // game export.
    auto field = thandor::FieldGrid::create(4U, 4U, 0, 0U);
    const auto anchor = field.at(1U, 1U);
    const int32_t water_surface = thandor::float_to_q12(3.0F);
    for (uint32_t y = 1U; y <= 2U; ++y) {
        for (uint32_t x = 1U; x <= 2U; ++x) {
            field.at(x, y).water_delta_q12 = water_surface;
        }
    }
    field.offset_terrain_anchor(thandor::q12_to_float(anchor.world_x_q12),
                                thandor::q12_to_float(anchor.world_y_q12), 1.25F,
                                thandor::TerrainWaterEditPolicy::PreserveAbsoluteSurface);
    for (uint32_t y = 0U; y < field.height(); ++y) {
        for (uint32_t x = 0U; x < field.width(); ++x) {
            const bool support = x >= 1U && x <= 2U && y >= 1U && y <= 2U;
            assert(field.at(x, y).terrain_height_q12 ==
                   (support ? thandor::float_to_q12(1.25F) : 0));
            if (support) {
                assert(field.at(x, y).terrain_height_q12 + field.at(x, y).water_delta_q12 ==
                       water_surface);
            }
        }
    }

    return 0;
}
