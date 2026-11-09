#include "thandor/simulation_lab_environment.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main()try{
 const auto field=FieldGrid::create(40,40,0,0);
 SimulationLabRuntimeCatalog catalog;std::vector<ModelVisualNode> nodes(9);
 for(size_t i=0;i<nodes.size();++i){auto& n=nodes[i];n.sprite_path=std::to_string(i);n.model_definition_id=1;
  n.runtime_class_id=3;n.local_rotation_angle1=0x4000;n.parent_index=i==0?UINT32_MAX:static_cast<uint32_t>(i-1);
  catalog.sprites[n.sprite_path]={};}
 nodes[1].parent_index=0;nodes[1].child_slot=0;nodes[5].parent_index=0;nodes[5].child_slot=1;
 for(size_t i:{2U,3U,4U,6U,7U,8U})nodes[i].child_slot=0;
 catalog.sprites["0"].child_translations_q12[0]=std::array<int32_t,3>{0,-4096,0};
 catalog.sprites["0"].child_translations_q12[1]=std::array<int32_t,3>{0,4096,0};
 for(size_t i:{1U,2U,3U,5U,6U,7U})catalog.sprites[std::to_string(i)].child_translations_q12[0]=std::array<int32_t,3>{0,0,-4096};
 // The authored ankles include a lateral radius. It must not be added to initial contact spacing.
 catalog.sprites["1"].child_translations_q12[0]=std::array<int32_t,3>{0,2048,-4096};
 catalog.sprites["5"].child_translations_q12[0]=std::array<int32_t,3>{0,-2048,-4096};
 catalog.sprites["8"].height_offset=.25F;
 for(uint32_t yaw:{0U,0x4000U,0x8000U,0xC000U}){
  SimulationLabEntity entity;entity.runtime_model_class_id=3;entity.actor.body_yaw_turn16=yaw;entity.actor.position={10,10,3};
  entity.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(nodes);
  check(simulation_lab_prepare_articulated_contacts(entity,field,catalog),"valid factory walker contact geometry missing");
  check(entity.articulated_contact_geometry_enabled&&!entity.actor.articulated_contacts_initialized,"contact ownership seeded incorrectly");
  check(std::abs(entity.articulated_contact_local_offsets[0].y+1)<.0001F&&std::abs(entity.articulated_contact_local_offsets[1].y-1)<.0001F,"body-local foot offsets changed under root rotation");
  check(entity.articulated_shared_foot_placement_offset==.25F,"shared right-foot offset missing");
  entity.actor.articulated_contacts_initialized=true;entity.actor.articulated_contact_progress_q12=123;
  check(simulation_lab_prepare_articulated_contacts(entity,field,catalog)&&entity.actor.articulated_contact_progress_q12==123&&entity.actor.articulated_contacts_initialized,"preseeded contact state reset");
 }
 SimulationLabEntity invalid;invalid.runtime_model_class_id=3;nodes.pop_back();invalid.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(nodes);
 check(!simulation_lab_prepare_articulated_contacts(invalid,field,catalog)&&!invalid.articulated_contact_geometry_enabled,"broken chain fabricated a foot");
 std::cout<<"V634_WALKER_CONTACTS PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
