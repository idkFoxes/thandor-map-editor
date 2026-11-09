#include "thandor/binary.hpp"
#include "thandor/catalog.hpp"
#include "thandor/level.hpp"

#include <algorithm>
#include <array>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <vector>

namespace {

std::vector<std::byte> make_level_fixture() {
    std::vector<std::byte> bytes(0x340U);
    thandor::write_u32(bytes, 0x00U, thandor::fourcc("lev"));
    thandor::write_u32(bytes, 0x04U, static_cast<uint32_t>(bytes.size()));
    thandor::write_u32(bytes, 0xD8U, 0U);
    thandor::write_u32(bytes, 0xDCU, static_cast<uint32_t>(bytes.size()));
    for (std::size_t index = 0U; index < 10U; ++index) {
        thandor::write_u32(bytes, 0xB0U + index * 4U,
                           static_cast<uint32_t>(0x318U + index * 4U));
    }
    for (uint32_t faction = 1U; faction <= thandor::LevelAsset::kFactionCameraStartCount; ++faction) {
        const std::size_t offset = 0x200U + static_cast<std::size_t>(faction - 1U) * 0x20U;
        thandor::write_i32(bytes, offset + 0x00U, static_cast<int32_t>(faction * 0x1000U));
        thandor::write_i32(bytes, offset + 0x04U, -static_cast<int32_t>(faction * 0x2000U));
        thandor::write_i32(bytes, offset + 0x08U, static_cast<int32_t>(faction * 0x3000U));
        thandor::write_u32(bytes, offset + 0x0CU, 0x1000U);
        const uint16_t heading = static_cast<uint16_t>(faction * 0x1111U);
        const int16_t pitch = static_cast<int16_t>(-static_cast<int32_t>(faction) * 0x0400);
        thandor::write_u32(bytes, offset + 0x10U,
            static_cast<uint32_t>(heading) |
            (static_cast<uint32_t>(static_cast<uint16_t>(pitch)) << 16U));
        thandor::write_u32(bytes, offset + 0x14U, 0xA0000000U + faction);
        thandor::write_u32(bytes, offset + 0x18U, 0xB0000000U + faction);
        thandor::write_u32(bytes, offset + 0x1CU, 0xC0000000U + faction);
    }
    return bytes;
}

bool is_inert_resource_id(uint32_t id) {
    static const std::set<uint32_t> ids{800U, 801U, 802U, 803U, 805U, 806U, 807U, 808U,
                                        850U, 851U, 852U, 853U, 854U, 855U};
    return ids.contains(id);
}

} // namespace

int main(int argc, char** argv) {
    auto fixture = make_level_fixture();
    auto level = thandor::LevelAsset::parse(fixture);
    for (uint32_t faction = 1U; faction <= thandor::LevelAsset::kFactionCameraStartCount; ++faction) {
        auto camera = level.faction_camera_start(faction);
        assert(camera.position_x_q12 == static_cast<int32_t>(faction * 0x1000U));
        assert(camera.position_y_q12 == -static_cast<int32_t>(faction * 0x2000U));
        assert(camera.position_z_q12 == static_cast<int32_t>(faction * 0x3000U));
        assert(camera.motion_magnitude_q12 == 0x1000U);
        const std::size_t offset = 0x200U + static_cast<std::size_t>(faction - 1U) * 0x20U;
        const uint32_t resource_a = thandor::read_u32(fixture, offset + 0x14U);
        const uint32_t resource_b = thandor::read_u32(fixture, offset + 0x18U);
        const uint32_t mode = thandor::read_u32(fixture, offset + 0x1CU);
        camera.position_x_q12 += 0x123;
        camera.heading_turn16 = static_cast<uint16_t>(camera.heading_turn16 + 7U);
        camera.pitch_turn16 = static_cast<int16_t>(camera.pitch_turn16 - 9);
        level.set_faction_camera_start(faction, camera);
        const auto serialized = level.serialize();
        assert(thandor::read_u32(serialized, offset + 0x14U) == resource_a);
        assert(thandor::read_u32(serialized, offset + 0x18U) == resource_b);
        assert(thandor::read_u32(serialized, offset + 0x1CU) == mode);
        level = thandor::LevelAsset::parse(serialized);
        const auto reloaded = level.faction_camera_start(faction);
        assert(reloaded.position_x_q12 == camera.position_x_q12);
        assert(reloaded.heading_turn16 == camera.heading_turn16);
        assert(reloaded.pitch_turn16 == camera.pitch_turn16);
    }

    if (argc < 2) {
        std::cout << "G333_CAMERA_RECORD_FIXTURE PASS\n";
        return 0;
    }

    const auto catalog = thandor::AssetCatalog::scan_game_root(std::filesystem::path(argv[1]));
    const auto* start = catalog.army_preset(50U);
    assert(start != nullptr);
    assert(start->source_path == "arm/unit.arm");
    assert(start->model_definition_id == 103U);
    assert(start->runtime_model_class_id == 18U);
    assert(start->visual_kind == thandor::PlacementVisualKind::PlayerStart);
    assert(start->browser_kind == thandor::PlacementBrowserKind::Unit);
    assert(start->browser_visible);
    assert(start->game_placeable);
    assert(!thandor::placement_uses_neutral_source_color(*start));

    const auto check_active = [&](uint32_t id, thandor::PlacementVisualKind kind,
                                  uint32_t effect, uint32_t canonical, bool visible) {
        const auto* item = catalog.army_preset(id);
        assert(item != nullptr);
        assert(item->source_path == "arm/rohstoff.arm");
        assert(item->runtime_model_class_id == 4U);
        assert(item->visual_kind == kind);
        assert(item->browser_kind == thandor::PlacementBrowserKind::General);
        assert(item->timed_effect_id == effect);
        assert(thandor::placement_is_resource(*item));
        assert(thandor::canonical_placement_registry_id(*item) == canonical);
        assert(item->browser_visible == visible);
    };
    check_active(810U, thandor::PlacementVisualKind::PowerResource, 114U, 811U, false);
    check_active(811U, thandor::PlacementVisualKind::PowerResource, 114U, 811U, true);
    check_active(814U, thandor::PlacementVisualKind::BuildingResource, 116U, 815U, false);
    check_active(815U, thandor::PlacementVisualKind::BuildingResource, 116U, 815U, true);

    std::size_t visible_general = 0U;
    for (const auto& item : catalog.army_presets()) {
        if (item.browser_kind == thandor::PlacementBrowserKind::General && item.browser_visible) {
            ++visible_general;
        }
        if (!is_inert_resource_id(item.registry_id)) continue;
        assert(item.visual_kind == thandor::PlacementVisualKind::ResourceScenery);
        assert(item.browser_kind == thandor::PlacementBrowserKind::Element);
        assert(!thandor::placement_is_resource(item));
        assert(thandor::placement_is_resource_scenery(item));
        assert(item.timed_effect_id != 114U && item.timed_effect_id != 116U);
    }
    assert(visible_general == 2U); // one card for each buildable resource type; pioneer is a unit

    std::map<uint32_t, std::size_t> placement_counts;
    std::map<uint32_t, std::size_t> extractor_overlaps;
    std::size_t camera_records_checked = 0U;
    std::size_t camera_start_pairs = 0U;
    double direct_camera_distance_sum = 0.0;
    double swapped_camera_distance_sum = 0.0;
    for (const auto& group : catalog.map_groups()) {
        const auto stock_level = thandor::LevelAsset::parse(catalog.load(group.level_path));
        const auto placements = stock_level.placements();
        for (uint32_t faction = 1U; faction <= thandor::LevelAsset::kFactionCameraStartCount; ++faction) {
            const auto camera = stock_level.faction_camera_start(faction);
            ++camera_records_checked;
            const auto start = std::find_if(placements.begin(), placements.end(), [&](const auto& placement) {
                return placement.army_asset_id == 50U && placement.faction_index == faction;
            });
            if (start == placements.end()) continue;
            direct_camera_distance_sum += std::hypot(
                static_cast<double>(camera.position_x()) - start->render_x(),
                static_cast<double>(camera.position_y()) - start->render_y());
            swapped_camera_distance_sum += std::hypot(
                static_cast<double>(camera.position_y()) - start->render_x(),
                static_cast<double>(camera.position_x()) - start->render_y());
            ++camera_start_pairs;
        }
        for (const auto& placement : placements) {
            ++placement_counts[placement.army_asset_id];
            const uint32_t extractor = (placement.army_asset_id == 810U || placement.army_asset_id == 811U)
                ? 332U : ((placement.army_asset_id == 814U || placement.army_asset_id == 815U) ? 330U : 0U);
            if (extractor == 0U) continue;
            for (const auto& other : placements) {
                if (other.army_asset_id != extractor) continue;
                const float dx = placement.render_x() - other.render_x();
                const float dy = placement.render_y() - other.render_y();
                if (std::hypot(dx, dy) <= 1.2F) {
                    ++extractor_overlaps[placement.army_asset_id];
                    break;
                }
            }
        }
    }
    assert(camera_records_checked == catalog.map_groups().size() * 7U);
    assert(camera_start_pairs > 50U);
    assert(direct_camera_distance_sum * 3.0 < swapped_camera_distance_sum);
    assert(placement_counts[811U] > placement_counts[810U]);
    assert(placement_counts[815U] > placement_counts[814U]);
    assert(extractor_overlaps[811U] > 50U);
    assert(extractor_overlaps[815U] > 50U);

    std::cout << "G333_RESOURCE_CAMERA_CLASSIFICATION PASS"
              << " visible_general=" << visible_general
              << " start_class=" << start->runtime_model_class_id
              << " energy_counts=" << placement_counts[810U] << '/' << placement_counts[811U]
              << " building_counts=" << placement_counts[814U] << '/' << placement_counts[815U]
              << " extractor_overlaps=" << extractor_overlaps[811U] << '/'
              << extractor_overlaps[815U]
              << " camera_records=" << camera_records_checked
              << " camera_start_pairs=" << camera_start_pairs
              << " direct_vs_swapped_distance=" << direct_camera_distance_sum << '/'
              << swapped_camera_distance_sum << '\n';
    return 0;
}
