#define main v648_unused_stock_replay_main
#include "simlab_stock_replay.cpp"
#undef main
#include <cmath>
int main(int argc, char **argv) try {
  if (argc != 2)
    throw std::runtime_error("usage: stock_airport_probe GAME_PCK_ROOT");
  const auto catalog = AssetCatalog::scan_archive_root(argv[1]);
  const auto runtime = simulation_lab_prepare_runtime_catalog(catalog);
  const auto field = FieldGrid::create(96, 80, 0, 0);
  const auto &cell = field.at(40, 40);
  std::array<uint32_t, AiKnowledgeData::kParameterCount> ki{};
  SimulationLabConfig cfg;
  cfg.run_count = 1;
  cfg.duration_ticks = 12000;
  uint32_t pads = 0, sorties = 0;
  for (bool upgraded : {false, true}) {
    FactionTechnologyMask256 mask{};
    if (upgraded)
      mask.fill(UINT32_MAX);
    std::array<FactionTechnologyMask256, 8> masks{};
    masks[1] = mask;
    for (const auto &preset : catalog.army_presets()) {
      if (preset.runtime_model_class_id != 22)
        continue;
      auto pad = seed_for(preset, catalog, mask);
      pad.faction_index = 1;
      pad.source_placement_index = SIZE_MAX;
      pad.actor.position = {q12_to_float(cell.world_x_q12),
                            q12_to_float(cell.world_y_q12), 0};
      std::vector<SimulationLabProductionAsset> products;
      for (auto id : kSimulationClass22LinkedArmyAssetIds) {
        const auto *p = catalog.army_preset(id);
        if (!p)
          throw std::runtime_error("aircraft missing");
        SimulationLabProductionAsset a;
        a.army_asset_id = id;
        a.army_runtime_flags14_raw = p->army_runtime_flags14_raw;
        a.xenite_cost_q4 = p->retail_xenite_cost_q4;
        a.build_duration_ticks = p->retail_build_time_q5;
        a.energy_demand_q4 = p->retail_build_energy_q4;
        a.entity_seed = seed_for(*p, catalog, mask);
        products.push_back(a);
      }
      std::array<SimulationLabFactionEconomySeed, 8> economy{};
      economy[1].xenite_current_q4 = 10000000;
      SimulationLabScenarioRuntimeSeed scenario;
      scenario.enabled = true;
      scenario.active_faction_count = 1;
      scenario.human_faction_mask = 2;
      scenario.runtime_catalog = runtime;
      SimulationLabExperiment lab;
      lab.reset(cfg, ki, std::span(&pad, 1), economy, products, {}, masks, {},
                scenario);
      auto &r = lab.runs()[0];
      for (auto id : kSimulationClass22LinkedArmyAssetIds)
        r.secondary_purchase_requests.push_back({id, 1, 0, 0});
      while (r.metrics.production_jobs_completed < 3 && r.tick < 4000)
        lab.step(field, 1);
      if (r.entities.size() != 1 || r.metrics.class22_assets_stored != 3 ||
          r.metrics.class22_aircraft_spawned)
        throw std::runtime_error("stock completion/storage mismatch");
      uint32_t states = 0;
      size_t peak_active = 0;
      for (int repeat = 0; repeat < 2; ++repeat) {
        if (!lab.queue_airport_launch(0, 0, 7, 0,
                                      float_to_q12(pad.actor.position.x + 2),
                                      float_to_q12(pad.actor.position.y)))
          throw std::runtime_error("stock launch rejected");
        const auto target = r.metrics.class22_aircraft_spawned + 3;
        bool done = false;
        while (r.tick < 11000 && !done) {
          lab.step(field, 1);
          size_t active = 0;
          for (const auto &e : r.entities)
            if (e.runtime_model_class_id == 21 && e.aircraft.initialized &&
                !e.runtime_removed) {
              ++active;
              states |= 1U << uint32_t(e.aircraft.runtime.phase);
              if (!std::isfinite(e.actor.position.x) ||
                  !std::isfinite(e.actor.position.y))
                throw std::runtime_error("nonfinite aircraft");
            }
          peak_active = std::max(peak_active, active);
          done = r.metrics.class22_aircraft_spawned == target && active == 0 &&
                 r.entities[0].production_linked_child_slot_mask == 0;
        }
        if (!done)
          throw std::runtime_error("stock sortie timed out");
      }
      if ((states & 0x7eU) != 0x7eU)
        throw std::runtime_error("stock phases missing");
      std::cout << "AIRPORT arm=" << preset.registry_id
                << " upgraded=" << upgraded
                << " completed=" << r.metrics.production_jobs_completed
                << " stored=" << r.entities[0].production_linked_child_count
                << " spawned=" << r.metrics.class22_aircraft_spawned
                << " shots=" << r.metrics.weapon_shots_fired
                << " phases=" << states << " peak_active=" << peak_active
                << " tick=" << r.tick << '\n';
      ++pads;
      sorties += r.metrics.class22_aircraft_spawned;
    }
  }
  if (!pads)
    throw std::runtime_error("no pads found");
  std::cout << "V648_STOCK_AIRPORT PASS pads_and_masks=" << pads
            << " complete_sorties=" << sorties
            << "; controlled launch commands, abundant resources, no AI "
               "sortie-selection claim\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
