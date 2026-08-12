#include "model_transform.hpp"
#include "thandor/catalog.hpp"
#include "thandor/effect.hpp"
#include "thandor/model.hpp"
#include "thandor/simulation.hpp"

#include <algorithm>
#include <bit>
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
    std::ostringstream text; text << stream.rdbuf(); return text.str();
}
void require_token(const std::string& text, std::string_view token) {
    if (text.find(token) == std::string::npos)
        throw std::runtime_error("missing G377 source contract: " + std::string(token));
}
void reject_token(const std::string& text, std::string_view token) {
    if (text.find(token) != std::string::npos)
        throw std::runtime_error("rejected G377 heuristic returned: " + std::string(token));
}
void validate_effect_recursive(const thandor::AssetCatalog& catalog, uint32_t effect_id,
                               std::unordered_set<uint32_t>& effects,
                               std::unordered_set<uint32_t>& shots) {
    if (effect_id == 0U || !effects.insert(effect_id).second) return;
    const auto* effect = catalog.effect_definition(effect_id);
    if (!effect) throw std::runtime_error("missing referenced EFF " + std::to_string(effect_id));
    const auto sprite = thandor::SpriteAsset::parse(catalog.load(effect->sprite_path));
    (void)sprite;
    validate_effect_recursive(catalog, effect->linked_effect_id, effects, shots);
    if (effect->linked_shot_id != 0U && shots.insert(effect->linked_shot_id).second) {
        const auto* shot = catalog.shot_definition(effect->linked_shot_id);
        if (!shot) throw std::runtime_error("missing EFF-linked SHT");
        (void)thandor::SpriteAsset::parse(catalog.load(shot->sprite_path));
        validate_effect_recursive(catalog, shot->launch_effect_id, effects, shots);
        validate_effect_recursive(catalog, shot->secondary_effect_id, effects, shots);
        validate_effect_recursive(catalog, shot->primary_effect_id, effects, shots);
        for (const auto id : shot->terrain_impact_effect_ids)
            validate_effect_recursive(catalog, id, effects, shots);
    }
}
}

int main(int argc, char** argv) try {
    using namespace thandor;
    const auto source = std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto editor = read_text(source / "src/editor/editor_app.cpp");
    const auto editor_h = read_text(source / "src/editor/editor_app.hpp");
    const auto renderer = read_text(source / "src/editor/terrain_renderer.cpp");
    const auto renderer_h = read_text(source / "src/editor/terrain_renderer.hpp");
    const auto simulation = read_text(source / "src/core/simulation.cpp");
    const auto catalog_source = read_text(source / "src/core/catalog.cpp");
    const auto transforms = read_text(source / "src/editor/model_transform.hpp");

    require_token(catalog_source, "profile.runtime_class_id = hierarchy[yaw_node_index].runtime_class_id");
    require_token(catalog_source, "profile.runtime_class_id == 9U");
    require_token(editor, "last_fired_attachment_selector");
    require_token(editor, "launch_mesh_group_mask");
    require_token(editor, "simulation_shot_frame_index(*shot,age)");
    require_token(editor, "simulation_mode3_model_pitch_turn16");
    require_token(simulation, "for (uint32_t tick = 0U; tick < age; ++tick)");
    require_token(simulation, "Retail mode 3 accelerates along the direct launch vector");
    require_token(simulation, "weapon.launch_attachment_selector_mask&(1U<<selector)");
    require_token(simulation, "state.next_launch_attachment=(selector+1U)%count");
    reject_token(simulation, "std::sin(t*std::numbers::pi_v<float>)");
    require_token(transforms, "class17_banked_root_transform");
    require_token(editor, "class17_bank_heading_turn16");
    require_token(editor_h, "struct DynamicShadowTemplate");
    require_token(editor, "dynamic_shadow_templates_");
    require_token(editor, "terrain_height_near(world_x,world_y)+receiver_offset");
    require_token(renderer_h, "dynamic_shadow_vertex_buffers_");
    require_token(renderer, "dynamic_shadow_index_counts_[frame_index]");
    require_token(renderer, "VK_PIPELINE_BIND_POINT_GRAPHICS,shadow_pipeline_");

    SimulationActorState class17;
    class17.body_yaw_turn16 = 0x2000U;
    class17.class17_bank_heading_turn16 = 0U;
    class17.class17_rotor_deploy_ticks_remaining = 24U;
    for (uint32_t tick = 0; tick < 24U; ++tick) simulation_tick_class17_pose(class17, true);
    if (class17.class17_rotor_tilt_turn16 != 0x3FF0U ||
        class17.class17_bank_angle_turn16 != 0x3800U)
        throw std::runtime_error("class-17 deployment/bank recurrence changed");
    if (class17.class17_bank_heading_turn16 != 0x2000U)
        throw std::runtime_error("class-17 heading did not follow by exact 0x400 clamps");
    simulation_tick_class17_pose(class17, false);
    if (class17.class17_bank_angle_turn16 != 0x3840U)
        throw std::runtime_error("class-17 stopped bank recovery is not +0x40");
    const auto neutral = editor::class17_banked_root_transform({0,0,0}, 0U, 0U, 0x4000U);
    const auto banked = editor::class17_banked_root_transform({0,0,0}, 0U, 0U, 0x3800U);
    if (std::abs(neutral.basis[8] - 1.0F) > 1.0e-5F || banked.basis[8] >= 0.999F)
        throw std::runtime_error("class-17 root bank transform is neutral or malformed");

    SimulationAxisState signed_pitch_state{0U,0};
    const auto down = simulation_target_angles({0,0,2},{10,0,0});
    if (static_cast<int16_t>(down.pitch_turn16) >= 0)
        throw std::runtime_error("downward target angle was not encoded as signed turn16");
    simulation_step_pitch(signed_pitch_state, down.pitch_turn16, 0x300, 0x80, -0x2000, 0x3000);
    simulation_step_pitch(signed_pitch_state, down.pitch_turn16, 0x300, 0x80, -0x2000, 0x3000);
    if (static_cast<int16_t>(signed_pitch_state.angle_turn16) >= 0)
        throw std::runtime_error("downward target pitch was treated as unsigned/upward");

    if (argc < 2) {
        std::cout << "G377_ATTACK_RUNTIME_ROTOR_SHADOW_PIPELINE PASS source-only\n";
        return 0;
    }

    const auto catalog = AssetCatalog::scan_game_root(argv[1]);
    std::size_t attackable=0, units=0, buildings=0, profiles=0, class17_count=0;
    std::map<uint32_t,std::size_t> weapon_classes, shot_modes, class17_effects;
    std::unordered_set<uint32_t> effects, shots;
    for (const auto& preset : catalog.army_presets()) {
        if (preset.runtime_model_class_id == 17U) {
            ++class17_count;
            ++class17_effects[preset.timed_effect_id];
            if (preset.timed_effect_interval_ticks != 4U ||
                preset.timed_effect_random_ticks != 0U ||
                !catalog.effect_definition(preset.timed_effect_id))
                throw std::runtime_error("class-17 file-backed rotor-wash schedule changed");
            validate_effect_recursive(catalog, preset.timed_effect_id, effects, shots);
        }
        if (preset.simulation_weapon_profiles.empty()) continue;
        ++attackable;
        if (preset.browser_kind == PlacementBrowserKind::Unit) ++units;
        else if (preset.browser_kind == PlacementBrowserKind::Building) ++buildings;
        for (const auto& weapon : preset.simulation_weapon_profiles) {
            ++profiles; ++weapon_classes[weapon.runtime_class_id];
            const auto* shot = catalog.shot_definition(weapon.shot_definition_id);
            if (!shot) throw std::runtime_error("attack profile references missing SHT");
            ++shot_modes[static_cast<uint32_t>(shot->trajectory_mode)];
            shots.insert(shot->definition_id);
            (void)SpriteAsset::parse(catalog.load(shot->sprite_path));
            validate_effect_recursive(catalog, shot->launch_effect_id, effects, shots);
            validate_effect_recursive(catalog, shot->secondary_effect_id, effects, shots);
            validate_effect_recursive(catalog, shot->primary_effect_id, effects, shots);
            for (const auto id : shot->terrain_impact_effect_ids)
                validate_effect_recursive(catalog, id, effects, shots);
            if (weapon.runtime_class_id == 9U) {
                if (weapon.launch_policy != SimulationLaunchPolicy::SequentialKind2Attachments ||
                    weapon.inter_shot_ticks == 0U || weapon.launch_attachment_selector_mask == 0U ||
                    std::popcount(weapon.launch_attachment_selector_mask) !=
                        static_cast<int>(weapon.launch_attachment_count))
                    throw std::runtime_error("linked MDL class-9 selector/reload contract changed");
            }
        }
    }
    if (attackable!=125U || units!=106U || buildings!=19U || profiles!=160U ||
        class17_count!=26U || class17_effects[132U]!=3U || class17_effects[134U]!=9U ||
        class17_effects[133U]!=9U || class17_effects[141U]!=5U ||
        weapon_classes[7U]!=105U || weapon_classes[8U]!=8U ||
        weapon_classes[9U]!=44U || weapon_classes[20U]!=1U || weapon_classes[21U]!=2U ||
        shot_modes[0U]!=19U || shot_modes[1U]!=8U || shot_modes[2U]!=1U || shot_modes[3U]!=132U)
        throw std::runtime_error("complete attackable unit/building census changed");

    const auto* id187 = catalog.army_preset(187U);
    const auto* id242 = catalog.army_preset(242U);
    const auto* id351 = catalog.army_preset(351U);
    const auto* id352 = catalog.army_preset(352U);
    const auto* id360 = catalog.army_preset(360U);
    const auto* id221 = catalog.army_preset(221U);
    if (!id187 || !id242 || !id351 || !id352 || !id360 || !id221)
        throw std::runtime_error("critical ARM id missing");
    const auto& w351=id351->simulation_weapon_profiles.at(0);
    const auto& w352=id352->simulation_weapon_profiles.at(0);
    const auto& w360=id360->simulation_weapon_profiles.at(0);
    if (w351.shot_definition_id!=18U || w351.launch_attachment_count!=2U ||
        catalog.shot_definition(18U)->trajectory_mode!=0)
        throw std::runtime_error("ARM 351 laser/ray profile changed");
    if (w352.runtime_class_id!=9U || w352.shot_definition_id!=20U ||
        w352.launch_attachment_count!=3U || w352.reload_ticks!=400U ||
        w352.inter_shot_ticks!=53U || w352.launch_attachment_selector_mask!=0x7U)
        throw std::runtime_error("ARM 352 three-rocket rack profile changed");
    SimulationActorState rocket_actor;
    rocket_actor.weapons.resize(1U);
    auto& rocket_state=rocket_actor.weapons.front();
    const auto* rocket_shot=catalog.shot_definition(20U);
    const Vec3f target{5,0,0};
    const auto rocket_aim=simulation_shot_launch_angles({0,0,0},target,*rocket_shot);
    rocket_state.yaw.angle_turn16=rocket_aim.yaw_turn16;
    rocket_state.pitch.angle_turn16=rocket_aim.pitch_turn16;
    auto first=simulation_tick_weapon(rocket_actor,rocket_state,w352,*rocket_shot,{0,0,0},&target,true);
    if(!first.fired||first.fired_attachment_selector!=0U||(rocket_state.launch_mesh_group_mask&1U)!=0U)
        throw std::runtime_error("ARM 352 did not fire/hide selector 0 first");
    for(uint32_t tick=1;tick<w352.inter_shot_ticks;++tick) {
        const auto held=simulation_tick_weapon(rocket_actor,rocket_state,w352,*rocket_shot,{0,0,0},&target,true);
        if(held.fired)
            throw std::runtime_error("ARM 352 ignored its shared inter-shot gate");
    }
    auto second=simulation_tick_weapon(rocket_actor,rocket_state,w352,*rocket_shot,{0,0,0},&target,true);
    if(!second.fired||second.fired_attachment_selector!=1U)
        throw std::runtime_error("ARM 352 did not advance to selector 1 after its shared gate");


    // A noncontiguous class-9 selector mask proves that the runtime scans actual
    // attachment selector bits, rather than inventing compact slots 0..count-1.
    const auto& w187=id187->simulation_weapon_profiles.at(0);
    const auto* shot187=catalog.shot_definition(w187.shot_definition_id);
    if(w187.runtime_class_id!=9U || w187.launch_attachment_selector_mask!=0x50U)
        throw std::runtime_error("ARM 187 noncontiguous class-9 selector profile changed");
    SimulationActorState sparse_actor;
    sparse_actor.weapons.resize(1U);
    auto& sparse_state=sparse_actor.weapons.front();
    const Vec3f sparse_target{2,0,0};
    const auto sparse_aim=simulation_shot_launch_angles({0,0,0},sparse_target,*shot187);
    sparse_state.yaw.angle_turn16=sparse_aim.yaw_turn16;
    sparse_state.pitch.angle_turn16=sparse_aim.pitch_turn16;
    auto sparse_first=simulation_tick_weapon(sparse_actor,sparse_state,w187,*shot187,
        {0,0,0},&sparse_target,true);
    if(!sparse_first.fired || sparse_first.fired_attachment_selector!=4U ||
       (sparse_state.launch_mesh_group_mask&(1U<<4U))!=0U)
        throw std::runtime_error("ARM 187 did not fire/hide real selector 4 first");
    for(uint32_t tick=1;tick<w187.inter_shot_ticks;++tick)
        if(simulation_tick_weapon(sparse_actor,sparse_state,w187,*shot187,
            {0,0,0},&sparse_target,true).fired)
            throw std::runtime_error("ARM 187 ignored its shared inter-shot gate");
    auto sparse_second=simulation_tick_weapon(sparse_actor,sparse_state,w187,*shot187,
        {0,0,0},&sparse_target,true);
    if(!sparse_second.fired || sparse_second.fired_attachment_selector!=6U)
        throw std::runtime_error("ARM 187 did not advance from selector 4 to selector 6");

    // Class 21 uses a flattened model-point sequence rather than the eight class-9
    // timer selectors. Ensure successive reload cycles advance that full sequence.
    const auto& w242=id242->simulation_weapon_profiles.at(0);
    const auto* shot242=catalog.shot_definition(w242.shot_definition_id);
    SimulationActorState class21_actor;
    class21_actor.weapons.resize(1U);
    auto& class21_state=class21_actor.weapons.front();
    const Vec3f class21_target{1,0,0};
    const auto class21_aim=simulation_shot_launch_angles({0,0,0},class21_target,*shot242);
    class21_state.yaw.angle_turn16=class21_aim.yaw_turn16;
    class21_state.pitch.angle_turn16=class21_aim.pitch_turn16;
    auto class21_first=simulation_tick_weapon(class21_actor,class21_state,w242,*shot242,
        {0,0,0},&class21_target,true);
    if(!class21_first.fired || class21_first.fired_attachment_selector!=0U)
        throw std::runtime_error("ARM 242 did not begin its flattened attachment sequence");
    SimulationWeaponTickResult class21_second;
    for(uint32_t tick=0;tick<w242.reload_ticks;++tick)
        class21_second=simulation_tick_weapon(class21_actor,class21_state,w242,*shot242,
            {0,0,0},&class21_target,true);
    if(!class21_second.fired || class21_second.fired_attachment_selector!=1U)
        throw std::runtime_error("ARM 242 did not advance its flattened attachment sequence");

    const auto* rail_shot=catalog.shot_definition(w360.shot_definition_id);
    const auto rail_sprite=SpriteAsset::parse(catalog.load(rail_shot->sprite_path));
    if(w360.shot_definition_id!=2U || rail_shot->launch_effect_id!=70U ||
       rail_shot->terrain_impact_effect_id(0U)!=3U || !rail_sprite.mesh().indices.empty())
        throw std::runtime_error("ARM 360 file-backed invisible rail projectile/effect chain changed");
    const auto rotor_effect=SpriteAsset::parse(catalog.load(catalog.effect_definition(141U)->sprite_path));
    if(id221->runtime_model_class_id!=17U || id221->timed_effect_id!=141U ||
       rotor_effect.mesh().indices.size()/3U!=12U)
        throw std::runtime_error("ARM 221 rotor deployment/wash resource chain changed");

    std::cout << "G377_ATTACK_RUNTIME_ROTOR_SHADOW_PIPELINE attackable=" << attackable
              << " units=" << units << " buildings=" << buildings
              << " profiles=" << profiles << " class17=" << class17_count
              << " effects=" << effects.size() << " shots=" << shots.size() << " PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G377_ATTACK_RUNTIME_ROTOR_SHADOW_PIPELINE FAIL: " << error.what() << '\n';
    return 1;
}
