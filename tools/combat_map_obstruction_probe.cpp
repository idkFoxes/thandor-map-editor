// Fresh stock ARM202/ARM171 fixture at reported positions; not a full GUI/retail replay.
#include <unordered_set>
#define main stock_replay_main
#include "simlab_stock_replay.cpp"
#undef main
#include "thandor/simulation_lab_environment.hpp"
#include "thandor/simulation_lab_targeting.hpp"
int main(int argc,char**argv){if(argc!=2){std::cerr<<"Usage: combat_map_obstruction_probe GAME_ROOT\n";return 2;}auto c=AssetCatalog::scan_archive_root(argv[1]);auto g=std::find_if(c.map_groups().begin(),c.map_groups().end(),[](auto&g){return g.level_path.ends_with("/asgard.lev");});auto d=MapDocument::open(c,*g);auto catalog=simulation_lab_prepare_runtime_catalog(c);
 std::array<FactionTechnologyMask256,8> masks{};for(unsigned t:{45U,46U})masks[2][t/32]|=1U<<(t%32);for(unsigned t:{51U,123U})masks[4][t/32]|=1U<<(t%32);
 auto make=[&](unsigned arm,unsigned f,Vec3f p){auto*s=c.army_preset(arm);auto seed=seed_for(*s,c,masks[f]);seed.faction_index=f;seed.actor.position=p;seed.source_placement_index=SIZE_MAX;seed.shot_impact_class_index=s->shot_impact_class_index;seed.class17_root_child_deployment_enabled=s->runtime_uses_class17_root_child_deployment;simulation_initialize_class17_deployment(seed.actor,true,s->runtime_uses_class17_root_child_deployment);return seed;};
 std::array seeds{make(202,4,{58.9089F,-32.0469F,0.625244F}),make(171,2,{58.3249F,-35.0109F,0.625244F})};
 SimulationLabScenarioRuntimeSeed scene;scene.enabled=true;scene.active_faction_count=4;scene.assignable_faction_count=4;scene.ai_planning_enabled=false;scene.runtime_catalog=catalog;
 auto env=simulation_lab_prepare_environment(c,d.level(),d.field(),*catalog);scene.static_shot_hulls=env.collision_hulls;scene.static_navigation_blockers=simulation_lab_prepare_static_navigation_blockers(c,d.level());scene.static_placement_owners=env.placement_owners;
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=3000;std::array<uint32_t,AiKnowledgeData::kParameterCount>ki{};
 SimulationLabExperiment lab;lab.reset(config,ki,seeds,{},{},{},masks,{},scene);auto&r=lab.runs()[0];simulation_lab_set_group_target_command(r.entities[0],1);
 std::array<unsigned,8> kinds{};unsigned target_hits=0,static_hits=0,other_hits=0;std::unordered_set<uint64_t> seen;
 for(unsigned tick=0;tick<3000;++tick){lab.step(d.field(),1);
  for(const auto&p:r.projectiles)if(p.owner_placement_index==0&&p.impact_damage_applied&&seen.insert(p.serial).second){
   ++kinds[unsigned(p.impact_kind)];if(p.impact_target_placement_index==1)++target_hits;else if(p.impact_target_placement_index)++other_hits;else ++static_hits;
   if(seen.size()<8)std::cout<<"HIT kind="<<unsigned(p.impact_kind)<<" target="<<p.impact_target_placement_index.value_or(SIZE_MAX)<<" material="<<p.impact_material_index<<" x="<<p.impact_position.x<<" y="<<p.impact_position.y<<" z="<<p.impact_position.z<<'\n';
  }
if(tick%200==0||tick==2999){std::cout<<"tick="<<r.tick<<" volleys="<<r.entities[0].weapon_volleys_fired<<" hp="<<r.entities[1].root_model_integrity<<" impacts="<<r.metrics.projectile_impacts<<" shots="<<r.projectiles.size()<<'\n';}}
 std::cout<<"HIT_TOTAL target="<<target_hits<<" static="<<static_hits<<" other="<<other_hits<<" kinds=";for(auto k:kinds)std::cout<<k<<',';std::cout<<'\n';
 for(unsigned i=0;i<2;++i){auto&e=r.entities[i];auto aim=simulation_lab_target_aim_point(e,d.field(),*catalog);std::cout<<"ENTITY "<<i<<" x="<<e.actor.position.x<<" y="<<e.actor.position.y<<" z="<<e.actor.position.z<<" aim="<<aim.x<<','<<aim.y<<','<<aim.z<<'\n';for(auto&n:e.model_components)std::cout<<"COMP "<<i<<" mdl="<<n.model_definition_id<<" hp="<<n.integrity<<" max="<<n.maximum_integrity<<" removed="<<n.runtime_removed<<'\n';}
}
