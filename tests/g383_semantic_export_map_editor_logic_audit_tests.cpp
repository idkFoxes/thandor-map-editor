#include "thandor/binary.hpp"
#include "thandor/model.hpp"
#include "thandor/simulation.hpp"

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
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void write_ascii_utf16(std::span<std::byte> data, std::size_t offset, std::string_view value) {
    for (char character : value) {
        thandor::write_u16(data, offset,
            static_cast<uint16_t>(static_cast<unsigned char>(character)));
        offset += 2U;
    }
    thandor::write_u16(data, offset, 0U);
}

std::vector<std::byte> semantic_model_fixture() {
    constexpr std::size_t record_offset = 0x200U;
    constexpr std::size_t record_size = 0x300U;
    constexpr std::size_t root_node_offset = 0x500U;
    std::vector<std::byte> data(0x600U, std::byte{});
    thandor::write_u32(data, 0x00U, thandor::fourcc("mdl"));
    thandor::write_u32(data, 0x04U, static_cast<uint32_t>(data.size()));
    thandor::write_u32(data, 0xB0U, 1U);

    thandor::write_u32(data, record_offset + 0x00U, static_cast<uint32_t>(record_size));
    thandor::write_u32(data, record_offset + 0x08U, 0x1234U);
    thandor::write_i32(data, record_offset + 0x0CU, -0x180U);
    thandor::write_i32(data, record_offset + 0x10U, 0x100U);
    thandor::write_i32(data, record_offset + 0x1CU, 0x20U);
    thandor::write_u32(data, record_offset + 0x4CU, 17U);
    thandor::write_u32(data, record_offset + 0x64U, static_cast<uint32_t>(root_node_offset));
    thandor::write_i32(data, record_offset + 0x278U, 1);

    thandor::write_u32(data, root_node_offset + 0x00U, 0x80U);
    thandor::write_u32(data, root_node_offset + 0x04U, 0U);
    thandor::write_u32(data, root_node_offset + 0x0CU, 0x4000U);
    thandor::write_u32(data, root_node_offset + 0x14U, 0U);
    write_ascii_utf16(data, root_node_offset + 0x38U, "spr/test/movement.spr");
    return data;
}
} // namespace

int main() try {
    const auto records = thandor::parse_model_visual_records(
        semantic_model_fixture(), "mdl/test/movement.mdl");
    require(records.size() == 1U, "semantic MDL fixture did not produce one record");
    const auto& simulation = records.front().simulation;
    require(simulation.class_step_q12 == -0x180,
            "MDL +0x0C class-specific step was not preserved");
    require(simulation.placement_contact_kind == 1,
            "MDL +0x278 placement-contact kind was not preserved");

    thandor::SimulationMotionProfile motion{};
    motion.speed_q12_per_tick = simulation.class_step_q12;
    motion.placement_contact_kind = simulation.placement_contact_kind;
    require(motion.movable() && motion.placement_contact_kind == 1,
            "catalog-facing motion profile lost placement-contact metadata");

    const std::filesystem::path root = THANDOR_SOURCE_DIR;
    const auto model_header = read_text(root / "include/thandor/model.hpp");
    const auto simulation_header = read_text(root / "include/thandor/simulation.hpp");
    const auto model_source = read_text(root / "src/core/model.cpp");
    const auto catalog_source = read_text(root / "src/core/catalog.cpp");
    const auto simulation_source = read_text(root / "src/core/simulation.cpp");
    const auto level_source = read_text(root / "src/core/level.cpp");
    const auto field_source = read_text(root / "src/core/field.cpp");
    const auto editor_source = read_text(root / "src/editor/editor_app.cpp");

    require(model_header.find("class_step_q12") != std::string::npos &&
            model_header.find("placement_contact_kind") != std::string::npos &&
            model_header.find("class_speed_q12") == std::string::npos,
            "model simulation fields still overstate MDL +0x0C or omit +0x278");
    require(simulation_header.find("Ground-movement interpretation of MDL +0x0C") !=
                std::string::npos &&
            simulation_header.find("every other value dispatches variant A") !=
                std::string::npos,
            "motion profile does not record the class-specific step and placement-contact contract");
    require(model_source.find("offset + 0x278U") != std::string::npos &&
            catalog_source.find("placement_contact_kind") != std::string::npos,
            "semantic movement selector is not parsed and propagated");

    require(simulation_source.find("ShotDefinition_ComputeLaunchAnglesRegs") !=
                std::string::npos &&
            simulation_source.find("ShotDefinition_ComputeLaunchAngles returns") ==
                std::string::npos,
            "source comment still cites a shortened shot helper name");
    require(level_source.find("InGameLevelRuntime_LoadResourcesAfterDefaultResetCf") !=
                std::string::npos &&
            level_source.find("InGameLevelRuntime_LoadResourcesAfterExternalTablesCf") !=
                std::string::npos,
            "LEV lighting comment does not cite both recovered loader owners");
    require(field_source.find("immutable field-grid draw owner at 0x00500CE0") !=
                std::string::npos &&
            field_source.find("FieldGrid_DrawCellTriangles at 0x00500CE0") ==
                std::string::npos,
            "field rendering comment still invents a callable identity absent from the export");
    require(editor_source.find("contact %d: %s · default movement callback %s") !=
                std::string::npos &&
            editor_source.find("ArmyRuntime_ResetMovementStateFromCurrentPosition") !=
                std::string::npos &&
            editor_source.find("ArmyRuntime_ResetMovementStatePreserveQueuedTarget") !=
                std::string::npos,
            "editor UI does not expose or cite the refreshed movement semantics");

    std::cout << "G383_SEMANTIC_EXPORT_MAP_EDITOR_LOGIC_AUDIT PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G383 checks failed: " << error.what() << '\n';
    return 1;
}
