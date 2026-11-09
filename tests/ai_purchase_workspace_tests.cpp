#include "thandor/simulation_lab_ai_workspaces.hpp"
#include "thandor/retail_random.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void require(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
int main() try {
  SimulationLabRun r;
  r.level_runtime_enabled = true;
  SimulationLabEntity producer;
  producer.army_asset_id = 301;
  producer.faction_index = 1;
  producer.runtime_model_class_id = 13;
  producer.production_building = true;
  producer.production_asset_id = 60;
  producer.model_components.resize(3);
  producer.root_model_component_index = 0;
  for (size_t i = 0; i < 3; ++i)
    producer.model_components[i].model_definition_id = uint32_t(100 + i);
  r.entities.push_back(producer);
  auto unit = producer;
  unit.army_asset_id = 60;
  unit.production_building = false;
  r.entities.push_back(unit);
  r.secondary_purchase_requests.push_back({62, 1, 0, 0});
  auto w = simulation_lab_ai_own_workspaces(r, 1);
  require(w.structures.size() == 1 && w.units.size() == 3,
          "root or in-production entries missing");
  require(w.units[0].model_definition_id == 100, "unit root identity changed");
  require(!w.units[1].entity_index && w.units[1].army_asset_id == 62 &&
              !w.units[2].entity_index && w.units[2].army_asset_id == 60,
          "pending/production order changed");
  r.entities[0].model_components.resize(129);
  w = simulation_lab_ai_own_workspaces(r, 1);
  require(w.structures.size() == 1 && w.root_admitted(r, 0),
          "attached runtimes consumed workspace00 capacity");
  auto extra = r.entities[0];
  extra.production_building = false;
  for (int i = 0; i < 130; ++i)
    r.entities.push_back(extra);
  w = simulation_lab_ai_own_workspaces(r, 1);
  require(w.structures.size() == 128 && !w.root_admitted(r, 0),
          "newest-root workspace00 limit changed");
  r.entities.resize(2);
  extra = r.entities[1];
  for (int i = 0; i < 80; ++i)
    r.entities.push_back(extra);
  w = simulation_lab_ai_own_workspaces(r, 1);
  require(w.units.size() == 64, "workspace01 exceeded native 64-entry limit");
  r.entities.clear();
  w = simulation_lab_ai_own_workspaces(r, 1);
  require(w.units.empty(),
          "empty owner list unexpectedly added pending assets");
  w = simulation_lab_ai_own_workspaces(r, 1, true);
  require(w.units.size() == 1,
          "explicit other world owners failed to admit pending assets");

  auto field = FieldGrid::create(16, 16, 0, 0);
  SimulationLabConfig cfg;
  cfg.run_count = 1;
  cfg.duration_ticks = 200;
  std::array<uint32_t, AiKnowledgeData::kParameterCount> ki{};
  SimulationLabSeedEntity source;
  source.army_asset_id = 300;
  source.faction_index = 1;
  source.runtime_model_class_id = 11;
  source.research_technology_ids = {7, 8};
  source.root_model_maximum_integrity = 1000;
  std::array<SimulationLabSeedEntity, 2> seeds{source, source};
  std::array<SimulationLabFactionEconomySeed, 8> eco{};
  eco[1].xenite_current_q4 = 200;
  SimulationLabScenarioRuntimeSeed sc;
  sc.enabled = true;
  sc.active_faction_count = 1;
  sc.human_faction_mask = 0;
  SimulationLabTechnologyAsset t;
  t.technology_id = 7;
  t.dependency_technology_id = 7;
  t.xenite_cost_q4 = 10;
  t.research_duration_ticks = 1000;
  auto t8 = t;
  t8.technology_id = t8.dependency_technology_id = 8;
  const std::array techs{t, t8};
  SimulationLabProductionAsset asset;
  asset.army_asset_id = 331;
  asset.army_runtime_flags14_raw = 0x10;
  asset.xenite_cost_q4 = 20;
  auto reset = [&](SimulationLabExperiment &lab) {
    lab.reset(cfg, ki, seeds, eco, std::span(&asset, 1), techs, {}, {}, sc);
    auto &run = lab.runs()[0];
    run.tick = 73;
    run.ai_candidate_cache_reuse_state[1] = 16;
  };
  {
    // The faction was active when the economy dispatch built its workspace.
    // Losing it inside diplomacy does not cancel the already dispatched turn.
    auto merge_scene=sc;
    merge_scene.active_faction_count=2;
    merge_scene.simulate_all_active_factions_as_ai=true;
    merge_scene.relation_ui_flags=0;
    auto merge_seeds=seeds;
    merge_seeds[1].faction_index=2;
    auto merge_economy=eco;
    merge_economy[2].xenite_current_q4=200;
    SimulationLabExperiment lab;
    lab.reset(cfg,ki,merge_seeds,merge_economy,std::span(&asset,1),techs,{},{},merge_scene);
    auto& run=lab.runs()[0];
    run.tick=81; // faction 2, economy phase, next tick.
    run.faction_frontend_player_counts[1]=1;
    run.faction_frontend_player_counts[2]=0;
    simulation_lab_apply_relation_transition(run,1,2,10,10);
    for(uint32_t seed=0;;++seed){
      auto state=seed;
      if((retail_random_next(state)&127U)==85U){run.rng_state=seed;break;}
    }
    run.ai_candidate_cache_reuse_state[2]=16;
    run.ai_candidate_cache_count[2]=1;
    run.ai_candidate_cache[2][0]={331,0x3A001,1};
    lab.step(field,1);
    require(run.faction_merge_redirect[2]==1&&run.faction_lifecycle_states[2]==0,
        "diplomacy fixture did not absorb the dispatched faction");
    require(run.ai_purchase_trace[2].tick==82&&run.ai_purchase_trace[2].cached&&
        run.ai_purchase_trace[2].candidate_count==1&&run.active_planning_snapshot==nullptr,
        "absorption canceled the in-progress planning dispatch or leaked its workspace");
    require(std::any_of(run.secondary_purchase_requests.begin(),run.secondary_purchase_requests.end(),
        [](const auto& request){return request.faction_index==2&&request.army_asset_id==331;}),
        "in-progress purchase lost its producer after faction absorption");
  }
  {
    // Workspace11 membership is frozen before diplomacy. A merge may unlock
    // a better unit, but that unit enters only the next planning rebuild.
    auto scene=sc;scene.active_faction_count=2;
    scene.simulate_all_active_factions_as_ai=true;scene.relation_ui_flags=0;
    auto heads=seeds;heads[1].faction_index=2;
    auto money=eco;money[1].xenite_current_q4=money[2].xenite_current_q4=100000;
    std::array<FactionTechnologyMask256,8> masks{};
    set_faction_technology_unlocked(masks[1],0,true);
    set_faction_technology_unlocked(masks[1],1,true);
    set_faction_technology_unlocked(masks[2],0,true);
    set_faction_technology_unlocked(masks[2],7,true);
    auto make_unit=[](uint32_t id,uint32_t required,int weight){
      SimulationLabProductionAsset a;a.army_asset_id=id;a.army_runtime_flags14_raw=0x11;
      a.xenite_cost_q4=20;a.build_duration_ticks=1000;
      a.ai_faction_score_seeds_prepared=true;a.ai_definition_class_value74=weight;
      a.ai_baseline_root_definition_value0c=64;a.ai_baseline_root_definition_value18=1;
      a.ai_baseline_root_definition_value60=1000;
      SimulationLabLinkedModelTechnology v;v.required_technology_id=required;
      v.score.resolved=true;v.score.root_definition_value0c=64;
      v.score.root_definition_value18=1;v.score.root_definition_value60=1000;
      SimulationLabLinkedModelGroup g;g.variants.push_back(v);
      a.entity_seed.linked_model_technologies.push_back(g);return a;
    };
    const std::array units{make_unit(2,0,50),make_unit(3,7,500)};
    auto weights=ki;weights[9]=1000;
    SimulationLabExperiment lab;lab.reset(cfg,weights,heads,money,units,{},masks,{},scene);
    auto& run=lab.runs()[0];run.tick=73;
    run.faction_frontend_player_counts[1]=1;run.faction_frontend_player_counts[2]=0;
    simulation_lab_apply_relation_transition(run,1,2,10,10);
    for(uint32_t seed=0;;++seed){auto state=seed;
      if((retail_random_next(state)&127U)==85U){run.rng_state=seed;break;}}
    lab.step(field,1);
    require(run.faction_merge_redirect[2]==1&&
        faction_technology_unlocked(run.faction_technology_masks[1],7),
        "workspace11 fixture did not merge and share technology");
    require(run.ai_army_profile_trace[1].workspace11_count==1&&
        run.ai_army_profile_trace[1].best_asset_id[0]==2,
        "newly merged technology entered the current workspace11");
    run.tick=201;lab.step(field,1);
    require(run.ai_army_profile_trace[1].workspace11_count==2&&
        run.ai_army_profile_trace[1].best_asset_id[0]==3,
        "merged technology was not admitted on the next workspace11 rebuild");
  }
  {
    SimulationLabExperiment lab;reset(lab);auto& run=lab.runs()[0];
    for(auto& e:run.entities)e.research_technology_ids.clear();
    run.ai_candidate_cache_count[1]=1;
    run.ai_candidate_cache[1][0]={331,0x3A001,1};
    run.ai_primary_anchor_cooldown[1]=100; // Forces a fresh, empty workspace.
    lab.step(field,1);
    require(!run.ai_purchase_trace[1].cached && run.ai_purchase_trace[1].candidate_count==0,
        "empty fresh-cache fixture unexpectedly proposed a candidate");
    require(run.ai_candidate_cache_reuse_state[1]==16 && run.ai_candidate_cache_count[1]==1 &&
        run.ai_candidate_cache[1][0].id==331 && run.ai_candidate_cache[1][0].packed_score==0x3A001,
        "empty forced rebuild erased the previously saved faction cache");
    run.tick=201;run.ai_primary_anchor_cooldown[1]=0;
    const auto rng=run.rng_state;
    lab.step(field,1);
    require(run.ai_purchase_trace[1].cached && run.secondary_purchase_requests.size()==1 &&
        run.secondary_purchase_requests[0].army_asset_id==331 && run.rng_state==rng,
        "saved candidate was rebuilt instead of replayed after the cooldown");
  }
  {
    // Active-faction planning is lifecycle-driven. Unfinished/terminal MODEL
    // owners remain in WS00 until unlink and may still receive a cached
    // research assignment.
    SimulationLabExperiment lab;
    reset(lab);
    auto &run = lab.runs()[0];
    for (auto &entity : run.entities) {
      entity.construction_complete = false;
      entity.root_model_terminal = true;
      entity.runtime_flags = 0x10U;
    }
    run.ai_candidate_cache_count[1] = 1;
    run.ai_candidate_cache[1][0] = {7, 0x3002, 2};
    lab.step(field, 1);
    require(run.entities[1].research_pending_payment &&
                run.entities[1].research_technology_id == 7,
            "terminal/unfinished WS00 owner suppressed active-faction planning");
  }
  {
    SimulationLabExperiment lab;
    reset(lab);
    auto &run = lab.runs()[0];
    for (int i = 0; i < 64; ++i)
      run.secondary_purchase_requests.push_back({999, 1, 0, 0});
    run.ai_candidate_cache_count[1] = 2;
    run.ai_candidate_cache[1][0] = {331, 0x3001, 1};
    run.ai_candidate_cache[1][1] = {7, 0x2002, 2};
    lab.step(field, 1);
    require(run.secondary_purchase_requests.size() == 64 &&
                run.entities[1].research_pending_payment,
            "full secondary queue prevented later research");
    require(run.ai_purchase_trace[1].candidates[0].decision ==
                    SimulationLabRun::PurchaseDecision::QueueFull &&
                run.metrics.ai_purchase_candidates_applied == 2,
            "queue-full native apply semantics lost");
    require(run.ai_candidate_cache_reuse_state[1] == 0,
            "full-queue attempt incorrectly retained failed cache");
  }
  {
    SimulationLabExperiment lab;
    reset(lab);
    auto &run = lab.runs()[0];
    auto &newer = run.entities[1];
    newer.research_active = true;
    newer.research_technology_id = 6;
    newer.research_duration_ticks = 1000;
    run.ai_candidate_cache_count[1] = 2;
    run.ai_candidate_cache[1][0] = {7, 0x3002, 2};
    run.ai_candidate_cache[1][1] = {8, 0x2002, 2};
    lab.step(field, 1);
    require(newer.research_technology_id == 6 &&
                run.entities[0].research_pending_payment &&
                run.entities[0].research_technology_id == 8,
            "no-op researcher was reused in same purchase pass");
    require(run.ai_purchase_trace[1].candidates[0].decision ==
                SimulationLabRun::PurchaseDecision::ResearchNoOp,
            "no-op research was reported as queued");
  }
  {
    SimulationLabExperiment lab;
    reset(lab);
    auto &run = lab.runs()[0];
    auto &newer = run.entities[1];
    newer.research_pending_payment = true;
    newer.research_technology_id = 7;
    newer.research_xenite_due_q4 = 10;
    newer.production_building = true;
    newer.production_duration_ticks = 1000;
    run.ai_candidate_cache_count[1] = 1;
    run.ai_candidate_cache[1][0] = {7, 0x3002, 2};
    lab.step(field, 1);
    require(run.entities[0].research_pending_payment &&
                run.entities[0].research_technology_id == 7,
            "cached execution incorrectly used AI collection's assigned-job "
            "exclusion");
  }
  {
    SimulationLabExperiment lab;
    reset(lab);
    auto &run = lab.runs()[0];
    run.faction_economy[1].xenite_current_q4 = 5;
    run.ai_candidate_cache_count[1] = 1;
    run.ai_candidate_cache[1][0] = {331, 0x3001, 1};
    lab.step(field, 1);
    require(run.ai_purchase_trace[1].candidates[0].budget_after_q4 ==
                uint32_t(5U - 20U),
            "failed planning subtraction was hidden from diagnostics");
    require(run.faction_economy[1].xenite_current_q4 == 5,
            "planning subtraction charged real faction stock");
  }
  {
    auto pad = source;
    pad.runtime_model_class_id = 22;
    pad.production_storage_capacity = 1;
    SimulationLabExperiment lab;
    lab.reset(cfg, ki, std::span(&pad, 1), eco, std::span(&asset, 1), {}, {},
              {}, sc);
    auto &run = lab.runs()[0];
    run.entities[0].production_linked_child_count = 1;
    auto aircraft = asset;
    aircraft.army_asset_id = 240;
    aircraft.army_runtime_flags14_raw = 8;
    lab.reset(cfg, ki, std::span(&pad, 1), eco, std::span(&aircraft, 1), {}, {},
              {}, sc);
    auto &p = lab.runs()[0];
    p.entities[0].production_linked_child_count = 1;
    p.secondary_purchase_requests.push_back({240, 1, 0, 0});
    lab.step(field, 1);
    require(!p.entities[0].production_building &&
                p.secondary_purchase_requests.size() == 1,
            "pad production ignored retained capacity count");
  }
  // Valid legacy model46 build counters recover the batch transitions below.
  // With observed assignment tick342, production completion/payment and
  // research completion must occur on the native ticks453 and498.
  // Purchases/assignment are controlled inputs here, not an autonomous replay
  // claim.
  {
    auto producer = source;
    producer.research_technology_ids = {201};
    auto mine = asset;
    mine.army_asset_id = 330;
    mine.xenite_cost_q4 = 9600;
    mine.build_duration_ticks = 439;
    auto research = t;
    research.technology_id = research.dependency_technology_id = 201;
    research.xenite_cost_q4 = 4800;
    research.research_duration_ticks = 222;
    auto human = sc;
    human.human_faction_mask = 2;
    auto funds = eco;
    funds[1].xenite_current_q4 = 28000;
    auto clock = cfg;
    clock.duration_ticks = 600;
    SimulationLabExperiment lab;
    lab.reset(clock, ki, std::span(&producer, 1), funds, std::span(&mine, 1),
              std::span(&research, 1), {}, {}, human);
    auto &run = lab.runs()[0];
    run.tick = 338;
    run.secondary_purchase_requests.push_back({330, 1, 338, 0});
    require(!lab.set_runtime_callback_step_ticks(0) &&
                !lab.set_runtime_callback_step_ticks(6),
            "invalid batch accepted");
    for (uint32_t tick = 339; tick <= 498; ++tick) {
      const uint32_t batch = tick < 367   ? 1
                             : tick < 371 ? 2
                             : tick < 375 ? 3
                             : tick < 377 ? 4
                                          : 5;
      require(lab.set_runtime_callback_step_ticks(batch),
              "valid batch rejected");
      lab.step(field, 1);
      if (tick == 342) {
        auto &e = run.entities[0];
        e.research_pending_payment = true;
        e.research_technology_id = 201;
        e.research_xenite_due_q4 = 4800;
        e.research_duration_ticks = 222;
      }
      if (tick == 452)
        require(run.entities[0].production_elapsed_ticks == 435 &&
                    run.entities[0].research_pending_payment,
                "native pre-completion checkpoint differs");
      if (tick == 453)
        require(!run.entities[0].production_building &&
                    run.entities[0].research_active &&
                    run.entities[0].research_elapsed_ticks == 0 &&
                    run.entities[0].production_elapsed_ticks == 440 &&
                    run.entities[0].production_duration_ticks == 439 &&
                    run.faction_economy[1].xenite_current_q4 == 13600,
                "native production/payment tick differs");
      if (tick == 497)
        require(run.entities[0].research_active &&
                    run.entities[0].research_elapsed_ticks == 220,
                "research completed before native tick498");
    }
    require(
        !run.entities[0].research_active &&
            faction_technology_unlocked(run.faction_technology_masks[1], 201) &&
            run.ai_research_trace[1].last_completion_tick == 498,
        "native research completion tick differs");
    std::cout << "V649_NATIVE_TIMING PASS controlled production_paid=339 "
                 "research_assigned=342 research_paid=453 "
                 "research_completed=498; recovered variable callback batches, "
                 "not autonomous purchase replay\n";
  }
  std::cout << "V649_PURCHASE_WORKSPACES PASS native capacities/order, root "
               "and in-production counts, full queue continuation, consumed "
               "no-op source, cached pending availability, budget wrap, pad "
               "retained count\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
