#include "thandor/binary.hpp"
#include "thandor/catalog.hpp"
#include "thandor/field.hpp"
#include "thandor/gfx.hpp"
#include "thandor/model.hpp"

#include <array>
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

std::vector<std::byte> animated_model_fixture() {
    constexpr std::size_t record_size = 0x500U;
    std::vector<std::byte> data(0x200U + record_size, std::byte{0});
    thandor::write_u32(data, 0x00U, thandor::fourcc("mdl"));
    thandor::write_u32(data, 0x04U, static_cast<uint32_t>(data.size()));
    thandor::write_u32(data, 0xB0U, 1U);
    thandor::write_u32(data, 0x200U, static_cast<uint32_t>(record_size));
    thandor::write_u32(data, 0x208U, 2302U);
    thandor::write_u32(data, 0x200U + 0x4CU, 13U);
    thandor::write_u32(data, 0x200U + 0xC0U, 1U);
    constexpr std::size_t node_offset = 0x480U;
    thandor::write_u32(data, 0x200U + 0x64U, static_cast<uint32_t>(node_offset));
    thandor::write_u32(data, node_offset + 0x00U, 0x80U);
    thandor::write_u32(data, node_offset + 0x04U, 0U);
    thandor::write_u32(data, node_offset + 0x0CU, 0x4000U);
    thandor::write_u32(data, node_offset + 0x14U, 0U);
    const std::u16string path = u"spr/building/animated.spr";
    std::size_t cursor = node_offset + 0x38U;
    for (const char16_t value : path) {
        data[cursor++] = static_cast<std::byte>(value & 0xFFU);
        data[cursor++] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    }
    return data;
}
}

int main() try {
    using namespace thandor;

    auto field = FieldGrid::create(4U, 4U, 0, 0U);
    field.at(1U, 1U).terrain_height_q12 = float_to_q12(1.0F);
    field.at(2U, 1U).terrain_height_q12 = float_to_q12(2.0F);
    field.at(1U, 2U).terrain_height_q12 = float_to_q12(7.0F);
    std::vector<uint8_t> selection(16U, 0U);
    selection[1U * 4U + 1U] = 1U;
    selection[1U * 4U + 2U] = 1U;
    selection[2U * 4U + 1U] = 1U;
    require(field.set_water_selection(selection, 5.0F, true) == 2U,
            "flat water selection changed an unexpected number of wet cells");
    require(std::abs(field.at(1U, 1U).water_height() - 5.0F) < 1.0e-5F &&
            std::abs(field.at(2U, 1U).water_height() - 5.0F) < 1.0e-5F,
            "selected water cells do not share one absolute surface");
    require(field.at(1U, 2U).water_delta_q12 < 0,
            "terrain above the selected flat surface was not stored as dry");
    require(field.at(0U, 0U).water_delta_q12 < 0,
            "water selection modified an unselected cell");

    RgbaImage image{2U, 2U, {
        255U, 0U, 0U, 255U, 0U, 255U, 0U, 128U,
        0U, 0U, 255U, 255U, 255U, 255U, 0U, 64U,
    }};
    const auto temporary = std::filesystem::temp_directory_path() / "thandor_g347_texture_roundtrip.tga";
    save_tga_rgba(temporary, image);
    const auto imported = load_tga_rgba(temporary);
    std::filesystem::remove(temporary);
    require(imported.width == image.width && imported.height == image.height &&
            imported.pixels == image.pixels, "TGA texture exchange is not lossless");
    const auto encoded = encode_single_rgba_gfx(image);
    const auto gfx = GfxAsset::parse(encoded);
    require(gfx.subresources().size() == 1U, "editable GFX encoder did not emit one layer");
    const auto decoded = gfx.decode(thandor::GraphicsSubresourceIndex{0U}, false);
    require(decoded.width == image.width && decoded.height == image.height &&
            decoded.pixels == image.pixels, "editable GFX encode/decode changed RGBA pixels");

    constexpr std::array<uint32_t, 8> expected{{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U}};
    for (uint32_t faction = 0U; faction < expected.size(); ++faction) {
        require(default_faction_graphics_family(faction) == expected[faction],
                "zero-offset faction-to-army-family fallback changed");
    }
    require(std::string(default_faction_color_name(1U)) == "Blue" &&
            std::string(default_faction_color_name(2U)) == "Lime / olive" &&
            std::string(graphics_family_color_name(3U)) == "Red" &&
            std::string(graphics_family_color_name(4U)) == "Teal green",
            "stock graphics-family colour labels changed");

    const auto visuals = parse_model_visual_records(animated_model_fixture(), "mdl/building.mdl");
    require(visuals.size() == 1U && visuals[0].runtime_class_id == 13U &&
            visuals[0].has_primary_texture_animation &&
            visuals[0].primary_animated_subresource == 1U &&
            visuals[0].hierarchy_nodes.size() == 1U &&
            visuals[0].hierarchy_nodes[0].sprite_path == "spr/building/animated.spr",
            "class-13 root texture animation and MDL hierarchy were not recovered");

    const std::filesystem::path root = THANDOR_SOURCE_DIR;
    const auto editor = read_text(root / "src/editor/editor_app.cpp");
    const auto header = read_text(root / "src/editor/editor_app.hpp");
    const auto workspace = read_text(root / "src/editor/editor_workspace_ui.cpp");
    const auto renderer = read_text(root / "src/editor/terrain_renderer.cpp");
    const auto shader = read_text(root / "shaders/model.vert");
    require(workspace.find("ribbon_button") != std::string::npos &&
            workspace.find("choose_workspace_tool") != std::string::npos &&
            editor.find("switch(tool_)") != std::string::npos,
            "per-brush icon/options workflow is missing");
    require(header.find("WaterSelectionStage") != std::string::npos &&
            editor.find("Ctrl+D") != std::string::npos &&
            editor.find("draw_water_selection_overlay") != std::string::npos,
            "temporary water selection workflow is incomplete");
    require(editor.find("Export selected layer as TGA") != std::string::npos &&
            editor.find("Import TGA into selected layer") != std::string::npos,
            "editable terrain texture exchange controls are missing");
    require(renderer.find("animation_v_amplitude") != std::string::npos &&
            renderer.find("animation_cycle_ticks") != std::string::npos &&
            shader.find("triangle_wave") != std::string::npos &&
            shader.find("0..0x80000") != std::string::npos,
            "placed-building texture animation path is incomplete");

    std::cout << "G347_BRUSH_WATER_TEXTURE_FACTION_ANIMATION PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G347 checks failed: " << error.what() << '\n';
    return 1;
}
