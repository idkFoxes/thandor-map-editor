#include "thandor/field.hpp"
#include "thandor/scenario_catalog.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

using namespace thandor;

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() try {
    auto field = FieldGrid::create(91U, 107U, 0, 0U);
    check(field.converter_version() == FieldGrid::kGameConverterVersion,
          "fresh FLD does not use converter version 0x00060006");
    check(field.has_game_compatible_dimensions(), "valid game dimensions were rejected");
    const auto serialized = field.serialize();
    check(read_u32(serialized, 0x0CU) == 0x00060006U,
          "serialized fresh FLD converter version changed");

    auto legacy_bytes = serialized;
    write_u32(legacy_bytes, 0x0CU, FieldGrid::kLegacyEditorConverterVersion);
    auto legacy = FieldGrid::parse(legacy_bytes);
    check(legacy.converter_version() == FieldGrid::kLegacyEditorConverterVersion,
          "legacy editor FLD fixture did not parse");
    legacy.normalize_for_game_export();
    check(legacy.converter_version() == FieldGrid::kGameConverterVersion,
          "legacy editor FLD was not normalized for game export");

    check(FieldGrid::is_game_compatible_dimension(59U), "minimum stock dimension rejected");
    check(FieldGrid::is_game_compatible_dimension(139U), "maximum proven dimension rejected");
    check(!FieldGrid::is_game_compatible_dimension(128U), "8n dimension incorrectly accepted");
    check(!FieldGrid::is_game_compatible_dimension(200U), "submitted 200-cell dimension accepted");
    check(FieldGrid::is_game_compatible_shape(131U, 131U), "stock maximum square rejected");
    check(FieldGrid::is_game_compatible_shape(139U, 123U), "bounded 139-cell-axis shape rejected");
    check(!FieldGrid::is_game_compatible_shape(139U, 139U),
          "unshipped 139x139 area accepted as game-compatible");

    auto oversized = FieldGrid::create(200U, 200U, 0, 0U);
    oversized.at(34U, 34U).terrain_height_q12 = 0x12345;
    oversized.resize(131U, 131U, true);
    check(oversized.at(0U, 0U).terrain_height_q12 == 0x12345,
          "center crop did not preserve the complete retained cell payload");
    check(oversized.at(0U, 0U).world_x_q12 == 0 && oversized.at(0U, 0U).world_y_q12 == 0,
          "center crop did not regenerate field world coordinates");
    check(oversized.at(130U, 130U).world_x_q12 == 130 * 0x901 + 130 * 0x480 &&
          oversized.at(130U, 130U).world_y_q12 == -130 * 1999,
          "resized field endpoint coordinates changed");

    std::vector<ScenarioCatalogRecord> aliases;
    aliases.push_back(ScenarioCatalogRecord::minimal("a", 0x5BU));
    aliases.push_back(ScenarioCatalogRecord::minimal("b", 0x5CU));
    check(choose_unused_scenario_alias_base(aliases) == 0x5AU,
          "deterministic alias scan did not return the first free alias");

    aliases.clear();
    for (uint32_t alias = kCustomScenarioAliasFirst; alias <= kCustomScenarioAliasLast; ++alias) {
        aliases.push_back(ScenarioCatalogRecord::minimal("m" + std::to_string(alias), alias));
    }
    bool exhausted = false;
    try {
        (void)choose_unused_scenario_alias_base(aliases);
    } catch (const FormatError&) {
        exhausted = true;
    }
    check(exhausted, "full alias domain did not report exhaustion");

    std::cout << "G331_FIELD_HEADER_BUILD PASS version=0x00060006 dimensions=59..139-step8-area<=131x131 "
                 "legacy-normalized=yes alias-scan=linear\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G331_FIELD_HEADER_BUILD FAIL: " << error.what() << '\n';
    return 1;
}
