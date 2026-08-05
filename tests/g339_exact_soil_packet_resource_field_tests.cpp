#include "thandor/binary.hpp"
#include "thandor/catalog.hpp"
#include "thandor/field.hpp"
#include "thandor/terrain_texture.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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
    if (!stream) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(stream), {}};
}

std::vector<std::byte> make_layout(uint32_t material_count) {
    std::vector<std::byte> data(0x20U + static_cast<std::size_t>(material_count) * 0x800U);
    thandor::write_u32(data, 0U, material_count);
    for (uint32_t material = 0U; material < material_count; ++material) {
        for (uint32_t group = 0U; group < 8U; ++group) {
            for (uint32_t variant = 0U; variant < 8U; ++variant) {
                const uint32_t record = group * 8U + variant;
                const std::size_t offset = 0x20U + static_cast<std::size_t>(material) * 0x800U +
                                           static_cast<std::size_t>(record) * 0x20U;
                for (uint32_t vertex = 0U; vertex < 3U; ++vertex) {
                    const int32_t u_pixel = static_cast<int32_t>(material * 7U + group * 3U +
                                                                  variant * 11U + vertex);
                    const int32_t v_pixel = static_cast<int32_t>(group * 13U + variant * 5U + vertex);
                    thandor::write_i32(data, offset + vertex * 8U,
                                       thandor::float_to_q12(static_cast<float>(u_pixel)));
                    thandor::write_i32(data, offset + vertex * 8U + 4U,
                                       thandor::float_to_q12(static_cast<float>(v_pixel)));
                }
                thandor::write_u32(data, offset + 0x18U, material);
                thandor::write_u32(data, offset + 0x1CU, variant);
            }
        }
    }
    return data;
}

float signed_area(const thandor::TerrainVertex& a, const thandor::TerrainVertex& b,
                  const thandor::TerrainVertex& c) {
    return (b.position.x - a.position.x) * (c.position.y - a.position.y) -
           (b.position.y - a.position.y) * (c.position.x - a.position.x);
}
}

int main(int argc, char** argv) try {
    using namespace thandor;

    const auto all_equal = select_terrain_soil_packets({2U, 2U, 2U});
    require(all_equal.count == 1U && all_equal.choices[0].source_vertex == 0U &&
            all_equal.choices[0].variant == 0U, "all-equal selector changed");
    const auto m0m1 = select_terrain_soil_packets({2U, 2U, 5U});
    require(m0m1.count == 2U && m0m1.choices[1].source_vertex == 2U &&
            m0m1.choices[1].variant == 1U, "m0=m1 selector changed");
    const auto m0m2 = select_terrain_soil_packets({2U, 5U, 2U});
    require(m0m2.count == 2U && m0m2.choices[1].source_vertex == 1U &&
            m0m2.choices[1].variant == 2U, "m0=m2 selector changed");
    const auto m1m2 = select_terrain_soil_packets({2U, 5U, 5U});
    require(m1m2.count == 2U && m1m2.choices[1].source_vertex == 1U &&
            m1m2.choices[1].variant == 3U, "m1=m2 selector changed");
    const auto distinct = select_terrain_soil_packets({2U, 4U, 5U});
    require(distinct.count == 3U && distinct.choices[1].source_vertex == 1U &&
            distinct.choices[1].variant == 4U && distinct.choices[2].source_vertex == 2U &&
            distinct.choices[2].variant == 5U, "all-distinct selector changed");

    uint32_t state = FieldGrid::kDefaultSoilRandomSeed;
    const std::array<uint32_t, 4> expected_random{{0xD2EE31AEU, 0x5421D998U,
                                                   0x94612623U, 0xDAA9BF4DU}};
    for (const uint32_t expected : expected_random) {
        require(FieldGrid::random_next_primary(state) == expected,
                "Random_NextPrimary recurrence changed");
    }

    auto field = FieldGrid::create(2U, 2U, 0, 0U);
    field.at(0U, 0U).set_material(0U); // TL
    field.at(0U, 1U).set_material(1U); // BL
    field.at(1U, 0U).set_material(2U); // TR
    field.at(1U, 1U).set_material(3U); // BR
    const auto groups = field.build_soil_random_groups();
    require(groups.size() == 4U, "soil random group count changed");

    const auto layout = TerrainTextureLayout::parse(make_layout(6U));
    TerrainLighting lighting{};
    lighting.ramp_124 = 0xFFFFFFFFU;
    const auto exact = field.build_vertices(lighting, 2.0F,
        TerrainMaterialPreviewMode::ExactSoilDatPackets, &layout,
        FieldGrid::kDefaultSoilRandomSeed);
    // Two all-distinct triangles, three packets each, three vertices per packet.
    require(exact.size() == 18U, "exact packet vertex count changed");
    for (std::size_t packet = 0U; packet < exact.size(); packet += 3U) {
        require(exact[packet].material == exact[packet + 1U].material &&
                exact[packet].material == exact[packet + 2U].material,
                "packet material is not flat");
        require(signed_area(exact[packet], exact[packet + 1U], exact[packet + 2U]) > 0.0F,
                "BL-to-TR packet winding changed");
    }
    require(exact[0U].material == 0U && exact[3U].material == 1U &&
            exact[6U].material == 2U, "triangle-0 packet order changed");
    require(exact[9U].material == 1U && exact[12U].material == 3U &&
            exact[15U].material == 2U, "triangle-1 packet order changed");

    const auto expected_u = [&](uint8_t material, uint8_t group, uint8_t variant,
                                uint8_t vertex) {
        const float pixel = static_cast<float>(material * 7U + group * 3U +
                                               variant * 11U + vertex);
        return pixel / 256.0F;
    };
    require(std::abs(exact[3U].u - expected_u(1U, groups[2U], 4U, 0U)) < 1.0e-6F,
            "soil record Q12 U coordinate was not mapped directly");
    require(exact[3U].u >= 0.0F, "synthetic exact UV unexpectedly negative");

    const auto base_only = field.build_vertices(lighting, 2.0F,
        TerrainMaterialPreviewMode::DirectCellMaterial, &layout,
        FieldGrid::kDefaultSoilRandomSeed);
    require(base_only.size() == 6U, "base-packet diagnostic count changed");

    Brush brush{};
    brush.shape = BrushShape::Square;
    brush.falloff = BrushFalloff::Constant;
    brush.radius_cells = 0.49F;
    require(field.paint_placement_occupancy_class(0.0F, 0.0F, brush, 0U, true) == 1U,
            "gold resource field did not paint");
    require(field.placement_occupancy_class_present(0U, 0U, 0U),
            "gold 0x0800 field bit missing");
    require(!field.placement_occupancy_class_present(0U, 0U, 1U),
            "painting gold modified blue field");
    require(field.paint_placement_occupancy_class(0.0F, 0.0F, brush, 1U, true) == 1U,
            "blue resource field did not paint");
    require(field.placement_occupancy_class_present(0U, 0U, 0U) &&
            field.placement_occupancy_class_present(0U, 0U, 1U),
            "resource field classes are not independent");

    const std::filesystem::path source_root = THANDOR_SOURCE_DIR;
    const auto editor = read_text(source_root / "src/editor/editor_app.cpp");
    const auto renderer = read_text(source_root / "src/editor/terrain_renderer.cpp");
    const auto fragment = read_text(source_root / "shaders/terrain.frag");
    require(editor.find("Building material — gold (FLD 0x0800)") != std::string::npos &&
            editor.find("Energy — blue (FLD 0x1000)") != std::string::npos,
            "both resource-field choices are not reachable in the UI");
    require(editor.find("Soil transition RNG seed") != std::string::npos,
            "soil random seed control is missing");
    require(renderer.find("VK_SAMPLER_ADDRESS_MODE_REPEAT") != std::string::npos,
            "terrain sampler is not repeat-wrapped");
    require(fragment.find("Each draw triangle is one exact soil.dat packet") != std::string::npos &&
            fragment.find("blendWeights") == std::string::npos,
            "terrain shader still blends whole materials");

    if (argc >= 2) {
        const std::filesystem::path game_root = argv[1];
        auto catalog = AssetCatalog::scan_game_root(game_root);
        const auto stock_layout = TerrainTextureLayout::parse(catalog.load("gfx/boden/soil.dat"));
        require(stock_layout.material_count() == 26U, "stock soil material count changed");
        int32_t minimum_u = std::numeric_limits<int32_t>::max();
        int32_t maximum_u = std::numeric_limits<int32_t>::min();
        int32_t minimum_v = std::numeric_limits<int32_t>::max();
        int32_t maximum_v = std::numeric_limits<int32_t>::min();
        std::size_t record_count = 0U;
        for (uint8_t material = 0U; material < 26U; ++material) {
            for (const auto& record : stock_layout.records(material)) {
                ++record_count;
                for (std::size_t vertex = 0U; vertex < 3U; ++vertex) {
                    const int32_t u = record.uv_q12[vertex * 2U] / 4096;
                    const int32_t v = record.uv_q12[vertex * 2U + 1U] / 4096;
                    minimum_u = std::min(minimum_u, u);
                    maximum_u = std::max(maximum_u, u);
                    minimum_v = std::min(minimum_v, v);
                    maximum_v = std::max(maximum_v, v);
                }
            }
        }
        require(record_count == 1664U, "stock soil record count changed");
        require(minimum_u == -64 && maximum_u == 287 && minimum_v == 0 && maximum_v == 255,
                "stock wrapped soil UV range changed");
        std::cout << "G339_STOCK_SOIL_PACKET_CENSUS materials=26 records=1664 u="
                  << minimum_u << ".." << maximum_u << " v=" << minimum_v << ".."
                  << maximum_v << '\n';
    }

    std::cout << "G339 exact soil packet, diagonal, RNG, UV-wrap, and resource-field tests passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G339 test failure: " << error.what() << '\n';
    return 1;
}
