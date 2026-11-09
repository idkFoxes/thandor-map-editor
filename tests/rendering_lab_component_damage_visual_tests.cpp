#include "thandor/simulation_lab_damage_visuals.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    using namespace thandor;
    constexpr size_t none=std::numeric_limits<size_t>::max();
    std::vector<ModelVisualNode> nodes(3);
    nodes[0].parent_index=std::numeric_limits<uint32_t>::max();
    nodes[0].model_definition_id=100;
    nodes[0].simulation.damage_effect_id=110;
    nodes[0].simulation.damage_effect_health_threshold_percent=33;
    nodes[1].parent_index=0;
    nodes[1].model_definition_id=200;
    nodes[1].simulation.damage_effect_id=117;
    nodes[1].simulation.damage_effect_health_threshold_percent=33;
    nodes[2].parent_index=1;
    nodes[2].model_definition_id=200; // same-MDL child: never a separate threshold emitter
    nodes[2].simulation.damage_effect_id=118;
    nodes[2].simulation.damage_effect_health_threshold_percent=33;
    SimulationLabEntity entity;
    entity.construction_complete=true;
    entity.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(nodes);
    entity.root_model_component_index=0;
    entity.root_model_maximum_integrity=1000;
    entity.root_model_integrity=900;
    SimulationLabModelComponentState root,gun;
    root.source_model_node_index=0;root.maximum_integrity=1000;root.integrity=900;
    gun.source_model_node_index=1;gun.parent_component_index=0;gun.maximum_integrity=100;gun.integrity=32;
    entity.model_components={root,gun};
    auto emitter=[&](size_t n){return simulation_lab_damage_emitter_for_model(entity,nodes,n);};
    check(!emitter(0),"healthy building root emitted damage smoke");
    check(emitter(1)==117U,"damaged child weapon lost its independent authored effect");
    check(!emitter(2),"same-definition SPR child emitted duplicate threshold effect");
    entity.model_components[1].integrity=33;
    check(!emitter(1),"strict threshold became less-than-or-equal");
    entity.model_components[1].integrity=32;
    entity.model_components[1].terminal_runtime_flag8=true;
    check(!emitter(1),"terminal turret emitted ordinary low-health smoke over destruction EFF");
    entity.model_components[1].terminal_runtime_flag8=false;
    entity.model_components[1].integrity=0;
    check(!emitter(1),"zero-integrity turret emitted normal damage smoke");
    entity.model_components[1].integrity=32;
    entity.model_components[1].runtime_removed=true;
    check(!emitter(1),"detached turret kept emitting smoke after removal");
    entity.model_components[1].runtime_removed=false;
    entity.model_components[0].integrity=320;
    check(emitter(0)==110U,"damaged root did not use its own authored effect");
    entity.model_components[0].terminal_runtime_flag8=true;
    entity.root_model_terminal=true;
    check(!emitter(0),"terminal building root emitted periodic low-health smoke");
    entity.model_components[0].terminal_runtime_flag8=false;
    entity.root_model_terminal=false;
    check(emitter(1)==117U,"root damage incorrectly suppressed live child smoke");
    entity.construction_complete=false;
    check(!emitter(0)&&!emitter(1),"construction emitted live damage effects");
    entity.construction_complete=true;
    entity.runtime_removed=true;
    check(!emitter(0)&&!emitter(1),"destroyed actor emitted damage effects");
    entity.runtime_removed=false;
    entity.model_components.clear();
    entity.root_model_integrity=320;
    check(emitter(0)==110U,"legacy root-only actor lost fallback");
    entity.root_model_terminal=true;
    check(!emitter(0),"terminal root-only actor emitted ordinary damage smoke");
    entity.root_model_terminal=false;
    check(!emitter(1),"missing child component inherited root damage");
    entity.model_components={root,gun};
    entity.model_components[1].maximum_integrity=0;
    check(!emitter(1),"zero max integrity passed threshold filter");
    check(!simulation_lab_damage_emitter_for_model(entity,nodes,none),"invalid node accepted");
    std::cout<<"V711_LAB_COMPONENT_DAMAGE_VISUAL PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
