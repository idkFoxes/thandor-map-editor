#include "thandor/technology.hpp"

#include <algorithm>
#include <cctype>

namespace thandor {
namespace {
constexpr std::size_t kHeaderBytes = 0x200U;
constexpr std::size_t kRecordBytes = 0x40U;
constexpr uint32_t kRecordCount = 256U;
}

TechnologyAsset TechnologyAsset::parse(std::span<const std::byte> data) {
    if (data.size() < kHeaderBytes || read_u32(data, 0U) != fourcc("tec"))
        throw FormatError("invalid TEC technology asset magic");
    const uint32_t allocation_size = read_u32(data, 4U);
    if (allocation_size != data.size())
        throw FormatError("TEC allocation size mismatch");
    const std::size_t required = kHeaderBytes + static_cast<std::size_t>(kRecordCount) * kRecordBytes;
    if (data.size() != required)
        throw FormatError("TEC must contain the fixed 256-record table");

    TechnologyAsset asset;
    asset.records_.reserve(kRecordCount);
    for (uint32_t technology_id = 0U; technology_id < kRecordCount; ++technology_id) {
        const std::size_t offset = kHeaderBytes + static_cast<std::size_t>(technology_id) * kRecordBytes;
        TechnologyRecord record;
        record.technology_id = technology_id;
        for (uint32_t word = 0U; word < record.prerequisite_masks.size(); ++word)
            record.prerequisite_masks[word] = read_u32(data, offset + word * 4U);
        record.xenite_cost_q4 = read_u32(data, offset + 0x20U);
        record.energy_demand_q4 = read_u32(data, offset + 0x24U);
        record.research_duration_q5 = read_u32(data, offset + 0x28U);
        record.dependency_technology_id = read_u32(data, offset + 0x2CU);
        record.completion_message_resource_id = read_u32(data, offset + 0x30U);
        const uint32_t category = read_u32(data, offset + 0x34U);
        record.category = category <= 3U ? static_cast<TechnologyCategory>(category)
                                         : TechnologyCategory::Category0;
        record.ai_base_candidate_score = read_i32(data, offset + 0x38U);
        record.reserved_3c = read_u32(data, offset + 0x3CU);
        record.title_resource_id = 0x300000U + technology_id * 2U;
        record.detail_resource_id = record.title_resource_id + 1U;
        asset.records_.push_back(std::move(record));
    }
    return asset;
}

const TechnologyRecord* TechnologyAsset::record(uint32_t technology_id) const noexcept {
    return technology_id < records_.size() ? &records_[technology_id] : nullptr;
}

const char* technology_category_name(TechnologyCategory category) noexcept {
    switch (category) {
        case TechnologyCategory::Category0: return "category 0";
        case TechnologyCategory::Category1: return "category 1";
        case TechnologyCategory::Category2: return "category 2";
        case TechnologyCategory::Category3: return "category 3";
    }
    return "unknown category";
}

const char* technology_category_retail_label(TechnologyCategory category) noexcept {
    // In-game TEC group letter (TEC record +0x34): 0 has no letter, then A/B/C.
    // This is separate from a factory/unit family and from AI pressure channel.
    // Stock example: TEC15 "TANK Badger" is A; TEC75 "SP Crow" has no letter.
    switch (category) {
        case TechnologyCategory::Category0: return "-";
        case TechnologyCategory::Category1: return "A";
        case TechnologyCategory::Category2: return "B";
        case TechnologyCategory::Category3: return "C";
    }
    return "?";
}

std::string normalize_technology_text(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    bool pending_space = false;
    for (std::size_t cursor = 0U; cursor < value.size();) {
        const auto first = static_cast<unsigned char>(value[cursor]);
        if (cursor + 2U < value.size() && first == 0xE8U) {
            const auto second = static_cast<unsigned char>(value[cursor + 1U]);
            const auto third = static_cast<unsigned char>(value[cursor + 2U]);
            if ((second & 0xC0U) == 0x80U && (third & 0xC0U) == 0x80U) {
                const uint32_t codepoint = ((static_cast<uint32_t>(first & 0x0FU)) << 12U) |
                    ((static_cast<uint32_t>(second & 0x3FU)) << 6U) |
                    static_cast<uint32_t>(third & 0x3FU);
                if (codepoint >= 0x8000U && codepoint <= 0x80FFU) {
                    pending_space = !normalized.empty();
                    cursor += 3U;
                    continue;
                }
            }
        }
        if (first <= 0x20U || first == 0x7FU) {
            pending_space = !normalized.empty();
            ++cursor;
            continue;
        }
        if (pending_space) {
            normalized.push_back(' ');
            pending_space = false;
        }
        normalized.push_back(value[cursor++]);
    }
    while (!normalized.empty() && std::isspace(static_cast<unsigned char>(normalized.back())) != 0)
        normalized.pop_back();
    return normalized;
}

} // namespace thandor
