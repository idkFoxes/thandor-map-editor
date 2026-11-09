#define main v621_stock_unused_main
#include "simlab_stock_replay.cpp"
#undef main
#include "thandor/simulation_lab_shot_rays.hpp"
int main(int argc,char** argv)try{
 if(argc!=2)throw std::runtime_error("usage: stock_beam_probe game_assets");
 const auto assets=AssetCatalog::scan_archive_root(argv[1]);const auto runtime=simulation_lab_prepare_runtime_catalog(assets);FactionTechnologyMask256 mask{};mask.fill(UINT32_MAX);
 auto field=FieldGrid::create(80,80,0,0);const auto& c=field.at(40,40);
 auto owner=seed_for(*assets.army_preset(345),assets,mask);owner.faction_index=1;owner.actor.position={q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};
 if(owner.combat_weapons.empty())throw std::runtime_error("stock laser coil lacks weapon");
 const auto weapon=owner.combat_weapons.front();if(weapon.shot.trajectory_mode!=0)throw std::runtime_error("coil shot not beam");
 // Disable new weapon firing; this control maintains one explicitly created stock shot.
 owner.combat_weapons[0].profile.reload_ticks=0;
 auto enemy=seed_for(*assets.army_preset(331),assets,mask);enemy.faction_index=2;enemy.source_placement_index=1;enemy.actor.position=owner.actor.position;enemy.actor.position.x+=3;
 auto crossing=enemy;crossing.source_placement_index=2;crossing.actor.position.y+=5;
 const std::array seeds{owner,enemy,crossing};std::array<FactionTechnologyMask256,8> masks{};masks[1]=masks[2]=mask;
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=64;std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=runtime;
 SimulationLabExperiment lab;lab.reset(config,ki,seeds,{},{},{},masks,{},scenario);auto& run=lab.runs()[0];
 auto geometry=simulation_lab_weapon_geometry(run.entities[0],weapon.profile,{},field,*runtime);if(geometry.launch_points.empty())throw std::runtime_error("coil has no stock muzzle");
 const auto aim=simulation_lab_target_aim_point(run.entities[1],field,*runtime);
 run.projectiles=simulation_create_volley(0,0,weapon.profile,geometry.launch_points,aim,weapon.shot,0,run.next_projectile_serial,0,1);
 if(run.projectiles.empty())throw std::runtime_error("stock beam creation failed");
 const auto before=run.entities[1].root_model_integrity;lab.step(field,1);const auto hit=run.entities[1].root_model_integrity;
 if(hit>=before)throw std::runtime_error("stock beam did no initial damage");
 run.entities[1].actor.position.y+=5;lab.step(field,1);
 if(run.entities[1].root_model_integrity!=hit)throw std::runtime_error("stock moved target kept taking damage");
 run.entities[2].actor.position.y=enemy.actor.position.y;const auto cross_before=run.entities[2].root_model_integrity;lab.step(field,1);
 if(run.entities[2].root_model_integrity>=cross_before)throw std::runtime_error("stock crossing target took no damage");
 if(run.metrics.weapon_impact_effect_events!=1)throw std::runtime_error("stock beam emitted duplicate impact EFF");
 std::cout<<"V621_STOCK_BEAM PASS arm=345 shot="<<weapon.shot.definition_id<<" lifetime="<<weapon.shot.projectile_lifetime_ticks<<" first_damage="<<before-hit<<" crossing_damage="<<cross_before-run.entities[2].root_model_integrity<<" impact_effects="<<run.metrics.weapon_impact_effect_events<<" ticks="<<run.tick<<"; one declared stock shot, not autonomous attack proof\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
