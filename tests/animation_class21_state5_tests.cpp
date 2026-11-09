#include "thandor/simulation.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace thandor;

int main(){
    SimulationClass21LinkedChildState child;
    child.phase=SimulationClass21LinkedChildPhase::OffmapOutbound;
    assert(simulation_class21_begin_mission_pass(child,20));
    assert(child.mission_countdown==20);
    assert(child.mission_emission_counter==10);

    std::vector<uint8_t> emitted;
    uint32_t sound_attempts=0;
    bool completed=false;
    for(int tick=0;tick<21;++tick){
        const auto result=simulation_class21_tick_mission_pass_events(child,2);
        sound_attempts+=result.mapped_terrain_sound_attempts;
        for(uint32_t i=0;i<result.emission_count;++i)emitted.push_back(result.model_point_ordinals[i]);
        completed|=result.completed;
    }
    assert(sound_attempts==21U);
    assert((emitted==std::vector<uint8_t>{1,2,3,4,5,6,7}));
    assert(completed);
    assert(child.phase==SimulationClass21LinkedChildPhase::OffmapReturn);
    assert(child.mission_countdown==-1);
    assert(child.mission_emission_counter==-11);

    SimulationClass21CandidateScoreInput score{};
    score.source_world_x_q12=0;
    score.source_world_y_q12=0;
    score.source_impact_class_index=2;
    score.source_target_class_damage_q12[3]=2000;
    score.candidate_world_x_q12=0x1000;
    score.candidate_world_y_q12=0;
    score.candidate_collision_radius_q12=0x1000;
    score.candidate_runtime_class_id=4;
    score.candidate_impact_class_index=3;
    score.candidate_target_class_damage_q12[2]=1000;
    assert(simulation_class21_candidate_score_full_integrity(score)==0x111A98U);

    // A recovery/negative source metric path zeroes the source counter and quarters the score.
    score.source_target_class_damage_q12[3]=-2000;
    assert(simulation_class21_candidate_score_full_integrity(score)>0U);
    // Outside candidate radius+0x2000 is rejected before target scoring.
    score.source_target_class_damage_q12[3]=2000;
    score.candidate_world_x_q12=0x4001;
    assert(simulation_class21_candidate_score_full_integrity(score)==0U);

    assert(simulation_class21_candidate_within_projectile_lock(0,0,0x1800,0,0x800));
    assert(!simulation_class21_candidate_within_projectile_lock(0,0,0x1801,0,0x800));

    assert(simulation_class21_apply_candidate_line_of_fire_result(100,90,true,false)==100U);
    assert(simulation_class21_apply_candidate_line_of_fire_result(100,90,false,true)==25U);
    assert(simulation_class21_apply_candidate_line_of_fire_result(100,90,false,false)==0U);
    // Retail skips the line-of-fire branch entirely when the raw score cannot beat current best.
    assert(simulation_class21_apply_candidate_line_of_fire_result(90,90,false,false)==90U);

    assert(simulation_class21_mapped_terrain_sound_gate(2,1,true,0x10,0x11));
    assert(!simulation_class21_mapped_terrain_sound_gate(1,1,true,0x10,0x11));
    assert(!simulation_class21_mapped_terrain_sound_gate(2,1,false,0x10,0x11));
    assert(!simulation_class21_mapped_terrain_sound_gate(2,1,true,0x08,0x11));
    assert(!simulation_class21_mapped_terrain_sound_gate(2,1,true,0x10,0x10));

    std::cout << "V546_CLASS21_STATE5 PASS\n";
    return 0;
}
