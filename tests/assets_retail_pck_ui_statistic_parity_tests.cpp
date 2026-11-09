#include "thandor/catalog.hpp"
#include "thandor/technology.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

const thandor::ArmyAssetPreset& preset(const thandor::AssetCatalog& catalog, uint32_t id) {
    const auto* value = catalog.army_preset(id);
    if (value == nullptr) throw std::runtime_error("missing ARM preset " + std::to_string(id));
    return *value;
}

const thandor::TechnologyRecord& technology(const thandor::AssetCatalog& catalog, uint32_t id) {
    const auto records = catalog.technologies();
    if (id >= records.size() || records[id].technology_id != id)
        throw std::runtime_error("missing TEC " + std::to_string(id));
    return records[id];
}

const thandor::TechnologyPanelEntry& panel_entry(
    const thandor::ArmyAssetPreset& value, uint32_t technology_id) {
    for (const auto& entry : value.technology_panel_entries)
        if (entry.technology_id == technology_id) return entry;
    throw std::runtime_error("missing panel TEC " + std::to_string(technology_id));
}
} // namespace

int main(int argc, char** argv) try {
    const auto root = std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto model_header = read_text(root / "include/thandor/model.hpp");
    const auto model_source = read_text(root / "src/core/model.cpp");
    const auto catalog_header = read_text(root / "include/thandor/catalog.hpp");
    const auto catalog_source = read_text(root / "src/core/catalog.cpp");
    const auto technology_source = read_text(root / "src/core/technology.cpp");
    const auto editor_source = read_text(root / "src/editor/editor_app.cpp");

    require(model_header.find("retail_build_time_q5") != std::string::npos &&
            model_header.find("retail_xenite_cost_q4") != std::string::npos &&
            model_header.find("retail_energy_demand_q4") != std::string::npos,
            "MDL retail-stat domains are missing");
    require(model_source.find("offset + 0x180U") != std::string::npos &&
            model_source.find("offset + 0x184U") != std::string::npos &&
            model_source.find("offset + 0x18CU") != std::string::npos,
            "MDL +0x180/+0x184/+0x18C parsing is missing");
    require(catalog_header.find("retail_armour") != std::string::npos &&
            catalog_header.find("retail_stat_arm_node_count") != std::string::npos &&
            catalog_header.find("special_features") != std::string::npos &&
            catalog_header.find("description") != std::string::npos,
            "catalog retail card or research-detail fields are missing");
    require(catalog_source.find("for (const auto& group : preset.model_variant_groups)") !=
                std::string::npos &&
            catalog_source.find("++preset.retail_stat_arm_node_count") != std::string::npos &&
            catalog_source.find("visual->simulation.maximum_integrity") != std::string::npos &&
            catalog_source.find("visual->simulation.retail_build_time_q5") != std::string::npos &&
            catalog_source.find("visual->simulation.retail_xenite_cost_q4") != std::string::npos &&
            catalog_source.find("visual->simulation.retail_energy_demand_q4") != std::string::npos,
            "once-per-ARM-node retail statistic aggregation is missing");
    require(technology_source.find("case TechnologyCategory::Category1: return \"A\"") !=
                std::string::npos &&
            technology_source.find("case TechnologyCategory::Category2: return \"B\"") !=
                std::string::npos &&
            technology_source.find("case TechnologyCategory::Category3: return \"C\"") !=
                std::string::npos,
            "retail A/B/C technology-category formatting is missing");
    require(editor_source.find("Retail build card") != std::string::npos &&
            editor_source.find("preset.retail_energy_demand_q4 >> 4U") != std::string::npos &&
            editor_source.find("preset.retail_xenite_cost_q4 >> 4U") != std::string::npos &&
            editor_source.find("preset.retail_build_time_q5 >> 5U") != std::string::npos &&
            editor_source.find("entry.research_duration_q5 >> 5U") != std::string::npos &&
            editor_source.find("technology_category_retail_label") != std::string::npos &&
            editor_source.find("special features: %s") != std::string::npos,
            "retail tooltip/research-card formatting is incomplete");

    const std::string all_source = model_header + model_source + catalog_header + catalog_source +
        technology_source + editor_source;
    require(all_source.find("base_cost_components") == std::string::npos &&
            all_source.find("base_xenite_cost_q4") == std::string::npos &&
            all_source.find("base_energy_cost_q4") == std::string::npos &&
            all_source.find("total_maximum_integrity") == std::string::npos &&
            all_source.find("Base configuration price") == std::string::npos,
            "superseded TEC-price or visual-node integrity inference remains");
    require(editor_source.find("research_duration_q5 / 32.0") == std::string::npos,
            "research time is still formatted as a fractional value instead of retail >>5");

    require(std::string(thandor::technology_category_retail_label(
                thandor::TechnologyCategory::Category0)) == "-" &&
            std::string(thandor::technology_category_retail_label(
                thandor::TechnologyCategory::Category1)) == "A" &&
            std::string(thandor::technology_category_retail_label(
                thandor::TechnologyCategory::Category2)) == "B" &&
            std::string(thandor::technology_category_retail_label(
                thandor::TechnologyCategory::Category3)) == "C",
            "retail technology-category labels changed");

    if (argc < 2) {
        std::cout << "G399_RETAIL_PCK_UI_STATISTIC_PARITY source-contract PASS\n";
        return 0;
    }

    const auto catalog = thandor::AssetCatalog::scan_game_root(argv[1]);

    // Screenshot: SMALL TURRET / ION GUN: Armour 7500, Energy 3MWh, Xenite 620T, Time 7T.
    const auto& small_ion_turret = preset(catalog, 370U);
    require(small_ion_turret.retail_armour == 7500 &&
            (small_ion_turret.retail_energy_demand_q4 >> 4U) == 3U &&
            (small_ion_turret.retail_xenite_cost_q4 >> 4U) == 620U &&
            (small_ion_turret.retail_build_time_q5 >> 5U) == 7U &&
            small_ion_turret.retail_stat_arm_node_count == 2U,
            "ARM 370 does not match the retail small-turret card");
    require(small_ion_turret.retail_energy_demand_q4 == 48U &&
            small_ion_turret.retail_xenite_cost_q4 == 9920U &&
            small_ion_turret.retail_build_time_q5 == 243U,
            "ARM 370 raw Q4/Q5 totals changed");

    // Screenshots: both Ion/Laser and Rocket/Artillery labs show the same base build card.
    for (const uint32_t id : {321U, 323U}) {
        const auto& laboratory = preset(catalog, id);
        require(laboratory.retail_armour == 12000 &&
                (laboratory.retail_energy_demand_q4 >> 4U) == 12U &&
                (laboratory.retail_xenite_cost_q4 >> 4U) == 1100U &&
                (laboratory.retail_build_time_q5 >> 5U) == 17U &&
                laboratory.retail_stat_arm_node_count == 1U,
                "research-lab retail card mismatch for ARM " + std::to_string(id));
        require(laboratory.retail_energy_demand_q4 == 192U &&
                laboratory.retail_xenite_cost_q4 == 17600U &&
                laboratory.retail_build_time_q5 == 571U,
                "research-lab raw Q4/Q5 totals changed for ARM " + std::to_string(id));
    }

    // Screenshot: TEC 105 Ion thrower: 750T, 5MWh, 10T, category B, special features '-'.
    const auto& ion_thrower = technology(catalog, 105U);
    require(ion_thrower.title == "Ion thrower" &&
            (ion_thrower.xenite_cost_q4 >> 4U) == 750U &&
            (ion_thrower.energy_demand_q4 >> 4U) == 5U &&
            (ion_thrower.research_duration_q5 >> 5U) == 10U &&
            std::string(thandor::technology_category_retail_label(ion_thrower.category)) == "B",
            "TEC 105 does not match the retail research card");
    const auto& ion_panel = panel_entry(preset(catalog, 321U), 105U);
    require(ion_panel.special_features == "-",
            "TEC 105 special-features line was not recovered");
    require(ion_panel.description.find("primarily immobile targets") != std::string::npos &&
            ion_panel.description.find("very slow") != std::string::npos &&
            ion_panel.description.find("very energetic") != std::string::npos,
            "TEC 105 retail description was not recovered");

    std::cout << "G399_RETAIL_PCK_UI_STATISTIC_PARITY "
              << "smallTurret=7500/3/620/7 "
              << "lab=12000/12/1100/17 "
              << "ionThrower=750/5/10/B PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G399 checks failed: " << error.what() << '\n';
    return 1;
}
