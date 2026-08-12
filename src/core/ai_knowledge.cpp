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

constexpr std::array<AiKnowledgeParameterSemantic, 43> kKnownSemantics{{
    // Candidate scoring, resource pressure, structure prerequisites, and count limits.
    semantic(
        0x00U,
        "AI_KNOWLEDGE_RESOURCE_136_DEFICIT_SCORE_NUMERATOR",
        "resource-136 deficit score numerator",
        AiKnowledgeValueDomain::RatioTerm),
    semantic(
        0x01U,
        "AI_KNOWLEDGE_SPECIAL_SITE_14A_BASE_WEIGHT",
        "special-site 14A base weight",
        AiKnowledgeValueDomain::Weight),
    semantic(
        0x02U,
        "AI_KNOWLEDGE_SPECIAL_SITE_14C_BASE_WEIGHT",
        "special-site 14C base weight",
        AiKnowledgeValueDomain::Weight),
    semantic(
        0x03U,
        "AI_KNOWLEDGE_STRUCTURE_14B_BASE_WEIGHT",
        "structure 14B base weight",
        AiKnowledgeValueDomain::Weight),
    semantic(
        0x04U,
        "AI_KNOWLEDGE_STRUCTURE_14D_BASE_WEIGHT",
        "structure 14D base weight",
        AiKnowledgeValueDomain::Weight),
    semantic(
        0x05U,
        "AI_KNOWLEDGE_WORKSPACE08_ID14A_DERIVED_WEIGHT",
        "workspace08 ID14A derived weight",
        AiKnowledgeValueDomain::Weight),
    semantic(
        0x06U,
        "AI_KNOWLEDGE_WORKSPACE08_OTHER_DERIVED_WEIGHT",
        "workspace08 other derived weight",
        AiKnowledgeValueDomain::Weight),
    semantic(
        0x07U,
        "AI_KNOWLEDGE_STRATEGIC_CLASS_12D_BASE_WEIGHT",
        "strategic class 12D base weight",
        AiKnowledgeValueDomain::Weight),
    semantic(
        0x08U,
        "AI_KNOWLEDGE_STRATEGIC_CLASS_12F_TO_132_BASE_WEIGHT",
        "strategic class 12F..132 base weight",
        AiKnowledgeValueDomain::Weight),
    semantic(
        0x09U,
        "AI_KNOWLEDGE_ARMY_VARIANT_A_BASE_WEIGHT",
        "army candidate A base weight",
        AiKnowledgeValueDomain::Weight),
    semantic(
        0x0AU,
        "AI_KNOWLEDGE_ARMY_VARIANT_B_UNEXPLORED_TERRAIN_WEIGHT_COEFFICIENT",
        "army candidate B unexplored-terrain coefficient",
        AiKnowledgeValueDomain::Coefficient),
    semantic(
        0x0BU,
        "AI_KNOWLEDGE_ARMY_VARIANT_C_BASE_WEIGHT",
        "army candidate C base weight",
        AiKnowledgeValueDomain::Weight),
    semantic(
        0x0CU,
        "AI_KNOWLEDGE_STRATEGIC_CLASS_141_TO_143_BASE_WEIGHT",
        "strategic class 141..143 base weight",
        AiKnowledgeValueDomain::Weight),
    semantic(
        0x0DU,
        "AI_KNOWLEDGE_WORKSPACE12_BEST_CANDIDATE_BASE_WEIGHT",
        "workspace12 best-candidate base weight",
        AiKnowledgeValueDomain::Weight),
    semantic(
        0x13U,
        "AI_KNOWLEDGE_RESOURCE_136_DEFICIT_SCORE_DENOMINATOR",
        "resource-136 deficit score denominator",
        AiKnowledgeValueDomain::RatioTerm),
    semantic(
        0x14U,
        "AI_KNOWLEDGE_STRUCTURE_14B_PREREQUISITE_14A_COUNT_LIMIT",
        "structure 14B prerequisite-14A count limit",
        AiKnowledgeValueDomain::CountLimit),
    semantic(
        0x15U,
        "AI_KNOWLEDGE_STRUCTURE_14D_PREREQUISITE_14C_COUNT_LIMIT",
        "structure 14D prerequisite-14C count limit",
        AiKnowledgeValueDomain::CountLimit),
    semantic(
        0x16U,
        "AI_KNOWLEDGE_STRUCTURE_14B_COUNT_GAP_LIMIT",
        "structure 14B count-gap limit",
        AiKnowledgeValueDomain::CountLimit),
    semantic(
        0x17U,
        "AI_KNOWLEDGE_STRUCTURE_14D_COUNT_GAP_LIMIT",
        "structure 14D count-gap limit",
        AiKnowledgeValueDomain::CountLimit),

    // General placement and workspace distance terms.
    semantic(
        0x1CU,
        "AI_KNOWLEDGE_SPECIAL_SITE_SEPARATION_QUANTUM_Q12",
        "special-site separation quantum",
        AiKnowledgeValueDomain::Q12Distance),
    semantic(
        0x20U,
        "AI_KNOWLEDGE_GENERAL_SITE_MINIMUM_AXIS_SEPARATION_Q12",
        "general-site minimum axis separation",
        AiKnowledgeValueDomain::Q12Distance),
    semantic(
        0x22U,
        "AI_KNOWLEDGE_GENERAL_SITE_SECONDARY_DISTANCE_CAP_Q12",
        "general-site secondary distance cap",
        AiKnowledgeValueDomain::Q12Distance),
    semantic(
        0x23U,
        "AI_KNOWLEDGE_WORKSPACE05_DISTANCE_BIAS_Q12",
        "workspace05 distance bias",
        AiKnowledgeValueDomain::Q12Distance),
    semantic(
        0x26U,
        "AI_KNOWLEDGE_GENERAL_SITE_SECONDARY_DISTANCE_COEFFICIENT",
        "general-site secondary distance coefficient",
        AiKnowledgeValueDomain::Coefficient),
    semantic(
        0x27U,
        "AI_KNOWLEDGE_WORKSPACE05_DISTANCE_SCALE_Q12",
        "workspace05 distance scale",
        AiKnowledgeValueDomain::Q12Distance),
    semantic(
        0x30U,
        "AI_KNOWLEDGE_FLAGGED_SITE_MINIMUM_AXIS_SEPARATION_Q12",
        "flagged-site minimum axis separation",
        AiKnowledgeValueDomain::Q12Distance),
    semantic(
        0x33U,
        "AI_KNOWLEDGE_FACTION_ANCHOR_DISTANCE_BIAS_Q12",
        "faction-anchor distance bias",
        AiKnowledgeValueDomain::Q12Distance),
    semantic(
        0x37U,
        "AI_KNOWLEDGE_FACTION_ANCHOR_DISTANCE_SCALE_Q12",
        "faction-anchor distance scale",
        AiKnowledgeValueDomain::Q12Distance),
    semantic(
        0x43U,
        "AI_KNOWLEDGE_SECONDARY_WORKSPACE_DISTANCE_BIAS_Q12",
        "secondary-workspace distance bias",
        AiKnowledgeValueDomain::Q12Distance),
    semantic(
        0x47U,
        "AI_KNOWLEDGE_SECONDARY_WORKSPACE_DISTANCE_SCALE_Q12",
        "secondary-workspace distance scale",
        AiKnowledgeValueDomain::Q12Distance),

    // Terrain-feature and special-class-12 placement terms.
    semantic(
        0x50U,
        "AI_KNOWLEDGE_TERRAIN_FEATURE_MINIMUM_AXIS_SEPARATION_Q12",
        "terrain-feature minimum axis separation",
        AiKnowledgeValueDomain::Q12Distance),
    semantic(
        0x51U,
        "AI_KNOWLEDGE_PLACEMENT_CLEARANCE_PADDING_Q12",
        "placement clearance padding",
        AiKnowledgeValueDomain::Q12Distance),
    semantic(
        0x52U,
        "AI_KNOWLEDGE_SPECIAL_CLASS12_SECONDARY_WORKSPACE_DISTANCE_THRESHOLD_Q12",
        "special-class-12 secondary-workspace threshold",
        AiKnowledgeValueDomain::Q12Distance),
    semantic(
        0x53U,
        "AI_KNOWLEDGE_SPECIAL_CLASS12_ENTITY_DISTANCE_BIAS_Q12",
        "special-class-12 entity distance bias",
        AiKnowledgeValueDomain::Q12Distance),
    semantic(
        0x54U,
        "AI_KNOWLEDGE_SPECIAL_CLASS12_WORKSPACE02_NEAR_DISTANCE_THRESHOLD_Q12",
        "special-class-12 workspace02 near threshold",
        AiKnowledgeValueDomain::Q12Distance),
    semantic(
        0x55U,
        "AI_KNOWLEDGE_SPECIAL_CLASS12_WORKSPACE08_FIELD0C_COEFFICIENT",
        "special-class-12 workspace08 field0C coefficient",
        AiKnowledgeValueDomain::Coefficient),
    semantic(
        0x56U,
        "AI_KNOWLEDGE_SPECIAL_CLASS12_SECONDARY_WORKSPACE_SHORTFALL_COEFFICIENT",
        "special-class-12 secondary-workspace shortfall coefficient",
        AiKnowledgeValueDomain::Coefficient),
    semantic(
        0x57U,
        "AI_KNOWLEDGE_SPECIAL_CLASS12_ENTITY_DISTANCE_COEFFICIENT",
        "special-class-12 entity distance coefficient",
        AiKnowledgeValueDomain::Coefficient),
    semantic(
        0x58U,
        "AI_KNOWLEDGE_SPECIAL_CLASS12_WORKSPACE02_NEAR_DISTANCE_COEFFICIENT",
        "special-class-12 workspace02 near-distance coefficient",
        AiKnowledgeValueDomain::Coefficient),
    semantic(
        0x59U,
        "AI_KNOWLEDGE_SPECIAL_SITE_MINIMUM_WORKSPACE_DISTANCE_Q12",
        "special-site minimum workspace distance",
        AiKnowledgeValueDomain::Q12Distance),

    // Strategic-class weights.
    semantic(
        0x78U,
        "AI_KNOWLEDGE_STRATEGIC_CLASS_141_WEIGHT",
        "strategic class 141 weight",
        AiKnowledgeValueDomain::Weight),
    semantic(
        0x79U,
        "AI_KNOWLEDGE_STRATEGIC_CLASS_142_WEIGHT",
        "strategic class 142 weight",
        AiKnowledgeValueDomain::Weight),
    semantic(
        0x7AU,
        "AI_KNOWLEDGE_STRATEGIC_CLASS_143_WEIGHT",
        "strategic class 143 weight",
        AiKnowledgeValueDomain::Weight),
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

const char* ai_knowledge_value_domain_name(AiKnowledgeValueDomain domain) noexcept {
    switch (domain) {
        case AiKnowledgeValueDomain::Q12Distance: return "Q12 distance";
        case AiKnowledgeValueDomain::Weight: return "weight";
        case AiKnowledgeValueDomain::Coefficient: return "coefficient";
        case AiKnowledgeValueDomain::CountLimit: return "count limit";
        case AiKnowledgeValueDomain::RatioTerm: return "ratio term";
        case AiKnowledgeValueDomain::Scalar: return "scalar";
    }
    return "unknown";
}

} // namespace thandor
