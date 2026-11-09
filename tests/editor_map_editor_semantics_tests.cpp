#include "thandor/binary.hpp"
#include "thandor/field.hpp"
#include "thandor/level.hpp"
#include "thandor/simulation.hpp"
#include "thandor/simulation_lab.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void put_u32(std::vector<std::byte>& data, std::size_t offset, std::uint32_t value) {
    if (offset + 4U > data.size()) throw std::runtime_error("synthetic LEV write overflow");
    data[offset + 0U] = static_cast<std::byte>(value & 0xFFU);
    data[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    data[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xFFU);
    data[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xFFU);
}

std::vector<std::byte> synthetic_level() {
    constexpr std::size_t size = 0x900U;
    std::vector<std::byte> data(size, std::byte{0});
    put_u32(data, 0x00U, thandor::fourcc("lev"));
    put_u32(data, 0xD8U, 0U);
    put_u32(data, 0xDCU, static_cast<std::uint32_t>(size));
    constexpr std::array<std::size_t, 10> path_fields{
        0xB0U, 0xB4U, 0xB8U, 0xBCU, 0xC0U, 0xC4U, 0xC8U, 0xCCU, 0xD0U, 0xD4U};
    for (std::size_t index = 0; index < path_fields.size(); ++index)
        put_u32(data, path_fields[index], 0x820U + static_cast<std::uint32_t>(index * 8U));
    return data;
}

void test_level_runtime_tail_roundtrip() {
    auto bytes = synthetic_level();
    constexpr std::uint32_t primary_pair = 0xE0002000U;
    constexpr std::uint32_t dimensions = 0x005A004BU;
    constexpr std::uint32_t alternate_pair = 0xA0006000U;
    constexpr std::uint32_t alternate_dimensions = 0x00640050U;
    put_u32(bytes, 0x2E0U, primary_pair);
    put_u32(bytes, 0x2E4U, 0xFF102030U);
    put_u32(bytes, 0x2E8U, 0xFF203040U);
    put_u32(bytes, 0x2ECU, 96U);
    put_u32(bytes, 0x2F0U, dimensions);
    put_u32(bytes, 0x2F4U, 0xFF304050U);
    put_u32(bytes, 0x2F8U, 0xFF405060U);
    put_u32(bytes, 0x300U, 0xFF506070U);
    put_u32(bytes, 0x304U, 0xFF607080U);
    put_u32(bytes, 0x308U, 0xFF708090U);
    put_u32(bytes, 0x30CU, 0xFF8090A0U);
    put_u32(bytes, 0x318U, 0x11223344U);
    put_u32(bytes, 0x31CU, 0x55667788U);
    put_u32(bytes, 0x320U, 0x99AABBCCU);
    put_u32(bytes, 0x328U, alternate_pair);
    put_u32(bytes, 0x32CU, alternate_dimensions);
    put_u32(bytes, 0x330U, 0xFFA0B0C0U);
    put_u32(bytes, 0x334U, 0xFFB0C0D0U);
    put_u32(bytes, 0x338U, 0xFFC0D0E0U);
    put_u32(bytes, 0x33CU, 0xFFD0E0F0U);
    put_u32(bytes, 0x340U, 0xFF010203U);
    put_u32(bytes, 0x344U, 0xFF111213U);
    put_u32(bytes, 0x348U, 0xFF212223U);
    put_u32(bytes, 0x34CU, 0xFF313233U);

    auto level = thandor::LevelAsset::parse(bytes);
    const auto& lighting = level.lighting();
    require(lighting.azimuth == 0x2000U && lighting.elevation == 0xE000U,
            "V537 LEV +0x2E0 direction pair decode failed");
    require(lighting.field_region_width == 0x004BU && lighting.field_region_height == 0x005AU,
            "V537 LEV +0x2F0 region dimensions decode failed");
    require(lighting.cycle_duration_ticks == 96U, "V537 lighting cycle duration decode failed");
    require(lighting.alternate_azimuth == 0x6000U && lighting.alternate_elevation == 0xA000U,
            "V537 alternate direction pair decode failed");
    require(lighting.alternate_field_region_width == 0x0050U &&
            lighting.alternate_field_region_height == 0x0064U,
            "V537 alternate field-region dimensions decode failed");
    require(level.relation_ui_flags() == 0x11223344U &&
            level.relation_state8_faction_group_masks() == 0x55667788U &&
            level.relation_state4_faction_group_masks() == 0x99AABBCCU,
            "V537 initial relation fields decode failed");

    level.set_initial_relation_setup(0xCAFEBABEU, 0x01020304U, 0xA1B2C3D4U);
    const auto serialized = level.serialize();
    require(thandor::read_u32(serialized, 0x2E0U) == primary_pair,
            "V537 primary direction pair did not round-trip");
    require(thandor::read_u32(serialized, 0x2F0U) == dimensions,
            "V537 region dimensions did not round-trip independently from direction");
    require(thandor::read_u32(serialized, 0x328U) == alternate_pair &&
            thandor::read_u32(serialized, 0x32CU) == alternate_dimensions,
            "V537 alternate direction/dimension pair did not round-trip");
    require(thandor::read_u32(serialized, 0x318U) == 0xCAFEBABEU &&
            thandor::read_u32(serialized, 0x31CU) == 0x01020304U &&
            thandor::read_u32(serialized, 0x320U) == 0xA1B2C3D4U,
            "V537 initial relation edits did not serialize");

    const auto start = lighting.sampled_at_tick(0U);
    const auto half = lighting.sampled_at_tick(48U);
    require(start.base_color == lighting.base_color && start.azimuth == lighting.azimuth,
            "lighting-cycle tick zero must use the primary endpoint");
    require(half.base_color == lighting.alternate_base_color &&
            half.ramp_124 == lighting.alternate_ramp_124 &&
            half.azimuth == lighting.alternate_azimuth &&
            half.elevation == lighting.alternate_elevation &&
            half.field_region_width == lighting.alternate_field_region_width &&
            half.field_region_height == lighting.alternate_field_region_height,
            "lighting-cycle half period must reach the alternate endpoint");
}

void test_depth_bins_and_heading_gate() {
    require(thandor::simulation_depth_bin_mask(0, 0) == 0x00000001U,
            "zero-radius depth mask at origin should select bin 0");
    require(thandor::simulation_depth_bin_mask(0, 0x4000) == 0x00000002U,
            "depth mask did not advance at one 0x4000-Q12 bin");
    require(thandor::simulation_depth_bin_mask(0x4000, 0) == 0x80000003U,
            "depth mask did not wrap signed bins exactly");
    require(thandor::simulation_depth_bin_overlap({0.0F,0.0F,0.0F},0x1000,
                                                   {3.0F,3.0F,0.0F},0x1000),
            "near depth-bin intervals should overlap");
    require(!thandor::simulation_depth_bin_overlap({0.0F,0.0F,0.0F},0x1000,
                                                    {20.0F,20.0F,0.0F},0x1000),
            "far depth-bin intervals should not overlap");

    thandor::SimulationMotionProfile motion;
    motion.speed_q12_per_tick = thandor::float_to_q12(1.0F);
    motion.heading_interpolation_distance_q12 = thandor::float_to_q12(100.0F);
    motion.near_heading_error_limit_turn16 = 0x5000U;
    motion.far_heading_error_limit_turn16 = 0x0100U;
    motion.yaw_max_velocity_turn16 = 0;
    motion.yaw_acceleration_turn16 = 0;

    thandor::SimulationActorState near_actor;
    near_actor.position = {0.0F,0.0F,0.0F};
    near_actor.body_yaw_turn16 = 0U;
    near_actor.move_target = thandor::Vec3f{0.0F,10.0F,0.0F};
    require(thandor::simulation_tick_actor_motion(near_actor,motion),
            "V537 near heading-error limit should allow advance");
    require(near_actor.position.x > 0.9F,
            "near heading gate did not advance along the retained body heading");

    thandor::SimulationActorState far_actor;
    far_actor.position = {0.0F,0.0F,0.0F};
    far_actor.body_yaw_turn16 = 0U;
    far_actor.move_target = thandor::Vec3f{0.0F,200.0F,0.0F};
    require(!thandor::simulation_tick_actor_motion(far_actor,motion),
            "V537 far heading-error limit should block advance");
    require(std::abs(far_actor.position.x) < 0.0001F && std::abs(far_actor.position.y) < 0.0001F,
            "blocked heading gate moved the actor");
}

void test_resource_capacity_water_and_vertical_deployment() {
    thandor::FieldGrid field = thandor::FieldGrid::create(8U,8U,0,0U);
    for (auto& cell : field.cells()) cell.water_delta_q12 = 1000;
    const auto& center_cell = field.at(3U,3U);
    const thandor::Vec3f center{thandor::q12_to_float(center_cell.world_x_q12),
                                thandor::q12_to_float(center_cell.world_y_q12),0.0F};
    require(field.sample_water_delta_q12(center.x,center.y) == 1000,
            "interpolated water delta did not preserve a constant field");

    thandor::SimulationLabSeedEntity storage_seed;
    storage_seed.army_asset_id = 100U;
    storage_seed.faction_index = 1U;
    storage_seed.runtime_model_class_id = 14U;
    storage_seed.actor.position = center;
    storage_seed.model_components = {
        thandor::SimulationLabModelComponentSeed{.runtime_class_id=14U,
            .maximum_integrity=1000,.resource_storage_selector=0U,
            .resource_storage_capacity_contribution_q4=160U,
            .has_resource_storage_capacity_contribution=true},
        thandor::SimulationLabModelComponentSeed{.runtime_class_id=15U,
            .maximum_integrity=1000,.resource_storage_selector=1U,
            .resource_storage_capacity_contribution_q4=320U,
            .has_resource_storage_capacity_contribution=true,
            .parent_component_index=0U}
    };

    std::array<thandor::SimulationLabFactionEconomySeed,8> economy{};
    economy[1].derive_storage_limits_from_components = true;
    std::array<std::uint32_t,thandor::AiKnowledgeData::kParameterCount> ai{};
    thandor::SimulationLabConfig config;
    config.run_count = 1U;
    config.duration_ticks = 4U;
    config.vary_seed_per_run = false;
    thandor::SimulationLabExperiment storage_experiment;
    storage_experiment.reset(config,ai,std::span<const thandor::SimulationLabSeedEntity>(&storage_seed,1),economy);
    const auto& storage_run = storage_experiment.runs().front();
    require(storage_run.faction_economy[1].xenite_storage_limit_q4 == 160U &&
            storage_run.faction_economy[1].tritium_storage_limit_q4 == 320U,
            "V537 class 14/15 storage contributions were not summed by selector");

    economy[1].xenite_storage_limit_q4 = 999U;
    economy[1].xenite_storage_limit_locked = true;
    storage_experiment.reset(config,ai,std::span<const thandor::SimulationLabSeedEntity>(&storage_seed,1),economy);
    require(storage_experiment.runs().front().faction_economy[1].xenite_storage_limit_q4 == 999U,
            "manual storage override did not retain precedence over V537-derived capacity");

    thandor::SimulationLabSeedEntity water_seed;
    water_seed.army_asset_id = 200U;
    water_seed.faction_index = 1U;
    water_seed.runtime_model_class_id = 2U;
    water_seed.actor.position = center;
    water_seed.root_model_maximum_integrity = 5000;
    water_seed.water_damage_threshold_q12 = 0;
    water_seed.water_damage_multiplier = 128;
    thandor::SimulationLabExperiment water_experiment;
    water_experiment.reset(config,ai,std::span<const thandor::SimulationLabSeedEntity>(&water_seed,1));
    water_experiment.step(field,1U);
    require(water_experiment.runs().front().entities.front().root_model_integrity == 4000,
            "V537 water damage equation did not apply waterDelta*multiplier>>7");

    thandor::SimulationLabSeedEntity deployment_seed;
    deployment_seed.army_asset_id = 300U;
    deployment_seed.faction_index = 1U;
    deployment_seed.runtime_model_class_id = 23U;
    deployment_seed.actor.position = center;
    deployment_seed.root_model_maximum_integrity = 5000;
    deployment_seed.vertical_deployment_step_q12_per_tick = 100;
    deployment_seed.vertical_deployment_travel_limit_q12 = 2000;
    thandor::SimulationLabExperiment deployment_experiment;
    deployment_experiment.reset(config,ai,std::span<const thandor::SimulationLabSeedEntity>(&deployment_seed,1));
    auto& deployment = deployment_experiment.runs().front().entities.front();
    deployment.vertical_deployment_travel_q12 = 1000;
    deployment_experiment.step(field,1U);
    require(deployment.vertical_deployment_travel_q12 == 1100 &&
            deployment.vertical_deployment_child_local_z_q12 == 100,
            "V537 class-23 clear-contact branch did not advance seeded travel/local-Z");
    deployment.collision_state_bits = 1U;
    deployment.collision_owner_timer = 2U;
    deployment.vertical_deployment_travel_q12 = 2100;
    deployment.vertical_deployment_child_local_z_q12 = 2100;
    deployment_experiment.step(field,1U);
    require(deployment.vertical_deployment_travel_q12 == 2000 &&
            deployment.vertical_deployment_child_local_z_q12 == 2000,
            "V537 class-23 collision branch did not retract travel beyond the limit");
}
}

int main() {
    try {
        test_level_runtime_tail_roundtrip();
        test_depth_bins_and_heading_gate();
        test_resource_capacity_water_and_vertical_deployment();
        std::cout << "V570 V537 map-editor semantic upgrade tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V570 V537 semantic test failure: " << error.what() << '\n';
        return 1;
    }
}
