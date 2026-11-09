#include "thandor/field.hpp"
#include "thandor/gfx.hpp"

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
    if (!stream) throw std::runtime_error("unable to read " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
}

int main() try {
    using namespace thandor;
    auto field = FieldGrid::create(2U, 2U, float_to_q12(10.0F), 0U);
    field.at(0U, 0U).water_delta_q12 = float_to_q12(2.0F);
    field.at(0U, 1U).water_delta_q12 = -1;
    field.at(1U, 0U).water_delta_q12 = float_to_q12(1.0F);
    field.at(1U, 1U).water_delta_q12 = -1;
    const auto vertices = field.build_water_vertices(nullptr, nullptr);
    require(vertices.size() == 6U, "mixed wet/dry cell did not emit both secondary triangles");
    bool saw_negative_depth = false;
    for (const auto& vertex : vertices) {
        const auto& source = field.cells()[vertex.fog_cell_index];
        const float expected = q12_to_float(source.terrain_height_q12 + source.water_delta_q12);
        require(std::abs(vertex.position.z - expected) < 1.0e-6F,
                "secondary vertex was flattened away from its source FLD signed depth");
        saw_negative_depth = saw_negative_depth || source.water_delta_q12 < 0;
    }
    require(saw_negative_depth, "fixture did not cover a dry signed-depth secondary vertex");

    const std::filesystem::path root = THANDOR_SOURCE_DIR;
    const auto renderer = read_text(root / "src/editor/terrain_renderer.cpp");
    const auto terrain = read_text(root / "shaders/terrain.frag");
    const auto surface = read_text(root / "shaders/surface.frag");
    const auto water = read_text(root / "shaders/water.frag");
    require(renderer.find("native_uniform_square_extent") != std::string::npos &&
            renderer.find("VK_FORMAT_R8G8B8A8_UNORM") != std::string::npos &&
            renderer.find("VK_FORMAT_R8G8B8A8_SRGB") != std::string::npos,
            "renderer no longer separates UNORM terrain/surface sampling from object sRGB sampling");
    require(renderer.find("const uint32_t extent = native_uniform_square_extent(layers, 256U)") != std::string::npos &&
            renderer.find("kSurfacePreviewStagingBudgetBytes") != std::string::npos &&
            renderer.find("upload_extent /= 2U") != std::string::npos &&
            renderer.find("upload_image_array(surface_textures_, layers,\n                           upload_extent") != std::string::npos,
            "surface texture upload must preserve native resolution when affordable, "
            "with a memory-bounded fallback for oversized preview arrays");
    require(terrain.find("linearToSrgb") == std::string::npos &&
            terrain.find("clamp(sampledBytes, 0.0, 1.0)") != std::string::npos,
            "terrain shader is no longer encoded-byte-domain UNORM modulation");
    for (const auto* text : {&surface, &water}) {
        require(text->find("linearToSrgb") == std::string::npos &&
                text->find("clamp(texel.rgb, 0.0, 1.0) * clamp(vertexDiffuse.rgb") != std::string::npos,
                "primary-surface shader is no longer encoded-byte-domain UNORM modulation");
    }
    std::cout << "V561_GROUND_SURFACE_TEXTURE_PARITY PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V561 ground/surface parity failed: " << error.what() << '\n';
    return 1;
}
