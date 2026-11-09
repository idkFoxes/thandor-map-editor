#pragma once
#include "thandor/simulation.hpp"
#include "thandor/retail_contact.hpp"
#include "thandor/pioneer_movement_update.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace thandor {
inline bool simulation_native_walker_completes_contact(const SimulationActorState& actor,uint32_t ticks){
    const auto& state=actor.articulated_native;
    if(!state.initialized||(state.flags&3U)==0U)return false;
    const size_t side=(state.flags&2U)?1U:0U;
    const auto increment=int32_t(uint32_t(state.speed)*uint32_t(state.step_rate)*ticks)>>12;
    return state.progress[side]+uint32_t(increment)>=4096U;
}
// Retained contact positions from the preceding suspension callback. Contact
// effects are spawned before the completing foot is snapped to its new anchor.
inline std::array<Vec3f,2> simulation_native_walker_contacts(
    const SimulationActorState& actor,int32_t lift,int32_t foot_offset){
    std::array<Vec3f,2> result{};
    for(size_t side=0;side<2;++side){
        const auto a=actor.articulated_foot_anchor_world[side];
        const auto b=actor.articulated_foot_target_world[side];
        const auto progress=actor.articulated_native.progress[side];
        const auto blend=[&](float start,float end){return float_to_q12(start)+
            int32_t((int64_t(float_to_q12(end)-float_to_q12(start))*progress)>>12);};
        const int32_t target_z=float_to_q12(b.z)+
            (int32_t(uint32_t(4096U-progress)*uint32_t(lift))>>10);
        const int32_t z=float_to_q12(a.z)+
            int32_t((int64_t(target_z-float_to_q12(a.z))*progress)>>12)+foot_offset;
        result[side]={q12_to_float(blend(a.x,b.x)),q12_to_float(blend(a.y,b.y)),q12_to_float(z)};
    }
    return result;
}
// Constructor-only contact initialization: no waypoint, motion or EFF callback.
template<class Terrain>
void simulation_initialize_native_walker(SimulationActorState& actor,
    const SimulationMotionProfile& motion,int32_t stride,int32_t lift,int32_t lateral,
    int32_t hip_height,int32_t foot_offset,Terrain&& terrain,bool place_root){
    auto& s=actor.articulated_native;
    using Point=std::array<int32_t,3>;
    const auto point=[](Vec3f p){return Point{float_to_q12(p.x),float_to_q12(p.y),float_to_q12(p.z)};};
    const auto world=[](Point p){return Vec3f{q12_to_float(p[0]),q12_to_float(p[1]),q12_to_float(p[2])};};
    const auto direction=[](uint32_t heading,int32_t scale){return retail_direction_scaled(0,heading,scale);};
    if(!s.initialized){
        s.initialized=true;s.start_heading=s.end_heading=actor.body_yaw_turn16;
        // ModelRuntimeSlotClassInit_InitializeSentinelBoundsAndTiming.
        const int32_t speed=motion.speed_q12_per_tick;
        s.restart_speed=stride>0&&speed>0
            ?int32_t((int64_t(int32_t((int64_t(speed)<<12)/stride))*(lift+stride))/stride)-
                int32_t((int64_t(stride)*motion.ground_acceleration_q12)/(int64_t(speed)*4))
            :0;
        s.speed=s.restart_speed;
        const auto root=point(actor.position),side=direction(actor.body_yaw_turn16+0x4000U,lateral);
        const int32_t ground=terrain(root[0],root[1]).value_or(root[2]-hip_height-foot_offset);
        for(size_t i=0;i<2;++i){const int sign=i==0?1:-1;
            const Point p{root[0]+sign*side[0],root[1]+sign*side[1],ground};
            actor.articulated_foot_anchor_world[i]=actor.articulated_foot_target_world[i]=world(p);
            actor.articulated_contact_heading_turn16[i]=actor.articulated_contact_target_heading_turn16[i]=actor.body_yaw_turn16;
        }
        actor.articulated_contacts_initialized=true;
        if(place_root)actor.position.z=q12_to_float(ground+hip_height+foot_offset);
    }
}
// ArmyRuntimeClass_UpdateArticulatedMovement and walker.cpp contact planning.
// Dependencies provide the live waypoint callback, command point, ground sample,
// footprint collision admission, reached notification and completed-foot event.
template<class Dependencies>
void simulation_tick_native_walker(SimulationActorState& actor,
    const SimulationMotionProfile& motion,int32_t stride,int32_t lift,uint32_t turn_limit,
    int32_t lateral,int32_t foot_radius,int32_t hip_height,int32_t foot_offset,
    uint32_t ticks,Dependencies& deps,bool allow_route=true){
    auto& s=actor.articulated_native;
    using Point=std::array<int32_t,3>;
    const auto point=[](Vec3f p){return Point{float_to_q12(p.x),float_to_q12(p.y),float_to_q12(p.z)};};
    const auto world=[](Point p){return Vec3f{q12_to_float(p[0]),q12_to_float(p[1]),q12_to_float(p[2])};};
    const auto length=[](Point p){return retail_uint64_sqrt(uint64_t(int64_t(p[0])*p[0])+uint64_t(int64_t(p[1])*p[1])+uint64_t(int64_t(p[2])*p[2]));};
    const auto difference=[](Point a,Point b){return Point{a[0]-b[0],a[1]-b[1],a[2]-b[2]};};
    const auto direction=[](uint32_t heading,int32_t scale){return retail_direction_scaled(0,heading,scale);};
    simulation_initialize_native_walker(actor,motion,stride,lift,lateral,hip_height,foot_offset,
        [&](int32_t x,int32_t y){return deps.terrain(x,y);},false);
    const auto anchor=[&](size_t i){return point(actor.articulated_foot_anchor_world[i]);};
    const auto target=[&](size_t i){return point(actor.articulated_foot_target_world[i]);};
    const auto rate=[&](size_t side,int lift_factor){
        const auto distance=length(difference(target(side),anchor(side)));
        const int32_t denominator=int32_t(distance)+lift*lift_factor;
        s.step_rate=denominator==0?8192:int32_t((uint32_t(stride)<<13)/int64_t(denominator));
    };
    const auto sample=[&](size_t side,uint32_t heading){
        auto p=target(side);const auto out=direction(heading+(side==0?0x4000U:0xC000U),foot_radius);
        const auto z=deps.terrain(p[0]+out[0],p[1]+out[1]);
        if(z){p[2]=*z;actor.articulated_foot_target_world[side]=world(p);
            if constexpr(requires { deps.normal(p[0]+out[0],p[1]+out[1]); })
                if(const auto normal=deps.normal(p[0]+out[0],p[1]+out[1]))s.target_ground_normals[side]=*normal;
        }
        return z.has_value();
    };
    const auto closing=[&](uint32_t heading){
        const size_t side=(s.flags&4U)?1:0;
        s.flags=(s.flags&~0x3FU)|0x40U|(side==0?5U:10U);
        s.end_heading=heading;
        actor.articulated_contact_target_heading_turn16[side]=heading;
        const auto offset=direction(heading+(side==0?0x4000U:0xC000U),lateral);
        const auto other=target(side^1U);
        // Only X/Y come from the opposite foot. A rejected terrain sample
        // leaves this foot's previous height and normal in the native record.
        const Point p{other[0]+2*offset[0],other[1]+2*offset[1],target(side)[2]};
        actor.articulated_foot_target_world[side]=world(p);
        if(sample(side,heading)){rate(side,4);s.flags&=~0x80U;}
    };
    const auto advance_waypoint=[&](){s.flags&=~0x30U;if(!deps.route().carry)deps.reached();};
    if((s.flags&3U)!=0U){
        const size_t side=(s.flags&2U)?1:0;
        const int32_t previous=s.speed;
        s.speed+=(s.progress[side]<=2048U?1:-1)*int32_t(motion.ground_acceleration_q12);
        const auto increment=int32_t(uint32_t(previous)*uint32_t(s.step_rate)*ticks)>>12;
        s.progress[side]+=uint32_t(increment);
        if(s.progress[side]>=4096U){
            s.flags&=~(1U<<side);s.progress[side]=0;
            actor.articulated_foot_anchor_world[side]=actor.articulated_foot_target_world[side];
            actor.articulated_contact_heading_turn16[side]=actor.articulated_contact_target_heading_turn16[side];
            s.ground_normals[side]=s.target_ground_normals[side];
            s.start_heading=s.end_heading;s.speed=s.restart_speed;
            actor.articulated_last_completed_side=uint32_t(side);++actor.articulated_contact_event_serial;
            deps.completed(side);
            const auto route=deps.route();bool reached=false;
            if(route.carry){
                const auto command=deps.command();
                if(!command)reached=true;
                else{const auto root=point(actor.position);const auto angle=fixed_atan2_angle16_compat(command->y-root[1],command->x-root[0]);
                    const auto error=(angle-actor.body_yaw_turn16)&65535U;reached=error<=0x800U||error>=0xF800U;}
            }else{const auto root=point(actor.position);reached=length({route.point.x-root[0],route.point.y-root[1],0})<=64U;}
            if(reached)advance_waypoint();
        }
    }else if(s.flags&0x10U){
        closing(((s.flags>>16)+s.end_heading)&65535U);
    }else{
        const auto route=allow_route?deps.route():MovementResult{{},true};const auto root=point(actor.position);
        uint32_t distance=0,heading=actor.body_yaw_turn16;bool has_heading=allow_route;
        if(!allow_route){}
        else if(route.carry){const auto command=deps.command();distance=UINT32_MAX;
            if(command)heading=fixed_atan2_angle16_compat(command->y-root[1],command->x-root[0]);else has_heading=false;
        }else{const int32_t dx=route.point.x-root[0],dy=route.point.y-root[1];distance=length({dx,dy,0});
            if(dx||dy)heading=fixed_atan2_angle16_compat(dy,dx);}
        const uint32_t error=(heading-actor.body_yaw_turn16)&65535U;
        const uint32_t walk_angle=(s.flags&0x20U)?0x200U:0x2000U;
        bool planned=false;
        if(has_heading&&int32_t(distance)>0&&(error<=walk_angle||error>=65536U-walk_angle)){
            const size_t side=(s.flags&4U)?1:0;
            s.flags=(s.flags&0xFFA0U)|0x20U|(side==0?5U:10U);
            s.end_heading=heading;actor.articulated_contact_target_heading_turn16[side]=heading;
            const auto offset=direction(heading+(side==0?0x4000U:0xC000U),lateral);
            const auto opposite=target(side^1U);
            const uint32_t reach=length({opposite[0]+offset[0]-root[0],opposite[1]+offset[1]-root[1],0})+uint32_t(stride);
            Point p{route.point.x,route.point.y,target(side)[2]};
            if(reach<distance){const auto ahead=direction(heading,int32_t(reach));p[0]=root[0]+ahead[0];p[1]=root[1]+ahead[1];}
            else{const auto current=deps.route();p[0]=current.point.x;p[1]=current.point.y;}
            p[0]+=offset[0];p[1]+=offset[1];actor.articulated_foot_target_world[side]=world(p);
            if(sample(side,heading)&&deps.free(p[0],p[1])){s.flags&=~0x80U;rate(side,2);}
            else{
                const auto other=anchor(side^1U);
                p={other[0]+2*offset[0],other[1]+2*offset[1],target(side)[2]};
                actor.articulated_foot_target_world[side]=world(p);
                if(sample(side,heading)){deps.reached();if(s.flags&0x80U)s.flags&=~3U;s.flags|=0x80U;rate(side,2);}
            }
            planned=true;
        }else if(has_heading&&(s.flags&0x20U)){
            closing(((s.flags>>16)+s.end_heading)&65535U);planned=true;
        }else if(has_heading&&distance<=64U){advance_waypoint();planned=true;
        }else if(has_heading&&(int32_t(distance)>=0||(error>0x800U&&error<0xF800U))){
            const size_t side=error<=0x8000U?0:1;
            const int32_t turn=side==0?int32_t(std::min(error,turn_limit)):-int32_t(std::min(65536U-error,turn_limit));
            const int32_t pivot_distance=int32_t((int64_t(lateral)*6144)>>12);
            const auto back=direction(s.start_heading+0x8000U,pivot_distance);
            const uint32_t foot_heading=(s.start_heading+uint32_t(turn))&65535U;
            const auto forward=direction(foot_heading,pivot_distance),offset=direction(foot_heading+(side==0?0x4000U:0xC000U),lateral);
            const Point p{root[0]+back[0]+forward[0]+offset[0],root[1]+back[1]+forward[1]+offset[1],target(side)[2]};
            actor.articulated_foot_target_world[side]=world(p);actor.articulated_contact_target_heading_turn16[side]=foot_heading;
            s.end_heading=(s.start_heading+uint32_t(turn>>1))&65535U;
            s.flags=(s.flags&0xFFFFU)|(uint32_t(turn)<<15);
            sample(side,foot_heading);rate(side,4);
            // Native's contact planner uses <= half-turn, while its caller
            // chooses the running-step flag with < half-turn. At exactly 180
            // degrees it plans the left target but advances the right contact.
            s.flags=(s.flags&~0x5FU)|0x10U|(error<0x8000U?5U:10U);planned=true;
        }
        if(!planned&&!(s.flags&0x40U)){
            const auto left=anchor(0),right=anchor(1);
            const auto angle=(fixed_atan2_angle16_compat(left[1]-right[1],left[0]-right[0])-actor.body_yaw_turn16-0x4000U)&65535U;
            if(angle>=0x1000U&&((angle<0x7000U)||(angle>=0x9000U&&angle<0xF000U)))closing(((s.flags>>16)+s.end_heading)&65535U);
        }
    }
    std::array<Point,2> contacts{};
    for(size_t side=0;side<2;++side){const auto a=anchor(side),b=target(side);const auto progress=s.progress[side];
        contacts[side][0]=a[0]+int32_t((int64_t(b[0]-a[0])*progress)>>12);
        contacts[side][1]=a[1]+int32_t((int64_t(b[1]-a[1])*progress)>>12);
        const int32_t z=b[2]+(int32_t(uint32_t(4096U-progress)*uint32_t(lift))>>10);
        contacts[side][2]=a[2]+int32_t((int64_t(z-a[2])*progress)>>12);
    }
    actor.position=world({(contacts[0][0]+contacts[1][0])>>1,(contacts[0][1]+contacts[1][1])>>1,
        ((contacts[0][2]+contacts[1][2])>>1)+foot_offset+hip_height});
    const int32_t delta=int16_t(uint16_t(s.end_heading-s.start_heading));
    actor.body_yaw_turn16=(s.start_heading+uint32_t((int64_t(delta)*(s.progress[0]+s.progress[1]))>>12))&65535U;
    actor.ground_advance_q12=uint32_t(s.speed);actor.articulated_contact_step_q12=s.step_rate;
    actor.articulated_swing_side=(s.flags&2U)?1U:0U;
    actor.articulated_contact_progress_q12=s.progress[actor.articulated_swing_side];
    actor.articulated_contact_transition_active=(s.flags&3U)!=0;
    actor.articulated_pose_transition_active=actor.articulated_contact_transition_active;
    actor.articulated_contact_settling=false;actor.articulated_turning=(s.flags&0x10U)!=0;
}
} // namespace thandor
