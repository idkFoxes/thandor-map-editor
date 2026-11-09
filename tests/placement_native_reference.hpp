// Generated native reference excerpts for bounded differential validation only.
// Function bodies below are copied unchanged from the pinned reference source.
// The adapter provides detached 8-byte cells, constants, pointer helpers and globals.
#pragma once
#include <vector>
#include <cstdint>
#include <cassert>
namespace retail_reference_v624 {
using Q12=int32_t;using FieldGridRadiusUnits=int32_t;using FieldGridCellCoordinate=int32_t;
struct GridScratchCell {uint32_t stateMask;uint32_t pathCost;};
inline GridScratchCell* g_GridScratchPrimary{};
inline uint32_t g_GridScratchWidth{},g_GridScratchHeight{};
inline uint32_t g_GridInfluenceSquaredThreshold[8]{};
inline constexpr uint32_t g_GridInfluenceRadiusOffset[8]={1000,1250,1500,1600,1920,2240,2600,4100};
inline constexpr uint32_t GRID_SCRATCH_BLOCKED=0x80000000U,GRID_SCRATCH_TRAVERSAL_VISITED=1,GRID_REACHABILITY_OPEN_STOP_MASK=0xF0007F01;
inline constexpr int GRID_FOOTPRINT_RADIUS_MARGIN=499,GRID_SCRATCH_INDEX_BIAS_Q12=0x800,GRID_SCRATCH_CELL_SHIFT=10,GRID_SCRATCH_CELL_Q12=0x400,GRID_SCRATCH_CELL_CENTER_Q12=0x600;
inline constexpr int FIELD_GRID_WORLD_Y_TO_ROW_Q20=-0x20C8CC,FIELD_GRID_WORLD_X_TO_COLUMN_Q20=0x1C6E9C,FIELD_GRID_WORLD_COLUMN_STEP_X=0x901,FIELD_GRID_WORLD_ROW_STEP_Y=-1999;
inline constexpr int GRID_SCRATCH_COLUMN_WORLD_X=0x240,GRID_SCRATCH_HALF_COLUMN_WORLD_X=0x120,GRID_SCRATCH_ROW_PAIR_WORLD_Y=999,GRID_SCRATCH_ROW_ABOVE_WORLD_Y=499,GRID_SCRATCH_ROW_BELOW_WORLD_Y=500;
inline GridScratchCell* GridScratchCell_RowAbove(GridScratchCell* p,uint32_t stride){return p-stride/8;}
inline GridScratchCell* GridScratchCell_RowBelow(GridScratchCell* p,uint32_t stride){return p+stride/8;}
inline uint32_t* GridScratchCell_StateMaskBits(GridScratchCell* p){return &p->stateMask;}
inline bool GridScratch_PrimaryContainsRecord(uint32_t* p){return p>=&g_GridScratchPrimary[0].stateMask&&p<&g_GridScratchPrimary[g_GridScratchWidth*g_GridScratchHeight].stateMask;}
struct GridScanlineFrame {
  GridScratchCell *leftStopCell;
  GridScratchCell *rightStopCell;
  GridScratchCell *rowCursor;
  bool inRowBelow;
};

static std::vector<GridScanlineFrame> s_GridScanlineFrames;

template <class MarkSpan,class IsOpen>
static void GridScanline_Fill(uint32_t rowStrideBytes,GridScratchCell *startCell,MarkSpan markSpan,IsOpen isOpen)

{
  GridScanlineFrame frame;
  GridScratchCell *cell;

  frame.inRowBelow = false;
  markSpan(startCell,&frame.leftStopCell,&frame.rightStopCell);
  frame.rowCursor = GridScratchCell_RowAbove(frame.leftStopCell + 1,rowStrideBytes);
  s_GridScanlineFrames.push_back(frame);
  while (!s_GridScanlineFrames.empty()) {
    GridScanlineFrame &top = s_GridScanlineFrames.back();
    if (!top.inRowBelow && GridScratchCell_RowAbove(top.rightStopCell,rowStrideBytes) < top.rowCursor) {
      top.inRowBelow = true;
      top.rowCursor = GridScratchCell_RowBelow(top.leftStopCell,rowStrideBytes);
    }
    if (top.inRowBelow && GridScratchCell_RowBelow(top.rightStopCell,rowStrideBytes) <= top.rowCursor) {
      s_GridScanlineFrames.pop_back();
      continue;
    }
    cell = top.rowCursor;
    top.rowCursor++;
    if (isOpen(cell)) {
      /* the "recursive call": top is not used again before the next iteration re-reads the back frame */
      markSpan(cell,&frame.leftStopCell,&frame.rightStopCell);
      frame.rowCursor = GridScratchCell_RowAbove(frame.leftStopCell + 1,rowStrideBytes);
      s_GridScanlineFrames.push_back(frame);
    }
  }
}

static bool GridReachability_IsMarkedRingEdgeCell(GridScratchCell *rowAboveCell,uint32_t scratchWidth)
{
  GridScratchCell *cell;

  cell = rowAboveCell + scratchWidth;
  if (((cell->stateMask & GRID_SCRATCH_BLOCKED) != 0) || (cell->pathCost == 0) ||
      ((cell->stateMask & GRID_SCRATCH_TRAVERSAL_VISITED) == 0)) {
    return false;
  }
  return (rowAboveCell->pathCost == 0) || (rowAboveCell[1].pathCost == 0) ||
         (rowAboveCell[scratchWidth - 1].pathCost == 0) || (rowAboveCell[scratchWidth + 1].pathCost == 0) ||
         (rowAboveCell[scratchWidth * 2 - 1].pathCost == 0) || (rowAboveCell[scratchWidth * 2].pathCost == 0);
}

int GridFootprint_ClearTraversalFlagsDiagonalNegative
          (FieldGridCellCoordinate centerWorldYQ12,FieldGridCellCoordinate centerWorldXQ12,
          FieldGridCellCoordinate cellWorldYQ12,FieldGridCellCoordinate cellWorldXQ12,uint32_t *scratchRecord)

{
  int nextCount;
  int squaredYDistance;
  int visitedCount;

  squaredYDistance = (cellWorldYQ12 - centerWorldYQ12) * (cellWorldYQ12 - centerWorldYQ12);
  nextCount = 0;
  do {
    visitedCount = nextCount;
    if (g_GridInfluenceSquaredThreshold[6] <
        (uint32_t)(squaredYDistance + (cellWorldXQ12 - centerWorldXQ12) * (cellWorldXQ12 - centerWorldXQ12))) {
      return visitedCount;
    }
    /* scratchRecord[0] = stateMask, [1] = pathCost */
    *scratchRecord = *scratchRecord & ~(GRID_SCRATCH_BLOCKED | GRID_SCRATCH_TRAVERSAL_VISITED);
    scratchRecord[1]++;
    cellWorldYQ12 = cellWorldYQ12 - GRID_SCRATCH_ROW_PAIR_WORLD_Y;
    squaredYDistance = (cellWorldYQ12 - centerWorldYQ12) * (cellWorldYQ12 - centerWorldYQ12);
    scratchRecord = scratchRecord + g_GridScratchWidth * 4 - 2;
    assert(GridScratch_PrimaryContainsRecord(scratchRecord));
    nextCount = visitedCount + 1;
  } while ((*scratchRecord & GRID_SCRATCH_BLOCKED) == 0);
  return visitedCount;
}

int GridFootprint_ClearTraversalFlagsDiagonalPositive
          (FieldGridCellCoordinate centerWorldYQ12,FieldGridCellCoordinate centerWorldXQ12,
          FieldGridCellCoordinate cellWorldYQ12,FieldGridCellCoordinate cellWorldXQ12,uint32_t *scratchRecord)

{
  int nextCount;
  int squaredYDistance;
  int visitedCount;

  squaredYDistance = (cellWorldYQ12 - centerWorldYQ12) * (cellWorldYQ12 - centerWorldYQ12);
  nextCount = 0;
  do {
    visitedCount = nextCount;
    if (g_GridInfluenceSquaredThreshold[6] <
        (uint32_t)(squaredYDistance + (cellWorldXQ12 - centerWorldXQ12) * (cellWorldXQ12 - centerWorldXQ12))) {
      return visitedCount;
    }
    cellWorldYQ12 = cellWorldYQ12 + GRID_SCRATCH_ROW_PAIR_WORLD_Y;
    *scratchRecord = *scratchRecord & ~(GRID_SCRATCH_BLOCKED | GRID_SCRATCH_TRAVERSAL_VISITED);
    scratchRecord[1]++;
    squaredYDistance = (cellWorldYQ12 - centerWorldYQ12) * (cellWorldYQ12 - centerWorldYQ12);
    scratchRecord = scratchRecord + (int32_t)(g_GridScratchWidth * -4) + 2;
    assert(GridScratch_PrimaryContainsRecord(scratchRecord));
    nextCount = visitedCount + 1;
  } while ((*scratchRecord & GRID_SCRATCH_BLOCKED) == 0);
  return visitedCount;
}

void GridFootprint_ClearTraversalFlagsAroundWorldPoint
          (FieldGridRadiusUnits radiusWorldUnits,Q12 worldYQ12,Q12 worldXQ12)

{
  int64_t wideProduct;
  int clearanceRadius;
  uint32_t scaledRowTerm;
  int centerColumn;
  int centerRow;
  int centerRowQ12;
  uint32_t centerCellWorldX;
  uint32_t centerCellWorldY;
  uint32_t leftCellWorldX;
  uint32_t rightCellWorldX;
  int sideRowLeftWorldX;
  int sideRowRightWorldX;
  GridScratchCell *centerCellCursor;
  GridScratchCell *leftWalkCursor;
  GridScratchCell *rightWalkCursor;

  clearanceRadius = radiusWorldUnits + g_GridInfluenceRadiusOffset[6] + GRID_FOOTPRINT_RADIUS_MARGIN;
  g_GridInfluenceSquaredThreshold[6] = clearanceRadius * clearanceRadius;
  scaledRowTerm = (int)((uint64_t)((int64_t)worldYQ12 * FIELD_GRID_WORLD_Y_TO_ROW_Q20) >> 32) << 11 |
          (uint32_t)((int64_t)worldYQ12 * FIELD_GRID_WORLD_Y_TO_ROW_Q20) >> 21;
  centerColumn = (int)((((int)((uint64_t)((int64_t)worldXQ12 * FIELD_GRID_WORLD_X_TO_COLUMN_Q20) >> 32) << 12 |
                 (uint32_t)((int64_t)worldXQ12 * FIELD_GRID_WORLD_X_TO_COLUMN_Q20) >> 20) - scaledRowTerm) +
                 GRID_SCRATCH_INDEX_BIAS_Q12) >> GRID_SCRATCH_CELL_SHIFT;
  centerRow = (int)(scaledRowTerm * 2 + GRID_SCRATCH_INDEX_BIAS_Q12) >> GRID_SCRATCH_CELL_SHIFT;
  if ((centerColumn < 0) || (centerRow < 0) || ((int)g_GridScratchWidth <= centerColumn) ||
      ((int)g_GridScratchHeight <= centerRow)) {
    return;
  }
  /* world position of the centre cell's centre */
  centerCellCursor = g_GridScratchPrimary + (int32_t)(g_GridScratchWidth * centerRow) + centerColumn;
  centerRowQ12 = centerRow * GRID_SCRATCH_CELL_Q12 - GRID_SCRATCH_CELL_CENTER_Q12;
  wideProduct = (int64_t)(centerRowQ12 + (centerColumn * GRID_SCRATCH_CELL_Q12 - GRID_SCRATCH_CELL_CENTER_Q12) * 2) * FIELD_GRID_WORLD_COLUMN_STEP_X;
  centerCellWorldX = (int)((uint64_t)wideProduct >> 32) << 19 | (uint32_t)wideProduct >> 13;
  wideProduct = (int64_t)centerRowQ12 * FIELD_GRID_WORLD_ROW_STEP_Y;
  centerCellWorldY = (int)((uint64_t)wideProduct >> 32) << 20 | (uint32_t)wideProduct >> 12;
  if ((centerCellCursor->stateMask & GRID_SCRATCH_BLOCKED) != 0) {
    return;
  }
  /* centre row leftwards, walking up and down, until a column has nothing inside */
  leftCellWorldX = centerCellWorldX;
  leftWalkCursor = centerCellCursor;
  while ((GridFootprint_ClearTraversalFlagsDiagonalPositive
            (worldYQ12,worldXQ12,centerCellWorldY,leftCellWorldX,GridScratchCell_StateMaskBits(leftWalkCursor)) != 0) &&
         (GridFootprint_ClearTraversalFlagsDiagonalNegative
            (worldYQ12,worldXQ12,centerCellWorldY,leftCellWorldX,GridScratchCell_StateMaskBits(leftWalkCursor)) != 0)) {
    leftWalkCursor--;
    leftCellWorldX = leftCellWorldX - GRID_SCRATCH_COLUMN_WORLD_X;
  }
  /* centre row rightwards (the centre column again) */
  rightCellWorldX = centerCellWorldX;
  rightWalkCursor = centerCellCursor;
  while ((GridFootprint_ClearTraversalFlagsDiagonalPositive
            (worldYQ12,worldXQ12,centerCellWorldY,rightCellWorldX,GridScratchCell_StateMaskBits(rightWalkCursor)) != 0) &&
         (GridFootprint_ClearTraversalFlagsDiagonalNegative
            (worldYQ12,worldXQ12,centerCellWorldY,rightCellWorldX,GridScratchCell_StateMaskBits(rightWalkCursor)) != 0)) {
    rightWalkCursor++;
    rightCellWorldX = rightCellWorldX + GRID_SCRATCH_COLUMN_WORLD_X;
  }
  /* row above: upward walks, leftwards then rightwards (starting one cell right of the left start) */
  sideRowLeftWorldX = centerCellWorldX - GRID_SCRATCH_HALF_COLUMN_WORLD_X;
  sideRowRightWorldX = centerCellWorldX + GRID_SCRATCH_HALF_COLUMN_WORLD_X;
  leftWalkCursor = centerCellCursor - g_GridScratchWidth;
  rightWalkCursor = leftWalkCursor + 1;
  while (GridFootprint_ClearTraversalFlagsDiagonalPositive
           (worldYQ12,worldXQ12,centerCellWorldY + GRID_SCRATCH_ROW_ABOVE_WORLD_Y,sideRowLeftWorldX,
            GridScratchCell_StateMaskBits(leftWalkCursor)) != 0) {
    leftWalkCursor--;
    sideRowLeftWorldX = sideRowLeftWorldX - GRID_SCRATCH_COLUMN_WORLD_X;
  }
  while (GridFootprint_ClearTraversalFlagsDiagonalPositive
           (worldYQ12,worldXQ12,centerCellWorldY + GRID_SCRATCH_ROW_ABOVE_WORLD_Y,sideRowRightWorldX,
            GridScratchCell_StateMaskBits(rightWalkCursor)) != 0) {
    sideRowRightWorldX = sideRowRightWorldX + GRID_SCRATCH_COLUMN_WORLD_X;
    rightWalkCursor++;
  }
  /* row below: downward walks, rightwards then leftwards (starting one cell left of the right start) */
  sideRowRightWorldX = centerCellWorldX + GRID_SCRATCH_HALF_COLUMN_WORLD_X;
  sideRowLeftWorldX = centerCellWorldX - GRID_SCRATCH_HALF_COLUMN_WORLD_X;
  rightWalkCursor = centerCellCursor + g_GridScratchWidth;
  leftWalkCursor = rightWalkCursor - 1;
  while (GridFootprint_ClearTraversalFlagsDiagonalNegative
           (worldYQ12,worldXQ12,centerCellWorldY - GRID_SCRATCH_ROW_BELOW_WORLD_Y,sideRowRightWorldX,
            GridScratchCell_StateMaskBits(rightWalkCursor)) != 0) {
    rightWalkCursor++;
    sideRowRightWorldX = sideRowRightWorldX + GRID_SCRATCH_COLUMN_WORLD_X;
  }
  while (GridFootprint_ClearTraversalFlagsDiagonalNegative
           (worldYQ12,worldXQ12,centerCellWorldY - GRID_SCRATCH_ROW_BELOW_WORLD_Y,sideRowLeftWorldX,
            GridScratchCell_StateMaskBits(leftWalkCursor)) != 0) {
    sideRowLeftWorldX = sideRowLeftWorldX - GRID_SCRATCH_COLUMN_WORLD_X;
    leftWalkCursor--;
  }
}

static bool GridReachability_IsOpenCell(GridScratchCell *cell)
{
  return (cell->stateMask & GRID_REACHABILITY_OPEN_STOP_MASK) == 0;
}

void GridReachability_MarkOpenRegionRecursive(uint32_t rowStrideBytes,GridScratchCell *currentCell)

{
  GridScanline_Fill(rowStrideBytes,currentCell,
                    [](GridScratchCell *spanCell,GridScratchCell **outLeftStopCell,
                       GridScratchCell **outRightStopCell) {
                      GridScratchCell *leftStopCell;
                      GridScratchCell *rightStopCell;

                      spanCell->stateMask = spanCell->stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
                      for (leftStopCell = spanCell - 1; GridReachability_IsOpenCell(leftStopCell); leftStopCell--) {
                        leftStopCell->stateMask = leftStopCell->stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
                      }
                      for (rightStopCell = spanCell + 1; GridReachability_IsOpenCell(rightStopCell);
                           rightStopCell++) {
                        rightStopCell->stateMask = rightStopCell->stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
                      }
                      *outLeftStopCell = leftStopCell;
                      *outRightStopCell = rightStopCell;
                    },
                    GridReachability_IsOpenCell);
}

static bool GridReachability_IsVisitedCostedCell(GridScratchCell *cell)
{
  return (cell->stateMask & GRID_SCRATCH_TRAVERSAL_VISITED) != 0 && cell->pathCost != 0;
}

void GridReachability_ClearCostedRegionRecursive(uint32_t rowStrideBytes,GridScratchCell *currentCell)

{
  GridScanline_Fill(rowStrideBytes,currentCell,
                    [](GridScratchCell *spanCell,GridScratchCell **outLeftStopCell,
                       GridScratchCell **outRightStopCell) {
                      GridScratchCell *leftStopCell;
                      GridScratchCell *rightStopCell;

                      spanCell->stateMask = spanCell->stateMask & ~GRID_SCRATCH_TRAVERSAL_VISITED;
                      for (leftStopCell = spanCell - 1; GridReachability_IsVisitedCostedCell(leftStopCell);
                           leftStopCell--) {
                        leftStopCell->stateMask = leftStopCell->stateMask & ~GRID_SCRATCH_TRAVERSAL_VISITED;
                      }
                      for (rightStopCell = spanCell + 1; GridReachability_IsVisitedCostedCell(rightStopCell);
                           rightStopCell++) {
                        rightStopCell->stateMask = rightStopCell->stateMask & ~GRID_SCRATCH_TRAVERSAL_VISITED;
                      }
                      *outLeftStopCell = leftStopCell;
                      *outRightStopCell = rightStopCell;
                    },
                    GridReachability_IsVisitedCostedCell);
}

bool GridReachability_RebuildConnectedRegionAroundWorldPoint
          (FieldGridRadiusUnits radiusMetric,Q12 worldYQ12,Q12 worldXQ12)

{
  uint32_t scratchWidth;
  uint32_t cellsToClear;
  uint32_t scaledRowTerm;
  int cellColumn;
  int cellRow;
  int cellsToScan;
  GridScratchCell *scratchCursor;
  uint32_t rowStrideBytes;
  bool moreBlocksRemain;

  cellsToClear = g_GridScratchWidth * g_GridScratchHeight;
  scratchCursor = g_GridScratchPrimary;
  do {
    scratchCursor->stateMask = scratchCursor->stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
    scratchCursor->pathCost = 0;
    scratchCursor[1].stateMask = scratchCursor[1].stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
    scratchCursor[1].pathCost = 0;
    scratchCursor[2].stateMask = scratchCursor[2].stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
    scratchCursor[2].pathCost = 0;
    scratchCursor[3].stateMask = scratchCursor[3].stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
    scratchCursor[3].pathCost = 0;
    scratchCursor[4].stateMask = scratchCursor[4].stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
    scratchCursor[4].pathCost = 0;
    scratchCursor[5].stateMask = scratchCursor[5].stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
    scratchCursor[5].pathCost = 0;
    scratchCursor[6].stateMask = scratchCursor[6].stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
    scratchCursor[6].pathCost = 0;
    scratchCursor[7].stateMask = scratchCursor[7].stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
    scratchCursor[7].pathCost = 0;
    scratchCursor[8].stateMask = scratchCursor[8].stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
    scratchCursor[8].pathCost = 0;
    scratchCursor[9].stateMask = scratchCursor[9].stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
    scratchCursor[9].pathCost = 0;
    scratchCursor[10].stateMask = scratchCursor[10].stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
    scratchCursor[10].pathCost = 0;
    scratchCursor[11].stateMask = scratchCursor[11].stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
    scratchCursor[11].pathCost = 0;
    scratchCursor[12].stateMask = scratchCursor[12].stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
    scratchCursor[12].pathCost = 0;
    scratchCursor[13].stateMask = scratchCursor[13].stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
    scratchCursor[13].pathCost = 0;
    scratchCursor[14].stateMask = scratchCursor[14].stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
    scratchCursor[14].pathCost = 0;
    scratchCursor[15].stateMask = scratchCursor[15].stateMask | GRID_SCRATCH_TRAVERSAL_VISITED;
    scratchCursor[15].pathCost = 0;
    scratchCursor = scratchCursor + 16;
    moreBlocksRemain = 15 < cellsToClear;
    cellsToClear = cellsToClear - 16;
  } while (moreBlocksRemain && cellsToClear != 0);
  GridFootprint_ClearTraversalFlagsAroundWorldPoint(radiusMetric * 3,worldYQ12,worldXQ12);
  scratchWidth = g_GridScratchWidth;
  scaledRowTerm = (int)((uint64_t)((int64_t)worldYQ12 * FIELD_GRID_WORLD_Y_TO_ROW_Q20) >> 32) << 11 |
          (uint32_t)((int64_t)worldYQ12 * FIELD_GRID_WORLD_Y_TO_ROW_Q20) >> 21;
  cellColumn = (int)((((int)((uint64_t)((int64_t)worldXQ12 * FIELD_GRID_WORLD_X_TO_COLUMN_Q20) >> 32) << 12 |
                 (uint32_t)((int64_t)worldXQ12 * FIELD_GRID_WORLD_X_TO_COLUMN_Q20) >> 20) - scaledRowTerm) +
                 GRID_SCRATCH_INDEX_BIAS_Q12) >> GRID_SCRATCH_CELL_SHIFT;
  cellRow = (int)(scaledRowTerm * 2 + GRID_SCRATCH_INDEX_BIAS_Q12) >> GRID_SCRATCH_CELL_SHIFT;
  if ((cellColumn < 0) || (cellRow < 0) || ((int)g_GridScratchWidth <= cellColumn) ||
      ((int)g_GridScratchHeight <= cellRow)) {
    return true;
  }
  rowStrideBytes = g_GridScratchWidth * 8;
  GridReachability_MarkOpenRegionRecursive
            (rowStrideBytes,g_GridScratchPrimary + (int32_t)(cellRow * g_GridScratchWidth) + cellColumn);
  GridFootprint_ClearTraversalFlagsAroundWorldPoint(radiusMetric,worldYQ12,worldXQ12);
  /* find the first marked cell inside the outer footprint that has a neighbour outside it (count 0); the
     tested cell is scratchCursor[scratchWidth], the cursor sits on the row above it */
  scratchCursor = g_GridScratchPrimary + scratchWidth * 3;
  cellsToScan = (g_GridScratchHeight - 8) * g_GridScratchWidth;
  /* The original scans ~2^32 cells here and below when the scratch grid has no row inside the 4-row border
     (a field grid of height <= 2 or width 0); bounded here because the scan runs far past the scratch grid:
     such a grid has no ring to split. */
  if (cellsToScan <= 0) {
    return false;
  }
  while (!GridReachability_IsMarkedRingEdgeCell(scratchCursor,scratchWidth)) {
    scratchCursor = scratchCursor + 1;
    cellsToScan--;
    if (cellsToScan == 0) {
      return false;
    }
  }
  GridReachability_ClearCostedRegionRecursive(rowStrideBytes,scratchCursor + scratchWidth);
  /* any other marked edge cell belongs to a separate piece of the ring */
  scratchCursor = g_GridScratchPrimary + scratchWidth * 3;
  cellsToScan = (g_GridScratchHeight - 8) * g_GridScratchWidth;
  /* count >= 1: the same count as the first scan, which returned for a count <= 0. */
  do {
    if (GridReachability_IsMarkedRingEdgeCell(scratchCursor,scratchWidth)) {
      return true;
    }
    scratchCursor = scratchCursor + 1;
    cellsToScan--;
  } while (cellsToScan != 0);
  return false;
}
}
