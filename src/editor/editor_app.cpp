#include "editor_app.hpp"
#include "model_transform.hpp"
#include "articulated_walker_pose.hpp"
#include "thandor/terrain_shadow.hpp"
#include "thandor/simulation_render_policy.hpp"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cctype>
#include <chrono>
#include <functional>
#include <cstdio>
#include <cstring>
#include <map>
#include <limits>
#include <numbers>
#include <unordered_set>
#include <utility>

namespace editor {
namespace {
constexpr float kLeftPanelWidth = 390.0F;
constexpr float kRightPanelWidth = 360.0F;
constexpr float kStatusBarHeight = 24.0F;
constexpr float kAssetCardWidth = 174.0F;
constexpr float kAssetCardHeight = 196.0F;
constexpr const char* kAssetDragPayloadType = "THANDOR_ARM_PRESET";

struct AssetDragPayload { uint32_t registry_id{}; };

constexpr float kTurn16ToRadians = 2.0F * std::numbers::pi_v<float> / 65536.0F;
constexpr float kRadiansToTurn16 = 65536.0F / (2.0F * std::numbers::pi_v<float>);

uint16_t radians_to_turn16(float radians) noexcept {
    const long long turns = std::llround(static_cast<double>(radians) * kRadiansToTurn16);
    return static_cast<uint16_t>(turns & 0xFFFFLL);
}

int16_t signed_radians_to_turn16(float radians) noexcept {
    const float clamped = std::clamp(radians, -0.25F * 2.0F * std::numbers::pi_v<float>,
                                    0.25F * 2.0F * std::numbers::pi_v<float>);
    const long long turns = std::llround(static_cast<double>(clamped) * kRadiansToTurn16);
    return static_cast<int16_t>(std::clamp<long long>(turns, -0x4000LL, 0x4000LL));
}

float turn16_to_radians(uint16_t value) noexcept {
    return static_cast<float>(value) * kTurn16ToRadians;
}

float signed_turn16_to_radians(int16_t value) noexcept {
    return static_cast<float>(value) * kTurn16ToRadians;
}

ImU32 argb_to_im_u32(uint32_t value) {
    return IM_COL32((value >> 16U) & 0xFFU, (value >> 8U) & 0xFFU, value & 0xFFU,
                    (value >> 24U) & 0xFFU);
}

uint32_t sample_wrapped_argb(const thandor::RgbaImage& image, float u, float v) {
    if (image.width == 0U || image.height == 0U ||
        image.pixels.size() != static_cast<std::size_t>(image.width) * image.height * 4U) {
        return 0xFFFFFFFFU;
    }
    const auto wrap = [](float coordinate) {
        coordinate -= std::floor(coordinate);
        return coordinate < 0.0F ? coordinate + 1.0F : coordinate;
    };
    const uint32_t x = std::min(image.width - 1U,
        static_cast<uint32_t>(wrap(u) * static_cast<float>(image.width)));
    const uint32_t y = std::min(image.height - 1U,
        static_cast<uint32_t>(wrap(v) * static_cast<float>(image.height)));
    const std::size_t offset = (static_cast<std::size_t>(y) * image.width + x) * 4U;
    return static_cast<uint32_t>(image.pixels[offset + 3U]) << 24U |
           static_cast<uint32_t>(image.pixels[offset + 0U]) << 16U |
           static_cast<uint32_t>(image.pixels[offset + 1U]) << 8U |
           static_cast<uint32_t>(image.pixels[offset + 2U]);
}


uint32_t sample_wrapped_bilinear_argb(const thandor::RgbaImage& image, float u, float v) {
    if (image.width == 0U || image.height == 0U ||
        image.pixels.size() != static_cast<std::size_t>(image.width) * image.height * 4U)
        return 0xFFFFFFFFU;
    const auto wrap = [](float coordinate) {
        coordinate -= std::floor(coordinate);
        return coordinate < 0.0F ? coordinate + 1.0F : coordinate;
    };
    const float x = wrap(u) * static_cast<float>(image.width) - 0.5F;
    const float y = wrap(v) * static_cast<float>(image.height) - 0.5F;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const auto pixel = [&](int px, int py) {
        const uint32_t ix = static_cast<uint32_t>((px % static_cast<int>(image.width) +
            static_cast<int>(image.width)) % static_cast<int>(image.width));
        const uint32_t iy = static_cast<uint32_t>((py % static_cast<int>(image.height) +
            static_cast<int>(image.height)) % static_cast<int>(image.height));
        const std::size_t offset = (static_cast<std::size_t>(iy) * image.width + ix) * 4U;
        return std::array<float,4>{static_cast<float>(image.pixels[offset + 3U]),
            static_cast<float>(image.pixels[offset + 0U]),
            static_cast<float>(image.pixels[offset + 1U]),
            static_cast<float>(image.pixels[offset + 2U])};
    };
    const auto p00 = pixel(x0, y0); const auto p10 = pixel(x0 + 1, y0);
    const auto p01 = pixel(x0, y0 + 1); const auto p11 = pixel(x0 + 1, y0 + 1);
    std::array<uint32_t,4> out{};
    for (std::size_t channel = 0; channel < out.size(); ++channel) {
        const float top = p00[channel] + (p10[channel] - p00[channel]) * tx;
        const float bottom = p01[channel] + (p11[channel] - p01[channel]) * tx;
        out[channel] = static_cast<uint32_t>(std::clamp(std::lround(
            top + (bottom - top) * ty), 0L, 255L));
    }
    return out[0] << 24U | out[1] << 16U | out[2] << 8U | out[3];
}

uint32_t average_argb(std::span<const uint32_t> colors, bool prefer_bright) {
    if (colors.empty()) return 0xFFFFFFFFU;
    uint64_t alpha = 0U, red = 0U, green = 0U, blue = 0U;
    uint32_t accepted = 0U;
    uint32_t brightest = 0xFF000000U;
    uint32_t brightest_luminance = 0U;
    for (const uint32_t color : colors) {
        const uint32_t a = (color >> 24U) & 0xFFU;
        const uint32_t r = (color >> 16U) & 0xFFU;
        const uint32_t g = (color >> 8U) & 0xFFU;
        const uint32_t b = color & 0xFFU;
        const uint32_t luminance = r * 3U + g * 6U + b;
        if (prefer_bright && luminance > brightest_luminance) {
            brightest_luminance = luminance;
            brightest = color;
        }
        if (a == 0U || (prefer_bright && luminance < 24U)) continue;
        alpha += a; red += r; green += g; blue += b; ++accepted;
    }
    if (prefer_bright && brightest_luminance > 80U) return brightest | 0xC0000000U;
    if (accepted == 0U) return 0x00000000U;
    return static_cast<uint32_t>(alpha / accepted) << 24U |
           static_cast<uint32_t>(red / accepted) << 16U |
           static_cast<uint32_t>(green / accepted) << 8U |
           static_cast<uint32_t>(blue / accepted);
}

uint32_t sample_preview_patch_argb(const thandor::RgbaImage& image,
                                   const std::array<PreviewVec2,3>& uv,
                                   bool prefer_bright) {
    const PreviewVec2 center{(uv[0].x+uv[1].x+uv[2].x)/3.0F,
                             (uv[0].y+uv[1].y+uv[2].y)/3.0F};
    const std::array<uint32_t,4> samples{{
        sample_wrapped_argb(image,uv[0].x,uv[0].y),
        sample_wrapped_argb(image,uv[1].x,uv[1].y),
        sample_wrapped_argb(image,uv[2].x,uv[2].y),
        sample_wrapped_argb(image,center.x,center.y),
    }};
    return average_argb(samples,prefer_bright);
}

float animated_child2_translation_q12(const thandor::ArmyAssetPreset& preset,uint32_t tick,
                                      float initial_translation) noexcept {
    if(preset.child2_vertical_step_q12==0||
       preset.child2_vertical_max_q12<=preset.child2_vertical_min_q12) return initial_translation;
    const int64_t minimum=preset.child2_vertical_min_q12;
    const int64_t maximum=preset.child2_vertical_max_q12;
    const int64_t span=maximum-minimum;
    const int64_t step=std::max<int64_t>(1,std::llabs(static_cast<long long>(preset.child2_vertical_step_q12)));
    const int64_t period_steps=std::max<int64_t>(2,(span+step-1)/step);
    const int64_t phase=static_cast<int64_t>(tick)%(period_steps*2);
    const int64_t distance=phase<=period_steps?phase:period_steps*2-phase;
    const int64_t value=std::min<int64_t>(maximum,minimum+distance*step);
    return thandor::q12_to_float(static_cast<int32_t>(value));
}

struct ModelHierarchyInstance {
    const thandor::ModelVisualNode* node{};
    const thandor::SpriteAsset* sprite{};
    ModelHierarchyTransform transform{};
    bool root{};
};


template<class TerrainHeightFn>
bool apply_walker_pose_to_instances(const thandor::ArmyAssetPreset& preset,
                                    std::span<const thandor::ModelVisualNode> hierarchy,
                                    std::vector<ModelHierarchyInstance>& instances,
                                    thandor::SimulationActorState* actor,
                                    TerrainHeightFn&& terrain_height) {
    if(preset.runtime_model_class_id!=3U||instances.size()!=hierarchy.size())return false;
    std::vector<ModelHierarchyTransform> transforms;
    transforms.reserve(instances.size());
    for(const auto& instance:instances)transforms.push_back(instance.transform);
    const auto result=apply_articulated_walker_support_pose(hierarchy,transforms,
        preset.articulated_root_height_q12,preset.articulated_contact_lift_q12,actor,
        std::forward<TerrainHeightFn>(terrain_height),[&](uint32_t node_index){
            return node_index<instances.size()&&instances[node_index].sprite!=nullptr
                ?instances[node_index].sprite->placement_height_offset():0.0F;
        });
    if(!result.applied)return false;
    for(std::size_t index=0;index<instances.size();++index)
        instances[index].transform=transforms[index];
    return true;
}

const char* tool_name(Tool tool){switch(tool){case Tool::Select:return "Select";case Tool::Raise:return "Raise";case Tool::Lower:return "Lower";case Tool::Smooth:return "Smooth";case Tool::Flatten:return "Flatten";case Tool::Water:return "Water";case Tool::ResourceField:return "Resource field";case Tool::Texture:return "Texture";case Tool::Resource:return "Resource";case Tool::PlayerStart:return "Player start";case Tool::EffectScenery:return "Effect / scenery";case Tool::SimulationTarget:return "Simulation target";case Tool::SimulationMove:return "Drive selected unit";}return "Tool";}
ImVec4 argb_to_imgui(uint32_t value){auto c=thandor::unpack_argb(value);return {c.r,c.g,c.b,c.a};}
uint32_t imgui_to_argb(ImVec4 c){return thandor::pack_argb({c.x,c.y,c.z,c.w});}
bool contains_ci(std::string_view text,std::string_view filter){if(filter.empty())return true;auto lower=[](char c){return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));};for(std::size_t i=0;i+filter.size()<=text.size();++i){bool same=true;for(std::size_t j=0;j<filter.size();++j)if(lower(text[i+j])!=lower(filter[j])){same=false;break;}if(same)return true;}return false;}
std::string terrain_base_path(std::string_view source){
    std::string normalized=thandor::normalize_asset_path(source);
    const auto slash=normalized.find_last_of('/');
    const auto dot=normalized.find_last_of('.');
    if(dot!=std::string::npos&&(slash==std::string::npos||dot>slash))normalized.resize(dot);
    return normalized;
}
std::string terrain_material_path(std::string_view base,uint32_t material){
    if(material>=26)throw std::out_of_range("terrain material index");
    return std::string(base)+static_cast<char>('a'+material)+".gfx";
}
std::string army_texture_family_path(std::string_view base,uint32_t faction,std::string_view extension){
    std::string normalized=terrain_base_path(base);
    if(normalized.empty())throw std::runtime_error("LEV army texture path is empty");
    return normalized+std::to_string(std::min(faction,8U))+std::string(extension);
}

bool draw_tool_icon_button(Tool tool,bool selected,ImVec2 size={74.0F,58.0F}){
    ImGui::PushID(static_cast<int>(tool));
    const ImVec2 origin=ImGui::GetCursorScreenPos();
    const bool pressed=ImGui::InvisibleButton("##tool-icon",size);
    const bool hovered=ImGui::IsItemHovered();
    auto* draw=ImGui::GetWindowDrawList();
    const ImU32 background=selected?IM_COL32(54,105,165,255):
        (hovered?IM_COL32(64,68,76,255):IM_COL32(43,46,53,255));
    draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},background,6.0F);
    draw->AddRect(origin,{origin.x+size.x,origin.y+size.y},
                  selected?IM_COL32(110,190,255,255):IM_COL32(88,92,102,255),6.0F,0,1.5F);
    const ImVec2 c{origin.x+size.x*0.5F,origin.y+22.0F};
    const ImU32 ink=IM_COL32(235,240,246,255);
    const ImU32 accent=IM_COL32(95,200,255,255);
    switch(tool){
        case Tool::Select:
            draw->AddTriangleFilled({c.x-8,c.y-11},{c.x-6,c.y+10},{c.x+10,c.y+3},ink);
            draw->AddLine({c.x,c.y+2},{c.x+9,c.y+12},ink,2.5F); break;
        case Tool::Raise:
            draw->AddTriangleFilled({c.x,c.y-12},{c.x-12,c.y+8},{c.x+12,c.y+8},accent);
            draw->AddLine({c.x,c.y+11},{c.x,c.y-9},ink,2.0F); break;
        case Tool::Lower:
            draw->AddTriangleFilled({c.x,c.y+12},{c.x-12,c.y-8},{c.x+12,c.y-8},accent);
            draw->AddLine({c.x,c.y-11},{c.x,c.y+9},ink,2.0F); break;
        case Tool::Smooth:
            draw->AddBezierCubic({c.x-13,c.y+5},{c.x-6,c.y-10},{c.x+5,c.y+12},{c.x+13,c.y-4},ink,2.5F);
            draw->AddLine({c.x-13,c.y+11},{c.x+13,c.y+11},accent,2.0F); break;
        case Tool::Flatten:
            draw->AddLine({c.x-13,c.y+8},{c.x+13,c.y+8},accent,3.0F);
            draw->AddLine({c.x-9,c.y-8},{c.x+9,c.y-8},ink,2.0F);
            draw->AddLine({c.x,c.y-8},{c.x,c.y+5},ink,2.0F); break;
        case Tool::Water:
            draw->AddCircleFilled({c.x,c.y-5},7.0F,accent,20);
            draw->AddTriangleFilled({c.x-7,c.y-5},{c.x+7,c.y-5},{c.x,c.y-15},accent);
            draw->AddBezierCubic({c.x-14,c.y+8},{c.x-7,c.y+2},{c.x+6,c.y+14},{c.x+14,c.y+7},ink,2.0F); break;
        case Tool::ResourceField:
            draw->AddCircle({c.x-7,c.y},7.0F,IM_COL32(255,190,50,255),16,2.0F);
            draw->AddCircle({c.x+7,c.y},7.0F,accent,16,2.0F);
            draw->AddLine({c.x-14,c.y+10},{c.x+14,c.y+10},ink,2.0F); break;
        case Tool::Texture:
            draw->AddRectFilled({c.x-12,c.y-12},{c.x+12,c.y+12},ink,2.0F);
            draw->AddRectFilled({c.x-10,c.y-10},{c.x,c.y},IM_COL32(210,120,75,255));
            draw->AddRectFilled({c.x,c.y-10},{c.x+10,c.y},IM_COL32(80,155,80,255));
            draw->AddRectFilled({c.x-10,c.y},{c.x,c.y+10},IM_COL32(75,110,185,255));
            draw->AddRectFilled({c.x,c.y},{c.x+10,c.y+10},IM_COL32(205,190,105,255)); break;
        default: break;
    }
    const char* label=tool_name(tool);
    const ImVec2 text_size=ImGui::CalcTextSize(label);
    draw->AddText({origin.x+(size.x-text_size.x)*0.5F,origin.y+size.y-text_size.y-5.0F},ink,label);
    if(hovered)ImGui::SetTooltip("%s",label);
    ImGui::PopID();
    return pressed;
}

struct DeferredObjectPacket {
    std::vector<uint32_t> indices;
    PrimitiveBlendPreset blend{PrimitiveBlendPreset::OpaqueDepthWrite};
    bool two_sided{};
    float view_depth{};
    std::size_t source_order{};
};

void order_dynamic_object_queue_like_game(
    std::vector<ObjectVertex>& vertices, std::vector<uint32_t>& indices,
    std::vector<ObjectDrawRun>& runs, const Camera& camera) {
    if(indices.empty()||runs.empty())return;
    std::vector<uint32_t> ordered_indices;
    ordered_indices.reserve(indices.size());
    std::vector<ObjectDrawRun> ordered_runs;
    std::vector<DeferredObjectPacket> deferred;
    std::vector<DeferredObjectPacket> late_fog;
    deferred.reserve(indices.size()/3U);
    const Vec3f eye=camera.eye();
    const Vec3f forward=normalize(sub(camera.target,eye));
    const auto append_packet=[&](PrimitiveBlendPreset blend,bool two_sided,
                                 uint32_t sort_group,std::span<const uint32_t> packet_indices){
        const uint32_t first=static_cast<uint32_t>(ordered_indices.size());
        ordered_indices.insert(ordered_indices.end(),packet_indices.begin(),packet_indices.end());
        if(!ordered_runs.empty()){
            auto& previous=ordered_runs.back();
            if(previous.blend==blend&&previous.two_sided==two_sided&&
               previous.sort_group==sort_group&&
               previous.first_index+previous.index_count==first){
                previous.index_count+=static_cast<uint32_t>(packet_indices.size());return;
            }
        }
        ordered_runs.push_back(ObjectDrawRun{blend,two_sided,first,
            static_cast<uint32_t>(packet_indices.size()),sort_group});
    };
    std::size_t source_order=0U;
    for(const auto& run:runs){
        if(run.index_count==0U||run.index_count%3U!=0U||
           run.first_index>indices.size()||run.index_count>indices.size()-run.first_index)
            throw std::runtime_error("dynamic object draw run is not triangle aligned");
        if(run.blend==PrimitiveBlendPreset::FogOverlayNoDepthTest){
            DeferredObjectPacket packet;
            packet.blend=run.blend;
            packet.two_sided=run.two_sided;
            packet.source_order=source_order;
            packet.indices.assign(indices.begin()+run.first_index,
                                  indices.begin()+run.first_index+run.index_count);
            late_fog.push_back(std::move(packet));
            source_order+=run.index_count/3U;
            continue;
        }
        if(run.blend==PrimitiveBlendPreset::OpaqueDepthWrite){
            append_packet(run.blend,run.two_sided,run.sort_group,
                std::span<const uint32_t>(indices).subspan(run.first_index,run.index_count));
            source_order+=run.index_count/3U;
            continue;
        }
        if(run.sort_group!=0U){
            DeferredObjectPacket packet;
            packet.blend=run.blend;
            packet.two_sided=run.two_sided;
            packet.source_order=source_order;
            packet.indices.assign(indices.begin()+run.first_index,
                                  indices.begin()+run.first_index+run.index_count);
            Vec3f centroid{};
            for(const uint32_t index:packet.indices){
                if(index>=vertices.size())throw std::runtime_error("dynamic object queue index exceeds vertex stream");
                centroid=add(centroid,vertices[index].position);
            }
            centroid=mul(centroid,1.0F/static_cast<float>(packet.indices.size()));
            packet.view_depth=dot(sub(centroid,eye),forward);
            deferred.push_back(std::move(packet));
            source_order+=run.index_count/3U;
            continue;
        }
        for(uint32_t offset=0U;offset<run.index_count;offset+=3U,++source_order){
            DeferredObjectPacket packet;
            packet.indices={indices[run.first_index+offset],indices[run.first_index+offset+1U],
                            indices[run.first_index+offset+2U]};
            packet.blend=run.blend;
            packet.two_sided=run.two_sided;
            packet.source_order=source_order;
            Vec3f centroid{};
            for(const uint32_t index:packet.indices){
                if(index>=vertices.size())throw std::runtime_error("dynamic object queue index exceeds vertex stream");
                centroid=add(centroid,vertices[index].position);
            }
            centroid=mul(centroid,1.0F/3.0F);
            packet.view_depth=dot(sub(centroid,eye),forward);
            deferred.push_back(std::move(packet));
        }
    }
    // GraphicsPrimitiveQueue_RadixSortForRendering uses the sum of projected depths and traverses
    // the final buckets high-to-low. One authored sprite billboard is one queue packet; its two
    // triangles must never be interleaved with a different smoke card or a diagonal seam appears.
    std::stable_sort(deferred.begin(),deferred.end(),[](const auto& left,const auto& right){
        if(left.view_depth!=right.view_depth)return left.view_depth>right.view_depth;
        return left.source_order<right.source_order;
    });
    for(const auto& packet:deferred)
        append_packet(packet.blend,packet.two_sided,0U,packet.indices);
    // Simulated fog is a screen-visible cover, not one of the retail object/effect packets. Keep
    // it after the full depth-sorted dynamic queue so steep terrain and animated lava cannot
    // overwrite it, and do not let its large triangles disturb smoke-card sorting.
    for(const auto& packet:late_fog)
        append_packet(packet.blend,packet.two_sided,0U,packet.indices);
    indices=std::move(ordered_indices);
    runs=std::move(ordered_runs);
}
}

EditorApp::EditorApp(GLFWwindow* window,VulkanContext& vulkan,std::filesystem::path game_root):window_(window),vulkan_(vulkan),catalog_(thandor::AssetCatalog::scan_game_root(game_root)),renderer_(vulkan){
    startup_validation_=catalog_.validate_supported_assets();
    if(!startup_validation_.ok()){
        const auto& issue=startup_validation_.issues.front();
        throw std::runtime_error("asset validation failed for "+issue.path+": "+issue.message);
    }
    game_cursor_=GameCursorAsset::parse(catalog_.load("engine/mouse.gfx"),
                                        catalog_.load("engine/mouse.dat"));
    if(catalog_.map_groups().empty())throw std::runtime_error("no paired LEV/FLD maps were found in the game PCK archives");
    load_scenario_catalog_labels();
    template_group_=0;load_map(0);
    status_="Validated "+std::to_string(startup_validation_.decoded_entries)+" non-movie PCK entries; loaded "+catalog_.map_groups()[0].display_name;
}

void EditorApp::run(){while(glfwWindowShouldClose(window_)==GLFW_FALSE){glfwPollEvents();frame();}vulkan_.wait_idle();}
void EditorApp::frame(){
    ImGui_ImplVulkan_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();
    game_cursor_frame_=GameCursorAsset::kDefaultFrame;
    constexpr double kSurfacePresentationTicksPerSecond=20.0;
    const double presentation_time=ImGui::GetTime();
    if(!presentation_timeline_initialized_){
        presentation_timeline_initialized_=true;
        last_presentation_time_seconds_=presentation_time;
        current_presentation_tick_=0U;
    }else{
        const double elapsed=std::clamp(presentation_time-last_presentation_time_seconds_,0.0,0.25);
        last_presentation_time_seconds_=presentation_time;
        presentation_tick_fraction_+=elapsed*kSurfacePresentationTicksPerSecond*
            static_cast<double>(animation_preview_multiplier_);
        const auto elapsed_ticks=static_cast<uint32_t>(std::floor(presentation_tick_fraction_));
        presentation_tick_fraction_-=static_cast<double>(elapsed_ticks);
        current_presentation_tick_+=elapsed_ticks;
    }
    renderer_.set_presentation_tick(current_presentation_tick_);
    current_building_animation_tick_=current_presentation_tick_;
    renderer_.set_building_animation_tick(current_building_animation_tick_);
    update_simulation(current_presentation_tick_);
    if(simulation_static_replacement_dirty_){
        simulation_static_replacement_dirty_=false;
        rebuild_objects();
    }
    draw_main_menu();draw_left_panel();draw_right_panel();draw_status_bar();draw_new_map_dialog();draw_load_map_dialog();draw_save_map_dialog();update_canvas_interaction();draw_technology_placement_popup();
    draw_water_selection_overlay();draw_placement_markers();draw_transform_gizmo();draw_drag_drop_preview();draw_game_cursor();ImGui::Render();
    const bool enabled_timed_effect_animation=animate_timed_effects_&&
        std::any_of(timed_effect_controllers_.begin(),timed_effect_controllers_.end(),
            [&](const TimedEffectControllerPreview& controller){
                return !controller.owner_is_building||animate_buildings_;
            });
    const bool dynamic_animation_active =
        (animate_buildings_ && !animated_building_placements_.empty()) ||
        (animate_units_ && !animated_unit_placements_.empty()) ||
        !continuous_radar_placements_.empty() ||
        (simulation_mode_ && (!simulation_actor_placements_.empty() || !simulation_projectiles_.empty())) ||
        fog_preview_mode_!=0 || enabled_timed_effect_animation;
    const bool dynamic_geometry_present = !dynamic_scene_cpu_.indices.empty() ||
        !dynamic_scene_cpu_.shading_emitters.empty();
    // A UI order can create the first transient simulation actor after the frame-start static
    // preload pass. Do not enter the dynamic renderer until the next frame has rebuilt the static
    // texture/layer inventory for that actor; this removes the hover-unit first-drive crash.
    if(!simulation_static_replacement_dirty_ &&
       current_presentation_tick_!=last_dynamic_scene_tick_ &&
       (dynamic_animation_active || dynamic_geometry_present)){
        last_dynamic_scene_tick_=current_presentation_tick_;
        rebuild_dynamic_scene_cpu();
    }
    auto frame=vulkan_.begin_frame();if(!frame)return;
    upload_dynamic_scene(frame->frame_index);
    renderer_.update_depth_sorted_static_objects(frame->frame_index,camera_);
    renderer_.render(frame->command_buffer,camera_,draw_water_,wireframe_,frame->frame_index,
                     animate_surface_,animate_buildings_||fog_preview_mode_!=0,
                     animate_units_||fog_preview_mode_!=0);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),frame->command_buffer);vulkan_.end_frame(*frame);
}

void EditorApp::draw_main_menu(){
    if(!ImGui::BeginMainMenuBar())return;
    if(ImGui::BeginMenu("File")){
        if(ImGui::MenuItem("New map","Ctrl+N"))show_new_dialog_=true;
        if(ImGui::BeginMenu("Open stock map")){
            for(std::size_t i=0;i<catalog_.map_groups().size();++i)
                if(ImGui::MenuItem(catalog_.map_groups()[i].display_name.c_str(),nullptr,i==current_group_))load_map(i);
            ImGui::EndMenu();
        }
        if(ImGui::MenuItem("Load map or project...","Ctrl+O")){
            show_load_dialog_=true;
            if(load_map_path_[0]=='\0')std::snprintf(load_map_path_,sizeof(load_map_path_),"%s",
                (catalog_.game_root()/"ThandorMapEditor"/"maps").string().c_str());
        }
        if(ImGui::MenuItem("Save full map project","Ctrl+S")){
            if(current_project_path_.empty())show_save_dialog_=true;
            else save_project(current_project_path_);
        }
        if(ImGui::MenuItem("Save full map project as..."))show_save_dialog_=true;
        ImGui::Separator();
        if(ImGui::MenuItem("Export loose complete map")){
            try{document_->export_loose(catalog_.game_root()/"ThandorMapEditor"/"loose");status_="Exported LEV, FLD, and custom terrain assets.";}
            catch(const std::exception& e){status_=e.what();}
        }
        if(ImGui::MenuItem("Install/repair game level package","Ctrl+E"))export_game_level_package();
        if(ImGui::MenuItem("Update PATCH00.PCK overrides"))update_patch00_overrides();
        if(ImGui::MenuItem("Quit"))glfwSetWindowShouldClose(window_,GLFW_TRUE);
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Edit")){
        if(ImGui::MenuItem("Undo","Ctrl+Z",false,!undo_.empty()))undo();
        if(ImGui::MenuItem("Redo","Ctrl+Y",false,!redo_.empty()))redo();
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("View")){
        ImGui::MenuItem("Show water/lava/ice surface",nullptr,&draw_water_);
        ImGui::MenuItem("Animate water/lava/ice",nullptr,&animate_surface_);
        if(ImGui::MenuItem("Animate visible buildings",nullptr,&animate_buildings_)){
            if(!animate_buildings_&&!simulation_actor_states_.empty())
                simulation_static_replacement_dirty_=true;
            last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
        }
        if(ImGui::MenuItem("Animate visible units",nullptr,&animate_units_)){
            if(!animate_units_&&!simulation_actor_states_.empty())
                simulation_static_replacement_dirty_=true;
            last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
        }
        if(ImGui::BeginMenu("Animation / simulation preview speed")){
            constexpr std::array<std::pair<const char*,uint32_t>,5> rates{{
                {"Exact game rate (1x)",1U},{"2x",2U},{"4x",4U},{"8x",8U},{"16x",16U}}};
            for(const auto& [label,multiplier]:rates){
                if(ImGui::MenuItem(label,nullptr,animation_preview_multiplier_==multiplier)){
                    animation_preview_multiplier_=multiplier;
                    last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
                }
            }
            ImGui::EndMenu();
        }
        if(ImGui::MenuItem("Animate visible timed map effects",nullptr,&animate_timed_effects_))
            last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
        ImGui::Separator();
        const std::string simulation_label="Simulation mode (20 Hz × "+
            std::to_string(animation_preview_multiplier_)+")";
        if(ImGui::MenuItem(simulation_label.c_str(),nullptr,&simulation_mode_)){
            if(simulation_mode_)last_simulation_tick_=current_presentation_tick_;
            else reset_simulation(true);
            last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
        }
        ImGui::MenuItem("Show simulation ranges",nullptr,&simulation_show_ranges_);
        if(ImGui::BeginMenu("Fog of war preview")){
            bool fog_changed=false;
            if(ImGui::MenuItem("Off",nullptr,fog_preview_mode_==0)){
                fog_preview_mode_=0;fog_changed=true;
            }
            if(ImGui::MenuItem("Selected faction",nullptr,fog_preview_mode_==1)){
                fog_preview_mode_=1;fog_changed=true;
            }
            if(ImGui::MenuItem("All active factions",nullptr,fog_preview_mode_==2)){
                fog_preview_mode_=2;fog_changed=true;
            }
            if(ImGui::BeginMenu("Faction")){
                for(int faction=1;faction<=7;++faction){
                    const std::string label="Faction "+std::to_string(faction);
                    if(ImGui::MenuItem(label.c_str(),nullptr,fog_preview_faction_==faction)){
                        fog_preview_faction_=faction;fog_preview_mode_=1;fog_changed=true;
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::SetNextItemWidth(150.0F);
            fog_changed|=ImGui::SliderFloat("Radius scale",&fog_preview_radius_scale_,0.25F,3.0F,"%.2fx");
            ImGui::TextDisabled("Editor-only current radial visibility");
            if(fog_changed){
                last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
                rebuild_objects();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        ImGui::MenuItem("Wireframe",nullptr,&wireframe_);
        ImGui::MenuItem("Game cursor",nullptr,&game_cursor_enabled_);
        if(ImGui::MenuItem("Center camera")){
            center_camera();
        }
        ImGui::EndMenu();
    }
    ImGui::TextDisabled("%s — %s%s",document_->display_name().c_str(),document_->map_name().c_str(),document_->dirty()?" *":"");
    ImGui::EndMainMenuBar();
}

void EditorApp::draw_left_panel(){
    const auto io=ImGui::GetIO();
    ImGui::SetNextWindowPos({0,ImGui::GetFrameHeight()});
    ImGui::SetNextWindowSize({kLeftPanelWidth,io.DisplaySize.y-ImGui::GetFrameHeight()-kStatusBarHeight});
    ImGui::Begin("Assets",nullptr,ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
    char filter[128]{};
    std::snprintf(filter,sizeof(filter),"%s",asset_filter_.c_str());
    if(ImGui::InputTextWithHint("##filter","Search assets and camera starts",filter,sizeof(filter)))asset_filter_=filter;
    ImGui::TextDisabled("Only game-ready ARM records are placeable. Raw/internal records remain in Files.");

    const char* scopes[]={"Current map","All placeable assets"};
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::Combo("##asset-scope",&asset_browser_scope_,scopes,2);
    asset_browser_scope_=std::clamp(asset_browser_scope_,0,1);

    std::vector<uint32_t> placement_ids;
    if(asset_browser_scope_==0){
        std::vector<const char*> groups;
        groups.reserve(catalog_.map_groups().size());
        for(const auto& group:catalog_.map_groups())groups.push_back(group.display_name.c_str());
        asset_browser_group_=std::clamp(asset_browser_group_,0,
            std::max(0,static_cast<int>(groups.size())-1));
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::Combo("##asset-map-group",&asset_browser_group_,groups.data(),static_cast<int>(groups.size()));
        const auto ids=catalog_.map_groups()[static_cast<std::size_t>(asset_browser_group_)].placement_registry_ids;
        placement_ids.assign(ids.begin(),ids.end());
    }else{
        placement_ids.reserve(catalog_.army_presets().size());
        for(const auto& preset:catalog_.army_presets())placement_ids.push_back(preset.registry_id);
    }

    if(ImGui::BeginTabBar("asset-tabs")){
        if(ImGui::BeginTabItem("General")){
            draw_faction_camera_start_cards();
            draw_simulation_target_card();
            ImGui::SeparatorText("Placeable map records");
            draw_placement_presets(placement_ids,thandor::PlacementBrowserKind::General);
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Elements")){
            draw_placement_presets(placement_ids,thandor::PlacementBrowserKind::Element);
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Units")){
            draw_global_faction_selector("unit-browser-faction");
            draw_placement_presets(placement_ids,thandor::PlacementBrowserKind::Unit);
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Buildings")){
            draw_global_faction_selector("building-browser-faction");
            draw_placement_presets(placement_ids,thandor::PlacementBrowserKind::Building);
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Files")){
            std::vector<const char*> groups;
            groups.reserve(catalog_.map_groups().size()+1U);
            for(const auto& group:catalog_.map_groups())groups.push_back(group.display_name.c_str());
            groups.push_back("General PCK assets");
            asset_file_group_=std::clamp(asset_file_group_,0,static_cast<int>(groups.size())-1);
            ImGui::SetNextItemWidth(-1.0F);
            ImGui::Combo("##file-map-group",&asset_file_group_,groups.data(),static_cast<int>(groups.size()));
            ImGui::BeginChild("asset-file-scroll",{0,0},false,ImGuiWindowFlags_AlwaysVerticalScrollbar);
            if(asset_file_group_==static_cast<int>(catalog_.map_groups().size())){
                draw_asset_group("general",catalog_.general_assets());
            }else{
                draw_asset_group("map-assets",catalog_.map_groups()[static_cast<std::size_t>(asset_file_group_)].asset_indices);
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void EditorApp::draw_asset_group(const char*,std::span<const std::size_t> indices){
    for(const auto index:indices){
        const auto& asset=catalog_.assets()[index];
        if(!contains_ci(asset.normalized_path,asset_filter_))continue;
        ImGui::PushID(static_cast<int>(index));
        if(ImGui::Selectable((std::string(thandor::asset_kind_name(asset.kind))+"  "+asset.normalized_path).c_str())){
            if(asset.kind==thandor::AssetKind::Texture){
                auto it=std::find(material_names_.begin(),material_names_.end(),asset.normalized_path);
                if(it!=material_names_.end())selected_material_=static_cast<uint8_t>(std::distance(material_names_.begin(),it));
                status_="Selected texture: "+asset.normalized_path;
            }else status_="Selected asset source: "+asset.normalized_path;
        }
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Archive: %s\nUnpacked: %u bytes",asset.archive_name.c_str(),asset.unpacked_size);
        ImGui::PopID();
    }
}


void EditorApp::draw_global_faction_selector(const char* widget_id){
    int faction=static_cast<int>(selected_faction_);
    const char* faction_names[]={"Neutral / map-owned","Faction 1 — Blue","Faction 2 — Red",
        "Faction 3 — Green","Faction 4 — Yellow","Faction 5 — Orange",
        "Faction 6 — Brown","Faction 7 — Purple"};
    ImGui::PushID(widget_id);
    ImGui::SetNextItemWidth(-1.0F);
    if(ImGui::Combo("Placement faction",&faction,faction_names,8)){
        selected_faction_=static_cast<uint32_t>(std::clamp(faction,0,7));
        asset_preview_cache_.clear();
        asset_preview_texture_cache_.clear();
        rebuild_objects();
    }
    ImGui::TextDisabled("Preview and new placements: army%u.gfx / army%u.pal (%s)",
        thandor::default_faction_graphics_family(selected_faction_),
        thandor::default_faction_graphics_family(selected_faction_),
        thandor::default_faction_color_name(selected_faction_));
    ImGui::PopID();
}


void EditorApp::draw_faction_camera_start_cards(){
    if(!document_)return;
    const uint32_t active=std::clamp(document_->level().frontend_active_faction_count(),1U,
        thandor::LevelAsset::kFactionCameraStartCount);
    const uint32_t assignable=std::min(document_->level().frontend_assignable_faction_count(),active);
    std::vector<uint32_t> visible_factions;
    visible_factions.reserve(active);
    for(uint32_t faction=1U;faction<=active;++faction){
        const bool human=faction<=assignable;
        const std::string searchable=std::string(human?"player ":"ai ")+std::to_string(faction)+
            " faction starting camera lev view start";
        if(asset_filter_.empty()||contains_ci(searchable,asset_filter_))visible_factions.push_back(faction);
    }
    if(visible_factions.empty())return;

    ImGui::SeparatorText("Player / AI starting cameras");
    ImGui::TextDisabled("LEV camera records, not placeable ARM objects. Frame the viewport, then save that view.");
    constexpr float card_width=210.0F;
    constexpr float card_height=184.0F;
    const std::array<ImU32,8> faction_colors{
        IM_COL32(115,125,140,255),IM_COL32(70,125,255,255),IM_COL32(225,70,70,255),
        IM_COL32(70,190,95,255),IM_COL32(235,205,65,255),IM_COL32(235,135,55,255),
        IM_COL32(145,100,70,255),IM_COL32(165,95,210,255)};
    ImGui::BeginChild("camera-start-card-row",{0.0F,card_height+ImGui::GetStyle().ScrollbarSize+8.0F},
        false,ImGuiWindowFlags_HorizontalScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    bool first=true;
    for(const uint32_t faction:visible_factions){
        if(!first)ImGui::SameLine();
        first=false;
        ImGui::PushID(static_cast<int>(faction));
        ImGui::BeginChild("camera-start-card",{card_width,card_height},true,
            ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoNavInputs);
        const bool human=faction<=assignable;
        const bool selected=selected_camera_faction_==static_cast<int>(faction);
        const ImVec2 card_min=ImGui::GetWindowPos();
        const ImVec2 card_max{card_min.x+card_width,card_min.y+card_height};
        auto* draw=ImGui::GetWindowDrawList();
        if(selected)draw->AddRect(card_min,card_max,IM_COL32(255,196,64,255),4.0F,0,2.0F);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(faction_colors[faction]),
            "%s %u starting camera",human?"Player":"AI",faction);
        try{
            const auto camera_start=document_->level().faction_camera_start(faction);
            ImGui::TextDisabled("LEV +0x%03X",0x200U+(faction-1U)*0x20U);
            const ImVec2 preview_size{card_width-18.0F,46.0F};
            ImGui::InvisibleButton("camera-preview",preview_size);
            const ImVec2 preview_min=ImGui::GetItemRectMin();
            const ImVec2 preview_max=ImGui::GetItemRectMax();
            const ImVec2 center{(preview_min.x+preview_max.x)*0.5F,(preview_min.y+preview_max.y)*0.5F};
            draw->AddRectFilled(preview_min,preview_max,IM_COL32(28,33,39,255),3.0F);
            draw->AddCircleFilled({center.x-24.0F,center.y},8.0F,faction_colors[faction],16);
            draw->AddLine({center.x-14.0F,center.y},{center.x+28.0F,center.y},faction_colors[faction],3.0F);
            draw->AddTriangleFilled({center.x+34.0F,center.y},{center.x+20.0F,center.y-8.0F},
                {center.x+20.0F,center.y+8.0F},faction_colors[faction]);
            if(ImGui::IsItemClicked(ImGuiMouseButton_Left))selected_camera_faction_=static_cast<int>(faction);
            ImGui::Text("%.1f, %.1f, %.1f",camera_start.position_x(),camera_start.position_y(),
                camera_start.position_z());
            ImGui::TextDisabled("heading %.1f deg · pitch %.1f deg",
                static_cast<float>(camera_start.heading_turn16)*(360.0F/65536.0F),
                static_cast<float>(camera_start.pitch_turn16)*(360.0F/65536.0F));
            if(ImGui::Button("View stored",{(card_width-24.0F)*0.5F,0.0F})){
                selected_camera_faction_=static_cast<int>(faction);
                focus_faction_camera_start(faction);
            }
            ImGui::SameLine();
            if(ImGui::Button("Aim at start",{-1.0F,0.0F})){
                selected_camera_faction_=static_cast<int>(faction);
                aim_camera_at_faction_start_vehicle(faction);
            }
            if(ImGui::Button("Set from current view",{-1.0F,0.0F})){
                selected_camera_faction_=static_cast<int>(faction);
                capture_faction_camera_start(faction);
            }
        }catch(const std::exception& error){
            ImGui::TextWrapped("Camera record unavailable: %s",error.what());
        }
        ImGui::EndChild();
        ImGui::PopID();
    }
    ImGui::EndChild();
}


void EditorApp::draw_simulation_target_card(){
    if(!document_)return;
    const std::string searchable="simulation target test spot weapon shot firing range tower unit";
    if(!asset_filter_.empty()&&!contains_ci(searchable,asset_filter_))return;
    ImGui::SeparatorText("Weapon simulation target");
    ImGui::BeginChild("simulation-target-card",{0.0F,154.0F},true,
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextUnformatted("Target spot (editor-only)");
    ImGui::TextWrapped("Place a global test target here, or select an actor and use the normal viewport command: right-click terrain to move/aim, or right-click a different-faction placement to attack it. Right-drag remains camera pan.");
    if(simulation_target_point_){
        ImGui::Text("World: %.2f, %.2f, %.2f",simulation_target_point_->x,
            simulation_target_point_->y,simulation_target_point_->z);
    }else ImGui::TextDisabled("No target spot placed.");
    if(ImGui::Button("Place / move target",{155.0F,0.0F})){
        tool_=Tool::SimulationTarget;simulation_mode_=true;
        status_="Click terrain to place the simulation target spot.";
    }
    ImGui::SameLine();
    if(ImGui::Button("Clear all targets",{-1.0F,0.0F})){
        simulation_target_point_.reset();
        simulation_attack_placement_targets_.clear();
        simulation_attack_point_targets_.clear();
        simulation_projectiles_.clear();
        last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
    }
    ImGui::Checkbox("Simulation mode (20 Hz)",&simulation_mode_);
    ImGui::EndChild();
}

void EditorApp::draw_placement_presets(std::span<const uint32_t> registry_ids,
                                       thandor::PlacementBrowserKind browser_kind){
    std::vector<const thandor::ArmyAssetPreset*> visible;
    visible.reserve(registry_ids.size());
    std::unordered_set<uint32_t> emitted_canonical_ids;
    for(const auto registry_id:registry_ids){
        const auto* preset=catalog_.army_preset(registry_id);
        if(preset==nullptr||preset->browser_kind!=browser_kind||!preset->browser_visible)continue;
        const uint32_t canonical_id=thandor::canonical_placement_registry_id(*preset);
        if(!emitted_canonical_ids.insert(canonical_id).second)continue;
        const std::string role=thandor::placement_visual_kind_name(preset->visual_kind);
        const std::string type=thandor::placement_browser_kind_name(preset->browser_kind);
        if(!contains_ci(preset->display_name,asset_filter_)&&!contains_ci(preset->short_name,asset_filter_)&&
           !contains_ci(preset->source_path,asset_filter_)&&!contains_ci(preset->sprite_path,asset_filter_)&&
           !contains_ci(preset->group_hint,asset_filter_)&&!contains_ci(preset->loadout_name,asset_filter_)&&
           !contains_ci(preset->technology_summary,asset_filter_)&&
           !contains_ci(preset->asset_search_text,asset_filter_)&&!contains_ci(role,asset_filter_)&&
           !contains_ci(type,asset_filter_)&&
           !contains_ci(std::to_string(preset->registry_id),asset_filter_))continue;
        visible.push_back(preset);
    }

    ImGui::BeginChild("placement-card-scroll",{0,0},false,ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if(visible.empty()){
        ImGui::TextDisabled("No placeable assets match the filter.");
        ImGui::EndChild();
        return;
    }
    const float spacing=ImGui::GetStyle().ItemSpacing.x;
    const float available=ImGui::GetContentRegionAvail().x;
    const int columns=asset_card_column_count(available,kAssetCardWidth,spacing);
    const float card_width=std::min(kAssetCardWidth,
        (available-spacing*static_cast<float>(columns-1))/static_cast<float>(columns));
    const int rows=static_cast<int>((visible.size()+static_cast<std::size_t>(columns)-1U)/
                                    static_cast<std::size_t>(columns));
    ImGuiListClipper clipper;
    clipper.Begin(rows,kAssetCardHeight+ImGui::GetStyle().ItemSpacing.y);
    while(clipper.Step()){
        for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row){
            for(int column=0;column<columns;++column){
                const std::size_t index=static_cast<std::size_t>(row*columns+column);
                if(index>=visible.size())break;
                if(column>0)ImGui::SameLine();
                const bool available = document_ != nullptr &&
                    catalog_.placement_available_in_level(*visible[index], document_->level());
                draw_placement_card(*visible[index],card_width,kAssetCardHeight,available);
            }
        }
    }
    ImGui::EndChild();
}

void EditorApp::draw_placement_card(const thandor::ArmyAssetPreset& preset,float card_width,
                                    float card_height,bool available_in_current_level){
    ImGui::PushID(static_cast<int>(preset.registry_id));
    const bool selected=selected_asset_id_==preset.registry_id;
    ImGui::BeginChild("asset-card",{card_width,card_height},true,
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoNavInputs);
    const ImVec2 child_min=ImGui::GetWindowPos();
    const ImVec2 child_max{child_min.x+card_width,child_min.y+card_height};
    auto* draw=ImGui::GetWindowDrawList();
    if(selected)draw->AddRect(child_min,child_max,IM_COL32(255,196,64,255),4.0F,0,2.0F);

    const float preview_height=132.0F;
    ImGui::InvisibleButton("asset-preview",{card_width-12.0F,preview_height});
    const ImVec2 preview_min=ImGui::GetItemRectMin();
    const ImVec2 preview_max=ImGui::GetItemRectMax();
    const bool preview_hovered=ImGui::IsItemHovered();
    if(available_in_current_level&&ImGui::IsItemClicked(ImGuiMouseButton_Left)){
        selected_asset_id_=preset.registry_id;
        tool_=preset.visual_kind==thandor::PlacementVisualKind::PlayerStart?Tool::PlayerStart:
            (thandor::placement_is_resource(preset)?Tool::Resource:Tool::EffectScenery);
        status_="Selected "+preset.display_name+" for placement.";
    }

    draw->AddRectFilled(preview_min,preview_max,IM_COL32(28,33,39,255),3.0F);
    draw->AddCircleFilled({(preview_min.x+preview_max.x)*0.5F,preview_max.y-13.0F},
        std::max(16.0F,(preview_max.x-preview_min.x)*0.18F),IM_COL32(0,0,0,72),32);
    try{
        const auto& mesh=asset_preview(preset);
        if(!mesh.raster_argb.empty()&&mesh.raster_width>0U&&mesh.raster_height>0U){
            const float scale_x=(preview_max.x-preview_min.x)/static_cast<float>(mesh.raster_width);
            const float scale_y=(preview_max.y-preview_min.y-6.0F)/static_cast<float>(mesh.raster_height);
            const auto quantize=[](uint32_t color){
                const uint32_t a=(color>>24U)&0xFFU;
                if(a==0U)return 0U;
                return (a&0xF8U)<<24U|((color>>16U)&0xF8U)<<16U|
                    ((color>>8U)&0xF8U)<<8U|(color&0xF8U);
            };
            for(uint32_t y=0U;y<mesh.raster_height;++y){
                uint32_t x=0U;
                while(x<mesh.raster_width){
                    const uint32_t color=quantize(mesh.raster_argb[
                        static_cast<std::size_t>(y)*mesh.raster_width+x]);
                    uint32_t end=x+1U;
                    while(end<mesh.raster_width&&quantize(mesh.raster_argb[
                        static_cast<std::size_t>(y)*mesh.raster_width+end])==color)++end;
                    if(((color>>24U)&0xFFU)!=0U){
                        draw->AddRectFilled({preview_min.x+static_cast<float>(x)*scale_x,
                                             preview_min.y+static_cast<float>(y)*scale_y},
                                            {preview_min.x+static_cast<float>(end)*scale_x+0.35F,
                                             preview_min.y+static_cast<float>(y+1U)*scale_y+0.35F},
                                            argb_to_im_u32(color));
                    }
                    x=end;
                }
            }
        }
        if(mesh.marker_only||mesh.covered_pixel_count==0U){
            const ImVec2 center{(preview_min.x+preview_max.x)*0.5F,(preview_min.y+preview_max.y)*0.5F};
            draw->AddCircleFilled(center,28.0F,IM_COL32(88,112,140,220),24);
            draw->AddText({center.x-18.0F,center.y-7.0F},IM_COL32_WHITE,"ARM");
        }
    }catch(const std::exception&){
        const ImVec2 center{(preview_min.x+preview_max.x)*0.5F,(preview_min.y+preview_max.y)*0.5F};
        draw->AddCircle(center,26.0F,IM_COL32(220,100,82,255),24,2.0F);
        draw->AddText({center.x-4.0F,center.y-8.0F},IM_COL32(255,180,160,255),"!");
    }

    if(available_in_current_level&&ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)){
        const AssetDragPayload payload{preset.registry_id};
        ImGui::SetDragDropPayload(kAssetDragPayloadType,&payload,sizeof(payload));
        ImGui::TextUnformatted(preset.display_name.c_str());
        ImGui::TextDisabled("Drop on the 3D map to place");
        ImGui::EndDragDropSource();
    }

    if(!available_in_current_level){
        draw->AddRectFilled(preview_min,preview_max,IM_COL32(40,16,16,118),3.0F);
        draw->AddText({preview_min.x+8.0F,preview_max.y-24.0F},IM_COL32(255,155,145,255),
            "Not loaded by this LEV");
    }

    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+card_width-14.0F);
    ImGui::TextUnformatted(preset.display_name.c_str());
    ImGui::PopTextWrapPos();
    ImGui::TextDisabled("ID %u · %s",preset.registry_id,
        thandor::placement_browser_kind_name(preset.browser_kind));
    if(!preset.technology_summary.empty())
        ImGui::TextDisabled("%s",preset.technology_summary.c_str());
    else ImGui::TextDisabled("%s",preset.group_hint.c_str());
    if(preview_hovered){
        std::string compatible_ids;
        for(const uint32_t id:preset.compatible_registry_ids){
            if(!compatible_ids.empty())compatible_ids+=", ";
            compatible_ids+=std::to_string(id);
        }
        std::string shared_sprite_effect_ids;
        for(const uint32_t id:preset.shared_sprite_effect_ids){
            if(!shared_sprite_effect_ids.empty())shared_sprite_effect_ids+=", ";
            shared_sprite_effect_ids+=std::to_string(id);
        }
        const std::string localized_source=!preset.localized_name.empty()
            ?(preset.localized_name_source_archive+":"+preset.localized_name_source_path)
            :(preset.name_text_resource_id!=0U
                ?(preset.localized_name_source_archive+":"+preset.localized_name_source_path+
                  " (empty/unresolved; research/loadout fallback)")
                :"SPR filename fallback");
        const uint32_t localized_index=preset.has_model_name_text_offset
            ?thandor::army_asset_name_text_index(preset.model_name_text_offset):0U;
        const std::string level_diagnostic=available_in_current_level
            ?"dependency chain loaded"
            :catalog_.placement_compatibility_diagnostic(preset,document_->level());

        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+650.0F);
        ImGui::Text("%s · ARM %u",thandor::placement_browser_kind_name(preset.browser_kind),
            preset.registry_id);
        ImGui::TextColored(ImVec4(0.86F,0.93F,0.96F,1.0F),"%s",preset.display_name.c_str());
        ImGui::Text("Localized PCK name: %s",preset.localized_name.empty()?"unavailable":preset.localized_name.c_str());
        ImGui::TextDisabled("Name source: %s · resource 0x%08X (page 0x%02X, index %u, MDL +0x04=%u)",
            localized_source.c_str(),preset.name_text_resource_id,thandor::kArmyAssetNameTextPage,
            localized_index,preset.model_name_text_offset);
        ImGui::Text("ARM: %s",preset.source_path.c_str());
        ImGui::Text("Base MDL %u: %s",preset.model_definition_id,preset.model_source_path.c_str());
        ImGui::Text("SPR: %s",preset.sprite_path.empty()?"marker only":preset.sprite_path.c_str());
        ImGui::TextDisabled("Runtime class %u · current LEV: %s",preset.runtime_model_class_id,
            level_diagnostic.c_str());
        ImGui::TextDisabled("Canonical ID %u · compatible IDs %s · stock placements %u",
            thandor::canonical_placement_registry_id(preset),
            compatible_ids.empty()?"same ID only":compatible_ids.c_str(),preset.stock_placement_count);

        ImGui::SeparatorText("PCK gameplay values");
        if(preset.fog_uncover_radius_q12>0){
            ImGui::Text("Fog uncover radius: %.3f world units",thandor::q12_to_float(
                preset.fog_uncover_radius_q12));
            ImGui::TextDisabled("MDL +0x48 max over resolved hierarchy: %d Q12 · terrain wedge scan %u steps (radius / 0x240)",
                preset.fog_uncover_radius_q12,preset.fog_uncover_scan_steps);
        }else ImGui::TextDisabled("Fog uncover radius: no nonzero MDL +0x48 value");
        ImGui::Text("HP / integrity: %lld total · %d root",
            static_cast<long long>(preset.total_maximum_integrity),preset.root_maximum_integrity);
        ImGui::TextDisabled("MDL +0x60 is copied into each live model node; total follows the resolved runtime hierarchy.");
        if(!preset.base_cost_components.empty()){
            ImGui::Text("Base configuration price: %.2f Xenite · %.2f energy",
                static_cast<double>(preset.base_xenite_cost_q4)/16.0,
                static_cast<double>(preset.base_energy_cost_q4)/16.0);
            ImGui::TextDisabled("Exact base ARM→MDL TEC component sum. Serialized stock ARM +0x28 is zero; this is not presented as a direct ARM field.");
            for(const auto& component:preset.base_cost_components){
                ImGui::BulletText("MDL %u · TEC %u %s: %.2f Xenite, %.2f energy, %.2f s research",
                    component.model_definition_id,component.technology_id,
                    component.title.empty()?"unnamed":component.title.c_str(),
                    static_cast<double>(component.xenite_cost_q4)/16.0,
                    static_cast<double>(component.energy_cost_q4)/16.0,
                    static_cast<double>(component.research_duration_q5)/32.0);
            }
        }else ImGui::TextDisabled("Base configuration price: no nonzero base TEC component");

        std::unordered_set<uint32_t> displayed_damage_shots;
        for(const auto& weapon:preset.simulation_weapon_profiles){
            if(!displayed_damage_shots.insert(weapon.shot_definition_id).second)continue;
            const auto* shot=catalog_.shot_definition(weapon.shot_definition_id);
            if(!shot)continue;
            const auto [damage_min,damage_max]=std::minmax_element(
                shot->target_impact_damage_q12.begin(),shot->target_impact_damage_q12.end());
            ImGui::Text("SHT %u damage: %.3f..%.3f Q12-decoded · range %.3f · reload %u ticks",
                shot->definition_id,thandor::q12_to_float(*damage_min),
                thandor::q12_to_float(*damage_max),thandor::shot_selection_range_world(*shot),
                weapon.reload_ticks);
            std::string damage_classes;
            for(uint32_t impact_class=0U;impact_class<shot->target_impact_damage_q12.size();++impact_class){
                if(!damage_classes.empty())damage_classes+=" · ";
                damage_classes+=std::to_string(impact_class)+":"+
                    std::to_string(shot->target_impact_damage_q12[impact_class]);
            }
            ImGui::TextDisabled("target-class damage raw (+0xB0): %s",damage_classes.c_str());
        }

        const auto& pipeline_validation=preset.effect_pipeline_validation;
        ImGui::Text("Effect pipeline: %s · %u weapon pipelines · %u SHT · %u EFF · %u SPR · depth %u",
            pipeline_validation.ok()?"validated":"FAILED",pipeline_validation.weapon_pipeline_count,
            pipeline_validation.shot_reference_count,pipeline_validation.effect_reference_count,
            pipeline_validation.sprite_reference_count,pipeline_validation.maximum_effect_depth);
        for(const auto& issue:pipeline_validation.issues)
            ImGui::TextColored(ImVec4(1.0F,0.55F,0.48F,1.0F),"%s",issue.c_str());

        if(!preset.loadout_name.empty()||!preset.technology_panel_entries.empty()||
           !preset.model_variant_groups.empty()){
            ImGui::SeparatorText("Research / model variants");
            if(!preset.loadout_name.empty())ImGui::Text("Loadout: %s",preset.loadout_name.c_str());
            if(!preset.technology_summary.empty())ImGui::Text("Panel: %s",preset.technology_summary.c_str());
            if(preset.linked_technology_id!=0U){
                ImGui::Text("Linked auto-unlock TEC %u (+0x1C4): %s",preset.linked_technology_id,
                    preset.linked_technology_title.empty()?"unnamed":preset.linked_technology_title.c_str());
            }
            for(uint32_t branch=0U;branch<preset.technology_branch_names.size();++branch){
                if(!preset.technology_branch_names[branch].empty())
                    ImGui::TextDisabled("branch %u: %s",branch+1U,
                        preset.technology_branch_names[branch].c_str());
            }
            for(const auto& entry:preset.technology_panel_entries){
                ImGui::TextDisabled("table index %u · branch %u · panel row %u · TEC %u · %s · %s",
                    entry.serialized_index,entry.direction_index+1U,entry.row_index+1U,
                    entry.technology_id,thandor::technology_category_name(entry.category),
                    entry.title.empty()?"unnamed":entry.title.c_str());
            }
            for(const auto& group:preset.model_variant_groups){
                ImGui::Text("ARM node +0x%X · depth %u · child slot %u%s",group.arm_node_offset,
                    group.depth,group.child_slot,group.depth==0U?" · chassis/root":" · attachment branch");
                for(const auto& variant:group.variants){
                    ImGui::BulletText("variant %u · MDL %u · unlock TEC %u%s%s · linked TEC %u%s%s",
                        variant.variant_index,variant.model_definition_id,variant.technology_unlock_id,
                        variant.technology_title.empty()?"":" · ",
                        variant.technology_title.empty()?"":variant.technology_title.c_str(),
                        variant.linked_technology_id,
                        variant.linked_technology_title.empty()?"":" · ",
                        variant.linked_technology_title.empty()?"":variant.linked_technology_title.c_str());
                    for(const auto& pipeline:variant.weapon_pipelines){
                        const auto describe_effect=[](const thandor::EffectPipelineReferenceInfo& effect){
                            if(effect.effect_definition_id==0U)return std::string("none");
                            std::string text=std::to_string(effect.effect_definition_id)+" "+
                                thandor::effect_lifecycle_transition_kind_name(effect.transition_kind);
                            if(!effect.sprite_path.empty())text+=" "+effect.sprite_path;
                            return text;
                        };
                        const auto describe_effects=[&](
                            const std::vector<thandor::EffectPipelineReferenceInfo>& effects){
                            std::string text;
                            for(const auto& effect:effects){
                                if(!text.empty())text+="; ";
                                text+=describe_effect(effect);
                            }
                            return text.empty()?std::string("none"):text;
                        };
                        const std::string terrain_effects=describe_effects(
                            pipeline.terrain_impact_effects);
                        const std::string target_effects=describe_effects(
                            pipeline.target_class_impact_effects);
                        ImGui::TextDisabled("    kind-2 launch ×%u mask 0x%02X · SHT %u %s · %s",
                            pipeline.launch_attachment_count,pipeline.launch_attachment_selector_mask,
                            pipeline.shot_definition_id,
                            thandor::shot_trajectory_mode_name(pipeline.trajectory_kind),
                            pipeline.shot_sprite_path.c_str());
                        ImGui::TextDisabled("      primary EFF: %s",describe_effect(pipeline.primary_effect).c_str());
                        ImGui::TextDisabled("      launch EFF: %s",describe_effect(pipeline.launch_effect).c_str());
                        ImGui::TextDisabled("      secondary EFF: %s",describe_effect(pipeline.secondary_effect).c_str());
                        ImGui::TextDisabled("      terrain impact EFF: %s",terrain_effects.c_str());
                        ImGui::TextDisabled("      target-class impact EFF: %s",target_effects.c_str());
                    }
                }
            }
        }

        ImGui::SeparatorText("Visual/effect evidence");
        ImGui::TextDisabled("Resolved model nodes %zu · linked ARM models %u · deferred MDL attachments %u",
            preset.resolved_model_hierarchy.empty()?preset.model_hierarchy.size():preset.resolved_model_hierarchy.size(),
            preset.linked_model_runtime_count,preset.deferred_model_attachment_count);
        ImGui::TextDisabled("Timed EFF %u every %u..%u ticks · shared-sprite EFF %s",
            preset.timed_effect_id,preset.timed_effect_interval_ticks,
            preset.timed_effect_interval_ticks+
                (preset.timed_effect_random_ticks==0?0:preset.timed_effect_random_ticks-1),
            shared_sprite_effect_ids.empty()?"none":shared_sprite_effect_ids.c_str());
        try{
            const auto& preview=asset_preview(preset);
            ImGui::TextDisabled("Textured triangles %zu · raster pixels %zu / %u×%u",
                preview.textured_source_triangle_count,preview.covered_pixel_count,
                preview.raster_width,preview.raster_height);
        }catch(const std::exception& error){
            ImGui::TextColored(ImVec4(1.0F,0.55F,0.48F,1.0F),"Preview unavailable: %s",error.what());
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::EndChild();
    ImGui::PopID();
}

void EditorApp::load_scenario_catalog_labels(){
    scenario_difficulty_labels_.clear();scenario_size_labels_.clear();scenario_planet_labels_.clear();
    try{
        const auto text=thandor::TextResourceAsset::parse(catalog_.load("texte/level.str"));
        for(uint32_t index=0;index<5U;++index)scenario_difficulty_labels_.push_back(text.string(index));
        for(uint32_t index=5U;index<10U;++index)scenario_size_labels_.push_back(text.string(index));
        for(uint32_t index=10U;index<20U;++index)scenario_planet_labels_.push_back(text.string(index));
    }catch(const std::exception&){
        for(uint32_t index=0;index<5U;++index){
            scenario_difficulty_labels_.push_back("Difficulty resource "+std::to_string(0x2200U+index));
            scenario_size_labels_.push_back("Size resource "+std::to_string(0x2205U+index));
        }
        for(uint32_t index=0;index<10U;++index)
            scenario_planet_labels_.push_back("Planet/theme resource "+std::to_string(0x220AU+index));
    }
}

void EditorApp::refresh_scenario_text_buffers(){
    if(!document_)return;
    const auto strings=document_->scenario_detail_strings();
    for(std::size_t index=0;index<strings.size();++index){
        std::snprintf(scenario_text_buffers_[index].data(),scenario_text_buffers_[index].size(),"%s",strings[index].c_str());
    }
    scenario_text_buffers_dirty_=false;
}

void EditorApp::apply_scenario_text_buffers(){
    if(!document_)return;
    try{
        checkpoint();
        for(uint32_t index=0;index<scenario_text_buffers_.size();++index)
            document_->set_scenario_detail_string(index,scenario_text_buffers_[index].data());
        scenario_text_buffers_dirty_=false;
        status_="Scenario summary and detail text updated.";
    }catch(const std::exception& error){status_=error.what();}
}

void EditorApp::draw_right_panel(){
    const auto io=ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x-kRightPanelWidth,ImGui::GetFrameHeight()});
    ImGui::SetNextWindowSize({kRightPanelWidth,io.DisplaySize.y-ImGui::GetFrameHeight()-kStatusBarHeight});
    ImGui::Begin("Map tools",nullptr,ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
    if(ImGui::BeginTabBar("tools-tabs")){
        if(ImGui::BeginTabItem("Terrain")){
            const std::array<Tool,8> tools{Tool::Select,Tool::Raise,Tool::Lower,Tool::Smooth,
                Tool::Flatten,Tool::Water,Tool::ResourceField,Tool::Texture};
            ImGui::TextDisabled("Choose one brush. Only its own settings are shown below.");
            for(std::size_t index=0;index<tools.size();++index){
                if(draw_tool_icon_button(tools[index],tool_==tools[index])){
                    tool_=tools[index];
                    stroke_active_=false;
                    if(tool_==Tool::Water){draw_water_=true;ensure_water_selection_size();}
                }
                if(index%4U!=3U)ImGui::SameLine();
            }

            const auto draw_radius=[&](){
                ImGui::SliderFloat("Radius",&brush_.radius_cells,0.25F,24.0F,"%.2f cells");
            };
            const auto draw_falloff=[&](){
                int falloff=static_cast<int>(brush_.falloff);
                const char* falloffs[]={"Constant","Linear","Smooth"};
                if(ImGui::Combo("Falloff",&falloff,falloffs,3))
                    brush_.falloff=static_cast<thandor::BrushFalloff>(falloff);
            };
            const auto draw_terrain_water_policy=[&](){
                int water_policy=static_cast<int>(terrain_water_policy_);
                const char* policies[]={"Keep absolute water level","Move water with terrain","Ignore water-covered cells"};
                if(ImGui::Combo("Water response",&water_policy,policies,3))
                    terrain_water_policy_=static_cast<thandor::TerrainWaterEditPolicy>(water_policy);
                if(ImGui::IsItemHovered())ImGui::SetTooltip("FLD stores water as a relative terrain delta. This controls how ordinary height brushes treat existing water.");
            };

            ImGui::SeparatorText(tool_name(tool_));
            switch(tool_){
                case Tool::Select:
                    ImGui::TextWrapped("Click a placed object to select it. Use the transform gizmo in the Objects tab. Ctrl+D clears the temporary water selection and object selection.");
                    break;
                case Tool::Raise:
                case Tool::Lower:
                case Tool::Smooth:
                    draw_radius();
                    ImGui::SliderFloat("Strength",&brush_.strength,0.01F,1.0F);
                    draw_falloff();
                    draw_terrain_water_policy();
                    ImGui::TextDisabled("Left-drag edits terrain. In Select mode, left-drag turns the camera. Right-drag moves it.");
                    break;
                case Tool::Flatten:
                    draw_radius();
                    ImGui::SliderFloat("Strength",&brush_.strength,0.01F,1.0F);
                    draw_falloff();
                    draw_terrain_water_policy();
                    ImGui::DragFloat("Target height",&flatten_height_,0.05F);
                    if(ImGui::Button("Use map-center terrain height",{-1.0F,0.0F})){
                        const auto& field=document_->field();
                        flatten_height_=field.at(field.width()/2U,field.height()/2U).terrain_height();
                    }
                    break;
                case Tool::Water:{
                    ensure_water_selection_size();
                    ImGui::Text("Active animated surface: %s",thandor::terrain_surface_family_name(surface_family_));
                    ImGui::TextDisabled("The game shares one DAT/GFX/PAL-driven movement shader for water, lava, and ice.");
                    draw_radius();
                    ImGui::DragFloat("Level step",&water_level_step_,0.01F,0.01F,16.0F,"%.2f");
                    ImGui::Text("Selected cells: %zu",water_selection_count());
                    const char* stage=water_selection_stage_==WaterSelectionStage::SelectArea?"1. Select rough area":
                        (water_selection_stage_==WaterSelectionStage::PickReference?"2. Pick reference terrain":"3. Adjust flat surface");
                    ImGui::TextColored({0.35F,0.78F,1.0F,1.0F},"%s",stage);
                    if(water_selection_stage_==WaterSelectionStage::SelectArea){
                        ImGui::TextWrapped("Left-drag marks the rough water area. Hold Shift to remove cells. The cyan overlay is temporary and is not exported.");
                        if(ImGui::Button("Continue: pick level from selected terrain",{-1.0F,0.0F})){
                            if(water_selection_count()==0U)status_="Select at least one cell first.";
                            else water_selection_stage_=WaterSelectionStage::PickReference;
                        }
                    }else if(water_selection_stage_==WaterSelectionStage::PickReference){
                        ImGui::TextWrapped("Click a cyan cell on the rim or surrounding terrain. Its terrain Z becomes the initial perfectly flat water surface.");
                        if(ImGui::Button("Back to area selection",{-1.0F,0.0F}))
                            water_selection_stage_=WaterSelectionStage::SelectArea;
                    }else{
                        ImGui::DragFloat("Absolute water surface Z",&water_height_,0.01F);
                        if(ImGui::Button("Water down",{155.0F,0.0F}))adjust_water_selection_level(-water_level_step_);
                        ImGui::SameLine();
                        if(ImGui::Button("Water up",{155.0F,0.0F}))adjust_water_selection_level(water_level_step_);
                        if(ImGui::Button("Apply flat water to selection",{-1.0F,0.0F}))
                            apply_flat_water_selection(true);
                        if(ImGui::Button("Erase water in selection",{-1.0F,0.0F}))
                            apply_flat_water_selection(false);
                        ImGui::TextWrapped("Mouse wheel over the map raises/lowers this single absolute surface. Every selected wet cell stays at the same Z; no radial hill or falloff is applied.");
                        if(ImGui::Button("Pick another reference point",{-1.0F,0.0F}))
                            water_selection_stage_=WaterSelectionStage::PickReference;
                    }
                    if(ImGui::Button("Clear selection  Ctrl+D",{-1.0F,0.0F}))clear_water_selection();
                    break;
                }
                case Tool::ResourceField:{
                    draw_radius();
                    ImGui::TextWrapped("Paints the buildable FLD resource mask independently from visible crystal placements.");
                    int type=resource_field_occupancy_class_==0U?0:1;
                    const char* types[]={"Building material — gold (FLD 0x0800)","Energy — blue (FLD 0x1000)"};
                    if(ImGui::Combo("Field type",&type,types,2))resource_field_occupancy_class_=type==0?0U:1U;
                    if(ImGui::RadioButton("Paint",!resource_field_erase_mode_))resource_field_erase_mode_=false;
                    ImGui::SameLine();
                    if(ImGui::RadioButton("Erase",resource_field_erase_mode_))resource_field_erase_mode_=true;
                    ImGui::TextDisabled("Shift temporarily reverses Paint/Erase.");
                    break;
                }
                case Tool::Texture:{
                    draw_radius();
                    ImGui::SliderFloat("Strength",&brush_.strength,0.01F,1.0F);
                    draw_falloff();
                    draw_material_palette();
                    ImGui::SeparatorText("Editable texture exchange");
                    ImGui::InputTextWithHint("##texture-exchange","Path to a 256x256 .tga",texture_exchange_path_,sizeof(texture_exchange_path_));
                    if(ImGui::Button("Export selected layer as TGA",{-1.0F,0.0F})){
                        try{
                            if(selected_material_>=material_images_.size())throw std::runtime_error("selected terrain layer is unavailable");
                            thandor::save_tga_rgba(texture_exchange_path_,material_images_[selected_material_]);
                            status_="Exported editable terrain layer to "+std::string(texture_exchange_path_)+".";
                        }catch(const std::exception& e){status_=e.what();}
                    }
                    if(ImGui::Button("Import TGA into selected layer",{-1.0F,0.0F})){
                        try{
                            auto image=thandor::load_tga_rgba(texture_exchange_path_);
                            if(image.width!=256U||image.height!=256U)
                                throw std::runtime_error("terrain texture import requires exactly 256x256 pixels");
                            checkpoint();
                            document_->import_terrain_material_bytes(catalog_,selected_material_,
                                std::string("tga:")+texture_exchange_path_,thandor::encode_single_rgba_gfx(image));
                            load_materials();rebuild_mesh();
                            status_="Imported TGA into terrain layer "+std::string(1,static_cast<char>('A'+selected_material_))+".";
                        }catch(const std::exception& e){status_=e.what();}
                    }
                    ImGui::TextDisabled("TGA import is stored as a map-local GFX override when the project or PATCH00.PCK is saved.");

                    std::vector<const char*> texture_sources;
                    for(const auto& group:catalog_.map_groups())texture_sources.push_back(group.display_name.c_str());
                    static std::array<std::string,26> layer_storage=[](){std::array<std::string,26> a{};for(int i=0;i<26;++i)a[i]=std::string(1,static_cast<char>('A'+i));return a;}();
                    const char* layer_names[26]{};for(int i=0;i<26;++i)layer_names[i]=layer_storage[static_cast<std::size_t>(i)].c_str();
                    ImGui::SeparatorText("Copy layer from another map");
                    ImGui::Combo("Source map",&texture_source_group_,texture_sources.data(),static_cast<int>(texture_sources.size()));
                    ImGui::Combo("Source layer",&texture_source_material_,layer_names,26);
                    ImGui::Combo("Destination layer",&texture_destination_material_,layer_names,26);
                    if(ImGui::Button("Import layer from stock map",{-1.0F,0.0F})){
                        try{
                            checkpoint();
                            const auto source=thandor::LevelAsset::parse(catalog_.load(catalog_.map_groups()[static_cast<std::size_t>(texture_source_group_)].level_path));
                            const std::string source_path=terrain_material_path(terrain_base_path(source.paths().ground_textures),static_cast<uint32_t>(texture_source_material_));
                            document_->import_terrain_material(catalog_,static_cast<uint8_t>(texture_destination_material_),source_path);
                            selected_material_=static_cast<uint8_t>(texture_destination_material_);load_materials();rebuild_mesh();
                            status_="Imported "+source_path+" into the current map.";
                        }catch(const std::exception& e){status_=e.what();}
                    }
                    ImGui::InputTextWithHint("##biome-project","Optional source .thmap project path",texture_import_project_path_,sizeof(texture_import_project_path_));
                    if(ImGui::Button("Import layer from project",{-1.0F,0.0F})){
                        try{
                            if(texture_import_project_path_[0]=='\0')throw std::runtime_error("source project path is empty");
                            checkpoint();
                            auto source_document=thandor::MapDocument::open_project(catalog_,texture_import_project_path_);
                            const std::string source_path=terrain_material_path(terrain_base_path(source_document.level().paths().ground_textures),static_cast<uint32_t>(texture_source_material_));
                            document_->import_terrain_material_bytes(catalog_,static_cast<uint8_t>(texture_destination_material_),
                                std::string("project:")+texture_import_project_path_+"#"+source_path,source_document.load_asset(catalog_,source_path));
                            selected_material_=static_cast<uint8_t>(texture_destination_material_);load_materials();rebuild_mesh();
                            status_="Imported custom terrain layer from project.";
                        }catch(const std::exception& e){status_=e.what();}
                    }
                    break;
                }
                default: break;
            }

            ImGui::SeparatorText("Terrain compatibility overlay");
            const char* compatibility_modes[]={"Off","Vehicle traversal","Building placement","Raw class bands"};
            if(ImGui::Combo("Overlay",&terrain_compatibility_overlay_,compatibility_modes,4))rebuild_objects();
            if(terrain_compatibility_overlay_!=0)
                ImGui::TextDisabled("Cells: %zu safe · %zu caution · %zu blocked",
                    terrain_overlay_safe_cell_count_,terrain_overlay_caution_cell_count_,terrain_overlay_blocked_cell_count_);
            ImGui::TextWrapped("This reports binary-proven traversal/placement compatibility. No independent terrain damage-over-time table was found.");

            ImGui::SeparatorText("Fog of war preview");
            const char* fog_modes[]={"Off","Selected faction","All active factions"};
            bool fog_changed=ImGui::Combo("Visibility",&fog_preview_mode_,fog_modes,3);
            if(fog_preview_mode_==1){
                int faction=std::clamp(fog_preview_faction_,1,7);
                if(ImGui::SliderInt("View faction",&faction,1,7)){
                    fog_preview_faction_=faction;fog_changed=true;
                }
            }
            if(ImGui::SliderFloat("Vision radius scale",&fog_preview_radius_scale_,0.25F,3.0F,"%.2fx"))
                fog_changed=true;
            if(fog_changed){
                last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
                rebuild_objects();
            }
            if(fog_preview_mode_!=0)
                ImGui::TextDisabled("Observers: %zu · currently visible cells: %zu",
                    fog_preview_observer_count_,fog_preview_visible_cell_count_);
            ImGui::TextWrapped("LEV/FLD contain no persistent explored-fog bitmap. This editor-only view recomputes current radial visibility from active unit/building placements; Selected faction uses one faction, All uses their union.");
            ImGui::TextWrapped("Select mode: left-drag turns the camera. Right-drag moves it; middle-drag remains a pan shortcut. Arrow keys also move.");
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Objects")){
            const std::array<Tool,4> tools{Tool::Select,Tool::Resource,Tool::PlayerStart,Tool::EffectScenery};
            for(auto value:tools){
                if(ImGui::RadioButton(tool_name(value),tool_==value))tool_=value;
                if(value!=tools.back())ImGui::SameLine();
            }
            if(ImGui::InputScalar("Army asset ID",ImGuiDataType_U32,&selected_asset_id_))rebuild_objects();
            if(const auto* selected_preset=catalog_.army_preset(selected_asset_id_);selected_preset!=nullptr){
                const auto diagnostic=catalog_.placement_compatibility_diagnostic(*selected_preset,document_->level());
                if(!diagnostic.empty())ImGui::TextColored({1.0F,0.45F,0.4F,1.0F},"Not placeable: %s",diagnostic.c_str());
            }
            draw_global_faction_selector("object-view-faction");
            ImGui::InputScalar("New orientation",ImGuiDataType_U32,&selected_orientation_);
            ImGui::TextWrapped("Drag a 3D asset card from the left panel directly onto the map. Click a placed object to select it.");

            ImGui::SeparatorText("Transform gizmo");
            if(ImGui::Button("Translate  W",{155.0F,0.0F}))gizmo_mode_=GizmoMode::Translate;
            ImGui::SameLine();
            if(ImGui::Button("Rotate  E",{155.0F,0.0F}))gizmo_mode_=GizmoMode::Rotate;
            if(gizmo_mode_==GizmoMode::Translate)
                ImGui::TextDisabled("Red X and green Y move the LEV record. Blue Z raises or lowers its FLD terrain anchor.");
            else
                ImGui::TextDisabled("Drag the yellow ring to change the stock LEV yaw. Hold Shift for 15 degree snapping.");

            draw_selected_object_editor();

            ImGui::SeparatorText("Placed records");
            ImGui::BeginChild("placed-record-scroll",{0,170.0F},false,ImGuiWindowFlags_AlwaysVerticalScrollbar);
            auto placements=document_->level().placements();
            for(std::size_t i=0;i<placements.size();++i){
                const auto& placement=placements[i];
                const auto* preset=catalog_.army_preset(placement.army_asset_id);
                std::string placed_name=preset?preset->display_name:"unresolved";
                for(const auto& tag:document_->placement_tags())if(tag.placement_index==i&&!tag.display_name.empty()){placed_name=tag.display_name;break;}
                char label[256];
                std::snprintf(label,sizeof(label),"#%zu  %s  [ID %u]  (%.1f, %.1f)",i,
                    placed_name.c_str(),placement.army_asset_id,placement.render_x(),placement.render_y());
                if(ImGui::Selectable(label,selected_placement_==i)){
                    selected_placement_=i;
                    tool_=Tool::Select;
                }
            }
            ImGui::EndChild();

            ImGui::SeparatorText("Render diagnostics");
            ImGui::Text("Textured %zu · trees %zu · rocks %zu · layers %zu",rendered_object_count_,tree_object_count_,rock_object_count_,object_texture_layer_count_);
            ImGui::Text("Resources blue %zu · gold %zu · body %zu · particles %zu",power_resource_decal_count_,building_resource_decal_count_,resource_body_triangle_count_,resource_luminous_particle_triangle_count_);
            ImGui::Text("EFF 114/116 shading records: %zu · timed controllers %zu · effect triangles %zu · lava shot particles %zu",
                resource_shading_emitter_count_,timed_effect_controller_count_,
                timed_effect_sprite_triangle_count_,lava_linked_shot_particle_count_);
            ImGui::Text("Generated shadows %zu / %zu atlas layers · receiver triangles %zu",
                generated_shadow_object_count_,generated_shadow_texture_count_,projected_shadow_triangle_count_);
            ImGui::Text("unsupported selectors %zu",unsupported_primitive_selector_count_);
            ImGui::Text("Recursive model nodes %zu · animated child nodes %zu",recursive_model_node_count_,animated_model_node_count_);
            ImGui::Text("Visible dynamic buildings %zu / %zu · timed effects %zu / %zu",
                visible_animated_building_count_,animated_building_placements_.size(),
                visible_timed_effect_controller_count_,timed_effect_controllers_.size());
            ImGui::Text("Dynamic triangles %zu · CPU update %.3f ms · double-buffered uploads",
                dynamic_triangle_count_,dynamic_scene_build_milliseconds_);
            ImGui::Text("Building preview rate %ux · gameplay data unchanged",
                animation_preview_multiplier_);
            ImGui::TextWrapped("Static terrain, water/lava surface, shadows, and non-animated objects remain full-map GPU buffers. CPU animation geometry is generated only for the current camera view; Vulkan clips static off-screen triangles.");
            ImGui::Text("GPU indices %u · shadows %u · runs %u · shading records %u",renderer_.object_index_count(),renderer_.shadow_index_count(),renderer_.object_draw_run_count(),renderer_.resource_shading_record_count());
            if(object_build_error_count_!=0)
                ImGui::TextWrapped("Object build errors: %zu; first: %s",object_build_error_count_,first_object_build_error_.c_str());
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Players & AI")){
            const auto faction_summary=document_->playable_faction_summary();
            ImGui::TextWrapped("The game stores a human-assignable faction prefix and a larger total active-faction count. Active rows after the human prefix are computer-controlled.");
            try{
                auto configuration=document_->player_ai_configuration();
                int active=static_cast<int>(configuration.active_factions);
                int human=static_cast<int>(configuration.human_assignable_factions);
                const int minimum_active=std::clamp(static_cast<int>(faction_summary.highest_faction_index),2,7);
                bool counts_changed=false;
                counts_changed|=ImGui::SliderInt("Total active factions",&active,minimum_active,7);
                human=std::min(human,active);
                counts_changed|=ImGui::SliderInt("Human player slots",&human,1,active);
                if(counts_changed){
                    checkpoint();
                    document_->set_player_ai_configuration({static_cast<uint32_t>(human),
                                                            static_cast<uint32_t>(active)});
                    status_="Player and computer faction setup updated.";
                }
                ImGui::Text("Computer factions: %d",std::max(0,active-human));
                ImGui::TextDisabled("Faction rows 1..%d are assignable to humans. The raw AI class/mode field is preserved for every row.",human);
                if(human<active)
                    ImGui::TextDisabled("Faction rows %d..%d are computer-only.",human+1,active);
                else
                    ImGui::TextDisabled("No computer-only faction rows are active.");
                if(active<static_cast<int>(faction_summary.highest_faction_index))
                    ImGui::TextColored({1.0F,0.35F,0.25F,1.0F},"Active count is below the highest faction used by placements.");

                ImGui::SeparatorText("Starting resources by active faction");
                bool any_resource_changed=false;
                for(int faction=1;faction<=active;++faction){
                    ImGui::PushID(faction);
                    const bool computer=faction>human;
                    ImGui::SeparatorText((std::string("Faction ")+std::to_string(faction)+
                        (computer?" — Computer":" — Human slot")).c_str());
                    int building_units=static_cast<int>(document_->level().faction_building_resource_q4(
                        static_cast<uint32_t>(faction))/16U);
                    int energy_units=static_cast<int>(document_->level().faction_energy_resource_q4(
                        static_cast<uint32_t>(faction))/16U);
                    bool changed=false;
                    changed|=ImGui::InputInt("Building resource",&building_units,100,1000);
                    changed|=ImGui::InputInt("Energy",&energy_units,100,1000);
                    int ai_class_or_mode=static_cast<int>(document_->level().faction_ai_class_or_mode(
                        static_cast<uint32_t>(faction)));
                    bool ai_mode_changed=ImGui::InputInt("AI class / mode (raw LEV)",&ai_class_or_mode,1,1);
                    ai_class_or_mode=std::clamp(ai_class_or_mode,0,0x7fffffff);
                    building_units=std::clamp(building_units,0,10000000);
                    energy_units=std::clamp(energy_units,0,10000000);
                    if(changed||ai_mode_changed){
                        if(!any_resource_changed) checkpoint();
                        any_resource_changed=true;
                        document_->level().set_faction_starting_resources_q4(
                            static_cast<uint32_t>(faction),static_cast<uint32_t>(building_units)*16U,
                            static_cast<uint32_t>(energy_units)*16U);
                        if(ai_mode_changed)
                            document_->level().set_faction_ai_class_or_mode(
                                static_cast<uint32_t>(faction),static_cast<uint32_t>(ai_class_or_mode));
                        document_->mark_dirty();
                        status_="Faction player-slot metadata updated.";
                    }
                    if(building_units==0&&energy_units==0)
                        ImGui::TextColored({1.0F,0.45F,0.35F,1.0F},"Starts with no construction resources.");
                    ImGui::PopID();
                }
                if(ImGui::Button("Apply 6500 / 3000 to all active factions",{-1.0F,0.0F})){
                    checkpoint();
                    for(int faction=1;faction<=active;++faction)
                        document_->level().set_faction_starting_resources_q4(
                            static_cast<uint32_t>(faction),thandor::LevelAsset::kDefaultBuildingResourceQ4,
                            thandor::LevelAsset::kDefaultEnergyResourceQ4);
                    document_->mark_dirty();
                    status_="Applied stock skirmish starting resources to all active factions.";
                }
                ImGui::SeparatorText("Placement readiness");
                ImGui::Text("Highest faction used by placements: %u",faction_summary.highest_faction_index);
                ImGui::Text("Faction-owned placements: %u · start vehicles: %u",
                            faction_summary.nonneutral_placement_count,faction_summary.player_start_count);
                ImGui::TextWrapped("%s",faction_summary.diagnostic.c_str());
                ImGui::TextDisabled("LEV +0x310 is the human prefix; +0x314 is total active factions. The scenario range and embedded LEV digits are written as human..active, matching stock campaign maps.");
            }catch(const std::exception& error){
                ImGui::TextWrapped("Player/AI metadata unavailable: %s",error.what());
            }
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Map")){
            ImGui::Text("Validated PCK entries: %zu / %zu",startup_validation_.decoded_entries,startup_validation_.catalog_entries);
            char name[64]{};
            std::snprintf(name,sizeof(name),"%s",document_->map_name().c_str());
            if(ImGui::InputText("Map name",name,sizeof(name),ImGuiInputTextFlags_EnterReturnsTrue)){
                try{checkpoint();document_->set_map_name(name);status_="Map renamed.";}catch(const std::exception& e){status_=e.what();}
            }
            char display_name[128]{};
            std::snprintf(display_name,sizeof(display_name),"%s",document_->display_name().c_str());
            if(ImGui::InputText("Display name",display_name,sizeof(display_name),ImGuiInputTextFlags_EnterReturnsTrue)){
                try{checkpoint();document_->set_display_name(display_name);status_="Map display name updated.";}catch(const std::exception& e){status_=e.what();}
            }
            ImGui::TextDisabled("The display name is stored in the full editor project; the internal map name remains the LEV/FLD filename.");
            const auto faction_summary=document_->playable_faction_summary();
            ImGui::TextDisabled("Player/AI roles and starting resources are in the Players & AI tab.");

            ImGui::SeparatorText("Faction camera starts");
            ImGui::SliderInt("Camera faction",&selected_camera_faction_,1,
                             static_cast<int>(thandor::LevelAsset::kFactionCameraStartCount));
            try{
                const auto camera_start=document_->level().faction_camera_start(
                    static_cast<uint32_t>(selected_camera_faction_));
                ImGui::Text("Position: %.2f, %.2f, %.2f",camera_start.position_x(),
                            camera_start.position_y(),camera_start.position_z());
                ImGui::Text("Heading %.1f deg · pitch %.1f deg · magnitude %.2f",
                    static_cast<float>(camera_start.heading_turn16)*(360.0F/65536.0F),
                    static_cast<float>(camera_start.pitch_turn16)*(360.0F/65536.0F),
                    thandor::q12_to_float(static_cast<int32_t>(camera_start.motion_magnitude_q12)));
                if(ImGui::Button("Use stored camera",{-1.0F,0.0F}))
                    focus_faction_camera_start(static_cast<uint32_t>(selected_camera_faction_));
                if(ImGui::Button("Aim current view at faction start vehicle",{-1.0F,0.0F}))
                    aim_camera_at_faction_start_vehicle(static_cast<uint32_t>(selected_camera_faction_));
                if(ImGui::Button("Save current view as faction camera",{-1.0F,0.0F}))
                    capture_faction_camera_start(static_cast<uint32_t>(selected_camera_faction_));
            }catch(const std::exception& error){
                ImGui::TextWrapped("Camera record unavailable: %s",error.what());
            }
            ImGui::TextDisabled("Binary-backed LEV records: faction N uses +0x200 + (N-1)*0x20. ID 50 is the movable start vehicle; it does not contain the camera.");

            ImGui::SeparatorText("Single-player list parameters");
            auto frontend=document_->scenario_frontend_metadata();
            int planet=static_cast<int>(frontend.planet_theme_index);
            int difficulty=static_cast<int>(frontend.difficulty_index);
            int size_category=static_cast<int>(frontend.map_size_category_index);
            const auto combo=[&](const char* label,int& value,const std::vector<std::string>& labels){
                std::vector<const char*> names;names.reserve(labels.size());
                for(const auto& text:labels)names.push_back(text.c_str());
                return !names.empty()&&ImGui::Combo(label,&value,names.data(),static_cast<int>(names.size()));
            };
            bool frontend_changed=false;
            frontend_changed|=combo("Planet / theme",planet,scenario_planet_labels_);
            frontend_changed|=combo("Difficulty label",difficulty,scenario_difficulty_labels_);
            frontend_changed|=combo("Map-size label",size_category,scenario_size_labels_);
            if(frontend_changed){
                try{
                    checkpoint();
                    document_->set_scenario_frontend_metadata({static_cast<uint32_t>(planet),
                        static_cast<uint32_t>(difficulty),static_cast<uint32_t>(size_category)});
                    status_="Single-player catalog parameters updated.";
                }catch(const std::exception& error){status_=error.what();}
            }
            ImGui::TextDisabled("Binary fields: scenario +0x50 -> text 0x220A+n, +0x60 -> 0x2200+n, +0x80 -> 0x2205+n. These are list labels; actual dimensions come from the FLD header.");
            ImGui::Text("Actual field dimensions: %u x %u",document_->field().width(),document_->field().height());
            ImGui::Text("FLD converter version: 0x%08X",document_->field().converter_version());
            if(!document_->field().has_game_compatible_dimensions() ||
               (document_->field().converter_version()!=thandor::FieldGrid::kGameConverterVersion &&
                document_->field().converter_version()!=thandor::FieldGrid::kLegacyEditorConverterVersion)){
                ImGui::TextWrapped("Game export blocked: FLD must use converter 0x00060006, each axis must follow the 8*n+3 stock sequence from 59 through 139, and total area must not exceed 131 x 131.");
            }else if(document_->field().converter_version()==thandor::FieldGrid::kLegacyEditorConverterVersion){
                ImGui::TextWrapped("Legacy editor FLD 0x00070001 detected. Game export will rewrite it to the required 0x00060006 header.");
            }

            ImGui::SeparatorText("Environment movie / biome shell");
            static constexpr std::array<const char*,4> movie_labels{
                "Earth (flm\\erde)", "Ice (flm\\eis)",
                "Desert (flm\\wüste)", "Lava (flm\\lava)"};
            static constexpr std::array<const char*,4> movie_paths{
                "flm/erde", "flm/eis", "flm/wüste", "flm/lava"};
            int movie_index=0;
            const auto current_movie=thandor::normalize_asset_path(document_->level().paths().ending_movie);
            for(int i=0;i<static_cast<int>(movie_paths.size());++i){
                if(current_movie==movie_paths[static_cast<std::size_t>(i)])movie_index=i;
            }
            if(ImGui::Combo("Environment",&movie_index,movie_labels.data(),static_cast<int>(movie_labels.size()))){
                checkpoint();
                document_->level().paths().ending_movie=movie_paths[static_cast<std::size_t>(movie_index)];
                document_->mark_dirty();
                status_="Environment movie base updated; export will verify the matching FLM exists.";
            }
            if(current_movie.empty())
                ImGui::TextWrapped("Game export blocked: game.exe opens and advances this FLM before loading terrain or placements.");
            else
                ImGui::TextDisabled("Required startup asset: %s.flm",current_movie.c_str());

            if(ImGui::CollapsingHeader("Scenario summary, labels, and objectives")){
                ImGui::TextWrapped("STR string 1 is the map summary/setup text. Strings 2..8 and 9..15 are the seven paired scenario/faction label and objective slots used by the detail panel. Editing text changes presentation; player counts remain derived from faction placements.");
                scenario_text_buffers_dirty_|=ImGui::InputTextMultiline("Summary / setup##scenario1",scenario_text_buffers_[0].data(),scenario_text_buffers_[0].size(),{-1,86});
                for(int row=0;row<7;++row){
                    ImGui::PushID(row);
                    ImGui::Text("Detail row %d",row+1);
                    scenario_text_buffers_dirty_|=ImGui::InputText("Label",scenario_text_buffers_[1+row].data(),scenario_text_buffers_[1+row].size());
                    scenario_text_buffers_dirty_|=ImGui::InputTextMultiline("Objective / description",scenario_text_buffers_[8+row].data(),scenario_text_buffers_[8+row].size(),{-1,64});
                    ImGui::PopID();
                }
                if(ImGui::Button("Apply scenario text",{-1,0}))apply_scenario_text_buffers();
                if(scenario_text_buffers_dirty_)ImGui::TextDisabled("Scenario text has unapplied edits.");
            }
            int width=static_cast<int>(document_->field().width());
            int height=static_cast<int>(document_->field().height());
            ImGui::InputInt("Width",&width);
            ImGui::InputInt("Height",&height);
            if(ImGui::Button("Resize centered")){
                try{
                    checkpoint();
                    document_->resize_field(static_cast<uint32_t>(std::max(width,2)),
                                            static_cast<uint32_t>(std::max(height,2)),true);
                    rebuild_mesh();center_camera();
                    status_="Resized field and translated retained placements.";
                }catch(const std::exception& e){status_=e.what();}
            }
            ImGui::SeparatorText("Lighting and color");
            auto& lighting=document_->level().lighting();
            bool changed=false;
            ImVec4 additive=argb_to_imgui(lighting.base_color);
            ImVec4 ramp_base=argb_to_imgui(lighting.ramp_124);
            ImVec4 secondary=argb_to_imgui(lighting.ramp_12c);
            changed|=ImGui::ColorEdit4("Directional ramp base (+0x124)",&ramp_base.x);
            changed|=ImGui::ColorEdit4("Directional additive step (+0x120)",&additive.x);
            changed|=ImGui::ColorEdit4("Secondary lighting (+0x12C)",&secondary.x);
            int elevation=lighting.elevation;
            int azimuth=lighting.azimuth;
            changed|=ImGui::SliderInt("Light elevation",&elevation,0,65535);
            changed|=ImGui::SliderInt("Light azimuth",&azimuth,0,65535);
            ImGui::TextDisabled("Visibility preview: fully visible (fog of war disabled; cell overlay 0xFFFFFFFF).");
            if(changed){
                lighting.base_color=imgui_to_argb(additive);lighting.ramp_124=imgui_to_argb(ramp_base);
                lighting.ramp_12c=imgui_to_argb(secondary);
                lighting.elevation=static_cast<uint16_t>(elevation);lighting.azimuth=static_cast<uint16_t>(azimuth);
                document_->mark_dirty();rebuild_mesh();
            }
            if(ImGui::Checkbox("Exact soil.dat transition packets",&exact_soil_packets_))
                rebuild_mesh();
            ImGui::TextDisabled("On reproduces the game's BL-to-TR triangles, neighbor selector, soil.dat packet order, Q12 UVs, source alpha, and repeat wrapping. Off draws only each triangle's exact base packet.");
            uint32_t seed=soil_transition_rng_seed_;
            if(ImGui::InputScalar("Soil transition RNG seed",ImGuiDataType_U32,&seed,nullptr,nullptr,"0x%08X",ImGuiInputTextFlags_CharsHexadecimal)){
                soil_transition_rng_seed_=seed;rebuild_mesh();
            }
            if(ImGui::Button("Reset soil RNG seed to game default",{-1.0F,0.0F})){
                soil_transition_rng_seed_=thandor::FieldGrid::kDefaultSoilRandomSeed;rebuild_mesh();
            }
            ImGui::TextDisabled("The game consumes a global RNG before assigning 3-bit soil groups. 0x0000198F is the executable's initial primary seed; a live session can differ if another subsystem consumed it first.");
            uint32_t resource_seed=resource_animation_rng_seed_;
            if(ImGui::InputScalar("Resource animation RNG seed",ImGuiDataType_U32,&resource_seed,nullptr,nullptr,"0x%08X",ImGuiInputTextFlags_CharsHexadecimal)){
                resource_animation_rng_seed_=resource_seed;rebuild_objects();
            }
            if(ImGui::Button("Reset resource animation seed",{-1.0F,0.0F})){
                resource_animation_rng_seed_=thandor::FieldGrid::kDefaultSoilRandomSeed;rebuild_objects();
            }
            ImGui::TextDisabled("DirectDraw preview: every map resource emits immediately, then schedules independent 80..89 tick pulses from the shared primary RNG. The live session seed is not stored in LEV.");
            if(ImGui::Checkbox("Projected object shadows",&projected_object_shadows_))
                rebuild_objects();
            ImGui::TextDisabled("Rebuilds the game.exe light-plane model shadow: LEV +0x2F0 low-word azimuth/high-word elevation, SPR mesh-record soft/hard groups, generated mask, and a terrain-conforming receiver. Luminous resource particles do not cast shadows.");
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Export")){
            int slot=static_cast<int>(level_package_slot_);
            if(ImGui::SliderInt("Game level slot",&slot,0,99))level_package_slot_=static_cast<unsigned>(slot);
            ImGui::SeparatorText("Playable numbered level package");
            const auto export_factions=document_->playable_faction_summary();
            try{
                const auto player_ai=document_->player_ai_configuration();
                ImGui::Text("Exported factions: %u human slot(s), %u active total, %u computer",
                    player_ai.human_assignable_factions,player_ai.active_factions,
                    player_ai.active_factions-player_ai.human_assignable_factions);
            }catch(const std::exception& error){
                ImGui::TextWrapped("Faction metadata unavailable: %s",error.what());
            }
            if(!export_factions.exportable)ImGui::TextWrapped("Export blocked: %s",export_factions.diagnostic.c_str());
            ImGui::Checkbox("Back up an existing levelNN.pck",&backup_existing_level_package_);
            ImGui::TextWrapped("game.exe scans level99.pck down to level00.pck in the executable directory. Install writes one complete map with exact lowercase UTF-16 paths. It also removes stale entries for this slot/map from PATCH00.PCK, because PATCH00 is mounted first and an old level\\levelNN.dat or STR can crash the Single Player list.");
            if(ImGui::Button("Install and repair complete levelNN.pck",{-1,0}))export_game_level_package();

            ImGui::SeparatorText("PATCH archive high-priority overrides");
            ImGui::Checkbox("Imported/custom terrain and assets",&patch00_include_custom_assets_);
            ImGui::Checkbox("Back up modified PATCH archive",&backup_existing_patch00_);
            ImGui::TextWrapped("Map DAT/LEV/FLD/STR entries are never appended to PATCH archives. game.exe mounts patch99..patch00 before levelNN.pck, so any old map-chain copy can shadow the repaired numbered package. This action updates only custom resources plus synchronized global text pages in the highest-priority existing patch.");
            if(ImGui::Button("Append/replace safe patch resources",{-1,0}))update_patch00_overrides();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void EditorApp::draw_status_bar(){const auto io=ImGui::GetIO();ImGui::SetNextWindowPos({0,io.DisplaySize.y-kStatusBarHeight});ImGui::SetNextWindowSize({io.DisplaySize.x,kStatusBarHeight});ImGui::Begin("status",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoInputs);ImGui::Text("Tool: %s | Material: %u | %s",tool_name(tool_),selected_material_,status_.c_str());ImGui::End();}

void EditorApp::draw_new_map_dialog(){
    if(show_new_dialog_){ImGui::OpenPopup("Create new map");show_new_dialog_=false;}
    if(!ImGui::BeginPopupModal("Create new map",nullptr,ImGuiWindowFlags_AlwaysAutoResize))return;
    ImGui::InputText("Internal map name",new_map_name_,sizeof(new_map_name_));
    ImGui::InputText("Display name",new_display_name_,sizeof(new_display_name_));
    ImGui::Checkbox("Start with an empty flat field",&new_empty_map_);
    ImGui::InputInt("Width",&new_width_);ImGui::InputInt("Height",&new_height_);
    const bool new_dimensions_ok=new_width_>=0&&new_height_>=0&&
        thandor::FieldGrid::is_game_compatible_shape(static_cast<uint32_t>(new_width_),
                                                     static_cast<uint32_t>(new_height_));
    if(!new_dimensions_ok)ImGui::TextWrapped("Game-compatible shape: each axis must be 59, 67, 75, 83, 91, 99, 107, 115, 123, 131, or 139, and total area must not exceed 131 x 131. A 139-cell axis requires a smaller opposite axis.");
    std::vector<const char*> names;for(const auto& g:catalog_.map_groups())names.push_back(g.display_name.c_str());
    ImGui::Combo(new_empty_map_?"Compatibility/resource shell":"Terrain template",&template_group_,names.data(),static_cast<int>(names.size()));
    if(new_empty_map_){
        ImGui::DragFloat("Initial terrain height",&new_initial_height_,0.05F,-128.0F,128.0F);
        ImGui::SliderInt("Initial material",&new_initial_material_,0,25,"Layer %d");
        ImGui::TextWrapped("Empty creates a fresh flat FLD with no placements. A stock LEV shell is still required for binary-compatible camera, resource-list, lighting, and runtime fields; its terrain and objects are not copied.");
    }else{
        ImGui::TextWrapped("Template mode copies the selected stock FLD, resizes it, and clears all LEV placement records.");
    }
    if(ImGui::Button("Create",{150.0F,0.0F})){create_new_map();ImGui::CloseCurrentPopup();}
    ImGui::SameLine();if(ImGui::Button("Cancel",{150.0F,0.0F}))ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void EditorApp::draw_load_map_dialog(){
    if(show_load_dialog_){ImGui::OpenPopup("Load map or project");show_load_dialog_=false;}
    if(!ImGui::BeginPopupModal("Load map or project",nullptr,ImGuiWindowFlags_AlwaysAutoResize))return;
    ImGui::SetNextItemWidth(620.0F);ImGui::InputText("Path",load_map_path_,sizeof(load_map_path_));
    ImGui::TextWrapped("Load a full editor project (.thmap/.pck) or a loose .lev file with its same-name .fld beside it.");
    if(ImGui::Button("Load",{150.0F,0.0F})){
        load_map_from_path(load_map_path_);
        if(document_&&!status_.starts_with("Load failed"))ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();if(ImGui::Button("Cancel",{150.0F,0.0F}))ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void EditorApp::draw_save_map_dialog(){
    if(show_save_dialog_){
        if(save_project_path_[0]=='\0'){
            const auto path=catalog_.game_root()/"ThandorMapEditor"/"maps"/(document_->map_name()+".thmap");
            std::snprintf(save_project_path_,sizeof(save_project_path_),"%s",path.string().c_str());
        }
        ImGui::OpenPopup("Save full map project");show_save_dialog_=false;
    }
    if(!ImGui::BeginPopupModal("Save full map project",nullptr,ImGuiWindowFlags_AlwaysAutoResize))return;
    ImGui::SetNextItemWidth(620.0F);ImGui::InputText("Project path",save_project_path_,sizeof(save_project_path_));
    ImGui::TextWrapped("The project stores LEV, FLD, display and object names, and every imported terrain texture. Exporting a patch later writes the same complete gameplay data.");
    if(ImGui::Button("Save",{150.0F,0.0F})){save_project(save_project_path_);ImGui::CloseCurrentPopup();}
    ImGui::SameLine();if(ImGui::Button("Cancel",{150.0F,0.0F}))ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void EditorApp::draw_material_palette(){
    if(material_images_.empty()){ImGui::TextDisabled("No terrain material images loaded.");return;}
    constexpr int columns=5;
    const float spacing=ImGui::GetStyle().ItemSpacing.x;
    const float width=(ImGui::GetContentRegionAvail().x-spacing*static_cast<float>(columns-1))/columns;
    const float tile=std::clamp(width,42.0F,62.0F);
    for(std::size_t material=0;material<material_images_.size();++material){
        if(material%columns!=0)ImGui::SameLine();
        ImGui::PushID(static_cast<int>(material));
        const ImVec2 begin=ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("material",{tile,tile+18.0F});
        auto* draw=ImGui::GetWindowDrawList();
        const auto& image=material_images_[material];
        constexpr int samples=6;
        for(int y=0;y<samples;++y)for(int x=0;x<samples;++x){
            const float u=(static_cast<float>(x)+0.5F)/samples;
            const float v=(static_cast<float>(y)+0.5F)/samples;
            const ImU32 color=argb_to_im_u32(sample_wrapped_argb(image,u,v));
            draw->AddRectFilled({begin.x+tile*x/samples,begin.y+tile*y/samples},
                                {begin.x+tile*(x+1)/samples,begin.y+tile*(y+1)/samples},color);
        }
        const ImU32 border=selected_material_==material?IM_COL32(255,220,90,255):IM_COL32(110,110,110,255);
        draw->AddRect(begin,{begin.x+tile,begin.y+tile},border,0.0F,0,selected_material_==material?3.0F:1.0F);
        const std::string label=std::string(1,static_cast<char>('A'+material))+" ("+std::to_string(material)+")";
        draw->AddText({begin.x+3.0F,begin.y+tile+2.0F},IM_COL32_WHITE,label.c_str());
        if(ImGui::IsItemClicked()){selected_material_=static_cast<uint8_t>(material);tool_=Tool::Texture;}
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Layer %s\n%s\nClick, then paint with the Texture tool.",label.c_str(),material_names_[material].c_str());
        ImGui::PopID();
    }
}

void EditorApp::draw_selected_object_editor(){
    ImGui::SeparatorText("Selected object");
    if(!selected_placement_||*selected_placement_>=document_->level().placements().size()){
        selected_edit_index_.reset();ImGui::TextDisabled("No placed object selected.");return;
    }
    const std::size_t index=*selected_placement_;
    auto placements=document_->level().placements();
    auto& placement=placements[index];
    if(selected_edit_index_!=index){
        selected_edit_index_=index;
        const auto* preset=catalog_.army_preset(placement.army_asset_id);
        std::string name=preset?preset->display_name:("ARM "+std::to_string(placement.army_asset_id));
        for(const auto& tag:document_->placement_tags())if(tag.placement_index==index&&!tag.display_name.empty())name=tag.display_name;
        std::snprintf(selected_display_name_,sizeof(selected_display_name_),"%s",name.c_str());
        selected_edit_asset_id_=placement.army_asset_id;selected_edit_faction_=static_cast<int>(placement.faction_index);
        selected_edit_position_[0]=placement.render_x();selected_edit_position_[1]=placement.render_y();
        selected_edit_position_[2]=terrain_height_near(selected_edit_position_[0],selected_edit_position_[1]);
        selected_edit_yaw_degrees_=static_cast<float>(placement.orientation_turn16&0xFFFFU)*(360.0F/65536.0F);
    }
    ImGui::InputText("Display name",selected_display_name_,sizeof(selected_display_name_));
    ImGui::InputScalar("Asset ID",ImGuiDataType_U32,&selected_edit_asset_id_);
    const char* object_factions[]={"Neutral / default","Faction 1 — Blue","Faction 2 — Red",
        "Faction 3 — Green","Faction 4 — Yellow","Faction 5 — Orange",
        "Faction 6 — Brown","Faction 7 — Purple"};
    ImGui::Combo("Object faction colour",&selected_edit_faction_,object_factions,8);
    ImGui::DragFloat3("Position XYZ",selected_edit_position_.data(),0.05F);
    ImGui::DragFloat("Yaw degrees",&selected_edit_yaw_degrees_,0.5F,-3600.0F,3600.0F);
    ImGui::TextDisabled("Z edits the terrain anchor because stock LEV placement records have X, Y, and yaw but no independent Z field.");
    if(ImGui::Button("Apply selected object values",{-1.0F,0.0F})){
        try{
            const auto* preset=catalog_.army_preset(selected_edit_asset_id_);
            if(preset==nullptr)throw std::runtime_error("asset ID is not present in the ARM registry");
            checkpoint();
            const float current_target_z=terrain_height_near(selected_edit_position_[0],selected_edit_position_[1]);
            placement.army_asset_id=selected_edit_asset_id_;
            placement.faction_index=static_cast<uint32_t>(std::clamp(selected_edit_faction_,0,7));
            placement.set_render_position_q12(thandor::float_to_q12(selected_edit_position_[0]),thandor::float_to_q12(selected_edit_position_[1]));
            const float wrapped=std::fmod(selected_edit_yaw_degrees_,360.0F);
            placement.orientation_turn16=static_cast<uint32_t>(std::llround(wrapped*(65536.0/360.0)))&0xFFFFU;
            const float z_delta=selected_edit_position_[2]-current_target_z;
            if(std::abs(z_delta)>0.0001F)document_->field().offset_terrain_anchor(selected_edit_position_[0],selected_edit_position_[1],z_delta,terrain_water_policy_);
            for(auto& tag:document_->placement_tags())if(tag.placement_index==index){
                tag.display_name=selected_display_name_;tag.role=placement_role_for_preset(*preset);break;
            }
            selected_asset_id_=placement.army_asset_id;selected_faction_=placement.faction_index;selected_orientation_=placement.orientation_turn16;
            document_->mark_dirty();rebuild_mesh();selected_edit_index_.reset();
            status_="Updated selected object name, faction, position, terrain Z anchor, and yaw.";
        }catch(const std::exception& e){status_=e.what();}
    }
    const auto* preset=catalog_.army_preset(placement.army_asset_id);
    ImGui::TextWrapped("#%zu · %s · unknown LEV dwords %08X %08X %08X",index,
        preset?preset->display_name.c_str():"unresolved",placement.preserved_tail_dwords[0],placement.preserved_tail_dwords[1],placement.preserved_tail_dwords[2]);
    if(ImGui::Button("Center camera on selected",{-1.0F,0.0F})){
        camera_.target={placement.render_x(),placement.render_y(),terrain_height_near(placement.render_x(),placement.render_y())};
    }
    draw_ingame_selection_panel();
    if(ImGui::Button("Delete selected",{-1.0F,0.0F}))delete_selected_placement();
}

void EditorApp::draw_ingame_selection_panel(){
    if(!document_||!selected_placement_||*selected_placement_>=document_->level().placements().size())return;
    const auto index=*selected_placement_;
    const auto& placement=document_->level().placements()[index];
    const auto* preset=catalog_.army_preset(placement.army_asset_id);
    if(!preset)return;
    ImGui::SeparatorText("In-game selection / simulation");
    ImGui::PushStyleColor(ImGuiCol_ChildBg,IM_COL32(22,28,34,245));
    ImGui::PushStyleColor(ImGuiCol_Border,IM_COL32(112,132,142,255));
    ImGui::BeginChild("ingame-selection-panel",{0.0F,0.0F},ImGuiChildFlags_Border|ImGuiChildFlags_AutoResizeY);
    ImGui::TextColored(ImVec4(0.82F,0.91F,0.94F,1.0F),"%s",preset->display_name.c_str());
    ImGui::SameLine();ImGui::TextDisabled("F%u · %s",placement.faction_index,
        thandor::placement_browser_kind_name(preset->browser_kind));
    ImGui::TextDisabled("Game-style commands are transient. The existing transform gizmo remains the map-authoring tool.");
    ImGui::TextWrapped("Viewport commands: right-click terrain to drive a movable unit; right-click terrain with an armed immobile unit/building to set an attack point; right-click a different-faction placement to attack and follow it. Right-drag still pans the camera.");
    if(ImGui::Checkbox("Simulation mode · exact 20 Hz",&simulation_mode_)){
        last_simulation_tick_=current_presentation_tick_;
        last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
    }
    ImGui::Checkbox("Selected armed placement targets exclusively",&simulation_selected_only_);
    ImGui::Checkbox("Show ranges and orders",&simulation_show_ranges_);

    auto found=simulation_actor_states_.find(index);
    if(found!=simulation_actor_states_.end()){
        const auto& actor=found->second;
        const float movement=actor.move_target?1.0F:0.0F;
        ImGui::ProgressBar(movement,{-1.0F,5.0F},actor.move_target?"MOVING":"IDLE");
        ImGui::Text("World %.2f / %.2f / %.2f · body %.1f deg",actor.position.x,actor.position.y,
            actor.position.z,static_cast<float>(actor.body_yaw_turn16)*(360.0F/65536.0F));
    }

    if(!preset->simulation_weapon_profiles.empty()){
        ImGui::SeparatorText("Weapons");
        ImGui::Text("Recovered weapon channels: %zu",preset->simulation_weapon_profiles.size());
        for(std::size_t weapon_index=0;weapon_index<preset->simulation_weapon_profiles.size();++weapon_index){
            const auto& weapon=preset->simulation_weapon_profiles[weapon_index];
            const auto* shot=catalog_.shot_definition(weapon.shot_definition_id);
            std::string weapon_title;
            for(const auto& group:preset->model_variant_groups){
                for(const auto& variant:group.variants){
                    if(variant.model_definition_id==weapon.model_definition_id&&
                       !variant.technology_title.empty()){
                        weapon_title=variant.technology_title;break;
                    }
                }
                if(!weapon_title.empty())break;
            }
            const std::string weapon_title_prefix =
                weapon_title.empty() ? std::string{} : weapon_title + " · ";
            ImGui::PushID(static_cast<int>(weapon_index));
            ImGui::Text("W%zu · %sMDL %u · SHT %u · muzzle%s %u",weapon_index+1U,
                weapon_title_prefix.c_str(),weapon.model_definition_id,
                weapon.shot_definition_id,weapon.launch_attachment_count==1U?"":"s",
                weapon.launch_attachment_count);
            ImGui::TextDisabled("range %.2f · reload %.2f s · mode %d · speed %.3f/tick · launch EFF %u",
                thandor::q12_to_float(weapon.maximum_range_q12),
                static_cast<float>(weapon.reload_ticks)/thandor::kSimulationTicksPerSecond,
                shot?shot->trajectory_mode:-1,
                shot?std::abs(thandor::q12_to_float(shot->launch_speed_q12)):0.0F,
                shot?shot->launch_effect_id:0U);
            if(found!=simulation_actor_states_.end()&&weapon_index<found->second.weapons.size()){
                const auto& state=found->second.weapons[weapon_index];
                const float ready=weapon.reload_ticks==0U?1.0F:
                    1.0F-std::min(1.0F,static_cast<float>(state.cooldown_ticks)/weapon.reload_ticks);
                const char* lock=state.target_aligned&&state.line_of_fire_clear?"LOCKED":
                    state.target_in_range?(state.line_of_fire_clear?"AIMING":"BLOCKED"):"OUT OF RANGE";
                ImGui::ProgressBar(ready,{-1.0F,5.0F},lock);
            }
            ImGui::PopID();
        }
        if(ImGui::Button("Choose attack point on terrain",{-1.0F,0.0F})){
            tool_=Tool::SimulationTarget;simulation_mode_=true;
            status_="Click terrain to assign a point target to the selected armed placement.";
        }
        if(preset->browser_kind==thandor::PlacementBrowserKind::Building&&
           ImGui::Button("Bake whole placement facing toward target",{-1.0F,0.0F}))
            bake_selected_target_facing();
        ImGui::TextWrapped("LEV stores whole-placement yaw only. Independent turret angles are runtime state, so permanent map authoring safely bakes the complete tower/building facing instead of writing unknown tail fields.");
    }else ImGui::TextDisabled("No SHT projectile weapon with kind-2 launch attachments was recovered.");

    if(const auto target=simulation_attack_placement_targets_.find(index);
       target!=simulation_attack_placement_targets_.end()&&
       target->second<document_->level().placements().size()){
        const auto& target_placement=document_->level().placements()[target->second];
        const auto* target_preset=catalog_.army_preset(target_placement.army_asset_id);
        ImGui::TextColored(ImVec4(1.0F,0.48F,0.38F,1.0F),"Attack entity: %s · F%u",
            target_preset?target_preset->display_name.c_str():"unresolved",target_placement.faction_index);
    }else if(const auto target=simulation_attack_point_targets_.find(index);
             target!=simulation_attack_point_targets_.end()){
        ImGui::TextColored(ImVec4(1.0F,0.58F,0.32F,1.0F),"Attack point: %.2f / %.2f / %.2f",
            target->second.x,target->second.y,target->second.z);
    }

    if(preset->browser_kind==thandor::PlacementBrowserKind::Unit&&
       preset->simulation_motion_profile.movable()){
        ImGui::SeparatorText("Movement");
        ImGui::TextDisabled("Recovered ground step %.3f world/tick · contact %d: %s · default movement callback %s",
            std::abs(thandor::q12_to_float(preset->simulation_motion_profile.speed_q12_per_tick)),
            preset->simulation_motion_profile.placement_contact_kind,
            placement_contact_kind_name(preset->simulation_motion_profile.placement_contact_kind),
            preset->simulation_motion_profile.placement_contact_kind == 1 ? "B" : "A");
        if(preset->runtime_model_class_id==2U)
            ImGui::TextDisabled("Tank tracks: selectors %u / %u · travel multiplier %d",
                preset->primary_animated_subresource,preset->secondary_animated_subresource,
                preset->track_texture_travel_scale);
        if(preset->simulation_motion_profile.placement_contact_kind==3)
            ImGui::TextColored(ImVec4(1.0F,0.72F,0.25F,1.0F),
                "Articulated support active: root %d Q12 · contact rate %d · lift %d. "
                "The paired-foot IK preserves the binary contacts; gait timing is compatibility-mode.",
                preset->articulated_root_height_q12,
                preset->articulated_contact_interpolation_rate_q12,
                preset->articulated_contact_lift_q12);
        if(ImGui::Button("Drive selected unit to terrain point",{-1.0F,0.0F})){
            tool_=Tool::SimulationMove;simulation_mode_=true;animate_units_=true;
            status_="Click terrain to issue a movement destination to the selected unit.";
        }
        if(ImGui::Button("Stop selected unit",{-1.0F,0.0F}))
            simulation_state_for(index).move_target.reset();
        ImGui::TextWrapped("Driving writes each completed 20 Hz position/body-yaw step directly to the placement record. This mirrors ArmyRuntime_ResetMovementStateFromCurrentPosition and ArmyRuntime_ResetMovementStatePreserveQueuedTarget copying the live model X/Y into movement state, so Save uses the current location.");
        if(ImGui::Button("Finish driving at current map position",{-1.0F,0.0F}))
            commit_selected_simulation_position();
    }
    if(ImGui::Button("Reset selected transient simulation",{-1.0F,0.0F})){
        simulation_attack_placement_targets_.erase(index);
        simulation_attack_point_targets_.erase(index);
        if(simulation_actor_states_.erase(index)!=0U)
            simulation_static_replacement_dirty_=true;
        simulation_projectiles_.erase(std::remove_if(simulation_projectiles_.begin(),
            simulation_projectiles_.end(),[&](const auto& projectile){return projectile.owner_placement_index==index;}),
            simulation_projectiles_.end());
        last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
    }
    if(ImGui::Button("Clear selected movement / attack order",{-1.0F,0.0F}))
        clear_selected_simulation_order();
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}


TransformGizmoProjection EditorApp::selected_gizmo_projection() const{
    if(!document_||!selected_placement_||*selected_placement_>=document_->level().placements().size())return {};
    const auto io=ImGui::GetIO();
    const auto& placement=document_->level().placements()[*selected_placement_];
    const Vec3f origin{placement.render_x(),placement.render_y(),
        terrain_height_near(placement.render_x(),placement.render_y())+0.18F};
    const float scale=std::clamp(camera_.distance*0.055F,1.25F,8.0F);
    return project_transform_gizmo(origin,
        camera_.view_projection(io.DisplaySize.x/std::max(io.DisplaySize.y,1.0F)),
        io.DisplaySize.x,io.DisplaySize.y,scale);
}

GizmoHandle EditorApp::hit_test_gizmo(PreviewVec2 mouse) const{
    const auto projection=selected_gizmo_projection();
    if(!projection.visible)return GizmoHandle::None;
    if(gizmo_mode_==GizmoMode::Rotate){
        return rotation_ring_distance(mouse,projection.rotation_ring)<8.0F?
            GizmoHandle::RotateZ:GizmoHandle::None;
    }
    struct Candidate{GizmoHandle handle;float distance;};
    const std::array<Candidate,3> candidates{{
        {GizmoHandle::MoveX,point_segment_distance(mouse,projection.origin,projection.x_axis)},
        {GizmoHandle::MoveY,point_segment_distance(mouse,projection.origin,projection.y_axis)},
        {GizmoHandle::MoveTerrainZ,point_segment_distance(mouse,projection.origin,projection.z_axis)},
    }};
    const auto best=std::min_element(candidates.begin(),candidates.end(),
        [](const auto& first,const auto& second){return first.distance<second.distance;});
    return best!=candidates.end()&&best->distance<8.0F?best->handle:GizmoHandle::None;
}

void EditorApp::update_active_gizmo(PreviewVec2 mouse){
    if(!document_||gizmo_drag_.handle==GizmoHandle::None||!selected_placement_||
       *selected_placement_>=document_->level().placements().size())return;
    auto placements=document_->level().placements();
    auto& placement=placements[*selected_placement_];
    bool changed=false;
    if(gizmo_drag_.handle==GizmoHandle::MoveX||gizmo_drag_.handle==GizmoHandle::MoveY){
        const bool x_axis=gizmo_drag_.handle==GizmoHandle::MoveX;
        const PreviewVec2 endpoint=x_axis?gizmo_drag_.projection_start.x_axis:gizmo_drag_.projection_start.y_axis;
        float delta=axis_drag_world_delta(gizmo_drag_.mouse_start,mouse,
            gizmo_drag_.projection_start.origin,endpoint,gizmo_drag_.projection_start.world_scale);
        if(ImGui::GetIO().KeyCtrl)delta=std::round(delta*4.0F)*0.25F;
        const float next_x=gizmo_drag_.placement_start.render_x()+(x_axis?delta:0.0F);
        const float next_y=gizmo_drag_.placement_start.render_y()+(x_axis?0.0F:delta);
        if(document_->field().sample_terrain(next_x,next_y).inside){
            placement.set_render_position_q12(thandor::float_to_q12(next_x),thandor::float_to_q12(next_y));
            changed=true;
        }
    }else if(gizmo_drag_.handle==GizmoHandle::MoveTerrainZ){
        const auto projection=selected_gizmo_projection();
        if(projection.visible){
            float delta=axis_drag_world_delta(gizmo_drag_.mouse_previous,mouse,
                projection.origin,projection.z_axis,projection.world_scale);
            if(ImGui::GetIO().KeyCtrl)delta=std::round(delta*8.0F)*0.125F;
            if(std::abs(delta)>1.0e-6F){
                document_->field().offset_terrain_anchor(placement.render_x(),placement.render_y(),
                    delta,terrain_water_policy_);
                gizmo_drag_.mouse_previous=mouse;
                document_->mark_dirty();
                rebuild_mesh();
                status_="Moved selected object on terrain Z; the four supporting FLD vertices were translated.";
            }
        }
        return;
    }else if(gizmo_drag_.handle==GizmoHandle::RotateZ){
        const float current_angle=std::atan2(mouse.y-gizmo_drag_.projection_start.origin.y,
                                             mouse.x-gizmo_drag_.projection_start.origin.x);
        const float delta=std::remainder(current_angle-gizmo_drag_.rotation_screen_angle_start,
                                         2.0F*std::numbers::pi_v<float>);
        placement.orientation_turn16=add_turn16_delta(gizmo_drag_.placement_start.orientation_turn16,
                                                       delta,ImGui::GetIO().KeyShift);
        selected_orientation_=placement.orientation_turn16;
        changed=true;
    }
    if(changed){
        document_->mark_dirty();
        rebuild_objects();
        status_=gizmo_drag_.handle==GizmoHandle::RotateZ?
            "Rotated selected placement yaw.":"Moved selected placement on a world axis.";
    }
}

void EditorApp::draw_transform_gizmo(){
    if(!document_||!selected_placement_||*selected_placement_>=document_->level().placements().size())return;
    const auto projection=selected_gizmo_projection();
    if(!projection.visible)return;
    auto* draw=ImGui::GetBackgroundDrawList();
    const auto active=[&](GizmoHandle handle){return gizmo_drag_.handle==handle||gizmo_hovered_==handle;};
    if(gizmo_mode_==GizmoMode::Translate){
        const auto axis=[&](PreviewVec2 end,ImU32 base,ImU32 highlight,const char* label,GizmoHandle handle){
            const ImU32 color=active(handle)?highlight:base;
            draw->AddLine({projection.origin.x,projection.origin.y},{end.x,end.y},color,active(handle)?4.0F:3.0F);
            draw->AddCircleFilled({end.x,end.y},active(handle)?6.0F:5.0F,color,16);
            draw->AddText({end.x+5.0F,end.y-8.0F},color,label);
        };
        axis(projection.x_axis,IM_COL32(220,70,70,255),IM_COL32(255,145,120,255),"X",GizmoHandle::MoveX);
        axis(projection.y_axis,IM_COL32(70,210,95,255),IM_COL32(145,255,160,255),"Y",GizmoHandle::MoveY);
        axis(projection.z_axis,IM_COL32(70,125,235,255),IM_COL32(130,190,255,255),"Z",GizmoHandle::MoveTerrainZ);
    }else{
        const ImU32 color=active(GizmoHandle::RotateZ)?IM_COL32(255,205,85,255):IM_COL32(235,175,45,235);
        for(std::size_t index=1;index<projection.rotation_ring.size();++index){
            draw->AddLine({projection.rotation_ring[index-1U].x,projection.rotation_ring[index-1U].y},
                          {projection.rotation_ring[index].x,projection.rotation_ring[index].y},
                          color,active(GizmoHandle::RotateZ)?4.0F:3.0F);
        }
        draw->AddText({projection.origin.x+8.0F,projection.origin.y+8.0F},color,"Yaw");
    }
    draw->AddCircleFilled({projection.origin.x,projection.origin.y},4.0F,IM_COL32(245,245,245,255),12);
}

void EditorApp::draw_drag_drop_preview(){
    if(!drag_preview_asset_id_)return;
    const auto io=ImGui::GetIO();
    if(!mouse_in_canvas(io.MousePos.x,io.MousePos.y))return;
    const auto picked=pick_cell(io.MousePos.x,io.MousePos.y);
    if(!picked)return;
    const auto& cell=document_->field().at(picked->first,picked->second);
    const Vec3f point{thandor::q12_to_float(cell.world_x_q12),thandor::q12_to_float(cell.world_y_q12),
                      cell.terrain_height()+0.12F};
    float screen_x=0.0F,screen_y=0.0F,depth=0.0F;
    const auto vp=camera_.view_projection(io.DisplaySize.x/std::max(io.DisplaySize.y,1.0F));
    if(!project_to_screen(point,vp,io.DisplaySize.x,io.DisplaySize.y,screen_x,screen_y,depth))return;
    auto* draw=ImGui::GetBackgroundDrawList();
    draw->AddCircle({screen_x,screen_y},14.0F,IM_COL32(120,235,150,255),24,3.0F);
    draw->AddLine({screen_x-18.0F,screen_y},{screen_x+18.0F,screen_y},IM_COL32(120,235,150,255),2.0F);
    draw->AddLine({screen_x,screen_y-18.0F},{screen_x,screen_y+18.0F},IM_COL32(120,235,150,255),2.0F);
    if(const auto* preset=catalog_.army_preset(*drag_preview_asset_id_))
        draw->AddText({screen_x+18.0F,screen_y-18.0F},IM_COL32_WHITE,preset->display_name.c_str());
}

void EditorApp::draw_game_cursor(){
    if(!game_cursor_enabled_||game_cursor_.empty())return;
    auto& io=ImGui::GetIO();
    if(!mouse_in_canvas(io.MousePos.x,io.MousePos.y))return;

    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    const bool button_down=io.MouseDown[ImGuiMouseButton_Left]||
                           io.MouseDown[ImGuiMouseButton_Right]||
                           io.MouseDown[ImGuiMouseButton_Middle];
    const auto& frame=game_cursor_.frame(game_cursor_frame_);
    const auto& image=game_cursor_.image(game_cursor_frame_,button_down,ImGui::GetTime());
    if(image.width==0U||image.height==0U||
       image.pixels.size()!=static_cast<std::size_t>(image.width)*image.height*4U)return;

    const ImVec2 origin{io.MousePos.x-static_cast<float>(frame.hotspot_x),
                        io.MousePos.y-static_cast<float>(frame.hotspot_y)};
    auto* draw=ImGui::GetForegroundDrawList();
    draw->PushClipRect({kLeftPanelWidth,ImGui::GetFrameHeight()},
                       {io.DisplaySize.x-kRightPanelWidth,io.DisplaySize.y-kStatusBarHeight},true);
    for(uint32_t y=0;y<image.height;++y){
        uint32_t x=0U;
        while(x<image.width){
            const std::size_t offset=(static_cast<std::size_t>(y)*image.width+x)*4U;
            const uint8_t alpha=image.pixels[offset+3U];
            if(alpha==0U){++x;continue;}
            const uint8_t red=image.pixels[offset+0U];
            const uint8_t green=image.pixels[offset+1U];
            const uint8_t blue=image.pixels[offset+2U];
            uint32_t run_end=x+1U;
            while(run_end<image.width){
                const std::size_t next=(static_cast<std::size_t>(y)*image.width+run_end)*4U;
                if(image.pixels[next+0U]!=red||image.pixels[next+1U]!=green||
                   image.pixels[next+2U]!=blue||image.pixels[next+3U]!=alpha)break;
                ++run_end;
            }
            draw->AddRectFilled({origin.x+static_cast<float>(x),origin.y+static_cast<float>(y)},
                                {origin.x+static_cast<float>(run_end),origin.y+static_cast<float>(y+1U)},
                                IM_COL32(red,green,blue,alpha));
            x=run_end;
        }
    }
    draw->PopClipRect();
}

void EditorApp::update_canvas_interaction(){
    if(!document_)return;
    auto& io=ImGui::GetIO();
    const bool ctrl=io.KeyCtrl;
    if(ctrl&&ImGui::IsKeyPressed(ImGuiKey_Z,false)){if(io.KeyShift)redo();else undo();}
    if(ctrl&&ImGui::IsKeyPressed(ImGuiKey_Y,false))redo();
    if(ctrl&&ImGui::IsKeyPressed(ImGuiKey_E,false))export_game_level_package();
    if(ctrl&&ImGui::IsKeyPressed(ImGuiKey_D,false)&&!io.WantTextInput){
        if(water_selection_count()!=0U)clear_water_selection();
        selected_placement_.reset();
    }

    drag_preview_asset_id_.reset();
    if(const ImGuiPayload* payload=ImGui::GetDragDropPayload();
       payload&&payload->IsDataType(kAssetDragPayloadType)&&
       payload->DataSize==static_cast<int>(sizeof(AssetDragPayload))){
        const auto drag=*static_cast<const AssetDragPayload*>(payload->Data);
        drag_preview_asset_id_=drag.registry_id;
        if(ImGui::IsMouseReleased(ImGuiMouseButton_Left)&&mouse_in_canvas(io.MousePos.x,io.MousePos.y)){
            if(const auto picked=pick_cell(io.MousePos.x,io.MousePos.y)) {
                const auto* preset = catalog_.army_preset(drag.registry_id);
                if (preset != nullptr && preset->technology_level_previews.size() > 1U)
                    pending_technology_placement_ = PendingTechnologyPlacement{
                        picked->first,picked->second,drag.registry_id,
                        {io.MousePos.x,io.MousePos.y}};
                else
                    add_asset_placement(picked->first,picked->second,drag.registry_id,0U);
            }
            drag_preview_asset_id_.reset();
            return;
        }
    }

    if(!io.WantCaptureKeyboard&&!io.WantTextInput&&!ImGui::IsAnyItemActive()){
        if(!ctrl&&ImGui::IsKeyPressed(ImGuiKey_W,false))gizmo_mode_=GizmoMode::Translate;
        if(!ctrl&&ImGui::IsKeyPressed(ImGuiKey_E,false))gizmo_mode_=GizmoMode::Rotate;
        if(ImGui::IsKeyPressed(ImGuiKey_Delete,false)&&selected_placement_)delete_selected_placement();
        float right_input=0.0F;
        float forward_input=0.0F;
        if(ImGui::IsKeyDown(ImGuiKey_LeftArrow))right_input-=1.0F;
        if(ImGui::IsKeyDown(ImGuiKey_RightArrow))right_input+=1.0F;
        if(ImGui::IsKeyDown(ImGuiKey_UpArrow))forward_input+=1.0F;
        if(ImGui::IsKeyDown(ImGuiKey_DownArrow))forward_input-=1.0F;
        const float input_length=std::hypot(right_input,forward_input);
        if(input_length>0.0F){
            right_input/=input_length;
            forward_input/=input_length;
            float speed=std::max(4.0F,camera_.distance*0.55F)*io.DeltaTime;
            if(io.KeyShift)speed*=3.0F;
            if(io.KeyCtrl)speed*=0.25F;
            camera_.move_planar(right_input*speed,forward_input*speed);
            }
    }

    if(gizmo_drag_.handle!=GizmoHandle::None){
        if(ImGui::IsMouseDown(ImGuiMouseButton_Left))
            update_active_gizmo({io.MousePos.x,io.MousePos.y});
        if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))gizmo_drag_=GizmoDragState{};
        return;
    }

    gizmo_hovered_=mouse_in_canvas(io.MousePos.x,io.MousePos.y)?
        hit_test_gizmo({io.MousePos.x,io.MousePos.y}):GizmoHandle::None;
    if(gizmo_hovered_!=GizmoHandle::None&&ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
        checkpoint();
        gizmo_drag_.handle=gizmo_hovered_;
        gizmo_drag_.mouse_start={io.MousePos.x,io.MousePos.y};
        gizmo_drag_.mouse_previous=gizmo_drag_.mouse_start;
        gizmo_drag_.placement_start=document_->level().placements()[*selected_placement_];
        gizmo_drag_.projection_start=selected_gizmo_projection();
        gizmo_drag_.rotation_screen_angle_start=std::atan2(
            io.MousePos.y-gizmo_drag_.projection_start.origin.y,
            io.MousePos.x-gizmo_drag_.projection_start.origin.x);
        tool_=Tool::Select;
        return;
    }

    if(io.WantCaptureMouse||!mouse_in_canvas(io.MousePos.x,io.MousePos.y)){
        hovered_placement_.reset();hovered_terrain_point_.reset();
        hovered_context_order_=thandor::ContextOrderKind::None;
        if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))camera_turn_drag_active_=false;
        if(ImGui::IsMouseReleased(ImGuiMouseButton_Right)){
            right_pointer_pressed_=false;right_pointer_dragged_=false;
        }
        return;
    }

    const auto context_order=resolve_context_order_at_pointer(io.MousePos.x,io.MousePos.y);
    switch(context_order){
        case thandor::ContextOrderKind::MoveToTerrain:
            game_cursor_frame_=GameCursorAsset::kMoveOrderValidFrame;break;
        case thandor::ContextOrderKind::AttackTerrainPoint:
            game_cursor_frame_=GameCursorAsset::kPointTargetValidFrame;break;
        case thandor::ContextOrderKind::AttackPlacement:
            game_cursor_frame_=GameCursorAsset::kAttackPlacementValidFrame;break;
        case thandor::ContextOrderKind::SetProductionRallyPoint:
            game_cursor_frame_=GameCursorAsset::kMoveOrderValidFrame;break;
        case thandor::ContextOrderKind::None:break;
    }

    if(ImGui::IsMouseClicked(ImGuiMouseButton_Right)){
        right_pointer_pressed_=true;right_pointer_dragged_=false;
        right_pointer_press_={io.MousePos.x,io.MousePos.y};
    }
    if(right_pointer_pressed_&&ImGui::IsMouseDown(ImGuiMouseButton_Right)){
        const float drag_distance=std::hypot(io.MousePos.x-right_pointer_press_.x,
                                             io.MousePos.y-right_pointer_press_.y);
        if(drag_distance>=4.0F)right_pointer_dragged_=true;
    }
    if(right_pointer_dragged_&&ImGui::IsMouseDown(ImGuiMouseButton_Right)){
        game_cursor_frame_=GameCursorAsset::kCameraMoveFrame;
        const float scale=game_camera_pan_world_units_per_pixel(camera_.distance);
        camera_.move_planar(-io.MouseDelta.x*scale,io.MouseDelta.y*scale);
        return;
    }
    if(ImGui::IsMouseReleased(ImGuiMouseButton_Right)&&right_pointer_pressed_){
        const bool issue_order=!right_pointer_dragged_;
        right_pointer_pressed_=false;right_pointer_dragged_=false;
        if(issue_order){issue_context_order_at_pointer(io.MousePos.x,io.MousePos.y);return;}
    }
    if(ImGui::IsMouseDragging(ImGuiMouseButton_Middle)){
        game_cursor_frame_=GameCursorAsset::kCameraMoveFrame;
        const float scale=game_camera_pan_world_units_per_pixel(camera_.distance);
        camera_.move_planar(-io.MouseDelta.x*scale,io.MouseDelta.y*scale);
        return;
    }
    if(tool_==Tool::Select&&ImGui::IsMouseDragging(ImGuiMouseButton_Left,4.0F)){
        game_cursor_frame_=GameCursorAsset::kCameraTurnFrame;
        camera_turn_drag_active_=true;
        constexpr float turn_scale=game_camera_turn_radians_per_pixel();
        camera_.orbit(-io.MouseDelta.x*turn_scale,io.MouseDelta.y*turn_scale);
        return;
    }
    if(ImGui::IsMouseReleased(ImGuiMouseButton_Left)&&camera_turn_drag_active_){
        camera_turn_drag_active_=false;
        return;
    }
    if(io.MouseWheel!=0.0F){
        if(tool_==Tool::Water&&water_selection_stage_==WaterSelectionStage::AdjustLevel&&
           water_selection_count()!=0U){
            adjust_water_selection_level(io.MouseWheel*water_level_step_);
        }else{
            camera_.distance=std::clamp(camera_.distance*std::pow(0.88F,io.MouseWheel),2.0F,500.0F);
            }
    }

    // Object selection is independent of the terrain ray. Large buildings are selected through
    // their complete resolved-model bounds, including roofs and linked children, rather than a
    // small screen-space circle around the placement root.
    if(tool_==Tool::Select&&ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
        selected_placement_=pick_placement(io.MousePos.x,io.MousePos.y);
        return;
    }
    if((tool_==Tool::SimulationTarget||tool_==Tool::SimulationMove)&&
       ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
        const auto ray=make_view_ray(camera_,io.MousePos.x,io.MousePos.y,
            io.DisplaySize.x,io.DisplaySize.y);
        const auto hit=pick_terrain_cell(document_->field(),ray);
        if(!hit){status_="Simulation order missed the terrain.";return;}
        if(tool_==Tool::SimulationTarget){
            bool assigned_selected=false;
            if(selected_placement_&&*selected_placement_<document_->level().placements().size()){
                const auto& selected=document_->level().placements()[*selected_placement_];
                if(const auto* preset=catalog_.army_preset(selected.army_asset_id);
                   preset&&!preset->simulation_weapon_profiles.empty()){
                    simulation_attack_point_targets_[*selected_placement_]=hit->position;
                    simulation_attack_placement_targets_.erase(*selected_placement_);
                    auto& actor=simulation_state_for(*selected_placement_);
                    actor.move_target.reset();
                    if(preset->browser_kind==thandor::PlacementBrowserKind::Building)animate_buildings_=true;
                    if(preset->browser_kind==thandor::PlacementBrowserKind::Unit)animate_units_=true;
                    assigned_selected=true;
                }
            }
            if(!assigned_selected)simulation_target_point_=hit->position;
            simulation_mode_=true;
            status_=assigned_selected?
                "Selected armed placement received a point-target order.":
                "Global simulation target placed for eligible armed actors.";
        }else{
            if(!selected_placement_||*selected_placement_>=document_->level().placements().size()){
                status_="Select a movable unit before issuing a drive order.";return;
            }
            const auto& placement=document_->level().placements()[*selected_placement_];
            const auto* preset=catalog_.army_preset(placement.army_asset_id);
            if(!preset||!preset->simulation_motion_profile.movable()){
                status_="The selected placement has no recovered movement-speed profile.";return;
            }
            checkpoint();
            auto& actor=simulation_state_for(*selected_placement_);
            actor.move_target=hit->position;
            simulation_mode_=true;animate_units_=true;
            status_="Selected unit is driving; its LEV position now follows every 20 Hz movement step.";
        }
        last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
        tool_=Tool::Select;
        return;
    }

    const auto picked=pick_cell(io.MousePos.x,io.MousePos.y);
    if(!picked){
        // A transient miss at the map edge must not terminate a held brush stroke. Keep the stroke
        // armed until the button is released so moving back onto terrain resumes immediately.
        if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))stroke_active_=false;
        return;
    }
    const auto [x,y]=*picked;
    if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
        if(tool_==Tool::Water&&water_selection_stage_==WaterSelectionStage::PickReference){
            ensure_water_selection_size();
            const std::size_t index=static_cast<std::size_t>(y)*document_->field().width()+x;
            if(index>=water_selection_mask_.size()||water_selection_mask_[index]==0U){
                status_="Pick the initial level from inside the temporary cyan water selection.";
                return;
            }
            water_reference_cell_=std::pair<uint32_t,uint32_t>{x,y};
            water_height_=document_->field().at(x,y).terrain_height();
            water_selection_stage_=WaterSelectionStage::AdjustLevel;
            apply_flat_water_selection(true);
            status_="Initial flat water level sampled from selected terrain. Use the wheel or Up/Down buttons to adjust it.";
            return;
        }
        if(tool_==Tool::Resource)add_placement(x,y,thandor::PlacementRole::Resource);
        else if(tool_==Tool::PlayerStart)add_placement(x,y,thandor::PlacementRole::PlayerStart);
        else if(tool_==Tool::EffectScenery)add_placement(x,y,thandor::PlacementRole::EffectOrScenery);
        else if(tool_==Tool::Water&&water_selection_stage_==WaterSelectionStage::SelectArea){
            stroke_active_=true;
            paint_water_selection(x,y,!io.KeyShift);
        }else if(tool_!=Tool::Water){
            checkpoint();
            stroke_active_=true;
        }
    }
    if(ImGui::IsMouseDown(ImGuiMouseButton_Left)&&stroke_active_){
        if(tool_==Tool::Water&&water_selection_stage_==WaterSelectionStage::SelectArea){
            paint_water_selection(x,y,!io.KeyShift);
        }else{
            auto& field=document_->field();
            bool modified=true;
            const float stroke_seconds=std::max(io.DeltaTime,
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)?(1.0F/60.0F):0.0F);
            switch(tool_){
                case Tool::Raise:field.raise_lower(x,y,brush_,2.5F*stroke_seconds,terrain_water_policy_);break;
                case Tool::Lower:field.raise_lower(x,y,brush_,-2.5F*stroke_seconds,terrain_water_policy_);break;
                case Tool::Smooth:field.smooth(x,y,brush_,1,terrain_water_policy_);break;
                case Tool::Flatten:field.flatten(x,y,brush_,flatten_height_,terrain_water_policy_);break;
                case Tool::ResourceField:{bool erase=resource_field_erase_mode_;if(io.KeyShift)erase=!erase;field.paint_placement_occupancy_class(x,y,brush_,resource_field_occupancy_class_,!erase);break;}
                case Tool::Texture:field.paint_material(x,y,brush_,selected_material_);break;
                default:modified=false;break;
            }
            if(modified){document_->mark_dirty();rebuild_mesh();}
        }
    }
    if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))stroke_active_=false;
}

std::optional<std::pair<uint32_t,uint32_t>> EditorApp::pick_cell(float mouse_x,float mouse_y) const{
    if(!document_)return std::nullopt;
    const auto io=ImGui::GetIO();
    const auto ray=make_view_ray(camera_,mouse_x,mouse_y,io.DisplaySize.x,io.DisplaySize.y);
    const auto hit=pick_terrain_cell(document_->field(),ray);
    if(!hit)return std::nullopt;
    return std::pair<uint32_t,uint32_t>{hit->cell_x,hit->cell_y};
}

std::optional<std::size_t> EditorApp::pick_placement(float mouse_x,float mouse_y) const{
    if(!document_)return std::nullopt;
    const auto placements=document_->level().placements();
    if(placement_pick_bounds_.size()!=placements.size())return std::nullopt;
    const auto io=ImGui::GetIO();
    const auto ray=make_view_ray(camera_,mouse_x,mouse_y,io.DisplaySize.x,io.DisplaySize.y);
    float nearest=std::numeric_limits<float>::max();
    std::optional<std::size_t> selected;
    for(std::size_t index=0;index<placement_pick_bounds_.size();++index){
        const auto broad_distance=ray_bounds_distance(ray,placement_pick_bounds_[index]);
        if(!broad_distance||*broad_distance>nearest)continue;
        float hierarchy_nearest=std::numeric_limits<float>::max();
        bool hierarchy_hit=false;
        if(index<placement_pick_hulls_.size()){
            for(const auto& hull:placement_pick_hulls_[index]){
                const auto distance=ray_transformed_bounds_distance(ray,hull);
                if(distance&&*distance<hierarchy_nearest){
                    hierarchy_nearest=*distance;
                    hierarchy_hit=true;
                }
            }
        }
        if(hierarchy_hit&&hierarchy_nearest<nearest){nearest=hierarchy_nearest;selected=index;}
    }
    return selected;
}

Vec3f EditorApp::placement_command_target_point(std::size_t placement_index) const{
    if(!document_||placement_index>=document_->level().placements().size())return {};
    const auto& placement=document_->level().placements()[placement_index];
    const Vec3f static_root{placement.render_x(),placement.render_y(),
        terrain_height_near(placement.render_x(),placement.render_y())};
    Vec3f runtime_root=static_root;
    if(const auto found=simulation_actor_states_.find(placement_index);
       found!=simulation_actor_states_.end())runtime_root=found->second.position;
    if(placement_index<placement_pick_bounds_.size()&&placement_pick_bounds_[placement_index].valid()){
        const auto& bounds=placement_pick_bounds_[placement_index];
        const Vec3f static_center{(bounds.minimum.x+bounds.maximum.x)*0.5F,
                                  (bounds.minimum.y+bounds.maximum.y)*0.5F,
                                  (bounds.minimum.z+bounds.maximum.z)*0.5F};
        return add(runtime_root,sub(static_center,static_root));
    }
    return add(runtime_root,{0.0F,0.0F,0.5F});
}

std::optional<Vec3f> EditorApp::simulation_attack_target_point(std::size_t attacker_index) const{
    if(!document_)return std::nullopt;
    const auto placements=document_->level().placements();
    if(const auto target=simulation_attack_placement_targets_.find(attacker_index);
       target!=simulation_attack_placement_targets_.end()){
        if(attacker_index<placements.size()&&target->second<placements.size()&&
           target->second!=attacker_index&&
           placements[target->second].faction_index!=placements[attacker_index].faction_index)
            return placement_command_target_point(target->second);
    }
    if(const auto target=simulation_attack_point_targets_.find(attacker_index);
       target!=simulation_attack_point_targets_.end())return target->second;
    return std::nullopt;
}

thandor::ContextOrderKind EditorApp::resolve_context_order_at_pointer(float mouse_x,float mouse_y){
    hovered_placement_.reset();
    hovered_terrain_point_.reset();
    hovered_context_order_=thandor::ContextOrderKind::None;
    if(!document_||tool_!=Tool::Select||!selected_placement_||
       *selected_placement_>=document_->level().placements().size())return hovered_context_order_;

    const auto placements=document_->level().placements();
    const auto selected_index=*selected_placement_;
    const auto& selected=placements[selected_index];
    const auto* preset=catalog_.army_preset(selected.army_asset_id);
    if(!preset)return hovered_context_order_;

    const auto io=ImGui::GetIO();
    const auto ray=make_view_ray(camera_,mouse_x,mouse_y,io.DisplaySize.x,io.DisplaySize.y);
    if(const auto picked=pick_placement(mouse_x,mouse_y);picked&&*picked!=selected_index)
        hovered_placement_=picked;
    if(const auto hit=pick_terrain_cell(document_->field(),ray))hovered_terrain_point_=hit->position;

    std::optional<uint32_t> hovered_faction;
    if(hovered_placement_&&*hovered_placement_<placements.size())
        hovered_faction=placements[*hovered_placement_].faction_index;
    const bool production_rally=preset->browser_kind==thandor::PlacementBrowserKind::Building&&
        (preset->technology_branch_count==3U||preset->technology_branch_count==4U);
    const thandor::ContextOrderActorCapabilities capabilities{
        preset->simulation_motion_profile.movable(),
        !preset->simulation_weapon_profiles.empty(),
        preset->browser_kind==thandor::PlacementBrowserKind::Building,
        production_rally,selected.faction_index};
    hovered_context_order_=thandor::resolve_context_order(
        capabilities,hovered_faction,hovered_terrain_point_.has_value());
    return hovered_context_order_;
}

void EditorApp::issue_context_order_at_pointer(float mouse_x,float mouse_y){
    const auto order=resolve_context_order_at_pointer(mouse_x,mouse_y);
    if(!document_||!selected_placement_||
       *selected_placement_>=document_->level().placements().size())return;
    const auto selected_index=*selected_placement_;
    const auto& placement=document_->level().placements()[selected_index];
    const auto* preset=catalog_.army_preset(placement.army_asset_id);
    if(!preset)return;

    switch(order){
        case thandor::ContextOrderKind::AttackPlacement:{
            if(!hovered_placement_)return;
            simulation_attack_placement_targets_[selected_index]=*hovered_placement_;
            simulation_attack_point_targets_.erase(selected_index);
            auto& actor=simulation_state_for(selected_index);
            actor.move_target.reset();
            simulation_mode_=true;
            if(preset->browser_kind==thandor::PlacementBrowserKind::Building)animate_buildings_=true;
            if(preset->browser_kind==thandor::PlacementBrowserKind::Unit)animate_units_=true;
            const auto* target_preset=catalog_.army_preset(
                document_->level().placements()[*hovered_placement_].army_asset_id);
            status_="Attack target assigned to "+
                (target_preset?target_preset->display_name:std::string("placement ")+std::to_string(*hovered_placement_))+".";
            break;
        }
        case thandor::ContextOrderKind::MoveToTerrain:{
            if(!hovered_terrain_point_)return;
            checkpoint();
            auto& actor=simulation_state_for(selected_index);
            actor.move_target=*hovered_terrain_point_;
            simulation_attack_placement_targets_.erase(selected_index);
            simulation_attack_point_targets_.erase(selected_index);
            simulation_mode_=true;animate_units_=true;
            status_="Selected unit received a game-style drive destination.";
            break;
        }
        case thandor::ContextOrderKind::AttackTerrainPoint:{
            if(!hovered_terrain_point_)return;
            simulation_attack_point_targets_[selected_index]=*hovered_terrain_point_;
            simulation_attack_placement_targets_.erase(selected_index);
            auto& actor=simulation_state_for(selected_index);
            actor.move_target.reset();
            simulation_mode_=true;
            if(preset->browser_kind==thandor::PlacementBrowserKind::Building)animate_buildings_=true;
            if(preset->browser_kind==thandor::PlacementBrowserKind::Unit)animate_units_=true;
            status_="Selected armed placement received a point-target order.";
            break;
        }
        case thandor::ContextOrderKind::SetProductionRallyPoint:{
            if(!hovered_terrain_point_)return;
            simulation_production_rally_points_[selected_index]=*hovered_terrain_point_;
            status_="Production drive/rally destination assigned.";
            break;
        }
        case thandor::ContextOrderKind::None:
            status_="No valid contextual order under the pointer.";
            return;
    }
    last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
}

void EditorApp::clear_selected_simulation_order(){
    if(!selected_placement_)return;
    const auto index=*selected_placement_;
    simulation_attack_placement_targets_.erase(index);
    simulation_attack_point_targets_.erase(index);
    simulation_production_rally_points_.erase(index);
    if(const auto found=simulation_actor_states_.find(index);found!=simulation_actor_states_.end())
        found->second.move_target.reset();
    status_="Cleared the selected placement's transient movement/attack order.";
    last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
}

float EditorApp::terrain_height_near(float world_x,float world_y) const{
    return document_?document_->field().sample_terrain(world_x,world_y).height:0.0F;
}

PlacementContactPose EditorApp::placement_contact_pose(
    const thandor::ArmyAssetPreset& preset,float world_x,float world_y,
    float sprite_height_offset) const{
    if(!document_)return {};
    const auto& field=document_->field();
    const auto terrain=field.sample_terrain(world_x,world_y);
    auto pose=resolve_placement_contact_pose(
        preset.simulation_motion_profile.placement_contact_kind,terrain,
        field.sample_water_surface_height(world_x,world_y),
        field.sample_top_surface_height(world_x,world_y),sprite_height_offset);
    if(preset.simulation_motion_profile.placement_contact_kind==3)
        pose.height+=thandor::q12_to_float(preset.articulated_root_height_q12);
    return pose;
}

bool EditorApp::mouse_in_canvas(float mouse_x,float mouse_y) const{
    const auto io=ImGui::GetIO();
    return mouse_x>=kLeftPanelWidth&&mouse_x<=io.DisplaySize.x-kRightPanelWidth&&
           mouse_y>=ImGui::GetFrameHeight()&&mouse_y<=io.DisplaySize.y-kStatusBarHeight;
}

AssetPreviewMesh& EditorApp::asset_preview(const thandor::ArmyAssetPreset& preset){
    const bool neutral=thandor::placement_uses_neutral_source_color(preset);
    const uint32_t preview_faction=neutral?0U:
        thandor::default_faction_graphics_family(selected_faction_);
    const uint64_t cache_key=static_cast<uint64_t>(preview_faction)<<32U|preset.registry_id;
    if(const auto found=asset_preview_cache_.find(cache_key);found!=asset_preview_cache_.end())return found->second;

    AssetPreviewMesh preview{};
    if(!document_||preset.sprite_path.empty()||preset.visual_kind==thandor::PlacementVisualKind::PlayerStart){
        preview.marker_only=true;
        return asset_preview_cache_.emplace(cache_key,std::move(preview)).first->second;
    }

    const std::string army_base=document_->level().paths().army_textures;
    const auto texture_family=[&](uint32_t faction)->const ArmyTextureFamily&{
        const uint32_t clamped=std::min(faction,8U);
        const std::string key=terrain_base_path(army_base)+"#"+std::to_string(clamped);
        auto found=army_texture_cache_.find(key);
        if(found==army_texture_cache_.end()){
            auto family=std::make_shared<ArmyTextureFamily>(ArmyTextureFamily{
                thandor::GfxAsset::parse(catalog_.load(army_texture_family_path(army_base,clamped,".gfx"))),
                thandor::PalAsset::parse(catalog_.load(army_texture_family_path(army_base,clamped,".pal")))});
            found=army_texture_cache_.emplace(key,std::move(family)).first;
        }
        return *found->second;
    };
    const auto& family=texture_family(preview_faction);
    const auto texture_image=[&](uint32_t subresource)->const thandor::RgbaImage&{
        const uint64_t key=static_cast<uint64_t>(preview_faction)<<32U|subresource;
        auto found=asset_preview_texture_cache_.find(key);
        if(found==asset_preview_texture_cache_.end()){
            if(subresource>=family.graphics.subresources().size())
                throw std::runtime_error("asset-card SPR selects missing army GFX subresource");
            found=asset_preview_texture_cache_.emplace(key,thandor::make_sprite_texture_square_canvas(
                family.graphics.decode(thandor::GraphicsSubresourceIndex{subresource},false))).first;
        }
        return found->second;
    };
    const auto load_sprite=[&](const std::string& path)->const thandor::SpriteAsset&{
        auto found=sprite_cache_.find(path);
        if(found==sprite_cache_.end())
            found=sprite_cache_.emplace(path,thandor::SpriteAsset::parse(catalog_.load(path))).first;
        return found->second;
    };

    std::vector<thandor::ModelVisualNode> fallback_nodes;
    std::span<const thandor::ModelVisualNode> hierarchy = preset.resolved_model_hierarchy.empty()
        ? std::span<const thandor::ModelVisualNode>(preset.model_hierarchy)
        : std::span<const thandor::ModelVisualNode>(preset.resolved_model_hierarchy);
    if(hierarchy.empty()){
        thandor::ModelVisualNode fallback;
        fallback.parent_index=std::numeric_limits<uint32_t>::max();
        fallback.local_rotation_angle1=0x4000U;
        fallback.sprite_path=preset.sprite_path;
        fallback_nodes.push_back(std::move(fallback));
        hierarchy=fallback_nodes;
    }
    std::vector<ModelHierarchyInstance> instances;
    std::vector<bool> active;
    instances.reserve(hierarchy.size());
    active.reserve(hierarchy.size());
    for(std::size_t node_index=0;node_index<hierarchy.size();++node_index){
        const auto& node=hierarchy[node_index];
        ModelHierarchyInstance instance{};
        instance.node=&node;
        instance.root=node.parent_index==std::numeric_limits<uint32_t>::max();
        bool node_active=node.creates_runtime_node()&&!node.sprite_path.empty();
        if(instance.root){
            instance.transform=placement_root_transform(0.0F,0.0F,0.0F,0.0F);
        }else{
            if(node.parent_index>=instances.size()||!active[node.parent_index])node_active=false;
            if(node_active){
                const auto& parent=instances[node.parent_index];
                const auto* attachment=parent.sprite->child_attachment(node.child_slot);
                if(attachment==nullptr)
                    throw std::runtime_error("MDL child slot has no matching SPR attachment transform");
                auto local=fixed_model_rotation_basis(node.local_rotation_angle2,
                    node.local_rotation_angle1,node.local_rotation_angle0);
                local.translation=attachment->local_translation;
                instance.transform=compose_model_transform(parent.transform,local);
            }
        }
        if(node_active)instance.sprite=&load_sprite(node.sprite_path);
        active.push_back(node_active);
        instances.push_back(instance);
    }
    apply_walker_pose_to_instances(preset,hierarchy,instances,nullptr,
        [](float,float){return 0.0F;});

    std::size_t total_source_triangles=0U;
    for(std::size_t index=0;index<instances.size();++index){
        if(!active[index])continue;
        const auto& sprite=*instances[index].sprite;
        total_source_triangles+=sprite.mesh().indices.size()/3U;
    }
    preview.texture_subdivision=1U;
    preview.triangles.reserve(total_source_triangles);

    thandor::ResourceEnergyKind resource_kind=thandor::ResourceEnergyKind::None;
    if(preset.visual_kind==thandor::PlacementVisualKind::PowerResource)
        resource_kind=thandor::ResourceEnergyKind::Power;
    else if(preset.visual_kind==thandor::PlacementVisualKind::BuildingResource)
        resource_kind=thandor::ResourceEnergyKind::Building;
    const uint32_t particle_subresource=thandor::resource_luminous_particle_subresource(resource_kind);
    const Vec3f light=normalize(Vec3f{-0.38F,-0.46F,0.80F});

    for(std::size_t instance_index=0;instance_index<instances.size();++instance_index){
        if(!active[instance_index])continue;
        const auto& instance=instances[instance_index];
        const auto& sprite=*instance.sprite;
        const auto& mesh=sprite.mesh();
        if(mesh.indices.size()%3U!=0U)throw std::runtime_error("SPR preview mesh is not triangle aligned");
        for(std::size_t triangle=0;triangle<mesh.indices.size();triangle+=3U){
            const uint32_t ia=mesh.indices[triangle];
            const uint32_t ib=mesh.indices[triangle+1U];
            const uint32_t ic=mesh.indices[triangle+2U];
            if(ia>=mesh.vertices.size()||ib>=mesh.vertices.size()||ic>=mesh.vertices.size())
                throw std::runtime_error("asset-card SPR index exceeds vertex stream");
            const std::array<const thandor::SpriteMeshVertex*,3> corners{
                &mesh.vertices[ia],&mesh.vertices[ib],&mesh.vertices[ic]};
            ++preview.source_triangle_count;
            const uint32_t subresource=corners[0]->texture_subresource;
            const bool untextured=subresource==std::numeric_limits<uint32_t>::max();
            const bool luminous=resource_kind!=thandor::ResourceEnergyKind::None&&subresource==particle_subresource;
            Vec3f transformed_normal{};
            for(const auto* corner:corners)transformed_normal=add(transformed_normal,
                transform_vector(instance.transform,corner->normal));
            const Vec3f normal=normalize(transformed_normal);
            const float brightness=luminous?1.15F:(0.45F+0.55F*std::max(0.0F,dot(normal,light)));
            const std::array<Vec3f,3> source_positions{
                transform_point(instance.transform,corners[0]->position),
                transform_point(instance.transform,corners[1]->position),
                transform_point(instance.transform,corners[2]->position)};

            if(untextured){
                uint32_t color=0xFFB8BEC6U;
                const uint32_t palette_index=sprite_palette_index(corners[0]->render_flags);
                if(palette_index<family.palette.colors_argb().size())
                    color=family.palette.colors_argb()[palette_index]|0xFF000000U;
                AssetPreviewTriangle output{};
                output.positions=source_positions;
                output.packed_color_argb=color;
                output.brightness=brightness;
                preview.triangles.push_back(output);
                for(const auto point:output.positions)include_preview_bounds(preview,point);
                continue;
            }

            if(subresource>=family.graphics.subresources().size())
                throw std::runtime_error("asset-card SPR selects missing army GFX subresource");
            ++preview.textured_source_triangle_count;
            const auto& metadata=family.graphics.subresources()[subresource];
            texture_image(subresource);
            std::array<PreviewVec2,3> source_uv{};
            for(std::size_t corner=0;corner<3U;++corner){
                const auto uv=thandor::normalize_sprite_texture_uv(corners[corner]->texture_u,
                    corners[corner]->texture_v,metadata.pixel_width,metadata.pixel_height);
                source_uv[corner]={uv.u,uv.v};
            }
            AssetPreviewTriangle output{};
            output.positions=source_positions;
            output.texture_uv=source_uv;
            output.texture_subresource=subresource;
            output.brightness=brightness;
            output.alpha_scale=luminous?0.92F:1.0F;
            output.textured=true;
            output.additive=luminous;
            preview.triangles.push_back(output);
            ++preview.generated_texture_triangle_count;
            for(const auto point:output.positions)include_preview_bounds(preview,point);
        }
    }
    preview.marker_only=preview.triangles.empty();
    if(!preview.marker_only){
        rasterize_asset_preview(preview,168U,124U,
            [&](uint32_t subresource,float u,float v){
                return sample_wrapped_bilinear_argb(texture_image(subresource),u,v);
            });
    }
    return asset_preview_cache_.emplace(cache_key,std::move(preview)).first->second;
}

void EditorApp::draw_placement_markers(){
    // Retail transient world-marker families recovered from the executable/PCK pipeline:
    // EFF 0x8F = production/gather drive marker (egath0.spr), EFF 0x94 = waypoint/move marker
    // (ewayp0.spr), and EFF 0x95 = target/attack marker (etarg0.spr). The editor projects their
    // semantic roles as an additional overlay while retaining the XYZ authoring gizmo.
    if(!document_)return;
    const auto io=ImGui::GetIO();
    const auto vp=camera_.view_projection(io.DisplaySize.x/std::max(io.DisplaySize.y,1.0f));
    auto* draw=ImGui::GetBackgroundDrawList();
    const auto placements=document_->level().placements();
    for(std::size_t i=0;i<placements.size();++i){
        const auto& placement=placements[i];
        const auto* preset=catalog_.army_preset(placement.army_asset_id);
        float sx,sy,depth;
        const float z=terrain_height_near(placement.render_x(),placement.render_y());
        if(!project_to_screen({placement.render_x(),placement.render_y(),z+0.08F},vp,io.DisplaySize.x,io.DisplaySize.y,sx,sy,depth))continue;
        const float radius=selected_placement_==i?9.0F:6.0F;
        if(selected_placement_==i&&i<placement_pick_bounds_.size()&&
           placement_pick_bounds_[i].valid()){
            const auto& bounds=placement_pick_bounds_[i];
            float min_x=std::numeric_limits<float>::max(),min_y=std::numeric_limits<float>::max();
            float max_x=std::numeric_limits<float>::lowest(),max_y=std::numeric_limits<float>::lowest();
            bool any=false;
            for(const float bx:{bounds.minimum.x,bounds.maximum.x})
                for(const float by:{bounds.minimum.y,bounds.maximum.y})
                    for(const float bz:{bounds.minimum.z,bounds.maximum.z}){
                        float px,py,pd;
                        if(!project_to_screen({bx,by,bz},vp,io.DisplaySize.x,io.DisplaySize.y,px,py,pd))continue;
                        any=true;min_x=std::min(min_x,px);min_y=std::min(min_y,py);
                        max_x=std::max(max_x,px);max_y=std::max(max_y,py);
                    }
            if(any){
                const ImU32 selection=IM_COL32(75,255,105,245);
                const float corner=std::clamp(std::min(max_x-min_x,max_y-min_y)*0.22F,7.0F,24.0F);
                draw->AddLine({min_x,min_y},{min_x+corner,min_y},selection,2.0F);
                draw->AddLine({min_x,min_y},{min_x,min_y+corner},selection,2.0F);
                draw->AddLine({max_x,min_y},{max_x-corner,min_y},selection,2.0F);
                draw->AddLine({max_x,min_y},{max_x,min_y+corner},selection,2.0F);
                draw->AddLine({min_x,max_y},{min_x+corner,max_y},selection,2.0F);
                draw->AddLine({min_x,max_y},{min_x,max_y-corner},selection,2.0F);
                draw->AddLine({max_x,max_y},{max_x-corner,max_y},selection,2.0F);
                draw->AddLine({max_x,max_y},{max_x,max_y-corner},selection,2.0F);
            }
        }
        if(preset&&preset->visual_kind==thandor::PlacementVisualKind::PlayerStart){
            const ImU32 color=IM_COL32(80+25*(placement.faction_index%6),220-18*(placement.faction_index%6),255,245);
            draw->AddQuadFilled({sx,sy-radius},{sx+radius,sy},{sx,sy+radius},{sx-radius,sy},color);
            const std::string label="P"+std::to_string(placement.faction_index);
            draw->AddText({sx+radius+2.0F,sy-radius},IM_COL32_WHITE,label.c_str());
        }else if(preset&&(preset->visual_kind==thandor::PlacementVisualKind::PowerResource||
                          preset->visual_kind==thandor::PlacementVisualKind::BuildingResource)){
            // The visible deposit body and glow are rendered in Vulkan. Keep only a selection
            // ring here so resources are not represented by a permanent blue/gold dot.
            if(selected_placement_==i){
                const bool power=preset->visual_kind==thandor::PlacementVisualKind::PowerResource;
                const ImU32 color=power?IM_COL32(65,155,255,235):IM_COL32(255,190,45,235);
                draw->AddCircle({sx,sy},radius,color,20,2.0F);
            }
        }else{
            const ImU32 color=placement.faction_index==0?IM_COL32(235,190,60,230):
                IM_COL32(70+30*(placement.faction_index%5),220-20*(placement.faction_index%5),255,240);
            const bool unresolved=preset==nullptr||preset->sprite_path.empty();
            if(unresolved){
                draw->AddCircleFilled({sx,sy},radius,color);
            }else if(selected_placement_==i){
                draw->AddCircle({sx,sy},radius,color,20,2.0F);
            }else{
                continue;
            }
            const std::string label=preset&&!preset->short_name.empty()?preset->short_name:
                ("ID "+std::to_string(placement.army_asset_id));
            draw->AddText({sx+radius+2.0F,sy-radius},IM_COL32_WHITE,label.c_str());
        }
    }
    if(simulation_target_point_){
        float sx,sy,depth;
        if(project_to_screen(add(*simulation_target_point_,{0.0F,0.0F,0.12F}),vp,
            io.DisplaySize.x,io.DisplaySize.y,sx,sy,depth)){
            const ImU32 color=IM_COL32(255,72,72,245);
            draw->AddCircle({sx,sy},13.0F,color,24,2.5F);
            draw->AddLine({sx-17.0F,sy},{sx+17.0F,sy},color,2.0F);
            draw->AddLine({sx,sy-17.0F},{sx,sy+17.0F},color,2.0F);
            draw->AddText({sx+14.0F,sy-15.0F},IM_COL32_WHITE,"Simulation target");
        }
    }
    const auto draw_attack_marker=[&](Vec3f point,const char* label,ImU32 color){
        float sx,sy,depth;
        if(!project_to_screen(add(point,{0.0F,0.0F,0.12F}),vp,
            io.DisplaySize.x,io.DisplaySize.y,sx,sy,depth))return;
        draw->AddCircle({sx,sy},12.0F,color,24,2.5F);
        draw->AddLine({sx-16.0F,sy},{sx-5.0F,sy},color,2.0F);
        draw->AddLine({sx+5.0F,sy},{sx+16.0F,sy},color,2.0F);
        draw->AddLine({sx,sy-16.0F},{sx,sy-5.0F},color,2.0F);
        draw->AddLine({sx,sy+5.0F},{sx,sy+16.0F},color,2.0F);
        if(label)draw->AddText({sx+14.0F,sy-15.0F},IM_COL32_WHITE,label);
    };
    const auto actor_position=[&](std::size_t index){
        if(const auto found=simulation_actor_states_.find(index);found!=simulation_actor_states_.end())
            return found->second.position;
        const auto& placement=placements[index];
        return Vec3f{placement.render_x(),placement.render_y(),
            terrain_height_near(placement.render_x(),placement.render_y())};
    };
    for(const auto& [attacker,target]:simulation_attack_placement_targets_){
        if(attacker>=placements.size()||target>=placements.size())continue;
        const Vec3f from=actor_position(attacker);
        const Vec3f to=placement_command_target_point(target);
        float ax,ay,ad,bx,by,bd;
        if(project_to_screen(from,vp,io.DisplaySize.x,io.DisplaySize.y,ax,ay,ad)&&
           project_to_screen(to,vp,io.DisplaySize.x,io.DisplaySize.y,bx,by,bd))
            draw->AddLine({ax,ay},{bx,by},IM_COL32(255,75,55,190),2.0F);
        draw_attack_marker(to,"Attack entity",IM_COL32(255,70,55,245));
    }
    for(const auto& [attacker,target]:simulation_attack_point_targets_){
        if(attacker>=placements.size())continue;
        const Vec3f from=actor_position(attacker);
        float ax,ay,ad,bx,by,bd;
        if(project_to_screen(from,vp,io.DisplaySize.x,io.DisplaySize.y,ax,ay,ad)&&
           project_to_screen(target,vp,io.DisplaySize.x,io.DisplaySize.y,bx,by,bd))
            draw->AddLine({ax,ay},{bx,by},IM_COL32(255,150,55,190),2.0F);
        draw_attack_marker(target,"Attack point",IM_COL32(255,145,45,245));
    }
    if(hovered_context_order_==thandor::ContextOrderKind::AttackPlacement&&hovered_placement_&&
       *hovered_placement_<placements.size()){
        draw_attack_marker(placement_command_target_point(*hovered_placement_),
            "Right-click: attack",IM_COL32(255,55,45,255));
    }else if(hovered_context_order_==thandor::ContextOrderKind::AttackTerrainPoint&&hovered_terrain_point_){
        draw_attack_marker(*hovered_terrain_point_,"Right-click: aim",IM_COL32(255,155,45,255));
    }else if(hovered_context_order_==thandor::ContextOrderKind::SetProductionRallyPoint&&hovered_terrain_point_){
        draw_attack_marker(*hovered_terrain_point_,"Right-click: production drive point",
            IM_COL32(80,235,135,255));
    }else if(hovered_context_order_==thandor::ContextOrderKind::MoveToTerrain&&hovered_terrain_point_){
        float sx,sy,depth;
        if(project_to_screen(add(*hovered_terrain_point_,{0.0F,0.0F,0.12F}),vp,
            io.DisplaySize.x,io.DisplaySize.y,sx,sy,depth)){
            const ImU32 color=IM_COL32(65,235,145,255);
            draw->AddCircle({sx,sy},10.0F,color,20,2.5F);
            draw->AddLine({sx-16.0F,sy},{sx+16.0F,sy},color,2.0F);
            draw->AddLine({sx,sy-16.0F},{sx,sy+16.0F},color,2.0F);
            draw->AddText({sx+12.0F,sy-14.0F},IM_COL32_WHITE,"Right-click: drive");
        }
    }
    for(const auto& [producer,target]:simulation_production_rally_points_){
        if(producer>=placements.size())continue;
        const Vec3f from=actor_position(producer);
        float ax,ay,ad,bx,by,bd;
        if(project_to_screen(from,vp,io.DisplaySize.x,io.DisplaySize.y,ax,ay,ad)&&
           project_to_screen(target,vp,io.DisplaySize.x,io.DisplaySize.y,bx,by,bd))
            draw->AddLine({ax,ay},{bx,by},IM_COL32(80,235,135,205),2.0F);
        draw_attack_marker(target,"Production drive point",IM_COL32(80,235,135,245));
    }
    if(simulation_show_ranges_&&selected_placement_&&
       *selected_placement_<placements.size()){
        const auto index=*selected_placement_;
        const auto* preset=catalog_.army_preset(placements[index].army_asset_id);
        if(preset&&!preset->simulation_weapon_profiles.empty()){
            Vec3f center{placements[index].render_x(),placements[index].render_y(),
                terrain_height_near(placements[index].render_x(),placements[index].render_y())};
            if(const auto found=simulation_actor_states_.find(index);found!=simulation_actor_states_.end())
                center=found->second.position;
            const int32_t maximum_range_q12=std::max_element(
                preset->simulation_weapon_profiles.begin(),preset->simulation_weapon_profiles.end(),
                [](const auto& left,const auto& right){return left.maximum_range_q12<right.maximum_range_q12;})
                ->maximum_range_q12;
            const float radius=thandor::q12_to_float(maximum_range_q12);
            constexpr int segments=64;
            std::optional<ImVec2> previous;
            for(int segment=0;segment<=segments;++segment){
                const float angle=static_cast<float>(segment)*(2.0F*std::numbers::pi_v<float>/segments);
                float sx,sy,depth;
                const Vec3f point{center.x+std::cos(angle)*radius,
                                  center.y+std::sin(angle)*radius,
                                  terrain_height_near(center.x+std::cos(angle)*radius,
                                                      center.y+std::sin(angle)*radius)+0.04F};
                if(project_to_screen(point,vp,io.DisplaySize.x,io.DisplaySize.y,sx,sy,depth)){
                    const ImVec2 current{sx,sy};
                    if(previous)draw->AddLine(*previous,current,IM_COL32(255,175,55,165),1.5F);
                    previous=current;
                }else previous.reset();
            }
        }
    }
    for(const auto& [index,state]:simulation_actor_states_){
        if(!state.move_target)continue;
        float ax,ay,ad,bx,by,bd;
        if(project_to_screen(state.position,vp,io.DisplaySize.x,io.DisplaySize.y,ax,ay,ad)&&
           project_to_screen(*state.move_target,vp,io.DisplaySize.x,io.DisplaySize.y,bx,by,bd)){
            draw->AddLine({ax,ay},{bx,by},IM_COL32(70,220,255,220),2.0F);
            draw->AddCircle({bx,by},7.0F,IM_COL32(70,220,255,235),20,2.0F);
        }
    }
}


void EditorApp::ensure_water_selection_size(){
    if(!document_){water_selection_mask_.clear();return;}
    const auto required=static_cast<std::size_t>(document_->field().width())*document_->field().height();
    if(water_selection_mask_.size()!=required){
        water_selection_mask_.assign(required,0U);
        water_reference_cell_.reset();
        water_selection_stage_=WaterSelectionStage::SelectArea;
        water_selection_checkpointed_=false;
    }
}

std::size_t EditorApp::water_selection_count() const noexcept{
    return static_cast<std::size_t>(std::count_if(water_selection_mask_.begin(),
        water_selection_mask_.end(),[](uint8_t value){return value!=0U;}));
}

void EditorApp::clear_water_selection(){
    std::fill(water_selection_mask_.begin(),water_selection_mask_.end(),0U);
    water_reference_cell_.reset();
    water_selection_stage_=WaterSelectionStage::SelectArea;
    water_selection_checkpointed_=false;
    stroke_active_=false;
    status_="Temporary water selection cleared.";
}

void EditorApp::paint_water_selection(uint32_t center_x,uint32_t center_y,bool enabled){
    if(!document_)return;
    ensure_water_selection_size();
    const auto& field=document_->field();
    const int radius=static_cast<int>(std::ceil(std::max(0.25F,brush_.radius_cells)));
    for(int dy=-radius;dy<=radius;++dy){
        for(int dx=-radius;dx<=radius;++dx){
            if(static_cast<float>(dx*dx+dy*dy)>brush_.radius_cells*brush_.radius_cells)continue;
            const int x=static_cast<int>(center_x)+dx;
            const int y=static_cast<int>(center_y)+dy;
            if(x<0||y<0||x>=static_cast<int>(field.width())||y>=static_cast<int>(field.height()))continue;
            water_selection_mask_[static_cast<std::size_t>(y)*field.width()+static_cast<std::size_t>(x)]=enabled?1U:0U;
        }
    }
    water_selection_checkpointed_=false;
}

void EditorApp::apply_flat_water_selection(bool enabled,bool checkpoint_if_needed){
    if(!document_)return;
    ensure_water_selection_size();
    if(water_selection_count()==0U){status_="Water selection is empty.";return;}
    if(checkpoint_if_needed&&!water_selection_checkpointed_){checkpoint();water_selection_checkpointed_=true;}
    const auto changed=document_->field().set_water_selection(water_selection_mask_,water_height_,enabled);
    if(changed!=0U){document_->mark_dirty();rebuild_mesh();}
    draw_water_=true;
    status_=(enabled?"Applied one flat water surface to ":"Erased water from ")+
        std::to_string(water_selection_count())+" selected cells ("+std::to_string(changed)+" changed).";
}

void EditorApp::adjust_water_selection_level(float delta){
    if(water_selection_count()==0U)return;
    water_height_+=delta;
    water_selection_stage_=WaterSelectionStage::AdjustLevel;
    apply_flat_water_selection(true);
}

void EditorApp::draw_water_selection_overlay(){
    if(!document_||water_selection_count()==0U)return;
    ensure_water_selection_size();
    const auto io=ImGui::GetIO();
    const auto vp=camera_.view_projection(io.DisplaySize.x/std::max(io.DisplaySize.y,1.0F));
    const auto& field=document_->field();
    auto* draw=ImGui::GetBackgroundDrawList();
    for(uint32_t y=0;y<field.height();++y){
        for(uint32_t x=0;x<field.width();++x){
            const std::size_t index=static_cast<std::size_t>(y)*field.width()+x;
            if(index>=water_selection_mask_.size()||water_selection_mask_[index]==0U)continue;
            const auto& cell=field.at(x,y);
            float sx=0.0F,sy=0.0F,depth=0.0F;
            if(!project_to_screen({thandor::q12_to_float(cell.world_x_q12),
                    thandor::q12_to_float(cell.world_y_q12),cell.terrain_height()+0.12F},
                    vp,io.DisplaySize.x,io.DisplaySize.y,sx,sy,depth))continue;
            const bool reference=water_reference_cell_&&water_reference_cell_->first==x&&
                water_reference_cell_->second==y;
            draw->AddCircleFilled({sx,sy},reference?5.5F:3.2F,
                reference?IM_COL32(255,220,70,235):IM_COL32(35,190,255,125),12);
            if(reference)draw->AddCircle({sx,sy},9.0F,IM_COL32(255,245,160,255),18,2.0F);
        }
    }
}

void EditorApp::load_map(std::size_t group_index){
    try{
        current_group_=group_index;texture_source_group_=static_cast<int>(group_index);
        document_=std::make_unique<thandor::MapDocument>(thandor::MapDocument::open(catalog_,catalog_.map_groups()[group_index]));
        current_project_path_.clear();undo_.clear();redo_.clear();selected_placement_.reset();selected_edit_index_.reset();
        reset_simulation(false);simulation_static_replacement_dirty_=false;
        water_selection_mask_.clear();water_reference_cell_.reset();water_selection_stage_=WaterSelectionStage::SelectArea;water_selection_checkpointed_=false;
        asset_preview_cache_.clear();asset_preview_texture_cache_.clear();refresh_scenario_text_buffers();load_materials();rebuild_mesh();center_camera();
        status_="Loaded "+catalog_.map_groups()[group_index].display_name+"; terrain layers "+std::to_string(loaded_material_count_)+"/26"+(terrain_texture_layout_?"; terrain DAT loaded":"; terrain DAT fallback");
    }catch(const std::exception& e){status_=e.what();}
}

void EditorApp::load_map_from_path(const std::filesystem::path& path){
    try{
        if(path.empty())throw std::runtime_error("map path is empty");
        std::string extension=path.extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        thandor::MapDocument loaded=extension==".lev"?
            thandor::MapDocument::open_loose(catalog_,path):thandor::MapDocument::open_project(catalog_,path);
        document_=std::make_unique<thandor::MapDocument>(std::move(loaded));
        current_project_path_=extension==".lev"?std::filesystem::path{}:path;
        undo_.clear();redo_.clear();selected_placement_.reset();selected_edit_index_.reset();
        reset_simulation(false);simulation_static_replacement_dirty_=false;
        water_selection_mask_.clear();water_reference_cell_.reset();water_selection_stage_=WaterSelectionStage::SelectArea;water_selection_checkpointed_=false;
        asset_preview_cache_.clear();asset_preview_texture_cache_.clear();refresh_scenario_text_buffers();load_materials();rebuild_mesh();center_camera();
        status_="Loaded "+document_->display_name()+" from "+path.string()+" with "+std::to_string(document_->custom_asset_count())+" custom assets.";
    }catch(const std::exception& e){status_="Load failed: "+std::string(e.what());}
}

void EditorApp::save_project(const std::filesystem::path& path){
    try{
        if(path.empty())throw std::runtime_error("project path is empty");
        if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
        document_->save_project(path);current_project_path_=path;document_->mark_clean();
        std::snprintf(save_project_path_,sizeof(save_project_path_),"%s",path.string().c_str());
        status_="Saved full map project "+path.string()+" (LEV, FLD, names, and "+std::to_string(document_->custom_asset_count())+" custom assets).";
    }catch(const std::exception& e){status_="Save failed: "+std::string(e.what());}
}

void EditorApp::create_new_map(){
    try{
        const auto& shell=catalog_.map_groups()[static_cast<std::size_t>(template_group_)];
        thandor::MapDocument created=new_empty_map_?
            thandor::MapDocument::create_empty(catalog_,shell,new_map_name_,new_display_name_,
                static_cast<uint32_t>(std::max(new_width_,2)),static_cast<uint32_t>(std::max(new_height_,2)),
                thandor::float_to_q12(new_initial_height_),static_cast<uint8_t>(std::clamp(new_initial_material_,0,25))):
            thandor::MapDocument::create_from_template(catalog_,shell,new_map_name_,
                static_cast<uint32_t>(std::max(new_width_,2)),static_cast<uint32_t>(std::max(new_height_,2)));
        created.set_display_name(new_display_name_);
        document_=std::make_unique<thandor::MapDocument>(std::move(created));
        current_group_=static_cast<std::size_t>(template_group_);texture_source_group_=template_group_;current_project_path_.clear();
        undo_.clear();redo_.clear();selected_placement_.reset();selected_edit_index_.reset();
        reset_simulation(false);simulation_static_replacement_dirty_=false;
        water_selection_mask_.clear();water_reference_cell_.reset();water_selection_stage_=WaterSelectionStage::SelectArea;water_selection_checkpointed_=false;
        asset_preview_cache_.clear();asset_preview_texture_cache_.clear();refresh_scenario_text_buffers();load_materials();rebuild_mesh();center_camera();
        status_=new_empty_map_?"Created an empty flat map with no placements.":"Created a template-terrain map with cleared placements.";
    }catch(const std::exception& e){status_=e.what();}
}

void EditorApp::apply_texture_family_from_map(std::size_t group_index){try{if(group_index>=catalog_.map_groups().size())throw std::out_of_range("texture source map");checkpoint();const auto source=thandor::LevelAsset::parse(catalog_.load(catalog_.map_groups()[group_index].level_path));document_->level().paths().ground_textures=source.paths().ground_textures;document_->level().paths().surface_textures=source.paths().surface_textures;document_->mark_dirty();asset_preview_cache_.clear();asset_preview_texture_cache_.clear();load_materials();rebuild_mesh();status_="Loaded terrain texture family from "+catalog_.map_groups()[group_index].display_name;}catch(const std::exception& e){status_=e.what();}}

void EditorApp::load_materials(){
    material_names_.clear();
    material_images_.clear();
    terrain_texture_layout_.reset();
    surface_texture_layout_.reset();
    surface_palette_argb_.clear();
    surface_material_images_.clear();
    loaded_surface_material_count_=0;
    surface_family_=thandor::TerrainSurfaceFamily::Other;
    loaded_material_count_=0;
    missing_material_count_=0;
    if(!document_)return;

    const std::string base=terrain_base_path(document_->level().paths().ground_textures);
    if(base.empty())throw std::runtime_error("LEV ground texture path is empty");
    try{
        terrain_texture_layout_=thandor::TerrainTextureLayout::parse(document_->load_asset(catalog_,base+".dat"));
    }catch(const std::exception&){
        terrain_texture_layout_.reset();
    }

    material_names_.reserve(26);
    material_images_.reserve(26);
    for(uint32_t material=0;material<26;++material){
        const std::string path=terrain_material_path(base,material);
        const auto sources=document_->terrain_material_sources();
        material_names_.push_back(sources[material].empty()?path:sources[material]);
        try{
            const auto gfx=thandor::GfxAsset::parse(document_->load_asset(catalog_,path));
            if(gfx.subresources().empty())throw std::runtime_error("empty terrain GFX");
            material_images_.push_back(gfx.decode(thandor::GraphicsSubresourceIndex{0U},false));
            ++loaded_material_count_;
        }catch(const std::exception&){
            material_images_.push_back(thandor::make_missing_terrain_material(material));
            material_names_.back()+=" [missing]";
            ++missing_material_count_;
        }
    }
    renderer_.update_materials(material_images_);

    const std::string surface_base=terrain_base_path(document_->level().paths().surface_textures);
    surface_family_=thandor::classify_terrain_surface_path(document_->level().paths().surface_textures);
    if(!surface_base.empty()){
        try{
            surface_texture_layout_=thandor::TerrainSurfaceTextureLayout::parse(
                document_->load_asset(catalog_,surface_base+".dat"));
            const auto palette=thandor::PalAsset::parse(
                document_->load_asset(catalog_,surface_base+".pal"));
            surface_palette_argb_.assign(palette.colors_argb().begin(),palette.colors_argb().end());
            const auto graphics=thandor::GfxAsset::parse(
                document_->load_asset(catalog_,surface_base+".gfx"));
            const std::size_t layer_count=std::min<std::size_t>(graphics.subresources().size(),256U);
            surface_material_images_.reserve(layer_count);
            for(std::size_t layer=0;layer<layer_count;++layer){
                surface_material_images_.push_back(graphics.decode(thandor::GraphicsSubresourceIndex{static_cast<uint32_t>(layer)},false));
            }
            loaded_surface_material_count_=surface_material_images_.size();
            for(const auto& record:surface_texture_layout_->records()){
                if(record.material_index>=surface_material_images_.size())
                    throw std::runtime_error("surface DAT texture index exceeds surface GFX layers");
                if(record.palette_index>=surface_palette_argb_.size())
                    throw std::runtime_error("surface DAT palette index exceeds surface PAL entries");
            }
        }catch(const std::exception&){
            surface_texture_layout_.reset();
            surface_palette_argb_.clear();
            surface_material_images_.clear();
            loaded_surface_material_count_=0;
        }
    }
    renderer_.update_surface_materials(surface_material_images_);
    if(selected_material_>=material_images_.size())selected_material_=0;
}
void EditorApp::rebuild_mesh(){if(document_){renderer_.update_mesh(
    document_->field(),document_->level().lighting(),
    terrain_texture_layout_?&*terrain_texture_layout_:nullptr,
    surface_texture_layout_?&*surface_texture_layout_:nullptr,
    exact_soil_packets_,soil_transition_rng_seed_,
    resource_animation_rng_seed_);rebuild_objects();}}

void EditorApp::rebuild_objects(){
    rendered_object_count_=0;
    marker_only_object_count_=0;
    object_texture_layer_count_=0;
    power_resource_decal_count_=0;
    building_resource_decal_count_=0;
    resource_body_triangle_count_=0;
    resource_luminous_particle_triangle_count_=0;
    resource_shading_emitter_count_=0;
    projected_shadow_triangle_count_=0;
    generated_shadow_object_count_=0;
    generated_shadow_texture_count_=0;
    unsupported_primitive_selector_count_=0;
    tree_object_count_=0;
    rock_object_count_=0;
    related_effect_definition_count_=0;
    timed_effect_controller_count_=0;
    timed_effect_sprite_triangle_count_=0;
    lava_linked_shot_particle_count_=0;
    player_start_count_=0;
    object_build_error_count_=0;
    selected_lod_triangle_count_=0;
    discarded_alternate_lod_triangle_count_=0;
    untextured_triangle_count_=0;
    neutral_source_color_triangle_count_=0;
    terrain_overlay_safe_cell_count_=0;
    terrain_overlay_caution_cell_count_=0;
    terrain_overlay_blocked_cell_count_=0;
    recursive_model_node_count_=0;
    animated_model_node_count_=0;
    visible_animated_building_count_=0;
    visible_timed_effect_controller_count_=0;
    dynamic_triangle_count_=0;
    dynamic_scene_build_milliseconds_=0.0;
    timed_effect_controllers_.clear();
    animated_building_placements_.clear();
    animated_unit_placements_.clear();
    continuous_radar_placements_.clear();
    simulation_actor_placements_.clear();
    dynamic_shadow_templates_.clear();
    visible_animated_buildings_.clear();
    visible_timed_effect_controllers_.clear();
    object_texture_layer_lookup_.clear();
    dynamic_scene_cpu_={};
    placement_pick_bounds_.clear();
    placement_pick_hulls_.clear();
    uploaded_dynamic_scene_revisions_.fill(0U);
    first_object_build_error_.clear();
    if(!document_){
        renderer_.update_objects(std::span<const ObjectVertex>{},std::span<const uint32_t>{},
                                 std::span<const ObjectDrawRun>{},
                                 std::span<const ObjectVertex>{},std::span<const uint32_t>{},
                                 std::span<const thandor::RgbaImage>{});
        ++dynamic_scene_revision_;
        return;
    }
    constexpr std::size_t kMaximumVertices=2'000'000;
    constexpr std::size_t kMaximumIndices=6'000'000;
    const std::size_t kMaximumTextureLayers=renderer_.maximum_object_texture_layers();
    std::vector<ObjectVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<ObjectDrawRun> draw_runs;
    std::vector<ObjectVertex> shadow_vertices;
    std::vector<uint32_t> shadow_indices;
    std::vector<TimedEffectControllerPreview> pending_timed_effects;
    std::vector<thandor::RgbaImage> texture_layers;
    std::unordered_map<uint64_t,uint32_t> layer_lookup;
    const auto& level_lighting=document_->level().lighting();
    const auto ramp=level_lighting.build_game_ramp();
    const std::string army_base=document_->level().paths().army_textures;

    const auto texture_family=[&](uint32_t faction)->const ArmyTextureFamily&{
        const uint32_t clamped=std::min(faction,8U);
        const std::string key=terrain_base_path(army_base)+"#"+std::to_string(clamped);
        auto found=army_texture_cache_.find(key);
        if(found==army_texture_cache_.end()){
            auto family=std::make_shared<ArmyTextureFamily>(ArmyTextureFamily{
                thandor::GfxAsset::parse(catalog_.load(army_texture_family_path(army_base,clamped,".gfx"))),
                thandor::PalAsset::parse(catalog_.load(army_texture_family_path(army_base,clamped,".pal")))});
            found=army_texture_cache_.emplace(key,std::move(family)).first;
        }
        return *found->second;
    };
    const auto texture_layer=[&](uint32_t faction,uint32_t subresource)->uint32_t{
        const uint32_t clamped=std::min(faction,8U);
        const uint64_t key=(static_cast<uint64_t>(clamped)<<32U)|subresource;
        if(const auto found=layer_lookup.find(key);found!=layer_lookup.end())return found->second;
        if(texture_layers.size()>=kMaximumTextureLayers)throw std::runtime_error("object/effect texture array exhausted the selected Vulkan device limit of "+
            std::to_string(kMaximumTextureLayers)+" layers");
        const auto& family=texture_family(clamped);
        if(subresource>=family.graphics.subresources().size())throw std::runtime_error("SPR selects missing army GFX subresource");
        const uint32_t layer=static_cast<uint32_t>(texture_layers.size());
        texture_layers.push_back(thandor::make_sprite_texture_square_canvas(
            family.graphics.decode(thandor::GraphicsSubresourceIndex{subresource},false)));
        layer_lookup.emplace(key,layer);
        return layer;
    };
    const std::string effect_base=terrain_base_path(document_->level().paths().effect_textures);
    const auto effect_texture_family=[&]()->const ArmyTextureFamily&{
        if(effect_base.empty())throw std::runtime_error("LEV effect texture path is empty");
        const std::string key=effect_base+"#effect";
        auto found=army_texture_cache_.find(key);
        if(found==army_texture_cache_.end()){
            auto family=std::make_shared<ArmyTextureFamily>(ArmyTextureFamily{
                thandor::GfxAsset::parse(catalog_.load(effect_base+".gfx")),
                thandor::PalAsset::parse(catalog_.load(effect_base+".pal"))});
            found=army_texture_cache_.emplace(key,std::move(family)).first;
        }
        return *found->second;
    };
    const auto effect_texture_layer=[&](uint32_t subresource)->uint32_t{
        constexpr uint64_t kEffectLayerNamespace=uint64_t{1}<<63U;
        const uint64_t key=kEffectLayerNamespace|subresource;
        if(const auto found=layer_lookup.find(key);found!=layer_lookup.end())return found->second;
        if(texture_layers.size()>=kMaximumTextureLayers)
            throw std::runtime_error("object/effect texture array exhausted the selected Vulkan device limit of "+
                std::to_string(kMaximumTextureLayers)+" layers");
        const auto& family=effect_texture_family();
        if(subresource>=family.graphics.subresources().size())
            throw std::runtime_error("EFF/SHOT sprite selects missing effect GFX subresource");
        const uint32_t layer=static_cast<uint32_t>(texture_layers.size());
        texture_layers.push_back(thandor::make_sprite_texture_square_canvas(
            family.graphics.decode(thandor::GraphicsSubresourceIndex{subresource},false)));
        layer_lookup.emplace(key,layer);
        return layer;
    };
    const auto load_cached_sprite=[&](const std::string& path)->const thandor::SpriteAsset&{
        auto found=sprite_cache_.find(path);
        if(found==sprite_cache_.end())
            found=sprite_cache_.emplace(path,thandor::SpriteAsset::parse(catalog_.load(path))).first;
        return found->second;
    };

    std::unordered_set<uint32_t> preloaded_effect_ids;
    std::unordered_set<uint32_t> preloaded_shot_ids;
    std::function<void(uint32_t)> preload_effect_textures;
    std::function<void(uint32_t)> preload_shot_textures;
    preload_effect_textures=[&](uint32_t effect_id){
        if(effect_id==0U||!preloaded_effect_ids.insert(effect_id).second)return;
        const auto* definition=catalog_.effect_definition(effect_id);
        if(definition==nullptr)throw std::runtime_error("timed effect is missing from the EFF registry");
        const auto& sprite=load_cached_sprite(definition->sprite_path);
        const auto& mesh=sprite.mesh();
        const uint32_t frame_count=std::max(1U,definition->runtime_value_0c);
        for(std::size_t triangle=0;triangle+2U<mesh.indices.size();triangle+=3U){
            const auto& vertex=mesh.vertices.at(mesh.indices[triangle]);
            if(vertex.texture_subresource==std::numeric_limits<uint32_t>::max())continue;
            for(uint32_t frame=0;frame<frame_count;++frame){
                const uint32_t subresource=vertex.texture_subresource+frame;
                if((definition->creation_flags&2U)!=0U){
                    // EffectRuntimePool_CreateInstanceFromDefinitionCf selects the fixed
                    // g_ArmyGraphicsBindings[0] family for this flag. Expanding these records
                    // into every map faction both recolours effects and multiplies texture layers.
                    (void)texture_layer(0U,subresource);
                }else{
                    (void)effect_texture_layer(subresource);
                }
            }
        }
        if(definition->linked_effect_present!=0U)
            preload_effect_textures(definition->linked_effect_id);
        if(definition->periodic_effect_id!=0U)
            preload_effect_textures(definition->periodic_effect_id);
        if(definition->linked_shot_present!=0U&&definition->linked_shot_id!=0U)
            preload_shot_textures(definition->linked_shot_id);
    };
    preload_shot_textures=[&](uint32_t shot_id){
        if(shot_id==0U||!preloaded_shot_ids.insert(shot_id).second)return;
        const auto* shot=catalog_.shot_definition(shot_id);
        if(!shot)throw std::runtime_error("weapon references a missing SHT definition");
        const auto& sprite=load_cached_sprite(shot->sprite_path);
        const auto& mesh=sprite.mesh();
        for(std::size_t triangle=0;triangle+2U<mesh.indices.size();triangle+=3U){
            const auto& vertex=mesh.vertices.at(mesh.indices[triangle]);
            if(vertex.texture_subresource==std::numeric_limits<uint32_t>::max())continue;
            for(uint32_t frame=0;frame<std::max(1U,shot->animation_frame_count);++frame)
                (void)effect_texture_layer(vertex.texture_subresource+frame);
        }
        preload_effect_textures(shot->primary_effect_id);
        preload_effect_textures(shot->launch_effect_id);
        preload_effect_textures(shot->secondary_effect_id);
        for(const uint32_t impact_id:shot->terrain_impact_effect_ids)
            preload_effect_textures(impact_id);
        for(const uint32_t impact_id:shot->target_impact_effect_ids)
            preload_effect_textures(impact_id);
    };

    // Projectiles can outlive a committed/reset actor. Keep their authored SHT/EFF layers
    // resident until the projectile lifecycle expires.
    for(const auto& projectile:simulation_projectiles_)
        preload_shot_textures(projectile.shot_definition_id);

    const auto untextured_layer=[&]()->uint32_t{
        constexpr uint64_t key=std::numeric_limits<uint64_t>::max();
        if(const auto found=layer_lookup.find(key);found!=layer_lookup.end())return found->second;
        if(texture_layers.size()>=kMaximumTextureLayers)throw std::runtime_error("object/effect texture array exhausted the selected Vulkan device limit of "+
            std::to_string(kMaximumTextureLayers)+" layers");
        thandor::RgbaImage white{1U,1U,{255U,255U,255U,255U}};
        const uint32_t layer=static_cast<uint32_t>(texture_layers.size());
        texture_layers.push_back(std::move(white));
        layer_lookup.emplace(key,layer);
        return layer;
    };
    struct ShadowAtlasTile {
        uint32_t layer{};
        float u0{};
        float v0{};
        float u1{};
        float v1{};
    };
    uint32_t current_shadow_atlas_layer=std::numeric_limits<uint32_t>::max();
    uint32_t next_shadow_atlas_tile=4U;
    const auto shadow_atlas_tile=[&](const thandor::RgbaImage& mask)->ShadowAtlasTile{
        if(mask.width!=64U||mask.height!=64U||mask.pixels.size()!=64U*64U*4U)
            throw std::runtime_error("generated shadow mask is not the binary 64x64 format");
        if(next_shadow_atlas_tile>=4U){
            if(texture_layers.size()>=kMaximumTextureLayers)
                throw std::runtime_error("generated shadows exhausted the selected Vulkan device image-array limit of "+
                    std::to_string(kMaximumTextureLayers)+" layers");
            current_shadow_atlas_layer=static_cast<uint32_t>(texture_layers.size());
            texture_layers.push_back(thandor::RgbaImage{128U,128U,
                std::vector<uint8_t>(128U*128U*4U,0U)});
            next_shadow_atlas_tile=0U;
            ++generated_shadow_texture_count_;
        }
        const uint32_t tile=next_shadow_atlas_tile++;
        const uint32_t tile_x=(tile&1U)*64U;
        const uint32_t tile_y=(tile>>1U)*64U;
        auto& atlas=texture_layers[current_shadow_atlas_layer];
        for(uint32_t y=0;y<64U;++y){
            const std::size_t source=static_cast<std::size_t>(y)*64U*4U;
            const std::size_t destination=(static_cast<std::size_t>(tile_y+y)*128U+tile_x)*4U;
            std::copy_n(mask.pixels.begin()+static_cast<std::ptrdiff_t>(source),64U*4U,
                        atlas.pixels.begin()+static_cast<std::ptrdiff_t>(destination));
        }
        constexpr float inv=1.0F/128.0F;
        return ShadowAtlasTile{current_shadow_atlas_layer,
            (static_cast<float>(tile_x)+0.5F)*inv,
            (static_cast<float>(tile_y)+0.5F)*inv,
            (static_cast<float>(tile_x)+63.5F)*inv,
            (static_cast<float>(tile_y)+63.5F)*inv};
    };
    const auto append_draw_run=[&](PrimitiveBlendPreset blend,bool two_sided,uint32_t first_index){
        if(!draw_runs.empty()){
            auto& previous=draw_runs.back();
            if(previous.blend==blend&&previous.two_sided==two_sided&&
               previous.first_index+previous.index_count==first_index){
                previous.index_count+=3U;
                return;
            }
        }
        draw_runs.push_back(ObjectDrawRun{blend,two_sided,first_index,3U});
    };


    const auto placements=document_->level().placements();
    placement_pick_bounds_.assign(placements.size(),AxisAlignedBounds{});
    placement_pick_hulls_.assign(placements.size(),{});
    // game.exe loads LEV +0x2F0 low-word azimuth/high-word elevation into
    // WorldRuntime +0xB8/+0xBC and passes that pair to both the terrain raycast
    // and FixedMath_DirectionFromAnglesScaledRegs used by generated shadows.
    const Vec3f shadow_projection_direction=document_->level().lighting().shadow_ray_direction();
    for(std::size_t placement_index=0;placement_index<placements.size();++placement_index){
        const auto& placement=placements[placement_index];
        auto& pick_bounds=placement_pick_bounds_[placement_index];
        auto& pick_hulls=placement_pick_hulls_[placement_index];
        const float placement_base_z=terrain_height_near(placement.render_x(),placement.render_y());
        // Every placement keeps a small selectable fallback volume, including marker-only and
        // unresolved records. Visible model vertices expand this to the complete resolved ARM/MDL
        // hierarchy, so clicking a building surface no longer depends on its root marker.
        AxisAlignedBounds fallback_pick_bounds;
        fallback_pick_bounds.include({placement.render_x()-0.35F,placement.render_y()-0.35F,placement_base_z-0.15F});
        fallback_pick_bounds.include({placement.render_x()+0.35F,placement.render_y()+0.35F,placement_base_z+0.85F});
        pick_bounds.include(fallback_pick_bounds.minimum);
        pick_bounds.include(fallback_pick_bounds.maximum);
        pick_hulls.push_back(TransformedBoundsHull::from_axis_aligned(fallback_pick_bounds));
        const auto* preset=catalog_.army_preset(placement.army_asset_id);
        if(preset==nullptr){++marker_only_object_count_;continue;}
        related_effect_definition_count_+=preset->shared_sprite_effect_ids.size();
        if(preset->visual_kind==thandor::PlacementVisualKind::PlayerStart){
            ++player_start_count_;
            continue;
        }
        if(preset->sprite_path.empty()){++marker_only_object_count_;continue;}
        try{
            const auto load_sprite=[&](const std::string& path)->const thandor::SpriteAsset&{
                auto found=sprite_cache_.find(path);
                if(found==sprite_cache_.end())
                    found=sprite_cache_.emplace(path,
                        thandor::SpriteAsset::parse(catalog_.load(path))).first;
                return found->second;
            };
            std::vector<thandor::ModelVisualNode> fallback_nodes;
            std::span<const thandor::ModelVisualNode> hierarchy =
                placement_hierarchy(*preset, placement_index);
            if(hierarchy.empty()){
                thandor::ModelVisualNode fallback;
                fallback.parent_index=std::numeric_limits<uint32_t>::max();
                fallback.local_rotation_angle1=0x4000U;
                fallback.sprite_path=preset->sprite_path;
                fallback_nodes.push_back(std::move(fallback));
                hierarchy=fallback_nodes;
            }

            const float radians=thandor::angle_turn32_to_radians(
                thandor::runtime_angle_from_serialized_turn16(placement.orientation_turn16));
            const uint32_t hierarchy_tick=0U;
            std::vector<ModelHierarchyInstance> instances;
            std::vector<bool> active;
            instances.reserve(hierarchy.size());
            active.reserve(hierarchy.size());
            for(std::size_t node_index=0;node_index<hierarchy.size();++node_index){
                const auto& node=hierarchy[node_index];
                ModelHierarchyInstance instance{};
                instance.node=&node;
                instance.root=node.parent_index==std::numeric_limits<uint32_t>::max();
                bool node_active=node.creates_runtime_node()&&!node.sprite_path.empty();
                const thandor::SpriteAsset* node_sprite=node_active?&load_sprite(node.sprite_path):nullptr;
                if(instance.root){
                    const auto contact=placement_contact_pose(*preset,placement.render_x(),
                        placement.render_y(),node_sprite?node_sprite->placement_height_offset():0.0F);
                    const Vec3f root_position{placement.render_x(),placement.render_y(),contact.height};
                    instance.transform=contact.align_to_normal
                        ?terrain_contact_root_transform(root_position,radians,contact.normal)
                        :placement_root_transform(root_position.x,root_position.y,root_position.z,radians);
                }else{
                    if(node.parent_index>=instances.size()||!active[node.parent_index])node_active=false;
                    if(node_active){
                        const auto& parent=instances[node.parent_index];
                        const auto* attachment=parent.sprite->child_attachment(node.child_slot);
                        if(attachment==nullptr)
                            throw std::runtime_error("MDL child slot has no matching SPR attachment transform");
                        uint32_t animated_angle2=node.local_rotation_angle2;
                        if(preset->runtime_uses_generic_model_subnode_animation&&
                           parent.root&&node.child_slot==0U&&preset->child0_rotation_step_turn16!=0){
                            animated_angle2=static_cast<uint32_t>(static_cast<int64_t>(animated_angle2)+
                                static_cast<int64_t>(preset->child0_rotation_step_turn16)*hierarchy_tick)&0xFFFFU;
                            ++animated_model_node_count_;
                        }else if(preset->runtime_uses_generic_model_subnode_animation&&
                                  parent.root&&node.child_slot==1U&&preset->child1_rotation_step_turn16!=0){
                            animated_angle2=static_cast<uint32_t>(static_cast<int64_t>(animated_angle2)+
                                static_cast<int64_t>(preset->child1_rotation_step_turn16)*hierarchy_tick)&0xFFFFU;
                            ++animated_model_node_count_;
                        }
                        uint32_t animated_angle1=node.local_rotation_angle1;
                        if(preset->runtime_model_class_id==17U&&parent.root&&node.child_slot<3U)
                            animated_angle1=thandor::wrap_turn16(
                                static_cast<int64_t>(animated_angle1)+0x3FF0);
                        auto local=fixed_model_rotation_basis(animated_angle2,
                            animated_angle1,node.local_rotation_angle0);
                        local.translation=attachment->local_translation;
                        if(preset->runtime_uses_generic_model_subnode_animation&&
                           parent.root&&node.child_slot==2U&&preset->child2_vertical_step_q12!=0){
                            local.translation.z=animated_child2_translation_q12(*preset,
                                hierarchy_tick,local.translation.z);
                            ++animated_model_node_count_;
                        }
                        instance.transform=compose_model_transform(parent.transform,local);
                    }
                }
                if(node_active){
                    instance.sprite=node_sprite;
                    ++recursive_model_node_count_;
                }
                active.push_back(node_active);
                instances.push_back(instance);
            }
            apply_walker_pose_to_instances(*preset,hierarchy,instances,nullptr,
                [&](float x,float y){return terrain_height_near(x,y);});
            // SPR +0xC0..+0xD8 are the runtime resource bounds used before detailed mesh work.
            // Include them recursively so conservative culling and picking do not hide large
            // buildings merely because their visible triangles have not yet expanded the box.
            for(std::size_t instance_index=0;instance_index<instances.size();++instance_index){
                if(instance_index>=active.size()||!active[instance_index]||
                   instances[instance_index].sprite==nullptr)continue;
                const auto& sprite=*instances[instance_index].sprite;
                if(sprite.has_valid_header_bounds()){
                    const auto minimum=sprite.local_bounds_min();
                    const auto maximum=sprite.local_bounds_max();
                    TransformedBoundsHull node_hull;
                    node_hull.valid=true;
                    for(uint32_t corner=0;corner<8U;++corner){
                        const Vec3f local{
                            (corner&1U)!=0U?maximum.x:minimum.x,
                            (corner&2U)!=0U?maximum.y:minimum.y,
                            (corner&4U)!=0U?maximum.z:minimum.z};
                        node_hull.corners[corner]=transform_point(
                            instances[instance_index].transform,local);
                        pick_bounds.include(node_hull.corners[corner]);
                    }
                    pick_hulls.push_back(node_hull);
                }else if(sprite.bounding_radius()>0.0F){
                    const Vec3f center=instances[instance_index].transform.translation;
                    const float radius=sprite.bounding_radius();
                    AxisAlignedBounds radius_bounds;
                    radius_bounds.include({center.x-radius,center.y-radius,center.z-radius});
                    radius_bounds.include({center.x+radius,center.y+radius,center.z+radius});
                    pick_bounds.include(radius_bounds.minimum);
                    pick_bounds.include(radius_bounds.maximum);
                    pick_hulls.push_back(TransformedBoundsHull::from_axis_aligned(radius_bounds));
                }
            }
            // Preserve the pre-simulation static composition contract. Merely being movable or
            // armed does not make an actor an authored animated object. Only the original MDL/ARM
            // subnode animation path enters the building animation list. A transient simulation
            // actor is tagged separately and replaces only that exact placement.
            const bool class17_lifecycle_pose=preset->runtime_model_class_id==17U&&
                simulation_actor_states_.contains(placement_index);
            const bool fog_dynamic_replacement=fog_preview_mode_!=0&&
                (preset->browser_kind==thandor::PlacementBrowserKind::Building||
                 preset->browser_kind==thandor::PlacementBrowserKind::Unit);
            const thandor::SimulationRenderPolicy render_policy{
                preset->browser_kind==thandor::PlacementBrowserKind::Building,
                preset->browser_kind==thandor::PlacementBrowserKind::Unit,
                preset->has_model_subnode_animation||class17_lifecycle_pose,
                simulation_mode_||fog_preview_mode_!=0,
                simulation_actor_states_.contains(placement_index)||fog_dynamic_replacement,
                preset->has_continuous_radar_subnode_animation};
            const bool animated_building=render_policy.authored_animated_building();
            const bool animated_unit=render_policy.authored_animated_unit();
            const bool simulation_replacement=render_policy.simulation_replacement();
            if(animated_building) animated_building_placements_.push_back(placement_index);
            if(animated_unit) animated_unit_placements_.push_back(placement_index);
            if(preset->has_continuous_radar_subnode_animation)
                continuous_radar_placements_.push_back(placement_index);
            if(simulation_replacement) simulation_actor_placements_.push_back(placement_index);
            // Simulation SHT/EFF textures share the object texture array. Preload them only for
            // actors that currently own transient simulation state; preloading all 125 stock armed
            // presets consumed the finite array and caused later units/buildings to fail midway.
            if(simulation_replacement&&simulation_actor_states_.contains(placement_index)){
                for(const auto& weapon:preset->simulation_weapon_profiles)
                    preload_shot_textures(weapon.shot_definition_id);
            }

            thandor::ResourceEnergyKind resource_kind=thandor::ResourceEnergyKind::None;
            if(preset->visual_kind==thandor::PlacementVisualKind::PowerResource){
                resource_kind=thandor::ResourceEnergyKind::Power;
            }else if(preset->visual_kind==thandor::PlacementVisualKind::BuildingResource){
                resource_kind=thandor::ResourceEnergyKind::Building;
            }
            const uint32_t resource_body=thandor::resource_body_subresource(resource_kind);
            const uint32_t resource_particle=thandor::resource_luminous_particle_subresource(resource_kind);
            const bool neutral_source_color=thandor::placement_uses_neutral_source_color(*preset);
            const uint32_t preview_faction=neutral_source_color?0U:
                thandor::default_faction_graphics_family(placement.faction_index);
            const auto& family=texture_family(preview_faction);

            const auto root_instance=std::find_if(instances.begin(),instances.end(),
                [](const ModelHierarchyInstance& instance){return instance.root;});
            const std::size_t root_index=root_instance==instances.end()?instances.size():
                static_cast<std::size_t>(std::distance(instances.begin(),root_instance));
            if(preset->timed_effect_id!=0U&&root_instance!=instances.end()&&
               root_index<active.size()&&active[root_index]&&root_instance->sprite!=nullptr){
                const auto* definition=catalog_.effect_definition(preset->timed_effect_id);
                if(definition==nullptr)
                    throw std::runtime_error("MDL timed effect is missing from the EFF registry");
                preload_effect_textures(definition->definition_id);
                TimedEffectControllerPreview pending;
                pending.effect_id=definition->definition_id;
                pending.placement_index=placement_index;
                pending.root_model_node_index=static_cast<uint32_t>(root_index);
                pending.emitter_model_node_index=static_cast<uint32_t>(root_index);
                // ArmyRuntime_UpdateTimedShotAndEffectEmitters selects child slot zero for class 21
                // before reading kind-6 attachment points. Other classes use the root node.
                if(preset->runtime_model_class_id==21U){
                    for(std::size_t node_index=0;node_index<hierarchy.size();++node_index){
                        if(node_index>=active.size()||!active[node_index])continue;
                        const auto& node=hierarchy[node_index];
                        if(node.parent_index==root_index&&node.child_slot==0U){
                            pending.emitter_model_node_index=static_cast<uint32_t>(node_index);
                            break;
                        }
                    }
                }
                const std::size_t emitter_index=pending.emitter_model_node_index;
                const auto& emitter_instance=instances[emitter_index];
                pending.root_position=root_instance->transform.translation;
                pending.root_orientation=root_instance->transform;
                pending.emitter_orientation=emitter_instance.transform;
                if(emitter_instance.sprite!=nullptr){
                    const auto local_points=emitter_instance.sprite->type6_attachment_points();
                    pending.local_type6_attachments.assign(local_points.begin(),local_points.end());
                }
                if((definition->shading_color_argb&0x00FFFFFFU)!=0U){
                    pending.shading_profile=thandor::make_resource_shading_glow_profile(*definition);
                    if(resource_kind!=thandor::ResourceEnergyKind::None&&
                       pending.shading_profile->effect_id!=114U&&
                       pending.shading_profile->effect_id!=116U)
                        throw std::runtime_error("resource MDL selected an unexpected periodic shading effect");
                }
                pending.timing={std::max(1U,preset->timed_effect_interval_ticks),
                    preset->timed_effect_random_ticks,
                    !pending.local_type6_attachments.empty()&&
                        (preset->model_runtime_render_flags&1U)==0U,
                    definition->transition_kind==2U};
                // Timed exhaust, machinery and other effects owned by a building are part of the
                // building animation contract. General lava/resource effects remain controlled by
                // the independent timed-map-effect switch.
                pending.owner_is_building=
                    preset->browser_kind==thandor::PlacementBrowserKind::Building;
                pending_timed_effects.push_back(std::move(pending));
                ++timed_effect_controller_count_;
            }
            bool emitted_object=false;
            bool emitted_body=false;
            bool emitted_particle=false;
            std::vector<thandor::GeneratedShadowTriangle> generated_shadow_triangles;

            for(std::size_t instance_index=0;instance_index<instances.size();++instance_index){
                if(!active[instance_index])continue;
                const auto& instance=instances[instance_index];
                const auto& sprite=*instance.sprite;
                const auto& mesh=sprite.mesh();
                if(mesh.indices.size()%3U!=0U)
                    throw std::runtime_error("SPR preview mesh is not triangle aligned");
                selected_lod_triangle_count_+=mesh.indices.size()/3U;
                discarded_alternate_lod_triangle_count_+=sprite.alternate_lod_triangle_count();

                for(std::size_t triangle=0;triangle<mesh.indices.size();triangle+=3U){
                    const uint32_t ia=mesh.indices[triangle];
                    const uint32_t ib=mesh.indices[triangle+1U];
                    const uint32_t ic=mesh.indices[triangle+2U];
                    if(ia>=mesh.vertices.size()||ib>=mesh.vertices.size()||ic>=mesh.vertices.size())
                        throw std::runtime_error("SPR preview index exceeds vertex stream");
                    const std::array<const thandor::SpriteMeshVertex*,3> corners{
                        &mesh.vertices[ia],&mesh.vertices[ib],&mesh.vertices[ic]};
                    const uint32_t triangle_subresource=corners[0]->texture_subresource;
                    if(corners[1]->texture_subresource!=triangle_subresource||
                       corners[2]->texture_subresource!=triangle_subresource)
                        throw std::runtime_error("SPR triangle mixes texture subresources");
                    const bool resource_particle_triangle=resource_kind!=thandor::ResourceEnergyKind::None&&
                        triangle_subresource==resource_particle;
                    const bool resource_body_triangle=resource_kind!=thandor::ResourceEnergyKind::None&&
                        triangle_subresource==resource_body;
                    const bool untextured=triangle_subresource==std::numeric_limits<uint32_t>::max();
                    const uint32_t triangle_render_flags=corners[0]->render_flags;
                    if(corners[1]->render_flags!=triangle_render_flags||
                       corners[2]->render_flags!=triangle_render_flags)
                        throw std::runtime_error("SPR triangle mixes render flags");
                    const auto resource_role=resource_body_triangle?ResourcePrimitiveRole::DenseBody:
                        (resource_particle_triangle?ResourcePrimitiveRole::LuminousParticle:
                         ResourcePrimitiveRole::None);
                    const auto primitive=classify_resource_preview_primitive(
                        triangle_render_flags,!untextured,resource_role);
                    if(!primitive.supported){++unsupported_primitive_selector_count_;continue;}
                    if(vertices.size()+3U>kMaximumVertices||indices.size()+3U>kMaximumIndices)
                        throw std::runtime_error("object preview geometry limit exceeded");
                    const uint32_t base=static_cast<uint32_t>(vertices.size());
                    if(untextured)++untextured_triangle_count_;
                    if(neutral_source_color)++neutral_source_color_triangle_count_;
                    for(const auto* source:corners){
                        uint32_t layer=0;
                        thandor::SpriteTextureUv uv{};
                        if(untextured){
                            layer=untextured_layer();
                        }else{
                            if(source->texture_subresource>=family.graphics.subresources().size())
                                throw std::runtime_error("SPR texture subresource exceeds faction GFX set");
                            const auto& subresource=family.graphics.subresources()[source->texture_subresource];
                            layer=texture_layer(preview_faction,source->texture_subresource);
                            uv=thandor::normalize_sprite_texture_uv(source->texture_u,source->texture_v,
                                subresource.pixel_width,subresource.pixel_height);
                        }
                        const Vec3f world=transform_point(instance.transform,source->position);
                        pick_bounds.include(world);
                        const Vec3f normal=normalize(transform_vector(instance.transform,source->normal));
                        const uint32_t directional_light=
                            level_lighting.directional_color_for_upward_normal(normal,ramp);
                        uint32_t modulation=0xFFFFFFFFU;
                        // armyN.gfx carries the faction colour for textured faces; armyN.pal
                        // is consulted only for genuinely untextured SPR triangles.
                        if(untextured){
                            const uint32_t palette_index=sprite_palette_index(source->render_flags);
                            if(palette_index<family.palette.colors_argb().size())
                                modulation=family.palette.colors_argb()[palette_index];
                        }
                        const uint32_t direct_model_tint=modulation|0xFF000000U;
                        const uint32_t directionally_lit_model_tint=
                            thandor::multiply_rgb_opaque(directional_light,modulation);
                        const uint32_t packed_color=directdraw_select_model_vertex_packed_color(
                            triangle_render_flags,direct_model_tint,directionally_lit_model_tint);
                        const uint32_t direct_tint_bypass=render_policy.packed_static_control_bits(
                            directdraw_model_vertex_uses_direct_tint(triangle_render_flags));
                        const bool animated_part=instance.root&&preset->has_primary_texture_animation&&
                            source->texture_subresource==preset->primary_animated_subresource;
                        vertices.push_back(ObjectVertex{world,normal,uv.u,uv.v,layer,packed_color,
                            direct_tint_bypass,animated_part?0.5F:0.0F,animated_part?80.0F:0.0F});
                    }
                    const uint32_t first_index=static_cast<uint32_t>(indices.size());
                    indices.insert(indices.end(),{base,base+1U,base+2U});
                    append_draw_run(primitive.blend,primitive.two_sided,first_index);

                    if(resource_particle_triangle){
                        emitted_particle=true;++resource_luminous_particle_triangle_count_;
                    }else{
                        emitted_object=true;
                        if(resource_body_triangle){emitted_body=true;++resource_body_triangle_count_;}
                    }
                }

                // game.exe follows the dedicated shadow table at SPR +0xEC. It never
                // substitutes the visible LOD mesh. Each variable-size shadow record
                // contributes to either the blurred first pass or the hard overwrite pass.
                if(projected_object_shadows_&&preset->casts_generated_shadow&&
                   resource_kind==thandor::ResourceEnergyKind::None&&
                   sprite.has_generated_shadow_mesh()){
                    const auto& shadow_mesh=sprite.generated_shadow_mesh();
                    if(shadow_mesh.indices.size()%3U!=0U)
                        throw std::runtime_error("SPR generated-shadow mesh is not triangle aligned");
                    for(std::size_t triangle=0;triangle<shadow_mesh.indices.size();triangle+=3U){
                        const uint32_t ia=shadow_mesh.indices[triangle];
                        const uint32_t ib=shadow_mesh.indices[triangle+1U];
                        const uint32_t ic=shadow_mesh.indices[triangle+2U];
                        if(ia>=shadow_mesh.vertices.size()||ib>=shadow_mesh.vertices.size()||
                           ic>=shadow_mesh.vertices.size())
                            throw std::runtime_error("SPR generated-shadow index exceeds vertex stream");
                        const auto& a=shadow_mesh.vertices[ia];
                        const auto& b=shadow_mesh.vertices[ib];
                        const auto& c=shadow_mesh.vertices[ic];
                        if(a.mesh_record_flags!=b.mesh_record_flags||
                           a.mesh_record_flags!=c.mesh_record_flags)
                            throw std::runtime_error("SPR generated-shadow triangle mixes record flags");
                        generated_shadow_triangles.push_back(thandor::GeneratedShadowTriangle{{
                            transform_point(instance.transform,a.position),
                            transform_point(instance.transform,b.position),
                            transform_point(instance.transform,c.position)},
                            (a.mesh_record_flags&1U)!=0U});
                    }
                }
            }
            if(projected_object_shadows_&&preset->casts_generated_shadow&&
               resource_kind==thandor::ResourceEnergyKind::None&&
               !generated_shadow_triangles.empty()){
                const auto generated=thandor::build_game_generated_object_shadow(
                    document_->field(),generated_shadow_triangles,shadow_projection_direction);
                if(generated){
                    const auto tile=shadow_atlas_tile(generated->mask);
                    std::vector<ObjectVertex> prepared_vertices;
                    prepared_vertices.reserve(generated->receiver_vertices.size());
                    for(const auto& source:generated->receiver_vertices){
                        const float u=tile.u0+(tile.u1-tile.u0)*source.u;
                        const float v=tile.v0+(tile.v1-tile.v0)*source.v;
                        prepared_vertices.push_back(ObjectVertex{source.position,{0.0F,0.0F,1.0F},
                            u,v,tile.layer,0x7F000000U,1U,0.0F,0.0F});
                    }
                    if(simulation_replacement){
                        // Static actor geometry is hidden while a live simulation replacement is
                        // drawn, so its original projected shadow must be hidden as well. Retain
                        // the generated 64x64 mask/receiver as an actor-local template and move it
                        // with the current body pose every presentation tick.
                        DynamicShadowTemplate dynamic_shadow;
                        dynamic_shadow.placement_index=placement_index;
                        dynamic_shadow.anchor_position={placement.render_x(),placement.render_y(),
                            placement_base_z};
                        dynamic_shadow.anchor_yaw_turn16=placement.orientation_turn16&0xFFFFU;
                        dynamic_shadow.receiver_vertices=std::move(prepared_vertices);
                        dynamic_shadow.receiver_indices=generated->receiver_indices;
                        dynamic_shadow_templates_.push_back(std::move(dynamic_shadow));
                    }else{
                        if(shadow_vertices.size()+prepared_vertices.size()>kMaximumVertices||
                           shadow_indices.size()+generated->receiver_indices.size()>kMaximumIndices)
                            throw std::runtime_error("generated shadow receiver geometry limit exceeded");
                        const uint32_t base=static_cast<uint32_t>(shadow_vertices.size());
                        shadow_vertices.insert(shadow_vertices.end(),prepared_vertices.begin(),prepared_vertices.end());
                        for(const uint32_t index:generated->receiver_indices)
                            shadow_indices.push_back(base+index);
                    }
                    projected_shadow_triangle_count_+=generated->receiver_indices.size()/3U;
                    ++generated_shadow_object_count_;
                }
            }
            if(resource_kind==thandor::ResourceEnergyKind::Power&&emitted_body&&emitted_particle){
                ++power_resource_decal_count_;++rendered_object_count_;
            }else if(resource_kind==thandor::ResourceEnergyKind::Building&&emitted_body&&emitted_particle){
                ++building_resource_decal_count_;++rendered_object_count_;
            }else if(resource_kind!=thandor::ResourceEnergyKind::None){
                ++marker_only_object_count_;
            }else if(emitted_object){
                ++rendered_object_count_;
                if(preset->visual_kind==thandor::PlacementVisualKind::Tree)++tree_object_count_;
                if(preset->visual_kind==thandor::PlacementVisualKind::Rock)++rock_object_count_;
            }else{
                ++marker_only_object_count_;
            }
        }catch(const std::exception& error){
            ++marker_only_object_count_;
            ++object_build_error_count_;
            if(first_object_build_error_.empty())first_object_build_error_=error.what();
        }
    }
    timed_effect_controllers_=std::move(pending_timed_effects);
    for(auto& bounds:placement_pick_bounds_)bounds.pad(0.20F);

    if(terrain_compatibility_overlay_!=0){
        const auto* profile=terrain_overlay_profile();
        const auto& field=document_->field();
        const uint32_t layer=untextured_layer();
        const auto append_overlay_vertex=[&](uint32_t x,uint32_t y,uint32_t packed_color){
            const auto& cell=field.at(x,y);
            vertices.push_back(ObjectVertex{{thandor::q12_to_float(cell.world_x_q12),
                thandor::q12_to_float(cell.world_y_q12),cell.terrain_height()+0.08F},
                {0.0F,0.0F,1.0F},0.5F,0.5F,layer,packed_color});
        };
        for(uint32_t y=0;y+1U<field.height();++y){
            for(uint32_t x=0;x+1U<field.width();++x){
                const auto compatibility=field.terrain_compatibility(x,y);
                bool blocked=false;
                bool caution=false;
                if(terrain_compatibility_overlay_==1){
                    blocked=profile==nullptr||!profile->has_traversal_profile||
                        field.terrain_forbidden_for_traversal_band(
                            x,y,profile->traversal_high_band_index);
                    caution=!blocked&&(compatibility.water_covered||
                        compatibility.terrain_normal_elevation_turn16<12000U||
                        compatibility.water_normal_elevation_turn16<12000U);
                }else if(terrain_compatibility_overlay_==2){
                    blocked=profile==nullptr||!profile->has_placement_occupancy_class||
                        field.terrain_rejects_placement_occupancy_class(
                            x,y,profile->placement_occupancy_class);
                    caution=!blocked&&(compatibility.water_covered||
                        compatibility.terrain_normal_elevation_turn16<12000U);
                }else{
                    blocked=compatibility.hard_boundary_or_blocker;
                    caution=!blocked&&(compatibility.classification_bits&0x7F000000U)!=0U;
                }
                uint32_t color=0x5038C96CU;
                if(blocked){color=0x90E54747U;++terrain_overlay_blocked_cell_count_;}
                else if(caution){color=0x78F2B134U;++terrain_overlay_caution_cell_count_;}
                else ++terrain_overlay_safe_cell_count_;
                if(vertices.size()+4U>kMaximumVertices||indices.size()+6U>kMaximumIndices)
                    throw std::runtime_error("terrain compatibility overlay geometry limit exceeded");
                const uint32_t base=static_cast<uint32_t>(vertices.size());
                append_overlay_vertex(x,y,color);
                append_overlay_vertex(x+1U,y,color);
                append_overlay_vertex(x+1U,y+1U,color);
                append_overlay_vertex(x,y+1U,color);
                uint32_t first=static_cast<uint32_t>(indices.size());
                indices.insert(indices.end(),{base,base+1U,base+2U});
                append_draw_run(PrimitiveBlendPreset::AlphaNoDepthWrite,true,first);
                first=static_cast<uint32_t>(indices.size());
                indices.insert(indices.end(),{base,base+2U,base+3U});
                append_draw_run(PrimitiveBlendPreset::AlphaNoDepthWrite,true,first);
            }
        }
    }
    // Dynamic effect and building geometry may need the flat-palette fallback after the
    // static texture array has been uploaded. Reserve it now so the animation path never
    // mutates or reuploads the texture array at presentation cadence.
    (void)untextured_layer();
    object_texture_layer_count_=texture_layers.size();
    object_texture_layer_lookup_=std::move(layer_lookup);
    renderer_.update_objects(vertices,indices,draw_runs,shadow_vertices,shadow_indices,
                             texture_layers);
    rebuild_dynamic_scene_cpu();
    last_dynamic_scene_tick_=current_presentation_tick_;
}

bool EditorApp::placement_visible_in_canvas(Vec3f center,float radius) const{
    if(!document_)return false;
    const auto& io=ImGui::GetIO();
    const float canvas_width=std::max(1.0F,io.DisplaySize.x-kLeftPanelWidth-kRightPanelWidth);
    const float canvas_height=std::max(1.0F,io.DisplaySize.y-ImGui::GetFrameHeight()-kStatusBarHeight);
    const auto vp=camera_.view_projection(canvas_width/canvas_height);
    float screen_x=0.0F,screen_y=0.0F,depth=0.0F;
    if(!project_to_screen(center,vp,io.DisplaySize.x,io.DisplaySize.y,screen_x,screen_y,depth)){
        const float eye_distance=std::sqrt(dot(sub(center,camera_.eye()),sub(center,camera_.eye())));
        return eye_distance<=std::max(2.0F,radius*2.5F);
    }
    const float distance=std::max(radius,std::sqrt(dot(sub(center,camera_.eye()),sub(center,camera_.eye()))));
    const float projected_radius=std::clamp(
        0.5F*canvas_height*radius/(std::tan(camera_.fov_y*0.5F)*distance),8.0F,512.0F);
    const float left=kLeftPanelWidth-projected_radius;
    const float right=io.DisplaySize.x-kRightPanelWidth+projected_radius;
    const float top=ImGui::GetFrameHeight()-projected_radius;
    const float bottom=io.DisplaySize.y-kStatusBarHeight+projected_radius;
    return screen_x>=left&&screen_x<=right&&screen_y>=top&&screen_y<=bottom;
}

bool EditorApp::bounds_visible_in_canvas(const AxisAlignedBounds& source_bounds,float padding) const{
    if(!document_||!source_bounds.valid())return false;
    AxisAlignedBounds bounds=source_bounds;
    bounds.pad(std::max(0.0F,padding));
    const auto& io=ImGui::GetIO();
    const float canvas_width=std::max(1.0F,io.DisplaySize.x-kLeftPanelWidth-kRightPanelWidth);
    const float canvas_height=std::max(1.0F,io.DisplaySize.y-ImGui::GetFrameHeight()-kStatusBarHeight);
    const auto vp=camera_.view_projection(canvas_width/canvas_height);
    const float left=kLeftPanelWidth;
    const float right=io.DisplaySize.x-kRightPanelWidth;
    const float top=ImGui::GetFrameHeight();
    const float bottom=io.DisplaySize.y-kStatusBarHeight;
    float projected_min_x=std::numeric_limits<float>::max();
    float projected_min_y=std::numeric_limits<float>::max();
    float projected_max_x=std::numeric_limits<float>::lowest();
    float projected_max_y=std::numeric_limits<float>::lowest();
    bool projected_any=false;
    for(const float x:{bounds.minimum.x,bounds.maximum.x}){
        for(const float y:{bounds.minimum.y,bounds.maximum.y}){
            for(const float z:{bounds.minimum.z,bounds.maximum.z}){
                float screen_x=0.0F,screen_y=0.0F,depth=0.0F;
                if(!project_to_screen({x,y,z},vp,io.DisplaySize.x,io.DisplaySize.y,
                                      screen_x,screen_y,depth))continue;
                projected_any=true;
                projected_min_x=std::min(projected_min_x,screen_x);
                projected_min_y=std::min(projected_min_y,screen_y);
                projected_max_x=std::max(projected_max_x,screen_x);
                projected_max_y=std::max(projected_max_y,screen_y);
            }
        }
    }
    if(projected_any)
        return projected_max_x>=left&&projected_min_x<=right&&
               projected_max_y>=top&&projected_min_y<=bottom;
    // When the camera is inside or immediately beside a large building, all eight corners may be
    // clipped by the near plane although part of the object still covers the viewport. Keep such
    // bounds alive conservatively instead of popping them as the user zooms in.
    const Vec3f eye=camera_.eye();
    const auto axis_distance=[](float value,float minimum,float maximum){
        return value<minimum?minimum-value:(value>maximum?value-maximum:0.0F);
    };
    const Vec3f outside{axis_distance(eye.x,bounds.minimum.x,bounds.maximum.x),
                        axis_distance(eye.y,bounds.minimum.y,bounds.maximum.y),
                        axis_distance(eye.z,bounds.minimum.z,bounds.maximum.z)};
    const float distance_to_bounds=std::sqrt(dot(outside,outside));
    const Vec3f extent{sub(bounds.maximum,bounds.minimum)};
    const float radius=0.5F*std::sqrt(dot(extent,extent));
    return distance_to_bounds<=std::max(3.0F,radius*1.5F);
}


thandor::SimulationActorState& EditorApp::simulation_state_for(
    std::size_t placement_index,SimulationStateOrigin origin){
    if(!document_||placement_index>=document_->level().placements().size())
        throw std::runtime_error("simulation placement index is out of range");
    if(const auto found=simulation_actor_states_.find(placement_index);
       found!=simulation_actor_states_.end())return found->second;
    const auto& placement=document_->level().placements()[placement_index];
    thandor::SimulationActorState state;
    state.position={placement.render_x(),placement.render_y(),
        terrain_height_near(placement.render_x(),placement.render_y())};
    state.body_yaw_turn16=placement.orientation_turn16&0xFFFFU;
    state.last_updated_tick=current_presentation_tick_;
    if(const auto* preset=catalog_.army_preset(placement.army_asset_id);preset){
        const auto hierarchy=placement_hierarchy(*preset,placement_index);
        state.weapons.resize(preset->simulation_weapon_profiles.size());
        if(preset->runtime_model_class_id==17U)
            thandor::simulation_initialize_class17_deployment(
                state,origin==SimulationStateOrigin::FreshPlacement);
        for(std::size_t weapon_index=0;weapon_index<preset->simulation_weapon_profiles.size();++weapon_index){
            const auto& weapon=preset->simulation_weapon_profiles[weapon_index];
            auto& weapon_state=state.weapons[weapon_index];
            weapon_state.launch_mesh_group_mask=0xFFFFFFFFU;
            if(weapon.yaw_model_node_index<hierarchy.size())
                weapon_state.yaw.angle_turn16=hierarchy[weapon.yaw_model_node_index].local_rotation_angle2&0xFFFFU;
            if(weapon.pitch_model_node_index<hierarchy.size())
                weapon_state.pitch.angle_turn16=hierarchy[weapon.pitch_model_node_index].local_rotation_angle1&0xFFFFU;
        }
    }
    auto [inserted,created]=simulation_actor_states_.emplace(placement_index,std::move(state));
    if(created)simulation_static_replacement_dirty_=true;
    return inserted->second;
}

std::vector<ModelHierarchyTransform> EditorApp::simulation_hierarchy_transforms(
    std::size_t placement_index,const thandor::ArmyAssetPreset& preset,
    thandor::SimulationActorState& actor){
    std::vector<ModelHierarchyTransform> transforms;
    if(!document_||placement_index>=document_->level().placements().size())return transforms;
    const auto hierarchy=placement_hierarchy(preset,placement_index);
    transforms.resize(hierarchy.size());
    std::vector<const thandor::SpriteAsset*> sprites(hierarchy.size(),nullptr);
    std::vector<bool> active(hierarchy.size(),false);
    const auto launch_node_selected_for_recoil=[](const thandor::SimulationWeaponProfile& profile,
                                                   const thandor::SimulationWeaponState& state,
                                                   std::size_t node_index){
        const auto& nodes=profile.launch_model_node_indices;
        if(nodes.empty())return node_index==profile.launch_model_node_index;
        if(profile.launch_policy==thandor::SimulationLaunchPolicy::AlternateLaunchNodes)
            return node_index==nodes[state.recoil_launch_selector%nodes.size()];
        return std::find(nodes.begin(),nodes.end(),static_cast<uint32_t>(node_index))!=nodes.end();
    };
    for(std::size_t node_index=0;node_index<hierarchy.size();++node_index){
        const auto& node=hierarchy[node_index];
        if(!node.sprite_path.empty()){
            auto found=sprite_cache_.find(node.sprite_path);
            if(found==sprite_cache_.end())found=sprite_cache_.emplace(node.sprite_path,
                thandor::SpriteAsset::parse(catalog_.load(node.sprite_path))).first;
            sprites[node_index]=&found->second;
        }
        bool node_active=node.creates_runtime_node()&&sprites[node_index]!=nullptr;
        if(node.parent_index==std::numeric_limits<uint32_t>::max()){
            uint32_t root_yaw=actor.body_yaw_turn16;
            int32_t root_recoil=0;
            for(std::size_t weapon_index=0;
                weapon_index<preset.simulation_weapon_profiles.size()&&weapon_index<actor.weapons.size();
                ++weapon_index){
                const auto& profile=preset.simulation_weapon_profiles[weapon_index];
                const auto& state=actor.weapons[weapon_index];
                if(profile.yaw_model_node_index==node_index)
                    root_yaw=thandor::wrap_turn16(static_cast<int64_t>(root_yaw)+state.yaw.angle_turn16);
                if(launch_node_selected_for_recoil(profile,state,node_index))
                    root_recoil+=state.recoil_angle_offset_turn16;
            }
            root_yaw=thandor::wrap_turn16(static_cast<int64_t>(root_yaw)+root_recoil);
            const auto contact=placement_contact_pose(preset,actor.position.x,actor.position.y,
                sprites[node_index]?sprites[node_index]->placement_height_offset():0.0F);
            const Vec3f contact_position{actor.position.x,actor.position.y,contact.height};
            transforms[node_index]=preset.runtime_model_class_id==17U
                ?class17_banked_root_transform(contact_position,root_yaw,
                    actor.class17_bank_heading_turn16,actor.class17_bank_angle_turn16,
                    contact.align_to_normal?contact.normal:Vec3f{0.0F,0.0F,1.0F})
                :(contact.align_to_normal
                    ?terrain_contact_root_transform(contact_position,
                        thandor::turn16_to_radians(root_yaw),contact.normal)
                    :placement_root_transform(contact_position.x,contact_position.y,contact_position.z,
                        thandor::turn16_to_radians(root_yaw)));
        }else{
            if(node.parent_index>=node_index||!active[node.parent_index]||sprites[node.parent_index]==nullptr)
                node_active=false;
            if(node_active){
                const auto* attachment=sprites[node.parent_index]->child_attachment(node.child_slot);
                if(!attachment){node_active=false;}
                else{
                    uint32_t angle2=node.local_rotation_angle2;
                    uint32_t angle1=node.local_rotation_angle1;
                    int32_t recoil_translation_q12=0;
                    int32_t recoil_angle_turn16=0;
                    bool simulated_node=false;
                    for(std::size_t weapon_index=0;
                        weapon_index<preset.simulation_weapon_profiles.size()&&weapon_index<actor.weapons.size();
                        ++weapon_index){
                        const auto& profile=preset.simulation_weapon_profiles[weapon_index];
                        const auto& state=actor.weapons[weapon_index];
                        if(profile.yaw_model_node_index==node_index){
                            angle2=state.yaw.angle_turn16;simulated_node=true;
                        }
                        if(profile.pitch_model_node_index==node_index){
                            angle1=state.pitch.angle_turn16;simulated_node=true;
                        }
                        if(launch_node_selected_for_recoil(profile,state,node_index)){
                            recoil_angle_turn16+=state.recoil_angle_offset_turn16;
                            recoil_translation_q12+=state.recoil_translation_offset_q12;
                            simulated_node=true;
                        }
                    }
                    const bool is_building=
                        preset.browser_kind==thandor::PlacementBrowserKind::Building;
                    const bool animate_authored_subnodes=preset.has_model_subnode_animation&&
                        ((is_building&&animate_buildings_)||(!is_building&&animate_units_));
                    const uint32_t authored_animation_tick=is_building
                        ?current_building_animation_tick_:current_presentation_tick_;
                    const bool root_child=node.parent_index<hierarchy.size()&&
                        hierarchy[node.parent_index].parent_index==std::numeric_limits<uint32_t>::max();
                    if(preset.runtime_model_class_id==17U&&root_child&&node.child_slot<3U){
                        angle1=thandor::wrap_turn16(static_cast<int64_t>(angle1)+
                            actor.class17_rotor_tilt_turn16);
                        simulated_node=true;
                    }else if(!simulated_node&&animate_authored_subnodes&&root_child&&
                             !thandor::simulation_model_node_is_weapon_aim_axis(
                                 preset.simulation_weapon_profiles,static_cast<uint32_t>(node_index))){
                        if(node.child_slot==0U&&preset.child0_rotation_step_turn16!=0)
                            angle2=thandor::wrap_turn16(static_cast<int64_t>(angle2)+
                                static_cast<int64_t>(preset.child0_rotation_step_turn16)*authored_animation_tick);
                        else if(node.child_slot==1U&&preset.child1_rotation_step_turn16!=0)
                            angle2=thandor::wrap_turn16(static_cast<int64_t>(angle2)+
                                static_cast<int64_t>(preset.child1_rotation_step_turn16)*authored_animation_tick);
                    }
                    angle2=thandor::wrap_turn16(static_cast<int64_t>(angle2)+recoil_angle_turn16);
                    auto local=fixed_model_rotation_basis(angle2,angle1,node.local_rotation_angle0);
                    local.translation=attachment->local_translation;
                    if(animate_authored_subnodes&&node.parent_index<hierarchy.size()&&
                       hierarchy[node.parent_index].parent_index==std::numeric_limits<uint32_t>::max()&&
                       node.child_slot==2U&&preset.child2_vertical_step_q12!=0)
                        local.translation.z=animated_child2_translation_q12(preset,
                            authored_animation_tick,local.translation.z);
                    if(recoil_translation_q12!=0){
                        const Vec3f recoil=transform_vector(local,{thandor::q12_to_float(recoil_translation_q12),0.0F,0.0F});
                        local.translation=add(local.translation,recoil);
                    }
                    transforms[node_index]=compose_model_transform(transforms[node.parent_index],local);
                }
            }
        }
        active[node_index]=node_active;
    }
    if(preset.runtime_model_class_id==3U){
        apply_articulated_walker_support_pose(hierarchy,transforms,
            preset.articulated_root_height_q12,preset.articulated_contact_lift_q12,&actor,
            [&](float x,float y){return terrain_height_near(x,y);},
            [&](uint32_t node_index){
                return node_index<sprites.size()&&sprites[node_index]!=nullptr
                    ?sprites[node_index]->placement_height_offset():0.0F;
            });
    }
    return transforms;
}

std::vector<Vec3f> EditorApp::simulation_weapon_launch_points(
    std::size_t placement_index,const thandor::SimulationWeaponProfile& weapon,
    const thandor::SimulationWeaponState& weapon_state,
    const std::vector<ModelHierarchyTransform>& transforms,
    uint32_t* available_selector_mask){
    std::vector<Vec3f> result;
    if(available_selector_mask)*available_selector_mask=0U;
    if(!document_||placement_index>=document_->level().placements().size())return result;
    const auto& placement=document_->level().placements()[placement_index];
    const auto* preset=catalog_.army_preset(placement.army_asset_id);
    if(!preset)return result;
    const auto hierarchy=placement_hierarchy(*preset,placement_index);
    std::vector<uint32_t> launch_nodes=weapon.launch_model_node_indices;
    if(launch_nodes.empty())launch_nodes.push_back(weapon.launch_model_node_index);
    if(weapon.launch_policy==thandor::SimulationLaunchPolicy::AlternateLaunchNodes&&
       !launch_nodes.empty()){
        const uint32_t selected=launch_nodes[weapon_state.next_launch_attachment%launch_nodes.size()];
        launch_nodes.assign(1U,selected);
    }
    struct LaunchPoint { Vec3f position{}; uint32_t selector{}; };
    std::vector<LaunchPoint> flattened;
    for(const uint32_t launch_node:launch_nodes){
        if(launch_node>=hierarchy.size()||launch_node>=transforms.size())continue;
        const auto& node=hierarchy[launch_node];
        auto found=sprite_cache_.find(node.sprite_path);
        if(found==sprite_cache_.end())found=sprite_cache_.emplace(node.sprite_path,
            thandor::SpriteAsset::parse(catalog_.load(node.sprite_path))).first;
        for(const auto& attachment:found->second.lookup_records()){
            if(attachment.key_class().value()!=2U)continue;
            if(available_selector_mask&&attachment.key_index().value()<32U)
                *available_selector_mask|=1U<<attachment.key_index().value();
            flattened.push_back({transform_point(transforms[launch_node],attachment.local_translation),
                                 attachment.key_index().value()});
        }
    }
    if(weapon.launch_policy==thandor::SimulationLaunchPolicy::SequentialKind2Attachments&&
       !flattened.empty()){
        const uint32_t selector=weapon_state.last_fired_attachment_selector!=0xFFFFFFFFU
            ?weapon_state.last_fired_attachment_selector
            :weapon_state.next_launch_attachment;
        if(weapon.runtime_class_id==9U){
            const auto selected=std::find_if(flattened.begin(),flattened.end(),
                [&](const LaunchPoint& point){return point.selector==selector;});
            if(selected!=flattened.end())result.push_back(selected->position);
        }else{
            // Class 21's timed attachment callback advances through the flattened model-point
            // sequence; its repeated child-index values are not class-9 timer selectors.
            result.push_back(flattened[selector%flattened.size()].position);
        }
        return result;
    }
    result.reserve(flattened.size());
    for(const auto& point:flattened)result.push_back(point.position);
    return result;
}

void EditorApp::synchronize_simulated_actor_placement(
    std::size_t placement_index,const thandor::SimulationActorState& actor){
    if(!document_||placement_index>=document_->level().placements().size())return;
    auto& placement=document_->level().placements()[placement_index];
    const Vec3f old_root{placement.render_x(),placement.render_y(),
        terrain_height_near(placement.render_x(),placement.render_y())};
    const uint32_t old_yaw=placement.orientation_turn16&0xFFFFU;
    const int32_t x_q12=thandor::float_to_q12(actor.position.x);
    const int32_t y_q12=thandor::float_to_q12(actor.position.y);
    const uint32_t yaw=actor.body_yaw_turn16&0xFFFFU;
    if(placement.world_y_q12==x_q12&&placement.world_x_q12==y_q12&&old_yaw==yaw)return;
    placement.set_render_position_q12(x_q12,y_q12);
    placement.orientation_turn16=yaw;
    const Vec3f new_root{placement.render_x(),placement.render_y(),
        terrain_height_near(placement.render_x(),placement.render_y())};
    if(placement_index<placement_pick_bounds_.size()&&
       placement_index<placement_pick_hulls_.size()){
        const float yaw_delta=thandor::turn16_to_radians(
            thandor::wrap_turn16(static_cast<int64_t>(yaw)-old_yaw));
        const float cosine=std::cos(yaw_delta);
        const float sine=std::sin(yaw_delta);
        AxisAlignedBounds updated_bounds;
        for(auto& hull:placement_pick_hulls_[placement_index]){
            if(!hull.valid)continue;
            for(auto& corner:hull.corners){
                const Vec3f relative=sub(corner,old_root);
                corner={new_root.x+relative.x*cosine-relative.y*sine,
                        new_root.y+relative.x*sine+relative.y*cosine,
                        new_root.z+relative.z};
                updated_bounds.include(corner);
            }
        }
        placement_pick_bounds_[placement_index]=updated_bounds;
        placement_pick_bounds_[placement_index].pad(0.20F);
    }
    document_->mark_dirty();
}

bool EditorApp::simulation_line_of_fire_clear(Vec3f source,Vec3f target) const{
    if(!document_)return false;
    const Vec3f delta=sub(target,source);
    const float distance=vec3_length(delta);
    if(!std::isfinite(distance)||distance<=0.24F)return true;
    const Vec3f direction=mul(delta,1.0F/distance);
    // The binary tests the primary field surface with the same bounded traversal used by world
    // picking and projectile collision. Skip a short endpoint margin so the source and intended
    // target contact surfaces do not self-occlude the shot.
    constexpr float endpoint_margin=0.12F;
    const Vec3f ray_origin=add(source,mul(direction,endpoint_margin));
    const auto hit=thandor::raycast_field_surface_compatibility(
        document_->field(),ray_origin,direction,distance-endpoint_margin*2.0F,
        thandor::FieldRaycastSurface::Terrain,0x400U);
    return !hit.has_value();
}

void EditorApp::update_idle_class17_deployment(uint32_t presentation_tick){
    if(simulation_mode_||!document_||!animate_units_)return;
    bool changed=false;
    const auto placements=document_->level().placements();
    for(auto& [index,actor]:simulation_actor_states_){
        if(index>=placements.size()||actor.class17_rotor_deploy_ticks_remaining==0U)continue;
        const auto* preset=catalog_.army_preset(placements[index].army_asset_id);
        if(preset==nullptr||preset->runtime_model_class_id!=17U)continue;
        if(presentation_tick<=actor.last_updated_tick)continue;
        const uint32_t steps=std::min<uint32_t>(presentation_tick-actor.last_updated_tick,128U);
        for(uint32_t step=0;step<steps;++step)
            thandor::simulation_tick_class17_deployment(actor);
        actor.last_updated_tick=presentation_tick;
        changed=true;
    }
    if(changed)last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
}

void EditorApp::reset_simulation(bool keep_target){
    const bool had_actor_replacements=!simulation_actor_states_.empty();
    simulation_actor_states_.clear();
    simulation_attack_placement_targets_.clear();
    simulation_attack_point_targets_.clear();
    simulation_production_rally_points_.clear();
    hovered_placement_.reset();hovered_terrain_point_.reset();
    hovered_context_order_=thandor::ContextOrderKind::None;
    simulation_projectiles_.clear();
    next_simulation_projectile_serial_=1U;
    simulation_active_actor_count_=0U;
    simulation_projectile_count_=0U;
    simulation_volley_count_=0U;
    last_simulation_tick_=std::numeric_limits<uint32_t>::max();
    if(!keep_target)simulation_target_point_.reset();
    if(had_actor_replacements)simulation_static_replacement_dirty_=true;
    last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
}

void EditorApp::update_simulation(uint32_t presentation_tick){
    if(!simulation_mode_){
        update_idle_class17_deployment(presentation_tick);
        last_simulation_tick_=presentation_tick;
        return;
    }
    if(!document_){last_simulation_tick_=presentation_tick;return;}
    if(last_simulation_tick_==std::numeric_limits<uint32_t>::max()){
        last_simulation_tick_=presentation_tick;return;
    }
    if(presentation_tick<=last_simulation_tick_)return;
    const uint32_t steps=std::min<uint32_t>(presentation_tick-last_simulation_tick_,128U);
    const uint32_t first_tick=presentation_tick-steps+1U;
    const auto placements=document_->level().placements();
    std::optional<std::size_t> exclusive_target_actor;
    if(selected_placement_&&*selected_placement_<placements.size()){
        if(const auto* selected=catalog_.army_preset(placements[*selected_placement_].army_asset_id);
           selected&&!selected->simulation_weapon_profiles.empty()){
            const bool enabled=(selected->browser_kind==thandor::PlacementBrowserKind::Building&&animate_buildings_)||
                               (selected->browser_kind==thandor::PlacementBrowserKind::Unit&&animate_units_);
            if(enabled&&simulation_selected_only_)exclusive_target_actor=*selected_placement_;
        }
    }
    for(uint32_t step=0;step<steps;++step){
        const uint32_t tick=first_tick+step;
        for(std::size_t index=0;index<placements.size();++index){
            const auto& placement=placements[index];
            const auto* preset=catalog_.army_preset(placement.army_asset_id);
            if(!preset)continue;
            const bool enabled=(preset->browser_kind==thandor::PlacementBrowserKind::Building&&animate_buildings_)||
                               (preset->browser_kind==thandor::PlacementBrowserKind::Unit&&animate_units_);
            if(!enabled)continue;
            const bool has_motion=preset->simulation_motion_profile.movable();
            const bool has_weapon=!preset->simulation_weapon_profiles.empty();
            if(!has_motion&&!has_weapon)continue;
            std::optional<Vec3f> resolved_target=simulation_attack_target_point(index);
            const bool has_explicit_target=resolved_target.has_value();
            if(!resolved_target&&has_weapon&&simulation_target_point_&&
               (!exclusive_target_actor||*exclusive_target_actor==index))
                resolved_target=simulation_target_point_;
            const bool receives_target=has_weapon&&resolved_target.has_value();
            const bool has_existing_state=simulation_actor_states_.contains(index);
            if(!receives_target&&!has_existing_state)continue;
            auto& actor=simulation_state_for(index);
            if(actor.weapons.size()!=preset->simulation_weapon_profiles.size())
                actor.weapons.resize(preset->simulation_weapon_profiles.size());
            const Vec3f previous_actor_position=actor.position;
            const uint32_t previous_body_yaw=actor.body_yaw_turn16;
            const bool actor_moved=preset->runtime_model_class_id==3U
                ?thandor::simulation_tick_articulated_actor_motion(
                    actor,preset->simulation_motion_profile,
                    preset->articulated_contact_interpolation_rate_q12)
                :thandor::simulation_tick_actor_motion(actor,preset->simulation_motion_profile);
            if(preset->runtime_model_class_id==2U&&
               preset->has_primary_texture_animation&&
               preset->has_secondary_texture_animation){
                const auto hierarchy=placement_hierarchy(*preset,index);
                const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){
                    return node.parent_index==std::numeric_limits<uint32_t>::max();
                });
                if(root!=hierarchy.end()&&!root->sprite_path.empty()){
                    auto found=sprite_cache_.find(root->sprite_path);
                    if(found==sprite_cache_.end())found=sprite_cache_.emplace(root->sprite_path,
                        thandor::SpriteAsset::parse(catalog_.load(root->sprite_path))).first;
                    thandor::simulation_update_tank_track_offsets(actor,previous_actor_position,
                        previous_body_yaw,actor.position,actor.body_yaw_turn16,
                        found->second.local_bounds_min().y,preset->track_texture_travel_scale);
                }
            }
            if(preset->runtime_model_class_id==3U)
                thandor::simulation_update_articulated_contact_transition(
                    actor,previous_actor_position,previous_body_yaw,actor.position,
                    actor.body_yaw_turn16,preset->articulated_contact_interpolation_rate_q12);
            thandor::simulation_tick_class17_pose(actor,actor_moved);
            auto transforms=simulation_hierarchy_transforms(index,*preset,actor);
            const auto hierarchy=placement_hierarchy(*preset,index);
            for(std::size_t node_index=0;node_index<hierarchy.size()&&node_index<transforms.size();++node_index){
                if(hierarchy[node_index].parent_index==std::numeric_limits<uint32_t>::max()){
                    actor.position.z=transforms[node_index].translation.z;
                    break;
                }
            }
            if(has_motion&&(actor.position.x!=previous_actor_position.x||
                           actor.position.y!=previous_actor_position.y||
                           actor.body_yaw_turn16!=previous_body_yaw))
                synchronize_simulated_actor_placement(index,actor);
            // Entity targets are resolved again every 20 Hz step so a moving hostile placement is
            // followed rather than reduced to the coordinate it occupied when the order began.
            if(has_explicit_target)resolved_target=simulation_attack_target_point(index);
            const Vec3f* target=receives_target&&resolved_target?&*resolved_target:nullptr;
            actor.target_in_range=false;actor.target_aligned=false;
            for(std::size_t weapon_index=0;weapon_index<preset->simulation_weapon_profiles.size();++weapon_index){
                const auto& weapon=preset->simulation_weapon_profiles[weapon_index];
                const auto* shot=catalog_.shot_definition(weapon.shot_definition_id);
                if(!shot)continue;
                uint32_t available_launch_selector_mask=0U;
                auto launch_points=simulation_weapon_launch_points(index,weapon,actor.weapons[weapon_index],
                    transforms,&available_launch_selector_mask);
                const Vec3f launch_origin=launch_points.empty()?actor.position:launch_points.front();
                Vec3f aim_origin=launch_origin;
                if(weapon.aim_model_node_index<transforms.size())
                    aim_origin=transforms[weapon.aim_model_node_index].translation;
                // Class 9 solves yaw/pitch from root->child[0], then rebuilds the hierarchy and
                // launches from the exact selected kind-2 point. Line-of-fire still starts at the
                // muzzle, while turret aiming uses the recovered pivot.
                const bool clear=!target||simulation_line_of_fire_clear(launch_origin,*target);
                auto result=thandor::simulation_tick_weapon(actor,actor.weapons[weapon_index],weapon,
                    *shot,aim_origin,target,clear,available_launch_selector_mask);
                actor.target_in_range|=result.in_range;
                actor.target_aligned|=result.aligned;
                if(!result.fired||!target)continue;
                auto& weapon_state=actor.weapons[weapon_index];
                weapon_state.recoil_launch_selector=result.fired_attachment_selector!=0xFFFFFFFFU
                    ?result.fired_attachment_selector:weapon_state.next_launch_attachment;
                // Rebuild after the aim/recoil update so launch effects and SHT flight sprites
                // originate at the exact kind-2 attachment on the fired recoil leaf.
                transforms=simulation_hierarchy_transforms(index,*preset,actor);
                launch_points=simulation_weapon_launch_points(index,weapon,weapon_state,transforms);
                if(launch_points.empty())continue;
                auto volley=thandor::simulation_create_volley(index,weapon_index,weapon,
                    launch_points,*target,*shot,tick,next_simulation_projectile_serial_,
                    result.fired_attachment_selector);
                if(!volley.empty()){
                    std::optional<uint32_t> target_impact_class;
                    if(const auto target_placement=simulation_attack_placement_targets_.find(index);
                       target_placement!=simulation_attack_placement_targets_.end()&&
                       target_placement->second<placements.size()){
                        if(const auto* target_preset=catalog_.army_preset(
                               placements[target_placement->second].army_asset_id))
                            target_impact_class=target_preset->shot_impact_class_index;
                    }
                    for(auto& projectile:volley){
                        if(target_impact_class&&*target_impact_class<8U){
                            // The executable resolves the nearest valid collision among model,
                            // terrain, and secondary surface. Preserve a terrain/water crossing
                            // that occurs before the selected model aim point instead of forcing
                            // every entity-targeted shot through the target impact table.
                            auto surface_candidate=projectile;
                            thandor::simulation_resolve_projectile_surface_impact(
                                surface_candidate,*shot,document_->field());
                            const auto squared_distance=[](Vec3f a,Vec3f b){
                                const Vec3f delta=sub(a,b);
                                return delta.x*delta.x+delta.y*delta.y+delta.z*delta.z;
                            };
                            const bool surface_is_nearer=
                                surface_candidate.impact_kind!=thandor::SimulationImpactKind::None&&
                                squared_distance(projectile.launch_position,
                                    surface_candidate.impact_position)+1.0e-6F<
                                squared_distance(projectile.launch_position,*target);
                            if(surface_is_nearer){
                                projectile=surface_candidate;
                            }else{
                                projectile.impact_kind=thandor::SimulationImpactKind::TargetModelImpactClass;
                                projectile.impact_position=*target;
                                projectile.target_position=*target;
                                projectile.impact_effect_id=
                                    shot->target_impact_effect_id(*target_impact_class);
                            }
                        }else{
                            thandor::simulation_resolve_projectile_surface_impact(
                                projectile,*shot,document_->field());
                        }
                        if(const auto* impact=catalog_.effect_definition(projectile.impact_effect_id))
                            projectile.impact_visual_lifetime_ticks=
                                std::max(1U,thandor::effect_lifetime_ticks(*impact));
                        else
                            projectile.impact_visual_lifetime_ticks=1U;
                    }
                    simulation_projectiles_.insert(simulation_projectiles_.end(),volley.begin(),volley.end());
                    ++simulation_volley_count_;
                    if(weapon.launch_policy==thandor::SimulationLaunchPolicy::AlternateLaunchNodes){
                        const auto count=weapon.launch_model_node_indices.empty()?1U:
                            static_cast<uint32_t>(weapon.launch_model_node_indices.size());
                        weapon_state.next_launch_attachment=(weapon_state.next_launch_attachment+1U)%count;
                    }
                }
            }
            actor.last_updated_tick=tick;
        }
    }
    last_simulation_tick_=presentation_tick;
    simulation_active_actor_count_=simulation_actor_states_.size();
    simulation_projectiles_.erase(std::remove_if(simulation_projectiles_.begin(),simulation_projectiles_.end(),
        [&](const thandor::SimulationProjectileState& projectile){
            return thandor::simulation_projectile_expired(projectile,presentation_tick);
        }),simulation_projectiles_.end());
    simulation_projectile_count_=simulation_projectiles_.size();
    last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
}

void EditorApp::commit_selected_simulation_position(){
    if(!document_||!selected_placement_)return;
    const auto found=simulation_actor_states_.find(*selected_placement_);
    if(found==simulation_actor_states_.end()){status_="Selected unit has no simulated position.";return;}
    synchronize_simulated_actor_placement(*selected_placement_,found->second);
    simulation_actor_states_.erase(found);
    simulation_attack_placement_targets_.erase(*selected_placement_);
    simulation_attack_point_targets_.erase(*selected_placement_);
    simulation_static_replacement_dirty_=true;
    last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
    status_="Finished driving. The current X/Y and whole-placement yaw are already stored in the LEV record.";
}

void EditorApp::bake_selected_target_facing(){
    if(!document_||!selected_placement_){
        status_="Select an armed tower/building first.";return;
    }
    auto resolved_target=simulation_attack_target_point(*selected_placement_);
    if(!resolved_target&&simulation_target_point_)resolved_target=simulation_target_point_;
    if(!resolved_target){
        status_="Assign an attack point or different-faction entity target first.";return;
    }
    auto placements=document_->level().placements();
    if(*selected_placement_>=placements.size())return;
    auto& placement=placements[*selected_placement_];
    const auto* preset=catalog_.army_preset(placement.army_asset_id);
    if(!preset||preset->simulation_weapon_profiles.empty()){
        status_="The selected placement has no recovered projectile weapon.";return;
    }
    checkpoint();
    const auto& state=simulation_state_for(*selected_placement_);
    const float dx=resolved_target->x-state.position.x;
    const float dy=resolved_target->y-state.position.y;
    placement.orientation_turn16=thandor::radians_to_turn16(std::atan2(dy,dx));
    document_->mark_dirty();
    simulation_actor_states_.erase(*selected_placement_);
    simulation_static_replacement_dirty_=false;
    rebuild_objects();
    status_="Baked whole-placement facing toward the target. LEV has no independent persistent turret-angle field.";
}

float EditorApp::fog_preview_observer_radius(
    const thandor::ArmyAssetPreset& preset)const noexcept{
    // Exact retail authority: max(MDL +0x48) across the resolved live model hierarchy. Weapon
    // range, browser kind, and the presence of a rotating radar model do not alter this value.
    const float radius=thandor::q12_to_float(std::max(0,preset.fog_uncover_radius_q12));
    return std::max(0.0F,radius*fog_preview_radius_scale_);
}

bool EditorApp::fog_preview_point_visible(Vec3f point)const noexcept{
    if(fog_preview_mode_==0)return true;
    return thandor::fog_preview_point_visible(fog_preview_observers_,point);
}

void EditorApp::rebuild_fog_preview_visibility(){
    fog_preview_observers_.clear();
    fog_preview_visible_cells_.clear();
    fog_preview_visible_cell_count_=0U;
    fog_preview_observer_count_=0U;
    if(!document_||fog_preview_mode_==0)return;
    const auto placements=document_->level().placements();
    const uint32_t active_factions=std::clamp(
        document_->player_ai_configuration().active_factions,1U,7U);
    for(std::size_t index=0;index<placements.size();++index){
        const auto& placement=placements[index];
        if(placement.faction_index==0U||placement.faction_index>7U)continue;
        if(fog_preview_mode_==1&&placement.faction_index!=static_cast<uint32_t>(fog_preview_faction_))continue;
        if(fog_preview_mode_==2&&placement.faction_index>active_factions)continue;
        const auto* preset=catalog_.army_preset(placement.army_asset_id);
        if(!preset||(preset->browser_kind!=thandor::PlacementBrowserKind::Unit&&
                     preset->browser_kind!=thandor::PlacementBrowserKind::Building))continue;
        Vec3f position{placement.render_x(),placement.render_y(),0.0F};
        if(const auto live=simulation_actor_states_.find(index);live!=simulation_actor_states_.end())
            position=live->second.position;
        const float uncover_radius=fog_preview_observer_radius(*preset);
        if(uncover_radius<=0.0F)continue;
        fog_preview_observers_.push_back({position,uncover_radius});
    }
    fog_preview_observer_count_=fog_preview_observers_.size();
    const auto& field=document_->field();
    fog_preview_visible_cells_=thandor::build_radial_fog_preview_visibility(
        field,fog_preview_observers_);
    fog_preview_visible_cell_count_=static_cast<std::size_t>(std::count(
        fog_preview_visible_cells_.begin(),fog_preview_visible_cells_.end(),uint8_t{1}));
}

void EditorApp::rebuild_dynamic_scene_cpu(){
    const auto started=std::chrono::steady_clock::now();
    DynamicSceneCpu next;
    visible_animated_buildings_.clear();
    visible_timed_effect_controllers_.clear();
    visible_animated_building_count_=0;
    visible_timed_effect_controller_count_=0;
    dynamic_triangle_count_=0;
    timed_effect_sprite_triangle_count_=0;
    lava_linked_shot_particle_count_=0;
    if(!document_){
        dynamic_scene_cpu_=std::move(next);
        ++dynamic_scene_revision_;
        dynamic_scene_build_milliseconds_=0.0;
        return;
    }
    rebuild_fog_preview_visibility();
    constexpr std::size_t kMaximumDynamicVertices=500'000U;
    constexpr std::size_t kMaximumDynamicIndices=1'500'000U;
    constexpr uint64_t kEffectLayerNamespace=uint64_t{1}<<63U;
    constexpr uint64_t kUntexturedLayerKey=std::numeric_limits<uint64_t>::max();
    const auto& level_lighting=document_->level().lighting();
    const auto ramp=level_lighting.build_game_ramp();
    const std::string army_base=document_->level().paths().army_textures;
    const std::string effect_base=terrain_base_path(document_->level().paths().effect_textures);

    const auto texture_family=[&](uint32_t faction)->const ArmyTextureFamily&{
        const uint32_t clamped=std::min(faction,8U);
        const std::string key=terrain_base_path(army_base)+"#"+std::to_string(clamped);
        auto found=army_texture_cache_.find(key);
        if(found==army_texture_cache_.end()){
            auto family=std::make_shared<ArmyTextureFamily>(ArmyTextureFamily{
                thandor::GfxAsset::parse(catalog_.load(army_texture_family_path(army_base,clamped,".gfx"))),
                thandor::PalAsset::parse(catalog_.load(army_texture_family_path(army_base,clamped,".pal")))});
            found=army_texture_cache_.emplace(key,std::move(family)).first;
        }
        return *found->second;
    };
    const auto effect_texture_family=[&]()->const ArmyTextureFamily&{
        const std::string key=effect_base+"#effect";
        const auto found=army_texture_cache_.find(key);
        if(found==army_texture_cache_.end())throw std::runtime_error("effect texture family was not prepared by the static scene");
        return *found->second;
    };
    const auto layer_for_key=[&](uint64_t key)->uint32_t{
        const auto found=object_texture_layer_lookup_.find(key);
        if(found==object_texture_layer_lookup_.end())throw std::runtime_error("dynamic scene selected an object texture that was not preloaded");
        return found->second;
    };
    const auto object_layer=[&](uint32_t faction,uint32_t subresource){
        return layer_for_key((static_cast<uint64_t>(std::min(faction,8U))<<32U)|subresource);
    };
    const auto effect_layer=[&](uint32_t subresource){return layer_for_key(kEffectLayerNamespace|subresource);};
    const auto untextured_layer=[&](){return layer_for_key(kUntexturedLayerKey);};
    const auto load_sprite=[&](const std::string& path)->const thandor::SpriteAsset&{
        auto found=sprite_cache_.find(path);
        if(found==sprite_cache_.end())found=sprite_cache_.emplace(path,
            thandor::SpriteAsset::parse(catalog_.load(path))).first;
        return found->second;
    };
    const auto projectile_model_pose=[&](const thandor::SimulationProjectileState& projectile,
                                          const thandor::ShotDefinitionRecord& shot,
                                          uint32_t age_ticks,Vec3f position,Vec3f next_position){
        uint32_t yaw_turn16=projectile.launch_yaw_turn16;
        uint32_t pitch_turn16=projectile.launch_pitch_turn16;
        if(shot.trajectory_mode==3){
            // Creation already stored direct pitch plus the full mode-3 elevation offset.
            // Maintenance moves along direct pitch while the visual offset decays. Adding the
            // live offset to launch_pitch a second time made rockets start above the launcher and
            // fly on an invented rising path.
            pitch_turn16=thandor::simulation_mode3_model_pitch_turn16(
                projectile,shot,age_ticks);
        }else if(shot.trajectory_mode!=0){
            const auto current_angles=thandor::simulation_target_angles(position,next_position,0);
            if(current_angles.valid){
                yaw_turn16=current_angles.yaw_turn16;
                pitch_turn16=current_angles.pitch_turn16;
            }
        }
        const uint32_t spin_turn16=thandor::wrap_turn16(
            static_cast<int64_t>(shot.model_spin_step_turn16)*age_ticks);
        return shot_model_transform(position,yaw_turn16,pitch_turn16,spin_turn16);
    };
    uint32_t next_effect_sort_group=1U;
    const auto append_draw_run=[&](PrimitiveBlendPreset blend,bool two_sided,uint32_t first_index,
                                   uint32_t sort_group=0U){
        if(!next.draw_runs.empty()){
            auto& previous=next.draw_runs.back();
            if(previous.blend==blend&&previous.two_sided==two_sided&&
               previous.sort_group==sort_group&&
               previous.first_index+previous.index_count==first_index){
                previous.index_count+=3U;return;
            }
        }
        next.draw_runs.push_back(ObjectDrawRun{blend,two_sided,first_index,3U,sort_group});
    };

    using EffectAppendFn=std::function<bool(const thandor::EffectDefinitionRecord&,Vec3f,
        uint32_t,float,uint32_t,const ModelHierarchyTransform*,std::optional<uint32_t>,uint32_t,
        uint32_t,std::optional<float>)>;
    using LinkedShotAppendFn=std::function<bool(const thandor::ShotDefinitionRecord&,Vec3f,Vec3f,
        uint32_t,uint32_t,uint32_t,uint32_t)>;
    EffectAppendFn append_effect_visual_recursive;
    LinkedShotAppendFn append_linked_shot_visual_recursive;
    std::size_t recursive_effect_spawn_count=0U;
    constexpr std::size_t kMaximumRecursiveEffectSpawns=4096U;
    const auto consume_recursive_effect_spawn=[&](){
        if(++recursive_effect_spawn_count>kMaximumRecursiveEffectSpawns)
            throw std::runtime_error("recursive EFF/SHT transition chain exceeded 4096 instances");
    };
    append_effect_visual_recursive=[&](const thandor::EffectDefinitionRecord& definition,
                                         Vec3f position,uint32_t age_ticks,float yaw_radians,
                                         uint32_t orientation_random,
                                         const ModelHierarchyTransform* authored_orientation,
                                         std::optional<uint32_t> forced_frame,
                                         uint32_t texture_faction,uint32_t recursion_depth,
                                         std::optional<float> mode0_beam_length)->bool{
        const uint32_t lifetime=std::max(1U,thandor::effect_lifetime_ticks(definition));
        const auto& sprite=load_sprite(definition.sprite_path);
        const auto& mesh=sprite.mesh();
        if(mesh.indices.size()%3U!=0U)throw std::runtime_error("EFF preview mesh is not triangle aligned");
        const Vec3f base_position=position;
        ModelHierarchyTransform base_authored=authored_orientation!=nullptr
            ?*authored_orientation
            :placement_root_transform(position.x,position.y,position.z,yaw_radians);
        if((definition.creation_flags&1U)!=0U){
            uint32_t random=orientation_random*1664525U+1013904223U+definition.definition_id;
            const auto random_basis=fixed_model_rotation_basis(random&0xFFFFU,
                (random>>16U)&0xFFFFU,0U);
            const Vec3f saved_translation=base_authored.translation;
            base_authored=compose_model_transform(base_authored,random_basis);
            base_authored.translation=saved_translation;
        }
        const auto pose_and_scale_at=[&](uint32_t pose_age){
            const float progress=std::clamp(static_cast<float>(std::min(pose_age,lifetime))/
                static_cast<float>(lifetime),0.0F,1.0F);
            const float start_scale=thandor::q12_to_float(definition.model_scale_x_q12);
            const float end_scale=thandor::q12_to_float(definition.model_scale_y_q12);
            const float scale=start_scale+(end_scale-start_scale)*progress;
            const auto displacement=thandor::effect_transition2_displacement_at_age(
                definition,pose_age,orientation_random);
            ModelHierarchyTransform pose=base_authored;
            pose.translation=add(base_position,Vec3f{displacement[0],displacement[1],displacement[2]});
            return std::pair<ModelHierarchyTransform,float>{pose,scale};
        };
        const auto attachment_world_at=[&](uint32_t kind,uint32_t child_index,uint32_t pose_age)
            ->std::optional<Vec3f>{
            const auto [pose,scale]=pose_and_scale_at(pose_age);
            const thandor::SpriteAttachmentRecord* fallback=nullptr;
            for(const auto& attachment:sprite.lookup_records()){
                if(attachment.key_class().value()!=kind)continue;
                if(fallback==nullptr)fallback=&attachment;
                if(attachment.key_index().value()==child_index)
                    return transform_point(pose,mul(attachment.local_translation,scale));
            }
            if(fallback!=nullptr&&child_index==0U)
                return transform_point(pose,mul(fallback->local_translation,scale));
            return std::nullopt;
        };

        bool emitted=false;
        if(age_ticks<lifetime&&!mesh.indices.empty()){
            const uint32_t frame=forced_frame.value_or(
                thandor::effect_frame_index_at_age(definition,age_ticks));
            const auto [authored,scale]=pose_and_scale_at(age_ticks);
            const uint32_t lod_flags=sprite.lod_flags(0U);
            const auto transform=sprite_lod_oriented_transform(
                authored.translation,camera_.eye(),authored,yaw_radians,lod_flags);
            const bool army_graphics=(definition.creation_flags&2U)!=0U;
            // EffectRuntimePool_CreateInstanceFromDefinitionCf binds creationFlags bit 2 to
            // g_ArmyGraphicsBindings[0], not to the firing faction.
            const uint32_t graphics_faction=army_graphics?0U:std::min(texture_faction,8U);
            const auto& family=army_graphics?texture_family(graphics_faction):effect_texture_family();
            const uint32_t effect_sort_group=next_effect_sort_group++;
            const uint32_t runtime_alpha=thandor::effect_alpha_at_age(definition,age_ticks);
            for(std::size_t triangle=0;triangle<mesh.indices.size();triangle+=3U){
                const uint32_t ia=mesh.indices[triangle],ib=mesh.indices[triangle+1U],ic=mesh.indices[triangle+2U];
                if(ia>=mesh.vertices.size()||ib>=mesh.vertices.size()||ic>=mesh.vertices.size())
                    throw std::runtime_error("EFF preview index exceeds vertex stream");
                const std::array<const thandor::SpriteMeshVertex*,3> corners{
                    &mesh.vertices[ia],&mesh.vertices[ib],&mesh.vertices[ic]};
                const uint32_t base_subresource=corners[0]->texture_subresource;
                const uint32_t render_flags=corners[0]->render_flags;
                if(corners[1]->texture_subresource!=base_subresource||corners[2]->texture_subresource!=base_subresource||
                   corners[1]->render_flags!=render_flags||corners[2]->render_flags!=render_flags)
                    throw std::runtime_error("EFF triangle mixes material state");
                const bool untextured=base_subresource==std::numeric_limits<uint32_t>::max();
                const auto primitive=classify_directdraw_direct3d_sprite_primitive(render_flags,!untextured);
                if(!primitive.supported)continue;
                if(next.vertices.size()+3U>kMaximumDynamicVertices||next.indices.size()+3U>kMaximumDynamicIndices)
                    throw std::runtime_error("dynamic effect geometry limit exceeded");
                const uint32_t texture_subresource=untextured?base_subresource:base_subresource+frame;
                const uint32_t layer=untextured?untextured_layer():
                    (army_graphics?object_layer(graphics_faction,texture_subresource):
                                   effect_layer(texture_subresource));
                const uint32_t base=static_cast<uint32_t>(next.vertices.size());
                for(const auto* source:corners){
                    thandor::SpriteTextureUv uv{};
                    if(!untextured){
                        const auto subresources=family.graphics.subresources();
                        if(texture_subresource>=subresources.size())
                            throw std::runtime_error("EFF frame selects missing GFX subresource");
                        const auto& subresource=subresources[texture_subresource];
                        uv=thandor::normalize_sprite_texture_uv(source->texture_u,source->texture_v,
                            subresource.pixel_width,subresource.pixel_height);
                    }
                    Vec3f local_position=mul(source->position,scale);
                    if(mode0_beam_length.has_value())
                        local_position=thandor::simulation_mode0_stretch_local_position(
                            local_position,*mode0_beam_length);
                    const Vec3f world=transform_point(transform,local_position);
                    const Vec3f normal=normalize(transform_vector(transform,source->normal));
                    uint32_t packed_color=definition.state_tint_argb;
                    const uint32_t serialized_alpha=(packed_color>>24U)&0xFFU;
                    const uint32_t base_alpha=serialized_alpha==0U?255U:serialized_alpha;
                    const uint32_t combined_alpha=(base_alpha*runtime_alpha+127U)/255U;
                    packed_color=(packed_color&0x00FFFFFFU)|(combined_alpha<<24U);
                    if(untextured){
                        const uint32_t palette_index=sprite_palette_index(source->render_flags);
                        if(palette_index<family.palette.colors_argb().size())
                            packed_color=thandor::multiply_rgb_opaque(packed_color,
                                family.palette.colors_argb()[palette_index]);
                    }
                    next.vertices.push_back(ObjectVertex{world,normal,uv.u,uv.v,layer,packed_color,1U,0.0F,0.0F});
                }
                const uint32_t first=static_cast<uint32_t>(next.indices.size());
                next.indices.insert(next.indices.end(),{base,base+1U,base+2U});
                append_draw_run(primitive.blend,primitive.two_sided,first,effect_sort_group);
                ++timed_effect_sprite_triangle_count_;
                emitted=true;
            }
        }

        // Transition states 0 and 1 use EFF +0x0C as a one-shot maintenance-tick
        // countdown. Invisible controller SPR records still execute this branch.
        if(recursion_depth<8U&&(definition.transition_kind==0U||definition.transition_kind==1U)&&
           definition.linked_effect_present!=0U&&definition.linked_effect_id!=0U){
            const uint32_t spawn_tick=definition.linked_effect_present;
            if(spawn_tick<=lifetime&&age_ticks>=spawn_tick){
                if(const auto* child=catalog_.effect_definition(definition.linked_effect_id)){
                    if(const auto emitter=attachment_world_at(3U,0U,spawn_tick)){
                        consume_recursive_effect_spawn();
                        auto [child_orientation,ignored_scale]=pose_and_scale_at(spawn_tick);
                        (void)ignored_scale;
                        child_orientation.translation=*emitter;
                        emitted|=append_effect_visual_recursive(*child,*emitter,age_ticks-spawn_tick,
                            yaw_radians,orientation_random+spawn_tick,&child_orientation,std::nullopt,
                            texture_faction,recursion_depth+1U,std::nullopt);
                    }
                }
            }
        }

        // EFF +0x48/+0x4C is a separate periodic kind-3 emitter. Each child is placed
        // from the parent's authored pose at its own historical spawn tick, not from the
        // current billboard transform.
        if(definition.periodic_effect_id!=0U&&definition.periodic_effect_interval_ticks!=0U&&
           recursion_depth<8U){
            if(const auto* child=catalog_.effect_definition(definition.periodic_effect_id)){
                const uint32_t interval=std::max(1U,definition.periodic_effect_interval_ticks);
                for(uint32_t spawn=interval;spawn<=lifetime&&spawn<=age_ticks;spawn+=interval){
                    if(const auto emitter=attachment_world_at(3U,0U,spawn)){
                        consume_recursive_effect_spawn();
                        ModelHierarchyTransform upright=fixed_model_rotation_basis(0U,0x4000U,0U);
                        upright.translation=*emitter;
                        emitted|=append_effect_visual_recursive(*child,*emitter,age_ticks-spawn,0.0F,
                            orientation_random+spawn,&upright,std::nullopt,texture_faction,
                            recursion_depth+1U,std::nullopt);
                    }
                    if(lifetime-spawn<interval)break;
                }
            }
        }

        // Transition state 1 independently reloads the EFF +0x18 counter every maintenance
        // tick and advances through kind-2 attachments. The emitted SHT runtime survives the
        // parent controller, so historical emissions are reconstructed after parent expiry.
        if(recursion_depth<8U&&definition.transition_kind==1U&&
           definition.linked_shot_present!=0U&&definition.linked_shot_id!=0U){
            if(const auto* shot=catalog_.shot_definition(definition.linked_shot_id)){
                for(const auto& emission:thandor::build_linked_shot_emissions(definition)){
                    if(emission.parent_age_tick>age_ticks)break;
                    const auto [source_pose,source_scale]=pose_and_scale_at(emission.parent_age_tick);
                    const auto emitter=attachment_world_at(2U,emission.attachment_index,
                        emission.parent_age_tick);
                    if(!emitter)continue;
                    consume_recursive_effect_spawn();
                    const Vec3f source=source_pose.translation;
                    const Vec3f target=add(source,mul(sub(*emitter,source),2.0F));
                    emitted|=append_linked_shot_visual_recursive(*shot,source,target,
                        age_ticks-emission.parent_age_tick,
                        orientation_random+emission.parent_age_tick+emission.attachment_index,
                        texture_faction,recursion_depth+1U);
                    (void)source_scale;
                }
            }
        }
        return emitted;
    };

    append_linked_shot_visual_recursive=[&](const thandor::ShotDefinitionRecord& shot,
                                               Vec3f origin,Vec3f target,uint32_t age_ticks,
                                               uint32_t stable_seed,uint32_t texture_faction,
                                               uint32_t recursion_depth)->bool{
        if(recursion_depth>8U)return false;
        thandor::SimulationWeaponProfile synthetic_weapon;
        synthetic_weapon.shot_definition_id=shot.definition_id;
        synthetic_weapon.reload_ticks=1U;
        synthetic_weapon.launch_attachment_count=1U;
        const std::array<Vec3f,1> launch_positions{origin};
        uint64_t serial=(static_cast<uint64_t>(stable_seed)<<32U)|shot.definition_id;
        auto volley=thandor::simulation_create_volley(0U,synthetic_weapon,launch_positions,
            target,shot,0U,serial);
        if(volley.empty())return false;
        auto projectile=volley.front();
        thandor::simulation_resolve_projectile_surface_impact(projectile,shot,document_->field());
        const float yaw=thandor::turn16_to_radians(projectile.launch_yaw_turn16);
        const auto initial_orientation=shot_model_transform(origin,projectile.launch_yaw_turn16,
            projectile.launch_pitch_turn16);
        const auto& shot_sprite=load_sprite(shot.sprite_path);
        Vec3f shot_emitter=origin;
        for(const auto& attachment:shot_sprite.lookup_records()){
            if(attachment.key_class().value()==3U){
                shot_emitter=transform_point(initial_orientation,attachment.local_translation);
                break;
            }
        }
        bool emitted=false;
        if(const auto* launch=catalog_.effect_definition(shot.launch_effect_id)){
            ModelHierarchyTransform launch_orientation=initial_orientation;
            launch_orientation.translation=shot_emitter;
            emitted|=append_effect_visual_recursive(*launch,shot_emitter,age_ticks,yaw,stable_seed,
                &launch_orientation,std::nullopt,texture_faction,recursion_depth+1U,std::nullopt);
        }
        const bool flight_visible=shot.trajectory_mode==0
            ?age_ticks<projectile.lifetime_ticks:age_ticks<projectile.travel_ticks;
        if(flight_visible){
            const Vec3f position=thandor::simulation_projectile_position(projectile,shot,age_ticks);
            const Vec3f next_position=thandor::simulation_projectile_position(projectile,shot,
                age_ticks+1U);
            const auto flight_orientation=projectile_model_pose(projectile,shot,age_ticks,
                position,next_position);
            thandor::EffectDefinitionRecord flight;
            flight.definition_id=0x80000000U|shot.definition_id;
            flight.runtime_value_0c=std::max(1U,shot.lifetime_ticks);
            flight.frame_advance_threshold_q4=16U;
            flight.state_tint_argb=shot.state_tint_argb==0U?0xFFFFFFFFU:shot.state_tint_argb;
            flight.model_scale_x_q12=0x1000;flight.model_scale_y_q12=0x1000;
            flight.sprite_path=shot.sprite_path;
            const std::optional<float> beam_length=shot.trajectory_mode==0
                ?std::optional<float>{thandor::simulation_mode0_beam_length_world(projectile,shot)}
                :std::nullopt;
            emitted|=append_effect_visual_recursive(flight,position,age_ticks,yaw,stable_seed,
                &flight_orientation,thandor::simulation_shot_frame_index(shot,age_ticks),
                texture_faction,recursion_depth+1U,beam_length);
            if(const auto* trail=catalog_.effect_definition(shot.secondary_effect_id)){
                const uint32_t interval=std::max(1U,shot.secondary_effect_interval_ticks);
                for(uint32_t spawn=interval;spawn<=age_ticks;spawn+=interval){
                    const Vec3f trail_origin=thandor::simulation_projectile_position(projectile,shot,spawn);
                    const Vec3f trail_next=thandor::simulation_projectile_position(projectile,shot,spawn+1U);
                    const auto shot_pose=projectile_model_pose(projectile,shot,spawn,
                        trail_origin,trail_next);
                    Vec3f trail_position=trail_origin;
                    for(const auto& attachment:shot_sprite.lookup_records()){
                        if(attachment.key_class().value()==3U){
                            trail_position=transform_point(shot_pose,attachment.local_translation);
                            break;
                        }
                    }
                    ModelHierarchyTransform upright=fixed_model_rotation_basis(0U,0x4000U,0U);
                    upright.translation=trail_position;
                    consume_recursive_effect_spawn();
                    emitted|=append_effect_visual_recursive(*trail,trail_position,age_ticks-spawn,0.0F,
                        stable_seed+spawn,&upright,std::nullopt,texture_faction,
                        recursion_depth+1U,std::nullopt);
                    if(age_ticks-spawn<interval)break;
                }
            }
        }
        if(age_ticks>=projectile.travel_ticks){
            const uint32_t impact_age=age_ticks-projectile.travel_ticks;
            if(const auto* impact=catalog_.effect_definition(projectile.impact_effect_id)){
                ModelHierarchyTransform upright=fixed_model_rotation_basis(0U,0x4000U,0U);
                upright.translation=projectile.impact_position;
                emitted|=append_effect_visual_recursive(*impact,projectile.impact_position,impact_age,0.0F,
                    stable_seed,&upright,std::nullopt,texture_faction,recursion_depth+1U,std::nullopt);
            }
        }
        return emitted;
    };

    const auto append_effect_visual=[&](const thandor::EffectDefinitionRecord& definition,
                                         Vec3f position,uint32_t age_ticks,float yaw_radians,
                                         uint32_t orientation_random=0U,
                                         const ModelHierarchyTransform* authored_orientation=nullptr,
                                         std::optional<uint32_t> forced_frame=std::nullopt,
                                         uint32_t texture_faction=0U)->bool{
        return append_effect_visual_recursive(definition,position,age_ticks,yaw_radians,
            orientation_random,authored_orientation,forced_frame,texture_faction,0U,std::nullopt);
    };

    const auto placements=document_->level().placements();
    std::vector<std::pair<std::size_t,bool>> dynamic_placements;
    std::unordered_set<std::size_t> dynamic_placement_set;
    const auto append_dynamic_placement=[&](std::size_t index,bool is_building){
        if(dynamic_placement_set.insert(index).second)
            dynamic_placements.emplace_back(index,is_building);
    };
    if(animate_buildings_){
        dynamic_placements.reserve(dynamic_placements.size()+animated_building_placements_.size());
        for(const auto index:animated_building_placements_)
            append_dynamic_placement(index,true);
    }
    if(animate_units_){
        dynamic_placements.reserve(dynamic_placements.size()+animated_unit_placements_.size());
        for(const auto index:animated_unit_placements_)
            append_dynamic_placement(index,false);
    }
    // Runtime class 10 is a continuous radar callback, not a weapon-target callback. It stays
    // dynamic even when general authored animation and combat simulation are disabled.
    dynamic_placements.reserve(dynamic_placements.size()+continuous_radar_placements_.size());
    for(const auto index:continuous_radar_placements_){
        if(index>=placements.size())continue;
        const auto* radar_preset=catalog_.army_preset(placements[index].army_asset_id);
        if(!radar_preset)continue;
        append_dynamic_placement(index,
            radar_preset->browser_kind==thandor::PlacementBrowserKind::Building);
    }
    // Simulation replaces only placements with live transient actor state. It must never route
    // every movable unit or every armed tower through the dynamic renderer merely because the
    // corresponding animation checkbox is enabled.
    if(simulation_mode_||fog_preview_mode_!=0){
        dynamic_placements.reserve(dynamic_placements.size()+simulation_actor_placements_.size());
        for(const auto index:simulation_actor_placements_){
            if(index>=placements.size())continue;
            const auto* preset=catalog_.army_preset(placements[index].army_asset_id);
            if(!preset)continue;
            const bool is_building=preset->browser_kind==thandor::PlacementBrowserKind::Building;
            const bool is_unit=preset->browser_kind==thandor::PlacementBrowserKind::Unit;
            const thandor::SimulationRenderPolicy render_policy{is_building,is_unit,
                preset->has_model_subnode_animation,simulation_mode_||fog_preview_mode_!=0,
                simulation_actor_states_.contains(index)||fog_preview_mode_!=0,
                preset->has_continuous_radar_subnode_animation};
            if(render_policy.dynamic_replacement_enabled(
                   animate_buildings_||fog_preview_mode_!=0,
                   animate_units_||fog_preview_mode_!=0))
                append_dynamic_placement(index,is_building);
        }
    }
    for(const auto [placement_index,is_building]:dynamic_placements){
        if(placement_index>=placements.size())continue;
        const auto& placement=placements[placement_index];
        const auto* preset=catalog_.army_preset(placement.army_asset_id);
        if(preset==nullptr)continue;
        if(is_building&&preset->browser_kind!=thandor::PlacementBrowserKind::Building)continue;
        if(!is_building&&preset->browser_kind!=thandor::PlacementBrowserKind::Unit)continue;
        Vec3f root_position{placement.render_x(),placement.render_y(),
            terrain_height_near(placement.render_x(),placement.render_y())};
        uint32_t root_yaw=placement.orientation_turn16&0xFFFFU;
        thandor::SimulationActorState* simulation_state=nullptr;
        if(const auto found=simulation_actor_states_.find(placement_index);
           found!=simulation_actor_states_.end()&&
           (simulation_mode_||preset->runtime_model_class_id==17U)){
            simulation_state=&found->second;
            if(simulation_mode_){
                root_position=simulation_state->position;
                root_position.z=terrain_height_near(root_position.x,root_position.y);
                root_yaw=simulation_state->body_yaw_turn16;
            }
        }
        if(fog_preview_mode_!=0&&!fog_preview_point_visible(root_position))continue;
        const bool placement_visible=simulation_state!=nullptr
            ?placement_visible_in_canvas(add(root_position,{0.0F,0.0F,4.0F}),24.0F)
            :(placement_index<placement_pick_bounds_.size()&&
              placement_pick_bounds_[placement_index].valid()
                ?bounds_visible_in_canvas(placement_pick_bounds_[placement_index],2.0F)
                :placement_visible_in_canvas(add(root_position,{0.0F,0.0F,4.0F}),24.0F));
        if(!placement_visible)continue;
        if(is_building)visible_animated_buildings_.push_back(placement_index);
        std::vector<thandor::ModelVisualNode> fallback_nodes;
        std::span<const thandor::ModelVisualNode> hierarchy=
            placement_hierarchy(*preset,placement_index);
        if(hierarchy.empty()){
            thandor::ModelVisualNode fallback;
            fallback.parent_index=std::numeric_limits<uint32_t>::max();
            fallback.local_rotation_angle1=0x4000U;
            fallback.sprite_path=preset->sprite_path;
            fallback_nodes.push_back(std::move(fallback));
            hierarchy=fallback_nodes;
        }
        const auto launch_node_selected_for_recoil=[](
            const thandor::SimulationWeaponProfile& profile,
            const thandor::SimulationWeaponState& state,std::size_t node_index){
            const auto& nodes=profile.launch_model_node_indices;
            if(nodes.empty())return node_index==profile.launch_model_node_index;
            if(profile.launch_policy==thandor::SimulationLaunchPolicy::AlternateLaunchNodes)
                return node_index==nodes[state.recoil_launch_selector%nodes.size()];
            return std::find(nodes.begin(),nodes.end(),static_cast<uint32_t>(node_index))!=nodes.end();
        };
        std::vector<ModelHierarchyInstance> instances;
        std::vector<bool> active;
        instances.reserve(hierarchy.size());active.reserve(hierarchy.size());
        for(std::size_t node_index=0;node_index<hierarchy.size();++node_index){
            const auto& node=hierarchy[node_index];
            ModelHierarchyInstance instance{};instance.node=&node;
            instance.root=node.parent_index==std::numeric_limits<uint32_t>::max();
            bool node_active=node.creates_runtime_node()&&!node.sprite_path.empty();
            const thandor::SpriteAsset* node_sprite=node_active?&load_sprite(node.sprite_path):nullptr;
            if(instance.root){
                uint32_t dynamic_root_yaw=root_yaw;
                int32_t root_recoil_angle=0;
                if(simulation_state){
                    for(std::size_t weapon_index=0;
                        weapon_index<preset->simulation_weapon_profiles.size()&&
                        weapon_index<simulation_state->weapons.size();++weapon_index){
                        const auto& profile=preset->simulation_weapon_profiles[weapon_index];
                        const auto& state=simulation_state->weapons[weapon_index];
                        if(profile.yaw_model_node_index==node_index)
                            dynamic_root_yaw=thandor::wrap_turn16(
                                static_cast<int64_t>(dynamic_root_yaw)+state.yaw.angle_turn16);
                        if(launch_node_selected_for_recoil(profile,state,node_index))
                            root_recoil_angle+=state.recoil_angle_offset_turn16;
                    }
                }
                dynamic_root_yaw=thandor::wrap_turn16(
                    static_cast<int64_t>(dynamic_root_yaw)+root_recoil_angle);
                const auto contact=placement_contact_pose(*preset,root_position.x,root_position.y,
                    node_sprite?node_sprite->placement_height_offset():0.0F);
                root_position.z=contact.height;
                instance.transform=simulation_state!=nullptr&&preset->runtime_model_class_id==17U
                    ?class17_banked_root_transform(root_position,dynamic_root_yaw,
                        simulation_state->class17_bank_heading_turn16,
                        simulation_state->class17_bank_angle_turn16,
                        contact.align_to_normal?contact.normal:Vec3f{0.0F,0.0F,1.0F})
                    :(contact.align_to_normal
                        ?terrain_contact_root_transform(root_position,
                            thandor::turn16_to_radians(dynamic_root_yaw),contact.normal)
                        :placement_root_transform(root_position.x,root_position.y,root_position.z,
                            thandor::turn16_to_radians(dynamic_root_yaw)));
            }else{
                if(node.parent_index>=instances.size()||!active[node.parent_index])node_active=false;
                if(node_active){
                    const auto& parent=instances[node.parent_index];
                    const auto* attachment=parent.sprite->child_attachment(node.child_slot);
                    if(attachment==nullptr){
                        // Optional or unresolved live child slots are skipped exactly like the
                        // recursive runtime creation failure path. A single malformed subtree must
                        // not crash first-drive simulation for the whole editor.
                        node_active=false;
                    }else{
                    uint32_t angle2=node.local_rotation_angle2;
                    uint32_t angle1=node.local_rotation_angle1;
                    bool simulated_node=false;
                    int32_t recoil_translation_q12=0;
                    int32_t recoil_angle_turn16=0;
                    if(simulation_state){
                        for(std::size_t weapon_index=0;
                            weapon_index<preset->simulation_weapon_profiles.size()&&
                            weapon_index<simulation_state->weapons.size();++weapon_index){
                            const auto& profile=preset->simulation_weapon_profiles[weapon_index];
                            const auto& state=simulation_state->weapons[weapon_index];
                            if(profile.yaw_model_node_index==node_index){
                                angle2=state.yaw.angle_turn16;simulated_node=true;
                            }
                            if(profile.pitch_model_node_index==node_index){
                                angle1=state.pitch.angle_turn16;simulated_node=true;
                            }
                            if(launch_node_selected_for_recoil(profile,state,node_index)){
                                recoil_angle_turn16+=state.recoil_angle_offset_turn16;
                                recoil_translation_q12+=state.recoil_translation_offset_q12;
                                simulated_node=true;
                            }
                        }
                    }
                    angle2=thandor::wrap_turn16(static_cast<int64_t>(angle2)+recoil_angle_turn16);
                    const bool animate_authored_subnodes=preset->has_model_subnode_animation&&
                        ((is_building&&animate_buildings_)||(!is_building&&animate_units_));
                    const uint32_t authored_animation_tick=is_building
                        ?current_building_animation_tick_:current_presentation_tick_;
                    const auto& authored_parent_node=hierarchy[node.parent_index];
                    if(!simulated_node&&authored_parent_node.runtime_class_id==10U&&
                       node.child_slot==0U&&
                       authored_parent_node.simulation.yaw_max_velocity_turn16!=0){
                        angle2=thandor::wrap_turn16(static_cast<int64_t>(angle2)+
                            static_cast<int64_t>(authored_parent_node.simulation.yaw_max_velocity_turn16)*
                                current_presentation_tick_);
                    }else if(!simulated_node&&authored_parent_node.runtime_class_id==10U&&
                             node.child_slot==1U&&
                             authored_parent_node.simulation.yaw_acceleration_turn16!=0){
                        angle2=thandor::wrap_turn16(static_cast<int64_t>(angle2)+
                            static_cast<int64_t>(authored_parent_node.simulation.yaw_acceleration_turn16)*
                                current_presentation_tick_);
                    }else if(preset->runtime_model_class_id==17U&&parent.root&&node.child_slot<3U&&
                       simulation_state!=nullptr){
                        angle1=thandor::wrap_turn16(static_cast<int64_t>(angle1)+
                            simulation_state->class17_rotor_tilt_turn16);
                        simulated_node=true;
                    }else if(!simulated_node&&animate_authored_subnodes&&parent.root&&
                             !thandor::simulation_model_node_is_weapon_aim_axis(
                                 preset->simulation_weapon_profiles,static_cast<uint32_t>(node_index))&&
                             node.child_slot==0U&&
                             preset->child0_rotation_step_turn16!=0){
                        angle2=static_cast<uint32_t>(static_cast<int64_t>(angle2)+
                            static_cast<int64_t>(preset->child0_rotation_step_turn16)*authored_animation_tick)&0xFFFFU;
                    }else if(!simulated_node&&animate_authored_subnodes&&parent.root&&
                             !thandor::simulation_model_node_is_weapon_aim_axis(
                                 preset->simulation_weapon_profiles,static_cast<uint32_t>(node_index))&&
                             node.child_slot==1U&&
                             preset->child1_rotation_step_turn16!=0){
                        angle2=static_cast<uint32_t>(static_cast<int64_t>(angle2)+
                            static_cast<int64_t>(preset->child1_rotation_step_turn16)*authored_animation_tick)&0xFFFFU;
                    }
                    auto local=fixed_model_rotation_basis(angle2,angle1,node.local_rotation_angle0);
                    local.translation=attachment->local_translation;
                    if(recoil_translation_q12!=0){
                        const Vec3f recoil=transform_vector(local,
                            {thandor::q12_to_float(recoil_translation_q12),0.0F,0.0F});
                        local.translation=add(local.translation,recoil);
                    }
                    if(animate_authored_subnodes&&parent.root&&node.child_slot==2U&&
                       preset->child2_vertical_step_q12!=0)
                        local.translation.z=animated_child2_translation_q12(*preset,
                            authored_animation_tick,local.translation.z);
                    instance.transform=compose_model_transform(parent.transform,local);
                    }
                }
            }
            if(node_active)instance.sprite=node_sprite;
            active.push_back(node_active);instances.push_back(instance);
        }
        apply_walker_pose_to_instances(*preset,hierarchy,instances,simulation_state,
            [&](float x,float y){return terrain_height_near(x,y);});
        const bool neutral=thandor::placement_uses_neutral_source_color(*preset);
        const uint32_t faction=neutral?0U:thandor::default_faction_graphics_family(placement.faction_index);
        const auto& family=texture_family(faction);
        for(std::size_t instance_index=0;instance_index<instances.size();++instance_index){
            if(!active[instance_index])continue;
            const auto& instance=instances[instance_index];
            const auto& mesh=instance.sprite->mesh();
            for(std::size_t triangle=0;triangle<mesh.indices.size();triangle+=3U){
                const std::array<const thandor::SpriteMeshVertex*,3> corners{
                    &mesh.vertices.at(mesh.indices[triangle]),&mesh.vertices.at(mesh.indices[triangle+1U]),
                    &mesh.vertices.at(mesh.indices[triangle+2U])};
                uint32_t visible_mesh_group_mask=0xFFFFFFFFU;
                if(simulation_state!=nullptr){
                    for(std::size_t weapon_index=0;
                        weapon_index<preset->simulation_weapon_profiles.size()&&
                        weapon_index<simulation_state->weapons.size();++weapon_index){
                        const auto& profile=preset->simulation_weapon_profiles[weapon_index];
                        if(profile.runtime_class_id==9U){
                            const bool rack_node=profile.rack_mesh_model_node_index!=
                                    std::numeric_limits<uint32_t>::max()
                                ?instance_index==profile.rack_mesh_model_node_index
                                :instance_index==profile.launch_model_node_index;
                            if(rack_node)
                                visible_mesh_group_mask&=thandor::simulation_class9_render_mesh_mask(
                                    simulation_state->weapons[weapon_index],profile);
                        }
                    }
                }
                const uint32_t selector_mask=corners[0]->mesh_group_selector_mask;
                if(selector_mask!=0U&&(selector_mask&visible_mesh_group_mask)==0U)continue;
                const uint32_t subresource=corners[0]->texture_subresource;
                const uint32_t flags=corners[0]->render_flags;
                const bool untextured=subresource==std::numeric_limits<uint32_t>::max();
                const auto primitive=classify_directdraw_direct3d_sprite_primitive(flags,!untextured);
                if(!primitive.supported)continue;
                if(next.vertices.size()+3U>kMaximumDynamicVertices||next.indices.size()+3U>kMaximumDynamicIndices)
                    throw std::runtime_error("visible simulation geometry limit exceeded");
                const uint32_t base=static_cast<uint32_t>(next.vertices.size());
                for(const auto* source:corners){
                    const uint32_t layer=untextured?untextured_layer():object_layer(faction,source->texture_subresource);
                    thandor::SpriteTextureUv uv{};
                    if(!untextured){
                        const auto subresources=family.graphics.subresources();
                        if(source->texture_subresource>=subresources.size())
                            throw std::runtime_error("dynamic actor selects missing GFX subresource");
                        const auto& metadata=subresources[source->texture_subresource];
                        uv=thandor::normalize_sprite_texture_uv(source->texture_u,source->texture_v,
                            metadata.pixel_width,metadata.pixel_height);
                        if(simulation_state!=nullptr&&preset->runtime_model_class_id==2U){
                            if(source->texture_subresource==preset->primary_animated_subresource)
                                uv.u+=thandor::normalize_model_texture_offset_u(
                                    simulation_state->primary_track_texture_offset_u,
                                    metadata.pixel_width,metadata.pixel_height);
                            else if(source->texture_subresource==preset->secondary_animated_subresource)
                                uv.u+=thandor::normalize_model_texture_offset_u(
                                    simulation_state->secondary_track_texture_offset_u,
                                    metadata.pixel_width,metadata.pixel_height);
                        }
                    }
                    const Vec3f world=transform_point(instance.transform,source->position);
                    const Vec3f normal=normalize(transform_vector(instance.transform,source->normal));
                    const uint32_t directional=level_lighting.directional_color_for_upward_normal(normal,ramp);
                    uint32_t modulation=0xFFFFFFFFU;
                    if(untextured){
                        const uint32_t palette_index=sprite_palette_index(source->render_flags);
                        if(palette_index<family.palette.colors_argb().size())modulation=family.palette.colors_argb()[palette_index];
                    }
                    const uint32_t packed=directdraw_select_model_vertex_packed_color(flags,
                        modulation|0xFF000000U,thandor::multiply_rgb_opaque(directional,modulation));
                    const uint32_t direct=directdraw_model_vertex_uses_direct_tint(flags)?1U:0U;
                    const bool animated_part=is_building&&instance.root&&preset->has_primary_texture_animation&&
                        source->texture_subresource==preset->primary_animated_subresource;
                    next.vertices.push_back(ObjectVertex{world,normal,uv.u,uv.v,layer,packed,direct,
                        animated_part?0.5F:0.0F,animated_part?80.0F:0.0F});
                }
                const uint32_t first=static_cast<uint32_t>(next.indices.size());
                next.indices.insert(next.indices.end(),{base,base+1U,base+2U});
                append_draw_run(primitive.blend,primitive.two_sided,first);
            }
        }
    }

    // Move each simulation replacement's projected shadow with its current LEV-backed
    // position and whole-body yaw. The generated alpha mask remains in the static object
    // texture atlas; only the small terrain-conforming receiver geometry is rebuilt per tick.
    for(const auto& shadow:dynamic_shadow_templates_){
        const auto actor_found=simulation_actor_states_.find(shadow.placement_index);
        if(actor_found==simulation_actor_states_.end())continue;
        const auto& actor=actor_found->second;
        const float yaw_delta=thandor::turn16_to_radians(thandor::wrap_turn16(
            static_cast<int64_t>(actor.body_yaw_turn16)-shadow.anchor_yaw_turn16));
        const float c=std::cos(yaw_delta),s=std::sin(yaw_delta);
        const uint32_t base=static_cast<uint32_t>(next.shadow_vertices.size());
        if(next.shadow_vertices.size()+shadow.receiver_vertices.size()>kMaximumDynamicVertices||
           next.shadow_indices.size()+shadow.receiver_indices.size()>kMaximumDynamicIndices)
            throw std::runtime_error("dynamic actor shadow geometry limit exceeded");
        for(const auto& source:shadow.receiver_vertices){
            const float local_x=source.position.x-shadow.anchor_position.x;
            const float local_y=source.position.y-shadow.anchor_position.y;
            const float world_x=actor.position.x+c*local_x-s*local_y;
            const float world_y=actor.position.y+s*local_x+c*local_y;
            const float old_terrain=terrain_height_near(source.position.x,source.position.y);
            const float receiver_offset=source.position.z-old_terrain;
            ObjectVertex moved=source;
            moved.position={world_x,world_y,terrain_height_near(world_x,world_y)+receiver_offset};
            next.shadow_vertices.push_back(moved);
        }
        for(const uint32_t index:shadow.receiver_indices)next.shadow_indices.push_back(base+index);
    }

    if(animate_timed_effects_&&!timed_effect_controllers_.empty()){
        struct ResolvedTimedEffectPose {
            Vec3f root_position{};
            ModelHierarchyTransform root_orientation{};
            ModelHierarchyTransform emitter_orientation{};
            std::vector<Vec3f> world_type6_attachments;
        };
        std::vector<thandor::ResourceEmitterTiming> timings;
        timings.reserve(timed_effect_controllers_.size());
        std::vector<uint8_t> visible(timed_effect_controllers_.size(),0U);
        std::vector<ResolvedTimedEffectPose> resolved_poses(timed_effect_controllers_.size());
        for(std::size_t index=0;index<timed_effect_controllers_.size();++index){
            const auto& source=timed_effect_controllers_[index];
            timings.push_back(source.timing);
            auto& resolved=resolved_poses[index];
            resolved.root_position=source.root_position;
            resolved.root_orientation=source.root_orientation;
            resolved.emitter_orientation=source.emitter_orientation;
            bool bounds_visible=placement_visible_in_canvas(
                source.root_position,source.visibility_radius);
            if(source.placement_index<placements.size()){
                const auto& placement=placements[source.placement_index];
                if(source.placement_index<placement_pick_bounds_.size()&&
                   placement_pick_bounds_[source.placement_index].valid())
                    bounds_visible=bounds_visible_in_canvas(
                        placement_pick_bounds_[source.placement_index],source.visibility_radius*0.1F);
                if(const auto* preset=catalog_.army_preset(placement.army_asset_id)){
                    thandor::SimulationActorState fallback_actor;
                    fallback_actor.position={placement.render_x(),placement.render_y(),
                        terrain_height_near(placement.render_x(),placement.render_y())};
                    fallback_actor.body_yaw_turn16=placement.orientation_turn16&0xFFFFU;
                    thandor::SimulationActorState* actor=&fallback_actor;
                    if(const auto live=simulation_actor_states_.find(source.placement_index);
                       live!=simulation_actor_states_.end())actor=&live->second;
                    const auto transforms=simulation_hierarchy_transforms(
                        source.placement_index,*preset,*actor);
                    if(source.root_model_node_index<transforms.size()){
                        resolved.root_orientation=transforms[source.root_model_node_index];
                        resolved.root_position=resolved.root_orientation.translation;
                    }
                    if(source.emitter_model_node_index<transforms.size())
                        resolved.emitter_orientation=transforms[source.emitter_model_node_index];
                }
            }
            resolved.world_type6_attachments.reserve(source.local_type6_attachments.size());
            for(const Vec3f attachment:source.local_type6_attachments)
                resolved.world_type6_attachments.push_back(
                    transform_point(resolved.emitter_orientation,attachment));
            const bool animation_enabled=!source.owner_is_building||animate_buildings_;
            if(animation_enabled&&bounds_visible){
                visible[index]=1U;visible_timed_effect_controllers_.push_back(index);
            }
        }
        const auto schedule=thandor::build_resource_emitter_schedule(timings,
            resource_animation_rng_seed_,2048U,256U);
        const auto wrapped_age=[&](uint32_t spawn_tick)->uint32_t{
            if(schedule.cycle_ticks==0U)return 0U;
            return (current_presentation_tick_%schedule.cycle_ticks+schedule.cycle_ticks-
                    spawn_tick%schedule.cycle_ticks)%schedule.cycle_ticks;
        };
        for(const auto& pulse:schedule.pulses){
            if(pulse.emitter_index>=timed_effect_controllers_.size()||!visible[pulse.emitter_index])continue;
            const auto& source=timed_effect_controllers_[pulse.emitter_index];
            const auto& pose=resolved_poses[pulse.emitter_index];
            const auto* definition=catalog_.effect_definition(source.effect_id);
            if(definition==nullptr)continue;
            Vec3f origin=pose.root_position;
            if(!pose.world_type6_attachments.empty())origin=pose.world_type6_attachments[
                pulse.attachment_random%pose.world_type6_attachments.size()];
            const float live_root_yaw=std::atan2(
                pose.root_orientation.basis[3],pose.root_orientation.basis[0]);
            const float randomized_yaw=live_root_yaw+
                static_cast<float>(pulse.orientation_random&0xFFFFU)*kTurn16ToRadians;
            const uint32_t age=wrapped_age(pulse.spawn_tick);
            if(source.shading_profile){
                const auto& profile=*source.shading_profile;
                const auto color=thandor::unpack_argb(0xFF000000U|profile.packed_color_rgb);
                next.shading_emitters.push_back(ResourceShadingEmitter{origin,profile.target_radius_world,
                    color.r,color.g,color.b,static_cast<float>(schedule.cycle_ticks),
                    static_cast<float>(pulse.spawn_tick),profile.grow_transition_ticks,
                    profile.shading_stop_tick,profile.release_transition_ticks});
            }
            ModelHierarchyTransform effect_orientation=pose.root_orientation;
            if(definition->transition_kind==2U)
                effect_orientation=placement_root_transform(
                    origin.x,origin.y,origin.z,randomized_yaw);
            effect_orientation.translation=origin;
            append_effect_visual(*definition,origin,age,randomized_yaw,
                pulse.orientation_random,&effect_orientation);

        }
    }

    // Editor-only weapon test simulation. This follows the executable's separate runtime
    // objects: one launch EFF at the SHT kind-3 attachment, one SHT model, periodic secondary
    // EFF instances from that same attachment, and one nearest-surface impact EFF.
    if(simulation_mode_&&!simulation_projectiles_.empty()){
        constexpr float kOneShotPreviewCycle=16777216.0F;
        const auto placements=document_->level().placements();
        for(const auto& projectile:simulation_projectiles_){
            const auto* shot=catalog_.shot_definition(projectile.shot_definition_id);
            if(!shot)continue;
            const uint32_t age=current_presentation_tick_-projectile.spawn_tick;
            const float yaw=thandor::turn16_to_radians(projectile.launch_yaw_turn16);
            const uint32_t faction=projectile.owner_placement_index<placements.size()
                ?thandor::default_faction_graphics_family(
                    placements[projectile.owner_placement_index].faction_index):0U;
            const auto initial_orientation=shot_model_transform(projectile.launch_position,
                projectile.launch_yaw_turn16,projectile.launch_pitch_turn16);
            const auto& shot_sprite=load_sprite(shot->sprite_path);
            Vec3f shot_emitter=projectile.launch_position;
            for(const auto& attachment:shot_sprite.lookup_records()){
                if(attachment.key_class().value()==3U){
                    shot_emitter=transform_point(initial_orientation,attachment.local_translation);
                    break;
                }
            }
            if(const auto* launch=catalog_.effect_definition(shot->launch_effect_id)){
                ModelHierarchyTransform launch_orientation=initial_orientation;
                launch_orientation.translation=shot_emitter;
                append_effect_visual(*launch,shot_emitter,age,yaw,
                    static_cast<uint32_t>(projectile.serial),&launch_orientation,std::nullopt,faction);
            }

            const bool flight_visible=shot->trajectory_mode==0
                ?age<projectile.lifetime_ticks:age<projectile.travel_ticks;
            if(flight_visible){
                const Vec3f position=thandor::simulation_projectile_position(
                    projectile,*shot,current_presentation_tick_);
                const Vec3f next_position=thandor::simulation_projectile_position(
                    projectile,*shot,current_presentation_tick_+1U);
                const auto flight_orientation=projectile_model_pose(projectile,*shot,age,
                    position,next_position);
                // The SHT model is one model node. Mode zero does not advance a projectile: the
                // renderer stretches the authored local +Z geometry to the nearest ray distance.
                thandor::EffectDefinitionRecord flight;
                flight.definition_id=0x80000000U|projectile.shot_definition_id;
                flight.runtime_value_0c=std::max(1U,shot->lifetime_ticks);
                flight.frame_advance_threshold_q4=16U;
                flight.state_tint_argb=shot->state_tint_argb==0U?0xFFFFFFFFU:shot->state_tint_argb;
                flight.model_scale_x_q12=0x1000;flight.model_scale_y_q12=0x1000;
                flight.sprite_path=shot->sprite_path;
                const std::optional<float> beam_length=shot->trajectory_mode==0
                    ?std::optional<float>{thandor::simulation_mode0_beam_length_world(projectile,*shot)}
                    :std::nullopt;
                append_effect_visual_recursive(flight,position,age,yaw,
                    static_cast<uint32_t>(projectile.serial),&flight_orientation,
                    thandor::simulation_shot_frame_index(*shot,age),faction,0U,beam_length);

                if(const auto* trail=catalog_.effect_definition(shot->secondary_effect_id)){
                    const uint32_t interval=std::max(1U,shot->secondary_effect_interval_ticks);
                    for(uint32_t trail_spawn=interval;trail_spawn<=age;trail_spawn+=interval){
                        const uint32_t trail_tick=projectile.spawn_tick+trail_spawn;
                        const Vec3f trail_origin=thandor::simulation_projectile_position(
                            projectile,*shot,trail_tick);
                        const Vec3f trail_next=thandor::simulation_projectile_position(
                            projectile,*shot,trail_tick+1U);
                        const auto shot_pose=projectile_model_pose(projectile,*shot,trail_spawn,
                            trail_origin,trail_next);
                        Vec3f trail_position=trail_origin;
                        for(const auto& attachment:shot_sprite.lookup_records()){
                            if(attachment.key_class().value()==3U){
                                trail_position=transform_point(shot_pose,attachment.local_translation);
                                break;
                            }
                        }
                        ModelHierarchyTransform upright=fixed_model_rotation_basis(0U,0x4000U,0U);
                        upright.translation=trail_position;
                        append_effect_visual(*trail,trail_position,age-trail_spawn,0.0F,
                            static_cast<uint32_t>(projectile.serial+trail_spawn),&upright,
                            std::nullopt,faction);
                    }
                }
                const uint32_t radius_byte=(shot->shading_color_argb>>24U)&0xFFU;
                if(radius_byte!=0U&&shot->shading_transition_ticks>0){
                    const auto color=thandor::unpack_argb(0xFF000000U|
                        (shot->shading_color_argb&0x00FFFFFFU));
                    next.shading_emitters.push_back(ResourceShadingEmitter{position,
                        static_cast<float>(radius_byte)/16.0F,color.r,color.g,color.b,
                        kOneShotPreviewCycle,static_cast<float>(projectile.spawn_tick),
                        static_cast<float>(std::max(1,shot->shading_transition_ticks)),
                        static_cast<float>(projectile.travel_ticks),
                        static_cast<float>(std::max(1,shot->shading_transition_ticks))});
                }
            }

            if(age>=projectile.travel_ticks){
                const uint32_t impact_age=age-projectile.travel_ticks;
                if(const auto* impact=catalog_.effect_definition(projectile.impact_effect_id)){
                    ModelHierarchyTransform impact_orientation;
                    if(projectile.impact_kind==thandor::SimulationImpactKind::TargetModelImpactClass){
                        const uint32_t sample_age=projectile.travel_ticks>0U
                            ?projectile.travel_ticks-1U:0U;
                        const Vec3f before=thandor::simulation_projectile_position(projectile,*shot,
                            projectile.spawn_tick+sample_age);
                        auto incoming=thandor::simulation_target_angles(before,
                            projectile.impact_position,0);
                        if(!incoming.valid){
                            incoming.yaw_turn16=projectile.launch_yaw_turn16;
                            incoming.pitch_turn16=projectile.launch_pitch_turn16;
                        }
                        const uint32_t reverse_yaw=thandor::wrap_turn16(
                            static_cast<int64_t>(incoming.yaw_turn16)+0x8000);
                        const uint32_t reverse_pitch=thandor::wrap_turn16(
                            -static_cast<int64_t>(static_cast<int16_t>(incoming.pitch_turn16)));
                        impact_orientation=shot_model_transform(projectile.impact_position,
                            reverse_yaw,reverse_pitch);
                    }else{
                        impact_orientation=fixed_model_rotation_basis(0U,0x4000U,0U);
                        impact_orientation.translation=projectile.impact_position;
                    }
                    append_effect_visual(*impact,projectile.impact_position,impact_age,0.0F,
                        static_cast<uint32_t>(projectile.serial),&impact_orientation,std::nullopt,faction);
                }
            }
        }
    }
    if(fog_preview_mode_!=0){
        const auto& field=document_->field();
        const uint32_t layer=untextured_layer();
        const auto append_fog_vertex=[&](uint32_t x,uint32_t y){
            const auto& cell=field.at(x,y);
            // Match the exact terrain heightfield. The fog pass is composited without depth
            // testing after the animated surface, so water/lava height and shader displacement
            // cannot punch holes through it.
            const float top=cell.terrain_height();
            next.vertices.push_back(ObjectVertex{{thandor::q12_to_float(cell.world_x_q12),
                thandor::q12_to_float(cell.world_y_q12),top+0.02F},
                {0.0F,0.0F,1.0F},0.5F,0.5F,layer,0xDC10141CU});
        };
        for(uint32_t y=0;y+1U<field.height();++y){
            for(uint32_t x=0;x+1U<field.width();++x){
                const std::size_t cell_index=static_cast<std::size_t>(y)*field.width()+x;
                if(cell_index<fog_preview_visible_cells_.size()&&
                   fog_preview_visible_cells_[cell_index]!=0U)continue;
                if(next.vertices.size()+4U>kMaximumDynamicVertices||
                   next.indices.size()+6U>kMaximumDynamicIndices)
                    throw std::runtime_error("fog-of-war preview geometry limit exceeded");
                const uint32_t base=static_cast<uint32_t>(next.vertices.size());
                append_fog_vertex(x,y);append_fog_vertex(x+1U,y);
                append_fog_vertex(x+1U,y+1U);append_fog_vertex(x,y+1U);
                uint32_t first=static_cast<uint32_t>(next.indices.size());
                // FieldGrid uses the BL->TR diagonal as {BL,TL,BR} and {TL,TR,BR}. The old
                // opposite split visibly detached on steep cells and exposed the terrain/lava.
                next.indices.insert(next.indices.end(),{base,base+3U,base+1U});
                append_draw_run(PrimitiveBlendPreset::FogOverlayNoDepthTest,true,first);
                first=static_cast<uint32_t>(next.indices.size());
                next.indices.insert(next.indices.end(),{base+3U,base+2U,base+1U});
                append_draw_run(PrimitiveBlendPreset::FogOverlayNoDepthTest,true,first);
            }
        }
    }
    order_dynamic_object_queue_like_game(next.vertices,next.indices,next.draw_runs,camera_);
    visible_animated_building_count_=visible_animated_buildings_.size();
    visible_timed_effect_controller_count_=visible_timed_effect_controllers_.size();
    dynamic_triangle_count_=next.indices.size()/3U;
    resource_shading_emitter_count_=next.shading_emitters.size();
    dynamic_scene_cpu_=std::move(next);
    ++dynamic_scene_revision_;
    dynamic_scene_build_milliseconds_=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-started).count();
}

void EditorApp::upload_dynamic_scene(uint32_t frame_index){
    if(frame_index>=VulkanContext::kFramesInFlight)throw std::runtime_error("dynamic upload frame index is out of range");
    if(uploaded_dynamic_scene_revisions_[frame_index]==dynamic_scene_revision_)return;
    renderer_.update_dynamic_objects(frame_index,dynamic_scene_cpu_.vertices,
        dynamic_scene_cpu_.indices,dynamic_scene_cpu_.draw_runs,
        dynamic_scene_cpu_.shadow_vertices,dynamic_scene_cpu_.shadow_indices);
    renderer_.update_resource_shading(frame_index,dynamic_scene_cpu_.shading_emitters);
    uploaded_dynamic_scene_revisions_[frame_index]=dynamic_scene_revision_;
}

void EditorApp::capture_faction_camera_start(uint32_t faction_index){
    if(!document_)return;
    const auto eye=camera_.eye();
    const Vec3f forward{camera_.target.x-eye.x,camera_.target.y-eye.y,camera_.target.z-eye.z};
    const float horizontal=std::hypot(forward.x,forward.y);
    auto bookmark=document_->level().faction_camera_start(faction_index);
    bookmark.position_x_q12=thandor::float_to_q12(eye.x);
    bookmark.position_y_q12=thandor::float_to_q12(eye.y);
    bookmark.position_z_q12=thandor::float_to_q12(eye.z);
    bookmark.motion_magnitude_q12=std::max<uint32_t>(bookmark.motion_magnitude_q12,0x1000U);
    bookmark.heading_turn16=radians_to_turn16(std::atan2(forward.y,forward.x));
    bookmark.pitch_turn16=signed_radians_to_turn16(std::atan2(forward.z,horizontal));
    checkpoint();
    document_->level().set_faction_camera_start(faction_index,bookmark);
    document_->mark_dirty();
    status_="Saved the current view into faction "+std::to_string(faction_index)+
        " LEV camera record.";
}

void EditorApp::focus_faction_camera_start(uint32_t faction_index){
    if(!document_)return;
    const auto bookmark=document_->level().faction_camera_start(faction_index);
    const float heading=turn16_to_radians(bookmark.heading_turn16);
    const float pitch=signed_turn16_to_radians(bookmark.pitch_turn16);
    const Vec3f eye{bookmark.position_x(),bookmark.position_y(),bookmark.position_z()};
    const Vec3f forward{std::cos(pitch)*std::cos(heading),
                        std::cos(pitch)*std::sin(heading),std::sin(pitch)};
    float distance=std::clamp(camera_.distance,8.0F,120.0F);
    if(forward.z<-0.02F){
        for(int iteration=0;iteration<5;++iteration){
            const float sample_x=eye.x+forward.x*distance;
            const float sample_y=eye.y+forward.y*distance;
            const float terrain_z=terrain_height_near(sample_x,sample_y);
            const float candidate=(terrain_z-eye.z)/forward.z;
            if(std::isfinite(candidate)&&candidate>2.0F&&candidate<500.0F)distance=candidate;
        }
    }
    camera_.target={eye.x+forward.x*distance,eye.y+forward.y*distance,
                    eye.z+forward.z*distance};
    camera_.distance=distance;
    camera_.yaw=std::remainder(heading-std::numbers::pi_v<float>,
                               2.0F*std::numbers::pi_v<float>);
    camera_.pitch=std::clamp(-pitch,Camera::kMinimumPitch,Camera::kMaximumPitch);
    status_="Loaded faction "+std::to_string(faction_index)+" camera start from the LEV header.";
}

void EditorApp::aim_camera_at_faction_start_vehicle(uint32_t faction_index){
    if(!document_)return;
    for(const auto& placement:document_->level().placements()){
        if(placement.army_asset_id!=50U||placement.faction_index!=faction_index)continue;
        const float x=placement.render_x();
        const float y=placement.render_y();
        camera_.target={x,y,terrain_height_near(x,y)};
        status_="Aimed the editor view at faction "+std::to_string(faction_index)+
            " start vehicle. Adjust the view, then save it as the faction camera.";
        return;
    }
    status_="Faction "+std::to_string(faction_index)+" has no stock player-start vehicle (ARM 50).";
}

void EditorApp::center_camera(){if(!document_)return;const auto& f=document_->field();const auto& a=f.at(0,0);const auto& b=f.at(f.width()-1,f.height()-1);camera_.target={(thandor::q12_to_float(a.world_x_q12)+thandor::q12_to_float(b.world_x_q12))*0.5f,(thandor::q12_to_float(a.world_y_q12)+thandor::q12_to_float(b.world_y_q12))*0.5f,0};camera_.distance=std::max(15.0f,std::hypot(thandor::q12_to_float(b.world_x_q12-a.world_x_q12),thandor::q12_to_float(b.world_y_q12-a.world_y_q12))*0.9f);}
EditorApp::Snapshot EditorApp::snapshot()const{return {document_->field().serialize(),document_->level().serialize(),std::vector<thandor::PlacementTag>(document_->placement_tags().begin(),document_->placement_tags().end()),document_->map_name(),document_->display_name(),document_->scenario_record_bytes(),document_->level_text_bytes()};}
void EditorApp::restore(const Snapshot& s){document_->field()=thandor::FieldGrid::parse(s.field);document_->level()=thandor::LevelAsset::parse(s.level);document_->placement_tags()=s.tags;document_->set_map_name(s.map_name);document_->set_display_name(s.display_name);document_->restore_editor_metadata(s.scenario_record,s.level_text);document_->mark_dirty();selected_edit_index_.reset();refresh_scenario_text_buffers();load_materials();rebuild_mesh();}
void EditorApp::checkpoint(){if(!document_)return;undo_.push_back(snapshot());if(undo_.size()>64)undo_.erase(undo_.begin());redo_.clear();}
void EditorApp::undo(){if(undo_.empty()||!document_)return;redo_.push_back(snapshot());auto s=std::move(undo_.back());undo_.pop_back();restore(s);status_="Undo";}
void EditorApp::redo(){if(redo_.empty()||!document_)return;undo_.push_back(snapshot());auto s=std::move(redo_.back());redo_.pop_back();restore(s);status_="Redo";}
thandor::PlacementRole EditorApp::placement_role_for_preset(
    const thandor::ArmyAssetPreset& preset) const noexcept {
    if(thandor::placement_is_resource(preset))return thandor::PlacementRole::Resource;
    if(preset.browser_kind==thandor::PlacementBrowserKind::Element)
        return thandor::PlacementRole::EffectOrScenery;
    switch(preset.visual_kind){
    case thandor::PlacementVisualKind::PlayerStart:
        return thandor::PlacementRole::PlayerStart;
    case thandor::PlacementVisualKind::PowerResource:
    case thandor::PlacementVisualKind::BuildingResource:
        return thandor::PlacementRole::Resource;
    case thandor::PlacementVisualKind::Tree:
    case thandor::PlacementVisualKind::Rock:
    case thandor::PlacementVisualKind::ResourceScenery:
        return thandor::PlacementRole::EffectOrScenery;
    default:
        return preset.group_hint.find("Scenery")!=std::string::npos?
            thandor::PlacementRole::EffectOrScenery:thandor::PlacementRole::Generic;
    }
}

const thandor::ArmyAssetPreset* EditorApp::terrain_overlay_profile() const noexcept {
    const auto* selected=catalog_.army_preset(selected_asset_id_);
    if(terrain_compatibility_overlay_==1){
        if(selected!=nullptr&&selected->has_traversal_profile&&
           (selected->browser_kind==thandor::PlacementBrowserKind::Unit||
            selected->visual_kind==thandor::PlacementVisualKind::PlayerStart))return selected;
        const auto* start=catalog_.army_preset(50U);
        if(start!=nullptr&&start->has_traversal_profile)return start;
        for(const auto& preset:catalog_.army_presets())
            if(preset.browser_visible&&preset.has_traversal_profile&&
               preset.browser_kind==thandor::PlacementBrowserKind::Unit)return &preset;
    }
    if(terrain_compatibility_overlay_==2){
        if(selected!=nullptr&&selected->has_placement_occupancy_class&&
           selected->browser_kind==thandor::PlacementBrowserKind::Building)return selected;
        for(const auto& preset:catalog_.army_presets())
            if(preset.browser_visible&&preset.has_placement_occupancy_class&&
               preset.browser_kind==thandor::PlacementBrowserKind::Building)return &preset;
    }
    return nullptr;
}

const thandor::ArmyTechnologyLevelPreview* EditorApp::technology_level_preview(
    const thandor::ArmyAssetPreset& preset,uint32_t level_index)const noexcept{
    const auto found=std::find_if(preset.technology_level_previews.begin(),
        preset.technology_level_previews.end(),[&](const auto& preview){
            return preview.level_index==level_index;
        });
    return found==preset.technology_level_previews.end()?nullptr:&*found;
}

std::span<const thandor::ModelVisualNode> EditorApp::placement_hierarchy(
    const thandor::ArmyAssetPreset& preset,std::size_t placement_index)const noexcept{
    uint32_t level_index=0U;
    if(document_&&placement_index<document_->placement_tags().size())
        level_index=document_->placement_tags()[placement_index].technology_level;
    if(const auto* preview=technology_level_preview(preset,level_index);
       preview!=nullptr&&!preview->resolved_model_hierarchy.empty())
        return preview->resolved_model_hierarchy;
    if(!preset.resolved_model_hierarchy.empty())return preset.resolved_model_hierarchy;
    return preset.model_hierarchy;
}

void EditorApp::draw_technology_placement_popup(){
    static constexpr const char* kPopup="Select technology level##placement";
    if(pending_technology_placement_&&!ImGui::IsPopupOpen(kPopup)){
        const auto position=pending_technology_placement_->popup_screen_position;
        const auto display=ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos({std::clamp(position.x,0.0F,std::max(0.0F,display.x-320.0F)),
                                 std::clamp(position.y,ImGui::GetFrameHeight(),
                                            std::max(ImGui::GetFrameHeight(),display.y-260.0F))},
                                ImGuiCond_Appearing);
        ImGui::OpenPopup(kPopup);
    }
    if(!ImGui::BeginPopup(kPopup,ImGuiWindowFlags_AlwaysAutoResize))return;
    const auto pending=*pending_technology_placement_;
    const auto* preset=catalog_.army_preset(pending.asset_id);
    if(preset==nullptr){
        ImGui::TextUnformatted("The selected ARM record is no longer available.");
        if(ImGui::Button("Close")){pending_technology_placement_.reset();ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
        return;
    }
    ImGui::TextUnformatted(preset->display_name.c_str());
    ImGui::TextWrapped("Choose the editor preview level. The retail LEV stores no technology level; the game resolves it from the faction technology state.");
    ImGui::Separator();
    for(const auto& preview:preset->technology_level_previews){
        std::string label="Level "+std::to_string(preview.level_index+1U)+
            " · STR: "+preview.technology_effect_title;
        if(ImGui::MenuItem(label.c_str())){
            pending_technology_placement_.reset();
            ImGui::CloseCurrentPopup();
            add_asset_placement(pending.cell_x,pending.cell_y,pending.asset_id,preview.level_index);
            break;
        }
    }
    ImGui::Separator();
    if(ImGui::Button("Cancel",{-1.0F,0.0F})){
        pending_technology_placement_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorApp::add_asset_placement(uint32_t x,uint32_t y,uint32_t asset_id,uint32_t technology_level){
    if(!document_||x>=document_->field().width()||y>=document_->field().height())return;
    const auto* preset=catalog_.army_preset(asset_id);
    if(preset==nullptr){
        status_="ARM registry ID "+std::to_string(asset_id)+" is not available in the asset catalog.";
        return;
    }
    const std::string compatibility=catalog_.placement_compatibility_diagnostic(
        *preset,document_->level());
    if(!compatibility.empty()){
        status_="Cannot place "+preset->display_name+": "+compatibility+
            ". Use a game-ready card for this base map; raw/internal records remain in Files.";
        return;
    }
    const auto role=placement_role_for_preset(*preset);
    uint32_t faction=selected_faction_;
    if(role==thandor::PlacementRole::PlayerStart)faction=std::max(1U,selected_faction_);
    if(role==thandor::PlacementRole::Resource||
       preset->browser_kind==thandor::PlacementBrowserKind::Element)faction=0U;

    checkpoint();
    const auto& cell=document_->field().at(x,y);
    thandor::LevelPlacement placement{};
    const uint32_t placed_asset_id=thandor::canonical_placement_registry_id(*preset);
    placement.army_asset_id=placed_asset_id;
    placement.faction_index=faction;
    placement.set_render_position_q12(cell.world_x_q12,cell.world_y_q12);
    placement.orientation_turn16=selected_orientation_;
    document_->level().add_placement(placement);
    uint32_t technology_effect_id = 0U;
    if (const auto* preview = technology_level_preview(*preset, technology_level); preview != nullptr)
        technology_effect_id = preview->technology_effect_id;
    document_->placement_tags().push_back({document_->level().placements().size()-1U,role,
        preset->display_name,technology_level,technology_effect_id});
    std::size_t resource_field_cells = 0U;
    if(const auto occupancy_class=thandor::resource_field_occupancy_class(placed_asset_id)){
        thandor::Brush resource_brush;
        resource_brush.radius_cells=2.0F;
        resource_brush.strength=1.0F;
        resource_brush.shape=thandor::BrushShape::Square;
        resource_brush.falloff=thandor::BrushFalloff::Constant;
        resource_field_cells=document_->field().paint_placement_occupancy_class(
            static_cast<float>(x),static_cast<float>(y),resource_brush,*occupancy_class,true);
    }
    document_->mark_dirty();
    selected_asset_id_=placed_asset_id;
    selected_placement_=document_->level().placements().size()-1U;
    tool_=Tool::Select;
    if(preset->runtime_model_class_id==17U){
        animate_units_=true;
        simulation_state_for(*selected_placement_,SimulationStateOrigin::FreshPlacement);
    }
    rebuild_objects();

    status_="Placed "+preset->display_name+" (ARM "+std::to_string(placed_asset_id)+") from the 3D asset browser.";
    if (const auto* preview = technology_level_preview(*preset, technology_level); preview != nullptr) {
        status_ += " Preview level " + std::to_string(preview->level_index+1U) +
            " · STR: " + preview->technology_effect_title +
            " is stored only in the .thmap project; retail LEV resolves the live model from faction technology.";
    }
    if(placed_asset_id!=asset_id)
        status_+=" The selected stock alias "+std::to_string(asset_id)+" was normalized to the canonical ID.";
    if(role==thandor::PlacementRole::PlayerStart)
        status_+=" Stock LEV player-start ID 50 uses faction "+std::to_string(faction)+".";
    else if(role==thandor::PlacementRole::Resource)
        status_+=" Resource ownership is neutral; its required stock 5x5 FLD build mask was synchronized ("+
            std::to_string(resource_field_cells)+" changed cells).";
}

void EditorApp::add_placement(uint32_t x,uint32_t y,thandor::PlacementRole role){
    uint32_t asset_id=selected_asset_id_;
    if(role==thandor::PlacementRole::PlayerStart)asset_id=50U;
    if(role==thandor::PlacementRole::Resource){
        const auto* preset=catalog_.army_preset(asset_id);
        if(preset==nullptr||(preset->visual_kind!=thandor::PlacementVisualKind::PowerResource&&
                            preset->visual_kind!=thandor::PlacementVisualKind::BuildingResource)){
            status_="Select the single blue energy-resource or gold building-resource card before placing a buildable deposit.";
            return;
        }
    }
    add_asset_placement(x,y,asset_id);
}
void EditorApp::delete_selected_placement(){if(!selected_placement_)return;checkpoint();const auto index=*selected_placement_;reset_simulation(false);simulation_static_replacement_dirty_=false;document_->level().erase_placement(index);auto& tags=document_->placement_tags();tags.erase(std::remove_if(tags.begin(),tags.end(),[&](auto& t){if(t.placement_index==index)return true;if(t.placement_index>index)--t.placement_index;return false;}),tags.end());selected_placement_.reset();selected_edit_index_.reset();document_->mark_dirty();rebuild_objects();}
void EditorApp::export_game_level_package(){
    try{
        const auto issues=catalog_.validate_level_placements(document_->level());
        if(!issues.empty()){
            const auto& first=issues.front();
            throw std::runtime_error("game export blocked: "+std::to_string(issues.size())+
                " placement(s) have an unavailable ARM/MDL chain; first is placement "+
                std::to_string(first.placement_index)+" ARM "+std::to_string(first.registry_id)+
                ": "+first.message);
        }
        const auto path=document_->export_game_level_package(
            catalog_.game_root(),level_package_slot_,backup_existing_level_package_);
        document_->mark_clean();
        status_="Installed and runtime-validated "+path.filename().string()+
            "; PATCH00 page-0x22/0x23 aliases were synchronized and stale map shadows were removed. Restart the game.";
    }catch(const std::exception& e){status_=e.what();}
}

void EditorApp::update_patch00_overrides(){
    try{
        thandor::Patch00ExportOptions options;
        options.include_custom_assets=patch00_include_custom_assets_;
        const auto path=document_->update_patch00_overrides(
            catalog_.game_root(),level_package_slot_,options,backup_existing_patch00_);
        status_="Updated "+path.filename().string()+
            " with safe custom resources and global text aliases only. Restart the game to remount patches.";
    }catch(const std::exception& e){status_=e.what();}
}

} // namespace editor
