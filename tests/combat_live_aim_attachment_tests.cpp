#include "thandor/simulation_lab_live_aim_nodes.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

static void require(bool good,const char* message) {
    if(!good)throw std::runtime_error(message);
}

int main() {
    using namespace thandor;
    std::vector<ModelVisualNode> nodes(4);
    nodes[0].parent_index=std::numeric_limits<uint32_t>::max();
    nodes[0].model_definition_id=100;
    nodes[1].parent_index=0;
    nodes[1].child_slot=0;
    nodes[1].model_definition_id=200;
    nodes[2].parent_index=1;
    nodes[2].child_slot=2;
    nodes[2].model_definition_id=200;
    nodes[3].parent_index=0;
    nodes[3].child_slot=1;
    nodes[3].model_definition_id=300;
    SimulationLabEntity entity;
    entity.runtime_model_class_id=21;
    entity.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(nodes);
    entity.model_components.resize(3);
    entity.model_components[0].source_model_node_index=0;
    entity.model_components[1].source_model_node_index=1;
    entity.model_components[1].parent_component_index=0;
    entity.model_components[2].source_model_node_index=3;
    entity.model_components[2].parent_component_index=0;

    require(simulation_lab_live_target_aim_node_index(entity,nodes)==1,
        "live class-21 child-zero aim should override root");
    require(simulation_lab_live_weapon_pose_node(entity,nodes,2,nodes.size()),
        "live descendant is a usable pose");
    entity.model_components[1].runtime_removed=true;
    require(simulation_lab_live_target_aim_node_index(entity,nodes)==0,
        "detached class-21 child must fall back to live root");
    require(!simulation_lab_live_weapon_pose_node(entity,nodes,1,nodes.size()),
        "detached gun pivot is not a live aim source");
    require(!simulation_lab_live_weapon_pose_node(entity,nodes,2,nodes.size()),
        "detached gun descendant is not a live aim source");
    require(simulation_lab_live_weapon_pose_node(entity,nodes,3,nodes.size()),
        "unrelated live sibling must remain usable");
    entity.model_components[0].runtime_removed=true;
    require(!simulation_lab_live_target_aim_node_index(entity,nodes),
        "released root must not leave a stale live target aim node");
    entity.model_components[0].runtime_removed=false;
    entity.model_components[1].runtime_removed=false;
    entity.runtime_removed=true;
    require(!simulation_lab_live_target_aim_node_index(entity,nodes),
        "released entity cannot be an aimed-at model");
    entity.runtime_removed=false;
    nodes[1].flags=1U;
    require(simulation_lab_live_target_aim_node_index(entity,nodes)==0,
        "non-runtime child should not override the root aim");
    // The unit-factory callback unlinks child slot 3 and all its descendants.
    nodes[1].flags=0U;nodes[1].child_slot=3U;
    entity.runtime_model_class_id=13U;
    entity.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(nodes);
    require(simulation_lab_model_node_present(entity,2),"factory marker missing before its callback");
    entity.production_child3_removed=true;
    require(!simulation_lab_model_node_present(entity,1)&&!simulation_lab_model_node_present(entity,2),
        "unlinked factory child slot 3 retained collision descendants");
    require(simulation_lab_model_node_present(entity,3),"factory child removal hid another slot");

}
