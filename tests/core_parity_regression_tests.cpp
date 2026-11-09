#include "thandor/model_animation.hpp"
#include "thandor/simulation_lab_audio.hpp"
#include <bit>
#include <iostream>
#include <stdexcept>
#include <random>
using namespace thandor;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int32_t native_bob(int32_t initial,int32_t low,int32_t high,int32_t step,uint32_t ticks){
    int64_t position=initial;bool rising=false;
    for(uint32_t i=0;i<ticks;++i){
        position+=rising?step:-int64_t(step);
        if(!rising&&position<low){position=low;rising=true;}
        else if(rising&&position>high){position=high;rising=false;}
    }
    return static_cast<int32_t>(position);
}
int32_t native_track(int32_t prior,int32_t distance,int32_t scale){
    int32_t value=std::bit_cast<int32_t>(uint32_t(prior)+uint32_t(distance)*uint32_t(scale));
    if(value>0x100000)value-=0x100000;
    else if(value< -0x100000)value+=0x100000;
    return value;
}
}
int main()try{
    std::mt19937 rng(644);
    for(int i=0;i<10000;++i){
        const int32_t low=int32_t(rng()%2001)-1000;
        const int32_t high=low+1+int32_t(rng()%4000);
        const int32_t step=1+int32_t(rng()%5000);
        const int32_t initial=low-200+int32_t(rng()%5000);
        const uint32_t tick=rng()%4000;
        check(model_runtime_child2_translation_q12(initial,low,high,step,tick)==
            native_bob(initial,low,high,step,tick),"bob differs from native callback sequence");
    }
    check(model_runtime_child2_translation_q12(10,0,10,5,0)==10,"authored pose lost at tick zero");
    check(model_runtime_child2_translation_q12(10,0,10,5,2)==0&&
          model_runtime_child2_translation_q12(10,0,10,5,3)==0,"strict endpoint dwell lost");
    check(model_runtime_child2_translation_q12(10,0,10,5,UINT32_MAX)==0,"long-running bob overflowed");
    for(const int32_t scale:{259,1000000,INT32_MAX,INT32_MIN,-259}){
        SimulationActorState actor;actor.primary_track_texture_offset_u=0xFFFF0;
        simulation_update_tank_track_offsets(actor,{0,0,0},0,{1,0,0},0,-0.5F,scale);
        check(actor.primary_track_texture_offset_u==native_track(0xFFFF0,4096,scale),"track arithmetic differs from native signed32/single wrap");
    }
    SimulationActorState backward;
    simulation_update_tank_track_offsets(backward,{0,0,0},0,{-0.1F,0,0},0,-0.5F,259);
    check(backward.primary_track_texture_offset_u<0&&backward.secondary_track_texture_offset_u<0,"reverse tracks did not reverse");
    SimulationRuntimeSoundProfile turret{5,0,8,0,0};
    check(simulation_runtime_looping_sound_indices(turret,{true,true,false,false})[0]==0,"translation triggered turret motor without axis rotation");
    check(simulation_runtime_looping_sound_indices(turret,{true,false,true,false})[0]==8,"turning turret silent");
    // Same serialized slot from two owners: later hidden/out-of-range/muted
    // requests must leave the earlier audible gains, as the native pool does.
    const SpatialAudioListener listener{{0,0,0},{1,0,0},{0,1,0}};
    std::vector<SimulationLabSoundRequest> requests{
        {7,{1,0,0},0x8000,409600},{7,{1000,0,0},0x8000,409600},
        {7,{2,0,0},0,409600},{7,{3,0,0},0x8000,409600}};
    auto audible=simulation_lab_audible_loops(requests,listener,0x8000,
        [](Vec3f p){return p.x!=3;});
    check(audible.requests.at(7).world_position.x==1&&audible.inaudible==2&&audible.fog_filtered==1,"inaudible owner erased audible shared slot");
    audible=simulation_lab_audible_loops(requests,listener,0x8000);
    check(audible.requests.at(7).world_position.x==3,"last audible owner did not win shared slot");
    check(simulation_lab_audible_loops(requests,listener,0).requests.empty(),"muted shared slots retained gains");
    SimulationLabRun run;SimulationLabRuntimeCatalog catalog;
    SimulationLabEntity a;a.construction_complete=true;a.runtime_model_class_id=2;
    a.last_translated=true;a.runtime_sound_profile={2,7,8,0,0};
    a.positioned_sound_gain_q15=0x8000;a.positioned_sound_maximum_distance_q12=409600;
    a.actor.position={1,0,0};run.entities.push_back(a);
    a.actor.position={1000,0,0};run.entities.push_back(a);
    const auto frame=simulation_lab_audio_frame(run,catalog,{});
    check(frame.ordered_loops.size()==2,"collector collapsed shared-slot owners before attenuation");
    check(simulation_lab_audible_loops(frame.ordered_loops,listener,0x8000).requests.at(7).world_position.x==1,"collector-to-presentation shared slot regression");
    run.entities.resize(1);
    std::vector<ModelVisualNode> hierarchy(2);
    hierarchy[0].runtime_class_id=2;hierarchy[0].sprite_path="root";
    hierarchy[1].parent_index=0;hierarchy[1].child_slot=0;
    hierarchy[1].runtime_class_id=5;hierarchy[1].sprite_path="turret";
    hierarchy[1].local_rotation_angle1=0x4000;
    hierarchy[1].simulation.projected_sound_asset_id_d8=12;
    hierarchy[1].simulation.positioned_sound_gain_q15=0x6000;
    hierarchy[1].simulation.positioned_sound_maximum_distance_q12=409600;
    auto& owner=run.entities[0];
    owner.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(hierarchy);
    owner.combat_weapons.resize(1);owner.actor.weapons.resize(1);
    owner.combat_weapons[0].profile.yaw_model_node_index=1;
    owner.actor.weapons[0].yaw.velocity_turn16=64;
    catalog.sprites["root"].child_translations_q12[0]=std::array<int32_t,3>{8192,0,0};
    const auto field=FieldGrid::create(20,20,0,0);
    auto child_frame=simulation_lab_audio_frame(run,catalog,{},&field);
    check(child_frame.loops.contains(12),"attached turret axis lost motor sound");
    check(child_frame.loops.at(12).world_position.x==3&&child_frame.loops.at(12).gain_q15==0x6000,"turret sound did not use its attachment origin and MDL gain");
    owner.actor.weapons[0].yaw.velocity_turn16=0;
    check(!simulation_lab_audio_frame(run,catalog,{},&field).loops.contains(12),"moving root triggered stationary child turret motor");
    owner.actor.weapons[0].pitch.velocity_turn16=64;
    owner.combat_weapons[0].profile.pitch_model_node_index=1;
    check(simulation_lab_audio_frame(run,catalog,{},&field).loops.contains(12),"pitch-only turret axis silent");
    owner.model_components.resize(1);owner.model_components[0].source_model_node_index=1;
    owner.model_components[0].runtime_removed=true;
    check(!simulation_lab_audio_frame(run,catalog,{},&field).loops.contains(12),"released turret attachment retained motor");
    std::cout<<"V644 PASS: 10000 native bob sequences, endpoints, long clock, signed32 tracks, reverse, turret gate, shared-slot fog/range/mute/order\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
