#include "thandor/simulation_lab_environment.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_defaults.hpp"
#include "thandor/simulation_lab_catalog.hpp"
#include "thandor/ai_unit_producibility_v601.hpp"
#include "thandor/catalog.hpp"
#include "thandor/map_document.hpp"
#include "thandor/simulation_lab.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include "simlab_faction_audit.hpp"
#include <memory>

using namespace thandor;
namespace {
SimulationLabSeedEntity seed_for(const ArmyAssetPreset& preset,
                                const AssetCatalog& catalog,
                                const FactionTechnologyMask256& mask) {
    SimulationLabSeedEntity seed;
    seed.army_asset_id = preset.registry_id;
    seed.runtime_model_class_id = preset.runtime_model_class_id;
    seed.building = simulation_lab_model_class_is_building(preset.runtime_model_class_id);
    seed.has_traversal_profile = preset.has_traversal_profile;
    seed.traversal_low_band_index = preset.traversal_low_band_index;
    seed.traversal_high_band_index = preset.traversal_high_band_index;
    seed.motion = preset.simulation_motion_profile;
    seed.water_damage_multiplier = preset.water_damage_multiplier;
    seed.water_damage_threshold_q12 = preset.water_damage_threshold_q12;
    seed.vertical_deployment_step_q12_per_tick = preset.vertical_deployment_step_q12_per_tick;
    seed.vertical_deployment_travel_limit_q12 = preset.vertical_deployment_travel_limit_q12;
    seed.class23_deployment_sound_asset_id = preset.runtime_class_effect_sound_asset_id_26c;
    seed.production_compatibility_mask = preset.runtime_class_specific_c4_raw;
    seed.production_storage_capacity = preset.runtime_class_specific_c4_raw;
    seed.class17_root_child_deployment_enabled = preset.runtime_uses_class17_root_child_deployment;
    seed.runtime_class_specific_c0_raw = preset.runtime_class_specific_c0_raw;
    seed.runtime_class_specific_c4_raw = preset.runtime_class_specific_c4_raw;
    seed.runtime_class_specific_19c_raw = preset.runtime_class_specific_19c_raw;
    seed.runtime_class_specific_1a8_raw = preset.runtime_class_specific_1a8_raw;
    seed.root_model_maximum_integrity = preset.root_maximum_integrity;
    seed.fog_uncover_radius_q12 = preset.fog_uncover_radius_q12;
    if (preset.runtime_model_class_id == 18U)
        if (const auto* effect = catalog.effect_definition(preset.runtime_class_specific_c4_raw))
            seed.class18_conversion_effect_lifetime_ticks = effect_model_completion_tick(*effect).value_or(0U);
    const auto hierarchy = resolve_faction_technology_hierarchy(preset,mask);
    seed.model_components = simulation_lab_model_components_from_hierarchy(hierarchy);
    for (const auto& node : hierarchy) if (node.creates_runtime_node()) {
        seed.occupancy_mark_radius_units = std::max(seed.occupancy_mark_radius_units,
            node.simulation.occupancy_mark_radius_units);
        seed.fog_reference_height_offset_q12 = std::max(seed.fog_reference_height_offset_q12,
            node.simulation.fog_reference_height_offset_q12);
    }
    for (const auto& entry : preset.technology_panel_entries)
        if (entry.technology_id != 0U) seed.research_technology_ids.push_back(entry.technology_id);
    const auto weapons=catalog.simulation_weapon_profiles_for_hierarchy(hierarchy);
    seed.actor.weapons.resize(weapons.size());
    for(const auto& weapon:weapons)
        if(const auto* shot=catalog.shot_definition(weapon.shot_definition_id))
            seed.combat_weapons.push_back({weapon,*shot});
    simulation_lab_prepare_model_technologies(preset,catalog,seed);
    return seed;
}
}

int main(int argc,char** argv) try {
    if (argc < 2 || argc > 6) throw std::runtime_error(
        "Usage: thandor_simlab_stock_replay GAME_ROOT [map=asgard] [ticks=3600] [runs=1] [diagnostic_xenite_and_storage_q4=LEV]");
    const std::string map = argc > 2 ? argv[2] : "asgard";
    // Opt-in lifecycle timestamps help isolate PCK loading from simulation time
    // without adding logging or contention to the ordinary replay path.
    const bool phase_log = std::getenv("THANDOR_SIMLAB_REPLAY_PHASES") != nullptr;
    const auto phase_start = std::chrono::steady_clock::now();
    const auto phase = [&](const char* name) {
        if (!phase_log) return;
        const auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - phase_start).count();
        std::cerr << "REPLAY_PHASE name=" << name << " elapsed_seconds=" << elapsed << '\n';
    };
    phase("before_pck_scan");
    auto catalog = AssetCatalog::scan_archive_root(argv[1]);
    phase("after_pck_scan");
    const auto group = std::find_if(catalog.map_groups().begin(),catalog.map_groups().end(),
        [&](const auto& g) { return g.display_name == map || g.level_path == map ||
            g.level_path == "level/" + map + ".lev"; });
    if (group == catalog.map_groups().end()) throw std::runtime_error("Map not found: " + map);
    phase("before_map_open");
    const auto map_group=*group;
    const auto registry_level=LevelAsset::parse(catalog.load(map_group.level_path));
    catalog=AssetCatalog::scan_archive_root(argv[1],registry_level);
    auto document = MapDocument::open(catalog,map_group);
    phase("after_map_open");
    const auto& field = document.field();
    const auto& level = document.level();
    const auto& masks = document.faction_technology_masks();
    for(const auto& t:catalog.technologies()) if(t.technology_id<=10U)
        std::cout << "TEC id=" << t.technology_id << " cost=" << t.xenite_cost_q4 << " time=" << t.research_duration_q5 << " score=" << t.ai_base_candidate_score << " prereq=" << t.prerequisite_masks[0] << '\n';
    const auto knowledge = AiKnowledgeData::parse(catalog.load("engine/ki.dat"));
    for(uint32_t i=1;i<24U;++i) std::cout << "KI " << i << "=" << knowledge.parameters()[i] << '\n';
    std::vector<SimulationLabSeedEntity> seeds;
    for (size_t i = 0; i < level.placements().size(); ++i) {
        const auto& p = level.placements()[i];
        const auto* preset = catalog.army_preset(p.army_asset_id);
        if (!preset || !simulation_lab_faction_placement_has_runtime_owner(*preset,p.faction_index)) continue;
        auto seed = seed_for(*preset,catalog,masks[p.faction_index]);
        seed.source_placement_index = i;
        seed.faction_index = p.faction_index;
        seed.actor.position = {p.render_x(),p.render_y(),field.sample_terrain(p.render_x(),p.render_y()).height};
        seed.actor.body_yaw_turn16 = p.orientation_turn16 & 0xFFFFU;
        if (seed.runtime_model_class_id == 17U)
            simulation_initialize_class17_deployment(seed.actor, false,
                seed.class17_root_child_deployment_enabled);
        seeds.push_back(std::move(seed));
    }
    std::vector<SimulationLabProductionAsset> products;
    for (const auto& p : catalog.army_presets()) {
        if (p.registry_id == 0U) continue;
        if(p.registry_id>=300U&&p.registry_id<=306U)
            std::cout << "PRODUCT arm=" << p.registry_id << " cost=" << p.retail_xenite_cost_q4 << " flags=" << p.army_runtime_flags14_raw << " grant=" << p.linked_technology_id << '\n';
        SimulationLabProductionAsset a;
        a.army_asset_id = p.registry_id;
        a.army_runtime_flags14_raw = p.army_runtime_flags14_raw;
        a.ai_definition_class_value70 = p.ai_definition_class_value70;
        a.ai_definition_class_value74 = p.ai_definition_class_value74;
        a.ai_definition_class_value78 = p.ai_definition_class_value78;
        a.runtime_class_specific_c0_raw = p.runtime_class_specific_c0_raw;
        a.xenite_cost_q4 = p.retail_xenite_cost_q4;
        a.build_duration_ticks = p.retail_build_time_q5;
        a.energy_demand_q4 = static_cast<int32_t>(p.retail_build_energy_q4);
        a.placement_effect_id = p.construction_destruction_effect_id;
        a.entity_seed = seed_for(p,catalog,masks[0]);
        simulation_lab_prepare_ai_product(p,catalog,a);
        products.push_back(std::move(a));
    }
    const bool audit_army = std::getenv("THANDOR_SIMLAB_ARMY_AUDIT") != nullptr;
    if (audit_army) for (const auto& a : products) {
        if(a.army_asset_id >= 300U && a.army_asset_id <= 339U) continue;
        if((a.army_runtime_flags14_raw & 1U)==0U) continue;
        std::ostringstream req;
        for(const auto& group : a.entity_seed.linked_model_technologies) {
            const auto it=std::find_if(group.variants.begin(),group.variants.end(),
                [](const auto& v){return v.variant_index==0U;});
            if(it!=group.variants.end()) req << it->required_technology_id << ',';
        }
        std::cout << "AUDIT_ASSET id=" << a.army_asset_id
                  << " class=" << a.entity_seed.runtime_model_class_id
                  << " flags=" << a.army_runtime_flags14_raw
                  << " cost=" << a.xenite_cost_q4
                  << " req=" << req.str()
                  << " stat70=" << a.ai_definition_class_value70
                  << " stat74=" << a.ai_definition_class_value74
                  << " stat78=" << a.ai_definition_class_value78 << '\n';
    }
    phase("before_technology");
    const auto technologies=simulation_lab_prepare_technologies(catalog,&level);
    std::array<SimulationLabFactionEconomySeed,8> economy{};
    for (uint32_t f = 1; f < 8; ++f) {
        auto& e = economy[f];
        e.xenite_current_q4 = argc>5?static_cast<uint32_t>(std::stoul(argv[5])):level.faction_xenite_q4(f);
        e.tritium_current_q4 = level.faction_tritium_q4(f);
        e.baseline_xenite_storage_limit_q4 = argc>5?std::max(kSimulationLabRetailInitialStorageLimitQ4,
            static_cast<uint32_t>(std::stoul(argv[5]))):kSimulationLabRetailInitialStorageLimitQ4;
        e.baseline_tritium_storage_limit_q4 = kSimulationLabRetailInitialStorageLimitQ4;
        e.baseline_energy_supply_q4 = kSimulationLabRetailInitialEnergySupplyQ4;
        e.baseline_energy_generation_capacity_q4 = kSimulationLabRetailInitialEnergyGenerationCapacityQ4;
        e.derive_storage_limits_from_components = true;
        e.derive_energy_allocation_from_components = true;
        e.terrain_contribution_scale_q8 = 0x100U; // same documented GUI proxy
    }
    SimulationLabScenarioRuntimeSeed scenario;
    scenario.static_navigation_blockers=simulation_lab_prepare_static_navigation_blockers(catalog,level);
    phase("before_runtime_catalog");
    scenario.runtime_catalog=simulation_lab_prepare_runtime_catalog(catalog);
    phase("after_runtime_catalog");
    const auto environment=simulation_lab_prepare_environment(catalog,level,field,*scenario.runtime_catalog);
    scenario.crushable_scenery=environment.crushable_scenery;
    scenario.environmental_emitters=environment.emitters;scenario.static_shot_hulls=environment.collision_hulls;scenario.static_placement_owners=environment.placement_owners;
    scenario.initial_army_placements=environment.initial_army_placements;
    scenario.enabled = true;
    scenario.terrain_lighting_cycle_active=level.lighting().cycle_duration_ticks!=0U;
    scenario.assignable_faction_count = level.frontend_assignable_faction_count();
    scenario.active_faction_count = level.frontend_active_faction_count();
    scenario.simulate_all_active_factions_as_ai = true;
    scenario.relation_ui_flags = level.relation_ui_flags();
    scenario.relation_state8_faction_group_masks = level.relation_state8_faction_group_masks();
    scenario.relation_state4_faction_group_masks = level.relation_state4_faction_group_masks();
    for (uint32_t i = 0; i < scenario.scheduled_conditions.size(); ++i)
        scenario.scheduled_conditions[i] = level.scheduled_condition(i);
    for (uint32_t i = 0; i < scenario.end_condition_triggers.size(); ++i)
        scenario.end_condition_triggers[i] = level.end_condition_trigger(i);
    auto config=simulation_lab_loaded_baseline_config(knowledge.parameters());
    config.run_count = argc > 4 ? static_cast<uint32_t>(std::stoul(argv[4])) : 1U;
    config.duration_ticks = argc > 3 ? static_cast<uint32_t>(std::stoul(argv[3])) : 3600U;
    config.base_seed = 1414024772U;
    config.vary_seed_per_run = true;
    const char* audit_dir=std::getenv("THANDOR_SIMLAB_FACTION_AUDIT_DIR");
    if(audit_dir){
        if(config.run_count!=1)throw std::runtime_error("Faction audit requires exactly one run");
        scenario.human_faction_mask=0U;
        scenario.frontend_player_counts=std::array<uint32_t,8>{};
        if(scenario.active_faction_count<1||scenario.active_faction_count>7)throw std::runtime_error("Faction audit requires 1..7 active factions");
        config.base_seed=std::getenv("THANDOR_SIMLAB_AUDIT_SEED")?uint32_t(std::stoul(std::getenv("THANDOR_SIMLAB_AUDIT_SEED"))):12345U;
        config.vary_seed_per_run=false;
        config.runtime_callback_step_ticks=1;
        config.worker_thread_limit=1;
    }
    // Replay repeats the loaded baseline; a run count is not an implicit KI sweep.
    SimulationLabExperiment experiment;
    experiment.reset(config,knowledge.parameters(),seeds,economy,products,technologies,masks,{},scenario);
    experiment.prepare_navigation_terrain(field);
    phase("after_navigation_preparation");
    std::cout << "THANDOR STOCK-ASSET CORE REPLAY (not retail, not GUI replay)\n"
              << "START map=" << map << " ticks=" << config.duration_ticks << " runs=" << config.run_count
              << " parameter=" << config.parameter_index << " fixed_value=" << config.fixed_value
              << " xenite_override=" << (argc>5?argv[5]:"none") << " seed_entities=" << seeds.size() << " products=" << products.size() << '\n';
    const auto start = std::chrono::steady_clock::now();
    // Opt-in progression timeline changes only observation granularity.
    const bool audit_progression=std::getenv("THANDOR_SIMLAB_PROGRESSION_AUDIT")!=nullptr;
    if(audit_dir){
        const double share=std::getenv("THANDOR_SIMLAB_AUDIT_CPU_SHARE")?std::stod(std::getenv("THANDOR_SIMLAB_AUDIT_CPU_SHARE")):0.5;
        if(!std::isfinite(share)||share<=0||share>1)throw std::runtime_error("Audit CPU share must be in (0,1]");
        audit::FactionAudit observation(audit_dir,catalog,level,share);
        observation.observe(experiment.runs()[0]);
        while(!experiment.finished()){
            experiment.step(field,1);
            observation.observe(experiment.runs()[0]);
            if(experiment.runs()[0].tick%20==0)observation.pace();
        }
        observation.finish();
    }else if(audit_progression){
        std::vector<std::array<std::array<bool,512>,8>> seen(config.run_count);
        for(uint32_t tick=0;tick<config.duration_ticks&&!experiment.finished();++tick){
            experiment.step(field,1);
            for(std::size_t run_index=0;run_index<experiment.runs().size();++run_index){
                const auto& run=experiment.runs()[run_index];
                for(const auto& entity:run.entities){
                    const auto id=entity.army_asset_id,faction=entity.faction_index;
                    if(faction>=8U||id>=512U||!entity.construction_complete||entity.root_model_terminal)continue;
                    if(!((id>=302U&&id<=306U)||(!entity.building&&id<300U)))continue;
                    if(seen[run_index][faction][id])continue;
                    seen[run_index][faction][id]=true;
                    std::cout<<"PROGRESSION run="<<run_index<<" tick="<<run.tick
                        <<" faction="<<faction<<" arm="<<id<<" class="<<entity.runtime_model_class_id
                        <<" building="<<entity.building<<'\n';
                }
            }
        }
    }else experiment.step(field,config.duration_ticks);
    phase("after_simulation_step");
    for (size_t i = 0; i < experiment.runs().size(); ++i) {
        const auto& run = experiment.runs()[i];
        std::cout << "RUN index=" << i << " tick=" << run.tick << " built=" << run.metrics.completed_builds
                  << " military_sites=" << run.metrics.military_ai_site_orders
                  << " military_anchors=" << run.metrics.military_ai_anchor_orders
                  << " military_groups=" << run.metrics.military_ai_group_orders
                  << " military_selector=" << run.metrics.military_ai_selector_calls
                  << " military_busy=" << run.metrics.military_ai_busy_skips
                  << " unseen_group_targets_bounded=" << run.metrics.military_ai_unseen_group_targets_bounded
                  << " unseen_group_targets_unavailable=" << run.metrics.military_ai_unseen_group_targets_unavailable
                  << " alert_updates=" << run.metrics.impact_anchor_updates
                  << " alert_decay_steps=" << run.metrics.impact_anchor_decay_steps
                  << " alert_expirations=" << run.metrics.impact_anchor_expirations
                  << " military_movement=" << run.metrics.military_movement_distance_world
                  << " units_spawned=" << run.metrics.class13_units_spawned << " movement=" << run.metrics.movement_distance_world << " targets=" << run.metrics.target_assignments << " research=" << run.metrics.research_jobs_completed
                  << " conversions_completed=" << run.metrics.pioneer_conversions_completed
                  << " extractor_site_tests=" << run.metrics.live_extractor_site_tests
                  << " extractor_site_accepts=" << run.metrics.live_extractor_site_accepts
                  << " reservation_tests=" << run.metrics.special_site_reservation_tests
                  << " reservation_accepts=" << run.metrics.special_site_reservation_accepts
                  << " probe_owners=" << run.metrics.special_site_probe_owners
                  << " storage_site_bonus=" << run.metrics.live_storage_site_bonus_events << '\n';
        for (uint32_t f = 1; f <= scenario.active_faction_count; ++f) {
            uint32_t buildings = 0, pioneers = 0;
            for (const auto& e : run.entities) if (e.faction_index == f && !e.root_model_terminal) {
                buildings += e.building;
                pioneers += e.runtime_model_class_id == 18;
                if (e.runtime_model_class_id == 18)
                    std::cout << "PIONEER run=" << i << " faction=" << f << " commands=" << e.pioneer_ai_command_count
                              << " special=" << e.pioneer_gate_trace.special_flag_calls
                              << " gates=" << e.pioneer_gate_trace.calls << " x=" << e.actor.position.x
                              << " y=" << e.actor.position.y << '\n';
            }
            std::cout << "FACTION run=" << i << " f=" << f << " buildings=" << buildings
                      << " pioneers=" << pioneers << " xenite=" << run.faction_economy[f].xenite_current_q4
                      << " capacity=" << run.faction_energy_generation_capacity_q4[f] << " supplied_demand=" << run.faction_supplied_energy_demand_q4[f] << " unpowered=" << run.faction_unpowered_energy_demand_q4[f] << " fuelrate=" << run.faction_tritium_extraction_rate_q4_per_tick[f]
                      << " tec1=" << faction_technology_unlocked(run.faction_technology_masks[f],1U)
                      << " research_completed=" << run.metrics.research_jobs_completed << '\n';
            const auto& block=run.ai_purchase_block_diagnostics[f];
            std::cout << "BLOCKER f=" << f << " calls=" << block.calls
                      << " storage_ceiling=" << block.ceiling_blocks
                      << " balance_shortage=" << block.balance_blocks
                      << " last_arm=" << block.last_candidate_id
                      << " last_cost=" << block.last_cost_q4
                      << " last_balance=" << block.last_balance_q4
                      << " last_storage=" << block.last_storage_cap_q4
                      << " last_cached=" << block.last_cached << '\n';
            for(uint32_t j=0;j<run.ai_candidate_cache_count[f];++j)
                std::cout << "CACHE f=" << f << " id=" << run.ai_candidate_cache[f][j].id << " kind=" << run.ai_candidate_cache[f][j].kind << " score=" << run.ai_candidate_cache[f][j].packed_score << '\n';
            std::cout << "MASK f=" << f << " bits=" << run.faction_technology_masks[f][0] << '\n';
            const auto& research=run.ai_research_trace[f];
            std::cout << "RESEARCH f=" << f << " tick=" << research.last_rebuild_tick
                << " arm330=" << research.arms_factory_present << " sources=" << research.source_count
                << " admitted=" << research.admitted_candidates << " positive=" << research.positive_candidates
                << " cap=" << research.candidate_cap_reached << " best=" << research.best_technology_id
                << " score=" << research.best_score << " purchase_tick=" << research.last_purchase_tick
                << " purchase=" << research.last_purchase_technology_id << " queued=" << research.last_purchase_queued
                << " completed=" << research.last_completed_technology_id << '\n';
            if (audit_army) {
                const auto& trace=run.ai_army_profile_trace[f];
                std::cout << "AUDIT_SCORE faction=" << f << " tick=" << trace.last_rebuild_tick
                    << " candidates=" << trace.workspace11_count
                    << " tracked=" << trace.eligible_tracked
                    << " walkers=" << trace.eligible_walkers
                    << " gliders=" << trace.eligible_gliders
                    << " aircraft=" << trace.eligible_aircraft
                    << " naval=" << trace.eligible_naval
                    << " turrets=" << trace.eligible_turrets;
                for(std::size_t profile=0; profile<3; ++profile)
                    std::cout << " best" << profile << "=" << trace.best_asset_id[profile]
                        << " score" << profile << "=" << trace.best_score[profile]
                        << " alt" << profile << "=" << trace.best_advanced_asset_id[profile]
                        << " altscore" << profile << "=" << trace.best_advanced_score[profile]
                        << " calls" << profile << "=" << trace.profile_invocations[profile];
                std::cout << '\n';
            }
            if (audit_army) {
                std::uint32_t factory_mask=0U;
                for(const auto& e:run.entities) if(e.faction_index==f &&
                    e.construction_complete && !e.root_model_terminal) {
                    if(e.runtime_model_class_id==11U) factory_mask|=0x10U;
                    else if(e.runtime_model_class_id==13U) factory_mask|=e.production_compatibility_mask;
                    else if(e.runtime_model_class_id==22U) factory_mask|=0x08U;
                }
                std::cout << "AUDIT_FACTORY faction=" << f << " mask=" << factory_mask << '\n';
                for(const auto& a:products) if(a.army_asset_id<300U || a.army_asset_id>339U) {
                    if(!(a.army_runtime_flags14_raw&1U)) continue;
                    const bool producer=(a.army_runtime_flags14_raw&factory_mask)!=0U;
                    const auto seed=simulation_lab_current_ai_score_seed(a,run.faction_technology_masks[f],f);
                    if(producer || (a.army_asset_id<100U && seed.all_selected_model_technologies_unlocked))
                        std::cout << "AUDIT_AVAILABLE faction=" << f
                          << " arm=" << a.army_asset_id
                          << " producer=" << producer
                          << " tech=" << seed.all_selected_model_technologies_unlocked
                          << " workspace11=" << ai_v601_workspace11_candidate(
                              a.army_asset_id, a.army_runtime_flags14_raw,
                              factory_mask, seed.all_selected_model_technologies_unlocked)
                          << " resolved=" << seed.resolved
                          << " class=" << a.entity_seed.runtime_model_class_id << '\n';
                }
            }
            for(const auto& e:run.entities) if(e.faction_index==f)
                std::cout << "ENTITY f=" << f
                    << " military_action=" << e.military_ai_command_kind
                    << " military_site_score=" << e.military_ai_last_choice.best_site_score
                    << " military_anchor_score=" << e.military_ai_last_choice.best_anchor_score
                    << " military_hostile_score=" << e.military_ai_last_choice.best_hostile_score << " arm=" << e.army_asset_id << " class=" << e.runtime_model_class_id << " building=" << e.building << " terminal=" << e.root_model_terminal << " speed=" << e.motion.speed_q12_per_tick << " yaw_max=" << e.motion.yaw_max_velocity_turn16 << " yaw_acc=" << e.motion.yaw_acceleration_turn16 << " root_flags=" << (e.model_components.empty()?0:e.model_components[0].runtime_flags_ec) << " hp=" << e.root_model_integrity << " flags=" << e.runtime_flags
                          << " research=" << e.research_technology_id << " active=" << e.research_active
                          << " elapsed=" << e.research_elapsed_ticks << " duration=" << e.research_duration_ticks
                          << " panel=" << e.research_technology_ids.size() << " panel0=" << (e.research_technology_ids.empty()?0:e.research_technology_ids[0])
                          << " linked=" << e.production_linked_to_parent << " target=" << e.actor.move_target.has_value()
                          << " x=" << e.actor.position.x << " y=" << e.actor.position.y << '\n';
        }
    }
    std::cout << "ELAPSED_SECONDS " << std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count() << '\n';
    std::cout << "LIMIT: shared core with catalog seeds; GUI full pose transforms and extraction offsets are not fully prepared here; targeting/navigation remain Lab approximations.\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
