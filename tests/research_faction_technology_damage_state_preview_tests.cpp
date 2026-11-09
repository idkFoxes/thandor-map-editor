#include "thandor/binary.hpp"
#include "thandor/catalog.hpp"
#include "thandor/effect.hpp"
#include "thandor/model.hpp"
#include "thandor/technology.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void write_ascii_utf16(std::span<std::byte> data, std::size_t offset, std::string_view value) {
    for (const char character : value) {
        thandor::write_u16(data, offset,
            static_cast<uint16_t>(static_cast<unsigned char>(character)));
        offset += 2U;
    }
    thandor::write_u16(data, offset, 0U);
}

std::vector<std::byte> model_runtime_fixture() {
    constexpr std::size_t record = 0x200U;
    constexpr std::size_t record_size = 0x300U;
    constexpr std::size_t node = 0x520U;
    std::vector<std::byte> data(0x620U, std::byte{});
    thandor::write_u32(data, 0x00U, thandor::fourcc("mdl"));
    thandor::write_u32(data, 0x04U, static_cast<uint32_t>(data.size()));
    thandor::write_u32(data, 0xB0U, 1U);
    thandor::write_u32(data, record + 0x00U, static_cast<uint32_t>(record_size));
    thandor::write_u32(data, record + 0x08U, 500U);
    thandor::write_u32(data, record + 0x64U, static_cast<uint32_t>(node));
    thandor::write_u32(data, record + 0x190U, 88U);
    thandor::write_u32(data, record + 0x1C0U, 17U);
    for (uint32_t slot = 0U; slot < 6U; ++slot)
        thandor::write_u32(data, record + 0x238U + slot * 4U, 600U + slot);
    thandor::write_i32(data, record + 0x250U, 63);
    thandor::write_u32(data, record + 0x254U, 77U);
    thandor::write_u32(data, record + 0x258U, 19U);
    thandor::write_u32(data, record + 0x25CU, 11U);
    thandor::write_u32(data, node + 0x00U, 0x80U);
    thandor::write_u32(data, node + 0x04U, 0U);
    thandor::write_u32(data, node + 0x0CU, 0x4000U);
    write_ascii_utf16(data, node + 0x38U, "spr/test/damage.spr");
    return data;
}

thandor::ModelVisualNode runtime_node(uint32_t definition) {
    thandor::ModelVisualNode node;
    node.model_definition_id = definition;
    node.parent_index = std::numeric_limits<uint32_t>::max();
    node.sprite_path = "spr/test/" + std::to_string(definition) + ".spr";
    return node;
}

thandor::ModelTechnologyVariant arm_variant(uint32_t slot, uint32_t definition,
                                             uint32_t technology) {
    thandor::ModelTechnologyVariant variant;
    variant.variant_index = slot;
    variant.model_definition_id = definition;
    variant.technology_unlock_id = technology;
    variant.model_hierarchy.push_back(runtime_node(definition));
    return variant;
}

uint32_t next_primary(uint32_t& state) {
    const uint32_t first = state * 33U + 101U;
    state = first * 33U + 101U;
    return (first << 14U) ^ (state >> 2U);
}
} // namespace

int main() try {
    // MDL serialized fields are parser authority for the runtime research replacement and damage
    // effect paths. These are deliberately beyond the older panel table at +0x1C8..+0x234.
    const auto parsed = thandor::parse_model_visual_records(model_runtime_fixture(), "mdl/test/runtime.mdl");
    require(parsed.size() == 1U, "synthetic MDL did not produce one definition");
    const auto& model = parsed.front();
    require(model.simulation.construction_destruction_effect_id == 88U,
            "MDL +0x190 construction/destruction effect was not parsed");
    require(model.technology_unlock_id == 17U, "MDL +0x1C0 technology requirement was not parsed");
    for (uint32_t slot = 0U; slot < 6U; ++slot)
        require(model.faction_technology_variant_model_ids[slot] == 600U + slot,
                "MDL +0x238..+0x24C replacement IDs were not parsed in serialized order");
    require(model.simulation.damage_effect_health_threshold_percent == 63 &&
                model.simulation.damage_effect_id == 77U &&
                model.simulation.damage_effect_min_cooldown_ticks == 19U &&
                model.simulation.damage_effect_random_cooldown_ticks == 11U,
            "MDL +0x250..+0x25C damage-state fields were not parsed");
    require(!model.hierarchy_nodes.empty() &&
                model.hierarchy_nodes.front().simulation.construction_destruction_effect_id == 88U &&
                model.hierarchy_nodes.front().simulation.damage_effect_id == 77U &&
                model.hierarchy_nodes.front().simulation.damage_effect_health_threshold_percent == 63,
            "late MDL damage-state fields were not propagated to runtime hierarchy nodes");

    thandor::FactionTechnologyMask256 mask{};
    require(thandor::faction_technology_unlocked_count(mask) == 0U,
            "empty faction technology mask is not empty");
    thandor::set_faction_technology_unlocked(mask, 10U, true);
    thandor::set_faction_technology_unlocked(mask, 40U, true);
    require(thandor::faction_technology_unlocked(mask, 10U) &&
                thandor::faction_technology_unlocked(mask, 40U) &&
                thandor::faction_technology_unlocked_count(mask) == 2U,
            "256-bit faction technology helpers are not exact");
    thandor::set_faction_technology_unlocked(mask, 10U, false);
    require(!thandor::faction_technology_unlocked(mask, 10U) &&
                thandor::faction_technology_unlocked_count(mask) == 1U,
            "technology-bit clear did not preserve other mask bits");

    thandor::ArmyAssetPreset preset;
    preset.registry_id = 900U;
    thandor::ArmyModelVariantGroup root_group;
    root_group.parent_group_index = std::numeric_limits<uint32_t>::max();
    root_group.depth = 0U;
    root_group.variants.push_back(arm_variant(0U, 100U, 1U));
    root_group.variants.push_back(arm_variant(1U, 101U, 10U));
    root_group.variants.push_back(arm_variant(2U, 102U, 20U));
    thandor::ModelFactionTechnologyReplacement replacement0;
    replacement0.slot_index = 0U;
    replacement0.model_definition_id = 200U;
    replacement0.technology_unlock_id = 30U;
    replacement0.model_hierarchy.push_back(runtime_node(200U));
    thandor::ModelFactionTechnologyReplacement replacement1;
    replacement1.slot_index = 1U;
    replacement1.model_definition_id = 201U;
    replacement1.technology_unlock_id = 40U;
    replacement1.model_hierarchy.push_back(runtime_node(201U));
    root_group.variants.back().faction_technology_replacements = {replacement0, replacement1};
    preset.model_variant_groups.push_back(root_group);

    thandor::FactionTechnologyMask256 research{};
    uint32_t root_definition = 0U;
    auto hierarchy = thandor::resolve_faction_technology_hierarchy(preset, research, &root_definition);
    require(root_definition == 100U && !hierarchy.empty() && hierarchy.front().model_definition_id == 100U,
            "ARM slot zero was not retained as the baseline definition");

    thandor::set_faction_technology_unlocked(research, 10U, true);
    hierarchy = thandor::resolve_faction_technology_hierarchy(preset, research, &root_definition);
    require(root_definition == 101U && hierarchy.front().model_definition_id == 101U,
            "ARM unlocked slot 1 was not selected");
    thandor::set_faction_technology_unlocked(research, 20U, true);
    hierarchy = thandor::resolve_faction_technology_hierarchy(preset, research, &root_definition);
    require(root_definition == 102U && hierarchy.front().model_definition_id == 102U,
            "ARM did not keep the last/highest serialized unlocked candidate");

    thandor::set_faction_technology_unlocked(research, 30U, true);
    thandor::set_faction_technology_unlocked(research, 40U, true);
    hierarchy = thandor::resolve_faction_technology_hierarchy(preset, research, &root_definition);
    require(root_definition == 200U && hierarchy.front().model_definition_id == 200U,
            "MDL did not stop at the first unlocked +0x238..+0x24C replacement");
    thandor::set_faction_technology_unlocked(research, 30U, false);
    hierarchy = thandor::resolve_faction_technology_hierarchy(preset, research, &root_definition);
    require(root_definition == 201U && hierarchy.front().model_definition_id == 201U,
            "MDL did not continue to the next replacement when the earlier slot was locked");

    // A zero serialized ARM slot zero must not silently promote slot 1 to an unlocked baseline.
    thandor::ArmyAssetPreset zero_slot_preset;
    thandor::ArmyModelVariantGroup zero_slot_group;
    zero_slot_group.parent_group_index = std::numeric_limits<uint32_t>::max();
    zero_slot_group.variants.push_back(arm_variant(1U, 301U, 55U));
    zero_slot_preset.model_variant_groups.push_back(zero_slot_group);
    research.fill(0U);
    hierarchy = thandor::resolve_faction_technology_hierarchy(zero_slot_preset, research, &root_definition);
    require(hierarchy.empty() && root_definition == 0U,
            "missing serialized ARM slot zero was incorrectly replaced by the first nonzero slot");
    thandor::set_faction_technology_unlocked(research, 55U, true);
    hierarchy = thandor::resolve_faction_technology_hierarchy(zero_slot_preset, research, &root_definition);
    require(root_definition == 301U && !hierarchy.empty(),
            "technology-unlocked nonzero ARM slot was not selected after zero slot zero");

    // The runtime does not consume the cooldown RNG when +0x25C is zero; the two immediate RNG
    // outputs must therefore be the orientation values in that case.
    constexpr uint32_t seed = 0x13579BDFU;
    uint32_t expected_state = seed;
    const uint32_t expected_orientation0 = next_primary(expected_state);
    const uint32_t expected_orientation1 = next_primary(expected_state);
    const auto fixed_schedule = thandor::build_damage_effect_schedule({7U, 0U, 2U}, seed, 40U, 8U);
    require(!fixed_schedule.pulses.empty() && fixed_schedule.pulses.front().spawn_tick == 0U &&
                fixed_schedule.pulses.front().world_rotation_angle0 == (expected_orientation0 & 0xFFFFU) &&
                fixed_schedule.pulses.front().world_rotation_angle2 == (expected_orientation0 >> 16U) &&
                fixed_schedule.pulses.front().world_rotation_angle1 ==
                    ((expected_orientation1 & 0x1FFFU) + 0x1FFFU),
            "zero random-cooldown range did not preserve the retail split-angle RNG sequence");
    require(fixed_schedule.pulses.size() >= 3U &&
                fixed_schedule.pulses[0].attachment_index == 0U &&
                fixed_schedule.pulses[1].attachment_index == 1U &&
                fixed_schedule.pulses[2].attachment_index == 0U,
            "damage-state class-3 attachment sequence does not wrap in index order");

    expected_state = seed;
    const uint32_t cooldown_random = next_primary(expected_state);
    const uint32_t randomized_orientation0 = next_primary(expected_state);
    const uint32_t randomized_orientation1 = next_primary(expected_state);
    const auto randomized_schedule = thandor::build_damage_effect_schedule({7U, 5U, 0U}, seed, 40U, 8U);
    require(!randomized_schedule.pulses.empty() &&
                randomized_schedule.pulses.front().world_rotation_angle0 ==
                    (randomized_orientation0 & 0xFFFFU) &&
                randomized_schedule.pulses.front().world_rotation_angle2 ==
                    (randomized_orientation0 >> 16U) &&
                randomized_schedule.pulses.front().world_rotation_angle1 ==
                    ((randomized_orientation1 & 0x1FFFU) + 0x1FFFU) &&
                randomized_schedule.pulses.size() >= 2U &&
                randomized_schedule.pulses[1].spawn_tick == 7U + cooldown_random % 5U,
            "nonzero random-cooldown range does not match the runtime RNG consumption order");

    thandor::EffectDefinitionRecord linear_effect;
    linear_effect.transition_kind = 2U;
    linear_effect.movement_speed_q12 = 0x1000;
    linear_effect.runtime_value_0c = 8U;
    linear_effect.frame_advance_threshold_q4 = 0x10U;
    const auto exact_x = thandor::effect_transition2_displacement_at_age_angles(
        linear_effect, 1U, 0U, 0U);
    require(std::abs(exact_x[0] - 1.0F) < 0.0001F && std::abs(exact_x[1]) < 0.0001F &&
                std::abs(exact_x[2]) < 0.0001F,
            "transition-2 fixed-angle vector builder did not reproduce the retail +X cardinal");
    const auto exact_z = thandor::effect_transition2_displacement_at_age_angles(
        linear_effect, 1U, 0x4000U, 0x1234U);
    require(std::abs(exact_z[0]) < 0.0001F && std::abs(exact_z[1]) < 0.0001F &&
                std::abs(exact_z[2] - 1.0F) < 0.0001F,
            "transition-2 fixed-angle vector builder did not reproduce the retail +Z cardinal");
    require(thandor::effect_transition2_world_rotation_angle1_at_pose_age(
                linear_effect, 1U, 0x2000U) == 0x2000U &&
            thandor::effect_transition2_world_rotation_angle1_at_pose_age(
                linear_effect, 2U, 0x2000U) == ((0x2000U * 63U + 0x4000U) >> 6U),
            "transition-2 transform rebuild/eased-angle ordering is not preserved");

    const std::filesystem::path source_root = THANDOR_SOURCE_DIR;
    const auto editor = read_text(source_root / "src/editor/editor_app.cpp");
    const auto document = read_text(source_root / "src/core/map_document.cpp");
    require(editor.find("integrity_percent>=source.health_threshold_percent") != std::string::npos,
            "damage preview does not preserve the runtime's strict integrity < threshold predicate");
    require(editor.find("simulation_integrity_states_.find(source.placement_index)") != std::string::npos,
            "damage preview does not consume live simulated placement integrity when present");
    require(editor.find("lookup.key_class().value()==3U") != std::string::npos,
            "damage preview does not resolve model lookup key class 3 attachment points");
    require(editor.find("origin=runtime_transform.translation") != std::string::npos,
            "damage preview has no model-root fallback when class-3 attachments are absent");
    // The writer emits only V3, but the loader must migrate older saved
    // user maps rather than rejecting their missing editor-only research masks.
    require(document.find("constexpr std::string_view kProjectMagic = \"THANDOR_MAP_EDITOR_PROJECT_V3\"") != std::string::npos &&
                document.find("manifest << kProjectMagic <<") != std::string::npos &&
                document.find("line != \"THANDOR_MAP_EDITOR_PROJECT_V2\"") != std::string::npos &&
                document.find("line != \"THANDOR_MAP_EDITOR_PROJECT_V1\"") != std::string::npos,
            "V3 write format or backward-compatible V1/V2 import was lost");
    require(document.find("faction_technology_mask.") != std::string::npos &&
                document.find("placement_integrity_percent.") != std::string::npos,
            "project V3 does not persist exact faction research and damage-preview state");

    std::cout << "G400_FACTION_TECHNOLOGY_DAMAGE_STATE_PREVIEW PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G400_FACTION_TECHNOLOGY_DAMAGE_STATE_PREVIEW FAIL: " << error.what() << '\n';
    return 1;
}
