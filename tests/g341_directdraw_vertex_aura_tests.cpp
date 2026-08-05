#include "thandor/effect.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("unable to read " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}

int main() {
    try {
        using namespace thandor;
        require(graphics_shading_directdraw_lookup_index(0.0F, 1.875F) == 128U,
                "center lookup index changed");
        require(graphics_shading_directdraw_lookup_index(0.9375F, 1.875F) == 96U,
                "half-radius lookup index changed");
        require(graphics_shading_directdraw_lookup_index(1.875F, 1.875F) == 0U,
                "radius edge lookup index changed");

        require(graphics_shading_directdraw_raw_word(155U, 128U) == 312U,
                "gold red raw MMX word changed");
        require(graphics_shading_directdraw_raw_word(90U, 128U) == 181U,
                "gold green raw MMX word changed");
        require(graphics_shading_directdraw_channel_addition(155U, 128U) == 77U,
                "gold red white-modulated addition changed");
        require(graphics_shading_directdraw_channel_addition(90U, 128U) == 45U,
                "gold green white-modulated addition changed");
        require(graphics_shading_directdraw_modulated_channel(128U, 312U) == 167U,
                "terrain-light modulation order changed");
        require(graphics_shading_directdraw_modulated_channel(255U, 0U) == 255U,
                "unshaded white lane changed");

        const std::filesystem::path root = THANDOR_SOURCE_DIR;
        const auto terrain_vert = read_text(root / "shaders/terrain.vert");
        const auto terrain_frag = read_text(root / "shaders/terrain.frag");
        const auto model_vert = read_text(root / "shaders/model.vert");
        const auto model_frag = read_text(root / "shaders/model.frag");
        const auto renderer = read_text(root / "src/editor/terrain_renderer.cpp");

        require(terrain_vert.find("remainingSquared * 128.0 / radiusSquared") != std::string::npos,
                "terrain vertex shader lost DirectDraw radius quotient");
        require(terrain_vert.find("lookupIndex * 257.0 / 16.0") != std::string::npos,
                "terrain vertex shader lost packed lookup table scale");
        require(terrain_vert.find("directDrawResourceShadingWords") != std::string::npos &&
                terrain_vert.find("directDrawFinalizeVertexLighting") != std::string::npos,
                "terrain resource aura is no longer assembled in vertex lighting");
        require(terrain_vert.find("layout(location=3) out vec3 lightingBytes") != std::string::npos,
                "terrain lighting interpolation became flat");
        require(terrain_frag.find("ResourceShadingBuffer") == std::string::npos &&
                terrain_frag.find("generatedResourceShadingOverlay") == std::string::npos &&
                terrain_frag.find("mix(result") == std::string::npos,
                "terrain fragment shader reintroduced a translucent aura overlay");
        require(model_vert.find("direct_draw_resource_shading_words") != std::string::npos &&
                model_vert.find("direct_draw_finalize_vertex_lighting") != std::string::npos,
                "model resource light is no longer evaluated per vertex");
        require(model_frag.find("ResourceShadingBuffer") == std::string::npos &&
                model_frag.find("resource_shading_addition") == std::string::npos,
                "model fragment shader reintroduced radial resource tinting");
        require(renderer.find("VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT") !=
                    std::string::npos,
                "resource shading buffer is not visible to the vertex shaders");

        std::cout << "G341_DIRECTDRAW_VERTEX_RESOURCE_AURA PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "G341_DIRECTDRAW_VERTEX_RESOURCE_AURA FAIL: " << error.what() << '\n';
        return 1;
    }
}
