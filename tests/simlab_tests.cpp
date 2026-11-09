#include "thandor/simulation_lab.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() try {
    auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    // V531 closes special-site eligibility: ARM 14A requires Xenite support (occupancy class 0)
    // and ARM 14C requires Tritium support (occupancy class 1). Keep both domains available so
    // this legacy sweep test still isolates KI[1] versus KI[2] weighting rather than site absence.
    thandor::Brush support_brush;
    support_brush.radius_cells = 5.0F;
    support_brush.shape = thandor::BrushShape::Square;
    support_brush.falloff = thandor::BrushFalloff::Constant;
    field.paint_placement_occupancy_class(12.0F, 20.0F, support_brush, 0U, true);
    field.paint_placement_occupancy_class(12.0F, 40.0F, support_brush, 0U, true);
    field.paint_placement_occupancy_class(34.0F, 20.0F, support_brush, 1U, true);
    field.paint_placement_occupancy_class(34.0F, 40.0F, support_brush, 1U, true);
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> base{};
    base[2] = 100U; // make ARM 14C the low-end control winner when KI[1] == 0

    thandor::SimulationMotionProfile motion;
    motion.speed_q12_per_tick = 0x0200;
    motion.yaw_max_velocity_turn16 = 0x1000U;
    motion.yaw_acceleration_turn16 = 0x0400U;
    motion.collision_radius_q12 = 0x0400;

    std::array<thandor::SimulationLabSeedEntity, 2> seeds{};
    seeds[0].source_placement_index = 0U;
    seeds[0].army_asset_id = 0x100U;
    seeds[0].faction_index = 1U;
    seeds[0].motion = motion;
    const auto& mover0_cell = field.at(20U, 20U);
    seeds[0].actor.position = {thandor::q12_to_float(mover0_cell.world_x_q12),
                               thandor::q12_to_float(mover0_cell.world_y_q12), 0.0F};
    seeds[1].source_placement_index = 1U;
    seeds[1].army_asset_id = 0x101U;
    seeds[1].faction_index = 2U;
    seeds[1].motion = motion;
    const auto& mover1_cell = field.at(20U, 40U);
    seeds[1].actor.position = {thandor::q12_to_float(mover1_cell.world_x_q12),
                               thandor::q12_to_float(mover1_cell.world_y_q12), 0.0F};

    std::array<thandor::SimulationLabSeedEntity, 2> build_area_seeds{};
    for (std::size_t i = 0; i < build_area_seeds.size(); ++i) {
        const std::uint32_t faction = static_cast<std::uint32_t>(i + 1U);
        const std::uint32_t row = i == 0U ? 20U : 40U;
        const auto& cell = field.at(23U, row);
        auto& anchor = build_area_seeds[i];
        anchor.source_placement_index = 100U + i;
        anchor.army_asset_id = 301U + static_cast<std::uint32_t>(i);
        anchor.faction_index = faction;
        anchor.building = true;
        anchor.runtime_class_specific_19c_raw = 0x10000;
        anchor.actor.position = {thandor::q12_to_float(cell.world_x_q12),
                                 thandor::q12_to_float(cell.world_y_q12), 0.0F};
    }
    std::array<thandor::SimulationLabSeedEntity, 4> all_seeds{
        seeds[0], seeds[1], build_area_seeds[0], build_area_seeds[1]};

    thandor::SimulationLabConfig config;
    config.run_count = 10U;
    config.duration_ticks = 256U;
    config.base_seed = 0x12345678U;
    config.vary_seed_per_run = false;
    config.parameter_index = 1U;
    config.minimum_value = 0;
    config.maximum_value = 1000;
    config.sweep_mode = thandor::SimulationLabSweepMode::Linear;

    const auto sweep = thandor::simulation_lab_sweep_values(config);
    require(sweep.size() == 10U && sweep.front() == 0 && sweep.back() == 1000,
            "linear sweep did not preserve requested min/max endpoints");

    thandor::SimulationLabExperiment experiment;
    experiment.reset(config, base, all_seeds);
    require(experiment.runs().size() == 10U, "simulation lab did not create ten independent runs");
    for (const auto& run : experiment.runs())
        require(run.seed == config.base_seed, "controlled sweep unexpectedly varied RNG seeds");

    // Economy callbacks select faction 1 at 74/78 and faction 2 at 82/86.
    // The first callback creates the pending request; the second can commit it.
    experiment.step(field, 74U);
    const auto& low = experiment.runs().front();
    const auto& high = experiment.runs().back();
    require(low.metrics.planning_cycles == 1U && high.metrics.planning_cycles == 1U,
            "simulation lab did not enter faction 1's retail economy slot");
    require(low.metrics.pending_requests_created == 1U && high.metrics.pending_requests_created == 1U &&
            low.metrics.planned_builds == 0U && high.metrics.planned_builds == 0U,
            "first faction callback did not leave the generated construction request pending");
    experiment.step(field, 12U); // through tick 86: both factions have acted
    require(low.metrics.pending_requests_created == 2U && high.metrics.pending_requests_created == 2U &&
            low.metrics.planned_builds == 2U && high.metrics.planned_builds == 2U,
            "sequential faction callbacks did not create and commit one request per faction");
    require(low.metrics.candidate_builds[1] == 2U,
            "low KI[1] control did not prefer the stronger KI[2]/ARM14C candidate");
    require(high.metrics.candidate_builds[0] == 2U,
            "high KI[1] sweep did not prefer ARM14A");
    require(low.metrics.movement_distance_world > 0.0 && high.metrics.movement_distance_world > 0.0,
            "concurrent runs did not move opposing units toward a target");

    // Isolation gate: mutating one run's state must not bleed into any other run.
    const float untouched = experiment.runs()[1].entities[0].actor.position.x;
    experiment.runs()[0].entities[0].actor.position.x += 123.0F;
    require(experiment.runs()[1].entities[0].actor.position.x == untouched,
            "simulation lab run state is aliased across concurrent worlds");

    experiment.step(field, 170U); // finish at tick 256
    require(experiment.finished(), "fixed-duration concurrent experiment did not auto-stop");
    require(experiment.runs().front().metrics.completed_builds >= 2U,
            "first pending construction generation did not complete on the expected shared timeline");

    require(thandor::simulation_lab_parameter_effect_modeled(1U) &&
            !thandor::simulation_lab_parameter_effect_modeled(120U),
            "AI parameter fidelity labels no longer distinguish modeled from verified-only consumers");

    const auto semantics = thandor::known_ai_knowledge_semantics();
    const auto consumers = thandor::known_ai_knowledge_consumers();
    require(semantics.size() == 62U, "named KI field census (45 GZF plus 17 game-source) drifted");
    require(consumers.size() == 57U, "V530 KI consumer-reference count drifted from the GZF census");
    std::uint32_t exact_consumer_backed_count = 0U;
    for (const auto& semantic : semantics)
        if (!thandor::ai_knowledge_consumers(semantic.index).empty())
            ++exact_consumer_backed_count;
    require(exact_consumer_backed_count == 45U,
            "the previously GZF-audited KI direct consumer census drifted");
    const auto placement_consumers = thandor::ai_knowledge_consumers(0x51U);
    require(placement_consumers.size() == 6U &&
            placement_consumers.front().function_address == 0x0051D450U &&
            placement_consumers[1].function_address == 0x005278D0U,
            "KI[0x51] placement-clearance consumer census drifted");
    const auto class12_consumers = thandor::ai_knowledge_consumers(0x52U);
    require(class12_consumers.size() == 1U &&
            class12_consumers.front().function_address == 0x0053B620U,
            "special-class-12 KI consumer address drifted");

    // V530 0x00529E60 / 0x0052A040 / runtime-update[0x17] collision-state gate.
    // First contact arms class-0x17 state bit 0 and timer 0x20. The next runtime update arms
    // bit 1 (mask 2); a subsequent qualifying same-owner contact links the pair bidirectionally.
    std::array<thandor::SimulationLabSeedEntity, 3> collision_seeds{};
    collision_seeds[0].source_placement_index = 10U;
    collision_seeds[0].army_asset_id = 0x200U;
    collision_seeds[0].faction_index = 1U;
    collision_seeds[0].runtime_model_class_id = 3U;
    collision_seeds[0].motion = motion;
    collision_seeds[0].motion.collision_radius_q12 = 0x0800;
    collision_seeds[0].actor.position = {8.0F, 8.0F, 0.0F};

    collision_seeds[1].source_placement_index = 11U;
    collision_seeds[1].army_asset_id = 0x201U;
    collision_seeds[1].faction_index = 1U;
    collision_seeds[1].runtime_model_class_id = 0x17U;
    collision_seeds[1].motion.collision_radius_q12 = 0x0800; // stationary collision partner
    collision_seeds[1].actor.position = {8.25F, 8.0F, 0.0F};

    collision_seeds[2].source_placement_index = 12U;
    collision_seeds[2].army_asset_id = 0x202U;
    collision_seeds[2].faction_index = 2U;
    collision_seeds[2].runtime_model_class_id = 0U;
    collision_seeds[2].motion.collision_radius_q12 = 0x0800;
    collision_seeds[2].actor.position = {30.0F, 8.0F, 0.0F};

    thandor::SimulationLabConfig collision_config;
    collision_config.run_count = 1U;
    collision_config.duration_ticks = 8U;
    collision_config.parameter_index = 1U;
    thandor::SimulationLabExperiment collision_experiment;
    collision_experiment.reset(collision_config, base, collision_seeds);
    collision_experiment.step(field, 1U);
    auto& collision_run = collision_experiment.runs().front();
    require((collision_run.entities[1].collision_state_bits & 1U) != 0U &&
            collision_run.entities[1].collision_owner_timer == 0x20U &&
            !collision_run.entities[1].linked_entity_index,
            "first class-0x17 contact did not arm the retail state/timer contract");
    collision_experiment.step(field, 1U);
    require(collision_run.entities[0].linked_entity_index == 1U &&
            collision_run.entities[1].linked_entity_index == 0U &&
            collision_run.metrics.class23_links == 1U,
            "armed class-0x17 contact did not establish the retail bidirectional link");

    // Fresh V530 machine code closes the class-0 collision target as ModelRuntimeSlot, not the
    // historically typed ArmyRuntimeSlot. ModelRuntime +0x3C is initialized from MDL +0x60 at
    // 0x005291EE; 0x0052A040 then applies fixed 0x100000 impact damage through 0x0052A2E0.
    std::array<thandor::SimulationLabSeedEntity, 2> impact_seeds{};
    impact_seeds[0] = collision_seeds[0];
    impact_seeds[0].source_placement_index = 20U;
    impact_seeds[1] = collision_seeds[2];
    impact_seeds[1].source_placement_index = 21U;
    impact_seeds[1].actor.position = {8.25F, 8.0F, 0.0F};
    impact_seeds[1].root_model_maximum_integrity = 0x180000;
    thandor::SimulationLabExperiment impact_experiment;
    impact_experiment.reset(collision_config, base, impact_seeds);
    impact_experiment.step(field, 1U);
    auto& impact_run = impact_experiment.runs().front();
    require(impact_run.metrics.class0_impact_events >= 1U &&
            impact_run.metrics.class0_impact_damage_applied == 0x100000U &&
            impact_run.entities[1].root_model_integrity == 0x080000 &&
            !impact_run.entities[1].root_model_terminal,
            "class-0 overlap did not apply the machine-proven fixed 0x100000 component damage");

    // A second impact exhausts the remaining 0x080000 and records the 0x080000 overshoot that
    // retail would propagate to the parent ModelRuntimeSlot when one exists. The compact lab seed
    // represents only the root component, so it records rather than invents a parent hierarchy.
    impact_experiment.step(field, 1U);
    require(impact_run.entities[1].root_model_integrity == 0 &&
            impact_run.entities[1].root_model_terminal &&
            impact_run.metrics.class0_terminal_impacts == 1U &&
            impact_run.metrics.class0_impact_overshoot == 0x080000U,
            "terminal class-0 impact did not preserve retail overshoot semantics");

    std::cout << "V534_SIMULATION_LAB PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V534_SIMULATION_LAB FAIL: " << error.what() << '\n';
    return 1;
}
