// Source-derived integer terrain-ray contract from Open Thandor's
// world/terrain/field_raycast.cpp. Picking keeps its separate floating API.
#pragma once
#include "thandor/field_raycast.hpp"
#include "thandor/retail_contact.hpp"
#include <optional>
namespace thandor::native_field_ray {
using Q12=int32_t;
constexpr int Q12_SHIFT=12,Q12_ONE=4096,FIELD_GRID_CELL_Q12=4096;
constexpr int FIELD_GRID_WORLD_COLUMN_STEP_X=2305,FIELD_GRID_WORLD_ROW_STEP_Y=-1999;
#define FIXED_PRODUCT_SHR(p,s) static_cast<int32_t>(static_cast<int64_t>(p)>>(s))
#define FIXED_MUL_SHR(a,b,s) static_cast<int32_t>((int64_t(a)*int64_t(b))>>(s))
inline uint32_t FixedMath_Length3(int32_t z,int32_t y,int32_t x){
 return retail_uint64_sqrt(uint64_t(int64_t(x)*x)+uint64_t(int64_t(y)*y)+uint64_t(int64_t(z)*z));
}
static uint32_t TerrainTriangle_MulCrossByHeightDelta(uint64_t rayCrossLocal,uint32_t heightDelta,int *outHigh)

{
  uint64_t lowProduct;
  int crossHigh;

  crossHigh = (int)(rayCrossLocal >> 32);
  lowProduct = (uint64_t)heightDelta * (rayCrossLocal & UINT32_MAX);
  if ((int)heightDelta < 0) {
    *outHigh = (crossHigh * heightDelta - (int)rayCrossLocal) + (int)(lowProduct >> 32);
  }
  else {
    *outHigh = crossHigh * heightDelta + (int)(lowProduct >> 32);
  }
  return (uint32_t)lowProduct;
}

/* Hit position inside a triangle: the divisor is remaining + coord1 + coord0 (all high:low word pairs scaled by
   2^12), shifted back down by 12 bits. Returns false (outputs untouched) when the divisor's low word is zero.
   When the divisor does not fit a signed 32-bit word, divisor and both numerators are shifted down by another
   12 bits; then both numerators are divided by it (signed 64/32 division) into the coord1 and coord0 offsets. */
static bool TerrainTriangle_DivideHitNumerators
          (uint32_t remainingLow,int remainingHigh,uint32_t coord1Low,int coord1High,uint32_t coord0Low,
          int coord0High,int *outCoord1Offset,int *outCoord0Offset)

{
  uint32_t partialSumLow;
  uint32_t sumLow;
  uint32_t divisorLow;
  int divisorHigh;

  partialSumLow = remainingLow + coord1Low;
  sumLow = partialSumLow + coord0Low;
  divisorHigh = remainingHigh + coord1High + (uint32_t)(partialSumLow < remainingLow) + coord0High +
                (uint32_t)(sumLow < partialSumLow);
  divisorLow = (sumLow >> Q12_SHIFT) | divisorHigh * (1 << (32 - Q12_SHIFT));
  divisorHigh = divisorHigh >> Q12_SHIFT;
  if (divisorLow == 0) {
    return false;
  }
  if (((int)divisorLow < 0) ? (divisorHigh != -1) : (divisorHigh != 0)) {
    coord0Low = coord0Low >> Q12_SHIFT | coord0High * (1 << (32 - Q12_SHIFT));
    coord1Low = coord1Low >> Q12_SHIFT | coord1High * (1 << (32 - Q12_SHIFT));
    divisorLow = divisorLow >> Q12_SHIFT | divisorHigh * (1 << (32 - Q12_SHIFT));
    coord1High = coord1High >> Q12_SHIFT;
    coord0High = coord0High >> Q12_SHIFT;
  }
  *outCoord1Offset = (int)((int64_t)((uint64_t)(uint32_t)coord1High << 32 | (uint64_t)coord1Low) /
                           (int64_t)(int)divisorLow);
  *outCoord0Offset = (int)((int64_t)((uint64_t)(uint32_t)coord0High << 32 | (uint64_t)coord0Low) /
                           (int64_t)(int)divisorLow);
  return true;
}

/* First triangle of TerrainTriangle_IntersectRayDistance, based on corner 3 (local coordinates as given). Returns
   true with the distance in *outDistanceQ12 on a hit, false on a miss. */
static bool TerrainTriangle_IntersectRayCorner3Triangle
          (Q12 rayDeltaZQ12,Q12 gridRayDelta0Q12,Q12 gridRayDelta1Q12,Q12 rayOriginZQ12,
          Q12 cornerHeight1Q12,Q12 cornerHeight2Q12,Q12 cornerHeight3Q12,
          Q12 cellLocalCoord1Q12,Q12 cellLocalCoord0Q12,Q12 *outDistanceQ12)

{
  int64_t planeRate;
  int64_t planeOffset;
  uint64_t rayCrossLocal;
  uint32_t crossProductLow;
  int64_t originByDelta1;
  int64_t originByDelta0;
  uint32_t originShiftedLow;
  uint32_t rayZShiftedLow;
  uint32_t coord0PartialLow;
  uint32_t coord0NumeratorLow;
  int coord0NumeratorHigh;
  uint32_t coord1PartialLow;
  uint32_t coord1NumeratorLow;
  int coord1NumeratorHigh;
  uint32_t rateShiftedHigh;
  uint32_t rateShiftedLow;
  uint32_t remainingLow;
  int remainingHigh;
  int coord1Offset;
  int coord0Offset;
  int hitCoord1;
  int64_t coord0HeightProduct;
  int64_t coord1HeightProduct;
  int64_t worldXOffsetProduct;
  int64_t worldYOffsetProduct;

  planeRate = ((int64_t)(cornerHeight1Q12 - cornerHeight3Q12) * (int64_t)gridRayDelta0Q12 +
              (int64_t)(cornerHeight2Q12 - cornerHeight3Q12) * (int64_t)gridRayDelta1Q12) -
              ((int64_t)rayDeltaZQ12 << Q12_SHIFT);
  planeOffset = (int64_t)(cornerHeight1Q12 - cornerHeight3Q12) * (int64_t)cellLocalCoord1Q12 +
                (int64_t)(cornerHeight2Q12 - cornerHeight3Q12) * (int64_t)cellLocalCoord0Q12 +
                ((int64_t)(rayOriginZQ12 - cornerHeight3Q12) << Q12_SHIFT);
  /* the ray must cross the triangle's plane */
  if (planeOffset < 0) {
    if ((planeRate >= 0) || (planeOffset - planeRate < 0)) {
      return false;
    }
  }
  else if ((planeRate < 0) || (planeOffset - planeRate >= 0)) {
    return false;
  }
  /* test the edges: each numerator must have the sign of the plane rate */
  rayCrossLocal = (int64_t)gridRayDelta1Q12 * (int64_t)cellLocalCoord1Q12 -
                  (int64_t)cellLocalCoord0Q12 * (int64_t)gridRayDelta0Q12;
  crossProductLow = TerrainTriangle_MulCrossByHeightDelta
                              (rayCrossLocal,cornerHeight1Q12 - cornerHeight3Q12,&coord0NumeratorHigh);
  originByDelta1 = (int64_t)(rayOriginZQ12 - cornerHeight3Q12) * (int64_t)gridRayDelta1Q12;
  originShiftedLow = (uint32_t)originByDelta1 * Q12_ONE;
  coord0PartialLow = crossProductLow + originShiftedLow;
  rayZShiftedLow = (uint32_t)((int64_t)rayDeltaZQ12 * (int64_t)cellLocalCoord0Q12) * Q12_ONE;
  coord0NumeratorLow = coord0PartialLow + rayZShiftedLow;
  coord0NumeratorHigh = coord0NumeratorHigh + FIXED_PRODUCT_SHR(originByDelta1, 32 - Q12_SHIFT) +
                        (uint32_t)(coord0PartialLow < crossProductLow) +
                        FIXED_MUL_SHR(rayDeltaZQ12, cellLocalCoord0Q12, 32 - Q12_SHIFT) +
                        (uint32_t)(coord0NumeratorLow < coord0PartialLow);
  if ((coord0NumeratorHigh < 0) != (planeRate < 0)) {
    return false;
  }
  crossProductLow = TerrainTriangle_MulCrossByHeightDelta
                              (rayCrossLocal,cornerHeight3Q12 - cornerHeight2Q12,&coord1NumeratorHigh);
  originByDelta0 = (int64_t)(rayOriginZQ12 - cornerHeight3Q12) * (int64_t)gridRayDelta0Q12;
  originShiftedLow = (uint32_t)originByDelta0 * Q12_ONE;
  coord1PartialLow = crossProductLow + originShiftedLow;
  rayZShiftedLow = (uint32_t)((int64_t)rayDeltaZQ12 * (int64_t)cellLocalCoord1Q12) * Q12_ONE;
  rateShiftedHigh = FIXED_PRODUCT_SHR(planeRate, 32 - Q12_SHIFT);
  rateShiftedLow = (uint32_t)planeRate * Q12_ONE;
  coord1NumeratorLow = rayZShiftedLow + coord1PartialLow;
  coord1NumeratorHigh = FIXED_MUL_SHR(rayDeltaZQ12, cellLocalCoord1Q12, 32 - Q12_SHIFT) +
                        coord1NumeratorHigh + FIXED_PRODUCT_SHR(originByDelta0, 32 - Q12_SHIFT) +
                        (uint32_t)(coord1PartialLow < crossProductLow) +
                        (uint32_t)(coord1NumeratorLow < rayZShiftedLow);
  if ((coord1NumeratorHigh < 0) != ((int)rateShiftedHigh < 0)) {
    return false;
  }
  /* third edge: plane rate minus both numerators */
  remainingLow = (rateShiftedLow - coord1NumeratorLow) - coord0NumeratorLow;
  remainingHigh = (((rateShiftedHigh - coord1NumeratorHigh) - (uint32_t)(rateShiftedLow < coord1NumeratorLow)) -
                   coord0NumeratorHigh) - (uint32_t)(rateShiftedLow - coord1NumeratorLow < coord0NumeratorLow);
  if ((coord1NumeratorHigh < 0) != (remainingHigh < 0)) {
    return false;
  }
  /* inside the first triangle: intersection distance */
  if (!TerrainTriangle_DivideHitNumerators(remainingLow,remainingHigh,coord1NumeratorLow,coord1NumeratorHigh,
                                           coord0NumeratorLow,coord0NumeratorHigh,&coord1Offset,&coord0Offset)) {
    *outDistanceQ12 = 0;
    return true;
  }
  hitCoord1 = cellLocalCoord1Q12 + coord1Offset;
  coord0HeightProduct = (int64_t)coord0Offset * (int64_t)(cornerHeight2Q12 - cornerHeight3Q12);
  coord1HeightProduct = (int64_t)coord1Offset * (int64_t)(cornerHeight1Q12 - cornerHeight3Q12);
  /* grid offsets of the hit back to world units (inverse of FIELD_GRID_WORLD_*_Q20): X = (column + row / 2)
     * FIELD_GRID_WORLD_COLUMN_STEP_X / 0x1000, Y = row * FIELD_GRID_WORLD_ROW_STEP_Y / 0x1000 */
  worldXOffsetProduct = (int64_t)(hitCoord1 + (cellLocalCoord0Q12 + coord0Offset) * 2) * FIELD_GRID_WORLD_COLUMN_STEP_X;
  worldYOffsetProduct = (int64_t)hitCoord1 * FIELD_GRID_WORLD_ROW_STEP_Y;
  *outDistanceQ12 =
       FixedMath_Length3(((cornerHeight3Q12 + (FIXED_PRODUCT_SHR(coord0HeightProduct, Q12_SHIFT))) - rayOriginZQ12) +
                         (FIXED_PRODUCT_SHR(coord1HeightProduct, Q12_SHIFT)),
                         FIXED_PRODUCT_SHR(worldYOffsetProduct, 12),
                         FIXED_PRODUCT_SHR(worldXOffsetProduct, Q12_SHIFT + 1));
  return true;
}

/* Second triangle of TerrainTriangle_IntersectRayDistance, based on the far corner 0 (local coordinates shifted by
   one cell). Returns true with the distance in *outDistanceQ12 on a hit, false on a miss. */
static bool TerrainTriangle_IntersectRayCorner0Triangle
          (Q12 rayDeltaZQ12,Q12 gridRayDelta0Q12,Q12 gridRayDelta1Q12,Q12 rayOriginZQ12,
          Q12 cornerHeight0Q12,Q12 cornerHeight1Q12,Q12 cornerHeight2Q12,
          Q12 cellLocalCoord1Q12,Q12 cellLocalCoord0Q12,Q12 *outDistanceQ12)

{
  int farCoord0;
  int farCoord1;
  int64_t planeRate;
  uint32_t rateLow;
  int rateHigh;
  int64_t planeOffset;
  int planeOffsetHigh;
  int offsetMinusRateHigh;
  uint64_t rayCrossLocal;
  uint32_t crossProductLow;
  int64_t originByDelta1;
  int64_t originByDelta0;
  uint32_t coord0PartialLow;
  uint32_t coord0NumeratorLow;
  int coord0NumeratorHigh;
  uint32_t coord1PartialLow;
  uint32_t coord1NumeratorLow;
  int coord1NumeratorHigh;
  uint32_t rayZShiftedLow;
  uint32_t rateShiftedHigh;
  uint32_t rateShiftedLow;
  uint32_t remainingLow;
  int remainingHigh;
  int coord1Offset;
  int coord0Offset;
  int hitCoord1;
  int64_t coord0HeightProduct;
  int64_t coord1HeightProduct;
  int64_t worldXOffsetProduct;
  int64_t worldYOffsetProduct;

  farCoord0 = cellLocalCoord0Q12 + FIELD_GRID_CELL_Q12;
  farCoord1 = cellLocalCoord1Q12 + FIELD_GRID_CELL_Q12;
  planeRate = (int64_t)(cornerHeight2Q12 - cornerHeight0Q12) * (int64_t)gridRayDelta0Q12 +
              (int64_t)(cornerHeight1Q12 - cornerHeight0Q12) * (int64_t)gridRayDelta1Q12 +
              ((int64_t)rayDeltaZQ12 << Q12_SHIFT);
  rateLow = (uint32_t)planeRate;
  rateHigh = (int)((uint64_t)planeRate >> 32);
  planeOffset = (int64_t)(cornerHeight2Q12 - cornerHeight0Q12) * (int64_t)farCoord1 +
                (int64_t)(cornerHeight1Q12 - cornerHeight0Q12) * (int64_t)farCoord0 +
                ((int64_t)(cornerHeight0Q12 - rayOriginZQ12) << Q12_SHIFT);
  planeOffsetHigh = (int)((uint64_t)planeOffset >> 32);
  /* the ray must cross the triangle's plane (high word of planeOffset - planeRate) */
  offsetMinusRateHigh = (int)((planeOffsetHigh - rateHigh) - (uint32_t)((uint32_t)planeOffset < rateLow));
  if (planeOffset < 0) {
    if ((planeRate >= 0) || (offsetMinusRateHigh < 0)) {
      return false;
    }
  }
  else if ((planeRate < 0) || (offsetMinusRateHigh >= 0)) {
    return false;
  }
  /* test the edges: each numerator must have the sign of the plane rate */
  rayCrossLocal = (int64_t)gridRayDelta1Q12 * (int64_t)farCoord1 - (int64_t)farCoord0 * (int64_t)gridRayDelta0Q12;
  crossProductLow = TerrainTriangle_MulCrossByHeightDelta
                              (rayCrossLocal,cornerHeight0Q12 - cornerHeight2Q12,&coord0NumeratorHigh);
  originByDelta1 = (int64_t)(rayOriginZQ12 - cornerHeight0Q12) * (int64_t)gridRayDelta1Q12;
  coord0PartialLow = crossProductLow + (uint32_t)originByDelta1 * Q12_ONE;
  rayZShiftedLow = (uint32_t)((int64_t)rayDeltaZQ12 * (int64_t)farCoord0) * Q12_ONE;
  coord0NumeratorLow = coord0PartialLow + rayZShiftedLow;
  coord0NumeratorHigh = coord0NumeratorHigh + FIXED_PRODUCT_SHR(originByDelta1, 32 - Q12_SHIFT) +
                        (uint32_t)(coord0PartialLow < crossProductLow) +
                        FIXED_MUL_SHR(rayDeltaZQ12, farCoord0, 32 - Q12_SHIFT) +
                        (uint32_t)(coord0NumeratorLow < coord0PartialLow);
  if ((coord0NumeratorHigh < 0) != (planeRate < 0)) {
    return false;
  }
  crossProductLow = TerrainTriangle_MulCrossByHeightDelta
                              (rayCrossLocal,cornerHeight1Q12 - cornerHeight0Q12,&coord1NumeratorHigh);
  originByDelta0 = (int64_t)(rayOriginZQ12 - cornerHeight0Q12) * (int64_t)gridRayDelta0Q12;
  coord1PartialLow = crossProductLow + (uint32_t)originByDelta0 * Q12_ONE;
  rayZShiftedLow = (uint32_t)((int64_t)rayDeltaZQ12 * (int64_t)farCoord1) * Q12_ONE;
  rateShiftedHigh = rateHigh << Q12_SHIFT | rateLow >> (32 - Q12_SHIFT);
  rateShiftedLow = rateLow * Q12_ONE;
  coord1NumeratorLow = rayZShiftedLow + coord1PartialLow;
  coord1NumeratorHigh = FIXED_MUL_SHR(rayDeltaZQ12, farCoord1, 32 - Q12_SHIFT) +
                        coord1NumeratorHigh + FIXED_PRODUCT_SHR(originByDelta0, 32 - Q12_SHIFT) +
                        (uint32_t)(coord1PartialLow < crossProductLow) +
                        (uint32_t)(coord1NumeratorLow < rayZShiftedLow);
  if ((coord1NumeratorHigh < 0) != ((int)rateShiftedHigh < 0)) {
    return false;
  }
  /* third edge: plane rate minus both numerators */
  remainingLow = (rateShiftedLow - coord1NumeratorLow) - coord0NumeratorLow;
  remainingHigh = (((rateShiftedHigh - coord1NumeratorHigh) - (uint32_t)(rateShiftedLow < coord1NumeratorLow)) -
                   coord0NumeratorHigh) - (uint32_t)(rateShiftedLow - coord1NumeratorLow < coord0NumeratorLow);
  if ((coord1NumeratorHigh < 0) != (remainingHigh < 0)) {
    return false;
  }
  /* inside the second triangle: intersection distance */
  if (!TerrainTriangle_DivideHitNumerators(remainingLow,remainingHigh,coord1NumeratorLow,coord1NumeratorHigh,
                                           coord0NumeratorLow,coord0NumeratorHigh,&coord1Offset,&coord0Offset)) {
    *outDistanceQ12 = 0;
    return true;
  }
  hitCoord1 = farCoord1 - coord1Offset;
  coord0HeightProduct = (int64_t)coord0Offset * (int64_t)(cornerHeight1Q12 - cornerHeight0Q12);
  coord1HeightProduct = (int64_t)coord1Offset * (int64_t)(cornerHeight2Q12 - cornerHeight0Q12);
  /* back to world units as for the first triangle */
  worldXOffsetProduct = (int64_t)(hitCoord1 + (farCoord0 - coord0Offset) * 2) * FIELD_GRID_WORLD_COLUMN_STEP_X;
  worldYOffsetProduct = (int64_t)hitCoord1 * FIELD_GRID_WORLD_ROW_STEP_Y;
  *outDistanceQ12 =
       FixedMath_Length3(((cornerHeight0Q12 + (FIXED_PRODUCT_SHR(coord0HeightProduct, Q12_SHIFT))) - rayOriginZQ12) +
                         (FIXED_PRODUCT_SHR(coord1HeightProduct, Q12_SHIFT)),
                         FIXED_PRODUCT_SHR(worldYOffsetProduct, 12),
                         FIXED_PRODUCT_SHR(worldXOffsetProduct, Q12_SHIFT + 1));
  return true;
}

/* Terrain raycast step: intersects the ray segment (grid-space delta, Z origin and Z delta, relative to the cell)
   with the two triangles of one field-grid cell given its four corner heights, first the one based on corner 3,
   then the one based on corner 0 (the far corner, local coordinates shifted by one cell). A hit returns true with
   the world distance from the ray origin in *outDistanceQ12; a miss (also the quick reject when all four corners
   lie below the ray's lowest point) returns false and leaves *outDistanceQ12 untouched.
*/
static bool TerrainTriangle_IntersectRayDistance
          (Q12 rayDeltaZQ12,Q12 gridRayDelta0Q12,Q12 gridRayDelta1Q12,Q12 rayOriginZQ12,
          Q12 cornerHeight0Q12,Q12 cornerHeight1Q12,Q12 cornerHeight2Q12,Q12 cornerHeight3Q12,
          Q12 cellLocalCoord1Q12,Q12 cellLocalCoord0Q12,Q12 *outDistanceQ12)

{
  Q12 lowestRayZQ12;

  lowestRayZQ12 = rayOriginZQ12;
  if (rayDeltaZQ12 < 0) {
    lowestRayZQ12 = rayOriginZQ12 + rayDeltaZQ12;
  }
  /* quick reject: all four corners at or below the ray's lowest point */
  if ((cornerHeight3Q12 <= lowestRayZQ12) && (cornerHeight2Q12 <= lowestRayZQ12) &&
      (cornerHeight1Q12 <= lowestRayZQ12) && (cornerHeight0Q12 <= lowestRayZQ12)) {
    return false;
  }
  if (TerrainTriangle_IntersectRayCorner3Triangle(rayDeltaZQ12,gridRayDelta0Q12,gridRayDelta1Q12,rayOriginZQ12,
                                                  cornerHeight1Q12,cornerHeight2Q12,cornerHeight3Q12,
                                                  cellLocalCoord1Q12,cellLocalCoord0Q12,outDistanceQ12)) {
    return true;
  }
  return TerrainTriangle_IntersectRayCorner0Triangle(rayDeltaZQ12,gridRayDelta0Q12,gridRayDelta1Q12,rayOriginZQ12,
                                                     cornerHeight0Q12,cornerHeight1Q12,cornerHeight2Q12,
                                                     cellLocalCoord1Q12,cellLocalCoord0Q12,outDistanceQ12);
}

#undef FIXED_PRODUCT_SHR
#undef FIXED_MUL_SHR
inline std::optional<FieldRaycastHit> raycast(const FieldGrid& field,Vec3f origin,
    int32_t pitch,int32_t yaw,int32_t scale,bool secondary){
 const int32_t ox=float_to_q12(origin.x),oy=float_to_q12(origin.y),oz=float_to_q12(origin.z);
 const auto direction=retail_direction_scaled(uint32_t(pitch),uint32_t(yaw),scale);
 const auto grid=[](int32_t x,int32_t y){
  const int32_t half=int32_t((int64_t(y)*-0x20c8cc)>>21);
  return std::array<int32_t,2>{int32_t((int64_t(x)*0x1c6e9c)>>20)-half,half*2};
 };
 const auto start=grid(ox,oy),end=grid(ox+direction[0],oy+direction[1]);
 const int32_t dc=end[0]-start[0],dr=end[1]-start[1];
 int32_t c=start[0]&~4095,r=start[1]&~4095;
 for(unsigned step=0;step<1023;++step){
  const int32_t x=c>>12,y=r>>12;
  if(x>=0&&y>=0&&x+1<int32_t(field.width())&&y+1<int32_t(field.height())){
   const auto height=[&](uint32_t cx,uint32_t cy){const auto& cell=field.at(cx,cy);
    return cell.terrain_height_q12+(secondary?cell.water_delta_q12:0);};
   int32_t distance=0;
   if(TerrainTriangle_IntersectRayDistance(direction[2],dr,dc,oz,
        height(x+1,y+1),height(x,y+1),height(x+1,y),height(x,y),
        r-start[1],c-start[0],&distance)){
    const auto unit=retail_direction_scaled(uint32_t(pitch),uint32_t(yaw),distance);
    return FieldRaycastHit{uint32_t(x),uint32_t(y),0,0,
      {q12_to_float(ox+unit[0]),q12_to_float(oy+unit[1]),q12_to_float(oz+unit[2])},
      q12_to_float(distance),secondary,1};
   }
  }
  if(end[0]>=c&&end[1]>=r&&end[0]-c<=4096&&end[1]-r<=4096)break;
  if(dr>0){
   const int64_t side=int64_t(dc)*(r+4096-start[1])+int64_t(start[0]-c)*dr;
   if(side>=0&&int64_t(dr)*4096-side>=0){r+=4096;continue;}
  }else if(dr<0){
   const int64_t side=int64_t(dc)*(r-start[1])+int64_t(start[0]-c)*dr;
   if(side<0&&int64_t(dr)*4096-side<0){r-=4096;continue;}
  }
  if(dc==0)break;
  c+=dc<0?-4096:4096;
 }
 return {};
}
} // namespace thandor::native_field_ray
