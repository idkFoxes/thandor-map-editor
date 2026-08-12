#include "thandor/catalog.hpp"
#include "thandor/fog_preview.hpp"
#include "thandor/map_document.hpp"
#include "thandor/model.hpp"
#include "thandor/simulation.hpp"
#include "thandor/simulation_render_policy.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <limits>
#include <span>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

const thandor::ArmyAssetPreset& require_preset(const thandor::AssetCatalog& catalog,
                                               uint32_t id) {
    const auto* result = catalog.army_preset(id);
    if (result == nullptr) throw std::runtime_error("missing stock ARM preset");
    return *result;
}

bool has_class10_radar_children(const thandor::ArmyAssetPreset& preset) {
    const auto hierarchy = preset.resolved_model_hierarchy.empty()
        ? std::span<const thandor::ModelVisualNode>(preset.model_hierarchy)
        : std::span<const thandor::ModelVisualNode>(preset.resolved_model_hierarchy);
    for (std::size_t parent = 0; parent < hierarchy.size(); ++parent) {
        const auto& node = hierarchy[parent];
        if (node.runtime_class_id != 10U) continue;
        bool child0 = false;
        bool child1 = false;
        for (const auto& child : hierarchy) {
            if (child.parent_index != parent || !child.creates_runtime_node()) continue;
            child0 = child0 || child.child_slot == 0U;
            child1 = child1 || child.child_slot == 1U;
        }
        if (child0 && child1 && node.simulation.yaw_max_velocity_turn16 == 192 &&
            node.simulation.yaw_acceleration_turn16 == -192)
            return true;
    }
    return false;
}
} // namespace

int main(int argc, char** argv) try {
    const float quarter = thandor::normalize_model_texture_offset_u(0x40000, 16U, 16U);
    const float negative_quarter = thandor::normalize_model_texture_offset_u(-0x40000, 16U, 16U);
    require(std::abs(quarter - 0.25F) < 1.0e-6F,
            "track fixed-coordinate offset did not normalize to the 0x100000 period");
    require(std::abs(negative_quarter + 0.25F) < 1.0e-6F,
            "signed track fixed-coordinate offset lost its direction");

    thandor::SimulationRenderPolicy radar_policy{false, true, false, false, false, true};
    require(radar_policy.dynamic_replacement_enabled(false, false),
            "continuous radar was incorrectly gated by animation or simulation controls");
    require((radar_policy.packed_static_control_bits(false) &
             thandor::kObjectVertexContinuousRadarBit) != 0U,
            "continuous radar static-replacement bit is missing");

    auto field = thandor::FieldGrid::create(59U, 59U);
    const auto& center = field.at(29U, 29U);
    const thandor::FogPreviewObserver observer{
        {thandor::q12_to_float(center.world_x_q12),
         thandor::q12_to_float(center.world_y_q12), 0.0F},
        2.0F};
    const auto visibility = thandor::build_radial_fog_preview_visibility(
        field, std::span<const thandor::FogPreviewObserver>(&observer, 1U));
    const auto visible_count = std::count(visibility.begin(), visibility.end(), uint8_t{1});
    require(visible_count > 0 && visible_count < static_cast<std::ptrdiff_t>(visibility.size()),
            "fog preview did not produce a bounded current-visibility region");
    require(thandor::fog_preview_point_visible(
                std::span<const thandor::FogPreviewObserver>(&observer, 1U), observer.position),
            "fog observer did not reveal its own position");

    if (argc >= 2) {
        const auto catalog = thandor::AssetCatalog::scan_game_root(argv[1]);
        for (const uint32_t id : {1U, 60U, 80U, 100U, 140U}) {
            const auto& asset = require_preset(catalog, id);
            const auto root_group = std::find_if(asset.model_variant_groups.begin(),
                asset.model_variant_groups.end(), [](const auto& group) { return group.depth == 0U; });
            require(root_group != asset.model_variant_groups.end() && root_group->variants.size() >= 3U,
                    "stock multi-level unit lost its ARM technology variants");
            thandor::FactionTechnologyMask256 none{};
            uint32_t base_root = 0U;
            const auto base_hierarchy = thandor::resolve_faction_technology_hierarchy(
                asset, none, &base_root);
            require(!base_hierarchy.empty() && base_root == root_group->variants.front().model_definition_id,
                    "zero research mask did not retain the first serialized ARM model");
            thandor::FactionTechnologyMask256 all{}; all.fill(0xFFFFFFFFU);
            uint32_t researched_root = 0U;
            const auto researched_hierarchy = thandor::resolve_faction_technology_hierarchy(
                asset, all, &researched_root);
            require(!researched_hierarchy.empty(),
                    "all-research mask produced no faction-resolved model hierarchy");
            require(researched_root != 0U, "all-research mask produced no root model definition");
        }

        const auto map_group = std::find_if(catalog.map_groups().begin(),
                                            catalog.map_groups().end(),
            [](const thandor::MapAssetGroup& item) {
                return item.level_path == "level/asgard.lev";
            });
        require(map_group != catalog.map_groups().end(),
                "stock map fixture for technology metadata round trip is missing");
        auto document = thandor::MapDocument::open(catalog, *map_group);
        require(!document.placement_tags().empty(),
                "stock map fixture has no placement metadata row");
        const auto lev_before_metadata = document.level().serialize();
        thandor::set_faction_technology_unlocked(document.faction_technology_mask(1U), 141U, true);
        document.placement_tags().front().preview_integrity_percent = 37U;
        require(document.level().serialize() == lev_before_metadata,
                "editor-only faction research/damage preview mutated serialized LEV placement bytes");
        const auto project_path = std::filesystem::temp_directory_path() /
                                  "thandor_g393_technology_metadata_roundtrip.thmap";
        std::error_code remove_error;
        std::filesystem::remove(project_path, remove_error);
        document.save_project(project_path);
        const auto reopened = thandor::MapDocument::open_project(catalog, project_path);
        require(!reopened.placement_tags().empty(),
                "technology metadata project round trip lost placement rows");
        require(thandor::faction_technology_unlocked(reopened.faction_technology_mask(1U), 141U) &&
                    reopened.placement_tags().front().preview_integrity_percent == 37U,
                "faction research mask and damage integrity did not round trip through .thmap");
        std::filesystem::remove(project_path, remove_error);

        std::size_t tanks = 0U;
        for (const auto& asset : catalog.army_presets()) {
            if (asset.runtime_model_class_id != 2U ||
                asset.browser_kind != thandor::PlacementBrowserKind::Unit)
                continue;
            ++tanks;
            require(asset.primary_animated_subresource == 20U &&
                        asset.secondary_animated_subresource == 98U,
                    "stock tank track texture selectors changed");
            require(asset.track_texture_travel_scale == 259,
                    "stock tank texture-travel multiplier changed");
        }
        require(tanks == 24U, "stock tank census changed");

        for (const uint32_t id : {104U, 284U, 340U}) {
            const auto& radar = require_preset(catalog, id);
            require(radar.has_continuous_radar_subnode_animation,
                    "stock radar asset was not classified as a continuous class-10 callback");
            require(has_class10_radar_children(radar),
                    "stock radar linked hierarchy lost its +192/-192 child rotation pair");
        }

        const auto& ordinary_turret = require_preset(catalog, 352U);
        require(!ordinary_turret.has_continuous_radar_subnode_animation,
                "ordinary weapon turret was misclassified as a continuous radar");
        require(!ordinary_turret.simulation_weapon_profiles.empty(),
                "ordinary weapon turret lost its target-driven weapon profile");
        const auto& weapon = ordinary_turret.simulation_weapon_profiles.front();
        require(thandor::simulation_model_node_is_weapon_aim_axis(
                    ordinary_turret.simulation_weapon_profiles,
                    weapon.yaw_model_node_index),
                "weapon yaw axis is no longer protected from ambient rotation");
        if (weapon.pitch_model_node_index != std::numeric_limits<uint32_t>::max())
            require(thandor::simulation_model_node_is_weapon_aim_axis(
                        ordinary_turret.simulation_weapon_profiles,
                        weapon.pitch_model_node_index),
                    "weapon pitch axis is no longer protected from ambient rotation");
    }

    std::cout << "G393_BUILD_TECHNOLOGY_TRACKS_RADAR_FOG PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G393_BUILD_TECHNOLOGY_TRACKS_RADAR_FOG FAIL: " << error.what() << '\n';
    return 1;
}
