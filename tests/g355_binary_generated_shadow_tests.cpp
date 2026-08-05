#include "thandor/field.hpp"
#include "thandor/terrain_shadow.hpp"

#include <algorithm>
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
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(stream), {}};
}

std::vector<thandor::GeneratedShadowTriangle> rectangle(float x, float y, bool soft) {
    using thandor::GeneratedShadowTriangle;
    using thandor::Vec3f;
    const Vec3f a{x - 1.3F, y, 0.15F};
    const Vec3f b{x + 1.3F, y, 0.15F};
    const Vec3f c{x + 1.3F, y, 3.2F};
    const Vec3f d{x - 1.3F, y, 3.2F};
    return {{GeneratedShadowTriangle{{a, b, c}, soft},
             GeneratedShadowTriangle{{a, c, d}, soft}}};
}
}

int main() {
    try {
        using namespace thandor;
        FieldGrid field = FieldGrid::create(24U, 24U, 0, 0U);
        const auto& center_cell = field.at(12U, 12U);
        const float x = q12_to_float(center_cell.world_x_q12);
        const float y = q12_to_float(center_cell.world_y_q12);
        const Vec3f light_a{0.32F, 0.24F, -0.916F};
        const Vec3f light_b{-0.28F, 0.37F, -0.886F};

        const auto soft = build_game_generated_object_shadow(field, rectangle(x, y, true), light_a);
        check(soft.has_value(), "soft generated shadow was not built");
        check(soft->mask.width == 64U && soft->mask.height == 64U,
              "generated shadow did not use the binary 64x64 default");
        uint8_t soft_max = 0U;
        for (std::size_t index = 3U; index < soft->mask.pixels.size(); index += 4U)
            soft_max = std::max(soft_max, soft->mask.pixels[index]);
        check(soft_max > 0U && soft_max <= 224U,
              "soft group was not quantized and blurred through the recovered 32-weight path");
        check(!soft->receiver_indices.empty() && soft->receiver_indices.size() % 3U == 0U,
              "terrain-conforming receiver patch was not generated");
        for (const auto& vertex : soft->receiver_vertices) {
            const auto sample = field.sample_terrain(vertex.position.x, vertex.position.y);
            if (sample.inside)
                check(std::abs(vertex.position.z - (sample.height + 0.035F)) < 0.003F,
                      "receiver vertex does not conform to the FLD surface plus bias");
        }

        const auto hard = build_game_generated_object_shadow(field, rectangle(x, y, false), light_a);
        check(hard.has_value(), "hard generated shadow was not built");
        uint8_t hard_max = 0U;
        for (std::size_t index = 3U; index < hard->mask.pixels.size(); index += 4U)
            hard_max = std::max(hard_max, hard->mask.pixels[index]);
        check(hard_max == 255U, "hard group did not overwrite the blurred mask with 0xFF");

        const auto rotated = build_game_generated_object_shadow(field, rectangle(x, y, false), light_b);
        check(rotated.has_value(), "second light-direction shadow was not built");
        bool receiver_changed = false;
        const auto count = std::min(hard->receiver_vertices.size(), rotated->receiver_vertices.size());
        for (std::size_t index = 0; index < count; ++index) {
            const auto& a = hard->receiver_vertices[index].position;
            const auto& b = rotated->receiver_vertices[index].position;
            if (std::hypot(a.x - b.x, a.y - b.y) > 0.02F) {
                receiver_changed = true;
                break;
            }
        }
        check(receiver_changed, "generated receiver did not follow the light direction");

        const std::filesystem::path root = THANDOR_SOURCE_DIR;
        const auto editor = read_text(root / "src/editor/editor_app.cpp");
        const auto model = read_text(root / "src/core/model.cpp");
        const auto shader = read_text(root / "shaders/shadow.frag");
        check(editor.find("build_game_generated_object_shadow") != std::string::npos &&
              editor.find("shadow_projection_direction=document_->level().lighting().shadow_ray_direction()") != std::string::npos,
              "placed objects are not using the LEV light-derived generated-shadow direction");
        const auto active_shadow_block = editor.find("generated_shadow_triangles.push_back");
        check(active_shadow_block != std::string::npos &&
              editor.find("project_directional_shadow_to_field", active_shadow_block) == std::string::npos,
              "active placed-shadow path still projects each visible-mesh vertex");
        check(model.find("offset + 0x68U") != std::string::npos &&
              model.find("runtime_render_flags & 0x40U") != std::string::npos,
              "MDL generated-shadow suppression flag is not parsed");
        check(shader.find("generated white 64x64 silhouette") != std::string::npos &&
              shader.find("in_color.a") != std::string::npos,
              "shadow shader does not apply generated-mask alpha and half model alpha");
        check(editor.find("shadow_camera_dirty_") == std::string::npos &&
              editor.find("shadow_camera_forward") == std::string::npos,
              "camera-dependent shadow direction or invalidation survived the light-direction fix");

        std::cout << "G355/G357 binary-generated light-plane shadow tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "G355/G357 test failure: " << error.what() << '\n';
        return 1;
    }
}
