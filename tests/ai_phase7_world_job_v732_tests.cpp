#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_phase7_occupancy.hpp"
#include "thandor/simulation_lab_phase7_owner.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace thandor;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
SimulationLabExperiment create_lab(bool ended, std::uint32_t active,
                                   std::span<const SimulationLabSeedEntity> seeds) {
    SimulationLabScenarioRuntimeSeed scenario{};
    scenario.enabled=true;
    scenario.active_faction_count=2;
    scenario.assignable_faction_count=2;
    scenario.ai_planning_enabled=false;
    scenario.local_faction_ended=ended;
    scenario.active_view_faction_index=active;
    SimulationLabConfig config{};
    config.run_count=1;
    config.duration_ticks=64;
    std::array<std::uint32_t,AiKnowledgeData::kParameterCount> params{};
    SimulationLabExperiment lab;
    lab.reset(config,params,seeds,{},{},{},{},{},scenario);
    return lab;
}
std::uint8_t faction_byte(std::uint64_t mask, std::uint32_t faction) {
    return static_cast<std::uint8_t>(mask>>(faction*8U));
}
}
int main() try {
    require(simulation_lab_phase7_ended_faction_mask(1,true)==0x02U,"ended faction 1 bit missing");
    require(simulation_lab_phase7_ended_faction_mask(7,true)==0x80U,"ended faction 7 bit missing");
    require(simulation_lab_phase7_ended_faction_mask(1,false)==0U,"normal run incorrectly ended");
    require(simulation_lab_phase7_ended_faction_mask(0,true)==0U,"neutral slot admitted");
    require(simulation_lab_phase7_ended_faction_mask(8,true)==0U,"out-of-range slot admitted");

    SimulationLabRun owner_image;
    require(!simulation_lab_phase7_maybe_has_world_owner(owner_image,0),"empty owner image nonempty");
    require(simulation_lab_phase7_maybe_has_world_owner(owner_image,1),"static model owner not recognized");
    owner_image.entities.resize(1);
    require(simulation_lab_phase7_maybe_has_world_owner(owner_image,0),"linked root missing");
    owner_image.entities[0].root_model_component_index=0U;
    owner_image.entities[0].model_components.resize(1);
    owner_image.entities[0].root_model_terminal=true;
    require(simulation_lab_phase7_maybe_has_world_owner(owner_image,0),"terminal effect prematurely removed root");
    owner_image.entities[0].model_components[0].runtime_removed=true;
    require(!simulation_lab_phase7_maybe_has_world_owner(owner_image,0),"released root remains linked");

    auto field=FieldGrid::create(64,64,0,0);
    auto ended=create_lab(true,2,{});
    require(ended.runs()[0].local_faction_ended,"scenario ended flag not propagated");
    ended.step(field);
    const auto initial=ended.runs()[0].retail_scratch;
    require(initial!=nullptr,"missing initial scratch");
    const std::size_t index=(24U*4U)*initial->width+24U*4U;
    const auto initial_presence=initial->state[index]&0xFEU;
    ended.step(field,6);
    const auto& finished=ended.runs()[0];
    require((finished.tick&7U)==7U,"fixture missed world phase 7");
    require(finished.retail_scratch!=nullptr,"phase 7 missing scratch");
    require(finished.retail_scratch->presence_update_tick==finished.tick,"phase 7 not run");
    require(finished.retail_scratch->field_occupancy_masks.size()==64U*64U,"missing raw occupancy");
    for (auto mask:finished.retail_scratch->field_occupancy_masks) {
        require((faction_byte(mask,2)&0x01U)!=0U,"ended-faction bit 0 absent from field cell");
        require((faction_byte(mask,1)&0x01U)==0U,"ended-faction bit leaked into another slot");
    }
    require((finished.retail_scratch->state[index]&0xFEU)==initial_presence,
        "empty world-owner list propagated faction presence into scratch");

    auto normal=create_lab(false,2,{});
    normal.step(field,7);
    const auto& normal_scratch=*normal.runs()[0].retail_scratch;
    for (auto mask:normal_scratch.field_occupancy_masks)
        require((faction_byte(mask,2)&0x01U)==0U,"normal run got full-map bit 0");

    SimulationLabSeedEntity seed{};
    seed.army_asset_id=50;
    seed.faction_index=2;
    seed.runtime_model_class_id=18;
    seed.actor.position={q12_to_float(field.at(24,24).world_x_q12),
                         q12_to_float(field.at(24,24).world_y_q12),0};
    seed.root_model_maximum_integrity=1000;
    const std::array seeds{seed};
    auto live=create_lab(true,2,seeds);
    live.step(field,7);
    const auto& live_scratch=*live.runs()[0].retail_scratch;
    bool propagated=false;
    for(std::size_t dy=0;dy<4U;++dy)
        for(std::size_t dx=0;dx<4U;++dx)
            if((live_scratch.state[(24U*4U+dy)*live_scratch.width+24U*4U+dx]&0x04U)!=0U)
                propagated=true;
    require(propagated,"linked world model did not allow ended-faction scratch propagation");

    // A previously linked owner can leave a cached projected marker behind.
    // Phase 7 must clear that transient marker when the owner chain is empty;
    // only exploration bit 7 and the explicit ended-faction bit can persist.
    auto& released=live.runs()[0];
    released.entities[0].runtime_removed=true;
    const auto centre=static_cast<std::size_t>(24U)*64U+24U;
    released.current_visibility_occupancy.assign(64U*64U,0U);
    released.current_visibility_occupancy[centre]=0x18ULL<<16U;
    require(!simulation_lab_phase7_maybe_has_world_owner(released,0),
        "removed runtime still counts as linked owner");
    live.step(field,8);
    const auto& after_release=live.runs()[0];
    require((after_release.tick&7U)==7U,"fixture missed next phase-7 refresh");
    require(after_release.retail_scratch->presence_update_tick==after_release.tick,
        "post-release phase-7 snapshot not produced");
    const auto byte=faction_byte(after_release.retail_scratch->field_occupancy_masks[centre],2);
    require((byte&0x78U)==0U,"unlinked owner leaked a transient projected marker");
    require((byte&0x01U)!=0U,"ended-faction state cleared on empty owner list");
    std::cout<<"V733_PHASE7_WORLD_JOB PASS markers, ended faction, owner release and empty-list propagation\n";
} catch (const std::exception& error) {
    std::cerr<<"V732_PHASE7_WORLD_JOB FAIL "<<error.what()<<'\n';
    return 1;
}
