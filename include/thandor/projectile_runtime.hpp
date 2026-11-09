#pragma once
#include "thandor/simulation.hpp"
#include <optional>
namespace thandor {
// Native movement-maintenance order: age/expiry, cast this interval, move,
// guidance, then the trajectory callback. A mode2 jump is never part of the ray.
bool simulation_begin_projectile_runtime_step(SimulationProjectileState& projectile,
    const ShotDefinitionRecord& shot,uint32_t tick) noexcept;
void simulation_finish_projectile_runtime_step(SimulationProjectileState& projectile,
    const ShotDefinitionRecord& shot,const Vec3f* live_aim,const Vec3f* live_root) noexcept;
struct SimulationGuidanceAngles { int32_t yaw{},pitch{}; };
SimulationGuidanceAngles simulation_guidance_angles(SimulationGuidanceAngles current,
    SimulationGuidanceAngles target,int32_t maximum_turn) noexcept;
std::array<int32_t,3> simulation_fixed_direction_q12(int32_t pitch,int32_t yaw,int32_t speed) noexcept;
}
