#pragma once
#include "thandor/catalog.hpp"
#include "thandor/field.hpp"
#include "thandor/fog_preview.hpp"
#include "thandor/level.hpp"
#include <span>
#include <vector>
namespace editor {
struct ResourceDepositFlagIssue {
    std::size_t placement_index{};
    uint32_t army_asset_id{},cell_x{},cell_y{};
    thandor::ResourceSupportCellAudit audit{};
};
struct ResourceDepositFlagValidation {
    std::size_t checked{};
    std::vector<ResourceDepositFlagIssue> issues;
};
// Editor map-issues list only: a deposit object whose matching FLD resource
// bit is missing. Placement legality and extractor simulation remain separate.
inline ResourceDepositFlagValidation validate_resource_deposit_flags(
    const thandor::FieldGrid& field,std::span<const thandor::LevelPlacement> placements){
    ResourceDepositFlagValidation result;
    for(std::size_t i=0;i<placements.size();++i){
        const auto& placement=placements[i];
        const auto kind=thandor::resource_field_occupancy_class(placement.army_asset_id);
        if(!kind)continue;
        // LEV stores its axes in the opposite order to the rendered FLD lattice.
        const auto cell=thandor::retail_fog_world_to_cell(field,
            placement.world_y_q12,placement.world_x_q12);
        if(!cell)continue; // No FLD cell to test; this is not a missing-bit report.
        ++result.checked;
        const auto audit=field.audit_resource_support_cell(cell->x,cell->y,*kind);
        if(!audit.support_present)
            result.issues.push_back({i,placement.army_asset_id,cell->x,cell->y,audit});
    }
    return result;
}
}
