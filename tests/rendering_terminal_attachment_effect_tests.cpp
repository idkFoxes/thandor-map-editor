#include "thandor/binary.hpp"
#include "thandor/model.hpp"
#include "thandor/simulation_lab.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void write_ascii_utf16(std::span<std::byte> data, std::size_t offset, std::string_view value) {
    for (const char character : value) {
        thandor::write_u16(data, offset, static_cast<std::uint16_t>(static_cast<unsigned char>(character)));
        offset += 2U;
    }
    thandor::write_u16(data, offset, 0U);
}

std::vector<std::byte> mdl_fixture() {
    constexpr std::size_t record_offset = 0x200U;
    constexpr std::size_t record_size = 0xC0U;
    constexpr std::size_t root_node_offset = 0x300U;
    std::vector<std::byte> data(0x400U, std::byte{});
    thandor::write_u32(data, 0x00U, thandor::fourcc("mdl"));
    thandor::write_u32(data, 0x04U, static_cast<std::uint32_t>(data.size()));
    thandor::write_u32(data, 0xB0U, 1U);
    thandor::write_u32(data, record_offset + 0x00U, static_cast<std::uint32_t>(record_size));
    thandor::write_u32(data, record_offset + 0x08U, 700U);
    thandor::write_i32(data, record_offset + 0x60U, 0x5000);
    thandor::write_u32(data, record_offset + 0x64U, static_cast<std::uint32_t>(root_node_offset));
    for (std::uint32_t channel = 0; channel < 8U; ++channel) {
        thandor::write_u32(data, record_offset + 0x80U + channel * 8U, 1000U + channel);
        thandor::write_i32(data, record_offset + 0x84U + channel * 8U,
                           static_cast<std::int32_t>(channel) - 2);
    }
    thandor::write_u32(data, root_node_offset + 0x00U, 0x80U);
    thandor::write_u32(data, root_node_offset + 0x04U, 0U);
    thandor::write_u32(data, root_node_offset + 0x0CU, 0x4000U);
    thandor::write_u32(data, root_node_offset + 0x14U, 0U);
    write_ascii_utf16(data, root_node_offset + 0x38U, "spr/test/terminal.spr");
    return data;
}

thandor::SimulationLabConfig config() {
    thandor::SimulationLabConfig value;
    value.run_count = 1U;
    value.duration_ticks = 8U;
    value.parameter_index = 1U;
    return value;
}

thandor::SimulationLabSeedEntity mover() {
    thandor::SimulationLabSeedEntity value;
    value.source_placement_index = 0U;
    value.army_asset_id = 0x100U;
    value.faction_index = 1U;
    value.runtime_model_class_id = 3U;
    value.motion.speed_q12_per_tick = 0x0100;
    value.motion.yaw_max_velocity_turn16 = 0x1000U;
    value.motion.yaw_acceleration_turn16 = 0x0400U;
    value.motion.collision_radius_q12 = 0x0800;
    value.actor.position = {8.0F, 8.0F, 0.0F};
    return value;
}
} // namespace

int main() try {
    const auto parsed = thandor::parse_model_visual_records(mdl_fixture(), "mdl/test/terminal.mdl");
    require(parsed.size() == 1U, "terminal MDL fixture did not parse");
    for (std::uint32_t channel = 0; channel < 8U; ++channel) {
        require(parsed.front().simulation.terminal_attachment_effect_ids[channel] == 1000U + channel,
                "MDL terminal attachment EFF id pair drifted");
        require(parsed.front().simulation.terminal_attachment_delay_ticks[channel] ==
                    static_cast<std::int32_t>(channel) - 2,
                "MDL terminal attachment countdown pair drifted");
        require(parsed.front().hierarchy_nodes.front().simulation.terminal_attachment_effect_ids[channel] ==
                    1000U + channel,
                "terminal attachment pairs were not propagated into hierarchy nodes");
    }

    std::array<thandor::ModelVisualNode, 2> hierarchy{};
    hierarchy[0].model_definition_id = 100U;
    hierarchy[0].runtime_class_id = 4U;
    hierarchy[0].parent_index = std::numeric_limits<std::uint32_t>::max();
    hierarchy[0].simulation.maximum_integrity = 0x70000000; // Keep parent alive after child impact overshoot.
    hierarchy[0].simulation.terminal_attachment_effect_ids[0] = 900U; // Healthy parent has no terminal self-reference.
    hierarchy[0].simulation.terminal_attachment_delay_ticks[0] = 0;
    hierarchy[1].model_definition_id = 200U;
    hierarchy[1].runtime_class_id = 0U;
    hierarchy[1].parent_index = 0U;
    hierarchy[1].simulation.maximum_integrity = 0x10000;
    hierarchy[1].simulation.terminal_attachment_effect_ids[0] = 501U;
    hierarchy[1].simulation.terminal_attachment_delay_ticks[0] = 0;
    hierarchy[1].simulation.terminal_attachment_effect_ids[1] = 502U;
    hierarchy[1].simulation.terminal_attachment_delay_ticks[1] = 2;
    hierarchy[1].simulation.terminal_attachment_effect_ids[2] = 503U;
    hierarchy[1].simulation.terminal_attachment_delay_ticks[2] = -1;
    const auto components = thandor::simulation_lab_model_components_from_hierarchy(hierarchy);
    require(components.size() == 2U && components[1].parent_component_index == 0U,
            "terminal test component link was not recovered");
    require(components[1].terminal_attachment_effect_ids[1] == 502U &&
            components[1].terminal_attachment_delay_ticks[1] == 2,
            "terminal attachment profile was not carried into component seed");

    std::array<thandor::SimulationLabSeedEntity, 2> seeds{};
    seeds[0] = mover();
    seeds[1].source_placement_index = 1U;
    seeds[1].army_asset_id = 0x200U;
    seeds[1].faction_index = 2U;
    seeds[1].runtime_model_class_id = 4U;
    seeds[1].motion.collision_radius_q12 = 0x0800;
    seeds[1].actor.position = {8.25F, 8.0F, 0.0F};
    seeds[1].model_components = components;
    seeds[1].collision_model_component_index = 1U;

    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> base{};
    auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    thandor::SimulationLabExperiment experiment;
    experiment.reset(config(), base, seeds);
    experiment.step(field, 1U);
    auto events = thandor::simulation_lab_one_shot_events_after(experiment.runs().front(), 0U);
    require(events.size() == 1U &&
            events.front().kind == thandor::SimulationLabOneShotEventKind::TerminalAttachmentEffect &&
            events.front().effect_id == 501U && events.front().attachment_channel == 0U &&
            events.front().source_model_node_index == 1U,
            "zero-delay linked terminal channel did not fire exactly once on the terminal service tick");

    // Retail compares before DEC: delay=2 becomes 1 on the terminal tick, 0 on the next tick,
    // then fires on the following service and wraps to -1. A serialized -1 never becomes ready.
    experiment.step(field, 1U);
    events = thandor::simulation_lab_one_shot_events_after(experiment.runs().front(), 0U);
    require(events.size() == 1U, "delay=2 terminal channel fired one service too early");
    experiment.step(field, 1U);
    events = thandor::simulation_lab_one_shot_events_after(experiment.runs().front(), 0U);
    require(events.size() == 2U && events.back().effect_id == 502U &&
            events.back().attachment_channel == 1U,
            "delay=2 terminal channel did not fire on compare-before-DEC service three");
    for (const auto& event : events)
        require(event.effect_id != 900U && event.effect_id != 503U,
                "root-unlinked or negative-countdown terminal channel fired incorrectly");

    std::cout << "V567_TERMINAL_ATTACHMENT_EFFECT PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V567_TERMINAL_ATTACHMENT_EFFECT FAIL: " << error.what() << '\n';
    return 1;
}
