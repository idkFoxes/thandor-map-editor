#include "thandor/simulation_lab_environment.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

using namespace thandor;
namespace {
void require(bool valid, const char* description) {
    if (!valid) throw std::runtime_error(description);
}

// An actual live model hull and the normal projectile->raycast->component-damage
// path, not the synthetic root-only weapon-firing assertion from V706.
int32_t contact_case(uint32_t target_faction, uint32_t target_flags,
                     int32_t starting_integrity, bool crossing_enemy) {
    auto field = FieldGrid::create(60, 60, 0, 0);
    const auto& cell = field.at(25, 25);
    const Vec3f origin{q12_to_float(cell.world_x_q12),
                       q12_to_float(cell.world_y_q12), 3.0F};
    auto catalog = std::make_shared<SimulationLabRuntimeCatalog>();
    auto& sprite = catalog->sprites["repair_target"];
    sprite.has_bounds = true;
    sprite.bounds_min = {-0.2F, -0.2F, -0.2F};
    sprite.bounds_max = { 0.2F,  0.2F,  0.2F};
    catalog->effect_lifetimes[44] = 3;

    ModelVisualNode node;
    node.parent_index = UINT32_MAX;
    node.model_definition_id = 20;
    node.runtime_class_id = 10;
    node.sprite_path = "repair_target";
    node.shot_impact_class_index = 1;
    node.simulation.placement_contact_kind = 3;
    auto hierarchy = std::make_shared<const std::vector<ModelVisualNode>>(std::vector{node});

    SimulationLabSeedEntity source;
    source.army_asset_id = 1;
    source.faction_index = 1;
    source.runtime_model_class_id = 10;
    source.shot_impact_class_index = 1;
    source.root_model_maximum_integrity = 1000;
    source.actor.position = origin;
    ShotDefinitionRecord repair;
    repair.definition_id = 82;
    repair.trajectory_mode = 0;
    repair.launch_speed_q12 = 4096;
    repair.projectile_lifetime_ticks = 8;
    repair.target_impact_effect_ids[1] = 44;
    repair.target_impact_damage_q12[1] = -80;
    repair.target_impact_damage_q12[0] = -80;
    SimulationLabWeaponSeed weapon;
    weapon.shot = repair;
    source.combat_weapons = {weapon};

    auto target = source;
    target.combat_weapons.clear();
    target.faction_index = target_faction;
    target.source_placement_index = 1;
    target.actor.position.x += 2.0F;
    std::array<SimulationLabSeedEntity,3> seeds{source,target,target};
    seeds[2].source_placement_index = 2;
    seeds[2].actor.position.x += 1.0F;
    seeds[2].actor.position.y += 5.0F;
    seeds[2].faction_index = 2;

    SimulationLabScenarioRuntimeSeed scenario;
    scenario.runtime_catalog = catalog;
    SimulationLabConfig config;
    config.run_count = 1;
    config.duration_ticks = 16;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
    SimulationLabExperiment experiment;
    experiment.reset(config,ki,seeds,{},{},{},{},{},scenario);
    auto& run = experiment.runs()[0];
    run.entities[1].runtime_model_hierarchy = hierarchy;
    run.entities[2].runtime_model_hierarchy = hierarchy;
    auto& hit = run.entities[1];
    if (crossing_enemy) {
        // The originally selected ally moved aside; the enemy crosses the beam.
        run.entities[2].actor.position = hit.actor.position;
        hit.actor.position.y += 5.0F;
    }
    hit.runtime_flags = target_flags;
    hit.root_model_integrity = starting_integrity;
    require(!hit.model_components.empty(), "fixture target has no root component");
    hit.model_components[hit.root_model_component_index].integrity = starting_integrity;
    run.entities[2].root_model_integrity = 600;
    run.entities[2].model_components[run.entities[2].root_model_component_index].integrity = 600;

    SimulationProjectileState beam;
    beam.serial = 1;
    beam.owner_placement_index = 0;
    beam.weapon_profile_index = 0;
    beam.shot_definition_id = 82;
    beam.launch_position = origin;
    beam.launch_velocity = {1,0,0};
    beam.target_position = target.actor.position;
    beam.live_target_placement_index = 1;
    beam.lifetime_ticks = 8;
    beam.travel_ticks = 1;
    run.projectiles = {beam};
    experiment.step(field,1);
    require(run.metrics.projectile_impacts == 1U, "repair beam failed real model collision");
    if (crossing_enemy) {
        require(run.projectiles[0].impact_target_placement_index == 2,
                "intervening actor did not become actual hit target");
        require(run.entities[2].root_model_integrity == 600,
                "repair projectile healed an enemy crossing its ray");
        require(run.metrics.projectile_repair_contacts_rejected == 1U &&
                run.metrics.projectile_repair_contacts_healed == 0U,
                "intervening enemy was not rejected by contact-time repair gate");
        return run.entities[2].root_model_integrity;
    }
    if (target_faction == 1 && target_flags == 0 && starting_integrity == 600) {
        require(run.metrics.projectile_repair_contacts_healed == 1U &&
                run.metrics.projectile_repair_integrity_restored == 10U,
                "real repair-contact healing telemetry differs from integrity");
    } else if (target_faction != 1 || target_flags != 0) {
        require(run.metrics.projectile_repair_contacts_rejected == 1U &&
                run.metrics.projectile_repair_contacts_healed == 0U,
                "disallowed repair contact did not report rejection");
    }
    return run.entities[1].root_model_integrity;
}
} // namespace
int main() try {
    require(contact_case(1,0,600,false)==610, "repair contact did not heal actual wounded ally");
    require(contact_case(2,0,600,false)==600, "repair contact healed an enemy");
    require(contact_case(1,0x400U,600,false)==600, "no-regeneration actor healed through projectile");
    require(contact_case(1,0,1000,false)==1000, "full-health actor exceeded maximum integrity");
    contact_case(1,0,600,true);
    std::cout << "V707_REPAIR_PROJECTILE_CONTACT PASS\n";
} catch(const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
}
