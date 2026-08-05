#include "thandor/field.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(stream), {}};
}
}

int main() {
    try {
        using namespace thandor;
        TerrainLighting lighting{};
        lighting.ramp_124 = 0xFF201008U;
        lighting.base_color = 0xFF102030U;
        lighting.azimuth = 0U;
        lighting.elevation = 0xC000U; // -90 degrees: light rays travel straight down.
        const auto ramp = lighting.build_game_ramp();

        check(lighting.signed_directional_lut_index({0.0F, 0.0F, 1.0F}) == -256,
              "flat upward terrain did not address the brightest negative LUT entry");
        check(lighting.directional_color_for_upward_normal({0.0F, 0.0F, 1.0F}, ramp) ==
                  ramp.front(),
              "flat upward terrain did not use generated ramp entry 0");
        check(lighting.signed_directional_lut_index({1.0F, 0.0F, 0.0F}) == 0,
              "perpendicular normal did not address the fixed base-color table");
        check(lighting.directional_color_for_upward_normal({1.0F, 0.0F, 0.0F}, ramp) ==
                  lighting.ramp_124,
              "perpendicular normal did not use the exact fixed base color");
        check(lighting.signed_directional_lut_index({0.0F, 0.0F, -1.0F}) == 256,
              "back-facing normal did not preserve positive signed LUT indexing");
        check(lighting.directional_color_for_upward_normal({0.0F, 0.0F, -1.0F}, ramp) ==
                  lighting.ramp_124,
              "positive signed LUT index did not use the fixed base table");
        check(TerrainLighting::kFullyVisibleOverlayArgb == 0xFFFFFFFFU,
              "editor full-visibility overlay is not opaque white");

        const std::filesystem::path root = THANDOR_SOURCE_DIR;
        const auto field_cpp = read_text(root / "src/core/field.cpp");
        const auto editor_cpp = read_text(root / "src/editor/editor_app.cpp");
        const auto terrain_vert = read_text(root / "shaders/terrain.vert");
        check(field_cpp.find("256 + signed_index") != std::string::npos &&
              field_cpp.find("signed_dot * 256.0F") != std::string::npos,
              "signed LUT anchor mapping disappeared from field lighting");
        check(field_cpp.find("(1.0F - intensity) * 127.5F") == std::string::npos,
              "superseded symmetric directional-ramp mapping survived");
        check(editor_cpp.find("fully visible (fog of war disabled; cell overlay 0xFFFFFFFF)") !=
                  std::string::npos,
              "editor no longer declares the fully visible preview state");
        check(terrain_vert.find("directDrawFinalizeVertexLighting") !=
                  std::string::npos &&
              terrain_vert.find("fullVisibilityOverlayBytes = vec3(255.0)") !=
                  std::string::npos &&
              terrain_vert.find("0x00503D26") != std::string::npos,
              "terrain shader no longer seals the binary full-visibility multiplier");

        std::cout << "G358 full-visibility and signed directional-LUT tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "G358 test failure: " << error.what() << '\n';
        return 1;
    }
}
