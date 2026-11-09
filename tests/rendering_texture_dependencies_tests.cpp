#include "thandor/render_texture_dependencies.hpp"
#include <iostream>
#include <map>
#include <set>
using namespace thandor;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main()try{
 std::map<uint32_t,EffectDefinitionRecord> effects;
 std::map<uint32_t,ShotDefinitionRecord> shots;
 auto& laser=shots[0];laser.definition_id=0;laser.sprite_path="spr/schuesse/slray0.spr";
 laser.primary_effect_id=9;laser.launch_effect_id=71;laser.terrain_impact_effect_ids[0]=10;
 laser.target_impact_effect_ids[0]=11;
 for(auto id:{9U,10U,11U,71U,100U,101U})effects[id].definition_id=id;
 effects[100].linked_shot_present=1;effects[100].linked_shot_id=0;
 effects[9].linked_effect_present=1;effects[9].linked_effect_id=100; // Cycle via SHT 0.
 effects[101].linked_shot_present=0;effects[101].linked_shot_id=999;
 std::set<uint32_t> seen_effects,seen_shots;unsigned visits=0;bool texture_zero_loaded=false;
 auto make=[&](){return RenderTextureDependencies(
  [&](uint32_t id)->const EffectDefinitionRecord*{auto it=effects.find(id);return it==effects.end()?nullptr:&it->second;},
  [&](uint32_t id)->const ShotDefinitionRecord*{auto it=shots.find(id);return it==shots.end()?nullptr:&it->second;},
  [&](const auto& e){++visits;check(seen_effects.insert(e.definition_id).second,"effect visited twice");},
  [&](const auto& s){++visits;check(seen_shots.insert(s.definition_id).second,"shot visited twice");texture_zero_loaded=s.sprite_path=="spr/schuesse/slray0.spr";});};
 auto preload=make();preload.shot(0);preload.shot(0);preload.effect(0);
 check(texture_zero_loaded&&seen_shots==std::set<uint32_t>{0},"valid laser SHT 0 skipped");
 check(seen_effects==std::set<uint32_t>({9,10,11,71,100})&&visits==6,"laser impact/launch graph incomplete or cyclic");
 preload.effect(101);check(!seen_shots.contains(999),"absent EFF shot link was followed");
 seen_effects.clear();seen_shots.clear();visits=0;texture_zero_loaded=false;
 auto linked=make();linked.effect(100);check(texture_zero_loaded&&seen_shots.contains(0),"explicit linked SHT 0 skipped");
 bool rejected=false;try{linked.shot(999);}catch(const std::runtime_error&){rejected=true;}
 check(rejected,"missing shot silently accepted");
 std::cout<<"PASS: direct/linked SHT 0, complete EFF graph, zero sentinel, cycle deduplication, missing definition\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
