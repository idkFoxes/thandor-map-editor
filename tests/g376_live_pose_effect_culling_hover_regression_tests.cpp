#include "model_transform.hpp"
#include "thandor/catalog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read " + path.string());
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

void require_token(const std::string& text, std::string_view token) {
    if (text.find(token) == std::string::npos)
        throw std::runtime_error("missing G376 source contract: " + std::string(token));
}

void reject_token(const std::string& text, std::string_view token) {
    if (text.find(token) != std::string::npos)
        throw std::runtime_error("rejected G376 source token returned: " + std::string(token));
}

bool near(float a, float b, float epsilon = 1.0e-4F) {
    return std::abs(a - b) <= epsilon;
}
}

int main(int argc, char** argv) try {
    const auto source = std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto editor = read_text(source / "src/editor/editor_app.cpp");
    const auto header = read_text(source / "src/editor/editor_app.hpp");
    const auto catalog_source = read_text(source / "src/core/catalog.cpp");
    const auto transform_source = read_text(source / "src/editor/model_transform.hpp");

    require_token(editor, "!simulation_static_replacement_dirty_");
    require_token(editor, "bounds_visible_in_canvas(placement_pick_bounds_[placement_index],2.0F)");
    require_token(editor, "synchronize_simulated_actor_placement(index,actor)");
    require_token(editor, "placement.set_render_position_q12(x_q12,y_q12)");
    require_token(editor, "pending.local_type6_attachments.assign");
    require_token(editor, "resolved.world_type6_attachments.push_back");
    require_token(editor, "simulation_hierarchy_transforms(");
    require_token(editor, "effect_orientation=pose.root_orientation");
    require_token(editor, "preset->runtime_model_class_id==21U");
    require_token(editor, "pending.emitter_model_node_index");
    require_token(editor, "sprite_lod_oriented_transform(");
    require_token(editor, "shot_model_transform(");
    require_token(editor, "node_active=false;");
    reject_token(editor, "dynamic MDL child slot lacks an SPR attachment");
    require_token(header, "ModelHierarchyTransform emitter_orientation{}");
    require_token(catalog_source, "preset.runtime_uses_generic_model_subnode_animation =");
    require_token(catalog_source, "preset.runtime_model_class_id == 17U");
    require_token(transform_source, "sprites must retain the complete owner basis");

    editor::ModelHierarchyTransform authored =
        editor::fixed_model_rotation_basis(0x1800U, 0x3400U, 0x0800U);
    authored.translation = {3.0F, 4.0F, 5.0F};
    const auto fixed = editor::sprite_lod_oriented_transform(
        authored.translation, {20.0F, -10.0F, 15.0F}, authored, 0.0F, 0U);
    for (std::size_t index = 0; index < authored.basis.size(); ++index)
        if (!near(fixed.basis[index], authored.basis[index]))
            throw std::runtime_error("fixed EFF orientation lost an authored basis component");

    const uint32_t yaw = thandor::radians_to_turn16(std::atan2(-3.0F, 2.0F));
    const uint32_t pitch = thandor::radians_to_turn16(std::atan2(4.0F, std::sqrt(13.0F)));
    const auto shot_pose = editor::shot_model_transform({0.0F, 0.0F, 0.0F}, yaw, pitch);
    const auto mapped_z = editor::transform_vector(shot_pose, {0.0F, 0.0F, 1.0F});
    const float length = std::sqrt(29.0F);
    if (!near(mapped_z.x, 2.0F / length) || !near(mapped_z.y, -3.0F / length) ||
        !near(mapped_z.z, 4.0F / length))
        throw std::runtime_error("projectile transform does not map authored local +Z to flight");

    if (argc < 2) {
        std::cout << "G376_LIVE_POSE_EFFECT_CULLING_HOVER_REGRESSION PASS source-only\n";
        return 0;
    }

    const auto catalog = thandor::AssetCatalog::scan_game_root(argv[1]);
    std::size_t class17 = 0;
    std::size_t generic_animated = 0;
    std::size_t class21_with_child0 = 0;
    for (const auto& preset : catalog.army_presets()) {
        if (preset.runtime_model_class_id == 17U) {
            ++class17;
            if (!preset.runtime_uses_class17_movement_banking ||
                preset.runtime_uses_generic_model_subnode_animation ||
                preset.has_model_subnode_animation)
                throw std::runtime_error("class-17 ARM fields were routed through generic fan animation");
        }
        if (preset.has_model_subnode_animation) {
            ++generic_animated;
            const uint32_t runtime_class = preset.runtime_model_class_id;
            if (runtime_class != 4U && runtime_class != 11U && runtime_class != 13U &&
                runtime_class != 14U)
                throw std::runtime_error("generic child animation escaped its binary class dispatch set");
        }
        if (preset.runtime_model_class_id == 21U) {
            const auto& hierarchy = preset.resolved_model_hierarchy.empty()
                ? preset.model_hierarchy : preset.resolved_model_hierarchy;
            const auto root = std::find_if(hierarchy.begin(), hierarchy.end(), [](const auto& node) {
                return node.parent_index == std::numeric_limits<uint32_t>::max();
            });
            if (root != hierarchy.end()) {
                const auto root_index = static_cast<uint32_t>(std::distance(hierarchy.begin(), root));
                if (std::any_of(hierarchy.begin(), hierarchy.end(), [&](const auto& node) {
                        return node.parent_index == root_index && node.child_slot == 0U &&
                               node.creates_runtime_node() && !node.sprite_path.empty();
                    }))
                    ++class21_with_child0;
            }
        }
    }
    for (const uint32_t id : {200U, 201U, 202U, 203U, 204U, 205U, 206U, 207U, 208U,
                              220U, 221U, 222U, 223U, 224U}) {
        const auto* preset = catalog.army_preset(id);
        if (!preset || preset->runtime_model_class_id != 17U ||
            preset->has_model_subnode_animation)
            throw std::runtime_error("Hornet/hover stock regression for ARM id " +
                                     std::to_string(id));
    }
    if (class17 < 14U || generic_animated != 12U || class21_with_child0 < 3U)
        throw std::runtime_error("stock class-dispatch/emitter census changed");

    std::cout << "G376_LIVE_POSE_EFFECT_CULLING_HOVER_REGRESSION class17=" << class17
              << " genericAnimated=" << generic_animated
              << " class21Child0=" << class21_with_child0 << " PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G376_LIVE_POSE_EFFECT_CULLING_HOVER_REGRESSION FAIL: "
              << error.what() << '\n';
    return 1;
}
