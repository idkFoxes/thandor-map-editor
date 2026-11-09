#include "thandor/simulation_lab_environment.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main()try{
    auto field=FieldGrid::create(32,32,0,3);
    const auto& cell=field.at(16,16);const Vec3f center{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),3};
    ShotDefinitionRecord shot;shot.trajectory_mode=0;shot.terrain_impact_effect_ids.fill(7);shot.primary_effect_id=8;
    auto resolve=[&](Vec3f from,Vec3f to,const ShotDefinitionRecord& s){SimulationProjectileState p;
        p.launch_position=from;p.target_position=to;p.travel_ticks=1;simulation_resolve_projectile_surface_impact(p,s,field);return p;};
    check(resolve(center,{center.x+1,center.y,2},shot).impact_kind==SimulationImpactKind::None,
        "airborne aim/expiry endpoint fabricated terrain impact");
    auto miss=resolve(center,{center.x+1,center.y,2},shot);
    check(!simulation_projectile_impacted(miss,100),"miss expiry was reported as physical impact");
    check(resolve({-1000,1000,3},{-999,1000,-3},shot).impact_kind==SimulationImpactKind::None,
        "outside shot fabricated a clamped terrain hit");
    for(const Vec3f end:std::array<Vec3f,4>{{{1000,center.y,3},{-1000,center.y,3},{center.x,1000,3},{center.x,-1000,3}}})
        check(resolve(center,end,shot).impact_kind==SimulationImpactKind::None,"map-edge exit fabricated contact");
    auto null=shot;null.terrain_impact_effect_ids.fill(0);null.primary_effect_id=0;
    check(resolve(center,{center.x,center.y,-2},null).impact_kind==SimulationImpactKind::None,
        "null terrain EFF slot stopped shot");
    auto hit=resolve(center,{center.x,center.y,-2},shot);
    check(hit.impact_kind==SimulationImpactKind::TerrainSurface&&hit.impact_effect_id==7,
        "actual ground intersection missed");
    for(auto& c:field.cells())c.water_delta_q12=4096;
    hit=resolve(center,{center.x,center.y,-2},shot);
    check(hit.impact_kind==SimulationImpactKind::SecondarySurface&&hit.impact_effect_id==8,
        "secondary surface did not win nearer contact");
    null=shot;null.primary_effect_id=0;
    hit=resolve(center,{center.x,center.y,-2},null);
    check(hit.impact_kind==SimulationImpactKind::TerrainSurface,"null secondary EFF concealed eligible terrain");
    auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();shot.definition_id=28;shot.projectile_lifetime_ticks=10;shot.launch_speed_q12=4096;
    catalog->shots[28]=shot;EffectDefinitionRecord eff;eff.definition_id=25;eff.transition_kind=1;
    eff.linked_shot_present=1;eff.linked_shot_id=28;eff.sprite_path="spr/outside.spr";eff.runtime_value_0c=10;eff.frame_advance_threshold_q4=16;
    eff.model_scale_x_q12=eff.model_scale_y_q12=4096;catalog->effects[25]={eff,{{1U,0U}}};
    catalog->sprites[eff.sprite_path].shot_points={{0,{1,0,0}}};
    ModelVisualRecord model;model.definition_id=647;model.simulation.timed_effect_id=25;model.simulation.timed_effect_interval_ticks=100;
    catalog->definitions[647]=model;SimulationLabEnvironmentalEmitter emitter;emitter.model_definition_id=647;emitter.pose.position={-1000,1000,3};
    SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=catalog;scenario.environmental_emitters={emitter};
    SimulationLabConfig config;config.run_count=1;config.duration_ticks=10;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};SimulationLabExperiment lab;
    lab.reset(config,parameters,{}, {},{},{},{},{},scenario);lab.step(field,2);
    const auto& run=lab.runs()[0];check(run.metrics.environmental_effects_spawned==1&&run.metrics.environmental_shots_spawned==1,
        "outside-map EFF/SHT creation was globally suppressed");
    check(!run.projectiles.empty()&&std::abs(run.projectiles[0].launch_position.x+999.F)<0.001F&&run.projectiles[0].impact_kind==SimulationImpactKind::None,
        "outside-map effect shot was snapped to map or fabricated impact");
    std::cout<<"MAP_BOUNDARY_EFFECTS PASS endpoints four_edges eligible_effect_slots outside_emitters\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
