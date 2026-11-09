#include "thandor/simulation_lab_site_placement.hpp"
#include "thandor/pioneer_resource_grid.hpp"
#include "thandor/simulation_lab_defaults.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}}
int main(int argc,char** argv)try{
 std::array<uint32_t,AiKnowledgeData::kParameterCount> loaded{};loaded[1]=500;
 auto baseline=simulation_lab_loaded_baseline_config(loaded);
 check(baseline.run_count==1&&baseline.vary_seed_per_run&&baseline.sweep_mode==SimulationLabSweepMode::Fixed&&
     simulation_lab_sweep_values(baseline)==std::vector<int32_t>{500},"startup silently changed loaded KI priority");
 SimulationLabExperiment unchanged;unchanged.reset(baseline,loaded,{});
 check(unchanged.runs()[0].ai_parameters==loaded,"baseline config changed KI image");
 baseline.run_count=3;baseline.sweep_mode=SimulationLabSweepMode::Linear;baseline.minimum_value=0;baseline.maximum_value=100;
 check(simulation_lab_sweep_values(baseline)==std::vector<int32_t>({0,50,100}),"explicit sweeps no longer available");
 auto field=FieldGrid::create(80,80,0,0);
 const auto point=[&](uint32_t x){const auto& c=field.at(x,40);return MovementPoint{c.world_x_q12,c.world_y_q12};};
 const auto xyz=[&](uint32_t x){const auto p=point(x);return Vec3f{q12_to_float(p.x),q12_to_float(p.y),0};};
 SimulationLabRun run;run.rng_state=12345;
 auto scratch=std::make_shared<SimulationLabRetailScratchImage>();
 scratch->field_occupancy_masks.assign(field.cells().size(),uint64_t{1}<<8U);run.retail_scratch=scratch;
 run.ai_parameters[0x51]=60000;run.ai_parameters[28]=6144;
 const auto site=point(40);const auto nearest=retail_nearest_world_to_cell(field,site.x,site.y);
 check(nearest.has_value(),"site outside field");field.at(nearest->x,nearest->y).flags_and_material|=0x800;
 SimulationLabSiteModel resource{330,14,256,0,8192,0,0},storage{333,15,256,0,8192,0,0};
 std::vector<SimulationLabSiteOwner> owners{{{point(30),1,11,0,8192,true},256,{}}};
 const std::array anchors{xyz(33),xyz(36),xyz(38)};
 auto q=simulation_lab_query_site_placement(run,field,owners,resource,site,1,4,0);
 check(!q.accepted&&q.reason==PioneerPlacementReason::NoSupportingModel,"distant deposit accepted without reach");
 q=simulation_lab_query_site_placement(run,field,owners,resource,site,1,7,60000);
 check(q.accepted&&q.placement_value>0,"mode7 padded support/clearance missing");
 auto reservation=simulation_lab_reserve_special_site(run,field,owners,resource,storage,site,anchors,1);
 check(reservation.accepted&&reservation.probes_created==3,"three dependent ARM333 probe owners did not extend reach");
 check(owners.size()==1&&owners[0].support.position.x==point(30).x&&run.entities.empty()&&run.rng_state==12345,
       "temporary placement leaked world owners/entities/RNG");
 // The strict Manhattan boundary must reject equality, then accept one Q12 unit beyond it.
 run.ai_parameters[28]=uint32_t(site.x-point(38).x);
 check(!simulation_lab_reserve_special_site(run,field,owners,resource,storage,site,anchors,1).accepted,"separation equality admitted");
 ++run.ai_parameters[28];
 check(simulation_lab_reserve_special_site(run,field,owners,resource,storage,site,anchors,1).accepted,"strict boundary +1 rejected");
 run.ai_parameters[28]=0;
 check(!simulation_lab_reserve_special_site(run,field,owners,resource,storage,site,anchors,1).accepted,"zero quantum allowed invalid division");
 run.ai_parameters[28]=8192;
 const auto far=point(46);const auto farcell=retail_nearest_world_to_cell(field,far.x,far.y);
 field.at(farcell->x,farcell->y).flags_and_material|=0x800;
 const std::array four{xyz(33),xyz(36),xyz(39),xyz(42)};
 reservation=simulation_lab_reserve_special_site(run,field,owners,resource,storage,far,four,1);
 check(!reservation.accepted&&reservation.probes_created==4,"reservation exceeded four probes or accepted distant fourth");
 // Add nearby support, then test structure-depth collision filtering independently from support.
 owners.push_back({{point(38),1,15,0,8192,true},256,{}});
 owners.push_back({{site,2,1,0,0,true},1024,{}});
 check(simulation_lab_query_site_placement(run,field,owners,resource,site,1,4,0).accepted,"mode4 incorrectly collides with mobile");
 check(!simulation_lab_query_site_placement(run,field,owners,resource,site,1,0,0).accepted,"mode0 ignores mobile collision");
 owners.back().support.runtime_class=10;
 check(simulation_lab_query_site_placement(run,field,owners,resource,site,1,4,0).accepted,"Group-A class10 incorrectly treated as depth0x90");
 owners.back().support.runtime_class=11;
 check(!simulation_lab_query_site_placement(run,field,owners,resource,site,1,4,0).accepted,"mode4 ignores structure collision");
 owners.pop_back();
 check(!simulation_lab_reserve_special_site(run,field,owners,resource,storage,site,anchors,1).accepted,"already reachable deposit adds storage bonus");
 scratch->field_occupancy_masks.assign(field.cells().size(),0);
 q=simulation_lab_query_site_placement(run,field,owners,resource,site,1,4,0);
 check(!q.accepted&&q.reason==PioneerPlacementReason::FieldBlocked,"unseen/unoccupied faction cell accepted");
 scratch->field_occupancy_masks.assign(field.cells().size(),uint64_t{1}<<8U);
 field.at(nearest->x,nearest->y).flags_and_material&=~0x800U;
 q=simulation_lab_query_site_placement(run,field,owners,resource,site,1,4,0);
 check(!q.accepted&&q.reason==PioneerPlacementReason::MissingResourceDeposit,"extractor ignores deposit bit");
 field.at(nearest->x,nearest->y).flags_and_material|=0x800;
 field.at(nearest->x,nearest->y).water_delta_q12=10;
 check(!simulation_lab_query_site_placement(run,field,owners,resource,site,1,4,0).accepted,"wet ground site accepted");
 check(simulation_lab_extractor_weight(100,3,330,10,160,0)==50,"assigned-extractor dilution");
 check(simulation_lab_extractor_weight(100,0,332,10,160,80)==200,"Tritium energy/extraction scaling");
 check(simulation_lab_extractor_weight(100,0,332,0,160,80)==100,"zero extraction rate must retain unscaled weight");
 check(simulation_lab_extractor_weight(0x60000000U,0,330,0,0,0)==178956970U,"weight multiply must wrap before unsigned divide");
 // A settled foot pose used to overwrite root XY after ordinary motion's gate.
 const auto contact_case=[&](bool deep){
  auto terrain=FieldGrid::create(64,64,0,0);
  const auto& a=terrain.at(20,30);const auto& b=terrain.at(30,30);
  const Vec3f start{q12_to_float(a.world_x_q12),q12_to_float(a.world_y_q12),0};
  const Vec3f forbidden{q12_to_float(b.world_x_q12),q12_to_float(b.world_y_q12),0};
  if(deep)for(uint32_t y=27;y<34;++y)for(uint32_t x=28;x<34;++x)terrain.at(x,y).water_delta_q12=600;
  SimulationLabSeedEntity walker;walker.army_asset_id=111;walker.faction_index=1;walker.runtime_model_class_id=3;
  walker.actor.position=start;walker.has_traversal_profile=true;walker.traversal_low_band_index=0;walker.traversal_high_band_index=4;
  walker.articulated_contact_geometry_enabled=true;
  std::array<uint32_t,AiKnowledgeData::kParameterCount> params{};SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=2;
  SimulationLabExperiment lab;lab.reset(cfg,params,std::span(&walker,1));
  auto& e=lab.runs()[0].entities[0];e.actor.articulated_contacts_initialized=true;
  e.actor.articulated_foot_anchor_world.fill(forbidden);e.actor.articulated_foot_target_world.fill(forbidden);
  lab.step(terrain);
  if(deep)check(e.actor.position.x==start.x&&e.actor.position.y==start.y&&lab.runs()[0].metrics.navigation_actual_step_blocks==1,
      "articulated root bypassed deep-liquid admission after motion check");
  else check(e.actor.position.x==forbidden.x&&e.last_translated,"legal foot settlement was suppressed or movement state stale");
 };
 contact_case(true);contact_case(false);
 if(argc>1){
  const auto c=AssetCatalog::scan_game_root(argv[1]);const auto prepared=simulation_lab_prepare_runtime_catalog(c);
  for(uint32_t id:{330U,332U,333U}){
   const auto model=simulation_lab_first_site_model(*prepared,id);check(model.has_value(),"stock placement root absent");
   SimulationLabSeedEntity tech;simulation_lab_prepare_model_technologies(*c.army_preset(id),c,tech);
   for(const auto& group:tech.linked_model_technologies)for(const auto& variant:group.variants)
       if(variant.variant_index==0)std::cout<<"stock creation ARM="<<id<<" grant="<<variant.granted_technology_id<<'\n';
   check(model->radius_q12>0&&model->support_q12>0,"stock footprint/support metadata absent");
   std::cout<<"stock ARM="<<id<<" class="<<model->runtime_class<<" radius="<<model->radius_q12
       <<" support="<<model->support_q12<<" margin="<<model->margin_q12<<'\n';
  }
  // Controlled mature-base branch fixture: stock definitions/costs, short scheduling window.
  auto mature_field=FieldGrid::create(80,80,0,0);
  for(auto x:{30U,36U,37U})mature_field.at(x,40).flags_and_material|=0x800U;
  const auto seed_for=[&](uint32_t id,uint32_t x){
   SimulationLabSeedEntity s;s.army_asset_id=id;s.faction_index=1;s.source_placement_index=id;
   const auto& cell=mature_field.at(x,40);s.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
   const auto* p=c.army_preset(id);s.runtime_model_class_id=p->runtime_model_class_id;s.building=true;
   FactionTechnologyMask256 mask{};mask[0]=3;
   s.model_components=simulation_lab_model_components_from_hierarchy(resolve_faction_technology_hierarchy(*p,mask));
   return s;
  };
  const std::array seeds{seed_for(300,26),seed_for(330,30)};
  const auto* preset=c.army_preset(330);SimulationLabProductionAsset product;
  product.army_asset_id=330;product.army_runtime_flags14_raw=preset->army_runtime_flags14_raw;
  product.xenite_cost_q4=preset->retail_xenite_cost_q4;product.build_duration_ticks=preset->retail_build_time_q5;
  product.entity_seed=seed_for(330,36);simulation_lab_prepare_ai_product(*preset,c,product);
  std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};parameters[1]=200;parameters[0x51]=60000;
  std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].xenite_current_q4=1000;
  SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=1;
  scenario.assignable_faction_count=1;scenario.simulate_all_active_factions_as_ai=true;scenario.runtime_catalog=prepared;
  std::array<FactionTechnologyMask256,8> masks{};masks[1][0]=3;
  SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=80;cfg.parameter_index=100;cfg.sweep_mode=SimulationLabSweepMode::Fixed;
  SimulationLabExperiment mature;mature.reset(cfg,parameters,seeds,economy,std::span(&product,1),{},masks,{},scenario);
  mature.step(mature_field,1);
  // Isolate the real planning consumer from the independently incomplete owner-influence writer.
  // This is a controlled input-image fixture, not an autonomous mature-base simulation.
  auto input=std::make_shared<SimulationLabRetailScratchImage>(*mature.runs()[0].retail_scratch);
  std::fill(input->state.begin(),input->state.end(),0U);
  input->field_occupancy_masks.assign(mature_field.cells().size(),uint64_t{1}<<8U);
  mature.runs()[0].retail_scratch=input;mature.runs()[0].tick=73;
  mature.step(mature_field,1);
  const auto& m=mature.runs()[0].metrics;
  std::cout<<"controlled74 plans="<<m.planning_cycles<<" consumed="<<m.pending_requests_consumed
      <<" lifecycle="<<unsigned(mature.runs()[0].faction_lifecycle_states[1])<<" ws08="<<mature.runs()[0].ai_workspace_diagnostics[1].resource08
      <<" site_tests="<<m.live_extractor_site_tests<<" accepts="<<m.live_extractor_site_accepts
      <<" cache_saves="<<m.ai_candidate_cache_saves<<'\n';
  check(m.live_extractor_site_tests>0&&m.live_extractor_site_accepts>0,"authored planner did not consume live extractor query");

 }
 std::cout<<"V615_LIVE_SITE_PLACEMENT PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
