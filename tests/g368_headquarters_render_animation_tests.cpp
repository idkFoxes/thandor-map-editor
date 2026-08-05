#include "model_transform.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read source file: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
float length(editor::Vec3f value) {
    return std::sqrt(value.x*value.x+value.y*value.y+value.z*value.z);
}
editor::Vec3f normalized(editor::Vec3f value) {
    const float l=length(value);
    return l>0.0F?editor::Vec3f{value.x/l,value.y/l,value.z/l}:editor::Vec3f{};
}
float dot(editor::Vec3f a,editor::Vec3f b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
}

int main() try {
    using namespace editor;
    const auto root=std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto app_h=read_text(root/"src/editor/editor_app.hpp");
    const auto app_c=read_text(root/"src/editor/editor_app.cpp");
    const auto renderer_h=read_text(root/"src/editor/terrain_renderer.hpp");
    const auto renderer_c=read_text(root/"src/editor/terrain_renderer.cpp");
    const auto transforms=read_text(root/"src/editor/model_transform.hpp");

    // EFF 131 is a single local-XY plane whose LOD flag requests the original full billboard
    // helper. Its local +Z normal must face the camera after the temporary transform.
    const Vec3f position{1.0F,2.0F,3.0F};
    const Vec3f camera{7.0F,-4.0F,10.0F};
    const auto billboard=sprite_lod_facing_transform(position,camera,0.0F,2U);
    const Vec3f world_normal=normalized(transform_vector(billboard,{0.0F,0.0F,1.0F}));
    const Vec3f toward=normalized({camera.x-position.x,camera.y-position.y,camera.z-position.z});
    assert(dot(world_normal,toward)>0.999F);

    // Cylindrical view-facing sprites keep world Z, while ordinary meshes keep placement yaw.
    const auto cylindrical=sprite_lod_facing_transform(position,camera,0.25F,1U);
    const auto cylindrical_z=transform_vector(cylindrical,{0.0F,0.0F,1.0F});
    assert(std::abs(cylindrical_z.x)<1.0e-5F&&std::abs(cylindrical_z.y)<1.0e-5F&&
           std::abs(cylindrical_z.z-1.0F)<1.0e-5F);
    const auto fixed=sprite_lod_facing_transform(position,camera,0.25F,0U);
    const auto fixed_z=transform_vector(fixed,{0.0F,0.0F,1.0F});
    assert(std::abs(fixed_z.z-1.0F)<1.0e-5F);

    assert(app_h.find("bool animate_buildings_{}")!=std::string::npos);
    assert(app_h.find("uint32_t animation_preview_multiplier_{1U}")!=std::string::npos);
    assert(app_h.find("bool owner_is_building{}")!=std::string::npos);
    assert(app_c.find("Exact game rate (1x)")!=std::string::npos);
    assert(app_c.find("16x")!=std::string::npos);
    assert(app_c.find("64x inspection")==std::string::npos);
    assert(app_c.find("256x inspection")==std::string::npos);
    assert(app_h.find("animation_preview_rate_user_selected_")==std::string::npos);
    assert(app_h.find("animation_preview_rate_auto_selected_for_buildings_")==std::string::npos);
    assert(app_c.find("current_building_animation_tick_")!=std::string::npos);
    assert(app_c.find("const uint32_t authored_animation_tick=is_building")!=std::string::npos);
    assert(app_c.find("?current_building_animation_tick_:current_presentation_tick_")!=std::string::npos);
    assert(app_c.find("model_runtime_child_yaw_step_turn16")!=std::string::npos);
    assert(app_c.find("animation_owner=hierarchy[node.parent_index]")!=std::string::npos);
    assert(app_c.find("!source.owner_is_building||animate_buildings_")!=std::string::npos);
    assert(app_c.find("sprite_lod_oriented_transform(")!=std::string::npos);
    // Fixed EFF sprites preserve all three authored owner angles; only explicit
    // SPR billboard/cylindrical flags may replace that basis. Validate behavior,
    // not a stale implementation comment or former helper token.
    assert(transforms.find("ModelHierarchyTransform result = authored_orientation;")!=std::string::npos);
    assert(transforms.find("result.translation = position;")!=std::string::npos);
    assert(app_c.find("order_dynamic_object_queue_like_game")!=std::string::npos);
    assert(app_c.find("update_depth_sorted_static_objects(frame->frame_index,camera_)")!=std::string::npos);
    assert(renderer_h.find("set_building_animation_tick")!=std::string::npos);
    assert(renderer_c.find("static_cast<float>(building_animation_tick_)")!=std::string::npos);
    assert(transforms.find("if ((lod_flags & 2U) != 0U)")!=std::string::npos);
    assert(transforms.find("billboard_model_transform")!=std::string::npos);

    // Do not fake volume by inventing crossed planes. The stock plane is billboarded and repeated.
    assert(app_c.find("volumetric_cross_plane") == std::string::npos);
    assert(app_c.find("invented_smoke_plane") == std::string::npos);

    std::cout << "G368/G369 Headquarters alpha queue, billboard smoke, owner-scoped effects and game-runtime MDL animation PASS\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr << "G368_HEADQUARTERS_RENDER_ANIMATION FAIL: " << error.what() << '\n';
    return 1;
}
