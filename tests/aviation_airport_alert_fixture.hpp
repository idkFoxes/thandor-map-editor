#pragma once
#include "thandor/simulation_lab_audio.hpp"
#include "thandor/fog_preview.hpp"
#include <algorithm>
#include <stdexcept>

namespace v650 {
using namespace thandor;
inline void check(bool v,const char* message) { if(!v)throw std::runtime_error(message); }
struct AlertCase {
    uint32_t source{2},listener{1},timer{17},batch{1};
    bool friendly{},reverse_friendly{},sound_valid{true};
    uint8_t occupancy{0x10};
    int32_t countdown{100},x{},y{};
};
inline SimulationLabExperiment alert_lab(const FieldGrid& field,const AlertCase& c) {
    SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=1000;
    cfg.runtime_callback_step_ticks=c.batch;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
    SimulationLabSeedEntity plane;plane.army_asset_id=240;
    plane.faction_index=c.source;plane.runtime_model_class_id=21;
    plane.root_model_maximum_integrity=1000;
    plane.actor.position={q12_to_float(c.x),q12_to_float(c.y),0};
    SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;
    scenario.active_faction_count=7;scenario.ai_planning_enabled=false;
    scenario.active_view_faction_index=c.listener;
    if(c.sound_valid)scenario.mapped_terrain_sound_asset_ids={9};
    SimulationLabExperiment lab;
    lab.reset(cfg,ki,std::span(&plane,1),{},{},{},{},{},scenario);
    auto& r=lab.runs()[0];r.tick=1;
    r.faction_relation_transition_ticks[c.listener]=c.timer;
    r.faction_packed_relation_states[c.source]&=~(15U<<(c.listener*4U));
    if(c.friendly)r.faction_packed_relation_states[c.source]|=4U<<(c.listener*4U);
    if(c.source!=c.listener){
        r.faction_packed_relation_states[c.listener]&=~(15U<<(c.source*4U));
        if(c.reverse_friendly)r.faction_packed_relation_states[c.listener]|=4U<<(c.source*4U);
    }
    r.current_visibility_occupancy.assign(size_t(field.width())*field.height(),
                                         uint64_t(c.occupancy)<<(c.listener*8U));
    auto nodes=std::make_shared<std::vector<ModelVisualNode>>(1);
    nodes->front().parent_index=UINT32_MAX;
    nodes->front().runtime_class_id=21;
    nodes->front().simulation.runtime_class_effect_sound_asset_id_26c=9;
    auto& e=r.entities[0];e.runtime_model_hierarchy=nodes;
    e.aircraft.initialized=true;
    e.aircraft.runtime.phase=SimulationClass21LinkedChildPhase::MissionPass;
    e.aircraft.runtime.mission_countdown=c.countdown;
    return lab;
}
inline std::vector<SimulationLabOneShotEvent> alerts(const SimulationLabRun& run) {
    auto events=simulation_lab_one_shot_events_after(run,0);
    std::erase_if(events,[](const auto& e){return e.kind!=SimulationLabOneShotEventKind::GlobalMappedTerrainSound;});
    return events;
}
}
