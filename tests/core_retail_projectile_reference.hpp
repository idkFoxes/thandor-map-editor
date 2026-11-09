// Test-only unchanged native reference bodies; detached types/globals below.
#pragma once
#include "thandor/retail_placement.hpp"
#include <cstdint>
namespace native_v626 {
using Q12=int32_t;using AngleTurn32=int32_t;using AngleTurn16Stored32=int32_t;using FixedMathScale32=int32_t;
inline constexpr uint32_t FIXED_ANGLE16_MASK=65535,FIXED_ANGLE16_FULL_TURN=65536;
inline constexpr uint32_t FIXED_SINE_TABLE_COS=32768,FIXED_SINE_TABLE_SIN=16384;
#define FIXED_PRODUCT_SHR(v,n) int32_t((v)>>(n))
struct SineTable {int32_t operator[](uint32_t index)const{return thandor::retail_placement_sine_table_entry(index);}};
inline SineTable g_FixedSineQ28;
struct FixedDirection{int32_t x{},y{},z{};};
struct FixedVectorAngles{int32_t azimuthAngle{},elevationAngle{};};inline FixedVectorAngles desired{};
inline FixedVectorAngles FixedMath_VectorToAngles(int32_t,int32_t,int32_t){return desired;}
struct GraphicsShadingRuntimeRecord{int32_t worldXQ12{},worldYQ12{},worldZQ12{};};
struct ModelRuntimeNode {
 struct {int32_t worldRotationAngle0{},worldRotationAngle1{},worldRotationAngle2{};}modelPayload;
 struct {FixedDirection translation;}worldTransform;
 ModelRuntimeNode* childNodes[1]{};GraphicsShadingRuntimeRecord* shadingRecord{};
};
struct ModelDefinition{uint32_t runtimeClassId{};int32_t aimHeightOffsetQ12{};};
struct ModelRuntimeSlot{struct{ModelRuntimeNode* modelNode{};}rootModelNodeOrSavedOffset;struct{ModelDefinition* runtimeDefinition{};}definitionOrSavedId;};
inline constexpr uint32_t MODEL_RUNTIME_CLASS_21_AIRCRAFT=21;
struct ShotDefinition{int32_t guidanceTurnLimitAngle16{};uint32_t trajectoryRampDurationTicks{};int32_t launchSpeedQ12{},elevationOffsetAngle16{};uint32_t fixedRangeTransitionAgeThresholdTicks{};};
struct ShotRuntimeSlot{struct{ShotDefinition* definition{};}definitionOrSavedId;uint32_t projectileAgeTicks{};int32_t launchSpeedQ12{},elevationOffsetAngle16{};struct {uintptr_t runtimeState{};ModelRuntimeSlot* targetModelRuntime{};}runtimeStateOrSavedOffset;};
inline bool released{};inline void ShotModel_ReleaseAndUnlink(ShotRuntimeSlot*,ShotDefinition*,ModelRuntimeNode*){released=true;}
FixedDirection FixedMath_DirectionFromAnglesScaled(AngleTurn32 elevationAngle,AngleTurn32 azimuthAngle,
          FixedMathScale32 scale)

{
  int64_t scaledYProduct;
  uint32_t azimuthPlusElevationAngle16;
  uint32_t elevationAngle16;
  uint32_t azimuthMinusElevationAngle16;
  FixedDirection scaledDirection;
  int64_t scaledXProduct;

  elevationAngle16 = elevationAngle & FIXED_ANGLE16_MASK;
  azimuthPlusElevationAngle16 = (elevationAngle16 + azimuthAngle) & FIXED_ANGLE16_MASK;
  azimuthMinusElevationAngle16 = (azimuthAngle - elevationAngle16) & FIXED_ANGLE16_MASK;
  scaledXProduct =
       (int64_t)(g_FixedSineQ28[FIXED_SINE_TABLE_COS + azimuthPlusElevationAngle16] +
                 g_FixedSineQ28[FIXED_SINE_TABLE_COS + azimuthMinusElevationAngle16]) *
       (int64_t)scale;
  scaledYProduct =
       (int64_t)(g_FixedSineQ28[FIXED_SINE_TABLE_SIN + azimuthPlusElevationAngle16] +
                 g_FixedSineQ28[FIXED_SINE_TABLE_SIN + azimuthMinusElevationAngle16]) *
       (int64_t)scale;
  scaledDirection.z =
       FIXED_PRODUCT_SHR((int64_t)g_FixedSineQ28[FIXED_SINE_TABLE_SIN + elevationAngle16] * (int64_t)scale,28);
  scaledDirection.y = FIXED_PRODUCT_SHR(scaledYProduct,29);
  scaledDirection.x = FIXED_PRODUCT_SHR(scaledXProduct,29);
  return scaledDirection;
}
static void ShotProjectile_SteerTowardsTarget
          (ShotDefinition *shotDefinition,ModelRuntimeSlot *targetModelRuntime,ModelRuntimeNode *shotNode)

{
  ModelRuntimeNode *targetNode;
  FixedVectorAngles targetAngles;
  AngleTurn16Stored32 elevationTurnDeltaAngle16;
  AngleTurn16Stored32 headingTurnDeltaAngle16;
  int maximumTurn;
  int minimumTurn;

  targetNode = targetModelRuntime->rootModelNodeOrSavedOffset.modelNode;
  if (targetModelRuntime->definitionOrSavedId.runtimeDefinition->runtimeClassId ==
      MODEL_RUNTIME_CLASS_21_AIRCRAFT) {
    targetNode = targetNode->childNodes[0];
  }
  targetAngles = FixedMath_VectorToAngles
                     ((targetModelRuntime->definitionOrSavedId.runtimeDefinition->aimHeightOffsetQ12 +
                       targetNode->worldTransform.translation.z) - shotNode->worldTransform.translation.z,
                      targetNode->worldTransform.translation.y - shotNode->worldTransform.translation.y,
                      targetNode->worldTransform.translation.x - shotNode->worldTransform.translation.x);
  elevationTurnDeltaAngle16 = targetAngles.elevationAngle - shotNode->modelPayload.worldRotationAngle1;
  maximumTurn = shotDefinition->guidanceTurnLimitAngle16;
  headingTurnDeltaAngle16 =
       (int)((targetAngles.azimuthAngle - shotNode->modelPayload.worldRotationAngle0) * FIXED_ANGLE16_FULL_TURN)
       >> 16; /* wrapped to a signed 16-bit turn */
  if (maximumTurn < elevationTurnDeltaAngle16) {
    elevationTurnDeltaAngle16 = maximumTurn;
  }
  if (maximumTurn < headingTurnDeltaAngle16) {
    headingTurnDeltaAngle16 = maximumTurn;
  }
  minimumTurn = -maximumTurn;
  if (headingTurnDeltaAngle16 < minimumTurn) {
    headingTurnDeltaAngle16 = minimumTurn;
  }
  if (elevationTurnDeltaAngle16 < minimumTurn) {
    elevationTurnDeltaAngle16 = minimumTurn;
  }
  shotNode->modelPayload.worldRotationAngle1 += elevationTurnDeltaAngle16;
  shotNode->modelPayload.worldRotationAngle0 =
       headingTurnDeltaAngle16 + shotNode->modelPayload.worldRotationAngle0 & FIXED_ANGLE16_MASK;
}
static void ShotProjectile_UpdateLeadAdjustedTrajectory
          (ShotDefinition *shotDefinition,ShotRuntimeSlot *shotRuntime,ModelRuntimeNode *shotNode)

{
  uint32_t rampAgeTicks;
  uint32_t elevationWindowTicks;
  int elevationRemainingTicks;
  int elevationOffsetStep;

  if (shotDefinition->trajectoryRampDurationTicks == 0) {
    return;
  }
  rampAgeTicks = shotRuntime->projectileAgeTicks;
  if (shotDefinition->trajectoryRampDurationTicks < rampAgeTicks) {
    rampAgeTicks = shotDefinition->trajectoryRampDurationTicks;
  }
  shotRuntime->launchSpeedQ12 =
       (Q12)(((int64_t)
              (int)(((int64_t)(int)rampAgeTicks * (int64_t)(int)rampAgeTicks) /
                   (int64_t)(int)shotDefinition->trajectoryRampDurationTicks) *
             (int64_t)shotDefinition->launchSpeedQ12) /
            (int64_t)(int)shotDefinition->trajectoryRampDurationTicks);
  if (shotDefinition->elevationOffsetAngle16 != 0) {
    elevationWindowTicks = shotDefinition->trajectoryRampDurationTicks * 2;
    elevationRemainingTicks = elevationWindowTicks - shotRuntime->projectileAgeTicks;
    if (elevationWindowTicks < shotRuntime->projectileAgeTicks) {
      elevationRemainingTicks = 0;
    }
    elevationOffsetStep =
         ((int)(((int64_t)elevationRemainingTicks * (int64_t)shotDefinition->elevationOffsetAngle16) /
                (int64_t)(int)shotDefinition->trajectoryRampDurationTicks) >> 1) -
         shotRuntime->elevationOffsetAngle16;
    shotRuntime->elevationOffsetAngle16 = shotRuntime->elevationOffsetAngle16 + elevationOffsetStep;
    shotNode->modelPayload.worldRotationAngle1 += elevationOffsetStep;
  }
}
static void ShotProjectile_ShiftOverTarget
          (ModelRuntimeSlot *targetModelRuntime,ModelRuntimeNode *shotNode,
           GraphicsShadingRuntimeRecord *nodeShadingRecord)

{
  ModelRuntimeNode *targetNode;
  int targetDeltaX;
  int targetDeltaY;

  targetNode = targetModelRuntime->rootModelNodeOrSavedOffset.modelNode;
  targetDeltaX = targetNode->worldTransform.translation.x - shotNode->worldTransform.translation.x;
  targetDeltaY = targetNode->worldTransform.translation.y - shotNode->worldTransform.translation.y;
  shotNode->worldTransform.translation.x = shotNode->worldTransform.translation.x + targetDeltaX;
  shotNode->worldTransform.translation.y = shotNode->worldTransform.translation.y + targetDeltaY;
  if (nodeShadingRecord != nullptr) {
    nodeShadingRecord->worldXQ12 = nodeShadingRecord->worldXQ12 + targetDeltaX;
    nodeShadingRecord->worldYQ12 = nodeShadingRecord->worldYQ12 + targetDeltaY;
  }
}
static bool ShotProjectile_UpdateFixedRangeTrajectory
          (ShotDefinition *shotDefinition,ShotRuntimeSlot *shotRuntime,ModelRuntimeNode *shotNode)

{
  uint32_t rampAgeTicks;
  GraphicsShadingRuntimeRecord *nodeShadingRecord;

  if ((int)shotNode->modelPayload.worldRotationAngle1 < 0) {
    nodeShadingRecord = shotNode->shadingRecord;
    if (shotRuntime->runtimeStateOrSavedOffset.runtimeState != 0) {
      ShotProjectile_ShiftOverTarget
                (shotRuntime->runtimeStateOrSavedOffset.targetModelRuntime,shotNode,
                 nodeShadingRecord);
    }
    return false;
  }
  if (shotDefinition->trajectoryRampDurationTicks != 0) {
    rampAgeTicks = shotRuntime->projectileAgeTicks;
    if (shotDefinition->trajectoryRampDurationTicks < rampAgeTicks) {
      rampAgeTicks = shotDefinition->trajectoryRampDurationTicks;
    }
    shotRuntime->launchSpeedQ12 =
         (Q12)(((int64_t)
                (int)(((int64_t)
                       (int)(((int64_t)(int)rampAgeTicks * (int64_t)(int)rampAgeTicks) /
                            (int64_t)(int)shotDefinition->trajectoryRampDurationTicks) *
                      (int64_t)(int)rampAgeTicks) /
                     (int64_t)(int)shotDefinition->trajectoryRampDurationTicks) *
               (int64_t)shotDefinition->launchSpeedQ12) /
              (int64_t)(int)shotDefinition->trajectoryRampDurationTicks);
  }
  if (shotDefinition->fixedRangeTransitionAgeThresholdTicks <= shotRuntime->projectileAgeTicks) {
    shotDefinition = shotRuntime->definitionOrSavedId.definition;
    if (shotRuntime->runtimeStateOrSavedOffset.runtimeState == 0) {
      ShotModel_ReleaseAndUnlink(shotRuntime,shotDefinition,shotNode);
      return true;
    }
    nodeShadingRecord = shotNode->shadingRecord;
    ShotProjectile_ShiftOverTarget
              (shotRuntime->runtimeStateOrSavedOffset.targetModelRuntime,shotNode,
               nodeShadingRecord);
    shotNode->modelPayload.worldRotationAngle1 = -shotNode->modelPayload.worldRotationAngle1;
  }
  return false;
}

#undef FIXED_PRODUCT_SHR
}
