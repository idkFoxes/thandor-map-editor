#include "thandor/field.hpp"

#include <cmath>
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
}

int main() try {
    using namespace thandor;

    auto field = FieldGrid::create(4U, 4U, 0, 0U);
    for (uint32_t y = 0U; y < field.height(); ++y) {
        for (uint32_t x = 0U; x < field.width(); ++x) {
            field.at(x, y).terrain_height_q12 = float_to_q12(9.0F);
        }
    }
    field.at(1U, 1U).terrain_height_q12 = float_to_q12(1.0F);
    field.at(2U, 1U).terrain_height_q12 = float_to_q12(2.0F);
    field.at(1U, 2U).terrain_height_q12 = float_to_q12(7.0F);
    std::vector<uint8_t> selection(16U, 0U);
    selection[1U * 4U + 1U] = 1U;
    selection[1U * 4U + 2U] = 1U;
    selection[2U * 4U + 1U] = 1U;
    require(field.set_water_selection(selection, 6.25F, true) == 2U,
            "flat-water setup changed an unexpected number of cells");

    TerrainLighting lighting{};
    lighting.ramp_12c = 0x6480C0FFU;
    const auto water = field.build_water_vertices(nullptr, &lighting);
    require(!water.empty(), "flat-water selection emitted no triangles");
    for (const auto& vertex : water) {
        require(std::abs(vertex.position.z - 6.25F) < 1.0e-5F,
                "dry shoreline corner bent a flat water selection into a hill");
        require(vertex.packed_light_argb == lighting.ramp_12c,
                "water vertex did not preserve LEV secondary colour and alpha");
        require(((vertex.packed_light_argb >> 24U) & 0xFFU) == 0x64U,
                "water alpha was forced opaque");
    }

    const std::filesystem::path root = THANDOR_SOURCE_DIR;
    const auto editor = read_text(root / "src/editor/editor_app.cpp");
    const auto field_source = read_text(root / "src/core/field.cpp");
    const auto water_shader = read_text(root / "shaders/water.frag");
    const auto renderer = read_text(root / "src/editor/terrain_renderer.cpp");

    require(editor.find("draw_global_faction_selector(\"unit-browser-faction\")") !=
                std::string::npos &&
            editor.find("draw_global_faction_selector(\"building-browser-faction\")") !=
                std::string::npos &&
            editor.find("draw_global_faction_selector(\"object-view-faction\")") !=
                std::string::npos,
            "global faction selector is not shared by Units, Buildings, and Objects");
    require(editor.find("armyN.gfx carries the faction colour") !=
                std::string::npos &&
            editor.find("if(untextured){") != std::string::npos &&
            editor.find("modulation=family.palette.colors_argb()[palette_index]") !=
                std::string::npos &&
            editor.find("palette_modulation") == std::string::npos,
            "textured faction assets are still multiplied by armyN.pal");
    require(field_source.find("boundary_surface_q12") != std::string::npos &&
            field_source.find("lighting->ramp_12c") != std::string::npos,
            "flat shoreline or LEV water-alpha source is missing");
    require(water_shader.find("texel.a * vertexDiffuse.a") != std::string::npos &&
            renderer.find("VK_BLEND_FACTOR_SRC_ALPHA") != std::string::npos &&
            renderer.find("VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA") != std::string::npos,
            "water shader or pipeline no longer consumes stock vertex alpha");

    std::cout << "G348_FLAT_WATER_ALPHA_FACTION_SCOPE PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G348 checks failed: " << error.what() << '\n';
    return 1;
}
