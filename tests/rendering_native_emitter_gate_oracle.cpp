// Execute the unchanged original class-4 callback against the live Lab callback.
#include THANDOR_NATIVE_DAMAGE
#include "thandor/simulation_lab_environment.hpp"
#include <iostream>
#include <random>
#include <stdexcept>
GameFactionRuntimeImage g_GameFactionRuntimeImage{};
static uint32_t calls;
void ArmyRuntime_UpdateTimedShotAndEffectEmitters(WorldRuntimeContext*,ModelRuntimeUpdateView*) {++calls;}
void ArmyRuntime_UpdateAnimatedModelSubnodes(WorldRuntimeContext*,ModelRuntimeUpdateView*) {}
void ArmyRuntime_EmitDamageThresholdEffect(WorldRuntimeContext*,ModelRuntimeSlot*) {}
void Thandor_Log(const char*,...) {}
void Thandor_Ptr32Overflow(uintptr_t){std::abort();}
static ModelDefinitionTimedEffectsUpdateView definition;
static ModelRuntimeTimedEffectsUpdateView native_model;
int main()try {
 using namespace thandor;
 auto runtime=std::make_shared<SimulationLabRuntimeCatalog>();
 ModelVisualRecord def;def.definition_id=1;def.runtime_class_id=4;
 def.simulation.timed_effect_id=10;def.simulation.timed_effect_interval_ticks=1;
 def.simulation.runtime_render_flags=1;
 runtime->definitions[1]=def;runtime->effects[10].definition.definition_id=10;
 ModelVisualNode node;node.parent_index=UINT32_MAX;node.model_definition_id=1;
 node.runtime_class_id=4;node.sprite_path="test";node.simulation=def.simulation;
 auto hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(std::vector{node});
 SimulationLabSeedEntity seed;seed.army_asset_id=320;seed.runtime_model_class_id=4;
 seed.faction_index=1;seed.building=true;seed.root_model_maximum_integrity=1000;
 SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=runtime;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 const auto field=FieldGrid::create(16,16,0,0);std::mt19937 rng(0x6564);
 uint32_t fixtures=0;
 for(uint32_t i=0;i<4096;++i) {
  SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=4;cfg.sweep_mode=SimulationLabSweepMode::Fixed;
  cfg.runtime_callback_step_ticks=1+i%5;
  SimulationLabExperiment lab;lab.reset(cfg,ki,std::span(&seed,1),{}, {}, {}, {}, {},scenario);
  auto& run=lab.runs()[0];auto& e=run.entities[0];e.runtime_model_hierarchy=hierarchy;
  auto& c=e.model_components[e.root_model_component_index];c.model_definition_id=1;
  c.runtime_class_id=4;c.source_model_node_index=0;
  const uint32_t flags=rng()&0x1c9U;
  c.runtime_flags_ec=(i&1U)?flags:0U;e.runtime_flags=(i&1U)?0U:flags;
  e.synthetic=(i&2U)!=0;definition.timedEffectsRequireStateBit40=(i&4U)?7:0;
  runtime->definitions[1].simulation.timed_effects_require_research=definition.timedEffectsRequireStateBit40;
  native_model.modelDefinition=&definition;
  native_model.classState.stateFlags=static_cast<ArmyRuntimeFlags>(flags);
  calls=0;WorldRuntimeContext world{};
  ArmyRuntimeClass_UpdateTimedEffectsModelsAndDamage(&world,&native_model);
  lab.step(field,1);
  if(run.metrics.production_timed_effects_spawned!=calls)
    throw std::runtime_error("native class4 activation differs at fixture "+std::to_string(i));
  ++fixtures;
 }
 std::cout<<"V656_NATIVE_EMITTER_GATE PASS fixtures="<<fixtures
          <<"; original class4 callback, raw flags, research gate, authored/synthetic, batches1..5\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
