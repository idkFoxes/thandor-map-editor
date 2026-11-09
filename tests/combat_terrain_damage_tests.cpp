#include "thandor/simulation_terrain_damage.hpp"
#include "thandor/simulation_lab.hpp"
#include "thandor/model.hpp"
#include "thandor/catalog.hpp"
#include "thandor/binary.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv)try{
    std::vector<std::byte> mdl(0x200+0x19c);
    write_u32(mdl,0,fourcc("mdl"));write_u32(mdl,4,mdl.size());write_u32(mdl,0xb0,1);
    write_u32(mdl,0x200,0x19c);write_u32(mdl,0x208,123);write_u32(mdl,0x24c,1);
    write_u32(mdl,0x394,128);write_u32(mdl,0x398,100);
    for(size_t i=0;i<sizeof("spr/test.spr")-1;++i)write_u16(mdl,0x300+i*2,uint16_t("spr/test.spr"[i]));
    const auto records=parse_model_visual_records(mdl,"fixture.mdl");
    check(records[0].simulation.water_damage_multiplier==128&&records[0].simulation.water_damage_threshold_q12==100,
        "class-1 MDL water damage fields were discarded");
    auto registered=mdl;registered.resize(0x500);
    write_u32(registered,4,registered.size());write_u32(registered,0x200,0x300);
    write_u32(registered,0x464,4);write_u32(registered,0x398,0);
    const auto registered_records=parse_model_visual_records(registered,"registered.mdl");
    check(registered_records[0].simulation.water_damage_threshold_q12==500,
        "native terrain-class registration did not replace serialized water threshold");
    auto field=FieldGrid::create(16,16,0,0);for(auto& c:field.cells())c.water_delta_q12=256;
    const Vec3f position{3,-1,0};
    std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabConfig config;config.run_count=1;config.duration_ticks=20;
    unsigned cases=0;
    for(uint32_t cls=0;cls<24;++cls)for(int contact:{0,1}){
        SimulationLabSeedEntity seed;seed.army_asset_id=1;seed.faction_index=1;seed.actor.position=position;
        seed.runtime_model_class_id=cls;seed.water_damage_multiplier=128;seed.water_damage_threshold_q12=100;
        SimulationLabModelComponentSeed c;c.runtime_class_id=cls;c.placement_contact_kind=contact;c.maximum_integrity=10000;
        seed.model_components={c};SimulationLabExperiment lab;lab.reset(config,parameters,std::span(&seed,1));lab.step(field);
        const bool native_ground=(cls==1||cls==2||cls==3||(cls==18&&contact!=1));
        check(lab.runs()[0].entities[0].root_model_integrity==(native_ground?9744:10000),
            "water-damage callback dispatch differs from native class/contact table");++cases;
    }
    check(!simulation_water_damage_amount(100,100,128),"threshold equality damaged owner");
    check(simulation_water_damage_amount(101,100,128)==101,"damage subtracted threshold instead of testing it");
    check(!simulation_water_damage_amount(256,0,-1),"negative damage became repair");
    check(simulation_water_damage_amount(256,0,0)==0,"zero callback application disappeared");
    SimulationLabSeedEntity seed;seed.faction_index=1;seed.actor.position=position;seed.runtime_model_class_id=1;
    seed.water_damage_multiplier=128;seed.root_model_maximum_integrity=500;
    SimulationLabExperiment death;death.reset(config,parameters,std::span(&seed,1));death.step(field,2);
    check(death.runs()[0].entities[0].root_model_terminal,"depth damage did not enter normal terminal pipeline");
    check(death.runs()[0].metrics.root_relation_counter_c_events==0&&death.runs()[0].metrics.root_relation_counter_d_events==0,
        "environmental depth damage credited a hostile faction");
    seed.actor.position={-1000,1000,0};SimulationLabExperiment outside;outside.reset(config,parameters,std::span(&seed,1));outside.step(field);
    check(outside.runs()[0].entities[0].root_model_integrity==500,"outside owner sampled clamped wet border");
    {
        auto shoreline=FieldGrid::create(20,20,0,0);
        for(uint32_t y=0;y<shoreline.height();++y)
            for(uint32_t x=7;x<shoreline.width();++x)shoreline.at(x,y).water_delta_q12=256;
        const auto& start=shoreline.at(6,6);
        SimulationLabSeedEntity crossing;crossing.faction_index=1;crossing.runtime_model_class_id=1;
        crossing.root_model_maximum_integrity=10000;crossing.water_damage_multiplier=128;
        crossing.actor.position={q12_to_float(start.world_x_q12),q12_to_float(start.world_y_q12),0};
        crossing.actor.move_target=Vec3f{crossing.actor.position.x+4,crossing.actor.position.y,0};
        crossing.motion.speed_q12_per_tick=4096;crossing.motion.ground_speed_control=true;
        crossing.motion.ground_acceleration_q12=4096;crossing.motion.yaw_max_velocity_turn16=0x7fff;
        crossing.motion.yaw_acceleration_turn16=0x7fff;
        SimulationLabScenarioRuntimeSeed session;session.enabled=true;session.active_faction_count=1;
        session.ai_planning_enabled=false;
        SimulationLabExperiment moving;moving.reset(config,parameters,std::span(&crossing,1),{},{},{},{},{},session);
        moving.step(shoreline);
        const auto& after=moving.runs()[0].entities[0];
        check(shoreline.sample_retail_water_delta_q12(float_to_q12(after.actor.position.x),
            float_to_q12(after.actor.position.y))>0,"shoreline fixture never entered water");
        check(after.root_model_integrity==10000,"native water damage sampled after movement");
        moving.step(shoreline);
        check(moving.runs()[0].entities[0].root_model_integrity<10000,
            "native water damage skipped the next callback's wet starting point");
    }
    if(argc>1){
        const auto catalog=AssetCatalog::scan_archive_root(argv[1]);size_t eligible=0,class1=0,wet=0,water_surface=0;
        for(const auto& p:catalog.army_presets()){
            if(p.browser_kind!=PlacementBrowserKind::Unit)continue;
            if(p.runtime_model_class_id==18&&p.simulation_motion_profile.placement_contact_kind==1)++water_surface;
            if(!simulation_uses_water_damage_callback(p.runtime_model_class_id,p.simulation_motion_profile.placement_contact_kind))continue;
            ++eligible;if(p.runtime_model_class_id==1&&p.water_damage_multiplier>0)++class1;
            if(p.water_damage_multiplier>0){++wet;auto s=seed;s.actor.position=position;s.runtime_model_class_id=p.runtime_model_class_id;
                s.water_damage_multiplier=p.water_damage_multiplier;s.water_damage_threshold_q12=p.water_damage_threshold_q12;
                s.root_model_maximum_integrity=1000000;
                auto deep=field;for(auto& cell:deep.cells())cell.water_delta_q12=std::max(4096,p.water_damage_threshold_q12+1);
                const auto amount=simulation_water_damage_amount(deep.sample_retail_water_delta_q12(12288,-4096),s.water_damage_threshold_q12,s.water_damage_multiplier);
                SimulationLabExperiment l;l.reset(config,parameters,std::span(&s,1));l.step(deep);
                check(l.runs()[0].entities[0].root_model_integrity==std::max(0,1000000-amount.value_or(0)),"stock model failed depth damage");
            }
        }
        check(class1>0,"stock class-1 models still have no decoded water damage");
        std::cout<<"STOCK_DEPTH_DAMAGE eligible="<<eligible<<" class1_with_damage="<<class1<<" positive_multipliers="<<wet<<" water_surface_class18="<<water_surface<<'\n';
    }
    std::cout<<"TERRAIN_DAMAGE PASS dispatch_cases="<<cases<<" threshold signed_math terminal outside\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
