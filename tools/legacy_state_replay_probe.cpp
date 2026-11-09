#define main stock_replay_main_v647
#include "simlab_stock_replay.cpp"
#undef main
#include <bit>
#include <cmath>
#include <fstream>
int main(int argc, char **argv) try {
  if (argc < 3 || argc > 4)
    throw std::runtime_error(
        "usage: legacy_state_probe PCK_ROOT INITIAL_WORDS.tsv [ticks=130]");
  auto catalog = AssetCatalog::scan_archive_root(argv[1]);
  auto group = std::find_if(
      catalog.map_groups().begin(), catalog.map_groups().end(),
      [](const auto &g) { return g.level_path == "level/lucas03.lev"; });
  if (group == catalog.map_groups().end())
    throw std::runtime_error("lucas03 map missing");
  auto doc = MapDocument::open(catalog, *group);
  const auto &field = doc.field();
  const auto &level = doc.level();
  const auto masks = doc.faction_technology_masks();
  auto runtime = simulation_lab_prepare_runtime_catalog(catalog);
  std::vector<std::array<uint32_t, 41>> native;
  std::array<uint32_t, 41> w;
  std::ifstream input(argv[2]);
  if (!input)
    throw std::runtime_error("cannot open initial actor interchange");
  while (input >> w[0]) {
    for (size_t i = 1; i < w.size(); ++i)
      if (!(input >> w[i]))
        throw std::runtime_error("truncated actor row");
    native.push_back(w);
  }
  if (!input.eof() || native.size() != level.placements().size())
    throw std::runtime_error("invalid or incomplete initial actor interchange");
  std::vector<bool> seen(native.size() + 1);
  for (const auto &row : native) {
    const auto id = row[0];
    if (id == 0 || id > native.size() || seen[id])
      throw std::runtime_error("invalid or duplicate initial model identity");
    seen[id] = true;
    const auto &p = level.placements()[native.size() - id];
    if (std::bit_cast<int32_t>(row[4]) != p.world_y_q12 ||
        std::bit_cast<int32_t>(row[5]) != p.world_x_q12)
      throw std::runtime_error(
          "initial actor does not match reversed lucas03 placement");
  }
  std::cout << "INITIAL_MAP checked=" << native.size()
            << " exact_xy=" << native.size() << " reversed=1\n";
  std::vector<SimulationLabSeedEntity> seeds;
  std::vector<SimulationLabProductionAsset> products;
  for (size_t i = 0; i < level.placements().size(); ++i) {
    const auto &p = level.placements()[i];
    auto *preset = catalog.army_preset(p.army_asset_id);
    if (!preset || !p.faction_index || p.faction_index >= 8)
      continue;
    if (preset->browser_kind != PlacementBrowserKind::Building &&
        preset->browser_kind != PlacementBrowserKind::Unit &&
        preset->runtime_model_class_id != 18)
      continue;
    auto s = seed_for(*preset, catalog, masks[p.faction_index]);
    s.source_placement_index = i;
    s.faction_index = p.faction_index;
    s.actor.position = {
        p.render_x(), p.render_y(),
        field.sample_terrain(p.render_x(), p.render_y()).height};
    s.actor.body_yaw_turn16 = p.orientation_turn16 & 65535;
    seeds.push_back(s);
  }
  for (const auto &p : catalog.army_presets()) {
    if (!p.registry_id)
      continue;
    SimulationLabProductionAsset a;
    a.army_asset_id = p.registry_id;
    a.army_runtime_flags14_raw = p.army_runtime_flags14_raw;
    a.ai_definition_class_value70 = p.ai_definition_class_value70;
    a.ai_definition_class_value74 = p.ai_definition_class_value74;
    a.ai_definition_class_value78 = p.ai_definition_class_value78;
    a.runtime_class_specific_c0_raw = p.runtime_class_specific_c0_raw;
    a.xenite_cost_q4 = p.retail_xenite_cost_q4;
    a.build_duration_ticks = p.retail_build_time_q5;
    a.energy_demand_q4 = p.retail_build_energy_q4;
    a.placement_effect_id = p.construction_destruction_effect_id;
    a.entity_seed = seed_for(p, catalog, masks[0]);
    simulation_lab_prepare_ai_product(p, catalog, a);
    products.push_back(a);
  }
  std::array<SimulationLabFactionEconomySeed, 8> economy{};
  for (uint32_t f = 1; f < 8; ++f) {
    auto &e = economy[f];
    e.xenite_current_q4 = level.faction_xenite_q4(f);
    e.tritium_current_q4 = level.faction_tritium_q4(f);
    e.baseline_xenite_storage_limit_q4 = e.baseline_tritium_storage_limit_q4 =
        4000;
    e.baseline_energy_supply_q4 = e.baseline_energy_generation_capacity_q4 =
        640;
    e.derive_storage_limits_from_components =
        e.derive_energy_allocation_from_components = true;
    e.terrain_contribution_scale_q8 = 256;
  }
  SimulationLabScenarioRuntimeSeed scenario;
  scenario.enabled = true;
  scenario.assignable_faction_count = level.frontend_assignable_faction_count();
  scenario.human_faction_mask = 2;
  scenario.game_speed_q8 = 307;
  scenario.active_faction_count = level.frontend_active_faction_count();
  scenario.runtime_catalog = runtime;
  scenario.relation_ui_flags = level.relation_ui_flags();
  scenario.relation_state8_faction_group_masks =
      level.relation_state8_faction_group_masks();
  scenario.relation_state4_faction_group_masks =
      level.relation_state4_faction_group_masks();
  for (size_t i = 0; i < scenario.scheduled_conditions.size(); ++i)
    scenario.scheduled_conditions[i] =
        level.scheduled_condition(static_cast<uint32_t>(i));
  for (size_t i = 0; i < scenario.end_condition_triggers.size(); ++i)
    scenario.end_condition_triggers[i] =
        level.end_condition_trigger(static_cast<uint32_t>(i));
  scenario.static_navigation_blockers =
      simulation_lab_prepare_static_navigation_blockers(catalog, level);
  auto environment =
      simulation_lab_prepare_environment(catalog, level, field, *runtime);
  scenario.static_placement_owners = environment.placement_owners;
  scenario.static_shot_hulls = environment.collision_hulls;
  scenario.environmental_emitters = environment.emitters;
  auto knowledge = AiKnowledgeData::parse(catalog.load("engine/ki.dat"));
  auto config = simulation_lab_loaded_baseline_config(knowledge.parameters());
  config.run_count = 1;
  config.base_seed = 2638864529U;
  config.duration_ticks = argc > 3 ? std::stoul(argv[3]) : 130;
  SimulationLabExperiment experiment;
  auto tech = simulation_lab_prepare_technologies(catalog, &level);
  experiment.reset(config, knowledge.parameters(), seeds, economy, products,
                   tech, masks, {}, scenario);
  experiment.prepare_navigation_terrain(field);
  auto print = [&]() {
    const auto &r = experiment.runs().front();
    std::cout << "FRAME tick=" << r.tick
              << " ai_mask=" << r.ai_controlled_faction_mask
              << " entities=" << r.entities.size() << '\n';
    for (uint32_t f = 1; f <= r.active_faction_count; ++f) {
      const auto &e = r.faction_economy[f];
      std::cout << "FACT f=" << f << " xenite=" << e.xenite_current_q4
                << " cap=" << e.xenite_storage_limit_q4.value_or(0)
                << " tritium=" << e.tritium_current_q4
                << " supplied=" << r.faction_supplied_energy_demand_q4[f]
                << " unpowered=" << r.faction_unpowered_energy_demand_q4[f]
                << " scale=" << e.terrain_contribution_scale_q8 << " planning="
                << r.ai_workspace_diagnostics[f].last_planning_tick << " mask=";
      for (auto x : r.faction_technology_masks[f])
        std::cout << x << ',';
      std::cout << '\n';
    }
  };
  print();
  size_t checked = 0, badhealth = 0, badxy = 0, badz = 0, badyaw = 0,
         badattachments = 0, exactz = 0;
  int64_t maxz = 0;
  for (const auto &e : experiment.runs()[0].entities) {
    auto id = level.placements().size() - e.source_placement_index;
    auto row = std::find_if(native.begin(), native.end(),
                            [&](const auto &n) { return n[0] == id; });
    if (row == native.end())
      continue;
    ++checked;
    auto q = [](float v) {
      return static_cast<int32_t>(std::llround(v * 4096));
    };
    const auto poses = simulation_lab_current_model_poses(e, field, *runtime);
    const auto pos = poses.empty() ? e.actor.position : poses[0].position;
    bool xy = q(pos.x) == std::bit_cast<int32_t>((*row)[4]) &&
              q(pos.y) == std::bit_cast<int32_t>((*row)[5]);
    const auto dz =
        std::abs(int64_t(q(pos.z)) - std::bit_cast<int32_t>((*row)[6]));
    maxz = std::max(maxz, dz);
    exactz += dz == 0;
    bool z = dz <= 2;
    bool health = e.root_model_integrity == std::bit_cast<int32_t>((*row)[19]);
    bool yaw = e.actor.body_yaw_turn16 == (*row)[9];
    size_t attachments = 0;
    for (const auto &n : e.model_components)
      if (n.parent_component_index == e.root_model_component_index)
        ++attachments;
    badhealth += !health;
    badxy += !xy;
    badz += !z;
    badyaw += !yaw;
    badattachments += attachments != (*row)[1];
    if (!xy || !z || !health || !yaw || attachments != (*row)[1])
      std::cout << "ROOT id=" << id << " arm=" << e.army_asset_id
                << " class=" << e.runtime_model_class_id
                << " f=" << e.faction_index
                << " health=" << e.root_model_integrity
                << " native_health=" << (*row)[19] << " x=" << q(pos.x)
                << " nx=" << std::bit_cast<int32_t>((*row)[4])
                << " y=" << q(pos.y)
                << " ny=" << std::bit_cast<int32_t>((*row)[5])
                << " z=" << q(pos.z)
                << " nz=" << std::bit_cast<int32_t>((*row)[6])
                << " yaw=" << e.actor.body_yaw_turn16 << " nyaw=" << (*row)[9]
                << " attachments=" << attachments << " nattach=" << (*row)[1]
                << '\n';
  }
  std::cout << "INITIAL_CHECK checked=" << checked
            << " health_diff=" << badhealth << " xy_diff=" << badxy
            << " z_diff=" << badz << " yaw_diff=" << badyaw
            << " z_exact=" << exactz << " z_max_error_q12=" << maxz
            << " attachment_diff=" << badattachments << '\n';
  for (uint32_t i = 1; i <= config.duration_ticks; ++i) {
    experiment.step(field, 1);
    if (i == 8 || i == 16 || i == 66 || i == 74 || i == 82 || i == 90 ||
        i == 98 || i == 106 || i == 114 || i == 122 || i == 130 || i == 256 ||
        i == 1000 || i == config.duration_ticks)
      print();
  }
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
