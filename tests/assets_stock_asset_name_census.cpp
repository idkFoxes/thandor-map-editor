#include "thandor/catalog.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
struct ExpectedName {
    uint32_t asset_id;
    uint32_t model_id;
    uint32_t offset;
    const char* name;
};

std::string json_escape(std::string_view value) {
    std::string result;
    for (char character : value) {
        if (character == '\\' || character == '"') result.push_back('\\');
        if (character == '\n') { result += "\\n"; continue; }
        result.push_back(character);
    }
    return result;
}
}

int main(int argc, char** argv) try {
    if (argc < 2) {
        std::cout << "G350_STOCK_ASSET_NAME_CENSUS PASS unit-only\n";
        return 0;
    }
    const auto catalog = thandor::AssetCatalog::scan_game_root(argv[1]);
    constexpr std::array<ExpectedName, 7> expected{{
        {300U, 301U, 0U, "Headquarters"},
        {301U, 318U, 1U, "Light weapons factory"},
        {330U, 303U, 12U, "Xenite mine"},
        {320U, 311U, 8U, "Special Research lab"},
        {1U, 100U, 50U, "Weasel Jeep"},
        {10U, 101U, 53U, "ARV Dingo"},
        {50U, 103U, 59U, "Pioneer vehicle"},
    }};

    std::size_t unit_count = 0U;
    std::size_t building_count = 0U;
    std::size_t player_start_count = 0U;
    std::size_t localized_count = 0U;
    std::size_t fallback_count = 0U;
    for (const auto& preset : catalog.army_presets()) {
        const bool runtime_named =
            preset.browser_kind == thandor::PlacementBrowserKind::Unit ||
            preset.browser_kind == thandor::PlacementBrowserKind::Building ||
            preset.visual_kind == thandor::PlacementVisualKind::PlayerStart;
        if (!runtime_named) continue;
        if (preset.browser_kind == thandor::PlacementBrowserKind::Unit) ++unit_count;
        if (preset.browser_kind == thandor::PlacementBrowserKind::Building) ++building_count;
        if (preset.visual_kind == thandor::PlacementVisualKind::PlayerStart) ++player_start_count;
        if (preset.localized_name.empty()) ++fallback_count;
        else ++localized_count;
    }

    std::ostringstream json;
    json << "{\n  \"schema\": \"thandor-g350-stock-asset-name-census-v1\",\n";
    json << "  \"name_text_page\": " << thandor::kArmyAssetNameTextPage << ",\n";
    json << "  \"name_base_index\": " << thandor::kArmyAssetNameTextBaseIndex << ",\n";
    json << "  \"unit_presets\": " << unit_count << ",\n";
    json << "  \"building_presets\": " << building_count << ",\n";
    json << "  \"player_start_presets\": " << player_start_count << ",\n";
    json << "  \"localized_runtime_presets\": " << localized_count << ",\n";
    json << "  \"fallback_runtime_presets\": " << fallback_count << ",\n";
    json << "  \"samples\": [\n";
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const auto& item = expected[index];
        const auto* preset = catalog.army_preset(item.asset_id);
        if (preset == nullptr) throw std::runtime_error("expected stock preset is missing");
        if (preset->model_definition_id != item.model_id ||
            preset->model_name_text_offset != item.offset ||
            preset->localized_name != item.name ||
            preset->localized_name_source_path != "texte/help.str" ||
            preset->localized_name_source_archive.empty() ||
            preset->name_text_resource_id != thandor::army_asset_name_text_resource_id(item.offset) ||
            preset->display_name.find(item.name) == std::string::npos ||
            preset->short_name != item.name) {
            throw std::runtime_error("stock asset localized-name mapping mismatch");
        }
        if (index != 0U) json << ",\n";
        json << "    {\"asset_id\": " << item.asset_id
             << ", \"model_id\": " << item.model_id
             << ", \"mdl_name_offset\": " << item.offset
             << ", \"resource_id\": " << preset->name_text_resource_id
             << ", \"name\": \"" << json_escape(preset->localized_name)
             << "\", \"archive\": \"" << json_escape(preset->localized_name_source_archive)
             << "\"}";
    }
    json << "\n  ]\n}\n";

    if (unit_count != 130U || building_count != 43U || player_start_count != 1U ||
        localized_count != 173U || fallback_count != 1U) {
        throw std::runtime_error("stock unit/building localized-name census changed");
    }
    std::cout << json.str();
    if (argc >= 3) {
        std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("could not create census output");
        output << json.str();
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G350_STOCK_ASSET_NAME_CENSUS FAIL: " << error.what() << '\n';
    return 1;
}
