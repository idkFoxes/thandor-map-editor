#pragma once
#include "thandor/simulation_lab_geometry.hpp"
#include "thandor/fog_preview.hpp"
#include "thandor/retail_placement.hpp"
#include <array>
#include <bit>
#include <span>

namespace thandor {
// TerrainHeightBand_TestAroundWorldPoint / TerrainAuxHeightThreshold_TestAroundWorldPoint.
// Preserve the six-sector scan's 4/7 step progression, with bounded editable-grid reads.
inline bool simulation_placement_terrain_clear(const FieldGrid& field,Vec3f point,
    int32_t reference_height_q12,uint32_t radius_q12,int32_t contact_kind) {
    const auto center=retail_fog_world_to_cell(field,float_to_q12(point.x),float_to_q12(point.y));
    if(!center)return false;
    const auto fails=[&](int x,int y,bool center_cell=false) {
        if(x<0||y<0||x>=int(field.width())||y>=int(field.height()))return true;
        const auto& cell=field.at(uint32_t(x),uint32_t(y));
        if((cell.flags_and_material&0x88006000U)!=0U)return true;
        if(contact_kind==1)return cell.water_delta_q12<0 ||
            (int32_t(retail_placement_normal_angles(field,uint32_t(x),uint32_t(y),!center_cell))>>16)<0x3000;
        const int64_t delta=int64_t(cell.terrain_height_q12)-reference_height_q12;
        return cell.water_delta_q12>0||delta<-0x400||delta>0x400;
    };
    if(fails(int(center->x),int(center->y),true))return false;
    constexpr std::array<std::array<int,2>,6> directions{{{1,0},{1,-1},{0,-1},{-1,0},{-1,1},{0,1}}};
    const uint32_t limit=std::clamp(radius_q12/0x240U,1U,255U);
    const auto leg=[&](uint32_t step,int x,int y,const auto& direction) {
        for(;step<limit;step+=4U,x+=direction[0],y+=direction[1])if(fails(x,y))return false;
        return true;
    };
    for(size_t sector=0;sector<6;++sector) {
        const auto a=directions[sector],b=directions[(sector+1U)%6U];
        int x=int(center->x)+a[0],y=int(center->y)+a[1];
        for(uint32_t step=0;step<limit;) {
            if(fails(x,y)||!leg(step+4U,x+a[0],y+a[1],a))return false;
            if(limit<=step+4U)break;
            const int bx=x+b[0],by=y+b[1];
            if(fails(bx,by))return false;
            step+=7U;
            if(!leg(step,bx+b[0],by+b[1],b))return false;
            x=bx+a[0];y=by+a[1];
        }
    }
    return true;
}

inline bool simulation_placement_factory_anchor_overlap(Vec3f point,int32_t query_radius_q12,
    const SimulationLabEntity& factory,const FieldGrid& field) {
    if(query_radius_q12==0||factory.runtime_removed||
       factory.runtime_model_class_id!=13U||!factory.production_exit_local)return false;
    const auto anchor=simulation_lab_factory_world_point(field,factory.actor.position,
        factory.actor.body_yaw_turn16,factory.motion.placement_contact_kind,
        factory.production_root_sprite_height_offset,*factory.production_exit_local);
    const int32_t dx=std::bit_cast<int32_t>(uint32_t(float_to_q12(point.x))-uint32_t(float_to_q12(anchor.x)));
    const int32_t dy=std::bit_cast<int32_t>(uint32_t(float_to_q12(point.y))-uint32_t(float_to_q12(anchor.y)));
    const uint64_t distance=uint64_t(int64_t(dx)*dx)+uint64_t(int64_t(dy)*dy);
    // ArmyRuntime_HasCircleCollisionAtWorldPoint uses floor(length) for class13's exit.
    return pioneer_uint64_sqrt(distance)<=uint32_t(query_radius_q12)+0xC00U;
}

// Class13 placement callback, 0x5244B0: root admission is the caller's common test.
// Rotate SPR point (1,5); require 0xC00 clearance there from runtime circles,
// existing factory exit anchors, and the contact-specific terrain scan.
inline bool simulation_placement_factory_exit_clear(const FieldGrid& field,Vec3f position,
    uint32_t yaw,const SimulationLabSeedEntity& seed,std::span<const SimulationLabEntity> owners) {
    if(seed.runtime_model_class_id!=13U||!seed.production_exit_local)return true; // metadata-free fixture
    const auto local=*seed.production_exit_local;
    const auto xy=retail_placement_anchor({float_to_q12(position.x),float_to_q12(position.y)},yaw,
        {float_to_q12(local.x),float_to_q12(local.y)});
    const Vec3f anchor{q12_to_float(xy.x),q12_to_float(xy.y),position.z};
    for(const auto& owner:owners) {
        if(owner.runtime_removed||!owner.construction_complete||
           owner.runtime_model_class_id==0U||owner.runtime_model_class_id==12U)continue;
        const auto r=owner.motion.collision_radius_q12;
        const double dx=double(float_to_q12(anchor.x))-float_to_q12(owner.actor.position.x);
        const double dy=double(float_to_q12(anchor.y))-float_to_q12(owner.actor.position.y);
        const double radius=double(r)+0xC00;
        if(r!=0&&dx*dx+dy*dy<=radius*radius)return false;
        if(simulation_placement_factory_anchor_overlap(anchor,0xC00,owner,field))return false;
    }
    return simulation_placement_terrain_clear(field,anchor,float_to_q12(position.z),0xC00,
        seed.motion.placement_contact_kind);
}

inline bool simulation_placement_runtime_clear(Vec3f point,int32_t radius_q12,
    std::span<const SimulationLabEntity> owners,const FieldGrid& field){
    if(radius_q12==0)return true;
    for(const auto& owner:owners){
        if(owner.runtime_removed||!owner.construction_complete||
           owner.runtime_model_class_id==0U||owner.runtime_model_class_id==12U)continue;
        const auto r=owner.motion.collision_radius_q12;
        const double dx=double(float_to_q12(point.x))-float_to_q12(owner.actor.position.x);
        const double dy=double(float_to_q12(point.y))-float_to_q12(owner.actor.position.y);
        const double radius=double(r)+radius_q12;
        if(r!=0&&dx*dx+dy*dy<=radius*radius)return false;
        if(simulation_placement_factory_anchor_overlap(point,radius_q12,owner,field))return false;
    }
    return true;
}
} // namespace thandor
