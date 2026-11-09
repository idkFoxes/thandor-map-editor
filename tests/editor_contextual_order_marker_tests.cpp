#include "thandor/context_order.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
} // namespace

int main() {
    using thandor::ContextOrderActorCapabilities;
    using thandor::ContextOrderKind;
    using thandor::resolve_context_order;

    const ContextOrderActorCapabilities armed_unit{true, true, false, false, 1U};
    require(resolve_context_order(armed_unit, 2U, true, true) == ContextOrderKind::AttackPlacement,
            "enemy placement must take precedence over terrain movement");
    require(resolve_context_order(armed_unit, 1U, true, true) == ContextOrderKind::None,
            "same-faction placement must block a drive destination beneath it");
    require(resolve_context_order(armed_unit, 0U, true, true) == ContextOrderKind::None,
            "neutral scenery/tree must block a drive destination beneath it without becoming an attack target");
    const ContextOrderActorCapabilities repair_unit{true, false, false, false, 1U, true};
    require(resolve_context_order(repair_unit, 1U, true, true) == ContextOrderKind::AttackPlacement,
            "recovery-capable unit must accept a same-faction placement target");
    require(resolve_context_order(repair_unit, 2U, true, true) == ContextOrderKind::None,
            "recovery-only unit must not treat a hostile placement as a support target");
    require(resolve_context_order(armed_unit, std::nullopt, false, true) == ContextOrderKind::MoveToTerrain,
            "movable unit terrain click must issue a drive order when no entity surface is under the pointer");

    const ContextOrderActorCapabilities tower{false, true, true, false, 3U};
    require(resolve_context_order(tower, std::nullopt, false, true) == ContextOrderKind::AttackTerrainPoint,
            "armed immobile building terrain click must issue a point-target order");
    require(resolve_context_order(tower, 4U, true, false) == ContextOrderKind::AttackPlacement,
            "tower must target a different-faction placement without a terrain fallback");

    const ContextOrderActorCapabilities scenery{false, false, false, false, 0U};
    require(resolve_context_order(scenery, 1U, true, true) == ContextOrderKind::None,
            "unarmed immobile scenery must not receive a contextual command");

    const ContextOrderActorCapabilities factory{false, false, true, true, 1U};
    require(resolve_context_order(factory, std::nullopt, false, true) == ContextOrderKind::SetProductionRallyPoint,
            "production building terrain click must set a drive/rally destination");

#ifdef THANDOR_SOURCE_DIR
    const std::filesystem::path source_root{THANDOR_SOURCE_DIR};
    const std::string app = read_text(source_root / "src/editor/editor_app.cpp");
    const std::string header = read_text(source_root / "src/editor/editor_app.hpp");
    const std::string cursor = read_text(source_root / "src/editor/game_cursor.hpp");

    require(app.find("issue_context_order_at_pointer") != std::string::npos,
            "editor must issue contextual pointer orders");
    require(app.find("right_pointer_dragged_") != std::string::npos &&
            app.find("Right-drag still pans the camera") != std::string::npos,
            "right-click orders must preserve right-drag camera pan");
    require(app.find("simulation_attack_placement_targets_") != std::string::npos &&
            app.find("simulation_attack_point_targets_") != std::string::npos,
            "entity and point targets must be distinct runtime states");
    require(app.find("pick_placement_hit") != std::string::npos &&
            app.find("nearest>terrain_hit->distance+kTerrainOcclusionTolerance") != std::string::npos,
            "placement picking must reject buildings/trees hidden behind the terrain surface");
    require(app.find("entity_hit.has_value(),hovered_terrain_point_.has_value()") != std::string::npos,
            "context orders must preserve a visible entity as a blocker instead of falling through to terrain");
    require(app.find("same_faction&&placement_has_recovery_weapon(attacker_index)") != std::string::npos &&
            app.find("!same_faction&&placement_has_hostile_weapon(attacker_index)") != std::string::npos &&
            app.find("entity_target_same_faction==recovery_weapon") != std::string::npos,
            "runtime target relation must keep hostile weapons on enemies and recovery weapons on allies");
    require(app.find("Entity targets are resolved again every 20 Hz step") != std::string::npos,
            "moving entity targets must be followed on the simulation cadence");
    require(app.find("Right-click: attack") != std::string::npos &&
            app.find("Right-click: drive") != std::string::npos &&
            app.find("production drive point") != std::string::npos,
            "hovered contextual actions must draw visible move, attack and rally markers");
    require(header.find("hovered_context_order_") != std::string::npos,
            "editor must retain the current hover command state for cursor and marker rendering");
    require(cursor.find("kMoveOrderValidFrame = 23U") != std::string::npos &&
            cursor.find("kAttackPlacementValidFrame = 25U") != std::string::npos,
            "stock cursor frame IDs must be explicit");
#endif

    std::cout << "G374 contextual move/attack order and enemy-hover marker PASS\n";
    return 0;
}
