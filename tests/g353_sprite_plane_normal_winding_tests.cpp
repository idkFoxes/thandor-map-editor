#include "thandor/binary.hpp"
#include "thandor/catalog.hpp"
#include "thandor/model.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
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
using thandor::SpriteMeshVertex;
using thandor::Vec3f;

struct RawCorner {
    Vec3f position{};
    float u{};
    float v{};
};

struct RawTriangle {
    std::array<RawCorner, 3> corners{};
    Vec3f plane_normal{};
    uint32_t texture_subresource{};
    uint32_t render_flags{};
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(float lhs, float rhs, float tolerance = 0.00001F) {
    return std::abs(lhs - rhs) <= tolerance;
}

Vec3f subtract(Vec3f lhs, Vec3f rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3f cross(Vec3f lhs, Vec3f rhs) {
    return {lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x};
}

float dot(Vec3f lhs, Vec3f rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Vec3f read_q12_vec3(std::span<const std::byte> data, std::size_t offset) {
    return {thandor::q12_to_float(thandor::read_i32(data, offset)),
            thandor::q12_to_float(thandor::read_i32(data, offset + 4U)),
            thandor::q12_to_float(thandor::read_i32(data, offset + 8U))};
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<RawTriangle> read_first_lod_raw_triangles(std::span<const std::byte> data) {
    constexpr std::size_t kHeaderSize = 0x200U;
    constexpr std::size_t kGroupHeaderSize = 0x20U;
    constexpr std::size_t kVertexStride = 0x40U;
    constexpr std::size_t kTriangleStride = 0x40U;
    constexpr std::array<std::size_t, 3> pointer_fields{0x00U, 0x0CU, 0x18U};
    constexpr std::array<std::size_t, 3> u_fields{0x04U, 0x10U, 0x1CU};
    constexpr std::array<std::size_t, 3> v_fields{0x08U, 0x14U, 0x20U};

    require(data.size() >= kHeaderSize + kGroupHeaderSize, "SPR too small for first LOD");
    require(thandor::read_u32(data, 0U) == thandor::fourcc("spr"), "SPR magic mismatch");
    const std::size_t group_offset = kHeaderSize;
    const uint32_t group_size = thandor::read_u32(data, group_offset);
    const uint32_t record_count = thandor::read_u32(data, group_offset + 4U);
    require(group_size >= kGroupHeaderSize && group_offset + group_size <= data.size(),
            "SPR first LOD group range invalid");

    std::vector<RawTriangle> result;
    std::size_t record_offset = group_offset + kGroupHeaderSize;
    for (uint32_t record = 0U; record < record_count; ++record) {
        const uint32_t record_size = thandor::read_u32(data, record_offset);
        const uint32_t vertex_count = thandor::read_u32(data, record_offset + 8U);
        const uint32_t triangle_count = thandor::read_u32(data, record_offset + 0x0CU);
        const std::size_t vertex_start = record_offset + kGroupHeaderSize;
        const std::size_t triangle_start = vertex_start + static_cast<std::size_t>(vertex_count) * kVertexStride;
        require(record_size >= kGroupHeaderSize && record_offset + record_size <= group_offset + group_size,
                "SPR mesh record range invalid");
        require(triangle_start + static_cast<std::size_t>(triangle_count) * kTriangleStride <=
                    record_offset + record_size,
                "SPR triangle stream exceeds mesh record");

        for (uint32_t triangle = 0U; triangle < triangle_count; ++triangle) {
            const std::size_t triangle_offset =
                triangle_start + static_cast<std::size_t>(triangle) * kTriangleStride;
            RawTriangle raw;
            raw.plane_normal = read_q12_vec3(data, triangle_offset + 0x24U);
            raw.texture_subresource = thandor::read_u32(data, triangle_offset + 0x30U);
            raw.render_flags = thandor::read_u32(data, triangle_offset + 0x34U);
            for (std::size_t corner = 0U; corner < 3U; ++corner) {
                const std::size_t vertex_offset =
                    static_cast<std::size_t>(thandor::read_u32(data, triangle_offset + pointer_fields[corner]));
                require(vertex_offset >= vertex_start && vertex_offset < triangle_start &&
                            (vertex_offset - vertex_start) % kVertexStride == 0U,
                        "SPR corner pointer does not select a first-LOD vertex");
                raw.corners[corner] = {
                    read_q12_vec3(data, vertex_offset),
                    thandor::q12_to_float(thandor::read_i32(data, triangle_offset + u_fields[corner])),
                    thandor::q12_to_float(thandor::read_i32(data, triangle_offset + v_fields[corner]))};
            }
            result.push_back(raw);
        }
        record_offset += record_size;
    }
    return result;
}

void require_corner(const SpriteMeshVertex& actual, const RawCorner& expected,
                    uint32_t texture_subresource, uint32_t render_flags) {
    require(near(actual.position.x, expected.position.x) &&
                near(actual.position.y, expected.position.y) &&
                near(actual.position.z, expected.position.z),
            "canonical winding changed an SPR corner position");
    require(near(actual.texture_u, expected.u) && near(actual.texture_v, expected.v),
            "canonical winding did not keep UVs attached to their SPR corner");
    require(actual.texture_subresource == texture_subresource && actual.render_flags == render_flags,
            "canonical winding changed SPR material metadata");
}
}

int main(int argc, char** argv) try {
    using namespace thandor;

    const std::filesystem::path source_root = THANDOR_SOURCE_DIR;
    const auto model_source = read_text(source_root / "src/core/model.cpp");
    const auto renderer_source = read_text(source_root / "src/editor/terrain_renderer.cpp");
    require(model_source.find("read_q12_vec3(data, offset + 0x24U)") != std::string::npos &&
                model_source.find("std::swap(corners[1], corners[2])") != std::string::npos,
            "SPR parser no longer canonicalizes corner order against the serialized plane normal");
    require(model_source.find("CULLMODE=NONE") != std::string::npos &&
                model_source.find("authoritative plane normal") != std::string::npos,
            "binary culling rationale disappeared from the SPR parser");
    require(renderer_source.find("VK_CULL_MODE_BACK_BIT") != std::string::npos,
            "placed renderer no longer uses canonical one-sided triangle winding");

    const char* game_root = argc >= 2 ? argv[1] : std::getenv("THANDOR_GAME_ROOT");
    if (game_root != nullptr && *game_root != '\0') {
        const auto catalog = AssetCatalog::scan_game_root(game_root);
        const auto raw_bytes = catalog.load("spr/gebaeude/grmi20.spr");
        const auto raw_triangles = read_first_lod_raw_triangles(raw_bytes);
        const auto sprite = SpriteAsset::parse(raw_bytes);
        const auto& lod = sprite.lod_groups().front();
        const auto& mesh = lod.mesh;

        require(raw_triangles.size() == 114U, "stock Xenite mine root triangle count changed");
        require(mesh.indices.size() == raw_triangles.size() * 3U &&
                    mesh.vertices.size() == raw_triangles.size() * 3U,
                "canonical winding changed Xenite mine triangle cardinality");

        std::size_t raw_opposed = 0U;
        std::size_t parsed_opposed = 0U;
        for (std::size_t triangle = 0U; triangle < raw_triangles.size(); ++triangle) {
            const auto& raw = raw_triangles[triangle];
            const Vec3f raw_geometric =
                cross(subtract(raw.corners[1].position, raw.corners[0].position),
                      subtract(raw.corners[2].position, raw.corners[0].position));
            const bool correction_required = dot(raw_geometric, raw.plane_normal) < 0.0F;
            raw_opposed += correction_required ? 1U : 0U;

            const std::array<std::size_t, 3> expected_order =
                correction_required ? std::array<std::size_t, 3>{0U, 2U, 1U}
                                    : std::array<std::size_t, 3>{0U, 1U, 2U};
            const std::size_t base = triangle * 3U;
            const auto& a = mesh.vertices[mesh.indices[base]];
            const auto& b = mesh.vertices[mesh.indices[base + 1U]];
            const auto& c = mesh.vertices[mesh.indices[base + 2U]];
            require_corner(a, raw.corners[expected_order[0]], raw.texture_subresource, raw.render_flags);
            require_corner(b, raw.corners[expected_order[1]], raw.texture_subresource, raw.render_flags);
            require_corner(c, raw.corners[expected_order[2]], raw.texture_subresource, raw.render_flags);
            const Vec3f parsed_geometric =
                cross(subtract(b.position, a.position), subtract(c.position, a.position));
            parsed_opposed += dot(parsed_geometric, raw.plane_normal) < -0.000001F ? 1U : 0U;
        }

        require(raw_opposed == 26U,
                "stock Xenite mine no longer contains the 26 reverse-wound serialized triangles");
        require(lod.winding_correction_count == 26U,
                "Xenite mine highest-detail reverse-wound triangle correction count changed");
        require(sprite.group_count() == 4U && sprite.winding_correction_count() == 92U,
                "Xenite mine complete LOD winding-correction census changed");
        require(parsed_opposed == 0U,
                "parsed Xenite mine still contains triangle winding opposed to its plane normals");
        std::cout << "G353_STOCK xenite_root_triangles=114 raw_reverse_winding=26 "
                     "canonical_reverse_winding=0 lod_groups=4 all_lod_corrections=92 PASS\n";
    }

    std::cout << "G353 SPR plane-normal winding regression PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G353 test failure: " << error.what() << '\n';
    return 1;
}
