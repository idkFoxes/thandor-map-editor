#include "thandor/simulation.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string read_text(const std::filesystem::path& path){
    std::ifstream input(path,std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),{});
}
}

int main(){
    try{
        using namespace thandor;
        ShotDefinitionRecord straight;
        straight.trajectory_mode=0;
        straight.launch_speed_q12=0x1000;
        straight.range_scale_d0_q12=0x2000;
        straight.range_scale_270_q12=0;
        straight.elevation_offset_angle16=0;
        require(shot_selection_range_q12(straight)>0,"straight shot range must be recovered");

        ShotDefinitionRecord ballistic;
        ballistic.trajectory_mode=1;
        ballistic.launch_speed_q12=0x3000;
        ballistic.ballistic_divisor_q12=0x100;
        require(shot_selection_range_q12(ballistic)>0,"ballistic shot range formula failed");
        const auto angles=simulation_shot_launch_angles({0,0,0},{8,0,0},ballistic);
        require(angles.valid&&angles.pitch_turn16!=0U,"ballistic high launch solution was not produced");

        SimulationAxisState yaw{0U,-10};
        require(!simulation_step_yaw(yaw,0x1000U,64,8),"opposed yaw velocity must not report aligned");
        require(yaw.velocity_turn16==0,"opposed yaw velocity must be cleared before reversing");
        require(yaw.angle_turn16==0U,"opposed yaw clear tick must not rotate");

        SimulationAxisState pitch{0U,0};
        (void)simulation_step_pitch(pitch,0x3000U,64,8,-0x0800,0x0800);
        for(int i=0;i<256;++i)(void)simulation_step_pitch(pitch,0x3000U,64,8,-0x0800,0x0800);
        require(static_cast<int16_t>(pitch.angle_turn16)==0x0800,"pitch did not clamp to MDL maximum");

        SimulationWeaponProfile profile;
        profile.shot_definition_id=7U;profile.reload_ticks=5U;profile.maximum_range_q12=0x20000;
        profile.yaw_max_velocity_turn16=0x4000;profile.yaw_acceleration_turn16=0x4000;
        profile.pitch_max_velocity_turn16=0x4000;profile.pitch_acceleration_turn16=0x4000;
        profile.pitch_min_turn16=-0x4000;profile.pitch_max_turn16=0x4000;profile.launch_attachment_count=2U;
        SimulationActorState actor;actor.position={0,0,0};actor.weapons.resize(2U);
        ShotDefinitionRecord fast;fast.trajectory_mode=0;fast.launch_speed_q12=0x1000;fast.lifetime_ticks=64U;
        Vec3f target{4,0,0};
        // Two independent channels must fire and reload without sharing state.
        for(int i=0;i<3;++i){
            (void)simulation_tick_weapon(actor,actor.weapons[0],profile,fast,actor.position,&target,true);
            (void)simulation_tick_weapon(actor,actor.weapons[1],profile,fast,actor.position,&target,true);
        }
        require(actor.weapons[0].cooldown_ticks!=0U&&actor.weapons[1].cooldown_ticks!=0U,
                "independent weapon channels did not fire");
        actor.weapons[0].cooldown_ticks=0U;
        require(actor.weapons[1].cooldown_ticks!=0U,"weapon cooldowns were aliased");

        uint64_t serial=1U;
        const Vec3f muzzles[2]{{0,0,1},{0,0,2}};
        const auto volley=simulation_create_volley(3U,1U,profile,muzzles,target,fast,10U,serial);
        require(volley.size()==2U,"one projectile per kind-2 muzzle was not preserved");
        require(volley[0].weapon_profile_index==1U&&volley[1].serial==2U,
                "volley weapon ownership/serials are incorrect");
        require(simulation_projectile_impacted(volley[0],10U+volley[0].travel_ticks),
                "projectile impact timing failed");

        const auto source=std::filesystem::path(THANDOR_SOURCE_DIR);
        const auto editor=read_text(source/"src/editor/editor_app.cpp");
        const auto header=read_text(source/"src/editor/editor_app.hpp");
        const auto catalog=read_text(source/"src/core/catalog.cpp");
        for(const char* token:{
            "Tool::SimulationTarget","Tool::SimulationMove","simulation_tick_weapon",
            "simulation_line_of_fire_clear","simulation_create_volley(index,weapon_index",
            "launch_effect_id","secondary_effect_id","terrain_impact_effect_id",
            "Finish driving at current map position","Bake whole placement facing toward target",
            "Selected armed placement targets exclusively","In-game selection / simulation",
            "synchronize_simulated_actor_placement(index,actor)",
            "SimulationLaunchPolicy::AlternateLaunchNodes",
            "SimulationLaunchPolicy::SequentialKind2Attachments"})
            require(editor.find(token)!=std::string::npos,token);
        require(header.find("std::vector<thandor::SimulationProjectileState>")!=std::string::npos,
                "editor does not own transient projectile state");
        require(catalog.find("profile.maximum_range_q12 = shot_selection_range_q12(*shot)")!=std::string::npos,
                "catalog does not use recovered SHT range");
        require(editor.find("preserved_tail_dwords")!=std::string::npos,
                "existing unknown LEV tail preservation disappeared");

        std::cout<<"G371 simulation weapon/selection tests PASS\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"G371 simulation weapon/selection tests FAIL: "<<error.what()<<'\n';return 1;
    }
}
