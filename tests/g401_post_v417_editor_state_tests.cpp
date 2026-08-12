#include "thandor/ai_knowledge.hpp"
#include "thandor/binary.hpp"
#include "thandor/effect.hpp"
#include "thandor/model.hpp"
#include "thandor/simulation.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

void write_ascii_utf16(std::span<std::byte> bytes, std::size_t offset, const std::string& text) {
    for (std::size_t index = 0; index < text.size(); ++index) {
        thandor::write_u16(bytes, offset + index * 2U, static_cast<std::uint16_t>(text[index]));
    }
    thandor::write_u16(bytes, offset + text.size() * 2U, 0U);
}

std::vector<std::byte> make_minimal_sprite(std::int32_t lighting_scale_q12) {
    constexpr std::size_t kSize = 0x220U;
    std::vector<std::byte> bytes(kSize);
    thandor::write_u32(bytes, 0x00U, thandor::fourcc("spr"));
    thandor::write_u32(bytes, 0x04U, static_cast<std::uint32_t>(bytes.size()));
    thandor::write_u32(bytes, 0x0CU, 0x00020007U);
    thandor::write_u32(bytes, 0xB0U, 1U); // one packed group
    thandor::write_u32(bytes, 0xB8U, 77U);
    thandor::write_i32(bytes, 0xD8U, thandor::float_to_q12(2.0F));
    thandor::write_i32(bytes, 0xDCU, thandor::float_to_q12(0.5F));
    thandor::write_i32(bytes, 0xE0U, lighting_scale_q12);
    thandor::write_u32(bytes, 0x200U, 0x20U); // group byte size
    thandor::write_u32(bytes, 0x204U, 0U);    // zero geometry records
    return bytes;
}

std::vector<std::byte> make_minimal_shot() {
    constexpr std::size_t kHeader = 0x200U;
    constexpr std::size_t kRecord = 0x2E0U;
    constexpr std::size_t kDefinition = kHeader;
    std::vector<std::byte> bytes(kHeader + kRecord);
    thandor::write_u32(bytes, 0x00U, thandor::fourcc("sht"));
    thandor::write_u32(bytes, 0x04U, static_cast<std::uint32_t>(bytes.size()));
    thandor::write_u32(bytes, 0x0CU, 0x00060006U);
    thandor::write_u32(bytes, 0xB0U, 1U);

    thandor::write_i32(bytes, kDefinition + 0x00U,
                       static_cast<std::int32_t>(thandor::ShotTrajectoryMode::FixedRange));
    thandor::write_u32(bytes, kDefinition + 0x08U, 123U);
    thandor::write_i32(bytes, kDefinition + 0x0CU, thandor::float_to_q12(1.5F));
    thandor::write_u32(bytes, kDefinition + 0xD0U, 40U);
    thandor::write_i32(bytes, kDefinition + 0xD8U, 0x0100);
    thandor::write_u32(bytes, kDefinition + 0x15CU, 0x20U);
    thandor::write_u32(bytes, kDefinition + 0x1DCU, 4U);
    thandor::write_i32(bytes, kDefinition + 0x25CU, static_cast<std::int32_t>(0xFF102030U));
    thandor::write_i32(bytes, kDefinition + 0x260U, 6);
    thandor::write_i32(bytes, kDefinition + 0x264U, 9);
    thandor::write_u32(bytes, kDefinition + 0x26CU, 12U);
    thandor::write_u32(bytes, kDefinition + 0x270U, 7U);
    thandor::write_u32(bytes, kDefinition + 0x274U, 11U);
    thandor::write_i32(bytes, kDefinition + 0x27CU, thandor::float_to_q12(18.0F));
    thandor::write_i32(bytes, kDefinition + 0x280U, 5);
    thandor::write_u32(bytes, kDefinition + 0x284U, 0xFF405060U);
    thandor::write_u32(bytes, kDefinition + 0x288U, 32768U);
    thandor::write_u32(bytes, kDefinition + 0x28CU,
                       static_cast<std::uint32_t>(thandor::float_to_q12(24.0F)));

    for (std::uint32_t index = 0; index < 31U; ++index) {
        thandor::write_u32(bytes, kDefinition + 0x14U + index * 4U, 1000U + index);
        thandor::write_u32(bytes, kDefinition + 0xE0U + index * 4U, 2000U + index);
        thandor::write_i32(bytes, kDefinition + 0x1E0U + index * 4U, -1);
    }
    thandor::write_i32(bytes, kDefinition + 0x1E0U, 2);       // valid with >=3 materials
    thandor::write_i32(bytes, kDefinition + 0x1E4U, 25);      // retail-valid only when all 26 loaded
    thandor::write_i32(bytes, kDefinition + 0x1E8U, 26);      // always invalid (retail bound)
    write_ascii_utf16(bytes, kDefinition + 0x29CU, "shotfx.spr");
    return bytes;
}

} // namespace

int main() try {
    // V417 keeps KI.DAT as an exact 128-dword image while closing all 43 currently proven
    // direct-index semantics.
    std::vector<std::byte> ki(thandor::AiKnowledgeData::kSerializedBytes);
    for (std::uint32_t index = 0; index < thandor::AiKnowledgeData::kParameterCount; ++index) {
        thandor::write_u32(ki, index * 4U, index * 0x1000U);
    }
    thandor::write_i32(ki, thandor::AiKnowledgeData::kPlacementClearancePaddingIndex * 4U,
                       thandor::float_to_q12(3.25F));
    const auto knowledge = thandor::AiKnowledgeData::parse(ki);
    require(knowledge.parameters().size() == 128U, "KI.DAT packed parameter count changed");
    require(knowledge.placement_clearance_padding_q12() == thandor::float_to_q12(3.25F),
            "KI[0x51] clearance padding did not preserve Q12 value");
    require(std::abs(knowledge.placement_clearance_padding_world() - 3.25F) < 1.0e-6F,
            "KI[0x51] world decode is wrong");
    require(std::abs(thandor::q15_gain_to_float(0x8000U) - 1.0F) < 1.0e-6F,
            "V417 Q15 unity gain conversion is wrong");

    const auto semantics = thandor::known_ai_knowledge_semantics();
    require(semantics.size() == 43U, "V417 KI semantic dictionary is not 43 entries");
    std::set<std::uint8_t> semantic_indices;
    for (const auto& semantic : semantics) {
        require(semantic_indices.insert(semantic.index).second, "duplicate KI semantic index");
    }
    const auto* clearance = thandor::find_ai_knowledge_semantic(0x51U);
    const auto* workspace_coefficient = thandor::find_ai_knowledge_semantic(0x55U);
    require(clearance != nullptr &&
                clearance->identifier == "AI_KNOWLEDGE_PLACEMENT_CLEARANCE_PADDING_Q12" &&
                clearance->domain == thandor::AiKnowledgeValueDomain::Q12Distance,
            "V417 KI[0x51] semantic/domain missing");
    require(workspace_coefficient != nullptr &&
                workspace_coefficient->domain == thandor::AiKnowledgeValueDomain::Coefficient,
            "V417 KI[0x55] coefficient semantic missing");
    require(thandor::find_ai_knowledge_semantic(0x7AU) != nullptr,
            "V417 strategic-class 143 KI semantic missing");
    require(thandor::find_ai_knowledge_semantic(0x7FU) == nullptr,
            "unproven KI index was assigned a semantic");

    bool rejected_wrong_ki_size = false;
    try {
        std::vector<std::byte> wrong_size(0x1FCU);
        (void)thandor::AiKnowledgeData::parse(wrong_size);
    } catch (const thandor::FormatError&) {
        rejected_wrong_ki_size = true;
    }
    require(rejected_wrong_ki_size, "KI parser accepted a non-0x200 image");

    // V414 proves SPR +0xE0 as a signed Q12 lighting-scale input. Parsing it is safe even while
    // the full legacy scaled-lighting equation remains deliberately unapproximated in Vulkan.
    constexpr std::int32_t kLightingScale = 0x1800;
    const auto sprite = thandor::SpriteAsset::parse(make_minimal_sprite(kLightingScale));
    require(sprite.registry_id() == 77U, "minimal SPR registry id changed");
    require(sprite.group_count() == 1U, "minimal SPR group count changed");
    require(sprite.lighting_scale_q12() == kLightingScale,
            "SPR +0xE0 lighting scale was not parsed exactly");

    // Post-V414/V415 SHT fields are parsed with their corrected tick/Q15/Q12 domains.
    const auto shot_asset = thandor::ShotAsset::parse(make_minimal_shot());
    require(shot_asset.definitions().size() == 1U, "minimal SHT definition count changed");
    const auto& parsed_shot = shot_asset.definitions().front();
    require(parsed_shot.definition_id == 123U, "SHT definition id changed");
    require(parsed_shot.projectile_lifetime_ticks == 40U, "SHT +0xD0 lifetime domain missing");
    require(parsed_shot.shading_release_transition_ticks == 9,
            "SHT +0x264 shading release duration missing");
    require(parsed_shot.trajectory_ramp_duration_ticks == 7,
            "SHT +0x270 trajectory ramp duration missing");
    require(parsed_shot.fixed_range_transition_age_threshold_ticks == 11U,
            "SHT +0x274 fixed-range transition age missing");
    require(parsed_shot.terrain_grid_mask_index == 5 &&
                parsed_shot.positioned_sound_gain_q15 == 32768 &&
                parsed_shot.positioned_sound_maximum_distance_q12 == thandor::float_to_q12(24.0F),
            "SHT positioned-sound metadata missing");
    require(parsed_shot.terrain_impact_effect_owner_slots[3] == 2003U,
            "SHT +0xE0 owner-slot array missing");
    require(parsed_shot.sprite_path == "shotfx.spr", "SHT sprite path parsing changed");

    const auto terrain_refs_26 = thandor::validate_shot_terrain_material_references(parsed_shot, 26U);
    require(terrain_refs_26.valid_reference_count == 2U &&
                terrain_refs_26.invalid_reference_count == 1U &&
                terrain_refs_26.sentinel_count == 28U,
            "V415 terrain-material validation did not preserve signed sentinel/bound rules");
    const auto terrain_refs_8 = thandor::validate_shot_terrain_material_references(parsed_shot, 8U);
    require(terrain_refs_8.valid_reference_count == 1U &&
                terrain_refs_8.invalid_reference_count == 2U,
            "terrain-material validation did not honor loaded material count");

    // The corrected tick-domain names must preserve the exact retail selection-range equation.
    thandor::ShotDefinitionRecord direct_range;
    direct_range.trajectory_mode = static_cast<std::int32_t>(thandor::ShotTrajectoryMode::DirectLine);
    direct_range.launch_speed_q12 = thandor::float_to_q12(2.0F);
    direct_range.projectile_lifetime_ticks = 20U;
    direct_range.trajectory_ramp_duration_ticks = 0;
    require(thandor::shot_selection_range_q12(direct_range) == 40 * 4096,
            "tick-domain rename changed direct selection-range arithmetic");

    // Mode 2 rises vertically, snaps to/follows live target X/Y at +0x274, then descends after
    // retail negates the pitch. Updating the entity target must affect an already in-flight shot.
    thandor::ShotDefinitionRecord fixed_range;
    fixed_range.trajectory_mode = static_cast<std::int32_t>(thandor::ShotTrajectoryMode::FixedRange);
    fixed_range.definition_id = 77U;
    fixed_range.launch_speed_q12 = thandor::float_to_q12(1.0F);
    fixed_range.projectile_lifetime_ticks = 10U;
    fixed_range.fixed_range_transition_age_threshold_ticks = 2U;
    fixed_range.trajectory_ramp_duration_ticks = 0;

    thandor::SimulationWeaponProfile weapon;
    weapon.shot_definition_id = fixed_range.definition_id;
    weapon.reload_ticks = 1U;
    weapon.maximum_range_q12 = thandor::float_to_q12(100.0F);
    const std::array<thandor::Vec3f, 1> launch{{{0.0F, 0.0F, 0.0F}}};
    thandor::Vec3f target{10.0F, 20.0F, 0.0F};
    std::uint64_t serial = 1U;
    auto volley = thandor::simulation_create_volley(
        4U, 0U, weapon, launch, target, fixed_range, 0U, serial, 0U, std::size_t{9U});
    require(volley.size() == 1U, "mode-2 test volley was not created");
    auto projectile = volley.front();
    const auto before_transition = thandor::simulation_projectile_position(
        projectile, fixed_range, 1U, &target);
    require(std::abs(before_transition.x) < 1.0e-6F &&
                std::abs(before_transition.y) < 1.0e-6F && before_transition.z > 0.9F,
            "mode-2 projectile left launch X/Y before transition");
    const auto at_transition = thandor::simulation_projectile_position(
        projectile, fixed_range, 2U, &target);
    require(std::abs(at_transition.x - 10.0F) < 1.0e-6F &&
                std::abs(at_transition.y - 20.0F) < 1.0e-6F,
            "mode-2 projectile did not snap to live target X/Y at transition");
    const auto old_travel_ticks = projectile.travel_ticks;
    target = {12.0F, 22.0F, 0.0F};
    thandor::simulation_update_projectile_live_target(projectile, fixed_range, target);
    const auto after_target_move = thandor::simulation_projectile_position(
        projectile, fixed_range, 3U, &target);
    require(std::abs(after_target_move.x - 12.0F) < 1.0e-6F &&
                std::abs(after_target_move.y - 22.0F) < 1.0e-6F,
            "in-flight mode-2 projectile did not follow moved target X/Y");
    require(projectile.travel_ticks == old_travel_ticks,
            "pure target X/Y change unexpectedly changed vertical mode-2 contact age");

    std::cout << "G401 post-V417 editor state core checks PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G401_POST_V417_EDITOR_STATE FAIL: " << error.what() << '\n';
    return 1;
}
