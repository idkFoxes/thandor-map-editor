#include "thandor/fog_preview.hpp"
#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_runtime.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace thandor;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::uint8_t faction_byte(std::uint64_t mask, std::uint32_t faction) {
    return static_cast<std::uint8_t>(mask >> (faction * 8U));
}

std::size_t sector_floor_index(const FieldGrid& field, std::int32_t world_x_q12,
                               std::int32_t world_y_q12) {
    const auto diagonal = (static_cast<std::int64_t>(world_y_q12) * -0x20C8CCLL) >> 21U;
    const auto column_q12 =
        ((static_cast<std::int64_t>(world_x_q12) * 0x1C6E9CLL) >> 20U) - diagonal;
    const auto row_q12 = diagonal * 2;
    const auto x = static_cast<std::int32_t>(column_q12 >> 12U);
    const auto y = static_cast<std::int32_t>(row_q12 >> 12U);
    if (x < 0 || y < 0 || static_cast<std::uint32_t>(x) >= field.width() ||
        static_cast<std::uint32_t>(y) >= field.height())
        throw std::runtime_error("sector floor point outside field");
    return static_cast<std::size_t>(y) * field.width() + static_cast<std::uint32_t>(x);
}

SimulationLabExperiment create_lab(const FieldGrid& field, std::uint32_t active_view) {
    auto catalog = std::make_shared<SimulationLabRuntimeCatalog>();
    ModelVisualRecord model{};
    model.runtime_class_id = 18U;
    // Native projected sight clamps an authored zero radius to one step. The
    // independent sector writer likewise clamps MDL +0x27C zero to one step.
    model.simulation.fog_uncover_radius_q12 = 0;
    model.simulation.occupancy_mark_radius_units = 0U;
    model.simulation.fog_reference_height_offset_q12 = 0;
    catalog->definitions.emplace(700U, model);

    SimulationLabScenarioRuntimeSeed scenario{};
    scenario.enabled = true;
    scenario.assignable_faction_count = 2U;
    scenario.active_faction_count = 2U;
    scenario.active_view_faction_index = active_view;
    scenario.ai_planning_enabled = false;
    scenario.runtime_catalog = catalog;

    SimulationLabStaticPlacementOwner owned{};
    owned.model_definition_id = 700U;
    owned.faction_index = 2U;
    const auto& owned_cell = field.at(24U, 24U);
    owned.pose.position = {q12_to_float(owned_cell.world_x_q12),
                           q12_to_float(owned_cell.world_y_q12), 0.0F};
    scenario.static_placement_owners.push_back(owned);

    // Neutral model roots keep the native world list nonempty but their army
    // callback returns before projected or sector occupancy writes.
    SimulationLabStaticPlacementOwner neutral = owned;
    neutral.faction_index = 0U;
    const auto& neutral_cell = field.at(52U, 52U);
    neutral.pose.position = {q12_to_float(neutral_cell.world_x_q12),
                             q12_to_float(neutral_cell.world_y_q12), 0.0F};
    scenario.static_placement_owners.push_back(neutral);

    SimulationLabConfig config{};
    config.run_count = 1U;
    config.duration_ticks = 32U;
    std::array<std::uint32_t, AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabExperiment lab;
    lab.reset(config, parameters, {}, {}, {}, {}, {}, {}, scenario);
    return lab;
}
} // namespace

int main() try {
    auto field = FieldGrid::create(64U, 64U, 0, 0);

    auto allied = create_lab(field, 2U);
    allied.step(field, 7U);
    const auto& allied_run = allied.runs().front();
    require((allied_run.tick & 7U) == 7U, "fixture missed phase 7");
    require(allied_run.retail_scratch != nullptr, "missing phase-7 scratch");
    require(allied_run.retail_scratch->presence_update_tick == allied_run.tick,
            "phase-7 scratch not published");

    const auto& owned_cell = field.at(24U, 24U);
    const auto projected_cell = retail_fog_world_to_cell(
        field, owned_cell.world_x_q12, owned_cell.world_y_q12);
    require(projected_cell.has_value(), "owned static root did not map to field");
    const auto owned_index = static_cast<std::size_t>(projected_cell->y) * field.width() +
                             projected_cell->x;
    const auto owned_byte = faction_byte(
        allied_run.retail_scratch->field_occupancy_masks[owned_index], 2U);
    require((owned_byte & retail_visibility_marker_for_model_class(18U)) ==
                retail_visibility_marker_for_model_class(18U),
            "static MODEL owner did not emit projected visibility marker");
    const auto sector_index = sector_floor_index(
        field, owned_cell.world_x_q12, owned_cell.world_y_q12);
    const auto sector_byte = faction_byte(
        allied_run.retail_scratch->field_occupancy_masks[sector_index], 2U);
    require((sector_byte & 0x02U) != 0U,
            "allied static MODEL owner did not emit occupancy bit 1");
    require((allied_run.retail_scratch->field_presence_by_faction[owned_index] & 0x04U) != 0U,
            "static MODEL owner did not publish Workspace 05 faction presence");

    // update_exploration_and_points still has no dynamic observer here. The
    // retained scratch marker must therefore come from the phase-7 owner
    // callback image, not from replaying current_visibility_occupancy.
    require(allied_run.current_visibility_occupancy.size() == field.cells().size(),
            "expected empty dynamic preview image");
    require(faction_byte(allied_run.current_visibility_occupancy[owned_index], 2U) == 0U,
            "fixture unexpectedly gained a dynamic preview observer");

    const auto& neutral_cell = field.at(52U, 52U);
    const auto neutral_projected = retail_fog_world_to_cell(
        field, neutral_cell.world_x_q12, neutral_cell.world_y_q12);
    require(neutral_projected.has_value(), "neutral root did not map to field");
    const auto neutral_index = static_cast<std::size_t>(neutral_projected->y) * field.width() +
                               neutral_projected->x;
    require(allied_run.retail_scratch->field_occupancy_masks[neutral_index] == 0U,
            "neutral static MODEL owner wrote occupancy");

    // Projected visibility uses the owner's relation row, while only the
    // sector bit-1 writer is gated by its relation to the active view faction.
    auto hostile_view = create_lab(field, 1U);
    hostile_view.step(field, 7U);
    const auto& hostile_run = hostile_view.runs().front();
    const auto hostile_projected_byte = faction_byte(
        hostile_run.retail_scratch->field_occupancy_masks[owned_index], 2U);
    require((hostile_projected_byte & retail_visibility_marker_for_model_class(18U)) ==
                retail_visibility_marker_for_model_class(18U),
            "active-view relation incorrectly suppressed projected visibility");
    const auto hostile_sector_byte = faction_byte(
        hostile_run.retail_scratch->field_occupancy_masks[sector_index], 2U);
    require((hostile_sector_byte & 0x02U) == 0U,
            "non-allied active view incorrectly received sector bit 1");

    std::cout << "V745_PHASE7_MODEL_OWNER_CALLBACKS PASS static root, zero radius, relation gate, no cached replay\n";
} catch (const std::exception& error) {
    std::cerr << "V745_PHASE7_MODEL_OWNER_CALLBACKS FAIL " << error.what() << '\n';
    return 1;
}
