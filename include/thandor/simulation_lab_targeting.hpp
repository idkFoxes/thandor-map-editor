#pragma once
#include "thandor/simulation_lab.hpp"
#include <functional>
namespace thandor {
struct SimulationLabCombatScoreInput {
    int32_t source_range_q12{};
    int32_t delta_x_q12{},delta_y_q12{};
    uint32_t target_runtime_class{};
    int32_t source_counter_for_target{},target_counter_for_source{};
    int32_t target_condition_q12{4096};
    int32_t source_class_count{1};
};
struct SimulationLabCombatScore { int32_t value{};uint32_t command_generation_shift{}; };
// Numeric AiCombatTarget_EvaluateCandidateScore terms, before the separate weapon
// obstruction test. Relation, occupancy and depth-bin gates belong to the caller.
SimulationLabCombatScore simulation_lab_combat_score(const SimulationLabCombatScoreInput& input) noexcept;
struct SimulationLabCombatMetrics {
    std::array<int32_t,8> class_damage_q12{};
    int32_t range_q12{},class_sum{},condition_q12{};
};
SimulationLabCombatMetrics simulation_lab_combat_metrics(const SimulationLabEntity& entity) noexcept;
struct SimulationLabCombatTargetChoice {
    std::optional<size_t> entity_index;
    SimulationLabCombatScore score;
};
// Factory ground products own normal model callbacks while their movement
// remains locked to the factory. Parked aircraft retain their separate hold.
inline bool simulation_lab_command_target_owner_available(
    const SimulationLabEntity& entity) noexcept {
    const auto cls=entity.runtime_model_class_id;
    const bool factory_ground=!entity.production_parked_on_pad&&
        (cls==1U||cls==2U||cls==3U||cls==17U||cls==18U||cls==19U);
    return simulation_lab_has_live_root_model_owner(entity)&&
           (!entity.production_linked_to_parent||factory_ground);
}
// Direct combat candidate selection rejects the destroyed/terminal owner even
// while the world-list node is retained.  Keep that narrower gate separate
// from workspace02/03/07 and existing command-target lifetime.
inline bool simulation_lab_combat_target_owner_available(
    const SimulationLabEntity& entity) noexcept {
    return simulation_lab_command_target_owner_available(entity) &&
           !entity.root_model_terminal;
}
// Positive-counter hostile firing search, restricted to a live weapon's actual range.
// The numeric command score retains its separate range+2 domain. Repair commands, obstruction callbacks and native
// owner-list interleaving remain separate. Supply one metric per entity to reuse a
// single derived-metric pass across the tick's weapon updates.
SimulationLabCombatTargetChoice simulation_lab_select_combat_target(
    const SimulationLabRun& run,const FieldGrid& field,size_t source_index,
    std::span<const SimulationLabCombatMetrics> metrics={}, bool command_search=false,
    std::span<const std::uint8_t> line_of_fire_blocked={},
    const std::function<bool(size_t)>& is_blocked={});
// Positive-damage military command subset of AiCombatDecision_UpdateTargetAssignment.
// Firing retains its actual-range search; command search uses the native range+2 domain.
void simulation_lab_service_combat_command(SimulationLabRun& run,const FieldGrid& field,
    size_t source_index,std::span<const SimulationLabCombatMetrics> metrics,uint32_t step_ticks,
    std::span<const std::uint8_t> line_of_fire_blocked={},
    const std::function<bool(size_t)>& is_blocked={});
void simulation_lab_set_position_command(SimulationLabEntity& entity) noexcept;
void simulation_lab_set_group_target_command(SimulationLabEntity& entity,size_t target) noexcept;
// Recovered player-selection command writers.  Both retain the native
// targetClassShotDamage[0] (stateOrTechnologyId) gate.  The caller validates
// the selected owner and, for an entity target, the target owner's lifetime.
bool simulation_lab_apply_selection_army_target(
    SimulationLabEntity& entity,size_t target) noexcept;
bool simulation_lab_apply_selection_target_position(
    SimulationLabEntity& entity,Vec3f target) noexcept;
bool simulation_lab_combat_command_class(const SimulationLabEntity& entity) noexcept;
bool simulation_lab_weapon_callback_active(const SimulationLabEntity& entity,
    const SimulationWeaponProfile& profile) noexcept;
void simulation_lab_clear_released_target(SimulationLabRun& run,size_t released_index) noexcept;
} // namespace thandor
