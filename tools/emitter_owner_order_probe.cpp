#include "thandor/retail_relation_prediction.hpp"
#include "thandor/simulation_lab_site_placement.hpp"
#include "thandor/simulation_lab_ai_workspaces.hpp"
#define main stock_replay_main_v656
#include "simlab_stock_replay.cpp"
#undef main
#include <bit>
#include <cmath>
#include <fstream>
#include <tuple>
int main(int argc, char **argv) try {
  if (argc < 3 || argc > 8)
    throw std::runtime_error(
        "usage: legacy_state_probe PCK_ROOT INITIAL_WORDS.tsv [ticks=130] "
        "[callback_batch=1|observed] [legacy-hook|bootstrap-only] [INFERRED_QUEUE_INPUTS.tsv] [INFERRED_WORLD_INPUTS.tsv]");
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
    std::cout << "CATALOG_BUILD arm=" << a.army_asset_id
                << " cost=" << a.xenite_cost_q4
                << " duration=" << a.build_duration_ticks
                << " root_hp=" << p.root_maximum_integrity
                << " root_mdl=" << p.model_definition_id
                << " model_nodes=" << p.resolved_model_hierarchy.size()
                << " class_c0=" << p.runtime_class_specific_c0_raw << '\n';
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
  const bool legacy_hook=argc>5 && std::string(argv[5])=="legacy-hook";
  const bool bootstrap=legacy_hook||(argc>5 && std::string(argv[5])=="bootstrap-only");
  if(argc>5 && !bootstrap)throw std::runtime_error("last argument must be legacy-hook or bootstrap-only");
  if(legacy_hook)scenario.ai_planner_dispatch_mask=4U; // Valid AIPH faction2; other calls contain0x53C811.

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
  if(bootstrap) {
    // Reconstruct the first emitter callback from its admitted post-update seed.
    // Native static asset paths consume40 draws; checked independently at later ticks.
    // This is a one-time bootstrap reconstruction, never per-tick seed injection.
    constexpr uint32_t inverse1089=0xb44e0bc1U;
    for(unsigned i=0;i<40;++i)config.base_seed=(config.base_seed-3434U)*inverse1089;
  } // Admitted post-setup sample; emitter timers are not in V1.
  config.duration_ticks = argc > 3 ? std::stoul(argv[3]) : 130;
  const bool observed=argc>4 && std::string(argv[4])=="observed";
  const auto callback_batch = argc>4 && !observed ? std::stoul(argv[4]) : 1;
  if (callback_batch < 1 || callback_batch > 5)
    throw std::runtime_error("callback_batch must be in 1..5");
  config.runtime_callback_step_ticks = static_cast<uint32_t>(callback_batch);
  std::cout << "PROBE callback_batch=" << config.runtime_callback_step_ticks
            << " explicit_batch=" << (argc > 4)
            << "; V1 does not capture this value\n";

  SimulationLabExperiment experiment;
  auto tech = simulation_lab_prepare_technologies(catalog, &level);
  experiment.reset(config, knowledge.parameters(), seeds, economy, products,
                   tech, masks, {}, scenario);
  experiment.prepare_navigation_terrain(field);
  if(bootstrap)experiment.runs()[0].tick=1;
  std::cout<<"FORENSIC legacy_hook="<<legacy_hook<<" native_ai_mask="<<experiment.runs()[0].ai_controlled_faction_mask
    <<" dispatch_mask="<<(legacy_hook?4U:0xFEU)<<" bootstrap_reconstruction="<<bootstrap
    <<"; legacy-hook reproduces a recorder-corrupted match, not uninstrumented native scheduling\n";
  auto print = [&]() {
    const auto &r = experiment.runs().front();
    std::cout << "FRAME tick=" << r.tick
              << " rng=" << r.rng_state
              << " ai_mask=" << r.ai_controlled_faction_mask
              << " entities=" << r.entities.size() << '\n';
    for (uint32_t f = 1; f <= r.active_faction_count; ++f) {
      const auto &e = r.faction_economy[f];
      std::cout << "FACT f=" << f << " xenite=" << e.xenite_current_q4
                << " cap=" << e.xenite_storage_limit_q4.value_or(0)
                << " tritium=" << e.tritium_current_q4
                << " supplied=" << r.faction_supplied_energy_demand_q4[f]
                << " unpowered=" << r.faction_unpowered_energy_demand_q4[f]
                << " scale=" << e.terrain_contribution_scale_q8
                << " max_pressure=" << r.faction_maximum_ai_pressure[f]
                << " planning="
                << r.ai_workspace_diagnostics[f].last_planning_tick << " mask=";
      for (auto x : r.faction_technology_masks[f])
        std::cout << x << ',';
      std::cout << '\n';
    }
  };
  std::cout<<"SCHEDULE observed_build_counter_intervals="<<observed<<"; startup and unobserved intervals are assumed, not replayed controls\n";
  print();
  size_t checked = 0, badhealth = 0, badxy = 0, badz = 0, badyaw = 0,
         badattachments = 0, exactz = 0;
  int64_t maxz = 0;
  for (const auto &e : experiment.runs()[0].entities) {
    auto id = level.placements().size() - e.source_placement_index;
    std::cout << "INITIAL_MODEL id=" << id << " arm=" << e.army_asset_id
              << " faction=" << e.faction_index
              << " class=" << e.runtime_model_class_id << '\n';
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
  struct InferredQueueInput {uint32_t tick,faction,asset;};
  std::vector<InferredQueueInput> inferred_inputs;size_t input_index=0;
  if(argc>6){
    std::ifstream controls(argv[6]);if(!controls)throw std::runtime_error("cannot open inferred queue inputs");
    InferredQueueInput event{};
    while(controls>>event.tick>>event.faction>>event.asset){
      if(event.tick<2||event.faction==0||event.faction>=8||!catalog.army_preset(event.asset)||
         (!inferred_inputs.empty()&&event.tick<inferred_inputs.back().tick))
        throw std::runtime_error("invalid inferred queue input");
      inferred_inputs.push_back(event);
    }
    if(!controls.eof())throw std::runtime_error("malformed inferred queue input");
  }
  std::cout<<"INFERRED_INPUT_POLICY count="<<inferred_inputs.size()
    <<"; explicit counterfactual inputs inferred from admitted production state, not captured UCMD commands\n";
  struct WorldInput {uint32_t tick;std::string operation;SimulationLabPlayerCommand command;uint32_t model;};
  std::vector<WorldInput> world_inputs;size_t world_index=0;
  if(argc>7){
    std::ifstream controls(argv[7]);if(!controls)throw std::runtime_error("cannot open inferred world inputs");
    WorldInput event;
    while(controls>>event.tick>>event.operation>>event.command.player_index>>event.command.faction_index
         >>event.command.army_asset_id>>event.model>>event.command.world_x_q12>>event.command.world_y_q12>>event.command.heading_turn16){
      if(event.operation=="take")event.command.kind=SimulationLabPlayerCommandKind::TakeStock;
      else if(event.operation=="return")event.command.kind=SimulationLabPlayerCommandKind::ReturnStock;
      else if(event.operation=="place")event.command.kind=SimulationLabPlayerCommandKind::PlaceStock;
      else if(event.operation=="move")event.command.kind=SimulationLabPlayerCommandKind::MovePioneer;
      else throw std::runtime_error("unknown world input operation");
      if(event.tick<2||(!world_inputs.empty()&&event.tick<world_inputs.back().tick))throw std::runtime_error("invalid world input tick");
      world_inputs.push_back(event);
    }
    if(!controls.eof())throw std::runtime_error("malformed inferred world input");
  }
  std::cout<<"INFERRED_WORLD_POLICY count="<<world_inputs.size()
    <<"; counterfactual commands from outcome constraints, not recovered UCMD packets; no sampled pose or budget injection\n";
  std::optional<std::tuple<uint32_t,bool,uint32_t,bool,bool>> previous_model46;
  for (uint32_t i = bootstrap?2U:1U; i <= config.duration_ticks; ++i) {
    if(observed) {
      if(i==367)experiment.set_runtime_callback_step_ticks(2);
      if(i==371)experiment.set_runtime_callback_step_ticks(3);
      if(i==375)experiment.set_runtime_callback_step_ticks(4);
      if(i==377)experiment.set_runtime_callback_step_ticks(5);
    }
    while(input_index<inferred_inputs.size()&&inferred_inputs[input_index].tick==i){
      const auto event=inferred_inputs[input_index++];
      experiment.runs()[0].secondary_purchase_requests.push_back({event.asset,event.faction,i-1,0});
      std::cout<<"INFERRED_QUEUE tick="<<i<<" faction="<<event.faction<<" asset="<<event.asset<<'\n';
    }
    while(world_index<world_inputs.size()&&world_inputs[world_index].tick==i){
      auto event=world_inputs[world_index++];
      if(event.operation=="move"){
        const auto& entities=experiment.runs()[0].entities;
        auto e=std::find_if(entities.begin(),entities.end(),[&](const auto& e){return e.source_placement_index==native.size()-event.model;});
        if(e==entities.end())throw std::runtime_error("inferred move model absent");
        event.command.entity_index=static_cast<size_t>(e-entities.begin());
      }
      if(!experiment.queue_player_command(0,event.command))throw std::runtime_error("inferred world input could not be queued");
      std::cout<<"INFERRED_WORLD tick="<<i<<" operation="<<event.operation<<" faction="<<event.command.faction_index<<'\n';
    }
    const auto before_ambient=experiment.runs()[0].environmental_emitter_states;
    std::vector<std::vector<std::pair<uint32_t,uint32_t>>> before_components;
    for(const auto& entity:experiment.runs()[0].entities){
      auto& row=before_components.emplace_back();
      for(const auto& c:entity.model_components)row.push_back({c.timed_effect_countdown,c.timed_effect_point_index});
    }
    const auto seed_before=experiment.runs()[0].rng_state;
    experiment.step(field, 1);
    const auto &state = experiment.runs()[0];
    for(const auto& result:state.player_command_results){
      std::cout<<"PLAYER_RESULT tick="<<i<<" kind="<<uint32_t(result.command.kind)<<" accepted="<<result.accepted;
      if(result.placed_point)std::cout<<" x_q12="<<result.placed_point->x<<" y_q12="<<result.placed_point->y;
      std::cout<<'\n';
    }
    for(const auto& entity:state.entities)if(entity.source_placement_index==native.size()-437U||entity.source_placement_index==native.size()-49U)
      std::cout<<"HUMAN_MODEL tick="<<i<<" model="<<native.size()-entity.source_placement_index<<" arm="<<entity.army_asset_id
        <<" x_q12="<<float_to_q12(entity.actor.position.x)<<" y_q12="<<float_to_q12(entity.actor.position.y)
        <<" yaw="<<entity.actor.body_yaw_turn16<<" selected="<<entity.production_asset_id<<" building="<<entity.production_building
        <<" elapsed="<<entity.production_elapsed_ticks<<" duration="<<entity.production_duration_ticks<<'\n';
    if(bootstrap && i==2 && state.rng_state!=2638864529U)
      throw std::runtime_error("asset callbacks do not reproduce the admitted bootstrap checkpoint");
    if(seed_before!=state.rng_state)std::cout<<"RNG_STEP tick="<<i<<" before="<<seed_before
        <<" after="<<state.rng_state<<" faction_slot="<<((i>>3U)&7U)<<" economy_phase="<<((i>>6U)&1U)<<'\n';
    if(i>=330 && i<=510){
      for(size_t n=0;n<state.environmental_emitter_states.size()&&n<before_ambient.size();++n){
        const auto& after=state.environmental_emitter_states[n];
        if(after.point_index!=before_ambient[n].point_index){
          const auto& source=scenario.environmental_emitters[n];
          std::cout<<"EMITTER tick="<<i<<" kind=static index="<<n<<" mdl="<<source.model_definition_id
            <<" x="<<source.pose.position.x<<" y="<<source.pose.position.y
            <<" before="<<before_ambient[n].countdown<<" after="<<after.countdown<<'\n';
        }
      }
      for(size_t n=0;n<state.entities.size()&&n<before_components.size();++n){
        const auto& entity=state.entities[n];
        for(size_t k=0;k<entity.model_components.size()&&k<before_components[n].size();++k){
          const auto& c=entity.model_components[k];
          if(c.timed_effect_point_index!=before_components[n][k].second)
            std::cout<<"EMITTER tick="<<i<<" kind=live index="<<n<<" placement="<<entity.source_placement_index
              <<" component="<<k<<" mdl="<<c.model_definition_id
              <<" before="<<before_components[n][k].first<<" after="<<c.timed_effect_countdown<<'\n';
        }
      }
    }
    const uint32_t selected_faction=(i>>3U)&7U;
    const auto& workspace_info=state.ai_workspace_diagnostics[selected_faction];
    if(workspace_info.last_planning_tick==i) {
        const auto owners=simulation_lab_site_owners(state,field,*runtime,scenario.static_placement_owners);
        const auto own=simulation_lab_ai_own_workspaces(state,selected_faction);
        auto predicted=state.scheduled_conditions;
        for(uint32_t other=7;other>0;--other)if(other!=selected_faction && state.faction_lifecycle_states[other]==1) {
            const auto relation=(state.faction_packed_relation_states[selected_faction]>>(other*4U))&15U;
            const bool blocked=retail_relation_pair_blocked(other,selected_faction,state.relation_ui_flags,
                state.faction_lifecycle_states,state.faction_packed_relation_states,predicted,state.end_condition_triggers);
            const bool draw=!blocked || relation==3 || relation==6 || relation==10;
            std::cout<<"RELATION_GUARD_POST_PASS tick="<<i<<" faction="<<selected_faction<<" other="<<other
                <<" flags="<<state.relation_ui_flags<<" state="<<relation<<" blocked="<<blocked
                <<" draw_if_guard_only="<<draw<<'\n';
        }
        size_t components=0;for(const auto& entity:state.entities)components+=entity.model_components.size();
        std::cout<<"PLACEMENT_INPUTS tick="<<i<<" faction="<<selected_faction<<" owners="<<owners.size()
            <<" live_components="<<components<<" ws00="<<own.structures.size()<<" ws01="<<own.units.size()
            <<" resource08="<<workspace_info.resource08<<" base09="<<workspace_info.base09<<'\n';
        for(uint32_t si=0;si<workspace_info.resource_site_sample_count;++si) {
            const auto& site=workspace_info.resource_site_samples[si];
            const auto model=simulation_lab_first_site_model(*runtime,site.army_asset_id);if(!model)continue;
            for(uint32_t mode:{4U,7U}) {
                const auto result=simulation_lab_query_site_placement(state,field,owners,*model,
                    {float_to_q12(site.point.x),float_to_q12(site.point.y)},selected_faction,mode,
                    mode==7?std::bit_cast<int32_t>(state.ai_parameters[81]):0);
                std::cout<<"SITE_QUERY_POST_PASS tick="<<i<<" faction="<<selected_faction<<" site="<<si
                    <<" id="<<site.army_asset_id<<" mode="<<mode<<" accepted="<<result.accepted
                    <<" reason="<<uint32_t(result.reason)<<" clearance="<<result.placement_value<<'\n';
            }
        }
    }
    for(const auto& e:state.entities)if(e.source_placement_index==native.size()-46U) {
      const auto key=std::tuple{e.production_asset_id,e.production_building,e.research_technology_id,
                                e.research_pending_payment,e.research_active};
      if(!previous_model46||*previous_model46!=key) {
        std::cout<<"MODEL46 tick="<<i<<" selected="<<e.production_asset_id
                 <<" building="<<e.production_building<<" elapsed="<<e.production_elapsed_ticks
                 <<" required="<<e.production_duration_ticks<<" tech="<<e.research_technology_id
                 <<" assigned="<<e.research_pending_payment<<" active="<<e.research_active
                 <<" research_elapsed="<<e.research_elapsed_ticks<<'\n';
        previous_model46=key;
      }
      break;
    }
    { // Export each tick for independent comparison with admitted TICK seeds.
      std::cout<<"RUNTIME tick="<<i<<" seed="<<state.rng_state<<" shots="<<state.metrics.weapon_shots_fired
        <<" prod_eff="<<state.metrics.production_timed_effects_spawned<<" env_eff="<<state.metrics.environmental_effects_spawned
        <<" relations_draws="<<(state.relation_trace.tick==i?state.relation_trace.draws:0U)<<'\n';
    }
    for(const auto& entity:state.entities)
      if(entity.synthetic&&entity.construction_start_tick==i)
        std::cout<<"CREATED tick="<<i<<" faction="<<entity.faction_index<<" arm="<<entity.army_asset_id
          <<" x_q12="<<float_to_q12(entity.actor.position.x)<<" y_q12="<<float_to_q12(entity.actor.position.y)
          <<" yaw="<<entity.actor.body_yaw_turn16<<" health="<<entity.root_model_integrity<<'\n';
    for(uint32_t f=1;f<=state.active_faction_count;++f){
      const auto& economy=state.faction_economy[f];
      const auto primary=std::count_if(state.pending_asset_requests.begin(),state.pending_asset_requests.end(),
        [&](const auto& request){return request.faction_index==f;});
      const auto secondary=std::count_if(state.secondary_purchase_requests.begin(),state.secondary_purchase_requests.end(),
        [&](const auto& request){return request.faction_index==f;});
      std::cout<<"RUNTIME_FACT tick="<<i<<" faction="<<f<<" xenite="<<economy.xenite_current_q4
        <<" tritium="<<economy.tritium_current_q4
        <<" xenite_rate="<<state.faction_xenite_extraction_rate_q4_per_tick[f]
        <<" primary_alert="<<state.ai_primary_anchor_cooldown[f]<<" primary="<<primary<<" secondary="<<secondary;
      for(size_t w=0;w<8;++w)std::cout<<" tech"<<w<<'='<<state.faction_technology_masks[f][w];
      std::cout<<'\n';
    }
    const auto &trace = state.ai_purchase_trace[2];
    if (trace.tick == i) {
      const auto& special=state.ai_special_site_trace[2];
      std::cout<<"SPECIAL tick="<<i<<" evaluated="<<special.tick
               <<" ws08="<<special.workspace08_count<<" tests="<<special.cluster_tests
               <<" accepts="<<special.cluster_accepts<<" hostile_blocks="<<special.hostile_blocks
               <<" probes="<<special.probes_created<<" selected="<<special.selected
               <<" id="<<special.candidate_id<<" kind="<<special.candidate_kind
               <<" weight="<<special.weight<<" clearance="<<special.clearance_q12
               <<" buckets="<<special.bucket_count<<'\n';
      std::cout << "PURCHASE tick=" << i << " rng=" << state.rng_state
                << " cached=" << trace.cached << " workspace11_assets="
                << state.ai_army_profile_trace[2].workspace11_count
                << " research_best="
                << state.ai_research_trace[2].best_technology_id << '\n';
      for (uint32_t c = 0; c < trace.recorded_count; ++c) {
        const auto &t = trace.candidates[c];
        std::cout << "CAND rank=" << t.rank << " id=" << t.id
                  << " kind=" << t.kind << " cost=" << t.cost_q4
                  << " score=" << t.packed_score
                  << " before=" << t.budget_before_q4
                  << " after=" << t.budget_after_q4
                  << " decision=" << uint32_t(t.decision) << '\n';
      }
    }
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
