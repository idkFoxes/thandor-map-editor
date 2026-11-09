#include "thandor/military_anchor_v599.hpp"
#include "thandor/simulation_lab_follow.hpp"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <iostream>
using namespace thandor;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main()try{
    constexpr std::array<std::uint32_t,5> unseen{{17,11,17,18,18}};
    // ws03[1] is the first valid 16-byte aliased candidate, ws03[3] second.
    check(military_bounded_unseen_group_target_v705(unseen)==3,"16-byte native stride not respected");
    constexpr std::array<std::uint32_t,6> tie{{1,17,4,17,19,17}};
    check(military_bounded_unseen_group_target_v705(tie)==5,"later candidate must win equal-score tie");
    constexpr std::array<std::uint32_t,1> single{{18}};
    check(!military_bounded_unseen_group_target_v705(single),"one 8-byte ws03 record must not fabricate a pointer");
    constexpr std::array<std::uint32_t,4> invalid{{18,100,18,5}};
    check(!military_bounded_unseen_group_target_v705(invalid),"out-of-range or zero-score class admitted");
    {
        const auto field=FieldGrid::create(32,32,0,0);
        SimulationLabRun run;run.entities.resize(2);
        auto& source=run.entities[0];auto& target=run.entities[1];
        source.faction_index=1;target.faction_index=2;
        target.actor.position={5.0F,-5.0F,0.0F};
        source.target_entity_index=1;source.military_ai_group_assigned=true;
        source.combat_command_mode_flags=5U;
        source.combat_assigned_target_entity_index=1;
        source.actor.move_target=target.actor.position;
        run.current_visibility_occupancy.assign(field.cells().size(),0);
        check(!simulation_lab_visible_command_target(run,field,0),"hidden group target incorrectly became a firing aimpoint");
        check(!source.target_entity_index&&source.combat_command_mode_flags==0U&&
            source.combat_assigned_target_entity_index==1&&source.actor.move_target&&source.military_ai_group_assigned,
            "hidden group target incorrectly cancelled a retained movement order");
        source.military_ai_group_assigned=false;source.target_entity_index=1;
        check(!simulation_lab_visible_command_target(run,field,0)&&!source.target_entity_index,
            "ordinary invisible local target unexpectedly retained");
    }
    std::cout<<"V705 bounded unseen group fallback PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
