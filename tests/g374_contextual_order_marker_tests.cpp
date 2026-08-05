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
    require(resolve_context_order(armed_unit, 2U, true) == ContextOrderKind::AttackPlacement,
            "enemy placement must take precedence over terrain movement");
    require(resolve_context_order(armed_unit, 1U, true) == ContextOrderKind::MoveToTerrain,
            "same-faction hover must not become an attack order");
    require(resolve_context_order(armed_unit, std::nullopt, true) == ContextOrderKind::MoveToTerrain,
            "movable unit terrain click must issue a drive order");

    const ContextOrderActorCapabilities tower{false, true, true, false, 3U};
    require(resolve_context_order(tower, std::nullopt, true) == ContextOrderKind::AttackTerrainPoint,
            "armed immobile building terrain click must issue a point-target order");
    require(resolve_context_order(tower, 4U, false) == ContextOrderKind::AttackPlacement,
            "tower must target a different-faction placement without a terrain fallback");

    const ContextOrderActorCapabilities scenery{false, false, false, false, 0U};
    require(resolve_context_order(scenery, 1U, true) == ContextOrderKind::None,
            "unarmed immobile scenery must not receive a contextual command");

    const ContextOrderActorCapabilities factory{false, false, true, true, 1U};
    require(resolve_context_order(factory, std::nullopt, true) == ContextOrderKind::SetProductionRallyPoint,
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
    require(app.find("placements[target->second].faction_index!=placements[attacker_index].faction_index") !=
                std::string::npos,
            "entity attack target must remain different-faction at runtime");
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
