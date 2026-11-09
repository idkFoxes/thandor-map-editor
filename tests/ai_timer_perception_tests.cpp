#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_targeting.hpp"
#include "thandor/fog_preview.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
SimulationLabConfig config(){SimulationLabConfig c;c.run_count=1;c.duration_ticks=1300;
    c.sweep_mode=SimulationLabSweepMode::Fixed;c.parameter_index=100;return c;}
}
int main(int argc,char** argv)try{
    const auto field=FieldGrid::create(16,16,0,0);
    std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabExperiment lab;lab.reset(config(),parameters,{});
    auto& run=lab.runs()[0];
    // Reset is not an elapsed economy job. Both represented images start at 0.
    check(run.metrics.impact_anchor_decay_steps==0,"reset serviced an economy tick");
    run.ai_primary_anchor_cooldown[1]=150;
    run.ai_primary_impact_anchor[1]={123,456,150};
    run.ai_anchor_cooldown0[2]=1;
    run.ai_secondary_impact_anchor[2]={789,321,1};
    lab.step(field,7);
    check(run.ai_primary_anchor_cooldown[1]==150 && run.ai_secondary_impact_anchor[2].cooldown==1,
        "alert decayed outside world job 0");
    lab.step(field,1);
    check(run.ai_primary_anchor_cooldown[1]==149 && run.ai_primary_impact_anchor[1].cooldown==149,
        "live primary alert failed to decay");
    check(run.ai_anchor_cooldown0[2]==0 && run.ai_secondary_impact_anchor[2].cooldown==0,
        "secondary alert failed to expire");
    check(run.metrics.impact_anchor_expirations==1,"alert expiration counter");
    lab.step(field,1191);
    check(run.ai_primary_anchor_cooldown[1]==1 && run.ai_primary_impact_anchor[1].cooldown==1,
        "150-step alert expired early or never counted down");
    lab.step(field,1);
    check(run.ai_primary_anchor_cooldown[1]==0 && run.ai_primary_impact_anchor[1].cooldown==0,
        "alert stuck after 1200 simulation ticks");
    check(run.ai_primary_impact_anchor[1].model_x_q12==123,"timer moved an alert anchor");
    lab.step(field,8);
    check(run.ai_primary_anchor_cooldown[1]==0 && run.metrics.impact_anchor_expirations==2,
        "expired alert wrapped or was counted twice");
    // Visibility uses the real staggered FLD projection. History alone admits
    // group-A buildings to workspace02/07; mobiles require current visibility.
    SimulationLabEntity enemy;enemy.faction_index=2;enemy.construction_complete=true;
    enemy.runtime_model_class_id=15;enemy.actor.position={
        q12_to_float(field.at(8,8).world_x_q12),q12_to_float(field.at(8,8).world_y_q12),0};
    run.level_runtime_enabled=true;
    run.faction_packed_relation_states.fill(0x11111111U);
    run.current_visibility_occupancy.assign(field.cells().size(),0);
    const auto cell=retail_fog_world_to_cell(field,float_to_q12(enemy.actor.position.x),
                                            float_to_q12(enemy.actor.position.y));
    check(cell.has_value(),"fixture projection failed");
    const auto offset=static_cast<size_t>(cell->y)*field.width()+cell->x;
    using P=SimulationLabHostilePerception;
    check(simulation_lab_ai_hostile_perception(run,field,1,enemy)==P::Unseen,"unseen enemy exposed");
    run.current_visibility_occupancy[offset]=uint64_t{0x80}<<8;
    check(simulation_lab_ai_hostile_perception(run,field,1,enemy)==P::Visible,"known tank forgotten outside current LOS");
    enemy.runtime_model_class_id=1;
    check(simulation_lab_ai_hostile_perception(run,field,1,enemy)==P::Unseen,"mobile tracked using exploration history");
    run.current_visibility_occupancy[offset]=uint64_t{0x10}<<8;
    check(simulation_lab_ai_hostile_perception(run,field,1,enemy)==P::Visible,"current enemy visibility lost");
    enemy.construction_complete=false;
    check(simulation_lab_ai_hostile_perception(run,field,1,enemy)==P::Visible,
          "live unfinished owner was omitted from workspace02/07");
    enemy.construction_complete=true;
    enemy.production_linked_to_parent=true;
    check(simulation_lab_ai_hostile_perception(run,field,1,enemy)==P::Visible,
          "movement-locked factory ground model disappeared from native workspaces");
    enemy.production_parked_on_pad=true;
    check(simulation_lab_ai_hostile_perception(run,field,1,enemy)==P::Excluded,
          "parked product entered workspace02/03/07 target surfaces");
    enemy.production_parked_on_pad=false;
    enemy.production_linked_to_parent=false;
    for(uint32_t state:{4U,8U}){
        run.faction_packed_relation_states[1]=(0x11111111U&~(0xFU<<8))|(state<<8);
        run.faction_capability_flags[1]|=1U<<2U;
        check(simulation_lab_ai_hostile_perception(run,field,1,enemy)==P::Excluded,"friendly faction treated as hostile");
    }
    run.faction_packed_relation_states[1]=0x11111111U;
    run.faction_capability_flags[1]&=~(1U<<2U);
    enemy.faction_index=0;
    check(simulation_lab_ai_hostile_perception(run,field,1,enemy)==P::Excluded,"neutral scenery entered hostile workspace");
    enemy.faction_index=2;enemy.root_model_terminal=true;
    check(simulation_lab_ai_hostile_perception(run,field,1,enemy)==P::Visible,
          "linked terminal MODEL owner disappeared from workspace02/07 before owner release");
    check(!simulation_lab_combat_target_owner_available(enemy),
          "terminal MODEL owner remained a direct combat candidate");
    check(simulation_lab_command_target_owner_available(enemy),
          "linked terminal MODEL owner was not retainable as an existing command target");
    enemy.root_model_terminal=false;enemy.runtime_removed=true;
    check(simulation_lab_ai_hostile_perception(run,field,1,enemy)==P::Excluded,"removed army owner entered hostile workspace");
    enemy.runtime_removed=false;enemy.model_components.resize(1);
    enemy.root_model_component_index=0;enemy.model_components[0].runtime_removed=true;
    check(!simulation_lab_has_live_root_model_owner(enemy),"removed root still reported a MODEL owner");
    check(simulation_lab_ai_hostile_perception(run,field,1,enemy)==P::Excluded,"removed root MODEL owner entered hostile workspace");
    enemy.model_components[0].runtime_removed=false;
    check(simulation_lab_has_live_root_model_owner(enemy),"live root MODEL owner was removed");
    // Score provenance reports all competing families, including the losing
    // hostile score, without changing strict-greater/tie ordering.
    MilitaryScoreParametersV599 p{};p.site_bias=100;p.site_scale=4096;
    p.hostile_bias=150;p.hostile_scale=4096;
    std::array sites{MilitarySiteV599{0,0,0}};
    const auto choice=military_choose_anchor_action_v599(0,0,p,2,0,1,sites,{},{},sites,false);
    check(choice.kind==1 && choice.score==200 && choice.best_site_score==200 &&
        choice.best_hostile_score==150,"military score provenance changed selector result");
    if(argc==2) {
        const auto catalog=AssetCatalog::scan_game_root(argv[1]);
        const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
        const auto stock_field=FieldGrid::create(59,59,0,0);
        const auto& center_cell=stock_field.at(29,29);
        const Vec3f center{q12_to_float(center_cell.world_x_q12),
                           q12_to_float(center_cell.world_y_q12),0};
        std::array<FactionTechnologyMask256,8> masks{};
        masks[1][0]=masks[2][0]=3;
        masks[1][6]=masks[2][6]=278003968;
        const auto* jeep=catalog.army_preset(1);check(jeep,"stock jeep absent");
        const auto make=[&](uint32_t id,uint32_t faction,Vec3f point,uint32_t cls){
            SimulationLabSeedEntity seed;seed.army_asset_id=id;seed.faction_index=faction;
            seed.runtime_model_class_id=cls;seed.building=cls==15;
            seed.actor.position=point;seed.root_model_maximum_integrity=1000;
            if(cls==1)seed.motion=jeep->simulation_motion_profile;
            return seed;
        };
        const std::array seeds{make(1,1,center,1),
            make(1,1,{center.x,center.y+0.5F,0},1),make(333,2,{center.x+3,center.y,0},15)};
        SimulationLabProductionAsset product;product.army_asset_id=1;
        product.ai_definition_class_value70=jeep->ai_definition_class_value70;
        product.ai_definition_class_value74=jeep->ai_definition_class_value74;
        product.ai_definition_class_value78=jeep->ai_definition_class_value78;
        const std::array products{product};
        for(uint32_t relation:{1U,4U,8U,99U}) {
            SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;
            scenario.active_faction_count=2;scenario.simulate_all_active_factions_as_ai=true;
            scenario.runtime_catalog=runtime;
            if(relation==4)scenario.relation_state4_faction_group_masks=6;
            if(relation==8)scenario.relation_state8_faction_group_masks=6;
            auto stock_config=config();stock_config.duration_ticks=relation==99?14:64;
            auto stock_parameters=parameters;
            if(relation==99)stock_parameters=AiKnowledgeData::parse(catalog.load("engine/ki.dat")).parameters();
            SimulationLabExperiment stock;
            stock.reset(stock_config,stock_parameters,seeds,{},products,{},masks,{},scenario);
            for(size_t i=0;i<2;++i)stock.runs()[0].entities[i].military_ai_source_dispatch=true;
            stock.step(stock_field,stock_config.duration_ticks);
            const auto& result=stock.runs()[0];
            std::cout<<"STOCK relation="<<relation<<" ticks="<<result.tick
                <<" groups="<<result.metrics.military_ai_group_orders
                <<" sites="<<result.metrics.military_ai_site_orders
                <<" movement="<<result.metrics.military_movement_distance_world
                <<" shots="<<result.metrics.weapon_shots_fired
                <<" best_site="<<result.entities[0].military_ai_last_choice.best_site_score
                <<" best_hostile="<<result.entities[0].military_ai_last_choice.best_hostile_score<<'\n';
            if(relation==1)check(result.metrics.military_ai_group_orders>=2 &&
                result.metrics.weapon_shots_fired>0,"bounded stock group did not command and fire");
            if(relation==4 || relation==8)check(result.metrics.military_ai_group_orders==0 &&
                result.metrics.weapon_shots_fired==0,"stock friend received attack or shots");
        }
    }
    std::cout<<"V613 live alert decay/expiry, hostile perception and competing score provenance PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
