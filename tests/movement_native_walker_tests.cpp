#include "thandor/simulation_native_walker.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Dependencies {
    MovementResult route(){return {{227159,-69622},false};}
    std::optional<int32_t> terrain(int32_t,int32_t){return 15179;}
    bool free(int32_t,int32_t){return true;}
    std::optional<MovementPoint> command(){return {};}
    void reached(){}
    void completed(size_t side){require(side==0U,"first completed contact was not the left foot");++completions;}
    unsigned completions{};
};
void half_turn_contact_side_case(){
    // Native S08/12345, slot15 ARM113, ticks14764..14767. The two native
    // half-turn comparisons deliberately select different contact sides.
    SimulationActorState actor;actor.position={q12_to_float(272134),q12_to_float(-159671),q12_to_float(32541)};
    actor.body_yaw_turn16=32768;
    actor.articulated_foot_anchor_world={Vec3f{q12_to_float(272134),q12_to_float(-159842),q12_to_float(31347)},
        Vec3f{q12_to_float(272134),q12_to_float(-159500),q12_to_float(31287)}};
    actor.articulated_foot_target_world=actor.articulated_foot_anchor_world;
    auto& state=actor.articulated_native;state.initialized=true;state.flags=32836;
    state.start_heading=state.end_heading=32768;state.speed=state.restart_speed=184;state.step_rate=11468;
    SimulationMotionProfile motion;motion.speed_q12_per_tick=132;motion.ground_acceleration_q12=102;
    struct TurnDependencies {
        MovementResult route(){return {{273862,-159671},false};}
        std::optional<int32_t> terrain(int32_t,int32_t){return 31335;}
        bool free(int32_t,int32_t){return true;}
        std::optional<MovementPoint> command(){return {};}
        void reached(){} void completed(size_t){}
    } deps;
    simulation_tick_native_walker(actor,motion,1400,250,16384,171,0,1224,0,1,deps);
    require(state.flags==536903706U&&state.step_rate==7992,"half-turn planner lost native side mismatch");
    require(float_to_q12(actor.articulated_foot_target_world[0].x)==272561&&
            float_to_q12(actor.articulated_foot_target_world[0].y)==-159927,"half-turn planned the wrong foot target");
    for(const auto& expected:std::array<std::array<uint32_t,3>,2>{{{359,32580,33486},{917,32627,34602}}}){
        simulation_tick_native_walker(actor,motion,1400,250,16384,171,0,1224,0,1,deps);
        require(state.progress[0]==0&&state.progress[1]==expected[0],"half-turn advanced the planned foot instead of native right contact");
        require(float_to_q12(actor.position.x)==272134&&float_to_q12(actor.position.y)==-159671&&
                float_to_q12(actor.position.z)==int32_t(expected[1])&&actor.body_yaw_turn16==expected[2],
                "half-turn body pose differs from captured native frames");
    }
}
void rejected_closing_contact_keeps_own_height(){
    // Deja Vu, ARM155 slot121. The next right contact is over an invalid
    // sample. Native changes its XY and keeps the right foot's Z and rate.
    const auto world=[](int32_t x,int32_t y,int32_t z){
        return Vec3f{q12_to_float(x),q12_to_float(y),q12_to_float(z)};
    };
    SimulationActorState actor;actor.position=world(133728,-226425,7967);
    actor.body_yaw_turn16=48062;
    actor.articulated_foot_anchor_world={world(134031,-227160,6713),world(133426,-225689,6500)};
    actor.articulated_foot_target_world=actor.articulated_foot_anchor_world;
    auto& state=actor.articulated_native;state.initialized=true;state.flags=32804;
    state.start_heading=state.end_heading=48062;
    state.speed=state.restart_speed=239;state.step_rate=3166;
    SimulationMotionProfile motion;motion.speed_q12_per_tick=96;motion.ground_acceleration_q12=25;
    struct InvalidContact {
        MovementResult route(){return {{135850,-219141},false};}
        std::optional<int32_t> terrain(int32_t,int32_t){return {};}
        bool free(int32_t,int32_t){return true;}
        std::optional<MovementPoint> command(){return {};}
        void reached(){} void completed(size_t){}
    } deps;
    simulation_tick_native_walker(actor,motion,1400,250,16384,383,0,1361,0,1,deps);
    const auto right=actor.articulated_foot_target_world[1];
    require(state.flags==32842&&state.step_rate==3166&&
            float_to_q12(right.x)==133269&&float_to_q12(right.y)==-227082&&
            float_to_q12(right.z)==6500,"invalid closing contact copied the other foot's height");
    simulation_tick_native_walker(actor,motion,1400,250,16384,383,0,1361,0,1,deps);
    require(state.progress[1]==184&&float_to_q12(actor.position.z)==7988,
            "invalid closing contact changed the native suspension height");
}
void hidden_command_does_not_start_turn(){
    const auto field=FieldGrid::create(80,80,0,0);
    const auto& cell=field.at(40,40);
    SimulationLabSeedEntity walker;walker.army_asset_id=1;walker.faction_index=1;
    walker.runtime_model_class_id=3;walker.root_model_maximum_integrity=100;
    walker.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),q12_to_float(1224)};
    walker.motion.speed_q12_per_tick=132;walker.motion.ground_acceleration_q12=102;
    walker.articulated_contact_geometry_enabled=true;walker.articulated_root_height_q12=1224;
    walker.articulated_contact_interpolation_rate_q12=1400;walker.articulated_contact_lift_q12=250;
    walker.articulated_contact_steering_limit_turn16=4096;
    auto target=walker;target.army_asset_id=2;target.faction_index=2;target.runtime_model_class_id=2;
    target.articulated_contact_geometry_enabled=false;target.motion={};target.actor.position.x-=10;
    const std::array seeds{walker,target};
    auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();
    SimulationLabScenarioRuntimeSeed scene;scene.enabled=true;scene.active_faction_count=2;
    scene.ai_planning_enabled=false;scene.runtime_catalog=catalog;
    SimulationLabConfig config;config.run_count=1;config.duration_ticks=1;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabExperiment lab;lab.reset(config,parameters,seeds,{},{},{},{},{},scene);
    auto& run=lab.runs()[0];auto& source=run.entities[0];
    auto nodes=std::make_shared<std::vector<ModelVisualNode>>(1);(*nodes)[0].parent_index=UINT32_MAX;
    (*nodes)[0].runtime_class_id=3;(*nodes)[0].model_definition_id=1;source.runtime_model_hierarchy=nodes;
    auto& state=source.actor.articulated_native;state.initialized=true;state.speed=state.restart_speed=184;
    source.actor.articulated_contacts_initialized=true;
    for(size_t side=0;side<2;++side){auto p=source.actor.position;p.y+=q12_to_float(side==0?171:-171);p.z=0;
        source.actor.articulated_foot_anchor_world[side]=source.actor.articulated_foot_target_world[side]=p;}
    source.combat_command_mode_flags=9;source.combat_command_generation=200;source.target_entity_index=1;
    run.entities[1].model_terrain_state_initialized=true;run.entities[1].model_primary_occupancy_mask=0;
    run.current_visibility_occupancy.assign(field.cells().size(),0);
    lab.step(field,1);
    require((state.flags&0x13U)==0,"invisible command target started a walker turn before being dropped");
    require(!source.target_entity_index,"walker retained an invisible command target");
}
void stock_class_initializer_case(){
    SimulationLabRuntimeCatalog catalog;
    ModelVisualRecord model;model.definition_id=115;model.runtime_class_id=3;model.simulation.maximum_integrity=1000;
    catalog.definitions[115]=model;
    ModelVisualNode root;root.parent_index=UINT32_MAX;root.model_definition_id=115;root.runtime_class_id=3;
    root.simulation=model.simulation;root.sprite_path="walker";
    ArmyAssetPreset preset;preset.registry_id=115;
    ArmyModelVariantGroup group;ModelTechnologyVariant variant;
    variant.model_definition_id=115;variant.model_hierarchy={root};group.variants={variant};preset.model_variant_groups={group};
    catalog.armies[115]=preset;
    SimulationLabSeedEntity seed;seed.army_asset_id=115;seed.runtime_model_class_id=3;
    SimulationLabEntity entity;
    require(simulation_lab_initialize_runtime_entity(entity,seed,{},catalog,true),"stock walker creation fixture failed");
    require(entity.model_components[0].timed_effect_countdown==0x7FFFFFFFU,
        "stock ARM creation omitted class 3's timed-emitter sentinel");
    entity.model_components[0].timed_effect_countdown=123;
    require(simulation_lab_initialize_runtime_entity(entity,seed,{},catalog,false),"stock refresh fixture failed");
    require(entity.model_components[0].timed_effect_countdown==123,"non-creation refresh restarted walker timer");
}
void contact_effect_cases(){
    // Contract from native walker.cpp -> effects/pool.cpp: select the retained
    // foot's wet/dry EFF, consume only creationFlags bit 0, and retain its owner.
    // Transition 2's orientation draw belongs to timed emitters, not the pool.
    for(bool wet:{false,true})for(uint32_t side:{0U,1U})
    for(uint32_t flags:{0U,1U})for(uint32_t transition:{1U,2U})for(bool completing:{false,true})for(bool interleaved:{false,true})for(unsigned damage_case:{0U,1U,2U}){
        const bool damaged=damage_case!=0U,terminal=damage_case==2U;
        auto field=FieldGrid::create(80,80,0,0);
        if(wet)for(auto& cell:field.cells())cell.water_delta_q12=64;
        const auto cell=field.at(40,40);
        SimulationLabSeedEntity seed;seed.faction_index=1;seed.army_asset_id=115;
        seed.runtime_model_class_id=3;seed.root_model_maximum_integrity=1000;
        seed.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),1};
        seed.motion.speed_q12_per_tick=132;seed.motion.ground_acceleration_q12=102;
        seed.articulated_contact_geometry_enabled=true;seed.articulated_root_height_q12=1224;
        seed.articulated_contact_interpolation_rate_q12=1400;seed.articulated_contact_lift_q12=250;
        seed.articulated_contact_steering_limit_turn16=4096;
        auto nodes=std::make_shared<std::vector<ModelVisualNode>>(1);
        auto& root=nodes->front();root.parent_index=UINT32_MAX;root.runtime_class_id=3;
        root.model_definition_id=115;root.simulation.timed_effect_id=25;root.simulation.timed_water_effect_id=26;
        auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();
        for(uint32_t id:{25U,26U}){
            EffectDefinitionRecord effect;effect.definition_id=id;effect.creation_flags=flags;
            effect.transition_kind=transition;effect.runtime_value_0c=22;effect.frame_advance_threshold_q4=10;
            effect.model_scale_x_q12=effect.model_scale_y_q12=4096;
            catalog->effects[id]={effect,{}};
        }
        if(damaged){
            root.simulation.maximum_integrity=1000;root.simulation.damage_effect_id=36;
            root.simulation.damage_effect_health_threshold_percent=100;
            root.simulation.damage_effect_min_cooldown_ticks=100;
            auto smoke=catalog->effects[25].definition;smoke.definition_id=36;smoke.creation_flags=0;
            smoke.transition_kind=1;catalog->effects[36]={smoke,{}};
        }
        SimulationLabScenarioRuntimeSeed scene;scene.enabled=true;scene.active_faction_count=1;
        scene.ai_planning_enabled=false;scene.runtime_catalog=catalog;
        SimulationLabConfig config;config.run_count=1;config.duration_ticks=20;
        config.base_seed=1538888426U;
        std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
        SimulationLabExperiment lab;lab.reset(config,parameters,std::span(&seed,1),{},{},{},{},{},scene);
        auto& run=lab.runs()[0];
        require(run.entities[0].model_components[0].timed_effect_countdown==0x7FFFFFFFU,
            "walker class initializer lost the native suppressed-emitter sentinel");
        if(damaged){run.entities[0].root_model_integrity=terminal?0:500;
            run.entities[0].model_components[0].integrity=terminal?0:500;
            run.entities[0].root_model_terminal=terminal;
            run.entities[0].model_components[0].terminal_runtime_flag8=terminal;
            run.entities[0].model_components[0].runtime_flags_ec=terminal?8U:0U;
            run.entities[0].damage_effect_cooldown=0;}
        run.entities[0].runtime_model_hierarchy=nodes;auto& actor=run.entities[0].actor;
        actor.articulated_contacts_initialized=true;actor.articulated_native.initialized=true;
        auto& state=actor.articulated_native;state.flags=1U<<side;state.speed=184;
        state.restart_speed=184;state.step_rate=6039;state.progress[side]=completing?3881U:271U;
        actor.articulated_foot_anchor_world.fill({seed.actor.position.x,seed.actor.position.y,0});
        actor.articulated_foot_target_world=actor.articulated_foot_anchor_world;
        actor.articulated_foot_target_world[side].x+=q12_to_float(1244);
        const auto retained=simulation_native_walker_contacts(actor,250,0)[side];
        const auto initial_rng=run.rng_state;
        if(interleaved){
            // A newer walker's contact must precede an older model emitter,
            // even when an older projectile triggers the merged emitter pass.
            run.tick=5;run.entities[0].synthetic=true;run.entities[0].construction_start_tick=3;
            auto emitter=run.entities[0];emitter.runtime_model_class_id=0;emitter.army_asset_id=310;
            emitter.motion={};emitter.construction_start_tick=2;emitter.root_model_terminal=false;
            auto emitter_nodes=std::make_shared<std::vector<ModelVisualNode>>(1);
            auto& node=emitter_nodes->front();node.parent_index=UINT32_MAX;node.model_definition_id=310;
            node.runtime_class_id=0;node.simulation.timed_effect_id=node.simulation.timed_water_effect_id=35;
            node.simulation.timed_effect_interval_ticks=7;node.simulation.timed_effect_random_ticks=100;
            emitter.runtime_model_hierarchy=emitter_nodes;
            auto& component=emitter.model_components[0];component.runtime_class_id=0;
            component.terminal_runtime_flag8=false;component.runtime_flags_ec=0;
            component.model_definition_id=310;component.source_model_node_index=0;component.timed_effect_countdown=1;
            ModelVisualRecord definition;definition.definition_id=310;definition.simulation=node.simulation;
            catalog->definitions[310]=definition;
            auto effect=catalog->effects[25].definition;effect.definition_id=35;
            effect.creation_flags=0;effect.transition_kind=1;catalog->effects[35]={effect,{}};
            run.entities.push_back(emitter);
            ShotDefinitionRecord shot;shot.definition_id=28;shot.trajectory_mode=1;
            shot.projectile_lifetime_ticks=100;catalog->shots[28]=shot;
            SimulationProjectileState projectile;projectile.shot_definition_id=28;
            projectile.spawn_tick=1;projectile.serial=1;projectile.lifetime_ticks=100;
            projectile.owner_kind=SimulationProjectileOwnerKind::Environmental;
            projectile.launch_position={seed.actor.position.x,seed.actor.position.y,50};
            run.projectiles.push_back(projectile);
        }
        lab.step(field,1);
        require(run.metrics.articulated_contact_completions==unsigned(completing),"contact fired before completion");
        const auto live_effects=std::count_if(run.environmental_effects.begin(),run.environmental_effects.end(),
            [](const auto& effect){return !effect.world_pool_only;});
        require(live_effects==unsigned(completing)+unsigned(interleaved),"contact EFF owner missing or emitted during swing");
        const auto smoke_owners=std::count_if(run.environmental_effects.begin(),run.environmental_effects.end(),
            [](const auto& effect){return effect.world_pool_only&&effect.effect_id==36U;});
        require(smoke_owners==unsigned(damaged),"damage smoke lost its native world record");
        // Independently captured Random_NextSecondary seed transition at tick 9168.
        require(initial_rng==1538888426U,"fixture changed initial RNG");
        constexpr std::array<uint32_t,5> native_states{1538888426U,812253908U,4076213566U,2295360040U,4271088018U};
        constexpr std::array<uint32_t,4> native_random_remainders{57U,43U,42U,8U};
        const auto before_timer=unsigned(damaged)*2U+unsigned(completing&&flags);
        require(run.rng_state==native_states[before_timer+unsigned(interleaved)],
            "contact/damage callbacks consumed the wrong native RNG sequence");
        if(interleaved)require(run.entities[1].model_components[0].timed_effect_countdown==
            7U+native_random_remainders[before_timer],"older emitter consumed a newer callback's RNG draw");
        if(damaged&&completing){
            uint64_t smoke_sequence=0,contact_sequence=0;
            for(auto event=run.one_shot_event_tail;event;event=event->previous){
                if(event->event.effect_id==36U)smoke_sequence=event->event.sequence;
                if(event->event.effect_id==25U||event->event.effect_id==26U)contact_sequence=event->event.sequence;
            }
            require(smoke_sequence&&contact_sequence&&smoke_sequence<contact_sequence,
                "walker damage smoke was emitted after the completed foot contact");
        }
        if(completing){const auto found=std::find_if(run.environmental_effects.begin(),run.environmental_effects.end(),
                [](const auto& effect){return effect.effect_id==25U||effect.effect_id==26U;});
            require(found!=run.environmental_effects.end(),"contact EFF not retained");const auto& effect=*found;
            require(effect.effect_id==(wet?26U:25U),"wrong wet/dry contact EFF");
            require(float_to_q12(effect.pose.position.x)==float_to_q12(retained.x)&&
                    float_to_q12(effect.pose.position.y)==float_to_q12(retained.y)&&
                    float_to_q12(effect.pose.position.z)==float_to_q12(retained.z),
                "contact effect used the snapped destination instead of the retained foot");
        }
    }
}

}
int main()try{
    half_turn_contact_side_case();
    rejected_closing_contact_keeps_own_height();
    hidden_command_does_not_start_turn();
    stock_class_initializer_case();
    contact_effect_cases();
    // Native ARM115 first two contacts, captured independently from walker.cpp.
    // Planning consumes a callback; progress then uses the previous speed.
    constexpr std::array<std::array<int32_t,3>,10> positions{{
        {221320,-66627,16403},{221361,-66649,16433},{221425,-66681,16473},
        {221512,-66726,16509},{221621,-66782,16527},{221754,-66850,16508},
        {221909,-66930,16427},{221942,-66947,16403},{221942,-66947,16403},
        {221989,-66971,16421}}};
    constexpr std::array<uint32_t,10> progress{0,271,692,1264,1986,2858,3881,0,0,156};
    SimulationActorState actor;actor.position={q12_to_float(221320),q12_to_float(-66627),q12_to_float(16403)};
    actor.body_yaw_turn16=60593;
    SimulationMotionProfile motion;motion.speed_q12_per_tick=132;motion.ground_acceleration_q12=102;
    Dependencies deps;
    for(size_t tick=0;tick<positions.size();++tick){
        simulation_tick_native_walker(actor,motion,1400,250,4096,171,0,1224,0,1,deps);
        const std::array<int32_t,3> actual{float_to_q12(actor.position.x),float_to_q12(actor.position.y),float_to_q12(actor.position.z)};
        require(actual==positions[tick],"walker contact position differs from native frame");
        require(actor.body_yaw_turn16==60593U,"straight contact changed body heading");
        const auto& state=actor.articulated_native;
        require(state.progress[tick<8?0:1]==progress[tick],"walker progress differs from native frame");
        require(state.restart_speed==184,"authored walker restart speed was not derived from MDL parameters");
        require(state.step_rate==(tick<8?6039:3478),"walker foot travel lost native integer rounding");
    }
    require(deps.completions==1U,"walker completed a foot during the planning callback");
    std::cout<<"NATIVE_WALKER PASS frames=10 contact_cases=384 stock_initializer=1 retained_timer=1 planning=1 acceleration=1 rounding=1 alternate_contact=1\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
