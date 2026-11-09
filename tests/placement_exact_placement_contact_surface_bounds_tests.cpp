#include "thandor/binary.hpp"
#include "thandor/field.hpp"
#include "thandor/model.hpp"
#include "model_transform.hpp"

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

void require_near(float actual, float expected, const char* message, float tolerance = 1.0e-4F) {
    if (std::abs(actual - expected) > tolerance) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::vector<std::byte> placement_header_sprite_fixture() {
    std::vector<std::byte> data(0x220U, std::byte{});
    thandor::write_u32(data, 0x00U, thandor::fourcc("spr"));
    thandor::write_u32(data, 0x04U, static_cast<uint32_t>(data.size()));
    thandor::write_u32(data, 0x0CU, 0x00020007U);
    thandor::write_u32(data, 0xB0U, 1U);
    thandor::write_u32(data, 0xB8U, 0x8844U);
    thandor::write_i32(data, 0xC0U, thandor::float_to_q12(-1.0F));
    thandor::write_i32(data, 0xC4U, thandor::float_to_q12(-2.0F));
    thandor::write_i32(data, 0xC8U, thandor::float_to_q12(-3.0F));
    thandor::write_i32(data, 0xCCU, thandor::float_to_q12(4.0F));
    thandor::write_i32(data, 0xD0U, thandor::float_to_q12(5.0F));
    thandor::write_i32(data, 0xD4U, thandor::float_to_q12(6.0F));
    thandor::write_i32(data, 0xD8U, thandor::float_to_q12(7.0F));
    thandor::write_i32(data, 0xDCU, thandor::float_to_q12(1.25F));
    thandor::write_u32(data, 0x200U, 0x20U);
    thandor::write_u32(data, 0x204U, 0U);
    return data;
}

thandor::Vec3f transform_axis(const editor::ModelHierarchyTransform& transform,
                              thandor::Vec3f axis) {
    return editor::transform_vector(transform, axis);
}
} // namespace

int main() try {
    const auto sprite = thandor::SpriteAsset::parse(placement_header_sprite_fixture());
    require(sprite.registry_id() == 0x8844U, "SPR registry ID changed");
    require(sprite.has_valid_header_bounds(), "SPR +0xC0..+0xD4 bounds were not accepted");
    require_near(sprite.local_bounds_min().x, -1.0F, "SPR local minimum X was not parsed");
    require_near(sprite.local_bounds_min().z, -3.0F, "SPR local minimum Z was not parsed");
    require_near(sprite.local_bounds_max().y, 5.0F, "SPR local maximum Y was not parsed");
    require_near(sprite.bounding_radius(), 7.0F, "SPR +0xD8 radius was not parsed");
    require_near(sprite.placement_height_offset(), 1.25F,
                 "SPR +0xDC placement height offset was not parsed");

    auto field = thandor::FieldGrid::create(4U, 4U, 0, 0U);
    auto& c00 = field.at(1U, 1U);
    auto& c10 = field.at(2U, 1U);
    auto& c01 = field.at(1U, 2U);
    auto& c11 = field.at(2U, 2U);
    c00.terrain_height_q12 = thandor::float_to_q12(1.0F);
    c10.terrain_height_q12 = thandor::float_to_q12(3.0F);
    c01.terrain_height_q12 = thandor::float_to_q12(5.0F);
    c11.terrain_height_q12 = thandor::float_to_q12(7.0F);
    c00.water_delta_q12 = thandor::float_to_q12(-4.0F);
    c10.water_delta_q12 = thandor::float_to_q12(-2.0F);
    c01.water_delta_q12 = thandor::float_to_q12(0.0F);
    c11.water_delta_q12 = thandor::float_to_q12(2.0F);
    const float x00 = thandor::q12_to_float(c00.world_x_q12);
    const float y00 = thandor::q12_to_float(c00.world_y_q12);
    const float x10 = thandor::q12_to_float(c10.world_x_q12);
    const float y10 = thandor::q12_to_float(c10.world_y_q12);
    const float x01 = thandor::q12_to_float(c01.world_x_q12);
    const float y01 = thandor::q12_to_float(c01.world_y_q12);
    const float sample_x = x00 + 0.25F * (x10 - x00) + 0.25F * (x01 - x00);
    const float sample_y = y00 + 0.25F * (y10 - y00) + 0.25F * (y01 - y00);
    const auto terrain = field.sample_terrain(sample_x, sample_y);
    require_near(terrain.height, 2.5F, "terrain triangle interpolation changed");
    require_near(field.sample_water_surface_height(sample_x, sample_y), 0.0F,
                 "water-surface interpolation is not terrain+water delta");
    require_near(field.sample_top_surface_height(sample_x, sample_y), 2.5F,
                 "top-surface interpolation did not clamp a negative water delta");

    const auto kind0 = editor::resolve_placement_contact_pose(
        0, terrain, 0.0F, 2.5F, sprite.placement_height_offset());
    const auto kind1 = editor::resolve_placement_contact_pose(
        1, terrain, 0.0F, 2.5F, sprite.placement_height_offset());
    const auto kind2 = editor::resolve_placement_contact_pose(
        2, terrain, 0.0F, 2.5F, sprite.placement_height_offset());
    const auto kind4 = editor::resolve_placement_contact_pose(
        4, terrain, 0.0F, 2.5F, sprite.placement_height_offset());
    require_near(kind0.height, 3.75F, "kind 0 omitted SPR +0xDC");
    require_near(kind1.height, 0.0F, "kind 1 incorrectly added SPR +0xDC");
    require_near(kind2.height, 3.75F, "kind 2 omitted SPR +0xDC");
    require(kind2.align_to_normal, "kind 2 did not request terrain-normal alignment");
    require_near(kind4.height, 2.5F, "kind 4 did not use the top surface");

    const thandor::Vec3f normal{0.2F, -0.3F, 0.9327379F};
    const auto contact_transform = editor::terrain_contact_root_transform(
        {2.0F, 3.0F, 4.0F}, 0.37F, normal);
    const auto local_z = transform_axis(contact_transform, {0.0F, 0.0F, 1.0F});
    const float normal_length = std::sqrt(normal.x * normal.x + normal.y * normal.y +
                                          normal.z * normal.z);
    require_near(local_z.x, normal.x / normal_length,
                 "terrain-contact transform local Z does not follow the terrain normal");
    require_near(local_z.y, normal.y / normal_length,
                 "terrain-contact transform local Z Y component is wrong");
    require_near(local_z.z, normal.z / normal_length,
                 "terrain-contact transform local Z Z component is wrong");
    const auto flat = editor::terrain_contact_root_transform(
        {2.0F, 3.0F, 4.0F}, 0.37F, {0.0F, 0.0F, 1.0F});
    const auto yaw_only = editor::placement_root_transform(2.0F, 3.0F, 4.0F, 0.37F);
    for (std::size_t index = 0; index < flat.basis.size(); ++index)
        require_near(flat.basis[index], yaw_only.basis[index],
                     "flat terrain-contact transform does not preserve placement yaw");

    const std::filesystem::path root = THANDOR_SOURCE_DIR;
    const auto editor_source = read_text(root / "src/editor/editor_app.cpp");
    const auto model_source = read_text(root / "src/core/model.cpp");
    const auto field_source = read_text(root / "src/core/field.cpp");
    require(model_source.find("0xC0U") != std::string::npos &&
            model_source.find("0xDCU") != std::string::npos,
            "SPR placement metadata offsets are absent from the parser");
    require(field_source.find("sample_water_surface_height") != std::string::npos &&
            field_source.find("sample_top_surface_height") != std::string::npos,
            "placement surface interpolation is absent from FieldGrid");
    require(editor_source.find("placement_contact_pose") != std::string::npos &&
            editor_source.find("terrain_contact_root_transform") != std::string::npos &&
            editor_source.find("has_valid_header_bounds") != std::string::npos,
            "editor roots, recursive bounds, or simulation pose do not use the recovered metadata");

    std::cout << "G384_EXACT_PLACEMENT_CONTACT_SURFACE_BOUNDS PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G384 checks failed: " << error.what() << '\n';
    return 1;
}
