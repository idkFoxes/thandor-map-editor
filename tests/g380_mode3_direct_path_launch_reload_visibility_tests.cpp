#include "model_transform.hpp"
#include "thandor/catalog.hpp"
#include "thandor/model.hpp"
#include "thandor/simulation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
        throw std::runtime_error("missing G380 source contract: " + std::string(token));
}
void reject_token(const std::string& text, std::string_view token) {
    if (text.find(token) != std::string::npos)
        throw std::runtime_error("rejected G380 source token returned: " + std::string(token));
}
bool near(float a, float b, float epsilon = 3.0e-4F) {
    return std::abs(a - b) <= epsilon;
}
void require_vec_near(thandor::Vec3f actual, thandor::Vec3f expected,
                      float epsilon, std::string_view label) {
    if (!near(actual.x, expected.x, epsilon) || !near(actual.y, expected.y, epsilon) ||
        !near(actual.z, expected.z, epsilon)) {
        throw std::runtime_error(std::string(label) + " changed: actual=" +
            std::to_string(actual.x) + "," + std::to_string(actual.y) + "," +
            std::to_string(actual.z));
    }
}
}

int main(int argc, char** argv) try {
    using namespace thandor;
    using namespace editor;
    const auto source = std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto simulation_source = read_text(source / "src/core/simulation.cpp");
    const auto editor_source = read_text(source / "src/editor/editor_app.cpp");

    require_token(simulation_source, "modelPitch-movementStateFlags");
    require_token(simulation_source, "simulation_mode3_model_pitch_turn16");
    require_token(simulation_source, "const float direct_distance=distance3(launch,target_position)");
    require_token(editor_source, "simulation_mode3_model_pitch_turn16");
    require_token(editor_source, "simulation_class9_render_mesh_mask");
    reject_token(editor_source,
        "projectile.launch_pitch_turn16)+\n                static_cast<int32_t>(thandor::simulation_mode3_elevation_offset_turn16");
    reject_token(simulation_source,
        "const float horizontal=std::hypot(target_position.x-launch.x,target_position.y-launch.y)");

    ShotDefinitionRecord mode3;
    mode3.trajectory_mode = 3;
    mode3.launch_speed_q12 = 4096;
    mode3.range_scale_270_q12 = 20;
    mode3.elevation_offset_angle16 = 6401;
    mode3.lifetime_ticks = 80U;
    SimulationWeaponProfile synthetic_weapon;
    synthetic_weapon.shot_definition_id = 20U;
    synthetic_weapon.reload_ticks = 400U;
    synthetic_weapon.launch_attachment_count = 1U;
    synthetic_weapon.launch_attachment_selector_mask = 1U;

    const Vec3f launch{1.0F, 2.0F, 3.0F};
    const Vec3f target{11.0F, 2.0F, 3.0F};
    const std::array<Vec3f, 1> launch_points{launch};
    uint64_t serial = 1U;
    auto volley = simulation_create_volley(0U, 0U, synthetic_weapon, launch_points, target,
        mode3, 100U, serial, 0U);
    if (volley.size() != 1U) throw std::runtime_error("synthetic mode-3 volley was not created");
    const auto& projectile = volley.front();
    if (projectile.launch_pitch_turn16 != 6401U)
        throw std::runtime_error("creation no longer stores direct pitch plus elevation offset");
    if (!near(projectile.launch_velocity.x, 1.0F) || !near(projectile.launch_velocity.y, 0.0F) ||
        !near(projectile.launch_velocity.z, 0.0F))
        throw std::runtime_error("mode-3 movement still consumes the visual elevation offset");

    if (simulation_mode3_model_pitch_turn16(projectile, mode3, 0U) != 6401U)
        throw std::runtime_error("mode-3 visual pitch is not exact at creation");
    const uint32_t half_pitch = simulation_mode3_model_pitch_turn16(projectile, mode3, 20U);
    if (half_pitch < 3199U || half_pitch > 3201U)
        throw std::runtime_error("mode-3 visual elevation did not decay to one half at ramp");
    if (simulation_mode3_model_pitch_turn16(projectile, mode3, 40U) != 0U)
        throw std::runtime_error("mode-3 visual elevation did not decay to direct pitch");

    require_vec_near(simulation_projectile_position(projectile, mode3, 100U), launch,
        1.0e-6F, "mode-3 spawn position");
    require_vec_near(simulation_projectile_position(projectile, mode3, 101U), launch,
        1.0e-6F, "mode-3 first maintenance position");
    const auto after_two = simulation_projectile_position(projectile, mode3, 102U);
    if (!(after_two.x > launch.x && near(after_two.y, launch.y) && near(after_two.z, launch.z)))
        throw std::runtime_error("mode-3 accelerated path is not the direct target line");
    for (uint32_t age = 0U; age < projectile.travel_ticks; ++age) {
        const auto point = simulation_projectile_position(projectile, mode3, 100U + age);
        if (!near(point.y, launch.y, 4.0e-4F) || !near(point.z, launch.z, 4.0e-4F))
            throw std::runtime_error("mode-3 projectile left the direct launch-target line");
    }
    require_vec_near(simulation_projectile_position(projectile, mode3,
        100U + projectile.travel_ticks), target, 1.0e-5F, "mode-3 target clamp");

    const auto initial_pose = shot_model_transform(projectile.launch_position,
        projectile.launch_yaw_turn16,
        simulation_mode3_model_pitch_turn16(projectile, mode3, 0U));
    require_vec_near(initial_pose.translation, launch, 1.0e-6F,
        "mode-3 shot model origin");
    const auto initial_forward = transform_vector(initial_pose, {0.0F, 0.0F, 1.0F});
    if (!(initial_forward.z > 0.55F && projectile.launch_velocity.z == 0.0F))
        throw std::runtime_error("mode-3 visual pitch and direct movement were flattened together");

    SimulationWeaponProfile class9;
    class9.runtime_class_id = 9U;
    class9.shot_definition_id = 20U;
    class9.reload_ticks = 400U;
    class9.launch_attachment_count = 3U;
    class9.launch_attachment_selector_mask = 0x07U;
    SimulationWeaponState rack;
    rack.launch_mesh_group_mask = 0xFFFFFFF8U;
    rack.attachment_reload_ticks[0] = 0;
    rack.attachment_reload_ticks[1] = 40;
    rack.attachment_reload_ticks[2] = 0;
    if ((simulation_class9_render_mesh_mask(rack, class9) & 0x07U) != 0U)
        throw std::runtime_error("renderer synthesized class-9 rack bits from timer zero");
    SimulationActorState rack_actor;
    ShotDefinitionRecord rack_shot;
    simulation_tick_weapon(rack_actor, rack, class9, rack_shot, {}, nullptr, true);
    if ((simulation_class9_render_mesh_mask(rack, class9) & 0x07U) != 0x05U)
        throw std::runtime_error("class-9 zero timers did not restore through maintenance");

    if (argc < 2) {
        std::cout << "G380_MODE3_DIRECT_PATH_LAUNCH_RELOAD PASS source-only\n";
        return 0;
    }

    const auto catalog = AssetCatalog::scan_game_root(argv[1]);
    const auto* rocket_turret = catalog.army_preset(352U);
    const auto* rocket = catalog.shot_definition(20U);
    if (!rocket_turret || !rocket)
        throw std::runtime_error("stock asset 352 or SHT 20 is missing");
    if (rocket->trajectory_mode != 3 || rocket->launch_speed_q12 != 750 ||
        rocket->range_scale_270_q12 != 20 || rocket->elevation_offset_angle16 != 6401)
        throw std::runtime_error("stock mode-3 rocket parameters changed");
    if (rocket_turret->simulation_weapon_profiles.size() != 1U)
        throw std::runtime_error("stock asset 352 weapon profile count changed");
    const auto& stock_weapon = rocket_turret->simulation_weapon_profiles.front();
    if (stock_weapon.runtime_class_id != 9U || stock_weapon.launch_model_node_index != 3U ||
        stock_weapon.launch_attachment_selector_mask != 0x07U ||
        stock_weapon.inter_shot_ticks != 53U || stock_weapon.reload_ticks != 400U)
        throw std::runtime_error("stock asset 352 class-9 launch contract changed");

    const auto& hierarchy = rocket_turret->resolved_model_hierarchy.empty()
        ? rocket_turret->model_hierarchy : rocket_turret->resolved_model_hierarchy;
    if (hierarchy.size() <= 3U) throw std::runtime_error("stock asset 352 hierarchy is incomplete");
    std::vector<ModelHierarchyTransform> transforms(hierarchy.size());
    std::vector<SpriteAsset> sprites;
    sprites.reserve(hierarchy.size());
    for (const auto& node : hierarchy)
        sprites.push_back(SpriteAsset::parse(catalog.load(node.sprite_path)));
    transforms[0] = placement_root_transform(0.0F, 0.0F, 0.0F, 0.0F);
    for (std::size_t node_index = 1U; node_index < hierarchy.size(); ++node_index) {
        const auto& node = hierarchy[node_index];
        const auto* attachment = sprites[node.parent_index].child_attachment(node.child_slot);
        if (!attachment) throw std::runtime_error("stock hierarchy child attachment is missing");
        uint32_t angle2 = node.local_rotation_angle2;
        uint32_t angle1 = node.local_rotation_angle1;
        if (node_index == stock_weapon.yaw_model_node_index) angle2 = 0U;
        if (node_index == stock_weapon.pitch_model_node_index)
            angle1 = static_cast<uint32_t>(rocket->elevation_offset_angle16);
        auto local = fixed_model_rotation_basis(angle2, angle1, node.local_rotation_angle0);
        local.translation = attachment->local_translation;
        transforms[node_index] = compose_model_transform(transforms[node.parent_index], local);
    }
    std::map<uint32_t, Vec3f> stock_launch_points;
    for (const auto& attachment : sprites[3U].attachment_records()) {
        if (attachment.kind() != 2U) continue;
        stock_launch_points.emplace(attachment.child_index(),
            transform_point(transforms[3U], attachment.local_translation));
    }
    if (stock_launch_points.size() != 3U || !stock_launch_points.contains(0U) ||
        !stock_launch_points.contains(1U) || !stock_launch_points.contains(2U))
        throw std::runtime_error("stock class-9 launch selector set changed");
    require_vec_near(stock_launch_points.at(0U), {-0.0444647F, 0.140380F, 0.493669F},
        8.0e-4F, "stock selector-0 live muzzle");
    require_vec_near(stock_launch_points.at(1U), {-0.114482F, -0.0061035F, 0.593066F},
        8.0e-4F, "stock selector-1 live muzzle");
    require_vec_near(stock_launch_points.at(2U), {-0.0454489F, -0.155518F, 0.495066F},
        8.0e-4F, "stock selector-2 live muzzle");

    uint64_t stock_serial = 100U;
    const std::array<Vec3f, 1> selector0{stock_launch_points.at(0U)};
    const Vec3f stock_target{5.0F, stock_launch_points.at(0U).y, stock_launch_points.at(0U).z};
    auto stock_volley = simulation_create_volley(0U, 0U, stock_weapon, selector0,
        stock_target, *rocket, 0U, stock_serial, 0U);
    if (stock_volley.size() != 1U)
        throw std::runtime_error("stock selector-0 rocket was not created");
    const auto& stock_projectile = stock_volley.front();
    require_vec_near(stock_projectile.launch_position, stock_launch_points.at(0U),
        1.0e-6F, "stock projectile launch point");
    if (std::abs(stock_projectile.launch_velocity.z) > 1.0e-5F)
        throw std::runtime_error("stock rocket flight still inherits its visual elevation offset");
    if (simulation_mode3_model_pitch_turn16(stock_projectile, *rocket, 0U) !=
        stock_projectile.launch_pitch_turn16)
        throw std::runtime_error("stock rocket visual pose no longer begins at creation pitch");

    std::cout << "G380_MODE3_DIRECT_PATH_LAUNCH_RELOAD PASS stockLaunchPoints="
              << stock_launch_points.size() << " travelTicks=" << stock_projectile.travel_ticks
              << "\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G380_MODE3_DIRECT_PATH_LAUNCH_RELOAD FAIL: " << error.what() << "\n";
    return 1;
}
