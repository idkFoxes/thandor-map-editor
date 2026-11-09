#include "thandor/simulation_lab_army_pool.hpp"
#include "thandor/simulation_lab_visibility.hpp"
#include "thandor/simulation_native_walker.hpp"
#include "thandor/simulation_lab_dismantle.hpp"
#include "thandor/retail_water_relaxation.hpp"
#include "thandor/retail_terrain_stamp.hpp"
#include "thandor/simulation_terrain_damage.hpp"
#include "thandor/retail_power_candidate.hpp"
#include "thandor/simulation_lab_ai_behavior_gate.hpp"
#include "thandor/retail_pressure_building.hpp"
#include "thandor/retail_special_site.hpp"
#include "thandor/simulation_lab_ai_grid_classes.hpp"
#include "thandor/simulation_lab_phase7_owner.hpp"
#include "thandor/simulation_lab_owner_schedule.hpp"
#include "thandor/simulation_lab_owner_order.hpp"
#include "thandor/simulation_lab_terrain_refresh.hpp"
#include "thandor/simulation_lab_shot_terrain.hpp"
#include "thandor/simulation_lab_effect_terrain.hpp"
#include "thandor/simulation_lab_phase7_occupancy.hpp"
#include "thandor/projectile_runtime.hpp"
#include "thandor/projectile_contacts.hpp"
#include "thandor/simulation_lab_targeting.hpp"
#include "thandor/simulation_lab_follow.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/retail_presence.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include "thandor/simulation_lab_shot_rays.hpp"
#include "thandor/simulation_lab_shot_lead.hpp"
#include "thandor/simulation_lab_site_placement.hpp"
#include "thandor/simulation_lab_workspace05_score.hpp"
#include "thandor/pioneer_site_reservation.hpp"
#include "thandor/simulation_lab_catalog.hpp"
#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_derived_selection_metrics.hpp"
#include "thandor/retail_random.hpp"
#include "thandor/simulation_lab_ai_workspaces.hpp"
#include "thandor/simulation_lab_ai_owner_list_v718.hpp"
#include "thandor/simulation_lab_ai_base_extents_v724.hpp"
#include "thandor/simulation_lab_ai_distances_v719.hpp"
#include "thandor/simulation_lab_geometry.hpp"
#include "thandor/simulation_placement.hpp"
#include "thandor/retail_placement.hpp"
#include "thandor/ai_unit_producibility_v601.hpp"
#include "thandor/pioneer_resource_grid.hpp"
#include "thandor/pioneer_class14_placement.hpp"
#include "thandor/pioneer_site_score.hpp"
#include "thandor/pioneer_retail_reset.hpp"
#include "thandor/pioneer_ai_dispatch.hpp"
#include "thandor/strategic_factory_terrain_v600.hpp"
#include "thandor/fog_preview.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <numeric>
#include <utility>

namespace thandor {

SimulationLabOneShotEventNode::SimulationLabOneShotEventNode(
    SimulationLabOneShotEvent event, std::shared_ptr<const SimulationLabOneShotEventNode> previous)
    : event(std::move(event)), previous(std::move(previous)) {}

SimulationLabOneShotEventNode::~SimulationLabOneShotEventNode() {
    // Releasing a long unshared suffix must not recurse once per event.
    // Each nested destructor hands its predecessor back to this thread's loop.
    static thread_local std::shared_ptr<const SimulationLabOneShotEventNode>* pending = nullptr;
    if (!previous) return;
    if (pending) {
        *pending = std::move(previous);
        return;
    }
    auto remaining = std::move(previous);
    pending = &remaining;
    while (remaining) {
        auto node = std::move(remaining);
        node.reset();
    }
    pending = nullptr;
}

struct LabResourceSite08 {
    Vec3f point{};
    std::uint32_t army_asset_id{};
    std::int32_t priority{};
};

struct LabScoredSite {
    Vec3f point{};
    std::int32_t score{};
};

struct LabPlacementWorkspaces {
    std::vector<LabScoredSite> general05;
    std::vector<Vec3f> flagged06; // Retained but not consumed: retail also never reads 06.
    std::vector<LabResourceSite08> resource08;
    std::vector<Vec3f> base09;
    std::vector<Vec3f> large10;
};

struct SimulationLabPlanningWorkspaceSnapshot {
    uint32_t faction{};
    LabPlacementWorkspaces placement;
    LabAiOwnerWorkspaceV718 owner_lists;
    SimulationLabAiOwnWorkspaces own;
    std::optional<Vec3f> asset300_anchor;
    std::uint32_t requested_asset_count{};
    FactionTechnologyMask256 technology_admission_mask{};
};
const SimulationLabAiOwnWorkspaces* simulation_lab_retained_planning_owners(
    const SimulationLabRun& run,uint32_t faction) noexcept {
    const auto* snapshot=run.active_planning_snapshot;
    return snapshot&&snapshot->faction==faction?&snapshot->own:nullptr;
}
namespace {
const SimulationLabPlanningWorkspaceSnapshot* lab_retained_planning_snapshot(
    const SimulationLabRun& run,uint32_t faction) noexcept {
    const auto* snapshot=run.active_planning_snapshot;
    return snapshot&&snapshot->faction==faction?snapshot:nullptr;
}

constexpr std::array<std::uint32_t, 4> kConstructionParameterIndices{1U, 2U, 3U, 4U};
constexpr std::array<std::uint32_t, 4> kConstructionAssetIds{0x14AU, 0x14CU, 0x14BU, 0x14DU};
// The world-job AI callbacks occur at tick & 7 == 2 or 6. Faction index is
// (tick >> 3) & 7; tick bit 6 alternates economy and unit-planning phases.
constexpr std::uint32_t kFactoryDoorOpenV = 0x80000U;
// V533 0x0051FF98 class-command dispatch table: these ten runtime classes route to
// ArmyRuntime_ClassCommandHandlerGroupACf at 0x005274D0. The same Group-A membership is used
// both by the retail initial-primary-queue gate and by the recovered terminal relation-D path.
constexpr std::array<std::uint32_t, 10> kGroupAClassCommandClasses{
    4U, 10U, 11U, 13U, 14U, 15U, 16U, 20U, 22U, 23U};
constexpr std::size_t kPendingWorkspaceCapacity = 8U; // legacy direct-test proxy only
constexpr std::size_t kFactionArmyAssetQueueCapacity = 64U;

std::optional<std::uint32_t> retail_triangle0_normal_azimuth_turn16(
    const FieldGrid& field, std::uint32_t x, std::uint32_t y) noexcept {
    if(x==0U||y==0U||x+1U>=field.width()||y+1U>=field.height())return std::nullopt;
    return retail_placement_normal_angles(field,x,y,false)&0xFFFFU;
}

constexpr std::uint8_t kFactionLifecycleInactive = 0U;
constexpr std::uint8_t kFactionLifecycleActive = 1U;
constexpr std::uint8_t kFactionLifecycleEndingPending = 2U;
constexpr std::uint8_t kFactionLifecycleEndedOrTransitioned = 3U;

std::uint32_t initial_packed_relation_row(std::uint32_t faction) noexcept {
    std::uint32_t row = 0x11111111U;
    const std::uint32_t shift = (faction & 7U) * 4U;
    row &= ~(0xFU << shift);
    row |= 0xFU << shift;
    return row;
}

void set_pair_relation_state(std::array<std::uint32_t, 8>& rows,
                             std::uint32_t first, std::uint32_t second,
                             std::uint32_t state) noexcept {
    if (first >= rows.size() || second >= rows.size()) return;
    const std::uint32_t first_shift = (second & 7U) * 4U;
    const std::uint32_t second_shift = (first & 7U) * 4U;
    rows[first] = (rows[first] & ~(0xFU << first_shift)) | ((state & 0xFU) << first_shift);
    rows[second] = (rows[second] & ~(0xFU << second_shift)) | ((state & 0xFU) << second_shift);
}

void apply_relation_group_masks(std::array<std::uint32_t, 8>& rows,
                                std::uint32_t packed_groups,
                                std::uint32_t state) noexcept {
    for (std::uint32_t group_index = 0; group_index < 4U; ++group_index) {
        const std::uint32_t group = packed_groups & 0xFFU;
        if (group == 0U) break; // retail treats the first zero byte as the packed-group terminator
        for (std::uint32_t first = 0; first < 8U; ++first) {
            if ((group & (1U << first)) == 0U) continue;
            for (std::uint32_t second = first + 1U; second < 8U; ++second)
                if ((group & (1U << second)) != 0U)
                    set_pair_relation_state(rows, first, second, state);
        }
        packed_groups >>= 8U;
    }
}

std::uint32_t relation_state(const SimulationLabRun& run,
                             std::uint32_t source, std::uint32_t target) noexcept {
    if (source >= run.faction_packed_relation_states.size() || target >= 8U) return 0U;
    return (run.faction_packed_relation_states[source] >> ((target & 7U) * 4U)) & 0xFU;
}

bool scenario_entity_disabled(const SimulationLabEntity& entity) noexcept {
    if (entity.root_model_component_index >= entity.model_components.size()) return false;
    return (entity.model_components[entity.root_model_component_index].runtime_flags_ec & 0x10U) != 0U;
}

// The child's newer owner callback runs before its factory. Publish the
// beginning-of-callback unlink before servicing the older factory's door state.
void release_native_factory_child_if_clear(SimulationLabRun& run,size_t index){
    if(!run.level_runtime_enabled)return;
    auto& child=run.entities[index];
    if(!child.production_linked_to_parent||child.production_parked_on_pad||
       !child.production_parent_entity_index||*child.production_parent_entity_index>=run.entities.size())return;
    const auto& parent=run.entities[*child.production_parent_entity_index];
    if(parent.runtime_model_class_id!=13U)return;
    // Native destruction clears model references immediately, even if the
    // factory never reached its exit-wait state.
    if(!parent.runtime_removed&&
       (parent.production_behavior_state!=3U||child.motion.collision_radius_q12<=0||
        parent.motion.collision_radius_q12<=0||!simulation_lab_factory_footprint_cleared(child,parent)))return;
    child.production_linked_to_parent=false;
    child.production_parent_entity_index.reset();
    child.military_ai_source_dispatch=true;
    if((child.runtime_model_class_id==1U||child.runtime_model_class_id==2U||child.runtime_model_class_id==3U||
        child.runtime_model_class_id==17U||child.runtime_model_class_id==19U)&&child.combat_movement_initialized){
        // The class callback drops the model link before pathing; native
        // clears LOCKED only after the hierarchy and combat command callbacks.
        child.production_movement_unlock_pending=true;
        child.combat_follow_owned=true;
    }
    if(child.runtime_model_class_id==18U&&child.combat_movement_initialized){
        child.pioneer_movement=child.combat_movement;child.pioneer_movement.flags&=~2U;
        child.pioneer_movement_initialized=true;
    }
}

std::uint32_t research_source_flags(const SimulationLabEntity& entity) noexcept {
    // Retail stores these job states in the same +0xEC model word. The Lab keeps
    // their timers separately; include them whenever interpreting that word.
    return entity.runtime_flags | (entity.root_model_component_index < entity.model_components.size()
        ? entity.model_components[entity.root_model_component_index].runtime_flags_ec : 0U) |
        (entity.research_active ? 0x40U : 0U) |
        (entity.research_pending_payment ? 0x80U : 0U) |
        // A unit factory remains PRODUCING while opening, waiting for exit
        // and closing; native clears bit 0x100 only when the door is shut.
        ((entity.production_building || (entity.runtime_model_class_id==13U&&
          entity.production_behavior_state!=0U)) ? 0x100U : 0U);
}
void set_research_job_flags(SimulationLabEntity& entity,std::uint32_t flags) noexcept {
    // Job flags can enter through either imported +0xEC image. Clear both copies
    // on a transition so a completed raw job cannot reappear on the next scan.
    const auto job=flags&0xC0U;
    entity.runtime_flags=(entity.runtime_flags&~0xC0U)|job;
    if(entity.root_model_component_index<entity.model_components.size()) {
        auto& root=entity.model_components[entity.root_model_component_index];
        root.runtime_flags_ec=(root.runtime_flags_ec&~0xC0U)|job;
    }
    entity.research_active=(job&0x40U)!=0U;
    entity.research_pending_payment=(job&0x80U)!=0U;
}
// Root owners are appended on creation; stable erase preserves surviving order.
// Conversion also appends its replacement. Native LinkOwnerListNode inserts at
// the head, so level-session owner scans walk represented roots newest first.
// Legacy metadata-only API fixtures retain their established forward order.
std::size_t runtime_owner_index(const SimulationLabRun& run,std::size_t visit) noexcept {
    return run.level_runtime_enabled ? run.entities.size()-1U-visit : visit;
}
struct ResearchSourceView {
    std::span<const std::uint32_t> ids;
    std::uint32_t runtime_class_id{};
};
ResearchSourceView research_source_view(const SimulationLabEntity& entity,
                                      const FactionTechnologyMask256& mask) {
    if(entity.runtime_model_hierarchy)return {entity.research_technology_ids,entity.runtime_model_class_id};
    const SimulationLabResearchVariant* selected=nullptr;
    for(const auto& variant:entity.research_model_variants) {
        if(variant.variant_index==0U) selected=&variant;
        if(faction_technology_unlocked(mask,variant.required_technology_id)) selected=&variant;
    }
    if(!selected) return {entity.research_technology_ids,entity.runtime_model_class_id};
    const auto* panel=&selected->panel;
    for(const auto& replacement:selected->replacements)
        if(faction_technology_unlocked(mask,replacement.required_technology_id)) {
            panel=&replacement.panel;
            break; // first unlocked MDL replacement, matching catalog root selection
        }
    return {panel->ids,panel->runtime_class_id};
}

struct AiArmyScoreWeightsV568 {
    std::array<std::int32_t, 8> pressure_category_weights{};
    std::int32_t definition_value60_weight{};
    std::int32_t definition_value0c_weight{};
    std::int32_t army_record74_weight{};
    std::int32_t army_record78_weight{};
    std::int32_t army_record70_weight{};
    std::int32_t nonzero_definition18_bonus{};
    std::int32_t base_score{};
};

// V533 globals 0x0053C63C/0x0053C600/0x0053C678, 15 signed dwords each.  Keep these
// byte-for-byte coefficient profiles rather than inventing a generic unit desirability heuristic.
constexpr AiArmyScoreWeightsV568 kAiArmyScoreVariantA{{0,512,512,512,512,640,512,0},
    512,-64,512,256,0,-256,0};
constexpr AiArmyScoreWeightsV568 kAiArmyScoreVariantB{{0,128,128,128,128,128,128,128},
    0,512,128,0,512,4096,-4096};
constexpr AiArmyScoreWeightsV568 kAiArmyScoreVariantC{{0,384,384,384,384,448,384,512},
    384,192,256,512,0,4096,-4096};

struct AiArmyScoreNormalizersV568 {
    std::int32_t flagged_definition_value0c_maximum{1};
    std::array<std::int32_t, 8> category_value60_maxima{1,1,1,1,1,1,1,1};
};

std::int64_t signed_product(std::uint32_t raw, std::int32_t weight) noexcept {
    return static_cast<std::int64_t>(static_cast<std::int32_t>(raw)) * weight;
}

// Native category research scores potential assets using the slot-0 fallback,
// even while their weapon/chassis technology is locked. Purchase workspace11
// separately requires the first definition of every linked list to be unlocked.
enum class AiArmyScorePurpose { Purchase, Research };

std::int32_t ai_army_static_score_v568(const SimulationLabProductionAsset& asset,
                                       const SimulationLabAiArmyScoreSeed& seed,
                                       const AiArmyScoreWeightsV568& weights,
                                       const AiArmyScoreNormalizersV568& normalizers,
                                       const std::array<std::int32_t, 8>& ai_pressure_values,
                                       std::int32_t maximum_ai_pressure,
                                       AiArmyScorePurpose purpose = AiArmyScorePurpose::Purchase) noexcept {
    if (!seed.resolved || (purpose == AiArmyScorePurpose::Purchase &&
        !seed.all_selected_model_technologies_unlocked)) return 0;
    const auto category = seed.root_category_maximum_index5c;
    if (category >= normalizers.category_value60_maxima.size()) return 0;
    const std::int64_t denominator0c = std::max<std::int32_t>(1, normalizers.flagged_definition_value0c_maximum);
    const std::int64_t denominator60 = std::max<std::int32_t>(1, normalizers.category_value60_maxima[category]);
    std::int64_t weighted_definition_score = weights.base_score;
    if (seed.root_definition_value18 != 0)
        weighted_definition_score += weights.nonzero_definition18_bonus;
    weighted_definition_score += static_cast<std::int64_t>(seed.root_definition_value0c) *
        weights.definition_value0c_weight / denominator0c;
    weighted_definition_score += static_cast<std::int64_t>(seed.root_definition_value60) *
        weights.definition_value60_weight / denominator60;
    weighted_definition_score *= 8;

    // V533 AiArmyCandidate_ComputeFactionWeightedScore scales up to the first two selected child
    // SHT damage vectors by the persistent faction aiPressureValues/maxAiPressure image. Each
    // intermediate is narrowed to a dword before the final Q10/category-normalizer contribution,
    // matching the executable's uint temporary/CONCAT44(x>>22,x<<10) dataflow.
    if (maximum_ai_pressure != 0) {
        // Retail reads no weapon when childListCount == 0, childList0 when >= 1,
        // and childList1 only when >= 2. Later child lists never contribute.
        const auto scored_child_count = std::min<std::size_t>(
            seed.child_list_count, std::min<std::size_t>(2U, seed.child_weapons.size()));
        for (std::size_t weapon_index = 0; weapon_index < scored_child_count; ++weapon_index) {
            const auto& weapon = seed.child_weapons[weapon_index];
            if (!weapon.valid || weapon.reload_divisor30 == 0) continue;
            for (std::size_t pressure_index = 0; pressure_index < ai_pressure_values.size(); ++pressure_index) {
                const auto weighted_damage_base =
                    static_cast<std::int64_t>(weapon.target_class_impact_damage_q12[pressure_index]) *
                    weights.pressure_category_weights[pressure_index] / weapon.reload_divisor30;
                const auto pressure_scaled = weighted_damage_base * ai_pressure_values[pressure_index] /
                    maximum_ai_pressure;
                const std::uint32_t pressure_weighted_damage =
                    static_cast<std::uint32_t>(pressure_scaled);
                weighted_definition_score += static_cast<std::int64_t>(
                    (static_cast<std::uint64_t>(pressure_weighted_damage) << 10U) /
                    static_cast<std::uint32_t>(std::max<std::int32_t>(
                        1, normalizers.category_value60_maxima[pressure_index])));
            }
        }
    }

    const std::int64_t total =
        signed_product(asset.ai_definition_class_value74, weights.army_record74_weight) +
        weighted_definition_score * 12 +
        signed_product(asset.ai_definition_class_value78, weights.army_record78_weight) +
        signed_product(asset.ai_definition_class_value70, weights.army_record70_weight);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(total));
}

void update_ai_pressure_state_v568(SimulationLabRun& run) noexcept {
    if(run.level_runtime_enabled)for(uint32_t f=1;f<8;++f)
        run.faction_economy[f].terrain_contribution_scale_q8=
            run.faction_lifecycle_states[f]==kFactionLifecycleActive &&
            (run.ai_controlled_faction_mask&(1U<<f))!=0U ? run.game_speed_q8 : 0x100U;
    // V533 0x00514350 AiFactionRuntime_RebuildPlanningCapacityState runs after all active faction
    // planning dispatches. For AI factions 1..7 it decays every channel as ((old*3+1)>>2)+1,
    // credits +0x100 using each runtime's classified occupancy bits (MDL +0x5C channel),
    // then rebuilds maximumAiPressure. Metadata-only synthetic callers retain their legacy proxy.
    for (std::uint32_t faction = 1U; faction < run.faction_ai_pressure_values.size(); ++faction) {
        auto& pressure = run.faction_ai_pressure_values[faction];
        for (auto& value : pressure) {
            value=std::bit_cast<int32_t>(((uint32_t(value)*3U+1U)>>2U)+1U);
        }
        run.faction_maximum_ai_pressure[faction] = 0;
    }

    for (const auto& entity : run.entities) {
        if ((!run.level_runtime_enabled && !entity.construction_complete) ||
            !simulation_lab_has_live_root_model_owner(entity) ||
            (!run.level_runtime_enabled&&entity.root_model_terminal) || entity.faction_index == 0U ||
            entity.faction_index >= run.faction_ai_pressure_values.size()) continue;
        // Only the root is linked into the world owner list; attached model
        // runtimes are serviced recursively and do not contribute another entry.
        std::array<uint32_t,8> contributions{};
        if(entity.shot_impact_class_index<contributions.size())
            contributions[entity.shot_impact_class_index]=0x100U;
        for (std::uint32_t target_faction = 1U;
             target_faction < run.faction_ai_pressure_values.size(); ++target_faction) {
            if (target_faction == entity.faction_index) continue;
            // Native presence/history classification, not merely a hostile diplomacy relation.
            if (run.level_runtime_enabled &&
                (entity.model_primary_occupancy_mask&(1U<<(2U*target_faction+1U)))==0U)continue;
            for(size_t channel=0;channel<contributions.size();++channel) {
                auto& value=run.faction_ai_pressure_values[target_faction][channel];
                value=std::bit_cast<int32_t>(uint32_t(value)+contributions[channel]);
            }
        }
    }

    for (std::uint32_t faction = 1U; faction < run.faction_ai_pressure_values.size(); ++faction) {
        const auto& pressure = run.faction_ai_pressure_values[faction];
        run.faction_maximum_ai_pressure[faction] = *std::max_element(pressure.begin(), pressure.end());
    }
}

bool field_has_resource_support(const FieldGrid& field, std::uint32_t occupancy_class) {
    for (std::uint32_t y = 0; y < field.height(); ++y)
        for (std::uint32_t x = 0; x < field.width(); ++x)
            if (field.audit_resource_support_cell(x, y, occupancy_class).valid()) return true;
    return false;
}


// Phase-0 connected-region economy port: registrations are rebuilt from active
// class-14 components in world-owner insertion order. A FLD support cell stores
// one descriptor/owner; the last model writing the cell replaces the previous.
// Flood connectivity matches TerrainRegionCollection_CollectConnectedCellsRecursive:
// same-row x±1; above (x,x+1); below (x-1,x). Each region records at most 256
// registrations; visited terrain cells are counted regardless of that cap.
struct LabExtractionRegistration {
    std::uint32_t faction{};
    std::uint32_t share{};
    std::size_t entity_index{};
    std::size_t component_index{};
};

void lab_pay_connected_resource_regions(SimulationLabRun& run, const FieldGrid& field,
                                       std::uint32_t callback_step_ticks) {
    // Direct/synthetic clients that never seeded the recovered faction scale
    // keep their prior explicit extraction-rate inputs (legacy compatibility).
    const bool enabled=std::any_of(run.faction_economy.begin(),run.faction_economy.end(),
        [](const SimulationLabFactionEconomySeed& seed) {
            return seed.terrain_contribution_scale_q8 != 0U;
        });
    if (!enabled) return;
    run.faction_xenite_extraction_rate_q4_per_tick.fill(0U);
    run.faction_tritium_extraction_rate_q4_per_tick.fill(0U);
    const auto width=field.width(), height=field.height();
    if (width < 3U || height < 3U) return;
    const auto count=static_cast<std::size_t>(width)*height;
    std::vector<std::optional<LabExtractionRegistration>> registrations(count);
    const auto support_index = [&](std::uint32_t x, std::uint32_t y) {
        return static_cast<std::size_t>(y)*width+x;
    };
    for (std::size_t entity_index=0U;entity_index<run.entities.size();++entity_index) {
        auto& entity=run.entities[entity_index];
        for (auto& component : entity.model_components) component.last_resource_payout_q4=0U;
        if (entity.faction_index == 0U || entity.faction_index >= 8U ||
            entity.runtime_removed || !entity.construction_complete) continue;
        for (std::size_t component_index=0U;
             component_index<entity.model_components.size();++component_index) {
            auto& component=entity.model_components[component_index];
            // The last callback registration survives lethal damage until it is
            // consumed by region collection or cleared by model release.
            if(component.runtime_class_id!=14U||component.runtime_removed||
               component.resource_support_class>=2U||
               (!component.resource_cell_registered&&(run.level_runtime_enabled||component.terminal_runtime_flag8||component.integrity<=1)))continue;
            // ArmyRuntimeClass_UpdateGridBoundEffectsAndModels reads the *model node's*
            // world translation, not the owning ArmyRuntime's root XY. Preserve the
            // editor-resolved hierarchy displacement when available. Yaw motion of
            // the parent rotates that displacement in the world XY plane.
            Vec3f model_world=entity.actor.position;
            if (component.extraction_model_world_offset_at_seed) {
                const auto& offset=*component.extraction_model_world_offset_at_seed;
                const auto delta_turns=(entity.actor.body_yaw_turn16 -
                    component.extraction_model_seed_yaw_turn16)&0xFFFFU;
                const float radians=turn16_to_radians(delta_turns);
                const float c=std::cos(radians), sn=std::sin(radians);
                model_world.x+=offset.x*c-offset.y*sn;
                model_world.y+=offset.x*sn+offset.y*c;
                model_world.z+=offset.z;
            }
            // Retail class-14 rounds each model's grid coordinates to the
            // nearest vertex. The fog projection's triangle test differs.
            const auto cell=retail_nearest_world_to_cell(field,
                float_to_q12(model_world.x), float_to_q12(model_world.y));
            if (!cell || cell->x == 0U || cell->y == 0U ||
                cell->x+1U >= width || cell->y+1U >= height) continue;
            const auto support=0x800U << component.resource_support_class;
            if ((field.at(cell->x,cell->y).flags_and_material & support) == 0U) continue;
            // The recovered callback writes support and class-14 share descriptor even
            // before the flood-fill collector reads the cell. A later model wins.
            component.resource_cell_registered=false;
            registrations[support_index(cell->x,cell->y)] = LabExtractionRegistration{
                entity.faction_index, component.resource_extraction_weight_u8 & 0xFFU,
                entity_index,component_index};
        }
    }
    const auto valid_cell = [&](int x, int y, std::uint32_t bit) -> bool {
        if (x <= 0 || y <= 0 || x+1 >= static_cast<int>(width) ||
            y+1 >= static_cast<int>(height)) return false;
        const auto flags=field.at(static_cast<std::uint32_t>(x),static_cast<std::uint32_t>(y)).flags_and_material;
        // Hard FLD edge flags must terminate the recovered region walk.
        return (flags & 0x88006000U) == 0U && (flags & bit) != 0U;
    };
    // The game's region scratch is 256 entries. Gather in deterministic
    // triangular-lattice order while counting ALL cells in each region.
    for (std::uint32_t kind=0U;kind<2U;++kind) {
        const std::uint32_t bit=0x800U<<kind;
        std::vector<std::uint8_t> seen(count);
        for (std::uint32_t y=1U;y+1U<height;++y) {
            for (std::uint32_t x=1U;x+1U<width;++x) {
                const auto origin=support_index(x,y);
                if (seen[origin] || !valid_cell(static_cast<int>(x),static_cast<int>(y),bit)) continue;
                std::vector<std::size_t> pending{origin};
                seen[origin]=1U;
                std::uint32_t visited=0U;
                std::vector<LabExtractionRegistration> entries;
                while (!pending.empty()) {
                    const auto index=pending.back(); pending.pop_back();
                    ++visited;
                    if (registrations[index] && entries.size()<256U)
                        entries.push_back(*registrations[index]);
                    const int cx=static_cast<int>(index%width);
                    const int cy=static_cast<int>(index/width);
                    constexpr std::array<std::array<int,2>,6> neighbors{{
                        {{-1,0}},{{1,0}},{{0,-1}},{{1,-1}},{{-1,1}},{{0,1}}}};
                    for (const auto& delta : neighbors) {
                        const int nx=cx+delta[0],ny=cy+delta[1];
                        if (!valid_cell(nx,ny,bit)) continue;
                        const auto next=support_index(static_cast<std::uint32_t>(nx),
                                                      static_cast<std::uint32_t>(ny));
                        if (seen[next]) continue;
                        seen[next]=1U;
                        pending.push_back(next);
                    }
                }
                if (entries.empty()) continue;
                const std::uint32_t cells_per_entry=visited/static_cast<std::uint32_t>(entries.size());
                for (const auto& entry : entries) {
                    const auto scale=run.faction_economy[entry.faction].terrain_contribution_scale_q8;
                    // Original expression is dword multiplication followed by >> 15.
                    const std::uint32_t rate=static_cast<std::uint32_t>(
                        static_cast<std::uint64_t>(cells_per_entry)*2U*entry.share*scale) >> 15U;
                    auto& economy=run.faction_economy[entry.faction];
                    auto& tick_rate=kind == 0U
                        ? run.faction_xenite_extraction_rate_q4_per_tick[entry.faction]
                        : run.faction_tritium_extraction_rate_q4_per_tick[entry.faction];
                    tick_rate+=rate;
                    const std::uint32_t contribution=rate*callback_step_ticks;
                    run.entities[entry.entity_index]
                        .model_components[entry.component_index].last_resource_payout_q4=contribution;
                    if (kind == 0U) {
                        economy.xenite_current_q4+=contribution;
                        run.faction_score[entry.faction].xenite_extracted_total_q4+=contribution;
                    } else {
                        economy.tritium_current_q4+=contribution;
                        run.faction_score[entry.faction].tritium_extracted_total_q4+=contribution;
                    }
                }
            }
        }
    }
    // Retail caps stock only after paying both Xenite and Tritium passes.
    for (std::size_t faction=0U;faction<run.faction_economy.size();++faction) {
        auto& economy=run.faction_economy[faction];
        if (economy.xenite_storage_limit_q4)
            economy.xenite_current_q4=std::min(economy.xenite_current_q4,*economy.xenite_storage_limit_q4);
        if (economy.tritium_storage_limit_q4)
            economy.tritium_current_q4=std::min(economy.tritium_current_q4,*economy.tritium_storage_limit_q4);
    }
}

// The retail AI owns independent bounded workspaces, rebuilt once per faction. This
// implementation preserves workspace08's scan order, 32-entry limit, duplicate-replacement
// rule, live-structure separation and priority. FieldGrid currently does not retain the
// game's four-samples-per-cell GridScratchPrimary/occupancy presence image, so source
// footprint admission is still an explicit reconstruction (see Stage8 checkpoint).
std::int64_t lab_axis_manhattan_q12(Vec3f a, Vec3f b) noexcept {
    return std::llabs(static_cast<long long>(float_to_q12(a.x)) - float_to_q12(b.x)) +
           std::llabs(static_cast<long long>(float_to_q12(a.y)) - float_to_q12(b.y));
}

// Keep an upper-bounded distance, not a float centroid or a connected-component flag.
// Keep all distance selectors on the same bounded owner-list snapshot that
// AiPlanning_RebuildFactionWorkspaces exposes to site and military consumers.
LabAiOwnerWorkspaceV718 lab_ai_owner_workspaces_v718(
        const SimulationLabRun& run, const FieldGrid& field, std::uint32_t faction) {
    if(const auto* snapshot=lab_retained_planning_snapshot(run,faction))
        return snapshot->owner_lists;
    return lab_ai_admit_owner_list_v718(run.entities.size(), [&](std::size_t i) {
        const auto& entity=run.entities[i];
        if (!lab_ai_has_world_model_owner_v722(entity.runtime_removed,entity.faction_index) ||
            !simulation_lab_has_live_root_model_owner(entity))
            return LabAiOwnerKindV718::Skip;
        if (entity.faction_index==faction)
            return entity.army_asset_id<300U ? LabAiOwnerKindV718::Unit01 :
                LabAiOwnerKindV718::Structure00;
        switch (simulation_lab_ai_hostile_perception(run,field,faction,entity)) {
            case SimulationLabHostilePerception::Visible: return LabAiOwnerKindV718::Visible02;
            case SimulationLabHostilePerception::Unseen: return LabAiOwnerKindV718::Unseen03;
            default: return LabAiOwnerKindV718::Skip;
        }
    });
}

// WS03 is one shared allocation. Rebuilds overwrite only their admitted prefix;
// the group selector's doubled stride can read a previous faction's live models.
void lab_retain_unseen_workspace(SimulationLabRun& run,const LabAiOwnerWorkspaceV718& ws) {
    auto& retained=run.ai_unseen03_model_slots;
    retained.resize(512U,SIZE_MAX);
    for(size_t i=0;i<ws.unseen03.size()&&i<retained.size();++i){
        const auto& e=run.entities[ws.unseen03[i]];
        retained[i]=e.root_model_component_index<e.model_components.size()
            ?e.model_components[e.root_model_component_index].native_model_pool_slot:SIZE_MAX;
    }
    run.ai_unseen03_written_prefix=std::max(run.ai_unseen03_written_prefix,ws.unseen03.size());
}

std::int64_t lab_nearest_entity_distance_q12(const SimulationLabRun& run,
                                              std::uint32_t faction, const FieldGrid& field,
                                              Vec3f point, std::uint32_t workspace,
                                              const LabAiOwnerWorkspaceV718* retained=nullptr) {
    // Source distances only use entries with a linked model, not unassigned
    // purchase/build requests. The owner-list snapshot contains live entries.
    const auto uncached=retained?LabAiOwnerWorkspaceV718{}:
        lab_ai_owner_workspaces_v718(run,field,faction);
    const auto& ws=retained?*retained:uncached;
    std::int64_t best = std::numeric_limits<std::int32_t>::max();
    for (const auto index:ws.workspace(workspace)) {
        const auto& entity=run.entities[index];
        const auto model=lab_ai_model_point_v719(entity.actor.position);
        const auto path=entity.ai_path_coordinate_q12.value_or(model);
        best=std::min(best,lab_ai_workspace_distance_v719(workspace,model,path,
            lab_ai_model_point_v719(point)));
    }
    return best;
}

void lab_append_resource08(LabPlacementWorkspaces& ws, const SimulationLabRun& run,
                            const LabAiOwnerWorkspaceV718& owner_workspaces, const FieldCell& cell) {
    const std::uint32_t id = (cell.flags_and_material & 0x800U) != 0U ? 0x14AU : 0x14CU;
    const Vec3f point{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),
                      q12_to_float(cell.terrain_height_q12)};
    // Workspace00's class-13 marker (packed point 1:5) inhibits a resource site
    // inside 2.0 world units. Lookup 1:5 is the factory exit, distinct from the spawn
    // marker 0:5.
    for (const auto index:owner_workspaces.structures00) {
        const auto& source=run.entities[index];
        if (source.runtime_model_class_id!=13U) continue;
        if (!source.production_exit_world) continue;
        const Vec3f& marker_world = *source.production_exit_world;
        if (lab_ai_class13_marker_near_v719(lab_ai_model_point_v719(point),
                lab_ai_model_point_v719(marker_world))) return;
    }
    const std::int64_t separation = static_cast<std::int32_t>(run.ai_parameters[0x50U]);
    const std::int64_t duplicate = (static_cast<std::uint64_t>(run.ai_parameters[0x50U]) * 85U) >> 8U;
    std::int64_t nearest = std::numeric_limits<std::int32_t>::max();
    for (const auto index:owner_workspaces.structures00) {
        const auto& entity=run.entities[index];
        const auto dx = std::llabs(static_cast<long long>(float_to_q12(point.x)) -
                                   float_to_q12(entity.actor.position.x));
        const auto dy = std::llabs(static_cast<long long>(float_to_q12(point.y)) -
                                   float_to_q12(entity.actor.position.y));
        if (entity.army_asset_id == id && dx < separation && dy < separation)
            return; // Retail workspace00 separation (admitted own buildings only).
        nearest = std::min<std::int64_t>(nearest, dx + dy);
    }
    const auto priority = static_cast<std::int32_t>(std::max<std::int64_t>(0,
        static_cast<std::int64_t>(run.ai_parameters[0x51U]) - nearest));
    for (auto& prior : ws.resource08) {
        if (prior.army_asset_id != id) continue;
        const auto dx = std::llabs(static_cast<long long>(float_to_q12(point.x)) -
                                   float_to_q12(prior.point.x));
        const auto dy = std::llabs(static_cast<long long>(float_to_q12(point.y)) -
                                   float_to_q12(prior.point.y));
        if (dx < duplicate && dy < duplicate) {
            if (priority > prior.priority) { prior.point=point; prior.priority=priority; }
            return; // First-wins on score ties; no reordering when replacing.
        }
    }
    if (ws.resource08.size() < 32U) ws.resource08.push_back({point,id,priority});
}

// Stage 9: reproduce the *terrain-classification* portion of
// GridScratch_RebuildTerrainAndRuntimeClassificationMasks, including the unusual
// staggered 4x expansion. This is not the whole mutable scratch image: the
// retail low/high distance-band influence writers and two reachability flood
// fills are still separate work. Never manufacture those bits from FLD flags.
using LabAiTerrainScratch = SimulationLabRetailScratchImage;

void lab_prepare_missing_model_geometry(SimulationLabRun& run,const FieldGrid& field,
                                        const SimulationLabRuntimeCatalog* catalog) {
    if(!catalog||catalog->definitions.empty())return;
    for(auto& entity:run.entities){
        if(run.tick==0U)simulation_lab_place_root_on_surface(entity,field,*catalog);
        simulation_lab_prepare_missing_model_geometry(entity,field,*catalog);
    }
}

V565PathTerrainCache& lab_navigation_cache(SimulationLabRun& run,const FieldGrid& field,
                                         V565PathTerrainCache& fallback,
                                         const SimulationLabRuntimeCatalog* catalog) {
    if (!run.level_runtime_enabled || !run.retail_scratch || !catalog ||
        catalog->definitions.empty()) return fallback;
    auto& cache=run.runtime_navigation_cache;
    cache.field=&field;
    if (cache.runtime_states.get()!=&run.retail_scratch->state)
        cache.runtime_states=std::shared_ptr<const std::vector<uint32_t>>(
            run.retail_scratch,&run.retail_scratch->state);
    return cache;
}



std::optional<std::size_t> lab_project_runtime_to_scratch(
    const LabAiTerrainScratch& scratch, const SimulationLabEntity& entity) noexcept;

// Recovered GridInfluence_WalkDistanceBandsAroundWorldPoint, including its
// asymmetric two-scratch-row diagonal walkers, projected world centre, and
// eight original ring radii. Distance-band writes are OR operations; no
// near/far ring is artificially made exclusive of another ring.
void lab_rebuild_grid_influence_snapshot(LabAiTerrainScratch& scratch,
    const SimulationLabRun& run, std::span<const SimulationLabSiteOwner> owners, bool use_model_owners) {
    // The game clears all 16 distance-band bits and calls each live model's
    // gridInfluenceAdd callback in world owner-list order. The represented model
    // adapter supplies surviving components and static roots. OR-only writes
    // commute; native registration order is still a separate integration gap.
    for (auto& mask:scratch.state) mask &= ~0x00FFFF00U;
    if (const auto trace=std::getenv("THANDOR_SIMLAB_INFLUENCE_TRACE_TICK"); trace&&std::strtoul(trace,nullptr,10)==run.tick)
        for (const auto& owner:owners) std::fprintf(stderr,"INFLUENCE tick=%u class=%u radius=%d xy=%d,%d\n",run.tick,owner.support.runtime_class,owner.radius_q12,owner.support.position.x,owner.support.position.y);
    if (use_model_owners) {
        for (const auto& owner:owners)
            retail_placement_stamp_influence(scratch,owner.support.position,owner.radius_q12,
                owner.support.runtime_class);
    } else {
        for (const auto& entity:run.entities)
            // World-owner release, not the start of terminal effects, ends a
            // model's contribution to the distance-band rebuild.
            if (simulation_lab_has_live_root_model_owner(entity))
                retail_placement_stamp_influence(scratch,
                    {float_to_q12(entity.actor.position.x),float_to_q12(entity.actor.position.y)},
                    entity.motion.collision_radius_q12,entity.runtime_model_class_id);
    }
}

// GridScratch_FloodFillFromPlacedRuntimeModels and the two reachability passes in
// world/pathing/scratch_grid.cpp. These mutate terrain-class bits, not faction
// vision or the AI's constructor-reach occupancy bytes.
// The reference projects each placed MODEL owner node to the 4x grid. Native-catalog
// runs use represented surviving components and static roots with their contact kinds.
std::optional<std::size_t> lab_project_runtime_to_scratch(
    const LabAiTerrainScratch& scratch, const SimulationLabEntity& entity) noexcept {
    // FIELD_GRID_WORLD_X_TO_COLUMN_Q20 = 0x1C6E9C;
    // FIELD_GRID_WORLD_Y_TO_ROW_Q20 = -0x20C8CC;
    // GRID_SCRATCH_INDEX_BIAS_Q12 = 0x800, CELL_SHIFT = 10.
    const auto x=static_cast<std::int64_t>(float_to_q12(entity.actor.position.x));
    const auto y=static_cast<std::int64_t>(float_to_q12(entity.actor.position.y));
    const auto row_term=(y*static_cast<std::int64_t>(-0x20C8CC)) >> 21;
    const auto col_term=(x*static_cast<std::int64_t>(0x1C6E9C)) >> 20;
    const auto col=(col_term-row_term+0x800) >> 10;
    const auto row=(row_term*2+0x800) >> 10;
    if (col<0 || row<0 || col>=scratch.width || row>=scratch.height)
        return std::nullopt;
    return static_cast<std::size_t>(row)*scratch.width+static_cast<std::size_t>(col);
}


void lab_apply_retail_unreachable_terrain_classes(
    LabAiTerrainScratch& scratch, const SimulationLabRun& run,
    std::span<const SimulationLabSiteOwner> owners, bool use_model_owners) {
    // The recovered rebuild skips BOTH flood passes when the world owner list
    // is null. In the Lab, all placed model owner records live in run.entities.
    if (use_model_owners ? owners.empty() : run.entities.empty()) return;
    constexpr std::uint32_t visited=0x00000001U, blocked=0x80000000U;
    constexpr std::uint32_t upper_classes=0x70000000U; // bits 28..30
    constexpr std::uint32_t lower_classes=0x0E000000U; // bits 25..27
    std::vector<std::size_t> seeds;
    seeds.reserve(run.entities.size());
    if (const auto trace=std::getenv("THANDOR_SIMLAB_INFLUENCE_TRACE_TICK"); trace&&std::strtoul(trace,nullptr,10)==run.tick)
        for (const auto& owner:owners) std::fprintf(stderr,"INFLUENCE tick=%u class=%u radius=%d xy=%d,%d\n",run.tick,owner.support.runtime_class,owner.radius_q12,owner.support.position.x,owner.support.position.y);
    if (use_model_owners) {
        for (const auto& owner:owners) {
            if (owner.support.faction==0U || owner.contact_kind==1) continue;
            if (const auto index=retail_placement_scratch_index(scratch,owner.support.position))
                seeds.push_back(*index);
        }
    } else {
    for (const auto& entity:run.entities) {
        // GridScratch_FloodFillFromPlacedRuntimeModels selects owner-class MODEL,
        // a nonzero owning faction, and MDL +0x278 != 1. Non-model temporary
        // effects are not promoted to occupancy sources.
        // Native flood traversal follows linked MODEL owners, including
        // terminal-effect roots until their owner node is released.
        if (entity.faction_index==0U ||
            !simulation_lab_has_live_root_model_owner(entity)) continue;
        const std::int32_t contact_kind=
            entity.root_model_component_index<entity.model_components.size()
            ? entity.model_components[entity.root_model_component_index].placement_contact_kind
            : entity.motion.placement_contact_kind;
        if (contact_kind==1) continue;
        if (const auto index=lab_project_runtime_to_scratch(scratch,entity))
            seeds.push_back(*index);
    }
    }
    // A reduced Lab entity may not expose a projectable MODEL owner (especially
    // synthetic fixtures without an MDL). A flood with no valid seeds is not
    // evidence that the entire world is unreachable. Preserve terrain classes
    // until an actual owner seed is available; do not manufacture barriers.
    if (seeds.empty() && !use_model_owners) return;
    std::vector<std::size_t> queue;
    // First flood: mark all currently unreachable OPEN cells as impassable
    // for high terrain classes (28..30). Original runs all model seeds in
    // owner-list order; their visitation order cannot alter the final union.
    for (const auto seed:seeds)
        retail_flood_reachable_scratch(scratch,seed,blocked|upper_classes|visited,queue);
    for (auto& mask:scratch.state) {
        if ((mask&(upper_classes|visited))==0U) mask |= upper_classes;
        mask &= ~visited;
    }
    // Second flood: independently mark unreachable terrain for classes 25..27.
    for (const auto seed:seeds)
        retail_flood_reachable_scratch(scratch,seed,blocked|lower_classes|visited,queue);
    for (auto& mask:scratch.state)
        if ((mask&(lower_classes|visited))==0U) mask |= lower_classes;
    // The reference leaves visited set after the second fill; it is removed
    // on the next scratch terrain rebuild, not by the AI site scanner.
}

// GridScratch_PropagateFieldOccupancyMaskNeighborhood (scratch_grid.cpp).
// The original addresses are DWORD offsets into 8-byte scratch cells. Convert
// the literal offsets to scratch-cell indices without changing their order.
void lab_propagate_field_presence_into_scratch(LabAiTerrainScratch& scratch,
                                               const FieldGrid& field) {
    retail_propagate_field_presence_into_scratch(scratch,field);
}

struct LabPhase7ModelOwnerCallback {
    std::int32_t world_x_q12{};
    std::int32_t world_y_q12{};
    std::int32_t reference_height_q12{};
    std::uint32_t faction_index{};
    std::uint32_t visibility_radius_world_units{};
    std::uint32_t occupancy_mark_radius_units{};
    std::uint8_t visibility_marker{};
    bool sector_eligible{};
};

std::uint32_t lab_phase7_static_visibility_radius(std::int32_t radius_q12) noexcept {
    // TerrainProjectedScan_SetStepLimitFromRadius clamps an authored zero to
    // one scan step. Negative metadata is not a valid native radius and stays
    // disabled rather than being reinterpreted as an unsigned distance.
    if (radius_q12 == 0) return 1U;
    return radius_q12 > 0 ? static_cast<std::uint32_t>(radius_q12) : 0U;
}

std::vector<LabPhase7ModelOwnerCallback> lab_phase7_model_owner_callbacks(
    const SimulationLabRun& run, const FieldGrid& field,
    const SimulationLabRuntimeCatalog& catalog,
    std::span<const SimulationLabStaticPlacementOwner> static_owners) {
    struct OrderedOwner {
        SimulationLabOwnerOrderKey key{};
        std::size_t index{};
        bool is_static{};
    };
    std::vector<OrderedOwner> ordered;
    ordered.reserve(run.entities.size()+static_owners.size());
    for(std::size_t index=0;index<run.entities.size();++index){
        const auto& entity=run.entities[index];
        if(!simulation_lab_phase7_occlusion_owner_eligible(run,entity))continue;
        ordered.push_back({{entity.synthetic,entity.construction_start_tick,
            entity.source_placement_index,index},index,false});
    }
    for(std::size_t index=0;index<static_owners.size();++index){
        const auto& owner=static_owners[index];
        if(owner.faction_index==0U||owner.faction_index>=8U)continue;
        ordered.push_back({{false,0U,(owner.source_placement_index==SIZE_MAX?0U:owner.source_placement_index),index},index,true});
    }
    std::sort(ordered.begin(),ordered.end(),[](const auto& left,const auto& right){
        return simulation_lab_owner_newer_first(left.key,right.key);
    });

    std::vector<LabPhase7ModelOwnerCallback> callbacks;
    callbacks.reserve(ordered.size());
    const auto reference_height=[](const Vec3f& position,
                                   std::int32_t offset_q12) noexcept {
        return std::bit_cast<int32_t>(uint32_t(float_to_q12(position.z))+uint32_t(offset_q12));
    };
    for(const auto& entry:ordered){
        if(!entry.is_static){
            const auto& entity=run.entities[entry.index];
            const auto root=entity.root_model_component_index;
            if(root>=entity.model_components.size())continue;
            const auto& component=entity.model_components[root];
            Vec3f position=entity.actor.position;
            const auto poses=simulation_lab_current_model_poses(entity,field,catalog);
            if(component.source_model_node_index<poses.size())
                position=poses[component.source_model_node_index].position;
            callbacks.push_back({float_to_q12(position.x),float_to_q12(position.y),
                reference_height(position,entity.fog_reference_height_offset_q12),
                entity.faction_index,
                simulation_lab_phase7_occlusion_preview_radius(run,entity),
                simulation_lab_phase7_owner_occupancy_radius(run,entity),
                retail_visibility_marker_for_model_class(entity.runtime_model_class_id),
                simulation_lab_phase7_sector_owner_eligible(run,entity)});
            continue;
        }
        const auto& owner=static_owners[entry.index];
        const auto definition=catalog.definitions.find(owner.model_definition_id);
        if(definition==catalog.definitions.end())continue;
        const auto& model=definition->second;
        const auto relation=run.active_view_faction_index<8U
            ?(run.faction_packed_relation_states[owner.faction_index]>>
              (run.active_view_faction_index*4U))&0xFU:0U;
        const auto visibility_radius=owner.derived_selection_metrics_valid
            ?owner.visibility_radius_q12:model.simulation.fog_uncover_radius_q12;
        const auto occupancy_radius=owner.derived_selection_metrics_valid
            ?owner.occupancy_mark_radius_units:model.simulation.occupancy_mark_radius_units;
        const auto height_offset=owner.derived_selection_metrics_valid
            ?owner.visibility_height_offset_q12:model.simulation.fog_reference_height_offset_q12;
        const auto runtime_class=owner.derived_selection_metrics_valid
            ?owner.runtime_class_id:model.runtime_class_id;
        callbacks.push_back({float_to_q12(owner.pose.position.x),
            float_to_q12(owner.pose.position.y),
            reference_height(owner.pose.position,height_offset),owner.faction_index,
            lab_phase7_static_visibility_radius(visibility_radius),occupancy_radius,
            retail_visibility_marker_for_model_class(runtime_class),(relation&0x8U)!=0U});
    }
    return callbacks;
}

// Reconstruct phase-7 FLD occupancy from persistent history and the native
// MODEL-owner callback inputs. Catalog-backed level runs rebuild projected
// occlusion and MDL +0x27C sector bit 1 from current root-model poses, including
// represented static LEV roots. Metadata-only fixtures retain the older
// projection buffer and direct-entity compatibility path.
void lab_rebuild_phase7_field_presence(LabAiTerrainScratch& scratch,
                                        const SimulationLabRun& run,
                                        const FieldGrid& field,
                                        const SimulationLabRuntimeCatalog* catalog=nullptr,
                                        std::span<const SimulationLabStaticPlacementOwner> static_owners={},
                                        bool is_world_job=false,
                                        bool has_world_owner=true) {
    const auto cell_count=static_cast<std::size_t>(field.width())*field.height();
    const bool use_model_owner_callbacks=catalog!=nullptr&&run.level_runtime_enabled;
    const auto model_owner_callbacks=use_model_owner_callbacks
        ?lab_phase7_model_owner_callbacks(run,field,*catalog,static_owners)
        :std::vector<LabPhase7ModelOwnerCallback>{};
    std::vector<std::uint64_t> model_owner_projection;
    if (use_model_owner_callbacks) {
        std::vector<FogPreviewObserver> observers;
        observers.reserve(model_owner_callbacks.size());
        for (const auto& owner:model_owner_callbacks) {
            const auto visibility_mask=retail_faction_visibility_mask(
                run.faction_packed_relation_states[owner.faction_index],
                owner.visibility_marker);
            if (visibility_mask==0U || owner.visibility_radius_world_units==0U) continue;
            observers.push_back({owner.world_x_q12,owner.world_y_q12,
                owner.reference_height_q12,owner.visibility_radius_world_units,
                visibility_mask});
        }
        model_owner_projection=build_projected_fog_preview_occupancy(field,observers);
    }
    // Native phase 7 retains the high/persistent bit in each existing FLD
    // occupancy byte. Preserve it across scratch snapshots even if the Lab's
    // compressed exploration history has not yet absorbed an imported field.
    auto previous_occupancy_masks=std::move(scratch.field_occupancy_masks);
    scratch.field_presence_by_faction.assign(cell_count,0U);
    scratch.field_occupancy_bit1_by_faction.assign(cell_count,0U);
    scratch.field_occupancy_masks.assign(cell_count,0U);
    // TerrainHexScan_AllSectors, occupancy.cpp: direction order, four steps
    // per straight cell and seven per sector diagonal. The original passes a
    // MAX(1, MIN(255, radius/0x240)) limit and stops at each map-edge cell.
    constexpr std::array<std::pair<int,int>,6> directions{{
        {1,0},{1,-1},{0,-1},{-1,0},{-1,1},{0,1}}};
    const auto inside=[&](int x,int y) noexcept {
        return x>=0 && y>=0 &&
            static_cast<std::uint32_t>(x)<field.width() &&
            static_cast<std::uint32_t>(y)<field.height();
    };
    const auto mark_sector=[&](std::int32_t owner_world_x_q12,
                               std::int32_t owner_world_y_q12,
                               std::uint32_t faction_index,
                               std::uint32_t radius) {
        const auto limit=std::clamp<std::uint32_t>(radius/0x240U,1U,255U);
        const auto world_x=static_cast<std::int64_t>(owner_world_x_q12);
        const auto world_y=static_cast<std::int64_t>(owner_world_y_q12);
        // FieldGrid_WorldToGridQ12 floor cell (NOT nearest grid vertex used by
        // projected fog); Q12 column is skewed by half of Y's row component.
        const auto diagonal=(world_y*-0x20C8CCLL)>>21U;
        const auto column_q12=((world_x*0x1C6E9CLL)>>20U)-diagonal;
        const auto row_q12=diagonal*2;
        const auto center_x=static_cast<int>(column_q12>>12U);
        const auto center_y=static_cast<int>(row_q12>>12U);
        if (!inside(center_x,center_y) ||
            (field.at(static_cast<std::uint32_t>(center_x),
                      static_cast<std::uint32_t>(center_y)).flags_and_material&0x88006000U)!=0U)
            return;
        const auto flag=static_cast<std::uint8_t>(1U<<faction_index);
        const auto mark=[&](int x,int y) {
            if (!inside(x,y)) return false;
            if ((field.at(static_cast<std::uint32_t>(x),
                          static_cast<std::uint32_t>(y)).flags_and_material&0x88006000U)!=0U)
                return false;
            scratch.field_occupancy_bit1_by_faction[
                static_cast<std::size_t>(y)*field.width()+static_cast<std::size_t>(x)]|=flag;
            return true;
        };
        mark(center_x,center_y);
        const auto leg=[&](int x,int y,int direction,std::uint32_t step) {
            while (step<limit) {
                if (!mark(x,y)) return;
                x+=directions[direction].first;
                y+=directions[direction].second;
                step+=4U;
            }
        };
        for (int sector=0;sector<6;++sector) {
            const auto next=(sector+1)%6;
            int x=center_x+directions[sector].first;
            int y=center_y+directions[sector].second;
            std::uint32_t step=0;
            while(step<limit) {
                if(!mark(x,y)) break;
                leg(x+directions[sector].first,y+directions[sector].second,
                    sector,step+4U);
                if(limit<=step+4U) break;
                const int between_x=x+directions[next].first;
                const int between_y=y+directions[next].second;
                if(!mark(between_x,between_y)) break;
                step+=7U;
                leg(between_x+directions[next].first,
                    between_y+directions[next].second,next,step);
                x=between_x+directions[sector].first;
                y=between_y+directions[sector].second;
            }
        }
    };
    if (use_model_owner_callbacks) {
        for (const auto& owner:model_owner_callbacks) {
            if (!owner.sector_eligible) continue;
            mark_sector(owner.world_x_q12,owner.world_y_q12,owner.faction_index,
                owner.occupancy_mark_radius_units);
        }
    } else for (const auto& own:run.entities) {
        if (!simulation_lab_phase7_sector_owner_eligible(run,own)) continue;
        // Native ArmyRuntimeSlot.occupancyMarkRadius is derived from live
        // MDL +0x27C. A zero native radius is valid; Stage10 +0x19C is NOT
        // a replacement for it. Retain the proxy only for synthetic fixtures.
        mark_sector(float_to_q12(own.actor.position.x),float_to_q12(own.actor.position.y),
            own.faction_index,simulation_lab_phase7_owner_occupancy_radius(run,own));
    }
    // A naked/direct Simulation Lab entity has no authored MDL visibility
    // radius or depth class.  In those fixtures, an all-zero *valid-sized*
    // projected fog buffer must not silently erase construction occupancy.
    // Preserve a tightly scoped non-retail compatibility source from its
    // explicit class-specific build-area extent. Retail/LEV actors with a
    // valid vision model never use this substitute. The fallback is kept
    // separate from the retail hex bit-1 writer and is not used for rendering.
    std::array<bool,8> has_authored_visibility{};
    for (const auto& own : run.entities)
        if (own.faction_index>0U && own.faction_index<8U &&
            !own.root_model_terminal && own.fog_uncover_radius_q12>0)
            has_authored_visibility[own.faction_index]=true;
    std::vector<std::uint8_t> missing_mdl_build_presence(cell_count,0U);
    if (!run.level_runtime_enabled) {
        for (const auto& own : run.entities) {
            if (own.faction_index==0U || own.faction_index>=8U ||
                own.root_model_terminal || !own.construction_complete ||
                has_authored_visibility[own.faction_index] ||
                own.runtime_class_specific_19c_raw<=0) continue;
            const auto radius=static_cast<std::uint64_t>(own.runtime_class_specific_19c_raw);
            const auto radius_squared=radius*radius;
            const auto cx=static_cast<std::int64_t>(float_to_q12(own.actor.position.x));
            const auto cy=static_cast<std::int64_t>(float_to_q12(own.actor.position.y));
            for (std::size_t index=0;index<cell_count;++index) {
                const auto& cell=field.cells()[index];
                const auto dx=std::llabs(static_cast<long long>(cell.world_x_q12)-cx);
                const auto dy=std::llabs(static_cast<long long>(cell.world_y_q12)-cy);
                if (static_cast<std::uint64_t>(dx)>radius ||
                    static_cast<std::uint64_t>(dy)>radius) continue;
                if (static_cast<std::uint64_t>(dx)*dx+
                    static_cast<std::uint64_t>(dy)*dy<=radius_squared)
                    missing_mdl_build_presence[index]|=
                        static_cast<std::uint8_t>(1U<<own.faction_index);
            }
        }
    }
    // FieldGrid_ClearOccupancyMaskBits0To6AllCells preserves bit 7 in all
    // eight faction slots. The projected buffer provides *current* bits; the
    // retained occupancy field and compressed Lab history provide explored
    // bits. Hex sector bit 1 is independent from the 0xF9 site predicate.
    // Native phase 7 clears transient occupancy before dispatching owner
    // callbacks. An empty world-owner chain dispatches no occlusion writers,
    // even when a previously cached visibility preview still has markers.
    // The startup scratch initializer is a separate non-world-job path.
    const bool projected=(use_model_owner_callbacks
        ?model_owner_projection.size()==cell_count
        :run.current_visibility_occupancy.size()==cell_count) &&
        simulation_lab_phase7_projection_available(is_world_job,has_world_owner);
    const auto ended_faction=is_world_job
        ? simulation_lab_phase7_ended_faction_mask(
            run.active_view_faction_index,run.local_faction_ended)
        : 0U;
    for (std::size_t cell=0;cell<cell_count;++cell) {
        std::uint8_t explored_factions=0U;
        for (std::uint32_t faction=1U;faction<8U;++faction) {
            const auto& explored=run.faction_explored_cell_bits[faction];
            if (cell/64U<explored.size() &&
                (explored[cell/64U]&(std::uint64_t{1}<<(cell%64U)))!=0U)
                explored_factions|=static_cast<std::uint8_t>(1U<<faction);
        }
        const auto previous=previous_occupancy_masks.size()==cell_count
            ?previous_occupancy_masks[cell]:0ULL;
        const auto current=projected
            ?(use_model_owner_callbacks?model_owner_projection[cell]
                                      :run.current_visibility_occupancy[cell])
            :0ULL;
        const auto mask=simulation_lab_phase7_merge_occupancy_mask(
            previous,current,scratch.field_occupancy_bit1_by_faction[cell],
            static_cast<std::uint8_t>(missing_mdl_build_presence[cell]|ended_faction),
            explored_factions);
        scratch.field_occupancy_masks[cell]=mask;
        scratch.field_presence_by_faction[cell]=simulation_lab_phase7_present_factions(mask);
    }
    // Native world job case 7 enters propagation only with a non-null
    // world-owner chain. Startup classification is a separate operation.
    if (!is_world_job || has_world_owner)
        lab_propagate_field_presence_into_scratch(scratch,field);
    scratch.presence_update_tick=run.tick;
}

// FieldGrid is mutable through at()/cells(), so it has no invalidation generation.
// This signature is checked ONCE per public step(field, ticks) batch, not in the
// 20Hz owner loop. Only externally edited terrain forces an exceptional rebuild.
std::uint64_t lab_external_field_signature(const FieldGrid& field) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    const auto feed = [&](std::uint32_t value) {
        hash ^= value;
        hash *= 0x100000001b3ULL;
    };
    feed(field.width()); feed(field.height());
    for (const auto& cell : field.cells()) {
        feed(cell.flags_and_material);
        feed(cell.loaded_terrain_normal_angles);
        feed(cell.loaded_water_normal_angles);
        feed(static_cast<std::uint32_t>(cell.water_delta_q12));
        feed(static_cast<std::uint32_t>(cell.terrain_height_q12));
    }
    return hash;
}

// Normal gameplay world jobs: influence update case 4, occupancy propagation
// case 7. The terrain-class rebuild is *not* a normal per-tick task; startup
// first runs classification/floods then influence (session/startup.cpp).
// Snapshot generations are immutable to GUI readers and other worker runs.
void lab_initialize_persistent_scratch(SimulationLabRun& run,const FieldGrid& field,
    const SimulationLabRuntimeCatalog* catalog, std::span<const SimulationLabStaticPlacementOwner> static_owners) {
    if (run.retail_scratch && run.retail_scratch->width==field.width()*4U &&
        run.retail_scratch->height==field.height()*4U) return;
    auto scratch=retail_build_terrain_scratch(field);
    const auto owners=catalog?simulation_lab_site_owners(run,field,*catalog,static_owners):std::vector<SimulationLabSiteOwner>{};
    lab_apply_retail_unreachable_terrain_classes(scratch,run,owners,catalog!=nullptr&&run.level_runtime_enabled);
    lab_rebuild_grid_influence_snapshot(scratch,run,owners,catalog!=nullptr&&run.level_runtime_enabled);
    for(auto& entity:run.entities)
        entity.navigation_influence_position_q12=MovementPoint{
            float_to_q12(entity.actor.position.x),float_to_q12(entity.actor.position.y)};
    // An editor synthetic run does not load saved FLD occupancy bytes. Seed
    // presence for the first AI turn, then update on each real case-7 phase.
    lab_rebuild_phase7_field_presence(scratch,run,field,catalog,static_owners);
    scratch.influence_update_tick=run.tick;
    run.retail_scratch=std::make_shared<const LabAiTerrainScratch>(std::move(scratch));
}

void lab_advance_persistent_scratch_world_job(SimulationLabRun& run,
    const FieldGrid& field,const SimulationLabRuntimeCatalog* catalog,
    std::span<const SimulationLabStaticPlacementOwner> static_owners) {
    if (!run.retail_scratch ||
        ((run.tick&7U)!=4U && (run.tick&7U)!=7U)) return;
    auto next=std::make_shared<LabAiTerrainScratch>(*run.retail_scratch);
    if ((run.tick&7U)==4U) {
        const auto owners=catalog?simulation_lab_site_owners(run,field,*catalog,static_owners):std::vector<SimulationLabSiteOwner>{};
        lab_rebuild_grid_influence_snapshot(*next,run,owners,catalog!=nullptr&&run.level_runtime_enabled);
        for(auto& entity:run.entities)
            entity.navigation_influence_position_q12=MovementPoint{
                float_to_q12(entity.actor.position.x),float_to_q12(entity.actor.position.y)};
        next->influence_update_tick=run.tick;
    } else {
        lab_rebuild_phase7_field_presence(*next,run,field,catalog,static_owners,true,
            simulation_lab_phase7_maybe_has_world_owner(run,run.static_placement_owner_count));
    }
    run.retail_scratch=std::move(next);
}

LabPlacementWorkspaces lab_rebuild_placement_workspaces(
    const SimulationLabRun& run, std::uint32_t faction, const FieldGrid& field,
    bool general_only = false) {
    if(const auto* snapshot=lab_retained_planning_snapshot(run,faction))
        return snapshot->placement;
    LabPlacementWorkspaces ws;
    ws.general05.reserve(32U); ws.flagged06.reserve(64U);
    ws.resource08.reserve(32U); ws.base09.reserve(1024U); ws.large10.reserve(256U);
    if (field.width() < 3U || field.height() < 3U) return ws;
    // Planning reads the last completed world-job snapshot, never re-stamps
    // moving entities or clears the phase-persistent faction occupancy bits.
    LabAiTerrainScratch uncached;
    const LabAiTerrainScratch* retained=run.retail_scratch.get();
    if (!retained || retained->width!=field.width()*4U ||
        retained->height!=field.height()*4U) {
        // Legacy API clients may call the planner before step_run supplies the
        // field for startup initialization. Preserve a bounded read-only fallback.
        uncached=retail_build_terrain_scratch(field);
        lab_apply_retail_unreachable_terrain_classes(uncached,run,{},false);
        lab_rebuild_grid_influence_snapshot(uncached,run,{},false);
        retained=&uncached;
    }
    const auto& terrain_scratch=*retained;
    const auto active_masks=simulation_lab_ai_active_grid_classes(run,faction);
    const auto owner_workspaces=lab_ai_owner_workspaces_v718(run,field,faction);
    const auto cell_count=static_cast<std::size_t>(field.width())*field.height();
    std::vector<std::uint8_t> presence(cell_count,0U);
    const bool has_raw_occupancy=retained->field_occupancy_masks.size()==cell_count;
    if (has_raw_occupancy) {
        for (std::size_t i=0;i<cell_count;++i)
            presence[i]=((retained->field_occupancy_masks[i]>>(faction*8U))&0xF9U)!=0U;
    } else if (retained->field_presence_by_faction.size()==cell_count) {
        for (std::size_t i=0;i<cell_count;++i)
            presence[i]=(retained->field_presence_by_faction[i] & (1U<<faction))!=0U;
    } else {
        // Direct callers that never advanced a tick still use the older
        // construction-reach projection, not an invented saved occupancy mask.
        for (std::uint32_t py=0;py<field.height();++py) {
            for (std::uint32_t px=0;px<field.width();++px) {
                const auto& target=field.at(px,py);
                for (const auto& own:run.entities) {
                    if (own.faction_index!=faction || own.root_model_terminal ||
                        !own.construction_complete || own.runtime_class_specific_19c_raw<=0) continue;
                    const auto dx=static_cast<std::int64_t>(target.world_x_q12)-
                        float_to_q12(own.actor.position.x);
                    const auto dy=static_cast<std::int64_t>(target.world_y_q12)-
                        float_to_q12(own.actor.position.y);
                    const auto radius=static_cast<std::int64_t>(own.runtime_class_specific_19c_raw);
                    if (dx*dx+dy*dy<=radius*radius) {
                        presence[static_cast<std::size_t>(py)*field.width()+px]=1U;
                        break;
                    }
                }
            }
        }
    }
    const auto present=[&](std::uint32_t cx,std::uint32_t cy) {
        return presence[static_cast<std::size_t>(cy)*field.width()+cx]!=0U;
    };
    const auto occupancy_byte=[&](std::uint32_t cx,std::uint32_t cy) -> std::uint8_t {
        const auto index=static_cast<std::size_t>(cy)*field.width()+cx;
        return has_raw_occupancy ? static_cast<std::uint8_t>(
            retained->field_occupancy_masks[index]>>(faction*8U)) : 0U;
    };
    // current cell plus six hex neighbours, as in
    // AiPlanningRebuild_FootprintHasResourceSupport/HasUnoccupiedNeighbour.
    constexpr std::array<std::pair<int,int>,7> kFootprint7{{
        {0,0},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1}}};
    const std::int64_t general_separation = static_cast<std::int32_t>(run.ai_parameters[0x20U]);
    const std::int64_t flagged_separation = static_cast<std::int32_t>(run.ai_parameters[0x30U]);
    // Rebuild FLD in its original row-major order. Workspace06 is intentionally not scored:
    // retail reads its score coefficients from uninitialized workspace memory, and never
    // consumes that buffer; synthesizing KI parameter values would introduce false behavior.
    for (std::uint32_t y=1U; y+1U<field.height(); ++y) {
        // Native AiPlanningRebuild_ScanFieldGridSites visits all columns in
        // both the first and subsequent rows (normal stock grid widths).
        for (std::uint32_t x=0U; x<field.width(); ++x) {
            const auto& cell=field.at(x,y);
            if (!run.level_runtime_enabled&&(cell.flags_and_material & 0x88006000U) != 0U) continue;
            const bool native_scan=run.level_runtime_enabled&&has_raw_occupancy;
            const auto admission=native_scan?retail_ai_site_admission(field,terrain_scratch,faction,x,y,active_masks):RetailAiSiteAdmission{};
            const Vec3f point{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),
                              q12_to_float(cell.terrain_height_q12)};
            // Workspace06 is independent of general/resource presence;
            // the other site families require bitmask 0xF9 at this cell.
            const bool current_cell_present=present(x,y);
            // Same source scratch addresses as AiPlanningRebuild_ScanFieldGridSites:
            // top = (4*(y-1)+2,4*x+2), center = three scratch rows below.
            constexpr std::uint32_t kObstacle=0xF0000000U;
            constexpr std::uint32_t kLowBands=0x0000FF00U;
            constexpr std::uint32_t kSiteBands0To5=0x00003F00U;
            const auto sx=x*4U+2U, sy=(y-1U)*4U+2U;
            const auto center=terrain_scratch.read(sx,sy+3U);
            const auto general_neighborhood=center |
                terrain_scratch.six(sx,sy,terrain_scratch.width*3U,3U);
            const bool lacks_active_class=std::any_of(active_masks.begin(),active_masks.end(),
                [&](std::uint32_t mask){return (mask & general_neighborhood)==0U;});
            // Military unit callbacks consume only workspace05. Its admission needs
            // the seven-neighbour occupancy test and general scratch bands, but
            // NOT structure-footprint, resource, large-site or flagged-site work.
            // Keep the exact same row-major order and score expression as full scan.
            const bool scratch_general_open=native_scan?admission.general:((center&0x80000000U)==0U && lacks_active_class);
            bool unoccupied_neighbour=false;
            if (current_cell_present && scratch_general_open && ws.general05.size()<32U) {
                for (const auto [ox,oy]:kFootprint7) {
                    if (ox==0 && oy==0) continue;
                    const auto fx=static_cast<std::uint32_t>(static_cast<int>(x)+ox);
                    const auto fy=static_cast<std::uint32_t>(static_cast<int>(y)+oy);
                    if (fx<field.width()&&fy<field.height()&&!present(fx,fy)) { unoccupied_neighbour=true; break; }
                }
            }
            if ((native_scan?admission.general:(current_cell_present && scratch_general_open && unoccupied_neighbour)) && ws.general05.size()<32U) {
                bool separated=true;
                for (const auto& other:ws.general05) {
                    const auto dx=std::llabs(static_cast<long long>(cell.world_x_q12)-
                                             float_to_q12(other.point.x));
                    const auto dy=std::llabs(static_cast<long long>(cell.world_y_q12)-
                                             float_to_q12(other.point.y));
                    if (dx<general_separation && dy<general_separation) {separated=false;break;}
                }
                if (separated) {
                    const auto primary=lab_nearest_entity_distance_q12(run,faction,field,point,0U,&owner_workspaces);
                    const auto secondary=lab_nearest_entity_distance_q12(run,faction,field,point,1U,&owner_workspaces);
                    const auto hostile=lab_nearest_entity_distance_q12(run,faction,field,point,2U,&owner_workspaces);
                    // Original workspace-05 scoring consists of three 32-bit
                    // products and wrapped additions. A 64-bit accumulated
                    // score changes sign on overflow and can change AI orders.
                    const auto score=simulation_lab_workspace05_native_score(
                        primary,secondary,hostile,run.ai_parameters);
                    ws.general05.push_back({point,score});
                }
            }
            if (general_only) continue;
            const auto small=terrain_scratch.six(sx,sy,terrain_scratch.width*3U,4U);
            const auto wide=terrain_scratch.six(sx,sy-3U,terrain_scratch.width*6U,8U);
            const bool scratch_resource_open=((center&(kObstacle|kSiteBands0To5))==0U) &&
                ((center&(kObstacle|kLowBands))==0U) &&
                ((small&kObstacle)==0U) && ((wide&kObstacle)==0U);
            const bool scratch_base_open=(small&kObstacle)==0U;
            const bool scratch_large_open=((center&(kObstacle|kLowBands))==0U) &&
                scratch_base_open && ((wide&0x70000000U)==0U);
            // AiPrimaryWorkspace_IsPointOutsideAllEntryExtents reads assigned
            // Workspace 00 model slots only (128 entries, world-owner order).
            // Construction completion and terminal-effect start do not unlink a
            // model owner. Ignore neither until runtime_removed becomes true.
            // Recovered source machine, preserved here verbatim for audit:
            //   bool inside_structure_extent=false;
            //   if (own.army_asset_id<300U) continue;
            //   if (dx<reach && dy<reach) { inside_structure_extent=true; break; }
            // Workspaces 09/10 then reject !inside_structure_extent before their
            // separate resource-footprint and large-site scratch gates.
            const bool inside_structure_extent = lab_ai_inside_primary_extents_v724(
                {cell.world_x_q12,cell.world_y_q12}, owner_workspaces.structures00,
                [&](std::size_t index) -> std::optional<LabAiPrimaryExtentV724> {
                    const auto& own=run.entities[index];
                    if (own.runtime_class_specific_19c_raw<=0) return std::nullopt;
                    return LabAiPrimaryExtentV724{
                        lab_ai_model_point_v719(own.actor.position),
                        own.runtime_class_specific_19c_raw};
                });
            // Resource exclusion for workspaces 09 and 10 uses all seven
            // footprint cells, not just the current cell.
            bool footprint_has_support=false;
            for (const auto [ox,oy]:kFootprint7) {
                const auto fx=static_cast<std::uint32_t>(static_cast<int>(x)+ox);
                const auto fy=static_cast<std::uint32_t>(static_cast<int>(y)+oy);
                if(fx>=field.width()||fy>=field.height()){footprint_has_support=true;continue;}
                const auto& nearby=field.at(fx,fy);
                footprint_has_support|=(nearby.flags_and_material&0x1800U)!=0U;
            }
            const bool resource=(cell.flags_and_material & 0x1800U) != 0U;
            // Workspace05 requires the literal six-sample/active-class test.
            // Recovered dynamic band and flood rules are present, but the
            // phase-persistent owner-list scratch image is not yet complete.
            if (native_scan?admission.resource:(current_cell_present && resource && scratch_resource_open)) {
                lab_append_resource08(ws,run,owner_workspaces,cell);
            }
            // Workspace 06 is filled but never consumed by retail. Reproduce
            // AiPlanningRebuild_ScanSiteCell's occupancy bit-4, clearance and
            // six-sample class tests plus the separated 64-entry admission
            // order. Do not invent the original invalid-pointer score reads:
            // they have no consumer and no deterministic memory value.
            if (has_raw_occupancy && (occupancy_byte(x,y)&0x10U)!=0U &&
                (center&(0x80000000U|0x00000100U))==0U && !resource) {
                const auto flagged_neighborhood=center |
                    terrain_scratch.six(sx,sy,terrain_scratch.width*3U,4U);
                const bool class_clear=
                    (flagged_neighborhood&(0x80000000U|0x01000000U))==0U ||
                    (flagged_neighborhood&(0x80000000U|0x02000000U))==0U ||
                    (flagged_neighborhood&(0x80000000U|0x10000000U))==0U;
                if ((native_scan?admission.flagged:class_clear) && ws.flagged06.size()<64U) {
                    bool separated=true;
                    for (const auto& other:ws.flagged06) {
                        const auto dx=std::llabs(static_cast<long long>(cell.world_x_q12)-
                            float_to_q12(other.x));
                        const auto dy=std::llabs(static_cast<long long>(cell.world_y_q12)-
                            float_to_q12(other.y));
                        if (dx<flagged_separation && dy<flagged_separation) {
                            separated=false; break;
                        }
                    }
                    if (separated) ws.flagged06.push_back(point);
                }
            }
            if (!inside_structure_extent || (native_scan?!admission.base:(!current_cell_present || footprint_has_support || !scratch_base_open ||
                (center&(kObstacle|kSiteBands0To5))!=0U))) continue;
            // Workspace09's exact seven-FLD-cell resource-footprint exclusion is
            // independent of workspace08's resource-site scoring.
            if (ws.base09.size()<1024U) ws.base09.push_back(point);
            // Large sites additionally require retail wide six-sample
            // terrain class 28..30 clearance (dynamic low bands still pending).
            if ((native_scan?admission.large:scratch_large_open) && ws.large10.size()<256U) ws.large10.push_back(point);
        }
    }
    return ws;
}

bool field_has_unclaimed_resource_site(const FieldGrid& field, const SimulationLabRun& run,
                                       std::uint32_t occupancy_class,
                                       std::uint32_t extractor_asset_id,
                                       std::uint32_t faction) {
    const auto ws=lab_rebuild_placement_workspaces(run,faction,field);
    for (const auto& site:ws.resource08) {
        if (site.army_asset_id != extractor_asset_id) continue;
        // Class-14 extractor resource dispatch uses rounded nearest FLD
        // coordinates, not the triangular field lookup used for fog/terrain.
        if (!pioneer_class14_resource_cell_eligible(field,float_to_q12(site.point.x),
                                                     float_to_q12(site.point.y),
                                                     occupancy_class)) continue;
        return true;
    }
    // A direct-API fixture may have no MDL visibility metadata AND no
    // footprint/build-range seed: this cannot populate a retail workspace08.
    // Keep the original Stage 7 candidate-score eligibility in that strictly
    // unmodeled case, without granting a placement site or changing fog state.
    // The retail planner always uses real workspace08 when model data exists.
    const bool missing_faction_model = !run.level_runtime_enabled &&
        std::none_of(run.entities.begin(), run.entities.end(),
            [&](const SimulationLabEntity& entity) {
                return entity.faction_index == faction && !entity.root_model_terminal &&
                    (entity.fog_uncover_radius_q12 > 0 ||
                     entity.runtime_class_specific_19c_raw > 0);
            });
    if (!missing_faction_model) return false;
    return field_has_resource_support(field, occupancy_class);
}

struct Class18SpecialSiteCandidate {
    Vec3f world{};
    std::uint32_t special_asset_id{};
    std::int32_t priority{};
};

std::optional<Vec3f> class18_special_site_target(const SimulationLabRun& run,
                                                  const SimulationLabEntity& entity,
                                                  const FieldGrid& field,
                                                  const SimulationLabRuntimeCatalog* runtime_catalog,
                                                  std::span<const SimulationLabStaticPlacementOwner> static_owners,
                                                  PioneerSiteSelectorTrace* trace,SimulationArmyPool* probe_pool=nullptr,
                                                  SimulationLabWorldObjectPool* world_pool=nullptr) {
    // V533 AiPlanning_RebuildFactionWorkspaces feeds resource-support FLD cells to workspace08,
    // capped at 0x20 records. AiSiteCandidate_AddTerrainFeatureCellIfSeparated assigns ARM ids 330 to
    // Xenite support and 332 to Tritium support, merges nearby same-type records using
    // KI[0x50] * 0x55 >> 8, and stores max(KI[0x51] - nearest-owned-runtime Manhattan distance,0)
    // as the record priority. Authored runs now consume the retained scratch-grid workspace08;
    // only metadata-free direct API runs retain the older bounded raw-field scan.
    constexpr std::size_t kWorkspace08Capacity = 0x20U;
    constexpr std::uint32_t kXeniteSpecialAsset = 330U;
    constexpr std::uint32_t kTritiumSpecialAsset = 332U;
    const std::int32_t minimum_axis_separation_q12 =
        static_cast<std::int32_t>(run.ai_parameters[80U]);
    const std::int32_t merge_axis_q12 = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(run.ai_parameters[80U]) * 0x55U >> 8U);
    const std::int32_t clearance_padding_q12 =
        static_cast<std::int32_t>(run.ai_parameters[81U]);

    std::vector<Class18SpecialSiteCandidate> workspace08;
    LabPlacementWorkspaces placement_workspaces;
    workspace08.reserve(kWorkspace08Capacity);
    if (run.level_runtime_enabled) {
        // The same retained scratch/FLD workspace08 that controls construction must also
        // control authored Pioneer navigation. The old raw-field scan could target cells
        // rejected by occupancy, terrain classes, or the 32-entry workspace admission.
        placement_workspaces = lab_rebuild_placement_workspaces(run, entity.faction_index, field);
        for (const auto& site : placement_workspaces.resource08)
            workspace08.push_back({site.point, site.army_asset_id, site.priority});
    } else {
    // Direct Lab API fixtures may omit the entire MDL/scratch input image; keep their
    // historical resource-site path distinct from the authored retail-workspace path.
    for (std::uint32_t y = 0; y < field.height(); ++y) {
        for (std::uint32_t x = 0; x < field.width(); ++x) {
            const auto xenite = field.audit_resource_support_cell(x, y, 0U);
            const auto tritium = field.audit_resource_support_cell(x, y, 1U);
            if (!xenite.valid() && !tritium.valid()) continue;
            const auto& cell = field.at(x, y);
            const std::uint32_t special_asset_id = xenite.valid()
                ? kXeniteSpecialAsset : kTritiumSpecialAsset;

            bool occupied_by_same_type = false;
            std::int32_t nearest_owned_manhattan_q12 = std::numeric_limits<std::int32_t>::max();
            // 0x00537CF0 measures from assigned workspace00 runtimes; 0x00538230
            // sends ARM<300 (including the Pioneer) to workspace01 instead.
            // Match the live prefix's recovered capacities before considering
            // null queue records. The pointer-form retail bootstrap predicate was:
            //   producer->production_building &&
            //   producer->production_asset_id >= 300U
            constexpr std::size_t kWorkspace00Capacity = 0x80U;
            std::size_t assigned_workspace00_count = 0U;
            for (const auto& owned : run.entities) {
                if (!owned.construction_complete ||
                    owned.faction_index != entity.faction_index || owned.army_asset_id < 300U)
                    continue;
                if (assigned_workspace00_count >= kWorkspace00Capacity) break;
                ++assigned_workspace00_count;
                const std::int32_t owned_x_q12 = float_to_q12(owned.actor.position.x);
                const std::int32_t owned_y_q12 = float_to_q12(owned.actor.position.y);
                const std::int64_t dx = std::llabs(static_cast<long long>(owned_x_q12) - cell.world_x_q12);
                const std::int64_t dy = std::llabs(static_cast<long long>(owned_y_q12) - cell.world_y_q12);
                const auto manhattan = static_cast<std::int32_t>(std::min<std::int64_t>(
                    std::numeric_limits<std::int32_t>::max(), dx + dy));
                nearest_owned_manhattan_q12 = std::min(nearest_owned_manhattan_q12, manhattan);
                if (owned.army_asset_id == special_asset_id &&
                    dx < minimum_axis_separation_q12 && dy < minimum_axis_separation_q12) {
                    occupied_by_same_type = true;
                    break;
                }
            }
            if (occupied_by_same_type) continue;
            const std::int32_t priority = std::max(0, clearance_padding_q12 - nearest_owned_manhattan_q12);
            Class18SpecialSiteCandidate candidate{
                {q12_to_float(cell.world_x_q12), q12_to_float(cell.world_y_q12),
                 q12_to_float(cell.terrain_height_q12)}, special_asset_id, priority};

            auto merged = std::find_if(workspace08.begin(), workspace08.end(),
                [&](const Class18SpecialSiteCandidate& existing) {
                    return existing.special_asset_id == special_asset_id &&
                        std::abs(float_to_q12(existing.world.x) - cell.world_x_q12) < merge_axis_q12 &&
                        std::abs(float_to_q12(existing.world.y) - cell.world_y_q12) < merge_axis_q12;
                });
            if (merged != workspace08.end()) {
                if (merged->priority < priority) *merged = candidate;
            } else if (workspace08.size() < kWorkspace08Capacity) {
                workspace08.push_back(candidate);
            }
        }
    }

    } // end synthetic/direct-API compatibility scan

    // AiUnitBehavior_UpdatePioneerVehicle dispatches this class-18 selector.
    // Catalog-backed authored runs now execute the recovered pre-score order:
    // workspace03 minimum distance, workspace02 minimum distance, mode-3/mode-0
    // bucket admission and the temporary ARM333 chain. Metadata-free direct API
    // fixtures retain the historical scoring-only compatibility path.
    // Retail AiUnitBehavior_ScorePioneerSite (units.cpp:375-429) applies three
    // additive terms in addition to workspace08 priority. Stage 21 omitted the
    // workspace01 shortfall entirely: at startup every workspace08 priority can
    // be zero because there are no assigned ARM>=300 structures. This made the
    // Pioneer scorer return no target even with an admitted deposit.
    // The Lab models workspace01 using the live owned ARM<300 entries; pending
    // null-model entries do not contribute to the retail distance query.
    if (trace) trace->begin(run.tick, static_cast<std::uint32_t>(workspace08.size()));
    const auto pioneer_owner_ws=lab_ai_owner_workspaces_v718(run,field,entity.faction_index);
    const auto own_workspaces=simulation_lab_ai_own_workspaces(run,entity.faction_index);
    const bool retail_admission=run.level_runtime_enabled&&runtime_catalog!=nullptr;
    std::vector<SimulationLabSiteOwner> placement_owners;
    std::optional<SimulationLabSiteModel> xenite_model,tritium_model,storage_model;
    if(retail_admission) {
        placement_owners=simulation_lab_site_owners(run,field,*runtime_catalog,static_owners);
        xenite_model=simulation_lab_first_site_model(*runtime_catalog,kXeniteSpecialAsset);
        tritium_model=simulation_lab_first_site_model(*runtime_catalog,kTritiumSpecialAsset);
        storage_model=simulation_lab_first_site_model(*runtime_catalog,333U);
        if(trace)trace->retail_admission_evaluated=true;
    }
    const std::int32_t minimum_hostile_distance_q12=
        std::bit_cast<std::int32_t>(run.ai_parameters[89U]);
    std::int32_t best_score = 0;
    std::optional<Vec3f> best;
    for (const auto& candidate : workspace08) {
        std::int64_t visible_hostile=std::numeric_limits<std::int32_t>::max();
        if(retail_admission) {
            const auto unseen_hostile=lab_nearest_entity_distance_q12(
                run,entity.faction_index,field,candidate.world,3U,&pioneer_owner_ws);
            if(static_cast<std::int64_t>(minimum_hostile_distance_q12)>unseen_hostile) {
                if(trace)++trace->last_unseen_distance_rejections;
                continue;
            }
            visible_hostile=lab_nearest_entity_distance_q12(
                run,entity.faction_index,field,candidate.world,2U,&pioneer_owner_ws);
            if(static_cast<std::int64_t>(minimum_hostile_distance_q12)>visible_hostile) {
                if(trace)++trace->last_visible_distance_rejections;
                continue;
            }
            const auto& resource_model=candidate.special_asset_id==kXeniteSpecialAsset
                ?xenite_model:tritium_model;
            if(!resource_model) {
                if(trace)++trace->last_placement_skips;
                continue;
            }
            const auto admission=simulation_lab_pioneer_site_admission(
                run,field,placement_owners,*resource_model,storage_model,
                {float_to_q12(candidate.world.x),float_to_q12(candidate.world.y)},
                placement_workspaces.base09,entity.faction_index,probe_pool,world_pool,runtime_catalog);
            if(trace)trace->last_temporary_arm333_probes+=admission.probes_created;
            if(admission.disposition==PioneerSiteAdmission::SkipSite) {
                if(trace)++trace->last_placement_skips;
                continue;
            }
            if(admission.disposition==PioneerSiteAdmission::StopSelector) {
                if(trace) {
                    ++trace->last_selector_stops;
                    trace->last_best_exists=false;
                    trace->last_best_score=0;
                    trace->last_best_point={};
                    trace->finish();
                }
                return std::nullopt;
            }
            if(trace)++trace->last_admitted_for_scoring;
        }
        const std::int64_t secondary = lab_nearest_entity_distance_q12(
            run, entity.faction_index, field, candidate.world, 1U,&pioneer_owner_ws);
        if(!retail_admission) visible_hostile = lab_nearest_entity_distance_q12(
            run, entity.faction_index, field, candidate.world, 2U,&pioneer_owner_ws);
        const std::int64_t unit_distance = lab_axis_manhattan_q12(
            entity.actor.position, candidate.world);
        const std::uint32_t assigned_count=simulation_lab_ai_assigned_structure_count(
            own_workspaces,candidate.special_asset_id);
        const std::int32_t score = simulation_lab_pioneer_site_weighted_score(
            candidate.priority, candidate.special_asset_id,
            secondary, visible_hostile, unit_distance, assigned_count,
            run.ai_parameters);
        if (trace) trace->observe(score, {float_to_q12(candidate.world.x),
                                          float_to_q12(candidate.world.y)});
        if (score > best_score) {
            best_score = score;
            best = candidate.world;
        }
    }

    if (trace) trace->finish();
    return best;
}

bool run_has_asset_presence(const SimulationLabRun& run, std::uint32_t faction,
                            std::uint32_t army_asset_id) {
    if(run.level_runtime_enabled) {
        const auto workspace=simulation_lab_ai_own_workspaces(run,faction);
        const auto& entries=army_asset_id>=300?workspace.structures:workspace.units;
        return std::any_of(entries.begin(),entries.end(),[&](const auto& e){return e.army_asset_id==army_asset_id;});
    }
    if (std::any_of(run.entities.begin(), run.entities.end(), [&](const SimulationLabEntity& entity) {
            return !entity.runtime_removed && entity.faction_index == faction &&
                   entity.army_asset_id == army_asset_id;
        })) return true;
    // Rebuild_AddAssetsInProduction contributes an unassigned workspace00/01
    // entry while a class11/13/22 producer is building. Do not buy the same
    // factory again merely because its secondary request was already dequeued.
    if(std::any_of(run.entities.begin(),run.entities.end(),[&](const auto& producer) {
        return producer.faction_index==faction&&!producer.root_model_terminal&&
            producer.production_building&&producer.production_asset_id==army_asset_id;
    })) return true;
    if (std::any_of(run.pending_asset_requests.begin(), run.pending_asset_requests.end(),
        [&](const SimulationLabPendingAssetRequest& request) {
            return request.faction_index == faction && request.army_asset_id == army_asset_id;
        })) return true;
    return std::any_of(run.secondary_purchase_requests.begin(), run.secondary_purchase_requests.end(),
        [&](const SimulationLabPendingAssetRequest& request) {
            return request.faction_index == faction && request.army_asset_id == army_asset_id;
        });
}

std::uint32_t run_asset_count(const SimulationLabRun& run, std::uint32_t faction,
                              std::uint32_t army_asset_id) {
    if(run.level_runtime_enabled) {
        const auto workspace=simulation_lab_ai_own_workspaces(run,faction);
        const auto& entries=army_asset_id>=300?workspace.structures:workspace.units;
        return static_cast<uint32_t>(std::count_if(entries.begin(),entries.end(),[&](const auto& e){return e.entity_index && e.army_asset_id==army_asset_id;}));
    }
    return static_cast<std::uint32_t>(std::count_if(run.entities.begin(), run.entities.end(),
        [&](const SimulationLabEntity& entity) {
            return entity.faction_index == faction && entity.army_asset_id == army_asset_id &&
                   entity.construction_complete && !entity.runtime_removed;
        }));
}


std::int64_t signed_parameter(std::uint32_t raw) noexcept {
    return static_cast<std::int64_t>(static_cast<std::int32_t>(raw));
}

void append_one_shot_event(SimulationLabRun& run, SimulationLabOneShotEvent event) {
    event.sequence = run.next_one_shot_event_sequence++;
    run.one_shot_event_tail = std::make_shared<const SimulationLabOneShotEventNode>(
        std::move(event), run.one_shot_event_tail);
}
}

std::optional<std::uint32_t> simulation_lab_ai_candidate_weighted_delta(
    std::uint32_t weight_range, std::uint32_t random_value) noexcept {
    if (weight_range <= 1U) return std::nullopt;
    // V533 machine behavior is dword arithmetic: (weight * 5 + random % weight) * 0x10.
    // Unsigned arithmetic intentionally preserves the retail 32-bit wrap semantics.
    const std::uint32_t weighted = weight_range * 5U + random_value % weight_range;
    return weighted * 0x10U;
}

std::vector<std::int32_t> simulation_lab_sweep_values(const SimulationLabConfig& input) {
    SimulationLabConfig config = input;
    config.run_count = std::clamp(config.run_count, 1U, 32U);
    std::vector<std::int32_t> values(config.run_count, config.fixed_value);
    if (config.sweep_mode == SimulationLabSweepMode::Fixed || config.run_count == 1U) return values;
    const std::int64_t lo = config.minimum_value;
    const std::int64_t hi = config.maximum_value;
    if (config.sweep_mode == SimulationLabSweepMode::Linear) {
        const std::int64_t denom = static_cast<std::int64_t>(config.run_count - 1U);
        for (std::uint32_t i = 0; i < config.run_count; ++i) {
            const std::int64_t numerator = (hi - lo) * static_cast<std::int64_t>(i);
            values[i] = static_cast<std::int32_t>(lo + numerator / denom);
        }
        values.front() = config.minimum_value;
        values.back() = config.maximum_value;
        return values;
    }
    const std::int32_t middle = static_cast<std::int32_t>(lo + (hi - lo) / 2);
    for (std::uint32_t i = 0; i < config.run_count; ++i) {
        const std::uint64_t bucket = static_cast<std::uint64_t>(i) * 3ULL / config.run_count;
        values[i] = bucket == 0U ? config.minimum_value : (bucket == 1U ? middle : config.maximum_value);
    }
    return values;
}


void simulation_lab_advance_component_health(SimulationLabModelComponentState& component,
                                              std::uint32_t step_ticks) noexcept {
    if ((component.runtime_flags_ec & 0x10U) != 0U) return;
    auto& delay = component.health_regeneration_delay_ticks;
    delay = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(delay) - step_ticks);
    if (delay >= 0) return;
    delay = 4;
    const auto previous = component.integrity;
    if (previous == 0) return;
    // x86 LEA then SHR: multiplication wraps as a dword, followed by a logical shift.
    const auto limit = std::bit_cast<std::int32_t>(
        (static_cast<std::uint32_t>(component.maximum_integrity) * 3U) >> 2U);
    const auto delta = static_cast<std::uint32_t>(component.health_regeneration_per_step);
    if ((component.runtime_flags_ec & 1U) != 0U) {
        auto candidate = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(previous) - delta);
        if (candidate < limit) candidate = limit;
        if (candidate < previous) component.integrity = candidate;
    } else if ((component.runtime_flags_ec & 0x400U) == 0U) {
        auto candidate = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(previous) + delta);
        if (candidate > limit) candidate = limit;
        if (candidate > previous) component.integrity = candidate;
    }
}

std::vector<SimulationLabModelComponentSeed> simulation_lab_model_components_from_hierarchy(
    std::span<const ModelVisualNode> hierarchy) {
    std::vector<SimulationLabModelComponentSeed> result;
    if (hierarchy.empty()) return result;
    std::vector<std::size_t> node_to_component(hierarchy.size(), kSimulationLabNoComponent);
    result.reserve(hierarchy.size());
    for (std::size_t node_index = 0; node_index < hierarchy.size(); ++node_index) {
        const auto& node = hierarchy[node_index];
        if (!node.creates_runtime_node()) continue;
        const bool runtime_root = node.parent_index == std::numeric_limits<std::uint32_t>::max() ||
            node.parent_index >= hierarchy.size() ||
            hierarchy[node.parent_index].model_definition_id != node.model_definition_id;
        if (!runtime_root) continue;
        SimulationLabModelComponentSeed component;
        component.source_model_node_index = node_index;
        component.model_definition_id = node.model_definition_id;
        component.definition_flags68 = node.simulation.runtime_render_flags;
        component.runtime_class_id = node.runtime_class_id;
        component.placement_contact_kind = node.simulation.placement_contact_kind;
        component.maximum_integrity = std::max<std::int32_t>(0, node.simulation.maximum_integrity);
        component.health_regeneration_per_step = node.simulation.health_regeneration_per_step;
        component.model_rotation_angle0 = node.local_rotation_angle0;
        if (node.runtime_class_id == 14U) {
            const auto selector = node.simulation.class_specific_c0_raw;
            if (selector >= 0 && selector <= 1) component.resource_support_class =
                static_cast<std::uint32_t>(selector);
            component.resource_extraction_weight_u8 = node.simulation.resource_extraction_weight_u8 & 0xFFU;
        }
        component.resource_storage_selector = node.simulation.resource_storage_selector;
        component.resource_storage_capacity_contribution_q4 =
            node.simulation.resource_storage_capacity_contribution_q4;
        component.has_resource_storage_capacity_contribution =
            node.simulation.has_resource_storage_capacity_contribution;
        component.retail_energy_demand_q4 = node.simulation.retail_energy_demand_q4;
        component.energy_generation_capacity_q4 = node.simulation.energy_generation_capacity_q4;
        component.terminal_attachment_effect_ids = node.simulation.terminal_attachment_effect_ids;
        component.terminal_attachment_delay_ticks = node.simulation.terminal_attachment_delay_ticks;
        node_to_component[node_index] = result.size();
        result.push_back(component);
    }
    for (std::size_t component_index = 0; component_index < result.size(); ++component_index) {
        auto& component = result[component_index];
        const std::size_t node_index = component.source_model_node_index;
        if (node_index >= hierarchy.size()) continue;
        std::uint32_t parent = hierarchy[node_index].parent_index;
        std::size_t hops = 0U;
        while (parent < hierarchy.size() && hops++ < hierarchy.size()) {
            const std::size_t parent_component = node_to_component[parent];
            if (parent_component != kSimulationLabNoComponent) {
                if (parent_component != component_index)
                    component.parent_component_index = parent_component;
                break;
            }
            const std::uint32_t next = hierarchy[parent].parent_index;
            if (next == parent) break;
            parent = next;
        }
    }
    // Editor sanitation only: valid retail hierarchies are acyclic, but a corrupt imported tree
    // must not turn later damage propagation into unbounded recursion.
    for (std::size_t component_index = 0; component_index < result.size(); ++component_index) {
        std::optional<std::size_t> cursor = result[component_index].parent_component_index;
        std::size_t hops = 0U;
        while (cursor && *cursor < result.size() && hops++ < result.size()) {
            if (*cursor == component_index) {
                result[component_index].parent_component_index.reset();
                break;
            }
            cursor = result[*cursor].parent_component_index;
        }
        if (cursor && hops >= result.size())
            result[component_index].parent_component_index.reset();
    }
    return result;
}

namespace {
bool runtime_class_uses_group_a_command_handler(std::uint32_t runtime_class_id) noexcept {
    return std::find(kGroupAClassCommandClasses.begin(), kGroupAClassCommandClasses.end(),
                     runtime_class_id) != kGroupAClassCommandClasses.end();
}
}

bool simulation_lab_runtime_class_routes_terminal_to_relation_d(
    std::uint32_t runtime_class_id) noexcept {
    return runtime_class_uses_group_a_command_handler(runtime_class_id);
}

bool simulation_lab_runtime_class_uses_group_a_command_handler(
    std::uint32_t runtime_class_id) noexcept {
    return runtime_class_uses_group_a_command_handler(runtime_class_id);
}

// Workspace02/03 classification shared by distances, purchase profiles and
// military workspace07. Native admission reads the source faction's capability
// bit, not its packed relation nibble; the Lab maintains that bit image through
// initialization and pairwise relation transitions.
SimulationLabHostilePerception simulation_lab_ai_hostile_perception(
    const SimulationLabRun& run,const FieldGrid& field,std::uint32_t faction,
    const SimulationLabEntity& entity) noexcept {
    if (!simulation_lab_command_target_owner_available(entity) ||
        faction==0U || faction>=8U || entity.faction_index==0U ||
        entity.faction_index>=8U || entity.faction_index==faction ||
        (run.faction_capability_flags[faction]&
         (1U<<(entity.faction_index&7U)))!=0U)
        return SimulationLabHostilePerception::Excluded;
    const auto bits=simulation_lab_model_visibility_bits(run,field,faction,entity);
    if((bits&2U)!=0U || ((bits&1U)!=0U &&
        runtime_class_uses_group_a_command_handler(entity.runtime_model_class_id)))
        return SimulationLabHostilePerception::Visible;
    return SimulationLabHostilePerception::Unseen;
}

float simulation_lab_construction_progress(const SimulationLabRun& run,
                                           const SimulationLabEntity& entity) noexcept {
    if (!entity.synthetic || entity.construction_complete) return 1.0F;
    if (entity.construction_complete_tick <= entity.construction_start_tick) return 0.0F;
    const std::uint32_t elapsed = run.tick > entity.construction_start_tick
        ? run.tick - entity.construction_start_tick : 0U;
    const std::uint32_t total = entity.construction_complete_tick - entity.construction_start_tick;
    return std::clamp(static_cast<float>(elapsed) / static_cast<float>(total), 0.0F, 1.0F);
}

bool simulation_lab_parameter_effect_modeled(std::uint32_t parameter_index) noexcept {
    // A modeled parameter may still rely on a proxy for an unmaterialized retail workspace.
    return (parameter_index >= 1U && parameter_index <= 7U) ||
           (parameter_index >= 9U && parameter_index <= 11U) ||
           (parameter_index >= 20U && parameter_index <= 23U) ||
           parameter_index == 80U || parameter_index == 81U ||
           parameter_index == 83U || parameter_index == 85U || parameter_index == 87U;
}

const char* simulation_lab_parameter_fidelity_label(std::uint32_t parameter_index) noexcept {
    if (ai_knowledge_consumers(parameter_index).empty())
        return "Game-source named; GZF address not yet independently audited; not modeled in Lab";
    return simulation_lab_parameter_effect_modeled(parameter_index)
        ? "GZF consumer verified; Lab models a partial/equivalent input with documented proxies"
        : "GZF consumer verified; downstream retail effect not modeled in this lab";
}

std::uint64_t simulation_lab_latest_one_shot_event_sequence(const SimulationLabRun& run) noexcept {
    return run.next_one_shot_event_sequence == 0U ? 0U : run.next_one_shot_event_sequence - 1U;
}

std::vector<SimulationLabOneShotEvent> simulation_lab_one_shot_events_after(
    const SimulationLabRun& run, std::uint64_t sequence) {
    std::vector<SimulationLabOneShotEvent> result;
    for (auto node = run.one_shot_event_tail; node && node->event.sequence > sequence;
         node = node->previous)
        result.push_back(node->event);
    std::reverse(result.begin(), result.end());
    return result;
}

void simulation_lab_recompute_points(SimulationLabFactionScoreState& score,
                                     const FactionTechnologyMask256& technology_mask) noexcept {
    const std::uint32_t unlocked = faction_technology_unlocked_count(technology_mask);
    score.unlocked_technology_count_beyond_baseline = unlocked > 5U ? unlocked - 5U : 0U;
    score.primary_resource_component = static_cast<std::int32_t>(score.xenite_extracted_total_q4) >> 4;
    score.secondary_resource_component = static_cast<std::int32_t>(score.tritium_extracted_total_q4) >> 4;
    const std::int64_t economy =
        static_cast<std::int64_t>(score.primary_resource_component) * 0x10 +
        static_cast<std::int64_t>(score.secondary_resource_component) * 8 +
        static_cast<std::int64_t>(score.unlocked_technology_count_beyond_baseline) * 0xA000 +
        static_cast<std::int64_t>(score.explored_terrain_percent) * 0x1000;
    const std::int64_t relation =
        static_cast<std::int64_t>(score.relation_counter_b) * 0x28000 +
        static_cast<std::int64_t>(score.relation_counter_d) * -0x14000 +
        static_cast<std::int64_t>(score.relation_counter_a) * 0x14000 +
        static_cast<std::int64_t>(score.relation_counter_c) * -0xA000 +
        static_cast<std::int64_t>(score.relation_counter_e) * 0x14000 +
        static_cast<std::int64_t>(score.relation_counter_f) * 0x28000;
    score.economy_progress_score = static_cast<std::int32_t>(economy >> 12);
    score.relation_score = static_cast<std::int32_t>(relation >> 12);
    score.points = score.economy_progress_score + score.relation_score;
}

std::uint32_t SimulationLabExperiment::random_next(std::uint32_t& state) noexcept {
    return retail_random_next(state);
}

void SimulationLabExperiment::prepare_ai_scoring_catalog() {
    // Like the native derived-limit rebuild, these values depend on immutable
    // baseline ARM definitions, not the faction's changing selected variants.
    production_asset_indices_.clear();
    technology_asset_indices_.clear();
    research_arm_index_.clear();
    ai_flagged_definition_value0c_maximum_=1;
    ai_category_value60_maxima_.fill(1);
    for (std::size_t index=0;index<production_assets_.size();++index) {
        const auto& asset=production_assets_[index];
        production_asset_indices_.emplace(asset.army_asset_id,index); // first duplicate wins, like find_if
        if ((asset.army_runtime_flags14_raw&1U)==0U) continue;
        const auto category=asset.ai_baseline_root_category_maximum_index5c;
        if (category<ai_category_value60_maxima_.size())
            ai_category_value60_maxima_[category]=std::max(ai_category_value60_maxima_[category],
                std::max<std::int32_t>(1,asset.ai_baseline_root_definition_value60));
        if (asset.ai_baseline_root_definition_value18!=0)
            ai_flagged_definition_value0c_maximum_=std::max(ai_flagged_definition_value0c_maximum_,
                std::max<std::int32_t>(1,asset.ai_baseline_root_definition_value0c));
        for (std::size_t list=0;list<asset.ai_linked_model_definition_ids.size();++list)
            for (const auto definition:asset.ai_linked_model_definition_ids[list]) {
                auto& links=research_arm_index_[definition];
                if (links.empty()||links.back().asset_index!=index) links.push_back({index,false,false});
                if (list==0U) links.back().root=true;
                else links.back().child=true;
            }
    }
    for (std::size_t index=0;index<technology_assets_.size();++index)
        technology_asset_indices_.emplace(technology_assets_[index].technology_id,index);

}

void SimulationLabExperiment::clear() noexcept {
    seed_entities_.clear();
    production_assets_.clear();
    production_asset_indices_.clear();
    technology_asset_indices_.clear();
    research_arm_index_.clear();
    ai_flagged_definition_value0c_maximum_=1;
    ai_category_value60_maxima_.fill(1);
    technology_assets_.clear();
    runs_.clear();
    navigation_terrain_cache_ = {};
    static_navigation_blockers_.clear();
    runtime_catalog_.reset();
    environmental_emitters_.clear();static_shot_hulls_.clear();static_placement_owners_.clear();
}

SimulationLabExperiment SimulationLabExperiment::snapshot_copy() const {
    SimulationLabExperiment snapshot;
    snapshot.config_ = config_;
    snapshot.runs_ = runs_;
    return snapshot;
}

void SimulationLabExperiment::prepare_navigation_terrain(const FieldGrid& field) {
    navigation_terrain_cache_.prepare(field);
    for(auto& run:runs_){
        lab_prepare_missing_model_geometry(run,field,runtime_catalog_.get());
        if(run.tick==0U&&run.level_runtime_enabled&&runtime_catalog_)for(auto& e:run.entities){
            if(e.runtime_removed||e.runtime_model_class_id!=3U||e.actor.articulated_native.initialized)continue;
            if(!simulation_lab_prepare_articulated_contacts(e,field,*runtime_catalog_))continue;
            simulation_initialize_native_walker(e.actor,e.motion,e.articulated_contact_interpolation_rate_q12,e.articulated_contact_lift_q12,
                float_to_q12(e.articulated_contact_local_offsets[0].y),e.articulated_root_height_q12,
                float_to_q12(e.articulated_shared_foot_placement_offset),[&](int32_t x,int32_t y)->std::optional<int32_t>{
                    const auto h=field.sample_retail_surface_heights_q12(x,y);return h.valid?h.terrain:0;
                },true);
            e.articulated_contact_geometry_prepared=true;
        }
    }
}


namespace {
SimulationLabWorldPose lab_world_pose(Vec3f p,uint32_t yaw) noexcept {
    return {{float_to_q12(p.x),float_to_q12(p.y),float_to_q12(p.z)},yaw};
}

SimulationLabEffectPose lab_root_effect_pose(const SimulationLabEntity& entity){
    SimulationLabEffectPose pose;
    pose.position=entity.actor.position;
    pose.native_angles=entity.actor.ground_recoil_angles.value_or(
        std::array<uint32_t,3>{entity.actor.body_yaw_turn16,
            static_cast<uint32_t>(entity.actor.ground_normal_elevation_turn16),
            entity.actor.ground_normal_azimuth_turn16});
    return pose;
}

void lab_release_model_world_node(SimulationLabRun& run,SimulationLabEntity& entity,size_t node,
                                  std::span<const SimulationLabEffectPose> poses);

std::optional<size_t> lab_world_model_component(const SimulationLabEntity& entity,size_t node) {
    if(!entity.runtime_model_hierarchy)return {};
    const auto& nodes=*entity.runtime_model_hierarchy;
    for(size_t hop=0;node<nodes.size()&&hop<nodes.size();++hop){
        for(size_t component=0;component<entity.model_components.size();++component)
            if(entity.model_components[component].source_model_node_index==node)return component;
        node=nodes[node].parent_index;
    }
    return {};
}

void lab_claim_model_world_nodes(SimulationLabRun& run,SimulationLabEntity& entity){
    const auto& nodes=*entity.runtime_model_hierarchy;
    entity.native_world_model_slots.assign(nodes.size(),kSimulationLabNoWorldSlot);
    // The native allocator creates the sprites owned by one MDL runtime
    // before starting the next attached runtime.
    for(size_t component=0;component<entity.model_components.size();++component){
        for(size_t i=0;i<nodes.size();++i)
            if(nodes[i].creates_runtime_node()&&lab_world_model_component(entity,i)==component){
                entity.native_world_model_slots[i]=run.native_world_objects.claim(
                    SimulationLabWorldObjectPool::Kind::Model,
                    entity.model_components[component].model_definition_id);
            }
        // Model class initialization detaches the extractor's preview mesh
        // (child 3) or the storage unit's preview mesh (child 1).
        const auto& c=entity.model_components[component];
        if(c.runtime_class_id==14U||c.runtime_class_id==15U){
            const auto child_slot=c.runtime_class_id==14U?3U:1U;
            for(size_t i=0;i<nodes.size();++i)
                if(nodes[i].parent_index==c.source_model_node_index&&nodes[i].child_slot==child_slot)
                    lab_release_model_world_node(run,entity,i,{});
        }
    }
}
}

void SimulationLabExperiment::claim_model_smoke_slots(SimulationLabRun& run,SimulationLabEntity& entity,size_t entity_index) {
    simulation_lab_claim_army_slot(run,entity,entity_index==SIZE_MAX?run.entities.size():entity_index);
    if(!run.level_runtime_enabled||!entity.runtime_model_hierarchy)return;
    auto& slots=run.vacant_model_smoke_cooldowns;
    for(std::size_t i=0;i<entity.model_components.size();++i){
        auto& component=entity.model_components[i];
        const auto free=std::find_if(slots.begin(),slots.end(),[](const auto& slot){return slot.has_value();});
        component.native_model_pool_slot=static_cast<std::size_t>(free-slots.begin());
        if(std::getenv("THANDOR_SIMLAB_MODEL_POOL_TRACE"))
            std::fprintf(stderr,"MODEL_ALLOC tick=%u slot=%zu def=%u army=%zu arm=%u retained=%d\n",run.tick,
                component.native_model_pool_slot,component.model_definition_id,entity.native_army_pool_slot,
                entity.army_asset_id,free==slots.end()?0:**free);
        if(free==slots.end()){
            slots.emplace_back();
            run.retained_model_effect_points.push_back(0U);
        }
        else {
            component.timed_effect_point_index=run.retained_model_effect_points.at(component.native_model_pool_slot);
            component.retained_damage_effect_cooldown=**free;
            if(i==entity.root_model_component_index)entity.damage_effect_cooldown=**free;
            free->reset();
        }
    }
    lab_claim_model_world_nodes(run,entity);
}

namespace {
void lab_release_model_world_node(SimulationLabRun& run,SimulationLabEntity& entity,size_t node,
                                  std::span<const SimulationLabEffectPose> poses){
    auto& slot=entity.native_world_model_slots[node];
    if(slot==kSimulationLabNoWorldSlot)return;
    auto p=lab_world_pose(entity.actor.position,entity.actor.body_yaw_turn16);
    if(node<poses.size())
        p=lab_world_pose(poses[node].position,poses[node].native_angles.value_or(
            std::array<uint32_t,3>{entity.actor.body_yaw_turn16,0,0})[0]);
    run.native_world_objects.release(slot,p);
    slot=kSimulationLabNoWorldSlot;
}

void lab_unlink_factory_child3(SimulationLabRun& run,SimulationLabEntity& entity,
        const FieldGrid& field,const SimulationLabRuntimeCatalog* catalog){
    if(entity.production_child3_removed)return;
    const auto world_scope=run.native_world_objects.enter_owner(simulation_lab_world_model_handle(entity));
    if(entity.runtime_model_hierarchy&&catalog&&!entity.native_world_model_slots.empty()){
        const auto& nodes=*entity.runtime_model_hierarchy;
        std::optional<std::vector<SimulationLabEffectPose>> poses;
        for(size_t node=1;node<entity.native_world_model_slots.size()&&node<nodes.size();++node){
            size_t ancestor=node;
            for(size_t hops=0;nodes[ancestor].parent_index!=0U&&
                nodes[ancestor].parent_index<nodes.size()&&hops<nodes.size();++hops)
                ancestor=nodes[ancestor].parent_index;
            if(nodes[ancestor].parent_index==0U&&nodes[ancestor].child_slot==3U&&
                entity.native_world_model_slots[node]!=kSimulationLabNoWorldSlot){
                if(!poses)poses=simulation_lab_current_model_poses(entity,field,*catalog);
                lab_release_model_world_node(run,entity,node,*poses);
            }
        }
    }
    entity.production_child3_removed=true;
}

void release_model_smoke_slot(SimulationLabRun& run,SimulationLabEntity& entity,std::size_t index,
                             std::span<const SimulationLabEffectPose> poses={}) {
    auto& component=entity.model_components[index];
    if(component.native_model_pool_slot==kSimulationLabNoComponent)return;
    run.vacant_model_smoke_cooldowns[component.native_model_pool_slot]=
        index==entity.root_model_component_index?entity.damage_effect_cooldown:component.retained_damage_effect_cooldown;
    run.retained_model_effect_points.at(component.native_model_pool_slot)=component.timed_effect_point_index;
    component.native_model_pool_slot=kSimulationLabNoComponent;
    if(entity.runtime_model_hierarchy)
        for(size_t node=0;node<entity.native_world_model_slots.size();++node)
            if(lab_world_model_component(entity,node)==index)
                lab_release_model_world_node(run,entity,node,poses);
}
}
 
void SimulationLabExperiment::reset(
    const SimulationLabConfig& input,
    std::span<const std::uint32_t, AiKnowledgeData::kParameterCount> base_parameters,
    std::span<const SimulationLabSeedEntity> seed_entities) {
    reset(input, base_parameters, seed_entities, {}, {});
}

void SimulationLabExperiment::reset(
    const SimulationLabConfig& input,
    std::span<const std::uint32_t, AiKnowledgeData::kParameterCount> base_parameters,
    std::span<const SimulationLabSeedEntity> seed_entities,
    std::span<const SimulationLabFactionEconomySeed> faction_economy) {
    reset(input, base_parameters, seed_entities, faction_economy, {});
}

void SimulationLabExperiment::reset(
    const SimulationLabConfig& input,
    std::span<const std::uint32_t, AiKnowledgeData::kParameterCount> base_parameters,
    std::span<const SimulationLabSeedEntity> seed_entities,
    std::span<const SimulationLabFactionEconomySeed> faction_economy,
    std::span<const SimulationLabProductionAsset> production_assets) {
    reset(input, base_parameters, seed_entities, faction_economy, production_assets, {}, {}, {});
}

void SimulationLabExperiment::reset(
    const SimulationLabConfig& input,
    std::span<const std::uint32_t, AiKnowledgeData::kParameterCount> base_parameters,
    std::span<const SimulationLabSeedEntity> seed_entities,
    std::span<const SimulationLabFactionEconomySeed> faction_economy,
    std::span<const SimulationLabProductionAsset> production_assets,
    std::span<const SimulationLabTechnologyAsset> technology_assets,
    std::span<const FactionTechnologyMask256> faction_technology_masks,
    std::span<const SimulationLabFactionScoreSeed> faction_score_seeds,
    const SimulationLabScenarioRuntimeSeed& scenario_runtime) {
    clear();
    config_ = input;
    config_.run_count = std::clamp(config_.run_count, 1U, 32U);
    config_.worker_thread_limit = std::min(config_.worker_thread_limit,32U);
    config_.runtime_callback_step_ticks = std::clamp(config_.runtime_callback_step_ticks, 1U, 5U);
    config_.parameter_index = std::min<std::uint32_t>(config_.parameter_index,
        static_cast<std::uint32_t>(AiKnowledgeData::kParameterCount - 1U));
    seed_entities_.assign(seed_entities.begin(), seed_entities.end());
    production_assets_.assign(production_assets.begin(), production_assets.end());
    technology_assets_.assign(technology_assets.begin(), technology_assets.end());
    prepare_ai_scoring_catalog();
    static_navigation_blockers_=scenario_runtime.static_navigation_blockers;
    runtime_catalog_=scenario_runtime.runtime_catalog;
    environmental_emitters_=scenario_runtime.environmental_emitters;
    static_placement_owners_=scenario_runtime.static_placement_owners;
    static_shot_hulls_=scenario_runtime.static_shot_hulls;
    for(const auto& scenery:scenario_runtime.crushable_scenery){
        const auto* product=production_asset(scenery.army_asset_id);
        if(!product)continue;
        auto seed=product->entity_seed;
        seed.source_placement_index=scenery.placement_index;
        seed.faction_index=0U;
        seed.actor.position=scenery.position;
        seed.actor.body_yaw_turn16=scenery.yaw&0xFFFFU;
        seed_entities_.push_back(std::move(seed));
    }
    const auto values = simulation_lab_sweep_values(config_);
    runs_.reserve(config_.run_count);
    for (std::uint32_t run_index = 0; run_index < config_.run_count; ++run_index) {
        SimulationLabRun run;
        run.static_placement_owner_count = static_placement_owners_.size();
        run.run_index = run_index;
        run.seed = config_.vary_seed_per_run ? config_.base_seed + run_index * 0x9E3779B9U
                                             : config_.base_seed;
        run.rng_state = run.seed;
        run.active_view_faction_index=std::min(scenario_runtime.active_view_faction_index,7U);
        run.faction_relation_transition_ticks.fill(17U);
        run.mapped_terrain_sound_asset_ids=scenario_runtime.mapped_terrain_sound_asset_ids;
        run.swept_value = values[run_index];
        std::copy(base_parameters.begin(), base_parameters.end(), run.ai_parameters.begin());
        run.ai_parameters[config_.parameter_index] = static_cast<std::uint32_t>(run.swept_value);
        if (scenario_runtime.enabled) {
            run.level_runtime_enabled = true;
            run.assignable_faction_count = std::min<std::uint32_t>(scenario_runtime.assignable_faction_count, 7U);
            run.game_speed_q8 = scenario_runtime.game_speed_q8;
            run.ai_planning_enabled=scenario_runtime.ai_planning_enabled;
            run.ai_planner_dispatch_mask=scenario_runtime.ai_planner_dispatch_mask;
            run.reduced_update=scenario_runtime.reduced_update;
            run.local_faction_ended=scenario_runtime.local_faction_ended;
            run.active_faction_count = std::min<std::uint32_t>(scenario_runtime.active_faction_count, 7U);
            run.relation_ui_flags = scenario_runtime.relation_ui_flags;
            for (std::uint32_t faction = 0U; faction < 8U; ++faction)
                run.faction_packed_relation_states[faction] = initial_packed_relation_row(faction);
            apply_relation_group_masks(run.faction_packed_relation_states,
                                       scenario_runtime.relation_state4_faction_group_masks, 4U);
            apply_relation_group_masks(run.faction_packed_relation_states,
                                       scenario_runtime.relation_state8_faction_group_masks, 8U);
            for(uint32_t f=0;f<8U;++f)for(uint32_t other=0;other<8U;++other)
                if(((run.faction_packed_relation_states[f]>>(other*4U))&15U)>=4U)
                    run.faction_capability_flags[f]|=1U<<other;
            for (std::uint32_t faction = 1U; faction <= run.active_faction_count; ++faction) {
                run.faction_lifecycle_states[faction] = kFactionLifecycleActive;
                const bool human=scenario_runtime.human_faction_mask
                    ? ((*scenario_runtime.human_faction_mask & (1U<<faction))!=0U)
                    : faction<=run.assignable_faction_count;
                run.faction_frontend_player_counts[faction]=scenario_runtime.frontend_player_counts
                    ?(*scenario_runtime.frontend_player_counts)[faction]:uint32_t(human);
                if (scenario_runtime.simulate_all_active_factions_as_ai || !human)
                    run.ai_controlled_faction_mask |= 1U << faction;
            }
            run.terrain_lighting_cycle_active=scenario_runtime.terrain_lighting_cycle_active;
            run.scheduled_conditions = scenario_runtime.scheduled_conditions;
            for (auto& condition : run.scheduled_conditions) condition.status_and_kind &= 0xFEU;
            run.end_condition_triggers = scenario_runtime.end_condition_triggers;
            for (auto& trigger : run.end_condition_triggers) trigger.state_flags &= 1U;
        }
        for (std::size_t faction = 0; faction < run.faction_economy.size() &&
             faction < faction_economy.size(); ++faction)
            run.faction_economy[faction] = faction_economy[faction];
        for (std::size_t faction = 0; faction < run.faction_technology_masks.size() &&
             faction < faction_technology_masks.size(); ++faction)
            run.faction_technology_masks[faction] = faction_technology_masks[faction];
        if (run.level_runtime_enabled)
            for (auto& mask : run.faction_technology_masks)
                set_faction_technology_unlocked(mask, 0U, true);
        for (std::size_t faction = 0; faction < run.faction_score.size() &&
             faction < faction_score_seeds.size(); ++faction) {
            run.faction_score[faction].xenite_extracted_total_q4 =
                faction_score_seeds[faction].xenite_extracted_total_q4;
            run.faction_score[faction].tritium_extracted_total_q4 =
                faction_score_seeds[faction].tritium_extracted_total_q4;
        }
        for (std::size_t faction = 0; faction < run.faction_score.size(); ++faction)
            simulation_lab_recompute_points(run.faction_score[faction],
                                            run.faction_technology_masks[faction]);
        run.entities.reserve(seed_entities_.size() + 32U);
        for (const auto& seed : seed_entities_) {
            grant_creation_technologies(run, seed.faction_index, seed);
            SimulationLabEntity entity;
            entity.source_placement_index = seed.source_placement_index;
            entity.army_asset_id = seed.army_asset_id;
            if (const auto* arm = production_asset(seed.army_asset_id)) {
                entity.military_site_weight = static_cast<std::int32_t>(arm->ai_definition_class_value70);
                entity.military_anchor_weight = static_cast<std::int32_t>(arm->ai_definition_class_value74);
                entity.military_secondary_weight = static_cast<std::int32_t>(arm->ai_definition_class_value78);
            }
            entity.faction_index = seed.faction_index;
            entity.runtime_model_class_id = seed.runtime_model_class_id;
    entity.destroyed_replacement_army_asset_id = seed.destroyed_replacement_army_asset_id;
            entity.building = seed.building;
            entity.has_traversal_profile = seed.has_traversal_profile;
            entity.traversal_low_band_index = seed.traversal_low_band_index;
            entity.traversal_high_band_index = seed.traversal_high_band_index;
            entity.motion = seed.motion;
            entity.water_damage_multiplier = seed.water_damage_multiplier;
            entity.water_damage_threshold_q12 = seed.water_damage_threshold_q12;
            entity.vertical_deployment_step_q12_per_tick = seed.vertical_deployment_step_q12_per_tick;
            entity.vertical_deployment_travel_limit_q12 = seed.vertical_deployment_travel_limit_q12;
            entity.class23_deployment_sound_asset_id = seed.class23_deployment_sound_asset_id;
            entity.tank_track_animation_enabled = seed.tank_track_animation_enabled;
            entity.tank_track_local_bounds_y0 = seed.tank_track_local_bounds_y0;
            entity.tank_track_texture_travel_scale = seed.tank_track_texture_travel_scale;
            entity.articulated_contact_interpolation_rate_q12 =
                seed.articulated_contact_interpolation_rate_q12;
            entity.articulated_contact_sound_asset_id = seed.articulated_contact_sound_asset_id;
            entity.articulated_contact_geometry_enabled = seed.articulated_contact_geometry_enabled;
            entity.articulated_contact_local_offsets = seed.articulated_contact_local_offsets;
            entity.articulated_root_height_q12 = seed.articulated_root_height_q12;
            entity.articulated_contact_lift_q12 = seed.articulated_contact_lift_q12;
            entity.articulated_contact_steering_limit_turn16 =
                seed.articulated_contact_steering_limit_turn16;
            entity.articulated_shared_foot_placement_offset =
                seed.articulated_shared_foot_placement_offset;
            entity.movement_ground_effect_interval_ticks = seed.movement_ground_effect_interval_ticks;
            entity.movement_ground_land_effect_id = seed.movement_ground_land_effect_id;
            entity.movement_ground_water_effect_id = seed.movement_ground_water_effect_id;
            entity.runtime_sound_profile = seed.runtime_sound_profile;
            entity.positioned_sound_gain_q15 = seed.positioned_sound_gain_q15;
            entity.positioned_sound_maximum_distance_q12 =
                seed.positioned_sound_maximum_distance_q12;
            entity.production_compatibility_mask = seed.production_compatibility_mask;
            entity.production_storage_capacity = std::min<std::uint32_t>(
                seed.production_storage_capacity, static_cast<std::uint32_t>(kSimulationClass22StoredSlotCount));
            entity.aircraft_definition=seed.aircraft_definition;
            entity.airport_platform_step_q12=seed.airport_platform_step_q12;
            entity.airport_platform_limit_q12=seed.airport_platform_limit_q12;
            entity.airport_platform_baseline_q12=seed.airport_platform_baseline_q12;
            entity.runtime_class_specific_c0_raw = seed.runtime_class_specific_c0_raw;
            entity.runtime_class_specific_c4_raw = seed.runtime_class_specific_c4_raw;
            entity.runtime_class_specific_19c_raw = seed.runtime_class_specific_19c_raw;
            entity.runtime_class_specific_1a8_raw = seed.runtime_class_specific_1a8_raw;
            entity.class18_conversion_effect_lifetime_ticks = seed.class18_conversion_effect_lifetime_ticks;
            entity.runtime_flags = seed.runtime_flags;
            entity.production_spawn_world = seed.production_spawn_world;
            entity.production_exit_world = seed.production_exit_world;
            entity.production_drive_target_world=seed.production_drive_target_world;
    entity.production_spawn_local = seed.production_spawn_local;
    entity.production_exit_local = seed.production_exit_local;
    entity.production_root_sprite_height_offset = seed.production_root_sprite_height_offset;
    entity.production_door_step_q12 = seed.production_door_step_q12;
            entity.research_technology_ids = seed.research_technology_ids;
            entity.research_model_variants = seed.research_model_variants;
            entity.combat_weapons = seed.combat_weapons;
            entity.shot_impact_class_index = seed.shot_impact_class_index;
            entity.fog_uncover_radius_q12 = seed.fog_uncover_radius_q12;
            entity.occupancy_mark_radius_units = seed.occupancy_mark_radius_units;
            entity.fog_reference_height_offset_q12 = seed.fog_reference_height_offset_q12;
            entity.actor = seed.actor;
            entity.ai_path_coordinate_q12=lab_ai_model_point_v719(entity.actor.position);
            entity.model_components.reserve(seed.model_components.size() + 1U);
            for (const auto& source : seed.model_components) {
                SimulationLabModelComponentState component;
                component.source_model_node_index = source.source_model_node_index;
                component.model_definition_id = source.model_definition_id;
                component.definition_flags68 = source.definition_flags68;
                component.runtime_class_id = source.runtime_class_id;
                // Class 3 shares the EFF fields with foot contacts but suppresses
                // timed emission with the native class initializer's sentinel.
                if(component.runtime_class_id==3U)component.timed_effect_countdown=0x7FFFFFFFU;
                component.placement_contact_kind = source.placement_contact_kind;
                component.maximum_integrity = std::max<std::int32_t>(0, source.maximum_integrity);
                component.health_regeneration_per_step = source.health_regeneration_per_step;
                component.health_regeneration_delay_ticks = source.health_regeneration_delay_ticks;
                component.integrity = component.maximum_integrity;
                component.model_rotation_angle0 = source.model_rotation_angle0;
                component.resource_support_class = source.resource_support_class;
                component.resource_extraction_weight_u8 = source.resource_extraction_weight_u8;
        component.extraction_model_world_offset_at_seed = source.extraction_model_world_offset_at_seed;
        component.extraction_model_seed_yaw_turn16 = source.extraction_model_seed_yaw_turn16;
                component.resource_storage_selector = source.resource_storage_selector;
                component.resource_storage_capacity_contribution_q4 =
                    source.resource_storage_capacity_contribution_q4;
                component.has_resource_storage_capacity_contribution =
                    source.has_resource_storage_capacity_contribution;
                component.retail_energy_demand_q4 = source.retail_energy_demand_q4;
                component.energy_generation_capacity_q4 = source.energy_generation_capacity_q4;
                component.terminal_attachment_effect_ids = source.terminal_attachment_effect_ids;
                component.terminal_attachment_countdowns = source.terminal_attachment_delay_ticks;
                component.parent_component_index = source.parent_component_index;
                entity.model_components.push_back(component);
            }
            if (entity.model_components.empty()) {
                SimulationLabModelComponentState root;
                root.runtime_class_id = seed.runtime_model_class_id;
                if(root.runtime_class_id==3U)root.timed_effect_countdown=0x7FFFFFFFU;
                root.maximum_integrity = std::max<std::int32_t>(0, seed.root_model_maximum_integrity);
                root.integrity = root.maximum_integrity;
                entity.model_components.push_back(root);
            }
            for (std::size_t component_index = 0; component_index < entity.model_components.size(); ++component_index) {
                auto& component = entity.model_components[component_index];
                if (component.parent_component_index &&
                    (*component.parent_component_index >= entity.model_components.size() ||
                     *component.parent_component_index == component_index))
                    component.parent_component_index.reset();
            }
            // Editor sanitation for direct API/test seeds. Retail-created component trees are
            // acyclic; rejecting a cycle here prevents malformed input from recursing forever.
            for (std::size_t component_index = 0; component_index < entity.model_components.size(); ++component_index) {
                auto& component = entity.model_components[component_index];
                std::optional<std::size_t> cursor = component.parent_component_index;
                std::size_t hops = 0U;
                while (cursor && *cursor < entity.model_components.size() &&
                       hops++ < entity.model_components.size()) {
                    if (*cursor == component_index) {
                        component.parent_component_index.reset();
                        break;
                    }
                    cursor = entity.model_components[*cursor].parent_component_index;
                }
                if (cursor && hops >= entity.model_components.size())
                    component.parent_component_index.reset();
            }
            for (std::size_t component_index = 0; component_index < entity.model_components.size(); ++component_index) {
                if (!entity.model_components[component_index].parent_component_index &&
                    entity.root_model_component_index == kSimulationLabNoComponent)
                    entity.root_model_component_index = component_index;
            }
            if (entity.root_model_component_index == kSimulationLabNoComponent)
                entity.root_model_component_index = 0U;
            entity.collision_model_component_index =
                seed.collision_model_component_index < entity.model_components.size()
                    ? seed.collision_model_component_index : entity.root_model_component_index;
            sync_root_component_summary(entity);
            // A map placement creates fresh SPR weapon nodes. Seed vectors carry
            // preview defaults, not a retained live turret pose.
            if(run.level_runtime_enabled&&runtime_catalog_)entity.actor.weapons.clear();
            if(runtime_catalog_)simulation_lab_initialize_runtime_entity(entity,seed,
                run.faction_technology_masks[std::min<size_t>(seed.faction_index,7U)],*runtime_catalog_,false);
            run.entities.push_back(std::move(entity));
        }
        if(run.level_runtime_enabled&&!scenario_runtime.initial_army_placements.empty()){
            run.army_pool.enabled=true;
            auto placements=scenario_runtime.initial_army_placements;
            std::stable_sort(placements.begin(),placements.end(),[](auto& a,auto& b){return a.source_placement_index<b.source_placement_index;});
            for(const auto& placement:placements){
                if(placement.faction!=0U&&(placement.faction>=8U||run.faction_lifecycle_states[placement.faction]!=1U))continue;
                const auto slot=run.army_pool.create(placement.army,placement.faction);
                if(!slot)throw std::runtime_error("Initial native army pool exhausted");
                for(size_t i=0;i<run.entities.size();++i)if(run.entities[i].source_placement_index==placement.source_placement_index){
                    run.entities[i].native_army_pool_slot=*slot;run.army_pool.slots[*slot].entity_index=i;break;
                }
            }
        }
        if(run.level_runtime_enabled&&runtime_catalog_){
            std::vector<std::size_t> placement_order(run.entities.size());
            std::iota(placement_order.begin(),placement_order.end(),0U);
            std::stable_sort(placement_order.begin(),placement_order.end(),[&](auto a,auto b){
                return run.entities[a].source_placement_index<run.entities[b].source_placement_index;
            });
            std::vector<size_t> emitter_order(environmental_emitters_.size());
            run.environmental_emitter_world_handles.resize(emitter_order.size(),kSimulationLabNoWorldSlot);
            std::iota(emitter_order.begin(),emitter_order.end(),0U);
            std::stable_sort(emitter_order.begin(),emitter_order.end(),[&](auto a,auto b){
                return environmental_emitters_[a].source_placement_index<
                    environmental_emitters_[b].source_placement_index;
            });
            // Scenery still reserves native world records when it has no
            // dynamic Lab entity or emitter. Account for its complete MODEL
            // hierarchy at its LEV position in the owner list.
            auto static_placements=scenario_runtime.initial_army_placements;
            std::erase_if(static_placements,[&](const auto& p){
                return (p.faction!=0U&&(p.faction>=8U||run.faction_lifecycle_states[p.faction]!=1U))||
                    std::any_of(run.entities.begin(),run.entities.end(),[&](const auto& e){
                        return e.source_placement_index==p.source_placement_index;
                    });
            });
            std::stable_sort(static_placements.begin(),static_placements.end(),[](const auto& a,const auto& b){
                return a.source_placement_index<b.source_placement_index;
            });
            size_t emitter=0;
            const auto claim_emitter=[&](size_t index) {
                run.environmental_emitter_world_handles[index]=run.native_world_objects.claim(
                    SimulationLabWorldObjectPool::Kind::Model,environmental_emitters_[index].model_definition_id);
            };
            const auto claim_emitters_before=[&](size_t placement){
                while(emitter<emitter_order.size()&&
                    environmental_emitters_[emitter_order[emitter]].source_placement_index<placement)
                    claim_emitter(emitter_order[emitter++]);
            };
            const auto attach_emitters=[&](size_t placement,size_t handle){
                while(emitter<emitter_order.size()&&
                    environmental_emitters_[emitter_order[emitter]].source_placement_index==placement){
                    const auto index=emitter_order[emitter++];
                    if(handle==kSimulationLabNoWorldSlot)claim_emitter(index);
                    else run.environmental_emitter_world_handles[index]=handle;
                }
            };
            const auto claim_static=[&](const SimulationArmyPlacementSeed& placement){
                claim_emitters_before(placement.source_placement_index);
                SimulationLabEntity model;
                SimulationLabSeedEntity seed;
                seed.army_asset_id=placement.army;
                model.army_asset_id=placement.army;
                if(simulation_lab_initialize_runtime_entity(model,seed,{},*runtime_catalog_,true))
                    lab_claim_model_world_nodes(run,model);
                attach_emitters(placement.source_placement_index,simulation_lab_world_model_handle(model));
            };
            size_t next_static=0;
            for(const auto index:placement_order){
                const auto placement=run.entities[index].source_placement_index;
                while(next_static<static_placements.size()&&
                    static_placements[next_static].source_placement_index<placement)
                    claim_static(static_placements[next_static++]);
                claim_emitters_before(placement);
                claim_model_smoke_slots(run,run.entities[index],index);
                attach_emitters(placement,simulation_lab_world_model_handle(run.entities[index]));
            }
            while(next_static<static_placements.size())claim_static(static_placements[next_static++]);
            for(;emitter<emitter_order.size();++emitter)claim_emitter(emitter_order[emitter]);
        }
        initialize_retail_starting_primary_queue(run);
        recompute_resource_storage_limits(run);
        // Stock defaults and class-16 initializers register capacity before tick zero.
        // This refresh neither allocates power nor burns fuel.
        recompute_energy_generation_capacity(run);
        runs_.push_back(std::move(run));
    }
}

bool SimulationLabExperiment::finished() const noexcept {
    return !runs_.empty() && std::all_of(runs_.begin(), runs_.end(),
        [](const SimulationLabRun& run) { return run.finished; });
}

std::optional<std::size_t> SimulationLabExperiment::nearest_enemy(
    const SimulationLabRun& run, std::size_t entity_index) noexcept {
    if (entity_index >= run.entities.size()) return std::nullopt;
    const auto& source = run.entities[entity_index];
    if (!simulation_lab_has_live_root_model_owner(source) ||
        source.faction_index == 0U || !source.construction_complete ||
        source.root_model_terminal || scenario_entity_disabled(source)) return std::nullopt;
    float best = std::numeric_limits<float>::max();
    std::optional<std::size_t> result;
    for (std::size_t visit = 0; visit < run.entities.size(); ++visit) {
        const std::size_t i=run.entities.size()-1U-visit;
        if (i == entity_index) continue;
        const auto& candidate = run.entities[i];
        if (!simulation_lab_combat_target_owner_available(candidate) ||
            scenario_entity_disabled(candidate) || candidate.faction_index == 0U ||
            candidate.faction_index == source.faction_index) continue;
        if (run.level_runtime_enabled &&
            (run.faction_capability_flags[source.faction_index]&
             (1U<<(candidate.faction_index&7U)))!=0U) continue;
        // Native target selection rejects a zero source damage counter for this class.
        if(!source.combat_weapons.empty()&&std::none_of(source.combat_weapons.begin(),source.combat_weapons.end(),
            [&](const auto& w){return !w.shot.is_repair_or_recovery_shot()&&candidate.shot_impact_class_index<8U&&
                w.shot.target_impact_damage_q12[candidate.shot_impact_class_index]>0;}))continue;
        const float dx = candidate.actor.position.x - source.actor.position.x;
        const float dy = candidate.actor.position.y - source.actor.position.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; result = i; }
    }
    return result;
}

std::optional<std::uint32_t> SimulationLabExperiment::choose_construction_asset(
    SimulationLabRun& run, std::uint32_t faction, const FieldGrid& field,
    std::int32_t* selected_packed_score,
    std::vector<SimulationLabCachedAiCandidate>* structural_candidates, bool include_strategic) {
    std::optional<std::uint32_t> best;
    std::int32_t best_score = std::numeric_limits<std::int32_t>::min();
    const bool live_sites=run.level_runtime_enabled && runtime_catalog_;
    const auto site_workspaces=live_sites ? lab_rebuild_placement_workspaces(run,faction,field) : LabPlacementWorkspaces{};
    const auto site_owners=live_sites ? simulation_lab_site_owners(run,field,*runtime_catalog_,static_placement_owners_) : std::vector<SimulationLabSiteOwner>{};
    const auto site_model=[&](uint32_t id)->std::optional<SimulationLabSiteModel>{
        return live_sites ? simulation_lab_first_site_model(*runtime_catalog_,id) : std::nullopt;
    };
    const auto record_candidate = [&](uint32_t id, int32_t score,uint32_t kind=1U) {
        if(structural_candidates) structural_candidates->push_back({id,score,kind});
        if(!best||score>best_score) { best=id; best_score=score; }
    };
    const auto own_workspace=run.level_runtime_enabled
        ? simulation_lab_ai_own_workspaces(run,faction) : SimulationLabAiOwnWorkspaces{};
    const auto pending_same = [&](std::uint32_t asset_id) {
        if(run.level_runtime_enabled) return std::any_of(own_workspace.structures.begin(),
            own_workspace.structures.end(),[&](const auto& e){
                return e.army_asset_id==asset_id && !e.entity_index;
            });
        const auto matches = [&](const SimulationLabPendingAssetRequest& request) {
            return request.faction_index == faction && request.army_asset_id == asset_id;
        };
        return std::any_of(run.pending_asset_requests.begin(), run.pending_asset_requests.end(), matches) ||
               std::any_of(run.secondary_purchase_requests.begin(),
                           run.secondary_purchase_requests.end(), matches) ||
               std::any_of(run.entities.begin(),run.entities.end(),[&](const auto& producer){
                   // AiPlanningRebuild_AddAssetsInProduction puts a null runtime
                   // entry in workspace00 for a class11 asset still being built.
                   return producer.faction_index==faction&&!producer.root_model_terminal&&
                       producer.runtime_model_class_id==11U&&producer.production_building&&
                       producer.production_asset_id==asset_id;
               });
    };

    const bool has_330 = run_has_asset_presence(run,faction,330U);
    if(has_330&&!pending_same(310U)) {
        const auto& economy=run.faction_economy[faction];
        if(const auto weight=retail_power_candidate_weight(
            run.faction_energy_generation_capacity_q4[faction],economy.baseline_energy_supply_q4,
            run.faction_tritium_extraction_rate_q4_per_tick[faction],
            run.faction_supplied_energy_demand_q4[faction],run.faction_unpowered_energy_demand_q4[faction],
            run.ai_parameters[0U],run.ai_parameters[0x13U])) {
            const auto delta=simulation_lab_ai_candidate_weighted_delta(*weight,random_next(run.rng_state));
            if(delta&&production_asset(310U)) record_candidate(310U,static_cast<int32_t>(*delta|1U));
        }
    }

    for (std::uint32_t i = 0; i < kConstructionAssetIds.size(); ++i) {
        const std::uint32_t asset_id = kConstructionAssetIds[i];
        if (pending_same(asset_id)) continue;

        // V533 AiCandidateWorkspace_AddOrAccumulateWeightedEntry randomizes every contribution
        // separately, then accumulates the packed dword score. The compact Lab can model at most
        // two contributions for these four candidates (14D base + one workspace08-derived term).
        std::array<std::int64_t, 2> contributions{};
        std::size_t contribution_count = 0U;
        const auto add_weight_source = [&](std::int64_t value) {
            if (contribution_count < contributions.size())
                contributions[contribution_count++] = value;
        };
        std::int64_t base_weight = signed_parameter(run.ai_parameters[kConstructionParameterIndices[i]]);

        if (asset_id == 0x14AU || asset_id == 0x14CU) {
            const std::uint32_t support_class = asset_id == 0x14AU ? 0U : 1U;
            if(live_sites) {
                if(const auto model=site_model(asset_id))for(const auto& site:site_workspaces.resource08) {
                    if(site.army_asset_id!=asset_id)continue;
                    ++run.metrics.live_extractor_site_tests;
                    const auto accepted=simulation_lab_query_site_placement(run,field,site_owners,*model,
                        {float_to_q12(site.point.x),float_to_q12(site.point.y)},faction,4U,0);
                    if(!accepted.accepted)continue;
                    ++run.metrics.live_extractor_site_accepts;
                    add_weight_source(base_weight<0 ? base_weight : simulation_lab_extractor_weight(
                        static_cast<uint32_t>(base_weight),run_asset_count(run,faction,asset_id),asset_id,
                        run.faction_tritium_extraction_rate_q4_per_tick[faction],
                        run.faction_supplied_energy_demand_q4[faction],run.faction_unpowered_energy_demand_q4[faction]));
                    break; // Only first accepted matching workspace08 entry contributes/RNG-draws.
                }
            } else if (field_has_resource_support(field, support_class)) add_weight_source(base_weight);
        } else if (asset_id == 0x14BU) {
            // V531 AiStructureCandidate_AddWeightedId14BOr14CCandidate: presence is satisfied by
            // assigned OR pending 14A. The KI[20]/KI[22] comparisons are strict and operate on
            // faction Xenite storage/current Q4, not building counts.
            if (run_has_asset_presence(run, faction, 0x14AU)) {
                const auto& economy = run.faction_economy[faction];
                if (!economy.xenite_storage_limit_q4) {
                    ++run.metrics.unknown_storage_capacity_blocks;
                } else {
                    const std::uint32_t storage = *economy.xenite_storage_limit_q4;
                    const std::uint32_t current = economy.xenite_current_q4;
                    const std::uint32_t free_storage = storage - current; // retail unsigned wrap
                    if (storage != 0U && storage < run.ai_parameters[20U] &&
                        free_storage < run.ai_parameters[22U]) {
                        if (storage == current) {
                            base_weight *= 4;
                            ++run.metrics.xenite_full_weight_boosts;
                        }
                        add_weight_source(base_weight);
                    }
                }
            }
        } else if (asset_id == 0x14DU) {
            // V533 proves the base and workspace08-derived 14D weights are separate calls to
            // AddOrAccumulateWeightedEntry, therefore they consume separate RNG draws and only
            // then accumulate. Workspace08 population itself remains a compact-Lab approximation.
            if (run_has_asset_presence(run, faction, 0x14CU)) {
                const auto& economy = run.faction_economy[faction];
                if (!economy.tritium_storage_limit_q4) {
                    ++run.metrics.unknown_storage_capacity_blocks;
                } else {
                    const std::uint32_t storage = *economy.tritium_storage_limit_q4;
                    const std::uint32_t current = economy.tritium_current_q4;
                    const std::uint32_t free_storage = storage - current;
                    if (storage != 0U && storage < run.ai_parameters[21U] &&
                        free_storage < run.ai_parameters[23U]) {
                        add_weight_source(base_weight);
                    }
                }
            }
            // Legacy metadata-free fixtures approximate the workspace08-derived
            // KI[5]/KI[6] weight using unclaimed resource islands. The authored
            // scenario path below does not treat that approximation as eligibility.
            const auto try_site = [&](std::uint32_t source_id,std::uint32_t kind,
                                      std::uint32_t weight_index) -> bool {
                if (!field_has_unclaimed_resource_site(field,run,kind,source_id,faction)) return false;
                const auto count = run_asset_count(run,faction,source_id);
                const auto derived = signed_parameter(run.ai_parameters[weight_index])*3LL /
                    static_cast<std::int64_t>(count+3U);
                add_weight_source(derived);
                ++run.metrics.derived14d_weight_events;
                return true;
            };
            // The free-site proxy cannot stand in for retail's mode7/mode4
            // count and four temporary placement probes (0x53A110/0x53ABE0).
            // In authored runs this invented an endless ARM333 bonus even
            // beyond the storage threshold. Ordinary capacity/gap scoring above
            // remains live. Keep the legacy metadata-free fixture branch explicit.
            if(live_sites) {
                const auto storage=site_model(333U);
                if(storage)for(const auto& site:site_workspaces.resource08) {
                    const auto resource=site_model(site.army_asset_id);if(!resource)continue;
                    ++run.metrics.special_site_reservation_tests;
                    const auto reservation=simulation_lab_reserve_special_site(run,field,site_owners,*resource,*storage,
                        {float_to_q12(site.point.x),float_to_q12(site.point.y)},site_workspaces.base09,faction,&run.army_pool,&run.native_world_objects,runtime_catalog_.get());
                    run.metrics.special_site_probe_owners+=reservation.probes_created;
                    if(!reservation.accepted)continue;
                    ++run.metrics.special_site_reservation_accepts;
                    const auto raw=run.ai_parameters[site.army_asset_id==330U?5U:6U];
                    add_weight_source(signed_parameter(raw)<0 ? signed_parameter(raw) :
                        simulation_lab_extractor_weight(raw,run_asset_count(run,faction,site.army_asset_id),330U,0,0,0));
                    ++run.metrics.derived14d_weight_events;
                    ++run.metrics.live_storage_site_bonus_events;
                    break;
                }
            } else if (!run.level_runtime_enabled && !try_site(0x14AU,0U,5U))
                (void)try_site(0x14CU,1U,6U);
        }

        std::optional<std::uint32_t> packed_score;
        for (std::size_t contribution = 0; contribution < contribution_count; ++contribution) {
            // Retail obtains the random dword before testing weightRange > 1. Preserve that draw
            // count. Negative Lab sweep values are an editor-only disable convention; they still
            // consume the draw but are not reinterpreted as enormous unsigned retail weights.
            const std::uint32_t random_value = random_next(run.rng_state);
            const std::int64_t modeled_weight = contributions[contribution];
            if (modeled_weight <= 1) continue;
            const auto delta = simulation_lab_ai_candidate_weighted_delta(
                static_cast<std::uint32_t>(modeled_weight), random_value);
            if (!delta) continue;
            if (!packed_score) packed_score = *delta | 1U; // entryKind 1: army/construction
            else *packed_score += *delta; // intentional dword wrap
        }
        if (!packed_score) continue;
        if (!production_assets_.empty()) {
            const auto* candidate = production_asset(asset_id);
            if (candidate == nullptr) continue;
        }
        const std::int32_t score = static_cast<std::int32_t>(*packed_score);
        record_candidate(asset_id,score);
    }

    // V574 restores the ordinary factory candidate that the compact construction proxy omitted.
    // Retail dispatch order is 330/332/331/333 first, then AiCandidatePlanning_AddSpecialSiteCandidate,
    // then the strategic family. Both recovered branches can independently emit ARM301. They use
    // different entry kinds in the retail candidate workspace, so model both random draws and retain
    // the stronger packed score; AiPurchaseCandidate_HasEligibleProducerCf then applies the same
    // class-11/13/22 producer gate as every other ARM candidate.
    constexpr std::uint32_t kFactory301 = 301U;
    const bool factory301_present = run_has_asset_presence(run, faction, kFactory301);
    const bool unit50_secondary_present = std::any_of(
        run.secondary_purchase_requests.begin(), run.secondary_purchase_requests.end(),
        [&](const SimulationLabPendingAssetRequest& request) {
            return request.faction_index == faction && request.army_asset_id == 50U;
        });
    const auto consider_factory301 = [&](std::int64_t modeled_weight, std::uint32_t entry_kind) {
        // AddOrAccumulate always consumes the RNG dword before testing weightRange > 1.
        const std::uint32_t random_value = random_next(run.rng_state);
        if (modeled_weight <= 1 || factory301_present) return;
        const auto* candidate = production_asset(kFactory301);
        if (candidate == nullptr) return;
        const auto delta = simulation_lab_ai_candidate_weighted_delta(
            static_cast<std::uint32_t>(modeled_weight), random_value);
        if (!delta) return;
        const std::uint32_t packed_score = *delta | (entry_kind & 0xFU);
        const std::int32_t score = static_cast<std::int32_t>(packed_score);
        record_candidate(kFactory301,score);
        ++run.metrics.factory301_candidate_events;
    };

    if(live_sites) {
        const auto own=simulation_lab_ai_own_workspaces(run,faction);
        const bool unit50_present=std::any_of(own.units.begin(),own.units.end(),
            [](const auto& e){return e.army_asset_id==50U;});
        const auto* unit50=production_asset(50U);
        auto& trace=run.ai_special_site_trace[faction];trace={};trace.tick=run.tick;
        trace.workspace08_count=static_cast<uint32_t>(site_workspaces.resource08.size());
        std::array<std::vector<MovementPoint>,2> hostile_points;
        if(const auto* snapshot=lab_retained_planning_snapshot(run,faction)){
            for(size_t slot=0;slot<hostile_points.size();++slot)
                for(const auto index:snapshot->owner_lists.workspace(2U+slot)){
                    const auto& p=run.entities[index].actor.position;
                    hostile_points[slot].push_back({float_to_q12(p.x),float_to_q12(p.y)});
                }
        }else for(size_t visit=0;visit<run.entities.size();++visit) {
            const auto& e=run.entities[run.entities.size()-1U-visit];
            if(e.runtime_removed)continue;
            const auto perception=simulation_lab_ai_hostile_perception(run,field,faction,e);
            if(perception==SimulationLabHostilePerception::Excluded)continue;
            const auto slot=perception==SimulationLabHostilePerception::Visible?0U:1U;
            const size_t capacity=slot==0U?128U:512U;
            if(hostile_points[slot].size()<capacity)
                hostile_points[slot].push_back({float_to_q12(e.actor.position.x),float_to_q12(e.actor.position.y)});
        }
        const auto candidate=retail_special_site_candidate(unit50_present,factory301_present,
            unit50&&(unit50->army_runtime_flags14_raw&1U)!=0U,
            faction_technology_unlocked(run.faction_technology_masks[faction],11U),
            site_workspaces.resource08,run.ai_parameters[1U],run.ai_parameters[2U],run.ai_parameters[89U],
            run.faction_economy[faction].tritium_current_q4,
            run.faction_supplied_energy_demand_q4[faction],run.faction_unpowered_energy_demand_q4[faction],
            [&](size_t i){
                ++trace.cluster_tests;
                const auto& site=site_workspaces.resource08[i];
                const auto model=site_model(site.army_asset_id);
                if(!model)return true;
                const auto cluster=simulation_lab_special_site_cluster(run,field,site_owners,*model,site_model(333U),
                    {float_to_q12(site.point.x),float_to_q12(site.point.y)},site_workspaces.base09,faction,&run.army_pool,&run.native_world_objects,runtime_catalog_.get());
                trace.probes_created+=cluster.probes_created;
                trace.clearance_q12=cluster.clearance_q12;trace.bucket_count=cluster.bucket_count;
                if(cluster.accepted)++trace.cluster_accepts;
                return !cluster.accepted;
            },
            [&](size_t i,uint32_t workspace){
                const auto& p=site_workspaces.resource08[i].point;
                const MovementPoint point{float_to_q12(p.x),float_to_q12(p.y)};
                int32_t nearest=INT32_MAX;
                for(const auto target:hostile_points[workspace==2U?0U:1U])
                    nearest=std::min(nearest,std::bit_cast<int32_t>(pioneer_abs_wrapped(point.x,target.x)+
                                                             pioneer_abs_wrapped(point.y,target.y)));
                if(std::bit_cast<int32_t>(run.ai_parameters[89U])>nearest)++trace.hostile_blocks;
                return nearest;
            },[&](uint32_t id){return run_asset_count(run,faction,id);});
        if(candidate) {
            trace.selected=true;trace.candidate_id=candidate->id;trace.candidate_kind=candidate->kind;
            trace.weight=candidate->weight;trace.site_index=static_cast<uint32_t>(candidate->site_index);
            const auto delta=simulation_lab_ai_candidate_weighted_delta(candidate->weight,random_next(run.rng_state));
            if(delta) {
                record_candidate(candidate->id,std::bit_cast<int32_t>(*delta|candidate->kind),candidate->kind);
                if(candidate->id==301U)++run.metrics.factory301_candidate_events;
            }
        }
    } else if (!run.level_runtime_enabled && !factory301_present && !unit50_secondary_present) {
        // Legacy metadata-only compatibility. Authored levels use actual cluster queries above.
        // AiCandidatePlanning_ComputeSpecialSiteWeight derives its candidate from the first usable
        // workspace08 special site. The compact Lab does not materialize workspace02/03 reservation
        // buffers, but it does preserve the exact resource-support domains and assigned-count term.
        if (field_has_resource_support(field, 0U)) {
            const auto assigned = run_asset_count(run, faction, 330U);
            const std::int64_t base = signed_parameter(run.ai_parameters[1U]);
            consider_factory301(base * 3LL / static_cast<std::int64_t>(assigned * 2U + 6U), 0U);
        } else if (field_has_resource_support(field, 1U)) {
            const auto assigned = run_asset_count(run, faction, 332U);
            const std::int64_t base = signed_parameter(run.ai_parameters[2U]);
            consider_factory301(base * 3LL / static_cast<std::int64_t>(assigned * 2U + 6U), 0U);
        }

    }

    if (include_strategic) {
        std::vector<SimulationLabCachedAiCandidate> strategic;
        append_strategic_construction_candidates(run,faction,strategic);
        for (const auto& candidate:strategic) record_candidate(candidate.id,candidate.packed_score);
    }
    if (selected_packed_score != nullptr && best) *selected_packed_score = best_score;
    return best;
}

void SimulationLabExperiment::append_strategic_construction_candidates(
    SimulationLabRun& run, std::uint32_t faction,
    std::vector<SimulationLabCachedAiCandidate>& candidates) {
    const auto capacity_available = [&](uint32_t additional) {
        const auto& e=run.faction_economy[faction];
        if(!e.derive_energy_allocation_from_components) return true;
        const int64_t supply=(e.baseline_energy_supply_q4>>4U)+
            run.faction_tritium_extraction_rate_q4_per_tick[faction];
        const int64_t capacity=run.faction_energy_generation_capacity_q4[faction]>>4U;
        const int64_t demand=(static_cast<uint64_t>(run.faction_supplied_energy_demand_q4[faction])+
            run.faction_unpowered_energy_demand_q4[faction])>>4U;
        return std::min(supply,capacity)>=demand+additional;
    };
    const bool has_330 = run_has_asset_presence(run,faction,330U);
    const bool factory301_present=run_has_asset_presence(run,faction,301U);
    const auto record_candidate=[&](uint32_t id,int32_t score) { candidates.push_back({id,score,1U}); };
    const auto consider_factory301=[&](int64_t weight,uint32_t kind) {
        const auto random=random_next(run.rng_state);
        if (weight<=1 || factory301_present || !production_asset(301U)) return;
        if (const auto delta=simulation_lab_ai_candidate_weighted_delta(static_cast<uint32_t>(weight),random)) {
            record_candidate(301U,static_cast<int32_t>(*delta|(kind&0xFU)));
            ++run.metrics.factory301_candidate_events;
        }
    };
    auto& trace=run.ai_strategic_construction_trace[faction];
    trace={};trace.tick=run.tick;trace.has_resource330=has_330;
    trace.arms_factories_unlocked=faction_technology_unlocked(run.faction_technology_masks[faction],1U);
    trace.factory_capacity_available=capacity_available(run.ai_parameters[0x11U]);
    trace.lab_capacity_available=capacity_available(run.ai_parameters[0x10U]);
    // 0x0053AC20 is independent of the ARM50 secondary-queue gate in 0x0053AE60.
    // A queued Pioneer vehicle (ARM50) must not suppress the Arms factories
    // (TEC1) / Xenite mine (ARM330) strategic gate.
    // Planning-capacity recovery remains separate.
    // Retail Technology_IsUnlockedForFaction(TEC1) returns TRUE while locked.  The
    // Lab's faction_technology_unlocked() is a normal positive predicate. The previous
    // condition inverted the strategic stage: it withheld ARM301 until TEC1 was already
    // researched, precisely the time when retail moves on to ARM302..306:
    // Armour factory, Walker factory, Glider factory, Harbour and Airport.
    // AiPrimaryWorkspace_HasEntryById also sees null-runtime pending and
    // class-11 in-production entries. A planned ARM330 satisfies this ID gate.
    const bool arms_factories_unlocked = faction < run.faction_technology_masks.size() &&
        faction_technology_unlocked(run.faction_technology_masks[faction], 1U);
    if (!factory301_present && has_330 && !arms_factories_unlocked &&
        capacity_available(run.ai_parameters[0x10U]))
        consider_factory301(signed_parameter(run.ai_parameters[0x07U]), 1U);

    // Recovered AiStrategicClass_SelectTerrainSuitedBuilding uses the actual retained
    // four-scratch-cells-per-axis GRID image. A prior Lab fallback always chose ARM302;
    // that distorted its cost and cache ranking on terrain favoring other factories.
    // Stock names and family grants: 302 Armour factory -> TEC15 TANK Badger;
    // 303 Walker factory -> TEC30 Emu walker; 304 Glider factory -> TEC45
    // GLD-1 Mosquito; 305 Harbour -> TEC60 Dolphin Class; 306 Airport -> TEC75
    // SP Crow. Those creation grants are not queued research. TEC categories
    // are a different field: the first four grants are A, SP Crow is "-".
    if (has_330 && arms_factories_unlocked && capacity_available(run.ai_parameters[0x11U]) &&
        run.retail_scratch && !run.retail_scratch->state.empty()) {
        constexpr std::array<std::uint32_t, 5> factory_ids{302U,303U,304U,305U,306U};
        std::array<bool, 5> present{};
        std::array<bool, 5> enabled{};
        for (std::size_t i = 0; i < factory_ids.size(); ++i) {
            present[i] = run_has_asset_presence(run, faction, factory_ids[i]);
            const auto* product = production_asset(factory_ids[i]);
            enabled[i] = product && (product->army_runtime_flags14_raw & 1U) != 0U;
        }
        // One selector RNG call is consumed even when all alternatives are present.
        const auto selection = select_strategic_factory_terrain_v600(
            run.retail_scratch->state, random_next(run.rng_state), present, enabled);
        trace.selected_factory=selection.selected_army_id;
        trace.existing_factory_count=selection.existing_count;
        trace.free_percent=selection.free_percent;
        if (selection.selected_army_id != 0U) {
            std::int64_t weight = signed_parameter(run.ai_parameters[0x08U]);
            if (selection.existing_count != 0U)
                weight /= static_cast<std::int64_t>(selection.existing_count * 2U);
            const auto random_value = random_next(run.rng_state);
            if (weight > 1) {
                const auto delta = simulation_lab_ai_candidate_weighted_delta(
                    static_cast<std::uint32_t>(weight), random_value);
                if (delta) record_candidate(selection.selected_army_id,
                                           static_cast<std::int32_t>(*delta | 1U));
            }
        }
    }
    // AiStrategicClass_SelectPressureWeightedBuilding (planning.cpp:550..626): a
    // distinct ARM321..323 family: Research lab Ion laser, Research lab Flak
    // guns, Research lab Rocket artillery; scored from pressure channels 2/3/4.
    // These channels are not the research panel's TEC categories A/B/C. Do not confuse
    // KI[0x78..7A] with candidate weights: they belong to ARM322's coefficients.
    // The selector consumes one RNG dword even when all three entries are already
    // present. The subsequent weighted workspace insertion consumes another draw.
    if (has_330 && capacity_available(run.ai_parameters[0x10U])) {
        const std::uint32_t random_bits = random_next(run.rng_state);
        const auto& channels=run.faction_ai_pressure_values[faction];
        std::array<std::array<uint32_t,3>,3> coefficients{};
        std::array<bool,3> present{},enabled{};
        for(size_t i=0;i<3;++i) {
            const auto id=321U+static_cast<uint32_t>(i);
            present[i]=run_has_asset_presence(run,faction,id);
            const auto* product=production_asset(id);
            enabled[i]=product&&(product->army_runtime_flags14_raw&1U)!=0U;
            for(size_t j=0;j<3;++j)coefficients[i][j]=run.ai_parameters[0x74U+i*4U+j];
        }
        const auto selection=retail_pressure_building({channels[2],channels[3],channels[4]},
            coefficients,random_bits,present,enabled);
        const auto existing_family_count=selection.existing;
        const auto* selected=production_asset(selection.id);
        if (selected) {
            trace.selected_lab=selected->army_asset_id;
            std::int64_t weight = signed_parameter(run.ai_parameters[0x0CU]);
            if (existing_family_count != 0U)
                weight /= static_cast<std::int64_t>(2U * existing_family_count);
            const std::uint32_t random_value = random_next(run.rng_state);
            if (weight > 1) {
                const auto delta = simulation_lab_ai_candidate_weighted_delta(
                    static_cast<std::uint32_t>(weight), random_value);
                if (delta) {
                    const std::int32_t score = static_cast<std::int32_t>(*delta | 1U);
                    record_candidate(selected->army_asset_id,score);
                }
            }
        }
    }
}

// AiConstructionPlanner_PlaceArmyAssetAtReachableCandidate (V533): workspace10
// scoring is deliberately independent of the occupancy/scratch admission
// algorithm. No ARM300 means *signed raw* world X + world Y, not a faction
// centroid or the location of an arbitrary other building.
std::int64_t simulation_lab_workspace10_candidate_score(
    Vec3f point, std::optional<Vec3f> asset300_anchor, std::uint32_t random_value) noexcept {
    std::int64_t dx = float_to_q12(point.x);
    std::int64_t dy = float_to_q12(point.y);
    if (asset300_anchor) {
        dx = std::llabs(dx - static_cast<std::int64_t>(float_to_q12(asset300_anchor->x)));
        dy = std::llabs(dy - static_cast<std::int64_t>(float_to_q12(asset300_anchor->y)));
    }
    return dx + dy + static_cast<std::int64_t>(random_value & 0xFFFFU);
}

std::optional<SimulationLabExperiment::ConstructionPlacement>
SimulationLabExperiment::construction_position(
    SimulationLabRun& run, std::uint32_t faction, std::uint32_t candidate_asset_id,
    const FieldGrid& field) {
    Vec3f anchor{};
    bool asset300_anchor = false;
    if(const auto* snapshot=lab_retained_planning_snapshot(run,faction)){
        asset300_anchor=snapshot->asset300_anchor.has_value();
        anchor=snapshot->asset300_anchor.value_or(Vec3f{});
    }else for (const auto& entity : run.entities) {
        if (entity.faction_index == faction && entity.army_asset_id == 300U &&
            entity.construction_complete && !entity.root_model_terminal) {
            anchor = entity.actor.position;
            asset300_anchor = true;
            break;
        }
    }
    // No ARM300: retail scores from raw world/cell origin, not the centroid of other buildings.

    const auto* product = production_asset(candidate_asset_id);
    const bool retail_query=run.level_runtime_enabled&&runtime_catalog_;
    const auto model=retail_query?simulation_lab_first_site_model(*runtime_catalog_,candidate_asset_id):std::nullopt;
    const auto placement_owners=retail_query?simulation_lab_site_owners(run,field,*runtime_catalog_,static_placement_owners_):std::vector<SimulationLabSiteOwner>{};
    const float candidate_radius = product
        ? std::max(0.0F, q12_to_float(std::abs(product->entity_seed.motion.collision_radius_q12)))
        : 0.0F;
    // Actual AI placement tests pass zero padding: 0x00539200 (mode 0),
    // unlike the KI[0x51]-padded mode-3 reachability query at 0x0053B570.
    // Applying that query padding to runtime collision exclusion can reject the
    // entire build radius of the Pioneer that supplies placement reachability.

    const auto too_close_to_existing = [&](Vec3f candidate) {
        if(product&&product->entity_seed.runtime_model_class_id!=0U)
            return !simulation_placement_runtime_clear(candidate,float_to_q12(candidate_radius),run.entities,field);
        for (const auto& entity : run.entities) {
            const float existing_radius = std::max(0.0F,
                q12_to_float(std::abs(entity.motion.collision_radius_q12)));
            // ArmyPlacementCandidate_TestOffsetClearanceCf performs runtime clearance before the
            // terrain callback, then validates the transformed model anchor against a 0xC00 radius.
            // The compact Lab has no live ModelRuntimeNode hull, so preserve both recovered radii as
            // a conservative center-distance admission test instead of the old fixed 1.0/1.5 rule.
            // The AI's retail caller supplies zero clearance padding.  The old editor-only
            // 0.75-unit floor came from a different factory-exit proximity test and could
            // reject otherwise valid placements near small-footprint structures.
            const float clearance = candidate_radius + existing_radius;
            const float dx = entity.actor.position.x - candidate.x;
            const float dy = entity.actor.position.y - candidate.y;
            if (dx * dx + dy * dy < clearance * clearance) return true;
            if(simulation_placement_factory_anchor_overlap(candidate,
                float_to_q12(candidate_radius),entity,field))return true;
        }
        return false;
    };

    // V533 ArmyPlacement_ValidateAssetAtPointAndCellCornersCf validates the requested point and
    // four points aligned to 0x100 Q12 with a +0x240 Q12 footprint. The terrain callback then scans
    // a 0xC00 radius (five 0x240 steps), rejects 0x88006000 blockers/water, and enforces the retail
    // placement-height band. The fixed-radius wedge footprint is two hex rings.
    constexpr std::int32_t kFootprintStepQ12 = 0x240;
    constexpr std::int32_t kTerrainRadiusQ12 = 0xC00;
    constexpr std::int32_t kTerrainHeightBandQ12 = 0x400;
    // ArmyPlacement_ValidateAssetAtPointAndCellCorners is an ANY-of-five test, not an
    // ALL-of-five footprint rule. It attempts the supplied point first, followed by the
    // four corners snapped to the lower 0x100-Q12 boundary. The first successful
    // CanPlaceAssetAtFieldPoint supplies the actual returned world position.
    // Critically, an extractor needs the deposit bit only UNDER that successful
    // point (ArmyPlacement_CanPlaceResourceExtractor), not at all five alternatives.
    const auto valid_probe = [&](std::int32_t world_x_q12, std::int32_t world_y_q12,
                                 std::int32_t reference_height_q12) {
        const auto mapped = retail_fog_world_to_cell(field, world_x_q12, world_y_q12);
        if (!mapped) return false;
        const auto& cell = field.at(mapped->x, mapped->y);
        if ((cell.flags_and_material & 0x88006000U) != 0U || cell.water_delta_q12 > 0)
            return false;
        const std::int64_t delta = static_cast<std::int64_t>(cell.terrain_height_q12) -
                                   reference_height_q12;
        return delta >= -kTerrainHeightBandQ12 && delta <= kTerrainHeightBandQ12;
    };
    const auto valid_retail_placement = [&](Vec3f requested,
                                             std::optional<std::uint32_t> required_support,
                                             std::uint32_t placement_mode) -> std::optional<ConstructionPlacement> {
        if(retail_query){
            if(!model)return std::nullopt;
            const MovementPoint point{float_to_q12(requested.x),float_to_q12(requested.y)};
            const auto cell=retail_nearest_world_to_cell(field,point.x,point.y);if(!cell)return std::nullopt;
            const auto heading=retail_triangle0_normal_azimuth_turn16(field,cell->x,cell->y).value_or(0U);
            // AI calls CanPlaceAssetAtFieldPoint directly. The five-point snapped
            // fallback belongs to cursor placement and must not move an AI site.
            if(!simulation_lab_query_site_placement(run,field,placement_owners,*model,point,faction,placement_mode,0,heading).accepted)
                return std::nullopt;
            return ConstructionPlacement{
                Vec3f{requested.x,requested.y,
                      q12_to_float(retail_placement_height_q12(field,point,model->contact_kind))},
                heading};
        }
        const std::int32_t original_x = float_to_q12(requested.x);
        const std::int32_t original_y = float_to_q12(requested.y);
        const std::int32_t base_x = original_x & ~0xFF;
        const std::int32_t base_y = original_y & ~0xFF;
        constexpr std::array<std::pair<std::int32_t, std::int32_t>, 4> corners{{
            {0, 0}, {kFootprintStepQ12, 0},
            {kFootprintStepQ12, kFootprintStepQ12}, {0, kFootprintStepQ12}}};
        const std::int64_t candidate_extent_q12 = product
            ? static_cast<std::int64_t>(product->entity_seed.runtime_class_specific_1a8_raw) : 0;
        const auto can_place_one = [&](std::int32_t x_q12,
                                       std::int32_t y_q12) -> std::optional<ConstructionPlacement> {
            const auto center = retail_fog_world_to_cell(field, x_q12, y_q12);
            if (!center) return std::nullopt;
            // Model class-14 dispatch checks the deposit at this ONE world-to-cell
            // position after the common terrain/build-area test succeeds.
            // The class-14 resource-bit check in ArmyPlacement_CanPlaceResourceExtractor
            // rounds world coordinates to the nearest FLD cell after common terrain
            // placement. `center` above is the distinct triangular lookup.
            if (required_support) {
                if (!pioneer_class14_resource_cell_eligible(field,x_q12,y_q12,
                                                          *required_support))
                    return std::nullopt;
            }
            Vec3f point{q12_to_float(x_q12), q12_to_float(y_q12),
                        q12_to_float(field.at(center->x, center->y).terrain_height_q12)};
            // Mode 0 admits eligible class-18 Pioneer reach; mode 1 excludes it.
            bool reachable_from_build_area = false;
            for (const auto& source : run.entities) {
                if (source.faction_index != faction || source.root_model_terminal ||
                    source.runtime_class_specific_19c_raw == 0) continue;
                if (source.runtime_model_class_id == 18U &&
                    (((placement_mode & 1U) != 0U) || (source.runtime_flags & 0x18U) != 0U))
                    continue;
                const std::int64_t radius_q12 =
                    static_cast<std::int64_t>(source.runtime_class_specific_19c_raw) +
                    candidate_extent_q12;
                if (radius_q12 <= 0) continue;
                const std::int64_t dx = static_cast<std::int64_t>(x_q12) -
                    float_to_q12(source.actor.position.x);
                const std::int64_t dy = static_cast<std::int64_t>(y_q12) -
                    float_to_q12(source.actor.position.y);
                if (dx * dx + dy * dy <= radius_q12 * radius_q12) {
                    reachable_from_build_area = true;
                    break;
                }
            }
            if (!reachable_from_build_area || too_close_to_existing(point)) return std::nullopt;
            const std::int32_t reference_height_q12 =
                field.at(center->x, center->y).terrain_height_q12;
            if(product&&product->entity_seed.runtime_model_class_id!=0U){
                if(!simulation_placement_terrain_clear(field,point,reference_height_q12,
                    static_cast<uint32_t>(std::abs(product->entity_seed.motion.collision_radius_q12)),
                    product->entity_seed.motion.placement_contact_kind))return std::nullopt;
            }else{
            if (!valid_probe(x_q12, y_q12, reference_height_q12)) return std::nullopt;
            // The compact terrain scan retains the recovered two-hex-ring height
            // and blocker test; it is applied to EACH attempted point individually.
            static_assert(kTerrainRadiusQ12 / kFootprintStepQ12 == 5);
            constexpr int kRadiusSteps = 2;
            const int cx = static_cast<int>(center->x);
            const int cy = static_cast<int>(center->y);
            for (int dy = -kRadiusSteps; dy <= kRadiusSteps; ++dy) {
                for (int dx = -kRadiusSteps; dx <= kRadiusSteps; ++dx) {
                    if (std::max({std::abs(dx), std::abs(dy), std::abs(dx + dy)}) > kRadiusSteps)
                        continue;
                    const int gx = cx + dx;
                    const int gy = cy + dy;
                    if (gx < 0 || gy < 0 || gx >= static_cast<int>(field.width()) ||
                        gy >= static_cast<int>(field.height())) return std::nullopt;
                    const auto& cell = field.at(static_cast<std::uint32_t>(gx),
                                                static_cast<std::uint32_t>(gy));
                    if ((cell.flags_and_material & 0x88006000U) != 0U || cell.water_delta_q12 > 0)
                        return std::nullopt;
                    const std::int64_t delta =
                        static_cast<std::int64_t>(cell.terrain_height_q12) - reference_height_q12;
                    if (delta < -kTerrainHeightBandQ12 || delta > kTerrainHeightBandQ12)
                        return std::nullopt;
                }
            }
            } // Fixed legacy footprint only for metadata-free API fixtures.
            if(product&&product->entity_seed.runtime_model_class_id==13U) {
                const auto yaw=retail_triangle0_normal_azimuth_turn16(field,center->x,center->y).value_or(0U);
                if(!simulation_placement_factory_exit_clear(field,point,yaw,product->entity_seed,run.entities))
                    return std::nullopt;
            }
            return ConstructionPlacement{
                point,
                retail_triangle0_normal_azimuth_turn16(field,center->x,center->y).value_or(0U)};
        };
        if (auto placed = can_place_one(original_x, original_y)) return placed;
        for (const auto& [offset_x, offset_y] : corners)
            if (auto placed = can_place_one(base_x + offset_x, base_y + offset_y))
                return placed;
        return std::nullopt;
    };

    // Rebuild faction-local workspaces 05/06/08/09/10 before selecting the site.
    // Resource sites use workspace08 priority/replacement and explicit mode-0
    // placement admission; no connected-island reservation or centroid heuristic.
    const auto workspaces=lab_rebuild_placement_workspaces(run,faction,field);
    const std::optional<std::uint32_t> occupancy_class = candidate_asset_id == 0x14AU
        ? std::optional<std::uint32_t>{0U}
        : (candidate_asset_id == 0x14CU ? std::optional<std::uint32_t>{1U} : std::nullopt);
    if (occupancy_class) {
        for (const auto& site:workspaces.resource08) {
            if (site.army_asset_id!=candidate_asset_id) continue;
            const auto accepted=valid_retail_placement(site.point,occupancy_class,0U);
            if (accepted) return accepted; // First usable workspace08 record wins.
        }
        return std::nullopt;
    }

    // GZF AiPlanning_RebuildFactionWorkspaces walks the FLD in scan order and feeds eligible
    // cells into workspace09/workspace10 until their 0x400/0x100 capacities are reached. Do not
    // reorder that population around a faction centroid: generic placement has exactly one
    // positional anchor in retail, g_AiWorkspaceOwnedAsset300Runtime. The compact Lab still lacks
    // the full GridScratch state-mask filter, but it preserves the proven scan order and applies the
    // recovered terrain/footprint validation before admitting each approximation candidate.
    // The retail workspace09/10 buffers are populated in original FLD order;
    // capacity, membership and validation timing are separate from scoring.
    const auto& workspace09=workspaces.base09;
    const auto& workspace10=workspaces.large10;

    if(candidate_asset_id==333U && run.level_runtime_enabled && runtime_catalog_) {
        const auto storage=simulation_lab_first_site_model(*runtime_catalog_,333U);
        const auto owners=simulation_lab_site_owners(run,field,*runtime_catalog_,static_placement_owners_);
        if(storage)for(uint32_t source:{330U,332U})for(const auto& site:workspaces.resource08) {
            if(site.army_asset_id!=source)continue;
            const auto resource=simulation_lab_first_site_model(*runtime_catalog_,source);if(!resource)continue;
            ++run.metrics.special_site_reservation_tests;
            const auto reserved=simulation_lab_reserve_special_site(run,field,owners,*resource,*storage,
                {float_to_q12(site.point.x),float_to_q12(site.point.y)},workspace09,faction,&run.army_pool,&run.native_world_objects,runtime_catalog_.get());
            run.metrics.special_site_probe_owners+=reserved.probes_created;
            if(!reserved.accepted)continue;
            ++run.metrics.special_site_reservation_accepts;
            std::vector<MovementPoint> anchors;anchors.reserve(workspace09.size());
            for(const auto p:workspace09)anchors.push_back({float_to_q12(p.x),float_to_q12(p.y)});
            const auto nearest=pioneer_nearest_mode1_anchor(MovementPoint{float_to_q12(site.point.x),float_to_q12(site.point.y)},anchors,
                [&](MovementPoint point){return simulation_lab_query_site_placement(run,field,owners,*storage,point,faction,1U,0).accepted;});
            if(nearest) return ConstructionPlacement{
                Vec3f{q12_to_float(nearest->x),q12_to_float(nearest->y),
                      field.sample_terrain(q12_to_float(nearest->x),q12_to_float(nearest->y)).height},
                0U};
        }
        // Native TryPlaceStorageAtResourceSites falls back to the generic reachable-candidate planner.
    }
    if (candidate_asset_id == 0x14DU && !workspace09.empty() &&
        !(run.level_runtime_enabled && runtime_catalog_)) {
        for (const std::uint32_t source_id : {0x14AU, 0x14CU}) {
            const auto source = std::find_if(run.entities.begin(), run.entities.end(),
                [&](const SimulationLabEntity& entity) {
                    return entity.faction_index == faction && entity.army_asset_id == source_id;
                });
            if (source == run.entities.end()) continue;
            ConstructionPlacement best{};
            float best_distance = std::numeric_limits<float>::max();
            for (const auto& point : workspace09) {
                const float distance = std::abs(point.x - source->actor.position.x) +
                                       std::abs(point.y - source->actor.position.y);
                if (distance >= best_distance) continue;
                const auto accepted=valid_retail_placement(point,std::nullopt,1U);
                if (accepted) { best_distance=distance; best=*accepted; }
            }
            if (best_distance < std::numeric_limits<float>::max()) {
                // AiConstructionPlanner_TryPlaceStorageAtResourceSite creates
                // this ARM333 special-site result with heading zero. Carry the
                // creation heading selected by the planner instead of
                // recomputing a terrain-normal heading at commit time.
                best.heading_turn16=0U;
                return best;
            }
        }
    }

    if(retail_query&&candidate_asset_id>=340U){
        if(!model)return std::nullopt;
        const auto extended=simulation_lab_extended_placement(run,field,placement_owners,
            lab_ai_owner_workspaces_v718(run,field,faction),*model,workspace09,faction);
        if(!extended)return std::nullopt;
        const auto cell=retail_nearest_world_to_cell(
            field,float_to_q12(extended->x),float_to_q12(extended->y));
        return ConstructionPlacement{*extended,cell?
            retail_triangle0_normal_azimuth_turn16(field,cell->x,cell->y).value_or(0U):0U};
    }
    if (workspace10.empty()||(retail_query&&run.ai_primary_anchor_cooldown[faction]!=0U)) return std::nullopt;
    ConstructionPlacement best{};
    std::int64_t best_score = retail_query?INT32_MAX:std::numeric_limits<std::int64_t>::max();
    std::shared_ptr<SimulationLabRetailScratchImage> placement_scratch;
    const std::optional<Vec3f> scoring_anchor = asset300_anchor
        ? std::optional<Vec3f>{anchor} : std::nullopt;
    for (const auto& point : workspace10) {
        // Candidate *admission* comes from the retained FLD/scratch workspace.
        // Scoring uses only the assigned ARM300 model's translation, or raw
        // signed world X+Y when ARM300 is absent. Consume one RNG draw per
        // considered candidate in workspace insertion order.
        const auto raw_score=simulation_lab_workspace10_candidate_score(
            point,scoring_anchor,random_next(run.rng_state));
        const std::int64_t score=retail_query?std::bit_cast<int32_t>(uint32_t(raw_score)):raw_score;
        if(score>=best_score)continue;
        const auto accepted=valid_retail_placement(point,std::nullopt,1U);
        if(!accepted)continue;
        if(retail_query){
            if(!model||!run.retail_scratch)continue;
            if(!placement_scratch)placement_scratch=std::make_shared<SimulationLabRetailScratchImage>(*run.retail_scratch);
            ++run.metrics.placement_connectivity_tests;
            const bool split=retail_placement_region_split(*placement_scratch,uint32_t(model->radius_q12),
                {float_to_q12(point.x),float_to_q12(point.y)});
            // Retail edits primary scratch itself; retain the native side effects
            // across candidates and the next planner, with immutable publication.
            run.retail_scratch=placement_scratch;
            if(split){++run.metrics.placement_connectivity_rejections;continue;}
        }
        best_score=score;best=*accepted;
    }
    if(best_score==(retail_query?INT32_MAX:std::numeric_limits<std::int64_t>::max()))return std::nullopt;
    return best;
}

SimulationLabEntity SimulationLabExperiment::make_entity_from_seed(
    const SimulationLabSeedEntity& seed, std::uint32_t faction, Vec3f position, bool synthetic,
    const FactionTechnologyMask256& mask) {
    SimulationLabEntity entity;
    entity.source_placement_index = synthetic ? std::numeric_limits<std::size_t>::max()
                                              : seed.source_placement_index;
    entity.army_asset_id = seed.army_asset_id;
    entity.faction_index = faction;
    entity.runtime_model_class_id = seed.runtime_model_class_id;
    entity.destroyed_replacement_army_asset_id = seed.destroyed_replacement_army_asset_id;
    entity.building = seed.building;
    entity.synthetic = synthetic;
    entity.construction_complete = true;
    entity.has_traversal_profile = seed.has_traversal_profile;
    entity.traversal_low_band_index = seed.traversal_low_band_index;
    entity.traversal_high_band_index = seed.traversal_high_band_index;
    entity.motion = seed.motion;
    entity.water_damage_multiplier = seed.water_damage_multiplier;
    entity.water_damage_threshold_q12 = seed.water_damage_threshold_q12;
    entity.vertical_deployment_step_q12_per_tick = seed.vertical_deployment_step_q12_per_tick;
    entity.vertical_deployment_travel_limit_q12 = seed.vertical_deployment_travel_limit_q12;
    entity.class23_deployment_sound_asset_id = seed.class23_deployment_sound_asset_id;
    entity.tank_track_animation_enabled = seed.tank_track_animation_enabled;
    entity.tank_track_local_bounds_y0 = seed.tank_track_local_bounds_y0;
    entity.tank_track_texture_travel_scale = seed.tank_track_texture_travel_scale;
    entity.articulated_contact_interpolation_rate_q12 = seed.articulated_contact_interpolation_rate_q12;
    entity.articulated_contact_sound_asset_id = seed.articulated_contact_sound_asset_id;
    entity.articulated_contact_geometry_enabled = seed.articulated_contact_geometry_enabled;
    entity.articulated_contact_local_offsets = seed.articulated_contact_local_offsets;
    entity.articulated_root_height_q12 = seed.articulated_root_height_q12;
    entity.articulated_contact_lift_q12 = seed.articulated_contact_lift_q12;
    entity.articulated_contact_steering_limit_turn16 = seed.articulated_contact_steering_limit_turn16;
    entity.articulated_shared_foot_placement_offset = seed.articulated_shared_foot_placement_offset;
    entity.movement_ground_effect_interval_ticks = seed.movement_ground_effect_interval_ticks;
    entity.movement_ground_land_effect_id = seed.movement_ground_land_effect_id;
    entity.movement_ground_water_effect_id = seed.movement_ground_water_effect_id;
    entity.runtime_sound_profile = seed.runtime_sound_profile;
    entity.positioned_sound_gain_q15 = seed.positioned_sound_gain_q15;
    entity.positioned_sound_maximum_distance_q12 = seed.positioned_sound_maximum_distance_q12;
    entity.production_compatibility_mask = seed.production_compatibility_mask;
    entity.production_storage_capacity = std::min<std::uint32_t>(
        seed.production_storage_capacity, static_cast<std::uint32_t>(kSimulationClass22StoredSlotCount));
    entity.aircraft_definition=seed.aircraft_definition;
    entity.airport_platform_step_q12=seed.airport_platform_step_q12;
    entity.airport_platform_limit_q12=seed.airport_platform_limit_q12;
    entity.airport_platform_baseline_q12=seed.airport_platform_baseline_q12;
    entity.runtime_class_specific_c0_raw = seed.runtime_class_specific_c0_raw;
    entity.runtime_class_specific_c4_raw = seed.runtime_class_specific_c4_raw;
    entity.runtime_class_specific_19c_raw = seed.runtime_class_specific_19c_raw;
    entity.runtime_class_specific_1a8_raw = seed.runtime_class_specific_1a8_raw;
    entity.class18_conversion_effect_lifetime_ticks = seed.class18_conversion_effect_lifetime_ticks;
    entity.runtime_flags = seed.runtime_flags;
    entity.production_spawn_world = seed.production_spawn_world;
    entity.production_exit_world = seed.production_exit_world;
    entity.production_drive_target_world=seed.production_drive_target_world;
    entity.production_spawn_local = seed.production_spawn_local;
    entity.production_exit_local = seed.production_exit_local;
    entity.production_root_sprite_height_offset = seed.production_root_sprite_height_offset;
    entity.production_door_step_q12 = seed.production_door_step_q12;
    entity.research_technology_ids = seed.research_technology_ids;
    entity.research_model_variants = seed.research_model_variants;
    entity.combat_weapons = seed.combat_weapons;
    entity.shot_impact_class_index = seed.shot_impact_class_index;
    entity.fog_uncover_radius_q12 = seed.fog_uncover_radius_q12;
    entity.occupancy_mark_radius_units = seed.occupancy_mark_radius_units;
            entity.fog_reference_height_offset_q12 = seed.fog_reference_height_offset_q12;
    entity.actor = seed.actor;
    entity.actor.position = position;
    entity.ai_path_coordinate_q12=lab_ai_model_point_v719(position);

    entity.model_components.reserve(seed.model_components.size() + 1U);
    for (const auto& source : seed.model_components) {
        SimulationLabModelComponentState component;
        component.source_model_node_index = source.source_model_node_index;
        component.model_definition_id = source.model_definition_id;
        component.definition_flags68 = source.definition_flags68;
        component.runtime_class_id = source.runtime_class_id;
                // Class 3 shares the EFF fields with foot contacts but suppresses
                // timed emission with the native class initializer's sentinel.
                if(component.runtime_class_id==3U)component.timed_effect_countdown=0x7FFFFFFFU;
        component.placement_contact_kind = source.placement_contact_kind;
        component.maximum_integrity = std::max<std::int32_t>(0, source.maximum_integrity);
                component.health_regeneration_per_step = source.health_regeneration_per_step;
                component.health_regeneration_delay_ticks = source.health_regeneration_delay_ticks;
        component.integrity = component.maximum_integrity;
        component.model_rotation_angle0 = source.model_rotation_angle0;
        component.resource_support_class = source.resource_support_class;
        component.resource_extraction_weight_u8 = source.resource_extraction_weight_u8;
        component.extraction_model_world_offset_at_seed = source.extraction_model_world_offset_at_seed;
        component.extraction_model_seed_yaw_turn16 = source.extraction_model_seed_yaw_turn16;
        component.resource_storage_selector = source.resource_storage_selector;
        component.resource_storage_capacity_contribution_q4 =
            source.resource_storage_capacity_contribution_q4;
        component.has_resource_storage_capacity_contribution =
            source.has_resource_storage_capacity_contribution;
        component.retail_energy_demand_q4 = source.retail_energy_demand_q4;
        component.energy_generation_capacity_q4 = source.energy_generation_capacity_q4;
        component.terminal_attachment_effect_ids = source.terminal_attachment_effect_ids;
        component.terminal_attachment_countdowns = source.terminal_attachment_delay_ticks;
        component.parent_component_index = source.parent_component_index;
        entity.model_components.push_back(component);
    }
    if (entity.model_components.empty()) {
        SimulationLabModelComponentState root;
        root.runtime_class_id = seed.runtime_model_class_id;
                if(root.runtime_class_id==3U)root.timed_effect_countdown=0x7FFFFFFFU;
        root.maximum_integrity = std::max<std::int32_t>(0, seed.root_model_maximum_integrity);
        root.integrity = root.maximum_integrity;
        entity.model_components.push_back(root);
    }
    for (std::size_t i = 0; i < entity.model_components.size(); ++i) {
        auto& component = entity.model_components[i];
        if (component.parent_component_index &&
            (*component.parent_component_index >= entity.model_components.size() ||
             *component.parent_component_index == i))
            component.parent_component_index.reset();
        if (!component.parent_component_index && entity.root_model_component_index == kSimulationLabNoComponent)
            entity.root_model_component_index = i;
    }
    if (entity.root_model_component_index == kSimulationLabNoComponent)
        entity.root_model_component_index = 0U;
    entity.collision_model_component_index = seed.collision_model_component_index < entity.model_components.size()
        ? seed.collision_model_component_index : entity.root_model_component_index;
    sync_root_component_summary(entity);
    if(runtime_catalog_)simulation_lab_initialize_runtime_entity(entity,seed,mask,*runtime_catalog_,true);
    return entity;
}

const SimulationLabProductionAsset* SimulationLabExperiment::production_asset(
    std::uint32_t army_asset_id) const noexcept {
    const auto found=production_asset_indices_.find(army_asset_id);
    return found==production_asset_indices_.end()?nullptr:&production_assets_[found->second];
}

bool SimulationLabExperiment::producer_accepts(
    const SimulationLabEntity& producer, const SimulationLabProductionAsset& candidate) const noexcept {
    return ai_v601_producer_accepts(producer.runtime_model_class_id,
        producer.production_compatibility_mask, candidate.army_runtime_flags14_raw);
}

bool SimulationLabExperiment::producer_available_for_purchase(
    const SimulationLabEntity& producer, const SimulationLabProductionAsset& candidate) const noexcept {
    // V533 AiPurchaseCandidate_HasEligibleProducerCf (0x00537630) runs before
    // AiPurchaseCandidate_ApplyToFaction appends an ARM to the faction secondary queue. The live
    // class-11/13/22 producer must be usable, idle, and accept the ARM.
    // The caller supplies membership from its own queue or captured workspace.
    if (producer.runtime_removed || !producer.construction_complete ||
        producer.root_model_terminal || producer.production_building ||
        (producer.runtime_model_class_id == 13U && producer.production_behavior_state != 0U) ||
        (research_source_flags(producer) & 0xC9U) != 0U) return false;
    return producer_accepts(producer, candidate);
}

void SimulationLabExperiment::start_production_jobs(SimulationLabRun& run,std::optional<std::size_t> owner) {
    if (production_assets_.empty()) return;
    for (std::size_t visit=0;visit<(owner?1U:run.entities.size());++visit) {
        const auto producer_index=owner.value_or(runtime_owner_index(run,visit));
        auto& producer = run.entities[producer_index];
        if (producer.faction_index == 0U || producer.faction_index >= run.faction_economy.size()) continue;

        for (std::size_t queue_index = 0; queue_index < run.secondary_purchase_requests.size(); ++queue_index) {
            const auto& request = run.secondary_purchase_requests[queue_index];
            if (request.faction_index != producer.faction_index) continue;
            const auto* candidate = production_asset(request.army_asset_id);
            if (candidate == nullptr ||
                !producer_available_for_purchase(producer, *candidate)) continue;
            // Purchase eligibility has no pad-capacity gate. The class-22
            // production callback itself compares its retained completed count.
            if (producer.runtime_model_class_id == 22U &&
                producer.production_linked_child_count >= std::min<uint32_t>(
                    producer.production_storage_capacity,kSimulationClass22StoredSlotCount))continue;

            auto& economy = run.faction_economy[producer.faction_index];
            if (candidate->xenite_cost_q4 > economy.xenite_current_q4) continue;

            // Retail spends ARM +0x28 exactly here, when a producer accepts the secondary-queue
            // entry. Merely enqueueing the purchase never changes faction Xenite.
            economy.xenite_current_q4 -= candidate->xenite_cost_q4;
            producer.production_building = true;
            if (producer.runtime_model_class_id == 13U) producer.production_behavior_state = 1U;
            producer.production_asset_id = candidate->army_asset_id;
            producer.production_start_tick = run.tick;
            producer.production_elapsed_ticks = 0U;
            producer.production_duration_ticks = config_.fast_build_cheat ? (candidate->build_duration_ticks>>4U)+1U : candidate->build_duration_ticks;
            producer.production_active_energy_demand_q4 = candidate->energy_demand_q4;
            run.faction_active_production_energy_q4[producer.faction_index] += candidate->energy_demand_q4;
            run.secondary_purchase_requests.erase(
                run.secondary_purchase_requests.begin() + static_cast<std::ptrdiff_t>(queue_index));
            ++run.metrics.secondary_purchase_requests_consumed;
            ++run.metrics.production_jobs_started;
            break;
        }
    }
}

namespace {

bool pioneer_conversion_overlap(const SimulationLabEntity& group_a,
                                const SimulationLabEntity& candidate) noexcept {
    // V533 0x00527648..0x00527691: signed Q12 XY circle test against
    // (candidate MDL +0x19C + Group-A MDL +0x1A8)^2. The executable evaluates X first and
    // then Y against the remaining 64-bit squared radius; the mathematically equivalent form
    // below preserves the same inclusive boundary without floating-point distance rounding.
    const std::int64_t radius = static_cast<std::int64_t>(candidate.runtime_class_specific_19c_raw) +
                                static_cast<std::int64_t>(group_a.runtime_class_specific_1a8_raw);
    const std::int64_t dx = static_cast<std::int64_t>(float_to_q12(group_a.actor.position.x)) -
                            static_cast<std::int64_t>(float_to_q12(candidate.actor.position.x));
    const std::int64_t dy = static_cast<std::int64_t>(float_to_q12(group_a.actor.position.y)) -
                            static_cast<std::int64_t>(float_to_q12(candidate.actor.position.y));
    return dx * dx + dy * dy <= radius * radius;
}

void erase_simulation_lab_entity_and_repair_indices(SimulationLabRun& run,
                                                     std::size_t erased_index) {
    if (erased_index >= run.entities.size()) return;
    const auto repair_optional = [erased_index](std::optional<std::size_t>& index) {
        if (!index) return;
        if (*index == erased_index) index.reset();
        else if (*index > erased_index) --*index;
    };
    simulation_lab_clear_released_target(run,erased_index);
    auto& erased=run.entities[erased_index];
    simulation_lab_release_army_slot(run,erased);
    for(std::size_t i=0;i<erased.model_components.size();++i)release_model_smoke_slot(run,erased,i);
    run.entities.erase(run.entities.begin() + static_cast<std::ptrdiff_t>(erased_index));
    for (auto& entity : run.entities) {
        repair_optional(entity.target_entity_index);
        repair_optional(entity.combat_assigned_target_entity_index);
        repair_optional(entity.linked_entity_index);
        repair_optional(entity.production_parent_entity_index);
        // Critical: the class-13 factory retains an index to its spawned child.
        // Erasing ANY preceding entity shifts that index as well. Leaving it
        // stale can freeze WAITING_EXIT or issue orders to an unrelated unit.
        repair_optional(entity.production_linked_child_entity_index);
    }
    for(auto& slot:run.army_pool.slots)repair_optional(slot.entity_index);
    for (auto& projectile : run.projectiles) {
        if (projectile.owner_placement_index == erased_index) {
            projectile.owner_placement_index = std::numeric_limits<std::size_t>::max();
            if(projectile.owner_army_pool_slot==SIZE_MAX)projectile.impact_damage_applied = true;
        } else if (projectile.owner_placement_index > erased_index &&
                   projectile.owner_placement_index != std::numeric_limits<std::size_t>::max()) {
            --projectile.owner_placement_index;
        }
        repair_optional(projectile.live_target_placement_index);
        repair_optional(projectile.impact_target_placement_index);
    }
}

} // namespace

void SimulationLabExperiment::dispatch_group_a_class_command(
    SimulationLabRun& run, std::size_t owner_index) {
    // V533 ArmyRuntime_DispatchClassCommand at 0x0051D4D0 indexes the 24-entry class-command table.
    // Its direct callers are the retail AI/local placement commit paths. Group-A membership is also
    // used elsewhere as a function-pointer identity tag, but those comparisons do NOT execute
    // ArmyRuntime_ClassCommandHandlerGroupACf. Dispatch this exactly once for the newly committed
    // Group-A runtime instead of rescanning every pre-existing Group-A object every simulation tick.
    if (owner_index >= run.entities.size()) return;
    const auto& owner = run.entities[owner_index];
    if (!owner.construction_complete || owner.root_model_terminal || owner.faction_index == 0U ||
        !runtime_class_uses_group_a_command_handler(owner.runtime_model_class_id))
        return;

    // A placement command damages every scenery/wreck model overlapping any
    // runtime attachment of the new structure, including its factory exit.
    // These owners must run their terminal lifecycle so their grid influence
    // and army slots disappear at the same time as the native owners.
    if(run.level_runtime_enabled && run.runtime_field && runtime_catalog_) {
        const auto& field=*run.runtime_field;
        const auto source_poses=simulation_lab_current_model_poses(owner,field,*runtime_catalog_);
        struct Footprint { Vec3f center; int32_t radius; };
        std::vector<Footprint> footprints;
        for(const auto& component:owner.model_components) {
            if(component.runtime_removed||component.source_model_node_index>=source_poses.size())continue;
            const auto definition=runtime_catalog_->definitions.find(component.model_definition_id);
            if(definition==runtime_catalog_->definitions.end())continue;
            footprints.push_back({source_poses[component.source_model_node_index].position,
                definition->second.simulation.attachment_proximity_radius_q12});
        }
        std::optional<Vec3f> exit;
        if(owner.runtime_model_class_id==13U && owner.production_exit_local)
            exit=simulation_lab_factory_world_point(field,owner.actor.position,owner.actor.body_yaw_turn16,
                owner.motion.placement_contact_kind,owner.production_root_sprite_height_offset,
                *owner.production_exit_local,owner.motion.placement_height_offset_q12);
        std::vector<size_t> candidates;
        for(size_t index=0;index<run.entities.size();++index)
            if(index!=owner_index&&simulation_lab_has_live_root_model_owner(run.entities[index]))
                candidates.push_back(index);
        std::sort(candidates.begin(),candidates.end(),[&](size_t a,size_t b){
            return simulation_lab_owner_order(run.entities[a],a)>simulation_lab_owner_order(run.entities[b],b);
        });
        for(const auto index:candidates) {
            auto& candidate=run.entities[index];
            const auto candidate_poses=simulation_lab_current_model_poses(candidate,field,*runtime_catalog_);
            for(size_t ci=0;ci<candidate.model_components.size();++ci) {
                const auto& component=candidate.model_components[ci];
                if(component.runtime_removed||component.terminal_runtime_flag8||
                   (component.runtime_class_id!=0U&&component.runtime_class_id!=12U)||
                   component.source_model_node_index>=candidate_poses.size())continue;
                const auto definition=runtime_catalog_->definitions.find(component.model_definition_id);
                if(definition==runtime_catalog_->definitions.end())continue;
                const auto radius=definition->second.simulation.attachment_proximity_radius_q12;
                const auto position=candidate_poses[component.source_model_node_index].position;
                const auto distance=[&](Vec3f center){
                    const int64_t dx=int64_t(float_to_q12(position.x))-float_to_q12(center.x);
                    const int64_t dy=int64_t(float_to_q12(position.y))-float_to_q12(center.y);
                    return uint64_t(dx*dx+dy*dy);
                };
                bool hit=exit&&retail_uint64_sqrt(distance(*exit))<=uint32_t(radius)+0xC00U;
                for(const auto& footprint:footprints) {
                    const int64_t reach=int64_t(radius)+footprint.radius;
                    hit|=distance(footprint.center)<=uint64_t(reach*reach);
                }
                if(!hit)continue;
                const auto angle=fixed_atan2_angle16_compat(
                    float_to_q12(position.y)-float_to_q12(owner.actor.position.y),
                    float_to_q12(position.x)-float_to_q12(owner.actor.position.x));
                apply_component_damage(run,candidate,ci,0x100000,angle);
            }
        }
    }

    if(run.runtime_field && owner.root_model_component_index<owner.model_components.size()) {
        const auto flags=owner.model_components[owner.root_model_component_index].definition_flags68;
        if(flags && (*flags&0x20U)==0U)
            run.runtime_normals_dirty|=retail_stamp_structure_height(*run.runtime_field,
                float_to_q12(owner.actor.position.x),float_to_q12(owner.actor.position.y),
                float_to_q12(owner.actor.position.z),
                static_cast<uint32_t>(std::max(0,owner.motion.collision_radius_q12)));
    }
    // V533 ArmyRuntime_ClassCommandHandlerGroupACf 0x00527600..0x00527726. The selected owner scans
    // same-faction nearby runtimes. Any in-range non-eligible runtime increments a blocker count;
    // eligible class-18 runtimes cancel their own increment and the last one wins.
    std::size_t blocker_count = 0U;
    std::optional<std::size_t> selected_pioneer;
    for (std::size_t candidate_index = 0; candidate_index < run.entities.size(); ++candidate_index) {
        if (candidate_index == owner_index) continue;
        const auto& candidate = run.entities[candidate_index];
        if ((run.level_runtime_enabled ? !simulation_lab_has_live_root_model_owner(candidate)
                                       : !candidate.construction_complete || candidate.root_model_terminal) ||
            candidate.faction_index != owner.faction_index ||
            candidate.runtime_class_specific_19c_raw == 0 ||
            !pioneer_conversion_overlap(owner, candidate))
            continue;

        ++blocker_count;
        if (candidate.runtime_model_class_id == 18U && (candidate.runtime_flags & 0x18U) == 0U &&
            !candidate.root_model_terminal && !scenario_entity_disabled(candidate)) {
            --blocker_count;
            // The last eligible owner in native's newest-first list wins.
            if(!selected_pioneer||simulation_lab_owner_order(candidate,candidate_index)<
               simulation_lab_owner_order(run.entities[*selected_pioneer],*selected_pioneer))
                selected_pioneer = candidate_index;
        }
    }
    if (blocker_count != 0U || !selected_pioneer || *selected_pioneer >= run.entities.size()) return;

    auto& pioneer = run.entities[*selected_pioneer];
    pioneer.runtime_flags |= 0x08U;
    pioneer.actor.move_target.reset();
    pioneer.actor.move_waypoint.reset();
    pioneer.navigation_target_cell_valid = false;
    pioneer.navigation_direct_route_valid = false;

    // The retail handler sets bit 0x08 before the EFF lookup. A missing effect therefore leaves
    // the Pioneer held but alive. A zero pre-resolved lifetime means the editor could not build
    // a valid worker-side completion schedule and is treated the same way.
    if (pioneer.runtime_class_specific_c4_raw == 0U ||
        pioneer.class18_conversion_effect_lifetime_ticks == 0U)
        return;

    pioneer.class18_conversion_effect_start_tick = run.tick;
    pioneer.class18_conversion_effect_remaining_ticks = pioneer.class18_conversion_effect_lifetime_ticks;
    pioneer.class18_conversion_effect_complete_tick =
        run.tick + (pioneer.class18_conversion_effect_remaining_ticks + config_.runtime_callback_step_ticks - 1U) /
            config_.runtime_callback_step_ticks;
    SimulationLabOneShotEvent event;
    event.tick = run.tick;
    event.source_placement_index = pioneer.source_placement_index;
    event.army_asset_id = pioneer.army_asset_id;
    event.faction_index = pioneer.faction_index;
    event.kind = SimulationLabOneShotEventKind::PioneerConversionEffect;
    event.effect_id = pioneer.runtime_class_specific_c4_raw;
    event.root_world_position = pioneer.actor.position;
    event.actor = pioneer.actor;
    if(run.level_runtime_enabled&&runtime_catalog_&&runtime_catalog_->effects.contains(event.effect_id))
        pioneer.class18_conversion_world_handle=link_environmental_effect(run,event.effect_id,lab_root_effect_pose(pioneer),true);
    append_one_shot_event(run, std::move(event));
    ++run.metrics.pioneer_conversion_effects_started;
}

void SimulationLabExperiment::advance_pioneer_conversions(SimulationLabRun& run) {
    // Effect completion action 3 at V533 0x0051EC8D..0x0051ED15 creates MDL +0xC0 at the
    // Pioneer's pose, scales integrity oldCurrent*newMax/oldMax, and destroys the Pioneer only
    // after creation succeeds. Iterate by index because successful completion replaces an entity.
    for (std::size_t index = 0; index < run.entities.size();) {
        auto& pioneer = run.entities[index];
        if (pioneer.runtime_model_class_id != 18U || (pioneer.runtime_flags & 0x08U) == 0U ||
            pioneer.class18_conversion_effect_remaining_ticks == 0U) {
            ++index;
            continue;
        }
        // Effect maintenance loops g_InGameSimulationStepTicks times. Preserve
        // remaining effect updates so a later callback-batch change is honored.
        if(pioneer.class18_conversion_effect_remaining_ticks > config_.runtime_callback_step_ticks) {
            pioneer.class18_conversion_effect_remaining_ticks -= config_.runtime_callback_step_ticks;
            pioneer.class18_conversion_effect_complete_tick = run.tick +
                (pioneer.class18_conversion_effect_remaining_ticks + config_.runtime_callback_step_ticks - 1U) /
                    config_.runtime_callback_step_ticks;
            ++index;
            continue;
        }
        const auto world_scope=run.native_world_objects.enter_owner(pioneer.class18_conversion_world_handle);

        // Completion is one-shot even if replacement creation cannot be represented. Retail effect
        // action 3 does not destroy the old Pioneer on ArmyRuntime_CreateInstanceFromAssetCf failure.
        pioneer.class18_conversion_effect_complete_tick = 0U;
        pioneer.class18_conversion_effect_remaining_ticks = 0U;
        const std::uint32_t replacement_asset_id = pioneer.runtime_class_specific_c0_raw > 0
            ? static_cast<std::uint32_t>(pioneer.runtime_class_specific_c0_raw) : 0U;
        const auto* replacement_asset = production_asset(replacement_asset_id);
        if (replacement_asset == nullptr) {
            ++index;
            continue;
        }

        const auto source_placement_index = pioneer.source_placement_index;
        const auto faction_index = pioneer.faction_index;
        const auto position = pioneer.actor.position;
        const auto yaw = pioneer.actor.body_yaw_turn16;
        const std::int32_t old_integrity = pioneer.root_model_integrity;
        const std::int32_t old_maximum = pioneer.root_model_maximum_integrity;

        grant_creation_technologies(run, faction_index, replacement_asset->entity_seed);
        auto replacement = make_entity_from_seed(
            replacement_asset->entity_seed, faction_index, position, true, run.faction_technology_masks[faction_index]);
        // Keep lineage to the original LEV PlayerStart so the editor can suppress the authored
        // Pioneer geometry once the live replacement exists while retaining its fallback marker.
        replacement.source_placement_index = source_placement_index;
        replacement.actor.body_yaw_turn16 = yaw;
        replacement.construction_start_tick = run.tick;
        replacement.construction_complete_tick = run.tick;

        if (replacement.root_model_component_index < replacement.model_components.size() &&
            old_maximum != 0) {
            auto& root = replacement.model_components[replacement.root_model_component_index];
            const std::int64_t scaled = static_cast<std::int64_t>(old_integrity) *
                                        static_cast<std::int64_t>(root.maximum_integrity) /
                                        static_cast<std::int64_t>(old_maximum);
            root.integrity = static_cast<std::int32_t>(std::clamp<std::int64_t>(
                scaled, std::numeric_limits<std::int32_t>::min(),
                std::numeric_limits<std::int32_t>::max()));
            sync_root_component_summary(replacement);
        }

        // Native creates the replacement while the Pioneer still owns its model slots.
        // Retained emitter cooldowns therefore come from the next free slot.
        claim_model_smoke_slots(run,replacement);
        erase_simulation_lab_entity_and_repair_indices(run, index);
        run.entities.push_back(std::move(replacement));
        recompute_resource_storage_limits(run);
        ++run.metrics.pioneer_conversions_completed;
        // The erase moved the next pre-existing entity into this slot. Process it before advancing.
    }
}

void SimulationLabExperiment::advance_production_jobs(SimulationLabRun& run,const FieldGrid& field,std::optional<std::size_t> owner) {
    if (production_assets_.empty()) return;
    const std::size_t producers_at_tick_start = run.entities.size();
    for(std::size_t visit=0;visit<(owner?1U:producers_at_tick_start);++visit) {
        const auto producer_index=owner.value_or(run.level_runtime_enabled?producers_at_tick_start-1U-visit:visit);
        auto& producer = run.entities[producer_index];
        const auto world_scope=run.native_world_objects.enter_owner(simulation_lab_world_model_handle(producer));
        const auto flags = producer.runtime_flags | (producer.root_model_component_index < producer.model_components.size()
            ? producer.model_components[producer.root_model_component_index].runtime_flags_ec : 0U);
        if (producer.runtime_removed) continue;
        if(producer.runtime_model_class_id==13U&&
           producer.production_spawn_local&&producer.production_exit_local) {
            const auto world_point=[&](const Vec3f& local) {
                return simulation_lab_factory_world_point(field,producer.actor.position,
                    producer.actor.body_yaw_turn16,producer.motion.placement_contact_kind,
                    producer.production_root_sprite_height_offset,local,producer.motion.placement_height_offset_q12,
                    !producer.runtime_model_node_height_offsets_q12.empty());
            };
            producer.production_spawn_world=world_point(*producer.production_spawn_local);
            producer.production_exit_world=world_point(*producer.production_exit_local);
            if(!producer.production_drive_target_world)
                producer.production_drive_target_world=producer.production_exit_world;
        }
        // The class-13 timer is followed by opening, waiting-for-exit and closing.  Do not
        // permit another secondary request during that interval.  The original uses the
        // definition's +0x0C movementSpeed to animate the 0x80000 V-offset door.
        if (producer.runtime_model_class_id == 13U &&
            producer.production_behavior_state >= 2U &&
            producer.production_behavior_state <= 4U) {
            const auto raw_speed = static_cast<std::int64_t>(producer.production_door_step_q12>0
                ?producer.production_door_step_q12:producer.motion.speed_q12_per_tick);
            // An unresolved/missing factory animation speed is not a reason to freeze all AI
            // production forever. This bounded 0x1000 fallback is editor-only, not retail data.
            const std::uint32_t door_step = raw_speed > 0 ?
                static_cast<std::uint32_t>(std::min<std::int64_t>(raw_speed*std::max(1U,config_.runtime_callback_step_ticks), kFactoryDoorOpenV)) :
                static_cast<uint32_t>(std::min<uint64_t>(uint64_t(0x1000U)*std::max(1U,config_.runtime_callback_step_ticks),kFactoryDoorOpenV));
            if (producer.production_behavior_state == 2U) {
                producer.production_door_texture_offset_v = std::min<std::uint32_t>(
                    kFactoryDoorOpenV, producer.production_door_texture_offset_v + door_step);
                if (producer.production_door_texture_offset_v == kFactoryDoorOpenV) {
                    producer.production_behavior_state = 3U;
                    if (producer.production_linked_child_entity_index &&
                        *producer.production_linked_child_entity_index < run.entities.size()) {
                        const auto child_index=*producer.production_linked_child_entity_index;
                        const auto& child=run.entities[child_index];
                        const auto exit=producer.production_exit_world.value_or(child.actor.position);
                        const auto rally=producer.production_drive_target_world.value_or(exit);
                        const MovementPoint exit_point{float_to_q12(exit.x),float_to_q12(exit.y)};
                        const MovementPoint rally_point{float_to_q12(rally.x),float_to_q12(rally.y)};
                        simulation_lab_set_factory_exit_route(run,field,child_index,producer_index,
                            exit_point,rally_point,static_navigation_blockers_,
                            lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get()));
                    }
                }
            } else if (producer.production_behavior_state == 3U) {
                const auto child_index = producer.production_linked_child_entity_index;
                if (!child_index || *child_index >= run.entities.size() ||
                    !run.entities[*child_index].production_linked_to_parent) {
                    producer.production_behavior_state = 4U;
                    producer.production_linked_child_entity_index.reset();
                }
            } else {
                producer.production_door_texture_offset_v =
                    producer.production_door_texture_offset_v > door_step ?
                    producer.production_door_texture_offset_v - door_step : 0U;
                if (producer.production_door_texture_offset_v == 0U)
                    producer.production_behavior_state = 0U;
            }
            continue;
        }
        // OPENING, WAITING_EXIT and CLOSING still service the built child's
        // exit while the factory's destroyed model remains in the owner list.
        if (producer.root_model_terminal || !producer.production_building || (flags & 9U)) continue;
        // Production pose advances with serviced build ticks and persists after completion.
        producer.production_animated_subnode_ticks += std::max(1U,config_.runtime_callback_step_ticks);
        producer.production_elapsed_ticks += std::max(1U,config_.runtime_callback_step_ticks);
        if (producer.production_elapsed_ticks < producer.production_duration_ticks) continue;

        const auto* product = production_asset(producer.production_asset_id);
        const std::uint32_t faction = producer.faction_index;
        if (faction < run.faction_active_production_energy_q4.size())
            run.faction_active_production_energy_q4[faction] -= producer.production_active_energy_demand_q4;
        producer.production_active_energy_demand_q4 = 0;
        producer.production_building = false;
        // Native class11/13/22 completion retains the elapsed/required counters.
        // They reset only when the next job is accepted.
        if(!run.level_runtime_enabled) {
            producer.production_elapsed_ticks = 0U;
            producer.production_duration_ticks = 0U;
        }
        const std::uint32_t completed_asset_id = producer.production_asset_id;
        producer.production_asset_id = 0U;
        ++run.metrics.production_jobs_completed;
        if (product == nullptr) continue;

        if (producer.runtime_model_class_id == 11U) {
            const auto primary_count = static_cast<std::size_t>(std::count_if(
                run.pending_asset_requests.begin(), run.pending_asset_requests.end(),
                [&](const SimulationLabPendingAssetRequest& request) { return request.faction_index == faction; }));
            if (primary_count < kFactionArmyAssetQueueCapacity) {
                run.pending_asset_requests.push_back({completed_asset_id, faction, run.tick, 0U});
                ++run.metrics.pending_requests_created;
                ++run.metrics.class11_primary_completions;
            } else {
                ++run.metrics.primary_queue_capacity_blocks;
            }
            continue;
        }

        if (producer.runtime_model_class_id == 13U) {
            Vec3f spawn = producer.production_spawn_world.value_or(producer.actor.position);
            Vec3f exit = producer.production_exit_world.value_or(spawn);
            grant_creation_technologies(run, faction, product->entity_seed);
            auto child = make_entity_from_seed(product->entity_seed, faction, spawn, true,run.faction_technology_masks[faction]);
            // Native creation receives XY and runs the child's contact callback;
            // it does not inherit the factory's transformed spawn-point height.
            if(runtime_catalog_)simulation_lab_place_root_on_surface(child,field,*runtime_catalog_);
            child.construction_start_tick = run.tick;
            child.construction_complete_tick = run.tick;
            child.production_parent_entity_index = producer_index;
            child.production_linked_to_parent = true;
            // The retail door remains closed until OPENING reaches 0x80000. Only then
            // does the spawned army receive the move command to the model point (1,5).
            child.actor.move_target.reset();
            producer.production_linked_child_entity_index = run.entities.size();
            producer.production_behavior_state = 2U;
            producer.production_door_texture_offset_v = 0U;
            // Native creation retains the raw angle. A stationary ground callback
            // compares it with the recoil result before advancing its emitters.
            const auto spawn_heading = fixed_atan2_angle16_compat(
                float_to_q12(exit.y)-float_to_q12(spawn.y),
                float_to_q12(exit.x)-float_to_q12(spawn.x));
            child.actor.body_yaw_turn16 = run.level_runtime_enabled ? spawn_heading : spawn_heading & 0xFFFFU;
            if (child.runtime_model_class_id == 17U)
                simulation_initialize_class17_deployment(child.actor, true,
                    product->entity_seed.class17_root_child_deployment_enabled);
            if(runtime_catalog_)simulation_lab_prepare_missing_model_geometry(child,field,*runtime_catalog_);
            if(run.level_runtime_enabled){
                // Native writes MIRROR_TARGET|LOCKED at creation. MIRROR_TARGET
                // survives a factory release before its door issues an exit.
                simulation_lab_sync_follow(child);
                child.combat_movement.flags|=0x402U;
            }
            claim_model_smoke_slots(run,child);
            run.entities.push_back(std::move(child));
            // Native class-14/15 initialization registers storage immediately,
            // including products created during this owner's callback.
            recompute_resource_storage_limits(run);
            if (faction < run.faction_score.size()) ++run.faction_score[faction].relation_counter_a;
            ++run.metrics.class13_units_spawned;
            ++run.metrics.completed_builds;
            continue;
        }

        if (producer.runtime_model_class_id == 22U) {
            const auto capacity=std::min<uint32_t>(producer.production_storage_capacity,kSimulationClass22StoredSlotCount);
            if(capacity) {
                size_t slot=0;
                for(uint32_t i=capacity;i>0;--i)if(producer.production_linked_child_asset_ids[i-1]==0){slot=i-1;break;}
                producer.production_linked_child_asset_ids[slot]=completed_asset_id;
                ++producer.production_linked_child_count;
                if(producer.airport_initialized) {
                    producer.airport.stored_army_asset_ids=producer.production_linked_child_asset_ids;
                    producer.airport.stored_slot_count=producer.production_linked_child_count;
                    ++producer.airport.available_stored_count;
                }
                if(faction<run.faction_score.size())++run.faction_score[faction].relation_counter_a;
                ++run.metrics.class22_assets_stored;++run.metrics.completed_builds;
            }
        }
    }
}

void SimulationLabExperiment::initialize_retail_starting_primary_queue(SimulationLabRun& run) {
    // Editor experiment override only. Retail primary inventory lives in faction runtime/save state,
    // not in the LEV faction block. An enabled override therefore replaces initialization for that
    // faction in Simulation Lab without pretending the map format owns these records. An enabled
    // zero-count override is meaningful: it explicitly starts the Lab faction with no primary stock.
    std::array<bool, kSimulationLabFactionCount> primary_queue_overridden{};
    for (std::uint32_t faction = 1U; faction < primary_queue_overridden.size(); ++faction) {
        const auto& override = config_.primary_queue_overrides[faction];
        if (!override.enabled) continue;
        primary_queue_overridden[faction] = true;
        const std::size_t count = std::min<std::size_t>(override.count, override.army_asset_ids.size());
        for (std::size_t index = 0; index < count; ++index) {
            const std::uint32_t army_asset_id = override.army_asset_ids[index];
            // A retail primary slot is an ARM pointer, so refuse editor typos/unresolved ids instead
            // of inserting a request that can never be materialized by the worker.
            if (production_asset(army_asset_id) == nullptr) continue;
            run.pending_asset_requests.push_back({army_asset_id, faction, 0U, 0U});
            ++run.metrics.lab_primary_queue_override_assets;
        }
        ++run.metrics.lab_primary_queue_override_factions;
    }

    if (production_assets_.empty()) return;

    // V533 0x00531DAF..0x00531F05 in InGameLevelRuntime_LoadResourcesAfterDefaultResetCf.
    // The executable scans g_ArmyAssetRecordRegistry[0..0x2ff] in slot order, requires ARM +0x14
    // bit 0, and retains the last matching class 11, class 16, class-14/+0xC0!=0, and
    // class-14/+0xC0==0 definitions. Registry id is the slot index, so selecting the greatest id
    // reproduces that last-match behavior without depending on catalog vector ordering.
    const SimulationLabProductionAsset* class11 = nullptr;
    const SimulationLabProductionAsset* class14_nonzero_c0 = nullptr;
    const SimulationLabProductionAsset* class14_zero_c0 = nullptr;
    const SimulationLabProductionAsset* class16 = nullptr;
    const auto later_registry_slot = [](const SimulationLabProductionAsset* current,
                                        const SimulationLabProductionAsset& candidate) {
        return current == nullptr || current->army_asset_id < candidate.army_asset_id;
    };
    for (const auto& candidate : production_assets_) {
        if ((candidate.army_runtime_flags14_raw & 1U) == 0U) continue;
        switch (candidate.entity_seed.runtime_model_class_id) {
            case 11U:
                if (later_registry_slot(class11, candidate)) class11 = &candidate;
                break;
            case 14U:
                if (candidate.runtime_class_specific_c0_raw == 0) {
                    if (later_registry_slot(class14_zero_c0, candidate)) class14_zero_c0 = &candidate;
                } else if (later_registry_slot(class14_nonzero_c0, candidate)) {
                    class14_nonzero_c0 = &candidate;
                }
                break;
            case 16U:
                if (later_registry_slot(class16, candidate)) class16 = &candidate;
                break;
            default:
                break;
        }
    }

    // Direct V533 instructions 0x00531E36/0x00531E40 gate stack locals -0x8 and -0xC:
    // class 11 and class 14 with MDL +0xC0 == 0. 0x00531EDD..0x00531EF5 then writes the four
    // primary entries in this exact order: class11, class14/C0==0, class14/C0!=0, class16.
    // Retail assumes the stock registry also supplies the latter two definitions (their stack locals are
    // not initialized before the scan). The editor refuses this startup branch if that catalog invariant
    // is broken rather than manufacturing values that do not exist in the executable.
    if (class11 == nullptr || class14_zero_c0 == nullptr ||
        class14_nonzero_c0 == nullptr || class16 == nullptr) return;
    const std::array<std::uint32_t, 4> initial_asset_ids{
        class11->army_asset_id, class14_zero_c0->army_asset_id,
        class14_nonzero_c0->army_asset_id, class16->army_asset_id};

    for (std::uint32_t faction = 1U; faction < run.faction_economy.size(); ++faction) {
        if (primary_queue_overridden[faction]) continue;
        bool has_class18 = false;
        bool has_group_a = false;
        for (const auto& entity : run.entities) {
            if (entity.faction_index != faction) continue;
            if (entity.runtime_model_class_id == 18U) has_class18 = true;
            else if (runtime_class_uses_group_a_command_handler(entity.runtime_model_class_id))
                has_group_a = true;
        }
        // Machine code accumulates bit 2 for class 18 and bit 1 for Group-A, then compares the
        // resulting mask to exactly 2. Thus class 18 + any Group-A object does not receive this queue.
        if (!has_class18 || has_group_a) continue;
        for (const auto army_asset_id : initial_asset_ids)
            run.pending_asset_requests.push_back({army_asset_id, faction, 0U, 0U});
        run.retail_initial_primary_queue_faction_mask |= (1U << faction);
        ++run.metrics.retail_initial_primary_queue_factions;
        run.metrics.retail_initial_primary_queue_assets +=
            static_cast<std::uint32_t>(initial_asset_ids.size());
    }
}

const SimulationLabTechnologyAsset* SimulationLabExperiment::technology_asset(
    std::uint32_t technology_id) const noexcept {
    const auto found=technology_asset_indices_.find(technology_id);
    return found==technology_asset_indices_.end()?nullptr:&technology_assets_[found->second];
}

bool SimulationLabExperiment::technology_available(
    const SimulationLabRun& run, std::uint32_t faction, std::uint32_t technology_id,bool include_pending) const noexcept {
    if (faction >= run.faction_technology_masks.size() || technology_id >= 256U) return false;
    const auto* technology = technology_asset(technology_id);
    if (technology == nullptr) return false;
    const auto* snapshot = include_pending ? lab_retained_planning_snapshot(run, faction) : nullptr;
    const auto& mask = snapshot ? snapshot->technology_admission_mask : run.faction_technology_masks[faction];
    if (faction_technology_unlocked(mask, technology_id)) return false;
    for (std::size_t word = 0; word < technology->prerequisite_masks.size(); ++word)
        if ((technology->prerequisite_masks[word] & mask[word]) != technology->prerequisite_masks[word])
            return false;
    // AI availability scans only admitted workspace00 runtime entries. Raw model
    // flags participate too; entries beyond its 128-slot cap cannot block a job.
    if (include_pending && run.level_runtime_enabled) {
        const auto workspace=simulation_lab_ai_own_workspaces(run,faction);
        return std::none_of(workspace.structures.begin(),workspace.structures.end(),[&](const auto& entry) {
            if (!entry.entity_index) return false;
            const auto& entity=run.entities[*entry.entity_index];
            return (research_source_flags(entity)&0xC0U)!=0U &&
                entity.research_technology_id==technology_id;
        });
    }
    // AI technology candidate availability differs from the general Technology_IsAvailable
    // predicate: both RUNNING (0x40) and ASSIGNED/PENDING (0x80) block duplicate AI jobs.
    return std::none_of(run.entities.begin(), run.entities.end(), [&](const SimulationLabEntity& entity) {
        if(entity.runtime_removed || entity.faction_index!=faction ||
           (entity.root_model_component_index<entity.model_components.size() &&
            entity.model_components[entity.root_model_component_index].runtime_removed))return false;
        const auto flags=research_source_flags(entity);
        return ((flags&0x40U)!=0U || (include_pending && entity.army_asset_id>=300U &&
               (flags&0x80U)!=0U)) && entity.research_technology_id==technology_id;
    });
}

std::optional<std::uint32_t> SimulationLabExperiment::select_research_candidate(
    SimulationLabRun& run, std::uint32_t faction) const {
    if (technology_assets_.empty() || faction >= run.faction_technology_masks.size())
        return std::nullopt;
    // AiTechnologyCandidate_AddBestResearch: no research without ARM330.
    auto& trace=run.ai_research_trace[faction];
    trace.last_rebuild_tick=run.tick;
    trace.source_count=trace.admitted_candidates=trace.positive_candidates=0U;
    trace.candidate_count=0U;
    trace.compatibility_index_entries_visited=trace.potential_seed_resolutions=0U;
    trace.potential_score_evaluations=trace.potential_score_cache_hits=0U;
    trace.best_technology_id=trace.best_score=0U;
    trace.candidate_cap_reached=false;
    trace.arms_factory_present=run_has_asset_presence(run,faction,330U);
    if(!trace.arms_factory_present) return std::nullopt;

    const auto own_workspace=simulation_lab_ai_own_workspaces(run,faction);
    const auto* snapshot=lab_retained_planning_snapshot(run,faction);
    const auto& panel_mask=snapshot?snapshot->technology_admission_mask:run.faction_technology_masks[faction];
    std::uint32_t category_mask = 0U;
    for (const auto& technology : technology_assets_) {
        if (!faction_technology_unlocked(run.faction_technology_masks[faction], technology.technology_id))
            continue;
        if (technology.category == 2U) category_mask |= 2U;
        if (technology.category == 3U) category_mask |= 4U;
    }
    // The C++20 retail oracle searches MDL requiredTechnologyBit (+0x1C0)
    // rather than MDL runtimeClassId, despite the historical function name.
    // Resolve that at catalog load, then score with signed/unsigned Q8 integer
    // math here. Direct API callers without prepared model metadata retain
    // the previous base-score compatibility fallback, explicitly approximate.
    const auto first_compatible_model_score = [&](std::uint32_t technology_id,
            std::uint32_t base, SimulationLabRun::ResearchCandidateTrace& diagnostic) -> std::optional<std::uint32_t> {
        const auto* record = technology_asset(technology_id);
        if (!record || !record->compatible_model_resolved) {
            if (record && record->compatible_model_lookup_complete) { diagnostic.reason=4U; return 0U; }
            diagnostic.reason=6U; return std::nullopt;
        }
        const std::uint32_t mdl_id = record->compatible_model_definition_id;
        const std::uint32_t mdl_class = record->compatible_model_runtime_class_id;
        std::uint32_t unit_count = 0U;
        std::uint32_t relation_q8 = 256U;
        if(run.level_runtime_enabled) {
            for(const auto& entry:own_workspace.units) {
                if(!entry.entity_index)continue;
                ++unit_count;
                const auto delta=int64_t(entry.model_definition_id)-mdl_id;
                if(delta==0 || delta==1000 || delta==-1000 || delta==2000 || delta==-2000)relation_q8+=512U;
            }
        } else for (const auto& entity : run.entities) {
            if (entity.faction_index != faction || entity.army_asset_id >= 300U ||
                !entity.construction_complete || entity.root_model_terminal) continue;
            if (entity.root_model_component_index >= entity.model_components.size()) continue;
            ++unit_count;
            const auto delta = static_cast<std::int64_t>(entity.model_components[entity.root_model_component_index].model_definition_id) - mdl_id;
            if (delta == 0 || delta == 1000 || delta == -1000 || delta == 2000 || delta == -2000) relation_q8 += 512U;
        }
        if (unit_count != 0U) relation_q8 /= unit_count;
        if (mdl_class == 1U) {
            // Exact ground-family source-class exception: presence of any of
            // ARM 302/303/304 zeros the score; 305/306 takes a 3/4 factor,
            // and class 1 is then halved.
            const bool blocked=run.level_runtime_enabled
                ? std::any_of(own_workspace.structures.begin(),own_workspace.structures.end(),
                    [](const auto& e){return e.army_asset_id>=302U && e.army_asset_id<=304U;})
                : std::any_of(run.entities.begin(),run.entities.end(),[&](const auto& e){
                    return e.faction_index==faction && e.construction_complete &&
                        !e.root_model_terminal && e.army_asset_id>=302U && e.army_asset_id<=304U;});
            if(blocked) { diagnostic.reason=7U; return 0U; }
        } else if (mdl_class != 21U && mdl_class != 2U && mdl_class != 3U &&
                   mdl_class != 17U && mdl_class != 19U) {
            // Native returns the definition address for these classes. Use the
            // definition's load position to rank these candidates above normal
            // scores without depending on the editor process's addresses.
            diagnostic.reason=5U;
            return record->compatible_model_registry_position != 0U
                ? 0x40000000U + record->compatible_model_registry_position : 0U;
        }
        // Preserve the game's 32-bit intermediate dword behavior (Q8, factor
        // 40000), including wrap before the final >>8 shift.
        std::uint32_t scaled = static_cast<std::uint32_t>(relation_q8 * 40000U) >> 8U;
        scaled = static_cast<std::uint32_t>(scaled * base) >> 8U;
        if (mdl_class == 1U) {
            const bool reduced = run.level_runtime_enabled
                ? std::any_of(own_workspace.structures.begin(),own_workspace.structures.end(),
                    [](const auto& e){return e.army_asset_id==305U || e.army_asset_id==306U;})
                : std::any_of(run.entities.begin(), run.entities.end(), [&](const auto& e) {
                    return e.faction_index == faction && e.construction_complete &&
                        !e.root_model_terminal && (e.army_asset_id == 305U || e.army_asset_id == 306U);
                });
            if (reduced) scaled = static_cast<std::uint32_t>(scaled * 3U) >> 2U;
            scaled >>= 1U;
        }
        return scaled;
    };
    const AiArmyScoreNormalizersV568 research_normalizers{
        ai_flagged_definition_value0c_maximum_,ai_category_value60_maxima_};
    struct PotentialScoreCache { bool evaluated{},resolved{}; std::int32_t score{}; };
    // Scores depend on this faction's current mask and pressure. Never keep them
    // across a planning pass, research completion, pressure update or another run.
    std::vector<PotentialScoreCache> potential_scores;
    const auto category_compatible_score = [&](std::uint32_t technology_id,
            std::uint32_t base, SimulationLabRun::ResearchCandidateTrace& diagnostic) -> std::optional<std::uint32_t> {
        const auto* record = technology_asset(technology_id);
        if (!record || !record->compatible_model_resolved) {
            if (record && record->compatible_model_lookup_complete) { diagnostic.reason=4U; return 0U; }
            diagnostic.reason=6U; return std::nullopt;
        }
        const auto definition_id = record->compatible_model_definition_id;
        const auto found=research_arm_index_.find(definition_id);
        std::uint32_t sum = 0U; // Retail accumulator is a wrapping dword.
        std::uint32_t count = 0U;
        if (found==research_arm_index_.end()) { diagnostic.reason=11U; return 0U; }
        for (const auto& link:found->second) {
            ++trace.compatibility_index_entries_visited;
            const auto& asset=production_assets_[link.asset_index];
            if (!link.root && link.child && !faction_technology_unlocked(
                    run.faction_technology_masks[faction],asset.ai_root_first_variant_required_technology_bit)) {
                ++diagnostic.locked_root_child_matches;
                continue;
            }
            ++diagnostic.compatible_arm_matches;
            if (!asset.ai_faction_score_seeds_prepared ||
                faction >= asset.ai_faction_score_seeds.size()) {
                ++diagnostic.compatible_arm_missing_metadata;
                continue;
            }
            if (potential_scores.empty()) potential_scores.resize(production_assets_.size());
            auto& cached=potential_scores[link.asset_index];
            if (!cached.evaluated) {
                cached.evaluated=true;
                ++trace.potential_seed_resolutions;
                const auto seed=simulation_lab_current_ai_score_seed(
                    asset,run.faction_technology_masks[faction],faction);
                cached.resolved=seed.resolved;
                if (cached.resolved) {
                    ++trace.potential_score_evaluations;
                    cached.score=ai_army_static_score_v568(asset,seed,kAiArmyScoreVariantC,
                        research_normalizers,run.faction_ai_pressure_values[faction],
                        run.faction_maximum_ai_pressure[faction],AiArmyScorePurpose::Research);
                }
            } else ++trace.potential_score_cache_hits;
            if (!cached.resolved) {
                ++diagnostic.compatible_arm_missing_metadata;
                continue;
            }
            const auto score=cached.score;
            if (diagnostic.best_potential_arm_id == 0U || score > diagnostic.best_potential_arm_score) {
                diagnostic.best_potential_arm_id = asset.army_asset_id;
                diagnostic.best_potential_arm_score = score;
            }
            if (score <= 0) continue;
            ++diagnostic.compatible_arm_positive;
            sum += static_cast<std::uint32_t>(score);
            ++count;
        }
        if (count == 0U) { diagnostic.reason=11U; return 0U; }
        const std::uint32_t average = sum / count;
        return static_cast<std::uint32_t>(average * base) >> 8U;
    };
    std::optional<std::uint32_t> best_id;
    std::uint32_t best_score = 0U;
    std::uint32_t admitted = 0U;
    // Native collection walks the retained workspace, without an extra completed/
    // terminal filter. Null queued entries have presence but no research panel.
    std::vector<size_t> sources;
    if(run.level_runtime_enabled) {
        for(const auto& entry:own_workspace.structures)
            if(entry.entity_index) sources.push_back(*entry.entity_index);
    } else for(size_t index=0;index<run.entities.size();++index) {
        const auto& source=run.entities[index];
        if (!source.runtime_removed && source.faction_index==faction && source.construction_complete &&
            source.army_asset_id>=300U && !source.root_model_terminal) sources.push_back(index);
    }
    for (const auto index:sources) {
        const auto& source=run.entities[index];
        ++trace.source_count;
        const auto view=research_source_view(source,panel_mask);
        // Retail slots 28..1, with duplicates preserved until the global availability check.
        for (auto slot=view.ids.rbegin(); slot!=view.ids.rend(); ++slot) {
            const std::uint32_t id=*slot;
            if (admitted == 32U) break; // retail workspace12 capacity
            if (id == 11U || !technology_available(run, faction, id)) continue;
            const auto* technology = technology_asset(id);
            if (!technology) continue;
            ++admitted;
            auto& candidate_trace=trace.candidates[trace.candidate_count++];
            candidate_trace={id,source.army_asset_id,view.runtime_class_id,0U,
                technology->compatible_model_definition_id,technology->compatible_model_runtime_class_id,
                technology->category,0U};
            candidate_trace.source_entity_index=static_cast<uint32_t>(&source-run.entities.data());
            candidate_trace.panel_slot=static_cast<uint32_t>(slot.base()-view.ids.begin());
            candidate_trace.base_score=technology->ai_base_candidate_score;
            candidate_trace.xenite_cost_q4=technology->xenite_cost_q4;
            candidate_trace.energy_demand_q4=technology->energy_demand_q4;
            candidate_trace.duration_ticks=technology->research_duration_ticks;
            candidate_trace.compatible_model_resolved=technology->compatible_model_resolved;
            candidate_trace.compatible_model_lookup_complete=technology->compatible_model_lookup_complete;
            const auto base = static_cast<std::uint32_t>(technology->ai_base_candidate_score);
            std::uint32_t score = 0U;
            // SelectScoreKind order matters: special TEC IDs precede source class.
            if (id == 216U || id == 217U) {
                score = 0U;
            } else if (id == 210U || id == 211U || id == 213U || id == 214U) {
                candidate_trace.score_kind=1U;
                candidate_trace.reason=8U;
                // KI[0x18] is a Q4 Xenite minimum, not an arbitrary raw weight.
                if (static_cast<std::int32_t>(run.faction_economy[faction].xenite_current_q4) >=
                    static_cast<std::int32_t>(run.ai_parameters[0x18U])) {
                    if (id == 210U || id == 211U) {
                        score = base; candidate_trace.reason=0U;
                    } else {
                        // Retail: (supplied + unpowered) / (baseline + 16*extraction), Q8.
                        // Stock code divides by zero if supply is zero; keep the Lab safe by
                        // rejecting that candidate rather than crashing the editor worker.
                        const auto& economy = run.faction_economy[faction];
                        const std::uint32_t supply = economy.baseline_energy_supply_q4 +
                            run.faction_tritium_extraction_rate_q4_per_tick[faction] * 16U;
                        const std::uint32_t demand = run.faction_supplied_energy_demand_q4[faction] +
                            run.faction_unpowered_energy_demand_q4[faction];
                        candidate_trace.reason=9U;
                        if (supply != 0U) {
                            const auto quotient = (static_cast<std::uint64_t>(demand) << 8U) / supply;
                            candidate_trace.reason=13U; // x86 DIV would overflow EAX; contain imported input.
                            if (quotient <= UINT32_MAX) {
                                const auto ratio_q8 = static_cast<std::uint32_t>(quotient);
                                candidate_trace.reason=10U;
                                if (std::bit_cast<std::int32_t>(ratio_q8) >= 240) {
                                    score = static_cast<std::uint32_t>(ratio_q8 * base) >> 8U;
                                    candidate_trace.reason=0U;
                                }
                            }
                        }
                    }
                }
            } else if (view.runtime_class_id == 11U) {
                // Kind 2 precedes the factory and radar branches.
                candidate_trace.score_kind=2U;
                score = base;
            } else if (view.runtime_class_id == 13U ||
                       view.runtime_class_id == 22U) {
                // Kind 3 uses MDL +0x1C0 registry lookup and workspace01
                // unit-family relation scale, with the retail class-1 gates.
                // Direct API fixtures without resolved MDL data retain the
                // earlier base-score approximation.
                candidate_trace.score_kind=3U;
                score = first_compatible_model_score(id, base,candidate_trace).value_or(base);
            } else if (id == 183U || id == 184U || id == 185U ||
                       id == 189U || id == 190U || id == 191U) {
                candidate_trace.score_kind=4U;
                score = base; // Kind 4 applies only after source-class tests.
            } else {
                // Kind 5: average only positive eligible ARM scores from
                // matching root/first-two child linked-definition variants.
                // The editor still has no native workspace01 unit-entry list,
                // so synthetic fixtures without MDL metadata keep fallback.
                candidate_trace.score_kind=5U;
                if (technology->category == 2U && (category_mask & 2U) == 0U){candidate_trace.reason=2U;continue;}
                if (technology->category == 3U && (category_mask & 4U) == 0U){candidate_trace.reason=3U;continue;}
                score = category_compatible_score(id, base,candidate_trace).value_or(base);
            }
            candidate_trace.score=score;
            // Strict >: first workspace12 candidate wins a tie, not lowest TEC ID.
            const auto signed_score=std::bit_cast<std::int32_t>(score);
            if (signed_score <= 0 && candidate_trace.reason==0U)
                candidate_trace.reason=technology->ai_base_candidate_score<=0 ? 1U : 12U;
            if(signed_score>0) ++trace.positive_candidates;
            if (signed_score > static_cast<std::int32_t>(best_score)) {
                best_score = score;
                best_id = id;
            }
        }
        if (admitted == 32U) break;
    }
    trace.admitted_candidates=admitted;
    trace.candidate_cap_reached=admitted==32U;
    trace.best_technology_id=best_id.value_or(0U);
    trace.best_score=best_score;
    return best_id;
}

bool SimulationLabExperiment::apply_research_purchase(
    SimulationLabRun& run, std::uint32_t faction, std::uint32_t technology_id,
    std::span<const size_t> unavailable,std::optional<size_t>* selected_source) {
    const auto* technology = technology_asset(technology_id);
    // Candidate collection blocks assigned and running jobs; execution's general
    // Technology_IsAvailableForFaction only blocks running jobs.
    if (!technology || !technology_available(run, faction, technology_id,false)) return false;
    const auto own_workspace=simulation_lab_ai_own_workspaces(run,faction);
    auto& trace=run.ai_research_trace[faction];
    trace.last_purchase_tick=run.tick;
    trace.last_purchase_technology_id=technology_id;
    trace.last_purchase_queued=false;
    for (std::size_t visit=0;visit<run.entities.size();++visit) {
        const auto index=runtime_owner_index(run,visit);
        if(std::find(unavailable.begin(),unavailable.end(),index)!=unavailable.end())continue;
        if(run.level_runtime_enabled && !own_workspace.root_admitted(run,index))continue;
        auto& source=run.entities[index];
        if (source.runtime_removed || (!run.level_runtime_enabled && source.faction_index!=faction) ||
            source.army_asset_id < 300U || (research_source_flags(source)&0x89U)!=0U) continue;
        // Live workspace admission and the native 0x89 word are the complete
        // source gates. Completion/terminal summaries are not additional flags.
        if(!run.level_runtime_enabled && (!source.construction_complete || source.root_model_terminal))continue;
        // A diplomatic merger changes the owner but leaves this planning pass's
        // workspace entry pointing at the same live structure.
        const auto view=research_source_view(source,run.faction_technology_masks[source.faction_index]);
        if(std::find(view.ids.begin(),view.ids.end(),technology_id)==view.ids.end()) continue;
        if(selected_source)*selected_source=index;
        // Retail eligibility accepts an active researcher of another technology. Applying
        // the record then does nothing (0xC0 guard), but consumes this planning opportunity.
        if((research_source_flags(source)&0xC0U)!=0U)return true;
        set_research_job_flags(source,0x80U);
        source.research_technology_id = technology_id;
        source.research_elapsed_ticks = 0U;
        source.research_duration_ticks = config_.fast_build_cheat ? (technology->research_duration_ticks>>4U)+1U : technology->research_duration_ticks;
        source.research_xenite_due_q4 = technology->xenite_cost_q4;
        source.research_energy_demand_q4 = technology->energy_demand_q4;
        ++run.metrics.research_jobs_queued;
        trace.last_purchase_queued=true;
        return true;
    }
    return false;
}

void SimulationLabExperiment::grant_creation_technologies(
    SimulationLabRun& run, std::uint32_t faction, const SimulationLabSeedEntity& seed) {
    if(faction>=run.faction_technology_masks.size()) return;
    for(const auto& group:seed.linked_model_technologies) {
        const SimulationLabLinkedModelTechnology* selected=nullptr;
        for(const auto& variant:group.variants) {
            if(variant.variant_index==0U) selected=&variant;
            if(faction_technology_unlocked(run.faction_technology_masks[faction],
                                          variant.required_technology_id)) selected=&variant;
        }
        if(selected) unlock_technology_recursive(run,faction,selected->granted_technology_id);
    }
}

void SimulationLabExperiment::unlock_technology_recursive(
    SimulationLabRun& run, std::uint32_t faction, std::uint32_t technology_id) {
    if (faction >= run.faction_technology_masks.size() || technology_id >= 256U) return;
    auto& mask = run.faction_technology_masks[faction];
    if (faction_technology_unlocked(mask, technology_id)) return;
    set_faction_technology_unlocked(mask, technology_id, true);
    const auto* technology = technology_asset(technology_id);
    if (technology != nullptr && technology->dependency_technology_id < 256U)
        unlock_technology_recursive(run, faction, technology->dependency_technology_id);
    if(runtime_catalog_)for(auto& entity:run.entities)
        if(entity.faction_index==faction)
            simulation_lab_apply_runtime_technology(entity,mask,*runtime_catalog_,run.metrics);
}

void SimulationLabExperiment::advance_research(SimulationLabRun& run,std::optional<std::size_t> owner) {
    if (technology_assets_.empty()) return;
    for (std::size_t visit=0;visit<(owner?1U:run.entities.size());++visit) {
        auto& entity=run.entities[owner.value_or(runtime_owner_index(run,visit))];
        const auto component_scope=run.native_world_objects.enter_component(entity.root_model_component_index,-3);
        if (entity.runtime_removed || entity.faction_index >= run.faction_economy.size()) continue;
        if(entity.root_model_component_index<entity.model_components.size() &&
           entity.model_components[entity.root_model_component_index].runtime_removed)continue;
        const std::uint32_t faction = entity.faction_index;
        const auto flags=research_source_flags(entity);
        entity.research_active=(flags&0x40U)!=0U;
        entity.research_pending_payment=(flags&0x80U)!=0U;
        // Retail services active 0x40 progress before the pending-payment 0x80 branch.
        if (entity.research_active && (research_source_flags(entity)&1U)==0U) {
            if((entity.runtime_model_class_id==11U||entity.runtime_model_class_id==13U) &&
               simulation_lab_production_effect_active(entity))entity.production_animated_subnode_ticks+=std::max(1U,config_.runtime_callback_step_ticks);
            entity.research_elapsed_ticks += std::max(1U,config_.runtime_callback_step_ticks);
            if (std::bit_cast<int32_t>(entity.research_elapsed_ticks) >=
                std::bit_cast<int32_t>(entity.research_duration_ticks)) {
                run.ai_research_trace[faction].last_completed_technology_id=entity.research_technology_id;
                run.ai_research_trace[faction].last_completion_tick=run.tick;
                unlock_technology_recursive(run, faction, entity.research_technology_id);
                run.faction_active_research_energy_q4[faction] -= entity.research_energy_demand_q4;
                // The original clears only RUNNING. With both bits set it can
                // enter the payment branch again during this same callback.
                set_research_job_flags(entity,research_source_flags(entity)&~0x40U);
                ++run.metrics.research_jobs_completed;
            }
        }
        if (!entity.research_pending_payment || entity.research_active || entity.production_building ||
            (research_source_flags(entity)&0x100U)!=0U) continue;
        auto& economy = run.faction_economy[faction];
        if (std::bit_cast<int32_t>(entity.research_xenite_due_q4) > std::bit_cast<int32_t>(economy.xenite_current_q4)) continue;
        economy.xenite_current_q4 -= entity.research_xenite_due_q4;
        entity.research_xenite_due_q4 = 0U;
        set_research_job_flags(entity,0x40U);
        run.faction_active_research_energy_q4[faction] += entity.research_energy_demand_q4;
        ++run.metrics.research_jobs_started;
    }
}

bool SimulationLabExperiment::process_pending_construction(
    SimulationLabRun& run, std::uint32_t faction, const FieldGrid& field) {
    const auto* snapshot = lab_retained_planning_snapshot(run, faction);
    const auto request_limit = snapshot ? snapshot->requested_asset_count
        : simulation_lab_ai_workspace04_count(run, faction);
    std::uint32_t visited_requests = 0;
    for (std::size_t index = 0; index < run.pending_asset_requests.size(); ++index) {
        auto& request = run.pending_asset_requests[index];
        if (request.faction_index != faction) continue;
        // The native planner sees only the ordered prefix admitted to WS04.
        if(run.level_runtime_enabled&&visited_requests++==request_limit)break;
        // Native defers ARM300 while workspace00 contains an unassigned
        // Xenite extractor. This includes primary stock and in-progress builds,
        // and respects the bounded workspace rather than scanning all requests.
        if(run.level_runtime_enabled&&request.army_asset_id==300U){
            const auto own=simulation_lab_ai_own_workspaces(run,faction);
            if(std::any_of(own.structures.begin(),own.structures.end(),[](const auto& e){
                return e.army_asset_id==330U&&!e.entity_index;
            }))continue;
        }
        ++request.placement_attempts;
        const auto placement = construction_position(run, faction, request.army_asset_id, field);
        if (!placement) {
            ++run.metrics.pending_placement_retries;
            continue;
        }

        if (!production_assets_.empty()) {
            const auto* product = production_asset(request.army_asset_id);
            if (product == nullptr) {
                ++run.metrics.pending_placement_retries;
                continue;
            }
            // The retail construction planner consumes the primary queue only after actual runtime
            // creation succeeds. The Lab's creation is represented by inserting the fully initialized
            // runtime entity here; there is no second synthetic six-second construction timer.
            grant_creation_technologies(run, faction, product->entity_seed);
            auto entity = make_entity_from_seed(product->entity_seed, faction, placement->position, true,
                                                run.faction_technology_masks[faction]);
            // Carry the exact heading used by the successful planner path into
            // ArmyRuntime creation. Ordinary and extended AI construction uses
            // triangle0NormalAngles.low16; the ARM333 special-site path is the
            // recovered exception and creates with heading zero. Do not inherit
            // a catalog/template yaw or recompute after the placement decision.
            entity.actor.body_yaw_turn16 = placement->heading_turn16;
            // This object was committed by the retail primary construction planner. Preserve that
            // semantic role even for special class-14/class-16 definitions that the editor catalog
            // exposes outside its ordinary Buildings browser category.
            entity.building = true;
            entity.construction_start_tick = run.tick;
            entity.construction_complete_tick = run.tick;
            if(runtime_catalog_)simulation_lab_prepare_missing_model_geometry(entity,field,*runtime_catalog_);
            claim_model_smoke_slots(run,entity);
            run.entities.push_back(std::move(entity));
            const std::size_t placed_entity_index = run.entities.size() - 1U;
            recompute_resource_storage_limits(run);
            if(run.level_runtime_enabled)recompute_energy_generation_capacity(run);
            // Retail construction/local-placement commit immediately dispatches the new runtime's
            // class command. Pre-existing/default buildings are intentionally not replayed at Lab start.
            dispatch_group_a_class_command(run, placed_entity_index);
            const std::uint32_t placement_effect_id =
                product->placement_effect_ids_prepared && faction < product->placement_effect_ids_by_faction.size()
                    ? product->placement_effect_ids_by_faction[faction]
                    : product->placement_effect_id;
            if (placement_effect_id != 0U) {
                const auto& placed = run.entities[placed_entity_index];
                SimulationLabOneShotEvent event;
                event.tick = run.tick;
                event.source_placement_index = placed.source_placement_index;
                event.army_asset_id = placed.army_asset_id;
                event.faction_index = faction;
                event.kind = SimulationLabOneShotEventKind::ConstructionPlacementEffect;
                event.effect_id = placement_effect_id;
                event.root_world_position = placed.actor.position;
                event.actor = placed.actor;
                if(run.level_runtime_enabled&&runtime_catalog_&&runtime_catalog_->effects.contains(event.effect_id))
                    link_environmental_effect(run,event.effect_id,lab_root_effect_pose(placed),true);
                append_one_shot_event(run, std::move(event));
            }
            run.pending_asset_requests.erase(
                run.pending_asset_requests.begin() + static_cast<std::ptrdiff_t>(index));
            ++run.metrics.pending_requests_consumed;
            ++run.metrics.planned_builds;
            ++run.metrics.completed_builds;
            if (faction < run.faction_score.size()) ++run.faction_score[faction].relation_counter_b;
            return true; // retail stops after one committed placement per planner pass
        }

        // Compatibility path for direct API/tests that do not provide the V562 production catalog.
        SimulationLabEntity entity;
        entity.source_placement_index = std::numeric_limits<std::size_t>::max();
        entity.army_asset_id = request.army_asset_id;
        entity.faction_index = faction;
        entity.building = true;
        entity.synthetic = true;
        // Compatibility-only asset lacks an authored catalog record, but retail runtime creation is
        // still immediate. Do not invent a construction-progress timer or vertical scale animation.
        entity.construction_complete = true;
        entity.construction_start_tick = run.tick;
        entity.construction_complete_tick = run.tick;
        entity.actor.position = placement->position;
        entity.actor.body_yaw_turn16 = placement->heading_turn16;
        entity.ai_path_coordinate_q12=lab_ai_model_point_v719(placement->position);
        run.entities.push_back(std::move(entity));
        run.pending_asset_requests.erase(
            run.pending_asset_requests.begin() + static_cast<std::ptrdiff_t>(index));
        ++run.metrics.pending_requests_consumed;
        ++run.metrics.planned_builds;
        ++run.metrics.completed_builds;
        return true;
    }
    return false;
}

void SimulationLabExperiment::plan_construction(SimulationLabRun& run, const FieldGrid& field,
                                                std::uint32_t selected_faction) {
    std::array<bool, 8> faction_present{};
    for (const auto& entity : run.entities)
        if (entity.faction_index < faction_present.size() && entity.faction_index != 0U)
            faction_present[entity.faction_index] = true;
    for (std::uint32_t faction = 1U; faction < faction_present.size(); ++faction) {
        if (faction != selected_faction ||
            (!faction_present[faction]&&!lab_retained_planning_snapshot(run,faction))) continue;
        if (process_pending_construction(run, faction, field)) continue;
        // When a production catalog exists, strategic/resource ARM candidates
        // enter the SAME sorted, Xenite-budgeted workspace13 as units and TEC.
        // The direct-API/no-catalog planner remains separate for old fixtures.
        if (!production_assets_.empty()) continue;

        if (production_assets_.empty()) {
            const auto pending_for_faction = static_cast<std::size_t>(std::count_if(
                run.pending_asset_requests.begin(), run.pending_asset_requests.end(),
                [&](const SimulationLabPendingAssetRequest& request) { return request.faction_index == faction; }));
            if (pending_for_faction >= kPendingWorkspaceCapacity) {
                ++run.metrics.pending_capacity_blocks;
                continue;
            }
        } else {
            const auto secondary_for_faction = static_cast<std::size_t>(std::count_if(
                run.secondary_purchase_requests.begin(), run.secondary_purchase_requests.end(),
                [&](const SimulationLabPendingAssetRequest& request) { return request.faction_index == faction; }));
            if (secondary_for_faction >= kFactionArmyAssetQueueCapacity) {
                ++run.metrics.pending_capacity_blocks;
                continue;
            }
        }

        const auto candidate = choose_construction_asset(run, faction, field);
        if (!candidate) continue;
        SimulationLabPendingAssetRequest request;
        request.army_asset_id = *candidate;
        request.faction_index = faction;
        request.created_tick = run.tick;
        if (production_assets_.empty()) {
            run.pending_asset_requests.push_back(request);
            ++run.metrics.pending_requests_created;
        } else {
            // This is a purchase request only. Xenite is deliberately untouched until an eligible
            // class 11/13/22 producer accepts it in start_production_jobs().
            run.secondary_purchase_requests.push_back(request);
            ++run.metrics.secondary_purchase_requests_created;
        }
        if (const auto legacy = std::find(kConstructionAssetIds.begin(), kConstructionAssetIds.end(), *candidate);
            legacy != kConstructionAssetIds.end())
            ++run.metrics.candidate_builds[static_cast<std::size_t>(legacy - kConstructionAssetIds.begin())];
    }
}

void SimulationLabExperiment::plan_autonomous_army_purchases(
    SimulationLabRun& run, const FieldGrid& field, std::uint32_t selected_faction) {
    if (production_assets_.empty() && technology_assets_.empty()) return;

    const AiArmyScoreNormalizersV568 normalizers{
        ai_flagged_definition_value0c_maximum_,ai_category_value60_maxima_};

    struct WeightedCandidate {
        std::uint32_t id{};
        std::uint32_t kind{}; // 1: ARM, 2: TEC
        // Workspace13 compares this packed score as a signed dword.
        std::int32_t packed_score{};
    };

    for (std::uint32_t faction = 1U; faction < run.faction_economy.size(); ++faction) {
        if (faction != selected_faction) continue;
        bool faction_present = false;
        bool enemy_present = false;
        std::uint32_t live_producer_mask = 0U;
        std::uint32_t workspace01_count = 0U;
        for (const auto& entity : run.entities) {
            // Level-runtime planning consumes the owner-list workspaces, whose
            // membership does not depend on construction completion or the
            // terminal-effect summary. Legacy synthetic mode keeps its former
            // completed-entity approximation.
            if (!run.level_runtime_enabled &&
                (!entity.construction_complete || entity.root_model_terminal)) continue;
            if (entity.faction_index == faction) {
                faction_present = true;
                if (entity.army_asset_id < 300U) ++workspace01_count;
                switch (entity.runtime_model_class_id) {
                    case 11U: live_producer_mask |= 0x10U; break;
                    case 13U: live_producer_mask |= entity.production_compatibility_mask; break;
                    case 22U: live_producer_mask |= 0x08U; break;
                    default: break;
                }
            } else if (entity.faction_index != 0U) {
                // Workspace07 derives from visible-hostile workspace02. An
                // unseen foreign base must not activate the attack profile.
                if(!run.level_runtime_enabled) { enemy_present=true; continue; }
                enemy_present |= simulation_lab_ai_hostile_perception(run,field,faction,entity)==
                    SimulationLabHostilePerception::Visible;
            }
        }
        const auto own_workspace=simulation_lab_ai_own_workspaces(run,faction);
        const auto* planning_snapshot=lab_retained_planning_snapshot(run,faction);
        const auto& admission_mask=planning_snapshot
            ?planning_snapshot->technology_admission_mask:run.faction_technology_masks[faction];
        if(run.level_runtime_enabled) {
            // The native planner is dispatched by faction lifecycle, not by a
            // completed-owner census. It can replay cached candidates or plan
            // from pending workspaces while all current owners are unfinished
            // or in terminal-effect lifetime.
            const auto* snapshot=lab_retained_planning_snapshot(run,faction);
            faction_present=run.faction_lifecycle_states[faction]==kFactionLifecycleActive||snapshot;
            if(snapshot)enemy_present=!snapshot->owner_lists.targets07.empty();
            workspace01_count=static_cast<uint32_t>(own_workspace.units.size());
            live_producer_mask=0;
            for(size_t index=0;index<run.entities.size();++index) {
                if(!own_workspace.root_admitted(run,index))continue;
                const auto& e=run.entities[index];
                if(e.runtime_model_class_id==11)live_producer_mask|=0x10U;
                else if(e.runtime_model_class_id==13)live_producer_mask|=e.production_compatibility_mask;
                else if(e.runtime_model_class_id==22)live_producer_mask|=8U;
            }
        }
        if(!run.level_runtime_enabled)for (const auto& request : run.secondary_purchase_requests)
            if (request.faction_index == faction && request.army_asset_id < 300U)
                ++workspace01_count;
        // Research still runs without a usable class-11/13/22 producer.
        if (!faction_present) continue;

        const bool use_cached_candidates = run.ai_candidate_cache_reuse_state[faction] != 0U &&
            run.ai_primary_anchor_cooldown[faction] <= 49U &&
            run.ai_anchor_cooldown0[faction] <= 49U;
        auto& purchase_trace=run.ai_purchase_trace[faction];
        purchase_trace={};
        purchase_trace.tick=run.tick;
        purchase_trace.cached=use_cached_candidates;
        purchase_trace.initial_budget_q4=run.faction_economy[faction].xenite_current_q4;
        auto& army_trace = run.ai_army_profile_trace[faction];
        // The faction's technology mask does not change during this dispatch.
        // Resolve each linked MDL hierarchy once, then reuse the same score
        // seed across the three retail A/B/C candidate profiles.
        struct Workspace11Entry {
            const SimulationLabProductionAsset* asset{};
            SimulationLabAiArmyScoreSeed score{};
        };
        std::vector<Workspace11Entry> workspace11;
        if(!use_cached_candidates){
        workspace11.reserve(std::min(production_assets_.size(),
                                     kSimulationLabAiWorkspace11Capacity));
        for (const auto& asset : production_assets_) {
            if ((asset.army_runtime_flags14_raw & 1U) == 0U ||
                (asset.army_runtime_flags14_raw & live_producer_mask) == 0U ||
                (asset.army_asset_id >= 300U && asset.army_asset_id < 340U)) continue;
            if (!asset.ai_faction_score_seeds_prepared ||
                faction >= asset.ai_faction_score_seeds.size()) continue;
            auto seed = simulation_lab_current_ai_score_seed(
                asset, run.faction_technology_masks[faction], faction);
            // Native freezes workspace11 admission before diplomacy. A merge
            // can share technology during this dispatch, so scoring selects
            // current variants but membership keeps the earlier mask.
            const bool admitted_technology=admission_mask==run.faction_technology_masks[faction]
                ?seed.all_selected_model_technologies_unlocked
                :simulation_lab_current_ai_score_seed(asset,admission_mask,faction)
                    .all_selected_model_technologies_unlocked;
            if (!seed.resolved || !ai_v601_workspace11_candidate(
                asset.army_asset_id, asset.army_runtime_flags14_raw, live_producer_mask,
                admitted_technology)) continue;
            workspace11.push_back({&asset, std::move(seed)});
            if (workspace11.size() == kSimulationLabAiWorkspace11Capacity) break;
        }
        // Diagnostics only: retain precisely the retail-admitted workspace11
        // population, not merely every ARM with a matching raw producer bit.
        army_trace.last_rebuild_tick = run.tick;
        army_trace.workspace11_count = static_cast<std::uint32_t>(workspace11.size());
        army_trace.eligible_tracked = army_trace.eligible_walkers =
            army_trace.eligible_gliders = army_trace.eligible_aircraft =
            army_trace.eligible_naval = army_trace.eligible_turrets = 0U;
        for (const auto& item : workspace11) {
            switch (item.asset->entity_seed.runtime_model_class_id) {
                case 2U: ++army_trace.eligible_tracked; break;
                case 3U: ++army_trace.eligible_walkers; break;
                case 17U: ++army_trace.eligible_gliders; break;
                case 21U: ++army_trace.eligible_aircraft; break;
                case 19U: ++army_trace.eligible_naval; break;
                case 4U: ++army_trace.eligible_turrets; break;
                default: break;
            }
        }
        } // A cached purchase pass needs neither workspace11 metadata nor profile diagnostics.
        std::vector<WeightedCandidate> candidates;
        candidates.reserve(4U);
        if (use_cached_candidates) {
            ++run.metrics.ai_candidate_cache_replays;
            // LoadFromFactionImage: replay saved top-three packed entries without
            // rerunning profile selection or consuming candidate-score RNG draws.
            for (std::uint32_t i = 0U; i < run.ai_candidate_cache_count[faction] && i < 3U; ++i) {
                const auto& entry = run.ai_candidate_cache[faction][i];
                candidates.push_back({entry.id, entry.kind, entry.packed_score});
            }
        }
        const auto add_weighted = [&](std::uint32_t id, std::uint32_t kind, std::uint32_t delta) {
            simulation_lab_ai_candidate_add_or_accumulate(candidates,id,kind,delta);
        };
        if (!use_cached_candidates) {
        ++run.metrics.ai_candidate_cache_rebuilds;
        // Structural/site selection is an independently RNG-weighted retail
        // candidate family. The existing compact selector returns its best
        // candidate's packed score; replay it directly, without drawing a
        // second RNG value or issuing a separate secondary purchase.
        // Preserve all structural contributions in workspace13. Retaining only
        // their maximum hid storage/power alternatives from affordable execution.
        if (!production_assets_.empty()) {
            std::vector<SimulationLabCachedAiCandidate> structures;
            (void)choose_construction_asset(run,faction,field,nullptr,&structures,false);
            for(const auto& structure:structures)
                add_weighted(structure.id,structure.kind,static_cast<uint32_t>(structure.packed_score)&~0xFU);
        }
        const auto add_profile = [&](const AiArmyScoreWeightsV568& profile,
                                     std::int64_t planning_weight,
                                     bool enabled,
                                     bool enforce_unit_limit,
                                     std::size_t profile_index) {
            if (!enabled || workspace11.empty()) return;
            const SimulationLabProductionAsset* best = nullptr;
            std::int32_t best_score = 0;
            const SimulationLabProductionAsset* advanced = nullptr;
            std::int32_t advanced_score = 0;
            ++army_trace.profile_invocations[profile_index];
            for (const auto& entry : workspace11) {
                const auto score = ai_army_static_score_v568(
                    *entry.asset, entry.score, profile, normalizers,
                    run.faction_ai_pressure_values[faction], run.faction_maximum_ai_pressure[faction]);
                if (score > best_score) {
                    best_score = score;
                    best = entry.asset;
                }
                // Ground class1 competition versus an unlocked tracked/walker/
                // glider/aircraft family. Navals are included only if a factory
                // actually admitted them into workspace11.
                const auto cls = entry.asset->entity_seed.runtime_model_class_id;
                if ((cls == 2U || cls == 3U || cls == 17U || cls == 19U || cls == 21U) &&
                    score > advanced_score) {
                    advanced_score = score;
                    advanced = entry.asset;
                }
            }
            army_trace.best_asset_id[profile_index] = best ? best->army_asset_id : 0U;
            army_trace.best_score[profile_index] = best_score;
            army_trace.best_advanced_asset_id[profile_index] = advanced ? advanced->army_asset_id : 0U;
            army_trace.best_advanced_score[profile_index] = advanced_score;
            if (best == nullptr || best_score <= 0) return;
            if (enforce_unit_limit && best->army_asset_id < 300U && workspace01_count >= 11U)
                return;
            // Retail only calls AddOrAccumulateWeightedEntry AFTER finding a
            // positive-score candidate; no best ARM means no RNG draw at all.
            // Once called, even a zero/one weight consumes one RNG dword.
            const auto random_value = random_next(run.rng_state);
            const auto delta = simulation_lab_ai_candidate_weighted_delta(
                static_cast<std::uint32_t>(planning_weight), random_value);
            if (delta) add_weighted(best->army_asset_id, 1U, *delta);
        };

        // AiFactionRuntime_TestPlanningCapacityExceeded(4, faction): defense
        // purchases require 4 units of spare energy after currently supplied
        // and unpowered demand. Direct-API synthetic runs without a component
        // energy image retain their earlier unconstrained fallback.
        bool defense_capacity_available = true;
        if (run.faction_economy[faction].derive_energy_allocation_from_components) {
            const auto& economy = run.faction_economy[faction];
            const std::uint64_t baseline_supply = economy.baseline_energy_supply_q4 >> 4U;
            const std::uint64_t supply = baseline_supply +
                run.faction_tritium_extraction_rate_q4_per_tick[faction];
            const std::uint64_t capacity =
                (run.faction_energy_generation_capacity_q4[faction] != 0U
                    ? run.faction_energy_generation_capacity_q4[faction]
                    : economy.baseline_energy_generation_capacity_q4) >> 4U;
            const std::uint64_t demand =
                (static_cast<std::uint64_t>(run.faction_supplied_energy_demand_q4[faction]) +
                 run.faction_unpowered_energy_demand_q4[faction]) >> 4U;
            defense_capacity_available = std::min(supply, capacity) >= demand + 4U;
        }
        if (defense_capacity_available && faction < run.faction_technology_masks.size() &&
            faction_technology_unlocked(run.faction_technology_masks[faction], 1U)) {
            std::uint32_t extended_count = 1U;
            const auto* snapshot=lab_retained_planning_snapshot(run,faction);
            const auto limit=snapshot?snapshot->requested_asset_count:UINT32_MAX;
            std::uint32_t visited=0U;
            for (const auto& request : run.pending_asset_requests) {
                if (request.faction_index!=faction) continue;
                if (visited++==limit) break;
                if (request.army_asset_id>=340U&&request.army_asset_id<380U) ++extended_count;
            }
            std::int64_t weight = static_cast<std::int32_t>(run.ai_parameters[0x09U]);
            weight /= static_cast<std::int64_t>(extended_count);
            // Retail changes defense priority after an anchor attack:
            // cooldown==0 -> weight*3/4; nonzero -> weight*2.
            weight = run.ai_primary_anchor_cooldown[faction] == 0U
                ? (weight * 3 / 4) : (weight * 2);
            add_profile(kAiArmyScoreVariantA, weight, true, true, 0U);
        }

        // Native exploration requires an admitted workspace05 site, not merely a map.
        // Keep the legacy metadata-only API fallback separate from level simulation.
        const bool exploration_site_available=run.level_runtime_enabled
            ? !lab_rebuild_placement_workspaces(run,faction,field,true).general05.empty()
            : field.width()!=0U && field.height()!=0U;
        if (exploration_site_available && workspace01_count < 11U) {
            const std::uint32_t explored = std::min<std::uint32_t>(
                100U, run.faction_score[faction].explored_terrain_percent);
            const std::int64_t numerator = static_cast<std::int64_t>(100U - explored) *
                static_cast<std::int32_t>(run.ai_parameters[0x0AU]);
            const std::int64_t denominator = static_cast<std::int64_t>(workspace01_count + 1U) * 32;
            std::int64_t variant_b_weight = denominator == 0 ? 0 : numerator / denominator;
            // AiArmyCandidate_AddBestScoredVariantB halves this planning weight when workspace07
            // is non-empty. Scenario runs use represented visible hostile targets;
            // direct synthetic fixtures retain their earlier all-other-factions fallback.
            if (enemy_present) variant_b_weight /= 2;
            add_profile(kAiArmyScoreVariantB, variant_b_weight, true, false, 1U);
        }
        if (enemy_present && workspace01_count < 11U)
            add_profile(kAiArmyScoreVariantC,
                static_cast<std::int32_t>(run.ai_parameters[0x0BU]), true, false, 2U);

        // Native 0x53C810 collects strategic buildings AFTER the three unit
        // profiles. Preserve RNG consumption and tied insertion order here.
        if (!production_assets_.empty()) {
            std::vector<SimulationLabCachedAiCandidate> strategic;
            append_strategic_construction_candidates(run,faction,strategic);
            for (const auto& candidate:strategic)
                add_weighted(candidate.id,1U,static_cast<uint32_t>(candidate.packed_score)&~0xFU);
        }

        // Technology candidates occupy the SAME workspace13 and compete for
        // the same planning Xenite as army purchases. KI[0x0D] is the retail
        // selected-research candidate's weight, not its technology score.
        if (const auto research = select_research_candidate(run, faction)) {
            std::uint32_t research_weight = run.ai_parameters[0x0DU];
            if (run.ai_primary_anchor_cooldown[faction] != 0U) research_weight >>= 1U;
            const auto random_value = random_next(run.rng_state);
            if (const auto delta = simulation_lab_ai_candidate_weighted_delta(
                    research_weight, random_value))
                add_weighted(*research, 2U, *delta);
        }
        } // Newly rebuilt workspace13 candidates; cached passes do no scoring.

        if (candidates.empty()) {
            // An empty fresh workspace makes ExecuteAffordableCandidates return
            // false without saving. The previously saved faction cache survives
            // a cooldown-forced rebuild and is replayed once that cooldown falls.
            if(use_cached_candidates||!run.level_runtime_enabled)
                run.ai_candidate_cache_reuse_state[faction] = 0U;
            if(!run.level_runtime_enabled)
                run.ai_candidate_cache_count[faction] = 0U;
            continue;
        }
        // Retail workspace13 performs a strict-greater signed selection sort (ties keep the
        // previous dispatch order, not ARM ID order).
        for (std::size_t first = 0; first + 1U < candidates.size(); ++first)
            for (std::size_t other = first + 1U; other < candidates.size(); ++other)
                if (candidates[first].packed_score < candidates[other].packed_score)
                    std::swap(candidates[first], candidates[other]);

        purchase_trace.candidate_count=static_cast<std::uint32_t>(candidates.size());
        purchase_trace.recorded_count=static_cast<std::uint32_t>(std::min(
            candidates.size(),purchase_trace.candidates.size()));
        purchase_trace.truncated=candidates.size()>purchase_trace.candidates.size();
        for (std::size_t rank=0;rank<purchase_trace.recorded_count;++rank) {
            const auto& candidate=candidates[rank];
            auto& row=purchase_trace.candidates[rank];
            row.rank=static_cast<std::uint32_t>(rank);row.id=candidate.id;
            row.kind=candidate.kind;row.packed_score=candidate.packed_score;
            if (candidate.kind==1U || (run.level_runtime_enabled && candidate.kind!=2U)) {
                if (const auto* a=production_asset(candidate.id)) row.cost_q4=a->xenite_cost_q4;
                else if(run.level_runtime_enabled)row.cost_q4=INT32_MAX;
            }
            if (candidate.kind==2U) { if (const auto* t=technology_asset(candidate.id)) row.cost_q4=t->xenite_cost_q4; }
        }
        std::uint32_t planning_xenite = run.faction_economy[faction].xenite_current_q4;
        std::uint32_t applied_producer_class_mask = 0U;
        bool applied_any_candidate = false;
        std::vector<size_t> unavailable_research_sources;
        for (std::size_t rank=0;rank<candidates.size();++rank) {
            const auto& candidate=candidates[rank];
            auto* decision=rank<purchase_trace.recorded_count?&purchase_trace.candidates[rank]:nullptr;
            if (decision) decision->budget_before_q4=decision->budget_after_q4=planning_xenite;
            const auto record_decision=[&](SimulationLabRun::PurchaseDecision reason) {
                if (decision) {decision->decision=reason;decision->budget_after_q4=planning_xenite;}
            };
            const SimulationLabProductionAsset* army = nullptr;
            const SimulationLabTechnologyAsset* technology = nullptr;
            if (candidate.kind == 1U || (run.level_runtime_enabled && candidate.kind != 2U))
                army = production_asset(candidate.id);
            if (candidate.kind == 2U) technology = technology_asset(candidate.id);
            const bool missing_native_arm=run.level_runtime_enabled&&candidate.kind!=2U&&!army;
            if (!army && !technology && !missing_native_arm) {
                record_decision(SimulationLabRun::PurchaseDecision::MissingAsset);continue;
            }
            const std::uint32_t cost = army ? army->xenite_cost_q4 :
                technology ? technology->xenite_cost_q4 : INT32_MAX;
            // Retail deducts from a *local* planning image before producer lookup,
            // even if the selected candidate has no eligible producer.
            const auto budget_before=planning_xenite;
            planning_xenite-=cost; // Native subtraction occurs even on the failing candidate.
            if (cost > budget_before) {
                const auto cap = run.faction_economy[faction].xenite_storage_limit_q4;
                run.ai_purchase_block_diagnostics[faction].record(
                    run.tick, candidate.id, cost, budget_before,
                    cap.has_value(), cap.value_or(0U), use_cached_candidates);
                record_decision(SimulationLabRun::PurchaseDecision::Unaffordable);
                break; // source-required: NEVER skip ahead to an affordable entry
            }
            if (technology) {
                std::optional<size_t> selected;
                if (apply_research_purchase(run, faction, candidate.id,unavailable_research_sources,&selected)) {
                    if(selected)unavailable_research_sources.push_back(*selected);
                    record_decision(run.ai_research_trace[faction].last_purchase_queued ?
                        SimulationLabRun::PurchaseDecision::ResearchQueued : SimulationLabRun::PurchaseDecision::ResearchNoOp);
                    ++run.metrics.ai_purchase_candidates_applied;
                    applied_any_candidate = true;
                } else record_decision(SimulationLabRun::PurchaseDecision::ResearchUnavailable);
                continue;
            }
            if(!army){record_decision(SimulationLabRun::PurchaseDecision::MissingAsset);continue;}
            const std::uint32_t class_mask = army->army_runtime_flags14_raw;
            if ((class_mask & applied_producer_class_mask) != 0U) {
                record_decision(SimulationLabRun::PurchaseDecision::ProducerClassUsed);continue;
            }
            const bool producer_available = std::any_of(run.entities.begin(), run.entities.end(),
                [&](const SimulationLabEntity& producer) {
                    const auto index=static_cast<size_t>(&producer-run.entities.data());
                    return (run.level_runtime_enabled ? own_workspace.root_admitted(run,index)
                                                      : producer.faction_index==faction) &&
                        std::find(unavailable_research_sources.begin(),unavailable_research_sources.end(),index)==unavailable_research_sources.end() &&
                        producer_available_for_purchase(producer, *army);
                });
            if (!producer_available) {record_decision(SimulationLabRun::PurchaseDecision::NoProducer);continue;}
            const auto secondary_for_faction = static_cast<std::size_t>(std::count_if(
                run.secondary_purchase_requests.begin(), run.secondary_purchase_requests.end(),
                [&](const SimulationLabPendingAssetRequest& request) {
                    return request.faction_index == faction;
                }));
            if (secondary_for_faction >= kFactionArmyAssetQueueCapacity) {
                ++run.metrics.pending_capacity_blocks;
                record_decision(SimulationLabRun::PurchaseDecision::QueueFull);
                // Native registration may reject a full queue. Apply still
                // consumes this class and continues the affordable pass.
            } else {
                record_decision(SimulationLabRun::PurchaseDecision::ArmyQueued);
                run.secondary_purchase_requests.push_back({candidate.id, faction, run.tick, 0U});
                ++run.metrics.secondary_purchase_requests_created;
            }
            applied_producer_class_mask |= class_mask;
            ++run.metrics.ai_purchase_candidates_applied;
            applied_any_candidate = true;
        }
        if (use_cached_candidates) {
            // Retail resets cacheReuseState only when a cached pass succeeds
            // (ExecuteAffordableCandidates returns false). A failed replay
            // leaves the same three candidates cached until a cooldown gate
            // forces rebuilding or a later replay succeeds.
            if (applied_any_candidate || candidates.empty()) run.ai_candidate_cache_reuse_state[faction] = 0U;
        } else if (!applied_any_candidate && !candidates.empty()) {
            // SaveToFactionImage stores up to three entries from the sorted
            // workspace, and sets cacheReuseState to 16 after an unsuccessful pass.
            const auto count = std::min<std::size_t>(3U, candidates.size());
            run.ai_candidate_cache_count[faction] = static_cast<std::uint32_t>(count);
            for (std::size_t i = 0; i < count; ++i) {
                run.ai_candidate_cache[faction][i] =
                    {candidates[i].id, candidates[i].packed_score, candidates[i].kind};
            }
            run.ai_candidate_cache_reuse_state[faction] = 16U;
            ++run.metrics.ai_candidate_cache_saves;
        } else if(!run.level_runtime_enabled) {
            run.ai_candidate_cache_reuse_state[faction] = 0U;
        }
        // A successful fresh pass leaves any previously saved native cache
        // intact. Only a successful cached pass clears its reuse state.
    }
}

void SimulationLabExperiment::complete_construction(SimulationLabRun& run) {
    if (!production_assets_.empty()) return;
    for (auto& entity : run.entities) {
        if (!entity.synthetic || entity.construction_complete ||
            run.tick < entity.construction_complete_tick) continue;
        entity.construction_complete = true;
        ++run.metrics.completed_builds;
    }
}

// Native phase 3/7 terrainStateRefresh is dispatched over linked MODEL
// owners and reads the *retained field occupancy* after the corresponding
// field job, not a previously projected fog preview. Shot/effect callbacks
// remain separate work; only army model terrain classification is mirrored.
// Native shot terrainStateRefresh runs in the same phase-3/7 owner-list
// traversal. It updates the sampled mask, resolved flags and native tint;
// renderer tint application and effect-node callbacks are separate TODOs.
void lab_refresh_shot_terrain_samples(SimulationLabRun& run,const FieldGrid& field,
    std::span<const std::uint64_t> occupancy,const SimulationLabRuntimeCatalog* catalog) {
    if(!catalog || occupancy.size()!=field.cells().size())return;
    for(auto& shot:run.projectiles) {
        if(simulation_projectile_expired(shot,run.tick))continue;
        const ShotDefinitionRecord* definition=nullptr;
        if(const auto found=catalog->shots.find(shot.shot_definition_id);
           found!=catalog->shots.end()) definition=&found->second;
        else if(shot.owner_placement_index<run.entities.size()) {
            const auto& owner=run.entities[shot.owner_placement_index];
            if(shot.weapon_profile_index<owner.combat_weapons.size())
                definition=&owner.combat_weapons[shot.weapon_profile_index].shot;
        }
        if(!definition)continue;
        const bool beam=definition->trajectory_kind()==ShotTrajectoryMode::DirectLine;
        Vec3f origin=beam?shot.launch_position:
            simulation_projectile_position(shot,*definition,run.tick);
        Vec3f end=origin;
        bool has_length=false;
        if(beam) {
            // Lab's nearest-ray contact is authoritative after a successful
            // recast; otherwise use the same authored beam stretch cap as the
            // renderer. Do not extend a beam past its actual contact point.
            if(shot.impact_started && shot.impact_kind!=SimulationImpactKind::None)
                end=shot.impact_position;
            else {
                const Vec3f delta{shot.target_position.x-origin.x,
                    shot.target_position.y-origin.y,shot.target_position.z-origin.z};
                const float distance=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
                if(distance>0.0F){
                    const float scale=simulation_mode0_beam_length_world(shot,*definition)/distance;
                    end={origin.x+delta.x*scale,origin.y+delta.y*scale,origin.z+delta.z*scale};
                }
            }
            has_length=(end.x!=origin.x||end.y!=origin.y||end.z!=origin.z);
        }
        shot.terrain_neighborhood_mask=simulation_lab_shot_neighborhood_mask(
            field,occupancy,origin,beam,end,has_length);
        const auto terrain=simulation_lab_resolve_shot_terrain_state(
            shot.terrain_neighborhood_mask,run.active_view_faction_index,
            definition->state_tint_argb);
        shot.terrain_runtime_class_mask=terrain.resolved_mask;
        shot.terrain_runtime_flags=terrain.runtime_flags;
        shot.terrain_tint_argb=terrain.tint_argb;
        shot.terrain_refresh_tick=run.tick;
    }
}

// Only linked, live environmental effect slots participate in the effect
// terrain callback. Immutable one-shot visual journals are deliberately not
// treated as WorldRuntime owner nodes.
void lab_refresh_linked_effect_terrain_states(SimulationLabRun& run,
    const FieldGrid& field,std::span<const std::uint64_t> occupancy,
    const SimulationLabRuntimeCatalog* catalog) {
    if(!catalog || occupancy.size()!=field.cells().size())return;
    for(auto& effect:run.environmental_effects) {
        if(effect.world_pool_only)continue;
        const auto it=catalog->effects.find(effect.effect_id);
        if(it==catalog->effects.end())continue;
        const auto age=run.tick-effect.spawn_tick;
        const auto& definition=it->second.definition;
        if(age>=effect_lifetime_ticks(definition))continue;
        effect.terrain_neighborhood_mask=retail_model_neighborhood_mask(
            field,occupancy,float_to_q12(effect.pose.position.x),
            float_to_q12(effect.pose.position.y),0x1000);
        const auto state=simulation_lab_resolve_effect_terrain_state(
            effect.terrain_neighborhood_mask,run.active_view_faction_index,
            definition.state_tint_argb,effect_alpha_at_age(definition,age));
        effect.terrain_runtime_class_mask=state.resolved_mask;
        effect.terrain_runtime_flags=state.runtime_flags;
        effect.terrain_tint_argb=state.tint_argb;
        effect.terrain_refresh_tick=run.tick;
    }
}

void lab_refresh_army_model_terrain_states(SimulationLabRun& run,
    const FieldGrid& field, std::span<const std::uint64_t> occupancy,bool initialize_only=false) {
    if (occupancy.size()!=field.cells().size()) return;
    for (auto& entity:run.entities) {
        if (!simulation_lab_terrain_refresh_root_linked(entity)||
            (initialize_only&&entity.model_terrain_state_initialized)) continue;
        const auto primary=retail_model_neighborhood_mask(field,occupancy,
            float_to_q12(entity.actor.position.x),float_to_q12(entity.actor.position.y),
            entity.motion.collision_radius_q12);
        bool not_remembered=false;
        if (entity.root_model_component_index<entity.model_components.size())
            not_remembered=(entity.model_components[entity.root_model_component_index]
                .definition_flags68.value_or(0U)&0x10U)!=0U;
        const auto resolved=retail_resolve_model_occupancy(
            primary,entity.model_occupancy_history,not_remembered);
        entity.model_primary_occupancy_mask=resolved.primary;
        entity.model_terrain_state_initialized=true;
        entity.model_occupancy_history=resolved.history;
    }
}

void SimulationLabExperiment::update_exploration_and_points(
    SimulationLabRun& run, const FieldGrid& field) {
    const std::size_t cell_count = static_cast<std::size_t>(field.width()) * field.height();
    const bool native_stats=run.level_runtime_enabled&&runtime_catalog_;
    if (cell_count != 0U && (run.tick == 1U || (run.tick & 7U) == 7U)) {
        std::vector<FogPreviewObserver> observers;
        observers.reserve(run.entities.size());
        for (const auto& entity : run.entities) {
            if (!simulation_lab_phase7_occlusion_owner_eligible(run,entity) ||
                entity.faction_index >= run.faction_score.size() ||
                simulation_lab_phase7_occlusion_preview_radius(run,entity)==0U) continue;
            const auto marker = retail_visibility_marker_for_model_class(entity.runtime_model_class_id);
            observers.push_back({float_to_q12(entity.actor.position.x),
                                 float_to_q12(entity.actor.position.y),
                                 static_cast<std::int32_t>(std::clamp<std::int64_t>(
                                     static_cast<std::int64_t>(float_to_q12(entity.actor.position.z))+
                                         entity.fog_reference_height_offset_q12,
                                     std::numeric_limits<std::int32_t>::min(),
                                     std::numeric_limits<std::int32_t>::max())),
                                 simulation_lab_phase7_occlusion_preview_radius(run,entity),
                                 run.level_runtime_enabled
                                     ? retail_faction_visibility_mask(
                                           run.faction_packed_relation_states[entity.faction_index], marker)
                                     : retail_self_faction_visibility_mask(entity.faction_index, marker)});
        }
        run.current_visibility_occupancy = build_projected_fog_preview_occupancy(field, observers);
        run.current_visibility_occupancy_tick = run.tick;
        const auto& occupancy = run.current_visibility_occupancy;
        const std::size_t word_count = (cell_count + 63U) / 64U;
        for (std::uint32_t faction = 1U; faction < run.faction_explored_cell_bits.size(); ++faction) {
            auto& bits = run.faction_explored_cell_bits[faction];
            if (bits.size() != word_count) bits.assign(word_count, 0U);
            for (std::size_t cell = 0; cell < occupancy.size(); ++cell) {
                if (!retail_fog_explored(fog_occupancy_byte(occupancy[cell], faction))) continue;
                bits[cell >> 6U] |= std::uint64_t{1} << (cell & 63U);
            }
            std::uint64_t explored = 0U;
            for (const auto word : bits) explored += static_cast<std::uint64_t>(std::popcount(word));
            if(!native_stats)
                run.faction_score[faction].explored_terrain_percent = static_cast<std::uint32_t>(
                    explored * 100U / std::max<std::size_t>(cell_count, 1U));
        }
    }
    // The purchase planner consumes the last 128-tick statistics sample,
    // not the continually refreshed fog preview.
    if(native_stats && (run.tick&127U)==0U && (run.tick>>7U)<4096U &&
       cell_count!=0U && run.retail_scratch && run.retail_scratch->field_occupancy_masks.size()==cell_count){
        for(uint32_t faction=1;faction<run.faction_score.size();++faction){
            size_t explored=0;
            for(auto mask:run.retail_scratch->field_occupancy_masks)
                explored+=retail_fog_explored(fog_occupancy_byte(mask,faction));
            run.faction_score[faction].explored_terrain_percent=static_cast<uint32_t>(explored*100U/cell_count);
        }
    }
    if(run.level_runtime_enabled && (run.tick==1U||(run.tick&7U)==3U)) {
        // Native phase 3 refreshes owner terrain class from the field image
        // retained by phase 7, not the old projected preview buffer. The
        // first-tick initialization is allowed to use the startup preview.
        const auto terrain_image=simulation_lab_terrain_refresh_source(
            run,cell_count,(run.tick&7U)==3U);
        lab_refresh_army_model_terrain_states(run,field,terrain_image);
        lab_refresh_shot_terrain_samples(run,field,terrain_image,runtime_catalog_.get());
        lab_refresh_linked_effect_terrain_states(run,field,terrain_image,runtime_catalog_.get());
    }
    for (std::size_t faction = 0; faction < run.faction_score.size(); ++faction) {
        // Keep the compatibility arrays and the exact score state on one source of truth.
        run.faction_score[faction].relation_counter_c =
            static_cast<std::int32_t>(run.faction_relation_counter_c[faction]);
        run.faction_score[faction].relation_counter_d =
            static_cast<std::int32_t>(run.faction_relation_counter_d[faction]);
        simulation_lab_recompute_points(run.faction_score[faction], run.faction_technology_masks[faction]);
    }
}


void SimulationLabExperiment::update_scheduled_conditions(SimulationLabRun& run,
                                                           const FieldGrid& field,uint32_t callback_step_ticks) {
    // Retail promotes ENDING_PENDING to ENDED_OR_TRANSITIONED at the beginning of the next
    // 20-tick condition pass, before reevaluating the 64 records.
    for (std::uint32_t faction = 1U; faction < run.faction_lifecycle_states.size(); ++faction)
        if (run.faction_lifecycle_states[faction] == kFactionLifecycleEndingPending)
            run.faction_lifecycle_states[faction] = kFactionLifecycleEndedOrTransitioned;

    std::optional<std::vector<std::uint64_t>> occupancy;
    const auto current_occupancy = [&]() -> const std::vector<std::uint64_t>& {
        // Native conditions read the live persisted FLD occupancy, including
        // previously seen cells, rather than regenerating a fog projection.
        if(run.retail_scratch && run.retail_scratch->field_occupancy_masks.size()==field.cells().size())
            return run.retail_scratch->field_occupancy_masks;
        if (!occupancy) {
            std::vector<FogPreviewObserver> observers;
            observers.reserve(run.entities.size());
            for (const auto& entity : run.entities) {
                if (!simulation_lab_phase7_occlusion_owner_eligible(run,entity) ||
                    simulation_lab_phase7_occlusion_preview_radius(run,entity)==0U) continue;
                const auto marker = retail_visibility_marker_for_model_class(entity.runtime_model_class_id);
                const auto mask = retail_faction_visibility_mask(
                    run.faction_packed_relation_states[entity.faction_index], marker);
                observers.push_back({float_to_q12(entity.actor.position.x),
                                     float_to_q12(entity.actor.position.y),
                                     static_cast<std::int32_t>(std::clamp<std::int64_t>(
                                     static_cast<std::int64_t>(float_to_q12(entity.actor.position.z))+
                                         entity.fog_reference_height_offset_q12,
                                     std::numeric_limits<std::int32_t>::min(),
                                     std::numeric_limits<std::int32_t>::max())),
                                     simulation_lab_phase7_occlusion_preview_radius(run,entity), mask});
            }
            occupancy = build_projected_fog_preview_occupancy(field, observers);
        }
        return *occupancy;
    };

    // One retained army root is one native MODEL owner-list node. Terminal
    // flags do not unlink it; attached models never add independent owners.
    const auto world_model_node_count = [](const SimulationLabEntity& entity) noexcept -> size_t {
        if(entity.runtime_removed || entity.root_model_component_index>=entity.model_components.size())return 0;
        return entity.model_components[entity.root_model_component_index].runtime_removed ? 0U : 1U;
    };
    const auto has_world_model_node = [&](const SimulationLabEntity& entity) noexcept {
        return world_model_node_count(entity)!=0U;
    };
    const auto root_class = [](const SimulationLabEntity& entity) {
        return entity.model_components[entity.root_model_component_index].runtime_class_id;
    };

    for (std::size_t condition_index = 0; condition_index < run.scheduled_conditions.size(); ++condition_index) {
        auto& condition = run.scheduled_conditions[condition_index];
        condition.status_and_kind &= 0xFEU;
        bool satisfied = false;
        using Kind = LevelScheduledConditionKind;
        switch (condition.kind()) {
        case Kind::NoneOrUnused:
            break;
        case Kind::NoActiveEntityWithDefinition: {
            // INGAME_SCHEDULED_CONDITION_FACTION_HAS_NO_ARMY (kind 1).
            const std::uint32_t faction = condition.operand(0);
            satisfied = std::none_of(run.entities.begin(), run.entities.end(), [&](const auto& entity) {
                return entity.faction_index == faction && has_world_model_node(entity);
            });
            break;
        }
        case Kind::NoActiveEntityWithDefinitionAndClassCommandGroupA: {
            // INGAME_SCHEDULED_CONDITION_FACTION_HAS_NO_COMMAND_GROUP_A_ARMY (kind 2).
            const std::uint32_t faction = condition.operand(0);
            satisfied = std::none_of(run.entities.begin(), run.entities.end(), [&](const auto& entity) {
                return entity.faction_index==faction && has_world_model_node(entity) &&
                    simulation_lab_runtime_class_uses_group_a_command_handler(root_class(entity));
            });
            break;
        }
        case Kind::NoActiveEntityWithDefinitionAndRuntimeId: {
            // INGAME_SCHEDULED_CONDITION_FACTION_HAS_NO_ARMY_OF_ASSET (kind 3).
            const std::uint32_t faction = condition.operand(0);
            const std::uint32_t army_asset_id = condition.operand(2);
            satisfied = std::none_of(run.entities.begin(), run.entities.end(), [&](const auto& entity) {
                return entity.faction_index == faction &&
                       entity.army_asset_id == army_asset_id && has_world_model_node(entity);
            });
            break;
        }
        case Kind::FactionInactiveOrRelationAtLeast8: {
            const std::uint32_t faction_a = condition.operand(0);
            const std::uint32_t faction_b = condition.operand(1);
            satisfied = faction_a >= 8U || faction_b >= 8U ||
                run.faction_lifecycle_states[faction_a] != kFactionLifecycleActive ||
                run.faction_lifecycle_states[faction_b] != kFactionLifecycleActive ||
                relation_state(run, faction_b, faction_a) >= 8U;
            break;
        }
        case Kind::XeniteCurrentAtLeast: {
            const std::uint32_t faction = condition.operand(0);
            satisfied = faction < run.faction_economy.size() &&
                std::bit_cast<int32_t>(run.faction_economy[faction].xenite_current_q4) >= std::bit_cast<int32_t>(condition.operand(1));
            break;
        }
        case Kind::TritiumCurrentAtLeast: {
            const std::uint32_t faction = condition.operand(0);
            satisfied = faction < run.faction_economy.size() &&
                std::bit_cast<int32_t>(run.faction_economy[faction].tritium_current_q4) >= std::bit_cast<int32_t>(condition.operand(1));
            break;
        }
        case Kind::ActiveArmyScaleValueAtLeast: {
            const std::uint32_t faction = condition.operand(0);
            satisfied = faction < run.faction_tritium_extraction_rate_q4_per_tick.size() &&
                std::bit_cast<int32_t>(run.faction_tritium_extraction_rate_q4_per_tick[faction]) >= std::bit_cast<int32_t>(condition.operand(1));
            break;
        }
        case Kind::MatchingDefinitionAndRuntimeIdActiveEntityCountAtLeast: {
            // INGAME_SCHEDULED_CONDITION_ARMY_OF_ASSET_COUNT_AT_LEAST (kind 8).
            // The retail loop counts linked MODEL roots, without a
            // construction-complete or terminal-flag filter.
            const std::uint32_t faction = condition.operand(0);
            const std::uint32_t required_count = condition.operand(1);
            const std::uint32_t army_asset_id = condition.operand(2);
            std::uint32_t count = 0U;
            for (const auto& entity : run.entities) {
                if (entity.faction_index != faction || entity.army_asset_id != army_asset_id)
                    continue;
                count += static_cast<std::uint32_t>(world_model_node_count(entity));
                if (count >= required_count && required_count != 0U) break;
            }
            // With a zero threshold the recovered unsigned decrement loop does
            // not succeed unless it wraps around; a finite model list cannot.
            satisfied = required_count != 0U && count >= required_count;
            break;
        }
        case Kind::FactionTerrainOccupancyPercentAtLeast: {
            const std::uint32_t faction = condition.operand(0);
            const auto& masks = current_occupancy();
            if (faction < 8U && !masks.empty()) {
                std::uint64_t occupied = 0U;
                for (const auto mask : masks)
                    occupied += (fog_occupancy_byte(mask, faction) & 0xF9U) != 0U;
                satisfied = static_cast<int32_t>(occupied * 100U / masks.size()) >= std::bit_cast<int32_t>(condition.operand(1));
            }
            break;
        }
        case Kind::CountdownElapsed: {
            const uint32_t remaining=condition.operand(1)-callback_step_ticks;
            satisfied=std::bit_cast<int32_t>(remaining)<1;
            condition.set_operand(1,satisfied?0U:remaining);
            break;
        }
        case Kind::XeniteStorageLimitAtMost0FA0: {
            const std::uint32_t faction = condition.operand(0);
            satisfied = faction < run.faction_economy.size() &&
                std::bit_cast<int32_t>(run.faction_economy[faction].xenite_storage_limit_q4.value_or(0U)) <= 0xFA0;
            break;
        }
        case Kind::NoActiveEntityWithClassIdOutsideClassCommandGroupA: {
            const std::uint32_t runtime_class = condition.operand(0);
            satisfied = std::none_of(run.entities.begin(), run.entities.end(), [&](const auto& entity) {
                return has_world_model_node(entity) && root_class(entity)==runtime_class &&
                    !simulation_lab_runtime_class_uses_group_a_command_handler(runtime_class);
            });
            break;
        }
        case Kind::BooleanPostfixExpression: {
            // Mirror the compact bit-stack implementation in 0x0050E120 exactly. References to
            // earlier conditions see this pass; later references intentionally see their previous
            // 20-tick result because retail clears each record only when it reaches that slot.
            std::uint32_t stack = 0U;
            for (const std::uint8_t token : condition.payload_bytes) {
                if (token == 0xFCU) break;
                if (token == 0xFDU) stack ^= 1U;
                else if (token == 0xFEU) stack = (stack >> 1U) & (stack | 0xFFFFFFFEU);
                else if (token == 0xFFU) stack = (stack >> 1U) | (stack & 1U);
                else if (token < run.scheduled_conditions.size())
                    stack = (run.scheduled_conditions[token].satisfied() ? 1U : 0U) + stack * 2U;
            }
            satisfied = (stack & 1U) != 0U;
            break;
        }
        }
        if (satisfied) condition.status_and_kind |= 1U;
    }

    run.last_fired_end_trigger_index.reset();
    for (std::size_t trigger_index = 0; trigger_index < run.end_condition_triggers.size(); ++trigger_index) {
        auto& trigger = run.end_condition_triggers[trigger_index];
        if (trigger.state_flags != 1U || trigger.condition_index >= run.scheduled_conditions.size() ||
            !run.scheduled_conditions[trigger.condition_index].satisfied()) continue;
        trigger.state_flags |= 2U;
        const std::uint32_t faction = trigger.faction_runtime_index;
        if (faction >= run.faction_lifecycle_states.size() ||
            run.faction_lifecycle_states[faction] != kFactionLifecycleActive)
            continue;
        run.faction_lifecycle_states[faction] = kFactionLifecycleEndingPending;
        if (trigger.skip_army_disable_when_one != 1U) {
            // EndTriggerFaction also sets LOCAL_FACTION_ENDED for the current
            // view (which may have followed a diplomatic merge). Phase 7 then
            // reveals that faction's field byte, including to its retained
            // terminal owners' command callbacks on the following tick.
            if(faction==run.active_view_faction_index)run.local_faction_ended=true;
            for (auto& entity : run.entities)
                if (entity.faction_index == faction) simulation_lab_mark_destroyed_hierarchy(entity);
            std::erase_if(run.pending_asset_requests,
                [&](const auto& request) { return request.faction_index == faction; });
            std::erase_if(run.secondary_purchase_requests,
                [&](const auto& request) { return request.faction_index == faction; });
        }
        run.last_fired_end_trigger_index = static_cast<std::uint32_t>(trigger_index);
        return; // Retail returns after ending the first active faction.
    }
}

bool SimulationLabExperiment::expanded_radius_overlap(
    const SimulationLabEntity& query, Vec3f query_position,
    const SimulationLabEntity& candidate) noexcept {
    const std::int32_t query_radius = query.motion.collision_radius_q12;
    const std::int32_t candidate_radius = candidate.motion.collision_radius_q12;
    if (query_radius == 0 || candidate_radius == 0) return false;
    const std::int64_t dx = static_cast<std::int64_t>(float_to_q12(candidate.actor.position.x)) -
                            static_cast<std::int64_t>(float_to_q12(query_position.x));
    const std::int64_t dy = static_cast<std::int64_t>(float_to_q12(candidate.actor.position.y)) -
                            static_cast<std::int64_t>(float_to_q12(query_position.y));
    const std::int64_t radius = static_cast<std::int64_t>(query_radius) + candidate_radius;
    return radius * radius >= dx * dx + dy * dy;
}

void SimulationLabExperiment::advance_class23_collision_state(SimulationLabRun& run, std::optional<size_t> owner) {
    for (std::size_t visit = 0; visit < (owner?1U:run.entities.size()); ++visit) {
        const auto index=owner.value_or(visit);
        auto& entity = run.entities[index];
        if (entity.runtime_model_class_id != 0x17U) continue;

        // 0x005283D0 multiplies the signed MDL +0x0C step by the global
        // simulation-step batch. Preserve the low 32-bit x86 IMUL result.
        const std::uint32_t batch = std::max(1U, config_.class23_callback_step_ticks)*std::max(1U,config_.runtime_callback_step_ticks);
        const std::int32_t step = std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(entity.vertical_deployment_step_q12_per_tick) * batch);
        const std::int32_t limit = entity.vertical_deployment_travel_limit_q12;
        const auto emit_deployment_sound = [&]() {
            if (entity.class23_deployment_sound_asset_id == 0U) return;
            SimulationLabOneShotEvent event;
            event.tick = run.tick;
            event.source_placement_index = entity.source_placement_index;
            event.army_asset_id = entity.army_asset_id;
            event.faction_index = entity.faction_index;
            event.kind = SimulationLabOneShotEventKind::Class23DeploymentSound;
            event.sound_asset_id = entity.class23_deployment_sound_asset_id;
            event.sound_gain_and_distance=std::array{entity.positioned_sound_gain_q15,entity.positioned_sound_maximum_distance_q12};
            event.root_world_position = entity.actor.position;
            event.actor = entity.actor;
            append_one_shot_event(run, std::move(event));
        };
        if ((entity.collision_state_bits & 1U) == 0U) {
            // Exact V537 class-23 release branch. Travel is initialized to zero; there is no
            // external non-zero seed. Once collision hold clears, an already-deployed child moves
            // back toward zero by adding the signed per-tick step to travel/local Z in lockstep.
            if (entity.vertical_deployment_travel_q12 != 0 && step != 0) {
                // 0x005283D0: the release branch emits at the signed full-travel
                // threshold before the first return stroke (not at every step).
                if (entity.vertical_deployment_travel_q12 <= limit)
                    emit_deployment_sound();
                entity.collision_state_bits &= ~2U;
                entity.vertical_deployment_child_local_z_q12 += step;
                entity.vertical_deployment_travel_q12 += step;
            }
            continue;
        }

        if (entity.collision_owner_timer != 0U) --entity.collision_owner_timer;
        // Bit 1 is endpoint readiness. Retail sets it optimistically, then clears it on every
        // deployment step; it therefore remains visible only on an update after travel has reached
        // the signed limit. The collision callback may establish the bidirectional link then.
        entity.collision_state_bits |= 2U;
        if (entity.collision_owner_timer == 0U) {
            entity.collision_state_bits &= ~1U;
            if (entity.linked_entity_index && *entity.linked_entity_index < run.entities.size()) {
                const std::size_t previous = *entity.linked_entity_index;
                entity.collision_owner_timer = 8U;
                entity.linked_entity_index.reset();
                if (expanded_radius_overlap(entity, entity.actor.position, run.entities[previous])) {
                    entity.linked_entity_index = previous;
                    entity.collision_state_bits |= 1U;
                }
            }
        }
        if (limit < entity.vertical_deployment_travel_q12 && step != 0) {
            // First outbound stroke begins at zero. Retail checks the old travel
            // value before modifying child local Z or the signed travel accumulator.
            if (entity.vertical_deployment_travel_q12 == 0)
                emit_deployment_sound();
            entity.collision_state_bits &= ~2U;
            entity.vertical_deployment_child_local_z_q12 -= step;
            entity.vertical_deployment_travel_q12 -= step;
        }
    }
}

void SimulationLabExperiment::recompute_resource_storage_limits(SimulationLabRun& run) {
    bool any_derived_limits = false;
    for (std::size_t faction = 1U; faction < run.faction_economy.size(); ++faction) {
        if (run.faction_economy[faction].derive_storage_limits_from_components) {
            any_derived_limits = true;
            break;
        }
    }
    if (!any_derived_limits) return;

    std::array<std::uint64_t, kSimulationLabFactionCount> xenite{};
    std::array<std::uint64_t, kSimulationLabFactionCount> tritium{};
    for (std::size_t faction = 1U; faction < run.faction_economy.size(); ++faction) {
        if (!run.faction_economy[faction].derive_storage_limits_from_components) continue;
        xenite[faction] = run.faction_economy[faction].baseline_xenite_storage_limit_q4;
        tritium[faction] = run.faction_economy[faction].baseline_tritium_storage_limit_q4;
    }
    for (const auto& entity : run.entities) {
        if (!entity.construction_complete || entity.faction_index == 0U ||
            entity.faction_index >= kSimulationLabFactionCount) continue;
        for (const auto& component : entity.model_components) {
            if (!component.has_resource_storage_capacity_contribution || component.runtime_removed)
                continue;
            auto& total = component.resource_storage_selector == 0U
                ? xenite[entity.faction_index] : tritium[entity.faction_index];
            total += component.resource_storage_capacity_contribution_q4;
        }
    }
    for (std::size_t faction = 1U; faction < run.faction_economy.size(); ++faction) {
        // Absorption transfers assets but leaves the old faction record intact.
        if (run.faction_merge_redirect[faction] != faction) continue;
        auto& economy = run.faction_economy[faction];
        if (!economy.derive_storage_limits_from_components) continue;
        if (!economy.xenite_storage_limit_locked)
            economy.xenite_storage_limit_q4 = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(xenite[faction], std::numeric_limits<std::uint32_t>::max()));
        if (!economy.tritium_storage_limit_locked)
            economy.tritium_storage_limit_q4 = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(tritium[faction], std::numeric_limits<std::uint32_t>::max()));
    }
}

void SimulationLabExperiment::recompute_energy_generation_capacity(SimulationLabRun& run) {
    run.faction_energy_generation_capacity_q4[0]=0U;
    for (std::size_t faction=1;faction<run.faction_economy.size();++faction) {
        if (run.faction_merge_redirect[faction] != faction) continue;
        const auto& economy=run.faction_economy[faction];
        run.faction_energy_generation_capacity_q4[faction]=
            economy.derive_energy_allocation_from_components
                ?economy.baseline_energy_generation_capacity_q4:0U;
    }
    for (const auto& entity:run.entities) {
        const auto faction=entity.faction_index;
        if (entity.runtime_removed || !entity.construction_complete || faction==0U ||
            faction>=kSimulationLabFactionCount || run.faction_merge_redirect[faction]!=faction ||
            !run.faction_economy[faction].derive_energy_allocation_from_components) continue;
        for (const auto& component:entity.model_components) {
            // Generation is registered at creation and subtracted at release;
            // damage and destruction animation do not remove that capacity.
            if (component.runtime_removed) continue;
            const auto generation=uint64_t(run.faction_energy_generation_capacity_q4[faction])+
                component.energy_generation_capacity_q4;
            run.faction_energy_generation_capacity_q4[faction]=static_cast<uint32_t>(
                std::min<uint64_t>(generation,UINT32_MAX));
        }
    }
}

void SimulationLabExperiment::update_energy_allocation(SimulationLabRun& run) {
    static constexpr std::array<std::uint32_t, 24> kPriorityByRuntimeClass{
        0x000U, 0x000U, 0x000U, 0x000U, 0x100U, 0x300U,
        0x400U, 0x500U, 0x600U, 0x700U, 0x200U, 0x1200U,
        0x000U, 0x800U, 0x1000U, 0x000U, 0x000U, 0x000U,
        0x000U, 0x000U, 0x000U, 0x000U, 0x800U, 0x100U};

    bool enabled = false;
    for (std::size_t faction = 1U; faction < run.faction_economy.size(); ++faction)
        enabled |= run.faction_economy[faction].derive_energy_allocation_from_components;
    if (!enabled) return;

    recompute_energy_generation_capacity(run);
    run.faction_supplied_energy_demand_q4.fill(0U);
    run.faction_unpowered_energy_demand_q4.fill(0U);
    run.faction_last_tritium_energy_burn_q4.fill(0U);

    struct DemandRecord {
        SimulationLabModelComponentState* component{};
        std::uint32_t faction{};
        std::uint32_t demand_q4{};
        std::uint32_t priority{};
        std::uint64_t order{};
    };
    std::vector<DemandRecord> demand_records;
    demand_records.reserve(256U);
    std::uint64_t order = 0U;
    for (size_t visit=0;visit<run.entities.size();++visit) {
        auto& entity=run.entities[runtime_owner_index(run,visit)];
        if (entity.runtime_removed || !entity.construction_complete || entity.faction_index == 0U ||
            entity.faction_index >= kSimulationLabFactionCount ||
            !run.faction_economy[entity.faction_index].derive_energy_allocation_from_components)
            continue;
        const auto root_index=entity.root_model_component_index;
        const auto* root=root_index<entity.model_components.size()?&entity.model_components[root_index]:nullptr;
        const bool authored=root && root->definition_flags68.has_value();
        const bool attached=authored && ((*root->definition_flags68&0x80U)!=0U);
        for (auto& component : entity.model_components) {
            if(component.runtime_removed)continue;
            if(authored) {
                if(root->runtime_removed || (root->runtime_flags_ec&0x10U)!=0U)continue;
                if(&component!=root && !(attached&&component.parent_component_index==root_index))continue;
            }else if(component.terminal_runtime_flag8 || (component.runtime_flags_ec&0x10U)!=0U)continue;
            uint32_t demand_q4=component.retail_energy_demand_q4;
            if(&component==root) {
                if(entity.production_building)demand_q4+=static_cast<uint32_t>(entity.production_active_energy_demand_q4);
                if(entity.research_active)demand_q4+=entity.research_energy_demand_q4;
            }
            if(demand_q4==0U || demand_records.size()>=256U)continue;
            component.runtime_flags_ec&=~1U;
            const uint32_t cls=component.runtime_class_id;
            demand_records.push_back({&component,entity.faction_index,demand_q4,
                cls<kPriorityByRuntimeClass.size()?kPriorityByRuntimeClass[cls]:0U,order++});
        }
    }
    // With no model consumers native skips allocation, including inventory demand.
    if (run.level_runtime_enabled && demand_records.empty()) return;
    // Retail uses repeated swaps against every later strictly-higher entry, NOT stable_sort.
    // Equal-priority order can change after an earlier higher-priority swap.
    for (std::size_t first = 0; first + 1U < demand_records.size(); ++first)
        for (std::size_t other = first + 1U; other < demand_records.size(); ++other)
            if (demand_records[first].priority < demand_records[other].priority)
                std::swap(demand_records[first], demand_records[other]);

    for (std::uint32_t faction = 1U; faction < kSimulationLabFactionCount; ++faction) {
        auto& economy = run.faction_economy[faction];
        if (!economy.derive_energy_allocation_from_components) continue;
        std::uint64_t primary_inventory_demand_q4 = 0U;
        for (const auto& request : run.pending_asset_requests) {
            if (request.faction_index != faction) continue;
            const auto* asset = production_asset(request.army_asset_id);
            if (asset == nullptr) continue;
            primary_inventory_demand_q4 += asset->ai_definition_class_value74 == 0U ? 0x10U : 0x50U;
        }
        run.faction_supplied_energy_demand_q4[faction] = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(primary_inventory_demand_q4,
                                    std::numeric_limits<std::uint32_t>::max()));

        // Native supply arithmetic is a dword followed by a signed capacity comparison.
        const uint32_t uncapped_supply=economy.tritium_current_q4*16U+economy.baseline_energy_supply_q4;
        const uint32_t capacity=run.faction_energy_generation_capacity_q4[faction];
        const uint32_t capped_supply=std::bit_cast<int32_t>(capacity)<std::bit_cast<int32_t>(uncapped_supply)?capacity:uncapped_supply;
        std::uint32_t remaining = capped_supply > run.faction_supplied_energy_demand_q4[faction]
            ? capped_supply - run.faction_supplied_energy_demand_q4[faction] : 0U;

        for (auto& record : demand_records) {
            if (record.faction != faction) continue;
            if (remaining < record.demand_q4) {
                record.component->runtime_flags_ec |= 1U;
                run.faction_unpowered_energy_demand_q4[faction] += record.demand_q4;
            } else {
                remaining -= record.demand_q4;
                run.faction_supplied_energy_demand_q4[faction] += record.demand_q4;
            }
        }

        const std::uint32_t supplied = run.faction_supplied_energy_demand_q4[faction];
        const std::uint32_t fuel_backed = supplied > economy.baseline_energy_supply_q4
            ? supplied - economy.baseline_energy_supply_q4 : 0U;
        const std::uint32_t tritium_burn_q4 = (fuel_backed >> 4U)*std::max(1U,config_.runtime_callback_step_ticks);
        run.faction_last_tritium_energy_burn_q4[faction] = tritium_burn_q4;
        economy.tritium_current_q4 -= tritium_burn_q4;
    }
}

namespace {
std::optional<int32_t> lab_owner_water_damage(const SimulationLabEntity& entity, const FieldGrid& field) {
    // Native movement callbacks still apply water impacts to a terminal model
    // until its hierarchy is released. Even zero health resets the delay.
    if (!entity.construction_complete || entity.runtime_removed ||
        entity.root_model_component_index>=entity.model_components.size()) return {};
    const auto contact_kind=entity.model_components[entity.root_model_component_index].placement_contact_kind;
    if(!simulation_uses_water_damage_callback(entity.runtime_model_class_id,contact_kind))return {};
    const auto water_delta=field.sample_retail_water_delta_q12(
        float_to_q12(entity.actor.position.x),float_to_q12(entity.actor.position.y));
    return simulation_water_damage_amount(water_delta,entity.water_damage_threshold_q12,
        entity.water_damage_multiplier);
}
}

void SimulationLabExperiment::apply_water_damage(SimulationLabRun& run, const FieldGrid& field,
    std::optional<std::size_t> only_owner) {
    for (size_t visit=0;visit<(only_owner?1U:run.entities.size());++visit) {
        const auto index=only_owner.value_or(visit);
        if(index>=run.entities.size())continue;
        auto& entity=run.entities[index];
        if(const auto damage=lab_owner_water_damage(entity,field)){
            const auto before=entity.root_model_integrity;
            apply_component_damage(run, entity, entity.root_model_component_index, *damage, std::nullopt);
            ++run.metrics.water_damage_applications;
            run.metrics.water_damage_integrity_lost+=static_cast<uint64_t>(std::max(0,before-entity.root_model_integrity));
        }
    }
}

void SimulationLabExperiment::sync_root_component_summary(SimulationLabEntity& entity) noexcept {
    if (entity.root_model_component_index >= entity.model_components.size()) {
        entity.root_model_maximum_integrity = 0;
        entity.root_model_integrity = 0;
        entity.root_model_terminal = false;
        entity.root_model_last_impact_overshoot = 0;
        return;
    }
    const auto& root = entity.model_components[entity.root_model_component_index];
    entity.root_model_maximum_integrity = root.maximum_integrity;
    entity.root_model_integrity = root.integrity;
    entity.root_model_terminal = root.terminal_runtime_flag8;
    entity.root_model_last_impact_overshoot = root.last_impact_overshoot;
}

void SimulationLabExperiment::apply_component_damage(
    SimulationLabRun& run, SimulationLabEntity& entity, std::size_t component_index,
    std::int32_t damage_amount, std::optional<std::uint32_t> direct_impact_angle_turn32,
    std::optional<std::uint32_t> source_faction) {
    if(const auto trace=std::getenv("THANDOR_SIMLAB_DAMAGE_TRACE_TICK");trace&&std::strtoul(trace,nullptr,10)==run.tick)
        std::fprintf(stderr,"DAMAGE tick=%u slot=%zu arm=%u component=%zu damage=%d source=%u angle=%u\n",run.tick,
            entity.native_army_pool_slot,entity.army_asset_id,component_index,damage_amount,
            source_faction.value_or(UINT32_MAX),direct_impact_angle_turn32.value_or(UINT32_MAX));
    if (component_index >= entity.model_components.size()) return;
    auto& component = entity.model_components[component_index];
    // Both 0x0052A200 and 0x0052A2E0 store raw 0x00000200 at +0xF8 before testing integrity.
    component.raw_impact_state_f8_ff = 0x00000200U;
    component.health_regeneration_delay_ticks = 0x200;
    if (component.integrity <= 0) { sync_root_component_summary(entity); return; }

    const std::int32_t before = component.integrity;
    const std::int64_t after = static_cast<std::int64_t>(before) - damage_amount;
    if (damage_amount > 0)
        run.metrics.class0_impact_damage_applied += static_cast<std::uint64_t>(
            std::min<std::int64_t>(before, damage_amount));

    if (after <= 0) {
        const std::int64_t overshoot64 = -after;
        component.integrity = 0;
        component.terminal_runtime_flag8 = true;
        component.terminal_self_reference = true;
        // A model already visited in the captured owner chain cannot process
        // newly fatal damage until its next callback (e.g. scenery crushed by
        // an older vehicle). Do not fire its terminal channels at tick end.
        if(run.level_runtime_enabled&&run.tick!=0U&&component.last_model_callback_tick==run.tick)
            component.terminal_effect_not_before_tick=run.tick+1U;
        component.last_impact_overshoot = static_cast<std::int32_t>(std::min<std::int64_t>(
            overshoot64, std::numeric_limits<std::int32_t>::max()));
        ++run.metrics.component_terminal_events;
        if(component_index!=entity.root_model_component_index)
            ++run.metrics.child_component_terminal_events;

        if (component.parent_component_index &&
            *component.parent_component_index < entity.model_components.size()) {
            ++run.metrics.component_parent_propagations;
            apply_component_damage(run, entity, *component.parent_component_index,
                                   component.last_impact_overshoot, std::nullopt, source_faction);
        } else {
            ++run.metrics.class0_terminal_impacts;
            run.metrics.class0_impact_overshoot +=
                static_cast<std::uint64_t>(component.last_impact_overshoot);
            if (direct_impact_angle_turn32 && component.runtime_class_id == 0U &&
                component.placement_contact_kind == 0) {
                component.model_rotation_angle0 = *direct_impact_angle_turn32;
                ++run.metrics.root_direct_impact_angle_updates;
            }
            // Only the shot-impact relation path credits C/D; water/collision destroys do not.
            if (source_faction && entity.faction_index < run.faction_relation_counter_c.size() &&
                entity.faction_index != 0U) {
                const bool relation_d = simulation_lab_runtime_class_routes_terminal_to_relation_d(
                    component.runtime_class_id);
                if (relation_d) {
                    ++run.faction_relation_counter_d[entity.faction_index];
                    ++run.metrics.root_relation_counter_d_events;
                } else {
                    ++run.faction_relation_counter_c[entity.faction_index];
                    ++run.metrics.root_relation_counter_c_events;
                }
                // GameEntityRuntime_ApplyImpactDamageAndFactionRelationState additionally credits
                // the firing faction through E/F. Collision-only 0x0052A2E0 callers pass no source.
                if (source_faction && *source_faction != 0U && *source_faction < run.faction_score.size() &&
                    damage_amount != 0) {
                    if (relation_d) ++run.faction_score[*source_faction].relation_counter_f;
                    else ++run.faction_score[*source_faction].relation_counter_e;
                }
            }
        }
    } else {
        component.integrity = static_cast<std::int32_t>(std::min<std::int64_t>(
            after, std::numeric_limits<std::int32_t>::max()));
        // Retail clamps only when the resulting live value is at/above the MDL +0x60 maximum;
        // this matters for negative damage/heal-style callers even though collision damage is positive.
        if (component.integrity >= component.maximum_integrity)
            component.integrity = component.maximum_integrity;
        component.last_impact_overshoot = 0;
    }
    sync_root_component_summary(entity);
}

void SimulationLabExperiment::advance_terminal_attachment_effects(SimulationLabRun& run, const FieldGrid& field,
                                                                bool release_completed_only,std::optional<uint64_t> before_projectile_serial,
                                                                std::optional<std::size_t> only_owner,
                                                                bool advance_wreck) {
    std::vector<SimulationLabEntity> replacement_owners;
    std::vector<std::size_t> owner_order;
    if(only_owner)owner_order.push_back(*only_owner);
    else {
        owner_order.resize(run.entities.size());
        std::iota(owner_order.begin(),owner_order.end(),0U);
    }
    for (const auto entity_index : owner_order) {
        auto& entity=run.entities[entity_index];
        if (entity.runtime_removed) continue;
        // Runtime class 12 is the authored sinking-wreck update. It moves the
        // original hierarchy, not a guessed part or a new destruction stage.
        // Keep the callback's signed travel/distance, and let the common root
        // release path dispose of the whole model when it has sunk out of sight.
        const bool native_owner_callbacks=run.level_runtime_enabled&&runtime_catalog_;
        bool wreck_finished=false;
        if (!release_completed_only &&
            (advance_wreck || (!only_owner && !native_owner_callbacks)) &&
            entity.runtime_model_class_id == 12U &&
            (!entity.synthetic||entity.faction_index!=0U||entity.construction_start_tick<run.tick) &&
            entity.class12_vertical_step_q12_per_tick != 0 &&
            entity.class12_model_height_q12 > 0 &&
            entity.root_model_component_index < entity.model_components.size()) {
            const std::int64_t step=std::int64_t(entity.class12_vertical_step_q12_per_tick) *
                std::max(1U,config_.runtime_callback_step_ticks);
            entity.class12_vertical_offset_q12=static_cast<std::int32_t>(std::clamp<std::int64_t>(
                std::int64_t(entity.class12_vertical_offset_q12)+step,INT32_MIN,INT32_MAX));
            entity.class12_travel_distance_q12=static_cast<std::int32_t>(std::clamp<std::int64_t>(
                std::int64_t(entity.class12_travel_distance_q12)-step,INT32_MIN,INT32_MAX));
            wreck_finished=entity.class12_travel_distance_q12>entity.class12_model_height_q12;
            if (wreck_finished)
                entity.model_components[entity.root_model_component_index].destruction_complete_tick=run.tick;
        }
        if(advance_wreck&&!wreck_finished)continue;
        for (size_t index = 0; index < entity.model_components.size(); ++index) {
            auto& component = entity.model_components[index];
            const auto component_scope=run.native_world_objects.enter_component(index,-2);
            if (component.runtime_removed) continue;
            if (component.destruction_complete_tick && run.tick >= *component.destruction_complete_tick) {
                if(only_owner&&!(wreck_finished&&index==entity.root_model_component_index))continue;
                if(before_projectile_serial&&(!component.destruction_effect_after_projectile_serial||
                   *component.destruction_effect_after_projectile_serial<*before_projectile_serial))continue;
                const auto world_scope=run.native_world_objects.enter_owner(
                    component.destruction_effect_world_handle!=kSimulationLabNoWorldSlot
                    ?component.destruction_effect_world_handle:simulation_lab_world_model_handle(entity));
                const auto released_poses=runtime_catalog_&&!entity.native_world_model_slots.empty()
                    ?simulation_lab_current_model_poses(entity,field,*runtime_catalog_)
                    :std::vector<SimulationLabEffectPose>{};
                // Release owner then descendants without shifting any retained runtime indices.
                for (size_t child = index; child < entity.model_components.size(); ++child) {
                    size_t cursor = child;
                    bool owned = cursor == index;
                    for (size_t hops = 0; !owned && hops < entity.model_components.size(); ++hops) {
                        const auto parent = entity.model_components[cursor].parent_component_index;
                        if (!parent || *parent >= entity.model_components.size()) break;
                        cursor = *parent; owned = cursor == index;
                    }
                    auto& released = entity.model_components[child];
                    if (!owned || released.runtime_removed) continue;
                    if (released.has_resource_storage_capacity_contribution && entity.faction_index > 0U &&
                        entity.faction_index < run.faction_economy.size()) {
                        auto& economy = run.faction_economy[entity.faction_index];
                        const bool tritium = released.resource_storage_selector != 0U;
                        auto& limit = tritium ? economy.tritium_storage_limit_q4 : economy.xenite_storage_limit_q4;
                        auto& stock = tritium ? economy.tritium_current_q4 : economy.xenite_current_q4;
                        const bool locked = tritium ? economy.tritium_storage_limit_locked : economy.xenite_storage_limit_locked;
                        if (economy.derive_storage_limits_from_components && !locked) {
                            const auto contribution = released.resource_storage_capacity_contribution_q4;
                            if (released.integrity < 2 && limit.value_or(0U) != 0U && !(released.runtime_flags_ec & 0x20U))
                                stock -= static_cast<uint32_t>(static_cast<int64_t>(static_cast<int32_t>(contribution)) *
                                    static_cast<int32_t>(stock) / static_cast<int32_t>(*limit));
                            limit = limit.value_or(0U) - contribution;
                        }
                    }
                    release_model_smoke_slot(run,entity,child,released_poses);
                    released.runtime_removed = true;
                    released.integrity = 0; released.terminal_runtime_flag8 = true;
                    released.terminal_self_reference = false;
                }
                while (entity.collision_model_component_index < entity.model_components.size() &&
                       entity.model_components[entity.collision_model_component_index].runtime_removed) {
                    const auto parent=entity.model_components[entity.collision_model_component_index].parent_component_index;
                    if(!parent || *parent>=entity.model_components.size())break;
                    entity.collision_model_component_index=*parent;
                }
                ++run.metrics.model_hierarchies_removed;
                if (index == entity.root_model_component_index) {
                    simulation_lab_capture_army_slot(run,entity);
                    // The native owner disappears from research and energy scans here.
                    // Retained Lab indices must not retain a live job or block its TEC.
                    if (entity.faction_index < run.faction_economy.size()) {
                        if (entity.research_active)
                            run.faction_active_research_energy_q4[entity.faction_index] -= entity.research_energy_demand_q4;
                        if (entity.production_building)
                            run.faction_active_production_energy_q4[entity.faction_index] -= entity.production_active_energy_demand_q4;
                    }
                    set_research_job_flags(entity,0U);
                    entity.research_energy_demand_q4 = entity.research_xenite_due_q4 = 0U;
                    entity.production_building = false;
                    entity.production_active_energy_demand_q4 = 0;
                    entity.runtime_removed = true;
                    entity.root_model_terminal = true; entity.root_model_integrity = 0;
                    entity.motion.collision_radius_q12 = 0;
                    simulation_lab_clear_released_target(run,static_cast<size_t>(&entity-run.entities.data()));
                    entity.actor.move_target.reset(); entity.target_entity_index.reset();
                    entity.last_translated = entity.last_turned = false;
                    ++run.metrics.entities_removed;
                    // ModelRuntimePool_DestroyHierarchyAndDetach creates the
                    // authored replacement as a neutral owner after freeing the
                    // model hierarchy. It participates in model-pool reuse too.
                    if(run.level_runtime_enabled&&entity.destroyed_replacement_army_asset_id>=0&&
                       (component.runtime_flags_ec&0x20U)==0U){
                        const auto id=static_cast<uint32_t>(entity.destroyed_replacement_army_asset_id);
                        if(const auto* asset=production_asset(id)){
                            auto replacement=make_entity_from_seed(asset->entity_seed,0U,
                                entity.actor.position,true,run.faction_technology_masks[0]);
                            replacement.actor.body_yaw_turn16=entity.actor.body_yaw_turn16;
                            replacement.source_placement_index=entity.source_placement_index;
                            replacement.construction_start_tick=run.tick;
                            replacement.construction_complete_tick=run.tick;
                            if(runtime_catalog_)simulation_lab_prepare_missing_model_geometry(replacement,field,*runtime_catalog_);
                            claim_model_smoke_slots(run,replacement,run.entities.size()+replacement_owners.size());
                            replacement_owners.push_back(std::move(replacement));
                            ++run.metrics.destroyed_replacement_armies_spawned;
                        }else ++run.metrics.destroyed_replacement_armies_unresolved;
                    }
                    // DestroyHierarchy creates its neutral replacement while the
                    // old ArmyRuntimeSlot is still occupied. Army release follows.
                    if(run.army_pool.enabled)run.army_pool.release(entity.native_army_pool_slot);
                    entity.native_army_pool_slot=SIZE_MAX;
                }else if(entity.runtime_model_hierarchy&&!released_poses.empty()&&
                         entity.root_model_component_index<entity.model_components.size()){
                    // Native rebuilds the owner's selection metrics on detach.
                    // An attachment below the root can dominate the unsigned
                    // eye-height maximum until its runtime is released.
                    const auto& nodes=*entity.runtime_model_hierarchy;
                    const auto root=entity.model_components[entity.root_model_component_index].source_model_node_index;
                    if(root<released_poses.size()){
                        SimulationLabDerivedSelectionMetrics metrics;
                        const auto root_z=float_to_q12(released_poses[root].position.z);
                        for(const auto& attached:entity.model_components){
                            const auto node=attached.source_model_node_index;
                            if(attached.runtime_removed||node>=nodes.size()||node>=released_poses.size())continue;
                            const auto offset=movement_sub(float_to_q12(released_poses[node].position.z),root_z);
                            simulation_lab_accumulate_derived_selection_metrics(metrics,nodes[node],offset);
                        }
                        entity.fog_uncover_radius_q12=metrics.fog_uncover_radius_q12;
                        entity.occupancy_mark_radius_units=metrics.occupancy_mark_radius_units;
                        entity.fog_reference_height_offset_q12=metrics.fog_reference_height_offset_q12;
                    }
                }
                continue;
            }
            if(release_completed_only||(run.level_runtime_enabled&&!only_owner)||
               run.tick<component.terminal_effect_not_before_tick)continue;
            // Damage sets +0x38 to SELF, including roots. It is not the parent pointer.
            if (component.integrity > 0 || !component.terminal_self_reference ||
                (component.runtime_flags_ec & 0x20U) != 0U) continue;
            // Open Thandor's MDL +0x190 is a placement/dismantle effect, not a
            // combat-death channel (army/class_dispatch.cpp, world/model/pool.cpp).
            // Do not fabricate a ninth terminal effect. The eight authored channels
            // below remain responsible for explosion points and their completion.
            for (std::size_t channel = 0; channel < component.terminal_attachment_countdowns.size();
                 ++channel) {
                auto& countdown = component.terminal_attachment_countdowns[channel];
                if (countdown == 0 && component.terminal_attachment_effect_ids[channel] != 0U &&
                    (!component.terminal_attachment_metadata_prepared ||
                     (component.terminal_attachment_point_mask & (1U << channel)))) {
                    const auto ticks = component.terminal_attachment_completion_ticks[channel];
                    if (ticks != 0U && (!component.destruction_complete_tick ||
                        run.tick + ticks <= *component.destruction_complete_tick)){
                        component.destruction_complete_tick = run.tick + ticks;
                        component.destruction_effect_after_projectile_serial=run.next_projectile_serial-1U;
                    }
                    SimulationLabOneShotEvent event;
                    event.tick = run.tick;
                    event.source_placement_index = entity.source_placement_index;
                    event.army_asset_id = entity.army_asset_id;
                    event.faction_index = entity.faction_index;
                    event.kind = SimulationLabOneShotEventKind::TerminalAttachmentEffect;
                    event.effect_id = component.terminal_attachment_effect_ids[channel];
                    event.source_model_node_index = component.source_model_node_index;
                    event.attachment_channel = static_cast<std::uint32_t>(channel);
                    event.root_world_position = entity.actor.position;
                    event.actor = entity.actor;
                    // Resolve terminal SPR class-3 attachments while the exact dying
                    // hierarchy and its pose still exist. The GUI must not infer the
                    // origin from a later snapshot or a non-existent synthetic LEV row.
                    if (runtime_catalog_ && entity.runtime_model_hierarchy &&
                        component.source_model_node_index < entity.runtime_model_hierarchy->size()) {
                        const auto& nodes = *entity.runtime_model_hierarchy;
                        const auto poses = simulation_lab_current_model_poses(entity,field,*runtime_catalog_);
                        const auto emit_points = [&](size_t node_index,bool child_zero){
                            if (node_index >= nodes.size() || node_index >= poses.size() ||
                                !simulation_lab_model_node_present(entity,node_index)) return;
                            const auto sprite = runtime_catalog_->sprites.find(nodes[node_index].sprite_path);
                            if (sprite == runtime_catalog_->sprites.end()) return;
                            const auto& pose=poses[node_index];
                            for (const auto& [key,local] : sprite->second.damage_effect_points) {
                                if(key != channel)continue;
                                SimulationLabTerminalEffectPoint point;
                                point.position=simulation_lab_model_world_point(pose,local);
                                point.basis=pose.basis;
                                if(child_zero){
                                    const auto fixed=retail_model_basis_q28(0U,0x4000U,0U);
                                    for(size_t axis=0;axis<9U;++axis)
                                        point.basis[axis]=static_cast<float>(fixed[axis])/268435456.0F;
                                }
                                event.terminal_attachment_world_points.push_back(point);
                                if(run.level_runtime_enabled){
                                    const auto effect=runtime_catalog_->effects.find(event.effect_id);
                                    if(effect!=runtime_catalog_->effects.end()){
                                        const auto& d=effect->second.definition;
                                        if(d.transition_kind==4U&&d.completion_countdown_ticks==1U&&
                                           effect_lifetime_ticks(d)>1U){
                                            const auto normal=retail_contact_normal_angles(field,
                                                float_to_q12(point.position.x),float_to_q12(point.position.y));
                                            if(normal){
                                                const float pitch=std::asin(std::clamp(point.basis[8],-1.F,1.F));
                                                const auto azimuth=std::abs(std::cos(pitch))<1.e-6F
                                                    ?component.model_rotation_angle0
                                                    :radians_to_turn16(std::atan2(point.basis[5],point.basis[2]));
                                                const auto direction=retail_direction_scaled(
                                                    radians_to_turn16(pitch)-d.terrain_pitch_drop_per_age_squared,azimuth,4096);
                                                const auto surface=retail_direction_scaled(*normal>>16,*normal&0xFFFFU,4096);
                                                const int64_t dot=int64_t(direction[0])*surface[0]+
                                                    int64_t(direction[1])*surface[1]+int64_t(direction[2])*surface[2];
                                                if(dot>=0&&(!component.destruction_complete_tick||
                                                           run.tick+1U<=*component.destruction_complete_tick)){
                                                    component.destruction_complete_tick=run.tick+1U;
                                                    component.destruction_effect_after_projectile_serial=run.next_projectile_serial-1U;
                                                }
                                            }
                                        }
                                    }
                                    if(effect!=runtime_catalog_->effects.end()){
                                        const bool has_callbacks=effect_linked_effect_spawn_tick(effect->second.definition)||
                                            !effect->second.emissions.empty();
                                        SimulationLabEffectPose effect_pose{point.basis,point.position};
                                        effect_pose.native_angles=child_zero
                                            ?std::optional{std::array<uint32_t,3>{0U,0x4000U,0U}}:pose.native_angles;
                                        if(effect_pose.native_angles){
                                            // Fatal impacts write the class-0 root's azimuth
                                            // directly. The destruction emitter reads that field.
                                            if(!child_zero&&index==entity.root_model_component_index&&
                                               component.runtime_class_id==0U&&component.placement_contact_kind==0)
                                                (*effect_pose.native_angles)[2]=component.model_rotation_angle0;
                                            const auto& a=*effect_pose.native_angles;
                                            effect_pose.native_basis=retail_model_basis_q28(a[0],a[1],a[2]);
                                            for(size_t axis=0;axis<9U;++axis)
                                                effect_pose.basis[axis]=float((*effect_pose.native_basis)[axis])/268435456.F;
                                        }
                                        if(has_callbacks||run.level_runtime_enabled){
                                            const auto handle=link_environmental_effect(run,event.effect_id,effect_pose,!has_callbacks);
                                            if(component.destruction_complete_tick&&
                                               component.destruction_effect_world_handle==kSimulationLabNoWorldSlot)
                                                component.destruction_effect_world_handle=handle;
                                        }
                                    }
                                }
                            }
                        };
                        const auto root=component.source_model_node_index;
                        emit_points(root,false);
                        // Retail repeats the selected channel on direct same-MDL child 0,
                        // with fixed (0,0x4000,0) orientation. Preserve event multiplicity.
                        for(size_t child=0;child<nodes.size();++child){
                            if(nodes[child].parent_index==root && nodes[child].child_slot==0U &&
                               nodes[child].model_definition_id==nodes[root].model_definition_id &&
                               nodes[child].creates_runtime_node()){
                                emit_points(child,true);break;
                            }
                        }
                    }
                    ++run.metrics.terminal_attachment_effect_events;
                    run.metrics.terminal_attachment_world_points_prepared +=
                        static_cast<uint32_t>(event.terminal_attachment_world_points.size());
                    append_one_shot_event(run, std::move(event));
                }
                // x86 DEC is 32-bit wraparound. Preserve that exactly without signed-overflow UB;
                // after a zero channel fires it becomes -1 and cannot fire again.
                countdown = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(countdown) - 1U);
            }
            for (auto& child : entity.model_components)
                if (child.parent_component_index == index) {
                    child.integrity = 0; child.terminal_runtime_flag8 = true;
                    child.terminal_self_reference = false;
                }
        }
    }
    for(auto& replacement:replacement_owners)run.entities.push_back(std::move(replacement));
}

namespace {
void release_projectile_world_slot(SimulationLabRun& run,SimulationProjectileState& projectile){
    const auto slot=projectile.native_world_slot;
    if(slot==kSimulationLabNoWorldSlot)return;
    const auto position=projectile.runtime_flight_initialized?
        projectile.runtime_flight_position:projectile.launch_position;
    const auto yaw=projectile.native_model_roll_turn16;
    const auto pose=lab_world_pose(position,yaw);
    run.native_world_objects.release(slot,pose);
    projectile.native_world_slot=kSimulationLabNoWorldSlot;
}
}

void SimulationLabExperiment::advance_projectiles(SimulationLabRun& run, const FieldGrid& field,
    std::vector<std::size_t>* serviced_model_emitters,
    std::vector<std::size_t>* serviced_model_callbacks) {
    std::vector<SimulationLabLiveShotHull> environmental_hulls;
    if(runtime_catalog_&&std::any_of(run.projectiles.begin(),run.projectiles.end(),[](const auto& p){
        return !p.impact_damage_applied;}))
        environmental_hulls=simulation_lab_current_shot_hulls(run,field,*runtime_catalog_);
    auto collision_angle_generation=run.metrics.root_direct_impact_angle_updates;
    std::vector<std::size_t> projectile_order;
    for(std::size_t i=0;i<run.projectiles.size();++i)
        if(!run.projectiles[i].impact_damage_applied)projectile_order.push_back(i);
    std::vector<std::size_t> model_order;
    if(serviced_model_emitters){
        for(std::size_t i=0;i<run.entities.size();++i){const auto& e=run.entities[i];
            const bool release_due=e.root_model_component_index<e.model_components.size()&&
                e.model_components[e.root_model_component_index].destruction_complete_tick&&
                run.tick>=*e.model_components[e.root_model_component_index].destruction_complete_tick;
            // A newer completion effect releases this root before its owner callback.
            // Servicing it among older shots would decrement retained pool state once more.
            if(e.synthetic&&!release_due&&
               e.construction_complete&&e.construction_start_tick<run.tick&&
               simulation_lab_has_live_root_model_owner(e))
                model_order.push_back(i);
        }
        std::sort(model_order.begin(),model_order.end(),[&](auto a,auto b){
            return simulation_lab_owner_order(run.entities[a],a)>simulation_lab_owner_order(run.entities[b],b);
        });
        std::sort(projectile_order.begin(),projectile_order.end(),[&](auto a,auto b){
            return std::tie(run.projectiles[a].spawn_tick,run.projectiles[a].serial)>
                   std::tie(run.projectiles[b].spawn_tick,run.projectiles[b].serial);
        });
    }
    std::size_t next_model=0;
    const auto service_model_emitter=[&](std::size_t owner){
        if(std::find(serviced_model_emitters->begin(),serviced_model_emitters->end(),owner)!=
            serviced_model_emitters->end())return;
        advance_production_emitters(run,field,owner);
        serviced_model_emitters->push_back(owner);
    };
    const auto service_next_model=[&](){
        const auto owner=model_order[next_model++];
        const auto& entity=run.entities[owner];
        const auto cls=entity.runtime_model_class_id;
        if(cls!=17U||!serviced_model_callbacks)service_model_emitter(owner);
        if(serviced_model_callbacks&&cls!=21U){
            // Root movement, hierarchy maintenance and AI assignment occupy
            // one position in the native owner list. This includes produced
            // ground children: their unlink must reach an older factory's
            // research/door callback before still older shots are serviced.
            release_native_factory_child_if_clear(run,owner);
            if(cls==13U)lab_unlink_factory_child3(run,run.entities[owner],field,runtime_catalog_.get());
            const bool busy=entity.production_building||(cls==13U&&entity.production_behavior_state!=0U);
            if(busy)advance_production_jobs(run,field,owner);
            else start_production_jobs(run,owner);
            step_airports(run,field,owner);
            advance_research(run,owner);
            if(cls==12U)advance_terminal_attachment_effects(run,field,false,std::nullopt,owner,true);
            step_combat(run,field,{},{},{},{},owner);
            serviced_model_callbacks->push_back(owner);
            environmental_hulls=simulation_lab_current_shot_hulls(run,field,*runtime_catalog_);
        }
    };

    // Newer model owners service their hierarchy emitters before an older
    // effect rolls its linked child's orientation. Keep that prefix in order
    // without advancing movement and collision callbacks in the effect pass.
    using ShotOrder = std::pair<uint32_t,uint64_t>;
    const bool interleave_effects=serviced_model_emitters&&runtime_catalog_;
    auto next_effect=interleave_effects?run.environmental_effects.size():0U;
    const auto model_precedes_shot=[&](const SimulationProjectileState& shot){
        if(next_model>=model_order.size())return false;
        const auto& model=run.entities[model_order[next_model]];
        if(model.construction_start_tick!=shot.spawn_tick)
            return model.construction_start_tick>shot.spawn_tick;
        // World-job construction is linked after shots launched in the same
        // tick. The captured owner order resolves that tie on the next tick.
        return run.native_world_objects.owner_order(simulation_lab_world_model_handle(model))>
               run.native_world_objects.owner_order(shot.native_world_slot);
    };
    const auto drain_effects_before=[&](std::optional<ShotOrder> shot){
        while(next_effect!=0){
            const auto& effect=run.environmental_effects[next_effect-1];
            const auto effect_tick=effect.spawn_tick;
            if(shot&&ShotOrder{effect_tick,effect.next_projectile_serial_at_creation}<=*shot)break;
            const auto effect_order=run.native_world_objects.owner_order(effect.native_world_slot);
            advance_environmental_effect(run,field,--next_effect,[&](){
                for(std::size_t i=next_model;i<model_order.size();++i){
                    const auto owner=model_order[i];
                    const auto& model=run.entities[owner];
                    if(model.construction_start_tick<effect_tick||
                       (shot&&model.construction_start_tick<=shot->first))break;
                    const auto model_creation_order=run.native_world_objects.owner_order(
                        simulation_lab_world_model_handle(model));
                    if(model.construction_start_tick==effect_tick&&
                       (!effect_order||model_creation_order<=effect_order))break;
                    // Banking emits from the pose produced by its movement callback.
                    if(model.runtime_model_class_id!=17U||!serviced_model_callbacks)
                        service_model_emitter(owner);
                }
            });
        }
    };
    for (const auto projectile_index : projectile_order) {
        if(run.projectiles[projectile_index].impact_damage_applied) continue;
        // An earlier projectile can kill scenery and change its stored angle0
        // while leaving its cached render transform intact. Later owner nodes
        // in this same tick must raycast the new angle, before model release.
        if(runtime_catalog_&&collision_angle_generation!=run.metrics.root_direct_impact_angle_updates){
            environmental_hulls=simulation_lab_current_shot_hulls(run,field,*runtime_catalog_);
            collision_angle_generation=run.metrics.root_direct_impact_angle_updates;
        }
        const ShotOrder shot_order{run.projectiles[projectile_index].spawn_tick,run.projectiles[projectile_index].serial};
        drain_effects_before(shot_order);
        if(run.level_runtime_enabled&&!run.projectiles[projectile_index].impact_damage_applied){
            const auto removed=run.metrics.model_hierarchies_removed;
            advance_terminal_attachment_effects(run,field,true,run.projectiles[projectile_index].serial);
            if(removed!=run.metrics.model_hierarchies_removed&&runtime_catalog_)
                environmental_hulls=simulation_lab_current_shot_hulls(run,field,*runtime_catalog_);
        }
        if (run.projectiles[projectile_index].impact_damage_applied) continue;
        while(model_precedes_shot(run.projectiles[projectile_index]))service_next_model();
        // A serviced model may have appended shots and reallocated the vector.
        auto& projectile=run.projectiles[projectile_index];
        const auto world_scope=run.native_world_objects.enter_owner(projectile.native_world_slot);
        struct ReleaseShotOnExit {
            SimulationLabRun& run;
            size_t index;
            ~ReleaseShotOnExit(){
                auto& shot=run.projectiles[index];
                if(shot.impact_damage_applied)release_projectile_world_slot(run,shot);
            }
        } release{run,projectile_index};
        const bool environmental=projectile.owner_kind==SimulationProjectileOwnerKind::Environmental;
        if(!environmental&&projectile.owner_army_pool_slot==SIZE_MAX&&projectile.owner_placement_index<run.entities.size())
            projectile.owner_army_pool_slot=run.entities[projectile.owner_placement_index].native_army_pool_slot;
        const auto pool_owner=simulation_lab_army_owner(run,projectile.owner_army_pool_slot);
        if(pool_owner.present)projectile.owner_placement_index=pool_owner.entity_index;
        SimulationLabEntity* owner=nullptr;
        const ShotDefinitionRecord* definition=nullptr;
        if((environmental||pool_owner.present)&&runtime_catalog_){
            const auto found=runtime_catalog_->shots.find(projectile.shot_definition_id);
            if(found!=runtime_catalog_->shots.end())definition=&found->second;
        }else if(projectile.owner_placement_index<run.entities.size()){
            owner=&run.entities[projectile.owner_placement_index];
            if(projectile.weapon_profile_index<owner->combat_weapons.size())definition=&owner->combat_weapons[projectile.weapon_profile_index].shot;
        }
        if(pool_owner.present&&pool_owner.entity_index<run.entities.size())owner=&run.entities[pool_owner.entity_index];
        if(!definition){projectile.impact_damage_applied=true;continue;}
        const auto& shot=*definition;
        const bool direct_beam = shot.trajectory_kind() == ShotTrajectoryMode::DirectLine;
        const std::uint32_t age = run.tick - projectile.spawn_tick;
        if(direct_beam){
            if(age==0U)continue;
            if(age>=std::max(1U,projectile.lifetime_ticks)){projectile.impact_damage_applied=true;continue;}
            emit_projectile_trail_effect(run,projectile,shot,age);
        }
        const bool native_flight=runtime_catalog_&&!direct_beam;
        bool native_contact=false;
        if(native_flight){
            if(age==0U)continue;
            for(uint32_t step=0;step<std::max(1U,config_.runtime_callback_step_ticks);++step){
                if(!simulation_begin_projectile_runtime_step(projectile,shot,run.tick))break;
                emit_projectile_trail_effect(run,projectile,shot,projectile.runtime_flight_age);
                native_contact=simulation_lab_refresh_projectile_interval_contact(projectile,shot,run.tick,field,
                    *runtime_catalog_,environmental_hulls,static_shot_hulls_);
                if(native_contact)break;
                const SimulationLabEntity* target=projectile.live_target_placement_index&&*projectile.live_target_placement_index<run.entities.size()
                    ?&run.entities[*projectile.live_target_placement_index]:nullptr;
                if(target&&!simulation_lab_has_live_root_model_owner(*target))target=nullptr;
                const Vec3f aim=target?simulation_lab_target_aim_point(*target,field,*runtime_catalog_):Vec3f{};
                const Vec3f root=target?target->actor.position:Vec3f{};
                simulation_finish_projectile_runtime_step(projectile,shot,target?&aim:nullptr,target?&root:nullptr);
                projectile.native_model_roll_turn16=(projectile.native_model_roll_turn16+
                    uint32_t(shot.model_spin_step_turn16))&0xFFFFU;
                if(projectile.runtime_flight_expired)break;
            }
            if(!native_contact){if(projectile.runtime_flight_expired)projectile.impact_damage_applied=true;continue;}
        }
        const bool beam_recast=direct_beam&&runtime_catalog_;
        const bool model_contact=native_contact || (runtime_catalog_&&!native_flight&&(beam_recast
            ?simulation_lab_refresh_beam_contact(projectile,shot,field,*runtime_catalog_,environmental_hulls,static_shot_hulls_)
            :simulation_lab_refresh_environmental_projectile_contact(projectile,shot,run.tick,*runtime_catalog_,environmental_hulls,static_shot_hulls_)));
        const auto target_index=(environmental||model_contact||beam_recast)?projectile.impact_target_placement_index:projectile.live_target_placement_index;
        const uint32_t owner_faction=pool_owner.present?pool_owner.faction:(owner?owner->faction_index:0U);
        if (target_index && *target_index < run.entities.size() &&
            shot.trajectory_kind() == ShotTrajectoryMode::FixedRange) {
            simulation_update_projectile_live_target(projectile, shot,
                run.entities[*target_index].actor.position);
        }

        if (direct_beam) {
            if(beam_recast&&projectile.impact_kind==SimulationImpactKind::None)continue;
        } else if (!simulation_projectile_impacted(projectile, run.tick)) {
            continue;
        }

        if (!projectile.impact_started) {
            projectile.impact_started = true;
            ++run.metrics.projectile_impacts;
        }
        if (!direct_beam) projectile.impact_damage_applied = true;

        Vec3f impact_position = projectile.impact_kind == SimulationImpactKind::None
            ? projectile.target_position : projectile.impact_position;
        std::uint32_t impact_effect_id = projectile.impact_effect_id;
        if (projectile.impact_kind == SimulationImpactKind::TargetModelImpactClass &&
            target_index && *target_index < run.entities.size()) {
            auto& target = run.entities[*target_index];
            // Retain the projectile's resolved contact point. Replacing it with the
            // actor origin shifts the explosion inside large buildings and makes
            // their bounds appear to be an invisible impact shield.
            const std::uint32_t impact_class = (environmental||model_contact)?projectile.impact_material_index:target.shot_impact_class_index;
            const std::int32_t raw_damage = impact_class < shot.target_impact_damage_q12.size()
                ? shot.target_impact_damage_q12[impact_class] : 0;
            const std::int32_t damage = direct_beam
                ? raw_damage / static_cast<std::int32_t>(std::max(1U, shot.projectile_lifetime_ticks))
                : raw_damage;
            impact_effect_id = shot.target_impact_effect_id(impact_class);
            if(const auto trace=std::getenv("THANDOR_SIMLAB_DAMAGE_TRACE_TICK");trace&&std::strtoul(trace,nullptr,10)==run.tick)
                std::fprintf(stderr,"PROJECTILE_HIT tick=%u shot=%u spawn=%u owner=%zu target=%zu class=%u damage=%d kind=%u xyz=%d,%d,%d\n",
                    run.tick,projectile.shot_definition_id,projectile.spawn_tick,projectile.owner_army_pool_slot,
                    target.native_army_pool_slot,impact_class,damage,unsigned(shot.trajectory_kind()),
                    float_to_q12(impact_position.x),float_to_q12(impact_position.y),float_to_q12(impact_position.z));
            const auto target_faction=target.faction_index;
            const bool damaging_army_hit=(owner||pool_owner.present)&&!shot.is_repair_or_recovery_shot()&&
                owner_faction<run.faction_pair_pressure.size()&&target_faction<run.faction_pair_pressure.size();
            if(run.level_runtime_enabled&&damaging_army_hit)
                run.faction_pair_pressure[target_faction][owner_faction]+=256U;
            const bool foreign_hit=damaging_army_hit&&owner_faction!=0U&&target_faction!=0U&&
                owner_faction!=target_faction;
            const bool hostile_hit=foreign_hit&&
                (run.faction_capability_flags[owner_faction]&(1U<<target_faction))==0U;
            if(run.level_runtime_enabled&&foreign_hit){
                if(hostile_hit){
                    run.faction_pair_change_ticks[owner_faction][target_faction]=run.tick;
                    run.faction_pair_change_ticks[target_faction][owner_faction]=run.tick;
                }else{
                    const bool has_target=((pool_owner.present?pool_owner.command_flags:owner->combat_command_mode_flags)&1U)!=0U;
                    const auto assigned=owner?owner->target_entity_index:std::optional<size_t>{};
                    const bool target_matches=pool_owner.present?pool_owner.target_faction==target_faction:
                        (assigned&&*assigned<run.entities.size()&&run.entities[*assigned].faction_index==target_faction);
                    const auto elapsed=run.tick*2U-
                        run.faction_pair_change_ticks[owner_faction][target_faction]-
                        run.faction_pair_change_ticks[target_faction][owner_faction];
                    if((!has_target||target_matches)&&std::bit_cast<int32_t>(elapsed)>99)
                        simulation_lab_apply_relation_transition(run,target_faction,owner_faction,0U,0U);
                }
            }
            if (hostile_hit||(!run.level_runtime_enabled&&foreign_hit)) {
                const auto hit_x=float_to_q12(target.actor.position.x);
                const auto hit_y=float_to_q12(target.actor.position.y);
                // Original impact classification reads selected root MDL +0x18.
                bool secondary=target.motion.movable(); // legacy synthetic fixture fallback
                if(const auto* product=production_asset(target.army_asset_id)) {
                    const auto selected=simulation_lab_current_ai_score_seed(*product,
                        run.faction_technology_masks[target.faction_index],target.faction_index);
                    if(selected.resolved)secondary=selected.root_definition_value18!=0;
                }
                auto& anchor=secondary ? run.ai_secondary_impact_anchor[target.faction_index]
                                       : run.ai_primary_impact_anchor[target.faction_index];
                if (military_impact_anchor_v599(anchor,hit_x,hit_y))
                    ++run.metrics.impact_anchor_notification_gates;
                ++run.metrics.impact_anchor_updates;
                if (secondary) run.ai_anchor_cooldown0[target.faction_index]=anchor.cooldown;
                else run.ai_primary_anchor_cooldown[target.faction_index]=anchor.cooldown;
            }
            // V707: a repair shot can collide with an actor other than its selected
            // friendly target (a moving enemy can cross its live collision hull).
            // Resolve the ACTUAL impacted owner's faction and regeneration gate before
            // applying signed healing, not just the intended target chosen at launch.
            const bool repair_contact_allowed = !shot.is_repair_or_recovery_shot() ||
                (owner != nullptr && owner_faction != 0U &&
                 target.faction_index == owner_faction &&
                 (target.runtime_flags & 0x400U) == 0U &&
                 simulation_lab_hierarchy_condition_ratio_q12(target) < 4096);
            if (shot.is_repair_or_recovery_shot() && damage < 0 && !repair_contact_allowed)
                ++run.metrics.projectile_repair_contacts_rejected;
            // Native hit/guidance references live until the model is released.
            // A dismantling model has flag 8 while it still has health and a collider.
            if (damage != 0 && repair_contact_allowed) {
                const float dx = impact_position.x - (owner?owner->actor.position:projectile.launch_position).x;
                const float dy = impact_position.y - (owner?owner->actor.position:projectile.launch_position).y;
                // Native impact uses the shot's stored heading. The firing army
                // may have moved since launch, so its current position is unrelated.
                const auto angle = run.level_runtime_enabled
                    ?(projectile.runtime_flight_initialized?uint32_t(projectile.runtime_flight_yaw):projectile.launch_yaw_turn16)
                    :radians_to_turn16(std::atan2(dy, dx));
                const auto before = target.root_model_integrity;
                // ArmyRuntime_ApplyImpactDamageToRuntimeAndParent halves every application:
                // low half to the hit model and the remainder to its parent, or the same model
                // again when the hit model is the root.
                const auto hit_index = (environmental||model_contact)&&projectile.impact_target_model_component_index
                    ?*projectile.impact_target_model_component_index:target.collision_model_component_index;
                std::size_t parent_index = hit_index;
                if (hit_index < target.model_components.size()) {
                    if(hit_index==target.root_model_component_index)
                        ++run.metrics.projectile_root_component_hits;
                    else ++run.metrics.projectile_child_component_hits;
                    const auto& hit = target.model_components[hit_index];
                    if (hit.parent_component_index &&
                        *hit.parent_component_index < target.model_components.size())
                        parent_index = *hit.parent_component_index;
                }
                const std::int32_t first_half = damage >> 1;
                apply_component_damage(run, target, hit_index,
                                       first_half, angle, environmental?std::nullopt:std::optional<uint32_t>{owner_faction});
                apply_component_damage(run, target, parent_index,
                                       damage - first_half, angle, environmental?std::nullopt:std::optional<uint32_t>{owner_faction});
                if (damage > 0 && before > target.root_model_integrity)
                    run.metrics.projectile_damage_applied += static_cast<std::uint64_t>(
                        before - target.root_model_integrity);
                if (shot.is_repair_or_recovery_shot() && damage < 0 &&
                    target.root_model_integrity > before) {
                    ++run.metrics.projectile_repair_contacts_healed;
                    run.metrics.projectile_repair_integrity_restored +=
                        static_cast<std::uint64_t>(target.root_model_integrity - before);
                }
            }
        }
        if (impact_effect_id != 0U && !projectile.impact_effect_emitted) {
            projectile.impact_effect_emitted = true;
            SimulationLabOneShotEvent event;
            event.tick = run.tick;
            event.source_placement_index = owner?owner->source_placement_index:SIZE_MAX;
            event.army_asset_id = owner?owner->army_asset_id:0U;
            event.faction_index = owner_faction;
            event.kind = SimulationLabOneShotEventKind::WeaponImpactEffect;
            event.effect_id = impact_effect_id;
            event.root_world_position = impact_position;
            if(run.level_runtime_enabled)initialize_projectile_impact_effect(run,projectile,shot,event);
            if(owner)event.actor = owner->actor;
            append_one_shot_event(run, std::move(event));
            ++run.metrics.weapon_impact_effect_events;
        }
    }
    if(interleave_effects){
        drain_effects_before(std::nullopt);
        prune_environmental_effects(run);
    }
    const auto expired=[&](const SimulationProjectileState& projectile) {
        return simulation_projectile_expired(projectile, run.tick) ||
               (projectile.impact_damage_applied && run.tick > projectile.spawn_tick + projectile.travel_ticks + std::max(2U,projectile.impact_visual_lifetime_ticks));
    };
    for(auto& projectile:run.projectiles)
        if(expired(projectile)){
            const auto world_scope=run.native_world_objects.enter_owner(projectile.native_world_slot);
            release_projectile_world_slot(run,projectile);
        }
    std::erase_if(run.projectiles,expired);
}

void SimulationLabExperiment::advance_damage_effects(SimulationLabRun& run,const FieldGrid& field,
    size_t owner,unsigned calls,std::optional<std::array<uint32_t,3>> retained_root_angles){
    if(!run.level_runtime_enabled||!runtime_catalog_||owner>=run.entities.size())return;
    auto& entity=run.entities[owner];
    const auto world_scope=run.native_world_objects.enter_owner(simulation_lab_world_model_handle(entity));
    if(!simulation_lab_has_live_root_model_owner(entity)||!entity.runtime_model_hierarchy||
       entity.runtime_model_hierarchy->empty()||(entity.runtime_flags&0x210U))return;
    const auto& root=entity.runtime_model_hierarchy->front();
    const auto& profile=root.simulation;
    if(profile.maximum_integrity<=0||
       int64_t(entity.root_model_integrity)*100/profile.maximum_integrity>=
           profile.damage_effect_health_threshold_percent)return;
    for(unsigned call=0;call<calls;++call){
        if(entity.damage_effect_cooldown>0){
            entity.damage_effect_cooldown-=static_cast<int32_t>(config_.runtime_callback_step_ticks);
            continue;
        }
        const auto effect=runtime_catalog_->effects.find(profile.damage_effect_id);
        if(effect==runtime_catalog_->effects.end())continue;
        const auto random_ticks=profile.damage_effect_random_cooldown_ticks;
        entity.damage_effect_cooldown=static_cast<int32_t>(profile.damage_effect_min_cooldown_ticks+
            (random_ticks?random_next(run.rng_state)%random_ticks:0U));
        auto position=entity.actor.position;
        if(const auto sprite=runtime_catalog_->sprites.find(root.sprite_path);
           sprite!=runtime_catalog_->sprites.end()){
            const auto& points=sprite->second.damage_effect_points;
            const auto find_point=[&](int32_t index){
                return std::find_if(points.begin(),points.end(),[&](const auto& p){
                    return p.first==static_cast<uint32_t>(index);
                });
            };
            auto point=find_point(entity.damage_effect_point);
            if(point==points.end()){
                entity.damage_effect_point=-1;
                point=find_point(0);
            }
            if(point!=points.end()){
                const auto poses=simulation_lab_current_model_poses(entity,field,*runtime_catalog_);
                if(!poses.empty()){
                    auto pose=poses.front();
                    if(retained_root_angles){
                        const auto& angles=*retained_root_angles;
                        pose.native_basis=retail_model_basis_q28(angles[0],angles[1],angles[2]);
                    }
                    const auto local=point->second;
                    position=simulation_lab_model_world_point(pose,local);
                }
                ++entity.damage_effect_point;
            }
        }
        const auto angles=random_next(run.rng_state);
        const auto elevation=(random_next(run.rng_state)&0x1FFFU)+0x1FFFU;
        const auto roll=(effect->second.definition.creation_flags&1U)
            ?random_next(run.rng_state)&0xFFFFU:angles>>16;
        SimulationLabOneShotEvent event;
        event.tick=run.tick;
        event.source_placement_index=entity.source_placement_index;
        event.army_asset_id=entity.army_asset_id;
        event.faction_index=entity.faction_index;
        event.kind=SimulationLabOneShotEventKind::ModelTimedEffect;
        event.effect_id=profile.damage_effect_id;
        event.root_world_position=position;
        event.effect_world_angles=std::array<uint32_t,3>{angles&0xFFFFU,elevation,roll};
        event.actor=entity.actor;
        SimulationLabEffectPose effect_pose;
        effect_pose.position=position;
        effect_pose.native_angles=std::array<uint32_t,3>{roll,elevation,angles&0xFFFFU};
        // A building factory calls the damage emitter from the timed emitter,
        // then the child animation, then the factory callback's epilogue.
        // Only that last call follows creation of the completed army.
        link_environmental_effect(run,event.effect_id,effect_pose,true,std::nullopt,
            entity.runtime_model_class_id==13U&&call+1U<calls,
            entity.runtime_model_class_id==13U&&call+1U==calls);
        append_one_shot_event(run,std::move(event));
    }
}

void SimulationLabExperiment::step_combat(SimulationLabRun& run, const FieldGrid& field,
                                           std::span<const DeferredEmitter> deferred_emitters,
                                           std::span<const SimulationLabLiveShotHull> ground_hulls_before_movement,
                                           std::span<const std::size_t> deferred_ambient_emitters,
                                           std::span<const SimulationLabLiveShotHull> animated_hulls_before_callbacks,
                                           std::optional<std::size_t> only_owner,
                                           std::span<const std::size_t> serviced_owners) {

    // New projectiles precede the older ground model owners in the native
    // callback list. Damage smoke can begin on the impact tick.
    struct EffectOwner {std::size_t index;bool ambient;std::optional<bool> root_timed_active{};};
    std::vector<EffectOwner> effect_owners;
    if(run.level_runtime_enabled&&runtime_catalog_)for(const auto& emitter:deferred_emitters)effect_owners.push_back({emitter.index,false,emitter.root_timed_active});
    if(run.level_runtime_enabled&&runtime_catalog_)for(const auto index:deferred_ambient_emitters)effect_owners.push_back({index,true});
    if(run.level_runtime_enabled&&runtime_catalog_)for(std::size_t i=0;i<run.entities.size();++i){
        if((only_owner&&i!=*only_owner)||std::find(serviced_owners.begin(),serviced_owners.end(),i)!=serviced_owners.end())continue;
        const auto cls=run.entities[i].runtime_model_class_id;
        if(cls==1U||cls==2U||cls==3U||cls==17U||cls==18U||cls==19U)
            effect_owners.push_back({i,false});
    }
    const auto effect_order=[&](const EffectOwner& owner){
        if(owner.ambient)return std::tuple{false,0U,
            environmental_emitters_[owner.index].source_placement_index,owner.index};
        return simulation_lab_owner_order(run.entities[owner.index],owner.index);
    };
    std::sort(effect_owners.begin(),effect_owners.end(),[&](const auto& a,const auto& b){
        return effect_order(a)>effect_order(b);
    });
    size_t next_effect=0;
    const auto service_effect=[&](const EffectOwner& callback,
            std::optional<std::array<uint32_t,3>> retained_root_angles){
        const auto owner=callback.index;
        if(callback.ambient){advance_environmental_emitters(run,field,owner);return;}
        auto& entity=run.entities[owner];
        if(entity.runtime_model_class_id==17U){
            if(!entity.synthetic||entity.construction_start_tick<run.tick)
                advance_production_emitters(run,field,owner,std::nullopt,false,retained_root_angles);
            return;
        }
        if(entity.runtime_model_class_id!=1U&&entity.runtime_model_class_id!=2U&&entity.runtime_model_class_id!=3U&&entity.runtime_model_class_id!=18U&&entity.runtime_model_class_id!=19U){
            advance_production_emitters(run,field,owner,callback.root_timed_active);
            if(entity.runtime_model_class_id==12U)
                advance_terminal_attachment_effects(run,field,false,std::nullopt,owner,true);
            return;
        }
        if((entity.runtime_model_class_id!=1U&&entity.runtime_model_class_id!=2U&&entity.runtime_model_class_id!=3U&&entity.runtime_model_class_id!=18U&&entity.runtime_model_class_id!=19U)||
           !simulation_lab_has_live_root_model_owner(entity)||!entity.runtime_model_hierarchy||
           entity.runtime_model_hierarchy->empty())return;
        advance_production_emitters(run,field,owner,std::nullopt,true,retained_root_angles);
        const bool moved=entity.last_translated||entity.last_turned;
        // Class 3 emits once before planning/contact work; only the moved
        // timed-emitter callback contributes the second call afterwards.
        advance_damage_effects(run,field,owner,
            entity.runtime_model_class_id==3U?(moved?1U:0U):(moved?2U:1U),retained_root_angles);

    };
    const auto drain_effects=[&](size_t index,bool inclusive,
            std::optional<std::array<uint32_t,3>> retained_root_angles=std::nullopt){
        const auto order=simulation_lab_owner_order(run.entities[index],index);
        while(next_effect<effect_owners.size()&&
              (inclusive?effect_order(effect_owners[next_effect])>=order:
                         effect_order(effect_owners[next_effect])>order)){
            const auto& effect=effect_owners[next_effect++];
            service_effect(effect,!effect.ambient&&effect.index==index?retained_root_angles:std::nullopt);
        }
    };
    if(run.level_runtime_enabled){
        for(auto& entity:run.entities)
            entity.ground_aim_before_movement=simulation_lab_ground_aim_snapshot(entity);
    }
    // Existing projectiles can reset the health timer before the native model callback.
    const auto advance_health=[&](SimulationLabEntity& entity){
        if (!entity.construction_complete || (run.level_runtime_enabled&&entity.synthetic&&entity.construction_start_tick>=run.tick)) return;
        bool health_changed = false;
        for (auto& component : entity.model_components) {
            const auto component_scope=run.native_world_objects.enter_component(
                static_cast<size_t>(&component-entity.model_components.data()),-2);
            const auto before = component.integrity;
            if(component.runtime_removed)continue;
            simulation_lab_advance_component_health(component);
            if(run.level_runtime_enabled&&runtime_catalog_){
                const auto definition=runtime_catalog_->definitions.find(component.model_definition_id);
                if(definition!=runtime_catalog_->definitions.end()){
                    const auto& profile=definition->second.simulation;
                    const auto dismantle=simulation_lab_advance_component_dismantling(component,
                        profile.retail_xenite_cost_q4,config_.runtime_callback_step_ticks);
                    if(entity.faction_index<run.faction_economy.size())
                        run.faction_economy[entity.faction_index].xenite_current_q4+=dismantle.refund_q4;
                    if(dismantle.advanced&&component.runtime_class_id==22U){
                        uint32_t value=0;
                        for(size_t slot=0;slot<std::min<size_t>(entity.production_storage_capacity,
                            entity.production_linked_child_asset_ids.size());++slot){
                            if(entity.production_linked_child_slot_mask&(1U<<slot))continue;
                            const auto id=entity.production_linked_child_asset_ids[slot];
                            const auto army=runtime_catalog_->armies.find(id);
                            if(army==runtime_catalog_->armies.end())continue;
                            const auto nodes=resolve_faction_technology_hierarchy(army->second,
                                run.faction_technology_masks[entity.faction_index]);
                            for(const auto& node:nodes)if(node.parent_index==UINT32_MAX){
                                value+=node.simulation.retail_xenite_cost_q4;break;
                            }
                        }
                        run.faction_economy[entity.faction_index].xenite_current_q4+=value>>5;
                    }
                    if(dismantle.completed){
                        if(&component==&entity.model_components[entity.root_model_component_index])
                            entity.runtime_flags=(entity.runtime_flags&~0x210U)|(component.runtime_flags_ec&0x230U);
                        const auto effect=runtime_catalog_->effects.find(profile.construction_destruction_effect_id);
                        if(effect!=runtime_catalog_->effects.end()){
                            const auto poses=simulation_lab_current_model_poses(entity,field,*runtime_catalog_);
                            const auto n=component.source_model_node_index;
                            if(n<poses.size()){
                                const auto& pose=poses[n];const auto& d=effect->second.definition;
                                auto ticks=effect_model_completion_tick(d);
                                if(d.transition_kind==4U&&d.completion_countdown_ticks==1U&&effect_lifetime_ticks(d)>1U){
                                    const auto normal=retail_contact_normal_angles(field,float_to_q12(pose.position.x),float_to_q12(pose.position.y));
                                    const auto angles=pose.native_angles.value_or(std::array<uint32_t,3>{entity.actor.body_yaw_turn16,0x4000U,0U});
                                    if(normal){const auto motion=retail_direction_scaled(angles[1]-d.terrain_pitch_drop_per_age_squared,angles[2],268435456);
                                        const auto surface=retail_direction_scaled(*normal>>16,*normal&65535U,268435456);
                                        const int64_t dot=int64_t(motion[0])*surface[0]+int64_t(motion[1])*surface[1]+int64_t(motion[2])*surface[2];
                                        if(dot>=0)ticks=1U;
                                    }
                                }
                                if(ticks){component.destruction_complete_tick=run.tick+*ticks;
                                    component.destruction_effect_after_projectile_serial=run.next_projectile_serial-1U;}
                                SimulationLabOneShotEvent event;event.tick=run.tick;
                                event.source_placement_index=entity.source_placement_index;
                                event.army_asset_id=entity.army_asset_id;event.faction_index=entity.faction_index;
                                event.kind=SimulationLabOneShotEventKind::TerminalAttachmentEffect;
                                event.effect_id=profile.construction_destruction_effect_id;
                                event.source_model_node_index=n;event.root_world_position=pose.position;
                                event.actor=entity.actor;event.terminal_attachment_world_points.push_back({pose.position,pose.basis});
                                if(d.creation_flags&1U)(void)random_next(run.rng_state);
                                append_one_shot_event(run,std::move(event));
                                const bool has_callbacks=effect_linked_effect_spawn_tick(d)||
                                    !effect->second.emissions.empty();
                                if(has_callbacks||run.level_runtime_enabled){
                                    const auto handle=link_environmental_effect(run,d.definition_id,pose,!has_callbacks);
                                    if(ticks)component.destruction_effect_world_handle=handle;
                                }
                            }
                        }
                    }
                }
            }
            health_changed |= component.integrity != before;
        }
        if (health_changed) sync_root_component_summary(entity);
    };
    if(!run.level_runtime_enabled)for(auto& entity:run.entities)advance_health(entity);
    if(!run.level_runtime_enabled)advance_projectiles(run,field);
    const std::size_t entity_count = run.entities.size();
    // Share one geometry snapshot across this tick's weapon LOS tests, not one rebuild per weapon.
    std::optional<std::vector<SimulationLabLiveShotHull>> line_hulls;
    const auto ground_owner_pending=[&](size_t target,size_t source){
        return run.level_runtime_enabled&&target<entity_count&&
            run.entities[target].ground_aim_before_movement&&
            simulation_lab_owner_order(run.entities[target],target)<
                simulation_lab_owner_order(run.entities[source],source);
    };
    std::vector<std::optional<std::vector<SimulationLabLiveShotHull>>> owner_line_hulls(entity_count);
    std::vector<bool> animated_owner(entity_count);
    for(const auto& hull:animated_hulls_before_callbacks)
        if(hull.entity_index<entity_count)animated_owner[hull.entity_index]=true;
    const auto animated_owner_pending=[&](size_t target,size_t source){
        return target<entity_count&&animated_owner[target]&&
            simulation_lab_owner_order(run.entities[target],target)<
                simulation_lab_owner_order(run.entities[source],source);
    };
    const auto hulls_for_owner=[&](size_t source)->std::span<const SimulationLabLiveShotHull>{
        if(!line_hulls)line_hulls=simulation_lab_current_shot_hulls(run,field,*runtime_catalog_);
        if(!run.level_runtime_enabled||(ground_hulls_before_movement.empty()&&animated_hulls_before_callbacks.empty()))return *line_hulls;
        auto& cached=owner_line_hulls[source];
        if(!cached){
            auto& mixed=cached.emplace();mixed.reserve(line_hulls->size());
            // Every older ground owner is still at its pre-callback pose,
            // including foreground blockers other than the selected target.
            for(const auto& hull:*line_hulls)
                if(!ground_owner_pending(hull.entity_index,source)&&!animated_owner_pending(hull.entity_index,source))mixed.push_back(hull);
            for(const auto& hull:ground_hulls_before_movement)
                if(ground_owner_pending(hull.entity_index,source)&&!animated_owner_pending(hull.entity_index,source))mixed.push_back(hull);
            // Older animated MODEL owners still expose their pre-callback SPR
            // pose, just as older moving owners expose their previous position.
            for(const auto& hull:animated_hulls_before_callbacks)
                if(animated_owner_pending(hull.entity_index,source))mixed.push_back(hull);
        }
        return *cached;
    };
    const bool scored_targeting=run.level_runtime_enabled && runtime_catalog_;
    std::vector<SimulationLabCombatMetrics> combat_metrics;
    if(scored_targeting){combat_metrics.reserve(entity_count);for(const auto& e:run.entities)combat_metrics.push_back(simulation_lab_combat_metrics(e));}
    // AiCombatTarget_EvaluateCandidateScore performs the primary/secondary
    // weapon obstruction test only for a candidate whose numeric score could
    // become the new winner. The selector requests rays after that gate.
    const auto line_blocked_for=[&](std::size_t source_index,std::size_t target_index){
        auto& source=run.entities[source_index];
        const auto& target=run.entities[target_index];
        if(!source.runtime_model_hierarchy||source.runtime_model_hierarchy->empty()||
           !target.runtime_model_hierarchy||target.runtime_model_hierarchy->empty())return false;
        if(source.actor.weapons.size()<source.combat_weapons.size())
            source.actor.weapons.resize(source.combat_weapons.size());
        std::optional<std::size_t> weapon_index;
        for(std::size_t i=0;i<source.combat_weapons.size();++i){
            const auto& seed=source.combat_weapons[i];
            // Native candidate scoring uses the retained primary attachment even
            // while power/state gates suppress that weapon's firing callback.
            if(seed.profile.valid()&&
               simulation_lab_model_node_present(source,seed.profile.model_node_index)){
                weapon_index=i;break;
            }
        }
        if(!weapon_index)return false;
        const auto& seed=source.combat_weapons[*weapon_index];
        const auto geometry=simulation_lab_weapon_geometry(source,seed.profile,
            source.actor.weapons[*weapon_index],field,*runtime_catalog_);
        // Candidate coordinates aim the ray, but native range adjustment
        // and hit ownership still read the source's current command target.
        const auto command_target=(source.combat_command_mode_flags&1U)
            ?source.target_entity_index:std::optional<std::size_t>{};
        auto aim=simulation_lab_target_aim_geometry(target,field,*runtime_catalog_).selected;
        if(ground_owner_pending(target_index,source_index)){
            const auto& previous=target.ground_aim_before_movement->position;
            aim.x=previous.x;
            aim.y=previous.y;
            aim.z+=previous.z-target.actor.position.z;
        }
        return !simulation_lab_weapon_line_of_fire_clear(
            run,source_index,command_target,geometry.target_resolution_origin,aim,
            seed.profile,seed.shot,field,hulls_for_owner(source_index),static_shot_hulls_);
    };
    std::vector<std::size_t> weapon_owners;
    if(only_owner)weapon_owners.push_back(*only_owner);
    else{
        weapon_owners.resize(entity_count);
        std::iota(weapon_owners.begin(),weapon_owners.end(),0U);
        if(run.level_runtime_enabled)
            std::sort(weapon_owners.begin(),weapon_owners.end(),[&](auto a,auto b){
                return simulation_lab_owner_order(run.entities[a],a)>simulation_lab_owner_order(run.entities[b],b);
            });
    }
    const auto finish_owner=[&](size_t index){
        auto& entity=run.entities[index];
        if(run.level_runtime_enabled&&!(entity.synthetic&&entity.construction_start_tick>=run.tick))
            for(auto& component:entity.model_components)
                if(!component.runtime_removed)component.last_model_callback_tick=run.tick;
        const bool service_command=scored_targeting&&!(entity.synthetic&&entity.construction_start_tick>=run.tick);
        if(service_command)
            simulation_lab_service_combat_command(run,field,index,combat_metrics,
                std::max(1U,config_.runtime_callback_step_ticks),{},
                [&](std::size_t target){return line_blocked_for(index,target);});
        if(entity.production_movement_unlock_pending){
            entity.combat_movement.flags&=~2U;
            entity.production_movement_unlock_pending=false;
        }
        if(service_command){
            auto& movement=entity.pioneer_movement_initialized&&!entity.production_linked_to_parent
                ?entity.pioneer_movement:entity.combat_movement;
            if(movement.retry)--movement.retry;
        }
    };
    for (const auto entity_index : weapon_owners) {
        if((only_owner&&entity_index!=*only_owner)||
           std::find(serviced_owners.begin(),serviced_owners.end(),entity_index)!=serviced_owners.end())continue;
        const auto world_scope=run.native_world_objects.enter_owner(
            simulation_lab_world_model_handle(run.entities[entity_index]));
        // Native visits root movement, effects and weapon children together,
        // newest owner first. A newer weapon route can affect an older root.
        drain_effects(entity_index,false);
        auto& owner=run.entities[entity_index];
        std::optional<std::array<uint32_t,3>> retained_root_angles;
        if(owner.runtime_model_class_id==1U||owner.runtime_model_class_id==2U||
           owner.runtime_model_class_id==17U||owner.runtime_model_class_id==18U||owner.runtime_model_class_id==19U)
            retained_root_angles=owner.actor.ground_recoil_angles.value_or(
                std::array<uint32_t,3>{owner.actor.body_yaw_turn16,
                    owner.placement_normal_angles.value_or(0x40000000U)>>16,
                    owner.placement_normal_angles.value_or(0U)&0xFFFFU});
        if(run.level_runtime_enabled&&runtime_catalog_){
            if(simulation_lab_has_live_root_model_owner(owner)&&
               !(owner.synthetic&&owner.construction_start_tick>=run.tick))
                advance_class23_collision_state(run,entity_index);
            step_movement(run,field,entity_index);line_hulls.reset();
        }
        drain_effects(entity_index,true,retained_root_angles);
        if(run.level_runtime_enabled&&runtime_catalog_)
            simulation_lab_flush_factory_exit_routes(run,field,entity_index,static_navigation_blockers_,
                lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get()));
        if(run.level_runtime_enabled){
            advance_health(owner);
            // Terminal attachment channels belong to this model callback.
            // Effects linked here must precede shots from older model owners.
            if(!(owner.synthetic&&owner.construction_start_tick>=run.tick)&&
               std::any_of(owner.model_components.begin(),owner.model_components.end(),[](const auto& c){
                   return c.integrity<=0&&c.terminal_self_reference&&!c.runtime_removed;
               }))
                advance_terminal_attachment_effects(run,field,false,std::nullopt,entity_index);
        }
        auto& entity=run.entities[entity_index];
        if (!simulation_lab_has_live_root_model_owner(entity) ||
            !entity.construction_complete || entity.root_model_terminal ||
            !simulation_lab_command_target_owner_available(entity)||
            (run.level_runtime_enabled&&entity.synthetic&&entity.construction_start_tick>=run.tick)||
            entity.combat_weapons.empty() ||
            entity.faction_index == 0U ||
            (entity.runtime_model_class_id == 18U && (entity.runtime_flags & 0x08U) != 0U)) {
            finish_owner(entity_index);continue;
        }
        if (entity.actor.weapons.size() < entity.combat_weapons.size())
            entity.actor.weapons.resize(entity.combat_weapons.size());
        const bool had_army_command=(entity.combat_command_mode_flags&1U)!=0U;
        const bool had_position_command=(entity.combat_command_mode_flags&2U)!=0U&&
            entity.combat_command_target_position_world.has_value();
        const auto weapon_callback_runs=[&](const auto& weapon) {
            return simulation_lab_weapon_callback_active(entity,weapon.profile)&&
                weapon.profile.valid()&&simulation_lab_model_node_present(entity,weapon.profile.model_node_index);
        };
        // ResolveShotAimPoint belongs to a powered, live weapon callback.
        // When every attachment is switched off, keep the existing target
        // and its command generation even if the retained fog image changes.
        const bool resolve_army_command=had_army_command&&(!run.level_runtime_enabled||
            std::any_of(entity.combat_weapons.begin(),entity.combat_weapons.end(),weapon_callback_runs));
        const auto command_target=resolve_army_command?
            simulation_lab_visible_command_target(run,field,entity_index):std::optional<size_t>{};
        const auto command_position=had_position_command?
            entity.combat_command_target_position_world:std::optional<Vec3f>{};
        // Losing visibility clears the command target during the weapon
        // callback. Candidate LOS must observe that cleared command too.
        const bool local_target_search=!scored_targeting||!entity.runtime_model_hierarchy||
            !simulation_lab_combat_command_class(entity);
        const auto target_index = !local_target_search?std::optional<size_t>{}:scored_targeting
            ?simulation_lab_select_combat_target(
                run,field,entity_index,combat_metrics,false,{},
                [&](std::size_t target){return line_blocked_for(entity_index,target);}).entity_index
            :nearest_enemy(run,entity_index);
        if ((!scored_targeting || !simulation_lab_combat_command_class(entity))&&
            !had_army_command&&!had_position_command)
            entity.target_entity_index = target_index;
        entity.actor.target_in_range = false;
        entity.actor.target_aligned = false;
        for (std::size_t weapon_index = 0; weapon_index < entity.combat_weapons.size(); ++weapon_index) {
            const auto& weapon_seed = entity.combat_weapons[weapon_index];
            const auto component_scope=run.native_world_objects.enter_component(
                lab_world_model_component(entity,weapon_seed.profile.model_node_index).value_or(entity.root_model_component_index));
            if (!weapon_callback_runs(weapon_seed))continue;
            const bool repair_weapon=weapon_seed.shot.is_repair_or_recovery_shot();
            // Entity commands use the selected owner for both damage and repair
            // weapons; the signed impact channel below still enforces whether
            // that target can actually be affected.  Explicit-position commands
            // carry no entity owner and aim every armed selected runtime at the
            // stored world point, matching commandTarget.targetFlags bit 2.
            // Native ResolveShotAimPoint reads the active command target, not
            // a freshly scored candidate while command acquisition is waiting.
            const auto weapon_target=had_army_command?command_target:
                (run.level_runtime_enabled&&entity.runtime_model_hierarchy&&
                 simulation_lab_combat_command_class(entity)
                    ?std::optional<size_t>{}:target_index);
            const bool entity_command_aim=had_army_command&&command_target.has_value();
            const bool position_command_aim=had_position_command&&command_position.has_value();
            const bool command_aim=entity_command_aim||position_command_aim;
            const auto* target=weapon_target?&run.entities[*weapon_target]:nullptr;
            // Native ResolveShotAimPoint honors every visible active army
            // command, including friendly/self targets from retained WS03.
            // Combat candidate eligibility must not erase that command's aim.
            if(target&&!(run.level_runtime_enabled&&entity_command_aim)) {
                const bool friendly=target->faction_index==entity.faction_index;
                const bool eligible=target->shot_impact_class_index<8U &&
                    (repair_weapon ? (friendly&&!target->root_model_terminal&&
                         simulation_lab_hierarchy_condition_ratio_q12(*target)<4096&&
                         weapon_seed.shot.target_impact_damage_q12[target->shot_impact_class_index]<0)
                                   : (!friendly&&weapon_seed.shot.target_impact_damage_q12[target->shot_impact_class_index]>0));
                if(!eligible)target=nullptr;
            }
            const auto target_geometry=target&&runtime_catalog_&&target->runtime_model_hierarchy?
                std::optional{simulation_lab_target_aim_geometry(*target,field,*runtime_catalog_)}:
                std::optional<SimulationLabTargetAimGeometry>{};
            // Model owners run newest-first in native. A shooter observes an
            // older ground target before that target's movement callback.
            const auto* previous_target=run.level_runtime_enabled&&target_geometry&&
                ground_owner_pending(*weapon_target,entity_index)
                ?&*target->ground_aim_before_movement:nullptr;
            Vec3f current_target_position=target?(target_geometry?
                target_geometry->selected:target->actor.position):
                command_position.value_or(Vec3f{});
            float target_root_aim_z=target_geometry?target_geometry->root_aim_z:0.0F;
            if(previous_target){
                current_target_position.x=previous_target->position.x;
                current_target_position.y=previous_target->position.y;
                const auto height_delta=previous_target->position.z-target->actor.position.z;
                current_target_position.z+=height_delta;
                target_root_aim_z+=height_delta;
            }
            // Aim from the live pivot and launch from the selected kind-2 muzzle,
            // rather than placing every shot at the ground-level army root.
            const bool live_geometry=runtime_catalog_&&entity.runtime_model_hierarchy;
            if(run.level_runtime_enabled&&live_geometry){
                auto& state=entity.actor.weapons[weapon_index];
                simulation_lab_step_barrel_recoil(entity,weapon_seed.profile,state,*runtime_catalog_,
                    weapon_seed.profile.runtime_class_id==8U||state.recoil_translation_ticks>0U?-1:0);
            }
            auto geometry=live_geometry?simulation_lab_weapon_geometry(entity,weapon_seed.profile,
                entity.actor.weapons[weapon_index],field,*runtime_catalog_):SimulationLabWeaponGeometry{};
            if(!live_geometry){geometry.target_resolution_origin=geometry.aim_origin=entity.actor.position;geometry.launch_points={entity.actor.position};geometry.available_selector_mask=UINT32_MAX;}
            // Resolve lead only after the live muzzle/pivot and selected target
            // model point are known. LOS, turret tracking and shot launch all use
            // the same endpoint; collision still tests real moving SPR geometry.
            const Vec3f target_position=target?simulation_lab_moving_target_aim_point(
                *target,run.level_runtime_enabled&&weapon_seed.profile.runtime_class_id>=7U&&
                    weapon_seed.profile.runtime_class_id<=9U?geometry.target_resolution_origin:geometry.aim_origin,
                current_target_position,weapon_seed.shot,
                target_geometry?std::optional<float>{target_root_aim_z}:std::nullopt,
                previous_target):current_target_position;
            const bool aim_available=target!=nullptr||position_command_aim;
            // Native turret callbacks 7/8/9 service the owner's waypoint state
            // when no aim point exists, including idle and factory-exit orders.
            // Keep this in the weapon callback for every root movement class.
            if(run.level_runtime_enabled && !aim_available &&
               weapon_seed.profile.runtime_class_id>=7U && weapon_seed.profile.runtime_class_id<=9U)
                simulation_lab_prepare_follow_motion(run,field,entity_index,static_navigation_blockers_,
                    lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get()));
            bool line_clear=true;
            if(command_aim&&!live_geometry)line_clear=weapon_seed.shot.trajectory_mode==2||
                simulation_target_within_range(geometry.aim_origin,target_position,weapon_seed.profile);
            if(aim_available&&live_geometry&&
               (command_aim||simulation_target_within_range(
                    geometry.aim_origin,target_position,weapon_seed.profile))){
                if(!line_hulls)line_hulls=simulation_lab_current_shot_hulls(run,field,*runtime_catalog_);
                const auto ray_hulls=hulls_for_owner(entity_index);
                line_clear=simulation_lab_weapon_line_of_fire_clear(run,entity_index,weapon_target,
                    run.level_runtime_enabled&&live_geometry&&weapon_seed.profile.runtime_class_id>=7U&&
                        weapon_seed.profile.runtime_class_id<=9U?geometry.target_resolution_origin:geometry.aim_origin,
                    target_position,weapon_seed.profile,weapon_seed.shot,
                    field,ray_hulls,static_shot_hulls_);
            }
            if(const auto trace=std::getenv("THANDOR_SIMLAB_WEAPON_TRACE_TICK");trace&&std::strtoul(trace,nullptr,10)==run.tick){
                std::fprintf(stderr,"WEAPON_RAY tick=%u source=%zu target=%zu actor=%d,%d current=%d,%d aim=%d,%d,%d origin=%d,%d,%d clear=%u\n",run.tick,entity.native_army_pool_slot,target?target->native_army_pool_slot:SIZE_MAX,target?float_to_q12(target->actor.position.x):0,target?float_to_q12(target->actor.position.y):0,float_to_q12(current_target_position.x),float_to_q12(current_target_position.y),float_to_q12(target_position.x),float_to_q12(target_position.y),float_to_q12(target_position.z),float_to_q12(geometry.target_resolution_origin.x),float_to_q12(geometry.target_resolution_origin.y),float_to_q12(geometry.target_resolution_origin.z),unsigned(line_clear));
            }
            // Maintain reload and recoil even when no eligible enemy is selected.
            auto launch_yaw=entity.actor.weapons[weapon_index].yaw.angle_turn16;
            auto launch_pitch=entity.actor.weapons[weapon_index].pitch.angle_turn16;
            auto result = simulation_tick_weapon(entity.actor, entity.actor.weapons[weapon_index],
                weapon_seed.profile, weapon_seed.shot, geometry.aim_origin,
                aim_available?&target_position:nullptr, line_clear,geometry.available_selector_mask,
                live_geometry?std::span<const float>(geometry.yaw_basis):std::span<const float>{},command_aim,
                run.level_runtime_enabled&&live_geometry&&
                    weapon_seed.profile.runtime_class_id>=7U&&weapon_seed.profile.runtime_class_id<=9U,
                geometry.native_yaw_angles);
            if(entity_command_aim&&target&&result.ready_to_fire)
                simulation_lab_weapon_follow(run,field,entity_index,weapon_index,target_position,line_clear,
                    static_navigation_blockers_,lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get()));
            entity.actor.target_in_range |= result.in_range;
            entity.actor.target_aligned |= result.aligned;
            if (!result.fired||!aim_available) continue;
            auto& weapon_state=entity.actor.weapons[weapon_index];
            weapon_state.recoil_launch_selector=result.fired_attachment_selector!=UINT32_MAX?
                result.fired_attachment_selector:weapon_state.next_launch_attachment;
            if(run.level_runtime_enabled&&live_geometry)
                simulation_lab_step_barrel_recoil(entity,weapon_seed.profile,weapon_state,*runtime_catalog_,
                    std::max(0,weapon_seed.profile.recoil_duration_ticks));
            if(run.level_runtime_enabled&&entity.motion.ground_speed_control&&live_geometry&&
               (weapon_seed.profile.runtime_class_id>=7U&&weapon_seed.profile.runtime_class_id<=9U)){
                const auto& node=(*entity.runtime_model_hierarchy)[weapon_seed.profile.yaw_model_node_index];
                if(node.simulation.action_vector0&&node.simulation.action_vector1){
                    entity.actor.ground_recoil_ticks=node.simulation.action_vector0;
                    entity.actor.ground_recoil_tilt_per_tick=node.simulation.action_vector1;
                    entity.actor.ground_recoil_heading=fixed_atan2_angle16_compat(
                        float_to_q12(target_position.y)-float_to_q12(geometry.aim_origin.y),
                        float_to_q12(target_position.x)-float_to_q12(geometry.aim_origin.x));
                }
            }
            if(live_geometry){
                const bool native_barrel_launch=run.level_runtime_enabled&&
                    weapon_seed.profile.runtime_class_id>=7U&&weapon_seed.profile.runtime_class_id<=9U;
                // The native launch rebuild starts at the barrel's parent and
                // reuses that parent's cached world transform from before aiming.
                if(native_barrel_launch){
                    std::swap(weapon_state.yaw.angle_turn16,launch_yaw);
                    std::swap(weapon_state.pitch.angle_turn16,launch_pitch);
                }
                geometry=simulation_lab_weapon_geometry(entity,weapon_seed.profile,weapon_state,field,*runtime_catalog_);
                if(native_barrel_launch){
                    std::swap(weapon_state.yaw.angle_turn16,launch_yaw);
                    std::swap(weapon_state.pitch.angle_turn16,launch_pitch);
                }
            }
            if(geometry.launch_points.empty())continue;
            auto volley = simulation_create_volley(entity_index, weapon_index, weapon_seed.profile,
                geometry.launch_points, target_position, weapon_seed.shot, run.tick,
                run.next_projectile_serial, result.fired_attachment_selector,
                target?weapon_target:std::optional<std::size_t>{},
                entity.synthetic ? SimulationProjectileOwnerKind::ProducedClass13
                                 : SimulationProjectileOwnerKind::Placement,
                entity_index + 1U,
                run.level_runtime_enabled&&live_geometry&&
                    (weapon_seed.profile.runtime_class_id==7U||weapon_seed.profile.runtime_class_id==8U)
                    ?std::span<const Vec3f>(geometry.launch_target_offsets):std::span<const Vec3f>{},
                run.level_runtime_enabled&&live_geometry);
            for (auto& projectile : volley) {
                const std::uint32_t original_travel = projectile.travel_ticks;
                auto surface = projectile;
                if(!runtime_catalog_||weapon_seed.shot.trajectory_mode==0)
                    simulation_resolve_projectile_surface_impact(surface, weapon_seed.shot, field);
                if (surface.impact_kind != SimulationImpactKind::None &&
                    surface.travel_ticks < original_travel) {
                    projectile = surface;
                    projectile.live_target_placement_index.reset();
                } else if (!runtime_catalog_&&target) {
                    // Prepared ballistic shots require an actual interval contact. An aim
                    // target alone must not become guaranteed damage at lifetime expiry.
                    projectile.impact_kind = SimulationImpactKind::TargetModelImpactClass;
                    projectile.impact_position = target_position;
                    projectile.impact_target_placement_index = weapon_target;
                    projectile.impact_effect_id = weapon_seed.shot.target_impact_effect_id(
                        target->shot_impact_class_index);
                }
                claim_projectile_world_slot(run,projectile);
                run.projectiles.push_back(std::move(projectile));
            }
            ++run.metrics.weapon_shots_fired;
            ++entity.weapon_volleys_fired;
            entity.last_weapon_fire_tick=run.tick;
            if (weapon_seed.shot.launch_effect_id != 0U) {
                SimulationLabOneShotEvent event;
                event.tick = run.tick;
                event.source_placement_index = entity.source_placement_index;
                event.army_asset_id = entity.army_asset_id;
                event.faction_index = entity.faction_index;
                event.kind = SimulationLabOneShotEventKind::WeaponLaunchEffect;
                event.effect_id = weapon_seed.shot.launch_effect_id;
                event.root_world_position = geometry.launch_points.front();
                event.actor = entity.actor;
                append_one_shot_event(run, std::move(event));
                ++run.metrics.weapon_launch_effect_events;
            }
        }
        finish_owner(entity_index);
    }
    while(next_effect<effect_owners.size())service_effect(effect_owners[next_effect++],std::nullopt);


}

void SimulationLabExperiment::process_collision_contacts(SimulationLabRun& run) {
    // The retail broad phase uses paired depth-bin masks. Precompute them once per entity for this
    // pass instead of rebuilding four masks for every O(n^2) pair. This is semantically identical
    // to simulation_depth_bin_overlap() but keeps the Simulation Lab's 24-actor timing budget.
    std::vector<std::uint32_t> depth_masks_x(run.entities.size(), 0U);
    std::vector<std::uint32_t> depth_masks_y(run.entities.size(), 0U);
    for (std::size_t index = 0; index < run.entities.size(); ++index) {
        const auto& entity = run.entities[index];
        const auto radius = entity.motion.depth_bin_broad_phase_radius_q12 != 0
            ? entity.motion.depth_bin_broad_phase_radius_q12
            : entity.motion.collision_radius_q12;
        if (!entity.construction_complete || radius == 0) continue;
        depth_masks_x[index] = simulation_depth_bin_mask(radius, float_to_q12(entity.actor.position.x));
        depth_masks_y[index] = simulation_depth_bin_mask(radius, float_to_q12(entity.actor.position.y));
    }

    for (std::size_t current_index = 0; current_index < run.entities.size(); ++current_index) {
        auto& current = run.entities[current_index];
        if (!current.construction_complete || !current.motion.movable() ||
            current.motion.collision_radius_q12 == 0) continue;
        for (std::size_t candidate_index = 0; candidate_index < run.entities.size(); ++candidate_index) {
            if (candidate_index == current_index) continue;
            auto& candidate = run.entities[candidate_index];
            if (!candidate.construction_complete || candidate.motion.collision_radius_q12 == 0)
                continue;
            // V537 recovers the paired near/far depth-bin masks used before the exact
            // expanded-radius helper. Catalog construction now carries the retail broad-phase
            // radius max(MDL +0xDC, recursively recomputed subtree radius). The exact overlap
            // below deliberately continues to use MDL +0xDC through collision_radius_q12.
            if (military_factory_parent_pair_v599(current_index,candidate_index,
                    current.production_linked_to_parent,current.production_parent_entity_index,
                    candidate.production_linked_to_parent,candidate.production_parent_entity_index) ||
                (current.linked_entity_index && *current.linked_entity_index == candidate_index) ||
                (candidate.linked_entity_index && *candidate.linked_entity_index == current_index))
                continue;
            if ((depth_masks_x[current_index] & depth_masks_x[candidate_index]) == 0U ||
                (depth_masks_y[current_index] & depth_masks_y[candidate_index]) == 0U)
                continue;
            if (!expanded_radius_overlap(current, current.actor.position, candidate)) continue;
            ++run.metrics.collision_contacts;
            const std::uint32_t collision_runtime_class =
                candidate.collision_model_component_index < candidate.model_components.size()
                    ? candidate.model_components[candidate.collision_model_component_index].runtime_class_id
                    : candidate.runtime_model_class_id;
            if (collision_runtime_class == 0x17U) {
                if (candidate.faction_index == current.faction_index &&
                    !candidate.linked_entity_index) {
                    candidate.collision_state_bits |= 1U;
                    candidate.collision_owner_timer = 0x20U;
                    if ((candidate.collision_state_bits & 2U) != 0U) {
                        candidate.linked_entity_index = current_index;
                        current.linked_entity_index = candidate_index;
                        ++run.metrics.class23_links;
                    }
                }
            } else if (collision_runtime_class == 0U) {
                // 0x0052A040 passes fixed 0x100000 damage and the candidate-current azimuth to
                // 0x0052A2E0. V537 follows the collided ModelRuntime component rather than treating
                // every entity as a single root integrity bucket.
                constexpr std::int32_t kRetailClass0ImpactDamage = 0x100000;
                ++run.metrics.class0_impact_events;
                const float dx = candidate.actor.position.x - current.actor.position.x;
                const float dy = candidate.actor.position.y - current.actor.position.y;
                const std::uint32_t impact_angle = radians_to_turn16(std::atan2(dy, dx));
                apply_component_damage(run, candidate, candidate.collision_model_component_index,
                                       kRetailClass0ImpactDamage, impact_angle);
            }
            break; // Retail returns the first qualifying owner-list candidate.
        }
    }
}

namespace {

Vec3f simulation_lab_articulated_authored_contact(
    const SimulationLabEntity& entity, std::uint32_t side, std::uint32_t heading_turn16,
    const FieldGrid& field) noexcept {
    const auto& local = entity.articulated_contact_local_offsets[side & 1U];
    const float radians = turn16_to_radians(heading_turn16);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    Vec3f result{
        entity.actor.position.x + local.x * cosine - local.y * sine,
        entity.actor.position.y + local.x * sine + local.y * cosine,
        entity.actor.position.z};
    const auto terrain = field.sample_terrain(result.x, result.y);
    result.z = terrain.height;
    return result;
}

void simulation_lab_service_articulated_contact_geometry(
    SimulationLabEntity& entity, const FieldGrid& field,
    std::optional<Vec3f> route_origin = {}) noexcept {
    if (entity.runtime_model_class_id != 3U || !entity.articulated_contact_geometry_enabled)
        return;
    auto& actor = entity.actor;
    const std::uint32_t swing = actor.articulated_swing_side & 1U;
    if (!actor.articulated_contacts_initialized) {
        for (std::uint32_t side = 0U; side < 2U; ++side) {
            const Vec3f contact = simulation_lab_articulated_authored_contact(
                entity, side, actor.body_yaw_turn16, field);
            actor.articulated_foot_anchor_world[side] = contact;
            actor.articulated_foot_target_world[side] = contact;
            actor.articulated_contact_heading_turn16[side] = actor.body_yaw_turn16;
            actor.articulated_contact_target_heading_turn16[side] = actor.body_yaw_turn16;
        }
        actor.articulated_pose_swing_side = swing;
        actor.articulated_pose_transition_active = actor.articulated_contact_transition_active;
        actor.articulated_contacts_initialized = true;
    }

    if (actor.articulated_pose_swing_side != swing) {
        const std::uint32_t completed = actor.articulated_pose_swing_side & 1U;
        actor.articulated_foot_anchor_world[completed] =
            actor.articulated_foot_target_world[completed];
        actor.articulated_contact_heading_turn16[completed] =
            actor.articulated_contact_target_heading_turn16[completed];
        actor.articulated_pose_swing_side = swing;
        actor.articulated_pose_transition_active = false;
    }

    if (actor.articulated_contact_transition_active &&
        !actor.articulated_pose_transition_active) {
        const std::uint32_t limit = std::clamp<std::uint32_t>(
            entity.articulated_contact_steering_limit_turn16 == 0U
                ? 0x8000U : entity.articulated_contact_steering_limit_turn16,
            1U, 0x8000U);
        const auto route_point = actor.move_waypoint ? actor.move_waypoint : actor.move_target;
        auto requested_heading=actor.body_yaw_turn16;
        if(route_origin&&route_point&&
           (actor.position.x!=route_origin->x||actor.position.y!=route_origin->y)){
            // UpdateLeft/RightTerrainContact receives the route vector heading,
            // not the temporary generic body's per-tick yaw increment. Freezing
            // that increment made every walking contact steer only a tiny part
            // of the required turn and sent the support root along wide arcs.
            requested_heading=radians_to_turn16(std::atan2(route_point->y-route_origin->y,
                                                           route_point->x-route_origin->x));
        }
        const std::int32_t requested_delta = shortest_turn16_delta(
            actor.articulated_contact_heading_turn16[swing], requested_heading);
        const std::int32_t clamped_delta = std::clamp(
            requested_delta, -static_cast<std::int32_t>(limit),
            static_cast<std::int32_t>(limit));
        const std::uint32_t contact_heading = wrap_turn16(
            static_cast<std::int64_t>(actor.articulated_contact_heading_turn16[swing]) +
            clamped_delta);
        Vec3f frozen_target = simulation_lab_articulated_authored_contact(
            entity, swing, contact_heading, field);
        if (route_origin && route_point &&
            (actor.position.x != route_origin->x || actor.position.y != route_origin->y)) {
            const Vec3f side_offset{frozen_target.x - actor.position.x,
                                    frozen_target.y - actor.position.y, 0};
            frozen_target = simulation_articulated_step_target(*route_origin,
                actor.articulated_foot_target_world[swing ^ 1U], side_offset,
                contact_heading, entity.articulated_contact_interpolation_rate_q12,
                *route_point);
            frozen_target.z = field.sample_terrain(frozen_target.x, frozen_target.y).height;
        }
        actor.articulated_foot_target_world[swing] = frozen_target;
        actor.articulated_contact_target_heading_turn16[swing] = contact_heading;
        const auto& anchor = actor.articulated_foot_anchor_world[swing];
        const float dx = frozen_target.x - anchor.x;
        const float dy = frozen_target.y - anchor.y;
        const float dz = frozen_target.z - anchor.z;
        const std::int32_t distance_q12 = static_cast<std::int32_t>(std::lround(
            std::sqrt(dx * dx + dy * dy + dz * dz) * 4096.0F));
        actor.articulated_contact_step_q12 = simulation_articulated_contact_step_q12(
            entity.articulated_contact_interpolation_rate_q12,
            entity.articulated_contact_lift_q12, distance_q12);
        actor.articulated_pose_transition_active = true;
    } else if (!actor.articulated_contact_transition_active) {
        actor.articulated_pose_transition_active = false;
    }

    // Reproduce the contact-owned root position without running hierarchy/SPR code in the worker.
    // Raw contact Z is terrain Z; the shared terminal-SPR placement offset is added only for the
    // support root average, exactly as the renderer-side class-3 solver does.
    std::array<Vec3f, 2> contacts = actor.articulated_foot_anchor_world;
    if (actor.articulated_contact_transition_active &&
        (actor.articulated_was_moving || actor.articulated_contact_settling)) {
        const float progress = std::clamp(
            static_cast<float>(actor.articulated_contact_progress_q12) / 4096.0F, 0.0F, 1.0F);
        const auto& start = actor.articulated_foot_anchor_world[swing];
        const auto& end = actor.articulated_foot_target_world[swing];
        contacts[swing].x = start.x + (end.x - start.x) * progress;
        contacts[swing].y = start.y + (end.y - start.y) * progress;
        const float lift = q12_to_float(entity.articulated_contact_lift_q12);
        contacts[swing].z = start.z + (end.z - start.z) * progress +
            4.0F * lift * progress * (1.0F - progress);
    }
    const float foot_offset = entity.articulated_shared_foot_placement_offset;
    actor.position = {
        (contacts[0].x + contacts[1].x) * 0.5F,
        (contacts[0].y + contacts[1].y) * 0.5F,
        (contacts[0].z + contacts[1].z) * 0.5F + foot_offset +
            q12_to_float(entity.articulated_root_height_q12)};
    actor.body_yaw_turn16=simulation_articulated_support_heading(actor);
}

} // namespace

namespace {
bool lab_has_native_movement_context(const SimulationLabEntity& entity) noexcept {
    return entity.motion.ground_speed_control || entity.runtime_model_hierarchy ||
        entity.combat_follow_owned || entity.combat_command_mode_flags!=0U ||
        entity.military_ai_command_kind!=0U || (entity.combat_movement.flags&0x240U)!=0U;
}

bool lab_actual_step_allowed(const SimulationLabRun& run,const SimulationLabEntity& entity,
    const FieldGrid& field,std::size_t index,Vec3f from,Vec3f to,
    std::span<const V528PathRuntimeBlocker> static_blockers,V565PathTerrainCache& cache,
    std::optional<std::size_t>* collision_owner=nullptr) {
    if(const auto trace=std::getenv("THANDOR_SIMLAB_COLLISION_TRACE_TICK");trace&&std::strtoul(trace,nullptr,10)==run.tick){
        std::fprintf(stderr,"COLLISION_QUERY tick=%u slot=%zu from=%d,%d to=%d,%d radius=%d\n",run.tick,entity.native_army_pool_slot,float_to_q12(from.x),float_to_q12(from.y),float_to_q12(to.x),float_to_q12(to.y),entity.motion.collision_radius_q12);
        for(size_t j=0;j<run.entities.size();++j){const auto& e=run.entities[j];const int64_t dx=int64_t(float_to_q12(to.x))-float_to_q12(e.actor.position.x),dy=int64_t(float_to_q12(to.y))-float_to_q12(e.actor.position.y);const int64_t radius=int64_t(entity.motion.collision_radius_q12)+e.motion.collision_radius_q12+2048;
            if(dx*dx+dy*dy<=radius*radius)std::fprintf(stderr,"COLLISION_NEAR slot=%zu arm=%u pos=%d,%d radius=%d live=%u link=%zu source_link=%zu parent=%zu locked=%u\n",e.native_army_pool_slot,e.army_asset_id,float_to_q12(e.actor.position.x),float_to_q12(e.actor.position.y),e.motion.collision_radius_q12,unsigned(simulation_lab_has_live_root_model_owner(e)),e.linked_entity_index.value_or(SIZE_MAX),entity.linked_entity_index.value_or(SIZE_MAX),e.production_parent_entity_index.value_or(SIZE_MAX),unsigned(e.production_linked_to_parent));
        }
    }
    if(entity.runtime_model_class_id==21U) return true; // flight bypasses ground admission
    if(entity.has_traversal_profile) {
        const auto masks=v519_path_masks(entity.faction_index,entity.traversal_low_band_index,
                                        entity.traversal_high_band_index);
        const auto start=v519_path_world_to_scratch(float_to_q12(from.x),float_to_q12(from.y));
        const auto end=v519_path_world_to_scratch(float_to_q12(to.x),float_to_q12(to.y));
        if(!masks.traversal_bands_valid||end!=v519_path_clip_to_inner_scratch(field,end)||
           // Native ground projection checks physical collision, not the route
           // lattice. Turning can carry a unit across a route-blocked cell.
           (!entity.motion.ground_speed_control &&
            !v565_path_segment_clear_static_cached(field,start,end,masks.faction_presence_mask,
                masks.high_cost_mask,masks.blocking_mask,{},0,cache))) return false;
    }
    const bool native_ground=run.level_runtime_enabled&&lab_has_native_movement_context(entity)&&
        (entity.runtime_model_class_id==1U||entity.runtime_model_class_id==2U||entity.runtime_model_class_id==3U||entity.runtime_model_class_id==17U||
         entity.runtime_model_class_id==18U||entity.runtime_model_class_id==19U);
    const auto hits=[&](const V528PathRuntimeBlocker& blocker){
        if(!native_ground)return simulation_lab_step_hits_blocker(from,to,entity.motion.collision_radius_q12,blocker);
        if(entity.motion.collision_radius_q12<=0||blocker.collision_radius_q12<=0)return false;
        const int64_t x=int64_t(float_to_q12(to.x))-blocker.world_x_q12;
        const int64_t y=int64_t(float_to_q12(to.y))-blocker.world_y_q12;
        const int64_t radius=int64_t(entity.motion.collision_radius_q12)+blocker.collision_radius_q12;
        return x*x+y*y<=radius*radius;
    };
    using OwnerKey=decltype(simulation_lab_owner_order(entity,index));
    std::optional<OwnerKey> first_blocker;
    std::optional<size_t> first_entity;
    for(size_t j=0;j<static_blockers.size();++j){const auto& blocker=static_blockers[j];
        if(!hits(blocker))continue;
        if(!native_ground)return false;
        const OwnerKey key{false,0U,blocker.source_placement_index,j};
        if(!first_blocker||key>*first_blocker){first_blocker=key;first_entity.reset();}
    }
    for(std::size_t j=0;j<run.entities.size();++j) {
        const auto& other=run.entities[j];
        if(j==index||
           !simulation_lab_has_live_root_model_owner(other)) continue;
        if(military_factory_parent_pair_v599(index,j,entity.production_linked_to_parent,
            entity.production_parent_entity_index,other.production_linked_to_parent,other.production_parent_entity_index)) continue;
        if((entity.linked_entity_index&&*entity.linked_entity_index==j)||
           (other.linked_entity_index&&*other.linked_entity_index==index)) continue;
        const V528PathRuntimeBlocker blocker{float_to_q12(other.actor.position.x),
            float_to_q12(other.actor.position.y),other.motion.collision_radius_q12};
        if(hits(blocker)){
            if(!native_ground){if(collision_owner)*collision_owner=j;return false;}
            const auto key=simulation_lab_owner_order(other,j);
            if(!first_blocker||key>*first_blocker){first_blocker=key;first_entity=j;}
        }
    }
    if(first_blocker){if(collision_owner)*collision_owner=first_entity;return false;}
    return true;
}

// A rejected contact must not leave the provisional body advance paired with
// old planted feet. Recover from the last admitted pose, lower the moving foot
// beside its support foot where that root path is clear, and then replan.
// This Lab recovery uses the native obstructed-step closing shape; the Lab's
// mid-step collision guard is separate from the original foot-target query.
void lab_recover_articulated_contact(SimulationLabRun& run,SimulationLabEntity& entity,
    const FieldGrid& field,std::size_t index,
    std::span<const V528PathRuntimeBlocker> statics,V565PathTerrainCache& cache,
    uint32_t step_ticks) {
    auto& actor=entity.actor;
    if(!actor.articulated_contacts_initialized)return;
    const uint32_t side=actor.articulated_swing_side&1U,other=side^1U;
    auto current=actor.articulated_foot_anchor_world[side];
    auto heading=actor.articulated_contact_heading_turn16[side];
    if(actor.articulated_contact_transition_active){
        const float t=static_cast<float>(std::min(actor.articulated_contact_progress_q12,4096U))/4096.F;
        const auto target=actor.articulated_foot_target_world[side];
        current={current.x+(target.x-current.x)*t,current.y+(target.y-current.y)*t,
            current.z+(target.z-current.z)*t+
                4.F*q12_to_float(entity.articulated_contact_lift_q12)*t*(1.F-t)};
        const auto delta=shortest_turn16_delta(heading,
            actor.articulated_contact_target_heading_turn16[side]);
        heading=wrap_turn16(int64_t(heading)+
            ((int64_t(delta)*std::min(actor.articulated_contact_progress_q12,4096U))>>12));
    }
    const auto authored=simulation_lab_articulated_authored_contact(entity,side,heading,field);
    const auto support=actor.articulated_foot_anchor_world[other];
    Vec3f landing{support.x+2.F*(authored.x-actor.position.x),
                  support.y+2.F*(authored.y-actor.position.y),0};
    auto terrain=field.sample_terrain(landing.x,landing.y);
    const Vec3f closing_root{(landing.x+support.x)*.5F,(landing.y+support.y)*.5F,actor.position.z};
    if(!terrain.inside||!lab_actual_step_allowed(run,entity,field,index,
            actor.position,closing_root,statics,cache)){
        // If closing sideways is also blocked, lower at the already admitted
        // contact XY. Never teleport the body or a planted foot through a hull.
        landing=current;terrain=field.sample_terrain(landing.x,landing.y);
    }
    landing.z=terrain.inside?terrain.height:current.z;
    actor.articulated_foot_anchor_world[side]=current;
    actor.articulated_foot_target_world[side]=landing;
    actor.articulated_contact_heading_turn16[side]=heading;
    actor.articulated_contact_target_heading_turn16[side]=heading;
    actor.articulated_contact_progress_q12=0;
    actor.articulated_gait_phase_turn16=side?0x8000U:0U;
    actor.articulated_pose_swing_side=side;
    actor.articulated_pose_transition_active=true;
    actor.articulated_contact_transition_active=true;
    actor.articulated_contact_settling=true;
    actor.articulated_was_moving=false;
    actor.articulated_turning=false;
    actor.articulated_contact_step_q12=simulation_articulated_contact_step_q12(
        entity.articulated_contact_interpolation_rate_q12,entity.articulated_contact_lift_q12,
        float_to_q12(std::sqrt((landing.x-current.x)*(landing.x-current.x)+
            (landing.y-current.y)*(landing.y-current.y)+(landing.z-current.z)*(landing.z-current.z))));
    // This is a new contact with a new distance/rate. The rejected provisional
    // root chord may have quantized its previous advance to one; reusing that
    // value can turn a short closing step into thousands of callbacks.
    const int64_t movement_advance=int64_t(std::abs(entity.motion.speed_q12_per_tick))*
        std::max(1U,step_ticks);
    actor.articulated_last_contact_advance_q12=static_cast<uint32_t>(std::clamp<int64_t>(
        (movement_advance*actor.articulated_contact_step_q12)>>12,1,4096));
    actor.move_waypoint.reset();
    if(entity.combat_follow_owned)entity.combat_movement.flags|=0x10U;
}

struct LabPioneerRouteDependencies {
 SimulationLabRun& run;SimulationLabEntity& entity;const FieldGrid& field;
 std::span<const V528PathRuntimeBlocker> blockers;
 bool route_rejected{};
 V565PathTerrainCache* runtime_cache{};
 std::span<const V528PathRuntimeBlocker> native_statics;
 size_t owner_index{SIZE_MAX};
 bool within_owner_callback{};
 MovementRouteResult resolve(PioneerCommandState& state,MovementPoint target) {
  if(run.level_runtime_enabled&&runtime_cache&&runtime_cache->runtime_states&&run.retail_scratch){
   bool usable=false;
   const auto result=simulation_lab_resolve_native_route(run,field,owner_index,state,target,
       native_statics,*runtime_cache,within_owner_callback,usable);
   route_rejected=!usable;
   if(!usable)++entity.pioneer_route_rejection_count;
   return result;
  }
  ++run.metrics.navigation_route_resolves;
  const auto raw=v519_path_world_to_scratch(target.x,target.y);
  const auto clipped=v519_path_clip_to_inner_scratch(field,raw);
  if(raw!=clipped){const auto p=v519_path_scratch_center_to_world_q12(clipped);target={p.x_q12,p.y_q12};++run.metrics.navigation_destination_retargets;}
  if(!entity.has_traversal_profile)return {target,target,false};
  const auto route=v528_resolve_route(field,state.model.x,state.model.y,target.x,target.y,
   1U<<(entity.faction_index&31U),entity.traversal_low_band_index,entity.traversal_high_band_index,blockers,entity.motion.collision_radius_q12);
  if(!route.usable()) {
   // V588 allowed this to masquerade as an accepted command to the CURRENT
   // model location. That fabricates an arrival and strands the 0x100 state.
   // Preserve the original routing rejection as a distinct Lab outcome.
   route_rejected=true;
   ++entity.pioneer_route_rejection_count;
   entity.pioneer_last_rejected_target_x_q12=target.x;
   entity.pioneer_last_rejected_target_y_q12=target.y;
   return {state.model,state.model,true};
  }
  if(route.primary_world_q12)++run.metrics.navigation_routed_solves;
  if(route.requested_cell_retargeted)++run.metrics.navigation_destination_retargets;
  const auto f=route.resolved_world_q12,p=route.primary_world_q12.value_or(f);
  return {{p.x_q12,p.y_q12},{f.x_q12,f.y_q12},false};
 }
 MovementRouteResult resolve(PioneerMovementState& s,MovementPoint p){return resolve(static_cast<PioneerCommandState&>(s),p);}
 void queue(PioneerMovementState& s,MovementPoint p){pioneer_command_chain(static_cast<PioneerCommandState&>(s),p,1,*this);}
 MovementResult retarget(PioneerMovementState& s,MovementPoint p){
  const MovementPoint d{std::bit_cast<std::int32_t>(movement_abs(s.model.x,s.primary.x)),std::bit_cast<std::int32_t>(movement_abs(s.model.y,s.primary.y))};
  return {d,pioneer_direct_command(static_cast<PioneerCommandState&>(s),p,*this)};
 }
};
std::vector<V528PathRuntimeBlocker> pioneer_route_blockers(const SimulationLabRun& run,
 const SimulationLabEntity& entity,std::size_t index,std::span<const V528PathRuntimeBlocker> static_blockers) {
 std::vector<V528PathRuntimeBlocker> blockers(static_blockers.begin(),static_blockers.end());
 for(std::size_t j=0;j<run.entities.size();++j){const auto& other=run.entities[j];
  if(j==index ||
     !simulation_lab_has_live_root_model_owner(other) ||
     (entity.production_linked_to_parent && entity.production_parent_entity_index==j) ||
     (other.production_linked_to_parent && other.production_parent_entity_index==index) ||
     (entity.linked_entity_index&&*entity.linked_entity_index==j) || (other.linked_entity_index&&*other.linked_entity_index==index))continue;
  if(other.motion.collision_radius_q12)blockers.push_back({float_to_q12(other.actor.position.x),float_to_q12(other.actor.position.y),other.motion.collision_radius_q12,simulation_lab_route_blocker_class(other)});
 }
 return blockers;
}
void initialize_pioneer_movement(SimulationLabEntity& entity) {
 const MovementPoint model{float_to_q12(entity.actor.position.x),float_to_q12(entity.actor.position.y)};
 auto& s=entity.pioneer_movement;
 if(!entity.pioneer_movement_initialized){s={};s.definition_allows_movement=entity.motion.movable();s.stored=model;s.primary=model;s.fallback=model;s.last_checked=model;entity.pioneer_movement_initialized=true;}
 s.model=model;
}
// Class 18 receives its AI orders through AiUnitBehavior_UpdateOwnUnits just
// like military classes, even though its movement runtime runs separately.
// Only the newest 64 linked own unit models belong to Workspace 01. Do not
// make overflow owners eligible merely because they have a movement callback.
// Scan only the own-unit subset; visible/unseen and structures have independent
// limits and do not consume slots in Workspace 01.
bool lab_pioneer_workspace01_admitted_v743(const SimulationLabRun& run,
                                           std::size_t index) noexcept {
    if(index>=run.entities.size()) return false;
    const auto faction=run.entities[index].faction_index;
    if(faction==0U) return false;
    return simulation_lab_ai_own_workspaces(run,faction).unit_root_admitted(run,index);
}

void service_pioneer_behavior(LabPioneerRouteDependencies& deps,
 const SimulationLabRuntimeCatalog* runtime_catalog,
 std::span<const SimulationLabStaticPlacementOwner> static_placement_owners,
 bool scheduled_callback) {
 auto& run=deps.run;
 auto& entity=deps.entity;
 const auto& field=deps.field;
 auto& s=entity.pioneer_movement;
 if(scheduled_callback || (s.flags&0x100U)){
  const auto entry=s.flags;MovementResult movement{};
  if(!(entry&0x100U))movement=pioneer_movement_update(s,deps);
  simulation_lab_sync_movement_coordinates(entity,s);
  const auto workspace_counts=simulation_lab_pioneer_workspace_counts(run,entity.faction_index);
  const PioneerGateInput gate_input{entry,s.flags,movement.carry,
      workspace_counts.workspace00,workspace_counts.workspace04,
      run.level_runtime_enabled ? static_cast<std::uint32_t>(
          lab_rebuild_placement_workspaces(run, entity.faction_index, field).resource08.size()) : 1U,
      s.model.x,s.model.y,s.fallback.x,s.fallback.y};
  const auto gate=pioneer_movement_gate(gate_input);
  entity.pioneer_gate_trace.record(run.tick,scheduled_callback,gate_input,gate);
  entity.pioneer_last_gate_action=static_cast<std::uint32_t>(gate.action)+1U;
  s.flags=gate.flags;
  if(gate.action==PioneerGateAction::ResetAtModel){
   // Ghidra V537: the reset masks flags with C6 and rewrites all three targets.
   // The independent retail +0x100 stateOrTechnologyId and generation state
   // are not exposed by this Lab model: their conditional side effect is UNKNOWN.
   simulation_lab_cancel_dismantling_on_stop(entity);
   const auto reset=pioneer_retail_reset_common({s.flags,s.command_mode,std::nullopt,s.model});
   s.flags=reset.movement_flags;
   s.stored=reset.stored;s.primary=reset.primary;s.fallback=reset.fallback;
  }
  else if(gate.action==PioneerGateAction::BootstrapForward || gate.action==PioneerGateAction::ScoreSites){
   std::optional<Vec3f> desired;
   if(gate.action==PioneerGateAction::BootstrapForward){
    const auto heading=entity.actor.body_yaw_turn16&0xFFFFU;
    const auto step=retail_direction_scaled(0U,heading,0x2d05);
    desired=Vec3f{q12_to_float(float_to_q12(entity.actor.position.x)+step[0]),
                  q12_to_float(float_to_q12(entity.actor.position.y)+step[1]),entity.actor.position.z};
   }
   else {
    // Scheduled selection allocates temporary models from the world job.
    const auto world_job=run.native_world_objects.enter_world_job(scheduled_callback);
    desired=class18_special_site_target(run,entity,field,runtime_catalog,
        static_placement_owners,&entity.pioneer_site_selector,&run.army_pool,&run.native_world_objects);
   }
   if(desired){
    entity.pioneer_ai_command_cooldown_ticks = 8; // source writes aiUnitState before routing
    const PioneerCommandState before_command=s;
    deps.route_rejected=false;
    deps.within_owner_callback=!scheduled_callback;
    pioneer_command_chain(s,{float_to_q12(desired->x),float_to_q12(desired->y)},0,deps);
    deps.within_owner_callback=true;
    if(deps.route_rejected) {
     // The original retail path resolver's whole fallback policy is not yet
     // reconstructed. A blocked solver result is NOT a completed Lab order;
     // restore the pre-command state instead of manufacturing a same-cell move.
     static_cast<PioneerCommandState&>(s)=before_command;
    } else {
     ++entity.pioneer_ai_command_count;
     entity.pioneer_ai_last_command_tick=run.tick;
    }
   }
  }
 }
}

bool step_pioneer_runtime(SimulationLabRun& run,SimulationLabEntity& entity,const FieldGrid& field,std::size_t index,
 std::span<const V528PathRuntimeBlocker> static_blockers,
 const SimulationLabRuntimeCatalog* runtime_catalog,
 std::span<const SimulationLabStaticPlacementOwner> static_placement_owners,
 V565PathTerrainCache& terrain_cache,uint32_t callback_step_ticks,
 const std::function<void(std::size_t,Vec3f)>& notify_collision){
 const auto world_owner=run.native_world_objects.enter_owner(simulation_lab_world_model_handle(entity));
 ++entity.pioneer_runtime_step_count;
 entity.pioneer_runtime_last_step_tick=run.tick;
 auto& s=entity.pioneer_movement;
 initialize_pioneer_movement(entity);
 const auto blockers=pioneer_route_blockers(run,entity,index,static_blockers);
 LabPioneerRouteDependencies deps{run,entity,field,blockers};
 deps.runtime_cache=&terrain_cache;deps.native_statics=static_blockers;deps.owner_index=index;
 // Catalog-backed AI dispatch occurs in workspace order after the owner walk.
 // Preserve the standalone movement fixture path when no native job is present.
 const bool native_ai_dispatch=run.level_runtime_enabled&&runtime_catalog;
 const bool ai_owner=!native_ai_dispatch&&run.ai_planning_enabled&&
     (!run.level_runtime_enabled||(run.ai_controlled_faction_mask&(1U<<entity.faction_index))!=0U);
 const bool selector_tick=ai_owner&&((run.tick&7U)==2U||(run.tick&7U)==6U)&&
     ((run.tick>>3U)&7U)==entity.faction_index&&((run.tick>>6U)&1U)==0U;
 const bool admitted_selector_tick=selector_tick&&lab_pioneer_workspace01_admitted_v743(run,index);
 const auto should_dispatch_scheduled=[&]{
  const bool scheduled_callback=admitted_selector_tick&&pioneer_ai_dispatch_ready(
      s.flags,entity.pioneer_ai_command_cooldown_ticks);
  if(admitted_selector_tick&&!scheduled_callback)++entity.pioneer_ai_busy_skipped_calls;
  return scheduled_callback;
 };
 service_pioneer_behavior(deps,runtime_catalog,static_placement_owners,
     !run.level_runtime_enabled&&should_dispatch_scheduled());
 if(entity.root_model_terminal)return false;
 deps.within_owner_callback=true;
 s.flags|=4U;const auto movement=pioneer_movement_update(s,deps);
 simulation_lab_sync_movement_coordinates(entity,s);
 if(movement.carry){
  entity.actor.move_target.reset();entity.actor.move_waypoint.reset();
  entity.actor.ground_advance_q12=0;entity.actor.body_turn_velocity_turn16=0;
  if(!run.level_runtime_enabled&&s.retry)--s.retry;
  if(run.level_runtime_enabled&&should_dispatch_scheduled())
   service_pioneer_behavior(deps,runtime_catalog,static_placement_owners,true);
  return false;
 }
 entity.actor.move_target=Vec3f{q12_to_float(s.fallback.x),q12_to_float(s.fallback.y),entity.actor.position.z};
 // Mark the primary point as a waypoint so generic final-target tolerance does not consume it.
 entity.actor.move_waypoint=Vec3f{q12_to_float(movement.point.x),q12_to_float(movement.point.y),entity.actor.position.z};
 const auto old=entity.actor.position;const auto yaw=entity.actor.body_yaw_turn16;
 const auto target_before=entity.actor.move_target,waypoint_before=entity.actor.move_waypoint;
 bool moved=simulation_tick_actor_motion(entity.actor,entity.motion,callback_step_ticks);
 std::optional<std::size_t> collision_owner;
 if(moved&&(old.x!=entity.actor.position.x||old.y!=entity.actor.position.y)&&
    !lab_actual_step_allowed(run,entity,field,index,old,entity.actor.position,static_blockers,terrain_cache,&collision_owner)) {
  if(collision_owner)notify_collision(*collision_owner,old);
  entity.actor.position=old;entity.actor.move_target=target_before;entity.actor.move_waypoint=waypoint_before;
  entity.actor.ground_advance_q12=0;
  if(run.level_runtime_enabled)s.flags|=0x10U;
  ++run.metrics.navigation_actual_step_blocks;moved=false;
 }
 if(!entity.actor.move_waypoint)s.flags|=0x10U;
 if(old.x!=entity.actor.position.x || old.y!=entity.actor.position.y || yaw!=entity.actor.body_yaw_turn16)s.flags&=~4U;
 s.model={float_to_q12(entity.actor.position.x),float_to_q12(entity.actor.position.y)};
 // ArmyRuntimeMaintenance_UpdateHierarchyAiAndTimers decrements +0xA4 when nonzero.
 if(!run.level_runtime_enabled&&s.retry)--s.retry;
 if(run.level_runtime_enabled&&should_dispatch_scheduled())
   service_pioneer_behavior(deps,runtime_catalog,static_placement_owners,true);
 return moved;
}
} // namespace

bool SimulationLabExperiment::queue_player_command(std::size_t index,
 const SimulationLabPlayerCommand& command) {
 if(index>=runs_.size() || runs_[index].finished || command.player_index>=255U ||
    command.faction_index==0U || command.faction_index>=8U)return false;
 switch(command.kind){
  case SimulationLabPlayerCommandKind::TakeStock:
   if(!production_asset(command.army_asset_id))return false;
   break;
  case SimulationLabPlayerCommandKind::ReturnStock:
  case SimulationLabPlayerCommandKind::PlaceStock:break;
  case SimulationLabPlayerCommandKind::MovePioneer:
   if(command.entity_index>=runs_[index].entities.size())return false;
   break;
  case SimulationLabPlayerCommandKind::StopMovement:
  case SimulationLabPlayerCommandKind::MoveSelection:
  case SimulationLabPlayerCommandKind::QueueSelectionWaypoint:
  case SimulationLabPlayerCommandKind::TargetArmySelection:
  case SimulationLabPlayerCommandKind::TargetPositionSelection:
   // Native fixed selection array permits null/invalid slots; validate
   // faction and runtime owner when applying, never by truncating the list.
   break;
  default:return false;
 }
 runs_[index].player_commands.push_back(command);
 return true;
}

void SimulationLabExperiment::apply_player_commands(SimulationLabRun& run,const FieldGrid& field) {
 run.player_command_results.clear();
 for(const auto& command:run.player_commands){
  auto& result=run.player_command_results.emplace_back();result.command=command;
  auto& pending=run.player_pending_placements[command.player_index];
  switch(command.kind){
   case SimulationLabPlayerCommandKind::TakeStock:{
    // RemoveFirstPrimaryArmyAsset followed by the player's pending slot assignment.
    pending={};
    const auto entry=std::find_if(run.pending_asset_requests.begin(),run.pending_asset_requests.end(),
      [&](const auto& r){return r.faction_index==command.faction_index&&r.army_asset_id==command.army_asset_id;});
    if(entry==run.pending_asset_requests.end())break;
    pending={command.army_asset_id,command.faction_index};
    run.pending_asset_requests.erase(entry);result.accepted=true;
    break;
   }
   case SimulationLabPlayerCommandKind::ReturnStock:{
    if(!pending.army_asset_id||pending.faction_index!=command.faction_index)break;
    const auto count=std::count_if(run.pending_asset_requests.begin(),run.pending_asset_requests.end(),
      [&](const auto& r){return r.faction_index==command.faction_index;});
    if(static_cast<std::size_t>(count)<kFactionArmyAssetQueueCapacity)
      run.pending_asset_requests.push_back({pending.army_asset_id,command.faction_index,run.tick,0});
    // Native clears pending even when its 64-entry primary stock is full.
    pending={};result.accepted=true;break;
   }
   case SimulationLabPlayerCommandKind::PlaceStock:{
    if(!pending.army_asset_id||pending.faction_index!=command.faction_index||!runtime_catalog_)break;
    const auto* product=production_asset(pending.army_asset_id);
    const auto model=simulation_lab_first_site_model(*runtime_catalog_,pending.army_asset_id);
    if(!product||!model)break;
    const auto owners=simulation_lab_site_owners(run,field,*runtime_catalog_,static_placement_owners_);
    const MovementPoint requested{command.world_x_q12,command.world_y_q12};
    // Native cursor validation tries the raw point followed by these four
    // snapped alternatives. AI site placement deliberately does not use them.
    const uint32_t bx=std::bit_cast<uint32_t>(requested.x)&~0xFFU;
    const uint32_t by=std::bit_cast<uint32_t>(requested.y)&~0xFFU;
    const auto point=[&](uint32_t dx,uint32_t dy){return MovementPoint{
      std::bit_cast<int32_t>(bx+dx),std::bit_cast<int32_t>(by+dy)};};
    const std::array candidates{requested,point(0,0),point(0x240,0),point(0x240,0x240),point(0,0x240)};
    for(const auto candidate:candidates){
     if(!simulation_lab_query_site_placement(run,field,owners,*model,candidate,
          command.faction_index,0U,0,command.heading_turn16).accepted)continue;
     grant_creation_technologies(run,command.faction_index,product->entity_seed);
     const Vec3f position{q12_to_float(candidate.x),q12_to_float(candidate.y),
       q12_to_float(retail_placement_height_q12(field,candidate,model->contact_kind))};
     auto entity=make_entity_from_seed(product->entity_seed,command.faction_index,position,true,
       run.faction_technology_masks[command.faction_index]);
     entity.actor.body_yaw_turn16=command.heading_turn16&0xFFFFU;
     entity.construction_start_tick=entity.construction_complete_tick=run.tick;
     claim_model_smoke_slots(run,entity);
     run.entities.push_back(std::move(entity));
     const auto placed_index=run.entities.size()-1U;
     dispatch_group_a_class_command(run,placed_index);
     const auto effect=product->placement_effect_ids_prepared
       ?product->placement_effect_ids_by_faction[command.faction_index]:product->placement_effect_id;
     if(effect){
      const auto& placed=run.entities[placed_index];SimulationLabOneShotEvent event;
      event.tick=run.tick;event.source_placement_index=placed.source_placement_index;
      event.army_asset_id=placed.army_asset_id;event.faction_index=command.faction_index;
      event.kind=SimulationLabOneShotEventKind::ConstructionPlacementEffect;event.effect_id=effect;
      event.root_world_position=placed.actor.position;event.actor=placed.actor;
      if(run.level_runtime_enabled&&runtime_catalog_&&runtime_catalog_->effects.contains(event.effect_id))
       link_environmental_effect(run,event.effect_id,lab_root_effect_pose(placed),true);
      append_one_shot_event(run,std::move(event));
     }
     ++run.faction_score[command.faction_index].relation_counter_b;
     pending={};result.accepted=true;result.placed_point=candidate;break;
    }
    // Rejection leaves the player-held stock intact; there is no second payment.
    break;
   }
   case SimulationLabPlayerCommandKind::StopMovement:{
    // The native selection callback iterates all 32 slots independently.
    // A stale slot or another player's army cannot change this faction.
    for(const auto selected:command.selected_entity_indices){
     if(!selected || *selected>=run.entities.size())continue;
     auto& entity=run.entities[*selected];
     if(entity.faction_index!=command.faction_index)continue;
     if(simulation_lab_explicit_stop_movement(entity))result.accepted=true;
    }
    break;
   }
   case SimulationLabPlayerCommandKind::TargetArmySelection:{
    if(command.entity_index>=run.entities.size())break;
    const auto& target=run.entities[command.entity_index];
    if(!simulation_lab_command_target_owner_available(target))break;
    for(const auto selected:command.selected_entity_indices){
     if(!selected||*selected>=run.entities.size())continue;
     auto& entity=run.entities[*selected];
     if(entity.faction_index!=command.faction_index||
        !simulation_lab_has_live_root_model_owner(entity)||
        entity.root_model_terminal||entity.production_linked_to_parent)continue;
     if(simulation_lab_apply_selection_army_target(entity,command.entity_index))
      result.accepted=true;
    }
    break;
   }
   case SimulationLabPlayerCommandKind::TargetPositionSelection:{
    const Vec3f target{q12_to_float(command.world_x_q12),
                       q12_to_float(command.world_y_q12),
                       q12_to_float(command.world_z_q12)};
    for(const auto selected:command.selected_entity_indices){
     if(!selected||*selected>=run.entities.size())continue;
     auto& entity=run.entities[*selected];
     if(entity.faction_index!=command.faction_index||
        !simulation_lab_has_live_root_model_owner(entity)||
        entity.root_model_terminal||entity.production_linked_to_parent)continue;
     if(simulation_lab_apply_selection_target_position(entity,target))
      result.accepted=true;
    }
    break;
   }
   case SimulationLabPlayerCommandKind::MoveSelection:
   case SimulationLabPlayerCommandKind::QueueSelectionWaypoint:{
    struct SelectionEntry {
     std::size_t entity_index{};
     MovementPoint position{};
     MovementPoint offset{};
    };
    std::vector<SelectionEntry> entries;
    entries.reserve(command.selected_entity_indices.size());
    std::int64_t position_sum_x=0,position_sum_y=0;
    for(const auto selected:command.selected_entity_indices){
     if(!selected||*selected>=run.entities.size())continue;
     const auto& entity=run.entities[*selected];
     if(entity.faction_index!=command.faction_index||
        !simulation_lab_has_live_root_model_owner(entity)||entity.root_model_terminal)continue;
     const MovementPoint position{float_to_q12(entity.actor.position.x),
                                  float_to_q12(entity.actor.position.y)};
     entries.push_back({*selected,position,{}});
     position_sum_x+=position.x;position_sum_y+=position.y;
    }
    if(entries.empty())break;

    const MovementPoint center{
     static_cast<std::int32_t>(position_sum_x/static_cast<std::int64_t>(entries.size())),
     static_cast<std::int32_t>(position_sum_y/static_cast<std::int64_t>(entries.size()))};
    std::int32_t min_x=0,max_x=0,min_y=0,max_y=0;
    bool first=true;
    for(auto& entry:entries){
     entry.offset={movement_sub(center.x,entry.position.x),movement_sub(center.y,entry.position.y)};
     if(first){min_x=max_x=entry.offset.x;min_y=max_y=entry.offset.y;first=false;}
     else{
      min_x=std::min(min_x,entry.offset.x);max_x=std::max(max_x,entry.offset.x);
      min_y=std::min(min_y,entry.offset.y);max_y=std::max(max_y,entry.offset.y);
     }
    }
    const std::int64_t extent_x=static_cast<std::int64_t>(max_x)-min_x;
    const std::int64_t extent_y=static_cast<std::int64_t>(max_y)-min_y;
    const bool spread_too_large=extent_x>0x5000||extent_y>0x5000||
                                extent_x+extent_y>0x7000;
    bool any_movable=false;
    for(const auto& entry:entries){
     auto& entity=run.entities[entry.entity_index];
     if(!entity.motion.movable())continue;
     any_movable=true;
     MovementPoint target{command.world_x_q12,command.world_y_q12};
     if(!spread_too_large){
      target.x=movement_sub(target.x,entry.offset.x);
      target.y=movement_sub(target.y,entry.offset.y);
     }
     if(command.kind==SimulationLabPlayerCommandKind::QueueSelectionWaypoint)
      simulation_lab_append_waypoint_or_start_route(run,field,entry.entity_index,target,
       static_navigation_blockers_,lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get()));
     else
      simulation_lab_set_site_route(run,field,entry.entity_index,target,
       static_navigation_blockers_,lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get()));
     result.accepted=true;
    }
    // A plain move of exactly one immobile class-13 factory changes its stored
    // rally point instead of issuing an ArmyRuntime movement command. The
    // waypoint/position command has no corresponding class-13 special case.
    if(command.kind==SimulationLabPlayerCommandKind::MoveSelection&&!any_movable&&
       entries.size()==1U){
     auto& entity=run.entities[entries.front().entity_index];
     if(entity.runtime_model_class_id==13U){
      entity.production_drive_target_world=Vec3f{q12_to_float(command.world_x_q12),
       q12_to_float(command.world_y_q12),entity.actor.position.z};
      result.accepted=true;
     }
    }
    break;
   }
   case SimulationLabPlayerCommandKind::MovePioneer:{
    if(command.entity_index>=run.entities.size())break;
    auto& entity=run.entities[command.entity_index];
    if(entity.faction_index!=command.faction_index||entity.runtime_model_class_id!=18U||
       !simulation_lab_has_live_root_model_owner(entity)||entity.root_model_terminal||!entity.motion.movable())break;
    initialize_pioneer_movement(entity);
    const auto before=entity.pioneer_movement;
    const auto blockers=pioneer_route_blockers(run,entity,command.entity_index,static_navigation_blockers_);
    LabPioneerRouteDependencies deps{run,entity,field,blockers};
    deps.runtime_cache=&lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get());
    deps.native_statics=static_navigation_blockers_;deps.owner_index=command.entity_index;
    pioneer_command_chain(entity.pioneer_movement,{command.world_x_q12,command.world_y_q12},0,deps);
    if(deps.route_rejected)entity.pioneer_movement=before;
    else result.accepted=true;
    simulation_lab_sync_movement_coordinates(entity,entity.pioneer_movement);
    break;
   }
  }
 }
 run.player_commands.clear();
}

void SimulationLabExperiment::step_movement(SimulationLabRun& run, const FieldGrid& field,
                                             std::optional<size_t> only_owner) {
    // Routed GridScratch searches are much more expensive than direct-line validation. Retail
    // spreads path work across live runtime updates; keep the Lab's 20 Hz coordinator bounded in
    // the same spirit by allowing one full detour solve per run/tick. Direct routes remain immediate,
    // and deferred detours are retried on the following tick without moving through blocked terrain.
    constexpr std::uint32_t kMaximumRoutedSolvesPerTick = 1U;
    std::uint32_t routed_solves_this_tick = 0U;
    // The editor worker precomputes immutable FLD traversal classification before timed
    // generations begin. The single-threaded test/tool API may enter without that setup hook;
    // prepare the same complete staggered lattice before the first movement query.
    auto& movement_cache=run.runtime_field?run.runtime_navigation_cache:navigation_terrain_cache_;
    if (movement_cache.field != &field ||
        movement_cache.states.size() !=
            static_cast<std::size_t>(field.width()) * field.height() * 16U) {
        movement_cache.reset(field);
    }
    std::vector<std::size_t> movement_owners;
    if(only_owner)movement_owners.push_back(*only_owner);
    else{movement_owners.resize(run.entities.size());std::iota(movement_owners.begin(),movement_owners.end(),0U);}
    if(!only_owner&&run.level_runtime_enabled&&runtime_catalog_)
        std::sort(movement_owners.begin(),movement_owners.end(),[&](auto a,auto b){
            return simulation_lab_owner_order(run.entities[a],a)>simulation_lab_owner_order(run.entities[b],b);
        });
    for (const auto i:movement_owners) {
        if(only_owner&&i!=*only_owner)continue;
        auto& entity = run.entities[i];
        if (!entity.construction_complete ||
            !simulation_lab_has_live_root_model_owner(entity)) continue;
        if(!only_owner)
            entity.ground_aim_before_movement=run.level_runtime_enabled&&runtime_catalog_
                ?simulation_lab_ground_aim_snapshot(entity):std::nullopt;
        const bool native_walker=run.level_runtime_enabled&&entity.runtime_model_class_id==3U&&
            entity.articulated_contact_geometry_enabled&&runtime_catalog_&&
            entity.runtime_model_hierarchy&&!entity.runtime_model_hierarchy->empty();
        // The walker services its first damage effect before water damage.
        // Only its second, movement-dependent callback sees the new health.
        if(native_walker)advance_damage_effects(run,field,i,1U);
        // Each native ground callback drops its own overlap link once the
        // current circles separate. The platform's reverse link has its own
        // timeout and must remain until that platform services it.
        if(run.level_runtime_enabled&&entity.linked_entity_index&&
           (entity.runtime_model_class_id==1U||entity.runtime_model_class_id==2U||
            entity.runtime_model_class_id==3U||entity.runtime_model_class_id==17U||
            entity.runtime_model_class_id==18U||entity.runtime_model_class_id==19U)) {
            const auto linked=*entity.linked_entity_index;
            if(linked>=run.entities.size()||
               !expanded_radius_overlap(entity,entity.actor.position,run.entities[linked]))
                entity.linked_entity_index.reset();
        }
        if(entity.runtime_model_class_id==17U&&!entity.native_world_bank_pose_initialized){
            const auto handle=simulation_lab_world_model_handle(entity);
            const auto slot=run.native_world_objects.slot(handle);
            if(slot!=kSimulationLabNoWorldSlot){
                // The class initializer runs before the new army's spawn
                // transform is written to the root's recycled world record.
                const auto& retained=run.native_world_objects.retained_pose(handle);
                entity.actor.class17_bank_heading_turn16=retained.yaw&0xFFFFU;
                entity.actor.class17_previous_route_point_q12={retained.position[0],retained.position[1]};
                entity.native_world_bank_pose_initialized=true;
                if(std::getenv("THANDOR_SIMLAB_WORLD_POOL_TRACE"))
                    std::fprintf(stderr,"BANK_INIT tick=%u army=%u slot=%zu world=%zu yaw=%u xyz=%d,%d,%d\n",
                        run.tick,entity.army_asset_id,entity.native_army_pool_slot,slot,retained.yaw,
                        retained.position[0],retained.position[1],retained.position[2]);
            }
        }
        // Native ground callbacks sample water at the old position, before steering.
        if(run.level_runtime_enabled)apply_water_damage(run,field,i);
        entity.last_translated = false;
        entity.last_turned = false;
        std::optional<SimulationActorState> articulated_tick_start;
        const bool factory_holds_rotor=run.level_runtime_enabled&&entity.runtime_model_class_id==17U&&
            entity.production_linked_to_parent&&entity.production_parent_entity_index&&
            *entity.production_parent_entity_index<run.entities.size()&&
            !simulation_lab_factory_footprint_cleared(entity,run.entities[*entity.production_parent_entity_index]);

        const auto service_presentation = [&](Vec3f before, std::uint32_t previous_yaw,
                                              bool moved, std::uint64_t previous_contact_serial,
                                              bool serviced_banking_motion=false) {
            const auto previous_ground_angles=entity.actor.ground_recoil_angles;
            bool placed=false;
            if(runtime_catalog_&&(entity.motion.ground_speed_control||entity.runtime_model_class_id==19U)){
                placed=simulation_lab_place_root_on_surface(entity,field,*runtime_catalog_);
                // Native contact callbacks leave XYZ untouched when the field
                // interpolation triangle is invalid, even after steering.
                if(run.level_runtime_enabled&&!placed&&entity.motion.placement_contact_kind!=3)
                    entity.actor.position=before;
            }
            if(entity.motion.ground_speed_control&&!moved&&!serviced_banking_motion){
                entity.actor.ground_advance_q12=0;
                // Native idle callbacks clear steering momentum as well as speed.
                // Keep it through the actual arrival step, then clear next tick.
                if(!entity.actor.move_target&&!entity.actor.move_waypoint)
                    entity.actor.body_turn_velocity_turn16=0;
            }
            if(entity.motion.ground_speed_control){
                auto& actor=entity.actor;
                actor.ground_recoil_angles.reset();
                const auto normal=entity.placement_normal_angles.value_or(0x40000000U);
                auto contact_angles=std::array<uint32_t,3>{actor.body_yaw_turn16,normal>>16,normal&0xFFFFU};
                if(run.level_runtime_enabled&&runtime_catalog_){
                    // Upright contact writes elevation only. The retained
                    // azimuth still affects the quantized transform at 90°.
                    if(previous_ground_angles){
                        if(entity.runtime_model_class_id==17U&&!serviced_banking_motion)
                            contact_angles[0]=(*previous_ground_angles)[0];
                        if(entity.motion.placement_contact_kind!=2||!placed)
                            contact_angles[2]=(*previous_ground_angles)[2];
                        if(!placed)contact_angles[1]=(*previous_ground_angles)[1];
                    }
                    actor.ground_recoil_angles=contact_angles;
                }
                if(actor.ground_recoil_ticks>0){
                    --actor.ground_recoil_ticks;
                    if(!actor.ground_recoil_angles&&entity.motion.placement_contact_kind==2)
                        if(const auto sampled=retail_contact_normal_angles(field,float_to_q12(actor.position.x),float_to_q12(actor.position.y)))
                            contact_angles={actor.body_yaw_turn16,*sampled>>16,*sampled&0xFFFFU};
                    const uint32_t tilt=uint32_t(actor.ground_recoil_ticks)*
                        uint32_t(actor.ground_recoil_tilt_per_tick);
                    actor.ground_recoil_angles=retail_compose_model_angles(
                        {0U,0x4000U-tilt,
                         (actor.ground_recoil_heading+0x8000U-actor.body_yaw_turn16)&0xFFFFU},
                        contact_angles);
                    actor.body_yaw_turn16=(*actor.ground_recoil_angles)[0];
                }
            }
            entity.last_translated = entity.actor.position.x != before.x ||
                                     entity.actor.position.y != before.y;
            entity.last_turned = entity.actor.body_yaw_turn16 != previous_yaw;
            if (entity.runtime_model_class_id == 2U && entity.tank_track_animation_enabled &&
                (entity.last_translated || entity.last_turned)) {
                simulation_update_tank_track_offsets(entity.actor, before, previous_yaw,
                    entity.actor.position, entity.actor.body_yaw_turn16,
                    entity.tank_track_local_bounds_y0, entity.tank_track_texture_travel_scale);
                ++run.metrics.tank_track_updates;
            }
            if (entity.runtime_model_class_id == 3U) {
                // The class-3 movement callback and the separate terrain-contact transition use the
                // MDL +0xC0 contact numerator, not movement speed. Service the transition even after
                // planar motion stops so a partially raised Orang/Emu/Gorilla foot can settle.
                simulation_update_articulated_contact_transition(entity.actor, before, previous_yaw,
                    entity.actor.position, entity.actor.body_yaw_turn16,
                    entity.articulated_contact_interpolation_rate_q12,
                    static_cast<int32_t>(std::min<int64_t>(INT32_MAX,
                        int64_t(std::abs(entity.motion.speed_q12_per_tick))*config_.runtime_callback_step_ticks)));
                simulation_lab_service_articulated_contact_geometry(entity, field, before);
                // Contact geometry also owns root XY. It runs after generic motion admission,
                // so its final root must pass the same traversal/collision gate before publishing.
                if(articulated_tick_start && (articulated_tick_start->position.x!=entity.actor.position.x ||
                    articulated_tick_start->position.y!=entity.actor.position.y) &&
                    !lab_actual_step_allowed(run,entity,field,i,articulated_tick_start->position,entity.actor.position,
                        static_navigation_blockers_,lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get()))) {
                    entity.actor=std::move(*articulated_tick_start);
                    lab_recover_articulated_contact(run,entity,field,i,
                        static_navigation_blockers_,lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get()),config_.runtime_callback_step_ticks);
                    entity.navigation_direct_route_valid=false;entity.navigation_target_cell_valid=false;
                    entity.navigation_recheck_tick=run.tick+1U;
                    ++run.metrics.navigation_actual_step_blocks;
                }
                entity.last_translated=entity.actor.position.x!=before.x||entity.actor.position.y!=before.y;
                entity.last_turned=entity.actor.body_yaw_turn16!=previous_yaw;
                const std::uint64_t completed =
                    entity.actor.articulated_contact_event_serial - previous_contact_serial;
                if (completed != 0U) {
                    run.metrics.articulated_contact_completions += static_cast<std::uint32_t>(completed);
                    // A single unusually large contact advance can cross more than one 0x1000
                    // boundary. Preserve every completion as a distinct historical event. The root
                    // pose is common to those boundaries in this worker tick; completed sides
                    // alternate exactly as the retail contact state does.
                    const std::uint32_t final_side = entity.actor.articulated_last_completed_side & 1U;
                    for (std::uint64_t event_index = 0; event_index < completed; ++event_index) {
                        const std::uint64_t remaining_after = completed - event_index - 1U;
                        SimulationLabOneShotEvent event;
                        event.tick = run.tick;
                        event.source_placement_index = entity.source_placement_index;
                        event.army_asset_id = entity.army_asset_id;
                        event.faction_index = entity.faction_index;
                        event.kind = SimulationLabOneShotEventKind::ArticulatedContact;
                        event.sound_asset_id = entity.articulated_contact_sound_asset_id;
                        event.sound_gain_and_distance=std::array{entity.positioned_sound_gain_q15,entity.positioned_sound_maximum_distance_q12};
                        event.completed_side = final_side ^ static_cast<std::uint32_t>(remaining_after & 1U);
                        event.root_world_position = entity.actor.position;
                        event.actor = entity.actor;
                        event.actor.articulated_last_completed_side = event.completed_side;
                        append_one_shot_event(run, std::move(event));
                    }
                }
            }
            const bool root_changed = entity.last_translated || entity.last_turned;
            if ((entity.runtime_model_class_id == 2U || entity.runtime_model_class_id == 3U) &&
                root_changed && entity.movement_ground_effect_interval_ticks != 0U &&
                (entity.movement_ground_land_effect_id != 0U ||
                 entity.movement_ground_water_effect_id != 0U) &&
                simulation_tick_movement_ground_effect_countdown(entity.actor, true,
                    entity.movement_ground_effect_interval_ticks)) {
                SimulationLabOneShotEvent event;
                event.tick = run.tick;
                event.source_placement_index = entity.source_placement_index;
                event.army_asset_id = entity.army_asset_id;
                event.faction_index = entity.faction_index;
                event.kind = SimulationLabOneShotEventKind::MovementGroundEffect;
                event.root_world_position = entity.actor.position;
                event.actor = entity.actor;
                append_one_shot_event(run, std::move(event));
                ++run.metrics.movement_ground_effect_events;
            }
            if (entity.runtime_model_class_id == 17U) {
                auto& actor=entity.actor;
                if(entity.motion.ground_speed_control){
                    // Native applies the retained bank before updating it for next tick.
                    auto angles=actor.ground_recoil_angles.value_or(
                        std::array<uint32_t,3>{actor.body_yaw_turn16,0x4000U,0U});
                    if(actor.class17_bank_angle_turn16!=0x4000U)
                        angles=retail_compose_model_angles(angles,
                            {0U,actor.class17_bank_angle_turn16,actor.class17_bank_heading_turn16});
                    actor.ground_recoil_angles=angles;
                    actor.body_yaw_turn16=angles[0]&0xFFFFU;
                    const auto route=actor.move_waypoint?actor.move_waypoint:actor.move_target;
                    const int32_t dx=route?float_to_q12(route->x)-float_to_q12(before.x):0;
                    const int32_t dy=route?float_to_q12(route->y)-float_to_q12(before.y):0;
                    const auto length=retail_uint64_sqrt(uint64_t(int64_t(dx)*dx)+uint64_t(int64_t(dy)*dy));
                    const bool banking=actor.ground_advance_q12!=0U&&
                        length>uint32_t(entity.motion.speed_q12_per_tick)*32U;
                    const auto heading=actor.body_yaw_turn16;
                    if(banking)actor.body_yaw_turn16=fixed_atan2_angle16_compat(dy,dx)&0xFFFFU;
                    simulation_tick_class17_banking(actor,banking);
                    actor.body_yaw_turn16=heading;
                    // Native starts the child animation only after the model
                    // leaves the factory's expanded footprint.
                    if(!factory_holds_rotor)simulation_tick_class17_deployment(actor);
                    // Banking and recoil can leave a signed Euler roll. The
                    // native stationary flag compares the raw stored angles.
                    const auto old_roll=previous_ground_angles?(*previous_ground_angles)[0]:previous_yaw;
                    entity.last_turned=angles[0]!=old_roll;
                }else simulation_tick_class17_pose(actor, moved);
                ++run.metrics.class17_pose_updates;
            }
        };

        if(entity.runtime_model_class_id==3U&&!entity.root_model_terminal&&
           !entity.articulated_contact_geometry_prepared&&runtime_catalog_){
            entity.articulated_contact_geometry_prepared=true;
            if(simulation_lab_prepare_articulated_contacts(entity,field,*runtime_catalog_))
                simulation_lab_service_articulated_contact_geometry(entity,field);
        }
        if(entity.runtime_model_class_id==3U&&entity.articulated_contact_geometry_enabled)
            articulated_tick_start=entity.actor;
        if(entity.runtime_model_class_id==21U && entity.aircraft.initialized)continue;
        const Vec3f before = entity.actor.position;
        const std::uint32_t previous_yaw = entity.actor.body_yaw_turn16;
        const std::uint64_t previous_contact_serial = entity.actor.articulated_contact_event_serial;
        // Contact kind 2 stores the interpolated terrain normal on the native
        // model node. Speed control reads that previous-position orientation,
        // after steering but before the next placement callback.
        if(entity.motion.ground_speed_control){
            entity.actor.ground_normal_azimuth_turn16=0;
            entity.actor.ground_normal_elevation_turn16=0x4000;
            if(entity.motion.placement_contact_kind==2){
                auto normal=entity.placement_normal_angles;
                if(!normal)normal=retail_contact_normal_angles(field,float_to_q12(before.x),float_to_q12(before.y));
                if(normal){
                    entity.actor.ground_normal_azimuth_turn16=*normal&0xFFFFU;
                    entity.actor.ground_normal_elevation_turn16=static_cast<int16_t>(*normal>>16);
                }
            }
        }
        if(entity.actor.ground_recoil_angles){
            entity.actor.ground_normal_azimuth_turn16=(*entity.actor.ground_recoil_angles)[2];
            entity.actor.ground_normal_elevation_turn16=static_cast<int32_t>((*entity.actor.ground_recoil_angles)[1]);
        }
        // Destroyed geometry remains a collision owner until release, but its old
        // command must not keep moving the model. Continue stationary pose servicing.
        const auto notify_native_collision=[&](size_t owner,Vec3f origin){
            if(!run.level_runtime_enabled||entity.runtime_model_class_id==19U)return;
            auto& partner=run.entities[owner];
            if(partner.runtime_model_class_id==23U&&partner.faction_index==entity.faction_index&&
               !partner.linked_entity_index){
                partner.collision_state_bits|=1U;partner.collision_owner_timer=32U;
                if(partner.collision_state_bits&2U){
                    partner.linked_entity_index=i;entity.linked_entity_index=owner;
                    ++run.metrics.class23_links;
                }
            }else if(partner.runtime_model_class_id==0U){
                const auto angle=fixed_atan2_angle16_compat(
                    float_to_q12(partner.actor.position.y)-float_to_q12(origin.y),
                    float_to_q12(partner.actor.position.x)-float_to_q12(origin.x));
                apply_component_damage(run,partner,partner.root_model_component_index,0x100000,angle);
            }
        };
        const bool dismantling_pioneer=entity.runtime_model_class_id==18U&&
            entity.root_model_component_index<entity.model_components.size()&&
            (entity.model_components[entity.root_model_component_index].runtime_flags_ec&0x10U)!=0U;
        // Class 18 dispatches its special behavior before the ground callback's
        // destroyed/hold test. Even a held model must finish the nearby segment
        // reset after another owner's group route writes its movement record.
        if(run.level_runtime_enabled&&entity.runtime_model_class_id==18U&&
           (entity.root_model_terminal||(entity.runtime_flags&0x08U)!=0U)&&
           !dismantling_pioneer){
            initialize_pioneer_movement(entity);
            if(entity.pioneer_movement.flags&0x100U){
                const auto blockers=pioneer_route_blockers(run,entity,i,static_navigation_blockers_);
                LabPioneerRouteDependencies deps{run,entity,field,blockers};
                auto& cache=lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get());
                deps.runtime_cache=&cache;deps.native_statics=static_navigation_blockers_;deps.owner_index=i;
                service_pioneer_behavior(deps,runtime_catalog_.get(),static_placement_owners_,false);
                simulation_lab_sync_movement_coordinates(entity,entity.pioneer_movement);
            }
        }
        if (entity.root_model_terminal&&!native_walker&&!dismantling_pioneer) {
            service_presentation(before, previous_yaw, false, previous_contact_serial);
            continue;
        }
        if (!entity.motion.movable()) {
            if (entity.runtime_model_class_id == 18U) ++entity.pioneer_immobile_skipped_ticks;
            service_presentation(before, previous_yaw, false, previous_contact_serial);
            continue;
        }
        if (entity.runtime_model_class_id == 18U && (entity.runtime_flags & 0x08U) != 0U&&!dismantling_pioneer) {
            ++entity.pioneer_hold_skipped_ticks;
            // Runtime movement/waypoint callbacks test the conversion hold bit before servicing the
            // Pioneer. Keep presentation state live, but do not advance ground motion while the
            // completion-action effect owns the unit.
            service_presentation(before, previous_yaw, false, previous_contact_serial);
            continue;
        }

        // A parked class-22 child is a physical world entity, but the pad still
        // owns it until a launch command is issued. Likewise a class-13 child
        // materializes while the factory door is CLOSED: it must remain linked
        // during OPENING even though it has no movement target yet. Dropping the
        // link here allowed aircraft to attack from their pads, produced ground
        // units to fire before door opening, and factories to close prematurely.
        // Do NOT generalize this to every linked unit with no target: after a
        // genuine exit command completes, the link must be released normally.
        const bool awaiting_factory_door = entity.production_linked_to_parent &&
            !entity.actor.move_target && !entity.production_parked_on_pad &&
            entity.production_parent_entity_index &&
            *entity.production_parent_entity_index < run.entities.size() &&
            run.entities[*entity.production_parent_entity_index].runtime_model_class_id == 13U &&
            run.entities[*entity.production_parent_entity_index].production_behavior_state == 2U;
        if ((entity.production_parked_on_pad && entity.production_linked_to_parent &&
             !entity.actor.move_target) || (awaiting_factory_door&&!native_walker) ||
            (run.level_runtime_enabled && entity.production_exit_command_tick!=0U &&
             entity.production_exit_command_tick==run.tick)) {
            // This is a presentation-only waiting state, never autonomous movement
            // or target acquisition. The parent issues a command in its own step.
            service_presentation(before, previous_yaw, false, previous_contact_serial);
            continue;
        }

        // Finish a requested landing before route selection can replace its
        // intent or provisional steering can turn it back into a walking step.
        // Native articulated movement services the active foot before choosing
        // another route step. In particular, recovery at phase zero still owns
        // the contact: replanning every tick here can reject the same landing
        // forever and resurrect an otherwise completed rally command.
        if(articulated_tick_start&&entity.actor.articulated_contact_settling){
            service_presentation(before,previous_yaw,false,previous_contact_serial);
            const float distance=std::hypot(entity.actor.position.x-before.x,
                                           entity.actor.position.y-before.y);
            run.metrics.movement_distance_world+=distance;
            run.metrics.military_movement_distance_world+=distance;
            continue;
        }

        bool factory_exit=false;
        if(entity.production_linked_to_parent&&!entity.production_parked_on_pad&&
           entity.production_parent_entity_index&&*entity.production_parent_entity_index<run.entities.size()) {
            const auto& parent=run.entities[*entity.production_parent_entity_index];
            if(parent.runtime_model_class_id==13U&&parent.production_behavior_state==3U) {
                const bool known_footprints=entity.motion.collision_radius_q12>0&&parent.motion.collision_radius_q12>0;
                const auto legacy_exit=parent.production_exit_world.value_or(
                    entity.actor.move_target.value_or(entity.actor.position));
                const bool legacy_exit_reached=!known_footprints&&
                    std::abs(entity.actor.position.x-legacy_exit.x)<=1.F/4096.F&&
                    std::abs(entity.actor.position.y-legacy_exit.y)<=1.F/4096.F;
                if((known_footprints&&simulation_lab_factory_footprint_cleared(entity,parent))||legacy_exit_reached) {
                    entity.production_linked_to_parent=false;
                    entity.production_parent_entity_index.reset();
                    if(run.level_runtime_enabled){
                        entity.military_ai_source_dispatch=true;
                        if((entity.runtime_model_class_id==1U||entity.runtime_model_class_id==2U||entity.runtime_model_class_id==3U||
                            entity.runtime_model_class_id==17U||entity.runtime_model_class_id==19U)&&
                           entity.combat_movement_initialized){
                            entity.production_movement_unlock_pending=true;
                            entity.combat_follow_owned=true;
                        }
                        if(entity.runtime_model_class_id==18U&&entity.combat_movement_initialized){
                            // Class 18 takes over the same movement image at unlock.
                            entity.pioneer_movement=entity.combat_movement;
                            entity.pioneer_movement.flags&=~2U;
                            entity.pioneer_movement_initialized=true;
                        }
                    }
                    if(legacy_exit_reached) {
                        if(entity.actor.move_target&&
                           std::abs(entity.actor.move_target->x-legacy_exit.x)<=1.F/4096.F&&
                           std::abs(entity.actor.move_target->y-legacy_exit.y)<=1.F/4096.F)
                            entity.actor.move_target.reset();
                        entity.actor.move_waypoint.reset();
                    }
                } else {
                    factory_exit=true;
                    // A failed route or the generic arrival window must not release a child inside its factory.
                    if(!native_walker&&!entity.actor.move_target&&parent.production_exit_world)
                        entity.actor.move_target=parent.production_exit_world;
                }
            }
        }

        if (entity.runtime_model_class_id == 18U &&
            !(entity.production_linked_to_parent && entity.actor.move_target)) {
            entity.production_linked_to_parent = false;
            entity.production_parent_entity_index.reset();
            entity.target_entity_index.reset();
            const bool moved=step_pioneer_runtime(run,entity,field,i,static_navigation_blockers_,
                runtime_catalog_.get(),static_placement_owners_,lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get()),
                config_.runtime_callback_step_ticks,notify_native_collision);
            service_presentation(before,previous_yaw,moved,previous_contact_serial);
            if(moved)run.metrics.movement_distance_world+=std::hypot(entity.actor.position.x-before.x,entity.actor.position.y-before.y);
            continue;
        }

        // The native locked walker can still turn toward its combat command.
        // Only the movement route is locked while its factory door opens.
        if(native_walker){
            auto& cache=lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get());
            const auto route_fn=[&](){
                const bool active=simulation_lab_prepare_follow_motion(run,field,i,static_navigation_blockers_,cache,true);
                const auto p=active?*entity.actor.move_target:entity.actor.position;
                return MovementResult{{float_to_q12(p.x),float_to_q12(p.y)},!active};
            };
            const auto terrain_fn=[&](int32_t x,int32_t y)->std::optional<int32_t>{
                const auto surface=field.sample_retail_surface_heights_q12(x,y);
                return surface.valid?std::optional<int32_t>{surface.terrain}:std::nullopt;
            };
            const auto normal_fn=[&](int32_t x,int32_t y){return retail_contact_normal_angles(field,x,y);};
            const auto free_fn=[&](int32_t x,int32_t y){
                auto query=entity;query.motion.ground_speed_control=true;
                std::optional<size_t> partner;
                const bool free=lab_actual_step_allowed(run,query,field,i,entity.actor.position,
                    Vec3f{q12_to_float(x),q12_to_float(y),entity.actor.position.z},static_navigation_blockers_,cache,&partner);
                if(!free&&partner)notify_native_collision(*partner,entity.actor.position);
                return free;
            };
            const auto command_fn=[&]()->std::optional<MovementPoint>{
                if(entity.combat_command_mode_flags&1U){
                    // The native command-position resolver drops invisible
                    // targets before a standing walker chooses its next turn.
                    const auto target=simulation_lab_visible_command_target(run,field,i);
                    if(!target)return {};
                    const auto p=simulation_lab_target_aim_geometry(run.entities[*target],field,*runtime_catalog_).selected;
                    return MovementPoint{float_to_q12(p.x),float_to_q12(p.y)};
                }
                if((entity.combat_command_mode_flags&2U)&&entity.combat_command_target_position_world){
                    const auto p=*entity.combat_command_target_position_world;
                    return MovementPoint{float_to_q12(p.x),float_to_q12(p.y)};
                }
                return {};
            };
            const auto reached_fn=[&](){entity.combat_movement.flags|=0x10U;};
            const auto retained_contacts=simulation_lab_articulated_contact_positions(entity,*runtime_catalog_);
            const auto retained_models=simulation_lab_current_model_poses(entity,field,*runtime_catalog_);
            std::array<std::array<uint32_t,3>,2> retained_angles{};
            for(size_t side=0;side<2;++side){
                const auto start=entity.actor.articulated_contact_heading_turn16[side];
                const auto end=entity.actor.articulated_contact_target_heading_turn16[side];
                const auto delta=int16_t(uint16_t(end-start));
                retained_angles[side]={wrap_turn16(int64_t(start)+
                    ((int64_t(delta)*entity.actor.articulated_native.progress[side])>>12)),0x4000U,0U};
                size_t parent=0;
                const auto& nodes=*entity.runtime_model_hierarchy;
                for(size_t depth=0;depth<4;++depth){
                    const auto child=std::find_if(nodes.begin(),nodes.end(),[&](const auto& node){
                        return node.parent_index==parent&&node.child_slot==(depth==0?side:0U);
                    });
                    parent=static_cast<size_t>(child-nodes.begin());
                    if(child==nodes.end())break;
                }
                if(parent<retained_models.size()&&retained_models[parent].native_angles)
                    retained_angles[side]=*retained_models[parent].native_angles;
            }
            const auto completed_fn=[&](size_t side){
                ++run.metrics.articulated_contact_completions;
                SimulationLabOneShotEvent event;event.tick=run.tick;event.source_placement_index=entity.source_placement_index;
                event.army_asset_id=entity.army_asset_id;event.faction_index=entity.faction_index;
                event.kind=SimulationLabOneShotEventKind::ArticulatedContact;event.sound_asset_id=entity.articulated_contact_sound_asset_id;
                event.sound_gain_and_distance=std::array{entity.positioned_sound_gain_q15,entity.positioned_sound_maximum_distance_q12};
                event.completed_side=uint32_t(side);event.root_world_position=entity.actor.position;event.actor=entity.actor;
                append_one_shot_event(run,std::move(event));
                emit_articulated_contact_effect(run,field,entity,retained_contacts[side],retained_angles[side]);
            };
            struct WalkerDependencies {
                decltype(route_fn)& route_call;decltype(terrain_fn)& terrain_call;decltype(free_fn)& free_call;
                decltype(command_fn)& command_call;decltype(reached_fn)& reached_call;decltype(completed_fn)& completed_call;
                decltype(normal_fn)& normal_call;
                auto route(){return route_call();}auto terrain(int32_t x,int32_t y){return terrain_call(x,y);}
                auto normal(int32_t x,int32_t y){return normal_call(x,y);}
                bool free(int32_t x,int32_t y){return free_call(x,y);}auto command(){return command_call();}
                void reached(){reached_call();}void completed(size_t side){completed_call(side);}
            } deps{route_fn,terrain_fn,free_fn,command_fn,reached_fn,completed_fn,normal_fn};
            int32_t foot_radius=0;
            const auto& nodes=*entity.runtime_model_hierarchy;
            const auto first=std::find_if(nodes.begin(),nodes.end(),[](const auto& n){return n.parent_index==0U&&n.child_slot==0U;});
            if(first!=nodes.end())if(const auto sprite=runtime_catalog_->sprites.find(first->sprite_path);sprite!=runtime_catalog_->sprites.end())
                if(const auto& translation=sprite->second.child_translations_q12[0])foot_radius=(*translation)[1];
            simulation_tick_native_walker(entity.actor,entity.motion,entity.articulated_contact_interpolation_rate_q12,
                entity.articulated_contact_lift_q12,entity.articulated_contact_steering_limit_turn16,
                float_to_q12(entity.articulated_contact_local_offsets[0].y),foot_radius,entity.articulated_root_height_q12,
                float_to_q12(entity.articulated_shared_foot_placement_offset),config_.runtime_callback_step_ticks,deps,!entity.root_model_terminal);
            entity.last_translated=entity.actor.position.x!=before.x||entity.actor.position.y!=before.y;
            entity.last_turned=entity.actor.body_yaw_turn16!=previous_yaw;
            const auto distance=std::hypot(entity.actor.position.x-before.x,entity.actor.position.y-before.y);
            run.metrics.movement_distance_world+=distance;run.metrics.military_movement_distance_world+=distance;
            continue;
        }

        // A factory-linked Pioneer runs the same locked waypoint callback as
        // a ground vehicle until its movement image is handed to class 18.
        // The editor's generic retry backoff skips the next native movement
        // callback after a collision and loses its retained exit route.
        const bool linked_pioneer_exit=entity.runtime_model_class_id==18U&&
            entity.production_linked_to_parent&&entity.combat_movement_initialized;
        // A metadata-only actor destination uses the bounded Lab adapter for
        // both routing and physical admission. Native orders and authored
        // hierarchies use the exact native callback and collision rules.
        const bool native_follow=run.level_runtime_enabled&&
            lab_has_native_movement_context(entity)&&
            (simulation_lab_combat_command_class(entity)||linked_pioneer_exit)&&
            simulation_lab_prepare_follow_motion(run,field,i,static_navigation_blockers_,lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get()),true);
        Vec3f desired{};
        if(native_follow)desired=*entity.actor.move_target;
        else if (entity.production_linked_to_parent && entity.actor.move_target) {
            // Class 13 owns this child until its recovered exit movement completes. Do not let
            // ordinary Lab target acquisition steal the command on the first spawned tick.
            desired = *entity.actor.move_target;
            if(factory_exit&&entity.production_parent_entity_index)
                desired=run.entities[*entity.production_parent_entity_index].production_exit_world.value_or(desired);
            entity.target_entity_index.reset();
        } else {
            if (entity.production_linked_to_parent) {
                entity.production_linked_to_parent = false;
                entity.production_parent_entity_index.reset();
            }
                if (run.level_runtime_enabled) {
                    // Retail military commands persist across owner movement ticks.
                    // This command-only path also owns human and not-yet-dispatched units.
                    // A missing AI callback is not permission to invent a nearest-hostile chase.
                    // Keep that simplified behavior solely for non-level API fixtures below.
                    if (entity.target_entity_index &&
                        *entity.target_entity_index < run.entities.size() &&
                        !run.entities[*entity.target_entity_index].root_model_terminal &&
                        simulation_lab_has_live_root_model_owner(
                            run.entities[*entity.target_entity_index]) &&
                        !(entity.combat_command_mode_flags&8U) &&
                        entity.combat_command_mode_flags==0U) {
                        // Imported targets without command state retain the legacy adapter.
                        // A resolved attack command owns movement through its route callbacks.
                        entity.actor.move_target = run.entities[*entity.target_entity_index].actor.position;
                    }
                    if (!entity.actor.move_target) {
                        service_presentation(before, previous_yaw, false, previous_contact_serial);
                        continue;
                    }
                    desired = *entity.actor.move_target;
                } else {
                    auto target = nearest_enemy(run, i);
                    if (target != entity.target_entity_index) {
                        entity.target_entity_index = target;
                        if (target) ++run.metrics.target_assignments;
                    }
                    if (!target || *target >= run.entities.size()) {
                        entity.actor.move_target.reset();
                        entity.actor.move_waypoint.reset();
                        entity.navigation_target_cell_valid = false;
                        entity.navigation_direct_route_valid = false;
                        service_presentation(before, previous_yaw, false, previous_contact_serial);
                        continue;
                    }
                    desired = run.entities[*target].actor.position;
                    entity.actor.move_target = desired;
                }

        }

        // V574 mirrors the first invariant of EntityPathing_ResolveDestinationAndRebuildRoutes:
        // a command never leaves the actor chasing an out-of-grid raw world coordinate. Retail
        // converts the requested point to GridScratch, clamps it to the one-cell inner boundary,
        // and stores the resolved/fallback coordinate in the movement runtime. Previously the Lab
        // clipped only the navigation cell while actor.move_target retained the raw point, so a
        // Pioneer bootstrap command could route to the map edge and then continue driving beyond it.
        if (entity.runtime_model_class_id == 18U || entity.has_traversal_profile) {
            const auto raw_target_cell = v519_path_world_to_scratch(
                float_to_q12(desired.x), float_to_q12(desired.y));
            const auto bounded_target_cell = v519_path_clip_to_inner_scratch(field, raw_target_cell);
            if (bounded_target_cell != raw_target_cell) {
                const auto bounded_world = v519_path_scratch_center_to_world_q12(bounded_target_cell);
                desired.x = q12_to_float(bounded_world.x_q12);
                desired.y = q12_to_float(bounded_world.y_q12);
                if (entity.actor.move_target) entity.actor.move_target = desired;
                ++run.metrics.navigation_destination_retargets;
            }
        }

        if (entity.has_traversal_profile&&!native_follow) {
            constexpr std::uint32_t kNavigationRecheckTicks = 8U;
            const auto target_cell = v519_path_clip_to_inner_scratch(field,
                v519_path_world_to_scratch(float_to_q12(desired.x), float_to_q12(desired.y)));
            const bool target_cell_changed = !entity.navigation_target_cell_valid ||
                target_cell != entity.navigation_target_cell;
            const bool cached_route_available = entity.navigation_direct_route_valid ||
                entity.actor.move_waypoint.has_value();
            // An active native route segment stays owned until its point is
            // consumed. A periodic direct-line check must not replace it early.
            const bool recheck_due = run.tick >= entity.navigation_recheck_tick &&
                !entity.actor.move_waypoint;
            if(!target_cell_changed&&!cached_route_available&&!recheck_due) {
                ++run.metrics.navigation_deferred_route_solves;
                service_presentation(before,previous_yaw,false,previous_contact_serial);
                continue;
            }
            if (target_cell_changed || !cached_route_available || recheck_due) {
                std::vector<V528PathRuntimeBlocker> blockers=static_navigation_blockers_;
                blockers.reserve(run.entities.size());
                for (std::size_t j = 0; j < run.entities.size(); ++j) {
                    if (j == i ||
                        !simulation_lab_has_live_root_model_owner(run.entities[j])) continue;
                    // The commanded combat target is the route destination, not an obstacle to its
                    // own destination. Contact/stand-off remains owned by the collision and weapon
                    // layers after movement. Treating the target as a dynamic path blocker forced a
                    // full-grid route around every ordinary attack command and dominated the 20 Hz
                    // Lab budget without adding retail evidence.
                    if (entity.target_entity_index && *entity.target_entity_index == j) continue;
                    if (military_factory_parent_pair_v599(i,j,
                            entity.production_linked_to_parent,entity.production_parent_entity_index,
                            run.entities[j].production_linked_to_parent,
                            run.entities[j].production_parent_entity_index)) continue;
                    if ((entity.linked_entity_index && *entity.linked_entity_index == j) ||
                        (run.entities[j].linked_entity_index &&
                         *run.entities[j].linked_entity_index == i)) continue;
                    const auto radius = run.entities[j].motion.collision_radius_q12;
                    if (radius == 0) continue;
                    const auto p = run.entities[j].actor.position;
                    blockers.push_back({float_to_q12(p.x), float_to_q12(p.y), radius,simulation_lab_route_blocker_class(run.entities[j])});
                }
                const auto masks = v519_path_masks(entity.faction_index,
                    entity.traversal_low_band_index, entity.traversal_high_band_index);
                const auto source_cell = v519_path_clip_to_inner_scratch(field,
                    v519_path_world_to_scratch(float_to_q12(entity.actor.position.x),
                                               float_to_q12(entity.actor.position.y)));
                const bool direct_clear = masks.traversal_bands_valid &&
                    simulation_lab_path_world_segment_clear(
                        {float_to_q12(entity.actor.position.x),float_to_q12(entity.actor.position.y)},
                        {float_to_q12(desired.x),float_to_q12(desired.y)},blockers,entity.motion.collision_radius_q12) &&
                    v565_path_segment_clear_static_cached(field, source_cell, target_cell,
                        masks.faction_presence_mask, masks.high_cost_mask, masks.blocking_mask,
                        blockers, entity.motion.collision_radius_q12, lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get()));
                if (direct_clear) {
                    ++run.metrics.navigation_route_resolves;
                    entity.navigation_target_cell = target_cell;
                    entity.navigation_target_cell_valid = true;
                    entity.navigation_recheck_tick = run.tick + kNavigationRecheckTicks +
                        static_cast<std::uint32_t>(i & 3U);
                    entity.navigation_direct_route_valid = true;
                    entity.actor.move_waypoint.reset();
                } else {
                    if (routed_solves_this_tick >= kMaximumRoutedSolvesPerTick ||
                        i < run.navigation_routed_solve_cursor) {
                        ++run.metrics.navigation_deferred_route_solves;
                        // A changed destination may not reuse a waypoint computed for the old target.
                        if (target_cell_changed) entity.actor.move_waypoint.reset();
                        entity.navigation_target_cell = target_cell;
                        entity.navigation_target_cell_valid = true;
                        entity.navigation_direct_route_valid = false;
                        entity.navigation_recheck_tick = run.tick + 1U;
                        if (!entity.actor.move_waypoint) {
                            service_presentation(before, previous_yaw, false, previous_contact_serial);
                            continue;
                        }
                        ++run.metrics.navigation_cached_ticks;
                    } else {
                        ++routed_solves_this_tick;
                        run.navigation_routed_solve_cursor=(i+1U)%run.entities.size();
                        ++run.metrics.navigation_routed_solves;
                        const auto route = v565_resolve_route_fast_for_simulation_lab(field,
                            float_to_q12(entity.actor.position.x), float_to_q12(entity.actor.position.y),
                            float_to_q12(desired.x), float_to_q12(desired.y),
                            masks.faction_presence_mask, entity.traversal_low_band_index,
                            entity.traversal_high_band_index, blockers,
                            entity.motion.collision_radius_q12, &lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get()), true);
                        ++run.metrics.navigation_route_resolves;
                        if (!route.usable()||(factory_exit&&route.requested_cell_retargeted)) {
                            if(factory_exit) ++run.metrics.factory_exit_route_retries;
                            // A dense group can temporarily have no admitted
                            // escape. Keep the command while neighboring units
                            // move away, and retry at the bounded recheck tick.
                            // An unsuccessful solve is not command completion.
                            entity.actor.move_waypoint.reset();
                            entity.navigation_target_cell=target_cell;
                            entity.navigation_target_cell_valid = true;
                            entity.navigation_direct_route_valid = false;
                            entity.navigation_recheck_tick=run.tick+kNavigationRecheckTicks+
                                static_cast<std::uint32_t>(i&3U);
                            service_presentation(before, previous_yaw, false, previous_contact_serial);
                            continue;
                        }
                        if (route.requested_cell_retargeted) {
                            desired.x = q12_to_float(route.resolved_world_q12.x_q12);
                            desired.y = q12_to_float(route.resolved_world_q12.y_q12);
                            if (entity.actor.move_target) entity.actor.move_target = desired;
                            entity.navigation_target_cell = route.resolved_cell;
                            ++run.metrics.navigation_destination_retargets;
                        } else {
                            entity.navigation_target_cell = target_cell;
                        }
                        entity.navigation_target_cell_valid = true;
                        // Stagger bounded refreshes across entities so one generation does not rebuild
                        // every active route at once. A target entering a new scratch cell still forces an
                        // immediate solve on that entity.
                        entity.navigation_recheck_tick = run.tick + kNavigationRecheckTicks +
                            static_cast<std::uint32_t>(i & 3U);
                        entity.navigation_direct_route_valid = !route.primary_world_q12.has_value();
                        entity.actor.move_waypoint.reset();
                        if (route.primary_world_q12) {
                            entity.actor.move_waypoint = Vec3f{
                                q12_to_float(route.primary_world_q12->x_q12),
                                q12_to_float(route.primary_world_q12->y_q12),
                                entity.actor.position.z};
                        }
                    }
                }
            } else {
                ++run.metrics.navigation_cached_ticks;
            }
        }
        // Keep the local factory exit as an intermediate point while still inside the parent footprint.
        if(factory_exit&&!entity.actor.move_waypoint) entity.actor.move_waypoint=desired;
        const bool had_waypoint = entity.actor.move_waypoint.has_value();
        const auto target_before=entity.actor.move_target,waypoint_before=entity.actor.move_waypoint;
        bool follow_step_blocked=false;
        bool moved = entity.runtime_model_class_id == 3U
            ? simulation_tick_articulated_actor_motion(entity.actor, entity.motion,
                  entity.articulated_contact_interpolation_rate_q12,config_.runtime_callback_step_ticks)
            : entity.runtime_model_class_id == 17U
                ? simulation_tick_class17_actor_motion(entity.actor,entity.motion,
                      config_.runtime_callback_step_ticks)
                : simulation_tick_actor_motion(entity.actor, entity.motion,
                      config_.runtime_callback_step_ticks);
        // A provisional kinematic snap is not an arrival for contact-owned
        // geometry. Keep the point available while placing the moving foot;
        // consume it only after the admitted support root actually reaches it.
        const bool articulated_waypoint_consumed=articulated_tick_start&&had_waypoint&&
            !entity.actor.move_waypoint;
        if(articulated_waypoint_consumed)entity.actor.move_waypoint=waypoint_before;
        std::optional<std::size_t> collision_owner;
        if(moved&&(before.x!=entity.actor.position.x||before.y!=entity.actor.position.y)&&
           !lab_actual_step_allowed(run,entity,field,i,before,entity.actor.position,
                static_navigation_blockers_,lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get()),&collision_owner)) {
            if(collision_owner)notify_native_collision(*collision_owner,before);
            entity.actor.position=before;
            entity.actor.ground_advance_q12=0;
            entity.actor.move_target=target_before;entity.actor.move_waypoint=waypoint_before;
            entity.navigation_direct_route_valid=false;entity.navigation_target_cell_valid=false;
            entity.navigation_recheck_tick=run.tick+1U;
            ++run.metrics.navigation_actual_step_blocks;moved=false;follow_step_blocked=true;
        }
        // A class-17 active waypoint can equal the current XY. Native still
        // services its activation metric and keeps the resulting speed for
        // the next segment, even though this step did not translate or turn.
        service_presentation(before,previous_yaw,moved,previous_contact_serial,
            native_follow&&entity.runtime_model_class_id==17U);
        // Native ArmyRuntime_UpdateMovementAndWaypoints consumes an arrived
        // destination without rewriting the entity's path/tracked coordinate
        // pairs. Only explicit Stop Movement and selection-position drag call
        // the separate coordinate-anchor writers (move_orders.cpp,
        // selection_set.cpp). In particular, ordinary arrival must not make
        // the weapon's special tracked-endpoint shot-lead branch eligible.
        if(articulated_waypoint_consumed&&entity.actor.move_waypoint&&
           std::hypot(entity.actor.position.x-waypoint_before->x,
                      entity.actor.position.y-waypoint_before->y)<=1.F/64.F)
            entity.actor.move_waypoint.reset();
        if(native_follow)simulation_lab_finish_follow_motion(entity,
            had_waypoint&&!entity.actor.move_waypoint,
            follow_step_blocked);
        const auto& follow=entity.combat_movement;
        const auto position_x=float_to_q12(entity.actor.position.x);
        const auto position_y=float_to_q12(entity.actor.position.y);
        // Concrete native models consume REACHED in the next movement callback,
        // before attempting another physical step. Clearing ACTIVE here admits
        // the AI world job one tick early and can replace the retained route.
        if(native_follow && !entity.runtime_model_hierarchy &&
           had_waypoint && !entity.actor.move_waypoint &&
           (entity.combat_command_mode_flags&3U)==0U &&
           !follow_step_blocked && follow.queue_count==0U &&
           movement_abs(position_x,follow.fallback.x)<0x40U &&
           movement_abs(position_y,follow.fallback.y)<0x40U &&
           movement_abs(position_x,follow.primary.x)<=0x1000U &&
           movement_abs(position_y,follow.primary.y)<=0x1000U){
            entity.actor.move_target.reset();
            entity.combat_movement.flags&=~0xF9U;
            entity.combat_follow_owned=false;
        }
        if (had_waypoint && !entity.actor.move_waypoint) {
            // The primary segment was consumed. Resolve the next segment on the following tick; a
            // cached direct-route bit must never outlive a routed waypoint.
            entity.navigation_direct_route_valid = false;
            entity.navigation_recheck_tick = run.tick + 1U;
        }
        // Publish ordinary arrival before this tick's AI world job. The native
        // waypoint callback clears these flags when it consumes the endpoint.
        if(run.level_runtime_enabled && target_before && !entity.actor.move_target &&
           !entity.actor.move_waypoint && !follow_step_blocked && !native_follow){
            auto& movement=entity.combat_movement;
            const MovementPoint position{float_to_q12(entity.actor.position.x),
                                         float_to_q12(entity.actor.position.y)};
            // The factory adapter can finish the exit and its identical queued
            // rally in one arrival. Consume that entry in the movement image too.
            if((movement.flags&8U)&&movement.queue_count==1U&&movement.queue[0].x==position.x&&
               movement.queue[0].y==position.y)
                movement.queue_count=0U;
            movement.flags&=~0xF9U;
        }
        if (entity.production_parked_on_pad && entity.actor.move_target)
            entity.production_parked_on_pad = false;
        if (entity.production_linked_to_parent && !entity.production_parked_on_pad &&
            !entity.actor.move_target&&!factory_exit) {
            entity.production_linked_to_parent = false;
            entity.production_parent_entity_index.reset();
        }
        if (moved) {
            const float dx = entity.actor.position.x - before.x;
            const float dy = entity.actor.position.y - before.y;
            const auto distance=std::hypot(dx, dy);
            run.metrics.movement_distance_world += distance;
            const auto cls=entity.runtime_model_class_id;
            if(cls==1U||cls==2U||cls==3U||cls==17U||cls==19U)
                run.metrics.military_movement_distance_world += distance;
        }
    }
    if(!only_owner&&routed_solves_this_tick==0U)run.navigation_routed_solve_cursor=0U;
}

// ModelRuntimeHierarchy_ComputeConditionRatioQ12 (retail 0x52A690).
// Missing/cyclic synthetic component references return zero; valid authored
// trees retain the recursive child average and signed 64-bit product.
std::int32_t simulation_lab_hierarchy_condition_ratio_q12(
        const SimulationLabEntity& entity) noexcept {
    if (entity.model_components.empty()) {
        if (entity.root_model_maximum_integrity<=0) return 0;
        return static_cast<std::int32_t>(static_cast<std::int64_t>(entity.root_model_integrity)*4096/
                                        entity.root_model_maximum_integrity);
    }
    const auto ratio=[&](auto&& self,std::size_t index,std::size_t depth)->std::int32_t {
        if(index>=entity.model_components.size()||depth>=entity.model_components.size()) return 0;
        const auto& node=entity.model_components[index];
        if(node.runtime_removed||node.maximum_integrity<=0) return 0;
        std::int32_t sum=4096,count=1;
        for(std::size_t child=0;child<entity.model_components.size();++child)
            if(entity.model_components[child].parent_component_index==index&&
               !entity.model_components[child].runtime_removed) {
                sum=military_add_v599(sum,self(self,child,depth+1U)); ++count;
            }
        return static_cast<std::int32_t>((static_cast<std::int64_t>(node.integrity)*sum)/
            (static_cast<std::int64_t>(count)*node.maximum_integrity));
    };
    return ratio(ratio,entity.root_model_component_index,0U);
}

void simulation_lab_append_military_order_trace(
        SimulationLabRun& run,SimulationLabMilitaryOrderTrace trace) noexcept {
    trace.sequence=run.next_military_order_trace_sequence++;
    const auto slot=static_cast<std::size_t>(run.military_order_trace_write_index) %
        kSimulationLabMilitaryOrderTraceCapacity;
    run.military_order_trace[slot]=trace;
    run.military_order_trace_write_index=static_cast<std::uint32_t>(
        (slot+1U)%kSimulationLabMilitaryOrderTraceCapacity);
    if(run.military_order_trace_count<kSimulationLabMilitaryOrderTraceCapacity)
        ++run.military_order_trace_count;
    else
        ++run.military_order_trace_overwrites;
}

namespace {
SimulationLabMilitaryOrderTrace military_order_trace_from_entity(
        const SimulationLabRun& run,const SimulationLabEntity& entity,
        std::size_t source_index,SimulationLabMilitaryOrderWorkspace workspace,
        std::size_t workspace_entry_index,std::optional<std::size_t> target_index,
        MovementPoint requested,std::int32_t selected_score,
        std::uint32_t collected_count=0U,std::uint32_t accumulated_strength=0U,
        bool target_from_unseen=false) noexcept {
    SimulationLabMilitaryOrderTrace trace;
    trace.tick=run.tick;
    trace.faction_index=entity.faction_index;
    trace.source_entity_index=source_index<=UINT32_MAX
        ? static_cast<std::uint32_t>(source_index):UINT32_MAX;
    trace.workspace=workspace;
    trace.workspace_entry_index=workspace_entry_index<=UINT32_MAX
        ? static_cast<std::uint32_t>(workspace_entry_index):UINT32_MAX;
    trace.target_entity_index=target_index&&*target_index<=UINT32_MAX
        ? static_cast<std::uint32_t>(*target_index):UINT32_MAX;
    trace.collected_count=collected_count;
    trace.accumulated_strength=accumulated_strength;
    trace.selected_score=selected_score;
    trace.best_site_score=entity.military_ai_last_choice.best_site_score;
    trace.best_anchor_score=entity.military_ai_last_choice.best_anchor_score;
    trace.best_hostile_score=entity.military_ai_last_choice.best_hostile_score;
    trace.requested=requested;
    trace.primary=entity.combat_movement.primary;
    trace.fallback=entity.combat_movement.fallback;
    trace.stored=entity.combat_movement.stored;
    trace.command_mode=entity.combat_command_mode_flags;
    trace.command_generation=entity.combat_command_generation;
    trace.movement_flags=entity.combat_movement.flags;
    trace.cooldown=entity.military_ai_busy_cooldown;
    trace.route_started=(entity.combat_movement.flags&1U)!=0U;
    trace.direct_route=entity.navigation_direct_route_valid;
    trace.target_owner_live=target_index&&*target_index<run.entities.size()&&
        simulation_lab_command_target_owner_available(run.entities[*target_index]);
    trace.target_from_unseen=target_from_unseen;
    return trace;
}
} // namespace

// V599: economy/unit wheel dispatch of AiUnitBehavior_SelectBestAnchorAction.
// The retained Lab workspace05 is a source-derived but incomplete classification
// (unknown retail 4x scratch owner influence). Visible ws07 is built from live
// scenario fog and relations. No synthesized workspace03 host addresses are used.
void SimulationLabExperiment::step_military_ai(SimulationLabRun& run, const FieldGrid& field,
                                                std::uint32_t faction) {
    if (!run.level_runtime_enabled || production_assets_.empty()) return;
    if (faction == 0 || faction >= run.faction_economy.size()) return;
    // AiUnitBehavior_UpdateOwnUnits traverses only the bounded workspace 01,
    // in linked world-owner order. Do not dispatch the entire entity array:
    // units beyond the first 64 admitted owners are not visited this cycle,
    // even when an admitted unit is busy, locked, or of another model class.
    // Retain one snapshot for both this dispatch and workspace 07 selection.
    const auto owner_ws=lab_ai_owner_workspaces_v718(run,field,faction);
    lab_retain_unseen_workspace(run,owner_ws);
    const bool unseen_zero_alias=military_unseen_zero_alias(owner_ws.unseen03.size(),
        run.ai_unseen03_written_prefix);
    const MilitaryScoreParametersV599 params{
        static_cast<std::int32_t>(run.ai_parameters[0x23U]),
        static_cast<std::int32_t>(run.ai_parameters[0x27U]),
        static_cast<std::int32_t>(run.ai_parameters[0x33U]),
        static_cast<std::int32_t>(run.ai_parameters[0x37U]),
        static_cast<std::int32_t>(run.ai_parameters[0x43U]),
        static_cast<std::int32_t>(run.ai_parameters[0x47U])};
    std::vector<MilitarySiteV599> sites;
    bool sites_prepared=false;
    // AiPlanningRebuild_CollectWorldEntities: 128 visible / 512 unseen
    // candidates. AiPlanningRebuild_CollectTargets: first 64 visible only.
    // Do not silently extend workspace07 to every visible enemy when the
    // world contains many actors; this changes the selected group target.
    const auto& visible_entities=owner_ws.targets07;
    const auto& unseen_entities=owner_ws.unseen03;
    std::vector<MilitarySiteV599> visible_points;
    visible_points.reserve(visible_entities.size());
    for (const auto index:visible_entities) {
        const auto& enemy=run.entities[index];
        visible_points.push_back({float_to_q12(enemy.actor.position.x),
                                  float_to_q12(enemy.actor.position.y),0});
    }
    std::vector<std::size_t> collected;
    collected.reserve(64);
    for (const auto i:owner_ws.units01) {
        auto& army=run.entities[i];
        const auto cls=army.runtime_model_class_id;
        if(cls==18U&&runtime_catalog_){
            // The locked factory exit still owns the native movement record.
            if(army.production_linked_to_parent||
               !pioneer_ai_dispatch_ready(army.pioneer_movement.flags,army.pioneer_ai_command_cooldown_ticks)){
                ++army.pioneer_ai_busy_skipped_calls;
                continue;
            }
            initialize_pioneer_movement(army);
            const auto blockers=pioneer_route_blockers(run,army,i,static_navigation_blockers_);
            LabPioneerRouteDependencies deps{run,army,field,blockers};
            deps.runtime_cache=&lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get());
            deps.native_statics=static_navigation_blockers_;
            deps.owner_index=i;
            service_pioneer_behavior(deps,runtime_catalog_.get(),static_placement_owners_,true);
            continue;
        }
        if(cls!=1U&&cls!=2U&&cls!=3U&&cls!=19U&&cls!=17U)continue;
        army.military_ai_source_dispatch=true;
        const auto flags=army.combat_movement.flags|
            (army.actor.move_target?1U:0U)|(army.production_linked_to_parent?2U:0U);
        if(!simulation_lab_ai_unit_behavior_ready(flags,army.military_ai_busy_cooldown)){
            ++run.metrics.military_ai_busy_skips;
            continue;
        }
        if(!sites_prepared){
            const auto workspaces=lab_rebuild_placement_workspaces(run,faction,field,true);
            sites.reserve(workspaces.general05.size());
            for(const auto& site:workspaces.general05)
                sites.push_back({float_to_q12(site.point.x),float_to_q12(site.point.y),site.score});
            sites_prepared=true;
        }
        if (const auto* source=production_asset(army.army_asset_id)) {
            const auto weights=military_arm_weights_v602(source->ai_definition_class_value70,
                source->ai_definition_class_value74,source->ai_definition_class_value78);
            army.military_site_weight=weights.site;
            army.military_anchor_weight=weights.anchor;
            army.military_secondary_weight=weights.secondary;
        }
        ++run.metrics.military_ai_selector_calls;
        simulation_lab_sync_follow(army);
        const auto choice=military_choose_anchor_action_v599(
            float_to_q12(army.actor.position.x),float_to_q12(army.actor.position.y),params,
            army.military_site_weight,army.military_anchor_weight,army.military_secondary_weight,
            sites,run.ai_primary_impact_anchor[faction],run.ai_secondary_impact_anchor[faction],
            visible_points,!unseen_entities.empty(),MilitaryPositionQ12{army.combat_movement.fallback.x,army.combat_movement.fallback.y},unseen_zero_alias);
        army.military_ai_last_choice=choice;
        if(const auto trace=std::getenv("THANDOR_SIMLAB_MILITARY_TRACE_TICK");trace&&std::strtoul(trace,nullptr,10)==run.tick)
            std::fprintf(stderr,"MILITARY_SELECT tick=%u slot=%zu cooldown=%d grouped=%u flags=%u kind=%u site=%d anchor=%d secondary=%d condition=%d\n",
                run.tick,army.native_army_pool_slot,army.military_ai_busy_cooldown,unsigned(army.military_ai_group_assigned),
                flags,choice.kind,choice.best_site_score,choice.best_anchor_score,choice.best_hostile_score,
                simulation_lab_hierarchy_condition_ratio_q12(army));
        if (choice.kind==1U && choice.site_index<sites.size()) {
            if (army.target_entity_index && *army.target_entity_index<run.entities.size() &&
                simulation_lab_has_live_root_model_owner(
                    run.entities[*army.target_entity_index]) &&
                !run.entities[*army.target_entity_index].root_model_terminal)
                ++run.metrics.military_ai_site_orders_with_live_target;
            const auto point=sites[choice.site_index];
            sites[choice.site_index].score=0;
            simulation_lab_set_site_route(run,field,i,
                {point.x_q12,point.y_q12},static_navigation_blockers_,lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get()));
            army.military_ai_group_assigned=false;
            army.military_ai_command_kind=1;
            army.military_ai_busy_cooldown=8;
            simulation_lab_append_military_order_trace(run,military_order_trace_from_entity(
                run,army,i,SimulationLabMilitaryOrderWorkspace::General05,
                choice.site_index,std::nullopt,{point.x_q12,point.y_q12},choice.score));
            ++run.metrics.military_ai_site_orders;
        } else if (choice.kind==2U) {
            if (army.target_entity_index && *army.target_entity_index<run.entities.size() &&
                simulation_lab_has_live_root_model_owner(
                    run.entities[*army.target_entity_index]) &&
                !run.entities[*army.target_entity_index].root_model_terminal)
                ++run.metrics.military_ai_anchor_orders_with_live_target;
            const bool use_primary=run.ai_primary_impact_anchor[faction].cooldown!=0;
            const auto& anchor=use_primary
                ? run.ai_primary_impact_anchor[faction] : run.ai_secondary_impact_anchor[faction];
            simulation_lab_set_site_route(run,field,i,
                {anchor.model_x_q12,anchor.model_y_q12},static_navigation_blockers_,lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get()));
            army.military_ai_group_assigned=false;
            army.military_ai_command_kind=2;
            army.military_ai_busy_cooldown=8;
            simulation_lab_append_military_order_trace(run,military_order_trace_from_entity(
                run,army,i,SimulationLabMilitaryOrderWorkspace::FactionAnchor,
                use_primary?0U:1U,std::nullopt,{anchor.model_x_q12,anchor.model_y_q12},choice.score));
            ++run.metrics.military_ai_anchor_orders;
        } else if (!army.military_ai_group_assigned && collected.size()<64U) {
            collected.push_back(i);
        }
    }
    // AiUnitGroup_AssignCollectedEntitiesToBestTarget: at least two units, a
    // summed floor((health ratio Q12 << 8)/4096) of 0x200, then best ws07
    // class score (ties take the LATER candidate). When ws07 is empty, the
    // native 16-byte read of 8-byte ws03 records aliases ws03[2*i+1],
    // including the retained tail from earlier faction rebuilds.
    if (collected.size()<2U || (visible_entities.empty()&&unseen_entities.empty())) return;
    std::uint32_t strength=0;
    bool strong_enough=false;
    for (const auto idx:collected) {
        const auto& e=run.entities[idx];
        const auto ratio=simulation_lab_hierarchy_condition_ratio_q12(e);
        strength+=static_cast<std::uint32_t>(ratio)*256U/4096U;
        if (strength>=0x200U) { strong_enough=true; break; }
    }
    if (!strong_enough) return;
    std::size_t best=run.entities.size();
    std::size_t best_workspace_index=SIZE_MAX;
    bool best_from_unseen=false;
    std::uint32_t score=0;
    if(!visible_entities.empty()) {
        for (std::size_t workspace_index=0;workspace_index<visible_entities.size();++workspace_index) {
            const auto idx=visible_entities[workspace_index];
            const auto cls=run.entities[idx].runtime_model_class_id;
            if (cls>=kMilitaryGroupClassScoresV599.size()) continue;
            const auto val=kMilitaryGroupClassScoresV599[cls];
            if (val>=score) { score=val; best=idx; best_workspace_index=workspace_index; }
        }
    }else {
        // Resolve MODEL pool identities at read time: the retained entry can
        // alias a new model after its original owner has been released.
        std::vector<uint32_t> classes(run.ai_unseen03_model_slots.size(),UINT32_MAX);
        std::vector<size_t> owners(classes.size(),SIZE_MAX);
        for(size_t i=0;i<run.entities.size();++i)for(const auto& component:run.entities[i].model_components){
            if(component.native_model_pool_slot==SIZE_MAX)continue;
            for(size_t slot=1;slot<classes.size()&&slot<2U*unseen_entities.size();slot+=2U)
                if(run.ai_unseen03_model_slots[slot]==component.native_model_pool_slot){
                    classes[slot]=component.runtime_class_id;owners[slot]=i;
                }
        }
        if(const auto selected=military_retained_unseen_group_target(classes,unseen_entities.size())){
            best=owners[*selected];best_workspace_index=*selected;best_from_unseen=true;
            score=kMilitaryGroupClassScoresV599[classes[*selected]];
            ++run.metrics.military_ai_unseen_group_targets_bounded;
        }else ++run.metrics.military_ai_unseen_group_targets_unavailable;
    }
    if (score==0 || best>=run.entities.size()) return;
    for (const auto idx:collected) {
        auto& e=run.entities[idx];
        simulation_lab_set_group_target_route(run,field,idx,best,
            static_navigation_blockers_,lab_navigation_cache(run,field,navigation_terrain_cache_,runtime_catalog_.get()));
        e.military_ai_command_kind=3;
        e.military_ai_group_assigned=true;
        e.military_ai_busy_cooldown=8;
        const auto target_point=MovementPoint{
            float_to_q12(run.entities[best].actor.position.x),
            float_to_q12(run.entities[best].actor.position.y)};
        simulation_lab_append_military_order_trace(run,military_order_trace_from_entity(
            run,e,idx,best_from_unseen
                ?SimulationLabMilitaryOrderWorkspace::BoundedUnseen03
                :SimulationLabMilitaryOrderWorkspace::Visible07,
            best_workspace_index,best,target_point,static_cast<std::int32_t>(score),
            static_cast<std::uint32_t>(collected.size()),strength,best_from_unseen));
        ++run.metrics.military_ai_group_orders;
        ++run.metrics.target_assignments;
    }
}

void SimulationLabExperiment::step_run(const FieldGrid& source_field, std::size_t run_index,
                                           std::uint32_t ticks) {
    if (run_index >= runs_.size() || ticks == 0U) return;
    auto& run = runs_[run_index];
    if(run.level_runtime_enabled && runtime_catalog_){
        if(!run.runtime_field){
            run.runtime_field=std::make_shared<FieldGrid>(source_field);
            // The native level loader computes interior normals once.
            // Construction stamps change heights without rebuilding those normals.
            run.runtime_field->capture_runtime_normals();
        }
        else if(!run.runtime_field.unique())run.runtime_field=std::make_shared<FieldGrid>(*run.runtime_field);
    }
    const auto& field=run.runtime_field?*run.runtime_field:source_field;
    lab_prepare_missing_model_geometry(run,field,runtime_catalog_.get());
    // Startup derives the retained scratch image from the original field
    // before the first planning callback, like the in-game session loader.
    lab_initialize_persistent_scratch(run,field,runtime_catalog_.get(),static_placement_owners_);
    for (std::uint32_t step_index = 0; step_index < ticks && !run.finished; ++step_index) {
        ++run.tick;run.army_pool.tick=run.tick;
        // Native interaction/reduced updates advance the session clock without
        // production, research, combat or AI callbacks. Contact is presentation-owned.
        if(run.level_runtime_enabled && run.reduced_update) {
            if(run.tick>=config_.duration_ticks)run.finished=true;
            continue;
        }
        if(run.level_runtime_enabled)run.native_world_objects.begin_tick(run.tick);
        {
            const auto world_scope=run.native_world_objects.enter_commands();
            apply_player_commands(run,field);
        }
        // Local placement commands precede primary owner maintenance. Their new
        // effects advance now; effects started by the later AI world job wait
        // until the next full tick, matching the native owner-list traversal.
        advance_pioneer_conversions(run);
        recompute_resource_storage_limits(run);
        std::vector<std::size_t> serviced_model_emitters;
        std::vector<std::size_t> serviced_model_callbacks;
        run.defer_factory_exit_routes=run.level_runtime_enabled;
        run.deferred_factory_exit_routes.clear();
        // Existing shots are usually newer than the model owners they can hit.
        // Resolve against the retained transforms before this tick moves them.
        if(run.level_runtime_enabled){
            lab_prepare_missing_model_geometry(run,field,runtime_catalog_.get());
            advance_projectiles(run,field,&serviced_model_emitters,&serviced_model_callbacks);
        }
        // Newer completion effects can unlink an older emitter before its callback.
        if(run.level_runtime_enabled)advance_terminal_attachment_effects(run,field,true);
        const auto animated_hulls_before_callbacks=run.level_runtime_enabled&&runtime_catalog_
            ?simulation_lab_current_shot_hulls(run,field,*runtime_catalog_,false,true)
            :std::vector<SimulationLabLiveShotHull>{};
        // Producers service an already-active build before AI planning. A purchase created on this
        // tick may then be accepted below, but its build timer cannot advance until the next tick.
        std::vector<DeferredEmitter> deferred_emitters;
        std::vector<std::size_t> deferred_ambient_emitters;
        run.defer_factory_exit_routes=run.level_runtime_enabled;
        std::optional<std::size_t> newest_pending_smoke;
        if(run.level_runtime_enabled&&runtime_catalog_){
            for(std::size_t i=0;i<run.entities.size();++i){
                const auto& entity=run.entities[i];
                if((entity.runtime_model_class_id!=1U&&entity.runtime_model_class_id!=2U&&entity.runtime_model_class_id!=3U&&entity.runtime_model_class_id!=17U&&entity.runtime_model_class_id!=18U&&entity.runtime_model_class_id!=19U)||
                   !entity.runtime_model_hierarchy||entity.runtime_model_hierarchy->empty()||
                   !simulation_lab_has_live_root_model_owner(entity))continue;
                const auto& p=entity.runtime_model_hierarchy->front().simulation;
                const auto batch=static_cast<int32_t>(config_.runtime_callback_step_ticks);
                const bool smoke_due=p.maximum_integrity>0&&p.damage_effect_id!=0U&&
                    entity.damage_effect_cooldown<=batch&&
                    // Water damage runs inside the movement callback. A unit
                    // crossing the smoke threshold still precedes older emitters.
                    (int64_t(entity.root_model_integrity)-lab_owner_water_damage(entity,field).value_or(0))
                        *100/p.maximum_integrity<p.damage_effect_health_threshold_percent;
                const bool timed_due=(entity.runtime_model_class_id!=17U||
                    entity.actor.class17_rotor_deploy_ticks_remaining<=1U)&&
                    entity.root_model_component_index<entity.model_components.size()&&
                    (p.timed_effect_id!=0U||p.timed_water_effect_id!=0U)&&
                    std::bit_cast<int32_t>(entity.model_components[entity.root_model_component_index].timed_effect_countdown)<=batch;
                const bool contact_due=entity.runtime_model_class_id==3U&&
                    simulation_native_walker_completes_contact(entity.actor,uint32_t(batch))&&
                    (p.timed_effect_id||p.timed_water_effect_id);
                if(!smoke_due&&!timed_due&&!contact_due)continue;
                if(!newest_pending_smoke||simulation_lab_owner_order(entity,i)>
                    simulation_lab_owner_order(run.entities[*newest_pending_smoke],*newest_pending_smoke))
                    newest_pending_smoke=i;
            }
        }
        if(run.level_runtime_enabled) {
            // Native owner nodes share one newest-first world list. Partitioning
            // static and live emitters changes which RNG draw sets each countdown.
            struct OwnerCallback {size_t index,placement;uint32_t created;bool ambient,synthetic;};
            std::vector<OwnerCallback> callbacks;
            callbacks.reserve(run.entities.size()+environmental_emitters_.size());
            for(size_t i=0;i<run.entities.size();++i){const auto& entity=run.entities[i];
                // A replacement created by a newer completion effect is not in
                // native's captured owner chain until the following tick.
                if(entity.synthetic&&entity.construction_start_tick>=run.tick)continue;
                callbacks.push_back({i,entity.source_placement_index,entity.construction_start_tick,false,entity.synthetic});}
            for(size_t i=0;i<environmental_emitters_.size();++i){
                const auto placement=environmental_emitters_[i].source_placement_index;
                callbacks.push_back({i,placement==SIZE_MAX?0U:placement,0U,true,false});}
            std::sort(callbacks.begin(),callbacks.end(),[](const auto& a,const auto& b){
                if(a.synthetic!=b.synthetic)return a.synthetic;
                if(a.synthetic&&a.created!=b.created)return a.created>b.created;
                if(a.placement!=b.placement)return a.placement>b.placement;
                if(a.ambient!=b.ambient)return !a.ambient;
                return a.index>b.index;
            });
            simulation_lab_visit_captured_owner_chain<OwnerCallback>(callbacks,
                [&](const OwnerCallback& callback){
                    return callback.ambient ||
                        (callback.index<run.entities.size()&&
                         simulation_lab_has_live_root_model_owner(run.entities[callback.index]));
                },[&](const OwnerCallback& callback) {
                    if(callback.ambient){
                        const auto order=std::tuple{false,0U,callback.placement,callback.index};
                        if(newest_pending_smoke&&order<simulation_lab_owner_order(
                            run.entities[*newest_pending_smoke],*newest_pending_smoke))
                            deferred_ambient_emitters.push_back(callback.index);
                        else advance_environmental_emitters(run,field,callback.index);
                        return;
                    }
                    const auto owner=callback.index;
                    if(std::find(serviced_model_callbacks.begin(),serviced_model_callbacks.end(),owner)!=serviced_model_callbacks.end())return;
                    auto& e=run.entities[owner];
                    release_native_factory_child_if_clear(run,owner);
                    // Native unit-factory callbacks unlink serialized child slot 3
                    // before servicing any state. It must not remain a shot collider.
                    if(e.runtime_model_class_id==13U)lab_unlink_factory_child3(run,e,field,runtime_catalog_.get());
                    const bool busy=e.production_building ||
                        (e.runtime_model_class_id==13U && e.production_behavior_state!=0U);
                    bool emitter_deferred=false;
                    // A native class switch runs once. Completing a build or closing a door
                    // cannot enter the idle case again in the same callback.
                    if(std::find(serviced_model_emitters.begin(),serviced_model_emitters.end(),owner)!=serviced_model_emitters.end()){
                        // Already serviced at its position among the shot owners.
                    } else if(e.runtime_model_class_id==17U){
                        // Banking evaluates the timed emitter at its new pose.
                    } else if(newest_pending_smoke&&e.runtime_model_class_id!=1U&&e.runtime_model_class_id!=2U&&e.runtime_model_class_id!=3U&&e.runtime_model_class_id!=18U&&e.runtime_model_class_id!=19U&&
                       simulation_lab_owner_order(e,owner)<simulation_lab_owner_order(run.entities[*newest_pending_smoke],*newest_pending_smoke)){
                        // Preserve callback eligibility before this tick advances or starts a job.
                        const bool factory=e.runtime_model_class_id==11U||e.runtime_model_class_id==13U;
                        bool active=!factory||simulation_lab_production_effect_active(e);
                        if(e.runtime_model_class_id==4U&&e.runtime_model_hierarchy&&!e.runtime_model_hierarchy->empty()){
                            const auto& profile=e.runtime_model_hierarchy->front().simulation;
                            active=profile.timed_effects_require_research==0U||(research_source_flags(e)&0x40U)!=0U;
                        }
                        deferred_emitters.push_back({owner,active});
                        emitter_deferred=true;
                    }
                    else advance_production_emitters(run,field,owner);
                    if(busy)advance_production_jobs(run,field,owner);
                    else start_production_jobs(run,owner);
                    step_airports(run,field,owner);
                    advance_research(run,owner);
                    // A wreck emits before its final sinking step can release the hierarchy.
                    if(!emitter_deferred&&runtime_catalog_&&run.entities[owner].runtime_model_class_id==12U)
                        advance_terminal_attachment_effects(run,field,false,std::nullopt,owner,true);
                });
        } else {
            advance_production_emitters(run,field);
            advance_production_jobs(run,field);
            advance_research(run);
            step_airports(run,field);
            advance_environmental_emitters(run,field);
        }
        run.defer_factory_exit_routes=false;
        if(!run.level_runtime_enabled)start_production_jobs(run);
        complete_construction(run);
        if(!run.level_runtime_enabled){advance_class23_collision_state(run);process_collision_contacts(run);}
        if(!run.level_runtime_enabled)apply_water_damage(run, field);
        const auto ground_hulls_before_movement=run.level_runtime_enabled&&runtime_catalog_
            ?simulation_lab_current_shot_hulls(run,field,*runtime_catalog_,true)
            :std::vector<SimulationLabLiveShotHull>{};
        if(!run.level_runtime_enabled||!runtime_catalog_)step_movement(run, field);
        lab_prepare_missing_model_geometry(run,field,runtime_catalog_.get());
        if(!run.level_runtime_enabled)advance_environmental_effects(run,field);
        if(run.level_runtime_enabled)
            lab_refresh_army_model_terrain_states(run,field,
                simulation_lab_terrain_refresh_source(run,field.cells().size(),true),true);
        step_combat(run,field,deferred_emitters,ground_hulls_before_movement,deferred_ambient_emitters,
            animated_hulls_before_callbacks,std::nullopt,serviced_model_callbacks);
        run.deferred_factory_exit_routes.clear();
        advance_terminal_attachment_effects(run,field);
        if(run.level_runtime_enabled)recompute_energy_generation_capacity(run);
        update_exploration_and_points(run, field);
        if (run.level_runtime_enabled && (run.tick % 20U) == 0U)
            update_scheduled_conditions(run, field, config_.runtime_callback_step_ticks);
        // The native world job runs after every model/shot/effect owner and
        // the scheduled level conditions. AI orders take effect next callback.
        // Effect-linked projectiles retain their separate pre-combat service phase.
        // Retail world-job wheel: AI dispatch on cases 2/6, one faction per 8-tick slot;
        // bit 6 alternates the unit phase (0) and economy/purchase phase (1).  The
        // compact Lab does not yet model full group assignment, but must not run purchases
        // for all factions on a fixed 64-tick boundary: this changes RNG, economy and AI.
        if ((run.tick & 7U) == 2U || (run.tick & 7U) == 6U) {
            const std::uint32_t selected_faction = (run.tick >> 3U) & 7U;
            if (run.ai_planning_enabled &&
                (!run.ai_planner_dispatch_mask || (*run.ai_planner_dispatch_mask & (1U<<selected_faction))!=0U) && ((run.tick >> 6U) & 1U) != 0U && selected_faction != 0U &&
                (!run.level_runtime_enabled ||
                 ((run.ai_controlled_faction_mask & (1U<<selected_faction))!=0U &&
                  run.faction_lifecycle_states[selected_faction] == kFactionLifecycleActive))) {
                ++run.metrics.planning_cycles;
                // Observe the exact faction/tick rather than inferring AI progress from
                // global cumulative Build/Plan counters; all seven AI indices are eligible.
                auto ws=lab_rebuild_placement_workspaces(run,selected_faction,field);
                auto diagnostic_owner_ws=lab_ai_owner_workspaces_v718(run,field,selected_faction);
                {
                    lab_retain_unseen_workspace(run,diagnostic_owner_ws);
                    simulation_lab_update_active_entity_pressure_flag(run,selected_faction,
                        diagnostic_owner_ws.structures00,diagnostic_owner_ws.units01);
                    auto& info=run.ai_workspace_diagnostics[selected_faction];
                    info.last_planning_tick=run.tick;
                    info.own_entities=info.live_pioneers=info.live_structures=
                        info.effective_presence_owners=0U;
                    for(const auto& entity:run.entities){
                        if(entity.faction_index!=selected_faction || entity.root_model_terminal)
                            continue;
                        ++info.own_entities;
                        if(entity.runtime_model_class_id==18U) ++info.live_pioneers;
                        if(entity.army_asset_id>=300U) ++info.live_structures;
                        if(entity.runtime_class_specific_19c_raw>0) ++info.effective_presence_owners;
                    }
                    info.general05=static_cast<std::uint32_t>(ws.general05.size());
                    info.flagged06=static_cast<std::uint32_t>(ws.flagged06.size());
                    info.resource08=static_cast<std::uint32_t>(ws.resource08.size());
                    info.base09=static_cast<std::uint32_t>(ws.base09.size());
                    info.large10=static_cast<std::uint32_t>(ws.large10.size());
                    // Preserve exactly the workspace-08 cells that this faction actually
                    // selected, rather than inferring candidate coordinates from all FLD
                    // deposits. This is observability only; it does not reorder candidates,
                    // change the RNG stream, or relax retail placement admission.
                    info.resource_site_sample_count = static_cast<std::uint32_t>(
                        std::min(ws.resource08.size(), info.resource_site_samples.size()));
                    // Ordered owner image is common across every site in this
                    // immutable planning snapshot. Build it once, never once per
                    // candidate. This is observation, not an inserted ARM333.
                    std::vector<PioneerPlacementOwner> support_owners;
                    support_owners.reserve(run.entities.size());
                    for (const auto& owner : run.entities) {
                        if (owner.root_model_terminal) continue;
                        support_owners.push_back({
                            {float_to_q12(owner.actor.position.x),
                             float_to_q12(owner.actor.position.y)},
                            owner.faction_index, owner.runtime_model_class_id,
                            owner.runtime_flags, owner.runtime_class_specific_19c_raw,
                            true});
                    }
                    for (std::size_t si = 0; si < info.resource_site_sample_count; ++si) {
                        const auto& site = ws.resource08[si];
                        auto& sample = info.resource_site_samples[si];
                        sample.army_asset_id = site.army_asset_id;
                        sample.point = site.point;
                        sample.priority = site.priority;
                        sample.in_build_reach = false;
                        sample.mode0_radius_with_extent = false;
                        sample.pioneer_weighted_score = 0;
                        sample.pioneer_secondary_distance_q12 = INT32_MAX;
                        sample.pioneer_visible_hostile_distance_q12 = INT32_MAX;
                        sample.pioneer_distance_q12 = INT32_MAX;
                        sample.pioneer_assigned_count = 0;
                        sample.support_only_inputs_available = false;
                        sample.support_only_owner_count = 0;
                        sample.support_only_mode3_reached = false;
                        sample.support_only_mode3_clearance_q12 = 0;
                        sample.support_only_mode0_reached = false;
                        const auto* product = production_asset(site.army_asset_id);
                        sample.placement_candidate_extent_q12 = product
                            ? product->entity_seed.runtime_class_specific_1a8_raw : 0;
                        sample.nearest_constructor_distance_q12 =
                            std::numeric_limits<std::uint32_t>::max();
                        const auto sx = float_to_q12(site.point.x);
                        const auto sy = float_to_q12(site.point.y);
                        // ModelDefinition +0x19C is the SUPPORT radius and +0x1A8
                        // is the candidate's placement margin. Both are already
                        // read from MDL. Run the source-recovered owner SUPPORT
                        // subquery on the ordered live Lab entities. This proves
                        // neither the collision/terrain gates nor real owner-list
                        // completeness, so it must never feed the AI selector.
                        if (product && product->entity_seed.runtime_model_class_id == 14U) {
                            const PioneerPlacementModel definition{
                                0, product->entity_seed.runtime_class_specific_1a8_raw,
                                static_cast<std::uint8_t>(product->entity_seed.runtime_class_specific_c0_raw)};
                            const MovementPoint candidate{sx,sy};
                            const auto mode3 = pioneer_class14_support(3U,
                                static_cast<std::int32_t>(run.ai_parameters[0x51U]),
                                candidate,definition,selected_faction,support_owners);
                            const auto mode0 = pioneer_class14_support(0U,0,
                                candidate,definition,selected_faction,support_owners);
                            sample.support_only_inputs_available = true;
                            sample.support_only_owner_count = static_cast<std::uint32_t>(support_owners.size());
                            sample.support_only_mode3_reached = mode3.accepted;
                            sample.support_only_mode3_clearance_q12 = mode3.accepted
                                ? mode3.placement_value : 0U;
                            sample.support_only_mode0_reached = mode0.accepted;
                        }
                        for (const auto& owner : run.entities) {
                            if (owner.faction_index != selected_faction || owner.root_model_terminal ||
                                owner.runtime_class_specific_19c_raw <= 0 ||
                                (owner.runtime_model_class_id == 18U &&
                                 (owner.runtime_flags & 0x18U) != 0U)) continue;
                            const std::int64_t dx = static_cast<std::int64_t>(sx) -
                                float_to_q12(owner.actor.position.x);
                            const std::int64_t dy = static_cast<std::int64_t>(sy) -
                                float_to_q12(owner.actor.position.y);
                            const auto radius = static_cast<std::int64_t>(
                                owner.runtime_class_specific_19c_raw);
                            const auto distance = static_cast<std::uint32_t>(std::min<std::int64_t>(
                                std::numeric_limits<std::uint32_t>::max(),
                                static_cast<std::int64_t>(std::sqrt(
                                    static_cast<long double>(dx) * dx +
                                    static_cast<long double>(dy) * dy))));
                            sample.nearest_constructor_distance_q12 = std::min(
                                sample.nearest_constructor_distance_q12, distance);
                            if (dx * dx + dy * dy <= radius * radius)
                                sample.in_build_reach = true;
                            const std::int64_t mode0_radius = radius +
                                sample.placement_candidate_extent_q12;
                            if (mode0_radius > 0 &&
                                dx * dx + dy * dy <= mode0_radius * mode0_radius)
                                sample.mode0_radius_with_extent = true;
                        }
                        // First live Pioneer corresponds to the actual runtime
                        // scorer (one score per candidate for each Pioneer).
                        for (const auto& pioneer : run.entities) {
                            if (pioneer.faction_index != selected_faction ||
                                pioneer.root_model_terminal ||
                                pioneer.runtime_model_class_id != 18U) continue;
                            const auto secondary = lab_nearest_entity_distance_q12(
                                run, selected_faction, field, site.point, 1U,&diagnostic_owner_ws);
                            const auto hostile = lab_nearest_entity_distance_q12(
                                run, selected_faction, field, site.point, 2U,&diagnostic_owner_ws);
                            const auto assigned = static_cast<std::uint32_t>(std::count_if(
                                run.entities.begin(), run.entities.end(),
                                [&](const SimulationLabEntity& owner) {
                                    return owner.faction_index == selected_faction &&
                                        owner.army_asset_id == site.army_asset_id &&
                                        owner.construction_complete && !owner.root_model_terminal;
                                }));
                            sample.pioneer_secondary_distance_q12 = secondary;
                            sample.pioneer_visible_hostile_distance_q12 = hostile;
                            sample.pioneer_distance_q12 = lab_axis_manhattan_q12(
                                pioneer.actor.position, site.point);
                            sample.pioneer_assigned_count = assigned;
                            sample.pioneer_weighted_score = simulation_lab_pioneer_site_weighted_score(
                                site.priority, site.army_asset_id, secondary, hostile,
                                sample.pioneer_distance_q12, assigned, run.ai_parameters);
                            break;
                        }
                    }
                }
                const auto consumed_before=run.metrics.pending_requests_consumed;
                std::optional<SimulationLabPlanningWorkspaceSnapshot> snapshot;
                struct ClearPlanningSnapshot {
                    SimulationLabRun& run;
                    ~ClearPlanningSnapshot(){run.active_planning_snapshot=nullptr;}
                } clear_snapshot{run};
                if(run.level_runtime_enabled){
                    // Native builds the workspaces before diplomacy. Pair
                    // state changes and merges affect the next rebuild.
                    auto own=simulation_lab_ai_own_workspaces(run,selected_faction);
                    const auto requested_asset_count=simulation_lab_ai_workspace04_count(run,selected_faction);
                    const auto technology_admission_mask=run.faction_technology_masks[selected_faction];
                    std::optional<Vec3f> anchor;
                    for(const auto& e:run.entities)
                        if(e.faction_index==selected_faction&&e.army_asset_id==300U&&
                           e.construction_complete&&!e.root_model_terminal){
                            anchor=e.actor.position;break;
                        }
                    simulation_lab_update_faction_relations(run,selected_faction);
                    snapshot.emplace(SimulationLabPlanningWorkspaceSnapshot{
                        selected_faction,std::move(ws),std::move(diagnostic_owner_ws),
                        std::move(own),anchor,requested_asset_count,technology_admission_mask});
                    run.active_planning_snapshot=&*snapshot;
                }
                plan_construction(run, field, selected_faction);
                // Retail stops this economy planning turn after a primary placement
                // succeeds. Purchase collection belongs only to the unsuccessful pass.
                if(run.metrics.pending_requests_consumed==consumed_before)
                    plan_autonomous_army_purchases(run, field, selected_faction);
            }
            // The retail pressure rebuild is a distinct world-job callback; the consumer
            // observes its completed image on the following appropriate planning turn.

        }
        // Retail scheduler phase 0 refreshes faction extraction/energy allocation once every
        // eight simulation ticks, after ordinary world-owner updates. The Lab currently models
        // the recovered model-component allocator portion of that pass.
        if ((run.tick & 7U) == 0U) {
            // Retail 0x5131FA..0x513207, world job 0. Decay during the
            // run, not reset(): setup has no elapsed economy step.
            ++run.metrics.impact_anchor_decay_steps;
            for (std::size_t faction=0;faction<8U;++faction) {
                ++run.faction_relation_transition_ticks[faction];
                for(auto& pressure:run.faction_pair_pressure[faction])pressure=(pressure*7U)>>3U;
                if (run.ai_primary_anchor_cooldown[faction])
                    --run.ai_primary_anchor_cooldown[faction];
                if (run.ai_anchor_cooldown0[faction])
                    --run.ai_anchor_cooldown0[faction];
                for (auto* anchor : {&run.ai_primary_impact_anchor[faction],
                                     &run.ai_secondary_impact_anchor[faction]}) {
                    if (anchor->cooldown && --anchor->cooldown==0U)
                        ++run.metrics.impact_anchor_expirations;
                }
            }
            lab_pay_connected_resource_regions(run,field,config_.runtime_callback_step_ticks);
            update_energy_allocation(run);
            if(run.level_runtime_enabled) {
                // Original world job 0: each allied pair exchanges its lowest
                // exclusive technology in both directions, in serialized pair
                // order. Both offers are chosen before either unlock recurses.
                // The native pass deliberately has no lifecycle-state gate.
                const auto exclusive=[&](uint32_t have,uint32_t lack) {
                    for(uint32_t id=0;id<256U;++id)
                        if(faction_technology_unlocked(run.faction_technology_masks[have],id) &&
                           !faction_technology_unlocked(run.faction_technology_masks[lack],id))return id;
                    return 256U;
                };
                for(uint32_t source=1;source<7U;++source)for(uint32_t other=source+1U;other<8U;++other) {
                    const auto state=relation_state(run,other,source);
                    if(state<8U || state>10U)continue;
                    const auto from_source=exclusive(source,other);
                    if(from_source==256U)continue;
                    const auto from_other=exclusive(other,source);
                    if(from_other==256U)continue;
                    unlock_technology_recursive(run,source,from_other);
                    unlock_technology_recursive(run,other,from_source);
                }
                // The native lighting world job rebuilds the cached field
                // normals when a cycle is configured. Unchanged heights
                // produce the same angles, so skip redundant recomputation.
                if(run.terrain_lighting_cycle_active&&run.runtime_normals_dirty&&run.runtime_field){
                    run.runtime_field->refresh_runtime_normals();
                    run.runtime_normals_dirty=false;
                }
            }
        }
        if (((run.tick & 7U) == 2U || (run.tick & 7U) == 6U) &&
            ((run.tick >> 6U) & 1U) == 0U) {
            const auto faction=(run.tick >> 3U) & 7U;
            if (run.ai_planning_enabled && faction != 0U &&
                (!run.ai_planner_dispatch_mask || (*run.ai_planner_dispatch_mask & (1U<<faction))!=0U) && (!run.level_runtime_enabled ||
                ((run.ai_controlled_faction_mask & (1U<<faction))!=0U &&
                 run.faction_lifecycle_states[faction] == kFactionLifecycleActive)))
                step_military_ai(run, field, faction);
        }
        lab_prepare_missing_model_geometry(run,field,runtime_catalog_.get());
        // Creation classifies new MODEL owners against the retained field image
        // before the same planning job tallies pressure from the owner list.
        if(run.level_runtime_enabled)
            lab_refresh_army_model_terrain_states(run,field,
                simulation_lab_terrain_refresh_source(run,field.cells().size(),true),true);
        if ((run.tick&7U)==2U || (run.tick&7U)==6U)
            update_ai_pressure_state_v568(run);
        if(run.level_runtime_enabled&&run.runtime_field&&((run.tick&7U)==1U||(run.tick&7U)==5U))
            run.runtime_normals_dirty|=retail_relax_water_surface(*run.runtime_field,(run.tick&7U)==5U);
        // World maintenance jobs are after the owner updates. Cases 4/7
        // publish separate immutable generations for the next planning turn.
        lab_advance_persistent_scratch_world_job(run,field,runtime_catalog_.get(),static_placement_owners_);
        // Native phase 7 order: clear/rebuild occupancy, dispatch the linked
        // MODEL terrainStateRefresh callbacks, then clamp lighting. Do not
        // classify against pre-job projection or update released model roots.
        if (run.level_runtime_enabled && (run.tick&7U)==7U) {
            const auto terrain_image=simulation_lab_terrain_refresh_source(
                run,field.cells().size(),true);
            lab_refresh_army_model_terrain_states(run,field,terrain_image);
            lab_refresh_shot_terrain_samples(run,field,terrain_image,runtime_catalog_.get());
            lab_refresh_linked_effect_terrain_states(run,field,terrain_image,runtime_catalog_.get());
        }
        if(run.level_runtime_enabled)run.native_world_objects.commit_tick();
        if (config_.duration_ticks != 0U && run.tick >= config_.duration_ticks) run.finished = true;
    }
}

void SimulationLabExperiment::step(const FieldGrid& field, std::uint32_t ticks) {
    // A test/editor may mutate FLD between calls without restarting the Lab.
    // Retail's ordinary per-tick terrain history remains persistent; this is an
    // explicit field replacement boundary, not a rebuild on every AI callback.
    if (ticks != 0U) {
        const auto signature = lab_external_field_signature(field);
        bool terrain_changed=false;
        for (auto& run : runs_) {
            if (run.external_field_signature_valid &&
                run.external_field_signature != signature) {
                run.retail_scratch.reset();
                terrain_changed=true;
                run.runtime_field.reset();
                for(auto& entity:run.entities) {
                    entity.navigation_target_cell_valid=false;
                    entity.navigation_direct_route_valid=false;
                    entity.navigation_recheck_tick=0U;
                    entity.actor.move_waypoint.reset();
                    if(entity.combat_follow_owned)entity.combat_movement.flags |= 0x10U;
                }
            }
            run.external_field_signature = signature;
            run.external_field_signature_valid = true;
        }
        if(terrain_changed)navigation_terrain_cache_.prepare(field);
    }
    // Preserve the deterministic single-threaded API for tests/tools. The editor worker uses
    // step_run() concurrently across independent run indices instead.
    for (std::uint32_t step_index = 0; step_index < ticks; ++step_index)
        for (std::size_t run_index = 0; run_index < runs_.size(); ++run_index)
            step_run(field, run_index, 1U);
}

} // namespace thandor
