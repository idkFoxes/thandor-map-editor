#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read source file: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::string function_body(const std::string& source, const std::string& signature,
                          const std::string& next_signature) {
    const auto begin = source.find(signature);
    if (begin == std::string::npos) throw std::runtime_error("missing function: " + signature);
    const auto end = source.find(next_signature, begin + signature.size());
    if (end == std::string::npos) throw std::runtime_error("missing following function: " + next_signature);
    return source.substr(begin, end - begin);
}
}

int main() try {
    const std::filesystem::path root = THANDOR_SOURCE_DIR;
    const auto app_hpp = read_text(root / "src/editor/editor_app.hpp");
    const auto app_cpp = read_text(root / "src/editor/editor_app.cpp");
    const auto renderer_hpp = read_text(root / "src/editor/terrain_renderer.hpp");
    const auto renderer_cpp = read_text(root / "src/editor/terrain_renderer.cpp");
    const auto context_hpp = read_text(root / "src/editor/vulkan_context.hpp");
    const auto model_vert = read_text(root / "shaders/model.vert");
    const auto model_frag = read_text(root / "shaders/model.frag");
    const auto surface_vert = read_text(root / "shaders/surface.vert");

    require(app_hpp.find("bool animate_surface_{true}") != std::string::npos,
            "water/lava animation is not enabled by default");
    require(app_hpp.find("bool animate_buildings_{}") != std::string::npos,
            "building animation is not disabled by default");
    require(app_hpp.find("bool animate_timed_effects_{true}") != std::string::npos,
            "timed map effects are not enabled by default");
    require(app_cpp.find("Animate water/lava/ice") != std::string::npos &&
            app_cpp.find("Animate visible buildings") != std::string::npos,
            "view menu does not expose separate surface/building animation controls");

    const auto frame = function_body(app_cpp, "void EditorApp::frame()", "void EditorApp::draw_main_menu()");
    // A one-time static rebuild is allowed when simulation replacement membership changes.
    // It must remain explicitly guarded and separate from the 20 Hz presentation-tick path.
    const auto membership_guard=frame.find("if(simulation_static_replacement_dirty_)");
    const auto guarded_static_rebuild=frame.find("rebuild_objects()",membership_guard);
    require(membership_guard!=std::string::npos&&guarded_static_rebuild!=std::string::npos,
            "simulation replacement membership is not guarded by a one-time static rebuild");
    require(guarded_static_rebuild<frame.find("draw_main_menu()"),
            "simulation membership rebuild is not completed before UI/render submission");
    require(frame.find("rebuild_objects()",guarded_static_rebuild+1U)==std::string::npos,
            "presentation cadence contains an additional unguarded full static rebuild");
    require(frame.find("dynamic_animation_active") != std::string::npos &&
            frame.find("rebuild_dynamic_scene_cpu()") != std::string::npos,
            "frame loop does not use the small dynamic scene path");
    require(frame.find("auto frame=vulkan_.begin_frame()") < frame.find("upload_dynamic_scene(frame->frame_index)"),
            "dynamic upload occurs before the current frame fence is acquired");

    const auto dynamic_build = function_body(app_cpp, "void EditorApp::rebuild_dynamic_scene_cpu()",
                                             "void EditorApp::upload_dynamic_scene(");
    require(dynamic_build.find("placement_visible_in_canvas") != std::string::npos &&
            dynamic_build.find("visible_animated_buildings_") != std::string::npos &&
            dynamic_build.find("visible_timed_effect_controllers_") != std::string::npos,
            "building/effect CPU animation is not view scoped");
    require(dynamic_build.find("timings.push_back(source.timing)") <
            dynamic_build.find("build_resource_emitter_schedule(timings"),
            "global timed-effect RNG schedule is not built from every controller before view filtering");
    require(dynamic_build.find("std::thread") == std::string::npos &&
            dynamic_build.find("std::async") == std::string::npos,
            "dynamic builder unsafely moved mutable editor/Vulkan state to an unproven worker thread");

    require(context_hpp.find("static constexpr uint32_t kFramesInFlight = 2") != std::string::npos,
            "frame-fenced dynamic upload count changed");
    require(renderer_hpp.find("std::array<Buffer, VulkanContext::kFramesInFlight> dynamic_object_vertex_buffers_") != std::string::npos &&
            renderer_hpp.find("std::array<Buffer, VulkanContext::kFramesInFlight> resource_shading_buffers_") != std::string::npos &&
            renderer_hpp.find("std::array<VkDescriptorSet, VulkanContext::kFramesInFlight> descriptor_sets_") != std::string::npos,
            "dynamic geometry, shading, and descriptors are not independently frame buffered");

    const auto dynamic_upload = function_body(renderer_cpp, "void TerrainRenderer::update_dynamic_objects(",
                                              "void TerrainRenderer::update_resource_shading(");
    const auto shading_upload = function_body(renderer_cpp, "void TerrainRenderer::update_resource_shading(",
                                              "void TerrainRenderer::render(");
    require(dynamic_upload.find("context_.wait_idle()") == std::string::npos &&
            shading_upload.find("context_.wait_idle()") == std::string::npos,
            "presentation-cadence upload still stalls the entire Vulkan device");
    require(dynamic_upload.find("update_reusable_host_buffer") != std::string::npos &&
            shading_upload.find("update_reusable_host_buffer") != std::string::npos,
            "dynamic buffers are recreated instead of reused with geometric capacity growth");

    require(surface_vert.find("scene.animationControls.x") != std::string::npos,
            "surface animation is not a GPU-only tick selection");
    require(model_vert.find("scene.animation_controls.y") != std::string::npos &&
            model_frag.find("scene.animation_controls.z") != std::string::npos,
            "visible building animation controls are not wired through the model pipeline");
    require(app_cpp.find("Static terrain, water/lava surface, shadows, and non-animated objects remain full-map GPU buffers") != std::string::npos,
            "editor does not disclose full-map static submission versus view-scoped CPU animation");

    std::cout << "G366_VIEW_SCOPED_ANIMATION_PERFORMANCE PASS defaults=surface:on,buildings:off,effects:on "
                 "frame_buffers=2 full_static_rebuild_at_20hz=no device_idle_at_20hz=no\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G366 view-scoped animation performance checks failed: " << error.what() << '\n';
    return 1;
}
