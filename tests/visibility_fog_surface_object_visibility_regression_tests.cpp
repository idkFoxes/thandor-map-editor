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
    const std::filesystem::path root = THANDOR_SOURCE_DIR;
    const auto field_hpp = read_text(root / "include/thandor/field.hpp");
    const auto field_cpp = read_text(root / "src/core/field.cpp");
    const auto render_policy = read_text(root / "include/thandor/simulation_render_policy.hpp");
    const auto editor = read_text(root / "src/editor/editor_app.cpp");
    const auto renderer_hpp = read_text(root / "src/editor/terrain_renderer.hpp");
    const auto renderer = read_text(root / "src/editor/terrain_renderer.cpp");
    const auto primitive = read_text(root / "src/editor/primitive_render.hpp");
    const auto terrain_vert = read_text(root / "shaders/terrain.vert");
    const auto surface_vert = read_text(root / "shaders/surface.vert");
    const auto model_vert = read_text(root / "shaders/model.vert");
    const auto model_frag = read_text(root / "shaders/model.frag");

    require(field_hpp.find("uint32_t fog_cell_index{}") != std::string::npos &&
            field_cpp.find("static_cast<uint32_t>(cell_index)") != std::string::npos &&
            field_cpp.find("static_cast<uint32_t>(static_cast<std::size_t>(y) * width_ + x)") != std::string::npos,
            "terrain/surface vertices lost their source FLD visibility index");

    require(renderer_hpp.find("update_fog_visibility") != std::string::npos &&
            renderer.find("dstBinding = 5") != std::string::npos &&
            renderer.find("VK_DESCRIPTOR_TYPE_STORAGE_BUFFER") != std::string::npos &&
            renderer.find("offsetof(thandor::TerrainVertex, fog_cell_index)") != std::string::npos,
            "fog runtime buffer is not bound to terrain/surface vertex data");

    require(terrain_vert.find("layout(std430,set=0,binding=5) readonly buffer FogVisibilityBuffer") != std::string::npos &&
            terrain_vert.find("runtimeVisibility = fogVisibility.cells[inFogCellIndex] & 255u") != std::string::npos &&
            terrain_vert.find("runtimeVisibility == 255u") != std::string::npos &&
            terrain_vert.find("lightingBytes = retailLighting * (float(runtimeVisibility) / 255.0)") != std::string::npos,
            "terrain fog is not per-vertex runtime-state modulation");

    const auto displacement = surface_vert.find("vec3 worldPosition = inPosition + displacement");
    const auto surface_fog = surface_vert.find("vertexDiffuse.rgb *= float(runtimeVisibility) / 255.0");
    require(displacement != std::string::npos && surface_fog != std::string::npos && displacement < surface_fog &&
            surface_vert.find("layout(std430,set=0,binding=5) readonly buffer FogVisibilityBuffer") != std::string::npos,
            "animated water/lava does not consume fog in its displaced surface path");

    require(render_policy.find("kObjectVertexFogDynamicReplacementBit = 1U << 6U") != std::string::npos &&
            model_vert.find("out_static_animation_category = in_direct_tint_bypass >> 1u") != std::string::npos &&
            model_frag.find("(in_static_animation_category & 32u) != 0u") != std::string::npos &&
            model_frag.find("fogVisibility.header.x != 0u") != std::string::npos,
            "static placement suppression is not tied to fog-enabled dynamic replacement");

    require(editor.find("const bool fog_dynamic_replacement=fog_preview_mode_!=0") != std::string::npos &&
            editor.find("if(fog_dynamic_replacement)\n                            direct_tint_bypass|=thandor::kObjectVertexFogDynamicReplacementBit") != std::string::npos &&
            editor.find("fog_preview_model_intensity(root_position,fog_flags)") != std::string::npos &&
            editor.find("if(fog_intensity==0U)continue") != std::string::npos,
            "fog replacement still excludes Element/General scenery or lacks root visibility culling");

    require(editor.find("effect_currently_visible=fog_preview_mode_==0||") != std::string::npos &&
            editor.find("if(fog_preview_mode_!=0&&!fog_preview_point_visible(origin))continue") != std::string::npos &&
            editor.find("fog_preview_model_intensity(shadow_position,shadow_model_flags)==0U") != std::string::npos,
            "effects or generated shadows can leak through unseen terrain");

    require(editor.find("renderer_.update_fog_visibility(frame_index,fog_preview_runtime_cells_,fog_preview_mode_!=0)") != std::string::npos,
            "temporally clamped fog runtime bytes are not uploaded each dynamic scene revision");

    require(editor.find("Fog is uploaded as per-cell runtime visibility") != std::string::npos &&
            editor.find("append_fog_vertex") == std::string::npos &&
            editor.find("FogOverlayTerrainDepthTest") == std::string::npos &&
            primitive.find("FogOverlayTerrainDepthTest") == std::string::npos &&
            renderer.find("FogOverlayTerrainDepthTest") == std::string::npos,
            "superseded coplanar fog world geometry returned");

    std::cout << "G411_FOG_SURFACE_OBJECT_VISIBILITY_CORRECTION PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G411 fog/surface/object visibility checks failed: " << error.what() << '\n';
    return 1;
}
