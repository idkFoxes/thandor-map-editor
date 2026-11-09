#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
}

int main() try {
    using namespace thandor;
    constexpr uint32_t army_id=0xA33U, root_model_id=0xB44U;
    constexpr uint32_t construction_effect=0xC55U, terminal_effect=0xC66U;
    constexpr uint32_t wreck_id=0xA77U, wreck_model_id=0xB88U;
    const auto field=FieldGrid::create(59,59,0,0U);
    auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();
    ModelVisualNode root{};
    root.parent_index=UINT32_MAX;
    root.model_definition_id=root_model_id;
    root.runtime_class_id=4U;
    root.sprite_path="spr/test/building_destroy.spr";
    root.simulation.maximum_integrity=4096;
    root.simulation.construction_destruction_effect_id=construction_effect;
    root.simulation.destroyed_replacement_army_asset_id=wreck_id;
    root.simulation.terminal_attachment_effect_ids[0]=terminal_effect;
    ArmyAssetPreset army{};
    army.registry_id=army_id;
    army.model_hierarchy.push_back(root);
    army.resolved_model_hierarchy.push_back(root);
    ArmyModelVariantGroup group{};
    ModelTechnologyVariant variant{};
    variant.model_definition_id=root_model_id;
    variant.model_hierarchy.push_back(root);
    group.variants.push_back(variant);
    army.model_variant_groups.push_back(group);
    catalog->armies.emplace(army_id,army);
    ModelVisualRecord record{};
    record.definition_id=root_model_id;
    record.runtime_class_id=4U;
    record.simulation=root.simulation;
    record.hierarchy_nodes.push_back(root);
    catalog->definitions.emplace(root_model_id,record);
    catalog->effects.emplace(construction_effect,SimulationLabRuntimeEffect{});
    catalog->effects.emplace(terminal_effect,SimulationLabRuntimeEffect{});
    SimulationLabRuntimeSprite sprite{};
    sprite.height_offset=0.5F;
    catalog->sprites.emplace(root.sprite_path,sprite);

    ModelVisualNode wreck_root{};
    wreck_root.parent_index=UINT32_MAX;
    wreck_root.model_definition_id=wreck_model_id;
    wreck_root.runtime_class_id=12U;
    wreck_root.sprite_path="spr/test/wreck.spr";
    wreck_root.simulation.maximum_integrity=4096;
    wreck_root.simulation.class_step_q12=-512;
    ArmyAssetPreset wreck_preset{};
    wreck_preset.registry_id=wreck_id;
    wreck_preset.model_hierarchy.push_back(wreck_root);
    wreck_preset.resolved_model_hierarchy.push_back(wreck_root);
    ArmyModelVariantGroup wreck_group{};
    ModelTechnologyVariant wreck_variant{};
    wreck_variant.model_definition_id=wreck_model_id;
    wreck_variant.model_hierarchy.push_back(wreck_root);
    wreck_group.variants.push_back(wreck_variant);
    wreck_preset.model_variant_groups.push_back(wreck_group);
    catalog->armies.emplace(wreck_id,wreck_preset);
    ModelVisualRecord wreck_record{};
    wreck_record.definition_id=wreck_model_id;
    wreck_record.runtime_class_id=12U;
    wreck_record.simulation=wreck_root.simulation;
    wreck_record.hierarchy_nodes.push_back(wreck_root);
    catalog->definitions.emplace(wreck_model_id,wreck_record);
    SimulationLabRuntimeSprite wreck_sprite{};
    wreck_sprite.has_bounds=true;
    wreck_sprite.bounds_min={-1.0F,-1.0F,0.0F};
    wreck_sprite.bounds_max={1.0F,1.0F,2.0F};
    catalog->sprites.emplace(wreck_root.sprite_path,wreck_sprite);

    SimulationLabSeedEntity seed{};
    seed.army_asset_id=army_id;
    seed.source_placement_index=SIZE_MAX;
    seed.faction_index=1;
    seed.runtime_model_class_id=4U;
    seed.building=true;
    seed.actor.position={10.0F,-10.0F,0.0F};
    seed.actor.body_yaw_turn16=0x3456U;
    seed.model_components=simulation_lab_model_components_from_hierarchy(army.model_hierarchy);
    SimulationLabProductionAsset wreck_product{};
    wreck_product.army_asset_id=wreck_id;
    wreck_product.entity_seed.army_asset_id=wreck_id;
    wreck_product.entity_seed.runtime_model_class_id=12U;
    wreck_product.entity_seed.model_components=simulation_lab_model_components_from_hierarchy(wreck_preset.model_hierarchy);
    SimulationLabScenarioRuntimeSeed scenario{};
    scenario.runtime_catalog=catalog;
    SimulationLabConfig config{};
    config.run_count=1;
    config.duration_ticks=64;
    config.parameter_index=1;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> knowledge{};
    std::array seeds{seed};
    std::array products{wreck_product};
    SimulationLabExperiment lab;
    const auto reset=[&](bool provide_wreck){
        lab.reset(config,knowledge,seeds,
            std::span<const SimulationLabFactionEconomySeed>{},
            provide_wreck ? std::span<const SimulationLabProductionAsset>(products)
                          : std::span<const SimulationLabProductionAsset>{},
            std::span<const SimulationLabTechnologyAsset>{},
            std::span<const FactionTechnologyMask256>{},
            std::span<const SimulationLabFactionScoreSeed>{},scenario);
    };
    const auto die=[&](bool completion){
        auto& entity=lab.runs()[0].entities.at(0);
        check(entity.runtime_model_hierarchy && entity.model_components.size()==1,
              "root hierarchy not prepared");
        auto& component=entity.model_components.at(0);
        component.integrity=0;
        component.terminal_runtime_flag8=true;
        component.terminal_self_reference=true;
        if (completion) component.destruction_complete_tick=1U;
        entity.root_model_terminal=true;
        entity.root_model_integrity=0;
    };
    // A recorded MDL +0x74 replacement is not enough to create a combat ruin.
    // Even with a fully resolved stock-like ARM in the production catalog,
    // removal of the destroyed root must not produce a second actor.
    reset(true);
    die(false);
    lab.step(field,1);
    const auto events=simulation_lab_one_shot_events_after(lab.runs()[0],0U);
    for (const auto& event:events)
        check(event.effect_id!=construction_effect,
              "placement/dismantle effect must not play as combat death");
    check(lab.runs()[0].entities.size()==1 &&
          !lab.runs()[0].entities[0].runtime_removed,
          "root must remain during its terminal effects");
    auto& dying=lab.runs()[0].entities[0];
    dying.model_components[0].destruction_complete_tick=lab.runs()[0].tick+1;
    lab.step(field,1);
    check(lab.runs()[0].entities.size()==1 &&
          lab.runs()[0].entities[0].runtime_removed &&
          lab.runs()[0].metrics.destroyed_replacement_armies_spawned==0 &&
          lab.runs()[0].metrics.destroyed_replacement_armies_unresolved==0,
          "combat root removal spawned a ruin despite replacement being disabled");
    lab.step(field,20);
    check(lab.runs()[0].entities.size()==1,
          "delayed combat ruin spawned after root teardown");

    reset(false);
    die(true);
    lab.step(field,1);
    check(lab.runs()[0].entities.size()==1 &&
          lab.runs()[0].entities[0].runtime_removed &&
          lab.runs()[0].metrics.destroyed_replacement_armies_unresolved==0,
          "missing replacement ARM must not be resolved or spawned");

    reset(true);
    die(true);
    lab.runs()[0].entities[0].model_components[0].runtime_flags_ec|=0x20U;
    lab.step(field,1);
    check(lab.runs()[0].entities.size()==1 &&
          lab.runs()[0].metrics.destroyed_replacement_armies_spawned==0,
          "construction/dismantle flag caused a combat ruin spawn");

    root.simulation.destroyed_replacement_army_asset_id=-1;
    catalog->definitions.at(root_model_id).simulation.destroyed_replacement_army_asset_id=-1;
    catalog->armies.at(army_id).model_variant_groups[0].variants[0].model_hierarchy[0].simulation.destroyed_replacement_army_asset_id=-1;
    reset(true);
    die(true);
    lab.step(field,1);
    check(lab.runs()[0].entities.size()==1 &&
          lab.runs()[0].metrics.destroyed_replacement_armies_spawned==0 &&
          lab.runs()[0].metrics.destroyed_replacement_armies_unresolved==0,
          "sentinel -1 created a replacement actor");

    // Mission-authored ruin placements are separate from combat replacements:
    // their class-12 lifecycle remains available when the level explicitly seeds one.
    auto mission_ruin=seed;
    mission_ruin.army_asset_id=wreck_id;
    mission_ruin.source_placement_index=7U;
    mission_ruin.faction_index=0U;
    mission_ruin.runtime_model_class_id=12U;
    mission_ruin.building=false;
    mission_ruin.model_components=simulation_lab_model_components_from_hierarchy(wreck_preset.model_hierarchy);
    std::array mission_seeds{mission_ruin};
    lab.reset(config,knowledge,mission_seeds,
        std::span<const SimulationLabFactionEconomySeed>{},
        std::span<const SimulationLabProductionAsset>(products),
        std::span<const SimulationLabTechnologyAsset>{},
        std::span<const FactionTechnologyMask256>{},
        std::span<const SimulationLabFactionScoreSeed>{},scenario);
    check(lab.runs()[0].entities.size()==1 &&
          lab.runs()[0].entities[0].army_asset_id==wreck_id &&
          !lab.runs()[0].entities[0].runtime_removed,
          "explicit mission ruin missing before gameplay");
    lab.step(field,3);
    check(lab.runs()[0].entities[0].class12_vertical_offset_q12<0 &&
          !lab.runs()[0].entities[0].runtime_removed,
          "mission-placed ruin lost its authored class-12 sinking behavior");
    lab.step(field,20);
    check(lab.runs()[0].entities.size()==1 &&
          lab.runs()[0].entities[0].runtime_removed &&
          lab.runs()[0].metrics.destroyed_replacement_armies_spawned==0,
          "mission ruin release incorrectly spawned another ruin");
    // Native level runtime releases the old hierarchy and creates MDL +0x74's
    // neutral ARM. Pool reuse preserves the old slot's smoke countdown.
    catalog->definitions.at(root_model_id).simulation.destroyed_replacement_army_asset_id=wreck_id;
    catalog->armies.at(army_id).model_variant_groups[0].variants[0].model_hierarchy[0].simulation.destroyed_replacement_army_asset_id=wreck_id;
    scenario.enabled=true;
    scenario.active_faction_count=scenario.assignable_faction_count=1;
    reset(true);
    die(true);
    lab.runs()[0].entities[0].damage_effect_cooldown=7;
    lab.step(field,1);
    check(lab.runs()[0].entities.size()==2&&
          lab.runs()[0].entities[0].runtime_removed&&
          lab.runs()[0].entities[1].army_asset_id==wreck_id&&
          lab.runs()[0].entities[1].faction_index==0U&&
          lab.runs()[0].entities[1].damage_effect_cooldown==7&&
          lab.runs()[0].metrics.destroyed_replacement_armies_spawned==1,
          "native model release did not create neutral replacement with reused pool state");
    auto& sinking_wreck=lab.runs()[0].entities[1];
    const auto wreck_pose=simulation_lab_current_model_poses(sinking_wreck,field,*catalog).front();
    sinking_wreck.class12_vertical_offset_q12=-512;
    const auto sunk_pose=simulation_lab_current_model_poses(sinking_wreck,field,*catalog).front();
    check(float_to_q12(sunk_pose.position.z)==float_to_q12(wreck_pose.position.z)-512&&
          sunk_pose.basis==wreck_pose.basis,
          "sinking wreck lost its displacement or changed its rigid model basis");
    // Conversion creates its replacement before releasing the Pioneer. Unlike a
    // wreck replacement, it cannot inherit the cooldown from that occupied slot.
    SimulationLabProductionAsset converted_product{};
    converted_product.army_asset_id=army_id;
    converted_product.entity_seed=seed;
    const std::array converted_products{converted_product};
    lab.reset(config,knowledge,seeds,
        std::span<const SimulationLabFactionEconomySeed>{},converted_products,
        std::span<const SimulationLabTechnologyAsset>{},
        std::span<const FactionTechnologyMask256>{},
        std::span<const SimulationLabFactionScoreSeed>{},scenario);
    auto& converting=lab.runs()[0].entities[0];
    converting.runtime_model_class_id=18U;
    converting.runtime_class_specific_c0_raw=army_id;
    converting.runtime_flags|=0x08U;
    converting.class18_conversion_effect_remaining_ticks=1U;
    converting.damage_effect_cooldown=7;
    converting.model_components[0].integrity=converting.root_model_integrity=1024;
    // The replacement is wounded and would regenerate if incorrectly visited
    // on the same frame as the completion effect that creates it.
    catalog->definitions.at(root_model_id).simulation.health_regeneration_per_step=3;
    const auto pioneer_slot=converting.model_components[0].native_model_pool_slot;
    lab.step(field,1);
    const auto& converted_run=lab.runs()[0];
    check(converted_run.metrics.pioneer_conversions_completed==1&&
          converted_run.entities.size()==1&&
          converted_run.entities[0].damage_effect_cooldown==0&&
          converted_run.entities[0].root_model_integrity==1024&&
          converted_run.entities[0].model_components[0].native_model_pool_slot!=pioneer_slot&&
          converted_run.vacant_model_smoke_cooldowns[pioneer_slot]==7,
          "Pioneer replacement reused its predecessor's still-occupied model slot");
    // A destroyed class-4 root retains one outer damage-smoke callback.
    // The timed-emitter and animation callbacks must no longer add two more.
    reset(true);
    die(false);
    auto nodes=std::make_shared<std::vector<ModelVisualNode>>(*lab.runs()[0].entities[0].runtime_model_hierarchy);
    auto& smoke=nodes->front().simulation;
    smoke.damage_effect_health_threshold_percent=100;
    smoke.damage_effect_id=construction_effect;
    smoke.damage_effect_min_cooldown_ticks=4;
    smoke.damage_effect_random_cooldown_ticks=4;
    catalog->definitions.at(root_model_id).simulation=smoke;
    lab.runs()[0].entities[0].runtime_model_hierarchy=nodes;
    lab.runs()[0].entities[0].damage_effect_cooldown=1;
    const auto before_smoke_rng=lab.runs()[0].rng_state;
    lab.step(field,1);
    check(lab.runs()[0].entities[0].damage_effect_cooldown==0&&
          lab.runs()[0].rng_state==before_smoke_rng,
          "destroyed root ran extra emitter damage-smoke callbacks");
    // A completion owner is newer than its dying root. An older shot must
    // not make that root run one final emitter callback before release.
    reset(true);
    die(true);
    auto& release_run=lab.runs()[0];
    release_run.tick=2U;
    auto& releasing=release_run.entities[0];
    releasing.synthetic=true;
    releasing.construction_start_tick=2U;
    releasing.runtime_model_hierarchy=nodes;
    releasing.damage_effect_cooldown=7;
    releasing.model_components[0].destruction_complete_tick=3U;
    SimulationProjectileState older_shot{};
    older_shot.spawn_tick=1U;
    older_shot.owner_placement_index=SIZE_MAX;
    release_run.projectiles.push_back(older_shot);
    lab.step(field,1);
    check(release_run.entities[0].runtime_removed&&release_run.entities.size()==2&&
          release_run.entities[1].damage_effect_cooldown==7,
          "completed root ran an emitter ahead of its newer release effect");
    reset(true);
    die(false);
    auto generator_nodes=std::make_shared<std::vector<ModelVisualNode>>(*lab.runs()[0].entities[0].runtime_model_hierarchy);
    auto& generator=generator_nodes->front();
    generator.runtime_class_id=16U;
    generator.simulation.damage_effect_health_threshold_percent=0;
    generator.simulation.timed_effect_id=construction_effect;
    generator.simulation.timed_effect_interval_ticks=5;
    generator.simulation.timed_effect_random_ticks=1;
    catalog->definitions.at(root_model_id).simulation=generator.simulation;
    auto& dead_generator=lab.runs()[0].entities[0];
    dead_generator.runtime_model_class_id=16U;
    dead_generator.model_components[0].runtime_class_id=16U;
    dead_generator.runtime_model_hierarchy=generator_nodes;
    dead_generator.model_components[0].timed_effect_countdown=0;
    const auto before_generator_rng=lab.runs()[0].rng_state;
    lab.step(field,1);
    check(lab.runs()[0].entities[0].model_components[0].timed_effect_countdown==5&&
          lab.runs()[0].rng_state!=before_generator_rng,
          "destroyed class-16 model stopped its direct emitter callback before release");
    reset(true);
    auto banking_nodes=std::make_shared<std::vector<ModelVisualNode>>(*lab.runs()[0].entities[0].runtime_model_hierarchy);
    auto& banking=banking_nodes->front();
    banking.runtime_class_id=17U;
    banking.simulation.damage_effect_health_threshold_percent=0;
    banking.simulation.timed_effect_id=construction_effect;
    banking.simulation.timed_effect_interval_ticks=5;
    banking.simulation.timed_effect_random_ticks=1;
    catalog->definitions.at(root_model_id).simulation=banking.simulation;
    auto& idle_banking=lab.runs()[0].entities[0];
    idle_banking.runtime_model_class_id=17U;
    idle_banking.model_components[0].runtime_class_id=17U;
    idle_banking.runtime_model_hierarchy=banking_nodes;
    idle_banking.model_components[0].timed_effect_countdown=1;
    const auto before_banking_rng=lab.runs()[0].rng_state;
    lab.step(field,1);
    check(lab.runs()[0].entities[0].model_components[0].timed_effect_countdown==5&&
          lab.runs()[0].rng_state!=before_banking_rng,
          "idle class-17 root did not run its native timed-emitter callback");
    reset(true);
    auto pioneer_nodes=std::make_shared<std::vector<ModelVisualNode>>(*lab.runs()[0].entities[0].runtime_model_hierarchy);
    pioneer_nodes->front().runtime_class_id=18U;
    pioneer_nodes->front().simulation.damage_effect_health_threshold_percent=100;
    pioneer_nodes->front().simulation.damage_effect_id=construction_effect;
    catalog->definitions.at(root_model_id).simulation=pioneer_nodes->front().simulation;
    auto& smoking_pioneer=lab.runs()[0].entities[0];
    smoking_pioneer.runtime_model_class_id=18U;
    smoking_pioneer.model_components[0].runtime_class_id=18U;
    smoking_pioneer.model_components[0].integrity=smoking_pioneer.root_model_integrity=1024;
    smoking_pioneer.runtime_model_hierarchy=pioneer_nodes;
    smoking_pioneer.damage_effect_cooldown=7;
    lab.step(field,1);
    check(lab.runs()[0].entities[0].damage_effect_cooldown==6,
          "stationary Pioneer skipped its outer damage-smoke callback");
    auto& held_special=lab.runs()[0].entities[0];
    held_special.runtime_flags|=8U;
    held_special.model_components[0].runtime_flags_ec|=8U;
    held_special.pioneer_movement_initialized=true;
    auto& held_movement=held_special.pioneer_movement;
    const MovementPoint held_position{float_to_q12(held_special.actor.position.x),
                                      float_to_q12(held_special.actor.position.y)};
    held_movement.flags=0x305U;
    held_movement.model=held_movement.primary=held_movement.last_checked=held_position;
    held_movement.stored=held_movement.fallback={held_position.x+481,held_position.y+834};
    lab.step(field,1);
    const auto& serviced_special=lab.runs()[0].entities[0];
    check((serviced_special.pioneer_movement.flags&1U)==0U&&
          serviced_special.pioneer_movement.stored.x==held_position.x&&
          serviced_special.pioneer_movement.stored.y==held_position.y&&
          serviced_special.pioneer_movement.fallback.x==held_position.x&&
          serviced_special.pioneer_movement.fallback.y==held_position.y,
          "held class-18 skipped its special-behavior reset before stationary ground handling");
    std::cout<<"TERMINAL_REPLACEMENT PASS native neutral owner, pool reuse, legacy compatibility\n";
    return 0;
} catch(const std::exception& e) {
    std::cerr<<"V721_NO_COMBAT_RUIN_SPAWN FAIL: "<<e.what()<<'\n';
    return 1;
}
