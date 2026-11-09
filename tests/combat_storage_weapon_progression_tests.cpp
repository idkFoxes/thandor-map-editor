#include "thandor/simulation_lab_catalog.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include "thandor/simulation_storage.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
}
int main(int argc,char**argv)try{
    for(uint32_t cls:{7U,8U}){
        SimulationActorState actor;SimulationWeaponState weapon_state;
        SimulationWeaponProfile profile;profile.runtime_class_id=cls;profile.shot_definition_id=2;
        profile.reload_ticks=5;profile.launch_attachment_count=1;profile.recoil_duration_ticks=0;
        profile.recoil_rotation_step_turn16=1365;
        ShotDefinitionRecord shot;shot.definition_id=2;
        weapon_state.cooldown_ticks=5;weapon_state.recoil_angle_offset_turn16=65500;
        for(unsigned tick=0;tick<5;++tick)
            simulation_tick_weapon(actor,weapon_state,profile,shot,{},nullptr,true,UINT32_MAX,{},false,true);
        check(weapon_state.cooldown_ticks==0&&weapon_state.recoil_angle_offset_turn16==6789,
            "barrel spin did not follow the independent reload clock or wrap at 16 bits");
        simulation_tick_weapon(actor,weapon_state,profile,shot,{},nullptr,true,UINT32_MAX,{},false,true);
        check(weapon_state.recoil_angle_offset_turn16==6789,"completed reload discarded cumulative barrel rotation");
    }

    {
        SimulationActorState actor;SimulationWeaponState state;state.pitch.angle_turn16=0;
        SimulationWeaponProfile p;p.runtime_class_id=8;p.shot_definition_id=6;p.reload_ticks=1;
        p.maximum_range_q12=100000;p.launch_attachment_count=2;p.recoil_duration_ticks=7;
        p.launch_model_node_indices={3U,4U};p.launch_policy=SimulationLaunchPolicy::AlternateLaunchNodes;
        p.pitch_min_turn16=-32768;p.pitch_max_turn16=32767;
        ShotDefinitionRecord shot;shot.definition_id=6;shot.trajectory_mode=0;
        const Vec3f target{10,0,0};
        const auto first=simulation_tick_weapon(actor,state,p,shot,{},&target,true,UINT32_MAX,{},false,true);
        const auto second=simulation_tick_weapon(actor,state,p,shot,{},&target,true,UINT32_MAX,{},false,true);
        check(first.fired&&second.fired&&first.fired_attachment_selector==1&&second.fired_attachment_selector==0,
            "double barrel did not fire 1,0 in native order");
        check(state.alternate_barrel_recoil_ticks[0]==7&&state.alternate_barrel_recoil_ticks[1]==6,
            "overlapping double-barrel recoil clocks were merged");
        for(const auto heading:{0x400U,0xFC00U}){
            SimulationWeaponState boundary;
            boundary.yaw.angle_turn16=heading;
            boundary.pitch.angle_turn16=0U;
            const auto outside=simulation_tick_weapon(actor,boundary,p,shot,{},&target,true,
                UINT32_MAX,{},false,true);
            check(!outside.aligned&&!outside.fired,"native turret fired at a yaw residual of 1024");
            boundary.yaw.angle_turn16=heading==0x400U?0x3FFU:0xFC01U;
            boundary.yaw.velocity_turn16=0;
            const auto inside=simulation_tick_weapon(actor,boundary,p,shot,{},&target,true,
                UINT32_MAX,{},false,true);
            check(inside.aligned&&inside.fired,"native turret rejected a yaw residual of 1023");
        }
    }
    ModelVisualNode tank;tank.runtime_class_id=15;
    tank.simulation.pitch_min_turn16=-500;tank.simulation.pitch_max_turn16=1000;
    check(simulation_storage_child_z_q12(tank,0,0,100)==-500,"empty tank height");
    check(simulation_storage_child_z_q12(tank,0,50,100)==250,"half tank height");
    check(simulation_storage_child_z_q12(tank,0,100,100)==1000,"full tank height");
    check(simulation_storage_child_z_q12(tank,0,150,100)==1750,"retail ratio was clamped");
    check(!simulation_storage_child_z_q12(tank,1,50,100),"wrong child animated");
    check(!simulation_storage_child_z_q12(tank,0,50,0),"zero capacity divided");
    tank.simulation.pitch_min_turn16=1000;tank.simulation.pitch_max_turn16=-501;
    check(simulation_storage_child_z_q12(tank,0,1,2)==250,"negative signed division must truncate toward zero");
    tank.runtime_class_id=14;
    check(!simulation_storage_child_z_q12(tank,0,50,100),"extractor reused class15 channel");
    {
        const auto field=FieldGrid::create(59,59,0,0);
        std::array<uint32_t,AiKnowledgeData::kParameterCount> p{};
        p[4]=100000;p[21]=1000;p[23]=1000;
        std::array<SimulationLabFactionEconomySeed,8> economy{};
        economy[1].tritium_current_q4=900;economy[1].tritium_storage_limit_q4=900;
        SimulationLabSeedEntity extractor;extractor.army_asset_id=332;
        extractor.faction_index=1;extractor.building=true;extractor.actor.position={8,8,0};
        SimulationLabSeedEntity constructor=extractor;constructor.army_asset_id=300;
        constructor.runtime_model_class_id=11;constructor.actor.position={12,8,0};
        const std::array seeds{extractor,constructor};
        SimulationLabConfig c;c.run_count=1;c.duration_ticks=100;c.parameter_index=100;
        for(bool busy:{false,true}) {
            SimulationLabExperiment e;e.reset(c,p,seeds,economy);
            auto& producer=e.runs()[0].entities[1];
            producer.production_building=busy;producer.production_asset_id=busy?333:0;
            producer.production_duration_ticks=1000;
            e.step(field,74);
            check(e.runs()[0].metrics.candidate_builds[3]==(busy?0U:1U),
                "active storage construction did not suppress a duplicate request");
        }
        // A free resource island is not the retail four-probe storage-site query.
        // Compare authored planning with the explicit legacy fixture behavior.
        auto support=field;Brush brush;brush.radius_cells=3;
        brush.shape=BrushShape::Square;brush.falloff=BrushFalloff::Constant;
        support.paint_placement_occupancy_class(20,20,brush,0,true);
        p={};p[5]=100000;
        for(bool authored:{false,true}) {
            SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=authored;
            scenario.active_faction_count=1;scenario.simulate_all_active_factions_as_ai=true;
            SimulationLabExperiment e;e.reset(c,p,seeds,{}, {}, {}, {}, {},scenario);
            e.step(support,74);
            check(e.runs()[0].metrics.derived14d_weight_events==(authored?0U:1U),
                "authored planning used the unsupported free-site storage bonus");
        }
        SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;
        scenario.active_faction_count=1;scenario.simulate_all_active_factions_as_ai=true;
        p={};p[4]=100000;p[21]=1000;p[23]=1000;
        SimulationLabExperiment e;e.reset(c,p,seeds,economy,{}, {}, {}, {},scenario);
        e.step(field,74);
        check(e.runs()[0].metrics.candidate_builds[3]==1,
            "authored capacity threshold no longer requests needed storage");
    }
    if(argc==2){
        const auto catalog=AssetCatalog::scan_game_root(argv[1]);
        const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
        check(runtime->effects.contains(51U)&&runtime->effects.contains(21U),
              "terminal effect without completion omitted its linked emission chain");
        check(runtime->sprites.contains(runtime->effects.at(51U).definition.sprite_path)&&
              !runtime->sprites.at(runtime->effects.at(51U).definition.sprite_path).damage_effect_points.empty(),
              "terminal linked-effect attachment geometry was not prepared");
        FactionTechnologyMask256 mask{};mask[0]=3;mask[6]=278003968;
        const auto* jeep=catalog.army_preset(1);check(jeep,"stock armed jeep missing");
        const auto hierarchy=resolve_faction_technology_hierarchy(*jeep,mask);
        check(hierarchy.size()>1,"unresolved TEC0 upgrade erased the weapon child");
        for(const auto& group:jeep->model_variant_groups)for(const auto& variant:group.variants)
            check(runtime->definitions.contains(variant.model_definition_id),"unknown MDL admitted as an unlocked variant");
        SimulationLabSeedEntity source;source.army_asset_id=1;source.faction_index=1;source.actor.position={2,-2,3};
        SimulationLabSeedEntity target;target.army_asset_id=333;target.faction_index=2;
        target.actor.position={5,-2,3};target.building=true;
        SimulationLabEntity entity;
        check(simulation_lab_initialize_runtime_entity(entity,source,mask,*runtime,true),"jeep runtime initialization failed");
        check(!entity.combat_weapons.empty(),"armed jeep spawned without weapons");
        SimulationLabConfig config;config.run_count=1;config.duration_ticks=64;config.parameter_index=100;
        std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
        std::array<FactionTechnologyMask256,8> masks{};masks[1]=mask;masks[2]=mask;
        SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=runtime;
        SimulationLabExperiment experiment;const std::array seeds{source,target};
        experiment.reset(config,parameters,seeds,{}, {}, {},masks,{},scenario);
        const auto field=FieldGrid::create(16,16,0,0);
        experiment.step(field,64);
        check(experiment.runs()[0].metrics.weapon_shots_fired>0,"armed jeep did not fire at nearby enemy tank in 64 ticks");
        {
            SimulationLabSeedEntity placed;placed.army_asset_id=2;placed.faction_index=1;
            placed.actor.weapons.resize(1); // Editor preview starts at pitch 0x4000.
            SimulationLabEntity authored;authored.army_asset_id=2;authored.faction_index=1;
            check(simulation_lab_initialize_runtime_entity(authored,placed,mask,*runtime,true),"placed weapon reference setup failed");
            placed.combat_weapons=authored.combat_weapons;
            auto level_scenario=scenario;level_scenario.enabled=true;
            level_scenario.active_faction_count=2;level_scenario.ai_planning_enabled=false;
            SimulationLabExperiment placed_lab;const std::array placed_seeds{placed};
            placed_lab.reset(config,parameters,placed_seeds,{}, {}, {},masks,{},level_scenario);
            const auto& actual=placed_lab.runs()[0].entities[0].actor.weapons;
            check(actual.size()==authored.actor.weapons.size()&&!actual.empty(),"placed weapon state absent");
            check(authored.actor.weapons[0].pitch.angle_turn16==0U&&
                  actual[0].pitch.angle_turn16==0U,"placed weapon retained quarter-turn preview pitch");
        }
        const auto* crate=catalog.army_preset(751);
        check(crate&&crate->simulation_motion_profile.collision_radius_q12==0&&
              simulation_lab_crushable_scenery(*crate,0),
              "shootable zero-footprint crate remained an immutable collision hull");
        const auto* storage=catalog.army_preset(333);check(storage,"stock Tritium tank missing");
        const auto h=resolve_faction_technology_hierarchy(*storage,mask);
        check(h.size()>1&&simulation_storage_child_z_q12(h[0],0,100,200)==500,"stock tank endpoint decoding incorrect");
        SimulationLabSeedEntity tank_seed;tank_seed.army_asset_id=333;tank_seed.faction_index=1;
        tank_seed.actor.position={8,-8,0};
        std::array<SimulationLabFactionEconomySeed,8> tank_economy{};
        tank_economy[1].tritium_current_q4=100;
        tank_economy[1].tritium_storage_limit_q4=200;
        tank_economy[1].tritium_storage_limit_locked=true;
        SimulationLabExperiment fill;const std::array tank_seeds{tank_seed};
        fill.reset(config,parameters,tank_seeds,tank_economy,{}, {},masks,{},scenario);
        fill.runs()[0].ai_planning_enabled=false;
        fill.step(field,1);
        const auto fill_height=[&]{const auto& e=fill.runs()[0].entities[0];
            const auto p=simulation_lab_current_model_poses(e,field,*runtime);
            return float_to_q12(p[1].position.z-p[0].position.z);};
        check(fill_height()==500,"combat storage hull did not follow the resource fill callback");
        fill.runs()[0].faction_economy[1].tritium_current_q4=0;
        check(fill_height()==500,"storage pose changed before its model callback");
        fill.step(field,1);
        check(fill_height()==0,"empty storage retained its previous collision height");
        SimulationLabSeedEntity factory_seed;factory_seed.army_asset_id=303;factory_seed.faction_index=1;
        factory_seed.actor.position={8,-8,0};SimulationLabEntity factory;
        check(simulation_lab_initialize_runtime_entity(factory,factory_seed,mask,*runtime,true),"factory initialization failed");
        simulation_lab_prepare_missing_model_geometry(factory,field,*runtime);
        check(factory.production_spawn_world&&factory.production_exit_world,
              "new idle factory did not expose its placement markers");
        std::cout<<"Stock ARM1 weapon retained; nearby enemy fired upon in 64 ticks; ARM333 fill endpoints PASS\n";
    }
    std::cout<<"V610 storage fill and known-definition selection PASS\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
