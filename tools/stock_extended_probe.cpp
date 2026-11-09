#include "thandor/simulation_lab_site_placement.hpp"
#define main v628_stock_replay_unused_main
#include "simlab_stock_replay.cpp"
#undef main
int main(int argc,char** argv)try{
 if(argc!=2)throw std::runtime_error("usage: stock_extended_probe game_assets");
 const auto catalog=AssetCatalog::scan_archive_root(argv[1]);const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
 auto field=FieldGrid::create(120,100,0,0);FactionTechnologyMask256 mask{};mask.fill(UINT32_MAX);
 const auto position=[&](uint32_t x,uint32_t y){const auto& c=field.at(x,y);return Vec3f{q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};};
 std::vector<SimulationLabSeedEntity> seeds;
 for(auto row:std::array<std::array<uint32_t,4>,2>{{{300,1,60,50},{1,2,68,50}}}){
  auto s=seed_for(*catalog.army_preset(row[0]),catalog,mask);s.faction_index=row[1];s.actor.position=position(row[2],row[3]);s.source_placement_index=seeds.size();seeds.push_back(s);}
 SimulationLabProductionAsset product;const auto& p=*catalog.army_preset(340);product.army_asset_id=340;
 product.army_runtime_flags14_raw=p.army_runtime_flags14_raw;product.entity_seed=seed_for(p,catalog,mask);simulation_lab_prepare_ai_product(p,catalog,product);
 SimulationLabConfig config;config.run_count=1;config.sweep_mode=SimulationLabSweepMode::Fixed;config.duration_ticks=128;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};std::array<FactionTechnologyMask256,8> masks{};masks[1]=masks[2]=mask;
 SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.runtime_catalog=runtime;scenario.active_faction_count=scenario.assignable_faction_count=2;scenario.simulate_all_active_factions_as_ai=true;
 SimulationLabExperiment lab;lab.reset(config,parameters,seeds,{},std::span(&product,1),{},masks,{},scenario);auto& run=lab.runs()[0];
 lab.step(field,8);
 const auto owners=simulation_lab_site_owners(run,field,*runtime);size_t tagged=0;for(const auto& o:owners)if(o.army_entity_index)++tagged;
 if(tagged!=owners.size()||tagged<2)throw std::runtime_error("stock hierarchy owner identity missing");
 const auto model=simulation_lab_first_site_model(*runtime,340);if(!model)throw std::runtime_error("no stock extended root");
 run.ai_primary_anchor_cooldown[1]=150;const auto anchor=position(65,50);run.ai_primary_impact_anchor[1].model_x_q12=float_to_q12(anchor.x);run.ai_primary_impact_anchor[1].model_y_q12=float_to_q12(anchor.y);
 std::vector<Vec3f> points;for(uint32_t y=35;y<65;++y)for(uint32_t x=45;x<80;++x)points.push_back(position(x,y));
 const auto rng=run.rng_state;const auto scratch=run.retail_scratch->state;
 const auto chosen=simulation_lab_extended_placement(run,field,*runtime,owners,*model,points,1);
 std::cout<<"STOCK_EXTENDED arm=340 class="<<model->runtime_class<<" radius="<<model->radius_q12<<" owners="<<owners.size()<<" selected="<<bool(chosen)<<" tests="<<run.metrics.extended_placement_tests<<'\n';
 if(!chosen){std::array<unsigned,32> reasons{};for(auto v:points){const auto result=simulation_lab_query_site_placement(run,field,owners,*model,{float_to_q12(v.x),float_to_q12(v.y)},1,1,0);++reasons[static_cast<size_t>(result.reason)];}for(size_t i=0;i<reasons.size();++i)if(reasons[i])std::cout<<" reason="<<i<<" count="<<reasons[i]<<'\n';for(const auto& o:owners)std::cout<<" owner faction="<<o.support.faction<<" class="<<o.support.runtime_class<<" support="<<o.support.support_radius_q12<<'\n';}
 if(!chosen||run.rng_state!=rng||run.retail_scratch->state!=scratch)throw std::runtime_error("stock extended placement/no-side-effect control failed");
 run.pending_asset_requests.push_back({340,1,run.tick,0});
 while(run.tick<128){run.ai_primary_anchor_cooldown[1]=150;run.ai_primary_impact_anchor[1].model_x_q12=float_to_q12(anchor.x);run.ai_primary_impact_anchor[1].model_y_q12=float_to_q12(anchor.y);lab.step(field,8);if(run.metrics.completed_builds)break;}
 std::cout<<"PENDING_CONTROL tick="<<run.tick<<" searches="<<run.metrics.extended_placement_searches<<" accepts="<<run.metrics.extended_placement_accepts<<" committed="<<run.metrics.completed_builds<<" pending="<<run.pending_asset_requests.size()<<" ws09="<<run.ai_workspace_diagnostics[1].base09<<'\n';
 if(run.metrics.completed_builds!=1||run.pending_asset_requests.size()!=0||run.entities.back().army_asset_id!=340)throw std::runtime_error("stock extended pending commit failed");
 std::cout<<"V628_STOCK_EXTENDED PASS; declared fully unlocked/pending/anchor inputs, not autonomous match proof\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
