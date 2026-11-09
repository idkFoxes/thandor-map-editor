#include "thandor/simulation_lab_ai_grid_classes.hpp"
#include <cassert>
#include <cstdint>
#include <initializer_list>

using namespace thandor;

static SimulationLabEntity unit(std::uint32_t low, std::uint32_t high=0U) {
    SimulationLabEntity e;
    e.army_asset_id=2U;
    e.faction_index=1U;
    e.has_traversal_profile=true;
    e.motion.speed_q12_per_tick=128;
    e.traversal_low_band_index=low;
    e.traversal_high_band_index=high;
    return e;
}
static std::uint32_t mask(std::uint32_t low, std::uint32_t high=0U) {
    return 0x80000000U | (0x100U<<low) | (0x01000000U<<high);
}
int main() {
    SimulationLabRun run;
    // A synthetic run must use the newest 64 admitted linked units. Older
    // units with different profiles must not occupy the four class slots.
    for(std::uint32_t i=0;i<6U;++i)run.entities.push_back(unit(i+1));
    for(std::uint32_t i=0;i<64U;++i)run.entities.push_back(unit(0));
    for(const bool level_mode : {false,true}) {
        run.level_runtime_enabled=level_mode;
        const auto classes=simulation_lab_ai_active_grid_classes(run,1U);
        assert(classes[0]==mask(0));
        assert(classes[1]==0xFFFFFFFFU);
        assert(classes[2]==0xFFFFFFFFU);
        assert(classes[3]==0xFFFFFFFFU);
    }
    // All linked owners still count even when a terminal effect has started.
    run.entities.clear();
    run.entities.push_back(unit(1));
    run.entities.back().root_model_terminal=true;
    run.entities.back().construction_complete=false;
    assert(simulation_lab_ai_active_grid_classes(run,1U)[0]==mask(1));
    // Unlinked owners must not leak an old traversal mask.
    run.entities.back().runtime_removed=true;
    assert(simulation_lab_ai_active_grid_classes(run,1U)[0]==0x90000100U);
    // Only four unique masks, in newest-first Workspace 01 order.
    run.entities.clear();
    for(std::uint32_t i=0;i<6U;++i)run.entities.push_back(unit(i));
    const auto classes=simulation_lab_ai_active_grid_classes(run,1U);
    assert(classes[0]==mask(5));
    assert(classes[1]==mask(4));
    assert(classes[2]==mask(3));
    assert(classes[3]==mask(2));
    // The native C++ definition tests negative sentinels, not a 7/8
    // maximum: high class 7 still contributes its shifted (overlapping)
    // terrain bit; low class 8 remains a valid shifted band.
    run.entities.clear();
    run.entities.push_back(unit(8,7));
    const auto extended=simulation_lab_ai_active_grid_classes(run,1U);
    assert(extended[0]==(0x80000000U | (0x100U<<8) | (0x01000000U<<7)));
    run.entities.back().traversal_high_band_index=UINT32_MAX;
    assert(simulation_lab_ai_active_grid_classes(run,1U)[0]==0x90000100U);
    // An authored MDL uses +0x18 acceleration, not the runtime speed.
    run.entities.clear();
    auto authored=unit(1);
    authored.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(1U);
    authored.motion.ground_acceleration_q12=0U;
    run.entities.push_back(authored);
    assert(simulation_lab_ai_active_grid_classes(run,1U)[0]==0x90000100U);
    run.entities.back().motion.speed_q12_per_tick=0U;
    run.entities.back().motion.ground_acceleration_q12=32U;
    assert(simulation_lab_ai_active_grid_classes(run,1U)[0]==mask(1));
}
