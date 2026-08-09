#pragma once

#include "thandor/binary.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace thandor {

// Partial enum recovered from TechnologyRecord +0x34. Category, research-panel branch,
// panel row, and model attachment kind are independent axes and must not be collapsed.
enum class TechnologyCategory : uint32_t {
    Category0 = 0,
    Category1 = 1,
    Category2 = 2,
    Category3 = 3,
};

struct TechnologyRecord {
    uint32_t technology_id{};
    std::array<uint32_t, 8> prerequisite_masks{};
    uint32_t xenite_cost_q4{};
    uint32_t energy_cost_q4{};
    uint32_t research_duration_q5{};
    uint32_t dependency_technology_id{};
    uint32_t completion_message_resource_id{};
    TechnologyCategory category{TechnologyCategory::Category0};
    int32_t ai_base_candidate_score{};
    uint32_t reserved_3c{};
    // texte/techno.str uses even indices for titles and the paired odd index for the detail text.
    uint32_t title_resource_id{};
    uint32_t detail_resource_id{};
    std::string title;
    std::string detail;
};

class TechnologyAsset {
public:
    static TechnologyAsset parse(std::span<const std::byte> data);

    std::span<const TechnologyRecord> records() const noexcept { return records_; }
    const TechnologyRecord* record(uint32_t technology_id) const noexcept;

private:
    std::vector<TechnologyRecord> records_;
};

const char* technology_category_name(TechnologyCategory category) noexcept;
// Retail research cards render categories 1..3 as A..C. Category zero means that no weapon
// category letter is displayed for the technology.
const char* technology_category_retail_label(TechnologyCategory category) noexcept;

// Removes rich-text command words from the short title path. It deliberately does not attempt to
// render command payloads; those remain available through TechnologyRecord::detail as source text.
std::string normalize_technology_text(std::string value);

// Exact InGameTechnologyPanel_Rebuild layout. The executable addresses the serialized MDL
// table with indices 1..28 at +0x1C8..+0x234. Index zero is the separate linked/base technology
// at +0x1C4 and must never be folded into this panel geometry.
constexpr uint32_t technology_panel_direction_from_serialized_index(
    uint32_t serialized_index) noexcept {
    return serialized_index == 0U ? 0U : (serialized_index - 1U) % 7U;
}
constexpr uint32_t technology_panel_row_from_serialized_index(
    uint32_t serialized_index) noexcept {
    return serialized_index == 0U ? 0U : (serialized_index - 1U) / 7U;
}

} // namespace thandor
