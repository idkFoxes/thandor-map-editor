#include "thandor/ai_knowledge.hpp"

#include "thandor/binary.hpp"

#include <array>
#include <stdexcept>

namespace thandor {
namespace {

constexpr AiKnowledgeParameterSemantic semantic(
    std::uint8_t index, std::string_view identifier, std::string_view label,
    AiKnowledgeValueDomain domain) noexcept {
    return {index, identifier, label, domain};
}

constexpr AiKnowledgeConsumerReference consumer(
    std::uint8_t index, std::uint32_t function_address, std::string_view function_name) noexcept {
    return {index, function_address, function_name};
}

// 45 previously documented GZF direct-index semantics; 17 additional game-source-verified fields.
// The 0x78..0x7A entries were incorrectly called global strategic weights: they are
// class-142 pressure coefficients. Unknown entries remain raw and unmodified.
constexpr std::array<AiKnowledgeParameterSemantic, 62> kKnownSemantics{{
    semantic(0x00U, "AI_KNOWLEDGE_RESOURCE_136_DEFICIT_SCORE_NUMERATOR", "Power plant: energy-shortfall score numerator", AiKnowledgeValueDomain::RatioTerm),
    semantic(0x01U, "AI_KNOWLEDGE_SPECIAL_SITE_14A_BASE_WEIGHT", "Xenite mine: build priority", AiKnowledgeValueDomain::Weight),
    semantic(0x02U, "AI_KNOWLEDGE_SPECIAL_SITE_14C_BASE_WEIGHT", "Tritium pump: build priority", AiKnowledgeValueDomain::Weight),
    semantic(0x03U, "AI_KNOWLEDGE_STRUCTURE_14B_BASE_WEIGHT", "Xenite silo: build priority", AiKnowledgeValueDomain::Weight),
    semantic(0x04U, "AI_KNOWLEDGE_STRUCTURE_14D_BASE_WEIGHT", "Tritium tank: build priority", AiKnowledgeValueDomain::Weight),
    semantic(0x05U, "AI_KNOWLEDGE_WORKSPACE08_ID14A_DERIVED_WEIGHT", "Xenite resource sites: extra build priority", AiKnowledgeValueDomain::Weight),
    semantic(0x06U, "AI_KNOWLEDGE_WORKSPACE08_OTHER_DERIVED_WEIGHT", "Tritium resource sites: extra build priority", AiKnowledgeValueDomain::Weight),
    semantic(0x07U, "AI_KNOWLEDGE_STRATEGIC_CLASS_12D_BASE_WEIGHT", "Light weapons factory: build priority", AiKnowledgeValueDomain::Weight),
    semantic(0x08U, "AI_KNOWLEDGE_STRATEGIC_CLASS_12F_TO_132_BASE_WEIGHT", "Advanced factories: build priority", AiKnowledgeValueDomain::Weight),
    semantic(0x09U, "AI_KNOWLEDGE_ARMY_VARIANT_A_BASE_WEIGHT", "Base defence: unit purchase priority", AiKnowledgeValueDomain::Weight),
    semantic(0x0AU, "AI_KNOWLEDGE_ARMY_VARIANT_B_UNEXPLORED_TERRAIN_WEIGHT_COEFFICIENT", "Exploration: unexplored-terrain purchase weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x0BU, "AI_KNOWLEDGE_ARMY_VARIANT_C_BASE_WEIGHT", "Attack force: unit purchase priority", AiKnowledgeValueDomain::Weight),
    semantic(0x0CU, "AI_KNOWLEDGE_STRATEGIC_CLASS_141_TO_143_BASE_WEIGHT", "Military research labs: build priority", AiKnowledgeValueDomain::Weight),
    semantic(0x0DU, "AI_KNOWLEDGE_WORKSPACE12_BEST_CANDIDATE_BASE_WEIGHT", "Research: technology purchase priority", AiKnowledgeValueDomain::Weight),
    semantic(0x10U, "AI_KNOWLEDGE_STRATEGIC_12D_AND_141_TO_143_ADDITIONAL_PLANNING_CAPACITY", "Light factory and research labs: extra planning slots", AiKnowledgeValueDomain::Scalar),
    semantic(0x11U, "AI_KNOWLEDGE_STRATEGIC_12F_TO_132_ADDITIONAL_PLANNING_CAPACITY", "Advanced factories: extra planning slots", AiKnowledgeValueDomain::Scalar),
    semantic(0x13U, "AI_KNOWLEDGE_RESOURCE_136_DEFICIT_SCORE_DENOMINATOR", "Power plant: energy-shortfall score denominator", AiKnowledgeValueDomain::RatioTerm),
    semantic(0x14U, "AI_KNOWLEDGE_STRUCTURE_14B_PREREQUISITE_14A_COUNT_LIMIT", "Xenite silo: storage-capacity ceiling", AiKnowledgeValueDomain::Q4Amount),
    semantic(0x15U, "AI_KNOWLEDGE_STRUCTURE_14D_PREREQUISITE_14C_COUNT_LIMIT", "Tritium tank: storage-capacity ceiling", AiKnowledgeValueDomain::Q4Amount),
    semantic(0x16U, "AI_KNOWLEDGE_STRUCTURE_14B_COUNT_GAP_LIMIT", "Xenite silo: free-storage trigger", AiKnowledgeValueDomain::Q4Amount),
    semantic(0x17U, "AI_KNOWLEDGE_STRUCTURE_14D_COUNT_GAP_LIMIT", "Tritium tank: free-storage trigger", AiKnowledgeValueDomain::Q4Amount),
    semantic(0x18U, "AI_KNOWLEDGE_FACTION_SCALED_TECH_MINIMUM_XENITE_Q4", "Research: minimum Xenite per faction", AiKnowledgeValueDomain::Q4Amount),
    semantic(0x1CU, "AI_KNOWLEDGE_SPECIAL_SITE_SEPARATION_QUANTUM_Q12", "Resource build sites: spacing step", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x20U, "AI_KNOWLEDGE_GENERAL_SITE_MINIMUM_AXIS_SEPARATION_Q12", "General build sites: minimum X/Y spacing", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x21U, "AI_KNOWLEDGE_GENERAL_SITE_PRIMARY_DISTANCE_CAP_Q12", "General build sites: own-building proximity cap", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x22U, "AI_KNOWLEDGE_GENERAL_SITE_SECONDARY_DISTANCE_CAP_Q12", "General build sites: own-unit distance cap", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x23U, "AI_KNOWLEDGE_WORKSPACE05_DISTANCE_BIAS_Q12", "Unit positioning: build-site attraction distance", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x24U, "AI_KNOWLEDGE_GENERAL_SITE_VISIBLE_HOSTILE_DISTANCE_CAP_Q12", "General build sites: visible-enemy distance cap", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x25U, "AI_KNOWLEDGE_GENERAL_SITE_PRIMARY_DISTANCE_COEFFICIENT", "General build sites: own-building proximity weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x26U, "AI_KNOWLEDGE_GENERAL_SITE_SECONDARY_DISTANCE_COEFFICIENT", "General build sites: own-unit distance weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x27U, "AI_KNOWLEDGE_WORKSPACE05_DISTANCE_SCALE_Q12", "Unit positioning: build-site attraction strength", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x28U, "AI_KNOWLEDGE_GENERAL_SITE_VISIBLE_HOSTILE_DISTANCE_COEFFICIENT", "General build sites: visible-enemy distance weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x30U, "AI_KNOWLEDGE_FLAGGED_SITE_MINIMUM_AXIS_SEPARATION_Q12", "Special-rule build sites: minimum X/Y spacing", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x31U, "AI_KNOWLEDGE_FLAGGED_SITE_PRIMARY_DISTANCE_CAP_Q12", "Special-rule build sites: own-building proximity cap", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x32U, "AI_KNOWLEDGE_FLAGGED_SITE_SECONDARY_DISTANCE_CAP_Q12", "Special-rule build sites: own-unit distance cap", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x33U, "AI_KNOWLEDGE_FACTION_ANCHOR_DISTANCE_BIAS_Q12", "Unit rally points: attraction distance", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x34U, "AI_KNOWLEDGE_FLAGGED_SITE_VISIBLE_HOSTILE_DISTANCE_CAP_Q12", "Special-rule build sites: visible-enemy distance cap", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x35U, "AI_KNOWLEDGE_FLAGGED_SITE_PRIMARY_DISTANCE_COEFFICIENT", "Special-rule build sites: own-building proximity weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x36U, "AI_KNOWLEDGE_FLAGGED_SITE_SECONDARY_DISTANCE_COEFFICIENT", "Special-rule build sites: own-unit distance weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x37U, "AI_KNOWLEDGE_FACTION_ANCHOR_DISTANCE_SCALE_Q12", "Unit rally points: attraction strength", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x38U, "AI_KNOWLEDGE_FLAGGED_SITE_VISIBLE_HOSTILE_DISTANCE_COEFFICIENT", "Special-rule build sites: visible-enemy distance weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x43U, "AI_KNOWLEDGE_SECONDARY_WORKSPACE_DISTANCE_BIAS_Q12", "Unit attack targets: attraction distance", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x47U, "AI_KNOWLEDGE_SECONDARY_WORKSPACE_DISTANCE_SCALE_Q12", "Unit attack targets: attraction strength", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x50U, "AI_KNOWLEDGE_TERRAIN_FEATURE_MINIMUM_AXIS_SEPARATION_Q12", "Resource sites: minimum X/Y spacing", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x51U, "AI_KNOWLEDGE_PLACEMENT_CLEARANCE_PADDING_Q12", "Building placement: extra clearance", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x52U, "AI_KNOWLEDGE_SPECIAL_CLASS12_SECONDARY_WORKSPACE_DISTANCE_THRESHOLD_Q12", "Pioneer resource sites: own-unit proximity threshold", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x53U, "AI_KNOWLEDGE_SPECIAL_CLASS12_ENTITY_DISTANCE_BIAS_Q12", "Pioneer resource sites: travel-distance allowance", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x54U, "AI_KNOWLEDGE_SPECIAL_CLASS12_WORKSPACE02_NEAR_DISTANCE_THRESHOLD_Q12", "Pioneer resource sites: nearby-enemy threshold", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x55U, "AI_KNOWLEDGE_SPECIAL_CLASS12_WORKSPACE08_FIELD0C_COEFFICIENT", "Pioneer resource sites: resource-priority weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x56U, "AI_KNOWLEDGE_SPECIAL_CLASS12_SECONDARY_WORKSPACE_SHORTFALL_COEFFICIENT", "Pioneer resource sites: own-unit proximity weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x57U, "AI_KNOWLEDGE_SPECIAL_CLASS12_ENTITY_DISTANCE_COEFFICIENT", "Pioneer resource sites: travel-distance weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x58U, "AI_KNOWLEDGE_SPECIAL_CLASS12_WORKSPACE02_NEAR_DISTANCE_COEFFICIENT", "Pioneer resource sites: nearby-enemy weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x59U, "AI_KNOWLEDGE_SPECIAL_SITE_MINIMUM_WORKSPACE_DISTANCE_Q12", "Resource sites: minimum distance from own army", AiKnowledgeValueDomain::Q12Distance),
    semantic(0x74U, "AI_KNOWLEDGE_STRATEGIC_CLASS_141_PRESSURE2_COEFFICIENT", "Ion-laser research lab: enemy tank pressure weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x75U, "AI_KNOWLEDGE_STRATEGIC_CLASS_141_PRESSURE3_COEFFICIENT", "Ion-laser research lab: enemy walker pressure weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x76U, "AI_KNOWLEDGE_STRATEGIC_CLASS_141_PRESSURE4_COEFFICIENT", "Ion-laser research lab: enemy glider pressure weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x78U, "AI_KNOWLEDGE_STRATEGIC_CLASS_142_PRESSURE2_COEFFICIENT", "Flak-gun research lab: enemy tank pressure weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x79U, "AI_KNOWLEDGE_STRATEGIC_CLASS_142_PRESSURE3_COEFFICIENT", "Flak-gun research lab: enemy walker pressure weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x7AU, "AI_KNOWLEDGE_STRATEGIC_CLASS_142_PRESSURE4_COEFFICIENT", "Flak-gun research lab: enemy glider pressure weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x7CU, "AI_KNOWLEDGE_STRATEGIC_CLASS_143_PRESSURE2_COEFFICIENT", "Rocket-artillery research lab: enemy tank pressure weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x7DU, "AI_KNOWLEDGE_STRATEGIC_CLASS_143_PRESSURE3_COEFFICIENT", "Rocket-artillery research lab: enemy walker pressure weight", AiKnowledgeValueDomain::Coefficient),
    semantic(0x7EU, "AI_KNOWLEDGE_STRATEGIC_CLASS_143_PRESSURE4_COEFFICIENT", "Rocket-artillery research lab: enemy glider pressure weight", AiKnowledgeValueDomain::Coefficient),
}};

constexpr std::array<AiKnowledgeConsumerReference, 57> kKnownConsumers{{
    consumer(0x00U, 0x00539E60U, "AiResourceCandidate_AddWeightedId136"),
    consumer(0x01U, 0x0053AD50U, "AiCandidatePlanning_ComputeSpecialSiteWeight"),
    consumer(0x01U, 0x0053C810U, "AiRuntime_DispatchFactionPlanningPhase"),
    consumer(0x02U, 0x0053AD50U, "AiCandidatePlanning_ComputeSpecialSiteWeight"),
    consumer(0x02U, 0x0053C810U, "AiRuntime_DispatchFactionPlanningPhase"),
    consumer(0x03U, 0x0053C810U, "AiRuntime_DispatchFactionPlanningPhase"),
    consumer(0x04U, 0x0053C810U, "AiRuntime_DispatchFactionPlanningPhase"),
    consumer(0x05U, 0x00539D20U, "AiStructureCandidate_AddWeightedId14BOr14CCandidate"),
    consumer(0x06U, 0x00539D20U, "AiStructureCandidate_AddWeightedId14BOr14CCandidate"),
    consumer(0x07U, 0x0053AC20U, "AiStrategicClass_AddCandidate12DOr12FTo132"),
    consumer(0x08U, 0x0053AC20U, "AiStrategicClass_AddCandidate12DOr12FTo132"),
    consumer(0x09U, 0x0053A6E0U, "AiArmyCandidate_AddBestScoredVariantA"),
    consumer(0x0AU, 0x0053A800U, "AiArmyCandidate_AddBestScoredVariantB"),
    consumer(0x0BU, 0x0053A8D0U, "AiArmyCandidate_AddBestScoredVariantC"),
    consumer(0x0CU, 0x0053B070U, "AiStrategicClass_AddWeightedClassCandidate"),
    consumer(0x0DU, 0x0053BF30U, "AiStrategicCandidate_AddBestWorkspace12Entry"),
    consumer(0x10U, 0x0053AC20U, "AiStrategicClass_AddCandidate12DOr12FTo132"),
    consumer(0x10U, 0x0053B070U, "AiStrategicClass_AddWeightedClassCandidate"),
    consumer(0x11U, 0x0053AC20U, "AiStrategicClass_AddCandidate12DOr12FTo132"),
    consumer(0x13U, 0x00539E60U, "AiResourceCandidate_AddWeightedId136"),
    consumer(0x14U, 0x00539D20U, "AiStructureCandidate_AddWeightedId14BOr14CCandidate"),
    consumer(0x15U, 0x00539D20U, "AiStructureCandidate_AddWeightedId14BOr14CCandidate"),
    consumer(0x16U, 0x00539D20U, "AiStructureCandidate_AddWeightedId14BOr14CCandidate"),
    consumer(0x17U, 0x00539D20U, "AiStructureCandidate_AddWeightedId14BOr14CCandidate"),
    consumer(0x1CU, 0x00539EF0U, "AiPlacement_ReserveSeparatedSpecialSiteChain"),
    consumer(0x1CU, 0x0053A110U, "AiPlacement_ReserveAdditionalSpecialSite"),
    consumer(0x1CU, 0x0053ACD0U, "AiPlacement_ReserveMode3SiteCluster"),
    consumer(0x1CU, 0x0053B570U, "AiPlacement_QueryReachableSiteBucketCount"),
    consumer(0x20U, 0x00537B20U, "AiSiteCandidate_AddGeneralCellIfSeparated"),
    consumer(0x22U, 0x00537B20U, "AiSiteCandidate_AddGeneralCellIfSeparated"),
    consumer(0x23U, 0x0053B1D0U, "AiUnitBehavior_ComputeWorkspace05DistanceScore"),
    consumer(0x26U, 0x00537B20U, "AiSiteCandidate_AddGeneralCellIfSeparated"),
    consumer(0x27U, 0x0053B1D0U, "AiUnitBehavior_ComputeWorkspace05DistanceScore"),
    consumer(0x30U, 0x00537C10U, "AiSiteCandidate_AddFlaggedCellIfSeparated"),
    consumer(0x33U, 0x0053B260U, "AiUnitBehavior_ComputeFactionAnchorDistanceScore"),
    consumer(0x37U, 0x0053B260U, "AiUnitBehavior_ComputeFactionAnchorDistanceScore"),
    consumer(0x43U, 0x0053B330U, "AiUnitBehavior_ComputeSecondaryWorkspaceDistanceScore"),
    consumer(0x47U, 0x0053B330U, "AiUnitBehavior_ComputeSecondaryWorkspaceDistanceScore"),
    consumer(0x50U, 0x00537CF0U, "AiSiteCandidate_AddTerrainFeatureCellIfSeparated"),
    consumer(0x51U, 0x0051D450U, "ArmyPlacement_DispatchAssetAtFieldPoint"),
    consumer(0x51U, 0x005278D0U, "ArmyPlacementCollision_TestCandidateAndClearanceCf"),
    consumer(0x51U, 0x00537CF0U, "AiSiteCandidate_AddTerrainFeatureCellIfSeparated"),
    consumer(0x51U, 0x0053A110U, "AiPlacement_ReserveAdditionalSpecialSite"),
    consumer(0x51U, 0x0053ACD0U, "AiPlacement_ReserveMode3SiteCluster"),
    consumer(0x51U, 0x0053B570U, "AiPlacement_QueryReachableSiteBucketCount"),
    consumer(0x52U, 0x0053B620U, "AiUnitBehavior_UpdateSpecialClass12Entity"),
    consumer(0x53U, 0x0053B620U, "AiUnitBehavior_UpdateSpecialClass12Entity"),
    consumer(0x54U, 0x0053B620U, "AiUnitBehavior_UpdateSpecialClass12Entity"),
    consumer(0x55U, 0x0053B620U, "AiUnitBehavior_UpdateSpecialClass12Entity"),
    consumer(0x56U, 0x0053B620U, "AiUnitBehavior_UpdateSpecialClass12Entity"),
    consumer(0x57U, 0x0053B620U, "AiUnitBehavior_UpdateSpecialClass12Entity"),
    consumer(0x58U, 0x0053B620U, "AiUnitBehavior_UpdateSpecialClass12Entity"),
    consumer(0x59U, 0x0053AD50U, "AiCandidatePlanning_ComputeSpecialSiteWeight"),
    consumer(0x59U, 0x0053B620U, "AiUnitBehavior_UpdateSpecialClass12Entity"),
    consumer(0x78U, 0x0053AF00U, "AiStrategicClass_SelectPressureWeightedBuilding"),
    consumer(0x79U, 0x0053AF00U, "AiStrategicClass_SelectPressureWeightedBuilding"),
    consumer(0x7AU, 0x0053AF00U, "AiStrategicClass_SelectPressureWeightedBuilding"),
}};

} // namespace

AiKnowledgeData AiKnowledgeData::parse(std::span<const std::byte> data) {
    if (data.size() != kSerializedBytes) {
        throw FormatError("engine/ki.dat must be exactly 0x200 bytes");
    }

    AiKnowledgeData result;
    for (std::size_t index = 0; index < kParameterCount; ++index) {
        result.parameters_[index] = read_u32(data, index * sizeof(std::uint32_t));
    }
    return result;
}

std::uint32_t AiKnowledgeData::raw(std::size_t index) const {
    if (index >= parameters_.size()) throw std::out_of_range("KI.DAT parameter index");
    return parameters_[index];
}

std::int32_t AiKnowledgeData::signed_raw(std::size_t index) const {
    return static_cast<std::int32_t>(raw(index));
}

std::int32_t AiKnowledgeData::placement_clearance_padding_q12() const noexcept {
    return static_cast<std::int32_t>(parameters_[kPlacementClearancePaddingIndex]);
}

float AiKnowledgeData::placement_clearance_padding_world() const noexcept {
    return q12_to_float(placement_clearance_padding_q12());
}

std::span<const AiKnowledgeParameterSemantic> known_ai_knowledge_semantics() noexcept {
    return kKnownSemantics;
}

const AiKnowledgeParameterSemantic* find_ai_knowledge_semantic(std::size_t index) noexcept {
    for (const auto& semantic : kKnownSemantics) {
        if (semantic.index == index) return &semantic;
    }
    return nullptr;
}

std::span<const AiKnowledgeConsumerReference> known_ai_knowledge_consumers() noexcept {
    return kKnownConsumers;
}

std::span<const AiKnowledgeConsumerReference> ai_knowledge_consumers(std::size_t index) noexcept {
    std::size_t first = 0U;
    while (first < kKnownConsumers.size() && kKnownConsumers[first].index < index) ++first;
    std::size_t last = first;
    while (last < kKnownConsumers.size() && kKnownConsumers[last].index == index) ++last;
    return std::span<const AiKnowledgeConsumerReference>{kKnownConsumers}.subspan(first, last - first);
}

std::span<const AiWorkspaceDescription> original_ai_workspace_descriptions() noexcept {
    static constexpr std::array<AiWorkspaceDescription,15> workspaces{{
        {0,"Owned and planned buildings","Existing structures, construction requests and production reservations."},
        {1,"Owned and ordered units","Live units and unit orders counted by the original planning limits."},
        {2,"Visible enemies","Hostile units and buildings currently observed by the faction."},
        {3,"Last-known enemies","Remembered hostile positions outside current visibility."},
        {4,"Requested assets","Outstanding building and unit requests awaiting placement or purchase."},
        {5,"General build sites","Scored construction positions around the faction's forces."},
        {6,"Special-rule build sites","Construction positions admitted by the original flagged-site rules."},
        {7,"Attack targets","Ranked targets considered by units and attack groups."},
        {8,"Resource and pioneer sites","Xenite and Tritium positions considered for new extraction buildings."},
        {9,"Building sites inside the base","Candidate cells within owned-building support extents."},
        {10,"Large-building sites inside the base","Base cells that pass the wider-clearance placement test."},
        {11,"Available units and buildings","Registry assets available to the faction's purchase scoring."},
        {12,"Research choices","Technology upgrades scored for the next research purchase."},
        {13,"Weighted purchase choices","Combined construction, unit and research proposals."},
        {14,"Unassigned attack-group units","Collected units waiting to be assigned to a common target."},
    }};
    return workspaces;
}

const char* ai_knowledge_value_domain_name(AiKnowledgeValueDomain domain) noexcept {
    switch (domain) {
        case AiKnowledgeValueDomain::Q12Distance: return "Q12 distance";
        case AiKnowledgeValueDomain::Q4Amount: return "Q4 resource";
        case AiKnowledgeValueDomain::Weight: return "weight";
        case AiKnowledgeValueDomain::Coefficient: return "coefficient";
        case AiKnowledgeValueDomain::CountLimit: return "count limit";
        case AiKnowledgeValueDomain::RatioTerm: return "ratio term";
        case AiKnowledgeValueDomain::Scalar: return "scalar";
    }
    return "unknown";
}

} // namespace thandor
