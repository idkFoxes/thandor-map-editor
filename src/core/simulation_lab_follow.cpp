#include "thandor/simulation_lab_dismantle.hpp"
#include "thandor/simulation_lab_visibility.hpp"
#include "thandor/simulation_lab_geometry.hpp"
#include "thandor/simulation_lab_follow.hpp"
#include "thandor/retail_placement.hpp"
#include "thandor/army_target_follow.hpp"
#include "thandor/fog_preview.hpp"
#include "thandor/simulation_lab_targeting.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
namespace thandor {
namespace {
MovementPoint point(Vec3f p){return {float_to_q12(p.x),float_to_q12(p.y)};}

bool uses_high_grid_influence(uint32_t runtime_class) noexcept {
    return runtime_class<=3U||(runtime_class>=17U&&runtime_class<=19U)||runtime_class==23U;
}

std::uint32_t represented_movement_flags(const SimulationLabEntity& entity) noexcept {
    // Pioneer and combat adapters represent the same native movement record.
    // Once initialized, the Pioneer adapter owns it; stale combat flags must
    // not reintroduce the factory's old LOCKED bit into group priorities.
    std::uint32_t flags=entity.pioneer_movement_initialized&&!entity.production_linked_to_parent
        ?entity.pioneer_movement.flags:entity.combat_movement.flags;
    if(entity.actor.move_target)flags|=1U; // ARMY_MOVEMENT_ACTIVE
    if(entity.production_linked_to_parent)flags|=2U; // ARMY_MOVEMENT_LOCKED
    return flags;
}

bool native_route_owner_accelerates(const SimulationLabEntity& entity) noexcept {
    // EntityPathing_RebuildOverlappingGroupRoutes admits a MODEL owner when
    // the selected definition's accelerationPerTick is non-zero. Authored
    // Lab entities carry that MDL +0x18 field in ground_acceleration_q12;
    // speed is only the compatibility proxy for older synthetic fixtures
    // that do not provide a resolved runtime hierarchy.
    const bool authored=entity.runtime_model_hierarchy&&
        !entity.runtime_model_hierarchy->empty();
    return authored ? entity.motion.ground_acceleration_q12!=0U
                    : entity.motion.movable();
}

std::int32_t route_priority(const SimulationLabEntity& candidate,
                            std::uint32_t route_faction) noexcept {
    const auto flags=represented_movement_flags(candidate);
    if((flags&2U)!=0U)return 0; // locked models retain the native zero priority
    std::int32_t priority=1; // another faction
    if(candidate.faction_index==route_faction){
        ++priority;
        if((flags&1U)==0U)priority+=candidate.motion.speed_q12_per_tick;
    }
    return priority;
}

void priority_pair_sift_up(SimulationLabRoutePriorityImage& image,std::size_t heap_size) noexcept {
    if(heap_size<2U)return;
    std::size_t current=heap_size-1U;
    while(current!=0U){
        const std::size_t parent=(current-1U)>>1U;
        if(image.pairs[current].priority<=image.pairs[parent].priority)return;
        std::swap(image.pairs[current],image.pairs[parent]);
        current=parent;
    }
}

void priority_pair_sift_down(SimulationLabRoutePriorityImage& image,std::size_t heap_size) noexcept {
    if(heap_size<2U)return;
    std::size_t current=0U;
    for(std::size_t left=1U;left<heap_size;left=current*2U+1U){
        std::size_t selected=left;
        if(left+1U<heap_size&&
           image.pairs[left].priority<image.pairs[left+1U].priority)
            selected=left+1U;
        if(image.pairs[selected].priority<=image.pairs[current].priority)return;
        std::swap(image.pairs[current],image.pairs[selected]);
        current=selected;
    }
}

SimulationLabRoutePriorityImage build_overlapping_route_priority_image(
        const SimulationLabRun& run,std::size_t source_index,MovementPoint target,
        bool within_owner_callback) noexcept {
    SimulationLabRoutePriorityImage image;
    if(source_index>=run.entities.size())return image;
    const auto& source=run.entities[source_index];
    if(!simulation_lab_has_live_root_model_owner(source))return image;
    const auto source_point=point(source.actor.position);
    // Native expands the source-to-segment search by the MDL footprint.
    // Candidate depth masks still include their complete model bounds below.
    const auto source_radius=source.motion.collision_radius_q12;
    const std::int64_t dx=std::llabs(static_cast<std::int64_t>(source_point.x)-target.x);
    const std::int64_t dy=std::llabs(static_cast<std::int64_t>(source_point.y)-target.y);
    const std::int64_t wide_radius=std::max(dx,dy)+
        std::llabs(static_cast<std::int64_t>(source_radius));
    const auto radius=static_cast<std::int32_t>(std::min<std::int64_t>(
        wide_radius,std::numeric_limits<std::int32_t>::max()));
    const auto center_x=static_cast<std::int32_t>(
        (static_cast<std::int64_t>(source_point.x)+target.x)>>1);
    const auto center_y=static_cast<std::int32_t>(
        (static_cast<std::int64_t>(source_point.y)+target.y)>>1);
    const auto route_mask_x=simulation_depth_bin_mask(radius,center_x);
    const auto route_mask_y=simulation_depth_bin_mask(radius,center_y);

    for(std::size_t cursor=run.entities.size();cursor!=0U;--cursor){
        const std::size_t index=cursor-1U; // native owner list is newest-first
        const auto& candidate=run.entities[index];
        if(!simulation_lab_has_live_root_model_owner(candidate)||
           !native_route_owner_accelerates(candidate))continue;
        // Production is staged before the movement pass, but a newborn
        // child is absent from native route queries made before its parent
        // callback. Later owners and world-job queries can already see it.
        if(within_owner_callback&&run.level_runtime_enabled&&candidate.synthetic&&
           candidate.construction_start_tick==run.tick&&candidate.production_parent_entity_index&&
           *candidate.production_parent_entity_index<run.entities.size()){
            const auto parent=*candidate.production_parent_entity_index;
            if(simulation_lab_owner_order(run.entities[parent],parent)<
               simulation_lab_owner_order(source,source_index))continue;
        }
        const auto candidate_radius=candidate.motion.depth_bin_broad_phase_radius_q12!=0
            ? candidate.motion.depth_bin_broad_phase_radius_q12
            : candidate.motion.collision_radius_q12;
        const auto candidate_point=point(candidate.actor.position);
        if((route_mask_x&simulation_depth_bin_mask(candidate_radius,candidate_point.x))==0U||
           (route_mask_y&simulation_depth_bin_mask(candidate_radius,candidate_point.y))==0U)
            continue;
        image.pairs[image.count++]={index,route_priority(candidate,source.faction_index)};
        if(image.count==kSimulationLabRoutePriorityPairCapacity){
            image.capacity_reached=true;
            break;
        }
    }
    if(image.count<2U)return image;
    for(std::size_t heap_size=1U;heap_size<=image.count;++heap_size)
        priority_pair_sift_up(image,heap_size);
    for(std::size_t heap_size=image.count;heap_size>1U;--heap_size){
        std::swap(image.pairs[0],image.pairs[heap_size-1U]);
        priority_pair_sift_down(image,heap_size-1U);
    }
    return image;
}

struct OwnedRouteBlocker {
    std::size_t owner_index{SIZE_MAX};
    V528PathRuntimeBlocker blocker{};
};

bool route_link_pair(const SimulationLabEntity& source,std::size_t source_index,
                     const SimulationLabEntity& other,std::size_t other_index) noexcept {
    return (source.linked_entity_index&&*source.linked_entity_index==other_index)||
           (other.linked_entity_index&&*other.linked_entity_index==source_index);
}

std::vector<OwnedRouteBlocker> group_route_owner_blockers(
        const SimulationLabRun& run,std::size_t source_index,
        std::optional<std::size_t> ignored_index) {
    std::vector<OwnedRouteBlocker> result;
    if(source_index>=run.entities.size())return result;
    const auto& source=run.entities[source_index];
    result.reserve(run.entities.size());
    for(std::size_t index=0;index<run.entities.size();++index){
        const auto& other=run.entities[index];
        if(index==source_index||ignored_index==index||
           !simulation_lab_has_live_root_model_owner(other)||
           source.target_entity_index==index||
           other.motion.collision_radius_q12<=0||
           route_link_pair(source,source_index,other,index))continue;
        const auto position=point(other.actor.position);
        result.push_back({index,{position.x,position.y,other.motion.collision_radius_q12,simulation_lab_route_blocker_class(other)}});
    }
    return result;
}

std::vector<V528PathRuntimeBlocker> flatten_route_blockers(
        std::span<const V528PathRuntimeBlocker> statics,
        const std::vector<OwnedRouteBlocker>& owners) {
    std::vector<V528PathRuntimeBlocker> blockers;
    blockers.reserve(statics.size()+owners.size());
    blockers.insert(blockers.end(),statics.begin(),statics.end());
    for(const auto& owner:owners)blockers.push_back(owner.blocker);
    return blockers;
}

void remove_route_owner(std::vector<OwnedRouteBlocker>& blockers,std::size_t owner_index){
    blockers.erase(std::remove_if(blockers.begin(),blockers.end(),
        [owner_index](const OwnedRouteBlocker& blocker){return blocker.owner_index==owner_index;}),
        blockers.end());
}

MovementPoint represented_route_target(const SimulationLabEntity& entity) noexcept {
    // Native group routing reads movementWorld even after a temporary follow
    // clears ACTIVE. The retained segment can be reactivated by another owner.
    if(entity.pioneer_movement_initialized&&!entity.production_linked_to_parent)
        return entity.pioneer_movement.stored;
    if(entity.combat_movement_initialized)return entity.combat_movement.stored;
    if(entity.actor.move_waypoint)return point(*entity.actor.move_waypoint);
    if(entity.actor.move_target)return point(*entity.actor.move_target);
    return point(entity.actor.position);
}

void append_segment_influence(std::vector<OwnedRouteBlocker>& blockers,
        std::size_t owner_index,MovementPoint from,MovementPoint to,
        std::int32_t radius_q12) {
    struct Piece { MovementPoint from{},to{}; };
    std::array<Piece,64> pieces{};
    std::size_t pending=1U;
    pieces[0]={from,to};
    while(pending!=0U){
        const std::size_t top=pending-1U;
        const auto dx=std::llabs(static_cast<std::int64_t>(pieces[top].to.x)-pieces[top].from.x);
        const auto dy=std::llabs(static_cast<std::int64_t>(pieces[top].to.y)-pieces[top].from.y);
        if(pending>=pieces.size()||(dx<=0x240&&dy<=0x240)){
            blockers.push_back({owner_index,{pieces[top].to.x,pieces[top].to.y,radius_q12}});
            --pending;
            continue;
        }
        const MovementPoint midpoint{
            static_cast<std::int32_t>((static_cast<std::int64_t>(pieces[top].from.x)+pieces[top].to.x)>>1),
            static_cast<std::int32_t>((static_cast<std::int64_t>(pieces[top].from.y)+pieces[top].to.y)>>1)};
        pieces[pending]={midpoint,pieces[top].to};
        pieces[top].to=midpoint;
        ++pending;
    }
}

V528RouteResult coordinate_overlapping_group_route(
        SimulationLabRun& run,const FieldGrid& field,std::size_t source_index,
        MovementPoint requested_target,const V528RouteResult& base_route,
        std::span<const V528PathRuntimeBlocker> statics,V565PathTerrainCache& cache,
        std::optional<std::size_t> ignored_index,[[maybe_unused]] bool within_movement_callback,
        bool within_owner_callback) {
    const auto segment_target=base_route.primary_world_q12.value_or(base_route.resolved_world_q12);
    auto image=build_overlapping_route_priority_image(run,source_index,
        cache.runtime_states?MovementPoint{segment_target.x_q12,segment_target.y_q12}:requested_target,
        within_owner_callback);
    if(const auto trace=std::getenv("THANDOR_SIMLAB_ROUTE_TRACE_TICK");trace&&std::strtoul(trace,nullptr,10)==run.tick){
        std::fprintf(stderr,"GROUP_PAIRS tick=%u source=%zu count=%zu target=%d,%d\n",run.tick,run.entities[source_index].native_army_pool_slot,image.count,segment_target.x_q12,segment_target.y_q12);
        for(size_t order=0;order<image.count;++order){const auto pair=image.pairs[order];const auto& e=run.entities[pair.entity_index];const auto to=represented_route_target(e);
            std::fprintf(stderr,"PAIR order=%zu slot=%zu priority=%d pos=%d,%d target=%d,%d flags=%u radius=%d\n",order,e.native_army_pool_slot,pair.priority,float_to_q12(e.actor.position.x),float_to_q12(e.actor.position.y),to.x,to.y,represented_movement_flags(e),e.motion.collision_radius_q12);
        }
    }
    ++run.metrics.navigation_overlap_group_rebuilds;
    run.metrics.navigation_overlap_group_pairs+=static_cast<std::uint32_t>(image.count);
    if(image.capacity_reached)++run.metrics.navigation_overlap_group_capacity_reached;
    if(image.count<2U||!base_route.usable())return base_route;

    if(cache.runtime_states && run.retail_scratch){
        auto scratch=std::make_shared<SimulationLabRetailScratchImage>(*run.retail_scratch);
        V565PathTerrainCache group_cache;
        group_cache.field=&field;
        group_cache.runtime_states=std::shared_ptr<const std::vector<uint32_t>>(scratch,&scratch->state);
        const auto stamp=[&](const SimulationLabEntity& entity,MovementPoint p,
                             uint32_t runtime_class,bool add){
            retail_visit_grid_influence(scratch->width,scratch->state,p,
                entity.motion.collision_radius_q12,runtime_class,[&](size_t cell,uint32_t bits){
                    if(add)scratch->state[cell]|=bits;else scratch->state[cell]&=~bits;
                });
        };
        for(std::size_t order=0;order<image.count;++order){
            auto& candidate=run.entities[image.pairs[order].entity_index];
            const auto from=point(candidate.actor.position);
            stamp(candidate,from,candidate.runtime_model_class_id,true);
            // The scratch copy is temporary. The native high-band callback
            // still writes the owner's remembered stamp position.
            if(candidate.motion.collision_radius_q12>0&&uses_high_grid_influence(candidate.runtime_model_class_id))
                candidate.navigation_influence_position_q12=from;
        }
        auto result=base_route;
        const auto primary=base_route.primary_world_q12.value_or(base_route.resolved_world_q12);
        for(std::size_t order=0;order<image.count;++order){
            const auto pair=image.pairs[order];
            auto& candidate=run.entities[pair.entity_index];
            const auto from=point(candidate.actor.position);
            constexpr uint32_t low_band_influence_class=4U;
            if(pair.priority==1){
                stamp(candidate,from,low_band_influence_class,true);
                continue;
            }
            stamp(candidate,from,candidate.runtime_model_class_id,false);
            const bool source=pair.entity_index==source_index;
            auto to=source?MovementPoint{primary.x_q12,primary.y_q12}:represented_route_target(candidate);
            const uint32_t blocking=(0x100U<<candidate.traversal_low_band_index)|
                (0x1000000U<<candidate.traversal_high_band_index);
            const auto resolved=v565_resolve_group_route_segment(field,
                {from.x,from.y},{to.x,to.y},1U<<(candidate.faction_index&31U),blocking,group_cache);
            if(!source&&(to.x!=resolved.x_q12||to.y!=resolved.y_q12)&&
               (represented_movement_flags(candidate)&2U)==0U){
                // Both adapters represent the same native movement record.
                // An unlocked Pioneer reads its own state during movement and
                // later group queries, so publish the segment there directly.
                const bool pioneer=candidate.pioneer_movement_initialized;
                auto& state=pioneer?candidate.pioneer_movement:candidate.combat_movement;
                const bool was_active=(state.flags&1U)!=0U;
                state.stored={resolved.x_q12,resolved.y_q12};
                if(!was_active)state.fallback=state.stored;
                if(state.flags&0x400U)state.primary=state.stored;
                state.flags=(state.flags|1U)&~0x30U;
                state.retry=0x40U;
                state.last_checked=from;
                simulation_lab_sync_movement_coordinates(candidate,state);
                const bool peer_callback_pending=within_owner_callback&&
                    simulation_lab_owner_order(candidate,pair.entity_index)<
                    simulation_lab_owner_order(run.entities[source_index],source_index);
                if(peer_callback_pending&&candidate.ground_aim_before_movement){
                    // The native route is visible to intervening weapons
                    // immediately, while the peer's pose is still retained.
                    auto& aim=*candidate.ground_aim_before_movement;
                    aim.route_point=Vec3f{q12_to_float(state.stored.x),
                        q12_to_float(state.stored.y),aim.position.z};
                    aim.path_coordinate=candidate.ai_path_coordinate_q12;
                    aim.damage_tracked_coordinate=candidate.ai_damage_tracked_coordinate_q12;
                }
                if(!pioneer){
                    candidate.group_route_pending_point=state.stored;
                    candidate.group_route_pending_tick=run.tick+(peer_callback_pending?0U:1U);
                }
            }
            to={resolved.x_q12,resolved.y_q12};
            if(source){
                const auto cell=v519_path_clip_to_inner_scratch(field,
                    v519_path_world_to_scratch(to.x,to.y));
                if(base_route.primary_world_q12){
                    result.primary_world_q12=resolved;
                    result.primary_cell=cell;
                }else{
                    result.resolved_world_q12=resolved;
                    result.resolved_cell=cell;
                }
            }
            std::vector<OwnedRouteBlocker> segment;
            append_segment_influence(segment,pair.entity_index,from,to,candidate.motion.collision_radius_q12);
            for(const auto& piece:segment)
                stamp(candidate,{piece.blocker.world_x_q12,piece.blocker.world_y_q12},
                      low_band_influence_class,true);
        }
        return result;
    }

    std::size_t source_order=image.count;
    for(std::size_t order=0;order<image.count;++order)
        if(image.pairs[order].entity_index==source_index){source_order=order;break;}
    // Native leaves routeTarget at the caller's point when the fixed 32-pair
    // image fills before the source owner is admitted.
    if(source_order==image.count||source_order==0U)return base_route;

    auto owners=group_route_owner_blockers(run,source_index,ignored_index);
    std::vector<V528PathRuntimeBlocker> planned_only;
    for(std::size_t order=0;order<source_order;++order){
        const auto pair=image.pairs[order];
        if(pair.entity_index>=run.entities.size()||pair.entity_index==ignored_index)continue;
        const auto& candidate=run.entities[pair.entity_index];
        // Priority one is another faction: native keeps its present influence
        // and only reinforces the low distance bands at that point.
        if(pair.priority==1)continue;
        remove_route_owner(owners,pair.entity_index);
        const auto from=point(candidate.actor.position);
        const auto to=represented_route_target(candidate);
        const auto before=owners.size();
        append_segment_influence(owners,pair.entity_index,from,to,
                                 candidate.motion.collision_radius_q12);
        for(std::size_t i=before;i<owners.size();++i)planned_only.push_back(owners[i].blocker);
    }
    if(planned_only.empty())return base_route;

    const V519PathWorldPointQ12 from{point(run.entities[source_index].actor.position).x,
                                      point(run.entities[source_index].actor.position).y};
    const auto base_primary=base_route.primary_world_q12.value_or(base_route.resolved_world_q12);
    if(simulation_lab_path_world_segment_clear(from,base_primary,planned_only,
            run.entities[source_index].motion.collision_radius_q12))return base_route;

    auto blockers=flatten_route_blockers(statics,owners);
    const auto& source=run.entities[source_index];
    const auto replanned=v565_resolve_route_fast_for_simulation_lab(
        field,from.x_q12,from.y_q12,base_primary.x_q12,base_primary.y_q12,
        1U<<(source.faction_index&31U),source.traversal_low_band_index,
        source.traversal_high_band_index,blockers,source.motion.collision_radius_q12,
        &cache,true);
    if(!replanned.usable())return base_route;
    ++run.metrics.navigation_overlap_group_source_replans;
    auto adjusted=base_route;
    adjusted.primary_world_q12=replanned.primary_world_q12.value_or(replanned.resolved_world_q12);
    adjusted.primary_cell=replanned.primary_cell.value_or(replanned.resolved_cell);
    adjusted.disposition=V528RouteDisposition::Routed;
    return adjusted;
}

void publish(SimulationLabEntity& e){
    auto& s=e.combat_movement;
    simulation_lab_sync_movement_coordinates(e,s);
    if(!(s.flags&1U)){e.actor.move_target.reset();e.actor.move_waypoint.reset();return;}
    const Vec3f p{q12_to_float(s.stored.x),q12_to_float(s.stored.y),e.actor.position.z};
    e.actor.move_target=p;e.actor.move_waypoint=p;
}
struct Dependencies {
    SimulationLabRun& run;SimulationLabEntity& entity;const FieldGrid& field;size_t index;
    std::span<const V528PathRuntimeBlocker> statics;V565PathTerrainCache& cache;
    bool account_follow_metrics{true};
    std::optional<size_t> additionally_ignored_entity_index;
    bool coordinate_overlapping_group_routes{};
    bool within_movement_callback{};
    bool last_route_usable{};
    bool last_resolved_cell_valid{};
    bool last_destination_retargeted{};
    V519PathScratchCoordinate last_resolved_cell{};
    MovementPoint clamp(MovementPoint from,MovementPoint to,int32_t maximum){
        const int32_t dx=movement_sub(to.x,from.x),dy=movement_sub(to.y,from.y);
        const auto length=retail_uint64_sqrt(uint64_t(int64_t(dx)*dx)+uint64_t(int64_t(dy)*dy));
        if(length<=static_cast<uint32_t>(maximum))return to;
        const auto heading=fixed_atan2_angle16_compat(dy,dx);
        const auto offset=retail_direction_scaled(0U,heading,maximum);
        return {std::bit_cast<int32_t>(uint32_t(from.x)+uint32_t(offset[0])),
                std::bit_cast<int32_t>(uint32_t(from.y)+uint32_t(offset[1]))};
    }
    MovementRouteResult resolve(PioneerCommandState& s,MovementPoint target){
        // Native lifts the linked overlap owner's influence as well as the
        // source's. A docked walker can retain its platform link while routing.
        if(run.level_runtime_enabled&&entity.linked_entity_index)
            additionally_ignored_entity_index=entity.linked_entity_index;
        const auto trace_grid=[&](const char* stage){
            const auto tick=std::getenv("THANDOR_SIMLAB_ROUTE_TRACE_TICK");
            if(!tick||std::strtoul(tick,nullptr,10)!=run.tick)return;
            const auto stamp=entity.navigation_influence_position_q12.value_or(point(entity.actor.position));
            std::fprintf(stderr,"ROUTE_INPUT tick=%u slot=%zu stage=%s from=%d,%d to=%d,%d flags=%u stamp=%d,%d bands=%u,%u radius=%d\n",run.tick,entity.native_army_pool_slot,stage,s.model.x,s.model.y,target.x,target.y,s.flags,stamp.x,stamp.y,entity.traversal_low_band_index,entity.traversal_high_band_index,entity.motion.collision_radius_q12);
            const auto prefix=std::getenv("THANDOR_SIMLAB_ROUTE_GRID_PREFIX");
            if(prefix&&run.retail_scratch){const auto name=std::string(prefix)+"_"+std::to_string(run.tick)+"_"+std::to_string(entity.native_army_pool_slot)+"_"+stage+".bin";
                if(auto* f=std::fopen(name.c_str(),"wb")){const auto& image=*run.retail_scratch;const uint32_t dims[]{image.width,image.height};std::fwrite(dims,4,2,f);std::fwrite(image.state.data(),4,image.state.size(),f);std::fclose(f);}
            }
        };
        trace_grid("before");
        last_route_usable=false;
        last_resolved_cell_valid=false;
        last_destination_retargeted=false;
        if(account_follow_metrics)++entity.combat_follow_route_resolves;
        ++run.metrics.navigation_route_resolves;
        const auto raw=v519_path_world_to_scratch(target.x,target.y),clipped=v519_path_clip_to_inner_scratch(field,raw);
        last_resolved_cell=clipped;
        last_resolved_cell_valid=true;
        if(raw!=clipped){const auto p=v519_path_scratch_center_to_world_q12(clipped);target={p.x_q12,p.y_q12};last_destination_retargeted=true;++run.metrics.navigation_destination_retargets;}
        // Legacy actors may omit terrain metadata. Authored stationary models
        // still run group coordination with the native zero blocking masks.
        const bool unrestricted=!entity.has_traversal_profile;
        if(unrestricted&&(!coordinate_overlapping_group_routes||!entity.runtime_model_hierarchy)){
            last_route_usable=true;return {target,target,false};
        }
        std::vector<V528PathRuntimeBlocker> blockers(statics.begin(),statics.end());
        for(size_t i=0;i<run.entities.size();++i){
            const auto& other=run.entities[i];
            if(i==index||additionally_ignored_entity_index==i||
               !simulation_lab_has_live_root_model_owner(other)||
               entity.target_entity_index==i||
               other.motion.collision_radius_q12<=0)continue;
            if((entity.linked_entity_index&&*entity.linked_entity_index==i)||(other.linked_entity_index&&*other.linked_entity_index==index))continue;
            const auto p=point(other.actor.position);blockers.push_back({p.x,p.y,other.motion.collision_radius_q12,simulation_lab_route_blocker_class(other)});
        }
        std::shared_ptr<SimulationLabRetailScratchImage> planning;
        const auto influence=[&](size_t owner,bool add){
            auto& e=run.entities[owner];
            const bool high=uses_high_grid_influence(e.runtime_model_class_id);
            const auto position=!add&&high
                ?e.navigation_influence_position_q12.value_or(point(e.actor.position)):point(e.actor.position);
            retail_visit_grid_influence(planning->width,planning->state,position,
                e.motion.collision_radius_q12,e.runtime_model_class_id,[&](size_t cell,uint32_t bits){
                    if(add)planning->state[cell]|=bits;else planning->state[cell]&=~bits;
                });
            if(add&&high)e.navigation_influence_position_q12=point(e.actor.position);
        };
        // Native route queries temporarily remove the source (and its linked
        // overlap owner), then restore their influence at the current positions.
        if(run.level_runtime_enabled&&run.retail_scratch&&cache.runtime_states){
            planning=std::make_shared<SimulationLabRetailScratchImage>(*run.retail_scratch);
            if(additionally_ignored_entity_index&&*additionally_ignored_entity_index<run.entities.size())
                influence(*additionally_ignored_entity_index,false);
            influence(index,false);
            run.retail_scratch=planning;
            cache.runtime_states=std::shared_ptr<const std::vector<uint32_t>>(planning,&planning->state);
        }
        trace_grid("removed");
        V528RouteResult route;
        if(unrestricted){
            route.disposition=V528RouteDisposition::Direct;
            route.resolved_world_q12={target.x,target.y};route.resolved_cell=clipped;
        }else route=v565_resolve_route_fast_for_simulation_lab(field,s.model.x,s.model.y,target.x,target.y,
            1U<<(entity.faction_index&31U),entity.traversal_low_band_index,entity.traversal_high_band_index,
            blockers,entity.motion.collision_radius_q12,&cache,false,(s.flags&2U)!=0U);
        if(const auto trace=std::getenv("THANDOR_SIMLAB_ROUTE_TRACE_TICK");trace&&std::strtoul(trace,nullptr,10)==run.tick){const auto p=route.primary_world_q12.value_or(route.resolved_world_q12);std::fprintf(stderr,"BASE_ROUTE tick=%u slot=%zu point=%d,%d\n",run.tick,entity.native_army_pool_slot,p.x_q12,p.y_q12);}
        if(coordinate_overlapping_group_routes&&route.usable())
            route=coordinate_overlapping_group_route(run,field,index,target,route,statics,cache,
                                                     additionally_ignored_entity_index,within_movement_callback,account_follow_metrics);
        if(planning){
            if(additionally_ignored_entity_index&&*additionally_ignored_entity_index<run.entities.size())
                influence(*additionally_ignored_entity_index,true);
            influence(index,true);
        }
        if(!route.usable()){
            // Preserve an uncompleted destination and let native retry gating
            // retry it. A failed adapter solve must not manufacture arrival.
            if(account_follow_metrics)++entity.combat_follow_route_failures;
            return {s.model,target,false};
        }
        last_route_usable=true;
        last_resolved_cell=route.resolved_cell;
        last_resolved_cell_valid=true;
        last_destination_retargeted=route.requested_cell_retargeted;
        if(route.requested_cell_retargeted)++run.metrics.navigation_destination_retargets;
        if(route.primary_world_q12)++run.metrics.navigation_routed_solves;
        const auto f=route.resolved_world_q12,p=route.primary_world_q12.value_or(f);
        return {{p.x_q12,p.y_q12},{f.x_q12,f.y_q12},false};
    }
    MovementRouteResult resolve(PioneerMovementState& s,MovementPoint p){return resolve(static_cast<PioneerCommandState&>(s),p);}
    void queue(PioneerMovementState& s,MovementPoint p){pioneer_command_chain(static_cast<PioneerCommandState&>(s),p,1,*this);}
    MovementResult retarget(PioneerMovementState& s,MovementPoint p){
        const MovementPoint d{std::bit_cast<int32_t>(movement_abs(s.model.x,s.primary.x)),std::bit_cast<int32_t>(movement_abs(s.model.y,s.primary.y))};
        return {d,pioneer_direct_command(static_cast<PioneerCommandState&>(s),p,*this)};
    }
};
}
SimulationLabRoutePriorityImage simulation_lab_overlapping_route_priority_image(
        const SimulationLabRun& run,std::size_t source_index,MovementPoint target,bool within_owner_callback) noexcept {
    return build_overlapping_route_priority_image(run,source_index,target,within_owner_callback);
}
std::optional<size_t> simulation_lab_visible_command_target(SimulationLabRun& run,const FieldGrid& field,size_t source){
    auto& e=run.entities[source];const auto target=e.target_entity_index;
    if(!target||*target>=run.entities.size())return {};
    const auto& other=run.entities[*target];
    if(simulation_lab_command_target_owner_available(other)&&
       (simulation_lab_model_visibility_bits(run,field,e.faction_index,other)&2U)!=0U)return target;
    // ResolveShotAimPoint drops an invisible command target even for a
    // group-assigned owner. The separate assigned target and route survive.
    e.target_entity_index.reset();e.combat_command_mode_flags=0;return {};
}
void simulation_lab_sync_movement_coordinates(SimulationLabEntity& e,
    const PioneerMovementState& state) noexcept {
    e.ai_path_coordinate_q12=state.fallback;
    e.ai_common_tracked_coordinate_q12=state.primary;
    e.ai_damage_tracked_coordinate_q12=state.stored;
}
void simulation_lab_sync_follow(SimulationLabEntity& e){
    auto& s=e.combat_movement;s.model=point(e.actor.position);
    s.definition_allows_movement=e.motion.movable();s.command_mode=e.combat_command_mode_flags;
    // SetPendingMoveTarget already published native movement flags and
    // coordinates. A deferred actor pose is not an idle native command.
    if(e.combat_follow_owned||e.group_route_pending_point)return;
    if(!e.combat_movement_initialized){
        s.primary=s.fallback=s.stored=s.last_checked=s.model;
        e.combat_movement_initialized=true;
    }
    if(e.actor.move_target){
        s.flags|=1U;s.primary=s.fallback=point(*e.actor.move_target);s.stored=point(e.actor.move_waypoint.value_or(*e.actor.move_target));
        simulation_lab_sync_movement_coordinates(e,s);
    }else{s.flags&=~0xF9U;}
    if(e.production_linked_to_parent||e.production_movement_unlock_pending)s.flags|=2U;
    else s.flags&=~2U;
}
// GameEntityRuntime_ResetMovementFlagsAndAnchorCoordinatesFromModel,
// called by SelectionRuntime_StopMovement (open-thandor selection/commands.cpp).
// The selection callback ignores movement-locked entities (0x2) and clears
// movement 0x01/0x08/0x10/0x20 plus routed 0x200. Unlike native arrival,
// this explicitly anchors path and damage-state tracked coordinates. Do not
// call this when a weapon merely stops following a visible target.
bool simulation_lab_explicit_stop_movement(SimulationLabEntity& e){
    if(!simulation_lab_has_live_root_model_owner(e) ||
       (e.combat_movement.flags&0x2U)!=0U ||
       (e.pioneer_movement_initialized && (e.pioneer_movement.flags&0x2U)!=0U))return false;
    simulation_lab_cancel_dismantling_on_stop(e);
    const auto location=point(e.actor.position);
    e.combat_movement.model=location;
    e.combat_movement.flags&=~0x239U;
    e.combat_movement.fallback=e.combat_movement.primary=e.combat_movement.stored=location;
    // Both adapters represent the same native common.commandFlags, but the
    // Pioneer tick uses pioneer_movement. Clearing only combat_movement lets
    // an explicitly stopped Pioneer resume its queued route on the next tick.
    if(e.pioneer_movement_initialized){
        e.pioneer_movement.model=location;
        e.pioneer_movement.flags&=~0x239U;
        // The detached Pioneer adapter consumes `primary` even with ACTIVE
        // clear. All three movement pairs alias the native common coordinates;
        // preserving a pre-stop fallback would resurrect it on synchronization.
        e.pioneer_movement.fallback=e.pioneer_movement.primary=e.pioneer_movement.stored=location;
    }
    e.combat_movement_initialized=true;
    e.combat_follow_owned=false;
    e.actor.move_target.reset();
    e.actor.move_waypoint.reset();
    e.navigation_target_cell_valid=false;
    e.navigation_direct_route_valid=false;
    e.navigation_recheck_tick=0;
    e.ai_path_coordinate_q12=location;
    e.ai_common_tracked_coordinate_q12=location;
    e.ai_damage_tracked_coordinate_q12=location;
    return true;
}

void simulation_lab_stop_follow(SimulationLabEntity& e){
    if(!(e.combat_movement.flags&0x20U))return;
    e.combat_movement.model=point(e.actor.position);
    army_reset_follow(e.combat_movement,(e.combat_command_mode_flags&8U)!=0);
    ++e.combat_follow_stops;
    if(e.combat_follow_owned)publish(e);
    if(!(e.combat_movement.flags&1U)){
        const auto& state=e.combat_movement;
        // The next native waypoint callback can resume the retained primary
        // destination after the weapon clears its temporary follow segment.
        e.combat_follow_owned=movement_abs(state.model.x,state.primary.x)>0x1000U||
            movement_abs(state.model.y,state.primary.y)>0x1000U;
    }
    e.navigation_target_cell_valid=e.navigation_direct_route_valid=false;
}
MovementRouteResult simulation_lab_resolve_native_route(SimulationLabRun& run,const FieldGrid& field,
        size_t source,PioneerCommandState& state,MovementPoint target,
        std::span<const V528PathRuntimeBlocker> statics,V565PathTerrainCache& cache,
        bool within_owner_callback,bool& usable){
    auto& entity=run.entities[source];
    Dependencies deps{run,entity,field,source,statics,cache,within_owner_callback,
        entity.production_linked_to_parent?entity.production_parent_entity_index:std::nullopt,
        true,within_owner_callback};
    const auto result=deps.resolve(state,target);usable=deps.last_route_usable;return result;
}
void simulation_lab_set_site_route(SimulationLabRun& run,const FieldGrid& field,
        size_t source,MovementPoint target,
        std::span<const V528PathRuntimeBlocker> statics,V565PathTerrainCache& cache){
    if(source>=run.entities.size())return;
    auto& e=run.entities[source];
    // ArmyRuntime_StartRoutedMoveCommand exits before touching command state
    // when the definition cannot move. Keep that branch a strict no-op.
    if(!e.motion.movable())return;
    simulation_lab_sync_follow(e);
    auto& s=e.pioneer_movement_initialized&&!e.production_linked_to_parent
        ?e.pioneer_movement:e.combat_movement;
    s.model=point(e.actor.position);
    s.definition_allows_movement=true;
    s.command_mode=e.combat_command_mode_flags;

    const bool locked=(s.flags&2U)!=0U;
    Dependencies deps{run,e,field,source,statics,cache,false,{},run.level_runtime_enabled};
    pioneer_command_chain(s,target,0,deps);
    e.combat_command_mode_flags=s.command_mode;
    e.combat_movement_initialized=true;

    // The locked native branch only replaces the waypoint queue. It does not
    // replace the active route, target-follow owner or navigation cache.
    if(locked)return;
    // The new route supersedes any peer adjustment queued earlier in this tick.
    e.group_route_pending_point.reset();
    simulation_lab_sync_movement_coordinates(e,s);

    e.combat_follow_owned=run.level_runtime_enabled &&
        (e.runtime_model_class_id==1U||e.runtime_model_class_id==2U||e.runtime_model_class_id==3U||e.runtime_model_class_id==17U);
    const Vec3f fallback{q12_to_float(s.fallback.x),q12_to_float(s.fallback.y),
                         e.actor.position.z};
    e.actor.move_target=fallback;
    if(deps.last_route_usable &&
       (s.stored.x!=s.fallback.x || s.stored.y!=s.fallback.y))
        e.actor.move_waypoint=Vec3f{q12_to_float(s.stored.x),q12_to_float(s.stored.y),
                                    e.actor.position.z};
    else
        e.actor.move_waypoint.reset();

    e.navigation_target_cell_valid=deps.last_resolved_cell_valid;
    if(deps.last_resolved_cell_valid)e.navigation_target_cell=deps.last_resolved_cell;
    e.navigation_direct_route_valid=deps.last_route_usable && !e.actor.move_waypoint;
    e.navigation_recheck_tick=run.tick+8U+static_cast<std::uint32_t>(source&3U);
}
void simulation_lab_append_waypoint_or_start_route(SimulationLabRun& run,const FieldGrid& field,
        size_t source,MovementPoint target,
        std::span<const V528PathRuntimeBlocker> statics,V565PathTerrainCache& cache){
    if(source>=run.entities.size())return;
    auto& e=run.entities[source];
    if(!e.motion.movable())return;
    simulation_lab_sync_follow(e);
    auto& state=e.pioneer_movement_initialized&&!e.production_linked_to_parent
        ?e.pioneer_movement:e.combat_movement;
    state.model=point(e.actor.position);
    state.definition_allows_movement=true;
    state.command_mode=e.combat_command_mode_flags;

    const bool queue_only=(state.flags&0x80U)==0U&&(state.flags&3U)!=0U;
    Dependencies deps{run,e,field,source,statics,cache,false,{}};
    pioneer_command_chain(state,target,2,deps);
    e.combat_command_mode_flags=state.command_mode;
    e.combat_movement_initialized=true;

    // The native append branch changes only the queue and command bits. Its
    // active route, follow owner, actor target and route-cache ownership stay
    // untouched until the current point completes.
    if(queue_only)return;
    simulation_lab_sync_movement_coordinates(e,state);

    e.combat_follow_owned=false;
    const Vec3f fallback{q12_to_float(state.fallback.x),q12_to_float(state.fallback.y),
                         e.actor.position.z};
    e.actor.move_target=fallback;
    if(deps.last_route_usable&&
       (state.stored.x!=state.fallback.x||state.stored.y!=state.fallback.y))
        e.actor.move_waypoint=Vec3f{q12_to_float(state.stored.x),q12_to_float(state.stored.y),
                                    e.actor.position.z};
    else
        e.actor.move_waypoint.reset();
    e.navigation_target_cell_valid=deps.last_resolved_cell_valid;
    if(deps.last_resolved_cell_valid)e.navigation_target_cell=deps.last_resolved_cell;
    e.navigation_direct_route_valid=deps.last_route_usable&&!e.actor.move_waypoint;
    e.navigation_recheck_tick=run.tick+8U+static_cast<std::uint32_t>(source&3U);
}
void simulation_lab_set_group_target_route(SimulationLabRun& run,const FieldGrid& field,
        size_t source,size_t target,
        std::span<const V528PathRuntimeBlocker> statics,V565PathTerrainCache& cache){
    if(source>=run.entities.size()||target>=run.entities.size())return;
    auto& e=run.entities[source];
    const auto& target_entity=run.entities[target];
    if(!simulation_lab_has_live_root_model_owner(e)||
       !simulation_lab_command_target_owner_available(target_entity))return;

    simulation_lab_sync_follow(e);
    // ArmyRuntime_ResolveCommandTargetAndRoute first resets an existing
    // target-follow move.  The recovered reset branch depends on whether the
    // previous command was an AI combat target, so do this before replacing the
    // command flags with TARGET_ARMY.
    if((e.combat_movement.flags&0x20U)!=0U)simulation_lab_stop_follow(e);
    simulation_lab_sync_follow(e);

    auto& state=e.combat_movement;
    state.model=point(e.actor.position);
    state.definition_allows_movement=e.motion.movable();
    e.combat_assigned_target_entity_index=target;
    e.target_entity_index=target;
    e.combat_command_mode_flags=1U; // ARMY_COMMAND_MODE_TARGET_ARMY
    e.combat_command_generation=1024;
    state.command_mode=e.combat_command_mode_flags;

    Dependencies deps{run,e,field,source,statics,cache,false,{},true};
    const bool started=e.motion.movable()&&
        army_start_fallback_follow(state,point(target_entity.actor.position),deps);

    // AiUnitGroup_AssignCollectedEntitiesToBestTarget clears ROUTED after the
    // resolver, then marks the target command interrupted/group-owned and uses
    // the shared group generation candidate (32 in the represented runtime).
    state.flags&=~0x200U;
    e.combat_command_mode_flags=5U;
    e.combat_command_generation=32;
    state.command_mode=e.combat_command_mode_flags;
    e.combat_movement_initialized=true;
    e.navigation_target_cell_valid=false;
    e.navigation_direct_route_valid=false;
    if(started||(run.level_runtime_enabled&&(state.flags&1U)!=0U)){
        e.combat_follow_owned=true;
        publish(e);
    }else if((state.flags&0x20U)==0U){
        e.combat_follow_owned=false;
    }
}
static void install_factory_exit_route(SimulationLabRun& run,const FieldGrid& field,
        size_t source,size_t parent,MovementPoint exit_target,MovementPoint queued_rally,
        std::span<const V528PathRuntimeBlocker> statics,V565PathTerrainCache& cache){
    auto& e=run.entities[source];
    if(!simulation_lab_has_live_root_model_owner(e))return;

    if(run.level_runtime_enabled)e.production_exit_command_tick=run.tick;
    simulation_lab_sync_follow(e);
    auto& state=e.combat_movement;
    state.model=point(e.actor.position);
    state.definition_allows_movement=e.motion.movable();

    // ArmyRuntime_StartMoveCommandWithAuxiliaryValues has no acceleration
    // guard. It creates a locked routed order, replaces any earlier waypoint
    // queue with exactly one auxiliary point, and resolves the exit in the
    // issue tick. Preserve that even for compact tests with an unresolved
    // motion profile.
    state.flags|=0x24BU; // ACTIVE|LOCKED|WAYPOINTS_QUEUED|ORDERED|ROUTED
    if(run.level_runtime_enabled)state.flags|=0x400U; // factory-produced units mirror pending routes
    state.flags&=~0xB0U; // ROUTE_POINT_REACHED|TARGET_FOLLOWING|DIRECT
    state.command_mode&=~0x10U; // ARMY_COMMAND_MODE_SELECTION_ORDER
    state.queue_count=1U;
    state.queue[0]=queued_rally;
    e.group_route_pending_point.reset();

    Dependencies deps{run,e,field,source,statics,cache,false,parent,run.level_runtime_enabled};
    const auto route=deps.resolve(state,exit_target);
    state.fallback=route.fallback;
    state.primary=route.fallback;
    state.stored=route.point;
    state.retry=0x40U;
    state.last_checked=state.model;
    simulation_lab_sync_movement_coordinates(e,state);
    e.combat_command_mode_flags=state.command_mode;
    e.combat_movement_initialized=true;
    e.combat_follow_owned=run.level_runtime_enabled&&
        (e.runtime_model_class_id==1U||e.runtime_model_class_id==2U||e.runtime_model_class_id==3U||
         e.runtime_model_class_id==17U||e.runtime_model_class_id==18U);

    const float z=e.actor.position.z;
    e.actor.move_target=Vec3f{q12_to_float(queued_rally.x),q12_to_float(queued_rally.y),z};
    if(deps.last_route_usable)
        e.actor.move_waypoint=Vec3f{q12_to_float(state.stored.x),q12_to_float(state.stored.y),z};
    else
        e.actor.move_waypoint.reset();

    // Seed the generic Lab movement cache with the issued exit route so the
    // same tick does not immediately solve it a second time. Use the requested
    // (bounded) exit cell as the cache key; the stored waypoint may be a
    // retargeted primary point produced by the native resolver.
    const auto raw=v519_path_world_to_scratch(exit_target.x,exit_target.y);
    e.navigation_target_cell=v519_path_clip_to_inner_scratch(field,raw);
    e.navigation_target_cell_valid=deps.last_route_usable;
    e.navigation_direct_route_valid=false;
    e.navigation_recheck_tick=run.tick+8U+static_cast<std::uint32_t>(source&3U);
}
void simulation_lab_set_factory_exit_route(SimulationLabRun& run,const FieldGrid& field,
        size_t source,size_t parent,MovementPoint exit_target,MovementPoint queued_rally,
        std::span<const V528PathRuntimeBlocker> statics,V565PathTerrainCache& cache){
    if(source>=run.entities.size()||parent>=run.entities.size()||source==parent)return;
    if(run.defer_factory_exit_routes){
        run.deferred_factory_exit_routes.push_back({source,parent,exit_target,queued_rally});
        // Walkers service their old gait until the older factory owner installs
        // the route. Ground adapters wait for the new route in this batch.
        auto& child=run.entities[source];
        if(run.level_runtime_enabled&&!child.articulated_contact_geometry_enabled)
            child.production_exit_command_tick=run.tick;
        return;
    }
    install_factory_exit_route(run,field,source,parent,exit_target,queued_rally,statics,cache);
}

void simulation_lab_flush_factory_exit_routes(SimulationLabRun& run,const FieldGrid& field,
    size_t parent,std::span<const V528PathRuntimeBlocker> statics,V565PathTerrainCache& cache){
    for(const auto& route:run.deferred_factory_exit_routes)
        if(route.parent==parent&&route.child<run.entities.size())
            install_factory_exit_route(run,field,route.child,parent,route.exit,route.rally,statics,cache);
    std::erase_if(run.deferred_factory_exit_routes,[&](const auto& route){return route.parent==parent;});
}
bool simulation_lab_primary_follow_weapon(const SimulationLabEntity& e,size_t weapon){
    if(weapon>=e.combat_weapons.size())return false;
    if(!e.runtime_model_hierarchy)return weapon==0;
    // Runtime attachment order follows new linked MDL owners, not SPR child
    // slots: walker slots0/1 are same-definition legs, its first weapon is9.
    if(e.root_model_component_index<e.model_components.size()){
        for(const auto& c:e.model_components){
            if(c.parent_component_index!=e.root_model_component_index)continue;
            if(c.runtime_removed)return true;
            return e.combat_weapons[weapon].profile.yaw_model_node_index==c.source_model_node_index;
        }
    }
    const auto& nodes=*e.runtime_model_hierarchy;
    const auto root=std::find_if(nodes.begin(),nodes.end(),[](const auto& n){return n.parent_index==UINT32_MAX;});
    if(root==nodes.end())return false;
    const auto root_index=static_cast<uint32_t>(root-nodes.begin());
    const auto primary=std::find_if(nodes.begin(),nodes.end(),[&](const auto& n){return n.parent_index==root_index&&n.model_definition_id!=root->model_definition_id&&n.creates_runtime_node();});
    if(primary==nodes.end())return true;
    return e.combat_weapons[weapon].profile.yaw_model_node_index==static_cast<uint32_t>(primary-nodes.begin());
}
void simulation_lab_weapon_follow(SimulationLabRun& run,const FieldGrid& field,size_t source,size_t weapon,
        Vec3f target,bool clear,std::span<const V528PathRuntimeBlocker> statics,V565PathTerrainCache& cache){
    // Armed static roots keep native follow/waypoint state even though their
    // movement class never translates them. Turret recentering reads that state.
    auto& e=run.entities[source];if(!simulation_lab_combat_command_class(e)||!simulation_lab_command_target_owner_available(e))return;
    simulation_lab_sync_follow(e);
    if(clear){simulation_lab_stop_follow(e);return;}
    ++e.combat_follow_blocked_callbacks;
    if(!simulation_lab_primary_follow_weapon(e,weapon)||(e.combat_movement.flags&0x20U))return;
    Dependencies deps{run,e,field,source,statics,cache,true,{},run.level_runtime_enabled};auto& s=e.combat_movement;
    const bool started=(e.combat_command_mode_flags&8U)?army_start_clamped_follow(s,point(target),deps):army_start_fallback_follow(s,point(target),deps);
    if(started){e.combat_follow_owned=true;++e.combat_follow_starts;publish(e);e.navigation_target_cell_valid=e.navigation_direct_route_valid=false;}
}
bool simulation_lab_prepare_follow_motion(SimulationLabRun& run,const FieldGrid& field,size_t source,
        std::span<const V528PathRuntimeBlocker> statics,V565PathTerrainCache& cache,bool within_movement_callback){
    auto& e=run.entities[source];simulation_lab_sync_follow(e);
    if(e.group_route_pending_point&&(e.combat_movement.flags&2U)!=0U)
        e.group_route_pending_point.reset();
    if(e.group_route_pending_point&&run.tick>=e.group_route_pending_tick){
        auto& state=e.combat_movement;
        // SetPendingMoveTarget already published the peer segment and retry.
        // Activating its deferred pose must not restart that timer.
        const bool already_published=state.stored.x==e.group_route_pending_point->x&&
            state.stored.y==e.group_route_pending_point->y;
        state.stored=*e.group_route_pending_point;
        if((state.flags&1U)==0U)state.fallback=state.stored;
        if(state.flags&0x400U)state.primary=state.stored;
        state.flags=(state.flags|1U)&~0x30U;
        if(!already_published){state.retry=0x40U;state.last_checked=state.model;}
        e.combat_follow_owned=true;
        e.group_route_pending_point.reset();
    }
    // Native ground/articulated callbacks service waypoint state even after
    // ACTIVE was cleared: a retained distant primary can start a direct route.
    // Ownership is an adapter cache, not the authored native callback's
    // admission gate. Legacy API actors without an MDL keep their arrival adapter.
    if(!e.combat_follow_owned&&!(run.level_runtime_enabled&&e.runtime_model_hierarchy))return false;
    Dependencies deps{run,e,field,source,statics,cache,true,
        e.production_linked_to_parent?e.production_parent_entity_index:std::nullopt,
        run.level_runtime_enabled,within_movement_callback};
    const auto next=pioneer_movement_update(e.combat_movement,deps);
    simulation_lab_sync_movement_coordinates(e,e.combat_movement);
    if(next.carry){
        // Arrival can leave ACTIVE set (for a retained routed destination).
        // Native runs the waypoint callback again next tick; dropping the
        // adapter here freezes its heading and retry state permanently.
        e.combat_follow_owned=run.level_runtime_enabled&&(e.combat_movement.flags&1U)!=0U;
        e.actor.move_target.reset();e.actor.move_waypoint.reset();return false;
    }
    const Vec3f p{q12_to_float(next.point.x),q12_to_float(next.point.y),e.actor.position.z};
    e.combat_follow_owned=true;e.actor.move_target=p;e.actor.move_waypoint=p;return true;
}
void simulation_lab_finish_follow_motion(SimulationLabEntity& e,bool reached,bool blocked){
    if(!e.combat_follow_owned)return;
    e.combat_movement.model=point(e.actor.position);
    if(reached||blocked)e.combat_movement.flags|=0x10U;
}
}
