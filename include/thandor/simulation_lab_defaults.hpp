#pragma once
#include "thandor/simulation_lab.hpp"
#include <algorithm>
#include <bit>
#include <limits>

namespace thandor {
// UI/replay defaults preserve every loaded KI word. The core experiment API keeps
// its explicit sweep semantics; callers can select a sweep after loading this baseline.
inline SimulationLabConfig simulation_lab_loaded_baseline_config(
    std::span<const uint32_t,AiKnowledgeData::kParameterCount> parameters,uint32_t index=1U) {
    SimulationLabConfig config;
    config.run_count=1U;config.vary_seed_per_run=true;
    config.parameter_index=std::min<uint32_t>(index,AiKnowledgeData::kParameterCount-1U);
    config.fixed_value=std::bit_cast<int32_t>(parameters[config.parameter_index]);
    config.sweep_mode=SimulationLabSweepMode::Fixed;
    config.minimum_value=0;
    config.maximum_value=config.fixed_value==0 ? 0x1000 : static_cast<int32_t>(
        std::clamp<int64_t>(int64_t(config.fixed_value)*2,1,std::numeric_limits<int32_t>::max()));
    return config;
}
}
