#include "thandor/scenario_catalog.hpp"

#include <algorithm>

namespace thandor {
namespace {
std::string normalize_identifier_name(std::string value) {
    value = lower_ascii(std::move(value));
    while (!value.empty() && (value.back() == '.' || value.back() == ' ')) value.pop_back();
    return value;
}
}

ScenarioCatalogRecord ScenarioCatalogRecord::parse(std::span<const std::byte> data) {
    if (data.size() != kSize) throw FormatError("scenario catalog record must be exactly 0x100 bytes");
    ScenarioCatalogRecord record;
    std::copy(data.begin(), data.end(), record.bytes_.begin());
    return record;
}

ScenarioCatalogRecord ScenarioCatalogRecord::minimal(std::string_view map_name, uint32_t alias_base) {
    ScenarioCatalogRecord record;
    // The remaining metadata is intentionally zero. Normal editor creation clones an existing
    // stock record; this fallback is only for legacy projects that lack a compatibility shell.
    record.set_map_name(map_name);
    record.set_alias_base(alias_base);
    return record;
}

std::string ScenarioCatalogRecord::identifier() const {
    return read_utf16le_z(bytes_, 0, kIdentifierBytes);
}

std::string ScenarioCatalogRecord::map_name() const {
    return normalize_identifier_name(identifier());
}

void ScenarioCatalogRecord::set_map_name(std::string_view value) {
    std::string identifier(value);
    identifier = lower_ascii(std::move(identifier));
    while (!identifier.empty() && identifier.back() == '.') identifier.pop_back();
    if (identifier.empty()) throw FormatError("scenario catalog map identifier is empty");
    identifier.push_back('.');
    write_utf16le_fixed(bytes_, 0, kIdentifierBytes, identifier);
}

namespace {
uint32_t decode_scenario_player_count(std::span<const std::byte> bytes, std::size_t offset,
                                      const char* field_name) {
    const uint32_t value = read_u32(bytes, offset);
    if (value < static_cast<uint32_t>('0') || value > static_cast<uint32_t>('9')) {
        throw FormatError(std::string("scenario ") + field_name + " is not an ASCII digit");
    }
    return value - static_cast<uint32_t>('0');
}
}

uint32_t ScenarioCatalogRecord::minimum_player_count() const {
    return decode_scenario_player_count(bytes_, kMinimumPlayerCountOffset, "minimum player count +0x40");
}

uint32_t ScenarioCatalogRecord::maximum_player_count() const {
    return decode_scenario_player_count(bytes_, kMaximumPlayerCountOffset, "maximum player count +0x48");
}

void ScenarioCatalogRecord::set_player_count_range(uint32_t minimum_count, uint32_t maximum_count) {
    if (minimum_count == 0U || maximum_count == 0U || minimum_count > maximum_count ||
        maximum_count > 7U) {
        throw FormatError("scenario player-count range must satisfy 1 <= minimum <= maximum <= 7");
    }
    write_u32(bytes_, kMinimumPlayerCountOffset, static_cast<uint32_t>('0') + minimum_count);
    write_u32(bytes_, kMaximumPlayerCountOffset, static_cast<uint32_t>('0') + maximum_count);
}

uint32_t ScenarioCatalogRecord::planet_theme_index() const {
    return read_u32(bytes_, kPlanetThemeIndexOffset);
}

uint32_t ScenarioCatalogRecord::difficulty_index() const {
    return read_u32(bytes_, kDifficultyIndexOffset);
}

uint32_t ScenarioCatalogRecord::map_size_category_index() const {
    return read_u32(bytes_, kMapSizeCategoryIndexOffset);
}

void ScenarioCatalogRecord::set_frontend_catalog_indices(uint32_t planet_theme_index,
                                                           uint32_t difficulty_index,
                                                           uint32_t map_size_category_index) {
    // ScenarioCatalog_RebuildLevelRecordListPage resolves these as 0x220A+theme,
    // 0x2200+difficulty, and 0x2205+size. Restrict editor writes to the complete stock
    // selector domains instead of allowing an unresolved page-0x22 resource ID.
    if (planet_theme_index > 9U) throw FormatError("scenario planet/theme index must be 0..9");
    if (difficulty_index > 4U) throw FormatError("scenario difficulty index must be 0..4");
    if (map_size_category_index > 4U) throw FormatError("scenario map-size category must be 0..4");
    write_u32(bytes_, kPlanetThemeIndexOffset, planet_theme_index);
    write_u32(bytes_, kDifficultyIndexOffset, difficulty_index);
    write_u32(bytes_, kMapSizeCategoryIndexOffset, map_size_category_index);
}

uint32_t ScenarioCatalogRecord::alias_base() const {
    return read_u32(bytes_, kAliasBaseOffset);
}

void ScenarioCatalogRecord::set_alias_base(uint32_t value) {
    write_u32(bytes_, kAliasBaseOffset, value);
}

std::vector<std::byte> ScenarioCatalogRecord::serialize() const {
    return {bytes_.begin(), bytes_.end()};
}

std::vector<ScenarioCatalogRecord> parse_scenario_catalog_records(std::span<const std::byte> data) {
    if ((data.size() % ScenarioCatalogRecord::kSize) != 0U) {
        throw FormatError("scenario catalog data is not an exact array of 0x100-byte records");
    }
    std::vector<ScenarioCatalogRecord> records;
    records.reserve(data.size() / ScenarioCatalogRecord::kSize);
    for (std::size_t offset = 0; offset < data.size(); offset += ScenarioCatalogRecord::kSize) {
        records.push_back(ScenarioCatalogRecord::parse(data.subspan(offset, ScenarioCatalogRecord::kSize)));
    }
    return records;
}

void merge_scenario_catalog_records(std::vector<ScenarioCatalogRecord>& destination,
                                    std::span<const ScenarioCatalogRecord> source) {
    for (const auto& incoming : source) {
        const auto identifier = incoming.identifier();
        const auto found = std::find_if(destination.begin(), destination.end(), [&](const auto& existing) {
            return existing.identifier() == identifier;
        });
        if (found == destination.end()) destination.push_back(incoming);
        else *found = incoming;
    }
}

const ScenarioCatalogRecord* find_scenario_catalog_record(
    std::span<const ScenarioCatalogRecord> records, std::string_view map_name) {
    const auto key = normalize_identifier_name(std::string(map_name));
    const auto found = std::find_if(records.begin(), records.end(), [&](const auto& record) {
        return record.map_name() == key;
    });
    return found == records.end() ? nullptr : &*found;
}

uint32_t game_custom_alias_for_slot(unsigned slot) {
    if (slot > 99U) throw FormatError("game level package slot must be in the range 00..99");
    const uint32_t value = kCustomScenarioAliasFirst + static_cast<uint32_t>(slot);
    if (value > kCustomScenarioAliasLast) {
        throw FormatError("game level package slot exceeds the compact/extended text alias domain");
    }
    return value;
}

uint32_t choose_unused_scenario_alias_base(std::span<const ScenarioCatalogRecord> records,
                                           uint32_t first_candidate) {
    const uint32_t begin = std::max(first_candidate, kCustomScenarioAliasFirst);
    for (uint32_t candidate = begin; candidate <= kCustomScenarioAliasLast; ++candidate) {
        const bool used = std::any_of(records.begin(), records.end(), [&](const auto& record) {
            return record.alias_base() == candidate;
        });
        if (!used) return candidate;
    }
    throw FormatError("no free scenario alias remains in the game-safe 0x5A..0xCF domain");
}

} // namespace thandor
