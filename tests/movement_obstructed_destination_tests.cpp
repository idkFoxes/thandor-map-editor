#include "thandor/simulation_lab_geometry.hpp"
#include "thandor/simulation_command_markers.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
SimulationLabSeedEntity mover(Vec3f position){
    SimulationLabSeedEntity s;s.army_asset_id=1;s.faction_index=1;s.runtime_model_class_id=1;
    s.actor.position=position;s.root_model_maximum_integrity=1000;
    s.has_traversal_profile=true;s.traversal_low_band_index=0;s.traversal_high_band_index=4;
    s.motion.speed_q12_per_tick=64;s.motion.collision_radius_q12=120;
    s.motion.yaw_max_velocity_turn16=s.motion.yaw_acceleration_turn16=0x4000;
    return s;
}
SimulationLabConfig config(){SimulationLabConfig c;c.run_count=1;c.duration_ticks=1024;
    c.sweep_mode=SimulationLabSweepMode::Fixed;return c;}
SimulationLabScenarioRuntimeSeed scene(){SimulationLabScenarioRuntimeSeed s;s.enabled=true;
    s.active_faction_count=s.assignable_faction_count=1;s.ai_planning_enabled=false;return s;}
}
int main()try{
    std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
    unsigned cases=0;
    // Small obstacles can cover the requested world point while leaving its
    // rounded scratch centre clear. Large obstacles cover several scratch cells.
    // Both static props and live building footprints must resolve a usable stop.
    for(int radius:{128,256,1024,3072})for(bool building:{false,true}){
        auto field=FieldGrid::create(40,40,0,0);
        const Vec3f from{q12_to_float(51430),q12_to_float(-30086),0};
        const Vec3f goal{q12_to_float(63718),q12_to_float(-30086),0};
        auto unit=mover(from);auto scenario=scene();std::vector<SimulationLabSeedEntity> seeds{unit};
        if(building){SimulationLabSeedEntity prop;prop.army_asset_id=300;prop.faction_index=1;
            prop.runtime_model_class_id=11;prop.building=true;prop.actor.position=goal;
            prop.root_model_maximum_integrity=1000;prop.motion.collision_radius_q12=radius;seeds.push_back(prop);
        }else scenario.static_navigation_blockers.push_back({63718,-30086,radius});
        SimulationLabExperiment lab;lab.reset(config(),ki,seeds,{},{},{},{},{},scenario);
        auto& run=lab.runs()[0];run.entities[0].actor.move_target=goal;
        lab.step(field,1024);const auto& actor=run.entities[0].actor;
        require(!actor.move_target&&!actor.move_waypoint,"blocked destination left a live command or waypoint");
        const auto markers=simulation_lab_command_markers(run);
        require(std::none_of(markers.begin(),markers.end(),[](auto& m){return m.owner_entity_index==0;}),
            "completed blocked-goal command left a drive marker");
        require(run.metrics.navigation_destination_retargets>0,"blocked endpoint was not resolved");
        require(std::hypot(actor.position.x-goal.x,actor.position.y-goal.y)>q12_to_float(radius+120),
            "unit entered blocked destination footprint");
        ++cases;
    }
    // Rounded-cell LOS used to shortcut through the blocked tile immediately
    // west of this off-centre source. The reverse search also admitted diagonal
    // corner edges whose centres were clear. Exercise complete command playback.
    {
        auto field=FieldGrid::create(100,100,0,0);field.at(85,6).water_delta_q12=600;
        auto unit=mover({50.176F,-3.08708F,0});unit.traversal_low_band_index=1;
        unit.traversal_high_band_index=5;unit.motion.collision_radius_q12=1250;
        unit.actor.body_yaw_turn16=53072;
        const Vec3f goal{47.8313F,-7.80859F,0};
        SimulationLabExperiment lab;lab.reset(config(),ki,std::span(&unit,1),{},{},{},{},{},scene());
        auto& run=lab.runs()[0];run.entities[0].actor.move_target=goal;lab.step(field,1024);
        const auto& actor=run.entities[0].actor;
        require(!actor.move_target&&!actor.move_waypoint,"off-centre terrain route remained stuck");
        require(std::abs(actor.position.x-goal.x)<=1.F&&std::abs(actor.position.y-goal.y)<=1.F,
            "terrain route cleared without reaching the final arrival window");
        ++cases;
    }
    // A hull between adjacent centres can be absent from every node's
    // occupancy sample. The physical route must still go around the hull.
    {
        auto field=FieldGrid::create(40,40,0,0);
        const auto a=v519_path_scratch_center_to_world_q12({30,30});
        const auto b=v519_path_scratch_center_to_world_q12({70,30});
        const auto left=v519_path_scratch_center_to_world_q12({45,30});
        const auto right=v519_path_scratch_center_to_world_q12({46,30});
        auto unit=mover({q12_to_float(a.x_q12),q12_to_float(a.y_q12),0});
        unit.motion.collision_radius_q12=16;
        auto scenario=scene();scenario.static_navigation_blockers.push_back(
            {(left.x_q12+right.x_q12)/2,left.y_q12,16});
        SimulationLabExperiment lab;lab.reset(config(),ki,std::span(&unit,1),{},{},{},{},{},scenario);
        auto& run=lab.runs()[0];run.entities[0].actor.move_target=Vec3f{q12_to_float(b.x_q12),q12_to_float(b.y_q12),0};
        lab.step(field,1024);
        require(!run.entities[0].actor.move_target&&!run.entities[0].actor.move_waypoint,
            "small hull between clear lattice centres stranded the route");++cases;
    }
    {
        auto field=FieldGrid::create(40,40,0,0);
        const auto a=v519_path_scratch_center_to_world_q12({30,30});
        const V519PathWorldPointQ12 b{a.x_q12+8192,a.y_q12};
        const auto r=v565_resolve_route_fast_for_simulation_lab(field,a.x_q12,a.y_q12,b.x_q12,b.y_q12,2,0,4,{},120);
        require(r.disposition==V528RouteDisposition::Direct&&!r.requested_cell_retargeted,
            "clear route was unnecessarily retargeted");
        require(r.resolved_world_q12.x_q12==b.x_q12&&r.resolved_world_q12.y_q12==b.y_q12,
            "clear route lost its exact requested point");++cases;
    }
    std::cout<<"MOVEMENT_OBSTRUCTION PASS cases="<<cases<<" static_and_building=1 off_centre_corner=1 clear_endpoint=1\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
