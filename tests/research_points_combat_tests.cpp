#include "thandor/simulation_lab.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

thandor::SimulationLabConfig config(std::uint32_t duration = 256U) {
    thandor::SimulationLabConfig value;
    value.run_count = 1U;
    value.duration_ticks = duration;
    value.parameter_index = 100U;
    value.sweep_mode = thandor::SimulationLabSweepMode::Fixed;
    return value;
}

thandor::SimulationLabSeedEntity entity(std::size_t placement, std::uint32_t asset,
                                         std::uint32_t faction, std::uint32_t runtime_class,
                                         thandor::Vec3f position) {
    thandor::SimulationLabSeedEntity value;
    value.source_placement_index = placement;
    value.army_asset_id = asset;
    value.faction_index = faction;
    value.runtime_model_class_id = runtime_class;
    value.actor.position = position;
    value.root_model_maximum_integrity = 1000;
    return value;
}
} // namespace

int main() try {
    const auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};

    // A completed factory product still owns PRODUCING through its exit cycle.
    {
        const auto factory=entity(0,301,1,13,{10,-10,0});
        thandor::SimulationLabTechnologyAsset technology{};
        technology.technology_id=6;technology.xenite_cost_q4=100;technology.research_duration_ticks=5;
        std::array<thandor::SimulationLabFactionEconomySeed,8> economy{};
        economy[1].xenite_current_q4=1000;
        thandor::SimulationLabExperiment lab;
        lab.reset(config(),parameters,std::span(&factory,1),economy,{},std::span(&technology,1),{},{});
        auto& run=lab.runs()[0];run.ai_planning_enabled=false;
        auto& source=run.entities[0];
        source.research_pending_payment=true;source.research_technology_id=6;
        source.research_xenite_due_q4=100;source.research_duration_ticks=5;
        source.production_behavior_state=3;
        lab.step(field,1);
        require(source.research_pending_payment&&!source.research_active&&
                run.faction_economy[1].xenite_current_q4==1000,
                "factory paid research before its product exit cycle ended");
        source.production_behavior_state=0;
        lab.step(field,1);
        require(source.research_active&&run.faction_economy[1].xenite_current_q4==900,
                "closed idle factory did not start pending research");
    }

    // Exact FrontendResultsTable Points formula: economyProgressScore + relationScore.
    thandor::SimulationLabFactionScoreState score;
    score.xenite_extracted_total_q4 = 160U;   // 10 after retail >>4
    score.tritium_extracted_total_q4 = 80U;   // 5 after retail >>4
    score.explored_terrain_percent = 25U;
    score.relation_counter_a = 1;
    score.relation_counter_b = 1;
    score.relation_counter_c = 1;
    score.relation_counter_d = 1;
    score.relation_counter_e = 1;
    score.relation_counter_f = 1;
    thandor::FactionTechnologyMask256 points_mask{};
    for (std::uint32_t id = 0; id < 7U; ++id)
        thandor::set_faction_technology_unlocked(points_mask, id, true);
    thandor::simulation_lab_recompute_points(score, points_mask);
    require(score.primary_resource_component == 10 && score.secondary_resource_component == 5,
            "retail resource score inputs did not use Q4 >> 4");
    require(score.unlocked_technology_count_beyond_baseline == 2U,
            "technology score did not subtract the retail five-tech baseline");
    require(score.economy_progress_score == 45 && score.relation_score == 90 && score.points == 135,
            "Simulation Lab Points do not match the recovered retail formula");

    // AI research is gated on a completed faction-owned ARM330. The Lab retains the
    // pending 0x80 -> active 0x40 -> paid completion timeline once the gate is satisfied.
    {
        auto researcher = entity(1U, 0x401U, 1U, 11U, {10.0F, 10.0F, 4.0F});
        // Retail workspace13 needs a nonzero KI research candidate weight;
        // a zero weight does not enqueue research even when a positive TEC wins.
        parameters[0x0DU] = 4096U;
        researcher.research_technology_ids = {6U};
        auto arms_factory = entity(2U, 330U, 1U, 13U, {14.0F, 10.0F, 4.0F});
        arms_factory.building = true;
        std::array<thandor::SimulationLabSeedEntity, 2> seeds{researcher, arms_factory};
        std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
        economy[1].xenite_current_q4 = 100U;
        std::array<thandor::FactionTechnologyMask256, 8> masks{};
        for (std::uint32_t id = 0; id < 5U; ++id)
            thandor::set_faction_technology_unlocked(masks[1], id, true);
        std::array<thandor::SimulationLabTechnologyAsset, 2> technologies{};
        technologies[0].technology_id = 5U;
        technologies[0].dependency_technology_id = 5U;
        technologies[1].technology_id = 6U;
        technologies[1].xenite_cost_q4 = 32U;
        technologies[1].energy_demand_q4 = 16U;
        technologies[1].research_duration_ticks = 2U;
        technologies[1].dependency_technology_id = 5U;
        technologies[1].ai_base_candidate_score = 100;
        std::array<thandor::SimulationLabFactionScoreSeed, 8> score_seeds{};

        // Without ARM330, the AI must not propose research even with an idle researcher.
        thandor::SimulationLabExperiment without_arm330;
        std::array<thandor::SimulationLabSeedEntity, 1> lone_researcher{researcher};
        without_arm330.reset(config(), parameters, lone_researcher, economy, {}, technologies,
                             masks, score_seeds);
        without_arm330.step(field, 74U);
        require(without_arm330.runs().front().metrics.research_jobs_queued == 0U,
                "AI research bypassed the retail ARM330 prerequisite");

        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(), parameters, seeds, economy, {}, technologies, masks, score_seeds);
        auto& run = experiment.runs().front();
        // Faction 1's first economy-phase planning turn occurs at tick 74:
        // (tick>>3)&7 == 1, (tick>>6)&1 == 1 and world-job phase == 2.
        experiment.step(field, 74U);
        require(run.metrics.research_jobs_queued == 1U &&
                run.entities.front().research_pending_payment && !run.entities.front().research_active,
                "AI research planner did not create the recovered pending 0x80 state");
        require(run.faction_economy[1].xenite_current_q4 == 100U,
                "research queueing spent Xenite before the runtime start branch");
        require(run.faction_active_research_energy_q4[1] == 0,
                "pending research incorrectly consumed active Energy");

        experiment.step(field, 1U);
        require(run.metrics.research_jobs_started == 1U && run.entities.front().research_active,
                "pending research did not transition to active after payment");
        require(run.faction_economy[1].xenite_current_q4 == 68U,
                "research Xenite was not deducted exactly once at start");
        require(run.faction_active_research_energy_q4[1] == 16,
                "active research Energy load does not match TEC +0x24");

        experiment.step(field, 2U);
        require(run.metrics.research_jobs_completed == 1U && !run.entities.front().research_active,
                "active research did not complete at the raw TEC duration");
        require(run.faction_active_research_energy_q4[1] == 0,
                "research completion did not release temporary Energy");
        require(thandor::faction_technology_unlocked(run.faction_technology_masks[1], 6U) &&
                thandor::faction_technology_unlocked(run.faction_technology_masks[1], 5U),
                "research completion did not unlock selected/dependency technologies");
        require(run.faction_economy[1].xenite_current_q4 == 68U,
                "research completion charged Xenite a second time");
        require(run.faction_score[1].unlocked_technology_count_beyond_baseline == 2U,
                "completed research was not reflected in the retail Points technology term");
    }

    // Integrated class-13 path: the produced ground unit stays linked during
    // its exit command while servicing native weapon callbacks.
    {
        auto factory = entity(10U, 0x601U, 1U, 13U, {8.0F, 8.0F, 10.0F});
        factory.building = true;
        factory.production_compatibility_mask = 0x04U;
        factory.production_spawn_world = thandor::Vec3f{8.0F, 10.0F, 10.0F};
        factory.production_exit_world = thandor::Vec3f{12.0F, 10.0F, 10.0F};
        auto target = entity(11U, 0x602U, 2U, 11U, {18.0F, 10.0F, 10.0F});
        target.building = true;
        target.shot_impact_class_index = 2U;

        thandor::SimulationWeaponProfile profile;
        profile.runtime_class_id = 2U;
        profile.shot_definition_id = 0x88U;
        profile.reload_ticks = 10U;
        profile.maximum_range_q12 = thandor::float_to_q12(6.0F);
        profile.yaw_max_velocity_turn16 = 0x7FFF;
        profile.yaw_acceleration_turn16 = 0x7FFF;
        profile.pitch_max_velocity_turn16 = 0x7FFF;
        profile.pitch_acceleration_turn16 = 0x7FFF;
        profile.pitch_min_turn16 = -0x4000;
        profile.pitch_max_turn16 = 0x4000;
        profile.launch_attachment_count = 1U;

        thandor::ShotDefinitionRecord shot;
        shot.definition_id = 0x88U;
        shot.trajectory_mode = static_cast<std::int32_t>(thandor::ShotTrajectoryMode::DirectLine);
        shot.launch_speed_q12 = thandor::float_to_q12(4.0F);
        shot.projectile_lifetime_ticks = 20U;
        shot.launch_effect_id = 0xA01U;
        shot.target_impact_effect_ids[2] = 0xA02U;
        shot.target_impact_damage_q12[2] = 2000;

        thandor::SimulationLabProductionAsset produced;
        produced.army_asset_id = 0x603U;
        produced.army_runtime_flags14_raw = 0x04U;
        produced.xenite_cost_q4 = 16U;
        produced.build_duration_ticks = 1U;
        produced.energy_demand_q4 = 8;
        produced.entity_seed.army_asset_id = produced.army_asset_id;
        produced.entity_seed.runtime_model_class_id = 2U;
        produced.entity_seed.motion.speed_q12_per_tick = thandor::float_to_q12(1.0F);
        produced.entity_seed.motion.yaw_max_velocity_turn16 = 0x7FFF;
        produced.entity_seed.motion.yaw_acceleration_turn16 = 0x7FFF;
        produced.entity_seed.root_model_maximum_integrity = 1000;
        produced.entity_seed.combat_weapons.push_back({profile, shot});
        produced.entity_seed.actor.weapons.resize(1U);

        std::array<thandor::SimulationLabSeedEntity, 2> seeds{factory, target};
        std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
        economy[1].xenite_current_q4 = 100U;
        std::array<thandor::SimulationLabProductionAsset, 1> products{produced};
        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(256U), parameters, seeds, economy, products);
        auto& run = experiment.runs().front();
        run.secondary_purchase_requests.push_back({produced.army_asset_id, 1U, 0U, 0U});

        experiment.step(field, 2U);
        require(run.metrics.class13_units_spawned == 1U && run.entities.size() == 3U,
                "class 13 did not materialize the armed produced unit");
        require(run.entities[2].production_linked_to_parent && run.metrics.weapon_shots_fired == 0U,
                "produced unit fired before completing its protected factory-exit command");
        for (std::uint32_t i = 0; i < 180U && run.entities[2].production_linked_to_parent; ++i) {
            experiment.step(field, 1U);
            if(run.entities[2].production_linked_to_parent)
                require(run.entities[2].production_parent_entity_index==0U,
                        "factory-exit child lost its linked parent");
        }
        require(!run.entities[2].production_linked_to_parent &&
                !run.entities[2].production_parent_entity_index,
                "produced unit never detached after reaching the factory exit window");

        for (std::uint32_t i = 0; i < 40U && !run.entities[1].root_model_terminal; ++i)
            experiment.step(field, 1U);
        require(run.metrics.weapon_shots_fired != 0U && run.metrics.weapon_launch_effect_events != 0U,
                "produced unit did not acquire/fight an enemy with its copied SHT weapon payload");
        require(run.metrics.projectile_impacts != 0U && run.metrics.weapon_impact_effect_events != 0U &&
                run.entities[1].root_model_terminal,
                "produced-unit beam combat did not service lifetime damage and target impact EFF");
        require(run.metrics.weapon_impact_effect_events == 1U,
                "direct-line beam emitted its target impact EFF more than once");
        const auto events = thandor::simulation_lab_one_shot_events_after(run, 0U);
        bool saw_launch = false, saw_impact = false;
        for (const auto& event : events) {
            saw_launch |= event.kind == thandor::SimulationLabOneShotEventKind::WeaponLaunchEffect &&
                          event.effect_id == 0xA01U && event.army_asset_id == produced.army_asset_id;
            saw_impact |= event.kind == thandor::SimulationLabOneShotEventKind::WeaponImpactEffect &&
                          event.effect_id == 0xA02U && event.army_asset_id == produced.army_asset_id;
        }
        require(saw_launch && saw_impact,
                "produced class-13 combat effects were not journaled with the produced ARM owner");
    }

    // Combat: tower/unit weapon state produces SHT launch and impact effects, a projectile, damage,
    // terminal relation accounting, and Points updates. Keep the shot above terrain so this test
    // exercises target-model impact rather than the independent terrain-impact branch.
    {
        auto shooter = entity(2U, 0x501U, 1U, 10U, {8.0F, 8.0F, 10.0F});
        auto target = entity(3U, 0x502U, 2U, 11U, {12.0F, 8.0F, 10.0F});
        target.shot_impact_class_index = 3U;

        thandor::SimulationWeaponProfile profile;
        profile.runtime_class_id = 10U;
        profile.shot_definition_id = 0x77U;
        profile.reload_ticks = 10U;
        profile.maximum_range_q12 = thandor::float_to_q12(100.0F);
        profile.yaw_max_velocity_turn16 = 0x7FFF;
        profile.yaw_acceleration_turn16 = 0x7FFF;
        profile.pitch_max_velocity_turn16 = 0x7FFF;
        profile.pitch_acceleration_turn16 = 0x7FFF;
        profile.pitch_min_turn16 = -0x4000;
        profile.pitch_max_turn16 = 0x4000;
        profile.launch_attachment_count = 1U;

        thandor::ShotDefinitionRecord shot;
        shot.definition_id = 0x77U;
        shot.trajectory_mode = static_cast<std::int32_t>(thandor::ShotTrajectoryMode::DirectLine);
        shot.launch_speed_q12 = thandor::float_to_q12(4.0F);
        shot.projectile_lifetime_ticks = 20U;
        shot.launch_effect_id = 0x901U;
        shot.target_impact_effect_ids[3] = 0x902U;
        shot.target_impact_damage_q12[3] = 2000;

        shooter.combat_weapons.push_back({profile, shot});
        shooter.actor.weapons.resize(1U);
        shooter.actor.weapons[0].pitch.angle_turn16 = 0U;
        std::array<thandor::SimulationLabSeedEntity, 2> seeds{shooter, target};

        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(16U), parameters, seeds);
        auto& run = experiment.runs().front();
        experiment.step(field, 1U);
        require(run.metrics.weapon_shots_fired == 1U && run.projectiles.size() == 1U,
                "armed Lab entity did not create a projectile from SHT weapon state");
        auto events = thandor::simulation_lab_one_shot_events_after(run, 0U);
        require(!events.empty() && events.front().kind == thandor::SimulationLabOneShotEventKind::WeaponLaunchEffect &&
                events.front().effect_id == 0x901U,
                "weapon fire did not journal the SHT launch effect");

        experiment.step(field, 1U);
        require(run.metrics.projectile_impacts == 1U && !run.entities[1].root_model_terminal &&
                run.entities[1].root_model_integrity == 900,
                "direct-line beam did not apply exactly one lifetime-scaled damage tick");
        require(run.metrics.projectile_damage_applied == 100U,
                "direct-line beam first maintenance tick used full-shot damage");
        require(run.metrics.weapon_impact_effect_events == 1U,
                "target impact did not journal its SHT impact effect exactly once");
        experiment.step(field, 9U);
        require(run.entities[1].root_model_terminal && run.metrics.projectile_damage_applied == 1000U,
                "direct-line beam did not accumulate lifetime-scaled damage to terminal integrity");
        require(run.metrics.projectile_impacts == 1U && run.metrics.weapon_impact_effect_events == 1U,
                "direct-line beam repeated logical impact/effect accounting across maintenance ticks");
        events = thandor::simulation_lab_one_shot_events_after(run, 0U);
        bool saw_impact = false;
        for (const auto& event : events)
            if (event.kind == thandor::SimulationLabOneShotEventKind::WeaponImpactEffect &&
                event.effect_id == 0x902U) saw_impact = true;
        require(saw_impact, "weapon impact event lost the target-class EFF id");
        require(run.faction_score[2].relation_counter_d == 1 &&
                run.faction_score[1].relation_counter_f == 1,
                "source/victim relation D/F accounting did not match terminal target class");
        require(run.faction_score[1].relation_score == 40 && run.faction_score[2].relation_score == -20,
                "combat relation counters were not folded into retail Points");
    }

    std::cout << "V563_SIMULATION_LAB_RESEARCH_POINTS_COMBAT PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V563_SIMULATION_LAB_RESEARCH_POINTS_COMBAT FAIL: " << error.what() << '\n';
    return 1;
}
