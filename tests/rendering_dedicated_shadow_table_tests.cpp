#include "thandor/binary.hpp"
#include "thandor/field.hpp"
#include "thandor/model.hpp"
#include "thandor/terrain_shadow.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void put_u32(std::vector<std::byte>& data, std::size_t offset, uint32_t value) {
    if (offset + 4U > data.size()) throw std::runtime_error("synthetic SPR write overflow");
    data[offset + 0U] = static_cast<std::byte>(value & 0xFFU);
    data[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    data[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xFFU);
    data[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xFFU);
}

void put_q12(std::vector<std::byte>& data, std::size_t offset, float value) {
    put_u32(data, offset, static_cast<uint32_t>(static_cast<int32_t>(std::lround(value * 4096.0F))));
}

void write_mesh_record(std::vector<std::byte>& data, std::size_t offset, uint32_t flags,
                       const std::array<thandor::Vec3f, 3>& vertices) {
    constexpr uint32_t record_size = 0x120U;
    put_u32(data, offset + 0x00U, record_size);
    put_u32(data, offset + 0x08U, 3U);
    put_u32(data, offset + 0x0CU, 1U);
    put_u32(data, offset + 0x10U, flags);
    const std::size_t vertex_start = offset + 0x20U;
    for (std::size_t index = 0; index < 3U; ++index) {
        const std::size_t v = vertex_start + index * 0x40U;
        put_q12(data, v + 0x00U, vertices[index].x);
        put_q12(data, v + 0x04U, vertices[index].y);
        put_q12(data, v + 0x08U, vertices[index].z);
        put_q12(data, v + 0x10U, 0.0F);
        put_q12(data, v + 0x14U, 0.0F);
        put_q12(data, v + 0x18U, 1.0F);
    }
    const std::size_t triangle = vertex_start + 3U * 0x40U;
    constexpr std::array<std::size_t, 3> pointers{0x00U, 0x0CU, 0x18U};
    for (std::size_t index = 0; index < 3U; ++index) {
        put_u32(data, triangle + pointers[index],
                static_cast<uint32_t>(vertex_start + index * 0x40U));
    }
    put_q12(data, triangle + 0x24U, 0.0F);
    put_q12(data, triangle + 0x28U, 0.0F);
    put_q12(data, triangle + 0x2CU, 1.0F);
    put_u32(data, triangle + 0x30U, 0U);
    put_u32(data, triangle + 0x34U, 0U);
}

std::vector<std::byte> make_synthetic_sprite() {
    constexpr std::size_t visible_group = 0x200U;
    constexpr std::size_t visible_record = visible_group + 0x20U;
    constexpr std::size_t shadow_table = 0x340U;
    constexpr std::size_t shadow_record0 = shadow_table + 0x20U;
    constexpr std::size_t shadow_record1 = shadow_record0 + 0x120U;
    constexpr std::size_t declared_size = shadow_table + 0x260U;
    std::vector<std::byte> data(declared_size, std::byte{0});
    put_u32(data, 0x00U, thandor::fourcc("spr"));
    put_u32(data, 0x04U, static_cast<uint32_t>(declared_size));
    put_u32(data, 0x0CU, 0x00020007U);
    put_u32(data, 0xB0U, 1U);
    put_u32(data, 0xB8U, 0x1234U);
    put_u32(data, 0xECU, static_cast<uint32_t>(shadow_table));

    put_u32(data, visible_group + 0x00U, 0x140U);
    put_u32(data, visible_group + 0x04U, 1U);
    put_u32(data, visible_group + 0x0CU, 0x80000000U);
    write_mesh_record(data, visible_record, 0U,
                      {{{-0.4F, 0.0F, 0.1F}, {0.4F, 0.0F, 0.1F}, {0.0F, 0.0F, 0.8F}}});

    put_u32(data, shadow_table + 0x00U, 0x260U);
    put_u32(data, shadow_table + 0x04U, 2U);
    write_mesh_record(data, shadow_record0, 1U,
                      {{{-1.2F, 0.0F, 0.1F}, {1.2F, 0.0F, 0.1F}, {0.0F, 0.0F, 2.8F}}});
    write_mesh_record(data, shadow_record1, 0U,
                      {{{-0.12F, 0.0F, 0.1F}, {0.12F, 0.0F, 0.1F}, {0.0F, 0.0F, 2.5F}}});
    return data;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(stream), {}};
}
} // namespace

int main() {
    try {
        using namespace thandor;
        const auto sprite = SpriteAsset::parse(make_synthetic_sprite());
        check(sprite.mesh().indices.size() == 3U, "visible LOD synthetic triangle count changed");
        check(sprite.lod_flags(0U) == 0x80000000U, "visible LOD group flag was not preserved");
        check(sprite.has_generated_shadow_mesh(), "SPR +0xEC shadow table was not parsed");
        check(sprite.generated_shadow_record_count() == 2U,
              "dedicated shadow record count was not preserved");
        check(sprite.generated_shadow_mesh().indices.size() == 6U,
              "dedicated shadow mesh was replaced by the visible LOD mesh");
        check(sprite.generated_shadow_soft_triangle_count() == 1U,
              "shadow record flag bit 0 did not select the blurred pass");
        check(sprite.generated_shadow_hard_triangle_count() == 1U,
              "clear shadow record flag bit 0 did not select the hard pass");

        FieldGrid field = FieldGrid::create(24U, 24U, 0, 0U);
        const auto& cell = field.at(12U, 12U);
        const float ox = q12_to_float(cell.world_x_q12);
        const float oy = q12_to_float(cell.world_y_q12);
        std::vector<GeneratedShadowTriangle> triangles;
        const auto& mesh = sprite.generated_shadow_mesh();
        for (std::size_t index = 0; index < mesh.indices.size(); index += 3U) {
            GeneratedShadowTriangle output{};
            for (std::size_t corner = 0; corner < 3U; ++corner) {
                const auto& source = mesh.vertices[mesh.indices[index + corner]];
                output.world[corner] = {source.position.x + ox, source.position.y + oy,
                                        source.position.z};
            }
            output.blurred_group =
                (mesh.vertices[mesh.indices[index]].mesh_record_flags & 1U) != 0U;
            triangles.push_back(output);
        }
        const auto generated = build_game_generated_object_shadow(
            field, triangles, Vec3f{0.32F, 0.24F, -0.916F});
        check(generated.has_value(), "dedicated synthetic shadow was not generated");
        check(generated->blurred_triangle_count == 1U && generated->hard_triangle_count == 1U,
              "dedicated shadow record pass classification was lost before rasterization");

        const std::filesystem::path root = THANDOR_SOURCE_DIR;
        const auto editor = read_text(root / "src/editor/editor_app.cpp");
        const auto model = read_text(root / "src/core/model.cpp");
        check(model.find("read_u32(data, 0xECU)") != std::string::npos &&
              model.find("generated_shadow_mesh_") != std::string::npos,
              "dedicated SPR shadow-table parser is absent");
        check(editor.find("sprite.generated_shadow_mesh()") != std::string::npos &&
              editor.find("sprite.has_generated_shadow_mesh()") != std::string::npos,
              "placed shadows do not use the dedicated SPR shadow table");
        const auto collection = editor.find("generated_shadow_triangles.push_back");
        check(collection != std::string::npos &&
              editor.rfind("sprite.mesh()", collection) < editor.rfind("sprite.generated_shadow_mesh()", collection),
              "visible LOD mesh still appears to be the active shadow source");

        std::cout << "G356 dedicated SPR shadow-table tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "G356 test failure: " << error.what() << '\n';
        return 1;
    }
}
