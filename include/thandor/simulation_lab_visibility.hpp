#pragma once
#include "thandor/simulation_lab.hpp"
#include "thandor/fog_preview.hpp"
namespace thandor {
// ArmyRuntimeSlot +0x50 / GameEntityDamageState2C::factionVisibilityBits1C.
// AI and weapon aim consume the retained model classification, not the GUI's
// per-cell projection. Metadata-only callers before their first model refresh
// retain the explicit projection adapter.
inline uint32_t simulation_lab_model_visibility_bits(const SimulationLabRun& run,
    const FieldGrid& field,uint32_t faction,const SimulationLabEntity& entity) noexcept {
    if(faction>=8U)return 0;
    if(entity.model_terrain_state_initialized)
        return (entity.model_primary_occupancy_mask>>(2U*faction))&3U;
    const auto cell=retail_fog_world_to_cell(field,float_to_q12(entity.actor.position.x),
        float_to_q12(entity.actor.position.y));
    if(!cell)return 0;
    const auto index=size_t(cell->y)*field.width()+cell->x;
    if(index>=run.current_visibility_occupancy.size())return 0;
    const auto byte=fog_occupancy_byte(run.current_visibility_occupancy[index],faction);
    return (retail_fog_explored(byte)?1U:0U)|(retail_fog_currently_visible(byte)?2U:0U);
}
}
