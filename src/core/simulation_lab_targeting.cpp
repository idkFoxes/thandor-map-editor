#include "thandor/simulation_lab_visibility.hpp"
#include "thandor/simulation_lab_targeting.hpp"
#include "thandor/simulation_lab_follow.hpp"
#include "thandor/simulation_lab_owner_order.hpp"
#include "thandor/fog_preview.hpp"
#include "thandor/retail_contact.hpp"
#include <algorithm>
#include <bit>
#include <limits>
namespace thandor {
namespace {
uint32_t root_flags(const SimulationLabEntity& entity) noexcept {
    return entity.runtime_flags|(entity.root_model_component_index<entity.model_components.size()
        ?entity.model_components[entity.root_model_component_index].runtime_flags_ec:0U);
}
int32_t add_signed_dword(int32_t a,int32_t b) noexcept {
    return std::bit_cast<int32_t>(static_cast<uint32_t>(a)+static_cast<uint32_t>(b));
}
}
SimulationLabCombatScore simulation_lab_combat_score(const SimulationLabCombatScoreInput& in) noexcept {
    SimulationLabCombatScore result;
    if(in.source_range_q12<=0||in.target_runtime_class>=kMilitaryGroupClassScoresV599.size()||
       in.source_counter_for_target==0||in.source_class_count==0)return result;
    if(in.source_class_count<0&&static_cast<uint32_t>(in.target_condition_q12)>=4096U)return result;
    const int64_t reach=static_cast<int64_t>(in.source_range_q12)+8192;
    const int64_t dx=in.delta_x_q12,dy=in.delta_y_q12;
    int64_t remaining=reach*reach-dx*dx;if(remaining<0)return result;
    remaining-=dy*dy;if(remaining<0)return result;
    int64_t target_counter=in.target_counter_for_source,source_counter=in.source_counter_for_target;
    result.command_generation_shift=target_counter==0?2U:0U;
    if(source_counter<0){
        target_counter=0;
        source_counter=std::bit_cast<int32_t>(0U-static_cast<uint32_t>(in.source_counter_for_target));
    }
    // Each contribution and the sum are dwords in the executable. Division is
    // signed and truncates toward zero; the final zero-counter quarter is logical.
    uint32_t score=static_cast<uint32_t>(static_cast<int64_t>(retail_uint64_sqrt(static_cast<uint64_t>(remaining)))*1536/in.source_range_q12);
    score+=kMilitaryGroupClassScoresV599[in.target_runtime_class]*2048U;
    score+=static_cast<uint32_t>(131072*target_counter/4096);
    score+=static_cast<uint32_t>(73728*source_counter/4096);
    const int32_t deficit=std::bit_cast<int32_t>(4096U-static_cast<uint32_t>(in.target_condition_q12));
    score+=static_cast<uint32_t>(4352LL*deficit/4096);
    if(target_counter==0)score>>=2U;
    result.value=std::bit_cast<int32_t>(score);return result;
}
SimulationLabCombatMetrics simulation_lab_combat_metrics(const SimulationLabEntity& entity) noexcept {
    SimulationLabCombatMetrics result;
    if(!simulation_lab_has_live_root_model_owner(entity))return result;
    result.condition_q12=simulation_lab_hierarchy_condition_ratio_q12(entity);
    for(const auto& weapon:entity.combat_weapons){
        if(weapon.profile.reload_ticks==0||
           !simulation_lab_model_node_present(entity,weapon.profile.yaw_model_node_index)||
           !simulation_lab_model_node_present(entity,weapon.profile.model_node_index))continue;
        // ModelRuntimeHierarchy_AccumulateDerivedSelectionMetrics sums every
        // linked weapon definition, including switched-off/terminal callbacks.
        // Firing eligibility is checked separately by the weapon owner callback.
        result.range_q12=std::max(result.range_q12,shot_selection_range_q12(weapon.shot));
        for(size_t i=0;i<8;++i)result.class_damage_q12[i]=add_signed_dword(result.class_damage_q12[i],weapon.shot.target_impact_damage_q12[i]);
    }
    for(size_t i=8;i>0;--i)result.class_sum=add_signed_dword(result.class_sum,result.class_damage_q12[i-1]);
    return result;
}
SimulationLabCombatTargetChoice simulation_lab_select_combat_target(
    const SimulationLabRun& run,const FieldGrid& field,size_t source_index,
    std::span<const SimulationLabCombatMetrics> metrics,bool command_search,
    std::span<const std::uint8_t> line_of_fire_blocked,
    const std::function<bool(size_t)>& is_blocked){
    SimulationLabCombatTargetChoice result;
    if(source_index>=run.entities.size())return result;
    std::vector<SimulationLabCombatMetrics> owned;
    if(metrics.size()!=run.entities.size()){
        owned.reserve(run.entities.size());for(const auto& e:run.entities)owned.push_back(simulation_lab_combat_metrics(e));metrics=owned;
    }
    const auto& source=run.entities[source_index];const auto& source_metrics=metrics[source_index];
    // Native primary owner maintenance invokes combat assignment for every
    // non-neutral live MODEL owner after its class callback. Construction,
    // dismantling and terminal-effect state do not remove the owner; only
    // unlinking the root does. Weapon callbacks retain their own health/state
    // gates, so do not duplicate them in the command selector.
    if(!simulation_lab_has_live_root_model_owner(source)||source.faction_index==0||source.faction_index>=8||
       source.shot_impact_class_index>=8||source_metrics.class_sum==0||source_metrics.range_q12<=0)return result;
    const int32_t sx=float_to_q12(source.actor.position.x),sy=float_to_q12(source.actor.position.y);
    const int32_t search=static_cast<int32_t>(std::min<int64_t>(static_cast<int64_t>(source_metrics.range_q12)+16384,std::numeric_limits<int32_t>::max()));
    const auto mask_x=simulation_depth_bin_mask(search,sx),mask_y=simulation_depth_bin_mask(search,sy);
    // World MODEL owners are linked at the list head. The Lab's append-only
    // entity vector therefore has the inverse order: newest index first. Native
    // target scoring uses a strict greater-than comparison, so this traversal
    // order also decides exact-score ties.
    for(size_t visit=0;visit<run.entities.size();++visit){
        const size_t i=run.entities.size()-1U-visit;
        if(i==source_index)continue;
        const auto& candidate=run.entities[i];
        // The batched production pass can publish a child before an earlier
        // combat callback. Native links the child when its factory is visited.
        if(command_search&&run.level_runtime_enabled&&candidate.synthetic&&
           candidate.construction_start_tick==run.tick&&candidate.production_parent_entity_index){
            const auto parent=*candidate.production_parent_entity_index;
            if(parent<run.entities.size()&&
               simulation_lab_owner_order(run.entities[parent],parent)<simulation_lab_owner_order(source,source_index))
                continue;
        }
        // AiCombatTarget_SelectBestCandidate rejects destroyed MODEL owners,
        // not live unfinished construction and not the dismantling bit alone.
        // Producer-linked products retain the explicit V749 lifecycle gate.
        if(!simulation_lab_combat_target_owner_available(candidate)||(root_flags(candidate)&0x08U)!=0U||candidate.faction_index==0||candidate.faction_index>=8||candidate.shot_impact_class_index>=8)continue;
        // Open Thandor AiCombatTarget_SelectBestCandidate: a positive aggregate
        // searches hostile owners; a negative aggregate searches damaged allied
        // owners of the SAME faction, excluding runtime no-regeneration flag 0x400.
        const bool repair=source_metrics.class_sum<0;
        if(repair) {
            if(candidate.faction_index!=source.faction_index || (root_flags(candidate)&0x400U)!=0U ||
               static_cast<uint32_t>(metrics[i].condition_q12)>=4096U)continue;
        }else {
            if(candidate.faction_index==source.faction_index)continue;
        }
        // GameFactionRuntime_TestCapabilityBitClear reads the source faction's
        // capability bit directly. Relation nibbles can be transiently or
        // directionally different, so they are not the combat admission gate.
        const bool capability_bit_clear=
            (run.faction_capability_flags[source.faction_index]&
             (1U<<(candidate.faction_index&7U)))==0U;
        if(repair ? capability_bit_clear : !capability_bit_clear)continue;
        if((simulation_lab_model_visibility_bits(run,field,source.faction_index,candidate)&2U)==0U)continue;
        // Target assignment runs after the source's model callback, while older
        // ground owners still have their positions from the previous tick.
        const auto position=command_search&&run.level_runtime_enabled&&i<source_index&&
            candidate.ground_aim_before_movement
            ?candidate.ground_aim_before_movement->position:candidate.actor.position;
        const int32_t cx=float_to_q12(position.x),cy=float_to_q12(position.y);
        const int32_t radius=candidate.motion.depth_bin_broad_phase_radius_q12?candidate.motion.depth_bin_broad_phase_radius_q12:candidate.motion.collision_radius_q12;
        if(!(mask_x&simulation_depth_bin_mask(radius,cx))||!(mask_y&simulation_depth_bin_mask(radius,cy)))continue;
        // Native command scoring admits range+2 for pursuit. Local firing still
        // requires an actual active weapon in range; a pursuit candidate must not
        // hide a shootable enemy or fabricate a long-range shot.
        const bool shootable=std::any_of(source.combat_weapons.begin(),source.combat_weapons.end(),[&](const auto& weapon){
            return weapon.profile.valid()&&
                (repair ? weapon.shot.target_impact_damage_q12[candidate.shot_impact_class_index]<0
                        : (!weapon.shot.is_repair_or_recovery_shot()&&
                           weapon.shot.target_impact_damage_q12[candidate.shot_impact_class_index]>0))&&
                simulation_lab_weapon_callback_active(source,weapon.profile)&&
                simulation_lab_model_node_present(source,weapon.profile.model_node_index)&&
                simulation_target_within_range(source.actor.position,candidate.actor.position,weapon.profile);
        });
        if(!command_search&&!shootable)continue;
        auto score=simulation_lab_combat_score({source_metrics.range_q12,std::bit_cast<int32_t>(static_cast<uint32_t>(sx)-static_cast<uint32_t>(cx)),std::bit_cast<int32_t>(static_cast<uint32_t>(sy)-static_cast<uint32_t>(cy)),candidate.runtime_model_class_id,
            source_metrics.class_damage_q12[candidate.shot_impact_class_index],metrics[i].class_damage_q12[source.shot_impact_class_index],metrics[i].condition_q12,source_metrics.class_sum});
        // Native performs its obstruction test only when the unadjusted score
        // beats the current winner. A blocked target is quartered; an immobile
        // source cannot pursue it and rejects it entirely.
        if(score.value>result.score.value &&
           (is_blocked?is_blocked(i):i<line_of_fire_blocked.size()&&line_of_fire_blocked[i]!=0U)) {
            const bool accelerates=source.runtime_model_hierarchy && !source.runtime_model_hierarchy->empty()
                ? source.motion.ground_acceleration_q12!=0U : source.motion.movable();
            if(!accelerates)score.value=0;
            else score.value=std::bit_cast<std::int32_t>(
                static_cast<std::uint32_t>(score.value)>>2U);
        }
        if(score.value>result.score.value){result.entity_index=i;result.score=score;}
    }
    return result;
}
bool simulation_lab_weapon_callback_active(const SimulationLabEntity& entity,const SimulationWeaponProfile& profile) noexcept {
    if(!simulation_lab_has_live_root_model_owner(entity)||entity.root_model_terminal)return false;
    for(size_t i=0;i<entity.model_components.size();++i){const auto& c=entity.model_components[i];
        if(c.source_model_node_index!=profile.yaw_model_node_index)continue;
        const uint32_t flags=c.runtime_flags_ec|(i==entity.root_model_component_index?entity.runtime_flags:0U);
        return !c.runtime_removed&&!c.terminal_runtime_flag8&&(flags&9U)==0;
    }
    // Seeds without a concrete runtime hierarchy use a synthesized root component;
    // preserve their implicit root-weapon callback. A concrete hierarchy must
    // never resurrect an absent or destroyed attachment through that fallback.
    return (!entity.runtime_model_hierarchy || entity.runtime_model_hierarchy->empty()) &&
           (entity.runtime_flags&9U)==0;
}
bool simulation_lab_combat_command_class(const SimulationLabEntity& entity) noexcept {
    const auto cls=entity.runtime_model_class_id;
    return cls==1U||cls==2U||cls==3U||cls==17U||cls==19U||
        (cls==4U&&!entity.combat_weapons.empty());
}
void simulation_lab_set_position_command(SimulationLabEntity& entity) noexcept {
    const auto desired=entity.actor.move_target;simulation_lab_stop_follow(entity);
    entity.actor.move_target=desired;entity.combat_follow_owned=false;entity.combat_movement_initialized=false;
    entity.combat_command_mode_flags=2U;entity.combat_command_generation=1024;
    entity.target_entity_index.reset();
}
void simulation_lab_set_group_target_command(SimulationLabEntity& entity,size_t target) noexcept {
    const auto desired=entity.actor.move_target;simulation_lab_stop_follow(entity);
    entity.actor.move_target=desired;entity.combat_follow_owned=false;entity.combat_movement_initialized=false;
    entity.combat_assigned_target_entity_index=target;
    entity.target_entity_index=target;entity.combat_command_target_position_world.reset();
    entity.combat_command_mode_flags=5U;
    entity.combat_command_generation=32;
}
bool simulation_lab_apply_selection_army_target(
        SimulationLabEntity& entity,size_t target) noexcept {
    // ArmyRuntime_TestHasNoWeaponDamage is the zero test of the first summed
    // target-class damage channel (ArmyRuntimeSlot::stateOrTechnologyId).
    if(simulation_lab_combat_metrics(entity).class_damage_q12[0]==0)return false;
    simulation_lab_stop_follow(entity);
    entity.combat_follow_owned=false;
    entity.target_entity_index=target;
    entity.combat_command_target_position_world.reset();
    entity.combat_assigned_target_entity_index=target;
    entity.combat_command_mode_flags=0x15U; // TARGET_ARMY | INTERRUPTED | SELECTION_ORDER
    entity.combat_command_generation=1024;
    entity.combat_movement.flags&=~0x200U; // caller clears ARMY_MOVEMENT_ROUTED
    entity.combat_movement.command_mode=entity.combat_command_mode_flags;
    return true;
}
bool simulation_lab_apply_selection_target_position(
        SimulationLabEntity& entity,Vec3f target) noexcept {
    if(simulation_lab_combat_metrics(entity).class_damage_q12[0]==0)return false;
    simulation_lab_stop_follow(entity);
    entity.combat_follow_owned=false;
    entity.target_entity_index.reset();
    entity.combat_command_target_position_world=target;
    // ArmyRuntime_ApplyTargetPositionCommand stamps 1024; the selection writer
    // then shifts the generation left by two.
    entity.combat_command_mode_flags=0x16U; // TARGET_POSITION | INTERRUPTED | SELECTION_ORDER
    entity.combat_command_generation=4096;
    entity.combat_movement.flags&=~0x220U; // TARGET_FOLLOWING | ROUTED
    entity.combat_movement.command_mode=entity.combat_command_mode_flags;
    return true;
}
void simulation_lab_service_combat_command(SimulationLabRun& run,const FieldGrid& field,
        size_t source_index,std::span<const SimulationLabCombatMetrics> metrics,uint32_t step_ticks,
        std::span<const std::uint8_t> line_of_fire_blocked,
        const std::function<bool(size_t)>& is_blocked) {
    if(source_index>=run.entities.size()||metrics.size()!=run.entities.size())return;
    auto& source=run.entities[source_index];
    if(!simulation_lab_has_live_root_model_owner(source)||
       (source.runtime_model_class_id==4U&&!source.construction_complete))return;
    if(source.faction_index==0U){
        source.combat_command_generation=std::bit_cast<int32_t>(
            static_cast<uint32_t>(source.combat_command_generation)-step_ticks);
        if(source.combat_command_generation<0)source.combat_command_mode_flags&=~12U;
        return;
    }
    // Compatibility for callers importing an explicit strategic position order.
    if(source.combat_command_mode_flags==0U&&source.combat_command_generation==0&&
       !source.combat_movement_initialized&&!source.target_entity_index&&source.actor.move_target&&
       (source.military_ai_command_kind==1U||source.military_ai_command_kind==2U))
        simulation_lab_set_position_command(source);
    const auto alive=[&](std::optional<size_t> target){return target&&*target<run.entities.size()&&
        simulation_lab_command_target_owner_available(run.entities[*target]);};
    if(!alive(source.combat_assigned_target_entity_index))source.combat_assigned_target_entity_index.reset();
    if(source.target_entity_index&&!alive(source.target_entity_index)) {
        source.target_entity_index.reset();source.combat_command_mode_flags=0;source.combat_command_generation=0;
    }
    if(simulation_lab_command_target_owner_available(source)&&
       !(source.production_linked_to_parent&&source.synthetic&&source.construction_start_tick>=run.tick)&&
       !(source.combat_command_mode_flags&4U)&&
       (source.combat_command_generation<1||!(source.combat_command_mode_flags&3U))) {
        const auto selected=simulation_lab_select_combat_target(
            run,field,source_index,metrics,true,line_of_fire_blocked,is_blocked);
        if(selected.entity_index==source.target_entity_index) {
            source.combat_command_generation=512;
            if(selected.entity_index)++source.combat_command_retains;
        } else if(!selected.entity_index && metrics[source_index].class_sum>=0) {
            // A zero aggregate follows the original positive stack-leftover
            // branch; positive damage falls back to the stored group target.
            simulation_lab_stop_follow(source);
            source.target_entity_index=source.combat_assigned_target_entity_index;
            source.combat_command_target_position_world.reset();
            source.combat_command_mode_flags=source.target_entity_index?1U:4U;
            source.combat_command_generation=source.target_entity_index?1024:0;
            ++source.combat_command_fallbacks;
        } else if(!selected.entity_index) {
            // Negative aggregates are repair searches. With no damaged friendly
            // winner retail clears the active target and stamps an AI command;
            // it does not reuse a hostile group-assigned target.
            simulation_lab_stop_follow(source);
            source.target_entity_index.reset();
            source.combat_command_target_position_world.reset();
            source.combat_command_mode_flags=8U;
            source.combat_command_generation=32;
        } else {
            simulation_lab_stop_follow(source);
            source.target_entity_index=selected.entity_index;
            source.combat_command_target_position_world.reset();
            source.combat_command_mode_flags=9U;
            source.combat_command_generation=static_cast<int32_t>(32U>>selected.score.command_generation_shift);
            source.military_ai_source_dispatch=true;source.military_ai_command_kind=3U;
            // ResolveCommandTarget assigns the enemy, not a direct move. Native
            // movement starts in the primary-weapon obstruction/follow callback.
            // Keep the existing route until that callback is represented fully.
            ++source.combat_command_acquisitions;++run.metrics.target_assignments;
        }
    }
    source.combat_command_generation=std::bit_cast<int32_t>(static_cast<uint32_t>(source.combat_command_generation)-step_ticks);
    if(source.combat_command_generation<0)source.combat_command_mode_flags&=~12U;
}
void simulation_lab_clear_released_target(SimulationLabRun& run,size_t released_index) noexcept {
    for(auto& owner:run.entities){
        // A docking peer can retain the released model without targeting it.
        if(owner.linked_entity_index==released_index)owner.linked_entity_index.reset();
        if(owner.production_linked_child_entity_index==released_index)
            owner.production_linked_child_entity_index.reset();
        if(owner.combat_assigned_target_entity_index==released_index)owner.combat_assigned_target_entity_index.reset();
        if(!owner.target_entity_index||*owner.target_entity_index!=released_index)continue;
        if(run.level_runtime_enabled){
            // Native clears the command reference and leaves the stored segment
            // intact. A stationary unit can still use it for lead while recoiling.
            owner.target_entity_index.reset();
            // Reference teardown clears only the target/interrupt/AI bits.
            // Generation belongs to the owner's later command callback: it may
            // select a new visible target immediately after this release.
            owner.combat_command_mode_flags&=~13U;
            owner.combat_movement.command_mode=owner.combat_command_mode_flags;
            // Releasing a command reference does not clear aiUnitFlags.
            // GROUP_ASSIGNED survives until a later explicit site/anchor order.
            owner.actor.target_in_range=owner.actor.target_aligned=false;
            continue;
        }
        simulation_lab_stop_follow(owner);owner.combat_follow_owned=false;owner.combat_movement={};owner.combat_movement_initialized=false;
        owner.target_entity_index.reset();owner.combat_command_mode_flags=0;owner.combat_command_generation=0;owner.military_ai_group_assigned=false;
        owner.military_ai_command_kind=0;owner.military_ai_busy_cooldown=0;
        owner.actor.move_target.reset();owner.actor.move_waypoint.reset();
        owner.navigation_target_cell_valid=owner.navigation_direct_route_valid=false;
        owner.actor.target_in_range=owner.actor.target_aligned=false;
    }
}
} // namespace thandor
