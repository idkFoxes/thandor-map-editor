#include "thandor/binary.hpp"
#include "thandor/catalog.hpp"
#include "thandor/field.hpp"
#include "thandor/gfx.hpp"
#include "thandor/level.hpp"
#include "thandor/terrain_texture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <limits>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

float signed_xy_area(const thandor::TerrainVertex& a, const thandor::TerrainVertex& b,
                     const thandor::TerrainVertex& c) {
    return (b.position.x - a.position.x) * (c.position.y - a.position.y) -
           (b.position.y - a.position.y) * (c.position.x - a.position.x);
}

std::vector<std::byte> make_test_dat() {
    std::vector<std::byte> data(0x20 + 26U * 0x800U);
    thandor::write_u32(data, 0, 26);
    for (uint32_t material = 0; material < 26; ++material) {
        for (uint32_t record = 0; record < 64; ++record) {
            const std::size_t offset = 0x20 + static_cast<std::size_t>(material) * 0x800 + record * 0x20;
            const std::array<float, 6> uv = record == 3
                ? std::array<float, 6>{128.0F, 9.0F, 64.0F, 119.0F, 191.0F, 119.0F}
                : std::array<float, 6>{64.0F, 9.0F, 128.0F, 119.0F, 191.0F, 9.0F};
            for (std::size_t component = 0; component < uv.size(); ++component) {
                thandor::write_i32(data, offset + component * 4, thandor::float_to_q12(uv[component]));
            }
            thandor::write_u32(data, offset + 0x18, material);
        }
    }
    return data;
}
} // namespace

int main(int argc, char** argv) try {
    using namespace thandor;
    const auto layout = TerrainTextureLayout::parse(make_test_dat());
    auto field = FieldGrid::create(4, 5, float_to_q12(1.0F), 2);
    field.at(1, 1).set_material(8);
    TerrainLighting lighting{};
    lighting.base_color = 0xFF606060U;
    lighting.ramp_124 = 0x00909090U;
    const auto vertices = field.build_vertices(lighting, 2.0F);
    const auto indices = field.build_triangle_indices();
    check(vertices.size() == static_cast<std::size_t>(3 * 4 * 6), "synthetic terrain mesh size mismatch");
    check(indices.size() == vertices.size(), "synthetic terrain index size mismatch");
    for (std::size_t triangle = 0; triangle < indices.size(); triangle += 3) {
        check(signed_xy_area(vertices[indices[triangle]], vertices[indices[triangle + 1]],
                             vertices[indices[triangle + 2]]) > 0.0F,
              "synthetic terrain triangle winding is not CCW");
    }
    // Adjacent logical cells must sample the same terrain point with the same UV. G308/G309
    // reset projected DAT coordinates per quad, which visibly mashed the diamond texture into every tile.
    check(std::abs(vertices[5].u - vertices[6].u) < 1.0e-6F &&
          std::abs(vertices[5].v - vertices[6].v) < 1.0e-6F,
          "adjacent terrain cells reset their diffuse UVs");
    check(std::abs(vertices[5].u - 0.5F) < 1.0e-6F &&
          std::abs(vertices[5].v) < 1.0e-6F,
          "terrain diffuse repeat scale is not two logical cells");

    // Ground-projected resource decals must follow the exact fixed FLD triangle diagonal rather
    // than snapping to the nearest cell vertex. Use a non-planar 2x2 height pattern so the two
    // triangle branches have different interpolation results.
    auto sampled = FieldGrid::create(2, 2, 0, 0);
    sampled.at(0, 0).terrain_height_q12 = float_to_q12(0.0F);
    sampled.at(1, 0).terrain_height_q12 = float_to_q12(10.0F);
    sampled.at(0, 1).terrain_height_q12 = float_to_q12(20.0F);
    sampled.at(1, 1).terrain_height_q12 = float_to_q12(40.0F);
    const auto& origin = sampled.at(0, 0);
    const auto& x_axis = sampled.at(1, 0);
    const auto& y_axis = sampled.at(0, 1);
    const auto sample_grid = [&](float gx, float gy) {
        const float ox = q12_to_float(origin.world_x_q12);
        const float oy = q12_to_float(origin.world_y_q12);
        const float xx = q12_to_float(x_axis.world_x_q12) - ox;
        const float xy = q12_to_float(x_axis.world_y_q12) - oy;
        const float yx = q12_to_float(y_axis.world_x_q12) - ox;
        const float yy = q12_to_float(y_axis.world_y_q12) - oy;
        return sampled.sample_terrain(ox + xx * gx + yx * gy, oy + xy * gx + yy * gy);
    };
    const auto first_triangle = sample_grid(0.25F, 0.50F);
    const auto second_triangle = sample_grid(0.75F, 0.50F);
    check(first_triangle.inside && std::abs(first_triangle.height - 12.5F) < 0.01F,
          "terrain sampler does not follow the TL/BL/TR triangle");
    check(second_triangle.inside && std::abs(second_triangle.height - 20.0F) < 0.01F,
          "terrain sampler does not follow the BL/BR/TR triangle");
    const auto outside_sample = sample_grid(-0.25F, 0.5F);
    check(!outside_sample.inside, "terrain sampler did not report an out-of-field position");

    if (argc >= 2) {
        const fs::path root = argv[1];
        std::function<std::vector<std::byte>(std::string_view)> load_stock;
        if (fs::is_regular_file(root / "game.exe")) {
            auto catalog = std::make_shared<AssetCatalog>(AssetCatalog::scan_game_root(root));
            load_stock = [catalog](std::string_view path) { return catalog->load(path); };
        } else {
            load_stock = [root](std::string_view path) {
                const std::string normalized = normalize_asset_path(path);
                const auto slash = normalized.find_last_of('/');
                return read_file(root / normalized.substr(slash == std::string::npos ? 0 : slash + 1));
            };
        }
        const auto stock_level = LevelAsset::parse(load_stock("level/asgard.lev"));
        const auto stock_field = FieldGrid::parse(load_stock("level/asgard.fld"));
        const auto stock_layout = TerrainTextureLayout::parse(load_stock("gfx/boden/soil.dat"));
        check(stock_field.width() == 123 && stock_field.height() == 123, "Asgard FLD dimensions changed");
        check(stock_field.material_mask() == 0x004003E0U, "Asgard material mask changed");
        check(stock_level.paths().ground_textures == "gfx/boden/soil", "Asgard ground texture base changed");
        check(stock_layout.material_count() == 26, "soil.dat material count changed");

        float field_min_x = std::numeric_limits<float>::max();
        float field_max_x = std::numeric_limits<float>::lowest();
        float field_min_y = std::numeric_limits<float>::max();
        float field_max_y = std::numeric_limits<float>::lowest();
        for (uint32_t y = 0; y < stock_field.height(); ++y) {
            for (uint32_t x = 0; x < stock_field.width(); ++x) {
                const auto& cell = stock_field.at(x, y);
                const float render_x = q12_to_float(cell.world_x_q12);
                const float render_y = q12_to_float(cell.world_y_q12);
                field_min_x = std::min(field_min_x, render_x);
                field_max_x = std::max(field_max_x, render_x);
                field_min_y = std::min(field_min_y, render_y);
                field_max_y = std::max(field_max_y, render_y);
            }
        }
        std::size_t aligned_placements = 0;
        std::size_t raw_pair_placements = 0;
        for (const auto& placement : stock_level.placements()) {
            const auto inside = [&](float x, float y) {
                return x >= field_min_x && x <= field_max_x &&
                       y >= field_min_y && y <= field_max_y;
            };
            if (inside(placement.render_x(), placement.render_y())) ++aligned_placements;
            if (inside(placement.world_x(), placement.world_y())) ++raw_pair_placements;
        }
        check(aligned_placements == stock_level.placements().size(),
              "Asgard LEV placements do not align with FLD render axes");
        check(raw_pair_placements == 0,
              "Asgard raw LEV world-X/world-Y pair unexpectedly matches FLD render axes");

        std::array<RgbaImage, 26> layers;
        for (uint32_t material = 0; material < 26; ++material) {
            const std::string path = std::string("gfx/boden/soil") + static_cast<char>('a' + material) + ".gfx";
            auto gfx = GfxAsset::parse(load_stock(path));
            check(gfx.subresources().size() == 1, "stock soil GFX subresource count changed");
            layers[material] = gfx.decode(thandor::GraphicsSubresourceIndex{0U}, false);
            check(layers[material].width == 256 && layers[material].height == 256,
                  "stock soil GFX dimensions changed");
        }

        const auto stock_vertices = stock_field.build_vertices(
            stock_level.lighting(), 2.0F, TerrainMaterialPreviewMode::ExactSoilDatPackets,
            &stock_layout, FieldGrid::kDefaultSoilRandomSeed);
        check(stock_vertices.size() >= static_cast<std::size_t>(122 * 122 * 6),
              "Asgard exact terrain packet count is below the base mesh");
        check((stock_vertices.size() % 3U) == 0U, "Asgard packet vertex count is not triangular");
        std::map<uint32_t, std::size_t> material_vertices;
        float minimum_area = std::numeric_limits<float>::max();
        for (std::size_t triangle = 0; triangle < stock_vertices.size(); triangle += 3U) {
            const auto& a = stock_vertices[triangle];
            const auto& b = stock_vertices[triangle + 1U];
            const auto& c = stock_vertices[triangle + 2U];
            const float area = signed_xy_area(a, b, c);
            minimum_area = std::min(minimum_area, area);
            check(area > 0.0F, "Asgard terrain contains a back-face-culled packet");
            check(a.material == b.material && a.material == c.material,
                  "Asgard soil packet material is not flat");
            check(a.material < layers.size(), "Asgard packet material exceeds 26 layers");
            material_vertices[a.material] += 3U;
        }
        check(material_vertices.size() == 6U, "Asgard used-material cardinality changed");
        check(stock_vertices.size() > static_cast<std::size_t>(122 * 122 * 6),
              "Asgard material boundaries did not emit transition packets");
        std::cout << "STOCK_TERRAIN_RENDER_VALIDATION PASS"
                  << " width=" << stock_field.width()
                  << " height=" << stock_field.height()
                  << " triangles=" << stock_vertices.size() / 3
                  << " vertices=" << stock_vertices.size()
                  << " material_mask=0x" << std::hex << stock_field.material_mask() << std::dec
                  << " used_materials=" << material_vertices.size()
                  << " transition_packets="
                  << (stock_vertices.size() / 3U - static_cast<std::size_t>(122 * 122 * 2))
                  << " loaded_layers=26"
                  << " placements_aligned=" << aligned_placements
                  << " raw_pair_placements_in_field=" << raw_pair_placements
                  << " minimum_signed_xy_area=" << minimum_area << '\n';
    }

    std::cout << "Thandor terrain render tests: PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "Thandor terrain render tests: FAIL: " << error.what() << '\n';
    return 1;
}
