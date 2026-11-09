#include "thandor/fog_preview.hpp"
#include "thandor/simulation_lab_geometry.hpp"
#include "thandor/simulation_lab_catalog.hpp"
#include "thandor/map_document.hpp"
#include "../src/editor/model_transform.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
bool close(Vec3f a,Vec3f b,float epsilon=1.e-5F) { return std::abs(a.x-b.x)<epsilon&&std::abs(a.y-b.y)<epsilon&&std::abs(a.z-b.z)<epsilon; }
SimulationLabConfig config() { SimulationLabConfig c;c.run_count=1;c.duration_ticks=256;c.parameter_index=100;c.sweep_mode=SimulationLabSweepMode::Fixed;return c; }
SimulationLabSeedEntity unit(uint32_t arm,uint32_t faction,Vec3f p) {
    SimulationLabSeedEntity s;s.army_asset_id=arm;s.faction_index=faction;s.runtime_model_class_id=1;
    s.actor.position=p;s.root_model_maximum_integrity=1000;
    s.motion.speed_q12_per_tick=float_to_q12(.25F);s.motion.yaw_max_velocity_turn16=0x1000;s.motion.yaw_acceleration_turn16=0x1000;
    s.motion.collision_radius_q12=float_to_q12(.25F);return s;
}
void reset(SimulationLabExperiment& e,std::span<const SimulationLabSeedEntity> seeds,
           std::span<const V528PathRuntimeBlocker> blockers={}) {
    std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabScenarioRuntimeSeed scenario;scenario.static_navigation_blockers.assign(blockers.begin(),blockers.end());
    e.reset(config(),parameters,seeds,{}, {},{}, {},{},scenario);
}
}
int main(int argc,char** argv) try {
    check(retail_model_fog_intensity(0,0)==0,"unexplored scenery appeared");
    check(retail_model_fog_intensity(0x80,0)==135,"discovered scenery disappeared after sight loss");
    check(retail_model_fog_intensity(0x80,0x10)==0,"not-remembered model remained visible");
    check(retail_model_fog_intensity(0x90,0x10)==255,"current sight did not override memory policy");
    check(retail_model_fog_intensity(0x80,0x20)==135,"unrelated MDL flag changed memory policy");

    auto field=FieldGrid::create(17,17,0,0);
    for(uint32_t y=0;y<field.height();++y) for(uint32_t x=0;x<field.width();++x)
        field.at(x,y).terrain_height_q12=static_cast<int32_t>(x*70+y*45);
    const auto& cell=field.at(8,8);
    const Vec3f root{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0.F};
    const Vec3f local{.3F,-2.5F,.1F};
    for(const int kind:{0,1,2,4}) for(const uint32_t yaw:{0U,0x4000U,0x8000U,0xC000U,7311U}) {
        auto terrain=field.sample_terrain(root.x,root.y);
        const auto fixed=field.sample_retail_surface_heights_q12(float_to_q12(root.x),float_to_q12(root.y));
        check(fixed.valid,"interior contact fixture rejected");
        terrain.height=q12_to_float(fixed.terrain);
        auto contact=editor::resolve_placement_contact_pose(kind,terrain,
            q12_to_float(fixed.water_surface),q12_to_float(fixed.top),.2F);
        if(kind==2)contact.packed_normal_angles=retail_contact_normal_angles(field,float_to_q12(root.x),float_to_q12(root.y));
        const Vec3f position{root.x,root.y,contact.height};
        const auto transform=contact.align_to_normal?editor::terrain_contact_root_transform(position,turn16_to_radians(yaw),contact.normal,contact.packed_normal_angles)
            :editor::placement_root_transform(position.x,position.y,position.z,turn16_to_radians(yaw));
        // Render poses remain floats; native simulation quantizes the input
        // point and translation to Q12 and truncates the Q28 dot product.
        check(close(simulation_lab_factory_world_point(field,root,yaw,kind,.2F,local),editor::transform_point(transform,local),2.F/4096.F),
            "worker factory lookup disagrees with rendered contact transform");
    }
    {
        V528PathRuntimeBlocker b{0,0,float_to_q12(.25F)};
        check(simulation_lab_step_hits_blocker({-2,0,0},{2,0,0},float_to_q12(.25F),b),"swept step tunneled through a model");
        check(!simulation_lab_step_hits_blocker({-2,1,0},{2,1,0},float_to_q12(.25F),b),"clear parallel step was blocked");
        check(!simulation_lab_step_hits_blocker({.1F,0,0},{.6F,0,0},float_to_q12(.25F),b),"overlapped source could not move outward");
        check(simulation_lab_step_hits_blocker({.3F,0,0},{0,0,0},float_to_q12(.25F),b),"overlapped source moved deeper");
    }
    field=FieldGrid::create(17,17,0,0);
    const auto& flat=field.at(8,8);
    const Vec3f start{q12_to_float(flat.world_x_q12),q12_to_float(flat.world_y_q12),0.F};
    {
        auto parent=unit(301,1,start);parent.runtime_model_class_id=13;parent.building=true;
        parent.motion.speed_q12_per_tick=0;parent.production_door_step_q12=0x20000;
        SimulationLabProductionAsset dummy;dummy.army_asset_id=301;
        std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
        const std::array seeds{parent};const std::array assets{dummy};SimulationLabExperiment e;
        e.reset(config(),parameters,seeds,{},assets);
        auto& factory=e.runs()[0].entities[0];factory.production_behavior_state=2;
        factory.production_animated_subnode_ticks=7;
        for(uint32_t tick=1;tick<=4;++tick) {
            e.step(field,1);
            check(factory.production_door_texture_offset_v==tick*0x20000,
                "factory opening ignored authored door speed");
            check(factory.production_animated_subnode_ticks==7,
                "production subnodes animated during door opening");
        }
        check(factory.production_behavior_state==3,"fully open door did not enter exit-wait state");
        e.step(field,1);check(factory.production_behavior_state==4,"unlinked factory did not start closing");
        e.step(field,4);
        check(factory.production_behavior_state==0&&factory.production_door_texture_offset_v==0,
            "factory door did not close at its authored speed");
        e.step(field,2);
        check(factory.production_animated_subnode_ticks==7,
            "idle factory continued production subnode animation");
    }
    {
        auto mover=unit(1,1,start),enemy=unit(2,2,{start.x+5,start.y,0});enemy.motion={};
        mover.motion.speed_q12_per_tick=float_to_q12(1.F);mover.actor.body_yaw_turn16=0x2000;
        mover.motion.yaw_max_velocity_turn16=1;mover.motion.yaw_acceleration_turn16=1;
        mover.motion.heading_interpolation_distance_q12=0x1000;
        mover.motion.near_heading_error_limit_turn16=mover.motion.far_heading_error_limit_turn16=0xFFFF;
        const V528PathRuntimeBlocker scenery{float_to_q12(start.x+.5F),float_to_q12(start.y+.5F),float_to_q12(.1F)};
        SimulationLabExperiment e;const std::array seeds{mover,enemy};reset(e,seeds,std::span{&scenery,1});e.step(field,1);
        const auto& run=e.runs()[0];check(close(run.entities[0].actor.position,start),"turning step crossed neutral scenery");
        check(run.entities[0].actor.body_turn_velocity_turn16!=0,"collision incorrectly prevented steering acceleration");
        check(run.metrics.navigation_actual_step_blocks==1,"physical-step block was not diagnosed");
    }
    {
        auto mover=unit(1,1,start),enemy=unit(2,2,{start.x+2,start.y,0});enemy.motion={};
        mover.motion.speed_q12_per_tick=float_to_q12(1.F);mover.actor.body_yaw_turn16=0x4000;
        mover.motion.yaw_max_velocity_turn16=1;mover.motion.yaw_acceleration_turn16=1;
        mover.motion.heading_interpolation_distance_q12=0x1000;
        mover.motion.near_heading_error_limit_turn16=mover.motion.far_heading_error_limit_turn16=0xFFFF;
        mover.has_traversal_profile=true;mover.traversal_low_band_index=0;mover.traversal_high_band_index=1;
        auto proposed=mover.actor;proposed.move_target=enemy.actor.position;simulation_tick_actor_motion(proposed,mover.motion);
        const auto end=v519_path_world_to_scratch(float_to_q12(proposed.position.x),float_to_q12(proposed.position.y));
        auto blocked_field=field;
        // Place the boundary on the native 4x block, rather than the former shifted world-sample block.
        blocked_field.at(static_cast<uint32_t>(end.column)/4U,static_cast<uint32_t>(end.row)/4U).flags_and_material|=0x80000000U;
        SimulationLabExperiment e;const std::array seeds{mover,enemy};reset(e,seeds);
        auto& state=e.runs()[0].entities[0];state.navigation_direct_route_valid=true;state.navigation_target_cell_valid=true;
        state.navigation_target_cell=v519_path_world_to_scratch(float_to_q12(enemy.actor.position.x),float_to_q12(enemy.actor.position.y));
        state.navigation_recheck_tick=1000;e.prepare_navigation_terrain(blocked_field);e.step(blocked_field,1);
        check(close(e.runs()[0].entities[0].actor.position,start),"cached route let the actual turning step enter blocked terrain");
        check(e.runs()[0].metrics.navigation_actual_step_blocks==1,"cached terrain violation was not diagnosed");
    }
    {
        auto parent=unit(301,1,start);parent.runtime_model_class_id=13;parent.building=true;parent.motion.speed_q12_per_tick=0;
        parent.motion.collision_radius_q12=float_to_q12(1.F);parent.production_exit_world=Vec3f{start.x+3,start.y,0};
        auto child=unit(1,1,start),enemy=unit(2,2,{start.x+5,start.y,0});enemy.motion={};
        const std::array seeds{parent,child,enemy};SimulationLabExperiment e;reset(e,seeds);
        auto& run=e.runs()[0];run.entities[0].production_behavior_state=3;
        run.entities[1].production_linked_to_parent=true;run.entities[1].production_parent_entity_index=0;
        run.entities[1].actor.move_target=*parent.production_exit_world;
        bool detached=false;
        for(int tick=0;tick<16;++tick) { e.step(field,1);
            if(!run.entities[1].production_linked_to_parent) {
                check(simulation_lab_factory_footprint_cleared(run.entities[1],run.entities[0]),"factory detached its unit inside its footprint");
                detached=true;break;
            }
        }
        check(detached,"clear factory exit never released its link");

        child.has_traversal_profile=true;child.traversal_low_band_index=0;child.traversal_high_band_index=1;
        const V528PathRuntimeBlocker obstruction{float_to_q12(start.x),float_to_q12(start.y),float_to_q12(100.F)};
        const std::array blocked_seeds{parent,child,enemy};reset(e,blocked_seeds,std::span{&obstruction,1});
        auto& blocked=e.runs()[0];blocked.entities[0].production_behavior_state=3;
        blocked.entities[1].production_linked_to_parent=true;blocked.entities[1].production_parent_entity_index=0;
        blocked.entities[1].actor.move_target=*parent.production_exit_world;
        e.step(field,4);
        check(blocked.entities[1].production_linked_to_parent&&blocked.entities[1].actor.move_target.has_value(),
            "blocked factory exit lost ownership or its exit order");
        check(close(blocked.entities[1].actor.position,start),"blocked factory child drove through an obstruction");
        check(blocked.metrics.factory_exit_route_retries>0,"blocked factory exit was not retried");
    }
    if(argc==2) {
        const auto catalog=AssetCatalog::scan_game_root(argv[1]);
        const auto map=std::find_if(catalog.map_groups().begin(),catalog.map_groups().end(),[](const auto& g){
            return g.level_path=="level/asgard.lev";});
        check(map!=catalog.map_groups().end(),"stock Asgard missing");
        const auto document=MapDocument::open(catalog,*map);
        const auto blockers=simulation_lab_prepare_static_navigation_blockers(catalog,document.level());
        check(!blockers.empty(),"stock neutral collision owners were omitted");
        std::cout<<"STOCK_STATIC_BLOCKERS "<<blockers.size()<<'\n';
        for(const uint32_t id:{301U,302U,303U,304U,305U}) {
            const auto* preset=catalog.army_preset(id);check(preset!=nullptr,"stock factory missing");
            const auto hierarchy=resolve_faction_technology_hierarchy(*preset,{});
            const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& n){return n.parent_index==UINT32_MAX;});
            check(root!=hierarchy.end(),"stock factory root unresolved");
            check(preset->simulation_motion_profile.placement_contact_kind==root->simulation.placement_contact_kind,
                "factory catalog discarded MDL contact type");
            SimulationLabSeedEntity seed;simulation_lab_prepare_model_technologies(*preset,catalog,seed);
            check(seed.production_door_step_q12==root->simulation.class_step_q12,"factory lost authored door speed");
            check(seed.production_spawn_local&&seed.production_exit_local,"factory lost its lookup points");
            std::cout<<"STOCK_FACTORY arm="<<id<<" contact="<<preset->simulation_motion_profile.placement_contact_kind
                <<" door_step="<<seed.production_door_step_q12<<" height_offset="<<seed.production_root_sprite_height_offset<<'\n';
        }
    }
    std::cout<<"V605_MOVEMENT_VISIBILITY PASS\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
