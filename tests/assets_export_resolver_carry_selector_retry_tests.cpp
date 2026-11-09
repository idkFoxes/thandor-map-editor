#include "thandor/simulation.hpp"

#include <bit>
#include <fstream>
#include <iostream>
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
    if (text.find(token) == std::string::npos)
        throw std::runtime_error("missing source token: " + token);
}
SimulationWeaponProfile class9_profile(uint32_t selector_mask) {
    SimulationWeaponProfile weapon;
    weapon.runtime_class_id = 9U;
    weapon.shot_definition_id = 20U;
    weapon.reload_ticks = 400U;
    weapon.inter_shot_ticks = 53U;
    weapon.maximum_range_q12 = 0x7FFFFFFF;
    weapon.yaw_max_velocity_turn16 = 0x4000;
    weapon.yaw_acceleration_turn16 = 0x4000;
    weapon.pitch_max_velocity_turn16 = 0x4000;
    weapon.pitch_acceleration_turn16 = 0x4000;
    weapon.pitch_min_turn16 = -0x4000;
    weapon.pitch_max_turn16 = 0x4000;
    weapon.launch_attachment_count = static_cast<uint32_t>(std::popcount(selector_mask));
    weapon.launch_attachment_selector_mask = selector_mask;
    return weapon;
}
}

int main() try {
    const std::string source = THANDOR_SOURCE_DIR;
    const auto simulation = read_text(source + "/src/core/simulation.cpp");
    const auto editor = read_text(source + "/src/editor/editor_app.cpp");
    require_token(simulation, "available_launch_attachment_selector_mask");
    require_token(simulation, "state.attachment_reload_ticks[selector]=-1");
    require_token(simulation, "continues scanning the remaining slots in the same tick");
    require_token(editor, "available_launch_selector_mask");
    require_token(editor, "for(const auto& attachment:cached_sprite(node.sprite_path).lookup_records())");
    require_token(editor, "if(attachment.key_class().value()!=2U)continue;");
    require_token(editor, "*available_selector_mask|=1U<<attachment.key_index().value()");

    ShotDefinitionRecord shot;
    shot.trajectory_mode = 0U;
    shot.launch_speed_q12 = 0x1000;
    shot.projectile_lifetime_ticks = 64U;
    const Vec3f source_position{0.0F, 0.0F, 0.0F};
    const Vec3f target{5.0F, 0.0F, 0.0F};
    const auto aim = simulation_shot_launch_angles(source_position, target, shot);

    // Selector 0 exists in the class profile but the live linked model variant has no
    // matching (selector<<4)|2 local point. The original resolver returns carry set,
    // marks that timer -1, and immediately continues to selector 1 without consuming
    // the shared inter-shot gate or hiding selector 0.
    auto weapon = class9_profile(0x03U);
    SimulationActorState actor;
    SimulationWeaponState state;
    state.yaw.angle_turn16 = aim.yaw_turn16;
    state.pitch.angle_turn16 = aim.pitch_turn16;
    const auto result = simulation_tick_weapon(actor, state, weapon, shot, source_position,
                                                &target, true, 0x02U);
    if (!result.fired || result.fired_attachment_selector != 1U)
        throw std::runtime_error("resolver carry did not continue to the next live selector");
    if (state.attachment_reload_ticks[0] != -1)
        throw std::runtime_error("failed selector was not marked with the binary -1 sentinel");
    if (state.attachment_reload_ticks[1] != 400)
        throw std::runtime_error("successful selector did not receive its reload timer");
    if (state.inter_shot_ticks != 53U)
        throw std::runtime_error("successful selector did not consume the shared inter-shot gate");
    if ((state.launch_mesh_group_mask & 0x03U) != 0U)
        throw std::runtime_error("successful selector did not apply the class-9 prefix clear");

    // With no resolvable local point, no projectile is fired and no inter-shot state or
    // rack visibility is consumed. The -1 sentinel is retried on the next maintenance tick.
    weapon = class9_profile(0x01U);
    actor = {};
    state = {};
    state.yaw.angle_turn16 = aim.yaw_turn16;
    state.pitch.angle_turn16 = aim.pitch_turn16;
    const auto missing = simulation_tick_weapon(actor, state, weapon, shot, source_position,
                                                 &target, true, 0x00U);
    if (missing.fired || state.attachment_reload_ticks[0] != -1 ||
        state.inter_shot_ticks != 0U || state.launch_mesh_group_mask != 0xFFFFFFFFU)
        throw std::runtime_error("missing launch point consumed fire state or rack visibility");
    const auto retried = simulation_tick_weapon(actor, state, weapon, shot, source_position,
                                                 &target, true, 0x00U);
    if (retried.fired || state.attachment_reload_ticks[0] != -1)
        throw std::runtime_error("missing selector was not retried through the signed sentinel");

    std::cout << "G382_SEMANTIC_EXPORT_RESOLVER_CARRY_SELECTOR_RETRY PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G382_SEMANTIC_EXPORT_RESOLVER_CARRY_SELECTOR_RETRY FAIL: "
              << error.what() << "\n";
    return 1;
}
