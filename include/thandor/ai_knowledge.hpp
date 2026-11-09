#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace thandor {

// engine/ki.dat is an exact 0x200-byte image containing 128 little-endian dwords.
// The retail file stays a flat packed image; semantic names are a read-only view over proven
// direct-index consumers and must never reshape the serialized representation.
class AiKnowledgeData {
public:
    static constexpr std::size_t kParameterCount = 128U;
    static constexpr std::size_t kSerializedBytes = kParameterCount * sizeof(std::uint32_t);
    static constexpr std::size_t kPlacementClearancePaddingIndex = 0x51U;

    static AiKnowledgeData parse(std::span<const std::byte> data);

    std::uint32_t raw(std::size_t index) const;
    std::int32_t signed_raw(std::size_t index) const;
    std::int32_t placement_clearance_padding_q12() const noexcept;
    float placement_clearance_padding_world() const noexcept;
    const std::array<std::uint32_t, kParameterCount>& parameters() const noexcept {
        return parameters_;
    }

private:
    std::array<std::uint32_t, kParameterCount> parameters_{};
};

enum class AiKnowledgeValueDomain : std::uint8_t {
    Q12Distance,
    Q4Amount,
    Weight,
    Coefficient,
    CountLimit,
    RatioTerm,
    Scalar,
};

struct AiKnowledgeParameterSemantic {
    std::uint8_t index{};
    // Exact conservative V417 identifier recovered from the direct-index consumer equations.
    std::string_view identifier;
    // Human-readable original-KI label; technical identifiers remain stable for provenance.
    std::string_view label;
    AiKnowledgeValueDomain domain{AiKnowledgeValueDomain::Scalar};
};

// 62 named fields: 45 from the prior GZF census plus 17 from the current game source.
// Three older class-142 coefficients were renamed after cross-checking the game.
// Other indices remain raw; the 128-dword binary image is unchanged.
std::span<const AiKnowledgeParameterSemantic> known_ai_knowledge_semantics() noexcept;
const AiKnowledgeParameterSemantic* find_ai_knowledge_semantic(std::size_t index) noexcept;
const char* ai_knowledge_value_domain_name(AiKnowledgeValueDomain domain) noexcept;
struct AiWorkspaceDescription {
    std::uint8_t index{};
    std::string_view name;
    std::string_view purpose;
};
std::span<const AiWorkspaceDescription> original_ai_workspace_descriptions() noexcept;

struct AiKnowledgeConsumerReference {
    std::uint8_t index{};
    std::uint32_t function_address{};
    std::string_view function_name;
};

// Fresh V530 GZF census: every named KI semantic is tied to one or more direct consumer
// functions. The C export was used only to locate candidates; addresses/names were re-dumped
// from the accepted GZF with Ghidra 12.1.2.
std::span<const AiKnowledgeConsumerReference> known_ai_knowledge_consumers() noexcept;
std::span<const AiKnowledgeConsumerReference> ai_knowledge_consumers(std::size_t index) noexcept;

} // namespace thandor
