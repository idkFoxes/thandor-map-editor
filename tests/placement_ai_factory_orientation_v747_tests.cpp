#include "thandor/retail_placement.hpp"
#include "thandor/fog_preview.hpp"
#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
using namespace thandor;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool close(Vec3f first, Vec3f second, float epsilon = 1.0F / 4096.0F) {
    return std::abs(first.x - second.x) <= epsilon &&
           std::abs(first.y - second.y) <= epsilon &&
           std::abs(first.z - second.z) <= epsilon;
}

SimulationLabConfig config() {
    SimulationLabConfig value;
    value.run_count = 1U;
    value.duration_ticks = 128U;
    value.parameter_index = 100U;
    value.sweep_mode = SimulationLabSweepMode::Fixed;
    return value;
}

void run_case(std::int32_t height_step_x_q12, std::int32_t height_step_y_q12) {
    auto field = FieldGrid::create(59U, 59U, 0, 0U);
    for (std::uint32_t y = 0; y < field.height(); ++y) {
        for (std::uint32_t x = 0; x < field.width(); ++x) {
            field.at(x, y).terrain_height_q12 =
                static_cast<std::int32_t>(x) * height_step_x_q12 +
                static_cast<std::int32_t>(y) * height_step_y_q12;
        }
    }

    std::array<std::uint32_t, AiKnowledgeData::kParameterCount> parameters{};
    parameters[1] = parameters[2] = parameters[3] = parameters[4] =
        static_cast<std::uint32_t>(-100000);

    SimulationLabSeedEntity builder;
    builder.source_placement_index = 0U;
    builder.army_asset_id = 300U;
    builder.faction_index = 1U;
    builder.runtime_model_class_id = 11U;
    builder.building = true;
    builder.runtime_class_specific_19c_raw = 0x10000;
    const auto& origin = field.at(30U, 30U);
    builder.actor.position = {q12_to_float(origin.world_x_q12),
                              q12_to_float(origin.world_y_q12), 0.0F};

    constexpr std::uint32_t factory_asset_id = 301U;
    SimulationLabProductionAsset factory_product;
    factory_product.army_asset_id = factory_asset_id;
    factory_product.army_runtime_flags14_raw = 1U;
    auto& factory_seed = factory_product.entity_seed;
    factory_seed.army_asset_id = factory_asset_id;
    factory_seed.runtime_model_class_id = 13U;
    factory_seed.building = true;
    factory_seed.runtime_class_specific_19c_raw = 0x20000;
    factory_seed.motion.collision_radius_q12 = 0x400;
    factory_seed.motion.placement_contact_kind = 2;
    factory_seed.motion.placement_height_offset_q12 = 0x80;
    factory_seed.production_root_sprite_height_offset = 0.125F;
    factory_seed.production_spawn_local = Vec3f{0.25F, -0.5F, 0.125F};
    factory_seed.production_exit_local = Vec3f{0.75F, -2.5F, 0.125F};
    factory_seed.actor.body_yaw_turn16 = 0x1234U;

    constexpr std::uint32_t child_asset_id = 111U;
    SimulationLabProductionAsset child_product;
    child_product.army_asset_id = child_asset_id;
    child_product.entity_seed.army_asset_id = child_asset_id;
    child_product.entity_seed.runtime_model_class_id = 1U;
    child_product.entity_seed.motion.speed_q12_per_tick = float_to_q12(0.25F);
    child_product.entity_seed.motion.yaw_max_velocity_turn16 = 0x1000U;
    child_product.entity_seed.motion.yaw_acceleration_turn16 = 0x1000U;

    const std::array seeds{builder};
    const std::array products{factory_product, child_product};
    SimulationLabExperiment experiment;
    experiment.reset(config(), parameters, seeds, {}, products);
    auto& run = experiment.runs().front();
    run.pending_asset_requests.push_back({factory_asset_id, 1U, 0U, 0U});

    experiment.step(field, 74U);
    const auto factory_it = std::find_if(run.entities.begin(), run.entities.end(), [](const auto& entity) {
        return entity.army_asset_id == factory_asset_id;
    });
    require(factory_it != run.entities.end(), "AI planner did not place the class-13 factory");
    const std::size_t factory_index = static_cast<std::size_t>(factory_it - run.entities.begin());
    auto& factory = run.entities[factory_index];

    const auto cell = retail_nearest_world_to_cell(
        field, float_to_q12(factory.actor.position.x), float_to_q12(factory.actor.position.y));
    require(cell.has_value(), "AI factory placement did not map back to an FLD cell");
    const std::uint32_t expected_heading =
        retail_placement_normal_angles(field, cell->x, cell->y, false) & 0xFFFFU;
    require(expected_heading != 0x1234U,
            "orientation fixture accidentally matched the catalog/template yaw");
    require(factory.actor.body_yaw_turn16 == expected_heading,
            "AI factory did not retain the exact triangle0NormalAngles.low16 creation heading");

    // The class-13 runtime resolves its lookup points from the placed root transform.
    // Service one callback so the worker publishes those world points.
    experiment.step(field, 1U);
    require(factory.production_spawn_world && factory.production_exit_world,
            "AI factory did not resolve its spawn/exit lookup points");
    const Vec3f expected_spawn = simulation_lab_factory_world_point(
        field, factory.actor.position, expected_heading,
        factory.motion.placement_contact_kind,
        factory.production_root_sprite_height_offset,
        *factory.production_spawn_local,
        factory.motion.placement_height_offset_q12);
    const Vec3f expected_exit = simulation_lab_factory_world_point(
        field, factory.actor.position, expected_heading,
        factory.motion.placement_contact_kind,
        factory.production_root_sprite_height_offset,
        *factory.production_exit_local,
        factory.motion.placement_height_offset_q12);
    require(close(*factory.production_spawn_world, expected_spawn),
            "AI factory spawn point ignored its placed orientation/contact transform");
    require(close(*factory.production_exit_world, expected_exit),
            "AI factory exit point ignored its placed orientation/contact transform");

    // A produced unit must inherit the direction from the transformed spawn to
    // transformed exit, not from the factory template yaw or unrotated local points.
    factory.production_building = true;
    factory.production_asset_id = child_asset_id;
    factory.production_elapsed_ticks = 0U;
    factory.production_duration_ticks = 1U;
    experiment.step(field, 1U);
    require(run.entities.size() == 3U, "AI factory did not create the orientation probe unit");
    const auto& child = run.entities.back();
    // Native CreateProduct derives its heading from integer world coordinates.
    const std::uint32_t expected_child_heading = fixed_atan2_angle16_compat(
        float_to_q12(expected_exit.y) - float_to_q12(expected_spawn.y),
        float_to_q12(expected_exit.x) - float_to_q12(expected_spawn.x)) & 0xFFFFU;
    require(close(child.actor.position, expected_spawn),
            "produced unit did not spawn at the oriented factory lookup point");
    require(child.actor.body_yaw_turn16 == expected_child_heading,
            "produced unit did not face along the oriented factory exit vector");
}
}

int main() try {
    // Two non-axis-aligned planes catch X/Y swaps, 90-degree rotations and sign reversal.
    run_case(8, 5);
    run_case(-6, 9);
    std::cout << "V747_AI_FACTORY_ORIENTATION PASS cases=2\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V747_AI_FACTORY_ORIENTATION FAIL: " << error.what() << '\n';
    return 1;
}
