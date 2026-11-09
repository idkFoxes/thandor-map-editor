#include "thandor/catalog.hpp"
#include "thandor/model.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
std::string json_escape(std::string_view value) {
    std::string result;
    for (char c : value) {
        if (c == '\\' || c == '"') result.push_back('\\');
        if (c == '\n') { result += "\\n"; continue; }
        result.push_back(c);
    }
    return result;
}
}

int main(int argc, char** argv) try {
    if (argc < 2) {
        std::cout << "G349_STOCK_BUILDING_HIERARCHY_CENSUS PASS unit-only\n";
        return 0;
    }
    const auto catalog = thandor::AssetCatalog::scan_game_root(argv[1]);
    constexpr std::array<uint32_t, 4> ids{301U, 330U, 331U, 333U};
    std::size_t model_records = 0U;
    std::size_t class13_root_animated_records = 0U;
    std::size_t class14_15_nonzero_c0_records = 0U;
    for (const auto& asset : catalog.assets()) {
        if (asset.kind != thandor::AssetKind::Model) continue;
        try {
            for (const auto& visual : thandor::parse_model_visual_records(catalog.load(asset), asset.normalized_path)) {
                ++model_records;
                if (visual.runtime_class_id == 13U && visual.has_primary_texture_animation)
                    ++class13_root_animated_records;
                if ((visual.runtime_class_id == 14U || visual.runtime_class_id == 15U) &&
                    visual.placement_occupancy_class != 0U)
                    ++class14_15_nonzero_c0_records;
            }
        } catch (const std::exception&) {
        }
    }

    std::ostringstream json;
    json << "{\n  \"schema\": \"thandor-g349-stock-building-hierarchy-census-v1\",\n";
    json << "  \"model_records\": " << model_records << ",\n";
    json << "  \"class13_root_animated_records\": " << class13_root_animated_records << ",\n";
    json << "  \"class14_15_nonzero_c0_records\": " << class14_15_nonzero_c0_records << ",\n";
    json << "  \"buildings\": [\n";
    std::size_t complete = 0U;
    for (std::size_t item = 0U; item < ids.size(); ++item) {
        const auto* preset = catalog.army_preset(ids[item]);
        if (preset == nullptr) throw std::runtime_error("stock building preset is missing");
        std::size_t live_nodes = 0U;
        std::size_t recursive_triangles = 0U;
        std::size_t root_triangles = 0U;
        std::size_t attachment_backed_children = 0U;
        for (std::size_t node_index = 0U; node_index < preset->model_hierarchy.size(); ++node_index) {
            const auto& node = preset->model_hierarchy[node_index];
            if (!node.creates_runtime_node() || node.sprite_path.empty()) continue;
            ++live_nodes;
            const auto sprite = thandor::SpriteAsset::parse(catalog.load(node.sprite_path));
            const auto triangles = sprite.mesh().indices.size() / 3U;
            recursive_triangles += triangles;
            if (node.parent_index == std::numeric_limits<uint32_t>::max()) {
                root_triangles = triangles;
            } else {
                if (node.parent_index >= preset->model_hierarchy.size())
                    throw std::runtime_error("stock hierarchy parent index is invalid");
                const auto& parent = preset->model_hierarchy[node.parent_index];
                const auto parent_sprite = thandor::SpriteAsset::parse(catalog.load(parent.sprite_path));
                if (parent_sprite.child_attachment(node.child_slot) == nullptr)
                    throw std::runtime_error("stock live child lacks its parent SPR attachment");
                ++attachment_backed_children;
            }
        }
        if (live_nodes != 0U && recursive_triangles >= root_triangles) ++complete;
        if (item != 0U) json << ",\n";
        json << "    {\"asset_id\": " << ids[item]
             << ", \"model_id\": " << preset->model_definition_id
             << ", \"runtime_class\": " << preset->runtime_model_class_id
             << ", \"hierarchy_nodes\": " << preset->model_hierarchy.size()
             << ", \"live_nodes\": " << live_nodes
             << ", \"attachment_backed_children\": " << attachment_backed_children
             << ", \"root_triangles\": " << root_triangles
             << ", \"recursive_triangles\": " << recursive_triangles
             << ", \"root_texture_animation\": " << (preset->has_primary_texture_animation ? "true" : "false")
             << ", \"child0_step\": " << preset->child0_rotation_step_turn16
             << ", \"child1_step\": " << preset->child1_rotation_step_turn16
             << ", \"child2_step_q12\": " << preset->child2_vertical_step_q12
             << ", \"root_sprite\": \"" << json_escape(preset->sprite_path) << "\"}";
    }
    json << "\n  ],\n  \"complete_records\": " << complete << "\n}\n";
    std::cout << json.str();
    if (argc >= 3) {
        std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("could not create census output");
        output << json.str();
    }
    if (complete != ids.size()) throw std::runtime_error("stock hierarchy census is incomplete");
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G349_STOCK_BUILDING_HIERARCHY_CENSUS FAIL: " << error.what() << '\n';
    return 1;
}
