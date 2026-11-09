#include "thandor/catalog.hpp"
#include "thandor/binary.hpp"
#include "thandor/level.hpp"
#include <iostream>
#include <algorithm>
#include <map>
#include <set>
#include <tuple>
using namespace thandor;
struct Timed {uint32_t cls{},land{},water{},interval{},random{},flags{},shot_interval{},shot_random{};int32_t shot{};std::string path;size_t offset{};};
int main(int argc,char** argv)try{
 if(argc!=2)throw std::runtime_error("usage: census GAME_ROOT");const auto catalog=AssetCatalog::scan_archive_root(argv[1]);std::map<uint32_t,Timed> records;
 const auto group=std::find_if(catalog.map_groups().begin(),catalog.map_groups().end(),[](const auto& g){return g.level_path=="level/asgard.lev";});
 if(group==catalog.map_groups().end())throw std::runtime_error("Asgard missing");const auto level=LevelAsset::parse(catalog.load(group->level_path));
 for(const auto& base:level.model_resource_paths_in_load_order()){
  const auto path=base.ends_with(".mdl")?base:base+".mdl";
  const auto data=catalog.load(path);const auto parsed=parse_model_visual_records(data,path);size_t offset=0x200;
  for(const auto& model:parsed){
   const auto size=read_u32(data,offset);if(size>=0x180){
    Timed t;t.cls=read_u32(data,offset+0x4c);t.land=read_u32(data,offset+0x174);t.water=read_u32(data,offset+0x58);t.interval=read_u32(data,offset+0x178);t.random=read_u32(data,offset+0x17c);t.flags=read_u32(data,offset+0x68);t.shot=read_i32(data,offset+0x168);t.shot_interval=read_u32(data,offset+0x16c);t.shot_random=read_u32(data,offset+0x170);t.path=path;t.offset=offset;
    const auto& s=model.simulation;
    if(t.cls!=model.runtime_class_id||t.land!=s.timed_effect_id||t.water!=s.timed_water_effect_id||t.interval!=s.timed_effect_interval_ticks||t.random!=s.timed_effect_random_ticks||t.flags!=s.runtime_render_flags||t.shot!=s.timed_shot_definition_id||t.shot_interval!=s.timed_shot_interval_ticks||t.shot_random!=s.timed_shot_random_ticks)throw std::runtime_error("raw MDL timed fields disagree with decoded record");
    if(!records.emplace(model.definition_id,t).second)throw std::runtime_error("duplicate stock MDL definition ID");
   }
   offset+=size;
  }
 }
 std::set<std::pair<uint32_t,uint32_t>> links;
 for(const auto& p:catalog.army_presets())if(catalog.placement_available_in_level(p,level)){
  const auto add=[&](const auto& nodes){for(const auto& n:nodes)links.emplace(p.registry_id,n.model_definition_id);};
  add(p.model_hierarchy);add(p.resolved_model_hierarchy);for(const auto& g:p.model_variant_groups)for(const auto& variant:g.variants){add(variant.model_hierarchy);for(const auto& replacement:variant.faction_technology_replacements)add(replacement.model_hierarchy);}
 }
 std::cout<<"# Asgard timed-effect census regenerated from original mounted PCK assets by V637.\n# This is a new measurement, not recovery of the historical G409 report bytes.\n# Every printed field is checked against raw MDL offsets; ARM links include baseline, variants and faction replacements.\n";
 size_t rows=0;
 for(const auto& [arm,id]:links)if(const auto it=records.find(id);it!=records.end()){
  const auto& t=it->second;if(t.land==0&&t.water==0&&t.shot<0)continue;++rows;
  std::cout<<"ARM "<<arm<<" MDL "<<id<<" class "<<t.cls<<" shot "<<t.shot<<' '<<t.shot_interval<<'+'<<t.shot_random<<" land "<<t.land<<" water "<<t.water<<" eff "<<t.interval<<'+'<<t.random<<" flags "<<t.flags<<" source "<<t.path<<" record_offset "<<t.offset<<'\n';
 }
 if(!rows)throw std::runtime_error("no timed stock records found");std::cout<<"V637_STOCK_TIMED_EFFECT_CENSUS PASS model_records="<<records.size()<<" arm_model_links="<<links.size()<<" timed_rows="<<rows<<'\n';
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
