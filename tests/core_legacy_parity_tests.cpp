#include "thandor/binary.hpp"
#include "thandor/simulation_lab_geometry.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
SimulationLabSeedEntity consumer(uint32_t flags) {
  SimulationLabSeedEntity s;
  s.army_asset_id = 1;
  s.faction_index = 1;
  s.building = true;
  s.actor.position = {3, -1, 0};
  s.runtime_model_class_id = 13;
  for (uint32_t i = 0; i < 3; ++i) {
    SimulationLabModelComponentSeed c;
    c.runtime_class_id = 13;
    c.maximum_integrity = 1000;
    c.definition_flags68 = i == 0 ? flags : 0U;
    c.retail_energy_demand_q4 = 16U << i;
    c.parent_component_index = i == 0 ? UINT32_MAX : i - 1;
    s.model_components.push_back(c);
  }
  return s;
}
} // namespace
int main() try {
  // Separate signed MDL +0x54 contact offset from +0x50 aim and +0x70 fog
  // height.
  std::vector<std::byte> mdl(0x480);
  write_u32(mdl, 0, fourcc("mdl"));
  write_u32(mdl, 4, mdl.size());
  write_u32(mdl, 0xB0, 1);
  write_u32(mdl, 0x200, 0x280);
  write_u32(mdl, 0x208, 1);
  write_u32(mdl, 0x24C, 11);
  write_u32(mdl, 0x250, 4096);
  write_u32(mdl, 0x254, uint32_t(-8192));
  write_u32(mdl, 0x270, 12288);
  write_u32(mdl, 0x264, 0x300);
  write_u32(mdl, 0x300, 0x80);
  write_u32(mdl, 0x30C, 0x4000);
  const std::string sprite = "spr/test.spr";
  for (size_t i = 0; i < sprite.size(); ++i)
    write_u16(mdl, 0x338 + i * 2, uint16_t(sprite[i]));
  const auto definitions = parse_model_visual_records(mdl, "contact.mdl");
  check(definitions.size() == 1 &&
            definitions[0].simulation.placement_height_offset_q12 == -8192 &&
            definitions[0].simulation.aim_height_offset_q12 == 4096 &&
            definitions[0].simulation.fog_reference_height_offset_q12 == 12288,
        "MDL contact offset aliases another field");
  const auto field = FieldGrid::create(16, 16, 0, 0);
  std::array<uint32_t, AiKnowledgeData::kParameterCount> ki{};
  SimulationLabConfig cfg;
  cfg.run_count = 1;
  cfg.duration_ticks = 130;
  SimulationLabScenarioRuntimeSeed scenario;
  scenario.enabled = true;
  scenario.assignable_faction_count = 5;
  scenario.active_faction_count = 2;
  scenario.human_faction_mask = 2;
  scenario.game_speed_q8 = 307;
  for (bool all_ai : {false, true}) {
    scenario.simulate_all_active_factions_as_ai = all_ai;
    SimulationLabExperiment lab;
    lab.reset(cfg, ki, {}, {}, {}, {}, {}, {}, scenario);
    lab.step(field, 90);
    const auto &r = lab.runs()[0];
    check(r.ai_controlled_faction_mask == (all_ai ? 6U : 4U),
          "explicit human mask/all-AI ownership differs");
    check((r.ai_workspace_diagnostics[1].last_planning_tick != 0) == all_ai,
          "human owner entered autonomous planning");
    check(r.ai_workspace_diagnostics[2].last_planning_tick == 86,
          "AI faction missed native planning slot");
    check(r.faction_economy[1].terrain_contribution_scale_q8 ==
              (all_ai ? 307U : 256U),
          "human terrain contribution speed differs");
    check(r.faction_economy[2].terrain_contribution_scale_q8 == 307,
          "AI game speed was not applied");
  }
  // Native economy.cpp collects root + direct attachments only when MDL flags68
  // bit80 is set. A grandchild remains excluded even if it has demand; toggling
  // the authored root flag reproduces the legacy trace's over-counting failure
  // independently of asset selection.
  std::array<SimulationLabFactionEconomySeed, 8> economy{};
  economy[1].derive_energy_allocation_from_components = true;
  economy[1].baseline_energy_generation_capacity_q4 = 4096;
  economy[1].tritium_current_q4 = 10000;
  for (uint32_t flags : {0U, 0x80U}) {
    const auto seed = consumer(flags);
    SimulationLabExperiment lab;
    lab.reset(cfg, ki, std::span(&seed, 1), economy);
    lab.step(field, 8);
    check(lab.runs()[0].faction_supplied_energy_demand_q4[1] ==
              (flags ? 48U : 16U),
          "authored attached energy scope differs");
  }
  // Captured fixed-point projection of world (3,-1): cell(4,2),
  // fractions(1255,200). Terrain and water are interpolated together for kind1,
  // separately for kind4.
  auto heights = FieldGrid::create(8, 8, 0, 0);
  heights.at(4, 2).terrain_height_q12 = -7;
  heights.at(5, 2).terrain_height_q12 = 11;
  heights.at(4, 3).terrain_height_q12 = 19;
  heights.at(5, 3).terrain_height_q12 = 1000;
  heights.at(4, 2).water_delta_q12 = -7;
  heights.at(5, 2).water_delta_q12 = 11;
  heights.at(4, 3).water_delta_q12 = 19;
  heights.at(5, 3).water_delta_q12 = 1000;
  const auto h = heights.sample_retail_surface_heights_q12(12288, -4096);
  check(h.valid && h.terrain == -1 && h.water_delta == -1 &&
            h.water_surface == -1 && h.top == -1,
        "native signed floor or combined interpolation differs");
  const Vec3f p{3, -1, 9};
  for (int kind : {0, 1, 2, 3, 4}) {
    const auto point =
        simulation_lab_factory_world_point(heights, p, 0, kind, 2, {}, 4096);
    const float expected =
        kind == 3
            ? 9.F
            : q12_to_float(-1) + 1.F + ((kind == 0 || kind == 2) ? 2.F : 0.F);
    check(point.z == expected, "MDL/SPR contact offset ownership differs");
  }
  heights.at(5, 3).flags_and_material |= 0x2000;
  check(!heights.sample_retail_surface_heights_q12(12288, -4096).valid,
        "flagged terrain was admitted");
  check(!heights.sample_retail_surface_heights_q12(-4096, 4096).valid,
        "outside terrain was clamped");
  std::cout << "V647_LEGACY_PARITY PASS ownership,planning,speed,energy "
               "scope,fixed contact heights\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
