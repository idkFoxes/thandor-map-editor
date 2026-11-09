#include "thandor/retail_terrain_stamp.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include <iostream>
#include <stdexcept>

using namespace thandor;
namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}

int main() try {
  SimulationLabConfig cfg;
  cfg.run_count = 1;
  cfg.duration_ticks = 9;
  std::array<uint32_t, AiKnowledgeData::kParameterCount> ki{};
  // A structure stamp leaves the old contact normals in place until a
  // configured lighting cycle reaches world-job phase 0.
  for (const bool cycle : {false, true}) {
    SimulationLabScenarioRuntimeSeed sc;
    sc.enabled = true;
    sc.active_faction_count = 1;
    sc.ai_planning_enabled = false;
    sc.terrain_lighting_cycle_active = cycle;
    sc.runtime_catalog = std::make_shared<SimulationLabRuntimeCatalog>();
    auto terrain = FieldGrid::create(10, 10, 0, 0);
    SimulationLabExperiment terrain_lab;
    terrain_lab.reset(cfg, ki, {}, {}, {}, {}, {}, {}, sc);
    terrain_lab.step(terrain, 1);
    auto &state = terrain_lab.runs()[0];
    auto &live = *state.runtime_field;
    const auto cell = live.at(4, 4);
    const auto before = live.at(5, 4).loaded_terrain_normal_angles;
    state.runtime_normals_dirty = retail_stamp_structure_height(
        live, cell.world_x_q12, cell.world_y_q12, 500, 0);
    check(state.runtime_normals_dirty, "terrain normal fixture did not stamp a slope");
    terrain_lab.step(terrain, 6);
    check(live.at(5, 4).loaded_terrain_normal_angles == before,
          "contact normal refreshed before lighting phase 0");
    terrain_lab.step(terrain, 1);
    check((live.at(5, 4).loaded_terrain_normal_angles != before) == cycle,
          "lighting phase 0 refreshed contact normals for the wrong level settings");
  }
  auto terrain = FieldGrid::create(10, 10, 0, 0);
  const auto center = terrain.at(4, 4);
  terrain.capture_runtime_normals();
  terrain.at(4, 4).terrain_height_q12 = 500;
  terrain.apply_runtime_terrain_impact(center.world_x_q12, center.world_y_q12, 4096, 0, 18);
  check(terrain.at(4, 4).terrain_height_q12 == 500 && terrain.at(4, 4).material() == 18,
        "zero-amplitude impact failed to paint the terrain");
  check(terrain.at(5, 4).loaded_terrain_normal_angles != 0x40000000U,
        "impact failed to refresh the cached normal in its bounding rectangle");
  terrain.at(4, 4).water_delta_q12 = 1000;
  terrain.apply_runtime_terrain_impact(center.world_x_q12, center.world_y_q12, 1000, 200, -1);
  check(terrain.at(4, 4).terrain_height_q12 == 300 && terrain.at(4, 4).water_delta_q12 == 1200 &&
        terrain.at(4, 4).material() == 18, "crater changed water level or repainted sentinel material");
  std::cout << "Terrain lighting refresh follows the native world-job phase\n";
} catch (const std::exception& e) {
  std::cerr << e.what() << '\n';
  return 1;
}
