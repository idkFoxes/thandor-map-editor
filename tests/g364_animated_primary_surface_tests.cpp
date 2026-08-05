#include "thandor/field.hpp"
#include "thandor/terrain_texture.hpp"

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

    require(classify_terrain_surface_path("gfx\\texturen\\water.gfx") ==
                TerrainSurfaceFamily::Water,
            "water surface family classification failed");
    require(classify_terrain_surface_path("GFX/TEXTUREN/LAVA") ==
                TerrainSurfaceFamily::Lava,
            "lava surface family classification failed");
    require(classify_terrain_surface_path("gfx\\texturen\\ice.pal") ==
                TerrainSurfaceFamily::Ice,
            "ice surface family classification failed");
    require(classify_terrain_surface_path("gfx/custom/molten") ==
                TerrainSurfaceFamily::Other,
            "custom surface family classification failed");
    require(std::string(terrain_surface_family_name(TerrainSurfaceFamily::Lava)) == "Lava",
            "lava family display name changed");

    const auto legacy = FieldGrid::build_water_wave_records(0x198FU);
    const auto shared = FieldGrid::build_surface_wave_records(0x198FU);
    require(legacy.size() == FieldGrid::kSurfaceDirectionRecordCount,
            "shared surface direction-table cardinality changed");
    for (std::size_t index = 0; index < legacy.size(); ++index) {
        require(legacy[index].angle_a == shared[index].angle_a &&
                    legacy[index].angle_b == shared[index].angle_b &&
                    legacy[index].rate_a == shared[index].rate_a &&
                    legacy[index].rate_b == shared[index].rate_b &&
                    legacy[index].scale_a == shared[index].scale_a &&
                    legacy[index].scale_b == shared[index].scale_b,
                "shared surface direction table diverged from binary-derived water records");
    }
    require(FieldGrid::surface_wave_record_index(0x123BU, 0x5675U) ==
                FieldGrid::water_wave_record_index(0x123BU, 0x5675U),
            "shared surface cell direction selector diverged");

    auto field = FieldGrid::create(2U, 2U, 0, 0U);
    for (auto& cell : field.cells()) cell.water_delta_q12 = 0x1000;
    TerrainLighting lighting{};
    lighting.ramp_12c = 0x7FA06020U;
    const auto historical = field.build_water_vertices(nullptr, &lighting);
    const auto animated = field.build_animated_surface_vertices(nullptr, &lighting);
    require(animated.size() == historical.size() && animated.size() == 6U,
            "shared surface mesh did not preserve two wet-cell triangles");
    for (std::size_t index = 0; index < animated.size(); ++index) {
        require(std::abs(animated[index].position.x - historical[index].position.x) < 1.0e-6F &&
                    std::abs(animated[index].position.y - historical[index].position.y) < 1.0e-6F &&
                    std::abs(animated[index].position.z - historical[index].position.z) < 1.0e-6F &&
                    animated[index].packed_light_argb == historical[index].packed_light_argb &&
                    animated[index].blend_materials == historical[index].blend_materials,
                "shared surface mesh changed the binary-derived projection payload");
    }

    const std::filesystem::path root = THANDOR_SOURCE_DIR;
    const auto renderer = read_text(root / "src/editor/terrain_renderer.cpp");
    const auto app = read_text(root / "src/editor/editor_app.cpp");
    const auto vertex = read_text(root / "shaders/surface.vert");
    const auto fragment = read_text(root / "shaders/surface.frag");
    const auto cmake = read_text(root / "CMakeLists.txt");

    require(renderer.find("surface.vert.spv") != std::string::npos &&
                renderer.find("surface.frag.spv") != std::string::npos &&
                renderer.find("surface_pipeline_") != std::string::npos &&
                renderer.find("build_animated_surface_vertices") != std::string::npos,
            "active renderer does not use the shared animated-surface pipeline");
    require(app.find("kSurfacePresentationTicksPerSecond=20.0") != std::string::npos &&
                app.find("renderer_.set_presentation_tick(current_presentation_tick_)") !=
                    std::string::npos &&
                renderer.find("uniform.material_scale[3] = static_cast<float>(presentation_tick_)") !=
                    std::string::npos &&
                renderer.find("animation_epoch_") == std::string::npos,
            "shared surface animation is not driven by one non-resetting recovered 20 Hz clock");
    require((vertex.find("Water, lava, and ice") != std::string::npos ||
                 vertex.find("water, lava, and ice") != std::string::npos) &&
                vertex.find("cos(angleXY) * amplitudeXY") != std::string::npos &&
                vertex.find("sin(angleXY) * amplitudeXY") != std::string::npos &&
                vertex.find("cos(angleZ) * amplitudeZ") != std::string::npos,
            "shared vertex shader lost binary-derived XYZ movement");
    require(fragment.find("shared water/lava/ice projection path") != std::string::npos &&
                fragment.find("encodedTexture * clamp(vertexDiffuse.rgb") != std::string::npos &&
                fragment.find("noise") == std::string::npos &&
                fragment.find("emissive") == std::string::npos,
            "surface fragment shader introduced an unsupported procedural lava model");
    require(app.find("classify_terrain_surface_path") != std::string::npos &&
                app.find("Active animated surface") != std::string::npos &&
                app.find("DAT/GFX/PAL-driven movement shader") != std::string::npos,
            "editor does not expose the active water/lava/ice surface family");
    require(cmake.find("surface.vert surface.frag water.vert water.frag") != std::string::npos &&
                cmake.find("thandor_g364_animated_primary_surface_tests") != std::string::npos,
            "shared surface shaders or regression were not registered");

    std::cout << "G364_ANIMATED_PRIMARY_SURFACE water_lava_ice=shared direction_records="
              << shared.size() << " PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G364 animated primary surface checks failed: " << error.what() << '\n';
    return 1;
}
