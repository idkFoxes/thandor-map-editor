#include "thandor/binary.hpp"
#include "thandor/field.hpp"
#include "thandor/gfx.hpp"
#include "thandor/terrain_texture.hpp"

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

std::vector<std::byte> stock_water_dat_fixture() {
    std::vector<std::byte> data(0xA0U);
    thandor::write_u32(data, 0x00U, 2U);
    constexpr std::array<std::array<int32_t, 6>, 4> uv{{
        {{0, 0, 0, 0x100000, 0x0DE000, 0x080000}},
        {{0x040000, 0, 0x040000, 0x100000, 0x11E000, 0x080000}},
        {{0x080000, 0, 0x080000, 0x100000, 0x15E000, 0x080000}},
        {{0x0C0000, 0, 0x0C0000, 0x100000, 0x19E000, 0x080000}},
    }};
    for (std::size_t record = 0; record < uv.size(); ++record) {
        const std::size_t offset = 0x20U + record * 0x20U;
        for (std::size_t component = 0; component < 6U; ++component) {
            thandor::write_i32(data, offset + component * 4U, uv[record][component]);
        }
        thandor::write_u32(data, offset + 0x18U, 0U);
        thandor::write_u32(data, offset + 0x1CU, 0U);
    }
    return data;
}
}

int main(int argc, char** argv) try {
    using namespace thandor;

    const auto layout = TerrainSurfaceTextureLayout::parse(stock_water_dat_fixture());
    require(layout.selector_bit_count() == 2U, "stock water selector bit count changed");
    require(layout.selector_mask() == 3U && layout.record_count() == 4U,
            "stock water selector range changed");
    require(layout.record(0U).uv_q12[3] == 0x100000 &&
            layout.record(0U).uv_q12[4] == 0x0DE000,
            "water DAT record zero UVs changed");
    require(layout.record(2U).uv_q12[0] == 0x080000 &&
            layout.record(2U).uv_q12[4] == 0x15E000,
            "water DAT record two UVs changed");
    for (const auto& record : layout.records()) {
        require(record.material_index == 0U && record.palette_index == 0U,
                "stock water DAT texture/palette selector changed");
    }

    const auto waves = FieldGrid::build_water_wave_records();
    require(waves[0].angle_a == 55704U && waves[0].angle_b == 48973U &&
            waves[0].rate_a == 622 && waves[0].rate_b == -609 &&
            waves[0].scale_a == 142U && waves[0].scale_b == 131U,
            "first binary-derived water direction record changed");
    require(waves[1].angle_a == 30594U && waves[1].angle_b == 16951U &&
            waves[1].rate_a == -581 && waves[1].rate_b == -521 &&
            waves[1].scale_a == 152U && waves[1].scale_b == 141U,
            "second binary-derived water direction record changed");
    for (const auto& wave : waves) {
        require(wave.scale_a >= 0x80U && wave.scale_a <= 0x9FU &&
                wave.scale_b >= 0x80U && wave.scale_b <= 0x9FU,
                "water wave amplitude escaped binary range");
        require(std::abs(static_cast<int>(wave.rate_a)) >= 0x200 &&
                std::abs(static_cast<int>(wave.rate_a)) <= 0x27F &&
                std::abs(static_cast<int>(wave.rate_b)) >= 0x200 &&
                std::abs(static_cast<int>(wave.rate_b)) <= 0x27F,
                "water wave phase step escaped binary range");
    }
    require(FieldGrid::water_wave_record_index(0x0000123BU, 0x00005675U) == 0xB5U,
            "water cell-to-direction-record mapping changed");

    auto field = FieldGrid::create(2U, 2U, 0, 0U);
    for (auto& cell : field.cells()) cell.water_delta_q12 = 0x1000;
    TerrainLighting lighting{};
    lighting.ramp_12c = 0x6480C0FFU;
    lighting.ramp_124 = 0x00204060U;
    lighting.base_color = 0x00102030U;
    lighting.elevation = 0x4000U;
    lighting.azimuth = 0U;
    const auto water = field.build_water_vertices(&layout, &lighting);
    require(water.size() == 6U, "wet cell did not emit two water triangles");
    const uint32_t expected_diffuse = lighting.ramp_12c;
    for (const auto& vertex : water) {
        require(vertex.material == 0U, "water DAT texture layer changed");
        require(vertex.packed_light_argb == expected_diffuse,
                "water packet does not carry the binary-derived per-vertex diffuse color");
        require(vertex.packed_light_argb != 0xFF001E43U,
                "textured water was incorrectly multiplied by the PAL-associated packet dword");
        require(vertex.blend_materials[2] != 0U,
                "water vertex lost its encoded amplitude pair");
    }
    // The first primary RNG value selects record 2 for the stock two-bit mask.
    require(std::abs(water[0].u - 0.5F) < 1.0e-6F &&
            std::abs(water[1].v - 1.0F) < 1.0e-6F &&
            water[2].u > 1.0F,
            "water DAT wrapped UVs are not preserved against the 256-pixel upload extent");

    const std::filesystem::path root = THANDOR_SOURCE_DIR;
    const auto vertex_shader = read_text(root / "shaders/water.vert");
    const auto fragment_shader = read_text(root / "shaders/water.frag");
    const auto renderer = read_text(root / "src/editor/terrain_renderer.cpp");
    const auto app = read_text(root / "src/editor/editor_app.cpp");
    require(vertex_shader.find("cos(angleXY) * amplitudeXY") != std::string::npos &&
            vertex_shader.find("sin(angleXY) * amplitudeXY") != std::string::npos &&
            vertex_shader.find("cos(angleZ) * amplitudeZ") != std::string::npos,
            "water vertex shader lost the recovered X/Y/Z wave vector construction");
    require(fragment_shader.find("clamp(texel.rgb, 0.0, 1.0) * clamp(vertexDiffuse.rgb") != std::string::npos &&
            fragment_shader.find("linearToSrgb") == std::string::npos &&
            fragment_shader.find("paletteTint") == std::string::npos &&
            fragment_shader.find("encodedLighting") == std::string::npos,
            "water fragment shader does not match the encoded-byte Direct3D diffuse path");
    require(renderer.find("VK_POLYGON_MODE_FILL, true, true, VK_CULL_MODE_BACK_BIT") !=
                std::string::npos,
            "water pipeline lost textured-preset-3 alpha/depth-write state");
    require(app.find("kSurfacePresentationTicksPerSecond=20.0") != std::string::npos &&
            app.find("renderer_.set_presentation_tick(current_presentation_tick_)") !=
                std::string::npos &&
            renderer.find("uniform.material_scale[3] = static_cast<float>(presentation_tick_)") !=
                std::string::npos,
            "water direction phases are not driven by the persistent editor presentation clock");
    require(renderer.find("update_surface_materials") != std::string::npos &&
            renderer.find("TextureSamplingMode::DirectDrawSingleLevelLinear") !=
                std::string::npos,
            "water GFX upload lost the DirectDraw single-level sampler policy");
    require(app.find("surface_base+\".dat\"") != std::string::npos &&
            app.find("surface_base+\".gfx\"") != std::string::npos &&
            app.find("surface_base+\".pal\"") != std::string::npos,
            "LEV primary surface DAT/GFX/PAL chain is incomplete");

    if (argc == 4) {
        const auto stock_layout = TerrainSurfaceTextureLayout::parse(read_file(argv[1]));
        const auto stock_gfx = GfxAsset::parse(read_file(argv[2]));
        const auto stock_pal = PalAsset::parse(read_file(argv[3]));
        require(stock_layout.selector_bit_count() == 2U && stock_layout.record_count() == 4U,
                "packaged water.dat shape changed");
        require(stock_gfx.subresources().size() == 4U,
                "packaged water.gfx layer count changed");
        require(!stock_pal.colors_argb().empty() && stock_pal.colors_argb()[0] == 0xFF001E43U,
                "packaged water.pal entry zero changed");
        std::size_t pixels = 0U;
        uint8_t minimum_blue = 255U, maximum_blue = 0U;
        for (std::size_t layer = 0; layer < stock_gfx.subresources().size(); ++layer) {
            const auto image = stock_gfx.decode(thandor::GraphicsSubresourceIndex{static_cast<uint32_t>(layer)}, false);
            require(image.width == 128U && image.height == 128U,
                    "packaged water GFX dimensions changed");
            for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4U) {
                require(image.pixels[offset + 3U] == 255U,
                        "packaged water GFX unexpectedly gained transparency");
                minimum_blue = std::min(minimum_blue, image.pixels[offset + 2U]);
                maximum_blue = std::max(maximum_blue, image.pixels[offset + 2U]);
                ++pixels;
            }
        }
        require(pixels == 4U * 128U * 128U && minimum_blue >= 90U && maximum_blue >= 160U,
                "packaged water color field no longer matches the blue/cyan source texture");
        std::cout << "G346_STOCK_WATER_CENSUS layers=4 pixels=" << pixels
                  << " blue=" << static_cast<unsigned>(minimum_blue) << ".."
                  << static_cast<unsigned>(maximum_blue) << '\n';
    }

    std::cout << "G346_DIRECTDRAW_WATER_SURFACE_ANIMATION_COLOR PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G346 water surface checks failed: " << error.what() << '\n';
    return 1;
}
