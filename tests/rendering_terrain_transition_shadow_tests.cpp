#include "thandor/catalog.hpp"
#include "thandor/field.hpp"
#include "thandor/terrain_texture.hpp"
#include "thandor/terrain_shadow.hpp"

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
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(stream), {}};
}
}

int main(int argc, char** argv) {
    try {
        using namespace thandor;
        FieldGrid field = FieldGrid::create(8U, 8U, 0, 0U);
        for (uint32_t y = 0; y < field.height(); ++y) {
            for (uint32_t x = 0; x < field.width(); ++x) {
                field.at(x, y).set_material(x < 4U ? 2U : 5U);
            }
        }
        TerrainLighting lighting{};
        lighting.ramp_124 = 0xFFB0B0B0U;
        lighting.base_color = 0xFF101010U;
        lighting.elevation = 0x3000U;
        lighting.azimuth = 0x1000U;

        const auto base_packets = field.build_vertices(lighting, 2.0F,
            TerrainMaterialPreviewMode::DirectCellMaterial);
        check(base_packets.size() == static_cast<std::size_t>(7U * 7U * 6U),
              "base terrain packet count changed");
        for (const auto& vertex : base_packets) {
            check(vertex.blend_weights[0] == 1.0F && vertex.blend_weights[1] == 0.0F &&
                  vertex.blend_weights[2] == 0.0F && vertex.blend_weights[3] == 0.0F,
                  "G339 base packet unexpectedly uses the superseded G338 weight blend");
        }

        const auto& center = field.at(4U, 4U);
        const Vec3f source{q12_to_float(center.world_x_q12),
                           q12_to_float(center.world_y_q12), 1.0F};
        const auto shadow = project_directional_shadow_to_field(
            field, source, {0.2F, 0.0F, 0.98F}, 0.035F, 3U, 4.0F);
        check(shadow.has_value(), "flat-field directional shadow projection failed");
        check(shadow->x < source.x, "shadow was not projected opposite the light direction");
        check(std::abs(shadow->z - 0.035F) < 0.002F,
              "shadow did not conform to flat terrain with the configured bias");

        const std::filesystem::path source_root = THANDOR_SOURCE_DIR;
        const auto terrain_fragment = read_text(source_root / "shaders/terrain.frag");
        const auto shadow_fragment = read_text(source_root / "shaders/shadow.frag");
        const auto renderer = read_text(source_root / "src/editor/terrain_renderer.cpp");
        const auto editor = read_text(source_root / "src/editor/editor_app.cpp");
        check(terrain_fragment.find("blendWeights") == std::string::npos &&
              terrain_fragment.find("exact soil.dat packet") != std::string::npos,
              "G338 whole-material transition approximation was not superseded");
        check(shadow_fragment.find("32x32 by default") != std::string::npos &&
              shadow_fragment.find("texture(object_textures") != std::string::npos,
              "G355 generated-shadow alpha path did not supersede source-texture silhouettes");
        // Preserve the two model queues and complete terrain queue; shadows now
        // come from a separate depth pass and are sampled directly by the receivers.
        const auto pre_model = renderer.find("draw_static_models(RetailWorldModelPass::PreTerrain)");
        const auto terrain_draw = renderer.find("if (index_count_)", pre_model);
        const auto surface_draw = renderer.find("if (draw_surface && depth_sorted_surface_index_counts_", terrain_draw);
        const auto post_model = renderer.find("draw_static_models(RetailWorldModelPass::PostGeneratedShading)", surface_draw);
        check(pre_model != std::string::npos && terrain_draw != std::string::npos &&
              surface_draw != std::string::npos && post_model != std::string::npos &&
              pre_model < terrain_draw && terrain_draw < surface_draw && surface_draw < post_model,
              "model and terrain queues changed order");
        check(renderer.find("if (shadow_index_count_)") == std::string::npos,
              "raised legacy shadow receivers are still drawn");
        const auto shadow_pass=editor.find("renderer_.prepare_shadow_frame");
        const auto main_pass=editor.find("vulkan_.begin_render_pass",shadow_pass);
        check(shadow_pass!=std::string::npos && main_pass!=std::string::npos && shadow_pass<main_pass,
              "directional depth pass must precede receiver rendering");
        check(terrain_fragment.find("shadowVisibility()")!=std::string::npos,
              "terrain no longer receives depth-map shadows");
        check(editor.find("resource_kind==thandor::ResourceEnergyKind::None") != std::string::npos,
              "resource effect geometry is not excluded from shadow generation");
        check(editor.find("sprite.generated_shadow_mesh()") != std::string::npos &&
              editor.find("sprite.has_generated_shadow_mesh()") != std::string::npos &&
              editor.find("receiver_surface_decal") == std::string::npos,
              "G356 dedicated shadow geometry did not supersede visible receiver-decal projection");
        check(editor.find("Object shadows") != std::string::npos,
              "shadow control is missing from the renderer UI");

        if (argc >= 2) {
            const std::filesystem::path game_root = argv[1];
            auto catalog = AssetCatalog::scan_archive_root(game_root);
            const auto soil_layout = TerrainTextureLayout::parse(catalog.load("gfx/boden/soil.dat"));
            check(soil_layout.material_count() == 26U, "stock soil material count changed");
            for (uint32_t material = 0U; material < 26U; ++material) {
                check(soil_layout.records(static_cast<uint8_t>(material)).size() == 64U,
                      "stock soil transition-record count changed");
            }
            std::cout << "G338_STOCK_SOIL_TRANSITION_CENSUS materials=26 records=1664\n";
        }

        std::cout << "G338 terrain transition and projected shadow tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "G338 test failure: " << error.what() << '\n';
        return 1;
    }
}
