#pragma once

#include "thandor/binary.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace thandor {

class ScenarioCatalogRecord {
public:
    static constexpr std::size_t kSize = 0x100;
    static constexpr std::size_t kIdentifierBytes = 0x40;
    static constexpr std::size_t kMinimumPlayerCountOffset = 0x40;
    static constexpr std::size_t kMaximumPlayerCountOffset = 0x48;
    static constexpr std::size_t kPlanetThemeIndexOffset = 0x50;
    static constexpr std::size_t kDifficultyIndexOffset = 0x60;
    static constexpr std::size_t kAliasBaseOffset = 0x70;
    static constexpr std::size_t kMapSizeCategoryIndexOffset = 0x80;

    static ScenarioCatalogRecord parse(std::span<const std::byte> data);
    static ScenarioCatalogRecord minimal(std::string_view map_name, uint32_t alias_base);

    std::string identifier() const;
    std::string map_name() const;
    void set_map_name(std::string_view value);
    uint32_t minimum_player_count() const;
    uint32_t maximum_player_count() const;
    void set_player_count_range(uint32_t minimum_count, uint32_t maximum_count);
    uint32_t planet_theme_index() const;
    uint32_t difficulty_index() const;
    uint32_t map_size_category_index() const;
    void set_frontend_catalog_indices(uint32_t planet_theme_index, uint32_t difficulty_index,
                                      uint32_t map_size_category_index);
    uint32_t alias_base() const;
    void set_alias_base(uint32_t value);
    std::span<const std::byte, kSize> bytes() const noexcept { return bytes_; }
    std::vector<std::byte> serialize() const;

private:
    std::array<std::byte, kSize> bytes_{};
};

std::vector<ScenarioCatalogRecord> parse_scenario_catalog_records(std::span<const std::byte> data);
void merge_scenario_catalog_records(std::vector<ScenarioCatalogRecord>& destination,
                                    std::span<const ScenarioCatalogRecord> source);
const ScenarioCatalogRecord* find_scenario_catalog_record(
    std::span<const ScenarioCatalogRecord> records, std::string_view map_name);
// The unpatched game resolves the compact level-list title through page 0x22 and the
// fifteen extended description strings through page 0x23. Custom aliases therefore stay in
// 0x5A..0xCF; 0x5A is the first value after the stock page-0x22 title range.
inline constexpr uint32_t kCustomScenarioAliasFirst = 0x5AU;
inline constexpr uint32_t kCustomScenarioAliasLast = 0xCFU;
uint32_t game_custom_alias_for_slot(unsigned slot);
uint32_t choose_unused_scenario_alias_base(std::span<const ScenarioCatalogRecord> records,
                                           uint32_t first_candidate = kCustomScenarioAliasFirst);

} // namespace thandor
