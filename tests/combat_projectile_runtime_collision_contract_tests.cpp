#include "thandor/effect.hpp"
#include "thandor/simulation.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
std::string read_text(const std::filesystem::path& path){
    std::ifstream in(path,std::ios::binary);std::ostringstream out;out<<in.rdbuf();return out.str();
}
bool near(float a,float b,float eps=1.0e-6F){return std::abs(a-b)<=eps;}
}

int main(){
    thandor::ShotDefinitionRecord shot;
    shot.trajectory_mode=1;
    shot.launch_speed_q12=thandor::float_to_q12(1.0F);
    shot.ballistic_divisor_q12=thandor::float_to_q12(0.1F);
    shot.projectile_lifetime_ticks=32U;
    thandor::SimulationWeaponProfile weapon;
    weapon.shot_definition_id=77U;
    std::array<thandor::Vec3f,1> launch{{{0.0F,0.0F,1.0F}}};
    uint64_t next_serial=10U;
    auto volley=thandor::simulation_create_volley(
        123U,4U,weapon,launch,{8.0F,0.0F,1.0F},shot,100U,next_serial,
        0xFFFFFFFFU,std::nullopt,thandor::SimulationProjectileOwnerKind::LinkedClass21,55U);
    assert(volley.size()==1U);
    auto projectile=volley.front();
    assert(projectile.owner_placement_index==123U);
    assert(projectile.owner_kind==thandor::SimulationProjectileOwnerKind::LinkedClass21);
    assert(projectile.owner_runtime_serial==55U);

    const auto frozen_target=projectile.target_position;
    thandor::simulation_update_projectile_live_target(projectile,shot,{20.0F,5.0F,2.0F});
    assert(near(projectile.target_position.x,frozen_target.x));
    assert(near(projectile.target_position.y,frozen_target.y));
    assert(near(projectile.target_position.z,frozen_target.z));

    shot.trajectory_mode=2;
    projectile.target_position=frozen_target;
    projectile.lifetime_ticks=32U;
    projectile.travel_ticks=32U;
    thandor::simulation_update_projectile_live_target(projectile,shot,{20.0F,5.0F,2.0F});
    assert(near(projectile.target_position.x,20.0F));
    assert(near(projectile.target_position.y,5.0F));

    const auto source=std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto editor=read_text(source/"src/editor/editor_app.cpp");
    assert(editor.find("simulation_refresh_non_direct_projectile_model_collision(projectile,*shot,tick)")!=std::string::npos);
    assert(editor.find("current_tick-1U")!=std::string::npos);
    assert(editor.find("projectile.travel_ticks==age")!=std::string::npos);
    assert(editor.find("surface_distance+1.0e-6F<model->distance")!=std::string::npos);
    assert(editor.find("if(effect_id==0U)return;")!=std::string::npos);
    assert(editor.find("projectile.travel_ticks=age;")!=std::string::npos);
    assert(editor.find("simulation_projectile_impacted(projectile,tick)")!=std::string::npos);
    return 0;
}
