#define main v650_unused_stock_replay_main
#include "simlab_stock_replay.cpp"
#undef main
#include "thandor/simulation_lab_audio.hpp"
#include "thandor/audio.hpp"
int main(int argc,char** argv) try {
    if(argc!=2)throw std::runtime_error("usage: stock_airport_alert_probe PCK_ROOT");
    const auto catalog=AssetCatalog::scan_archive_root(argv[1]);
    const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
    std::vector<uint32_t> sound_ids;
    uint32_t levels=0;
    for(const auto& group:catalog.map_groups()){
        const auto level=LevelAsset::parse(catalog.load(group.level_path));
        const auto ids=simulation_lab_prepare_sound_asset_indices(catalog,level);
        if(ids.empty())throw std::runtime_error("empty stock level sound table: "+group.level_path);
        if(sound_ids.empty())sound_ids=ids;
        else if(ids!=sound_ids)throw std::runtime_error("stock sound table differs; probe must select per level");
        ++levels;
    }
    const auto field=FieldGrid::create(32,32,0,0);
    std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
    uint32_t cases=0;
    for(bool upgraded:{false,true})for(const auto& preset:catalog.army_presets()){
        if(preset.runtime_model_class_id!=21)continue;
        FactionTechnologyMask256 mask{};if(upgraded)mask.fill(UINT32_MAX);
        auto seed=seed_for(preset,catalog,mask);seed.faction_index=2;
        seed.actor.position={q12_to_float(field.at(12,12).world_x_q12),q12_to_float(field.at(12,12).world_y_q12),0};
        for(uint32_t batch:{1U,5U}){
            SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=100;
            cfg.runtime_callback_step_ticks=batch;
            SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;
            scenario.active_faction_count=2;scenario.ai_planning_enabled=false;
            scenario.runtime_catalog=runtime;scenario.mapped_terrain_sound_asset_ids=sound_ids;
            std::array<FactionTechnologyMask256,8> masks{};masks[2]=mask;
            SimulationLabExperiment lab;lab.reset(cfg,ki,std::span(&seed,1),{},{},{},masks,{},scenario);
            auto& r=lab.runs()[0];r.tick=1;auto& plane=r.entities[0];
            plane.aircraft.initialized=true;
            plane.aircraft.runtime.phase=SimulationClass21LinkedChildPhase::MissionPass;
            plane.aircraft.runtime.mission_countdown=1000;
            // Controlled recorded-world inputs isolate the alert callback. This
            // is not an autonomous flight, fog or diplomacy reconstruction.
            plane.aircraft_definition.speed_q12=0;
            r.current_visibility_occupancy.assign(32*32,uint64_t{0x10}<<8);
            lab.step(field,1);
            auto events=simulation_lab_one_shot_events_after(r,0);
            std::erase_if(events,[](const auto& e){return e.kind!=SimulationLabOneShotEventKind::GlobalMappedTerrainSound;});
            if(events.size()!=1||events[0].sound_listener_faction_index!=1)
                throw std::runtime_error("stock aircraft alert not emitted exactly once");
            const auto frame=simulation_lab_audio_frame(r,*runtime,events);
            if(frame.global_one_shots.size()!=1||!frame.one_shots.empty())
                throw std::runtime_error("stock alert entered spatial transport");
            const auto id=events[0].sound_asset_id;
            const auto path="sound/sound"+(id<10?std::string("0"):std::string{})+std::to_string(id)+".sam";
            const auto pcm=decode_sam_to_pcm(catalog.load(path));
            if(pcm.empty())throw std::runtime_error("empty alert PCM");
            ++cases;std::cout<<"STOCK_ALERT arm="<<preset.registry_id<<" upgraded="<<upgraded
                <<" batch="<<batch<<" sound="<<id<<" frames="<<pcm.interleaved_stereo.size()/2<<'\n';
        }
    }
    if(cases!=12)throw std::runtime_error("stock aircraft variant coverage changed");
    std::cout<<"V650_STOCK_AIRPORT_ALERT PASS cases="<<cases<<" level_sound_tables="<<levels
        <<" resolved_slots="<<sound_ids.size()<<"; controlled occupancy, original stock hierarchy and PCM, no speaker output\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
