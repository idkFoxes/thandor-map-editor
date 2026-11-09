#include "thandor/simulation_lab_site_placement.hpp"
#include "thandor/simulation_placement.hpp"
#include "thandor/retail_extended_placement.hpp"
#include "thandor/simulation_lab_targeting.hpp"
#include "thandor/simulation_lab_ai_owner_list_v718.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include "thandor/simulation_lab_owner_order.hpp"
#include "thandor/pioneer_site_reservation.hpp"
#include "thandor/pioneer_resource_grid.hpp"
#include "thandor/retail_special_site.hpp"
#include <bit>
#include <algorithm>
#include <stdexcept>
#include <cmath>

namespace thandor {
namespace {
SimulationLabSiteModel site_model(uint32_t army,const ModelVisualRecord& d) {
    const auto& p=d.simulation;
    return {army,d.runtime_class_id,p.movement_collision_radius_q12,p.class_specific_1a8_raw,
        p.class_specific_19c_raw,p.placement_contact_kind,p.class_specific_c0_raw,{}};
}
MovementPoint point_xy(Vec3f p){return {float_to_q12(p.x),float_to_q12(p.y)};}
}
RetailAiSiteAdmission retail_ai_site_admission(const FieldGrid& field,
    const SimulationLabRetailScratchImage& scratch,uint32_t faction,uint32_t x,uint32_t y,
    std::span<const uint32_t,4> active_masks) {
    RetailAiSiteAdmission result;
    if(faction>=8||x>=field.width()||y==0||y+1>=field.height()||
        scratch.width!=field.width()*4U||scratch.height!=field.height()*4U||
        scratch.field_occupancy_masks.size()!=field.cells().size())return result;
    const int64_t current=int64_t(y)*field.width()+x;
    const auto present=[&](int64_t i){return i>=0&&uint64_t(i)<field.cells().size()&&
        ((scratch.field_occupancy_masks[size_t(i)]>>(faction*8U))&0xF9U)!=0;};
    const auto resource=[&](int64_t i){return i<0||uint64_t(i)>=field.cells().size()||
        (field.cells()[size_t(i)].flags_and_material&0x1800U)!=0;};
    const std::array<int64_t,6> neighbors{-int64_t(field.width()),-int64_t(field.width())+1,
        -1,1,int64_t(field.width())-1,int64_t(field.width())};
    bool unoccupied=false,has_resource=resource(current);
    for(const auto delta:neighbors){unoccupied|=!present(current+delta);has_resource|=resource(current+delta);}
    const int64_t top=int64_t(4U*(y-1U)+2U)*scratch.width+4U*x+2U;
    const int64_t stride=int64_t(scratch.width)*3;
    const auto get=[&](int64_t i){return i>=0&&uint64_t(i)<scratch.state.size()?scratch.state[size_t(i)]:0x80000000U;};
    const auto six=[&](int64_t p,int64_t row,int64_t step){return get(p)|get(p+step)|get(p+row-step)|
        get(p+row+step)|get(p+row*2-step)|get(p+row*2);};
    const uint32_t center=get(top+stride),small=six(top,stride,4),wide=six(top-stride,stride*2,8);
    const uint32_t general=center|six(top,stride,3),flagged=center|small;
    result.general=(center&0x80000000U)==0&&present(current)&&unoccupied&&
        std::any_of(active_masks.begin(),active_masks.end(),[&](uint32_t mask){return (mask&general)==0;});
    result.flagged=((scratch.field_occupancy_masks[size_t(current)]>>(faction*8U))&0x10U)!=0&&
        (center&0x80000100U)==0&&!resource(current)&&
        ((flagged&0x81000000U)==0||(flagged&0x82000000U)==0||(flagged&0x90000000U)==0);
    if(!present(current)||(center&0xF0003F00U)!=0)return result;
    result.resource=(center&0xF000FF00U)==0&&(small&0xF0000000U)==0&&
        (wide&0xF0000000U)==0&&resource(current);
    result.base=(small&0xF0000000U)==0&&!has_resource;
    result.large=(center&0xF000FF00U)==0&&(small&0xF0000000U)==0&&
        (wide&0x70000000U)==0&&!has_resource;
    return result;
}
std::optional<SimulationLabSiteModel> simulation_lab_first_site_model(
    const SimulationLabRuntimeCatalog& catalog,uint32_t army) {
    const auto a=catalog.armies.find(army);if(a==catalog.armies.end())return std::nullopt;
    for(const auto& group:a->second.model_variant_groups) {
        if(group.parent_group_index!=UINT32_MAX||group.variants.empty())continue;
        const auto d=catalog.definitions.find(group.variants.front().model_definition_id);
        if(d!=catalog.definitions.end()){
            auto model=site_model(army,d->second);
            const auto sprite=catalog.sprites.find(d->second.sprite_path);
            if(sprite!=catalog.sprites.end()&&sprite->second.factory_exit)
                model.factory_exit_local=point_xy(*sprite->second.factory_exit);
            return model;
        }
        return std::nullopt;
    }
    return std::nullopt;
}
std::vector<SimulationLabSiteOwner> simulation_lab_site_owners(
    const SimulationLabRun& run,const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog,
    std::span<const SimulationLabStaticPlacementOwner> static_models) {
    struct OrderedOwner {SimulationLabOwnerOrderKey key{};size_t index{};bool is_static{};};
    std::vector<OrderedOwner> ordered;ordered.reserve(run.entities.size()+static_models.size());
    for(size_t index=0;index<run.entities.size();++index){
        const auto& entity=run.entities[index];
        if(entity.runtime_removed||entity.root_model_component_index>=entity.model_components.size()||
           entity.model_components[entity.root_model_component_index].runtime_removed)continue;
        ordered.push_back({{entity.synthetic,entity.construction_start_tick,
            entity.source_placement_index,index},index,false});
    }
    for(size_t index=0;index<static_models.size();++index)
        ordered.push_back({{false,0U,(static_models[index].source_placement_index==SIZE_MAX?0U:static_models[index].source_placement_index),index},index,true});
    if(run.level_runtime_enabled)
        std::sort(ordered.begin(),ordered.end(),[](const auto& left,const auto& right){
            return simulation_lab_owner_newer_first(left.key,right.key);
        });

    std::vector<SimulationLabSiteOwner> out;out.reserve(ordered.size());
    const auto append=[&](uint32_t army,uint32_t faction,uint32_t flags,const ModelVisualRecord& d,
        const SimulationLabEffectPose& pose,int32_t broad){
        const auto m=site_model(army,d);
        SimulationLabSiteOwner owner{{point_xy(pose.position),faction,m.runtime_class,flags,m.support_q12,true},m.radius_q12,{},broad,m.contact_kind};
        if(m.runtime_class==13U){
            const auto sprite=catalog.sprites.find(d.sprite_path);
            if(sprite!=catalog.sprites.end()&&sprite->second.factory_exit){
                const auto p=*sprite->second.factory_exit;const auto& b=pose.basis;
                owner.factory_exit=point_xy({pose.position.x+b[0]*p.x+b[1]*p.y+b[2]*p.z,
                    pose.position.y+b[3]*p.x+b[4]*p.y+b[5]*p.z,pose.position.z});
            }
        }
        out.push_back(owner);
    };
    for(const auto& entry:ordered){
        if(entry.is_static){
            const auto& owner=static_models[entry.index];
            const auto d=catalog.definitions.find(owner.model_definition_id);if(d==catalog.definitions.end())continue;
            int32_t broad=0;const auto sprite=catalog.sprites.find(d->second.sprite_path);
            if(sprite!=catalog.sprites.end())broad=std::max(d->second.simulation.movement_collision_radius_q12,sprite->second.bounding_radius_q12);
            append(0,owner.faction_index,0,d->second,owner.pose,broad);
            continue;
        }
        const auto entity_index=entry.index;const auto& entity=run.entities[entity_index];
        const auto poses=simulation_lab_current_model_poses(entity,field,catalog);
        const auto i=entity.root_model_component_index;
        const auto& c=entity.model_components[i];
        const auto d=catalog.definitions.find(c.model_definition_id);if(d==catalog.definitions.end())continue;
        SimulationLabEffectPose pose;pose.position=entity.actor.position;
        if(c.source_model_node_index<poses.size())pose=poses[c.source_model_node_index];
        append(entity.army_asset_id,entity.faction_index,c.runtime_flags_ec|
            entity.runtime_flags|(c.terminal_runtime_flag8?8U:0U),d->second,pose,
            entity.motion.depth_bin_broad_phase_radius_q12);
        out.back().army_entity_index=entity_index;
        if(d->second.runtime_class_id==13U&&!out.back().factory_exit&&entity.production_exit_local)
            out.back().factory_exit=point_xy(simulation_lab_factory_world_point(field,entity.actor.position,
                entity.actor.body_yaw_turn16,d->second.simulation.placement_contact_kind,
                entity.production_root_sprite_height_offset,*entity.production_exit_local,d->second.simulation.placement_height_offset_q12));
    }
    return out;
}

std::optional<Vec3f> simulation_lab_extended_placement(SimulationLabRun& run,
    const FieldGrid& field,std::span<const SimulationLabSiteOwner> owners,
    const LabAiOwnerWorkspaceV718& planning_owners,SimulationLabSiteModel model,
    std::span<const Vec3f> workspace09,uint32_t faction) {
    if(faction>=run.ai_primary_anchor_cooldown.size())return std::nullopt;
    ++run.metrics.extended_placement_searches;
    if(run.ai_primary_anchor_cooldown[faction]==0U||workspace09.empty())return std::nullopt;
    std::vector<RetailExtendedPrimary> primary;
    std::vector<MovementPoint> visible,unseen,candidates;
    primary.reserve(planning_owners.structures00.size());
    for(const auto index:planning_owners.structures00){
        const auto& entity=run.entities[index];
        primary.push_back({point_xy(entity.actor.position),
            simulation_lab_combat_metrics(entity).class_damage_q12[0]});
    }
    visible.reserve(planning_owners.visible02.size());
    for(const auto index:planning_owners.visible02)
        visible.push_back(point_xy(run.entities[index].actor.position));
    unseen.reserve(planning_owners.unseen03.size());
    for(const auto index:planning_owners.unseen03)
        unseen.push_back(point_xy(run.entities[index].actor.position));
    candidates.reserve(workspace09.size());
    for(const auto point:workspace09)candidates.push_back(point_xy(point));
    const auto& anchor=run.ai_primary_impact_anchor[faction];
    std::optional<Vec3f> accepted;
    const auto best=retail_extended_placement_select(run.ai_primary_anchor_cooldown[faction],
        MovementPoint{anchor.model_x_q12,anchor.model_y_q12},candidates,primary,visible,unseen,
        [&](size_t i){
            const auto point=candidates[i];
            const auto cell=retail_nearest_world_to_cell(field,point.x,point.y);
            if(!cell)return false;
            const auto heading=retail_placement_normal_angles(field,cell->x,cell->y,false)&65535U;
            ++run.metrics.extended_placement_tests;
            if(!simulation_lab_query_site_placement(run,field,owners,model,point,faction,1U,0,heading).accepted)return false;
            accepted=Vec3f{q12_to_float(point.x),q12_to_float(point.y),
                q12_to_float(retail_placement_height_q12(field,point,model.contact_kind))};
            return true;
        });
    if(!best)return std::nullopt;
    ++run.metrics.extended_placement_accepts;
    return accepted;
}
PioneerPlacementResult simulation_lab_query_site_placement(
    const SimulationLabRun& run,const FieldGrid& field,std::span<const SimulationLabSiteOwner> owners,
    SimulationLabSiteModel model,MovementPoint point,uint32_t faction,uint32_t mode,int32_t padding,uint32_t heading) {
    const auto reject=[](PioneerPlacementReason why){return PioneerPlacementResult{false,0,why};};
    switch(model.runtime_class){
      case 0:case 5:case 6:case 7:case 8:case 9:case 12:case 21:return {true,0,PioneerPlacementReason::Accepted};
      case 4:case 10:case 11:case 13:case 14:case 15:case 16:case 20:case 22:case 23:break;
      default:return reject(PioneerPlacementReason::TerrainRejected);
    }
    if(model.radius_q12<0)return reject(PioneerPlacementReason::TerrainRejected);
    const auto blocked=[&](MovementPoint query,int32_t radius){
      if(!radius)return false;
      for(const auto& owner:owners){
        const auto cls=owner.support.runtime_class;
        if(!owner.support.is_runtime_model||cls==0U||cls==12U||
           ((mode&4U)&&!simulation_lab_structure_depth_class(cls)))continue;
        if(owner.radius_q12<0)return true;
        // Native interval prefilter uses the represented model's subtree envelope.
        if(owner.broad_phase_radius_q12>0&&!simulation_depth_bin_overlap(
            {q12_to_float(query.x),q12_to_float(query.y),0},radius,
            {q12_to_float(owner.support.position.x),q12_to_float(owner.support.position.y),0},owner.broad_phase_radius_q12))continue;
        const auto distance_sq=[&](MovementPoint center)->uint64_t{
            const auto dx=int64_t(query.x)-center.x,dy=int64_t(query.y)-center.y;
            const auto ax=uint64_t(dx<0?-dx:dx),ay=uint64_t(dy<0?-dy:dy);
            if(ax>UINT32_MAX||ay>UINT32_MAX||ax*ax>UINT64_MAX-ay*ay)return UINT64_MAX;
            return ax*ax+ay*ay;
        };
        const uint64_t reach=uint64_t(uint32_t(radius))+uint32_t(owner.radius_q12);
        if(owner.radius_q12&&distance_sq(owner.support.position)<=reach*reach)return true;
        // Native second-footprint test compares floor(length), not squared length.
        if(cls==13U&&owner.factory_exit&&pioneer_uint64_sqrt(distance_sq(*owner.factory_exit))<=uint32_t(radius)+0xC00U)return true;
      }
      return false;
    };
    if(blocked(point,model.radius_q12))return reject(PioneerPlacementReason::RuntimeCollision);
    const int32_t reference=retail_placement_height_q12(field,point,model.contact_kind);
    const Vec3f p{q12_to_float(point.x),q12_to_float(point.y),q12_to_float(reference)};
    if(!simulation_placement_terrain_clear(field,p,reference,uint32_t(model.radius_q12),model.contact_kind))
        return reject(PioneerPlacementReason::TerrainRejected);
    const auto cell=retail_nearest_world_to_cell(field,point.x,point.y);
    if(!cell||faction>=8U||!run.retail_scratch||
       run.retail_scratch->field_occupancy_masks.size()!=field.cells().size()||
       ((run.retail_scratch->field_occupancy_masks[size_t(cell->y)*field.width()+cell->x]>>(faction*8U))&0xF9U)==0U)
        return reject(PioneerPlacementReason::FieldBlocked);
    auto result=owners.empty()?PioneerPlacementResult{true,0,PioneerPlacementReason::Accepted}:
        pioneer_class14_support_projected(mode,padding,point,
            {model.radius_q12,model.margin_q12,static_cast<uint8_t>(model.resource_selector)},faction,owners,
            [](const auto& owner)->const auto& {return owner.support;});
    if(!result.accepted)return result;
    if(model.runtime_class==14U&&!pioneer_class14_resource_cell_eligible(field,point.x,point.y,uint32_t(model.resource_selector)))
        return reject(PioneerPlacementReason::MissingResourceDeposit);
    if(model.runtime_class==13U){
        if(!model.factory_exit_local)return reject(PioneerPlacementReason::MissingFactoryAnchor);
        const auto exit=retail_placement_anchor(point,heading,*model.factory_exit_local);
        if(blocked(exit,0xC00))return reject(PioneerPlacementReason::FactoryExitRuntimeCollision);
        if(!simulation_placement_terrain_clear(field,{q12_to_float(exit.x),q12_to_float(exit.y),p.z},reference,0xC00,model.contact_kind))
            return reject(PioneerPlacementReason::FactoryExitTerrainRejected);
    }
    return result;
}
namespace {
// These reservations create real MODEL nodes in the native world arena.
class PlacementProbeNodes {
public:
    PlacementProbeNodes(SimulationArmyPool* army_pool,SimulationLabWorldObjectPool* world_pool,
                        const SimulationLabRuntimeCatalog* catalog,const FieldGrid& field)
        :army_pool_(army_pool),world_pool_(world_pool),catalog_(catalog),field_(field){}
    bool create(uint32_t army,uint32_t faction,MovementPoint p){
        Probe probe;
        const Vec3f position{q12_to_float(p.x),q12_to_float(p.y),0.F};
        if(army_pool_&&army_pool_->enabled){
            probe.army=army_pool_->create(army,faction,position);
            if(!probe.army)return false;
        }
        if(world_pool_&&catalog_){
            SimulationLabEntity entity;
            SimulationLabSeedEntity seed;
            seed.army_asset_id=army;
            entity.army_asset_id=army;
            entity.actor.position=position;
            if(simulation_lab_initialize_runtime_entity(entity,seed,{},*catalog_,true)){
                const auto poses=simulation_lab_current_model_poses(entity,field_,*catalog_);
                const auto& nodes=*entity.runtime_model_hierarchy;
                for(size_t i=0;i<nodes.size();++i){
                    if(!nodes[i].creates_runtime_node())continue;
                    const auto slot=world_pool_->claim(
                        SimulationLabWorldObjectPool::Kind::Model,nodes[i].model_definition_id);
                    const auto& pose=poses[i];
                    const auto yaw=pose.native_angles?(*pose.native_angles)[0]:0U;
                    probe.nodes.push_back({slot,{{float_to_q12(pose.position.x),
                        float_to_q12(pose.position.y),float_to_q12(pose.position.z)},yaw}});
                }
            }
        }
        probes_.push_back(std::move(probe));
        return true;
    }
    void release(){
        auto& probe=probes_.back();
        for(auto i=probe.nodes.rbegin();i!=probe.nodes.rend();++i)
            world_pool_->release(i->first,i->second);
        if(probe.army)army_pool_->release(*probe.army);
        probes_.pop_back();
    }
private:
    struct Probe {
        std::optional<size_t> army;
        std::vector<std::pair<size_t,SimulationLabWorldPose>> nodes;
    };
    SimulationArmyPool* army_pool_;
    SimulationLabWorldObjectPool* world_pool_;
    const SimulationLabRuntimeCatalog* catalog_;
    const FieldGrid& field_;
    std::vector<Probe> probes_;
};
}

SimulationLabSiteReservation simulation_lab_reserve_special_site(
    const SimulationLabRun& run,const FieldGrid& field,std::span<const SimulationLabSiteOwner> owners,
    SimulationLabSiteModel resource,SimulationLabSiteModel storage,MovementPoint point,
    std::span<const Vec3f> base09,uint32_t faction,SimulationArmyPool* probe_pool,
    SimulationLabWorldObjectPool* world_pool,const SimulationLabRuntimeCatalog* catalog) {
    SimulationLabSiteReservation result;
    const auto distant=simulation_lab_query_site_placement(run,field,owners,resource,point,faction,7U,
        static_cast<int32_t>(run.ai_parameters[0x51U]));
    result.mode7_accepted=distant.accepted;result.clearance_q12=distant.placement_value;
    if(!distant.accepted||distant.placement_value==0U)return result;
    const auto near=simulation_lab_query_site_placement(run,field,owners,resource,point,faction,4U,0);
    result.mode4_accepted=near.accepted;if(near.accepted)return result;
    const uint32_t quantum=run.ai_parameters[28U];
    if(quantum==0U||((distant.placement_value-1U)+quantum)/quantum>=5U)return result;
    std::vector<SimulationLabSiteOwner> temporary(owners.begin(),owners.end());
    PlacementProbeNodes probes(probe_pool,world_pool,catalog,field);
    std::vector<MovementPoint> anchors;anchors.reserve(base09.size());
    for(const auto p:base09)anchors.push_back(point_xy(p));
    const bool rejected=pioneer_reserve_separated_site_chain<size_t>(point,quantum,
        [&](MovementPoint p){return pioneer_nearest_mode1_anchor(p,anchors,[&](MovementPoint q){
            return simulation_lab_query_site_placement(run,field,temporary,storage,q,faction,1U,0).accepted;});},
        [&](MovementPoint p)->std::optional<size_t>{
            if(!probes.create(storage.army_asset_id,faction,p))return {};
            temporary.push_back({{p,faction,storage.runtime_class,0U,storage.support_q12,true},storage.radius_q12,{}});
            ++result.probes_created;return temporary.size()-1U;},
        [&](size_t token){if(token+1U!=temporary.size())throw std::logic_error("reservation cleanup order");probes.release();temporary.pop_back();});
    result.accepted=!rejected;return result;
}
uint32_t simulation_lab_extractor_weight(uint32_t base,uint32_t assigned,uint32_t id,
    uint32_t rate,uint32_t supplied,uint32_t unpowered) noexcept {
    const auto divisor=assigned+3U;if(divisor==0U)return 0U;
    uint32_t weight=(base*3U)/divisor;
    const int32_t extraction=std::bit_cast<int32_t>(rate<<4U);
    if(id!=330U&&extraction!=0) {
        const int32_t demand=std::bit_cast<int32_t>(unpowered*2U+supplied);
        weight=static_cast<uint32_t>(static_cast<int64_t>(std::bit_cast<int32_t>(weight))*demand/extraction);
    }
    return weight;
}
SimulationLabSpecialSiteCluster simulation_lab_special_site_cluster(
    const SimulationLabRun& run,const FieldGrid& field,std::span<const SimulationLabSiteOwner> owners,
    SimulationLabSiteModel resource,std::optional<SimulationLabSiteModel> storage,MovementPoint point,
    std::span<const Vec3f> base09,uint32_t faction,SimulationArmyPool* probe_pool,
    SimulationLabWorldObjectPool* world_pool,const SimulationLabRuntimeCatalog* catalog) {
    SimulationLabSpecialSiteCluster result;
    const auto cell=retail_nearest_world_to_cell(field,point.x,point.y);
    const uint32_t heading=cell?retail_placement_normal_angles(field,cell->x,cell->y,false)&65535U:0U;
    const auto mode3=simulation_lab_query_site_placement(run,field,owners,resource,point,faction,3U,
        std::bit_cast<int32_t>(run.ai_parameters[0x51U]),heading);
    result.mode3_accepted=mode3.accepted;result.clearance_q12=mode3.placement_value;
    const auto quantum=run.ai_parameters[28U];
    if(quantum)result.bucket_count=((result.clearance_q12-1U)+quantum)/quantum;
    result.accepted=!retail_special_site_cluster_rejected(mode3.accepted,mode3.placement_value,quantum,[&]{
        // Native failure to find/create an ARM333 probe leaves chain rejection
        // true. In this caller that is a successful small-cluster test.
        result.chain_rejected=true;
        if(!storage)return true;
        std::vector<SimulationLabSiteOwner> temporary(owners.begin(),owners.end());
    PlacementProbeNodes probes(probe_pool,world_pool,catalog,field);
        std::vector<MovementPoint> anchors;anchors.reserve(base09.size());
        for(const auto p:base09)anchors.push_back(point_xy(p));
        result.chain_rejected=pioneer_reserve_separated_site_chain<size_t>(point,quantum,
            [&](MovementPoint p){return pioneer_nearest_mode1_anchor(p,anchors,[&](MovementPoint q){
                return simulation_lab_query_site_placement(run,field,temporary,*storage,q,faction,1U,0).accepted;});},
            [&](MovementPoint p)->std::optional<size_t>{
                if(!probes.create(storage->army_asset_id,faction,p))return {};
                temporary.push_back({{p,faction,storage->runtime_class,0U,storage->support_q12,true},storage->radius_q12,{}});
                ++result.probes_created;return temporary.size()-1U;},
            [&](size_t token){if(token+1U!=temporary.size())throw std::logic_error("cluster probe cleanup order");probes.release();temporary.pop_back();});
        return result.chain_rejected;
    });
    return result;
}

SimulationLabPioneerSiteAdmission simulation_lab_pioneer_site_admission(
    const SimulationLabRun& run,const FieldGrid& field,std::span<const SimulationLabSiteOwner> owners,
    SimulationLabSiteModel resource,std::optional<SimulationLabSiteModel> storage,MovementPoint point,
    std::span<const Vec3f> base09,uint32_t faction,SimulationArmyPool* probe_pool,
    SimulationLabWorldObjectPool* world_pool,const SimulationLabRuntimeCatalog* catalog) {
    SimulationLabPioneerSiteAdmission result;
    const auto cell=retail_nearest_world_to_cell(field,point.x,point.y);
    const uint32_t heading=cell?
        retail_placement_normal_angles(field,cell->x,cell->y,false)&65535U:0U;
    const auto admission=pioneer_site_admission(run.ai_parameters[28U],
        [&] {
            const auto mode3=simulation_lab_query_site_placement(
                run,field,owners,resource,point,faction,3U,
                std::bit_cast<int32_t>(run.ai_parameters[0x51U]),heading);
            result.mode3_accepted=mode3.accepted;
            result.clearance_q12=mode3.placement_value;
            return std::pair{mode3.accepted,mode3.placement_value};
        },
        [&] {
            result.mode0_queried=true;
            const auto mode0=simulation_lab_query_site_placement(
                run,field,owners,resource,point,faction,0U,0,heading);
            result.mode0_accepted=mode0.accepted;
            return mode0.accepted;
        },
        [&] {
            result.reserve_chain_called=true;
            // Missing ARM333 metadata reproduces a failed temporary creation:
            // the native chain returns true and the caller may score the site.
            if(!storage) {
                result.reserve_chain_rejected=true;
                return true;
            }
            std::vector<SimulationLabSiteOwner> temporary(owners.begin(),owners.end());
    PlacementProbeNodes probes(probe_pool,world_pool,catalog,field);
            std::vector<MovementPoint> anchors;anchors.reserve(base09.size());
            for(const auto p:base09)anchors.push_back(point_xy(p));
            result.reserve_chain_rejected=pioneer_reserve_separated_site_chain<size_t>(
                point,run.ai_parameters[28U],
                [&](MovementPoint p) {
                    return pioneer_nearest_mode1_anchor(p,anchors,[&](MovementPoint q) {
                        return simulation_lab_query_site_placement(
                            run,field,temporary,*storage,q,faction,1U,0).accepted;
                    });
                },
                [&](MovementPoint p)->std::optional<size_t> {
                    if(!probes.create(storage->army_asset_id,faction,p))return {};
                    temporary.push_back({{p,faction,storage->runtime_class,0U,
                                           storage->support_q12,true},
                                          storage->radius_q12,{}});
                    ++result.probes_created;
                    return temporary.size()-1U;
                },
                [&](size_t token) {
                    if(token+1U!=temporary.size())
                        throw std::logic_error("pioneer admission cleanup order");
                    probes.release();temporary.pop_back();
                });
            return result.reserve_chain_rejected;
        });
    result.disposition=admission.disposition;
    result.bucket_count=admission.bucket_count;
    return result;
}
}
