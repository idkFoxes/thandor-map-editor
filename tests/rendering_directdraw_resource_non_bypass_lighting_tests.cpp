#include "thandor/catalog.hpp"
#include "thandor/model.hpp"
#include "../src/editor/primitive_render.hpp"

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
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read source file: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
}

int main(int argc, char** argv) try {
    using editor::directdraw_select_model_vertex_packed_color;

    constexpr std::uint32_t white_model_tint = 0xFFFFFFFFU;
    constexpr std::uint32_t gold_directional_facet = 0xFF9B6E35U;
    constexpr std::uint32_t faction_model_tint = 0xFF305090U;
    constexpr std::uint32_t faction_directional_facet = 0xFF182848U;

    // game.exe 0x004BD5A2-0x004BD610: 0x0200 copies modelNode+0x58 directly.
    require(directdraw_select_model_vertex_packed_color(
                0x221AU, white_model_tint, gold_directional_facet) == white_model_tint,
            "resource direct-tint body triangle did not preserve opaque white model tint");
    require(directdraw_select_model_vertex_packed_color(
                0x221BU, white_model_tint, gold_directional_facet) == white_model_tint,
            "resource luminous particle did not preserve opaque white model tint");

    // game.exe 0x004BD5B6-0x004BD5FB: triangles without 0x0200 call the ordinary
    // model-lighting helper. They must not be collapsed to the direct white tint.
    require(directdraw_select_model_vertex_packed_color(
                0x201AU, white_model_tint, gold_directional_facet) == gold_directional_facet,
            "non-bypass resource body triangle lost directional facet lighting");
    require(directdraw_select_model_vertex_packed_color(
                0x2020U, white_model_tint, gold_directional_facet) == gold_directional_facet,
            "building-resource non-bypass body triangle lost directional facet lighting");
    require(directdraw_select_model_vertex_packed_color(
                0x2200U, faction_model_tint, faction_directional_facet) == faction_model_tint,
            "generic 0x0200 model triangle did not preserve its model tint");
    require(directdraw_select_model_vertex_packed_color(
                0x2000U, faction_model_tint, faction_directional_facet) == faction_directional_facet,
            "generic non-bypass model triangle did not preserve directional lighting");

    const std::filesystem::path source = THANDOR_SOURCE_DIR;
    const auto editor_app = read_text(source / "src/editor/editor_app.cpp");
    const auto primitive = read_text(source / "src/editor/primitive_render.hpp");
    require(editor_app.find("directdraw_select_model_vertex_packed_color(") != std::string::npos &&
            editor_app.find("directionally_lit_model_tint") != std::string::npos &&
            editor_app.find("direct_model_tint") != std::string::npos,
            "object builder does not select between direct and directionally lit model colour");
    require(editor_app.find("resource_kind!=thandor::ResourceEnergyKind::None\n"
                            "                        ?0xFFFFFFFFU") == std::string::npos,
            "object builder still forces every resource triangle to opaque white");
    require(primitive.find("return directdraw_model_vertex_uses_direct_tint(render_flags)") !=
                std::string::npos,
            "DirectDraw packed-colour selection helper is missing its flag branch");

    if (argc >= 2) {
        auto catalog = thandor::AssetCatalog::scan_game_root(argv[1]);
        std::size_t body_direct = 0, body_lit = 0, particle_direct = 0, particle_lit = 0;
        for (const auto& preset : catalog.army_presets()) {
            thandor::ResourceEnergyKind kind = thandor::ResourceEnergyKind::None;
            if (preset.visual_kind == thandor::PlacementVisualKind::PowerResource)
                kind = thandor::ResourceEnergyKind::Power;
            else if (preset.visual_kind == thandor::PlacementVisualKind::BuildingResource)
                kind = thandor::ResourceEnergyKind::Building;
            if (kind == thandor::ResourceEnergyKind::None || preset.sprite_path.empty()) continue;
            const auto sprite = thandor::SpriteAsset::parse(catalog.load(preset.sprite_path));
            const auto& mesh = sprite.mesh();
            const auto body = thandor::resource_body_subresource(kind);
            const auto particle = thandor::resource_luminous_particle_subresource(kind);
            for (std::size_t index = 0; index < mesh.indices.size(); index += 3U) {
                const auto& vertex = mesh.vertices.at(mesh.indices.at(index));
                const bool direct = editor::directdraw_model_vertex_uses_direct_tint(
                    vertex.render_flags);
                if (vertex.texture_subresource == body) {
                    direct ? ++body_direct : ++body_lit;
                } else if (vertex.texture_subresource == particle) {
                    direct ? ++particle_direct : ++particle_lit;
                }
            }
        }
        require(body_direct == 272U && body_lit == 192U,
                "stock resource body direct/lit split changed");
        require(particle_direct == 159U && particle_lit == 0U,
                "stock resource particle direct/lit split changed");
        std::cout << "G344_STOCK_RESOURCE_NON_BYPASS_LIGHTING PASS body_direct="
                  << body_direct << " body_lit=" << body_lit
                  << " particle_direct=" << particle_direct
                  << " particle_lit=" << particle_lit << '\n';
    }

    std::cout << "G344 DirectDraw resource non-bypass directional-lighting checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G344 DirectDraw resource non-bypass directional-lighting checks failed: "
              << error.what() << '\n';
    return 1;
}
