#include THANDOR_NATIVE_PURCHASING
#include THANDOR_NATIVE_WORKSPACES
#include THANDOR_NATIVE_TECHNOLOGY
#include THANDOR_NATIVE_ARMY_STOCK
#include "thandor/simulation_lab.hpp"
#include <iostream>
#include <random>
static ArmyAssetRecord asset{};
static ModelRuntimeSlot models[2]{};
static ModelDefinition definitions[2]{};
static AiStructureWorkspaceEntry structures[128]{};
static AiCandidateWorkspaceEntry candidates[128]{};
static struct {
  TechnologyAssetHeader header;
  TechnologyRecord records[256];
} tech_image{};
static InGameRuntimeRoot native_root{};
InGameRuntimeRoot *g_InGameRuntimeRoot = &native_root;
UiCommandRuntimeFlagMask g_UiCommandRuntimeFlags{};
void Thandor_Ptr32Overflow(uintptr_t) { std::abort(); }
void Thandor_Log(const char *, ...) {}
uint32_t ArmyAssetRegistry_FindById(PckArmyAssetIdCatalog id,
                                    ArmyAssetRecordPrefix **out) {
  if (id != 331)
    return 1;
  *out = reinterpret_cast<ArmyAssetRecordPrefix *>(&asset);
  return 0;
}
int main() try {
  static_assert(offsetof(ModelRuntimeSlot, classState.stateFlags) ==
                offsetof(GameEntityRuntime, common.runtimeFlags));
  static_assert(offsetof(ModelRuntimeSlot, researchTechnologyId) ==
                offsetof(GameEntityRuntime, common.commandState));
  using namespace thandor;
  asset.registryId = 331;
  asset.flags = static_cast<decltype(asset.flags)>(0x10);
  asset.xeniteCostQ4 = 20;
  g_TechnologyAsset = reinterpret_cast<::TechnologyAsset *>(&tech_image);
  tech_image.records[7].xeniteCostQ4 = 10;
  tech_image.records[8].xeniteCostQ4 = 10;
  tech_image.records[7].researchDurationQ5 =
      tech_image.records[8].researchDurationQ5 = 1000;
  g_AiWorkspace00Structures = structures;
  g_AiWorkspace13Candidates = candidates;
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
  std::array seeds{source, source};
  SimulationLabProductionAsset product;
  product.army_asset_id = 331;
  product.army_runtime_flags14_raw = 0x10;
  product.xenite_cost_q4 = 20;
  SimulationLabTechnologyAsset tech;
  tech.technology_id = tech.dependency_technology_id = 7;
  tech.xenite_cost_q4 = 10;
  tech.research_duration_ticks = 1000;
  auto other = tech;
  other.technology_id = other.dependency_technology_id = 8;
  std::array techs{tech, other};
  SimulationLabScenarioRuntimeSeed scenario;
  scenario.enabled = true;
  scenario.active_faction_count = 1;
  scenario.human_faction_mask = 0;
  std::mt19937 rng(0x649);
  size_t checks = 0;
  for (uint32_t n = 0; n < 2000; ++n) {
    const uint32_t funds = rng() % 101, mode = rng() % 3;
    const bool full = rng() & 1;
    for (size_t i = 0; i < 2; ++i) {
      models[i] = {};
      definitions[i] = {};
      definitions[i].runtimeClassId = static_cast<ModelRuntimeClassId>(11);
      definitions[i].researchTechnologyIds[1] = 7;
      definitions[i].researchTechnologyIds[2] = 8;
      models[i].definitionOrSavedId.runtimeDefinition = &definitions[i];
      structures[i].runtimeSlotAddressOrZero = &models[1 - i];
      structures[i].armyAssetId = 300;
    }
    if (mode == 2)
      models[1].classState.stateFlags = static_cast<ArmyRuntimeFlags>(1);
    if (mode == 1) {
      models[1].classState.stateFlags = static_cast<ArmyRuntimeFlags>(0x40);
      models[1].researchTechnologyId = 6;
    }
    g_AiWorkspace00Count = 2;
    g_AiCandidateWorkspaceEntryCount = mode == 1 ? 2 : 3;
    candidates[0].entityIdAndMultiplicity = mode == 1 ? 7 : 331;
    candidates[0].weightedScoreAndKind = mode == 1 ? 0x3002 : 0x3001;
    candidates[1].entityIdAndMultiplicity = mode == 1 ? 8 : 7;
    candidates[1].weightedScoreAndKind = 0x2002;
    candidates[2].entityIdAndMultiplicity = 8;
    candidates[2].weightedScoreAndKind = 0x1002;
    auto &faction = g_GameFactionRuntimeImage.records[1];
    faction = {};
    faction.xeniteCurrentQ4 = funds;
    faction.secondaryArmyAssetCount = full ? 64 : 0;
    const bool no_applied = AiPurchasePlanner_ExecuteAffordableCandidates(1);
    std::array<SimulationLabFactionEconomySeed, 8> economy{};
    economy[1].xenite_current_q4 = funds;
    SimulationLabExperiment lab;
    lab.reset(cfg, ki, seeds, economy, std::span(&product, 1), techs, {}, {},
              scenario);
    auto &r = lab.runs()[0];
    r.tick = 73;
    r.ai_candidate_cache_reuse_state[1] = 16;
    r.ai_candidate_cache_count[1] = mode == 1 ? 2 : 3;
    r.ai_candidate_cache[1][0] = {mode == 1 ? 7U : 331U,
                                  mode == 1 ? 0x3002 : 0x3001,
                                  mode == 1 ? 2U : 1U};
    r.ai_candidate_cache[1][1] = {mode == 1 ? 8U : 7U, 0x2002, 2};
    r.ai_candidate_cache[1][2] = {8, 0x1002, 2};
    if (full)
      for (int i = 0; i < 64; ++i)
        r.secondary_purchase_requests.push_back({999, 1, 0, 0});
    if (mode == 2)
      r.entities[1].runtime_flags = 1;
    if (mode == 1) {
      r.entities[1].research_active = true;
      r.entities[1].research_technology_id = 6;
      r.entities[1].research_duration_ticks = 1000;
    }
    lab.step(field, 1);
    bool same = r.secondary_purchase_requests.size() ==
                    faction.secondaryArmyAssetCount &&
                (r.ai_candidate_cache_reuse_state[1] != 0) == no_applied;
    for (size_t i = 0; i < 2; ++i) {
      const auto &e = r.entities[i];
      const auto flags = uint32_t(models[i].classState.stateFlags);
      same &= e.research_active == bool(flags & 0x40) &&
              e.research_pending_payment == bool(flags & 0x80);
      if (flags & 0xC0)
        same &= e.research_technology_id == models[i].researchTechnologyId;
    }
    if (!same) {
      for (size_t j = 0; j < 2; ++j)
        std::cerr << "state " << j << " native_flags="
                  << uint32_t(models[j].classState.stateFlags)
                  << " native_id=" << models[j].researchTechnologyId
                  << " editor_active=" << r.entities[j].research_active
                  << " editor_pending="
                  << r.entities[j].research_pending_payment
                  << " editor_id=" << r.entities[j].research_technology_id
                  << '\n';
      std::cerr << "native_queue=" << faction.secondaryArmyAssetCount
                << " editor_queue=" << r.secondary_purchase_requests.size()
                << " native_failure=" << no_applied
                << " editor_cache=" << r.ai_candidate_cache_reuse_state[1]
                << '\n';
      std::cerr << "purchase mismatch case=" << n << " funds=" << funds
                << " full=" << full << " mode=" << mode << '\n';
      return 1;
    }
    ++checks;
  }
  std::cout << "V649_NATIVE_PURCHASE PASS exact_passes=" << checks
            << "; compiled original purchase execution, eligibility, "
               "assignment, sorting, costs and bounded queue registration; "
               "isolated registry and empty unrelated world\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 2;
}
