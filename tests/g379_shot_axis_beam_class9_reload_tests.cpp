#include "model_transform.hpp"
#include "thandor/catalog.hpp"
#include "thandor/model.hpp"
#include "thandor/simulation.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
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
        throw std::runtime_error("missing G379 source contract: " + std::string(token));
}
void reject_token(const std::string& text, std::string_view token) {
    if (text.find(token) != std::string::npos)
        throw std::runtime_error("rejected G379 source token returned: " + std::string(token));
}
bool near(float a, float b, float epsilon = 2.0e-4F) {
    return std::abs(a - b) <= epsilon;
}
}

int main(int argc, char** argv) try {
    using namespace thandor;
    const auto source = std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto editor = read_text(source / "src/editor/editor_app.cpp");
    const auto transform = read_text(source / "src/editor/model_transform.hpp");
    const auto simulation = read_text(source / "src/core/simulation.cpp");

    require_token(transform, "The authored SHT");
    require_token(transform, "forward axis is therefore local +Z");
    require_token(editor, "projectile_model_pose");
    require_token(editor, "simulation_mode0_beam_length_world");
    require_token(editor, "simulation_mode0_stretch_local_position");
    require_token(editor, "shot->trajectory_mode==0");
    require_token(editor, "age<projectile.lifetime_ticks");
    require_token(editor, "reverse_yaw");
    require_token(simulation, "0xFFFFFFFEU<<selector");
    require_token(simulation, "std::max(projectile.lifetime_ticks,impact_end)");
    reject_token(editor, "if(shot->trajectory_mode==0)visual_direction=initial_direction");
    reject_token(simulation, "state.launch_mesh_group_mask&=~(1U<<selector)");

    const Vec3f direction{2.0F, -3.0F, 4.0F};
    const uint32_t yaw = radians_to_turn16(std::atan2(direction.y, direction.x));
    const uint32_t pitch = radians_to_turn16(std::atan2(direction.z,
        std::hypot(direction.x, direction.y)));
    const auto pose = editor::shot_model_transform({1.0F, 2.0F, 3.0F}, yaw, pitch);
    const auto forward = editor::transform_vector(pose, {0.0F, 0.0F, 1.0F});
    const float length = std::sqrt(29.0F);
    if (!near(forward.x, direction.x / length) ||
        !near(forward.y, direction.y / length) ||
        !near(forward.z, direction.z / length))
        throw std::runtime_error("authored SHT local +Z no longer maps to shot flight");
    const auto zero_pose = editor::shot_model_transform({}, 0U, 0U);
    const auto local_x = editor::transform_vector(zero_pose, {1.0F, 0.0F, 0.0F});
    if (!near(local_x.x, 0.0F) || !near(local_x.y, 0.0F) || !near(local_x.z, -1.0F))
        throw std::runtime_error("shot basis unexpectedly returned to local +X forward");

    if (!near(simulation_mode0_stretch_local_position({0.0F, 0.0F, -0.25F}, 8.0F).z, -0.25F) ||
        !near(simulation_mode0_stretch_local_position({0.0F, 0.0F, 0.0F}, 8.0F).z, 4.0F) ||
        !near(simulation_mode0_stretch_local_position({0.0F, 0.0F, 0.25F}, 8.0F).z, 8.25F))
        throw std::runtime_error("mode-zero local-Z half/full beam extrusion changed");

    ShotDefinitionRecord stationary_beam_definition;
    stationary_beam_definition.trajectory_mode = 0;
    SimulationProjectileState stationary_beam;
    stationary_beam.launch_position = {1.0F, 2.0F, 3.0F};
    stationary_beam.target_position = {9.0F, 2.0F, 3.0F};
    stationary_beam.spawn_tick = 10U;
    stationary_beam.travel_ticks = 1U;
    stationary_beam.lifetime_ticks = 8U;
    const auto beam_after_collision = simulation_projectile_position(
        stationary_beam, stationary_beam_definition, 15U);
    if (!near(beam_after_collision.x, 1.0F) || !near(beam_after_collision.y, 2.0F) ||
        !near(beam_after_collision.z, 3.0F))
        throw std::runtime_error("mode-zero beam moved to its impact point after first-tick collision");

    if (simulation_class9_mesh_mask_after_fire(0xFFFFFFFFU, 0U) != 0xFFFFFFFEU ||
        simulation_class9_mesh_mask_after_fire(0xFFFFFFFFU, 1U) != 0xFFFFFFFCU ||
        simulation_class9_mesh_mask_after_fire(0xFFFFFFFFU, 2U) != 0xFFFFFFF8U)
        throw std::runtime_error("class-9 cumulative low-prefix mesh clear changed");

    ShotDefinitionRecord synthetic_laser;
    synthetic_laser.trajectory_mode = 0;
    synthetic_laser.projectile_lifetime_ticks = 8U;
    synthetic_laser.launch_speed_q12 = 4500;
    SimulationProjectileState beam;
    beam.launch_position = {0.0F, 0.0F, 0.0F};
    beam.target_position = {20.0F, 0.0F, 0.0F};
    const float expected_maximum = q12_to_float(8 * 4500);
    if (!near(simulation_mode0_beam_length_world(beam, synthetic_laser), expected_maximum))
        throw std::runtime_error("mode-zero beam length is not capped by SHT D0*launchSpeed");
    beam.target_position = {3.0F, 4.0F, 0.0F};
    if (!near(simulation_mode0_beam_length_world(beam, synthetic_laser), 5.0F))
        throw std::runtime_error("mode-zero beam did not stop at the nearer resolved hit");

    SimulationProjectileState overlap;
    overlap.spawn_tick = 10U;
    overlap.travel_ticks = 1U;
    overlap.lifetime_ticks = 8U;
    overlap.impact_kind = SimulationImpactKind::TerrainSurface;
    overlap.impact_visual_lifetime_ticks = 3U;
    if (simulation_projectile_expired(overlap, 17U) ||
        !simulation_projectile_expired(overlap, 18U))
        throw std::runtime_error("beam model lifetime is still truncated by impact timing");

    if (argc < 2) {
        std::cout << "G379_SHOT_AXIS_BEAM_CLASS9_RELOAD PASS source-only\n";
        return 0;
    }

    const auto catalog = AssetCatalog::scan_game_root(argv[1]);
    const auto* laser_turret = catalog.army_preset(351U);
    const auto* rocket_turret = catalog.army_preset(352U);
    const auto* laser = catalog.shot_definition(18U);
    const auto* rocket = catalog.shot_definition(20U);
    if (!laser_turret || !rocket_turret || !laser || !rocket)
        throw std::runtime_error("stock turret or SHT records are missing");
    if (laser->trajectory_mode != 0 || laser->projectile_lifetime_ticks != 8U ||
        laser->launch_speed_q12 != 4500)
        throw std::runtime_error("stock SHT 18 ray/beam geometry contract changed");
    if (rocket->trajectory_mode != 3 || rocket->sprite_path != "spr/schuesse/srmid0.spr")
        throw std::runtime_error("stock SHT 20 rocket contract changed");
    if (rocket_turret->simulation_weapon_profiles.size() != 1U) 
        throw std::runtime_error("stock rocket turret weapon count changed");
    const auto& weapon = rocket_turret->simulation_weapon_profiles.front();
    if (weapon.runtime_class_id != 9U || weapon.launch_attachment_selector_mask != 0x07U ||
        weapon.inter_shot_ticks != 53U || weapon.reload_ticks != 400U ||
        weapon.launch_model_node_index != 3U)
        throw std::runtime_error("stock class-9 three-rocket timing/mask changed");

    const auto rocket_sprite = SpriteAsset::parse(catalog.load(rocket->sprite_path));
    float minimum_z = 1.0e9F, maximum_z = -1.0e9F;
    for (const auto& vertex : rocket_sprite.mesh().vertices) {
        minimum_z = std::min(minimum_z, vertex.position.z);
        maximum_z = std::max(maximum_z, vertex.position.z);
    }
    if (!(minimum_z < -0.60F && maximum_z > 0.24F))
        throw std::runtime_error("stock rocket is no longer authored along local Z");
    const auto rear_attachment = std::find_if(rocket_sprite.attachment_records().begin(),
        rocket_sprite.attachment_records().end(), [](const SpriteAttachmentRecord& record) {
            return record.kind() == 3U && record.child_index() == 1U;
        });
    if (rear_attachment == rocket_sprite.attachment_records().end() ||
        rear_attachment->local_translation.z >= -0.24F)
        throw std::runtime_error("stock rocket exhaust attachment is not on negative local Z");

    const auto& hierarchy = rocket_turret->resolved_model_hierarchy.empty()
        ? rocket_turret->model_hierarchy : rocket_turret->resolved_model_hierarchy;
    if (hierarchy.size() <= 3U) throw std::runtime_error("stock rocket hierarchy is incomplete");
    const auto rack_sprite = SpriteAsset::parse(catalog.load(hierarchy[3U].sprite_path));
    std::map<uint32_t, std::size_t> rack_triangles;
    for (std::size_t index = 0; index + 2U < rack_sprite.mesh().indices.size(); index += 3U)
        ++rack_triangles[rack_sprite.mesh().vertices.at(rack_sprite.mesh().indices[index]).mesh_group_selector_mask];
    const std::map<uint32_t, std::size_t> expected_racks{{1U,23U},{2U,23U},{4U,23U}};
    if (rack_triangles != expected_racks)
        throw std::runtime_error("stock three independent reload mesh groups changed");

    std::size_t attack_placements = 0U;
    std::size_t weapon_profiles = 0U;
    std::set<uint32_t> shot_ids;
    for (const auto& preset : catalog.army_presets()) {
        if (preset.simulation_weapon_profiles.empty()) continue;
        ++attack_placements;
        for (const auto& profile : preset.simulation_weapon_profiles) {
            ++weapon_profiles;
            shot_ids.insert(profile.shot_definition_id);
            if (!catalog.shot_definition(profile.shot_definition_id))
                throw std::runtime_error("attack profile references a missing SHT");
        }
    }
    if (attack_placements != 125U || weapon_profiles != 160U || shot_ids.size() != 26U)
        throw std::runtime_error("complete attacking unit/building census changed: placements=" +
            std::to_string(attack_placements) + " profiles=" + std::to_string(weapon_profiles) +
            " shots=" + std::to_string(shot_ids.size()));

    std::cout << "G379_SHOT_AXIS_BEAM_CLASS9_RELOAD PASS attackPlacements="
              << attack_placements << " weaponProfiles=" << weapon_profiles
              << " shotDefinitions=" << shot_ids.size() << "\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G379_SHOT_AXIS_BEAM_CLASS9_RELOAD FAIL: " << error.what() << "\n";
    return 1;
}
