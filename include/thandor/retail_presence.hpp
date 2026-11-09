#pragma once
namespace thandor {
class FieldGrid;
struct SimulationLabRetailScratchImage;
// Exact native 42-cell occupancy dilation; existing presence is retained.
void retail_propagate_field_presence_into_scratch(SimulationLabRetailScratchImage& scratch,
                                                 const FieldGrid& field);
}
