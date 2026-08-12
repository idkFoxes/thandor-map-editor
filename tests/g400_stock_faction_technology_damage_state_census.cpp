#include "thandor/catalog.hpp"
#include "thandor/model.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

bool runtime_root(const std::vector<thandor::ModelVisualNode>& hierarchy, std::size_t index) {
    const auto& node = hierarchy[index];
    return node.parent_index == std::numeric_limits<uint32_t>::max() ||
        (node.parent_index < hierarchy.size() &&
         hierarchy[node.parent_index].model_definition_id != node.model_definition_id);
}
}

int main(int argc, char** argv) try {
    if (argc != 2) {
        std::cerr << "usage: g400_stock_faction_technology_damage_state_census GAME_ROOT\n";
        return 2;
    }
    const auto catalog = thandor::AssetCatalog::scan_game_root(argv[1]);
    std::size_t visible_presets = 0U;
    std::size_t variant_groups = 0U;
    std::size_t arm_variants = 0U;
    std::size_t mdl_replacements = 0U;
    std::size_t damage_presets = 0U;
    std::size_t damage_runtime_roots = 0U;
    std::size_t damage_roots_without_class3 = 0U;
    std::size_t damage_roots_with_noncontiguous_class3 = 0U;
    std::set<uint32_t> damage_effects;
    std::set<int32_t> damage_thresholds;

    const auto scan_hierarchy = [&](const std::vector<thandor::ModelVisualNode>& hierarchy,
                                    bool& preset_has_damage) {
        for (std::size_t index = 0; index < hierarchy.size(); ++index) {
            const auto& node = hierarchy[index];
            if (!runtime_root(hierarchy, index) || node.simulation.damage_effect_id == 0U ||
                node.simulation.damage_effect_health_threshold_percent <= 0) continue;
            preset_has_damage = true;
            ++damage_runtime_roots;
            damage_effects.insert(node.simulation.damage_effect_id);
            damage_thresholds.insert(node.simulation.damage_effect_health_threshold_percent);
            const auto sprite = thandor::SpriteAsset::parse(catalog.load(node.sprite_path));
            std::vector<uint32_t> class3;
            for (const auto& lookup : sprite.lookup_records())
                if (lookup.key_class().value() == 3U) class3.push_back(lookup.key_index().value());
            std::sort(class3.begin(), class3.end());
            class3.erase(std::unique(class3.begin(), class3.end()), class3.end());
            if (class3.empty()) {
                ++damage_roots_without_class3;
            } else {
                bool contiguous = class3.front() == 0U;
                for (std::size_t i = 0; i < class3.size(); ++i)
                    contiguous = contiguous && class3[i] == i;
                if (!contiguous) ++damage_roots_with_noncontiguous_class3;
            }
        }
    };

    for (const auto& preset : catalog.army_presets()) {
        if (!preset.browser_visible) continue;
        ++visible_presets;
        variant_groups += preset.model_variant_groups.size();
        bool preset_has_damage = false;
        for (const auto& group : preset.model_variant_groups) {
            for (const auto& variant : group.variants) {
                ++arm_variants;
                mdl_replacements += variant.faction_technology_replacements.size();
                scan_hierarchy(variant.model_hierarchy, preset_has_damage);
                for (const auto& replacement : variant.faction_technology_replacements)
                    scan_hierarchy(replacement.model_hierarchy, preset_has_damage);
            }
        }
        if (preset_has_damage) ++damage_presets;
    }

    require(visible_presets == 270U, "stock browser-visible preset count changed");
    require(variant_groups == 448U, "stock model-variant group count changed");
    require(arm_variants == 1262U, "stock ARM variant count changed");
    require(mdl_replacements == 4U, "stock MDL faction-technology replacement count changed");
    require(damage_presets == 173U, "stock damage-state preset count changed");
    require(damage_runtime_roots == 483U, "stock damage-state runtime-root count changed");
    require(damage_effects == std::set<uint32_t>({110U,117U,118U}),
            "stock damage-state EFF identity set changed");
    require(damage_thresholds == std::set<int32_t>({33}),
            "stock damage-state threshold is no longer uniformly 33 percent");

    for (const uint32_t effect_id : damage_effects) {
        const auto* effect = catalog.effect_definition(effect_id);
        require(effect != nullptr, "stock damage-state EFF missing");
        require(effect->transition_kind == 2U && effect->creation_flags == 0U &&
                    effect->movement_speed_q12 == 48,
                "stock damage-state EFF no longer uses the proven transition-2 flags/speed");
    }

    const auto* xenite = catalog.army_preset(330U);
    const auto* tritium = catalog.army_preset(332U);
    require(xenite != nullptr && tritium != nullptr, "stock resource buildings are missing");
    require(!xenite->model_variant_groups.empty() && !tritium->model_variant_groups.empty(),
            "stock resource-building variant groups are missing");
    const auto& xv = xenite->model_variant_groups.front().variants;
    const auto& tv = tritium->model_variant_groups.front().variants;
    require(xv.size() >= 3U && xv[0].model_definition_id == 303U &&
                xv[0].faction_technology_replacements.size() == 1U &&
                xv[0].faction_technology_replacements[0].model_definition_id == 1303U &&
                xv[0].faction_technology_replacements[0].technology_unlock_id == 210U &&
                xv[1].model_definition_id == 1303U && xv[1].technology_unlock_id == 210U &&
                xv[1].faction_technology_replacements.size() == 1U &&
                xv[1].faction_technology_replacements[0].model_definition_id == 2303U &&
                xv[1].faction_technology_replacements[0].technology_unlock_id == 211U,
            "Xenite mine ARM/MDL live-upgrade chain changed");
    require(tv.size() >= 3U && tv[0].model_definition_id == 302U &&
                tv[0].faction_technology_replacements.size() == 1U &&
                tv[0].faction_technology_replacements[0].model_definition_id == 1302U &&
                tv[0].faction_technology_replacements[0].technology_unlock_id == 213U &&
                tv[1].model_definition_id == 1302U && tv[1].technology_unlock_id == 213U &&
                tv[1].faction_technology_replacements.size() == 1U &&
                tv[1].faction_technology_replacements[0].model_definition_id == 2302U &&
                tv[1].faction_technology_replacements[0].technology_unlock_id == 214U,
            "Tritium pump ARM/MDL live-upgrade chain changed");

    std::cout << "{\n"
              << "  \"schema\": \"thandor-g400-stock-faction-technology-damage-state-census-v1\",\n"
              << "  \"browser_visible_presets\": " << visible_presets << ",\n"
              << "  \"model_variant_groups\": " << variant_groups << ",\n"
              << "  \"arm_variants\": " << arm_variants << ",\n"
              << "  \"mdl_faction_technology_replacements\": " << mdl_replacements << ",\n"
              << "  \"damage_state_presets\": " << damage_presets << ",\n"
              << "  \"damage_state_runtime_roots\": " << damage_runtime_roots << ",\n"
              << "  \"damage_roots_without_class3_attachment\": " << damage_roots_without_class3 << ",\n"
              << "  \"damage_roots_with_noncontiguous_class3_keys\": " << damage_roots_with_noncontiguous_class3 << ",\n"
              << "  \"damage_effect_ids\": [110, 117, 118],\n"
              << "  \"damage_threshold_percent\": 33,\n"
              << "  \"damage_effect_transition_kind\": 2,\n"
              << "  \"damage_effect_creation_flags\": 0,\n"
              << "  \"damage_effect_movement_speed_q12\": 48,\n"
              << "  \"xenite_upgrade_chain\": [303, 1303, 2303],\n"
              << "  \"xenite_upgrade_technology_ids\": [210, 211],\n"
              << "  \"tritium_upgrade_chain\": [302, 1302, 2302],\n"
              << "  \"tritium_upgrade_technology_ids\": [213, 214],\n"
              << "  \"status\": \"PASS\"\n"
              << "}\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G400 stock census failed: " << error.what() << '\n';
    return 1;
}
