#include "thandor/retail_contact.hpp"
#include "thandor/retail_random.hpp"
#include "thandor/simulation_lab.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool b, const char *s) {
  if (!b)
    throw std::runtime_error(s);
}
int main() try {
  uint32_t zero_seed = 0;
  check(retail_random_next(zero_seed) == 1655642U && zero_seed == 3434U,
        "native zero-seed RNG vector differs");
  auto field = FieldGrid::create(32, 32, 0, 0);
  const auto &cell = field.at(12, 12);
  SimulationLabConfig cfg;
  cfg.run_count = 1;
  cfg.duration_ticks = 1000;
  std::array<uint32_t, AiKnowledgeData::kParameterCount> ki{};
  SimulationLabScenarioRuntimeSeed scenario;
  scenario.enabled = true;
  scenario.active_faction_count = 1;
  scenario.human_faction_mask = 2;
  SimulationLabSeedEntity pad;
  pad.army_asset_id = 306;
  pad.faction_index = 1;
  pad.runtime_model_class_id = 22;
  pad.building = true;
  pad.production_storage_capacity = 1;
  pad.production_door_step_q12 = 0x80000;
  pad.airport_platform_step_q12 = 4096;
  pad.airport_platform_limit_q12 = 4096;
  pad.actor.position = {q12_to_float(cell.world_x_q12),
                        q12_to_float(cell.world_y_q12), 0};
  pad.root_model_maximum_integrity = 20000;
  SimulationLabProductionAsset plane;
  plane.army_asset_id = 240;
  plane.army_runtime_flags14_raw = 8;
  plane.build_duration_ticks = 3;
  plane.xenite_cost_q4 = 32;
  plane.entity_seed.army_asset_id = 240;
  plane.entity_seed.runtime_model_class_id = 21;
  plane.entity_seed.root_model_maximum_integrity = 1000;
  plane.entity_seed.aircraft_definition = {128, 8, 2, 3, 10, -4, 1};
  std::array<SimulationLabFactionEconomySeed, 8> economy{};
  economy[1].xenite_current_q4 = 1000;
  SimulationLabExperiment lab;
  lab.reset(cfg, ki, std::span(&pad, 1), economy, std::span(&plane, 1), {}, {},
            {}, scenario);
  auto &run = lab.runs()[0];
  run.secondary_purchase_requests.push_back({240, 1, 0, 0});
  lab.step(field, 1);
  check(run.entities[0].production_building &&
            run.entities[0].production_elapsed_ticks == 0,
        "new build advanced during its acceptance callback");
  lab.step(field, 3);
  check(run.entities.size() == 1 &&
            run.entities[0].production_linked_child_count == 1 &&
            run.metrics.class22_aircraft_spawned == 0,
        "storage completion created an unsolicited physical aircraft");
  check(run.entities[0].production_linked_child_asset_ids[0] == 240,
        "completed id not stored");
  for (int sortie = 0; sortie < 3; ++sortie) {
    const auto old_spawn = run.metrics.class22_aircraft_spawned;
    check(lab.queue_airport_launch(0, 0, 1, 0,
                                   float_to_q12(pad.actor.position.x + 2),
                                   float_to_q12(pad.actor.position.y)),
          "launch command rejected");
    lab.step(field, 1);
    check(run.metrics.class22_aircraft_spawned == old_spawn + 1,
          "launch did not create exactly one child");
    const auto child = run.entities.size() - 1;
    check(run.entities[child].aircraft.runtime.phase ==
              SimulationClass21LinkedChildPhase::Attached,
          "spawn cascaded into take-off");
    bool takeoff = false, approach = false, mission = false, returning = false,
         landing = false, retired = false;
    for (int i = 0; i < 160 && !retired; ++i) {
      const auto old = run.entities[child].aircraft.runtime.phase;
      const auto oldx = run.entities[child].actor.position.x;
      lab.step(field, 1);
      const auto &e = run.entities[child];
      const auto now = e.aircraft.runtime.phase;
      if (old == SimulationClass21LinkedChildPhase::Attached &&
          now == SimulationClass21LinkedChildPhase::OutboundRelease)
        check(e.actor.position.x == oldx,
              "parked to take-off transition also flew in the same callback");
      takeoff |= now == SimulationClass21LinkedChildPhase::OutboundRelease;
      approach |= now == SimulationClass21LinkedChildPhase::OffmapOutbound;
      mission |= now == SimulationClass21LinkedChildPhase::MissionPass;
      returning |= now == SimulationClass21LinkedChildPhase::OffmapReturn;
      landing |= now == SimulationClass21LinkedChildPhase::ReturningToPlatform;
      retired = e.runtime_removed;
    }
    check(takeoff && approach && mission && returning && landing && retired,
          "airport sortie did not traverse all six native phases");
    check(run.entities[0].production_linked_child_slot_mask == 0 &&
              run.entities[0].production_linked_child_asset_ids[0] == 240,
          "return did not release transient slot while retaining persistent "
          "inventory");
  }
  // A real cached AI army purchase executes on 74; producer payment waits for
  // owner tick75.
  {
    auto source = pad;
    source.army_asset_id = 300;
    source.runtime_model_class_id = 11;
    auto product = plane;
    product.army_asset_id = 331;
    product.army_runtime_flags14_raw = 0x10;
    auto ai = scenario;
    ai.human_faction_mask = 0;
    SimulationLabExperiment purchase;
    purchase.reset(cfg, ki, std::span(&source, 1), economy,
                   std::span(&product, 1), {}, {}, {}, ai);
    auto &r = purchase.runs()[0];
    r.tick = 73;
    r.ai_candidate_cache_reuse_state[1] = 16;
    r.ai_candidate_cache_count[1] = 1;
    r.ai_candidate_cache[1][0] = {331, 0x1001, 1};
    purchase.step(field, 1);
    check(!r.entities[0].production_building &&
              r.secondary_purchase_requests.size() == 1 &&
              r.faction_economy[1].xenite_current_q4 == 1000,
          "AI purchase was paid/started after its owner callback on the same "
          "tick");
    purchase.step(field, 1);
    check(r.entities[0].production_start_tick == 75 &&
              r.entities[0].production_elapsed_ticks == 0 &&
              r.faction_economy[1].xenite_current_q4 == 968,
          "queued AI purchase did not start on next owner callback");
    r.ai_candidate_cache_reuse_state[1] = 16;
    r.ai_candidate_cache_count[1] = 0;
    r.tick = 201;
    purchase.step(field, 1);
    check(r.ai_candidate_cache_reuse_state[1] == 0,
          "empty cached pass was rebuilt/saved indefinitely");
    r.ai_planning_enabled = false;
    r.tick = 329;
    const auto passes = r.metrics.planning_cycles;
    purchase.step(field, 1);
    check(r.metrics.planning_cycles == passes, "local AI-off flag ignored");
    r.reduced_update = true;
    r.entities[0].research_active = true;
    r.entities[0].research_elapsed_ticks = 0;
    purchase.step(field, 5);
    check(r.entities[0].research_elapsed_ticks == 0,
          "reduced session update advanced full callbacks");
  }
  // Owner interleaving: the newer producer pays before an older unpaid
  // researcher.
  {
    auto older = pad;
    older.runtime_model_class_id = 11;
    older.army_asset_id = 300;
    auto newer = older;
    newer.army_asset_id = 301;
    std::array seeds{older, newer};
    auto product = plane;
    product.army_asset_id = 331;
    product.army_runtime_flags14_raw = 0x10;
    product.xenite_cost_q4 = 48;
    auto money = economy;
    money[1].xenite_current_q4 = 48;
    SimulationLabTechnologyAsset tech;
    tech.technology_id = 6;
    SimulationLabExperiment order;
    order.reset(cfg, ki, seeds, money, std::span(&product, 1),
                std::span(&tech, 1), {}, {}, scenario);
    auto &r = order.runs()[0];
    r.entities[0].research_pending_payment = true;
    r.entities[0].research_xenite_due_q4 = 48;
    r.entities[0].research_technology_id = 6;
    r.secondary_purchase_requests.push_back({331, 1, 0, 0});
    order.step(field, 1);
    check(
        r.entities[1].production_building &&
            r.entities[0].research_pending_payment &&
            !r.entities[0].research_active,
        "global research pass stole funds before newer owner's class callback");
  }
  // Native take-off/landing counters decrement once per callback even for a
  // 5-tick batch; mission emission counters run their per-tick inner loop.
  SimulationClass21LinkedChildState flight;
  flight.phase = SimulationClass21LinkedChildPhase::OutboundRelease;
  flight.release_countdown = 2;
  flight.outbound_countdown = 3;
  simulation_class21_tick_outbound_release(flight, 5);
  check(flight.release_countdown == 1 && flight.outbound_countdown == 2,
        "take-off incorrectly consumed batch as countdown ticks");
  flight.phase = SimulationClass21LinkedChildPhase::ReturningToPlatform;
  flight.return_countdown = 2;
  SimulationClass22AirportState airport;
  simulation_class21_tick_return_to_platform(flight, airport, 5);
  check(flight.return_countdown == 1,
        "landing incorrectly consumed batch as countdown ticks");
  // A newly queued purchase on the economy wheel waits for the next owner
  // callback.
  SimulationLabSeedEntity factory = pad;
  factory.army_asset_id = 300;
  factory.runtime_model_class_id = 11;
  factory.production_storage_capacity = 0;
  auto building = plane;
  building.army_asset_id = 331;
  building.army_runtime_flags14_raw = 0x10;
  building.entity_seed.runtime_model_class_id = 14;
  SimulationLabExperiment timing;
  timing.reset(cfg, ki, std::span(&factory, 1), economy,
               std::span(&building, 1), {}, {}, {}, scenario);
  auto &tr = timing.runs()[0];
  tr.tick = 73;
  tr.secondary_purchase_requests.push_back({331, 1, 74, 0});
  // This is a queued external command available to the next callback, not an AI
  // pass injection.
  timing.step(field, 1);
  check(tr.entities[0].production_start_tick == 74 &&
            tr.entities[0].production_elapsed_ticks == 0,
        "owner queue acceptance/progress order differs");
  timing.step(field, 3);
  check(!tr.entities[0].production_building,
        "building did not complete at its native duration");
  tr.secondary_purchase_requests.push_back({331, 1, tr.tick, 0});
  timing.step(field, 1);
  check(tr.entities[0].production_building &&
            tr.entities[0].production_elapsed_ticks == 0,
        "completed factory restarted with progress in same callback");
  // Research completion clears active state and demand, preserving the native
  // +100 identifier.
  SimulationLabTechnologyAsset tech;
  tech.technology_id = tech.dependency_technology_id = 6;
  tech.research_duration_ticks = 1;
  SimulationLabExperiment research;
  research.reset(cfg, ki, std::span(&factory, 1), economy, {},
                 std::span(&tech, 1), {}, {}, scenario);
  auto &r = research.runs()[0];
  r.entities[0].research_active = true;
  r.entities[0].research_technology_id = 6;
  r.entities[0].research_duration_ticks = 1;
  research.step(field, 1);
  check(!r.entities[0].research_active &&
            r.entities[0].research_technology_id == 6 &&
            r.entities[0].research_elapsed_ticks == 1,
        "completed research state was erased instead of retained");
  // A visible Army contributes once for its registered world root. Attached
  // model runtimes are not separate owner-list entries. The owner is excluded.
  {
    auto sc = scenario;
    sc.active_faction_count = 2;
    sc.human_faction_mask = 6;
    SimulationLabExperiment pressure;
    pressure.reset(cfg, ki, std::span(&factory, 1), economy, {}, {}, {}, {},
                   sc);
    auto &pr = pressure.runs()[0];
    auto &e = pr.entities[0];
    auto nodes = std::make_shared<std::vector<ModelVisualNode>>(2);
    (*nodes)[0].shot_impact_class_index = 0;
    (*nodes)[1].shot_impact_class_index = 6;
    e.runtime_model_hierarchy = nodes;
    e.shot_impact_class_index = 0; // Classified root, not every attached MODEL.
    e.model_components.resize(2);
    e.model_components[0].source_model_node_index = 0;
    e.model_components[1].source_model_node_index = 1;
    e.model_primary_occupancy_mask = 1U << 5U;
    e.model_terrain_state_initialized = true; // This fixture supplies a retained classification.
    pr.tick = 1;
    pressure.step(field, 1);
    check(pr.faction_ai_pressure_values[2][0] == 257 &&
              pr.faction_ai_pressure_values[2][6] == 1 &&
              pr.faction_ai_pressure_values[1][0] == 1,
          "pressure counted attached runtimes or credited the owning faction");
    pr.entities[0].model_components[1].runtime_removed = true;
    pressure.step(field, 4);
    check(pr.faction_ai_pressure_values[2][6] == 2,
          "removed attached runtime still contributed to pressure");
  }
  std::cout
      << "V648_DEEP_PARITY PASS 3 complete reusable airport sorties, batched "
         "counters, production callback ordering, retained research state\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
