// PCK-backed inventory, not a full effect-lifecycle or rendered-image oracle.
#include "thandor/simulation_lab_environment.hpp"
#include "thandor/map_document.hpp"
#include <iostream>
#include <set>
using namespace thandor;
bool shot_descendant(const SimulationLabRuntimeCatalog& runtime,uint32_t id,std::set<uint32_t>& visited){
 if(!id||!visited.insert(id).second)return false;
 const auto it=runtime.effects.find(id);if(it==runtime.effects.end())return false;
 const auto& effect=it->second;
 if(!effect.emissions.empty())return true;
 return shot_descendant(runtime,effect.definition.linked_effect_id,visited)||
     shot_descendant(runtime,effect.definition.periodic_effect_id,visited);
}
int main(int argc,char** argv)try{
 if(argc!=2)throw std::runtime_error("Usage: map_environment_inventory PCK_ROOT");
 const auto assets=AssetCatalog::scan_archive_root(argv[1]);const auto runtime=simulation_lab_prepare_runtime_catalog(assets);
 size_t maps=0,total=0,outside=0,potential=0;uint32_t maximum_travel_lifetime=0;
 for(const auto& [id,shot]:runtime->shots)if(shot.trajectory_mode!=0)
     maximum_travel_lifetime=std::max(maximum_travel_lifetime,shot.projectile_lifetime_ticks);
 std::cout<<"map,emitters,outside_origins,outside_effect_points,potential_descendant_shot_emitters\n";
 for(const auto& group:assets.map_groups()){
  const auto doc=MapDocument::open(assets,group);const auto environment=simulation_lab_prepare_environment(assets,doc.level(),doc.field(),*runtime);
  size_t origins=0,points=0,desc=0;
  for(const auto& emitter:environment.emitters){
   origins+=!doc.field().sample_terrain(emitter.pose.position.x,emitter.pose.position.y).inside;
   for(const auto& point:emitter.world_effect_point_records)points+=!doc.field().sample_terrain(point.second.x,point.second.y).inside;
   const auto definition=runtime->definitions.find(emitter.model_definition_id);if(definition==runtime->definitions.end())continue;
   for(const auto id:{definition->second.simulation.timed_effect_id,definition->second.simulation.timed_water_effect_id}){
    const auto effect=runtime->effects.find(id);if(effect==runtime->effects.end()||!effect->second.emissions.empty())continue;
    std::set<uint32_t> visited;
    if(shot_descendant(*runtime,id,visited)){++desc;break;}
   }
  }
  std::cout<<group.level_path<<','<<environment.emitters.size()<<','<<origins<<','<<points<<','<<desc<<'\n';
  ++maps;total+=environment.emitters.size();outside+=origins+points;potential+=desc;
 }
 std::cerr<<"ENVIRONMENT_INVENTORY maps="<<maps<<" emitters="<<total<<" outside_origins_and_points="<<outside
    <<" potential_descendant_shot_emitters="<<potential<<" maximum_travel_lifetime="<<maximum_travel_lifetime
    <<" structural_inventory_only=1\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
