#include "thandor/binary.hpp"
#include "thandor/level.hpp"
#include "thandor/model.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

void write_ascii_utf16(std::span<std::byte> data, std::size_t offset, std::string_view value) {
    for (const char character : value) {
        thandor::write_u16(data, offset,
            static_cast<uint16_t>(static_cast<unsigned char>(character)));
        offset += 2U;
    }
    thandor::write_u16(data, offset, 0U);
}

std::vector<std::byte> make_level_fixture() {
    std::vector<std::byte> bytes(0x340U, std::byte{});
    thandor::write_u32(bytes, 0x00U, thandor::fourcc("lev"));
    thandor::write_u32(bytes, 0x04U, static_cast<uint32_t>(bytes.size()));
    thandor::write_u32(bytes, 0xD8U, 0U);
    thandor::write_u32(bytes, 0xDCU, static_cast<uint32_t>(bytes.size()));
    for (std::size_t index = 0U; index < 10U; ++index)
        thandor::write_u32(bytes, 0xB0U + index * 4U, static_cast<uint32_t>(0x318U + index * 4U));

    const std::size_t slot0 = 0x200U;
    thandor::write_u32(bytes, slot0 + 0x0CU, 0x1000U);
    thandor::write_u32(bytes, slot0 + 0x14U, 6500U * 16U);
    thandor::write_u32(bytes, slot0 + 0x18U, 3000U * 16U);
    return bytes;
}

std::vector<std::byte> make_model_fixture(uint32_t runtime_class) {
    constexpr std::size_t record_offset = 0x200U;
    constexpr std::size_t record_size = 0x300U;
    constexpr std::size_t root_node_offset = 0x500U;
    std::vector<std::byte> data(0x600U, std::byte{});
    thandor::write_u32(data, 0x00U, thandor::fourcc("mdl"));
    thandor::write_u32(data, 0x04U, static_cast<uint32_t>(data.size()));
    thandor::write_u32(data, 0xB0U, 1U);
    thandor::write_u32(data, record_offset + 0x00U, static_cast<uint32_t>(record_size));
    thandor::write_u32(data, record_offset + 0x08U, 999U);
    thandor::write_u32(data, record_offset + 0x4CU, runtime_class);
    thandor::write_u32(data, record_offset + 0xC0U, 800U);
    thandor::write_i32(data, record_offset + 0xC4U, 123);
    thandor::write_u32(data, record_offset + 0x18CU, 192U);
    thandor::write_u32(data, record_offset + 0x64U, static_cast<uint32_t>(root_node_offset));
    thandor::write_u32(data, root_node_offset + 0x00U, 0x80U);
    thandor::write_u32(data, root_node_offset + 0x04U, 0U);
    thandor::write_u32(data, root_node_offset + 0x0CU, 0x4000U);
    thandor::write_u32(data, root_node_offset + 0x14U, 0U);
    write_ascii_utf16(data, root_node_offset + 0x38U, "spr/test/g405.spr");
    return data;
}
} // namespace

int main() try {
    auto level = thandor::LevelAsset::parse(make_level_fixture());
    require(level.faction_xenite_q4(1U) == 6500U * 16U,
            "LEV player slot +0x14 is not exposed as starting Xenite Q4");
    require(level.faction_tritium_q4(1U) == 3000U * 16U,
            "LEV player slot +0x18 is not exposed as starting Tritium Q4");
    level.set_faction_starting_resources_q4(1U, 777U * 16U, 333U * 16U);
    const auto serialized = level.serialize();
    require(thandor::read_u32(serialized, 0x214U) == 777U * 16U &&
            thandor::read_u32(serialized, 0x218U) == 333U * 16U,
            "LEV Xenite/Tritium setter did not preserve the exact +0x14/+0x18 fields");

    const auto generator = thandor::parse_model_visual_records(make_model_fixture(16U), "mdl/g405_generator.mdl");
    require(generator.size() == 1U, "class-16 synthetic model was not parsed");
    require(generator.front().simulation.retail_energy_demand_q4 == 192U,
            "MDL +0x18C is not parsed as ongoing Energy demand Q4");
    require(generator.front().simulation.energy_generation_capacity_q4 == 800U,
            "runtime-class-16 MDL +0xC0 is not parsed as Energy generation capacity Q4");

    const auto articulated = thandor::parse_model_visual_records(make_model_fixture(3U), "mdl/g405_articulated.mdl");
    require(articulated.size() == 1U, "class-3 synthetic model was not parsed");
    require(articulated.front().simulation.energy_generation_capacity_q4 == 0U,
            "MDL +0xC0 generation semantics leaked into a non-class-16 model");
    require(articulated.front().simulation.articulated_contact_interpolation_rate_q12 == 800,
            "class-specific MDL +0xC0 articulated semantics were damaged by the Energy closure");

    const auto root = std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto editor = read_text(root / "src/editor/editor_app.cpp");
    const auto catalog = read_text(root / "src/core/catalog.cpp");
    const auto level_header = read_text(root / "include/thandor/level.hpp");
    const auto model_header = read_text(root / "include/thandor/model.hpp");
    const auto technology_header = read_text(root / "include/thandor/technology.hpp");

    require(editor.find("Xenite (gold)") != std::string::npos &&
            editor.find("Tritium (blue)") != std::string::npos,
            "resource-field paint choices still expose pre-V428 semantics");
    require(editor.find("InputInt(\"Xenite\"") != std::string::npos &&
            editor.find("InputInt(\"Tritium\"") != std::string::npos,
            "faction starting-resource editor does not expose Xenite/Tritium");
    require(editor.find("Energy generation +") != std::string::npos &&
            editor.find("Demand %llu MWh · Xenite ") != std::string::npos,
            "asset tooltip does not distinguish Energy demand from generation");
    require(editor.find("Demand%u Time%u") != std::string::npos,
            "technology tooltip still presents research Energy demand as an opaque cost");

    require(catalog.find("Tritium resource (buildable, blue)") != std::string::npos &&
            catalog.find("Xenite resource (buildable, gold)") != std::string::npos,
            "asset-browser resource names were not corrected to Tritium/Xenite");
    require(catalog.find("Active Tritium deposit") != std::string::npos &&
            catalog.find("Active Xenite deposit") != std::string::npos,
            "resource group descriptions were not corrected to Tritium/Xenite");

    require(level_header.find("start_xenite_q4") != std::string::npos &&
            level_header.find("start_tritium_q4") != std::string::npos &&
            level_header.find("start_building_resource_q4") == std::string::npos &&
            level_header.find("start_energy_resource_q4") == std::string::npos,
            "LEV semantic field names are mixed between old and V428 meanings");
    require(model_header.find("retail_energy_demand_q4") != std::string::npos &&
            model_header.find("energy_generation_capacity_q4") != std::string::npos &&
            model_header.find("retail_energy_cost_q4") == std::string::npos,
            "MDL Energy demand/generation semantics are not represented distinctly");
    require(technology_header.find("energy_demand_q4") != std::string::npos &&
            technology_header.find("energy_cost_q4") == std::string::npos,
            "TEC +0x24 still uses the superseded Energy-cost name");

    require(editor.find("Building material — gold (FLD 0x0800)") == std::string::npos &&
            editor.find("Energy — blue (FLD 0x1000)") == std::string::npos &&
            catalog.find("Energy resource (buildable, blue)") == std::string::npos &&
            catalog.find("Building resource (buildable, gold)") == std::string::npos,
            "pre-V428 user-facing resource semantics remain in active source");

    std::cout << "G405_RESOURCE_FUEL_ENERGY_SEMANTIC_CLOSURE PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G405_RESOURCE_FUEL_ENERGY_SEMANTIC_CLOSURE FAIL: " << error.what() << '\n';
    return 1;
}
