#include "thandor/simulation_lab_follow.hpp"
#include "thandor/military_anchor_v599.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using namespace thandor;

static MovementPoint point(Vec3f p){return {float_to_q12(p.x),float_to_q12(p.y)};}
static bool same(MovementPoint a, MovementPoint b){return a.x==b.x&&a.y==b.y;}
static bool same(std::optional<MovementPoint> a, MovementPoint b){return a&&same(*a,b);}
static bool same(std::optional<MovementPoint> a, std::optional<MovementPoint> b){return a.has_value()==b.has_value()&&(!a||same(*a,*b));}
int main(){
    SimulationLabEntity e;
    e.actor.position={17.25F,22.5F,3.F};
    e.actor.move_target=Vec3f{33.F,41.F,3.F};
    e.actor.move_waypoint=Vec3f{25.F,25.F,3.F};
    e.combat_movement_initialized=true;
    e.combat_follow_owned=true;
    e.combat_movement.flags=0x239U|0x400U;
    e.combat_movement.fallback={float_to_q12(33),float_to_q12(41)};
    e.combat_movement.primary=e.combat_movement.fallback;
    e.combat_movement.stored=e.combat_movement.fallback;
    e.ai_path_coordinate_q12=MovementPoint{1,2};
    e.ai_damage_tracked_coordinate_q12=MovementPoint{3,4};
    e.ai_common_tracked_coordinate_q12=MovementPoint{5,6};
    e.target_entity_index=19U;
    e.combat_command_mode_flags=8U;
    e.navigation_target_cell_valid=e.navigation_direct_route_valid=true;
    e.navigation_recheck_tick=123;
    const auto before=e.combat_movement.fallback;
    assert(simulation_lab_explicit_stop_movement(e));
    assert(same(e.ai_path_coordinate_q12,point(e.actor.position)));
    assert(same(e.ai_damage_tracked_coordinate_q12,point(e.actor.position)));
    assert(same(e.ai_common_tracked_coordinate_q12,point(e.actor.position)));
    assert((e.combat_movement.flags&0x239U)==0U);
    assert((e.combat_movement.flags&0x400U)!=0U);
    assert(same(e.combat_movement.fallback,point(e.actor.position))); // aliases native common.pathCoordinate
    assert(!e.actor.move_target && !e.actor.move_waypoint && !e.combat_follow_owned);
    assert(!e.navigation_target_cell_valid && !e.navigation_direct_route_valid && e.navigation_recheck_tick==0U);
    assert(e.target_entity_index==19U && e.combat_command_mode_flags==8U); // only movement is stopped
    assert(e.combat_movement_initialized);
    // Explicit stop is idempotent at a stationary model, but reanchors if
    // a later selection relocation changed the model's actual position.
    assert(simulation_lab_explicit_stop_movement(e));
    e.actor.position.x=20.25F;
    assert(simulation_lab_explicit_stop_movement(e));
    assert(same(e.ai_path_coordinate_q12,point(e.actor.position)));
    assert(same(e.ai_damage_tracked_coordinate_q12,point(e.actor.position)));
    assert(same(e.ai_common_tracked_coordinate_q12,point(e.actor.position)));
    // Native selection stop ignores locked entities entirely.
    e.combat_movement.flags|=0x2U|0x1U|0x200U;
    e.actor.move_target=Vec3f{100.F,100.F,0.F};
    const auto prior_path=e.ai_path_coordinate_q12;
    const auto prior_common=e.ai_common_tracked_coordinate_q12;
    e.actor.position.x=21.25F;
    assert(!simulation_lab_explicit_stop_movement(e));
    assert((e.combat_movement.flags&0x203U)==0x203U);
    assert(e.actor.move_target && same(e.ai_path_coordinate_q12,prior_path));
    assert(same(e.ai_common_tracked_coordinate_q12,prior_common));
    // A released model cannot be targeted by a selection stop.
    e.runtime_removed=true;e.combat_movement.flags&=~0x2U;
    assert(!simulation_lab_explicit_stop_movement(e));
    // Scoring must still use signed x86 low-32 wrap, not a saturation clamp.
    MilitaryScoreParametersV599 params{};
    params.site_bias=1000000;params.site_scale=4096;
    const MilitarySiteV599 site{0,0,0};
    assert(military_site_score_v599(0,0,site,params,1)<0);
    assert(military_choose_anchor_action_v599(0,0,params,1,0,0,
        std::span<const MilitarySiteV599>(&site,1),{},{},{},false).kind==0U);
    params.site_scale=1024;
    assert(military_site_score_v599(0,0,site,params,1)>0);
    assert(military_choose_anchor_action_v599(0,0,params,1,0,0,
        std::span<const MilitarySiteV599>(&site,1),{},{},{},false).kind==1U);
    std::cout<<"V730_EXPLICIT_STOP PASS selection-lock, movement-only flags, path/tracked anchors, signed site-score wrap\n";
}
