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
    // Short label for editor presentation; it intentionally does not add semantics to identifier.
    std::string_view label;
    AiKnowledgeValueDomain domain{AiKnowledgeValueDomain::Scalar};
};

// V417 closes all 43 currently proven direct-index KI.DAT semantics. Indices not present in this
// table remain raw. The underlying packedParameters[128] image is intentionally unchanged.
std::span<const AiKnowledgeParameterSemantic> known_ai_knowledge_semantics() noexcept;
const AiKnowledgeParameterSemantic* find_ai_knowledge_semantic(std::size_t index) noexcept;
const char* ai_knowledge_value_domain_name(AiKnowledgeValueDomain domain) noexcept;

} // namespace thandor
