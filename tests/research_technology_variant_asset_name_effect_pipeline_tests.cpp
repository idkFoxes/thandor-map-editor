#include "thandor/catalog.hpp"
#include "thandor/technology.hpp"

#include <algorithm>
#include <array>
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

const thandor::ArmyAssetPreset& preset(const thandor::AssetCatalog& catalog, uint32_t id) {
    const auto* result = catalog.army_preset(id);
    if (result == nullptr) throw std::runtime_error("missing ARM preset " + std::to_string(id));
    return *result;
}

const thandor::ModelTechnologyVariant* find_variant(
    const thandor::ArmyAssetPreset& asset, uint32_t model_definition_id) {
    for (const auto& group : asset.model_variant_groups)
        for (const auto& variant : group.variants)
            if (variant.model_definition_id == model_definition_id) return &variant;
    return nullptr;
}

bool panel_has(const thandor::ArmyAssetPreset& asset, uint32_t technology_id) {
    return std::any_of(asset.technology_panel_entries.begin(),
        asset.technology_panel_entries.end(), [&](const auto& entry) {
            return entry.technology_id == technology_id;
        });
}

bool technology_path_has(const thandor::ArmyAssetPreset& asset, uint32_t technology_id) {
    return asset.linked_technology_id == technology_id || panel_has(asset, technology_id);
}

const thandor::TechnologyPanelEntry* find_panel_entry(
    const thandor::ArmyAssetPreset& asset, uint32_t technology_id) {
    const auto found = std::find_if(asset.technology_panel_entries.begin(),
        asset.technology_panel_entries.end(), [&](const auto& entry) {
            return entry.technology_id == technology_id;
        });
    return found == asset.technology_panel_entries.end() ? nullptr : &*found;
}
} // namespace

int main(int argc, char** argv) try {
    std::vector<std::byte> synthetic(0x200U + 256U * 0x40U, std::byte{0});
    thandor::write_u32(synthetic, 0U, thandor::fourcc("tec"));
    thandor::write_u32(synthetic, 4U, static_cast<uint32_t>(synthetic.size()));
    const std::size_t record = 0x200U + 156U * 0x40U;
    thandor::write_u32(synthetic, record + 0x20U, 0x1234U);
    thandor::write_u32(synthetic, record + 0x24U, 0x5678U);
    thandor::write_u32(synthetic, record + 0x28U, 0x9ABCU);
    thandor::write_u32(synthetic, record + 0x2CU, 150U);
    thandor::write_u32(synthetic, record + 0x30U, 0x2FEU);
    thandor::write_u32(synthetic, record + 0x34U, 3U);
    thandor::write_u32(synthetic, record + 0x38U, 256U);
    const auto technology_asset = thandor::TechnologyAsset::parse(synthetic);
    const auto* rocket_ramp = technology_asset.record(156U);
    require(rocket_ramp != nullptr, "synthetic TEC record 156 is unavailable");
    require(rocket_ramp->xenite_cost_q4 == 0x1234U &&
            rocket_ramp->energy_demand_q4 == 0x5678U &&
            rocket_ramp->research_duration_q5 == 0x9ABCU,
            "TEC +0x20/+0x24/+0x28 fields were not parsed exactly");
    require(rocket_ramp->dependency_technology_id == 150U &&
            rocket_ramp->completion_message_resource_id == 0x2FEU &&
            rocket_ramp->category == thandor::TechnologyCategory::Category3,
            "TEC dependency/message/category fields were not parsed exactly");
    require(rocket_ramp->title_resource_id == 0x300138U &&
            rocket_ramp->detail_resource_id == 0x300139U,
            "technology title/detail resource mapping is not 0x300000 + id*2/+1");
    require(thandor::technology_panel_direction_from_serialized_index(21U) == 6U &&
            thandor::technology_panel_row_from_serialized_index(21U) == 2U &&
            thandor::technology_panel_direction_from_serialized_index(8U) == 0U &&
            thandor::technology_panel_row_from_serialized_index(8U) == 1U,
            "MDL technology-table indices 1..28 do not map to branch/row geometry exactly");

    const auto source = std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto catalog_source = read_text(source / "src/core/catalog.cpp");
    const auto editor_source = read_text(source / "src/editor/editor_app.cpp");
    const auto model_source = read_text(source / "src/core/model.cpp");
    require(catalog_source.find("base_arm_model_definition_id") != std::string::npos &&
            catalog_source.find("linked_arm_model_id") == std::string::npos,
            "ARM base-variant helper still uses the ambiguous old name");
    require(catalog_source.find("effect_definition_ids_by_sprite") != std::string::npos &&
            catalog_source.find("effect_ids_by_sprite") == std::string::npos,
            "sprite-alias EFF map still uses the ambiguous old name");
    require(model_source.find("visual.technology_unlock_id = read_u32(data, offset + 0x1C0U)") != std::string::npos &&
            model_source.find("visual.linked_technology_id = read_u32(data, offset + 0x1C4U)") != std::string::npos &&
            model_source.find("visual.technology_panel_ids[slot] = read_u32(data, offset + 0x1C8U + slot * 4U)") != std::string::npos,
            "MDL +0x1C0 unlock, +0x1C4 linked, and +0x1C8..+0x234 panel fields are not separated");
    require(editor_source.find("Research / model variants") != std::string::npos &&
            editor_source.find("preset.simulation_weapon_profiles") != std::string::npos &&
            editor_source.find("preset.effect_pipeline_validation") != std::string::npos &&
            editor_source.find("asset_search_text") != std::string::npos,
            "asset view does not expose/search the research and shot/effect chain");

    if (argc < 2) {
        std::cout << "G386_TECHNOLOGY_VARIANT_ASSET_NAME_EFFECT_PIPELINE synthetic PASS\n";
        return 0;
    }

    const auto catalog = thandor::AssetCatalog::scan_game_root(argv[1]);
    require(catalog.technologies().size() == 256U, "stock TEC table is not 256 records");
    require(catalog.technology(156U) != nullptr &&
            catalog.technology(156U)->title == "Rocket firing ramp",
            "stock technology title 156 was not loaded from texte/techno.str");

    const auto& weasel_basic = preset(catalog, 1U);
    const auto& weasel_laser = preset(catalog, 2U);
    const auto& weasel_unarmed = preset(catalog, 3U);
    require(weasel_basic.display_name == "Weasel Jeep · Basic weapon (SHT 14)" &&
            weasel_laser.display_name == "Weasel Jeep · Laser" &&
            weasel_unarmed.display_name == "Weasel Jeep · Unarmed",
            "duplicate Weasel records are not disambiguated by their exact ARM/MDL chain");
    const auto* laser_upgrade = find_variant(weasel_laser, 1224U);
    require(laser_upgrade != nullptr && laser_upgrade->technology_unlock_id == 91U &&
            laser_upgrade->technology_title == "Laser range +15%" &&
            !laser_upgrade->weapon_pipelines.empty() &&
            laser_upgrade->weapon_pipelines.front().shot_definition_id == 1000U,
            "technology-gated Weasel laser variant/pipeline was not retained");

    const auto& hornet = preset(catalog, 201U);
    require(hornet.display_name == "GLD-3 Hornet · Rocket battery + Rocket launcher",
            "two-attachment Hornet loadout name is not derived from both ARM children");
    require(hornet.model_variant_groups.size() == 3U,
            "Hornet root plus two attachment groups were not retained");

    const auto& laser_coil_structure = preset(catalog, 345U);
    require(laser_coil_structure.localized_name.empty() &&
            laser_coil_structure.name_text_resource_id ==
                thandor::army_asset_name_text_resource_id(29U) &&
            laser_coil_structure.display_name == "Laser coil" &&
            laser_coil_structure.short_name == "Laser coil",
            "empty stock help.str name did not fall back to the exact research attachment title");

    const auto& headquarters = preset(catalog, 300U);
    require(headquarters.linked_technology_id == 200U &&
            headquarters.linked_technology_title == "Small calibre turret," &&
            panel_has(headquarters, 203U) && panel_has(headquarters, 206U) &&
            panel_has(headquarters, 209U) && panel_has(headquarters, 212U),
            "headquarters linked/base technology and tower/resource branches are incomplete");
    require(headquarters.technology_panel_entries.size() == 16U &&
            headquarters.technology_branch_count == 6U,
            "headquarters 16-entry, six-branch tower/resource mapping changed");

    const auto& light_factory = preset(catalog, 301U);
    const auto& armour_factory = preset(catalog, 302U);
    const auto& walker_factory = preset(catalog, 303U);
    const auto& glider_factory = preset(catalog, 304U);
    const auto& airport = preset(catalog, 306U);
    require(light_factory.linked_technology_id == 1U &&
            light_factory.technology_panel_entries.size() == 8U &&
            light_factory.technology_branch_count == 4U &&
            armour_factory.linked_technology_id == 15U &&
            armour_factory.technology_panel_entries.size() == 8U &&
            armour_factory.technology_branch_count == 3U &&
            walker_factory.linked_technology_id == 30U &&
            walker_factory.technology_panel_entries.size() == 8U &&
            walker_factory.technology_branch_count == 3U &&
            glider_factory.linked_technology_id == 45U &&
            glider_factory.technology_panel_entries.size() == 11U &&
            glider_factory.technology_branch_count == 4U &&
            airport.linked_technology_id == 75U &&
            airport.technology_panel_entries.size() == 8U &&
            airport.technology_branch_count == 3U,
            "factory/airport production-line research mappings changed");

    const auto& special_lab = preset(catalog, 320U);
    const auto& laser_lab = preset(catalog, 321U);
    const auto& flak_lab = preset(catalog, 322U);
    const auto& rocket_lab = preset(catalog, 323U);
    require(special_lab.linked_technology_id == 0U &&
            special_lab.technology_panel_entries.size() == 15U &&
            special_lab.technology_branch_count == 5U &&
            panel_has(special_lab, 180U) && panel_has(special_lab, 186U) &&
            panel_has(special_lab, 189U) && panel_has(special_lab, 192U),
            "special research lab five attachment branches are missing");
    require(laser_lab.linked_technology_id == 90U &&
            laser_lab.technology_panel_entries.size() == 20U &&
            laser_lab.technology_branch_count == 7U &&
            technology_path_has(laser_lab, 90U) && panel_has(laser_lab, 99U) &&
            panel_has(laser_lab, 108U),
            "ion/laser research lab base technology and seven weapon branches are missing");
    require(flak_lab.linked_technology_id == 0U &&
            flak_lab.technology_panel_entries.size() == 21U &&
            flak_lab.technology_branch_count == 7U &&
            panel_has(flak_lab, 120U) && panel_has(flak_lab, 132U) &&
            panel_has(flak_lab, 138U),
            "flak-gun research lab seven weapon branches are missing");
    require(rocket_lab.linked_technology_id == 0U &&
            rocket_lab.technology_panel_entries.size() == 21U &&
            rocket_lab.technology_branch_count == 7U &&
            panel_has(rocket_lab, 150U) && panel_has(rocket_lab, 156U) &&
            panel_has(rocket_lab, 168U),
            "rocket/artillery research lab seven weapon branches are missing");
    require(laser_lab.technology_summary.find("7 branches · 3 populated rows") != std::string::npos &&
            flak_lab.technology_summary.find("7 branches · 3 populated rows") != std::string::npos &&
            rocket_lab.technology_summary.find("7 branches · 3 populated rows") != std::string::npos,
            "research-lab branch/row geometry is not shown");
    const auto* laser_range = find_panel_entry(laser_lab, 91U);
    const auto* laser_damage = find_panel_entry(laser_lab, 92U);
    const auto* ion_cannon = find_panel_entry(laser_lab, 108U);
    const auto* ion_cannon_damage = find_panel_entry(laser_lab, 110U);
    require(laser_range != nullptr && laser_range->serialized_index == 1U &&
            laser_range->direction_index == 0U && laser_range->row_index == 0U &&
            laser_damage != nullptr && laser_damage->serialized_index == 8U &&
            laser_damage->direction_index == 0U && laser_damage->row_index == 1U &&
            ion_cannon != nullptr && ion_cannon->serialized_index == 7U &&
            ion_cannon->direction_index == 6U && ion_cannon->row_index == 0U &&
            ion_cannon_damage != nullptr && ion_cannon_damage->serialized_index == 21U &&
            ion_cannon_damage->direction_index == 6U && ion_cannon_damage->row_index == 2U,
            "laser-lab serialized table indices do not preserve exact branch/row mapping");

    const auto& laser_turret = preset(catalog, 351U);
    const auto& rocket_turret = preset(catalog, 352U);
    require(laser_turret.display_name == "Large turret · Laser turret" &&
            rocket_turret.display_name == "Large turret · Rocket firing ramp",
            "headquarters tower records are not disambiguated by attachment technology");
    const auto* laser_variant = find_variant(laser_turret, 225U);
    const auto* rocket_variant = find_variant(rocket_turret, 213U);
    require(laser_variant != nullptr && !laser_variant->weapon_pipelines.empty() &&
            laser_variant->weapon_pipelines.front().shot_definition_id == 18U &&
            laser_variant->weapon_pipelines.front().trajectory_kind ==
                thandor::ShotTrajectoryMode::DirectLine &&
            laser_variant->weapon_pipelines.front().launch_effect.effect_definition_id == 76U &&
            laser_variant->weapon_pipelines.front().launch_attachment_count == 2U,
            "laser turret ARM->MDL->SPR(kind2)->SHT->EFF pipeline is incomplete");
    require(rocket_variant != nullptr && !rocket_variant->weapon_pipelines.empty() &&
            rocket_variant->weapon_pipelines.front().shot_definition_id == 20U &&
            rocket_variant->weapon_pipelines.front().trajectory_kind ==
                thandor::ShotTrajectoryMode::LeadAdjusted &&
            rocket_variant->weapon_pipelines.front().launch_effect.effect_definition_id == 83U &&
            rocket_variant->weapon_pipelines.front().launch_attachment_count == 3U,
            "rocket turret ARM->MDL->SPR(kind2)->SHT->EFF pipeline is incomplete");
    require(!laser_variant->weapon_pipelines.front().terrain_impact_effects.empty() &&
            !laser_variant->weapon_pipelines.front().target_class_impact_effects.empty() &&
            !rocket_variant->weapon_pipelines.front().terrain_impact_effects.empty() &&
            !rocket_variant->weapon_pipelines.front().target_class_impact_effects.empty(),
            "shot terrain/target impact EFF references were not exposed");
    require(!laser_variant->weapon_pipelines.front().launch_effect.sprite_path.empty() &&
            !rocket_variant->weapon_pipelines.front().launch_effect.sprite_path.empty() &&
            std::string(thandor::shot_trajectory_mode_name(
                laser_variant->weapon_pipelines.front().trajectory_kind)) == "direct line" &&
            std::string(thandor::shot_trajectory_mode_name(
                rocket_variant->weapon_pipelines.front().trajectory_kind)) == "lead adjusted",
            "typed SHT trajectory and EFF sprite/transition descriptions are missing");

    std::cout << "G386_TECHNOLOGY_VARIANT_ASSET_NAME_EFFECT_PIPELINE technologies="
              << catalog.technologies().size()
              << " headquartersPanel=" << headquarters.technology_panel_entries.size()
              << " laserLabPanel=" << laser_lab.technology_panel_entries.size()
              << " hornetGroups=" << hornet.model_variant_groups.size()
              << " PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G386 checks failed: " << error.what() << '\n';
    return 1;
}
