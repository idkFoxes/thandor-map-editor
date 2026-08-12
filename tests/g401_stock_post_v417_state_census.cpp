#include "thandor/ai_knowledge.hpp"
#include "thandor/binary.hpp"
#include "thandor/catalog.hpp"
#include "thandor/effect.hpp"
#include "thandor/model.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

void collect_sprite_paths(const std::vector<thandor::ModelVisualNode>& hierarchy,
                          std::set<std::string>& paths) {
    for (const auto& node : hierarchy) {
        if (!node.sprite_path.empty()) paths.insert(node.sprite_path);
    }
}

} // namespace

int main(int argc, char** argv) try {
    if (argc != 2) {
        std::cerr << "usage: g401_stock_post_v417_state_census GAME_ROOT\n";
        return 2;
    }

    const auto catalog = thandor::AssetCatalog::scan_game_root(argv[1]);
    const auto knowledge = thandor::AiKnowledgeData::parse(catalog.load("engine/ki.dat"));
    const auto semantics = thandor::known_ai_knowledge_semantics();

    require(knowledge.parameters().size() == thandor::AiKnowledgeData::kParameterCount,
            "stock KI.DAT no longer has 128 dwords");
    require(semantics.size() == 43U, "V417 semantic dictionary no longer has 43 entries");

    std::set<std::uint8_t> semantic_indices;
    for (const auto& semantic : semantics) {
        require(semantic_indices.insert(semantic.index).second,
                "V417 semantic dictionary contains a duplicate index");
    }

    const auto* clearance = thandor::find_ai_knowledge_semantic(
        thandor::AiKnowledgeData::kPlacementClearancePaddingIndex);
    require(clearance != nullptr &&
                clearance->identifier == "AI_KNOWLEDGE_PLACEMENT_CLEARANCE_PADDING_Q12" &&
                clearance->domain == thandor::AiKnowledgeValueDomain::Q12Distance,
            "KI[0x51] placement-clearance semantic/domain changed");

    std::set<std::uint32_t> shot_ids;
    std::set<std::string> sprite_paths;
    for (const auto& preset : catalog.army_presets()) {
        for (const auto& weapon : preset.simulation_weapon_profiles) {
            if (weapon.shot_definition_id != 0U) shot_ids.insert(weapon.shot_definition_id);
        }
        collect_sprite_paths(preset.resolved_model_hierarchy, sprite_paths);
        for (const auto& group : preset.model_variant_groups) {
            for (const auto& variant : group.variants) {
                collect_sprite_paths(variant.model_hierarchy, sprite_paths);
                for (const auto& replacement : variant.faction_technology_replacements) {
                    collect_sprite_paths(replacement.model_hierarchy, sprite_paths);
                }
            }
        }
    }

    std::size_t terrain_reference_valid = 0U;
    std::size_t terrain_reference_sentinel = 0U;
    std::size_t terrain_reference_invalid = 0U;
    std::size_t fixed_range_shots = 0U;
    std::set<std::uint32_t> fixed_range_transition_ticks;
    for (const std::uint32_t shot_id : shot_ids) {
        const auto* shot = catalog.shot_definition(shot_id);
        require(shot != nullptr, "referenced stock SHT definition is missing");
        const auto validation = thandor::validate_shot_terrain_material_references(*shot, 26U);
        terrain_reference_valid += validation.valid_reference_count;
        terrain_reference_sentinel += validation.sentinel_count;
        terrain_reference_invalid += validation.invalid_reference_count;

        if (shot->trajectory_mode == static_cast<std::int32_t>(thandor::ShotTrajectoryMode::FixedRange)) {
            ++fixed_range_shots;
            fixed_range_transition_ticks.insert(shot->fixed_range_transition_age_threshold_ticks);
        }
    }
    require(terrain_reference_invalid == 0U,
            "stock referenced SHT contains an invalid nonnegative terrain-material index");

    std::size_t parsed_sprites = 0U;
    std::size_t sprites_with_nonzero_lighting_scale = 0U;
    std::set<std::int32_t> lighting_scales_q12;
    for (const auto& sprite_path : sprite_paths) {
        const auto sprite = thandor::SpriteAsset::parse(catalog.load(sprite_path));
        ++parsed_sprites;
        lighting_scales_q12.insert(sprite.lighting_scale_q12());
        if (sprite.lighting_scale_q12() != 0) ++sprites_with_nonzero_lighting_scale;
    }

    std::cout << "{\n"
              << "  \"schema\": \"thandor-g401-stock-post-v417-state-census-v1\",\n"
              << "  \"ki_parameter_count\": 128,\n"
              << "  \"ki_proven_semantics\": " << semantics.size() << ",\n"
              << "  \"ki_0x51_raw_q12\": " << knowledge.placement_clearance_padding_q12() << ",\n"
              << "  \"ki_0x51_world\": " << knowledge.placement_clearance_padding_world() << ",\n"
              << "  \"referenced_shot_definitions\": " << shot_ids.size() << ",\n"
              << "  \"fixed_range_shots\": " << fixed_range_shots << ",\n"
              << "  \"fixed_range_transition_values\": " << fixed_range_transition_ticks.size() << ",\n"
              << "  \"terrain_material_refs_valid\": " << terrain_reference_valid << ",\n"
              << "  \"terrain_material_refs_sentinel\": " << terrain_reference_sentinel << ",\n"
              << "  \"terrain_material_refs_invalid\": " << terrain_reference_invalid << ",\n"
              << "  \"unique_referenced_sprites\": " << sprite_paths.size() << ",\n"
              << "  \"parsed_sprites\": " << parsed_sprites << ",\n"
              << "  \"nonzero_spr_lighting_scale\": " << sprites_with_nonzero_lighting_scale << ",\n"
              << "  \"distinct_spr_lighting_scales_q12\": " << lighting_scales_q12.size() << ",\n"
              << "  \"status\": \"PASS\"\n"
              << "}\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G401 stock post-V417 census failed: " << error.what() << '\n';
    return 1;
}
