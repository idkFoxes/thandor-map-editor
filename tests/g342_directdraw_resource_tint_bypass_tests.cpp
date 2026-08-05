#include "thandor/catalog.hpp"
#include "thandor/model.hpp"
#include "../src/editor/primitive_render.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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
    using editor::directdraw_model_vertex_uses_direct_tint;

    // game.exe 0x004BD5A2-0x004BD610: bit 0x0200 stores model tint directly and returns
    // before either model-lighting helper can consume nearby GraphicsShadingRuntime records.
    require(!directdraw_model_vertex_uses_direct_tint(0x201AU),
            "lit resource body flag unexpectedly uses direct tint");
    require(!directdraw_model_vertex_uses_direct_tint(0x2020U),
            "lit building-resource body flag unexpectedly uses direct tint");
    require(directdraw_model_vertex_uses_direct_tint(0x221AU),
            "dense resource direct-tint flag was not recognized");
    require(directdraw_model_vertex_uses_direct_tint(0x221BU),
            "blue luminous-particle direct-tint flag was not recognized");
    require(directdraw_model_vertex_uses_direct_tint(0x2221U),
            "gold luminous-particle direct-tint flag was not recognized");

    const std::filesystem::path source = THANDOR_SOURCE_DIR;
    const auto model_vertex = read_text(source / "shaders/model.vert");
    const auto renderer = read_text(source / "src/editor/terrain_renderer.cpp");
    const auto editor_app = read_text(source / "src/editor/editor_app.cpp");
    const auto vertex_header = read_text(source / "src/editor/terrain_renderer.hpp");
    require(model_vertex.find("layout(location=5) in uint in_direct_tint_bypass") != std::string::npos &&
            model_vertex.find("if ((in_direct_tint_bypass & 1u) != 0u)") != std::string::npos &&
            model_vertex.find("out_color = base") != std::string::npos,
            "model vertex shader does not preserve the DirectDraw direct-tint branch after packing the frozen-building bit");
    require(renderer.find("offsetof(ObjectVertex, direct_tint_bypass)") != std::string::npos &&
            renderer.find("std::array<VkVertexInputAttributeDescription, 8>") != std::string::npos,
            "Vulkan object input does not carry the direct-tint branch bit");
    require(vertex_header.find("uint32_t direct_tint_bypass") != std::string::npos &&
            editor_app.find("directdraw_model_vertex_uses_direct_tint(triangle_render_flags)") !=
                std::string::npos,
            "object builder does not preserve the serialized 0x0200 branch");

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
                const bool direct = directdraw_model_vertex_uses_direct_tint(vertex.render_flags);
                if (vertex.texture_subresource == body) {
                    direct ? ++body_direct : ++body_lit;
                } else if (vertex.texture_subresource == particle) {
                    direct ? ++particle_direct : ++particle_lit;
                }
            }
        }
        require(body_direct == 272U && body_lit == 192U,
                "stock resource body direct-tint/lit split changed");
        require(particle_direct == 159U && particle_lit == 0U,
                "stock luminous particles no longer use the direct-tint branch");
        std::cout << "G342_STOCK_RESOURCE_TINT_ROUTING PASS body_direct=" << body_direct
                  << " body_lit=" << body_lit << " particle_direct=" << particle_direct
                  << " particle_lit=" << particle_lit << '\n';
    }

    std::cout << "G342 DirectDraw resource crystal tint-bypass checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G342 DirectDraw resource crystal tint-bypass checks failed: "
              << error.what() << '\n';
    return 1;
}
