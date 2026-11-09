#define main v620_stock_replay_unused_main
#include "simlab_stock_replay.cpp"
#undef main
int main(int argc,char** argv)try{
 if(argc!=2)throw std::runtime_error("usage: stock_combat_probe game_assets");
 const auto catalog=AssetCatalog::scan_archive_root(argv[1]);const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
 const auto field=FieldGrid::create(100,100,0,0);const auto& cell=field.at(50,50);
 FactionTechnologyMask256 mask{};mask.fill(UINT32_MAX);std::array<FactionTechnologyMask256,8> masks{};masks[1]=masks[2]=mask;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};SimulationLabConfig config;config.run_count=1;config.duration_ticks=256;
 for(uint32_t unit_id:{2U,60U,110U,170U}){
  auto source=seed_for(*catalog.army_preset(unit_id),catalog,mask);source.faction_index=1;source.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
  auto target=seed_for(*catalog.army_preset(331),catalog,mask);target.faction_index=2;target.actor.position=source.actor.position;target.actor.position.x+=3;target.source_placement_index=1;
  const std::array seeds{source,target};SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=scenario.assignable_faction_count=2;scenario.simulate_all_active_factions_as_ai=true;scenario.runtime_catalog=runtime;
  SimulationLabExperiment lab;lab.reset(config,ki,seeds,{},{},{},masks,{},scenario);auto& run=lab.runs()[0];run.entities[0].military_ai_source_dispatch=true;
  for(uint32_t tick=0;tick<256;++tick)lab.step(field,1);
  std::cout<<"ARM "<<unit_id<<" class="<<run.entities[0].runtime_model_class_id<<" weapons="<<run.entities[0].combat_weapons.size()<<" observed_tick="<<run.tick<<" shots="<<run.metrics.weapon_shots_fired<<" damage="<<run.metrics.projectile_damage_applied<<" hp="<<run.entities[1].root_model_integrity<<'\n';
  if(unit_id==60U&&!run.metrics.projectile_damage_applied)throw std::runtime_error("ground-level tracked cannon did no damage");
  if(!run.metrics.weapon_shots_fired)throw std::runtime_error("stock unit did not fire at visible storage in bounded fixture");
 }
 std::cout<<"V620_STOCK_COMBAT PASS; fully unlocked input, roots at ground, idle strategic commands; not autonomous attack proof\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
