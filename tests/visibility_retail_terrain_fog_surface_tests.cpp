#include "thandor/field.hpp"
#include "../src/editor/texture_sampling.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read source file: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
}

int main() try {
    using editor::TextureSamplingMode;
    using editor::texture_sampling_plan;

    // Retail Glide terrain setup disables mipmaps, enables wrap, and requests bilinear min/mag.
    constexpr auto terrain = texture_sampling_plan(
        256U, TextureSamplingMode::DirectDrawSingleLevelLinear);
    require(terrain.mip_levels == 1U, "retail terrain sampler gained mip levels");
    require(!terrain.allow_anisotropy, "retail terrain sampler gained anisotropy");
    require(!terrain.blend_between_mip_levels, "retail terrain sampler blends mip levels");

    // Raise only the axial down-left neighbour. A four-neighbour rectangular central difference
    // incorrectly leaves the center normal vertical; the retail six-neighbour FLD normal must tilt.
    auto field = thandor::FieldGrid::create(5U, 5U, 0, 0U);
    field.at(1U, 3U).terrain_height_q12 = 0x1000;
    const auto& center = field.at(2U, 2U);
    const auto sample = field.sample_terrain(thandor::q12_to_float(center.world_x_q12),
                                             thandor::q12_to_float(center.world_y_q12));
    const auto normal = sample.normal;
    require(normal.z > 0.0F, "retail terrain normal points downward");
    require(std::abs(normal.x) > 0.01F || std::abs(normal.y) > 0.01F,
            "six-neighbour diagonal height did not affect terrain normal");

    const auto& neighbour = field.at(1U, 3U);
    const long double dz = static_cast<long double>(neighbour.terrain_height_q12 - center.terrain_height_q12);
    const long double expected_x = -static_cast<long double>(neighbour.world_x_q12 - center.world_x_q12) * dz;
    const long double expected_y = -static_cast<long double>(neighbour.world_y_q12 - center.world_y_q12) * dz;
    const long double expected_z = 0x00C00000;
    const long double length = std::sqrt(expected_x * expected_x + expected_y * expected_y + expected_z * expected_z);
    require(std::abs(normal.x - static_cast<float>(expected_x / length)) < 1.0e-5F &&
            std::abs(normal.y - static_cast<float>(expected_y / length)) < 1.0e-5F &&
            std::abs(normal.z - static_cast<float>(expected_z / length)) < 1.0e-5F,
            "terrain normal does not match the six-neighbour fixed-point vector");

    const std::filesystem::path root = THANDOR_SOURCE_DIR;
    const auto editor = read_text(root / "src/editor/editor_app.cpp");
    const auto renderer = read_text(root / "src/editor/terrain_renderer.cpp");
    const auto field_source = read_text(root / "src/core/field.cpp");

    const auto field_header = read_text(root / "include/thandor/field.hpp");
    const auto terrain_vertex_shader = read_text(root / "shaders/terrain.vert");
    require(field_header.find("uint32_t fog_cell_index{}") != std::string::npos &&
            field_source.find("static_cast<uint32_t>(cell_index)") != std::string::npos &&
            terrain_vertex_shader.find("runtimeVisibility = fogVisibility.cells[inFogCellIndex] & 255u") != std::string::npos &&
            terrain_vertex_shader.find("lightingBytes = retailLighting * (float(runtimeVisibility) / 255.0)") != std::string::npos,
            "fog no longer carries independent FLD-vertex visibility into interpolated terrain lighting");
    require(editor.find("append_fog_vertex") == std::string::npos &&
            editor.find("FogOverlayTerrainDepthTest") == std::string::npos,
            "superseded coplanar fog geometry returned after the G411 visibility correction");

    const auto material_function = renderer.substr(renderer.find("void TerrainRenderer::update_materials"),
        renderer.find("void TerrainRenderer::update_surface_materials") - renderer.find("void TerrainRenderer::update_materials"));
    require(material_function.find("TextureSamplingMode::DirectDrawSingleLevelLinear") != std::string::npos &&
            material_function.find("TextureSamplingMode::MipmappedAnisotropic") == std::string::npos,
            "ground texture upload does not use retail single-level linear sampling");
    require(field_source.find("kHexNeighbours") != std::string::npos &&
            field_source.find("{1, 0}, {0, 1}, {0, -1}, {-1, 0}, {-1, 1}, {1, -1}") != std::string::npos,
            "terrain normal no longer uses the retail six-neighbour FLD lattice");

    std::cout << "G408_RETAIL_TERRAIN_FOG_SURFACE PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G408 retail terrain/fog surface checks failed: " << error.what() << '\n';
    return 1;
}
