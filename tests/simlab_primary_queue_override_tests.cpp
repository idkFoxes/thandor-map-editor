#include "thandor/simulation_lab.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

thandor::SimulationLabProductionAsset product(std::uint32_t id, std::uint32_t runtime_class,
                                               std::int32_t c0 = 0) {
    thandor::SimulationLabProductionAsset value;
    value.army_asset_id = id;
    value.army_runtime_flags14_raw = 1U;
    value.runtime_class_specific_c0_raw = c0;
    value.entity_seed.army_asset_id = id;
    value.entity_seed.runtime_model_class_id = runtime_class;
    value.entity_seed.building = true;
    return value;
}

thandor::SimulationLabSeedEntity pioneer(std::uint32_t faction) {
    thandor::SimulationLabSeedEntity value;
    value.army_asset_id = 50U;
    value.faction_index = faction;
    value.runtime_model_class_id = 18U;
    return value;
}
} // namespace

int main() try {
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
    std::array<thandor::SimulationLabSeedEntity, 2> seeds{pioneer(1U), pioneer(2U)};
    std::vector<thandor::SimulationLabProductionAsset> products{
        product(300U, 11U), product(301U, 14U, 0), product(302U, 14U, 1), product(303U, 16U),
        product(410U, 11U), product(411U, 14U, 0),
    };

    thandor::SimulationLabConfig config;
    config.run_count = 1U;
    config.parameter_index = 100U;
    config.sweep_mode = thandor::SimulationLabSweepMode::Fixed;
    config.primary_queue_overrides[1].enabled = true;
    config.primary_queue_overrides[1].count = 3U;
    config.primary_queue_overrides[1].army_asset_ids[0] = 410U;
    config.primary_queue_overrides[1].army_asset_ids[1] = 0xDEADBEEFU; // unresolved: must not poison queue
    config.primary_queue_overrides[1].army_asset_ids[2] = 411U;
    // Enabled zero-count is an explicit empty primary inventory and suppresses the retail fallback.
    config.primary_queue_overrides[2].enabled = true;
    config.primary_queue_overrides[2].count = 0U;

    thandor::SimulationLabExperiment experiment;
    experiment.reset(config, parameters, seeds, {}, products);
    const auto& run = experiment.runs().front();

    require(run.pending_asset_requests.size() == 2U,
            "Lab primary override did not replace the derived queue or retained an invalid ARM id");
    require(run.pending_asset_requests[0].faction_index == 1U &&
                run.pending_asset_requests[0].army_asset_id == 410U &&
                run.pending_asset_requests[1].faction_index == 1U &&
                run.pending_asset_requests[1].army_asset_id == 411U,
            "Lab primary override did not preserve the entered valid ARM order");
    require(run.metrics.lab_primary_queue_override_factions == 2U &&
                run.metrics.lab_primary_queue_override_assets == 2U,
            "Lab primary override diagnostics are wrong");
    require(run.metrics.retail_initial_primary_queue_factions == 0U &&
                run.retail_initial_primary_queue_faction_mask == 0U,
            "enabled Lab override did not suppress the retail-derived Pioneer-only queue");

    std::cout << "V569_SIMULATION_LAB_PRIMARY_QUEUE_OVERRIDE PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V569_SIMULATION_LAB_PRIMARY_QUEUE_OVERRIDE FAIL: " << error.what() << '\n';
    return 1;
}
