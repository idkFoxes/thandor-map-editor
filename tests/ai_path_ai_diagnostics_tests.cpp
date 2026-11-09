#include "thandor/field.hpp"
#include "thandor/path_diagnostics.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}
}

int main() try {
    const auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);

    // V519's path grid is exactly 4x each FLD dimension and uses +0x800 >> 10 rounding.
    require(thandor::v519_path_world_to_scratch(0, 0) ==
                thandor::V519PathScratchCoordinate{2, 2},
            "world origin no longer projects to V519 scratch cell 2/2");
    const auto& cell_11 = field.at(1U, 1U);
    require(thandor::v519_path_world_to_scratch(cell_11.world_x_q12, cell_11.world_y_q12) ==
                thandor::V519PathScratchCoordinate{6, 5},
            "known FLD vertex no longer matches V519 4x scratch projection");

    require(thandor::v519_path_clip_to_inner_scratch(field, {-20, 999999}) ==
                thandor::V519PathScratchCoordinate{1, 234},
            "V519 one-cell inner scratch-border clipping changed");

    const auto center = thandor::v519_path_scratch_center_to_world_q12({2, 2});
    require(center.x_q12 == 432 && center.y_q12 == -250,
            "V519 scratch-center inverse fixed-point constants changed");

    const auto masks = thandor::v519_path_masks(3U, 2U, 4U);
    require(masks.traversal_bands_valid && masks.faction_presence_mask == 0x8U,
            "V519 faction-presence mask changed");
    require(masks.high_cost_mask == 0x00040000U,
            "V519 low-band high-cost mask changed");
    require(masks.blocking_mask == 0x10000400U,
            "V519 low/high traversal blocking mask changed");
    require(!thandor::v519_path_masks(1U, 8U, 0U).traversal_bands_valid,
            "invalid traversal bands were accepted by editor diagnostics");

    // Source contract: the V519 AI bit must be an additive semantic view over raw ARM +0x14, not
    // a regression that removes the existing class-2 signed track multiplier interpretation.
    const auto source = std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto catalog_header = read_text(source / "include/thandor/catalog.hpp");
    const auto catalog_cpp = read_text(source / "src/core/catalog.cpp");
    require(catalog_header.find("army_runtime_flags14_raw") != std::string::npos &&
                catalog_header.find("ai_candidate_eligible_bit0") != std::string::npos,
            "V519 raw ARM +0x14 / AI eligibility semantic fields missing");
    require(catalog_cpp.find("preset.army_runtime_flags14_raw = read_u32(bytes, offset + 0x14U)") !=
                std::string::npos,
            "V519 raw ARM +0x14 parser missing");
    require(catalog_cpp.find("preset.track_texture_travel_scale =") != std::string::npos &&
                catalog_cpp.find("preset.track_texture_travel_scale = read_i32(bytes, offset + 0x14U)") !=
                    std::string::npos,
            "class-2 signed ARM +0x14 track interpretation regressed");

    std::cout << "V519_PATH_AI_DIAGNOSTICS PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V519_PATH_AI_DIAGNOSTICS FAIL: " << error.what() << '\n';
    return 1;
}
