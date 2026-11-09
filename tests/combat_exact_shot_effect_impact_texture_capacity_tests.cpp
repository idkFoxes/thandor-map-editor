#include "thandor/catalog.hpp"
#include "thandor/effect.hpp"
#include "thandor/field.hpp"
#include "thandor/model.hpp"
#include "thandor/simulation.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read " + path.string());
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}
void require_token(const std::string& text, std::string_view token) {
    if (text.find(token) == std::string::npos)
        throw std::runtime_error("missing G378 source contract: " + std::string(token));
}
void reject_token(const std::string& text, std::string_view token) {
    if (text.find(token) != std::string::npos)
        throw std::runtime_error("rejected G378 regression returned: " + std::string(token));
}
void validate_effect_tree(const thandor::AssetCatalog& catalog, uint32_t effect_id,
                          std::unordered_set<uint32_t>& visited_effects,
                          std::unordered_set<uint32_t>& visited_shots) {
    if (effect_id == 0U || !visited_effects.insert(effect_id).second) return;
    const auto* effect = catalog.effect_definition(effect_id);
    if (!effect) throw std::runtime_error("missing EFF " + std::to_string(effect_id));
    (void)thandor::SpriteAsset::parse(catalog.load(effect->sprite_path));
    if (effect->linked_effect_present != 0U)
        validate_effect_tree(catalog, effect->linked_effect_id, visited_effects, visited_shots);
    validate_effect_tree(catalog, effect->periodic_effect_id, visited_effects, visited_shots);
    if (effect->linked_shot_present != 0U &&
        visited_shots.insert(effect->linked_shot_id).second) {
        const auto* shot = catalog.shot_definition(effect->linked_shot_id);
        if (!shot) throw std::runtime_error("missing EFF-linked SHT");
        (void)thandor::SpriteAsset::parse(catalog.load(shot->sprite_path));
        validate_effect_tree(catalog, shot->launch_effect_id, visited_effects, visited_shots);
        validate_effect_tree(catalog, shot->primary_effect_id, visited_effects, visited_shots);
        validate_effect_tree(catalog, shot->secondary_effect_id, visited_effects, visited_shots);
        for (uint32_t id : shot->terrain_impact_effect_ids)
            validate_effect_tree(catalog, id, visited_effects, visited_shots);
        for (uint32_t id : shot->target_impact_effect_ids)
            validate_effect_tree(catalog, id, visited_effects, visited_shots);
    }
}
}

int main(int argc, char** argv) try {
    using namespace thandor;
    const auto source = std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto editor = read_text(source / "src/editor/editor_app.cpp");
    const auto dependencies = read_text(source / "include/thandor/render_texture_dependencies.hpp");
    const auto simulation = read_text(source / "src/core/simulation.cpp");
    const auto effect = read_text(source / "src/core/effect.cpp");
    const auto model = read_text(source / "src/core/model.cpp");
    const auto catalog_source = read_text(source / "src/core/catalog.cpp");
    const auto vulkan = read_text(source / "src/editor/vulkan_context.cpp");
    const auto renderer = read_text(source / "src/editor/terrain_renderer.cpp");

    require_token(effect, "offset + 0x90 + class_index * 4U");
    require_token(effect, "offset + 0x14 + effect_index * 4U");
    require_token(effect, "offset + 0x48");
    require_token(effect, "offset + 0x4C");
    require_token(editor, "SimulationImpactKind::TargetModelImpactClass");
    require_token(editor, "target_impact_effect_id(*target_impact_class)");
    require_token(editor, "target_preset->shot_impact_class_index");
    require_token(editor, "surface_is_nearer");
    require_token(model, "read_u32(data, offset + 0x5CU)");
    require_token(model, "node.shot_impact_class_index = visual.shot_impact_class_index");
    require_token(catalog_source, "preset.shot_impact_class_index = visual.shot_impact_class_index");
    require_token(editor, "g_ArmyGraphicsBindings[0]");
    require_token(editor, "texture_layer(0U,0U,subresource)");
    require_token(editor, "RenderTextureDependencies texture_dependencies");
    require_token(dependencies, "if(definition->linked_shot_present)shot(definition->linked_shot_id)");
    require_token(editor, "append_linked_shot_visual_recursive");
    require_token(editor, "build_linked_shot_emissions(definition)");
    require_token(editor, "simulation_create_volley(0U,synthetic_weapon");
    require_token(editor, "EFF/SHT preview expansion exceeded 4096 descendants; remaining children skipped");
    require_token(editor, "shot.animation_frame_count");
    require_token(editor, "fixed_model_rotation_basis(0U,0x4000U,0U)");
    require_token(editor, "shot_model_transform(projectile.impact_position");
    require_token(simulation, "std::max(projectile.lifetime_ticks,impact_end)");
    require_token(simulation, "simulation_resolve_projectile_surface_impact");
    require_token(vulkan, "limits.maxImageArrayLayers");
    require_token(renderer, "simulation_validate_texture_layer_capacity");
    reject_token(editor, "constexpr std::size_t kMaximumTextureLayers=256");
    reject_token(editor, "std::array<bool,9> present");
    reject_token(editor, "target_impact_class=target_preset->runtime_model_class_id");
    reject_token(editor, "segment_count=std::clamp");
    reject_token(editor, "if(mesh.indices.empty())return false;");
    reject_token(renderer, "std::min<std::size_t>(images.size(), maximum_layers)");

    EffectDefinitionRecord invisible_controller;
    invisible_controller.transition_kind = 1U;
    invisible_controller.runtime_value_0c = 24U;
    invisible_controller.frame_advance_threshold_q4 = 15U;
    invisible_controller.linked_shot_present = 1U;
    invisible_controller.linked_shot_id = 8U;
    const auto invisible_emissions = build_linked_shot_emissions(invisible_controller);
    if (invisible_emissions.size() != 23U ||
        invisible_emissions.front().parent_age_tick != 1U ||
        invisible_emissions.back().parent_age_tick != 23U ||
        invisible_emissions.back().attachment_index != 22U)
        throw std::runtime_error("transition-1 linked-shot frame cadence/terminal unlink changed");

    if (simulation_validate_texture_layer_capacity(511U, 2048U) != 511U)
        throw std::runtime_error("actual Vulkan image-array capacity was not accepted");
    bool capacity_rejected = false;
    try { (void)simulation_validate_texture_layer_capacity(257U, 256U); }
    catch (const std::runtime_error&) { capacity_rejected = true; }
    if (!capacity_rejected) throw std::runtime_error("real device layer overflow was not rejected");

    SimulationProjectileState retained;
    retained.spawn_tick = 10U;
    retained.travel_ticks = 4U;
    retained.lifetime_ticks = 1000U;
    retained.impact_kind = SimulationImpactKind::TerrainSurface;
    retained.impact_visual_lifetime_ticks = 6U;
    if (simulation_projectile_expired(retained, 20U) ||
        simulation_projectile_expired(retained, 1009U) ||
        !simulation_projectile_expired(retained, 1010U))
        throw std::runtime_error("SHT lifetime was truncated by the shorter impact EFF lifetime");

    ShotDefinitionRecord dry_shot;
    dry_shot.trajectory_mode = 0;
    dry_shot.primary_effect_id = 901U;
    dry_shot.terrain_impact_effect_ids.fill(777U);
    auto dry_field = FieldGrid::create(59U, 59U, 0, 7U);
    const auto& dry_cell = dry_field.at(5U, 5U);
    const float dry_x = q12_to_float(dry_cell.world_x_q12);
    const float dry_y = q12_to_float(dry_cell.world_y_q12);
    SimulationProjectileState dry_projectile;
    dry_projectile.launch_position = {dry_x, dry_y, 2.0F};
    dry_projectile.target_position = {dry_x, dry_y, -1.0F};
    dry_projectile.travel_ticks = 1U;
    simulation_resolve_projectile_surface_impact(dry_projectile, dry_shot, dry_field);
    if (dry_projectile.impact_kind != SimulationImpactKind::TerrainSurface ||
        dry_projectile.impact_effect_id != 777U ||
        dry_projectile.impact_material_index != 7U ||
        std::abs(dry_projectile.impact_position.z) > 1.0e-5F)
        throw std::runtime_error("dry terrain ray impact did not use SHT +0x14 material effect");

    auto wet_field = FieldGrid::create(59U, 59U, 0, 3U);
    wet_field.at(5U, 5U).water_delta_q12 = 0x1000;
    const auto& wet_cell = wet_field.at(5U, 5U);
    const float wet_x = q12_to_float(wet_cell.world_x_q12);
    const float wet_y = q12_to_float(wet_cell.world_y_q12);
    SimulationProjectileState wet_projectile;
    wet_projectile.launch_position = {wet_x, wet_y, 2.0F};
    wet_projectile.target_position = {wet_x, wet_y, 0.0F};
    wet_projectile.travel_ticks = 1U;
    simulation_resolve_projectile_surface_impact(wet_projectile, dry_shot, wet_field);
    if (wet_projectile.impact_kind != SimulationImpactKind::SecondarySurface ||
        wet_projectile.impact_effect_id != 901U ||
        std::abs(wet_projectile.impact_position.z - 1.0F) > 1.0e-4F)
        throw std::runtime_error("secondary-surface ray impact did not use SHT primary EFF");

    if (argc < 2) {
        std::cout << "G378_EXACT_SHOT_EFFECT_IMPACT_TEXTURE_CAPACITY PASS source-only\n";
        return 0;
    }

    const auto catalog = AssetCatalog::scan_game_root(argv[1]);
    const auto* rail = catalog.shot_definition(2U);
    const auto* laser = catalog.shot_definition(18U);
    const auto* rocket = catalog.shot_definition(20U);
    if (!rail || !laser || !rocket) throw std::runtime_error("stock SHT 2/18/20 missing");
    if (rail->trajectory_mode != 3 || rail->launch_effect_id != 70U ||
        rail->terrain_impact_effect_id(0U) != 3U ||
        rail->target_impact_effect_id(7U) != 32U)
        throw std::runtime_error("stock railgun SHT 2 mapping changed");
    if (laser->trajectory_mode != 0 || laser->launch_effect_id != 76U ||
        laser->terrain_impact_effect_id(0U) != 2U ||
        laser->target_impact_effect_id(7U) != 36U)
        throw std::runtime_error("stock laser SHT 18 mapping changed");
    if (rocket->trajectory_mode != 3 || rocket->launch_effect_id != 83U ||
        rocket->secondary_effect_id != 102U || rocket->secondary_effect_interval_ticks != 2U ||
        rocket->terrain_impact_effect_id(0U) != 53U ||
        rocket->target_impact_effect_id(7U) != 31U)
        throw std::runtime_error("stock rocket SHT 20 mapping changed");

    std::size_t attack_placements = 0U;
    std::map<uint32_t, std::size_t> impact_class_counts;
    std::size_t weapon_profiles = 0U;
    std::unordered_set<uint32_t> visited_effects;
    std::unordered_set<uint32_t> visited_shots;
    for (const auto& preset : catalog.army_presets()) {
        if (preset.simulation_weapon_profiles.empty()) continue;
        ++attack_placements;
        ++impact_class_counts[preset.shot_impact_class_index];
        for (const auto& weapon : preset.simulation_weapon_profiles) {
            ++weapon_profiles;
            const auto* shot = catalog.shot_definition(weapon.shot_definition_id);
            if (!shot) throw std::runtime_error("attack profile references missing SHT");
            visited_shots.insert(shot->definition_id);
            (void)SpriteAsset::parse(catalog.load(shot->sprite_path));
            validate_effect_tree(catalog, shot->launch_effect_id, visited_effects, visited_shots);
            validate_effect_tree(catalog, shot->primary_effect_id, visited_effects, visited_shots);
            validate_effect_tree(catalog, shot->secondary_effect_id, visited_effects, visited_shots);
            for (uint32_t id : shot->terrain_impact_effect_ids)
                validate_effect_tree(catalog, id, visited_effects, visited_shots);
            for (uint32_t id : shot->target_impact_effect_ids)
                validate_effect_tree(catalog, id, visited_effects, visited_shots);
        }
    }
    if (attack_placements != 125U || weapon_profiles != 160U)
        throw std::runtime_error("stock attack census changed");
    const std::map<uint32_t, std::size_t> expected_impact_classes{
        {1U,19U},{2U,20U},{3U,24U},{4U,20U},{5U,2U},{6U,21U},{7U,19U}};
    if (impact_class_counts != expected_impact_classes)
        throw std::runtime_error("MDL +0x5C shot-impact class census changed");
    const auto* hornet = catalog.army_preset(221U);
    const auto* laser_turret = catalog.army_preset(351U);
    if (!hornet || hornet->runtime_model_class_id != 17U ||
        hornet->shot_impact_class_index != 4U)
        throw std::runtime_error("Hornet MDL +0x4C/+0x5C distinction was lost");
    if (!laser_turret || laser_turret->runtime_model_class_id != 4U ||
        laser_turret->shot_impact_class_index != 7U)
        throw std::runtime_error("turret MDL +0x4C/+0x5C distinction was lost");

    const auto* effect53 = catalog.effect_definition(53U);
    const auto* effect23 = catalog.effect_definition(23U);
    const auto* effect31 = catalog.effect_definition(31U);
    const auto* effect33 = catalog.effect_definition(33U);
    const auto* effect82 = catalog.effect_definition(82U);
    if (!effect53 || effect53->transition_kind != 1U ||
        effect53->linked_effect_present != 1U || effect53->linked_effect_id != 23U)
        throw std::runtime_error("rocket terrain impact EFF 53 -> EFF 23 chain changed");
    if (!effect31 || !effect33 || effect31->linked_effect_id != 90U ||
        effect33->linked_effect_id != 90U || effect31->linked_effect_present != 1U ||
        effect33->linked_effect_present != 1U)
        throw std::runtime_error("target impact EFF 31/33 -> EFF 90 chain changed");
    const auto validate_invisible_shot_controller = [&](const EffectDefinitionRecord* controller,
                                                         uint32_t shot_id,
                                                         std::size_t attachment_count) {
        if (!controller || controller->transition_kind != 1U ||
            controller->linked_shot_present != 1U || controller->linked_shot_id != shot_id)
            throw std::runtime_error("stock invisible EFF linked-shot controller changed");
        const auto sprite = SpriteAsset::parse(catalog.load(controller->sprite_path));
        if (!sprite.mesh().indices.empty())
            throw std::runtime_error("stock invisible EFF controller unexpectedly has triangles");
        const auto count = std::count_if(sprite.attachment_records().begin(),
            sprite.attachment_records().end(), [](const SpriteAttachmentRecord& record) {
                return record.kind() == 2U;
            });
        if (static_cast<std::size_t>(count) != attachment_count)
            throw std::runtime_error("stock invisible EFF type-2 attachment count changed");
    };
    validate_invisible_shot_controller(effect23, 8U, 3U);
    validate_invisible_shot_controller(effect82, 22U, 8U);

    std::cout << "G378_EXACT_SHOT_EFFECT_IMPACT_TEXTURE_CAPACITY PASS attackPlacements="
              << attack_placements << " weaponProfiles=" << weapon_profiles
              << " shots=" << visited_shots.size() << " effects=" << visited_effects.size()
              << " impactClasses=1:19,2:20,3:24,4:20,5:2,6:21,7:19\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G378_EXACT_SHOT_EFFECT_IMPACT_TEXTURE_CAPACITY FAIL: " << error.what() << '\n';
    return 1;
}
