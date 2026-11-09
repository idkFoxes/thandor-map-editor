#include "model_transform.hpp"
#include "thandor/catalog.hpp"
#include "thandor/model.hpp"
#include "thandor/simulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

using namespace thandor;

namespace {
std::string read_text(const std::string& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("cannot read " + path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void require_token(const std::string& text, const std::string& token) {
    if (text.find(token) == std::string::npos) throw std::runtime_error("missing source token: " + token);
}
uint32_t child_index(std::span<const ModelVisualNode> hierarchy, uint32_t parent, uint32_t slot) {
    for (uint32_t index = 0; index < hierarchy.size(); ++index)
        if (hierarchy[index].creates_runtime_node() && hierarchy[index].parent_index == parent &&
            hierarchy[index].child_slot == slot) return index;
    return std::numeric_limits<uint32_t>::max();
}
}

int main(int argc, char** argv) try {
    const std::string source = THANDOR_SOURCE_DIR;
    const auto simulation_hpp = read_text(source + "/include/thandor/simulation.hpp");
    const auto simulation_cpp = read_text(source + "/src/core/simulation.cpp");
    const auto catalog_cpp = read_text(source + "/src/core/catalog.cpp");
    const auto editor_cpp = read_text(source + "/src/editor/editor_app.cpp");
    require_token(simulation_hpp, "aim_model_node_index");
    require_token(simulation_hpp, "rack_mesh_model_node_index");
    require_token(catalog_cpp, "direct_child_index(yaw_node_index, 0U)");
    require_token(catalog_cpp, "direct_child_index(profile.aim_model_node_index, 0U)");
    require_token(editor_cpp, "aim_origin=transforms[weapon.aim_model_node_index].translation");
    require_token(editor_cpp, "instance_index==profile.rack_mesh_model_node_index");
    require_token(simulation_cpp, "return state.launch_mesh_group_mask");

    SimulationWeaponProfile class9;
    class9.runtime_class_id = 9U;
    class9.shot_definition_id = 20U;
    class9.reload_ticks = 2U;
    class9.inter_shot_ticks = 53U;
    class9.launch_attachment_count = 1U;
    class9.launch_attachment_selector_mask = 1U;
    SimulationWeaponState state;
    state.launch_mesh_group_mask &= ~1U;
    state.attachment_reload_ticks[0] = 2;
    SimulationActorState actor;
    ShotDefinitionRecord shot;
    simulation_tick_weapon(actor, state, class9, shot, {}, nullptr, true);
    if (state.attachment_reload_ticks[0] != 1 || (state.launch_mesh_group_mask & 1U) != 0U)
        throw std::runtime_error("class-9 reload tick 1 mismatch");
    simulation_tick_weapon(actor, state, class9, shot, {}, nullptr, true);
    if (state.attachment_reload_ticks[0] != 0 || (state.launch_mesh_group_mask & 1U) != 0U)
        throw std::runtime_error("class-9 rack returned at zero instead of signed underflow");
    simulation_tick_weapon(actor, state, class9, shot, {}, nullptr, true);
    if (state.attachment_reload_ticks[0] != 0 || (state.launch_mesh_group_mask & 1U) == 0U)
        throw std::runtime_error("class-9 rack did not return on the following maintenance tick");

    SimulationWeaponState authoritative;
    authoritative.attachment_reload_ticks[0] = 0;
    authoritative.launch_mesh_group_mask &= ~1U;
    if ((simulation_class9_render_mesh_mask(authoritative, class9) & 1U) != 0U)
        throw std::runtime_error("renderer bypassed the callback-owned rack mask");

    if (argc < 2) {
        std::cout << "G381_SEMANTIC_C_CLASS9_PIVOT_RACK_RELOAD PASS source-only\n";
        return 0;
    }

    const auto catalog = AssetCatalog::scan_game_root(argv[1]);
    const auto* preset = catalog.army_preset(352U);
    if (!preset || preset->simulation_weapon_profiles.size() != 1U)
        throw std::runtime_error("stock ARM 352 profile missing");
    const auto& weapon = preset->simulation_weapon_profiles.front();
    const auto hierarchy = preset->resolved_model_hierarchy.empty()
        ? std::span<const ModelVisualNode>(preset->model_hierarchy)
        : std::span<const ModelVisualNode>(preset->resolved_model_hierarchy);
    const uint32_t callback_root_index = weapon.yaw_model_node_index;
    const uint32_t aim_index = child_index(hierarchy, callback_root_index, 0U);
    const uint32_t rack_index = child_index(hierarchy, aim_index, 0U);
    if (weapon.runtime_class_id != 9U || weapon.aim_model_node_index != aim_index ||
        weapon.rack_mesh_model_node_index != rack_index)
        throw std::runtime_error("stock ARM 352 class-9 pivot/rack ownership changed");
    if (weapon.aim_model_node_index != weapon.pitch_model_node_index)
        throw std::runtime_error("stock ARM 352 aim pivot is not the pitch node");
    if (weapon.yaw_model_node_index != 1U || weapon.aim_model_node_index != 2U ||
        weapon.rack_mesh_model_node_index != 3U || weapon.launch_model_node_index != 3U)
        throw std::runtime_error("stock ARM 352 callback-root/aim/rack/launch indices changed");
    if (rack_index >= hierarchy.size()) throw std::runtime_error("stock ARM 352 rack index invalid");
    std::vector<editor::ModelHierarchyTransform> transforms(hierarchy.size());
    std::vector<SpriteAsset> sprites;
    sprites.reserve(hierarchy.size());
    for (const auto& node : hierarchy) sprites.push_back(SpriteAsset::parse(catalog.load(node.sprite_path)));
    transforms[0] = editor::placement_root_transform(0.0F, 0.0F, 0.0F, 0.0F);
    for (std::size_t index = 1U; index < hierarchy.size(); ++index) {
        const auto& node = hierarchy[index];
        const auto* attachment = sprites[node.parent_index].child_attachment(node.child_slot);
        if (!attachment) throw std::runtime_error("stock ARM 352 hierarchy attachment missing");
        auto local = editor::fixed_model_rotation_basis(node.local_rotation_angle2,
            node.local_rotation_angle1, node.local_rotation_angle0);
        local.translation = attachment->local_translation;
        transforms[index] = editor::compose_model_transform(transforms[node.parent_index], local);
    }
    const Vec3f aim_origin = transforms[aim_index].translation;
    const auto selector0 = std::find_if(sprites[weapon.launch_model_node_index].attachment_records().begin(),
        sprites[weapon.launch_model_node_index].attachment_records().end(),
        [](const SpriteAttachmentRecord& record) {
            return record.kind() == 2U && record.child_index() == 0U;
        });
    if (selector0 == sprites[weapon.launch_model_node_index].attachment_records().end())
        throw std::runtime_error("stock ARM 352 selector-0 muzzle missing");
    const Vec3f muzzle_origin = editor::transform_point(transforms[weapon.launch_model_node_index],
        selector0->local_translation);
    const float aim_muzzle_distance = std::sqrt(
        (aim_origin.x-muzzle_origin.x)*(aim_origin.x-muzzle_origin.x) +
        (aim_origin.y-muzzle_origin.y)*(aim_origin.y-muzzle_origin.y) +
        (aim_origin.z-muzzle_origin.z)*(aim_origin.z-muzzle_origin.z));
    if (aim_muzzle_distance < 0.05F)
        throw std::runtime_error("stock ARM 352 aim pivot collapsed onto the muzzle");

    const auto rack_sprite = SpriteAsset::parse(catalog.load(hierarchy[rack_index].sprite_path));
    std::map<uint32_t, std::size_t> groups;
    for (std::size_t i = 0; i + 2U < rack_sprite.mesh().indices.size(); i += 3U)
        ++groups[rack_sprite.mesh().vertices.at(rack_sprite.mesh().indices[i]).mesh_group_selector_mask];
    const std::map<uint32_t, std::size_t> expected{{1U,23U},{2U,23U},{4U,23U}};
    if (groups != expected) throw std::runtime_error("stock ARM 352 rack mesh groups changed");

    std::cout << "G381_SEMANTIC_C_CLASS9_PIVOT_RACK_RELOAD PASS aimNode=" << aim_index
              << " rackNode=" << rack_index << " launchNode=" << weapon.launch_model_node_index
              << " aimMuzzleDistance=" << aim_muzzle_distance << "\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G381_SEMANTIC_C_CLASS9_PIVOT_RACK_RELOAD FAIL: " << error.what() << "\n";
    return 1;
}
