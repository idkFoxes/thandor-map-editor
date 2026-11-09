#include "thandor/simulation_storage.hpp"
#include "thandor/simulation_native_walker.hpp"
#include "thandor/simulation_lab_shot_rays.hpp"
#include "thandor/simulation_lab_geometry.hpp"
#include "thandor/simulation_lab_visual.hpp"
#include "thandor/simulation_lab_live_aim_nodes.hpp"
#include "thandor/simulation_lab_derived_selection_metrics.hpp"
#include "thandor/pioneer_resource_grid.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace thandor {
namespace {
void release_effect_world_slot(SimulationLabRun& run,SimulationLabEnvironmentalEffect& effect){
    if(effect.native_world_slot==kSimulationLabNoWorldSlot)return;
    const auto& pose=effect.pose;
    const auto yaw=pose.native_angles?(*pose.native_angles)[0]:0U;
    const SimulationLabWorldPose retained{{
        float_to_q12(pose.position.x),float_to_q12(pose.position.y),float_to_q12(pose.position.z)},yaw};
    run.native_world_objects.release(effect.native_world_slot,retained);
    effect.native_world_slot=kSimulationLabNoWorldSlot;
}
void append_environment_effect_sound(SimulationLabRun& run,uint32_t id,
    const EffectDefinitionRecord& definition,Vec3f position){
    if(!definition.positioned_sound_table_index())return;
    SimulationLabOneShotEvent event;event.sequence=run.next_one_shot_event_sequence++;
    event.tick=run.tick;event.kind=SimulationLabOneShotEventKind::PositionedEffectSound;
    event.source_placement_index=SIZE_MAX;event.effect_id=id;event.root_world_position=position;
    run.one_shot_event_tail=std::make_shared<const SimulationLabOneShotEventNode>(
        std::move(event),run.one_shot_event_tail);
}
Vec3f difference(Vec3f a,Vec3f b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
float dot(Vec3f a,Vec3f b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3f cross(Vec3f a,Vec3f b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Vec3f point(const SimulationLabEffectPose& p,Vec3f v){
    if(p.native_basis){
        const auto& basis=*p.native_basis;
        const std::array<int32_t,3> xyz{float_to_q12(v.x),float_to_q12(v.y),float_to_q12(v.z)};
        const auto coordinate=[&](size_t row,float origin){
            const auto delta=(int64_t(basis[row])*xyz[0]+int64_t(basis[row+1])*xyz[1]+
                              int64_t(basis[row+2])*xyz[2])>>28;
            return q12_to_float(float_to_q12(origin)+static_cast<int32_t>(delta));
        };
        return {coordinate(0,p.position.x),coordinate(3,p.position.y),coordinate(6,p.position.z)};
    }
    return {p.position.x+p.basis[0]*v.x+p.basis[1]*v.y+p.basis[2]*v.z,
            p.position.y+p.basis[3]*v.x+p.basis[4]*v.y+p.basis[5]*v.z,
            p.position.z+p.basis[6]*v.x+p.basis[7]*v.y+p.basis[8]*v.z};
}
SimulationLabEffectPose rotation(uint32_t a2,uint32_t a1,uint32_t a0,bool fixed_points=false){
    // Same FixedTransform field permutation as the editor's model_transform.hpp.
    SimulationLabEffectPose p;const auto fixed=retail_model_basis_q28(a2,a1,a0);
    if(fixed_points){
        p.native_angles=std::array<uint32_t,3>{a2,a1,a0};
        p.native_basis=fixed;
    }
    for(size_t i=0;i<9;++i)p.basis[i]=float(fixed[i])/268435456.F;
    return p;
}
// Pool arguments and stored world angles are integer fields. Preserve native
// angles directly; an Euler round trip through floats can change a launch ray.
template<class Draw>
SimulationLabEffectPose timed_effect_pose(SimulationLabEffectPose pose,
    const EffectDefinitionRecord& definition,Draw&& draw){
    std::array<uint32_t,3> angles{}; // worldRotationAngle0,1,2
    if(pose.native_angles){const auto& a=*pose.native_angles;angles={a[2],a[1],a[0]};}
    else{
        const float pitch=std::asin(std::clamp(pose.basis[8],-1.F,1.F));
        const float roll=std::abs(std::cos(pitch))<1.e-6F?0.F:std::atan2(pose.basis[5],pose.basis[2]);
        const float yaw=std::abs(std::cos(pitch))<1.e-6F?std::atan2(pose.basis[3],pose.basis[0]):
            std::atan2(pose.basis[7],-pose.basis[6])+roll;
        angles={radians_to_turn16(roll),radians_to_turn16(pitch),radians_to_turn16(yaw)};
    }
    if(definition.transition_kind==2U){const auto random=draw();
        angles={random&0xFFFFU,0x4000U-(random>>20U),random&0xFFFFU};}
    if(definition.creation_flags&1U)angles[2]=draw()&0xFFFFU;
    const auto position=pose.position;
    pose=rotation(angles[2],angles[1],angles[0],true);
    pose.position={q12_to_float(float_to_q12(position.x)),q12_to_float(float_to_q12(position.y)),
        q12_to_float(float_to_q12(position.z))};
    return pose;
}
SimulationLabEffectPose compose(const SimulationLabEffectPose& a,const SimulationLabEffectPose& b){
    SimulationLabEffectPose p;p.position=point(a,b.position);
    for(size_t y=0;y<3;++y)for(size_t x=0;x<3;++x){p.basis[y*3+x]=0;
        for(size_t k=0;k<3;++k)p.basis[y*3+x]+=a.basis[y*3+k]*b.basis[k*3+x];}return p;
}
std::array<Vec3f,2> native_walker_geometry(
    const SimulationLabEntity& entity,const SimulationLabRuntimeCatalog& catalog,
    std::vector<SimulationLabEffectPose>* posed_models){
    const auto fallback=simulation_native_walker_contacts(entity.actor,entity.articulated_contact_lift_q12,
        float_to_q12(entity.articulated_shared_foot_placement_offset));
    if(!entity.runtime_model_hierarchy||!entity.actor.articulated_native.initialized)return fallback;
    const auto& nodes=*entity.runtime_model_hierarchy;
    using Point=std::array<int32_t,3>;
    const auto child=[&](uint32_t parent,uint32_t slot){
        for(uint32_t n=0;n<nodes.size();++n)if(nodes[n].parent_index==parent&&nodes[n].child_slot==slot)return n;
        return UINT32_MAX;
    };
    std::array<std::array<uint32_t,4>,2> chains{};
    for(size_t side=0;side<2;++side){auto parent=0U;
        for(size_t depth=0;depth<4;++depth){parent=child(parent,depth==0?uint32_t(side):0U);
            if(parent==UINT32_MAX)return fallback;
            chains[side][depth]=parent;}}
    struct Pose {Point position{};std::array<int32_t,9> basis{};};
    std::vector<Pose> world(nodes.size());std::vector<Point> translations(nodes.size());
    std::vector<std::array<uint32_t,3>> angles(nodes.size());
    for(size_t n=0;n<nodes.size();++n){
        angles[n]={nodes[n].local_rotation_angle2,nodes[n].local_rotation_angle1,nodes[n].local_rotation_angle0};
        if(nodes[n].parent_index<n){const auto sprite=catalog.sprites.find(nodes[nodes[n].parent_index].sprite_path);
            if(sprite==catalog.sprites.end()||nodes[n].child_slot>=6U||!sprite->second.child_translations_q12[nodes[n].child_slot])return fallback;
            translations[n]=*sprite->second.child_translations_q12[nodes[n].child_slot];}
    }
    const auto root_heading=entity.actor.body_yaw_turn16;
    angles[0][0]=root_heading;
    const auto rebuild=[&](){for(size_t n=0;n<nodes.size();++n){
        const auto basis=retail_model_basis_q28(angles[n][0],angles[n][1],angles[n][2]);
        if(n==0){world[n].basis=basis;const auto p=entity.actor.position;
            world[n].position={float_to_q12(p.x),float_to_q12(p.y),float_to_q12(p.z)};continue;}
        const auto parent=nodes[n].parent_index;if(parent>=n)continue;
        world[n].basis=retail_compose_model_basis_q28(world[parent].basis,basis);
        for(size_t axis=0;axis<3;++axis){const auto& b=world[parent].basis;const auto& p=translations[n];
            world[n].position[axis]=world[parent].position[axis]+int32_t(
                (int64_t(b[3*axis])*p[0]+int64_t(b[3*axis+1])*p[1]+int64_t(b[3*axis+2])*p[2])>>28);}
    }};
    const auto length=[](Point p){return int32_t(retail_uint64_sqrt(uint64_t(int64_t(p[0])*p[0])+
        uint64_t(int64_t(p[1])*p[1])+uint64_t(int64_t(p[2])*p[2])));};
    rebuild();
    const auto thigh=length(translations[chains[0][2]]),shin=length(translations[chains[0][3]]);
    const auto radius=translations[chains[0][1]][1];
    for(size_t side=0;side<2;++side){const auto chain=chains[side];
        const Point contact{float_to_q12(fallback[side].x),float_to_q12(fallback[side].y),float_to_q12(fallback[side].z)};
        const auto hip=world[chain[0]].position;
        const int32_t dx=contact[0]-hip[0],dy=contact[1]-hip[1];
        const auto planar=length({dx,dy,0});
        const auto relative=(fixed_atan2_angle16_compat(dy,dx)-root_heading)&0xFFFFU;
        int32_t hip_yaw=int16_t(uint16_t(relative));
        if(relative>=0x4000U&&relative<=0xC000U)hip_yaw=int16_t(uint16_t(relative-0x8000U));
        if(planar<320)hip_yaw=planar<64?0:(hip_yaw*(planar-64))>>8;
        angles[chain[0]][0]=(uint32_t(hip_yaw)+(side?0x8000U:0U))&0xFFFFU;rebuild();
        const auto offset=retail_direction_scaled(0,(root_heading+0x4000U+uint32_t(hip_yaw))&0xFFFFU,radius);
        const auto upper=world[chain[1]].position;
        const int sign=side?-1:1;
        const Point delta{contact[0]+sign*offset[0]-upper[0],contact[1]+sign*offset[1]-upper[1],contact[2]-upper[2]};
        const auto distance=length(delta);
        int32_t elevation=int32_t(fixed_atan2_angle16_compat(delta[2],length({delta[0],delta[1],0})))+0x4000;
        const auto azimuth=(fixed_atan2_angle16_compat(delta[1],delta[0])-root_heading)&0xFFFFU;
        if(azimuth>=0x4000U&&azimuth<=0xC000U)elevation=-elevation;
        int32_t joint0=uint32_t(shin)<uint32_t(distance)?0:0x8000,joint1=joint0;
        if(distance>16){const int64_t s0=int64_t(shin)*shin,s1=int64_t(thigh)*thigh,s2=int64_t(distance)*distance;
            const int64_t projection=(s0-s1)/distance;
            const auto height_squared=-projection*projection-s2+2*s1+2*s0;
            if(height_squared>=0){const auto height=int32_t(retail_uint64_sqrt(uint64_t(height_squared)))>>1;
                const auto first=fixed_atan2_angle16_compat(height,int32_t((s2-s1+s0)/distance)>>1);
                joint0=int32_t(fixed_atan2_angle16_compat(height,int32_t((s1+s2-s0)/distance)>>1));
                joint1=joint0+int32_t(first);
            }}
        const int32_t bend=joint0-elevation;
        angles[chain[1]][2]=side?(bend<0?0U:0x8000U):(bend<0?0x8000U:0U);
        angles[chain[1]][1]=uint32_t(bend<0?bend+0x4000:0x4000-bend)&0xFFFFU;
        angles[chain[2]][1]=uint32_t(0x4000-joint1)&0xFFFFU;
        angles[chain[3]]={0U,0x4000U,0U};rebuild();
    }
    if(posed_models&&posed_models->size()==nodes.size()) {
        // The foot's desired world orientation is converted back into its
        // current local frame using the native cofactor inverse, then rebuilt.
        for(size_t side=0;side<2;++side) {
            const auto foot=chains[side][3];
            const auto current_angles=retail_extract_model_angles(world[foot].basis);
            const auto b=retail_model_basis_q28(current_angles[0],current_angles[1],current_angles[2]);
            std::array<int32_t,9> inverse{};
            const auto product=[&](size_t a,size_t c,size_t d,size_t e){return int32_t((int64_t(b[a])*b[c]-int64_t(b[d])*b[e])>>28);};
            inverse={product(4,8,5,7),product(2,7,1,8),product(1,5,2,4),
                     product(5,6,3,8),product(0,8,2,6),product(2,3,0,5),
                     product(3,7,4,6),product(1,6,0,7),product(0,4,1,3)};
            const auto& state=entity.actor.articulated_native;
            const auto progress=state.progress[side];
            const auto a=retail_direction_scaled(state.ground_normals[side]>>16,state.ground_normals[side]&65535U,1<<28);
            const auto z=retail_direction_scaled(state.target_ground_normals[side]>>16,state.target_ground_normals[side]&65535U,1<<28);
            Point blended{};
            for(size_t axis=0;axis<3;++axis)blended[axis]=int32_t((int64_t(z[axis])*progress)>>12)+
                int32_t((int64_t(a[axis])*(4096U-progress))>>12);
            const auto elevation=fixed_atan2_angle16_compat(blended[2],length({blended[0],blended[1],0}));
            const auto azimuth=fixed_atan2_angle16_compat(blended[1],blended[0])&65535U;
            const auto start=entity.actor.articulated_contact_heading_turn16[side];
            const auto target=entity.actor.articulated_contact_target_heading_turn16[side];
            // The left foot scales an unsigned sign-extended delta; the right
            // foot scales the same delta as a signed dword, as in walker.cpp.
            const int32_t delta=int16_t(uint16_t(target-start));
            const uint32_t scaled=side==0?
                (uint32_t(delta)*progress)>>12:uint32_t((int64_t(delta)*progress)>>12);
            const auto desired=retail_model_basis_q28((start+scaled)&65535U,elevation,azimuth);
            angles[foot]=retail_extract_model_angles(retail_compose_model_basis_q28(inverse,desired));
        }
        rebuild();
        for(size_t n=1;n<nodes.size();++n) {
            auto ancestor=uint32_t(n);bool leg=false;
            for(size_t hops=0;ancestor<nodes.size()&&hops<nodes.size();++hops){
                if(ancestor==chains[0][0]||ancestor==chains[1][0]){leg=true;break;}
                ancestor=nodes[ancestor].parent_index;
            }
            if(!leg)continue;
            auto& pose=(*posed_models)[n];const auto position=world[n].position;
            pose.position={q12_to_float(position[0]),q12_to_float(position[1]),q12_to_float(position[2])};
            pose.native_basis=world[n].basis;pose.native_angles=retail_extract_model_angles(world[n].basis);
            for(size_t axis=0;axis<9;++axis)pose.basis[axis]=float(world[n].basis[axis])/268435456.F;
        }
    }
    std::array<Vec3f,2> result{};
    for(size_t side=0;side<2;++side){const auto p=world[chains[side][3]].position;
        result[side]={q12_to_float(p[0]),q12_to_float(p[1]),q12_to_float(p[2])};}
    return result;
}

std::vector<SimulationLabEffectPose> poses(std::span<const ModelVisualNode> hierarchy,Vec3f position,
    uint32_t yaw,const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog,
    const SimulationLabEntity* entity=nullptr) {
    // Every authored native model uses Q28 transforms and Q12 points. Scenery
    // and factory emitters must not take a float-only path: a one-Q12 launch
    // error can change a later terrain contact and the shared RNG sequence.
    constexpr bool fixed_geometry=true;
    std::vector<SimulationLabEffectPose> result(hierarchy.size());
    std::vector<std::array<int32_t,9>> fixed_bases(fixed_geometry?hierarchy.size():0);
    const auto store_basis=[&](size_t i,const std::array<int32_t,9>& basis){
        fixed_bases[i]=basis;
        result[i].native_basis=basis;
        for(size_t j=0;j<basis.size();++j)result[i].basis[j]=float(basis[j])/268435456.F;
    };
    std::vector<SimulationWeaponProfile> profiles;
    if(entity)for(const auto& weapon:entity->combat_weapons)profiles.push_back(weapon.profile);
    for(size_t i=0;i<hierarchy.size();++i){
        const auto& n=hierarchy[i];const auto s=catalog.sprites.find(n.sprite_path);
        if(n.parent_index==UINT32_MAX){
            const float offset=s==catalog.sprites.end()?0.F:s->second.height_offset;
            uint32_t root_yaw=yaw;
            if(entity)for(size_t w=0;w<profiles.size()&&w<entity->actor.weapons.size();++w)
                if(profiles[w].yaw_model_node_index==i)root_yaw=wrap_turn16(static_cast<int64_t>(root_yaw)+entity->actor.weapons[w].yaw.angle_turn16);
            const auto world=[&](Vec3f v){return simulation_lab_factory_world_point(field,position,root_yaw,
                n.simulation.placement_contact_kind,offset,v,n.simulation.placement_height_offset_q12);};
            auto& p=result[i];p.position=world({});
            for(size_t axis=0;axis<3;++axis){Vec3f v{};if(axis==0)v.x=1;else if(axis==1)v.y=1;else v.z=1;
                v=difference(world(v),p.position);p.basis[axis]=v.x;p.basis[3+axis]=v.y;p.basis[6+axis]=v.z;}
            if(fixed_geometry){
                auto normal=entity?entity->placement_normal_angles:std::nullopt;
                if(!normal&&n.simulation.placement_contact_kind==2)
                    normal=retail_contact_normal_angles(field,float_to_q12(position.x),float_to_q12(position.y));
                p.native_angles=std::array<uint32_t,3>{root_yaw,normal.value_or(0x40000000U)>>16,
                                                       normal.value_or(0U)&0xFFFFU};
            }
            if(entity&&entity->actor.ground_recoil_angles){
                const auto& angles=*entity->actor.ground_recoil_angles;
                if(fixed_geometry)p.native_angles=angles;
                else p.basis=rotation(angles[0],angles[1],angles[2]).basis;
            }
            if(p.native_angles){
                const auto& angles=*p.native_angles;
                store_basis(i,retail_model_basis_q28(angles[0],angles[1],angles[2]));
            }
            // Airborne/current suspension height belongs to the live actor, not terrain contact.
            if((entity&&!entity->runtime_model_node_height_offsets_q12.empty()) ||
               n.simulation.placement_contact_kind==3)p.position.z=position.z;
            // Sinking translates the finished wreck pose. Applying it before
            // basis construction shears the matrix, and the cached root height
            // would then overwrite the vertical displacement.
            if(entity&&entity->runtime_model_class_id==12U)
                p.position.z+=q12_to_float(entity->class12_vertical_offset_q12);
        }else if(n.parent_index<i){
            std::array<uint32_t,3> local_angles{n.local_rotation_angle2,n.local_rotation_angle1,n.local_rotation_angle0};
            int32_t recoil_translation=0;std::optional<uint32_t> vertical_tick;
            if(entity){const bool producer=entity->runtime_model_class_id==11U||entity->runtime_model_class_id==13U||entity->runtime_model_class_id==22U;
                uint32_t resource_tick=0;bool resource_animation=false;
                for(const auto& c:entity->model_components)
                    if(c.source_model_node_index==n.parent_index&&
                       (c.runtime_class_id==4U||c.runtime_class_id==10U||c.runtime_class_id==14U)){
                        resource_tick=c.animated_subnode_ticks;resource_animation=true;break;
                    }
                const auto live=simulation_runtime_child_pose_v602(hierarchy[n.parent_index],n,i,profiles,entity->actor,
                    resource_animation,resource_tick,producer?std::optional<uint32_t>{entity->production_animated_subnode_ticks}:std::nullopt);
                local_angles={live.angle2,live.angle1,live.angle0};recoil_translation=live.recoil_translation_q12;vertical_tick=live.vertical_animation_tick;}
            if(entity&&entity->runtime_model_class_id==17U&&n.child_slot<3U&&
               hierarchy[n.parent_index].parent_index==UINT32_MAX)
                local_angles[1]=(local_angles[1]+entity->actor.class17_rotor_tilt_turn16)&0xFFFFU;
            auto local=rotation(local_angles[0],local_angles[1],local_angles[2]);
            const auto parent=catalog.sprites.find(hierarchy[n.parent_index].sprite_path);
            if(parent!=catalog.sprites.end()&&n.child_slot<6U)
                if(const auto& xyz=parent->second.child_translations_q12[n.child_slot])
                    local.position={q12_to_float((*xyz)[0]),q12_to_float((*xyz)[1]),q12_to_float((*xyz)[2])};
            if(vertical_tick){const auto& profile=hierarchy[n.parent_index].simulation;
                local.position.z=q12_to_float(model_runtime_child2_translation_q12(float_to_q12(local.position.z),
                    profile.pitch_min_turn16,profile.pitch_max_turn16,profile.pitch_max_velocity_turn16,*vertical_tick));
            }
            if(entity&&n.child_slot==0U)for(const auto& c:entity->model_components)
                if(c.source_model_node_index==n.parent_index&&c.storage_child_z_q12)
                    local.position.z=q12_to_float(*c.storage_child_z_q12);
            if(entity && hierarchy[n.parent_index].parent_index==UINT32_MAX && n.child_slot==0) {
                if(entity->runtime_model_class_id==23U)
                    local.position.z+=q12_to_float(entity->vertical_deployment_child_local_z_q12);
                if(entity->runtime_model_class_id==22 && entity->airport_initialized)
                    local.position.z=q12_to_float(entity->airport.platform_local_z_q12);
                if(entity->runtime_model_class_id==21 && entity->aircraft.initialized) {
                    const auto translation=local.position;
                    local=rotation(n.local_rotation_angle2,entity->aircraft.body_pitch,0x8000U);
                    local.position=translation;local.position.z=q12_to_float(entity->aircraft.body_height);
                }
            }
            if(entity)for(size_t w=0;w<profiles.size()&&w<entity->actor.weapons.size();++w){
                const auto& weapon=profiles[w];
                const auto& state=entity->actor.weapons[w];
                if(weapon.runtime_class_id==8U){
                    const auto node=std::find(weapon.launch_model_node_indices.begin(),weapon.launch_model_node_indices.end(),i);
                    if(node!=weapon.launch_model_node_indices.end()){
                        const auto barrel=size_t(node-weapon.launch_model_node_indices.begin());
                        if(barrel<state.alternate_barrel_translations_q12.size()&&state.alternate_barrel_translations_q12[barrel]){
                            const auto& xyz=*state.alternate_barrel_translations_q12[barrel];
                            local.position={q12_to_float(xyz[0]),q12_to_float(xyz[1]),q12_to_float(xyz[2])};
                        }
                    }
                    continue;
                }
                if(weapon.runtime_class_id!=7U||weapon.launch_model_node_index!=i)continue;
                recoil_translation-=state.recoil_translation_offset_q12;
                if(state.barrel_translation_q12){
                    const auto& xyz=*state.barrel_translation_q12;
                    local.position={q12_to_float(xyz[0]),q12_to_float(xyz[1]),q12_to_float(xyz[2])};
                }else{
                    const auto x=float_to_q12(local.position.x),y=float_to_q12(local.position.y);
                    const auto z=float_to_q12(local.position.z);
                    const auto length=retail_uint64_sqrt(uint64_t(int64_t(x)*x)+uint64_t(int64_t(y)*y));
                    const auto step=retail_direction_scaled(
                        fixed_atan2_angle16_compat(z,static_cast<int32_t>(length)),
                        fixed_atan2_angle16_compat(y,x),weapon.recoil_translation_q12);
                    const auto ticks=int64_t(state.recoil_translation_ticks);
                    local.position={q12_to_float(static_cast<int32_t>(x-ticks*step[0])),
                        q12_to_float(static_cast<int32_t>(y-ticks*step[1])),
                        q12_to_float(static_cast<int32_t>(z-ticks*step[2]))};
                }
            }
            if(recoil_translation){const auto shifted=point(local,{q12_to_float(recoil_translation),0,0});local.position=shifted;}
            result[i]=compose(result[n.parent_index],local);
            if(fixed_geometry){
                const auto& parent_pose=result[n.parent_index];
                const auto& parent_basis=fixed_bases[n.parent_index];
                const auto local_basis=retail_model_basis_q28(local_angles[0],local_angles[1],local_angles[2]);
                store_basis(i,retail_compose_model_basis_q28(parent_basis,local_basis));
                result[i].native_angles=retail_extract_model_angles(fixed_bases[i]);
                const int32_t x=float_to_q12(local.position.x),y=float_to_q12(local.position.y);
                const int32_t z=float_to_q12(local.position.z);
                const auto delta=[&](size_t row){
                    return static_cast<int32_t>((int64_t(parent_basis[row])*x+
                        int64_t(parent_basis[row+1])*y+int64_t(parent_basis[row+2])*z)>>28);
                };
                result[i].position={
                    q12_to_float(float_to_q12(parent_pose.position.x)+delta(0)),
                    q12_to_float(float_to_q12(parent_pose.position.y)+delta(3)),
                    q12_to_float(float_to_q12(parent_pose.position.z)+delta(6))};
            }
        }
    }
    if(entity&&entity->runtime_model_class_id==3U&&entity->actor.articulated_native.initialized)
        native_walker_geometry(*entity,catalog,&result);
    // Fatal direct impacts on class-0 roots write the stored world angle0
    // without rebuilding the cached world matrix. Native mesh raycasts invert
    // those stored angles, not the render basis: even an upright/gimbal pose
    // can change the integer inverse by one Q28 unit.
    if(entity)for(const auto& component:entity->model_components){
        if(component.runtime_class_id!=0U||component.placement_contact_kind!=0||
           !component.terminal_self_reference||component.parent_component_index||
           component.source_model_node_index>=result.size())continue;
        auto& pose=result[component.source_model_node_index];
        if(pose.native_angles)(*pose.native_angles)[2]=component.model_rotation_angle0;
    }
    return result;
}
bool ray_transparent(std::span<const ModelVisualNode> hierarchy,size_t node){
    // Native marks the MDL root with MODEL_NODE_FLAG_RAY_TRANSPARENT from
    // definition bit 0x100 and omits its entire subtree from weapon raycasts.
    for(size_t hops=0;node<hierarchy.size()&&hops<hierarchy.size();++hops){
        if(simulation_lab_is_model_runtime_root(hierarchy,node)&&
           (hierarchy[node].simulation.runtime_render_flags&0x100U))return true;
        node=hierarchy[node].parent_index;
    }
    return false;
}
std::vector<int32_t> subtree_radii(std::span<const ModelVisualNode> nodes,
                                  const SimulationLabRuntimeCatalog& catalog){
    std::vector<int32_t> radii(nodes.size());
    for(size_t i=nodes.size();i-- >0;){
        const auto sprite=catalog.sprites.find(nodes[i].sprite_path);
        if(sprite==catalog.sprites.end())continue;
        int64_t radius=std::max(0,sprite->second.bounding_radius_q12);
        for(size_t child=i+1;child<nodes.size();++child){
            if(nodes[child].parent_index!=i||nodes[child].child_slot>=6U)continue;
            if(simulation_lab_is_model_runtime_root(nodes,i)&&
               (nodes[i].runtime_class_id==14U||nodes[i].runtime_class_id==15U)&&
               !model_runtime_world_child_enabled(nodes[i].runtime_class_id,nodes[child].child_slot))continue;
            const auto& xyz=sprite->second.child_translations_q12[nodes[child].child_slot];
            if(!xyz)continue;
            uint64_t square=0;for(auto value:*xyz)square+=uint64_t(int64_t(value)*value);
            radius=std::max(radius,int64_t(retail_uint64_sqrt(square))+radii[child]);
        }
        radii[i]=static_cast<int32_t>(std::min<int64_t>(radius,INT32_MAX));
    }
    return radii;
}
SimulationLabStaticShotHull hull(const SimulationLabRuntimeSprite& sprite,const SimulationLabEffectPose& pose,uint32_t cls,
                               std::optional<int32_t> radius={}){
    SimulationLabStaticShotHull h;h.impact_class=cls;h.collision_surface=sprite.collision_surface;h.collision_pose=pose;
    if(sprite.has_native_bounding_radius)h.native_bounding_radius_q12=radius;
    for(uint32_t i=0;i<8;++i)h.corners[i]=point(pose,{(i&1)?sprite.bounds_max.x:sprite.bounds_min.x,
        (i&2)?sprite.bounds_max.y:sprite.bounds_min.y,(i&4)?sprite.bounds_max.z:sprite.bounds_min.z});
    return h;
}
std::optional<int32_t> fixed_mesh_distance(Vec3f origin,const SimulationLabStaticShotHull& hull,
                                            const SimulationLabFixedRay& ray) {
    using FixedVector=std::array<int32_t,3>;
    const auto dot_fixed=[](const FixedVector& a,const FixedVector& b){
        return int64_t(a[0])*b[0]+int64_t(a[1])*b[1]+int64_t(a[2])*b[2];
    };
    const auto sub_fixed=[](const FixedVector& a,const FixedVector& b){
        return FixedVector{a[0]-b[0],a[1]-b[1],a[2]-b[2]};
    };
    const auto cross_normal=[](const FixedVector& n,const FixedVector& v){
        return FixedVector{
            int32_t((int64_t(n[1])*v[2]-int64_t(n[2])*v[1])>>12),
            int32_t((int64_t(n[2])*v[0]-int64_t(n[0])*v[2])>>12),
            int32_t((int64_t(n[0])*v[1]-int64_t(n[1])*v[0])>>12)};
    };
    const auto& pose=hull.collision_pose;
    std::array<int32_t,9> basis{};
    for(size_t i=0;i<basis.size();++i)basis[i]=int32_t(std::lround(double(pose.basis[i])*268435456.0));
    // Native inverts the node's stored Euler angles, without extracting them
    // again from a float rendering matrix near a triangle's collision edge.
    const auto angles=pose.native_angles.value_or(retail_extract_model_angles(basis));
    const auto [roll,elevation,azimuth]=angles;
    const auto inverse=retail_model_basis_q28(0U-roll,elevation,azimuth+0x8000U-roll);
    const auto to_local=[&](const FixedVector& v){
        FixedVector result{};
        for(size_t i=0;i<result.size();++i){
            const FixedVector row{inverse[3*i],inverse[3*i+1],inverse[3*i+2]};
            result[i]=int32_t(dot_fixed(row,v)>>28);
        }
        return result;
    };
    const auto local_origin=to_local({float_to_q12(origin.x)-float_to_q12(pose.position.x),
        float_to_q12(origin.y)-float_to_q12(pose.position.y),
        float_to_q12(origin.z)-float_to_q12(pose.position.z)});
    const auto local_direction=to_local(ray.direction_q28);
    std::optional<int32_t> nearest;
    for(const auto& triangle:hull.collision_surface->native_triangles){
        const auto& [a,b,c]=triangle.vertices_q12;
        // Native promotes each packed normal to Q28 in a 32-bit register.
        // The shift drops the upper word, including the degenerate-face sentinel.
        auto normal=triangle.normal_q12;
        for(auto& value:normal)value=std::bit_cast<int16_t>(static_cast<uint16_t>(value));
        FixedVector plane{};
        for(size_t i=0;i<plane.size();++i)plane[i]=(2*a[i]+b[i]+c[i])>>2;
        const int64_t offset=dot_fixed(normal,sub_fixed(plane,local_origin))*65536;
        const int64_t direction=dot_fixed(normal,local_direction)>>12;
        if(direction==0)continue;
        // Native rejects out-of-range planes using the signed high dwords
        // before dividing the full offset by the projected direction.
        const int64_t offset_high=offset>>32;
        const int64_t max_high=(int64_t(ray.distance_q12)*direction)>>32;
        const int64_t half_high=offset_high>>1;
        if(offset<0 ? (offset_high<max_high||(half_high<=-direction&&half_high<=direction)) :
                      (max_high<offset_high||(-direction<=half_high&&direction<=half_high)))continue;
        const auto distance=offset/direction;
        // The signed-high-dword plane admission above is native's range test.
        // Its quantization can admit a plane a few Q12 units beyond the query;
        // a second exact cap changes ballistic LOS at those boundaries.
        if(distance<0||(nearest&&distance>=*nearest))continue;
        FixedVector relative{};
        for(size_t i=0;i<relative.size();++i)
            relative[i]=int32_t((distance*local_direction[i])>>28)+local_origin[i]-a[i];
        const auto edge1=sub_fixed(b,a),edge2=sub_fixed(c,a);
        const auto cross_edge=cross_normal(normal,edge1);
        const auto area=dot_fixed(edge2,cross_edge);
        const auto u=dot_fixed(edge2,cross_normal(normal,relative));
        const auto v=dot_fixed(relative,cross_edge);
        const auto w=area-u-v;
        if(area<0 ? u<0&&v<0&&w<0 : u>=0&&v>=0&&w>=0)
            nearest=int32_t(distance);
    }
    return nearest;
}
std::optional<float> segment_hull(Vec3f origin,Vec3f delta,const SimulationLabStaticShotHull& h,
                                  const SimulationLabFixedRay* fixed_ray=nullptr){
    if(fixed_ray&&h.native_bounding_radius_q12){
        const int64_t x=int64_t(float_to_q12(h.collision_pose.position.x))-float_to_q12(origin.x);
        const int64_t y=int64_t(float_to_q12(h.collision_pose.position.y))-float_to_q12(origin.y);
        const int64_t z=int64_t(float_to_q12(h.collision_pose.position.z))-float_to_q12(origin.z);
        const auto& direction=fixed_ray->direction_q28;
        const int64_t projected=(x*direction[0]+y*direction[1]+z*direction[2])>>28;
        const int64_t radius=*h.native_bounding_radius_q12;
        if(projected < -radius||int64_t(fixed_ray->distance_q12)+radius<=projected||
           radius*radius+projected*projected-x*x-y*y-z*z<0)return std::nullopt;
    }
    // An authored SPR without raycast triangles has no native mesh contact.
    // Bounds-only fallback belongs to metadata-only fixtures, never stock assets.
    if(fixed_ray&&h.native_bounding_radius_q12&&
       (!h.collision_surface||h.collision_surface->native_triangles.empty()))return std::nullopt;
    // Native uses its sphere and packed triangles. The rendered float AABB is
    // not an additional native rejection test at quantized collision edges.
    if(fixed_ray&&h.collision_surface&&!h.collision_surface->native_triangles.empty()){
        const auto distance=fixed_mesh_distance(origin,h,*fixed_ray);
        return distance?std::optional{float(*distance)/float(std::max(1,fixed_ray->distance_q12))}:std::nullopt;
    }
    float enter=0.F,exit=1.F;
    const std::array o{origin.x,origin.y,origin.z},d{delta.x,delta.y,delta.z};
    for(size_t axis=0;axis<3;++axis){
        float lo=std::numeric_limits<float>::max(),hi=std::numeric_limits<float>::lowest();
        for(const auto& corner:h.corners){const std::array p{corner.x,corner.y,corner.z};lo=std::min(lo,p[axis]);hi=std::max(hi,p[axis]);}
        if(std::abs(d[axis])<1.e-10F){if(o[axis]<lo||o[axis]>hi)return std::nullopt;continue;}
        float a=(lo-o[axis])/d[axis],b=(hi-o[axis])/d[axis];if(a>b)std::swap(a,b);
        enter=std::max(enter,a);exit=std::min(exit,b);if(enter>exit)return std::nullopt;
    }
    // The box is a broad phase only. A bounding-box face does not imply a
    // visible model contact: it may lie far outside a building's SPR surface.
    // Validate the actual decoded triangles in their current hierarchy pose.
    if(h.collision_surface && !h.collision_surface->indices.empty()){
        const auto& mesh=*h.collision_surface;
        std::optional<float> nearest;
        const auto& pose=h.collision_pose;
        // Transform the segment ONCE into SPR-local space, instead of transforming
        // all three vertices of every triangle on every projectile/LOS ray. The
        // full affine inverse preserves contact t even for tilted terrain roots.
        const Vec3f axis_x{pose.basis[0],pose.basis[3],pose.basis[6]};
        const Vec3f axis_y{pose.basis[1],pose.basis[4],pose.basis[7]};
        const Vec3f axis_z{pose.basis[2],pose.basis[5],pose.basis[8]};
        const Vec3f co_x=cross(axis_y,axis_z),co_y=cross(axis_z,axis_x),co_z=cross(axis_x,axis_y);
        const float determinant=dot(axis_x,co_x);
        const bool invertible=std::isfinite(determinant)&&std::abs(determinant)>1.e-12F;
        const auto local=[&](Vec3f v){
            return Vec3f{dot(v,co_x)/determinant,dot(v,co_y)/determinant,dot(v,co_z)/determinant};
        };
        const Vec3f local_origin=invertible?local(difference(origin,pose.position)):Vec3f{};
        const Vec3f local_delta=invertible?local(delta):Vec3f{};
        for(size_t idx=0;idx+2U<mesh.indices.size();idx+=3U){
            const auto a=invertible?mesh.positions[mesh.indices[idx]]:point(pose,mesh.positions[mesh.indices[idx]]);
            const auto b=invertible?mesh.positions[mesh.indices[idx+1U]]:point(pose,mesh.positions[mesh.indices[idx+1U]]);
            const auto c=invertible?mesh.positions[mesh.indices[idx+2U]]:point(pose,mesh.positions[mesh.indices[idx+2U]]);
            const auto test_origin=invertible?local_origin:origin;
            const auto test_delta=invertible?local_delta:delta;
            const auto e1=difference(b,a),e2=difference(c,a);
            const auto p=cross(test_delta,e2);
            const float det=dot(e1,p);
            if(std::abs(det)<1.e-10F)continue;
            const auto v=difference(test_origin,a);
            const float u=dot(v,p)/det;
            if(u<-1.e-6F||u>1.F+1.e-6F)continue;
            const auto q=cross(v,e1);
            const float w=dot(test_delta,q)/det;
            if(w<-1.e-6F||u+w>1.F+1.e-6F)continue;
            const float t=dot(e2,q)/det;
            if(t>=0.F&&t<=1.F&&(!nearest||t<*nearest))nearest=t;
        }
        return nearest;
    }
    constexpr std::array<std::array<uint8_t,3>,12> faces{{{0,2,3},{0,3,1},{4,5,7},{4,7,6},
        {0,1,5},{0,5,4},{2,6,7},{2,7,3},{0,4,6},{0,6,2},{1,3,7},{1,7,5}}};
    std::optional<float> nearest;
    for(const auto& f:faces){
        const auto a=h.corners[f[0]],e1=difference(h.corners[f[1]],a),e2=difference(h.corners[f[2]],a);
        const auto p=cross(delta,e2);const float det=dot(e1,p);if(std::abs(det)<1.e-10F)continue;
        const auto v=difference(origin,a);const float u=dot(v,p)/det;if(u<0||u>1)continue;
        const auto q=cross(v,e1);const float w=dot(delta,q)/det;if(w<0||u+w>1)continue;
        const float t=dot(e2,q)/det;if(t>=0&&t<=1&&(!nearest||t<*nearest))nearest=t;
    }return nearest;
}
}
std::optional<SimulationLabModelRayHit> simulation_lab_raycast_models(
    Vec3f origin,Vec3f direction,float maximum_distance,std::optional<size_t> excluded_entity,
    std::span<const SimulationLabLiveShotHull> live_hulls,
    std::span<const SimulationLabStaticShotHull> static_hulls,const SimulationLabFixedRay* fixed_ray){
    if(maximum_distance<=0.F){
        if(!fixed_ray||fixed_ray->distance_q12!=0)return std::nullopt;
        // Use one Q12 unit as a denominator while the native range stays zero.
        maximum_distance=q12_to_float(1);
    }
    const Vec3f delta{direction.x*maximum_distance,direction.y*maximum_distance,direction.z*maximum_distance};
    std::optional<SimulationLabModelRayHit> result;
    const auto test=[&](const auto& h,std::optional<size_t> index,std::optional<size_t> component){
        if(const auto t=segment_hull(origin,delta,h,fixed_ray);t&&(!result||*t*maximum_distance<result->distance))
            result=SimulationLabModelRayHit{*t*maximum_distance,h.impact_class,index,component};
    };
    for(const auto& h:live_hulls)if(!excluded_entity||h.entity_index!=*excluded_entity)test(h.hull,h.entity_index,h.component_index);
    for(const auto& h:static_hulls)test(h,std::nullopt,std::nullopt);
    return result;
}
Vec3f simulation_lab_model_world_point(const SimulationLabEffectPose& pose,Vec3f local){
    return point(pose,local);
}
std::vector<SimulationLabEffectPose> simulation_lab_current_model_poses(const SimulationLabEntity& entity,
    const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog){
    if(!entity.runtime_model_hierarchy)return {};
    return poses(*entity.runtime_model_hierarchy,entity.actor.position,entity.actor.body_yaw_turn16,field,catalog,&entity);
}
bool simulation_lab_place_root_on_surface(SimulationLabEntity& entity,
    const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog) {
    if(!entity.runtime_model_hierarchy)return false;
    const auto& hierarchy=*entity.runtime_model_hierarchy;
    const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){return node.parent_index==UINT32_MAX;});
    if(root==hierarchy.end())return false;
    const auto kind=root->simulation.placement_contact_kind;
    if(kind!=0&&kind!=1&&kind!=2&&kind!=4)return false;
    const auto heights=field.sample_retail_surface_heights_q12(float_to_q12(entity.actor.position.x),float_to_q12(entity.actor.position.y));
    if(!heights.valid)return false;
    // Terrain deformation refreshes field normals. The model keeps its placed
    // orientation until its next contact callback, just like its height.
    entity.placement_normal_angles=kind==2?retail_contact_normal_angles(
        field,float_to_q12(entity.actor.position.x),float_to_q12(entity.actor.position.y)):std::nullopt;
    int32_t height=kind==1?heights.water_surface:(kind==4?heights.top:heights.terrain);
    uint32_t total=uint32_t(height)+uint32_t(root->simulation.placement_height_offset_q12);
    if(kind==0||kind==2)if(const auto sprite=catalog.sprites.find(root->sprite_path);sprite!=catalog.sprites.end())
        total+=uint32_t(float_to_q12(sprite->second.height_offset));
    entity.actor.position.z=q12_to_float(static_cast<int32_t>(total));
    return true;
}
bool simulation_lab_prepare_missing_model_geometry(SimulationLabEntity& entity,
    const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog) {
    if(entity.runtime_removed||!entity.runtime_model_hierarchy||entity.runtime_model_hierarchy->empty())return false;
    const auto& hierarchy=*entity.runtime_model_hierarchy;
    const bool need_heights=entity.runtime_model_node_height_offsets_q12.size()!=hierarchy.size();
    const bool need_extraction=std::any_of(entity.model_components.begin(),entity.model_components.end(),
        [](const auto& component){return component.runtime_class_id==14U && !component.extraction_model_world_offset_at_seed;});
    const bool need_factory_anchors=entity.runtime_model_class_id==13U&&
        ((entity.production_spawn_local&&!entity.production_spawn_world)||
         (entity.production_exit_local&&!entity.production_exit_world));
    const bool need_articulated=entity.runtime_model_class_id==3U&&!entity.actor.articulated_native.initialized;
    if(!need_heights&&!need_extraction&&!need_factory_anchors&&!need_articulated)return false;
    if(need_heights)simulation_lab_place_root_on_surface(entity,field,catalog);
    // The native placement constructor seats the contacts and solves the legs
    // before the new owner is visible, including factory-created walkers.
    if(need_articulated&&simulation_lab_prepare_articulated_contacts(entity,field,catalog)){
        simulation_initialize_native_walker(entity.actor,entity.motion,
            entity.articulated_contact_interpolation_rate_q12,entity.articulated_contact_lift_q12,
            float_to_q12(entity.articulated_contact_local_offsets[0].y),entity.articulated_root_height_q12,
            float_to_q12(entity.articulated_shared_foot_placement_offset),
            [&](int32_t x,int32_t y)->std::optional<int32_t>{
                const auto height=field.sample_retail_surface_heights_q12(x,y);
                return height.valid?height.terrain:0;
            },true);
        entity.articulated_contact_geometry_prepared=true;
    }
    const auto transforms=simulation_lab_current_model_poses(entity,field,catalog);
    const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){return node.parent_index==UINT32_MAX;});
    if(root==hierarchy.end()||size_t(root-hierarchy.begin())>=transforms.size())return false;
    const auto& root_pose=transforms[size_t(root-hierarchy.begin())];
    // Placement planning reads factory lookup markers even before the first
    // production job. They exist as soon as the native model is created.
    if(need_factory_anchors){
        if(entity.production_spawn_local)entity.production_spawn_world=point(root_pose,*entity.production_spawn_local);
        if(entity.production_exit_local)entity.production_exit_world=point(root_pose,*entity.production_exit_local);
    }
    const auto root_z=root_pose.position.z;
    if(need_heights){
        entity.runtime_model_node_height_offsets_q12.resize(hierarchy.size());
        for(size_t i=0;i<transforms.size();++i)
            entity.runtime_model_node_height_offsets_q12[i]=float_to_q12(transforms[i].position.z-root_z);
        const auto metrics=simulation_lab_derived_selection_metrics(hierarchy,entity.runtime_model_node_height_offsets_q12);
        entity.fog_uncover_radius_q12=metrics.fog_uncover_radius_q12;
        entity.occupancy_mark_radius_units=metrics.occupancy_mark_radius_units;
        entity.fog_reference_height_offset_q12=metrics.fog_reference_height_offset_q12;
    }
    for(auto& component:entity.model_components){
        const auto node=component.source_model_node_index;
        if(component.runtime_class_id!=14U||component.extraction_model_world_offset_at_seed||node>=transforms.size())continue;
        component.extraction_model_world_offset_at_seed=difference(transforms[node].position,entity.actor.position);
        component.extraction_model_seed_yaw_turn16=entity.actor.body_yaw_turn16;
    }
    return true;
}
bool simulation_lab_prepare_articulated_contacts(SimulationLabEntity& entity,
    const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog){
    if(entity.runtime_model_class_id!=3U||!entity.runtime_model_hierarchy)return false;
    if(entity.articulated_contact_geometry_enabled)return true;
    const auto& nodes=*entity.runtime_model_hierarchy;
    const auto root=std::find_if(nodes.begin(),nodes.end(),[](const auto& n){return n.parent_index==UINT32_MAX;});
    if(root==nodes.end())return false;
    const auto transforms=poses(nodes,entity.actor.position,entity.actor.body_yaw_turn16,field,catalog);
    std::array<std::size_t,2> feet{};
    for(uint32_t side=0;side<2;++side){
        std::size_t parent=static_cast<std::size_t>(root-nodes.begin());
        for(uint32_t depth=0;depth<4;++depth){
            const auto child=std::find_if(nodes.begin(),nodes.end(),[&](const auto& n){
                return n.creates_runtime_node()&&n.parent_index==parent&&n.child_slot==(depth==0?side:0U);
            });
            if(child==nodes.end()||parent>=transforms.size())return false;
            const auto sprite=catalog.sprites.find(nodes[parent].sprite_path);
            const uint32_t slot=depth==0?side:0U;
            if(sprite==catalog.sprites.end()||!sprite->second.child_translations_q12[slot])return false;
            parent=static_cast<std::size_t>(child-nodes.begin());
        }
        feet[side]=parent;
    }
    const auto right=catalog.sprites.find(nodes[feet[1]].sprite_path);
    if(right==catalog.sprites.end())return false;
    const auto root_sprite=catalog.sprites.find(root->sprite_path);
    if(root_sprite==catalog.sprites.end()||!root_sprite->second.child_translations_q12[0])return false;
    // Native initialization seats contacts beside the root using the FIRST child's local Y.
    // The authored terminal translation is an ankle, already displaced by the contact radius.
    // Seeding from that ankle added the radius twice and spread Orang/Gorilla legs too far apart.
    const float lateral=q12_to_float((*root_sprite->second.child_translations_q12[0])[1]);
    entity.articulated_contact_local_offsets={Vec3f{0,lateral,0},Vec3f{0,-lateral,0}};
    // Retail's support solver uses the right terminal SPR offset for both feet.
    entity.articulated_shared_foot_placement_offset=right->second.height_offset;
    entity.actor.articulated_contacts_initialized=false;
    entity.articulated_contact_geometry_enabled=true;
    return true;
}
SimulationLabTargetAimGeometry simulation_lab_target_aim_geometry(
    const SimulationLabEntity& entity,const FieldGrid& field,
    const SimulationLabRuntimeCatalog& catalog){
    // The native lead-endpoint path returns the army root Z plus the root MDL
    // aim offset even if ordinary aim selected an aircraft's child-zero node.
    SimulationLabTargetAimGeometry result{entity.actor.position,entity.actor.position.z};
    const auto transforms=simulation_lab_current_model_poses(entity,field,catalog);
    if(!entity.runtime_model_hierarchy||transforms.empty())return result;
    const auto& nodes=*entity.runtime_model_hierarchy;
    const auto root=std::find_if(nodes.begin(),nodes.end(),[](const auto& n){
        return n.parent_index==UINT32_MAX;
    });
    if(root==nodes.end())return result;
    const auto root_index=static_cast<std::size_t>(root-nodes.begin());
    if(root_index>=transforms.size())return result;
    const auto height=q12_to_float(root->simulation.aim_height_offset_q12);
    result.root_aim_z=transforms[root_index].position.z+height;
    const auto selected=simulation_lab_live_target_aim_node_index(entity,nodes);
    if(!selected || *selected>=transforms.size())return result;
    result.selected=transforms[*selected].position;
    result.selected.z+=height;
    return result;
}
Vec3f simulation_lab_target_aim_point(const SimulationLabEntity& entity,
    const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog){
    return simulation_lab_target_aim_geometry(entity,field,catalog).selected;
}
void simulation_lab_step_barrel_recoil(const SimulationLabEntity& entity,
    const SimulationWeaponProfile& profile,SimulationWeaponState& state,
    const SimulationLabRuntimeCatalog& catalog,int32_t multiplier){
    if(multiplier==0||!entity.runtime_model_hierarchy||
       (profile.runtime_class_id!=7U&&profile.runtime_class_id!=8U))return;
    const auto step_barrel=[&](uint32_t node_index,std::optional<std::array<int32_t,3>>& stored){
        if(!stored){
            const auto& nodes=*entity.runtime_model_hierarchy;
            if(node_index>=nodes.size())return;
            const auto& barrel=nodes[node_index];
            if(barrel.parent_index>=nodes.size()||barrel.child_slot>=6U)return;
            const auto parent=catalog.sprites.find(nodes[barrel.parent_index].sprite_path);
            if(parent==catalog.sprites.end())return;
            stored=parent->second.child_translations_q12[barrel.child_slot];
            if(!stored)return;
        }
        auto& xyz=*stored;
        const auto horizontal=retail_uint64_sqrt(uint64_t(int64_t(xyz[0])*xyz[0])+uint64_t(int64_t(xyz[1])*xyz[1]));
        const auto step=retail_direction_scaled(fixed_atan2_angle16_compat(xyz[2],horizontal),
            fixed_atan2_angle16_compat(xyz[1],xyz[0]),profile.recoil_translation_q12);
        for(size_t i=0;i<xyz.size();++i)xyz[i]-=step[i]*multiplier;
    };
    if(profile.runtime_class_id==7U){step_barrel(profile.launch_model_node_index,state.barrel_translation_q12);return;}
    // Each class-8 barrel retains its own quantized local vector and recoil clock.
    for(size_t barrel=0;barrel<2U&&barrel<profile.launch_model_node_indices.size();++barrel){
        const bool active=multiplier<0?state.alternate_barrel_recoil_ticks[barrel]!=0U:
            barrel==state.recoil_launch_selector;
        if(!active)continue;
        step_barrel(profile.launch_model_node_indices[barrel],state.alternate_barrel_translations_q12[barrel]);
    }
}
SimulationLabWeaponGeometry simulation_lab_weapon_geometry(const SimulationLabEntity& entity,
    const SimulationWeaponProfile& profile,const SimulationWeaponState& state,
    const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog){
    SimulationLabWeaponGeometry result;result.aim_origin=entity.actor.position;
    result.target_resolution_origin=entity.actor.position;
    const auto transforms=simulation_lab_current_model_poses(entity,field,catalog);
    if(!entity.runtime_model_hierarchy||transforms.empty())return result;
    const auto& hierarchy=*entity.runtime_model_hierarchy;
    auto nodes=profile.launch_model_node_indices;if(nodes.empty())nodes.push_back(profile.launch_model_node_index);
    if(profile.launch_policy==SimulationLaunchPolicy::AlternateLaunchNodes&&!nodes.empty()){
        const auto selected=nodes[state.next_launch_attachment%nodes.size()];nodes.assign(1U,selected);}
    struct Launch{Vec3f position;Vec3f target_offset;uint32_t selector;};std::vector<Launch> points;
    for(const auto n:nodes){
        if(n>=hierarchy.size()||!simulation_lab_model_node_present(entity,n))continue;
        const auto sprite=catalog.sprites.find(hierarchy[n].sprite_path);if(sprite==catalog.sprites.end())continue;
        for(const auto& [selector,local]:sprite->second.shot_points){
            auto position=point(transforms[n],local);

            const auto offset=difference(position,transforms[n].position);
            points.push_back({position,{offset.x,offset.y,0.0F},selector});
            if(selector<32U)result.available_selector_mask|=1U<<selector;
        }
    }
    const auto append_launch=[&](const Launch& launch){
        result.launch_points.push_back(launch.position);
        result.launch_target_offsets.push_back(launch.target_offset);
    };
    if(profile.launch_policy==SimulationLaunchPolicy::SequentialKind2Attachments&&!points.empty()){
        const auto selector=state.last_fired_attachment_selector!=UINT32_MAX?state.last_fired_attachment_selector:state.next_launch_attachment;
        if(profile.runtime_class_id==9U){for(const auto& p:points)if(p.selector==selector){append_launch(p);break;}}
        else append_launch(points[selector%points.size()]);
    }else for(const auto& p:points)append_launch(p);
    if(!result.launch_points.empty())result.aim_origin=result.launch_points.front();
    // A destroyed pivot remains in the immutable pose array until release. Do not
    // use that retained position/orientation for a surviving launch attachment.
    if(simulation_lab_live_weapon_pose_node(entity,hierarchy,profile.aim_model_node_index,transforms.size()))
        result.aim_origin=transforms[profile.aim_model_node_index].position;
    if(simulation_lab_live_weapon_pose_node(entity,hierarchy,profile.yaw_model_node_index,transforms.size()))
    {
        const auto& yaw_pose=transforms[profile.yaw_model_node_index];
        result.yaw_basis=yaw_pose.basis;
        result.native_yaw_angles=yaw_pose.native_angles;
        result.target_resolution_origin=yaw_pose.position;
    }
    return result;
}
SimulationLabEnvironmentSeed simulation_lab_prepare_environment(const AssetCatalog& assets,const LevelAsset& level,
    const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog){
    SimulationLabEnvironmentSeed result;
    for(size_t placement_index=0;placement_index<level.placements().size();++placement_index){
        const auto& placement=level.placements()[placement_index];
        const auto* army=assets.army_preset(placement.army_asset_id);if(!army)continue;
        result.initial_army_placements.push_back({placement_index,placement.army_asset_id,placement.faction_index});
        if(simulation_lab_faction_placement_has_runtime_owner(*army,placement.faction_index))continue;
        // Authored wrecks need the same timed-emitter and sinking lifecycle as
        // wrecks created during play; a static collider cannot service class 12.
        if(simulation_lab_scenery_has_runtime_owner(*army,placement.faction_index)){
            result.crushable_scenery.push_back({placement_index,placement.army_asset_id,
                placement.orientation_turn16,{placement.render_x(),placement.render_y(),0.F}});
            continue;
        }
        const auto& nodes=army->resolved_model_hierarchy.empty()?army->model_hierarchy:army->resolved_model_hierarchy;
        const auto transforms=poses(nodes,{placement.render_x(),placement.render_y(),0},placement.orientation_turn16,field,catalog);
        const auto radii=subtree_radii(nodes,catalog);
        std::vector<size_t> runtime_roots;
        std::vector<size_t> owner_roots;
        runtime_roots.reserve(nodes.size());owner_roots.reserve(1U);
        for(size_t i=0;i<nodes.size();++i){
            const auto& n=nodes[i];if(!n.creates_runtime_node())continue;
            const bool runtime_root=simulation_lab_is_model_runtime_root(nodes,i);
            if(runtime_root){
                runtime_roots.push_back(i);
                if(n.parent_index==UINT32_MAX||n.parent_index>=nodes.size())owner_roots.push_back(i);
            }
            const auto sprite=catalog.sprites.find(n.sprite_path);if(sprite==catalog.sprites.end())continue;
            if(sprite->second.has_bounds&&!ray_transparent(nodes,i))result.collision_hulls.push_back(hull(sprite->second,transforms[i],n.shot_impact_class_index,radii[i]));
            // Static class0/16 emitters are unconditional. Class4 uses MDL +0x1C8
            // as a research gate, including neutral resource models outside the entity list.
            if(!runtime_root||(n.runtime_class_id!=0U&&n.runtime_class_id!=16U&&
                !(n.runtime_class_id==4U&&n.simulation.timed_effects_require_research==0U))||
                (n.simulation.timed_effect_id==0&&n.simulation.timed_water_effect_id==0))continue;
            SimulationLabEnvironmentalEmitter emitter;emitter.model_definition_id=n.model_definition_id;emitter.pose=transforms[i];
            emitter.source_placement_index=placement_index;
            for(auto p:sprite->second.effect_points)emitter.world_effect_points.push_back(point(transforms[i],p));
            for(const auto& [key,p]:sprite->second.effect_point_records)emitter.world_effect_point_records.push_back({key,point(transforms[i],p)});
            result.emitters.push_back(std::move(emitter));
        }
        // One ArmyRuntime links one root MODEL owner. Attached ModelRuntime roots
        // contribute to its aggregate selection/visibility metrics, but are not
        // independent world-owner nodes.
        if(owner_roots.empty()&&!runtime_roots.empty())owner_roots.push_back(runtime_roots.front());
        const auto descends_from=[&](size_t node,size_t root){
            for(size_t hops=0;node<nodes.size()&&hops<nodes.size();++hops){
                if(node==root)return true;
                const auto parent=nodes[node].parent_index;
                if(parent>=nodes.size()||parent==node)break;
                node=parent;
            }
            return false;
        };
        for(const auto root_index:owner_roots){
            if(root_index>=nodes.size()||root_index>=transforms.size())continue;
            SimulationLabDerivedSelectionMetrics metrics;
            for(const auto runtime_root:runtime_roots){
                if(runtime_root>=transforms.size()||!descends_from(runtime_root,root_index))continue;
                const auto relative_height=float_to_q12(
                    transforms[runtime_root].position.z-transforms[root_index].position.z);
                simulation_lab_accumulate_derived_selection_metrics(
                    metrics,nodes[runtime_root],relative_height);
            }
            const auto& root=nodes[root_index];
            SimulationLabStaticPlacementOwner owner;
            owner.model_definition_id=root.model_definition_id;
            owner.faction_index=placement.faction_index;
            owner.pose=transforms[root_index];
            owner.source_placement_index=placement_index;
            owner.derived_selection_metrics_valid=metrics.runtime_root_count!=0U;
            owner.runtime_class_id=root.runtime_class_id;
            owner.visibility_radius_q12=metrics.fog_uncover_radius_q12;
            owner.occupancy_mark_radius_units=metrics.occupancy_mark_radius_units;
            owner.visibility_height_offset_q12=metrics.fog_reference_height_offset_q12;
            result.placement_owners.push_back(std::move(owner));
        }
    }return result;
}

void SimulationLabExperiment::advance_production_emitters(SimulationLabRun& run,const FieldGrid& field,
    std::optional<size_t> owner,std::optional<bool> deferred_root_timed_active,bool root_only,
    std::optional<std::array<uint32_t,3>> retained_root_angles){
    if(!runtime_catalog_)return;
    const auto& catalog=*runtime_catalog_;
    for(size_t visit=0;visit<(owner?1U:run.entities.size());++visit){
        auto& entity=run.entities[owner.value_or(run.level_runtime_enabled?run.entities.size()-1U-visit:visit)];
        const auto world_scope=run.native_world_objects.enter_owner(simulation_lab_world_model_handle(entity));
        // Every live producer services this callback, including authored map placements.
        // Its RNG draws belong to simulation even when presentation has a placement controller.
        if(entity.runtime_removed||!entity.construction_complete||!entity.runtime_model_hierarchy||
           (entity.synthetic&&entity.construction_start_tick>=run.tick))continue;
        std::vector<SimulationLabEffectPose> transforms;
        for(auto& component:entity.model_components){
            const auto component_scope=run.native_world_objects.enter_component(
                static_cast<size_t>(&component-entity.model_components.data()));
            const bool root_component=&component==&entity.model_components[entity.root_model_component_index];
            if(root_only&&!root_component)continue;
            if(component.runtime_class_id==15U&&!component.runtime_removed&&
               component.source_model_node_index<entity.runtime_model_hierarchy->size()&&
               entity.faction_index<run.faction_economy.size()){
                const auto& economy=run.faction_economy[entity.faction_index];
                const bool tritium=component.resource_storage_selector==1U;
                const auto stock=tritium?economy.tritium_current_q4:economy.xenite_current_q4;
                const auto limit=tritium?economy.tritium_storage_limit_q4:economy.xenite_storage_limit_q4;
                component.storage_child_z_q12=simulation_storage_child_z_q12(
                    (*entity.runtime_model_hierarchy)[component.source_model_node_index],0U,stock,limit.value_or(0U));
            }
            const auto root_class=entity.runtime_model_class_id;
            // Class 10 directly dispatches the animation callback even while
            // switched off. Bit 4 records that the off state was observed.
            if(run.level_runtime_enabled&&component.runtime_class_id==10U&&!component.runtime_removed){
                const auto flags=component.runtime_flags_ec|(root_component?entity.runtime_flags:0U);
                if(flags&1U)component.runtime_flags_ec|=4U;
                else component.runtime_flags_ec&=~4U;
            }
            unsigned damage_calls=(root_component&&(root_class==0U||root_class==4U||root_class==10U||
                root_class==11U||root_class==12U||root_class==13U||root_class==14U||root_class==15U||root_class==16U||root_class==17U))?1U:0U;
            // Class 10 directly calls the same animation routine as static
            // and resource models. Its clock belongs to this MODEL owner, even
            // when attached to an idle producer; global or production time
            // freezes rotating collision geometry or starts it at the wrong phase.
            bool animate_subnodes=run.level_runtime_enabled&&component.runtime_class_id==10U&&
                !component.runtime_removed&&!(component.runtime_flags_ec&1U)&&
                (!root_component||!(entity.runtime_flags&1U));
            const auto advance=[&](){
            const auto cls=component.runtime_class_id;
            // Classes 0 and 16 dispatch the emitter directly, including on
            // attached models whose hierarchy is marked for dismantling.
            // Native banking owners keep servicing the emitter until the
            // owner is released, including the destroyed stationary phase.
            const bool direct_emitter=cls==0U||cls==16U||
                (run.level_runtime_enabled&&(cls==1U||cls==2U||cls==3U||
                    cls==17U||cls==18U||cls==19U));
            if(component.runtime_removed||(!direct_emitter&&
                ((component.runtime_flags_ec|entity.runtime_flags)&9U)))return;
            const auto definition=catalog.definitions.find(component.model_definition_id);
            if(definition==catalog.definitions.end())return;
            const auto& p=definition->second.simulation;
            // Ground movement still services its emitter when stationary
            // recoil changes a destroyed model's facing before its release.
            const bool guarded=cls==4U||cls==11U||cls==13U||cls==14U;
            if(guarded&&(component.terminal_runtime_flag8||entity.root_model_terminal))return;
            if(cls==11U||cls==13U) {
                if(!(root_component&&deferred_root_timed_active?*deferred_root_timed_active:
                     simulation_lab_production_effect_active(entity)))return;
            } else if(cls==4U) {
                const auto flags=component.runtime_flags_ec|
                    (component.parent_component_index?0U:entity.runtime_flags);
                const bool active=p.timed_effects_require_research==0U||(flags&0x40U)!=0U||
                    (!component.parent_component_index&&entity.research_active);
                if(!(root_component&&deferred_root_timed_active?*deferred_root_timed_active:active))return;
                animate_subnodes=run.level_runtime_enabled;
            } else if(cls==14U) {
                if(component.integrity<=1)return;
                if(transforms.empty())transforms=poses(*entity.runtime_model_hierarchy,entity.actor.position,
                    entity.actor.body_yaw_turn16,field,catalog,&entity);
                const auto node=component.source_model_node_index;
                if(node>=transforms.size())return;
                const auto pos=transforms[node].position;
                const auto cell=retail_nearest_world_to_cell(field,float_to_q12(pos.x),float_to_q12(pos.y));
                if(!cell||cell->x==0||cell->y==0||cell->x+1U>=field.width()||cell->y+1U>=field.height()||
                   (field.at(cell->x,cell->y).flags_and_material&(0x800U<<(uint32_t(p.class_specific_c0_raw)&31U)))==0U)return;
                component.resource_cell_registered=true;
                animate_subnodes=run.level_runtime_enabled;
            } else if(cls==17U){
                // Banking units call their emitter while idle too, once the
                // deployment behavior's suppress-emitter bit has cleared.
                if(entity.actor.class17_rotor_deploy_ticks_remaining!=0U)return;
                if(root_component)damage_calls=2U;
            } else if(cls==1U||cls==2U||cls==3U||cls==18U||cls==19U){
                if(!root_only||!(entity.last_translated||entity.last_turned))return;
            } else if(!run.level_runtime_enabled||(cls!=0U&&cls!=12U&&cls!=16U))return;
            if(root_component&&(cls==4U||cls==11U||cls==13U||cls==14U))damage_calls=3U;
            const int32_t previous=std::bit_cast<int32_t>(component.timed_effect_countdown);
            const auto batch=std::max(1U,config_.runtime_callback_step_ticks);
            component.timed_effect_countdown-=batch;
            if(previous>static_cast<int32_t>(batch)||(p.timed_effect_id==0&&p.timed_water_effect_id==0))return;
            component.timed_effect_countdown=p.timed_effect_interval_ticks+
                (p.timed_effect_random_ticks?random_next(run.rng_state)%p.timed_effect_random_ticks:0U);
            const auto n=component.source_model_node_index;
            if(n>=entity.runtime_model_hierarchy->size())return;
            if(transforms.empty())transforms=poses(*entity.runtime_model_hierarchy,entity.actor.position,
                entity.actor.body_yaw_turn16,field,catalog,&entity);
            auto pose=transforms[n];
            const auto sprite=catalog.sprites.find((*entity.runtime_model_hierarchy)[n].sprite_path);
            if(sprite!=catalog.sprites.end()){
                const auto& points=sprite->second.effect_point_records;
                uint32_t count=0;for(const auto& entry:points)count=std::max(count,entry.first+1U);
                if(count){
                    const auto selector=(p.runtime_render_flags&1U)?component.timed_effect_point_index:random_next(run.rng_state);
                    const auto selected=std::find_if(points.begin(),points.end(),[&](const auto& v){return v.first==selector%count;});
                    // A missing sparse key falls back to the model origin in retail.
                    if(selected!=points.end()){
                        auto marker_pose=pose;
                        if(root_component&&retained_root_angles){
                            const auto& a=*retained_root_angles;
                            // Class 17 writes the new translation and Euler
                            // angles before the emitter. The point transform
                            // still uses the basis from the last rebuild.
                            marker_pose.native_basis=retail_model_basis_q28(a[0],a[1],a[2]);
                        }
                        pose.position=point(marker_pose,selected->second);
                    }
                }
            }
            ++component.timed_effect_point_index;
            const auto id=retail_nearest_water_delta_q12(field,float_to_q12(pose.position.x),float_to_q12(pose.position.y))>0?
                p.timed_water_effect_id:p.timed_effect_id;
            const auto effect=catalog.effects.find(id);if(effect==catalog.effects.end())return;
            const auto& d=effect->second.definition;
            pose=timed_effect_pose(pose,d,[&](){return random_next(run.rng_state);});
            const auto& native_angles=*pose.native_angles;
            const std::array<uint32_t,3> angles{native_angles[2],native_angles[1],native_angles[0]};
            SimulationLabOneShotEvent event;event.sequence=run.next_one_shot_event_sequence++;event.tick=run.tick;
            event.kind=SimulationLabOneShotEventKind::ModelTimedEffect;event.source_placement_index=entity.source_placement_index;
            event.army_asset_id=entity.army_asset_id;event.faction_index=entity.faction_index;event.effect_id=id;
            event.source_model_node_index=n;event.root_world_position=pose.position;event.effect_world_basis=pose.basis;event.effect_world_angles=angles;event.actor=entity.actor;
            run.one_shot_event_tail=std::make_shared<const SimulationLabOneShotEventNode>(std::move(event),run.one_shot_event_tail);
            ++run.metrics.production_timed_effects_spawned;
            // A valid EFF is an independent model owner even when it has no
            // linked SHT emissions. Its visible lifetime is not an emission count.
            // A factory emits before allocating the finished army. Keep that
            // arena order even when the timed emitter is serviced later.
            const bool before_factory_spawn=cls==13U&&root_component;
            link_environmental_effect(run,id,pose,false,std::nullopt,before_factory_spawn);
            };
            advance();
            if(animate_subnodes){
                component.animated_subnode_ticks+=std::max(1U,config_.runtime_callback_step_ticks);
                transforms.clear();
            }
            if(damage_calls)advance_damage_effects(run,field,
                static_cast<size_t>(&entity-run.entities.data()),damage_calls,retained_root_angles);
        }
    }
}
void SimulationLabExperiment::advance_environmental_emitters(SimulationLabRun& run,const FieldGrid& field,
    std::optional<size_t> emitter){
    if(!runtime_catalog_)return;
    const auto& catalog=*runtime_catalog_;
    run.environmental_emitter_states.resize(environmental_emitters_.size());
    for(size_t visit=0;visit<(emitter?1U:environmental_emitters_.size());++visit){
        const auto i=emitter.value_or(run.level_runtime_enabled?environmental_emitters_.size()-1U-visit:visit);
        const auto& source=environmental_emitters_[i];auto& state=run.environmental_emitter_states[i];
        const auto world_scope=run.native_world_objects.enter_owner(i<run.environmental_emitter_world_handles.size()
            ?run.environmental_emitter_world_handles[i]:kSimulationLabNoWorldSlot);
        const auto model=catalog.definitions.find(source.model_definition_id);if(model==catalog.definitions.end())continue;
        const auto& p=model->second.simulation;const int32_t old=std::bit_cast<int32_t>(state.countdown);
        const auto batch=std::max(1U,config_.runtime_callback_step_ticks);state.countdown-=batch;
        if(old>static_cast<int32_t>(batch))continue;
        state.countdown=p.timed_effect_interval_ticks+(p.timed_effect_random_ticks?random_next(run.rng_state)%p.timed_effect_random_ticks:0U);
        auto pose=source.pose;
        if(!source.world_effect_point_records.empty()){
            uint32_t count=0;for(const auto& entry:source.world_effect_point_records)count=std::max(count,entry.first+1U);
            if(count){const auto selector=(p.runtime_render_flags&1U)?state.point_index:random_next(run.rng_state);
                const auto selected=std::find_if(source.world_effect_point_records.begin(),source.world_effect_point_records.end(),
                    [&](const auto& entry){return entry.first==selector%count;});
                if(selected!=source.world_effect_point_records.end())pose.position=selected->second;
            }
        }else if(!source.world_effect_points.empty()){
            const uint32_t selected=(p.runtime_render_flags&1U)?state.point_index:random_next(run.rng_state);
            pose.position=source.world_effect_points[selected%source.world_effect_points.size()];
        }
        ++state.point_index;
        const auto id=retail_nearest_water_delta_q12(field,float_to_q12(pose.position.x),float_to_q12(pose.position.y))>0?
            p.timed_water_effect_id:p.timed_effect_id;
        const auto effect=catalog.effects.find(id);if(effect==catalog.effects.end())continue;
        const auto& d=effect->second.definition;
        pose=timed_effect_pose(pose,d,[&](){return random_next(run.rng_state);});
        append_environment_effect_sound(run,id,d,pose.position);
        // Effects without a linked shot still own visible EFF geometry.
        link_environmental_effect(run,id,pose);
        ++run.metrics.environmental_effects_spawned;
    }
 }
std::array<Vec3f,2> simulation_lab_articulated_contact_positions(
    const SimulationLabEntity& entity,const SimulationLabRuntimeCatalog& catalog){
    return native_walker_geometry(entity,catalog,nullptr);
}

void SimulationLabExperiment::emit_articulated_contact_effect(SimulationLabRun& run,const FieldGrid& field,
    const SimulationLabEntity& entity,Vec3f position,const std::array<uint32_t,3>& angles){
    const auto world_scope=run.native_world_objects.enter_owner(simulation_lab_world_model_handle(entity));
    if(!runtime_catalog_||!entity.runtime_model_hierarchy||entity.runtime_model_hierarchy->empty())return;
    const auto& profile=entity.runtime_model_hierarchy->front().simulation;
    const auto id=retail_nearest_water_delta_q12(field,float_to_q12(position.x),float_to_q12(position.y))>0
        ?profile.timed_water_effect_id:profile.timed_effect_id;
    const auto found=runtime_catalog_->effects.find(id);
    if(found==runtime_catalog_->effects.end())return;
    auto pose=rotation(angles[0],angles[1],angles[2],true);pose.position=position;
    // walker.cpp calls the pool directly. Unlike timed emitters, transition 2
    // does NOT request an extra randomized launch orientation here.
    if(found->second.definition.creation_flags&1U){
        const auto angles=*pose.native_angles;
        pose=rotation(random_next(run.rng_state)&0xFFFFU,angles[1],angles[2],true);pose.position=position;
    }
    append_environment_effect_sound(run,id,found->second.definition,position);
    SimulationLabOneShotEvent event;event.tick=run.tick;event.kind=SimulationLabOneShotEventKind::ModelTimedEffect;
    event.source_placement_index=entity.source_placement_index;event.army_asset_id=entity.army_asset_id;
    event.faction_index=entity.faction_index;event.effect_id=id;event.root_world_position=position;
    event.effect_world_basis=pose.basis;
    const auto& a=*pose.native_angles;event.effect_world_angles=std::array<uint32_t,3>{a[2],a[1],a[0]};event.actor=entity.actor;
    event.sequence=run.next_one_shot_event_sequence++;
    run.one_shot_event_tail=std::make_shared<const SimulationLabOneShotEventNode>(
        std::move(event),run.one_shot_event_tail);
    link_environmental_effect(run,id,pose);
    ++run.metrics.environmental_effects_spawned;
}
void SimulationLabExperiment::initialize_projectile_impact_effect(SimulationLabRun& run,
    const SimulationProjectileState& projectile,const ShotDefinitionRecord& shot,SimulationLabOneShotEvent& event){
    if(!runtime_catalog_)return;
    const auto effect=runtime_catalog_->effects.find(event.effect_id);
    if(effect==runtime_catalog_->effects.end())return;
    // Pool creation is shared by terrain, secondary-surface and army impacts.
    // Its orientation draw is independent of the timed-emitter launch channel.
    std::array<uint32_t,3> angles{0U,0x4000U,0U}; // worldRotationAngle0,1,2
    if(projectile.impact_kind==SimulationImpactKind::TargetModelImpactClass)
        angles={((projectile.runtime_flight_initialized?uint32_t(projectile.runtime_flight_yaw):
                    projectile.launch_yaw_turn16)+0x8000U)&0xFFFFU,
                0U-(projectile.runtime_flight_initialized?uint32_t(projectile.runtime_flight_pitch):
                    projectile.launch_pitch_turn16),0U};
    if(effect->second.definition.creation_flags&1U)angles[2]=random_next(run.rng_state)&0xFFFFU;
    event.effect_world_angles=angles;
    auto pose=rotation(angles[2],angles[1],angles[0],true);
    pose.position=event.root_world_position;event.effect_world_basis=pose.basis;
    std::optional<SimulationLabTerrainImpact> terrain_impact;
    const auto material=projectile.impact_material_index;
    if(projectile.impact_kind==SimulationImpactKind::TerrainSurface&&
       material<shot.terrain_impact_radii_q12.size()&&shot.terrain_impact_radii_q12[material]>0){
        if(const auto completion=effect_model_completion_tick(effect->second.definition))
            terrain_impact=SimulationLabTerrainImpact{*completion,shot.terrain_impact_radii_q12[material],
                std::bit_cast<int32_t>(shot.terrain_impact_effect_owner_slots[material]),
                shot.terrain_material_indices[material]};
    }
    // An impact journal entry cannot service linked EFF/SHT callbacks. Retain
    // an independent owner when the authored definition has simulation work.
    if(terrain_impact||effect_linked_effect_spawn_tick(effect->second.definition)||!effect->second.emissions.empty()){
        link_environmental_effect(run,event.effect_id,pose,false,terrain_impact);
        ++run.metrics.environmental_effects_spawned;
    }else if(run.level_runtime_enabled)link_environmental_effect(run,event.effect_id,pose,true);
}
void SimulationLabExperiment::claim_projectile_world_slot(SimulationLabRun& run,SimulationProjectileState& projectile){
    if(!run.level_runtime_enabled)return;
    if(projectile.owner_kind!=SimulationProjectileOwnerKind::Environmental&&
       projectile.owner_placement_index<run.entities.size())
        projectile.owner_army_pool_slot=run.entities[projectile.owner_placement_index].native_army_pool_slot;
    projectile.native_model_roll_turn16=projectile.launch_yaw_turn16;
    projectile.native_world_slot=run.native_world_objects.claim(
        SimulationLabWorldObjectPool::Kind::Shot,projectile.shot_definition_id);
    if(!runtime_catalog_)return;
    const auto marker=runtime_catalog_->shot_launch_effect_points.find(projectile.shot_definition_id);
    if(marker==runtime_catalog_->shot_launch_effect_points.end())return;
    const auto& shot=runtime_catalog_->shots.at(projectile.shot_definition_id);
    const auto effect=runtime_catalog_->effects.find(shot.launch_effect_id);
    if(effect==runtime_catalog_->effects.end())return;
    const auto yaw=projectile.launch_yaw_turn16,pitch=projectile.launch_pitch_turn16;
    auto pose=rotation(yaw,pitch,yaw,true);
    pose.position=projectile.launch_position;
    const auto position=point(pose,marker->second);
    if(effect->second.definition.creation_flags&1U)
        pose=rotation(random_next(run.rng_state)&0xFFFFU,pitch,yaw,true);
    pose.position=position;
    const bool has_callbacks=effect_linked_effect_spawn_tick(effect->second.definition)||!effect->second.emissions.empty();
    link_environmental_effect(run,shot.launch_effect_id,pose,!has_callbacks);
}

void SimulationLabExperiment::emit_projectile_trail_effect(SimulationLabRun& run,
    const SimulationProjectileState& projectile,const ShotDefinitionRecord& shot,uint32_t age){
    const auto interval=shot.secondary_effect_interval_ticks;
    if(!run.level_runtime_enabled||!runtime_catalog_||!interval||age%interval)return;
    const auto marker=runtime_catalog_->shot_trail_effect_points.find(projectile.shot_definition_id);
    if(marker==runtime_catalog_->shot_trail_effect_points.end())return;
    const auto effect=runtime_catalog_->effects.find(shot.secondary_effect_id);
    if(effect==runtime_catalog_->effects.end())return;
    const auto yaw=projectile.runtime_flight_initialized?
        uint32_t(projectile.runtime_flight_yaw):projectile.launch_yaw_turn16;
    const auto pitch=projectile.runtime_flight_initialized?
        uint32_t(projectile.runtime_flight_pitch):projectile.launch_pitch_turn16;
    auto shot_pose=rotation(projectile.native_model_roll_turn16,pitch,yaw,true);
    shot_pose.position=projectile.runtime_flight_initialized?
        projectile.runtime_flight_position:projectile.launch_position;
    const auto heading=(effect->second.definition.creation_flags&1U)?random_next(run.rng_state)&0xFFFFU:0U;
    auto pose=rotation(heading,0x4000U,0U,true);
    pose.position=point(shot_pose,marker->second);
    const bool has_callbacks=effect_linked_effect_spawn_tick(effect->second.definition)||!effect->second.emissions.empty();
    link_environmental_effect(run,shot.secondary_effect_id,pose,!has_callbacks);
}

std::size_t SimulationLabExperiment::link_environmental_effect(SimulationLabRun& run,uint32_t id,
    SimulationLabEffectPose pose,bool world_pool_only,std::optional<SimulationLabTerrainImpact> terrain_impact,
    bool before_owner_body,bool after_owner_body){
    SimulationLabEnvironmentalEffect instance{};
    instance.effect_id=id;
    instance.spawn_tick=run.tick;
    instance.pose=std::move(pose);
    instance.next_projectile_serial_at_creation=run.next_projectile_serial;
    instance.world_pool_only=world_pool_only;
    instance.terrain_impact=terrain_impact;
    if(run.level_runtime_enabled)
        instance.native_world_slot=run.native_world_objects.claim(
            SimulationLabWorldObjectPool::Kind::Effect,id,before_owner_body,after_owner_body);
    const auto handle=instance.native_world_slot;
    run.environmental_effects.push_back(std::move(instance));
    return handle;
}

void SimulationLabExperiment::advance_environmental_effects(SimulationLabRun& run,const FieldGrid& field){
    if(!runtime_catalog_)return;
    for(size_t visit=run.environmental_effects.size();visit!=0;--visit)
        advance_environmental_effect(run,field,visit-1);
    prune_environmental_effects(run);
}

void SimulationLabExperiment::advance_environmental_effect(SimulationLabRun& run,const FieldGrid& field,
    size_t index,const std::function<void()>& before_linked_effect_random){
    const auto& catalog=*runtime_catalog_;
    const auto& instance=run.environmental_effects[index];
    const auto world_scope=run.native_world_objects.enter_owner(instance.native_world_slot);
    const auto found=catalog.effects.find(instance.effect_id);if(found==catalog.effects.end())return;
    const auto& effect=found->second;const uint32_t age=run.tick-instance.spawn_tick;
    if(age==0U)return;
    const auto lifetime=effect_lifetime_ticks(effect.definition);
    struct ReleaseOnExit {
        SimulationLabRun& run;
        size_t index;
        bool due;
        ~ReleaseOnExit(){if(due)release_effect_world_slot(run,run.environmental_effects[index]);}
    } release{run,index,lifetime!=0U&&age>=lifetime};
    if(run.level_runtime_enabled&&effect.definition.transition_kind==4U&&
       !release.due&&instance.pose.native_angles){
        auto& live=run.environmental_effects[index];
        const auto normal=retail_contact_normal_angles(field,float_to_q12(live.pose.position.x),
            float_to_q12(live.pose.position.y));
        if(normal){
            constexpr int32_t q28_one=1<<28;
            auto& angles=*live.pose.native_angles;
            const auto pitch=angles[1]-age*age*effect.definition.terrain_pitch_drop_per_age_squared;
            const auto terrain=retail_direction_scaled(*normal>>16,*normal&0xFFFFU,q28_one);
            const auto direction=retail_direction_scaled(pitch,angles[2],q28_one);
            const auto dot=int64_t(terrain[0])*direction[0]+int64_t(terrain[1])*direction[1]+
                int64_t(terrain[2])*direction[2];
            if(dot<0){
                if(live.terrain_contact_alpha==0U){
                    live.ended_on_terrain=true;
                    release.due=true;
                    return;
                }
                live.terrain_contact_alpha-=std::min(live.terrain_contact_alpha,
                    effect.definition.terrain_contact_alpha_fade_step);
            }else{
                live.terrain_contact_alpha=255U;
                angles[1]=pitch;
            }
        }
    }
    if(effect.definition.transition_kind==2U&&age<lifetime&&instance.pose.native_angles){
        auto& live_pose=run.environmental_effects[index].pose;
        auto angles=*live_pose.native_angles;
        const auto delta=retail_direction_scaled(angles[1],angles[2],effect.definition.movement_speed_q12);
        const Vec3f moved{
            q12_to_float(float_to_q12(live_pose.position.x)+delta[0]),
            q12_to_float(float_to_q12(live_pose.position.y)+delta[1]),
            q12_to_float(float_to_q12(live_pose.position.z)+delta[2])};
        angles[1]=static_cast<uint32_t>((static_cast<int32_t>(angles[1])*63+0x4000)>>6);
        live_pose=rotation(angles[0],angles[1],angles[2],true);
        live_pose.position=moved;
    }
    if(instance.world_pool_only)return;
    const auto shot=catalog.shots.find(effect.definition.linked_shot_id);
    const auto sprite=catalog.sprites.find(effect.definition.sprite_path);
    if(shot!=catalog.shots.end()&&sprite!=catalog.sprites.end()){
        while(run.environmental_effects[index].next_emission<effect.emissions.size()&&
              effect.emissions[run.environmental_effects[index].next_emission].parent_age_tick<=age){
            auto& live=run.environmental_effects[index];
            const auto emission=effect.emissions[live.next_emission++];
            const auto& points=sprite->second.shot_points;
            auto attachment=std::find_if(points.begin(),points.end(),[&](const auto& p){return p.first==emission.attachment_index;});
            if(attachment==points.end()&&emission.attachment_index==0U)attachment=points.begin();
            if(attachment==points.end())continue;
            const float progress=std::clamp(float(emission.parent_age_tick)/float(std::max(1U,effect_lifetime_ticks(effect.definition))),0.F,1.F);
            const float scale=q12_to_float(effect.definition.model_scale_x_q12)+
                (q12_to_float(effect.definition.model_scale_y_q12)-q12_to_float(effect.definition.model_scale_x_q12))*progress;
            const auto p=attachment->second;
            const auto geometry=simulation_linked_effect_shot_geometry(live.pose.position,point(live.pose,{p.x*scale,p.y*scale,p.z*scale}));
            SimulationWeaponProfile weapon;weapon.shot_definition_id=shot->first;weapon.reload_ticks=1;weapon.launch_attachment_count=1;
            const std::array launch{geometry.launch_position};
            auto volley=simulation_create_volley(SIZE_MAX,0,weapon,launch,geometry.aim_position,shot->second,run.tick,
                run.next_projectile_serial,emission.attachment_index,std::nullopt,SimulationProjectileOwnerKind::Environmental,0,{},run.level_runtime_enabled);
            for(auto& projectile:volley){if(shot->second.trajectory_mode==0)simulation_resolve_projectile_surface_impact(projectile,shot->second,field);
                const auto life=catalog.effect_lifetimes.find(projectile.impact_effect_id);
                projectile.impact_visual_lifetime_ticks=life==catalog.effect_lifetimes.end()?1U:std::max(1U,life->second);
                claim_projectile_world_slot(run,projectile);
                run.projectiles.push_back(std::move(projectile));++run.metrics.environmental_shots_spawned;}
        }
    }
    const auto linked_tick=effect_linked_effect_spawn_tick(effect.definition);
    if(linked_tick&&age==*linked_tick){
        const auto linked=catalog.effects.find(effect.definition.linked_effect_id);
        const auto parent_sprite=catalog.sprites.find(effect.definition.sprite_path);
        if(linked!=catalog.effects.end()&&parent_sprite!=catalog.sprites.end()){
            const auto& points=parent_sprite->second.damage_effect_points;
            const auto attachment=std::find_if(points.begin(),points.end(),[](const auto& p){return p.first==0U;});
            if(attachment!=points.end()){
                auto pose=run.environmental_effects[index].pose;
                pose.position=point(pose,attachment->second);
                SimulationLabOneShotEvent event;
                event.tick=run.tick;
                event.source_placement_index=SIZE_MAX;
                event.kind=SimulationLabOneShotEventKind::ModelTimedEffect;
                event.effect_id=linked->first;
                event.root_world_position=pose.position;
                if(linked->second.definition.creation_flags&1U){
                    if(before_linked_effect_random)before_linked_effect_random();
                    const auto roll=random_next(run.rng_state)&0xFFFFU;
                    auto angles=pose.native_angles.value_or(std::array<uint32_t,3>{0U,
                        radians_to_turn16(std::asin(std::clamp(pose.basis[8],-1.F,1.F))),
                        radians_to_turn16(std::atan2(pose.basis[5],pose.basis[2]))});
                    angles[0]=roll;
                    pose.native_angles=angles;
                    pose.native_basis=retail_model_basis_q28(angles[0],angles[1],angles[2]);
                    for(size_t axis=0;axis<9;++axis)
                        pose.basis[axis]=float((*pose.native_basis)[axis])/268435456.F;
                    event.actor.body_yaw_turn16=roll;
                }
                event.effect_world_basis=pose.basis;
                event.sequence=run.next_one_shot_event_sequence++;
                run.one_shot_event_tail=std::make_shared<const SimulationLabOneShotEventNode>(
                    std::move(event),run.one_shot_event_tail);
                const bool has_callbacks=effect_linked_effect_spawn_tick(linked->second.definition)||
                    !linked->second.emissions.empty();
                if(has_callbacks||run.level_runtime_enabled)
                    link_environmental_effect(run,linked->first,pose,!has_callbacks);
            }
        }
    }
    const auto& live=run.environmental_effects[index];
    if(run.runtime_field&&live.terrain_impact&&age==live.terrain_impact->completion_age){
        const auto& impact=*live.terrain_impact;
        run.runtime_field->apply_runtime_terrain_impact(float_to_q12(live.pose.position.x),
            float_to_q12(live.pose.position.y),impact.radius_q12,impact.height_amplitude_q12,impact.material);
        run.runtime_normals_dirty=true;
    }
}

void SimulationLabExperiment::prune_environmental_effects(SimulationLabRun& run){
    const auto& catalog=*runtime_catalog_;
    for(auto& instance:run.environmental_effects){
        const auto e=catalog.effects.find(instance.effect_id);
        const auto lifetime=e==catalog.effects.end()?0U:effect_lifetime_ticks(e->second.definition);
        if(e==catalog.effects.end()||(lifetime&&run.tick-instance.spawn_tick>=lifetime)){
            const auto world_scope=run.native_world_objects.enter_owner(instance.native_world_slot);
            release_effect_world_slot(run,instance);
        }
    }
    std::erase_if(run.environmental_effects,[&](const auto& instance){const auto e=catalog.effects.find(instance.effect_id);
        if(instance.ended_on_terrain)return true;
        if(e==catalog.effects.end())return true;
        const auto authored_lifetime=effect_lifetime_ticks(e->second.definition);
        // The native EFF owner decrements its unsigned frame counter. An authored
        // zero therefore wraps rather than expiring immediately. This matters for
        // sparse static-emitter fixtures and for controller effects whose visual
        // lifetime is intentionally left open-ended.
        return authored_lifetime!=0U &&
            run.tick-instance.spawn_tick>=authored_lifetime;});
}
std::vector<SimulationLabLiveShotHull> simulation_lab_current_shot_hulls(const SimulationLabRun& run,
    const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog,bool ground_only,bool animated_only){
    std::vector<SimulationLabLiveShotHull> result;
    for(size_t i=0;i<run.entities.size();++i){const auto& entity=run.entities[i];
        if(entity.runtime_removed||!entity.construction_complete||!entity.runtime_model_hierarchy)continue;
        if(animated_only&&std::none_of(entity.model_components.begin(),entity.model_components.end(),[&](const auto& c){
            if(c.runtime_removed||c.source_model_node_index>=entity.runtime_model_hierarchy->size())return false;
            const auto& node=(*entity.runtime_model_hierarchy)[c.source_model_node_index];
            return (c.runtime_class_id==4U||c.runtime_class_id==10U||c.runtime_class_id==14U)&&
                (model_runtime_child_yaw_step_turn16(node,0U)!=0||model_runtime_child_yaw_step_turn16(node,1U)!=0||
                 model_runtime_child_uses_bounded_vertical_channel(node,2U));
        }))continue;
        if(ground_only&&entity.runtime_model_class_id!=1U&&entity.runtime_model_class_id!=2U&&entity.runtime_model_class_id!=3U&&entity.runtime_model_class_id!=17U&&entity.runtime_model_class_id!=18U&&entity.runtime_model_class_id!=19U)continue;
        const auto& hierarchy=*entity.runtime_model_hierarchy;
        const auto transforms=simulation_lab_current_model_poses(entity,field,catalog);
        const auto radii=subtree_radii(hierarchy,catalog);
        for(size_t n=0;n<hierarchy.size();++n){const auto& node=hierarchy[n];
            if(!simulation_lab_model_node_present(entity,n)||!node.creates_runtime_node()||
               ray_transparent(hierarchy,n))continue;
            const auto sprite=catalog.sprites.find(node.sprite_path);if(sprite==catalog.sprites.end()||!sprite->second.has_bounds)continue;
            // A child SPR inherits its nearest enclosing runtime MDL component.
            // Returning null for child sprites routed their impacts to the generic
            // collision component (often the building root), preventing detachable
            // turrets from receiving the authored share of projectile damage.
            std::optional<size_t> hit;
            size_t ancestor=n;
            for(size_t hops=0;hops<hierarchy.size();++hops){
                for(size_t ci=0;ci<entity.model_components.size();++ci){
                    if(entity.model_components[ci].source_model_node_index==ancestor){
                        hit=ci;break;
                    }
                }
                if(hit)break;
                if(hierarchy[ancestor].parent_index>=hierarchy.size())break;
                ancestor=hierarchy[ancestor].parent_index;
            }
            result.push_back({hull(sprite->second,transforms[n],node.shot_impact_class_index,radii[n]),i,hit});
        }
    }
    return result;
}
bool simulation_lab_refresh_environmental_projectile_contact(SimulationProjectileState& projectile,
    const ShotDefinitionRecord& shot,uint32_t current_tick,const SimulationLabRuntimeCatalog& catalog,
    std::span<const SimulationLabLiveShotHull> live_hulls,std::span<const SimulationLabStaticShotHull> static_hulls){
    if(current_tick<=projectile.spawn_tick||shot.trajectory_mode==0||shot.trajectory_mode==2)return false;
    const uint32_t age=current_tick-projectile.spawn_tick;if(age>projectile.lifetime_ticks||projectile.travel_ticks<age)return false;
    const auto origin=simulation_projectile_position(projectile,shot,current_tick-1),end=simulation_projectile_position(projectile,shot,current_tick);
    const auto delta=difference(end,origin);const float length2=dot(delta,delta);if(length2<=1.e-12F)return false;
    float nearest=2.F;uint32_t cls=0;std::optional<size_t> target,component;
    const auto test=[&](const SimulationLabStaticShotHull& h,std::optional<size_t> index,std::optional<size_t> node){if(const auto t=segment_hull(origin,delta,h);t&&*t<nearest){nearest=*t;cls=h.impact_class;target=index;component=node;}};
    for(const auto& h:live_hulls){
        if(projectile.owner_kind!=SimulationProjectileOwnerKind::Environmental&&
           h.entity_index==projectile.owner_placement_index)continue;
        test(h.hull,h.entity_index,h.component_index);
    }
    for(const auto& h:static_hulls)test(h,std::nullopt,std::nullopt);
    // Native first chooses the nearest model, then invalidates its distance
    // when its SHT effect slot is null. It does not search behind that owner.
    if(nearest>1.F||cls>=8U||shot.target_impact_effect_id(cls)==0U)return false;
    if(projectile.travel_ticks==age&&(projectile.impact_kind==SimulationImpactKind::TerrainSurface||projectile.impact_kind==SimulationImpactKind::SecondarySurface)){
        const auto d=difference(projectile.impact_position,origin);
        if(std::sqrt(dot(d,d))+1.e-6F<nearest*std::sqrt(length2))return false;
    }
    projectile.impact_kind=SimulationImpactKind::TargetModelImpactClass;projectile.impact_material_index=cls;
    projectile.impact_effect_id=shot.target_impact_effect_id(cls);projectile.impact_target_placement_index=target;projectile.impact_target_model_component_index=component;
    projectile.live_target_placement_index.reset();projectile.travel_ticks=age;
    projectile.impact_position={origin.x+delta.x*nearest,origin.y+delta.y*nearest,origin.z+delta.z*nearest};
    const auto life=catalog.effect_lifetimes.find(projectile.impact_effect_id);
    projectile.impact_visual_lifetime_ticks=life==catalog.effect_lifetimes.end()?1U:std::max(1U,life->second);
    return true;
}
}
