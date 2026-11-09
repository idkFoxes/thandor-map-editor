#include THANDOR_NATIVE_AIRCRAFT_SOURCE
#include "thandor/simulation_lab.hpp"
#include "thandor/catalog.hpp"
#include <iostream>
#include <random>
static ModelRuntimeClass21UpdateView native_model{};
static ArmyRuntimeClassUpdate21DefinitionView native_definition{};
static ModelRuntimeNode native_root{}, native_body{};
InGameSimulationStepBatchTicks g_InGameSimulationStepTicks = 1;
void Thandor_Ptr32Overflow(uintptr_t) { std::abort(); }
int main(int argc,char** argv) try {
  FixedMath_BuildSinCosTables();
  native_model.modelDefinition = &native_definition;
  native_model.rootModelNode = &native_root;
  native_root.childNodes[0] = &native_body;
  using namespace thandor;
  auto field = FieldGrid::create(16, 16, 0, 0);
  const auto &c = field.at(8, 8);
  SimulationLabSeedEntity seed;
  seed.army_asset_id = 240;
  seed.runtime_model_class_id = 21;
  seed.faction_index = 1;
  seed.root_model_maximum_integrity = 1000;
  SimulationLabScenarioRuntimeSeed scenario;
  scenario.enabled = true;
  scenario.active_faction_count = 1;
  scenario.human_faction_mask = 2;
  SimulationLabConfig config;
  config.run_count = 1;
  config.duration_ticks = 100000;
  std::array<uint32_t, AiKnowledgeData::kParameterCount> ki{};
  SimulationLabExperiment lab;
  lab.reset(config, ki, std::span(&seed, 1), {}, {}, {}, {}, {}, scenario);
  std::mt19937 rng(0x64821);
  uint32_t cases = 0;
  for (uint32_t batch = 1; batch <= 5; ++batch)
    for (int n = 0; n < 1000; ++n) {
      // reset() owns callback batch, so refresh once per batch rather than
      // falsifying a tick multiplier.
      if (n == 0) {
        config.runtime_callback_step_ticks = batch;
        lab.reset(config, ki, std::span(&seed, 1), {}, {}, {}, {}, {},
                  scenario);
      }
      auto &e = lab.runs()[0].entities[0];
      const auto phase = int32_t(rng() % 131073) - 65536,
                 coefficient = int32_t(rng() % 129) - 64,
                 base = int32_t(rng() % 16384), speed = int32_t(rng() % 1024);
      const uint32_t heading = rng() & 65535;
      g_InGameSimulationStepTicks = batch;
      native_definition.movementStepQ12 = speed;
      native_definition.arcCoefficient = coefficient;
      native_model.classLinkState.classState7C = uint32_t(phase);
      native_model.classLinkState.classState80 = uint32_t(base);
      native_root.modelPayload.worldRotationAngle2 = heading;
      native_root.worldTransform.translation.x = c.world_x_q12;
      native_root.worldTransform.translation.y = c.world_y_q12;
      ArmyAircraft_FlyPadArc(&native_model, &native_definition);
      e.aircraft.initialized = true;
      e.aircraft.runtime.phase =
          SimulationClass21LinkedChildPhase::OutboundRelease;
      e.aircraft.runtime.release_countdown = 100000;
      e.aircraft.runtime.outbound_countdown = 100000;
      e.aircraft.arc_position = phase;
      e.aircraft.platform_height = base;
      e.aircraft_definition = {speed, coefficient, 100000, 100000, 10, -4, 1};
      e.actor.position = {q12_to_float(c.world_x_q12),
                          q12_to_float(c.world_y_q12), 0};
      e.actor.body_yaw_turn16 = heading;
      lab.step(field, 1);
      if (uint32_t(e.aircraft.arc_position) !=
              native_model.classLinkState.classState7C ||
          e.aircraft.body_height !=
              native_body.modelPayload.localTranslationZQ12 ||
          e.aircraft.body_pitch !=
              native_body.modelPayload.localRotationAngle1 ||
          float_to_q12(e.actor.position.x) !=
              native_root.worldTransform.translation.x ||
          float_to_q12(e.actor.position.y) !=
              native_root.worldTransform.translation.y) {
        std::cerr << "aircraft native mismatch batch=" << batch << " case=" << n
                  << '\n';
        return 1;
      }
      ++cases;
    }
  uint32_t stock_variants=0, stock_steps=0;
  if(argc>1){
    const auto catalog=AssetCatalog::scan_archive_root(argv[1]);
    std::mt19937 initial(12345);
    config.base_seed=12345;
    for(const auto& preset:catalog.army_presets()){
      if(preset.runtime_model_class_id!=21 || preset.browser_kind!=PlacementBrowserKind::Unit)continue;
      const auto& hierarchy=preset.resolved_model_hierarchy.empty()?preset.model_hierarchy:preset.resolved_model_hierarchy;
      const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){return node.parent_index==UINT32_MAX;});
      if(root==hierarchy.end())throw std::runtime_error("stock aircraft has no root");
      const auto& p=root->simulation;
      seed.army_asset_id=preset.registry_id;
      config.runtime_callback_step_ticks=1;
      lab.reset(config,ki,std::span(&seed,1),{},{},{},{},{},scenario);
      auto& e=lab.runs()[0].entities[0];
      native_definition.movementStepQ12=p.class_step_q12;
      native_definition.arcCoefficient=p.pitch_max_velocity_turn16;
      const int32_t phase=int32_t(initial()%32768)-16384, base=int32_t(initial()%8192);
      uint32_t heading=initial()&65535;
      native_model.classLinkState.classState7C=uint32_t(phase);
      native_model.classLinkState.classState80=uint32_t(base);
      native_root.worldTransform.translation.x=c.world_x_q12;
      native_root.worldTransform.translation.y=c.world_y_q12;
      e.aircraft.initialized=true;
      e.aircraft.runtime.phase=SimulationClass21LinkedChildPhase::OutboundRelease;
      e.aircraft.runtime.release_countdown=e.aircraft.runtime.outbound_countdown=100000;
      e.aircraft.arc_position=phase;e.aircraft.platform_height=base;
      e.aircraft_definition={p.class_step_q12,p.pitch_max_velocity_turn16,p.class_specific_c0_raw,
        int32_t(p.class_specific_c4_raw),p.class_specific_c8_raw,p.class_specific_cc_raw,int32_t(p.reload_ticks)};
      e.actor.position={q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};
      for(uint32_t tick=0;tick<128;++tick){
        if(tick%16==0)heading=initial()&65535;
        native_root.modelPayload.worldRotationAngle2=heading;e.actor.body_yaw_turn16=heading;
        g_InGameSimulationStepTicks=1;
        ArmyAircraft_FlyPadArc(&native_model,&native_definition);
        lab.step(field,1);
        if(uint32_t(e.aircraft.arc_position)!=native_model.classLinkState.classState7C ||
           e.aircraft.body_height!=native_body.modelPayload.localTranslationZQ12 ||
           e.aircraft.body_pitch!=native_body.modelPayload.localRotationAngle1 ||
           float_to_q12(e.actor.position.x)!=native_root.worldTransform.translation.x ||
           float_to_q12(e.actor.position.y)!=native_root.worldTransform.translation.y){
          std::cerr<<"stock aircraft divergence arm="<<preset.registry_id<<" seed=12345 tick="<<tick+1<<'\n';return 1;
        }
        ++stock_steps;
      }
      ++stock_variants;
    }
    if(!stock_variants)throw std::runtime_error("no stock class-21 variants compared");
    std::cout<<"NATIVE_LAB_STOCK_AIRCRAFT PASS seed=12345 variants="<<stock_variants
             <<" consecutive_steps_per_variant=128 compared_steps="<<stock_steps<<'\n';
  }
  std::cout << "V648_NATIVE_AIRCRAFT PASS original_arc_and_travel_cases="
            << cases
            << " batches=1..5; original aircraft.cpp body, no game "
               "loop/Windows execution\n";
} catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
