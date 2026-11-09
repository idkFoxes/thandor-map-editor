#include "thandor/binary.hpp"
#include "thandor/field.hpp"
#include "thandor/level.hpp"
#include "thandor/terrain_shadow.hpp"

#include <algorithm>
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
    if (offset + 4U > data.size()) throw std::runtime_error("synthetic LEV write overflow");
    data[offset + 0U] = static_cast<std::byte>(value & 0xFFU);
    data[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    data[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xFFU);
    data[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xFFU);
}

std::vector<std::byte> synthetic_level(uint32_t packed_light_angles) {
    constexpr std::size_t size = 0x400U;
    std::vector<std::byte> data(size, std::byte{0});
    put_u32(data, 0x00U, thandor::fourcc("lev"));
    put_u32(data, 0xD8U, 0U);
    put_u32(data, 0xDCU, static_cast<uint32_t>(size));
    constexpr std::array<std::size_t, 10> fields{
        0xB0U, 0xB4U, 0xB8U, 0xBCU, 0xC0U, 0xC4U, 0xC8U, 0xCCU, 0xD0U, 0xD4U};
    for (std::size_t index = 0; index < fields.size(); ++index)
        put_u32(data, fields[index], 0x320U + static_cast<uint32_t>(index * 8U));
    put_u32(data, 0x2E0U, packed_light_angles);
    put_u32(data, 0x2F0U, 0x005A004BU); // V537 field-region height/width sentinel.
    return data;
}

std::vector<thandor::GeneratedShadowTriangle> rectangle(float x, float y) {
    using thandor::GeneratedShadowTriangle;
    using thandor::Vec3f;
    const Vec3f a{x - 1.2F, y, 0.15F};
    const Vec3f b{x + 1.2F, y, 0.15F};
    const Vec3f c{x + 1.2F, y, 3.0F};
    const Vec3f d{x - 1.2F, y, 3.0F};
    return {{GeneratedShadowTriangle{{a, b, c}, false},
             GeneratedShadowTriangle{{a, c, d}, false}}};
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(stream), {}};
}

bool near(float actual, float expected, float tolerance = 0.0002F) {
    return std::abs(actual - expected) <= tolerance;
}
}

int main() {
    try {
        using namespace thandor;
        constexpr uint16_t azimuth = 0x2000U;   // +45 degrees
        constexpr uint16_t elevation = 0xE000U; // -45 degrees as turn16
        constexpr uint32_t packed = static_cast<uint32_t>(azimuth) |
                                    (static_cast<uint32_t>(elevation) << 16U);
        auto level = LevelAsset::parse(synthetic_level(packed));
        check(level.lighting().azimuth == azimuth,
              "LEV +0x2E0 low word was not decoded as light azimuth");
        check(level.lighting().elevation == elevation,
              "LEV +0x2E0 high word was not decoded as light elevation");
        const auto roundtrip = level.serialize();
        check(read_u32(roundtrip, 0x2E0U) == packed,
              "LEV light-angle roundtrip changed the low-azimuth/high-elevation order");
        check(read_u32(roundtrip, 0x2F0U) == 0x005A004BU,
              "V537 field-region dimensions at LEV +0x2F0 were overwritten by light angles");
        check(level.lighting().field_region_width == 0x004BU &&
              level.lighting().field_region_height == 0x005AU,
              "LEV +0x2F0 was not decoded as V537 field-region width/height");

        const Vec3f direction = level.lighting().shadow_ray_direction();
        check(near(direction.x, 0.5F) && near(direction.y, 0.5F) &&
              near(direction.z, -0.70710678F),
              "TerrainLighting direction does not match FixedMath elevation/azimuth ordering");

        FieldGrid field = FieldGrid::create(24U, 24U, 0, 0U);
        const auto& cell = field.at(12U, 12U);
        const float x = q12_to_float(cell.world_x_q12);
        const float y = q12_to_float(cell.world_y_q12);
        const auto first = build_game_generated_object_shadow(field, rectangle(x, y), direction);
        check(first.has_value(), "LEV light-direction shadow was not generated");
        check(near(first->projection_direction.x, direction.x) &&
              near(first->projection_direction.y, direction.y) &&
              near(first->projection_direction.z, direction.z),
              "generated shadow did not preserve the normalized LEV projection direction");

        auto second_lighting = level.lighting();
        second_lighting.azimuth = 0x6000U; // same elevation, opposite horizontal quadrant
        const auto second = build_game_generated_object_shadow(
            field, rectangle(x, y), second_lighting.shadow_ray_direction());
        check(second.has_value(), "second LEV light-direction shadow was not generated");
        bool moved = false;
        const auto count = std::min(first->receiver_vertices.size(), second->receiver_vertices.size());
        for (std::size_t index = 0; index < count; ++index) {
            const auto& a = first->receiver_vertices[index].position;
            const auto& b = second->receiver_vertices[index].position;
            if (std::hypot(a.x - b.x, a.y - b.y) > 0.02F) {
                moved = true;
                break;
            }
        }
        check(moved, "changing LEV light azimuth did not rotate the generated receiver");

        const std::filesystem::path root = THANDOR_SOURCE_DIR;
        const auto level_cpp = read_text(root / "src/core/level.cpp");
        const auto editor_cpp = read_text(root / "src/editor/editor_app.cpp");
        check(level_cpp.find("read_u32(fixed_image_, 0x2E0U)") != std::string::npos &&
              level_cpp.find("lighting_.azimuth = static_cast<uint16_t>(packed_direction & 0xFFFFU)") !=
                  std::string::npos &&
              level_cpp.find("lighting_.elevation = static_cast<uint16_t>(packed_direction >> 16U)") !=
                  std::string::npos &&
              level_cpp.find("read_u32(fixed_image_, 0x2F0U)") != std::string::npos,
              "LEV parser source no longer proves the V537 angle/dimension split");
        check(editor_cpp.find("shadow_projection_direction=level_lighting.shadow_ray_direction()") !=
                  std::string::npos &&
              editor_cpp.find("shadow_camera_forward") == std::string::npos &&
              editor_cpp.find("shadow_camera_dirty_") == std::string::npos,
              "editor shadow source still depends on camera pose");
        std::cout << "G357 LEV light-angle and generated-shadow direction tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "G357 test failure: " << error.what() << '\n';
        return 1;
    }
}
