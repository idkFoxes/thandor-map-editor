// Test-only native-reference function bodies copied unchanged from the pinned source.
#pragma once
#include <cstdint>
namespace native_v625 {
using FieldGridRadiusUnits=int32_t;using Q12=int32_t;using FieldGridCellCoordinate=int32_t;
using FactionRuntimeIndex=uint32_t;
struct GridScratchCell{uint32_t stateMask;uint32_t pathCost;};
inline GridScratchCell* g_GridScratchPrimary{};inline uint32_t g_GridScratchWidth{},g_GridScratchHeight{};
inline uint32_t g_GridInfluenceSquaredThreshold[8]{};
inline constexpr uint32_t g_GridInfluenceRadiusOffset[8]={1000,1250,1500,1600,1920,2240,2600,4100};
inline constexpr uint32_t GRID_SCRATCH_BLOCKED=0x80000000U,GRID_SCRATCH_LOW_BAND0=0x100,GRID_SCRATCH_HIGH_BAND0=0x10000;
inline constexpr int GRID_FOOTPRINT_RADIUS_MARGIN=499,GRID_SCRATCH_INDEX_BIAS_Q12=0x800,GRID_SCRATCH_CELL_SHIFT=10,GRID_SCRATCH_CELL_Q12=0x400,GRID_SCRATCH_CELL_CENTER_Q12=0x600;
inline constexpr int FIELD_GRID_WORLD_Y_TO_ROW_Q20=-0x20C8CC,FIELD_GRID_WORLD_X_TO_COLUMN_Q20=0x1C6E9C,FIELD_GRID_WORLD_COLUMN_STEP_X=0x901,FIELD_GRID_WORLD_ROW_STEP_Y=-1999;
inline constexpr int GRID_SCRATCH_COLUMN_WORLD_X=0x240,GRID_SCRATCH_HALF_COLUMN_WORLD_X=0x120,GRID_SCRATCH_ROW_PAIR_WORLD_Y=999,GRID_SCRATCH_ROW_ABOVE_WORLD_Y=499,GRID_SCRATCH_ROW_BELOW_WORLD_Y=500;
inline uint32_t* GridScratchCell_StateMaskBits(GridScratchCell* p){return &p->stateMask;}
static uint32_t GridInfluence_RingBandMask(uint32_t squaredDistance)
{
  return (uint32_t)(squaredDistance <= g_GridInfluenceSquaredThreshold[0]) |
         (uint32_t)(squaredDistance <= g_GridInfluenceSquaredThreshold[1]) << 1 |
         (uint32_t)(squaredDistance <= g_GridInfluenceSquaredThreshold[2]) << 2 |
         (uint32_t)(squaredDistance <= g_GridInfluenceSquaredThreshold[3]) << 3 |
         (uint32_t)(squaredDistance <= g_GridInfluenceSquaredThreshold[4]) << 4 |
         (uint32_t)(squaredDistance <= g_GridInfluenceSquaredThreshold[5]) << 5 |
         (uint32_t)(squaredDistance <= g_GridInfluenceSquaredThreshold[6]) << 6 |
         (uint32_t)(squaredDistance <= g_GridInfluenceSquaredThreshold[7]) << 7;
}

int GridInfluence_SetLowDistanceBandsDiagonalNegative
          (FieldGridCellCoordinate centerWorldYQ12,FieldGridCellCoordinate centerWorldXQ12,
          FieldGridCellCoordinate cellWorldYQ12,FieldGridCellCoordinate cellWorldXQ12,
          uint32_t *scratchCell)

{
  int squaredXDistance;
  int squaredYDistance;
  int cellsWritten;
  uint32_t bandMask;

  /* the walk is straight down in world space, so the X distance stays the same */
  squaredXDistance = (cellWorldXQ12 - centerWorldXQ12) * (cellWorldXQ12 - centerWorldXQ12);
  squaredYDistance = (cellWorldYQ12 - centerWorldYQ12) * (cellWorldYQ12 - centerWorldYQ12);
  cellsWritten = 0;
  bandMask = GridInfluence_RingBandMask(squaredYDistance + squaredXDistance);
  while (bandMask != 0) {
    cellWorldYQ12 = cellWorldYQ12 - GRID_SCRATCH_ROW_PAIR_WORLD_Y;
    *scratchCell = *scratchCell | bandMask * GRID_SCRATCH_LOW_BAND0;
    squaredYDistance = (cellWorldYQ12 - centerWorldYQ12) * (cellWorldYQ12 - centerWorldYQ12);
    scratchCell = scratchCell + g_GridScratchWidth * 4 - 2;
    if ((*scratchCell & GRID_SCRATCH_BLOCKED) != 0) {
      return cellsWritten; /* the cell just written is not counted */
    }
    cellsWritten = cellsWritten + 1;
    bandMask = GridInfluence_RingBandMask(squaredYDistance + squaredXDistance);
  }
  return cellsWritten;
}

int GridInfluence_SetLowDistanceBandsDiagonalPositive
          (FieldGridCellCoordinate centerWorldYQ12,FieldGridCellCoordinate centerWorldXQ12,
          FieldGridCellCoordinate cellWorldYQ12,FieldGridCellCoordinate cellWorldXQ12,
          uint32_t *scratchCell)

{
  int squaredXDistance;
  int squaredYDistance;
  int cellsWritten;
  uint32_t bandMask;

  squaredXDistance = (cellWorldXQ12 - centerWorldXQ12) * (cellWorldXQ12 - centerWorldXQ12);
  squaredYDistance = (cellWorldYQ12 - centerWorldYQ12) * (cellWorldYQ12 - centerWorldYQ12);
  cellsWritten = 0;
  bandMask = GridInfluence_RingBandMask(squaredYDistance + squaredXDistance);
  while (bandMask != 0) {
    cellWorldYQ12 = cellWorldYQ12 + GRID_SCRATCH_ROW_PAIR_WORLD_Y;
    *scratchCell = *scratchCell | bandMask * GRID_SCRATCH_LOW_BAND0;
    squaredYDistance = (cellWorldYQ12 - centerWorldYQ12) * (cellWorldYQ12 - centerWorldYQ12);
    scratchCell = scratchCell + (int32_t)(g_GridScratchWidth * -4) + 2;
    if ((*scratchCell & GRID_SCRATCH_BLOCKED) != 0) {
      return cellsWritten; /* the cell just written is not counted */
    }
    cellsWritten = cellsWritten + 1;
    bandMask = GridInfluence_RingBandMask(squaredYDistance + squaredXDistance);
  }
  return cellsWritten;
}

int GridInfluence_SetHighDistanceBandsDiagonalNegative
          (FieldGridCellCoordinate centerWorldYQ12,FieldGridCellCoordinate centerWorldXQ12,
          FieldGridCellCoordinate cellWorldYQ12,FieldGridCellCoordinate cellWorldXQ12,
          uint32_t *scratchCell)

{
  int squaredXDistance;
  int squaredYDistance;
  int cellsWritten;
  uint32_t bandMask;

  squaredXDistance = (cellWorldXQ12 - centerWorldXQ12) * (cellWorldXQ12 - centerWorldXQ12);
  squaredYDistance = (cellWorldYQ12 - centerWorldYQ12) * (cellWorldYQ12 - centerWorldYQ12);
  cellsWritten = 0;
  bandMask = GridInfluence_RingBandMask(squaredYDistance + squaredXDistance);
  while (bandMask != 0) {
    cellWorldYQ12 = cellWorldYQ12 - GRID_SCRATCH_ROW_PAIR_WORLD_Y;
    *scratchCell = *scratchCell | bandMask * GRID_SCRATCH_HIGH_BAND0;
    squaredYDistance = (cellWorldYQ12 - centerWorldYQ12) * (cellWorldYQ12 - centerWorldYQ12);
    scratchCell = scratchCell + g_GridScratchWidth * 4 - 2;
    if ((*scratchCell & GRID_SCRATCH_BLOCKED) != 0) {
      return cellsWritten; /* the cell just written is not counted */
    }
    cellsWritten = cellsWritten + 1;
    bandMask = GridInfluence_RingBandMask(squaredYDistance + squaredXDistance);
  }
  return cellsWritten;
}

int GridInfluence_SetHighDistanceBandsDiagonalPositive
          (FieldGridCellCoordinate centerWorldYQ12,FieldGridCellCoordinate centerWorldXQ12,
          FieldGridCellCoordinate cellWorldYQ12,FieldGridCellCoordinate cellWorldXQ12,
          uint32_t *scratchCell)

{
  int squaredXDistance;
  int squaredYDistance;
  int cellsWritten;
  uint32_t bandMask;

  squaredXDistance = (cellWorldXQ12 - centerWorldXQ12) * (cellWorldXQ12 - centerWorldXQ12);
  squaredYDistance = (cellWorldYQ12 - centerWorldYQ12) * (cellWorldYQ12 - centerWorldYQ12);
  cellsWritten = 0;
  bandMask = GridInfluence_RingBandMask(squaredYDistance + squaredXDistance);
  while (bandMask != 0) {
    cellWorldYQ12 = cellWorldYQ12 + GRID_SCRATCH_ROW_PAIR_WORLD_Y;
    *scratchCell = *scratchCell | bandMask * GRID_SCRATCH_HIGH_BAND0;
    squaredYDistance = (cellWorldYQ12 - centerWorldYQ12) * (cellWorldYQ12 - centerWorldYQ12);
    scratchCell = scratchCell + (int32_t)(g_GridScratchWidth * -4) + 2;
    if ((*scratchCell & GRID_SCRATCH_BLOCKED) != 0) {
      return cellsWritten; /* the cell just written is not counted */
    }
    cellsWritten = cellsWritten + 1;
    bandMask = GridInfluence_RingBandMask(squaredYDistance + squaredXDistance);
  }
  return cellsWritten;
}

using GridInfluenceBandWalker = int (FieldGridCellCoordinate,FieldGridCellCoordinate,FieldGridCellCoordinate,FieldGridCellCoordinate,uint32_t*);

static void GridInfluence_WalkDistanceBandsAroundWorldPoint
          (FieldGridRadiusUnits radiusMetric,Q12 worldYQ12,Q12 worldXQ12,GridInfluenceBandWalker *walkUp,
          GridInfluenceBandWalker *walkDown)
{
  int64_t wideProduct;
  int radiusWithMargin;
  uint32_t rowTerm;
  int column;
  int row;
  int rowCenterQ12;
  uint32_t centerCellWorldX;
  uint32_t centerCellWorldY;
  GridScratchCell *centerCell;
  GridScratchCell *leftCell;
  GridScratchCell *rightCell;
  uint32_t leftCellWorldX;
  uint32_t rightCellWorldX;
  GridScratchCell *aboveRowCell;
  int aboveLeftWorldX;
  int aboveRightWorldX;
  GridScratchCell *belowRowCell;
  int belowLeftWorldX;
  int belowRightWorldX;

  radiusWithMargin = radiusMetric + GRID_FOOTPRINT_RADIUS_MARGIN;
  g_GridInfluenceSquaredThreshold[0] =
       (g_GridInfluenceRadiusOffset[0] + radiusWithMargin) * (g_GridInfluenceRadiusOffset[0] + radiusWithMargin);
  g_GridInfluenceSquaredThreshold[1] =
       (g_GridInfluenceRadiusOffset[1] + radiusWithMargin) * (g_GridInfluenceRadiusOffset[1] + radiusWithMargin);
  g_GridInfluenceSquaredThreshold[2] =
       (g_GridInfluenceRadiusOffset[2] + radiusWithMargin) * (g_GridInfluenceRadiusOffset[2] + radiusWithMargin);
  g_GridInfluenceSquaredThreshold[3] =
       (g_GridInfluenceRadiusOffset[3] + radiusWithMargin) * (g_GridInfluenceRadiusOffset[3] + radiusWithMargin);
  g_GridInfluenceSquaredThreshold[4] =
       (g_GridInfluenceRadiusOffset[4] + radiusWithMargin) * (g_GridInfluenceRadiusOffset[4] + radiusWithMargin);
  g_GridInfluenceSquaredThreshold[5] =
       (g_GridInfluenceRadiusOffset[5] + radiusWithMargin) * (g_GridInfluenceRadiusOffset[5] + radiusWithMargin);
  g_GridInfluenceSquaredThreshold[6] =
       (g_GridInfluenceRadiusOffset[6] + radiusWithMargin) * (g_GridInfluenceRadiusOffset[6] + radiusWithMargin);
  g_GridInfluenceSquaredThreshold[7] =
       (g_GridInfluenceRadiusOffset[7] + radiusWithMargin) * (g_GridInfluenceRadiusOffset[7] + radiusWithMargin);
  /* scratch cell under the world point */
  rowTerm = (int)((uint64_t)((int64_t)worldYQ12 * FIELD_GRID_WORLD_Y_TO_ROW_Q20) >> 32) << 11 |
          (uint32_t)((int64_t)worldYQ12 * FIELD_GRID_WORLD_Y_TO_ROW_Q20) >> 21;
  column = (int)((((int)((uint64_t)((int64_t)worldXQ12 * FIELD_GRID_WORLD_X_TO_COLUMN_Q20) >> 32) << 12 |
                 (uint32_t)((int64_t)worldXQ12 * FIELD_GRID_WORLD_X_TO_COLUMN_Q20) >> 20) - rowTerm) +
                 GRID_SCRATCH_INDEX_BIAS_Q12) >> GRID_SCRATCH_CELL_SHIFT;
  row = (int)(rowTerm * 2 + GRID_SCRATCH_INDEX_BIAS_Q12) >> GRID_SCRATCH_CELL_SHIFT;
  if (column < 0 || row < 0 || column >= (int)g_GridScratchWidth || row >= (int)g_GridScratchHeight) {
    return;
  }
  /* world position of the centre cell's centre */
  centerCell = g_GridScratchPrimary + (int32_t)(g_GridScratchWidth * row) + column;
  rowCenterQ12 = row * GRID_SCRATCH_CELL_Q12 - GRID_SCRATCH_CELL_CENTER_Q12;
  wideProduct = (int64_t)(rowCenterQ12 + (column * GRID_SCRATCH_CELL_Q12 - GRID_SCRATCH_CELL_CENTER_Q12) * 2) * FIELD_GRID_WORLD_COLUMN_STEP_X;
  centerCellWorldX = (int)((uint64_t)wideProduct >> 32) << 19 | (uint32_t)wideProduct >> 13;
  wideProduct = (int64_t)rowCenterQ12 * FIELD_GRID_WORLD_ROW_STEP_Y;
  centerCellWorldY = (int)((uint64_t)wideProduct >> 32) << 20 | (uint32_t)wideProduct >> 12;
  if ((centerCell->stateMask & GRID_SCRATCH_BLOCKED) != 0) {
    return;
  }
  /* centre row leftwards, walking up and down, until a column has nothing inside */
  leftCell = centerCell;
  leftCellWorldX = centerCellWorldX;
  while (walkUp(worldYQ12,worldXQ12,centerCellWorldY,leftCellWorldX,GridScratchCell_StateMaskBits(leftCell)) != 0 &&
         walkDown(worldYQ12,worldXQ12,centerCellWorldY,leftCellWorldX,GridScratchCell_StateMaskBits(leftCell)) != 0) {
    leftCell--;
    leftCellWorldX = leftCellWorldX - GRID_SCRATCH_COLUMN_WORLD_X;
  }
  /* centre row rightwards (the centre column again) */
  rightCell = centerCell;
  rightCellWorldX = centerCellWorldX;
  while (walkUp(worldYQ12,worldXQ12,centerCellWorldY,rightCellWorldX,GridScratchCell_StateMaskBits(rightCell)) != 0 &&
         walkDown(worldYQ12,worldXQ12,centerCellWorldY,rightCellWorldX,GridScratchCell_StateMaskBits(rightCell)) != 0) {
    rightCell++;
    rightCellWorldX = rightCellWorldX + GRID_SCRATCH_COLUMN_WORLD_X;
  }
  /* row above: upward walks, leftwards then rightwards */
  aboveRowCell = centerCell - g_GridScratchWidth;
  aboveLeftWorldX = centerCellWorldX - GRID_SCRATCH_HALF_COLUMN_WORLD_X;
  aboveRightWorldX = centerCellWorldX + GRID_SCRATCH_HALF_COLUMN_WORLD_X;
  leftCell = aboveRowCell;
  while (walkUp(worldYQ12,worldXQ12,centerCellWorldY + GRID_SCRATCH_ROW_ABOVE_WORLD_Y,aboveLeftWorldX,
                GridScratchCell_StateMaskBits(leftCell)) != 0) {
    leftCell--;
    aboveLeftWorldX = aboveLeftWorldX - GRID_SCRATCH_COLUMN_WORLD_X;
  }
  rightCell = aboveRowCell + 1;
  while (walkUp(worldYQ12,worldXQ12,centerCellWorldY + GRID_SCRATCH_ROW_ABOVE_WORLD_Y,aboveRightWorldX,
                GridScratchCell_StateMaskBits(rightCell)) != 0) {
    rightCell++;
    aboveRightWorldX = aboveRightWorldX + GRID_SCRATCH_COLUMN_WORLD_X;
  }
  /* row below: downward walks, rightwards then leftwards */
  belowRowCell = centerCell + g_GridScratchWidth;
  belowRightWorldX = centerCellWorldX + GRID_SCRATCH_HALF_COLUMN_WORLD_X;
  belowLeftWorldX = centerCellWorldX - GRID_SCRATCH_HALF_COLUMN_WORLD_X;
  rightCell = belowRowCell;
  while (walkDown(worldYQ12,worldXQ12,centerCellWorldY - GRID_SCRATCH_ROW_BELOW_WORLD_Y,belowRightWorldX,
                  GridScratchCell_StateMaskBits(rightCell)) != 0) {
    rightCell++;
    belowRightWorldX = belowRightWorldX + GRID_SCRATCH_COLUMN_WORLD_X;
  }
  leftCell = belowRowCell - 1;
  while (walkDown(worldYQ12,worldXQ12,centerCellWorldY - GRID_SCRATCH_ROW_BELOW_WORLD_Y,belowLeftWorldX,
                  GridScratchCell_StateMaskBits(leftCell)) != 0) {
    leftCell--;
    belowLeftWorldX = belowLeftWorldX - GRID_SCRATCH_COLUMN_WORLD_X;
  }
}

struct FieldGridCell{uint32_t flagsAndMaterial{};uint64_t occupancy{};};
inline uint8_t FieldGridCell_OccupancyByte(const FieldGridCell* p,uint32_t f){return uint8_t(p->occupancy>>(f*8U));}
inline constexpr uint32_t FIELD_CELL_OCCUPANCY_PRESENCE_BITS=0xF9,FIELD_CELL_XENITE_OR_TRITIUM_SUPPORT_MASK=0x1800;
inline constexpr uint32_t GRID_SCRATCH_TERRAIN_CLASS_BIT24=0x01000000,GRID_SCRATCH_TERRAIN_CLASS_BIT25=0x02000000,GRID_SCRATCH_TERRAIN_CLASS_BIT28=0x10000000,GRID_SCRATCH_TERRAIN_CLASS_BIT29=0x20000000,GRID_SCRATCH_TERRAIN_CLASS_BIT30=0x40000000;
inline constexpr uint32_t AI_SITE_SCRATCH_OBSTACLE_BITS=0xF0000000,AI_SITE_SCRATCH_BANDS_CLASSES_0_TO_5=0x3F00,GRID_SCRATCH_LOW_DISTANCE_BANDS=0xFF00;
inline uint32_t g_AiActiveGridMaskClasses[4]{};inline uint32_t admitted{};
inline bool Any(uint32_t x){return x!=0;}
inline void AiSiteCandidate_AddGeneralCellIfSeparated(FieldGridCell*){admitted|=1;}
inline void AiSiteCandidate_AddFlaggedCellIfSeparated(FieldGridCell*){admitted|=2;}
inline void AiSiteCandidate_AddTerrainFeatureCellIfSeparated(FieldGridCell*,uint32_t){admitted|=4;}
inline void AiBaseSiteWorkspace_AddCellInsideBase(FieldGridCell*){admitted|=8;}
inline void AiBaseSiteWorkspace_AddLargeCellInsideBase(FieldGridCell*){admitted|=16;}
static uint32_t AiPlanningRebuild_ScratchFootprintMask(const GridScratchCell *topCell,uint32_t rowStride,
          uint32_t step)

{
  return topCell[0].stateMask | topCell[step].stateMask | topCell[rowStride - step].stateMask |
         topCell[rowStride + step].stateMask | topCell[rowStride * 2 - step].stateMask |
         topCell[rowStride * 2].stateMask;
}
static bool AiPlanningRebuild_LacksActiveMaskClass(uint32_t neighborhoodMask)

{
  return (g_AiActiveGridMaskClasses[0] & neighborhoodMask) == 0 ||
         (g_AiActiveGridMaskClasses[1] & neighborhoodMask) == 0 ||
         (g_AiActiveGridMaskClasses[2] & neighborhoodMask) == 0 ||
         (g_AiActiveGridMaskClasses[3] & neighborhoodMask) == 0;
}
static bool AiPlanningRebuild_HasUnoccupiedNeighbour(FieldGridCell *cellAbove,uint32_t gridWidth,
          FactionRuntimeIndex factionIndex)

{
  return (FieldGridCell_OccupancyByte(&cellAbove[0],factionIndex) & FIELD_CELL_OCCUPANCY_PRESENCE_BITS) == 0 ||
         (FieldGridCell_OccupancyByte(&cellAbove[1],factionIndex) & FIELD_CELL_OCCUPANCY_PRESENCE_BITS) == 0 ||
         (FieldGridCell_OccupancyByte(&cellAbove[gridWidth - 1],factionIndex) & FIELD_CELL_OCCUPANCY_PRESENCE_BITS) ==
          0 ||
         (FieldGridCell_OccupancyByte(&cellAbove[gridWidth + 1],factionIndex) & FIELD_CELL_OCCUPANCY_PRESENCE_BITS) ==
          0 ||
         (FieldGridCell_OccupancyByte(&cellAbove[gridWidth * 2 - 1],factionIndex) &
          FIELD_CELL_OCCUPANCY_PRESENCE_BITS) == 0 ||
         (FieldGridCell_OccupancyByte(&cellAbove[gridWidth * 2],factionIndex) & FIELD_CELL_OCCUPANCY_PRESENCE_BITS) ==
          0;
}
static bool AiPlanningRebuild_FootprintHasResourceSupport(const FieldGridCell *cellAbove,uint32_t gridWidth)

{
  return Any(cellAbove[gridWidth].flagsAndMaterial & FIELD_CELL_XENITE_OR_TRITIUM_SUPPORT_MASK) ||
         Any(cellAbove[0].flagsAndMaterial & FIELD_CELL_XENITE_OR_TRITIUM_SUPPORT_MASK) ||
         Any(cellAbove[1].flagsAndMaterial & FIELD_CELL_XENITE_OR_TRITIUM_SUPPORT_MASK) ||
         Any(cellAbove[gridWidth - 1].flagsAndMaterial & FIELD_CELL_XENITE_OR_TRITIUM_SUPPORT_MASK) ||
         Any(cellAbove[gridWidth + 1].flagsAndMaterial & FIELD_CELL_XENITE_OR_TRITIUM_SUPPORT_MASK) ||
         Any(cellAbove[gridWidth * 2 - 1].flagsAndMaterial & FIELD_CELL_XENITE_OR_TRITIUM_SUPPORT_MASK) ||
         Any(cellAbove[gridWidth * 2].flagsAndMaterial & FIELD_CELL_XENITE_OR_TRITIUM_SUPPORT_MASK);
}
static void AiPlanningRebuild_ScanSiteCell(FactionRuntimeIndex factionIndex,FieldGridCell *cellAbove,
          uint32_t gridWidth,const GridScratchCell *scratchCell,uint32_t scratchRowStride,
          uint32_t terrainFeatureSpacing)

{
  FieldGridCell *currentCell;
  uint32_t neighborhoodMask;

  currentCell = cellAbove + gridWidth;
  /* general site: an occupied, unblocked cell at the edge of the faction's presence where also one of the active
     mask classes is missing around it (both conditions, not either) */
  if (((scratchCell[scratchRowStride].stateMask & GRID_SCRATCH_BLOCKED) == 0) &&
      ((FieldGridCell_OccupancyByte(currentCell,factionIndex) & FIELD_CELL_OCCUPANCY_PRESENCE_BITS) != 0) &&
      AiPlanningRebuild_HasUnoccupiedNeighbour(cellAbove,gridWidth,factionIndex) &&
      (AiPlanningRebuild_LacksActiveMaskClass
                 (scratchCell[scratchRowStride].stateMask |
                  AiPlanningRebuild_ScratchFootprintMask(scratchCell,scratchRowStride,3)))) {
    AiSiteCandidate_AddGeneralCellIfSeparated(currentCell);
  }
  /* flagged site: occupancy bit 4 set and one of the terrain classes 24/25/28 absent around it */
  if (((FieldGridCell_OccupancyByte(currentCell,factionIndex) & 0x10) != 0) &&
      ((scratchCell[scratchRowStride].stateMask & (GRID_SCRATCH_BLOCKED | GRID_SCRATCH_LOW_BAND0)) == 0)) {
    neighborhoodMask = scratchCell[scratchRowStride].stateMask |
                       AiPlanningRebuild_ScratchFootprintMask(scratchCell,scratchRowStride,4);
    if (((neighborhoodMask & (GRID_SCRATCH_BLOCKED | GRID_SCRATCH_TERRAIN_CLASS_BIT24)) == 0 ||
         (neighborhoodMask & (GRID_SCRATCH_BLOCKED | GRID_SCRATCH_TERRAIN_CLASS_BIT25)) == 0 ||
         (neighborhoodMask & (GRID_SCRATCH_BLOCKED | GRID_SCRATCH_TERRAIN_CLASS_BIT28)) == 0) &&
        (!Any(currentCell->flagsAndMaterial & FIELD_CELL_XENITE_OR_TRITIUM_SUPPORT_MASK))) {
      AiSiteCandidate_AddFlaggedCellIfSeparated(currentCell);
    }
  }
  if (((FieldGridCell_OccupancyByte(currentCell,factionIndex) & FIELD_CELL_OCCUPANCY_PRESENCE_BITS) == 0) ||
      ((scratchCell[scratchRowStride].stateMask &
        (AI_SITE_SCRATCH_OBSTACLE_BITS | AI_SITE_SCRATCH_BANDS_CLASSES_0_TO_5)) != 0)) {
    return;
  }
  /* terrain-feature site: a resource cell with no obstacles in the small and the wide footprint */
  if (((scratchCell[scratchRowStride].stateMask & (AI_SITE_SCRATCH_OBSTACLE_BITS | GRID_SCRATCH_LOW_DISTANCE_BANDS))
       == 0) &&
      ((AiPlanningRebuild_ScratchFootprintMask(scratchCell,scratchRowStride,4) & AI_SITE_SCRATCH_OBSTACLE_BITS) ==
       0) &&
      ((AiPlanningRebuild_ScratchFootprintMask(scratchCell - scratchRowStride,scratchRowStride * 2,8) &
        AI_SITE_SCRATCH_OBSTACLE_BITS) == 0) &&
      (Any(currentCell->flagsAndMaterial & FIELD_CELL_XENITE_OR_TRITIUM_SUPPORT_MASK))) {
    AiSiteCandidate_AddTerrainFeatureCellIfSeparated(currentCell,terrainFeatureSpacing);
  }
  /* base site: no obstacles around it and no resource cell in its footprint */
  if (((AiPlanningRebuild_ScratchFootprintMask(scratchCell,scratchRowStride,4) & AI_SITE_SCRATCH_OBSTACLE_BITS) ==
       0) &&
      !AiPlanningRebuild_FootprintHasResourceSupport(cellAbove,gridWidth)) {
    AiBaseSiteWorkspace_AddCellInsideBase(currentCell);
  }
  /* large base site: additionally none of the terrain classes 28..30 in the wide footprint */
  if (((scratchCell[scratchRowStride].stateMask & (AI_SITE_SCRATCH_OBSTACLE_BITS | GRID_SCRATCH_LOW_DISTANCE_BANDS))
       == 0) &&
      ((AiPlanningRebuild_ScratchFootprintMask(scratchCell,scratchRowStride,4) & AI_SITE_SCRATCH_OBSTACLE_BITS) ==
       0) &&
      ((AiPlanningRebuild_ScratchFootprintMask(scratchCell - scratchRowStride,scratchRowStride * 2,8) &
        (GRID_SCRATCH_TERRAIN_CLASS_BIT30 | GRID_SCRATCH_TERRAIN_CLASS_BIT29 | GRID_SCRATCH_TERRAIN_CLASS_BIT28)) ==
       0) &&
      !AiPlanningRebuild_FootprintHasResourceSupport(cellAbove,gridWidth)) {
    AiBaseSiteWorkspace_AddLargeCellInsideBase(currentCell);
  }
}
}
