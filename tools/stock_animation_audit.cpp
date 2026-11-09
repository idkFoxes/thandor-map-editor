// Declared technology-branch census plus bounded flat-ground movement checks.
// CPU/state coverage only: this is not an all-unit rendered-animation verdict.
#define main v645_unused_stock_replay_main
#include "simlab_stock_replay.cpp"
#undef main
#include "thandor/model_animation.hpp"
#include "thandor/simulation_lab_visual.hpp"
#include "thandor/text_resource.hpp"
#include <set>
#include <iomanip>
#include <cmath>
int main(int argc,char** argv)try{
 if(argc!=2)throw std::runtime_error("usage: stock_animation_audit PCK_DIRECTORY");
 const auto assets=AssetCatalog::scan_archive_root(argv[1]);
 if(assets.army_presets().empty())throw std::runtime_error("no ARM assets found");
 const auto runtime=simulation_lab_prepare_runtime_catalog(assets);
 const auto validation=assets.validate_supported_assets();
 std::cout<<"ASSETS total="<<assets.assets().size()<<" armies="<<assets.army_presets().size()
          <<" technologies="<<assets.technologies().size()<<" maps="<<assets.map_groups().size()
          <<" payload_issues="<<validation.issues.size()<<'\n';
 for(const auto& issue:validation.issues)std::cout<<"ASSET_ISSUE path="<<std::quoted(issue.path)<<" why="<<std::quoted(issue.message)<<'\n';
 for(const auto& map:assets.map_groups()){
  const auto level=LevelAsset::parse(assets.load(map.level_path));
  const auto text_path=map.level_path.substr(0,map.level_path.size()-4)+".str";
  std::string title;
  if(assets.find(text_path))title=TextResourceAsset::parse(assets.load(text_path)).compatibility_display_name();
  std::cout<<"MAP name="<<std::quoted(map.display_name)<<" title="<<std::quoted(title)
           <<" lev="<<std::quoted(map.level_path)<<" placements="<<level.placements().size()<<'\n';
 }
 const auto close_mask=[&](FactionTechnologyMask256& mask){
  bool changed=true;
  while(changed){changed=false;
   for(const auto& tech:assets.technologies())if(faction_technology_unlocked(mask,tech.technology_id)){
    const auto before=mask;
    for(size_t i=0;i<8;++i)mask[i]|=tech.prerequisite_masks[i];
    if(tech.dependency_technology_id<256)set_faction_technology_unlocked(mask,tech.dependency_technology_id,true);
    changed|=before!=mask;
   }
  }
 };
 const auto field=FieldGrid::create(96,80,0,0);const auto& cell=field.at(40,40);
 auto water_field=field;const std::vector<uint8_t> water_selection(field.width()*field.height(),1U);
 water_field.set_water_selection(water_selection,1.F,true);
 uint32_t cases=0,units=0,motion_cases=0,walker_cases=0,tracked_cases=0,deployment_cases=0,naval_cases=0,failures=0,nonpositive_bob_steps=0;
 for(const auto& preset:assets.army_presets()){
  if(preset.browser_kind!=PlacementBrowserKind::Unit&&preset.browser_kind!=PlacementBrowserKind::Building)continue;
  if(preset.browser_kind==PlacementBrowserKind::Unit)++units;
  std::set<FactionTechnologyMask256> masks;FactionTechnologyMask256 all{},empty{};
  all.fill(UINT32_MAX);masks.insert(empty);masks.insert(all);
  for(const auto& group:preset.model_variant_groups)for(const auto& variant:group.variants){
   FactionTechnologyMask256 mask{};set_faction_technology_unlocked(mask,variant.technology_unlock_id,true);close_mask(mask);masks.insert(mask);
   for(const auto& replacement:variant.faction_technology_replacements){
    auto upgraded=mask;set_faction_technology_unlocked(upgraded,replacement.technology_unlock_id,true);close_mask(upgraded);masks.insert(upgraded);
   }
  }
  std::set<std::vector<uint32_t>> seen;
  for(const auto& mask:masks){
   const auto nodes=resolve_faction_technology_hierarchy(preset,mask);
   std::vector<uint32_t> signature;for(const auto& node:nodes){signature.push_back(node.model_definition_id);signature.push_back(node.parent_index);signature.push_back(node.child_slot);}
   if(!seen.insert(signature).second)continue;
   ++cases;uint32_t missing=0,rotating=0,bobbing=0,weapon_nodes=0,unresolved_links=0;
   const auto root=std::find_if(nodes.begin(),nodes.end(),[](const auto& n){return n.parent_index==UINT32_MAX;});
   if(root==nodes.end()){++failures;std::cout<<"CASE arm="<<preset.registry_id<<" missing_root=1\n";continue;}
   for(size_t i=0;i<nodes.size();++i){const auto& node=nodes[i];
    if(!node.creates_runtime_node())continue;
    if(!runtime->definitions.contains(node.model_definition_id)||(!node.sprite_path.empty()&&!runtime->sprites.contains(node.sprite_path)))++missing;
    if(node.parent_index<nodes.size()){
     const auto& parent=nodes[node.parent_index];
     rotating+=model_runtime_child_yaw_step_turn16(parent,node.child_slot)!=0;
     bobbing+=model_runtime_child_uses_bounded_vertical_channel(parent,node.child_slot);
     if(model_runtime_child_uses_bounded_vertical_channel(parent,node.child_slot)&&parent.simulation.pitch_max_velocity_turn16<=0)++nonpositive_bob_steps;
     const auto sprite=runtime->sprites.find(parent.sprite_path);
     if(sprite==runtime->sprites.end()||node.child_slot>=6||!sprite->second.child_translations_q12[node.child_slot])++unresolved_links;
    }
    weapon_nodes+=node.simulation.has_weapon_definition();
   }
   const auto weapons=assets.simulation_weapon_profiles_for_hierarchy(nodes);
   uint32_t bad_shots=0;for(const auto& weapon:weapons)bad_shots+=!runtime->shots.contains(weapon.shot_definition_id);
   const auto cls=root->runtime_class_id;bool contacts=false;float travel=0;uint32_t contact_serial=0;int32_t tread0=0,tread1=0;
   // Class18 consumes its pioneer command state, not a bare actor move target.
   const bool mobile=cls==1||cls==2||cls==3||cls==17||cls==19;
   if(mobile&&root->simulation.class_step_q12>0){
    ++motion_cases;auto seed=seed_for(preset,assets,mask);seed.faction_index=1;seed.source_placement_index=SIZE_MAX;
    const auto& test_field=cls==19?water_field:field;
    seed.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),cls==19?1.F:0.F};seed.actor.move_target=seed.actor.position;seed.actor.move_target->x+=3;
    seed.tank_track_animation_enabled=cls==2&&preset.has_primary_texture_animation;
    seed.tank_track_texture_travel_scale=preset.track_texture_travel_scale;
    if(const auto sprite=runtime->sprites.find(root->sprite_path);sprite!=runtime->sprites.end())seed.tank_track_local_bounds_y0=sprite->second.bounds_min.y;
    seed.class17_root_child_deployment_enabled=hierarchy_root_uses_class17_deployment_tilt(nodes);
    simulation_initialize_class17_deployment(seed.actor,true,seed.class17_root_child_deployment_enabled);
    SimulationLabConfig config;config.run_count=1;config.duration_ticks=128;
    SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;
    scenario.active_faction_count=1;scenario.assignable_faction_count=1;
    scenario.runtime_catalog=runtime;
    std::array<FactionTechnologyMask256,8> tech{};tech[1]=mask;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};SimulationLabExperiment lab;
    lab.reset(config,parameters,std::span(&seed,1),{},{},{},tech,{},scenario);
    // This fixture supplies a persistent route order. The legacy nearest-hostile
    // adapter intentionally drops bare targets in a world with no hostiles.
    lab.runs()[0].entities[0].military_ai_source_dispatch=true;
    lab.step(test_field,128);
    const auto& entity=lab.runs()[0].entities[0];const auto& actor=entity.actor;
    travel=std::hypot(actor.position.x-seed.actor.position.x,actor.position.y-seed.actor.position.y);
    contacts=actor.articulated_contacts_initialized;contact_serial=actor.articulated_contact_event_serial;
    tread0=actor.primary_track_texture_offset_u;tread1=actor.secondary_track_texture_offset_u;
    if(cls==3){++walker_cases;if(!contacts||!contact_serial)++failures;}
    if(cls==2&&seed.tank_track_animation_enabled){++tracked_cases;if(!tread0&&!tread1)++failures;}
    if(cls==17){++deployment_cases;
     if(actor.class17_rotor_deploy_ticks_remaining!=0||
        actor.class17_rotor_tilt_turn16!=(seed.class17_root_child_deployment_enabled?0x3FF0U:0U))++failures;
    }
    if(cls==19)++naval_cases;
    if(!std::isfinite(travel)||travel<0.001F)++failures;
    const auto poses=simulation_lab_current_model_poses(entity,test_field,*runtime);
    for(const auto& pose:poses){if(!std::isfinite(pose.position.x)||!std::isfinite(pose.position.y)||!std::isfinite(pose.position.z))++failures;for(const auto v:pose.basis)if(!std::isfinite(v))++failures;}
   }
   failures+=missing+bad_shots;
   std::cout<<"CASE arm="<<preset.registry_id<<" class="<<cls<<" root_mdl="<<root->model_definition_id
    <<" nodes="<<nodes.size()<<" rotate_channels="<<rotating<<" bob_channels="<<bobbing
    <<" weapon_nodes="<<weapon_nodes<<" weapons="<<weapons.size()<<" missing="<<missing
    <<" bad_shots="<<bad_shots<<" unresolved_links="<<unresolved_links<<" motion_test="<<(mobile&&root->simulation.class_step_q12>0)
    <<" travel="<<travel<<" contacts="<<contacts<<" contact_serial="<<contact_serial
    <<" tread0="<<tread0<<" tread1="<<tread1<<'\n';
  }
 }
 std::cout<<"V645_STOCK_ANIMATION cases="<<cases<<" unit_presets="<<units<<" motion_cases="<<motion_cases
  <<" walker_cases="<<walker_cases<<" track_cases="<<tracked_cases<<" deployment_cases="<<deployment_cases<<" naval_cases="<<naval_cases<<" nonpositive_bob_steps="<<nonpositive_bob_steps
  <<" failures="<<failures<<"; CPU/state and asset references,not GPU pixels or complete aircraft/pioneer/sortie behavior\n";
 return failures||!validation.issues.empty()?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
