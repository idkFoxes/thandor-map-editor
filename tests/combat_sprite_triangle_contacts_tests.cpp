#include "thandor/simulation_lab_shot_rays.hpp"
#include <cmath>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace thandor;
static void check(bool ok, const char* why){if(!ok)throw std::runtime_error(why);}
static SimulationLabStaticShotHull make_box(Vec3f origin,float r){
    SimulationLabStaticShotHull h; h.impact_class=1;
    for(uint32_t i=0;i<8U;++i)h.corners[i]={origin.x+((i&1U)?r:-r),origin.y+((i&2U)?r:-r),origin.z+((i&4U)?r:-r)};
    return h;
}
static std::shared_ptr<SimulationLabTriangleSurface> make_surface(){
    auto mesh=std::make_shared<SimulationLabTriangleSurface>();
    // An offset rectangle within the full model's much larger bounding volume.
    // Two triangles, intentionally double-sided.
    mesh->positions={{.3F,-.25F,-.25F},{.3F,.25F,-.25F},{.3F,.25F,.25F},{.3F,-.25F,.25F}};
    mesh->indices={0,1,2,0,2,3};
    return mesh;
}
int main()try{
    auto h=make_box({2,0,0},1.F);
    h.collision_surface=make_surface(); h.collision_pose.position={2,0,0};
    const std::array objects{h};
    const Vec3f direction{1,0,0};
    auto hit=simulation_lab_raycast_models({0,0,0},direction,4.F,std::nullopt,{},objects);
    check(hit&&std::fabs(hit->distance-2.3F)<.0001F,"ray hit broad box rather than actual SPR triangle");
    // This ray intersects the broad box at x=1 but misses all model triangles.
    hit=simulation_lab_raycast_models({0,.6F,0},direction,4.F,std::nullopt,{},objects);
    check(!hit,"empty bounding-box space incorrectly intercepted projectile");
    // When no decoded triangles exist, preserve compatibility with authored/static hulls.
    h.collision_surface.reset(); const std::array fallback{h};
    hit=simulation_lab_raycast_models({0,.6F,0},direction,4.F,std::nullopt,{},fallback);
    check(hit&&std::fabs(hit->distance-1.F)<.0001F,"missing mesh lost legacy hull fallback");
    // A second pose confirms that geometry uses the model's world transform.
    h.collision_surface=make_surface(); h.collision_pose.position={3,0,0};
    // Update broad phase too (the actor has moved as a single entity).
    for(auto& corner:h.corners)corner.x+=1.F;
    const std::array moved{h};
    hit=simulation_lab_raycast_models({0,0,0},direction,5.F,std::nullopt,{},moved);
    check(hit&&std::fabs(hit->distance-3.3F)<.0001F,"narrow phase ignored live model transform");
    // A nearer narrow-phase miss must not shadow an actual farther target.
    auto near=make_box({1.5F,.6F,0},1.F);near.collision_surface=make_surface();
    near.collision_pose.position={1.5F,.6F,0};
    const std::array combo{near,moved[0]};
    hit=simulation_lab_raycast_models({0,0,0},direction,5.F,std::nullopt,{},combo);
    check(hit&&std::fabs(hit->distance-3.3F)<.0001F,"empty foreground hull hid valid triangle hit");
    // A rotated and sheared hierarchy pose must still report the same
    // segment fraction as world-space triangle intersection.
    auto tilted=make_box({2,0,0},2.F);
    tilted.collision_surface=make_surface();tilted.collision_pose.position={2,0,0};
    tilted.collision_pose.basis={0,-1,0,1,.2F,0,0,0,1};
    const std::array tilted_hulls{tilted};
    hit=simulation_lab_raycast_models({2,-2,0},{0,1,0},4.F,std::nullopt,{},tilted_hulls);
    check(hit&&std::fabs(hit->distance-2.3F)<.0003F,"tilted SPR hit changed under inverse-local ray");
    // A child node without its own MDL component must inherit the nearest
    // enclosing turret component, not default to the building collision root.
    auto field=FieldGrid::create(60,60,0,0);
    SimulationLabRuntimeCatalog catalog;
    std::vector<ModelVisualNode> hierarchy(3);
    hierarchy[0].sprite_path="root";hierarchy[0].model_definition_id=10;
    hierarchy[1].sprite_path="turret";hierarchy[1].model_definition_id=20;hierarchy[1].parent_index=0;
    hierarchy[2].sprite_path="barrel";hierarchy[2].model_definition_id=20;hierarchy[2].parent_index=1;
    for(const auto* key:{"root","turret","barrel"}){
        auto& sprite=catalog.sprites[key];sprite.has_bounds=true;
        sprite.bounds_min={-.1F,-.1F,-.1F};sprite.bounds_max={.1F,.1F,.1F};
    }
    SimulationLabEntity building;building.actor.position={10,-10,0};building.construction_complete=true;
    building.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(hierarchy);
    building.model_components.resize(2);
    building.model_components[0].source_model_node_index=0;
    building.model_components[1].source_model_node_index=1;
    building.model_components[1].parent_component_index=0;
    SimulationLabRun run;run.entities.push_back(building);
    const auto hulls=simulation_lab_current_shot_hulls(run,field,catalog);
    check(hulls.size()==3,"test hierarchy was not projected into three shot hulls");
    check(hulls[0].component_index==0&&hulls[1].component_index==1&&hulls[2].component_index==1,
        "nested barrel impact was misattributed to parent building instead of turret");
    run.entities[0].model_components[1].runtime_removed=true;
    const auto after_detach=simulation_lab_current_shot_hulls(run,field,catalog);
    check(after_detach.size()==1&&after_detach[0].component_index==0,
        "destroyed turret descendants remained collidable");
    std::cout<<"COMBAT_SPRITE_TRIANGLE_CONTACTS PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
