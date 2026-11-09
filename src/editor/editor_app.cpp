#include "thandor/retail_contact.hpp"
#include "thandor/retail_random.hpp"
#include "simulation_lab_selected_residency.hpp"
#include "thandor/simulation_lab_terminal_residency.hpp"
#include "thandor/render_texture_dependencies.hpp"
#include "thandor/simulation_lab_diagnostics.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include "thandor/simulation_lab_phase7_preview.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_presentation.hpp"
#include "thandor/simulation_lab_damage_visuals.hpp"
#include "thandor/simulation_lab_render_assets.hpp"
#include "thandor/simulation_lab_defaults.hpp"
#include "thandor/simulation_storage.hpp"
#include "thandor/simulation_lab_visual.hpp"
#include "thandor/simulation_lab_catalog.hpp"
#include "editor_app.hpp"
#include "editor_paths.hpp"
#include "editor_resource_validation.hpp"
#include "editor_text_color.hpp"
#include "thandor/simulation_placement.hpp"
#include "model_transform.hpp"
#include "model_vertex_lighting.hpp"
#include "articulated_walker_pose.hpp"
#include "thandor/editor_version.hpp"
#include "thandor/simulation_terrain_damage.hpp"
#include "simulation_lab_ui_cadence.hpp"
#include "object_texture_stable_slots.hpp"
#include "shadow_receiver_vertex_compaction.hpp"
#include "shadow_receiver_surface_guard.hpp"
#include "thandor/terrain_shadow.hpp"
#include "thandor/simulation_render_policy.hpp"
#include "thandor/model_animation.hpp"
#include "thandor/path_diagnostics.hpp"
#include "thandor/v528_pathing.hpp"
#include "thandor/frontend_player_texture.hpp"

#include <new>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <cstdio>
#include <cstring>
#include <map>
#include <limits>
#include <numbers>
#include <sstream>
#include <span>
#include <unordered_set>
#include <utility>

namespace editor {
namespace {
constexpr float kStatusBarHeight = 24.0F;
constexpr float kAssetCardWidth = 150.0F;
constexpr float kAssetCardHeight = 196.0F;
constexpr const char* kAssetDragPayloadType = "THANDOR_ARM_PRESET";
std::optional<int32_t> preview_depth_damage(const thandor::FieldGrid& field,
    const thandor::ArmyAssetPreset& preset,Vec3f position) {
    if(!thandor::simulation_uses_water_damage_callback(preset.runtime_model_class_id,
        preset.simulation_motion_profile.placement_contact_kind))return std::nullopt;
    return thandor::simulation_water_damage_amount(field.sample_retail_water_delta_q12(
        thandor::float_to_q12(position.x),thandor::float_to_q12(position.y)),
        preset.water_damage_threshold_q12,preset.water_damage_multiplier);
}

// Retain the original CPU receiver implementation for forensic probes. The viewport
// uses live Vulkan depth maps, with no receiver sheets or CPU mask generation.
constexpr bool kEditorLegacyProjectedShadowMeshes=false;
constexpr thandor::GeneratedShadowSettings kEditorGeneratedShadowSettings{128U,64U,16U,0.035F};

struct AssetDragPayload { uint32_t registry_id{}; };

struct PreparedGeneratedShadowReceiver {
    std::vector<ObjectVertex> vertices;
    std::vector<uint32_t> indices;
    std::size_t rejected_triangles{};
    std::size_t vertices_before_compaction{};
};

struct GeneratedShadowRenderVertex {
    Vec3f position;
    float u{};
    float v{};
    float attenuation{};
};

GeneratedShadowRenderVertex midpoint_generated_shadow_vertex(
    const GeneratedShadowRenderVertex& a,const GeneratedShadowRenderVertex& b) noexcept {
    return GeneratedShadowRenderVertex{{(a.position.x+b.position.x)*0.5F,
                                       (a.position.y+b.position.y)*0.5F,
                                       (a.position.z+b.position.z)*0.5F},
                                      (a.u+b.u)*0.5F,(a.v+b.v)*0.5F,
                                      (a.attenuation+b.attenuation)*0.5F};
}

float sample_shadow_receiver_height(const thandor::FieldGrid& field,float x,float y,float surface_bias,
                                    bool& inside) noexcept {
    const auto sample=field.sample_terrain(x,y);
    inside=sample.inside;
    return sample.height+std::max(0.0F,surface_bias);
}

bool generated_shadow_triangle_needs_refinement(
    const thandor::FieldGrid& field,const GeneratedShadowRenderVertex& a,
    const GeneratedShadowRenderVertex& b,const GeneratedShadowRenderVertex& c,
    float surface_bias) {
    constexpr float kMaximumHoverDelta=0.12F;
    constexpr float kRefineLongEdge=6.0F;
    constexpr float kRefineTerrainRange=0.10F;
    const auto dist_xy=[](const Vec3f& p,const Vec3f& q){
        const float dx=p.x-q.x;const float dy=p.y-q.y;return std::sqrt(dx*dx+dy*dy);
    };
    const float longest_edge=std::max({dist_xy(a.position,b.position),dist_xy(b.position,c.position),
                                       dist_xy(c.position,a.position)});

    struct SampleCheck { float x,y,expected_z; };
    const std::array<SampleCheck,4> checks{{
        {(a.position.x+b.position.x)*0.5F,(a.position.y+b.position.y)*0.5F,(a.position.z+b.position.z)*0.5F},
        {(b.position.x+c.position.x)*0.5F,(b.position.y+c.position.y)*0.5F,(b.position.z+c.position.z)*0.5F},
        {(c.position.x+a.position.x)*0.5F,(c.position.y+a.position.y)*0.5F,(c.position.z+a.position.z)*0.5F},
        {(a.position.x+b.position.x+c.position.x)/3.0F,(a.position.y+b.position.y+c.position.y)/3.0F,
         (a.position.z+b.position.z+c.position.z)/3.0F}
    }};

    float maximum_gap=0.0F;
    float terrain_min=std::numeric_limits<float>::max();
    float terrain_max=-std::numeric_limits<float>::max();
    bool saw_sample=false;
    for(const auto& check:checks){
        bool inside=false;
        const float terrain_z=sample_shadow_receiver_height(field,check.x,check.y,surface_bias,inside);
        if(!inside) continue;
        saw_sample=true;
        maximum_gap=std::max(maximum_gap,std::fabs(check.expected_z-terrain_z));
        terrain_min=std::min(terrain_min,terrain_z);
        terrain_max=std::max(terrain_max,terrain_z);
    }
    if(!saw_sample) return false;
    if(maximum_gap>kMaximumHoverDelta) return true;
    return longest_edge>kRefineLongEdge && (terrain_max-terrain_min)>kRefineTerrainRange;
}

ObjectVertex convert_generated_shadow_vertex(const GeneratedShadowRenderVertex& source,
                                             float tile_u0,float tile_v0,float tile_u1,float tile_v1,
                                             uint32_t tile_layer) noexcept {
    const float u=tile_u0+(tile_u1-tile_u0)*source.u;
    const float v=tile_v0+(tile_v1-tile_v0)*source.v;
    const uint32_t shadow_alpha=thandor::retail_generated_shadow_packet_alpha(
        0xFFU,static_cast<uint8_t>(std::clamp(std::lround(source.attenuation),0L,255L)));
    return ObjectVertex{source.position,{0.0F,0.0F,1.0F},u,v,tile_layer,shadow_alpha<<24U,1U,0.0F,0.0F};
}

void append_generated_shadow_receiver_triangle(
    PreparedGeneratedShadowReceiver& out,const thandor::FieldGrid& field,
    const GeneratedShadowRenderVertex& a,const GeneratedShadowRenderVertex& b,
    const GeneratedShadowRenderVertex& c,float surface_bias,float tile_u0,float tile_v0,
    float tile_u1,float tile_v1,uint32_t tile_layer,unsigned depth,unsigned max_depth,
    std::size_t& rejected_triangles) {
    if(depth<max_depth &&
       generated_shadow_triangle_needs_refinement(field,a,b,c,surface_bias)) {
        auto ab=midpoint_generated_shadow_vertex(a,b);
        auto bc=midpoint_generated_shadow_vertex(b,c);
        auto ca=midpoint_generated_shadow_vertex(c,a);
        for(auto* vertex:{&ab,&bc,&ca}){
            bool inside=false;
            const float terrain_z=sample_shadow_receiver_height(field,vertex->position.x,vertex->position.y,
                                                                surface_bias,inside);
            if(inside) vertex->position.z=terrain_z;
        }
        append_generated_shadow_receiver_triangle(out,field,a,ab,ca,surface_bias,
            tile_u0,tile_v0,tile_u1,tile_v1,tile_layer,depth+1U,max_depth,rejected_triangles);
        append_generated_shadow_receiver_triangle(out,field,ab,b,bc,surface_bias,
            tile_u0,tile_v0,tile_u1,tile_v1,tile_layer,depth+1U,max_depth,rejected_triangles);
        append_generated_shadow_receiver_triangle(out,field,ca,bc,c,surface_bias,
            tile_u0,tile_v0,tile_u1,tile_v1,tile_layer,depth+1U,max_depth,rejected_triangles);
        append_generated_shadow_receiver_triangle(out,field,ab,bc,ca,surface_bias,
            tile_u0,tile_v0,tile_u1,tile_v1,tile_layer,depth+1U,max_depth,rejected_triangles);
        return;
    }
    // A triangle can still bridge a cliff or hover over a depression at the last
    // refinement level. Do not draw a vertical projected-shadow sheet as a wall.
    if(shadow_receiver_triangle_is_upright(a.position,b.position,c.position)) {
        ++rejected_triangles;
        return;
    }
    if(shadow_receiver_triangle_has_unresolved_contact(a.position,b.position,c.position,
           [&](float x,float y)->std::optional<float>{
               const auto sample=field.sample_terrain(x,y);
               if(!sample.inside) return std::nullopt;
               return sample.height;
           },surface_bias)) {
        ++rejected_triangles;
        return;
    }
    const uint32_t base=static_cast<uint32_t>(out.vertices.size());
    out.vertices.push_back(convert_generated_shadow_vertex(a,tile_u0,tile_v0,tile_u1,tile_v1,tile_layer));
    out.vertices.push_back(convert_generated_shadow_vertex(b,tile_u0,tile_v0,tile_u1,tile_v1,tile_layer));
    out.vertices.push_back(convert_generated_shadow_vertex(c,tile_u0,tile_v0,tile_u1,tile_v1,tile_layer));
    out.indices.insert(out.indices.end(),{base,base+1U,base+2U});
}

PreparedGeneratedShadowReceiver build_prepared_generated_shadow_receiver(
    const thandor::FieldGrid& field,std::span<const thandor::GeneratedShadowReceiverVertex> receiver_vertices,
    std::span<const uint32_t> receiver_indices,float surface_bias,float tile_u0,float tile_v0,
    float tile_u1,float tile_v1,uint32_t tile_layer) {
    PreparedGeneratedShadowReceiver out;
    out.vertices.reserve(receiver_indices.size());
    out.indices.reserve(receiver_indices.size());
    constexpr unsigned kMaximumRefineDepth=2U;
    for(std::size_t index=0;index+2U<receiver_indices.size();index+=3U){
        const uint32_t ia=receiver_indices[index];
        const uint32_t ib=receiver_indices[index+1U];
        const uint32_t ic=receiver_indices[index+2U];
        if(ia>=receiver_vertices.size()||ib>=receiver_vertices.size()||ic>=receiver_vertices.size())
            continue;
        const auto& sa=receiver_vertices[ia];
        const auto& sb=receiver_vertices[ib];
        const auto& sc=receiver_vertices[ic];
        append_generated_shadow_receiver_triangle(out,field,
            GeneratedShadowRenderVertex{sa.position,sa.u,sa.v,static_cast<float>(sa.distance_attenuation)},
            GeneratedShadowRenderVertex{sb.position,sb.u,sb.v,static_cast<float>(sb.distance_attenuation)},
            GeneratedShadowRenderVertex{sc.position,sc.u,sc.v,static_cast<float>(sc.distance_attenuation)},
            surface_bias,tile_u0,tile_v0,tile_u1,tile_v1,tile_layer,0U,kMaximumRefineDepth,
            out.rejected_triangles);
    }
    out.vertices_before_compaction=out.vertices.size();
    compact_generated_shadow_receiver_vertices(out.vertices,out.indices);
    return out;
}

constexpr float kTurn16ToRadians = 2.0F * std::numbers::pi_v<float> / 65536.0F;
constexpr float kRadiansToTurn16 = 65536.0F / (2.0F * std::numbers::pi_v<float>);

uint64_t technology_mask_hash(const thandor::FactionTechnologyMask256& mask) noexcept {
    uint64_t hash = 1469598103934665603ULL;
    for (uint32_t word : mask) {
        hash ^= word;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string technology_hierarchy_cache_key(
    uint32_t registry_id, uint32_t faction_index,
    const thandor::FactionTechnologyMask256& mask) {
    std::string key = std::to_string(registry_id) + ":" + std::to_string(faction_index);
    char word_text[16]{};
    for (uint32_t word : mask) {
        std::snprintf(word_text, sizeof(word_text), ":%08X", word);
        key += word_text;
    }
    return key;
}

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

ImU32 graphics_family_ui_swatch(uint32_t graphics_family, uint32_t alpha = 255U) noexcept {
    // Keep every editor swatch on the same core family table as the textual labels.
    // World/preview colour still comes from the selected armyN.gfx/armyN.pal resources.
    const uint32_t rgb=thandor::graphics_family_representative_rgb24(graphics_family);
    return IM_COL32((rgb>>16U)&0xFFU,(rgb>>8U)&0xFFU,rgb&0xFFU,std::min(alpha,255U));
}

ImVec4 graphics_family_ui_text_color(uint32_t graphics_family) noexcept {
    const auto color=readable_editor_text_rgb(
        thandor::graphics_family_single_player_text_rgb24(graphics_family));
    return {color[0],color[1],color[2],1.0F};
}

std::string faction_single_player_label(uint32_t faction_index, uint32_t graphics_family,
                                        bool include_debug_family = true) {
    if (faction_index == 0U) return "Neutral / map-owned";
    std::string label=thandor::graphics_family_single_player_faction_name(graphics_family);
    label += " · Faction " + std::to_string(faction_index);
    if (graphics_family == 8U) label += " [mission-only AI colour]";
    if (include_debug_family) label += " (army" + std::to_string(graphics_family) + ")";
    return label;
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

[[maybe_unused]] uint32_t sample_preview_patch_argb(const thandor::RgbaImage& image,
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

float animated_child2_translation_q12(const thandor::ModelRuntimeSimulationProfile& profile,
                                      uint32_t tick,float initial_translation) noexcept {
    if(profile.pitch_max_velocity_turn16==0||
       profile.pitch_max_turn16<=profile.pitch_min_turn16) return initial_translation;
    return thandor::q12_to_float(thandor::model_runtime_child2_translation_q12(
        thandor::float_to_q12(initial_translation),profile.pitch_min_turn16,
        profile.pitch_max_turn16,profile.pitch_max_velocity_turn16,tick));
}

struct ModelHierarchyInstance {
    const thandor::ModelVisualNode* node{};
    const thandor::SpriteAsset* sprite{};
    ModelHierarchyTransform transform{};
    bool root{};
};

void record_render_receipt_geometry(SimulationLabRenderReceipt& receipt,
    std::span<const ObjectVertex> vertices,std::span<const uint32_t> indices,
    std::size_t first_index) {
    receipt.triangles=static_cast<uint32_t>((indices.size()-first_index)/3U);
    receipt.bounds_valid=indices.size()>first_index;
    if(!receipt.bounds_valid)return;
    uint32_t last_vertex=indices[first_index];
    receipt.first_vertex=last_vertex;
    receipt.bounds_min=receipt.bounds_max=vertices[indices[first_index]].position;
    for(std::size_t index=first_index;index<indices.size();++index){
        const auto v=vertices[indices[index]].position;
        receipt.first_vertex=std::min(receipt.first_vertex,indices[index]);
        last_vertex=std::max(last_vertex,indices[index]);
        receipt.bounds_valid=receipt.bounds_valid&&std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
        receipt.bounds_min={std::min(receipt.bounds_min.x,v.x),std::min(receipt.bounds_min.y,v.y),std::min(receipt.bounds_min.z,v.z)};
        receipt.bounds_max={std::max(receipt.bounds_max.x,v.x),std::max(receipt.bounds_max.y,v.y),std::max(receipt.bounds_max.z,v.z)};
    }
    receipt.vertex_count=last_vertex-receipt.first_vertex+1U;
}

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
        preset.articulated_root_height_q12,preset.articulated_contact_interpolation_rate_q12,
        preset.articulated_contact_lift_q12,preset.articulated_contact_steering_limit_turn16,
        actor,false,std::forward<TerrainHeightFn>(terrain_height),[&](uint32_t node_index){
            return node_index<instances.size()&&instances[node_index].sprite!=nullptr
                ?instances[node_index].sprite->placement_height_offset():0.0F;
        });
    if(!result.applied)return false;
    for(std::size_t index=0;index<instances.size();++index)
        instances[index].transform=transforms[index];
    return true;
}

const char* tool_name(Tool tool){switch(tool){case Tool::Select:return "Select";case Tool::Raise:return "Raise";case Tool::Lower:return "Lower";case Tool::Smooth:return "Smooth";case Tool::Flatten:return "Flatten";case Tool::Water:return "Water";case Tool::ResourceField:return "Resource field";case Tool::Texture:return "Texture";case Tool::Resource:return "Resource";case Tool::PlayerStart:return "Player start";case Tool::EffectScenery:return "Effect / scenery";case Tool::SimulationTarget:return "Simulation target";case Tool::SimulationMove:return "Drive selected unit";case Tool::GenerateTerrain:return "Generate terrain";}return "Tool";}
ImVec4 argb_to_imgui(uint32_t value){auto c=thandor::unpack_argb(value);return {c.r,c.g,c.b,c.a};}
uint32_t imgui_to_argb(ImVec4 c){return thandor::pack_argb({c.x,c.y,c.z,c.w});}
bool contains_ci(std::string_view text,std::string_view filter){if(filter.empty())return true;auto lower=[](char c){return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));};for(std::size_t i=0;i+filter.size()<=text.size();++i){bool same=true;for(std::size_t j=0;j<filter.size();++j)if(lower(text[i+j])!=lower(filter[j])){same=false;break;}if(same)return true;}return false;}

void show_item_tooltip(std::string_view text,float preferred_max_width=420.0F){
    if(text.empty()||!ImGui::IsItemHovered())return;
    const float display_width=std::max(240.0F,ImGui::GetIO().DisplaySize.x);
    const float max_width=std::min(preferred_max_width,std::max(220.0F,display_width-48.0F));
    const float wrap_width=std::clamp(display_width*0.30F,260.0F,max_width);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0F,0.0F),ImVec2(wrap_width+ui_px(24.0F),ui_px(10000.0F)));
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+wrap_width);
    ImGui::TextUnformatted(text.data(),text.data()+text.size());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

const char* scheduled_condition_kind_name(thandor::LevelScheduledConditionKind kind){
    using K=thandor::LevelScheduledConditionKind;
    switch(kind){
    case K::NoneOrUnused:return "None / unused";
    case K::NoActiveEntityWithDefinition:return "Faction has no linked army";
    case K::NoActiveEntityWithDefinitionAndClassCommandGroupA:return "Faction has no command-group-A army";
    case K::NoActiveEntityWithDefinitionAndRuntimeId:return "Faction has no army of asset";
    case K::FactionInactiveOrRelationAtLeast8:return "Faction inactive OR relation >= 8";
    case K::XeniteCurrentAtLeast:return "Faction Xenite current >= threshold";
    case K::TritiumCurrentAtLeast:return "Faction Tritium current >= threshold";
    case K::ActiveArmyScaleValueAtLeast:return "Faction Tritium extraction rate Q4/tick >= threshold";
    case K::MatchingDefinitionAndRuntimeIdActiveEntityCountAtLeast:return "Faction army asset count >= threshold";
    case K::FactionTerrainOccupancyPercentAtLeast:return "Faction terrain occupancy percent >= threshold";
    case K::CountdownElapsed:return "Countdown elapsed";
    case K::XeniteStorageLimitAtMost0FA0:return "Faction Xenite storage limit <= 0xFA0";
    case K::NoActiveEntityWithClassIdOutsideClassCommandGroupA:return "No active entity of class outside command group A";
    case K::BooleanPostfixExpression:return "Boolean postfix expression";
    }
    return "Unknown";
}

std::string format_condition_expression(const thandor::LevelScheduledCondition& condition){
    std::string out;
    for(const uint8_t token:condition.payload_bytes){
        if(!out.empty())out+=' ';
        if(token==0xFCU){out+="END";break;}
        if(token==0xFDU)out+="NOT";
        else if(token==0xFEU)out+="AND";
        else if(token==0xFFU)out+="OR";
        else out+="C"+std::to_string(token);
    }
    return out;
}
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

constexpr uint32_t kRetailFrontendPlayerTextureSubresource=113U;
constexpr uint64_t kRetailFrontendPlayerTextureLayerNamespace=uint64_t{1}<<62U;

uint64_t army_texture_layer_key(uint32_t runtime_faction,uint32_t graphics_family,
                                uint32_t subresource,bool local_player_override_available,
                                uint32_t local_player_runtime_faction) noexcept {
    uint64_t key=(static_cast<uint64_t>(std::min(graphics_family,8U))<<32U)|subresource;
    // ArmyGraphics_CopyFrontendPlayerPaletteAndTexture patches the already-selected graphics
    // binding, not the shared armyN.gfx source. Keep faction-1's patched subresource distinct even
    // when another faction resolves to the same armyN family.
    if(local_player_override_available&&runtime_faction==local_player_runtime_faction&&
       subresource==kRetailFrontendPlayerTextureSubresource)
        key|=kRetailFrontendPlayerTextureLayerNamespace;
    return key;
}

std::filesystem::path find_loose_file_case_insensitive(const std::filesystem::path& root,
                                                        std::string_view filename){
    if(!std::filesystem::is_directory(root))return {};
    const std::string wanted=thandor::lower_ascii(std::string(filename));
    for(const auto& entry:std::filesystem::directory_iterator(root)){
        if(!entry.is_regular_file())continue;
        if(thandor::lower_ascii(entry.path().filename().string())==wanted)return entry.path();
    }
    return {};
}

struct DeferredObjectPacket {
    // Point into the immutable original index array until the sorted output
    // replaces it. Avoid a heap allocation for every alpha-blended triangle.
    std::span<const uint32_t> indices;
    PrimitiveBlendPreset blend{PrimitiveBlendPreset::OpaqueDepthWrite};
    bool two_sided{};
    uint32_t sort_group{};
    RetailWorldModelPass retail_world_pass{RetailWorldModelPass::PostGeneratedShading};
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
    deferred.reserve(indices.size()/3U);
    const Vec3f eye=camera.eye();
    const Vec3f forward=normalize(sub(camera.target,eye));
    const auto append_packet=[&](PrimitiveBlendPreset blend,bool two_sided,
                                 uint32_t sort_group,RetailWorldModelPass retail_world_pass,
                                 std::span<const uint32_t> packet_indices){
        const uint32_t first=static_cast<uint32_t>(ordered_indices.size());
        ordered_indices.insert(ordered_indices.end(),packet_indices.begin(),packet_indices.end());
        if(!ordered_runs.empty()){
            auto& previous=ordered_runs.back();
            if(previous.blend==blend&&previous.two_sided==two_sided&&
               previous.sort_group==sort_group&&previous.retail_world_pass==retail_world_pass&&
               previous.first_index+previous.index_count==first){
                previous.index_count+=static_cast<uint32_t>(packet_indices.size());return;
            }
        }
        ordered_runs.push_back(ObjectDrawRun{blend,two_sided,first,
            static_cast<uint32_t>(packet_indices.size()),sort_group,retail_world_pass});
    };
    std::size_t source_order=0U;
    for(const auto& run:runs){
        if(run.index_count==0U||run.index_count%3U!=0U||
           run.first_index>indices.size()||run.index_count>indices.size()-run.first_index)
            throw std::runtime_error("dynamic object draw run is not triangle aligned");
        if(run.blend==PrimitiveBlendPreset::OpaqueDepthWrite){
            append_packet(run.blend,run.two_sided,run.sort_group,run.retail_world_pass,
                std::span<const uint32_t>(indices).subspan(run.first_index,run.index_count));
            source_order+=run.index_count/3U;
            continue;
        }
        if(run.sort_group!=0U){
            DeferredObjectPacket packet;
            packet.blend=run.blend;
            packet.two_sided=run.two_sided;
            packet.sort_group=run.sort_group;
            packet.retail_world_pass=run.retail_world_pass;
            packet.source_order=source_order;
            packet.indices=std::span<const uint32_t>(indices).subspan(
                run.first_index,run.index_count);
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
            packet.indices=std::span<const uint32_t>(indices).subspan(
                run.first_index+offset,3U);
            packet.blend=run.blend;
            packet.two_sided=run.two_sided;
            packet.sort_group=0U;
            packet.retail_world_pass=run.retail_world_pass;
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
        if(left.retail_world_pass!=right.retail_world_pass)
            return left.retail_world_pass<right.retail_world_pass;
        if(left.view_depth!=right.view_depth)return left.view_depth>right.view_depth;
        return left.source_order<right.source_order;
    });
    for(const auto& packet:deferred)
        append_packet(packet.blend,packet.two_sided,packet.sort_group,
                      packet.retail_world_pass,packet.indices);
    indices=std::move(ordered_indices);
    runs=std::move(ordered_runs);
}
}

EditorApp::EditorApp(GLFWwindow* window,VulkanContext& vulkan,std::filesystem::path game_root):window_(window),vulkan_(vulkan),catalog_(thandor::AssetCatalog::scan_game_root(game_root)),renderer_(vulkan){
    simulation_lab_config_.vary_seed_per_run=true;
    startup_validation_=catalog_.validate_supported_assets();
    // A corrupt optional asset must not prevent opening otherwise usable maps.
    // Retain validation results and report each path; guarded loaders supply fallbacks.
    for(const auto& issue:startup_validation_.issues)report_asset_failure(issue.path,issue.message);
    try{
        game_cursor_=GameCursorAsset::parse(catalog_.load("engine/mouse.gfx"),
                                            catalog_.load("engine/mouse.dat"));
    }catch(const std::bad_alloc&){throw;}
    catch(const std::exception& error){
        report_asset_failure("engine/mouse.gfx + mouse.dat",error.what());game_cursor_enabled_=false;
    }
    try {
        const auto ki_image = catalog_.load("engine/ki.dat");
        ai_knowledge_ = thandor::AiKnowledgeData::parse(ki_image);
        const auto semantics=thandor::known_ai_knowledge_semantics();
        if(!semantics.empty()) {
            simulation_lab_semantic_row_=std::clamp(simulation_lab_semantic_row_,0,static_cast<int>(semantics.size())-1);
            simulation_lab_config_=thandor::simulation_lab_loaded_baseline_config(ai_knowledge_->parameters(),
                semantics[static_cast<size_t>(simulation_lab_semantic_row_)].index);
        }
    } catch (const std::exception& error) {
        // KI.DAT is optional editor diagnostics. Missing/modded AI data must not prevent map work.
        ai_knowledge_.reset();
        ai_knowledge_load_error_ = error.what();
    }
    try {
        // Retail PersistentSettingsRuntime starts from thandor.dat. The local frontend player's
        // twenty-code-unit name at +0x60 is sanitized and resolved beside the executable as .pcx;
        // when that 64x64 indexed preview exists, ArmyGraphics patches binding/faction 1's GFX
        // subresource 113 with its palette and pixels before model rendering begins.
        const auto settings_path=find_loose_file_case_insensitive(catalog_.game_root(),"thandor.dat");
        if(!settings_path.empty()){
            const auto player_name=thandor::persistent_settings_player_name(thandor::read_file(settings_path));
            if(!player_name.empty()){
                const auto pcx_leaf=thandor::retail_frontend_player_pcx_leaf(player_name);
                const auto requested=std::filesystem::path(pcx_leaf).filename().string();
                const auto pcx_path=find_loose_file_case_insensitive(catalog_.game_root(),requested);
                if(!pcx_path.empty()){
                    local_player_texture_override_=thandor::render_retail_frontend_player_texture(
                        thandor::decode_frontend_player_pcx64(thandor::read_file(pcx_path)));
                    local_player_texture_override_status_="Retail local-player PCX: "+pcx_path.filename().string();
                }else{
                    local_player_texture_override_status_="Retail local-player PCX not found: "+requested;
                }
            }else local_player_texture_override_status_="thandor.dat has no persisted local player name";
        }else local_player_texture_override_status_="thandor.dat not found; stock army textures are unchanged";
    } catch(const std::exception& error){
        // Retail treats a missing/invalid PCX as a failed optional snapshot and keeps the loaded
        // army graphics package unchanged. Match that non-fatal behavior but keep the reason visible.
        local_player_texture_override_.reset();
        local_player_texture_override_status_=std::string("Local-player PCX ignored: ")+error.what();
    }
    if(catalog_.map_groups().empty())throw std::runtime_error("no paired LEV/FLD maps were found in the game PCK archives");
    refresh_custom_maps();
    load_scenario_catalog_labels();
    template_group_=0;
    user_settings_path_=editor_user_settings_path(application_directory());
    EditorUserSettings defaults;
    defaults.simulation_lab=simulation_lab_config_;
    const auto preferences=editor_user_settings_read(user_settings_path_,std::move(defaults));
    simulation_lab_config_=preferences.simulation_lab;
    if(ai_knowledge_){
        try{
            ai_profiles_.initialize(ai_knowledge_->parameters(),application_directory(),preferences.ai_profile=="custom");
            ai_knowledge_=ai_profile_knowledge(ai_profiles_.active());
        }catch(const std::exception& error){ai_profiles_.message=error.what();}
    }
    logging_enabled_=preferences.logging_enabled;
    ui_scale_percent_=preferences.ui_scale_percent;
    simulation_lab_tick_duration_ms_=preferences.simulation_tick_ms;
    simulation_lab_storage_limit_overrides_enabled_=preferences.storage_overrides_enabled;
    simulation_lab_xenite_storage_limit_q4_=preferences.xenite_storage_q4;
    simulation_lab_tritium_storage_limit_q4_=preferences.tritium_storage_q4;
    simulation_lab_show_all_runs_=preferences.show_other_runs;
    const auto semantics=thandor::known_ai_knowledge_semantics();
    if(!semantics.empty()){
        const auto desired=simulation_lab_config_.parameter_index;
        const auto found=std::find_if(semantics.begin(),semantics.end(),
            [&](const auto& item){return item.index==desired;});
        if(found!=semantics.end())simulation_lab_semantic_row_=static_cast<int>(found-semantics.begin());
        else simulation_lab_config_.parameter_index=semantics[static_cast<std::size_t>(
            std::clamp(simulation_lab_semantic_row_,0,static_cast<int>(semantics.size())-1))].index;
    }
    user_settings_restoring_=true;
    bool reopened=false;
    if(preferences.last_map_type=="file"&&!preferences.last_map_value.empty())
        reopened=load_map_from_path(preferences.last_map_value);
    else if(preferences.last_map_type=="stock"&&!preferences.last_map_value.empty()){
        const auto& groups=catalog_.map_groups();
        const auto found=std::find_if(groups.begin(),groups.end(),[&](const auto& group){
            return group.level_path==preferences.last_map_value;});
        if(found!=groups.end()){
            load_map(static_cast<std::size_t>(found-groups.begin()));
            reopened=(document_!=nullptr);
        }
    }
    if(!reopened)load_map(0);
    user_settings_restoring_=false;
    if(reopened)status_="Restored last map: "+document_->display_name();
    else status_="Loaded "+document_->display_name()+(preferences.last_map_value.empty()?"":
        "; previously opened map unavailable, using first stock map");
    // Don't rewrite the INI just because the editor started. Changes flush on
    // a short debounce, plus at orderly shutdown.
    std::error_code preferences_error;
    last_saved_user_settings_=std::filesystem::exists(user_settings_path_,preferences_error)
        ?editor_user_settings_encode(preferences):std::string{};
}

void EditorApp::start_editor_performance_log(){
    if(!logging_enabled_)return;
    try{
        const auto directory=editor_logs_directory();
        std::filesystem::create_directories(directory);
        const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto suffix=std::chrono::steady_clock::now().time_since_epoch().count()&0xFFFFU;
        editor_performance_log_.start(directory/("editor_"+std::to_string(stamp)+"_"+
            std::to_string(suffix)+".log"),thandor::kEditorVersion);
        for(const auto& [path,message]:asset_load_failures_)
            editor_performance_log_.error("ASSET_FALLBACK path="+path+" reason="+message);
    }catch(const std::exception& error){
        editor_performance_log_.unavailable(error.what());
    }
}
EditorPerfContext EditorApp::editor_performance_context() const{
    EditorPerfContext c;c.map=document_?document_->map_name():"<none>";
    c.simulation=simulation_mode_;c.lab=simulation_lab_active();c.paused=simulation_lab_paused_;
    c.tick=current_presentation_tick_;c.fog_mode=fog_preview_mode_;c.shadows=projected_object_shadows_;
    c.animate_units=animate_units_;c.animate_buildings=animate_buildings_;c.animate_effects=animate_timed_effects_;c.animate_surface=animate_surface_;
    if(document_){c.placements=document_->level().placements().size();c.field_width=document_->field().width();c.field_height=document_->field().height();}
    c.terrain_vertices=renderer_.cpu_vertices().size();c.static_model_indices=renderer_.object_index_count();c.static_shadow_indices=renderer_.shadow_index_count();
    c.model_vertices=dynamic_scene_cpu_.vertices.size();c.model_indices=dynamic_scene_cpu_.indices.size();
    c.shadow_vertices=dynamic_scene_cpu_.shadow_vertices.size();c.shadow_indices=dynamic_scene_cpu_.shadow_indices.size();
    c.textures=object_texture_layer_count_;c.shadow_templates=dynamic_shadow_templates_.size();c.dynamic_revision=dynamic_scene_revision_;
    c.upload_bytes=c.model_vertices*sizeof(ObjectVertex)+c.model_indices*sizeof(uint32_t)+
        c.shadow_vertices*sizeof(ObjectVertex)+c.shadow_indices*sizeof(uint32_t)+
        dynamic_scene_cpu_.shading_emitters.size()*sizeof(ResourceShadingEmitter)+fog_preview_runtime_cells_.size()*sizeof(uint32_t);
    const auto& upload=renderer_.object_upload_stats();
    c.object_upload={upload.calls,upload.skipped,upload.array_recreates,upload.partial_uploads,
        upload.layers_uploaded,upload.bytes_uploaded,upload.buffer_writes,upload.idle_waits,
        upload.resampled,upload.reused,upload.capacity,upload.last_dirty_layers,upload.relocated,
        upload.frame_fence_waits,upload.frame_slot_uploads,upload.frame_slot_skips,
        upload.async_copies};
    for (const auto& [key, family] : army_texture_cache_) {
        (void)key;
        if (!family) continue;
        c.canvas_cache[0] += family->sprite_canvases.hits();
        c.canvas_cache[1] += family->sprite_canvases.misses();
        c.canvas_cache[2] += family->sprite_canvases.evictions();
        c.canvas_cache[3] += family->sprite_canvases.resident_bytes();
    }
    const auto& cached=dynamic_model_cache_.scene;
    c.source_reuse={object_texture_source_reuse_.hits(),object_texture_source_reuse_.misses(),
        object_texture_source_reuse_.retained_bytes()};
    c.object_layer_identity=object_layer_identity_totals_;
    c.generated_shadow_cache_hits=generated_shadow_reuse_.hits();
    c.generated_shadow_cache_misses=generated_shadow_reuse_.misses();
    c.generated_shadow_cache_entries=generated_shadow_reuse_.entries();
    c.generated_shadow_compaction_saved_vertices=generated_shadow_compaction_saved_vertices_;
    c.generated_shadow_surface_culled_triangles=generated_shadow_surface_culled_triangles_;
    c.generated_shadow_dynamic_culled_triangles=generated_shadow_dynamic_culled_triangles_;
    c.generated_shadow_pose_cache_hits=generated_shadow_pose_cache_hits_;
    c.generated_shadow_pose_cache_misses=generated_shadow_pose_cache_misses_;
    c.generated_shadow_pose_cache_reused_vertices=generated_shadow_pose_cache_reused_vertices_;
    c.dynamic_texture_family_fast_hits=dynamic_texture_family_fast_hits_;
    c.dynamic_texture_family_fast_misses=dynamic_texture_family_fast_misses_;
    c.lab_snapshot_dirty_coalesced=lab_snapshot_dirty_coalesced_;
    c.lab_selected_render_variants=lab_selected_render_variants_;
    c.lab_product_cache_hits=simulation_lab_product_cache_.hits();
    c.lab_product_cache_misses=simulation_lab_product_cache_.misses();
    c.lab_product_cache_assets=simulation_lab_product_cache_.assets();
    c.lab_runtime_catalog_cache_hits=simulation_lab_runtime_catalog_cache_.hits();
    c.lab_runtime_catalog_cache_misses=simulation_lab_runtime_catalog_cache_.misses();
    c.lab_runtime_catalog_cache_ready=simulation_lab_runtime_catalog_cache_.populated();
    c.scene_cache={dynamic_model_cache_hits_,dynamic_model_cache_misses_,dynamic_view_cache_misses_,
        dynamic_fog_cache_hits_,dynamic_shadow_cache_hits_,
        cached.vertices.size()*sizeof(ObjectVertex)+cached.indices.size()*sizeof(uint32_t)+
        cached.shadow_vertices.size()*sizeof(ObjectVertex)+cached.shadow_indices.size()*sizeof(uint32_t)+
        cached.draw_runs.size()*sizeof(ObjectDrawRun)+cached.shading_emitters.size()*sizeof(ResourceShadingEmitter),
        dynamic_plain_cache_hits_};
    c.camera={camera_.target.x,camera_.target.y,camera_.target.z,camera_.distance,camera_.yaw,camera_.pitch};
    c.viewport={ImGui::GetIO().DisplaySize.x,ImGui::GetIO().DisplaySize.y};
    c.audio_requests=audio_one_shot_requests_;c.audio_submitted=audio_submitted_;c.audio_inaudible=audio_inaudible_;c.audio_missing=audio_missing_samples_;
    c.audio_host_failed=thandor::audio_playback_diagnostics().one_shots_failed;
    return c;
}
void EditorApp::finish_editor_performance_frame(bool presented,EditorPerformanceLog::Clock::time_point started){
    const auto now=EditorPerformanceLog::Clock::now();
    editor_performance_log_.frame(presented,std::chrono::duration<double,std::milli>(now-started).count(),now);
    if(editor_performance_log_.due(now))try{editor_performance_log_.sample(editor_performance_context(),false,now);}catch(...){}
}
EditorApp::~EditorApp(){
    save_user_settings(true);
}

EditorUserSettings EditorApp::collect_user_settings() const{
    EditorUserSettings settings;
    settings.last_map_type=last_opened_map_type_;
    settings.last_map_value=last_opened_map_value_;
    settings.simulation_lab=simulation_lab_config_;
    settings.simulation_tick_ms=simulation_lab_tick_duration_ms_;
    settings.storage_overrides_enabled=simulation_lab_storage_limit_overrides_enabled_;
    settings.xenite_storage_q4=simulation_lab_xenite_storage_limit_q4_;
    settings.tritium_storage_q4=simulation_lab_tritium_storage_limit_q4_;
    settings.show_other_runs=simulation_lab_show_all_runs_;
    settings.logging_enabled=logging_enabled_;
    settings.ui_scale_percent=ui_scale_percent_;
    settings.ai_profile=ai_profiles_.use_custom?"custom":"default";
    return settings;
}

void EditorApp::save_user_settings(bool force) noexcept {
    if(user_settings_restoring_||user_settings_path_.empty())return;
    const auto now=std::chrono::steady_clock::now();
    if(!force && now<next_user_settings_check_)return;
    next_user_settings_check_=now+std::chrono::milliseconds(750);
    try{
        const auto content=editor_user_settings_encode(collect_user_settings());
        if(content==last_saved_user_settings_)return;
        if(editor_user_settings_write(user_settings_path_,content))last_saved_user_settings_=content;
    }catch(...){} // Preferences must never interrupt the editor or map saves.
}

void EditorApp::run(){
    update_ui_scale();
    start_editor_performance_log();
    try{
        while(glfwWindowShouldClose(window_)==GLFW_FALSE){
            {auto timer=editor_performance_log_.measure(EditorPerfStage::PollEvents);glfwPollEvents();}
            frame();
            save_user_settings();
        }
        save_user_settings(true);
        vulkan_.wait_idle();
        if(logging_enabled_)editor_performance_log_.finish(editor_performance_context());
    }catch(const std::exception& error){
        save_user_settings(true);
        editor_performance_log_.error(error.what());
        if(logging_enabled_)editor_performance_log_.finish(editor_performance_context(),"editor_exception");
        throw;
    }
}
float EditorApp::left_panel_width() const noexcept {
    return workspace_layout().assets.width;
}

float EditorApp::right_panel_width() const noexcept {
    return workspace_layout().inspector.width;
}

uint32_t EditorApp::estimated_debug_draw_packets() const noexcept {
    uint64_t packets=renderer_.object_draw_run_count()+dynamic_scene_cpu_.draw_runs.size();
    if(document_&&!renderer_.cpu_vertices().empty())++packets;
    if(draw_water_)++packets;
    if(renderer_.shadow_index_count()!=0U)++packets;
    if(!dynamic_scene_cpu_.shadow_indices.empty())++packets;
    return static_cast<uint32_t>(std::min<uint64_t>(packets,std::numeric_limits<uint32_t>::max()));
}

void EditorApp::reset_retail_debug_counter_sample() noexcept {
    retail_debug_frame_accumulator_=0U;
    retail_debug_sample_frames_=0U;
    retail_debug_draw_packet_accumulator_=0U;
    retail_debug_sample_draw_packets_=0U;
}

void EditorApp::record_presented_frame(double now_seconds) noexcept {
    if(!measured_fps_initialized_){
        measured_fps_initialized_=true;
        measured_fps_window_start_seconds_=now_seconds;
        // The first presented frame defines the window origin. Count subsequent completed frame
        // intervals so the first 0.5 s sample is not biased high by one frame.
        measured_fps_presented_frames_=0U;
        return;
    }
    ++measured_fps_presented_frames_;
    const double elapsed=now_seconds-measured_fps_window_start_seconds_;
    if(elapsed<0.5)return;
    measured_present_fps_=elapsed>0.0
        ?static_cast<double>(measured_fps_presented_frames_)/elapsed:0.0;
    measured_present_frame_ms_=measured_present_fps_>0.0?1000.0/measured_present_fps_:0.0;
    std::snprintf(measured_fps_label_,sizeof(measured_fps_label_),"%.1f FPS",measured_present_fps_);
    measured_fps_window_start_seconds_=now_seconds;
    measured_fps_presented_frames_=0U;
}

void EditorApp::frame(){
    const auto performance_frame_started=EditorPerformanceLog::Clock::now();
    update_ui_scale();
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
    ++retail_debug_frame_accumulator_;
    retail_debug_draw_packet_accumulator_+=estimated_debug_draw_packets();
    if(retail_debug_frame_accumulator_>=20U){
        retail_debug_sample_frames_=retail_debug_frame_accumulator_;
        retail_debug_sample_draw_packets_=retail_debug_draw_packet_accumulator_;
        retail_debug_frame_accumulator_=0U;
        retail_debug_draw_packet_accumulator_=0U;
    }
    {auto performance_update=editor_performance_log_.measure(EditorPerfStage::Update);
    if(simulation_lab_active()) update_simulation_lab(presentation_time);
    else update_simulation(current_presentation_tick_);
    if(simulation_lab_active()){
        const auto runs=simulation_lab_runs();
        if(!runs.empty()){
            const std::size_t run_index=static_cast<std::size_t>(std::clamp(
                simulation_lab_selected_run_,0,static_cast<int>(runs.size())-1));
            current_building_animation_tick_=runs[run_index].tick;
            renderer_.set_presentation_tick(current_building_animation_tick_);
            renderer_.set_building_animation_tick(current_building_animation_tick_);
        }
    }
    if(simulation_static_replacement_dirty_){
        simulation_static_replacement_dirty_=false;
        rebuild_objects();
        // rebuild_objects() has already reconstructed the selected Lab scene from
        // this snapshot. The next ScenePrepare pass must not rebuild it again
        // unless the camera/viewport changes after the static rebuild.
        if(simulation_lab_active() && simulation_lab_snapshot_dirty_){
            simulation_lab_snapshot_dirty_=false;
            ++lab_snapshot_dirty_coalesced_;
            last_lab_scene_update_=std::chrono::steady_clock::now();
        }
    }
    }
    {auto performance_ui=editor_performance_log_.measure(EditorPerfStage::Ui);
    {auto scope=editor_performance_log_.measure(EditorPerfStage::UiMainMenu);draw_main_menu();}
    {auto scope=editor_performance_log_.measure(EditorPerfStage::UiRibbon);draw_workspace_ribbon();}
    draw_map_overview();
    if(show_left_panel_){auto scope=editor_performance_log_.measure(EditorPerfStage::UiLeftPanel);draw_left_panel();}
    if(show_right_panel_){auto scope=editor_performance_log_.measure(EditorPerfStage::UiRightPanel);draw_right_panel();}
    draw_retail_debug_overlay();
    draw_status_bar();draw_new_map_dialog();draw_load_map_dialog();draw_save_map_dialog();
    draw_campaign_window();
    if(tool_!=Tool::GenerateTerrain||simulation_mode_||simulation_lab_active()){
        if(generator_preview_)discard_terrain_generator_preview();
        generator_drawing_=false;generator_edit_path_=false;generator_outline_.clear();
    }
    draw_simulation_lab_run_switcher();
    {auto scope=editor_performance_log_.measure(EditorPerfStage::UiCanvasInteraction);update_canvas_interaction();}
    const auto overlay_canvas=workspace_layout().canvas;
    ImGui::GetBackgroundDrawList()->PushClipRect({overlay_canvas.x,overlay_canvas.y},
        {overlay_canvas.x+overlay_canvas.width,overlay_canvas.y+overlay_canvas.height},true);
    ImGui::GetForegroundDrawList()->PushClipRect({overlay_canvas.x,overlay_canvas.y},
        {overlay_canvas.x+overlay_canvas.width,overlay_canvas.y+overlay_canvas.height},true);
    draw_water_selection_overlay();draw_retail_field_diagnostics_overlay();draw_placement_markers();
    draw_campaign_exit_zones();draw_terrain_generator_overlay();
    {auto scope=editor_performance_log_.measure(EditorPerfStage::UiLabOverlay);draw_simulation_lab_overlay();}
    draw_transform_gizmo();draw_drag_drop_preview();draw_game_cursor();
    ImGui::GetForegroundDrawList()->PopClipRect();
    ImGui::GetBackgroundDrawList()->PopClipRect();ImGui::Render();
    }
    {auto performance_scene=editor_performance_log_.measure(EditorPerfStage::ScenePrepare);
    const bool enabled_timed_effect_animation=
        std::any_of(timed_effect_controllers_.begin(),timed_effect_controllers_.end(),
            [&](const TimedEffectControllerPreview& controller){
                if(controller.owner_is_resource_field)return animate_resource_fields_;
                if(simulation_mode_&&controller.runtime_class_id==13U){
                    const auto factory=simulation_factory_states_.find(controller.placement_index);
                    return animate_timed_effects_&&factory!=simulation_factory_states_.end()&&
                        factory->second.phase==thandor::SimulationClass13ProductionPhase::Building;
                }
                return thandor::timed_effect_preview_enabled(
                    controller.owner_is_building,controller.owner_has_authored_model_animation,
                    animate_timed_effects_,animate_buildings_,simulation_mode_||simulation_lab_active());
            });
    const bool dynamic_animation_active = has_factory_drive_markers_ ||
        (animate_buildings_ && !animated_building_placements_.empty()) ||
        (animate_units_ && !animated_unit_placements_.empty()) ||
        !continuous_radar_placements_.empty() ||
        (simulation_mode_ &&
         (!simulation_actor_placements_.empty() || !simulation_projectiles_.empty() ||
         !building_destruction_states_.empty() ||
          !simulation_produced_entities_.empty() || !simulation_linked_child_entities_.empty() ||
          !simulation_factory_states_.empty() || !simulation_airport_states_.empty())) ||
        std::any_of(movement_ground_effects_.begin(),movement_ground_effects_.end(),
            [&](const auto& effect){return !effect.ground_mark||show_movement_ground_marks_;}) ||
        fog_preview_mode_!=0 || enabled_timed_effect_animation ||
        (animate_timed_effects_ && !damage_effect_controllers_.empty());
    const bool dynamic_geometry_present = !dynamic_scene_cpu_.indices.empty() ||
        !dynamic_scene_cpu_.shading_emitters.empty();
    const auto canvas=workspace_layout().canvas;
    if(canvas.x!=last_dynamic_canvas_.x||canvas.y!=last_dynamic_canvas_.y||
       canvas.width!=last_dynamic_canvas_.width||canvas.height!=last_dynamic_canvas_.height){
        last_dynamic_canvas_=canvas;
        last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
        if(simulation_lab_active())simulation_lab_snapshot_dirty_=true;
    }
    const bool lab_snapshot_rebuild=simulation_lab_active()&&simulation_lab_snapshot_dirty_;
    const bool tick_driven_rebuild=dynamic_animation_active||
        (!simulation_lab_active()&&dynamic_geometry_present);
    // Simulation Lab publishes immutable worker snapshots. Its CPU model/ghost geometry only needs
    // rebuilding when a new snapshot arrives; tying an existing lab scene to every presentation
    // tick defeats the worker split and can starve ImGui/Vulkan on large multi-run experiments.
    // Other editor animations retain their ordinary 20 Hz rebuild cadence.
    const std::array<float,6> marker_camera{camera_.yaw,camera_.pitch,camera_.distance,
        camera_.target.x,camera_.target.y,camera_.target.z};
    const bool marker_camera_changed=marker_camera!=last_marker_camera_;
    last_marker_camera_=marker_camera;
    const auto scene_update_time=std::chrono::steady_clock::now();
    const bool dynamic_rebuild_due=simulation_lab_active()
        ?thandor::simulation_lab_scene_update_due(lab_snapshot_rebuild,marker_camera_changed,
            scene_update_time-last_lab_scene_update_)
        :((current_presentation_tick_!=last_dynamic_scene_tick_&&tick_driven_rebuild)||
            (marker_camera_changed&&(has_factory_drive_markers_||dynamic_geometry_present)));
    if(!simulation_static_replacement_dirty_&&dynamic_rebuild_due){
        const bool camera_only=simulation_lab_active()? (!lab_snapshot_rebuild&&
            last_dynamic_scene_tick_!=std::numeric_limits<uint32_t>::max()):
            current_presentation_tick_==last_dynamic_scene_tick_;
        simulation_lab_snapshot_dirty_=false;
        last_dynamic_scene_tick_=current_presentation_tick_;
        rebuild_dynamic_scene_cpu(camera_only);
        last_lab_scene_update_=scene_update_time;
    }
    }
    const auto performance_begin_started=EditorPerformanceLog::Clock::now();
    // Update only the frame slot whose fence has completed. Inactive Lab runs
    // never receive a separate atlas or framebuffer; selecting a run changes
    // the logical CPU scene, and this slot uploads changed pages lazily.
    auto frame=vulkan_.begin_frame();
    editor_performance_log_.record(EditorPerfStage::BeginFrame,std::chrono::duration<double,std::milli>(
        EditorPerformanceLog::Clock::now()-performance_begin_started).count());
    if(!frame){finish_editor_performance_frame(false,performance_frame_started);return;}
    // Only the chosen simulation scene is staged. GPU texture copies are
    // recorded asynchronously before this frame's render pass, and the
    // backing staging buffer is kept until this slot's next completed fence.
    renderer_.prepare_static_frame(frame->frame_index,frame->command_buffer);
    upload_dynamic_scene(frame->frame_index);
    {auto timer=editor_performance_log_.measure(EditorPerfStage::StaticSort);
    renderer_.update_depth_sorted_static_objects(frame->frame_index,camera_);}
    {auto timer=editor_performance_log_.measure(EditorPerfStage::SurfaceSort);
    renderer_.update_depth_sorted_surface(frame->frame_index,camera_,animate_surface_);}
    // Authored animation toggles and live simulation replacement are different static-copy
    // suppression domains. Simulation Lab previously forced the authored flags on globally,
    // discarding animated buildings/units for which no dynamic copy had been queued.
    const bool simulation_runtime_preview=simulation_mode_||simulation_lab_active();
    const bool hide_simulated_buildings=
        (simulation_runtime_preview&&animate_buildings_)||!building_destruction_states_.empty();
    const bool hide_simulated_units=simulation_runtime_preview&&animate_units_;
    renderer_.prepare_shadow_frame(frame->frame_index,frame->command_buffer,camera_,
                                   workspace_layout().canvas.aspect(),projected_object_shadows_);
    vulkan_.begin_render_pass(*frame);
    {auto timer=editor_performance_log_.measure(EditorPerfStage::RenderCommands);
    renderer_.render(frame->command_buffer,camera_,draw_water_,wireframe_,frame->frame_index,
                     animate_surface_,animate_buildings_||fog_preview_mode_!=0,
                     animate_units_||fog_preview_mode_!=0,
                     hide_simulated_buildings,hide_simulated_units,workspace_layout().canvas,
                     {ImGui::GetIO().DisplaySize.x,ImGui::GetIO().DisplaySize.y});
    }
    {auto timer=editor_performance_log_.measure(EditorPerfStage::ImGuiCommands);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),frame->command_buffer);}
    {auto timer=editor_performance_log_.measure(EditorPerfStage::EndFrame);
    vulkan_.end_frame(*frame);}
    // Count only frames that reached the present/end-frame boundary. This is a real wall-clock
    // presented-frame rate, not the retail debug routine's 20-call counter sample.
    record_presented_frame(glfwGetTime());
    finish_editor_performance_frame(true,performance_frame_started);
}

void EditorApp::draw_main_menu(){
    const auto header=workspace_layout().header;
    ImGui::SetNextWindowPos({header.x,header.y});
    ImGui::SetNextWindowSize({header.width,header.height});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2{ui_px(8),ui_px(12)});
    ImGui::Begin("##workspace-header",nullptr,ImGuiWindowFlags_NoDecoration|
        ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_MenuBar);
    if(!ImGui::BeginMenuBar()){ImGui::End();ImGui::PopStyleVar();return;}
    if(header.width>ui_px(1100.0F)){ImGui::TextColored({0.35F,0.71F,1.0F,1.0F},"THANDOR");
        ImGui::SameLine();ImGui::TextUnformatted("Map Editor");ImGui::SameLine(0,ui_px(20)); }
    if(ImGui::BeginMenu("File")){
        if(ImGui::MenuItem("New map","Ctrl+N"))show_new_dialog_=true;
        if(ImGui::BeginMenu("Open map")){
            if(ImGui::BeginMenu("Stock maps (PCK)")){
                for(std::size_t i=0;i<catalog_.map_groups().size();++i)
                    if(ImGui::MenuItem(catalog_.map_groups()[i].display_name.c_str(),nullptr,
                        current_stock_group_ && *current_stock_group_==i))load_map(i);
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("Custom maps")){
                // Avoid a dependency on newer Dear ImGui APIs; the explicit Refresh
                // option remains available after the initial directory discovery.
                if(custom_maps_.empty())refresh_custom_maps();
                if(ImGui::MenuItem("Refresh custom maps"))refresh_custom_maps();
                ImGui::Separator();
                if(custom_maps_.empty())ImGui::TextDisabled("No .thmap or .lev files in maps/");
                for(const auto& entry:custom_maps_){
                    if(ImGui::MenuItem(entry.label.c_str())){
                        if(!load_map_from_path(entry.path)){
                            // Surface errors in the file dialog instead of a short
                            // status-bar line that vanishes after menu selection.
                            std::snprintf(load_map_path_,sizeof(load_map_path_),"%s",
                                entry.path.string().c_str());
                            show_load_dialog_=true;
                        }
                        break; // A reload replaces the document, not the file-list backing storage.
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if(ImGui::MenuItem("Load map or project...","Ctrl+O")){
            load_map_error_.clear();
            show_load_dialog_=true;
        }
        if(ImGui::MenuItem("Save current stock map in place","Ctrl+S",false,current_stock_group_.has_value()))
            save_current_map();
        if(ImGui::MenuItem("Save full map project",current_stock_group_.has_value()?nullptr:"Ctrl+S")){
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
        if(ImGui::IsItemHovered())show_item_tooltip("Writes levelNN.pck and updates PATCH00.PCK in the game folder. The Export panel controls backups for both archives.");
        if(ImGui::MenuItem("Update PATCH00.PCK overrides"))update_patch00_overrides();
        if(ImGui::MenuItem("Quit"))glfwSetWindowShouldClose(window_,GLFW_TRUE);
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Campaign")){
        if(ImGui::MenuItem("Campaign workspace..."))show_campaign_window_=true;
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Edit")){
        if(ImGui::MenuItem("Generate terrain..."))choose_workspace_tool(Tool::GenerateTerrain,1);
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
        if(ImGui::MenuItem("Animate Xenite / Tritium resource fields",nullptr,&animate_resource_fields_))
            last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
        if(ImGui::MenuItem("Show moving-unit ground marks",nullptr,&show_movement_ground_marks_))
            last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
        ImGui::Separator();
        const std::string simulation_label="Simulation mode (20 Hz × "+
            std::to_string(animation_preview_multiplier_)+")";
        if(ImGui::MenuItem(simulation_label.c_str(),nullptr,&simulation_mode_,!simulation_lab_active())){
            if(simulation_mode_){animate_buildings_=true;animate_units_=true;
                last_simulation_tick_=current_presentation_tick_;}
            else reset_simulation(true);
            last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
        }
        if(ImGui::MenuItem("Sound playback",nullptr,&sound_playback_enabled_)){
            if(!sound_playback_enabled_)stop_all_runtime_looping_sounds();
            else looping_sound_start_failures_.clear();
        }
        ImGui::MenuItem("Simulation sound follows map focus",nullptr,&simulation_sound_at_map_focus_);
        int sound_volume_percent=static_cast<int>(std::lround(
            static_cast<double>(sound_effects_gain_q15_)*100.0/32768.0));
        ImGui::SetNextItemWidth(ui_px(150.0F));
        if(ImGui::SliderInt("Sound volume",&sound_volume_percent,0,100,"%d%%")){
            sound_effects_gain_q15_=static_cast<uint32_t>(std::clamp(
                static_cast<int>(std::lround(sound_volume_percent*32768.0/100.0)),0,32768));
            if(sound_effects_gain_q15_==0U)stop_all_runtime_looping_sounds();
            else looping_sound_start_failures_.clear();
        }

        if(!sound_playback_status_.empty())ImGui::TextDisabled("%s",sound_playback_status_.c_str());
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
                    const uint32_t family=graphics_family_for_faction(static_cast<uint32_t>(faction));
                    const std::string label=faction_single_player_label(
                        static_cast<uint32_t>(faction),family,false);
                    ImGui::PushStyleColor(ImGuiCol_Text,graphics_family_ui_text_color(family));
                    const bool picked=ImGui::MenuItem(label.c_str(),nullptr,fog_preview_faction_==faction);
                    ImGui::PopStyleColor();
                    if(picked){
                        fog_preview_faction_=faction;fog_preview_mode_=1;fog_changed=true;
                    }
                }
                ImGui::EndMenu();
            }
            fog_changed|=ImGui::MenuItem("Whole map already explored",nullptr,&fog_preview_all_explored_);
            ImGui::MenuItem("Show nominal observer-radius circle",nullptr,&fog_preview_show_nominal_radius_);

            if(fog_changed){
                fog_preview_occupancy_history_.clear();
                last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
                // A paused Lab has no new worker generation. Still refresh its fog preview
                // when the user changes faction or visibility mode.
                if(simulation_lab_active())simulation_lab_snapshot_dirty_=true;
                rebuild_objects();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if(ImGui::BeginMenu("Panels")){
            ImGui::MenuItem("Assets",nullptr,&show_left_panel_);
            ImGui::MenuItem("Compact ribbon",nullptr,&compact_ribbon_);
            ImGui::MenuItem("Expanded map overview",nullptr,&overview_expanded_);
            ImGui::MenuItem("Inspector",nullptr,&show_right_panel_);
            if(ImGui::MenuItem("Show both panels")){show_left_panel_=true;show_right_panel_=true;}
            if(ImGui::MenuItem("Hide both panels")){show_left_panel_=false;show_right_panel_=false;}
            ImGui::EndMenu();
        }
        ImGui::MenuItem("Wireframe",nullptr,&wireframe_);
        ImGui::MenuItem("Game cursor",nullptr,&game_cursor_enabled_);
        if(ImGui::MenuItem("Center camera")){
            center_camera();
        }
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Settings")){
        bool logging=logging_enabled_;
        if(ImGui::MenuItem("Create diagnostic logs",nullptr,&logging))set_logging_enabled(logging);
        if(ImGui::IsItemHovered())show_item_tooltip("Enable or stop editor performance and Simulation Lab log files. Saved for the next session.");
        if(logging_enabled_ && editor_performance_log_.capped())ImGui::TextDisabled("Log size limit reached");
        if(ImGui::BeginMenu("UI scale")){
            constexpr int scales[]={0,100,125,150,175,200,250,300};
            constexpr const char* labels[]={"Automatic","100%","125%","150%","175%","200%","250%","300%"};
            for(std::size_t i=0;i<std::size(scales);++i)
                if(ImGui::MenuItem(labels[i],nullptr,ui_scale_percent_==scales[i]))ui_scale_percent_=scales[i];
            ImGui::EndMenu();
        }
        ImGui::MenuItem("Show FPS",nullptr,&show_measured_fps_counter_);
        if(ImGui::BeginMenu("Diagnostics")){
            ImGui::MenuItem("Runtime counters and coordinates",nullptr,&show_retail_debug_overlay_);
            ImGui::MenuItem("Render counters",nullptr,&debug_show_render_counters_);
            ImGui::MenuItem("World vectors",nullptr,&debug_show_world_vectors_);
            ImGui::MenuItem("Cursor / selection coordinates",nullptr,&debug_show_cursor_coordinates_);
            ImGui::MenuItem("Memory / resource state",nullptr,&debug_show_memory_resources_);
            ImGui::MenuItem("Simulation clock",nullptr,&debug_show_simulation_clock_);
            if(ImGui::MenuItem("Reset counter sample"))reset_retail_debug_counter_sample();
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    ImGui::SameLine(0,ui_px(18));
    if(header.width>ui_px(1250.0F)){
        ImGui::SetNextItemWidth(std::min(ui_px(260.0F),header.width*0.18F));
        if(ImGui::BeginCombo("##workspace-map",(document_->display_name()+(document_->dirty()?" *":"")).c_str())){
            for(std::size_t i=0;i<catalog_.map_groups().size();++i)
                if(ImGui::Selectable(catalog_.map_groups()[i].display_name.c_str(),current_stock_group_==i))load_map(i);
            if(ImGui::Selectable("Open custom map..."))show_load_dialog_=true;
            ImGui::EndCombo();
        }
        ImGui::SameLine(0,ui_px(18));
    }
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4{0.10F,0.36F,0.25F,1});
    if(ImGui::Button("Simulate")){show_right_panel_=true;requested_inspector_tab_=3;}
    if(ImGui::IsItemHovered())show_item_tooltip("Open Simulation Lab settings and start controls.");
    ImGui::PopStyleColor();ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4{0.12F,0.35F,0.61F,1});
    if(ImGui::Button("Game export")){show_right_panel_=true;requested_inspector_tab_=5;}
    ImGui::PopStyleColor();ImGui::SameLine();
    if(ImGui::Button("Map settings")){show_right_panel_=true;requested_inspector_tab_=4;}
    // Persistent measured presented-FPS label, right-aligned inside the top navigation bar.
    // This uses our wall-clock presented-frame sample, not the ImGui-smoothed UI rate.
    if(show_measured_fps_counter_){
        const float right_x=header.width-ui_px(22.0F)-ImGui::CalcTextSize(measured_fps_label_).x;
        const float after_buttons=ImGui::GetCursorPosX()+ui_px(18.0F);
        if(right_x>after_buttons){
            ImGui::SameLine(right_x);
            ImGui::TextColored({0.50F,0.84F,0.69F,1.0F},"%s",measured_fps_label_);
        }
    }
    ImGui::EndMenuBar();ImGui::End();ImGui::PopStyleVar();
}

void EditorApp::draw_retail_debug_overlay(){
    if(!show_retail_debug_overlay_)return;
    ImGui::SetNextWindowSize({ui_px(430.0F),0.0F},ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("Retail debug overlay##thandor-debug",&show_retail_debug_overlay_,
                     ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::End();return;
    }
    ImGui::TextDisabled("game.exe: FrontendDebugOverlay_RefreshCountersAndWorldCoordinates @ 0x00546190");
    ImGui::TextWrapped("The retail routine binds fourteen live UTF-16 slots. This window exposes the same proven diagnostic categories using editor runtime values; retail-only allocator state is labeled rather than guessed.");

    if(debug_show_render_counters_&&ImGui::CollapsingHeader("Render counters",ImGuiTreeNodeFlags_DefaultOpen)){
        const uint32_t sample_frames=retail_debug_sample_frames_!=0U?retail_debug_sample_frames_:retail_debug_frame_accumulator_;
        const uint64_t sample_packets=retail_debug_sample_frames_!=0U?retail_debug_sample_draw_packets_:retail_debug_draw_packet_accumulator_;
        const double packets_per_call=sample_frames==0U?0.0:static_cast<double>(sample_packets)/sample_frames;
        ImGui::Text("Slot 00 · frames in 20-call sample: %u",sample_frames);
        ImGui::Text("Slot 01 · editor draw-packet estimate / call: %.2f",packets_per_call);
        ImGui::Text("Slot 02 · retail texture-bind state changes: unavailable in Vulkan rewrite");
        ImGui::Text("Slot 03 · retail texture-device reloads: unavailable in Vulkan rewrite");
        if(measured_present_fps_>0.0)
            ImGui::Text("Editor measured present rate: %.1f FPS (%.2f ms)",
                        measured_present_fps_,measured_present_frame_ms_);
        else ImGui::TextDisabled("Editor measured present rate: sampling");
        ImGui::Text("Editor resource state · object texture layers: %u",renderer_.object_texture_layer_count());
        ImGui::Text("Editor resource state · terrain/surface layers: %zu / %zu",loaded_material_count_,loaded_surface_material_count_);
        ImGui::TextDisabled("Retail resets frame/draw/texture-state/reload counters every twenty calls.");
    }

    if(debug_show_world_vectors_&&ImGui::CollapsingHeader("World vectors",ImGuiTreeNodeFlags_DefaultOpen)){
        const Vec3f target=camera_.target;
        const Vec3f eye=camera_.eye();
        const auto q12=[](float value){return static_cast<int32_t>(std::lround(static_cast<double>(value)*4096.0));};
        ImGui::Text("Slots 04–06 · editor equivalent / camera target Q12: %d, %d, %d",q12(target.x),q12(target.y),q12(target.z));
        ImGui::Text("Slots 07–09 · editor equivalent / camera eye Q12:    %d, %d, %d",q12(eye.x),q12(eye.y),q12(eye.z));
    }

    if(debug_show_cursor_coordinates_&&ImGui::CollapsingHeader("Cursor / selection coordinates",ImGuiTreeNodeFlags_DefaultOpen)){
        if(hovered_terrain_point_){
            const auto q12=[](float value){return static_cast<int32_t>(std::lround(static_cast<double>(value)*4096.0));};
            ImGui::Text("Slots 10–11 · terrain cursor Q12: %d, %d",q12(hovered_terrain_point_->x),q12(hovered_terrain_point_->y));
        }else{
            const ImVec2 mouse=ImGui::GetIO().MousePos;
            ImGui::Text("Slots 10–11 · pointer pixels: %.0f, %.0f",mouse.x,mouse.y);
            ImGui::TextDisabled("No terrain hit: retail-equivalent world cursor coordinates are unavailable.");
        }
        if(selected_placement_)ImGui::Text("Selected placement index: %zu",*selected_placement_);
        else ImGui::TextDisabled("Selected placement: none");
    }

    if(debug_show_memory_resources_&&ImGui::CollapsingHeader("Memory / resource state",ImGuiTreeNodeFlags_DefaultOpen)){
        const uint64_t dynamic_bytes=static_cast<uint64_t>(dynamic_scene_cpu_.vertices.size())*sizeof(ObjectVertex)+
            static_cast<uint64_t>(dynamic_scene_cpu_.indices.size())*sizeof(uint32_t)+
            static_cast<uint64_t>(dynamic_scene_cpu_.shadow_vertices.size())*sizeof(ObjectVertex)+
            static_cast<uint64_t>(dynamic_scene_cpu_.shadow_indices.size())*sizeof(uint32_t);
        ImGui::Text("Slot 12 · retail free-arena bytes: unavailable in Vulkan rewrite");
        ImGui::Text("Editor dynamic-scene CPU payload: %llu bytes",static_cast<unsigned long long>(dynamic_bytes));
        ImGui::Text("Object textures: %zu · sprites cached: %zu · SAM cached: %zu",object_texture_layer_count_,sprite_cache_.size(),sam_pcm_cache_.size());
        ImGui::Text("Active looping sounds: %zu",active_looping_sounds_.size());
    }

    if(debug_show_simulation_clock_&&ImGui::CollapsingHeader("Simulation clock",ImGuiTreeNodeFlags_DefaultOpen)){
        uint32_t clock_tick=current_presentation_tick_;
        if(simulation_lab_active()){
            const auto runs=simulation_lab_runs();
            if(!runs.empty())clock_tick=runs[static_cast<std::size_t>(std::clamp(
                simulation_lab_selected_run_,0,static_cast<int>(runs.size())-1))].tick;
        }
        const uint64_t seconds=static_cast<uint64_t>(clock_tick)/20ULL;
        const uint64_t hours=seconds/3600ULL;
        const uint64_t minutes=(seconds/60ULL)%60ULL;
        const uint64_t secs=seconds%60ULL;
        ImGui::Text("Slot 13 · retail-style elapsed clock: %02llu:%02llu:%02llu",
                    static_cast<unsigned long long>(hours),static_cast<unsigned long long>(minutes),
                    static_cast<unsigned long long>(secs));
        ImGui::Text("%s tick: %u",simulation_lab_active()?"Selected Lab run":"Presentation",clock_tick);
        ImGui::Text("Preview multiplier: %ux",animation_preview_multiplier_);
    }
    ImGui::End();
}

void EditorApp::draw_left_panel(){
    const auto panel=workspace_layout().assets;
    ImGui::SetNextWindowPos({panel.x,panel.y});
    ImGui::SetNextWindowSize({panel.width,panel.height});
    ImGui::Begin("Assets##workspace-assets",nullptr,ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings);
    if(ImGui::SmallButton("Collapse assets"))show_left_panel_=false;
    ImGui::Separator();
    char filter[128]{};
    std::snprintf(filter,sizeof(filter),"%s",asset_filter_.c_str());
    ImGui::SetNextItemWidth(-1.0F);
    if(ImGui::InputTextWithHint("##filter","Search assets and camera starts",filter,sizeof(filter)))asset_filter_=filter;

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

    if(ImGui::BeginTabBar("asset-tabs",ImGuiTabBarFlags_FittingPolicyScroll)){
        if(ImGui::BeginTabItem("Assets")){
            draw_asset_categories(placement_ids);
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Map items")){
            draw_faction_camera_start_cards();
            draw_simulation_target_card();
            ImGui::SeparatorText("Placeable map records");
            draw_placement_presets(placement_ids,thandor::PlacementBrowserKind::General);
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("World")){
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
        if(ImGui::BeginTabItem("Raw files")){
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
        if(ImGui::IsItemHovered())show_item_tooltip("Archive: "+asset.archive_name+
            "\nUnpacked: "+std::to_string(asset.unpacked_size)+" bytes");
        ImGui::PopID();
    }
}

uint32_t EditorApp::graphics_family_for_faction(uint32_t faction_index) const{
    faction_index=std::min(faction_index,7U);
    if(faction_index==0U||!document_)return faction_index;
    return document_->level().faction_graphics_family_selector(faction_index);
}

uint32_t EditorApp::runtime_faction_for_placement(std::size_t placement_index,
                                                   uint32_t authored_faction) const noexcept{
    if(const auto live=simulation_runtime_factions_.find(placement_index);
       live!=simulation_runtime_factions_.end())
        return std::min(live->second,7U);
    if(simulation_lab_active()){
        const auto runs=simulation_lab_runs();
        if(!runs.empty()){
            const auto run_index=static_cast<std::size_t>(std::clamp(
                simulation_lab_selected_run_,0,static_cast<int>(runs.size())-1));
            authored_faction=std::min(authored_faction,7U);
            return std::min(runs[run_index].faction_merge_redirect[authored_faction],7U);
        }
    }
    return std::min(authored_faction,7U);
}

void EditorApp::draw_global_faction_selector(const char* widget_id){
    ImGui::PushID(widget_id);
    const uint32_t selected_family=graphics_family_for_faction(selected_faction_);
    const std::string preview_label=faction_single_player_label(selected_faction_,selected_family);
    ImGui::SetNextItemWidth(-1.0F);
    if(selected_faction_!=0U)ImGui::PushStyleColor(ImGuiCol_Text,graphics_family_ui_text_color(selected_family));
    const bool faction_combo_open=ImGui::BeginCombo("Placement faction",preview_label.c_str());
    if(selected_faction_!=0U)ImGui::PopStyleColor();
    if(faction_combo_open){
        for(uint32_t faction=0U;faction<=7U;++faction){
            const uint32_t family=graphics_family_for_faction(faction);
            const std::string label=faction_single_player_label(faction,family);
            const bool selected=faction==selected_faction_;
            if(faction!=0U)ImGui::PushStyleColor(ImGuiCol_Text,graphics_family_ui_text_color(family));
            const bool picked=ImGui::Selectable(label.c_str(),selected);
            if(faction!=0U)ImGui::PopStyleColor();
            if(picked){
                selected_faction_=faction;
                asset_preview_cache_.clear();
                asset_preview_texture_cache_.clear();
            }
            if(selected)ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    const uint32_t family=graphics_family_for_faction(selected_faction_);
    if(selected_faction_!=0U){
        ImGui::TextColored(graphics_family_ui_text_color(family),"%s",
            thandor::graphics_family_single_player_faction_name(family));
        ImGui::SameLine();

    }else{
        ImGui::TextDisabled("Neutral / map-owned source colour");
    }

    if(family!=8U && document_){
        bool has_mission_golrathen=false;
        for(uint32_t slot=1U;slot<=7U;++slot)
            has_mission_golrathen|=graphics_family_for_faction(slot)==8U;
        if(!has_mission_golrathen)
            ImGui::TextDisabled("For Golrathen (army8), use Players > Mission-only faction setup.");
    }
    if(!local_player_texture_override_status_.empty()){
        if(local_player_texture_override_.has_value()){

            ImGui::SetNextItemWidth(-1.0F);
            const uint32_t local_family=graphics_family_for_faction(local_player_runtime_faction_);
            const std::string local_faction_label=faction_single_player_label(
                local_player_runtime_faction_,local_family,false);
            ImGui::PushStyleColor(ImGuiCol_Text,graphics_family_ui_text_color(local_family));
            const bool local_combo_open=ImGui::BeginCombo("Local player PCX faction",local_faction_label.c_str());
            ImGui::PopStyleColor();
            if(local_combo_open){
                for(uint32_t faction_index=1U;faction_index<=7U;++faction_index){
                    const uint32_t faction_family=graphics_family_for_faction(faction_index);
                    const bool selected=faction_index==local_player_runtime_faction_;
                    const std::string label=faction_single_player_label(faction_index,faction_family,false);
                    ImGui::PushStyleColor(ImGuiCol_Text,graphics_family_ui_text_color(faction_family));
                    const bool picked=ImGui::Selectable(label.c_str(),selected);
                    ImGui::PopStyleColor();
                    if(picked){
                        local_player_runtime_faction_=faction_index;
                        asset_preview_cache_.clear();
                        asset_preview_texture_cache_.clear();
                        rebuild_mesh();
                    }
                    if(selected)ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }else if(selected_faction_==1U){
            ImGui::TextDisabled("%s",local_player_texture_override_status_.c_str());
        }
    }
    if(selected_faction_!=0U&&(family<1U||family>8U))
        ImGui::TextColored({1.0F,0.45F,0.30F,1.0F},
            "LEV resolves this faction to army%u; stock editor resources normally use families 1..8.",family);
    draw_faction_research_mask_editor(selected_faction_,"research-mask");
    ImGui::PopID();
}

void EditorApp::draw_faction_research_mask_editor(uint32_t faction_index,const char* widget_id){
    if(!document_)return;
    faction_index=std::min(faction_index,7U);
    auto& mask=document_->faction_technology_mask(faction_index);
    ImGui::PushID(widget_id);
    if(faction_index!=0U){
        const uint32_t family=graphics_family_for_faction(faction_index);
        ImGui::TextColored(graphics_family_ui_text_color(family),"%s · Faction %u",
            thandor::graphics_family_single_player_faction_name(family),faction_index);
    }
    if(ImGui::Button("Clear research")){
        checkpoint();mask.fill(0U);document_->mark_dirty();
        faction_technology_hierarchy_cache_.clear();faction_technology_weapon_profile_cache_.clear();asset_preview_cache_.clear();
        reset_simulation(true);rebuild_objects();
        status_="Cleared faction "+std::to_string(faction_index)+" 256-bit research preview mask.";
    }
    ImGui::SameLine();
    if(ImGui::Button("Unlock all")){
        checkpoint();mask.fill(0xFFFFFFFFU);document_->mark_dirty();
        faction_technology_hierarchy_cache_.clear();faction_technology_weapon_profile_cache_.clear();asset_preview_cache_.clear();
        reset_simulation(true);rebuild_objects();
        status_="Enabled all 256 faction research bits for runtime model preview.";
    }
    if(ImGui::TreeNode("Research state (all technologies)")){

        ImGui::BeginChild("technology-mask-scroll",{0.0F,ui_px(190.0F)},true);
        for(const auto& technology:catalog_.technologies()){
            const uint32_t id=technology.technology_id;
            if(id>=256U)continue;
            bool bit=thandor::faction_technology_unlocked(mask,id);
            std::string label="TEC "+std::to_string(id);
            if(!technology.title.empty())label+=" · "+technology.title;
            ImGui::PushID(static_cast<int>(id));
            if(ImGui::Checkbox(label.c_str(),&bit)){
                checkpoint();
                thandor::set_faction_technology_unlocked(mask,id,bit);
                document_->mark_dirty();
                faction_technology_hierarchy_cache_.clear();faction_technology_weapon_profile_cache_.clear();asset_preview_cache_.clear();
                reset_simulation(true);rebuild_objects();
                status_=std::string(bit?"Unlocked ":"Locked ")+"TEC "+std::to_string(id)+
                    " for faction "+std::to_string(faction_index)+" preview.";
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::TreePop();
    }
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

    const float card_width=ui_px(210.0F);
    const float card_height=ui_px(184.0F);
    ImGui::BeginChild("camera-start-card-row",{0.0F,card_height+ImGui::GetStyle().ScrollbarSize+ui_px(8.0F)},
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
        const uint32_t graphics_family=graphics_family_for_faction(faction);
        const ImU32 heading_color=graphics_family_ui_swatch(graphics_family);
        ImGui::TextColored(graphics_family_ui_text_color(graphics_family),
            "%s · Faction %u — %s starting camera",
            thandor::graphics_family_single_player_faction_name(graphics_family),faction,
            human?"Player":"AI");

        try{
            const auto camera_start=document_->level().faction_camera_start(faction);

            const ImVec2 preview_size{card_width-ui_px(18.0F),ui_px(46.0F)};
            ImGui::InvisibleButton("camera-preview",preview_size);
            const ImVec2 preview_min=ImGui::GetItemRectMin();
            const ImVec2 preview_max=ImGui::GetItemRectMax();
            const ImVec2 center{(preview_min.x+preview_max.x)*0.5F,(preview_min.y+preview_max.y)*0.5F};
            draw->AddRectFilled(preview_min,preview_max,IM_COL32(28,33,39,255),3.0F);
            draw->AddCircleFilled({center.x-24.0F,center.y},8.0F,heading_color,16);
            draw->AddLine({center.x-14.0F,center.y},{center.x+28.0F,center.y},heading_color,3.0F);
            draw->AddTriangleFilled({center.x+34.0F,center.y},{center.x+20.0F,center.y-8.0F},
                {center.x+20.0F,center.y+8.0F},heading_color);
            if(ImGui::IsItemClicked(ImGuiMouseButton_Left))selected_camera_faction_=static_cast<int>(faction);
            ImGui::Text("%.1f, %.1f, %.1f",camera_start.position_x(),camera_start.position_y(),
                camera_start.position_z());
            ImGui::TextDisabled("heading %.1f deg · pitch %.1f deg",
                static_cast<float>(camera_start.heading_turn16)*(360.0F/65536.0F),
                static_cast<float>(camera_start.pitch_turn16)*(360.0F/65536.0F));
            if(ImGui::Button("View stored",{(card_width-ui_px(24.0F))*0.5F,0.0F})){
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
    ImGui::BeginChild("simulation-target-card",{0.0F,ui_px(154.0F)},true,
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextUnformatted("Target spot (editor-only)");
    ImGui::TextWrapped("Place a global test target here, or select an actor and use the normal viewport command: right-click terrain to move/aim, or right-click a different-faction placement to attack it. Right-drag remains camera pan.");
    if(simulation_target_point_){
        ImGui::Text("World: %.2f, %.2f, %.2f",simulation_target_point_->x,
            simulation_target_point_->y,simulation_target_point_->z);
    }else ImGui::TextDisabled("No target spot placed.");
    if(ImGui::Button("Place / move target",{ui_px(155.0F),0.0F})){
        tool_=Tool::SimulationTarget;simulation_mode_=true;animate_buildings_=true;animate_units_=true;
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
    if(ImGui::Checkbox("Simulation mode (20 Hz)",&simulation_mode_)&&simulation_mode_){
        animate_buildings_=true;animate_units_=true;
    }
    ImGui::EndChild();
}

void EditorApp::draw_placement_presets(std::span<const uint32_t> registry_ids,
                                       std::optional<thandor::PlacementBrowserKind> browser_kind){
    std::vector<const thandor::ArmyAssetPreset*> visible;
    visible.reserve(registry_ids.size());
    std::unordered_set<uint32_t> emitted_canonical_ids;
    for(const auto registry_id:registry_ids){
        const auto* preset=catalog_.army_preset(registry_id);
        if(preset==nullptr||(browser_kind&&preset->browser_kind!=*browser_kind)||!preset->browser_visible)continue;
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
    const int columns=asset_card_column_count(available,ui_px(kAssetCardWidth),spacing);
    const float card_width=std::min(ui_px(kAssetCardWidth),
        (available-spacing*static_cast<float>(columns-1))/static_cast<float>(columns));
    const int rows=static_cast<int>((visible.size()+static_cast<std::size_t>(columns)-1U)/
                                    static_cast<std::size_t>(columns));
    ImGuiListClipper clipper;
    clipper.Begin(rows,ui_px(kAssetCardHeight)+ImGui::GetStyle().ItemSpacing.y);
    while(clipper.Step()){
        for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row){
            for(int column=0;column<columns;++column){
                const std::size_t index=static_cast<std::size_t>(row*columns+column);
                if(index>=visible.size())break;
                if(column>0)ImGui::SameLine();
                const bool available = document_ != nullptr &&
                    catalog_.placement_available_in_level(*visible[index], document_->level());
                draw_placement_card(*visible[index],card_width,ui_px(kAssetCardHeight),available);
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

    const float preview_height=ui_px(132.0F);
    ImGui::InvisibleButton("asset-preview",{card_width-ui_px(12.0F),preview_height});
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
    draw->AddCircleFilled({(preview_min.x+preview_max.x)*0.5F,preview_max.y-ui_px(13.0F)},
        std::max(ui_px(16.0F),(preview_max.x-preview_min.x)*0.18F),IM_COL32(0,0,0,72),32);
    try{
        const auto& mesh=asset_preview(preset);
        if(!mesh.raster_argb.empty()&&mesh.raster_width>0U&&mesh.raster_height>0U){
            const float scale_x=(preview_max.x-preview_min.x)/static_cast<float>(mesh.raster_width);
            const float scale_y=(preview_max.y-preview_min.y-ui_px(6.0F))/static_cast<float>(mesh.raster_height);
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
            draw->AddCircleFilled(center,ui_px(28.0F),IM_COL32(88,112,140,220),24);
            draw->AddText({center.x-ui_px(18.0F),center.y-ui_px(7.0F)},IM_COL32_WHITE,"Asset");
        }
    }catch(const std::exception&){
        const ImVec2 center{(preview_min.x+preview_max.x)*0.5F,(preview_min.y+preview_max.y)*0.5F};
        draw->AddCircle(center,ui_px(26.0F),IM_COL32(220,100,82,255),24,2.0F);
        draw->AddText({center.x-ui_px(4.0F),center.y-ui_px(8.0F)},IM_COL32(255,180,160,255),"!");
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
        draw->AddText({preview_min.x+ui_px(8.0F),preview_max.y-ui_px(24.0F)},IM_COL32(255,155,145,255),
            "Unavailable on this map");
    }

    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+card_width-ui_px(14.0F));
    ImGui::TextUnformatted(preset.display_name.c_str());
    ImGui::PopTextWrapPos();

    if(!preset.technology_summary.empty())
        ImGui::TextDisabled("%s",preset.technology_summary.c_str());
    else ImGui::TextDisabled("%s",preset.group_hint.c_str());
    if(preview_hovered){
        const bool technical_details=ImGui::GetIO().KeyShift;
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
            ?"ready in this LEV"
            :catalog_.placement_compatibility_diagnostic(preset,document_->level());

        const float tooltip_width=std::clamp(ImGui::GetIO().DisplaySize.x*0.60F,ui_px(620.0F),ui_px(860.0F));
        ImGui::SetNextWindowSizeConstraints(ImVec2(tooltip_width,0.0F),ImVec2(tooltip_width,ui_px(10000.0F)));
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(0.86F,0.93F,0.96F,1.0F),"%s",preset.display_name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s · ARM %u · class %u",thandor::placement_browser_kind_name(preset.browser_kind),
            preset.registry_id,preset.runtime_model_class_id);
        if(available_in_current_level)
            ImGui::TextDisabled("Ready to place · Shift = technical details");
        else
            ImGui::TextColored(ImVec4(1.0F,0.62F,0.56F,1.0F),"Not placeable here · %s",level_diagnostic.c_str());
        ImGui::Separator();

        if(ImGui::BeginTable("##asset_tooltip_columns",2,
            ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_NoSavedSettings)){
            ImGui::TableSetupColumn("Gameplay",ImGuiTableColumnFlags_WidthStretch,1.0F);
            ImGui::TableSetupColumn("Research / model",ImGuiTableColumnFlags_WidthStretch,1.0F);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x);
            ImGui::SeparatorText("Retail build card");
            ImGui::Text("HP %lld · Demand %llu MWh · Xenite %u T · Time %u T",
                static_cast<long long>(preset.retail_armour),
                static_cast<unsigned long long>(preset.retail_energy_demand_q4 >> 4U),
                preset.retail_xenite_cost_q4 >> 4U,preset.retail_build_time_q5 >> 5U);
            if(preset.retail_energy_generation_capacity_q4>0U)
                ImGui::Text("Energy generation +%llu MWh",
                    static_cast<unsigned long long>(preset.retail_energy_generation_capacity_q4 >> 4U));
            if(preset.fog_uncover_radius_q12>0)
                ImGui::Text("Fog %.3f · scan %u",thandor::q12_to_float(preset.fog_uncover_radius_q12),
                    preset.fog_uncover_scan_steps);

            if(preset.has_placement_occupancy_class||preset.has_resource_storage_capacity_contribution){
                ImGui::SeparatorText("Resources");
                if(preset.has_placement_occupancy_class){
                    const char* field_name=preset.placement_occupancy_class==0U?"Xenite":
                        (preset.placement_occupancy_class==1U?"Tritium":"resource field");
                    ImGui::Text("Extractor · %s · weight %u",field_name,preset.resource_extraction_weight_u8);
                }
                if(preset.has_resource_storage_capacity_contribution){
                    const char* resource=preset.resource_storage_selector==0U?"Xenite":
                        (preset.resource_storage_selector==1U?"Tritium":"unknown selector");
                    ImGui::Text("Storage capacity +%.3f %s",
                        static_cast<double>(preset.resource_storage_capacity_contribution_q4)/16.0,resource);
                }
            }

            if(preset.runtime_model_class_id==2U||preset.runtime_model_class_id==3U||
               preset.runtime_model_class_id==18U){
                ImGui::SeparatorText("Movement / environment");
                ImGui::Text("Water damage: threshold %.3f · multiplier %d",
                    thandor::q12_to_float(preset.water_damage_threshold_q12),preset.water_damage_multiplier);
                if(preset.runtime_model_class_id==2U||preset.runtime_model_class_id==18U)
                    ImGui::Text("Heading gate: near 0x%04X · far 0x%04X · blend %.3f",
                        preset.ground_near_heading_error_limit_turn16,
                        preset.ground_far_heading_error_limit_turn16,
                        thandor::q12_to_float(preset.ground_heading_interpolation_distance_q12));
                if(preset.runtime_model_class_id==3U)
                    ImGui::Text("Walker contact: lift %.3f · steer 0x%04X · footstep %u",
                        thandor::q12_to_float(preset.articulated_contact_lift_q12),
                        preset.articulated_contact_steering_limit_turn16,
                        preset.articulated_contact_sound_asset_id);
            }
            if(preset.runtime_model_class_id==23U){
                ImGui::SeparatorText("Vertical deployment");
                ImGui::Text("Step %.3f/tick · travel %.3f",
                    thandor::q12_to_float(preset.vertical_deployment_step_q12_per_tick),
                    thandor::q12_to_float(preset.vertical_deployment_travel_limit_q12));
            }

            ImGui::SeparatorText("Weapons");
            std::unordered_set<uint32_t> displayed_damage_shots;
            std::size_t displayed_weapon_count=0U;
            for(const auto& weapon:preset.simulation_weapon_profiles){
                if(!displayed_damage_shots.insert(weapon.shot_definition_id).second)continue;
                const auto* shot=catalog_.shot_definition(weapon.shot_definition_id);
                if(!shot)continue;
                if(displayed_weapon_count>=4U)continue;
                ++displayed_weapon_count;
                const auto [damage_min,damage_max]=std::minmax_element(
                    shot->target_impact_damage_q12.begin(),shot->target_impact_damage_q12.end());
                ImGui::Text("SHT %u · dmg %.3f..%.3f · range %.3f · reload %ut",shot->definition_id,
                    thandor::q12_to_float(*damage_min),thandor::q12_to_float(*damage_max),
                    thandor::shot_selection_range_world(*shot),weapon.reload_ticks);
                if(technical_details){
                    std::string damage_classes;
                    for(uint32_t impact_class=0U;impact_class<shot->target_impact_damage_q12.size();++impact_class){
                        if(!damage_classes.empty())damage_classes+=" · ";
                        damage_classes+=std::to_string(impact_class)+":"+
                            std::to_string(shot->target_impact_damage_q12[impact_class]);
                    }
                    ImGui::TextDisabled("target-class damage raw (+0xB0): %s",damage_classes.c_str());
                    ImGui::TextDisabled("Timing: life %ut · ramp %ut · fixed %ut · shade %dt",
                        shot->projectile_lifetime_ticks,shot->trajectory_ramp_duration_ticks,
                        shot->fixed_range_transition_age_threshold_ticks,shot->shading_release_transition_ticks);
                    ImGui::TextDisabled("Sound: mask %d · gain %.3fx · max %.3f",
                        shot->terrain_grid_mask_index,thandor::q15_gain_to_float(shot->positioned_sound_gain_q15),
                        thandor::q12_to_float(static_cast<int32_t>(shot->positioned_sound_maximum_distance_q12)));
                    const auto terrain_references=thandor::validate_shot_terrain_material_references(
                        *shot,static_cast<uint32_t>(loaded_material_count_));
                    if(terrain_references.invalid_reference_count==0U)
                        ImGui::TextDisabled("Terrain refs: %u ok · %u sentinel",
                            terrain_references.valid_reference_count,terrain_references.sentinel_count);
                    else
                        ImGui::TextColored(ImVec4(1.0F,0.55F,0.48F,1.0F),
                            "Terrain refs: %u ok · %u sentinel · %u INVALID",
                            terrain_references.valid_reference_count,terrain_references.sentinel_count,
                            terrain_references.invalid_reference_count);
                }
            }
            if(displayed_damage_shots.size()>displayed_weapon_count)
                ImGui::TextDisabled("+%zu more shot definition(s)",displayed_damage_shots.size()-displayed_weapon_count);
            if(displayed_weapon_count==0U)ImGui::TextDisabled("No resolved weapon profile");

            const auto& pipeline_validation=preset.effect_pipeline_validation;
            ImGui::SeparatorText("Effects");
            ImGui::Text("Effect pipeline: %s · W%u S%u E%u P%u · depth %u",
                pipeline_validation.ok()?"validated":"FAILED",pipeline_validation.weapon_pipeline_count,
                pipeline_validation.shot_reference_count,pipeline_validation.effect_reference_count,
                pipeline_validation.sprite_reference_count,pipeline_validation.maximum_effect_depth);
            for(const auto& issue:pipeline_validation.issues)
                ImGui::TextColored(ImVec4(1.0F,0.55F,0.48F,1.0F),"%s",issue.c_str());

            if(technical_details){
                ImGui::SeparatorText("PCK gameplay values");
                ImGui::TextDisabled("Raw: %u ARM nodes · root HP %d · demand %lluQ4 · gen %lluQ4 · X %uQ4 · T %uQ5",
                    preset.retail_stat_arm_node_count,preset.root_maximum_integrity,
                    static_cast<unsigned long long>(preset.retail_energy_demand_q4),
                    static_cast<unsigned long long>(preset.retail_energy_generation_capacity_q4),
                    preset.retail_xenite_cost_q4,preset.retail_build_time_q5);
                if(preset.has_placement_occupancy_class){
                    ImGui::TextDisabled("Field extraction weight: %u",preset.resource_extraction_weight_u8);
                    ImGui::TextDisabled("MDL +0xC0 resource selector · +0xC8 extraction weight; production resolves over the connected field region");
                }
                if(preset.has_resource_storage_capacity_contribution)
                    ImGui::TextDisabled("V537 class 14/15: MDL +0xC0 storage selector · +0xC4 contribution Q4");
                if(preset.runtime_model_class_id==2U||preset.runtime_model_class_id==3U||
                   preset.runtime_model_class_id==18U)
                    ImGui::TextDisabled("V537 water: MDL +0x194 multiplier · +0x198 threshold");
                if(preset.runtime_model_class_id==2U||preset.runtime_model_class_id==18U)
                    ImGui::TextDisabled("V537 steering: MDL +0xC0/+0xC4/+0xC8");
                if(preset.runtime_model_class_id==3U){
                    ImGui::TextDisabled("Footstep world sound index: %u",preset.articulated_contact_sound_asset_id);
                    ImGui::TextDisabled("Dry / water contact effects: EFF %u / EFF %u",
                        preset.timed_effect_id,preset.timed_water_effect_id);
                }
                if(preset.runtime_model_class_id==23U)
                    ImGui::TextDisabled("V537 class 23: MDL +0x0C step · +0x24 travel limit; collision hold deploys from zero, endpoint readiness permits the subsequent bidirectional link");
            }
            ImGui::PopTextWrapPos();

            ImGui::TableSetColumnIndex(1);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x);
            ImGui::SeparatorText("Research / model variants");
            if(document_){
                const uint32_t preview_faction=std::min(selected_faction_,7U);
                const auto& preview_mask=document_->faction_technology_mask(preview_faction);
                uint32_t selected_root=0U;
                uint32_t linked_count=0U;
                uint32_t deferred_count=0U;
                const auto exact_hierarchy=thandor::resolve_faction_technology_hierarchy(
                    preset,preview_mask,&selected_root,&linked_count,&deferred_count);
                const uint32_t preview_family=graphics_family_for_faction(preview_faction);
                ImGui::TextColored(graphics_family_ui_text_color(preview_family),"%s · Faction %u",
                    thandor::graphics_family_single_player_faction_name(preview_family),preview_faction);
                ImGui::SameLine();
                ImGui::TextDisabled("· research %u/256 · root MDL %u",
                    thandor::faction_technology_unlocked_count(preview_mask),selected_root);
                ImGui::TextDisabled("Runtime: %zu MDL · %u ARM · %u deferred",exact_hierarchy.size(),
                    linked_count,deferred_count);

                uint32_t timed_rules=0U;
                uint32_t damage_rules=0U;
                uint32_t notification_rules=0U;
                for(std::size_t node_index=0U;node_index<exact_hierarchy.size();++node_index){
                    const auto& node=exact_hierarchy[node_index];
                    const bool runtime_root=node.parent_index==std::numeric_limits<uint32_t>::max()||
                        (node.parent_index<exact_hierarchy.size()&&
                         exact_hierarchy[node.parent_index].model_definition_id!=node.model_definition_id);
                    if(!runtime_root)continue;
                    if(node.simulation.first_creation_notification_movie_id!=0U||
                       node.simulation.repeat_creation_notification_movie_id!=0U){
                        ++notification_rules;
                        if(technical_details)
                            ImGui::TextDisabled("Notification MDL %u · first movie %u · repeat movie %u",
                                node.model_definition_id,node.simulation.first_creation_notification_movie_id,
                                node.simulation.repeat_creation_notification_movie_id);
                    }
                    if(node.simulation.timed_shot_definition_id!=-1||node.simulation.timed_effect_id!=0U||
                       node.simulation.timed_water_effect_id!=0U)++timed_rules;
                    if(node.simulation.damage_effect_id!=0U&&
                       node.simulation.damage_effect_health_threshold_percent>0)++damage_rules;
                    if(technical_details&&node.simulation.damage_effect_id!=0U&&
                       node.simulation.damage_effect_health_threshold_percent>0){
                        ImGui::TextDisabled("Damage MDL %u · EFF %u < %d%% · cd %u..%ut",
                            node.model_definition_id,node.simulation.damage_effect_id,
                            node.simulation.damage_effect_health_threshold_percent,
                            node.simulation.damage_effect_min_cooldown_ticks,
                            node.simulation.damage_effect_min_cooldown_ticks+
                                (node.simulation.damage_effect_random_cooldown_ticks==0U?0U:
                                 node.simulation.damage_effect_random_cooldown_ticks-1U));
                    }
                }
                ImGui::TextDisabled("Runtime rules: timed %u · damage %u · notifications %u",
                    timed_rules,damage_rules,notification_rules);
            }
            if(!preset.loadout_name.empty())ImGui::Text("Loadout: %s",preset.loadout_name.c_str());
            if(!preset.technology_summary.empty())ImGui::Text("Panel: %s",preset.technology_summary.c_str());
            if(preset.linked_technology_id!=0U)
                ImGui::Text("Linked TEC %u · %s",preset.linked_technology_id,
                    preset.linked_technology_title.empty()?"unnamed":preset.linked_technology_title.c_str());
            ImGui::TextDisabled("TEC entries %zu · variant groups %zu",preset.technology_panel_entries.size(),
                preset.model_variant_groups.size());
            const std::size_t technology_preview_count=std::min<std::size_t>(preset.technology_panel_entries.size(),3U);
            for(std::size_t i=0U;i<technology_preview_count;++i){
                const auto& entry=preset.technology_panel_entries[i];
                ImGui::Text("TEC %u %s · B%u/R%u · X%u Demand%u Time%u · %s",entry.technology_id,
                    entry.title.empty()?"unnamed":entry.title.c_str(),entry.direction_index+1U,entry.row_index+1U,
                    entry.xenite_cost_q4 >> 4U,entry.energy_demand_q4 >> 4U,entry.research_duration_q5 >> 5U,
                    thandor::technology_category_retail_label(entry.category));
                if(technical_details&&!entry.special_features.empty())
                    ImGui::TextDisabled("special features: %s",entry.special_features.c_str());
            }
            if(preset.technology_panel_entries.size()>technology_preview_count)
                ImGui::TextDisabled("+%zu more technology entries",preset.technology_panel_entries.size()-technology_preview_count);

            ImGui::SeparatorText("Visual / runtime");
            ImGui::Text("Models %zu · linked ARM %u · deferred %u",
                preset.resolved_model_hierarchy.empty()?preset.model_hierarchy.size():preset.resolved_model_hierarchy.size(),
                preset.linked_model_runtime_count,preset.deferred_model_attachment_count);
            ImGui::TextDisabled("Shared EFF IDs: %s",
                shared_sprite_effect_ids.empty()?"none":shared_sprite_effect_ids.c_str());
            ImGui::TextDisabled("Canonical ARM %u · stock placements %u",
                thandor::canonical_placement_registry_id(preset),preset.stock_placement_count);

            try{
                const auto& preview=asset_preview(preset);
                ImGui::TextDisabled("Preview: %zu tris · %zu px · %u×%u",preview.textured_source_triangle_count,
                    preview.covered_pixel_count,preview.raster_width,preview.raster_height);
            }catch(const std::exception& error){
                ImGui::TextColored(ImVec4(1.0F,0.55F,0.48F,1.0F),"Preview: %s",error.what());
            }

            if(technical_details){
                ImGui::SeparatorText("Identity / source");
                ImGui::Text("Localized PCK name: %s",preset.localized_name.empty()?"unavailable":preset.localized_name.c_str());
                ImGui::TextDisabled("Name source: %s · resource 0x%08X · p%u #%u · MDL name %u",
                    localized_source.c_str(),preset.name_text_resource_id,thandor::kArmyAssetNameTextPage,
                    localized_index,preset.model_name_text_offset);
                ImGui::Text("ARM: %s",preset.source_path.c_str());
                ImGui::Text("MDL %u: %s",preset.model_definition_id,preset.model_source_path.c_str());
                ImGui::Text("SPR: %s",preset.sprite_path.empty()?"marker only":preset.sprite_path.c_str());
                ImGui::TextDisabled("Compatible ARM IDs: %s",compatible_ids.empty()?"same ID":compatible_ids.c_str());
                if(!preset.sprite_path.empty()){
                    try{
                        const auto& sprite=cached_sprite(preset.sprite_path);
                        std::size_t scaled_lighting_triangles=0U;
                        std::size_t total_triangles=0U;
                        for(const auto& lod:sprite.lod_groups()){
                            total_triangles+=lod.mesh.indices.size()/3U;
                            for(std::size_t triangle=0;triangle+2U<lod.mesh.indices.size();triangle+=3U){
                                const auto vertex_index=lod.mesh.indices[triangle];
                                if(vertex_index<lod.mesh.vertices.size()&&
                                   (lod.mesh.vertices[vertex_index].render_flags&0x0800U)!=0U)
                                    ++scaled_lighting_triangles;
                            }
                        }
                        ImGui::TextDisabled("SPR light %.3f · scaled tris %zu/%zu",
                            thandor::q12_to_float(sprite.lighting_scale_q12()),scaled_lighting_triangles,total_triangles);
                    }catch(const std::exception& error){
                        ImGui::TextColored(ImVec4(1.0F,0.55F,0.48F,1.0F),"SPR: %s",error.what());
                    }
                }
            }
            ImGui::PopTextWrapPos();
            ImGui::EndTable();
        }
        ImGui::Separator();
        ImGui::TextDisabled("%s",technical_details?"Release Shift for compact view":"Hold Shift for IDs, raw fields, and provenance");
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
    resize_width_=static_cast<int>(document_->field().width());
    resize_height_=static_cast<int>(document_->field().height());
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
    const auto panel=workspace_layout().inspector;
    ImGui::SetNextWindowPos({panel.x,panel.y});
    ImGui::SetNextWindowSize({panel.width,panel.height});
    ImGui::Begin("Inspector",nullptr,ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings);
    if(ImGui::SmallButton("Collapse inspector"))show_right_panel_=false;
    ImGui::Separator();
    if(requested_inspector_tab_>=0)inspector_page_=requested_inspector_tab_>=4?requested_inspector_tab_:0;
    if(inspector_page_>=4){
        if(ImGui::SmallButton("< Editor tools")){inspector_page_=0;requested_inspector_tab_=0;}
        ImGui::Separator();
    }
    if(ImGui::BeginTabBar("tools-tabs",ImGuiTabBarFlags_FittingPolicyScroll)){
        if(begin_inspector_tab("Inspector",0)){
            {auto scope=editor_performance_log_.measure(EditorPerfStage::UiInspectorSelection);draw_selected_object_editor();}
            if(ImGui::CollapsingHeader("Placement / gameplay")){
            const std::array<Tool,4> tools{Tool::Select,Tool::Resource,Tool::PlayerStart,Tool::EffectScenery};
            for(auto value:tools){
                if(ImGui::RadioButton(tool_name(value),tool_==value))tool_=value;
                if(value!=tools.back())ImGui::SameLine();
            }
            if(ImGui::InputScalar("Asset ID",ImGuiDataType_U32,&selected_asset_id_))rebuild_objects();
            if(const auto* selected_preset=catalog_.army_preset(selected_asset_id_);selected_preset!=nullptr){
                const auto diagnostic=catalog_.placement_compatibility_diagnostic(*selected_preset,document_->level());
                if(!diagnostic.empty())ImGui::TextColored({1.0F,0.45F,0.4F,1.0F},"Not placeable: %s",diagnostic.c_str());
            }
            draw_global_faction_selector("object-view-faction");
            ImGui::InputScalar("New orientation",ImGuiDataType_U32,&selected_orientation_);
            ImGui::TextWrapped("Drag a 3D asset card from the left panel directly onto the map. Click a placed object to select it.");

            ImGui::SeparatorText("Transform gizmo");
            if(ImGui::Button("Move  W",{ui_px(130.0F),0.0F})){tool_=Tool::Select;gizmo_mode_=GizmoMode::Translate;workspace_transform_action_=1;}
            ImGui::SameLine();
            if(ImGui::Button("Rotate  E",{ui_px(130.0F),0.0F})){tool_=Tool::Select;gizmo_mode_=GizmoMode::Rotate;workspace_transform_action_=2;}
            if(gizmo_mode_==GizmoMode::Translate)
                ImGui::TextDisabled("Red X and green Y move the LEV record. Blue Z raises or lowers its FLD terrain anchor.");
            else
                ImGui::TextDisabled("Drag the yellow ring to change the stock LEV yaw. Hold Shift for 15 degree snapping.");

            }

            ImGui::SeparatorText("Placed records");
            ImGui::BeginChild("placed-record-scroll",{0,ui_px(170.0F)},false,ImGuiWindowFlags_AlwaysVerticalScrollbar);
            auto placements=document_->level().placements();
            for(std::size_t i=0;i<placements.size();++i){
                const auto& placement=placements[i];
                const auto* preset=catalog_.army_preset(placement.army_asset_id);
                std::string placed_name=preset?preset->display_name:"unresolved";
                for(const auto& tag:document_->placement_tags())if(tag.placement_index==i&&!tag.display_name.empty()){placed_name=tag.display_name;break;}
                char label[256];
                std::snprintf(label,sizeof(label),"%s##placement_%zu",placed_name.c_str(),i);
                if(ImGui::Selectable(label,selected_placement_==i)){
                    selected_placement_=i;
                    selected_simulation_lab_entity_.reset();
                    tool_=Tool::Select;
                }
            }
            ImGui::EndChild();

            if(object_build_error_count_!=0)
                ImGui::TextWrapped("Some objects could not be displayed: %s",first_object_build_error_.c_str());
            if(!asset_load_failures_.empty())
                ImGui::TextWrapped("Unavailable assets skipped (%zu): %s: %s",asset_load_failures_.size(),
                                   asset_load_failures_.begin()->first.c_str(),asset_load_failures_.begin()->second.c_str());
            ImGui::EndTabItem();
        }
        if(begin_inspector_tab("Terrain",1)){
            if(tool_!=Tool::GenerateTerrain&&ImGui::Button("Generate terrain...",{-1,0}))choose_workspace_tool(Tool::GenerateTerrain,1);
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
                show_item_tooltip("FLD stores water as a relative terrain delta. Ordinary height brushes preserve or adjust that delta according to this option.");
            };

            ImGui::SeparatorText(tool_name(tool_));
            switch(tool_){
                case Tool::GenerateTerrain:draw_terrain_generator_panel();break;
                case Tool::Select:
                    ImGui::TextWrapped("Click a placed object to select it. Use the transform gizmo in the Inspector tab. Ctrl+D clears the temporary water selection and object selection.");
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
                        if(ImGui::Button("Water down",{ui_px(155.0F),0.0F}))adjust_water_selection_level(-water_level_step_);
                        ImGui::SameLine();
                        if(ImGui::Button("Water up",{ui_px(155.0F),0.0F}))adjust_water_selection_level(water_level_step_);
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
                    ImGui::TextWrapped("Paint resource fields. Crystal objects are placed separately.");
                    int type=resource_field_occupancy_class_==0U?0:1;
                    const char* types[]={"Xenite (gold)","Tritium (blue)"};
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
                    ImGui::TextDisabled("Imported textures are included when you save the project or export game assets.");

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
            const char* compatibility_modes[]={"Off","Vehicle traversal","Building placement","All terrain bands"};
            if(ImGui::Combo("Overlay",&terrain_compatibility_overlay_,compatibility_modes,4))rebuild_objects();
            if(terrain_compatibility_overlay_!=0)
                ImGui::TextDisabled("Cells: %zu safe · %zu caution · %zu blocked",
                    terrain_overlay_safe_cell_count_,terrain_overlay_caution_cell_count_,terrain_overlay_blocked_cell_count_);

            ImGui::SeparatorText("Fog of war preview");
            const char* fog_modes[]={"Off","Selected faction","All active factions"};
            bool fog_changed=ImGui::Combo("Visibility",&fog_preview_mode_,fog_modes,3);
            if(fog_preview_mode_==1){
                int faction=std::clamp(fog_preview_faction_,1,7);
                if(ImGui::SliderInt("View faction",&faction,1,7)){
                    fog_preview_faction_=faction;fog_changed=true;
                }
            }
            if(ImGui::Checkbox("Whole map already explored",&fog_preview_all_explored_))fog_changed=true;
            if(fog_preview_all_explored_)
                ImGui::TextDisabled("Discovered terrain and remembered models remain dimly visible. Models marked not remembered require current sight.");
            ImGui::Checkbox("Show nominal observer-radius circle",&fog_preview_show_nominal_radius_);
            if(fog_changed){
                fog_preview_occupancy_history_.clear();
                last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
                // A paused Lab has no new worker generation. Still refresh its fog preview
                // when the user changes faction or visibility mode.
                if(simulation_lab_active())simulation_lab_snapshot_dirty_=true;
                rebuild_objects();
            }
            ImGui::EndTabItem();
        }
        if(begin_inspector_tab("Players",2)){
            const auto faction_summary=document_->playable_faction_summary();
            ImGui::TextWrapped("Choose the active factions and the default player faction for Custom maps. All other active factions are computer-controlled.");
            try{
                auto configuration=document_->player_ai_configuration();
                int active=static_cast<int>(configuration.active_factions);
                int human=static_cast<int>(configuration.human_assignable_factions);
                const int minimum_active=std::clamp(static_cast<int>(faction_summary.highest_faction_index),2,7);
                bool counts_changed=false;
                counts_changed|=ImGui::SliderInt("Total active factions",&active,minimum_active,7);
                human=std::min(human,active);
                int default_human=static_cast<int>(document_->default_human_faction());
                if(ImGui::SliderInt("Human player slot",&default_human,1,active)){
                    if(graphics_family_for_faction(static_cast<uint32_t>(default_human))==8U)
                        status_="Golrathen must stay computer-controlled.";
                    else{checkpoint();document_->set_default_human_faction(static_cast<uint32_t>(default_human));status_="Default player faction updated.";}
                }
                ImGui::TextDisabled("Custom maps preselect this faction; the player can choose another. The others use AI.");
                const bool original_slots_open=ImGui::CollapsingHeader("Original-game slot compatibility");
                int requested_human=human;
                if(original_slots_open&&ImGui::SliderInt("Human player slots",&requested_human,1,active)){
                    bool would_make_golrathen_human=false;
                    for(int slot=1;slot<=requested_human;++slot)
                        would_make_golrathen_human|=graphics_family_for_faction(static_cast<uint32_t>(slot))==8U;
                    if(would_make_golrathen_human){
                        status_="Golrathen (army8) is mission-only: its slot must stay computer-controlled.";
                    }else{
                        human=requested_human;
                        counts_changed=true;
                    }
                }
                if(counts_changed){
                    checkpoint();
                    document_->set_player_ai_configuration({static_cast<uint32_t>(human),
                                                            static_cast<uint32_t>(active)});
                    status_="Player and computer faction setup updated.";
                }
                ImGui::Text("Computer factions in Custom maps: %d",std::max(0,active-1));

                ImGui::SeparatorText("Mission-only faction: Golrathen");
                ImGui::TextWrapped("Golrathen (gray army8) is a graphics family, not gameplay faction 8. "
                    "Assign it to a computer-controlled faction row (2..7). That row's units and buildings "
                    "then use Golrathen graphics in mission maps; the slot cannot be human-playable.");
                bool imported_golrathen_human=false;
                for(int slot=1;slot<=human;++slot)
                    imported_golrathen_human|=graphics_family_for_faction(static_cast<uint32_t>(slot))==8U;
                if(imported_golrathen_human)
                    ImGui::TextColored({1.0F,0.42F,0.30F,1.0F},
                        "Imported map uses army8 in a human slot; lower human slots or change its family.");
                int assigned_golrathen_ai_slot=0;
                for(int slot=human+1;slot<=7;++slot)
                    if(graphics_family_for_faction(static_cast<uint32_t>(slot))==8U){
                        assigned_golrathen_ai_slot=slot;
                        if(slot<=active)break;
                    }
                // Prefer an additional, currently inactive AI slot so the current
                // Ares/Devoken/etc. factions retain their original colours.
                if(mission_golrathen_ai_slot_<=human||mission_golrathen_ai_slot_>7){
                    mission_golrathen_ai_slot_=assigned_golrathen_ai_slot!=0?
                        assigned_golrathen_ai_slot:(active<7?active+1:human+1);
                }
                if(human<7){
                    const int target=mission_golrathen_ai_slot_;
                    const std::string target_label="Faction "+std::to_string(target)+
                        (target>active?" (new AI slot)":" (active AI slot)");
                    ImGui::SetNextItemWidth(-1.0F);
                    if(ImGui::BeginCombo("Golrathen AI faction row",target_label.c_str())){
                        for(int slot=human+1;slot<=7;++slot){
                            const uint32_t family=graphics_family_for_faction(static_cast<uint32_t>(slot));
                            std::string row_label="Faction "+std::to_string(slot)+" — "+
                                thandor::graphics_family_single_player_faction_name(family);
                            row_label+=slot>active?" (activate AI slot)":" (active AI slot)";
                            const bool is_selected=slot==mission_golrathen_ai_slot_;
                            if(ImGui::Selectable(row_label.c_str(),is_selected))
                                mission_golrathen_ai_slot_=slot;
                            if(is_selected)ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    const uint32_t target_family=graphics_family_for_faction(
                        static_cast<uint32_t>(mission_golrathen_ai_slot_));
                    if(target_family==8U){
                        ImGui::TextColored(graphics_family_ui_text_color(8U),
                            "Golrathen is assigned to AI faction %d.",mission_golrathen_ai_slot_);
                    }else{
                        std::size_t affected=0;
                        for(const auto& placement:document_->level().placements())
                            if(placement.faction_index==static_cast<uint32_t>(mission_golrathen_ai_slot_))
                                ++affected;
                        if(affected!=0U)
                            ImGui::TextWrapped("Warning: %zu existing placements share this owner. "
                                "All of them will switch to the gray army8 graphics family.",affected);
                        if(mission_golrathen_ai_slot_>active)
                            ImGui::TextDisabled("This also activates AI faction %d, leaving existing faction colours intact.",
                                mission_golrathen_ai_slot_);
                        if(ImGui::Button("Assign Golrathen (army8) to this AI faction",{-1.0F,0.0F})){
                            try{
                                const auto mission_slot=static_cast<uint32_t>(mission_golrathen_ai_slot_);
                                checkpoint();
                                if(mission_slot>static_cast<uint32_t>(active)){
                                    document_->set_player_ai_configuration({static_cast<uint32_t>(human),mission_slot});
                                    active=static_cast<int>(mission_slot);
                                }
                                document_->level().set_faction_graphics_family_offset_raw(
                                    mission_slot,8U-mission_slot);
                                document_->mark_dirty();
                                selected_faction_=mission_slot;
                                asset_preview_cache_.clear();
                                asset_preview_texture_cache_.clear();
                                rebuild_mesh();
                                status_="Assigned Golrathen gray army8 to computer-only faction "+
                                    std::to_string(mission_slot)+"; new placements use this row.";
                            }catch(const std::exception& e){
                                status_=std::string("Could not assign Golrathen: ")+e.what();
                            }
                        }
                    }
                    if(assigned_golrathen_ai_slot!=0&&assigned_golrathen_ai_slot!=mission_golrathen_ai_slot_)
                        ImGui::TextDisabled("A second AI row (faction %d) already uses army8.",
                            assigned_golrathen_ai_slot);
                }else{
                    ImGui::TextDisabled("No computer-only row is available. Reduce Human player slots first.");
                }
                ImGui::TextDisabled("After assignment, pick Golrathen in the Units / Buildings / Objects faction selector.");
                if(original_slots_open){
                    if(human<active) ImGui::TextDisabled("Original-game computer-only rows: %d..%d.",human+1,active);
                    else ImGui::TextDisabled("Original-game slots allow a human in every row.");
                }
                if(active<static_cast<int>(faction_summary.highest_faction_index))
                    ImGui::TextColored({1.0F,0.35F,0.25F,1.0F},"Active count is below the highest faction used by placements.");

                if(ImGui::CollapsingHeader("Faction relations")){
                    uint32_t relation_ui_flags=document_->level().relation_ui_flags();
                    uint32_t relation_state8=document_->level().relation_state8_faction_group_masks();
                    uint32_t relation_state4=document_->level().relation_state4_faction_group_masks();
                    bool relation_changed=false;
                    relation_changed|=ImGui::InputScalar("Relation UI flags",ImGuiDataType_U32,
                        &relation_ui_flags,nullptr,nullptr,"%08X",ImGuiInputTextFlags_CharsHexadecimal);
                    relation_changed|=ImGui::InputScalar("State-8 faction group masks",ImGuiDataType_U32,
                        &relation_state8,nullptr,nullptr,"%08X",ImGuiInputTextFlags_CharsHexadecimal);
                    relation_changed|=ImGui::InputScalar("State-4 faction group masks",ImGuiDataType_U32,
                        &relation_state4,nullptr,nullptr,"%08X",ImGuiInputTextFlags_CharsHexadecimal);
                    if(relation_changed){
                        checkpoint();
                        document_->level().set_initial_relation_setup(
                            relation_ui_flags,relation_state8,relation_state4);
                        document_->mark_dirty();
                        status_="Initial authored faction relation masks updated.";
                    }

                }

                ImGui::SeparatorText("Starting resources by active faction");
                bool any_resource_changed=false;
                for(int faction=1;faction<=active;++faction){
                    ImGui::PushID(faction);
                    const bool computer=static_cast<uint32_t>(faction)!=document_->default_human_faction();
                    const uint32_t current_family=graphics_family_for_faction(static_cast<uint32_t>(faction));
                    const std::string faction_heading=std::string(
                        thandor::graphics_family_single_player_faction_name(current_family))+
                        " · Faction "+std::to_string(faction)+(computer?" · Computer":" · Default player");
                    ImGui::PushStyleColor(ImGuiCol_Text,graphics_family_ui_text_color(current_family));
                    ImGui::SeparatorText(faction_heading.c_str());
                    ImGui::PopStyleColor();
                    int xenite_units=static_cast<int>(document_->level().faction_xenite_q4(
                        static_cast<uint32_t>(faction))/16U);
                    int tritium_units=static_cast<int>(document_->level().faction_tritium_q4(
                        static_cast<uint32_t>(faction))/16U);
                    bool changed=false;
                    changed|=ImGui::InputInt("Xenite",&xenite_units,100,1000);
                    changed|=ImGui::InputInt("Tritium",&tritium_units,100,1000);
                    int ai_class_or_mode=static_cast<int32_t>(document_->level().faction_graphics_family_offset_raw(
                        static_cast<uint32_t>(faction)));
                    bool ai_mode_changed=ImGui::InputInt("Graphics family offset (raw LEV)",&ai_class_or_mode,1,1);
                    uint32_t resolved_graphics_family=static_cast<uint32_t>(faction)+
                        static_cast<uint32_t>(static_cast<int32_t>(ai_class_or_mode));
                    if(ai_mode_changed&&!computer&&resolved_graphics_family==8U){
                        ai_mode_changed=false;
                        ai_class_or_mode=static_cast<int32_t>(
                            document_->level().faction_graphics_family_offset_raw(static_cast<uint32_t>(faction)));
                        resolved_graphics_family=graphics_family_for_faction(static_cast<uint32_t>(faction));
                        status_="Golrathen gray (army8) is mission-only and must use a computer-controlled row.";
                    }

                    xenite_units=std::clamp(xenite_units,0,10000000);
                    tritium_units=std::clamp(tritium_units,0,10000000);
                    if(changed||ai_mode_changed){
                        if(!any_resource_changed) checkpoint();
                        any_resource_changed=true;
                        document_->level().set_faction_starting_resources_q4(
                            static_cast<uint32_t>(faction),static_cast<uint32_t>(xenite_units)*16U,
                            static_cast<uint32_t>(tritium_units)*16U);
                        if(ai_mode_changed)
                            document_->level().set_faction_graphics_family_offset_raw(
                                static_cast<uint32_t>(faction),
                                static_cast<uint32_t>(static_cast<int32_t>(ai_class_or_mode)));
                        document_->mark_dirty();
                        status_="Faction player-slot metadata updated.";
                    }
                    if(xenite_units==0&&tritium_units==0)
                        ImGui::TextDisabled("Starts with no Xenite or Tritium.");
                    ImGui::PopID();
                }
                if(ImGui::Button("Apply 6500 / 3000 to all active factions",{-1.0F,0.0F})){
                    checkpoint();
                    for(int faction=1;faction<=active;++faction)
                        document_->level().set_faction_starting_resources_q4(
                            static_cast<uint32_t>(faction),thandor::LevelAsset::kDefaultXeniteQ4,
                            thandor::LevelAsset::kDefaultTritiumQ4);
                    document_->mark_dirty();
                    status_="Applied stock skirmish starting resources to all active factions.";
                }
                ImGui::SeparatorText("Placement readiness");
                ImGui::Text("Highest faction used by placements: %u",faction_summary.highest_faction_index);
                ImGui::Text("Faction-owned placements: %u · start vehicles: %u",
                            faction_summary.nonneutral_placement_count,faction_summary.player_start_count);
                ImGui::TextWrapped("%s",faction_summary.diagnostic.c_str());

            }catch(const std::exception& error){
                ImGui::TextWrapped("Player/AI metadata unavailable: %s",error.what());
            }
            if(ImGui::CollapsingHeader("KI.DAT reference"))
                {auto scope=editor_performance_log_.measure(EditorPerfStage::UiInspectorAiKnowledge);
                ImGui::TextWrapped("Values from %s. Choose the profile in Simulation Lab.",ai_profiles_.use_custom?"ki.dat_custom.ini":"ki.dat.ini");
                draw_ai_knowledge_panel(ai_knowledge_, ai_knowledge_load_error_, ai_knowledge_ui_);}
            ImGui::EndTabItem();
        }
        if(begin_inspector_tab("Simulation",3)){
            {auto scope=editor_performance_log_.measure(EditorPerfStage::UiInspectorSimulation);draw_simulation_lab_panel();}
            ImGui::EndTabItem();
        }
        if(begin_inspector_tab("Map",4)){
            if(ImGui::CollapsingHeader("Map validation")){
                ImGui::Checkbox("Show resource support",&retail_field_diagnostics_);
                if(retail_field_diagnostics_){
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ui_px(150.0F));
                    ImGui::Combo("Resource support",&retail_field_resource_selector_,
                        "Xenite\0Tritium\0");

                }
                ImGui::TextWrapped("Checks resource-field objects for a missing Xenite (0x0800) or Tritium (0x1000) flag on their terrain cell.");
                const auto validation=validate_resource_deposit_flags(document_->field(),document_->level().placements());
                const auto& resource_issues=validation.issues;
                const auto resource_checks=validation.checked;
                if(resource_issues.empty())
                    ImGui::TextColored({0.55F,0.90F,0.62F,1.0F},
                        "Resource support flags: %zu checked · no mismatches",resource_checks);
                else
                    ImGui::TextColored({1.0F,0.52F,0.30F,1.0F},
                        "Resource support flags: %zu checked · %zu issue%s",resource_checks,
                        resource_issues.size(),resource_issues.size()==1U?"":"s");

                if(!resource_issues.empty()&&ImGui::BeginTable("resource_flag_issues",5,
                    ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,
                    ImVec2(0.0F,std::min(ui_px(220.0F),ui_px(56.0F)+ui_px(24.0F)*static_cast<float>(resource_issues.size()))))){
                    ImGui::TableSetupColumn("Placement");ImGui::TableSetupColumn("ARM");
                    ImGui::TableSetupColumn("Cell");
                    ImGui::TableSetupColumn("Need");ImGui::TableSetupColumn("FLD flags");
                    ImGui::TableHeadersRow();
                    for(const auto& issue:resource_issues){
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);ImGui::Text("%zu",issue.placement_index);
                        ImGui::TableSetColumnIndex(1);ImGui::Text("%u",issue.army_asset_id);
                        ImGui::TableSetColumnIndex(2);ImGui::Text("%u,%u",issue.cell_x,issue.cell_y);
                        ImGui::TableSetColumnIndex(3);ImGui::Text("0x%08X",issue.audit.required_support_mask);
                        ImGui::TableSetColumnIndex(4);ImGui::Text("0x%08X",issue.audit.flags_and_material);
                    }
                    ImGui::EndTable();
                }
            }
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
            char author[256]{};
            std::snprintf(author,sizeof(author),"%s",document_->author().c_str());
            if(ImGui::InputText("Author",author,sizeof(author),ImGuiInputTextFlags_EnterReturnsTrue)){
                checkpoint();document_->set_author(author);document_->mark_dirty();status_="Map author updated.";
            }
            ImGui::TextDisabled("The display name is stored in the full editor project; the internal map name remains the LEV/FLD filename.");
            const auto faction_summary=document_->playable_faction_summary();
            ImGui::TextDisabled("Player roles and starting resources are in Players.");

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
                scenario_text_buffers_dirty_|=ImGui::InputTextMultiline("Summary / setup##scenario1",scenario_text_buffers_[0].data(),scenario_text_buffers_[0].size(),{-1,ui_px(86)});
                for(int row=0;row<7;++row){
                    ImGui::PushID(row);
                    ImGui::Text("Detail row %d",row+1);
                    scenario_text_buffers_dirty_|=ImGui::InputText("Label",scenario_text_buffers_[1+row].data(),scenario_text_buffers_[1+row].size());
                    scenario_text_buffers_dirty_|=ImGui::InputTextMultiline("Objective / description",scenario_text_buffers_[8+row].data(),scenario_text_buffers_[8+row].size(),{-1,ui_px(64)});
                    ImGui::PopID();
                }
                if(ImGui::Button("Apply scenario text",{-1,0}))apply_scenario_text_buffers();
                if(scenario_text_buffers_dirty_)ImGui::TextDisabled("Scenario text has unapplied edits.");
            }
            if(ImGui::CollapsingHeader("Campaign / mission script (LEV conditions)")){
                try{
                    const uint32_t condition_count=document_->level().active_scheduled_condition_count();
                    const uint32_t trigger_count=document_->level().active_end_condition_trigger_count();
                    const bool stock_skirmish_shape=condition_count==14U&&trigger_count==8U;
                    ImGui::Text("Active scheduled conditions: %u / 64",condition_count);
                    ImGui::Text("Active end triggers: %u / 16",trigger_count);
                    ImGui::TextColored(stock_skirmish_shape?ImVec4{0.55F,0.85F,0.55F,1.0F}:ImVec4{0.95F,0.75F,0.35F,1.0F},
                        "%s",stock_skirmish_shape?"Stock skirmish elimination script shape":"Custom mission/campaign script shape");

                    static int selected_condition=0;
                    static int selected_trigger=0;
                    ImGui::SliderInt("Condition slot",&selected_condition,0,63);
                    auto condition=document_->level().scheduled_condition(static_cast<uint32_t>(selected_condition));
                    static constexpr std::array<thandor::LevelScheduledConditionKind,14> kinds{{
                        thandor::LevelScheduledConditionKind::NoneOrUnused,
                        thandor::LevelScheduledConditionKind::NoActiveEntityWithDefinition,
                        thandor::LevelScheduledConditionKind::NoActiveEntityWithDefinitionAndClassCommandGroupA,
                        thandor::LevelScheduledConditionKind::NoActiveEntityWithDefinitionAndRuntimeId,
                        thandor::LevelScheduledConditionKind::FactionInactiveOrRelationAtLeast8,
                        thandor::LevelScheduledConditionKind::XeniteCurrentAtLeast,
                        thandor::LevelScheduledConditionKind::TritiumCurrentAtLeast,
                        thandor::LevelScheduledConditionKind::ActiveArmyScaleValueAtLeast,
                        thandor::LevelScheduledConditionKind::MatchingDefinitionAndRuntimeIdActiveEntityCountAtLeast,
                        thandor::LevelScheduledConditionKind::FactionTerrainOccupancyPercentAtLeast,
                        thandor::LevelScheduledConditionKind::CountdownElapsed,
                        thandor::LevelScheduledConditionKind::XeniteStorageLimitAtMost0FA0,
                        thandor::LevelScheduledConditionKind::NoActiveEntityWithClassIdOutsideClassCommandGroupA,
                        thandor::LevelScheduledConditionKind::BooleanPostfixExpression,
                    }};
                    int kind_index=0;
                    for(int i=0;i<static_cast<int>(kinds.size());++i)if(kinds[static_cast<std::size_t>(i)]==condition.kind())kind_index=i;
                    std::array<const char*,kinds.size()> kind_names{};
                    for(std::size_t i=0;i<kinds.size();++i)kind_names[i]=scheduled_condition_kind_name(kinds[i]);
                    bool script_changed=false;
                    if(ImGui::Combo("Condition kind",&kind_index,kind_names.data(),static_cast<int>(kind_names.size()))){
                        condition.status_and_kind=static_cast<uint8_t>(kinds[static_cast<std::size_t>(kind_index)]);
                        condition.payload_bytes.fill(0U);
                        if(condition.kind()==thandor::LevelScheduledConditionKind::BooleanPostfixExpression)
                            condition.payload_bytes[0]=0xFCU;
                        script_changed=true;
                    }
                    const auto edit_operand=[&](const char* label,std::size_t operand,uint32_t maximum=UINT32_MAX){
                        uint32_t value=condition.operand(operand);
                        if(ImGui::InputScalar(label,ImGuiDataType_U32,&value,nullptr,nullptr,"%u")){
                            condition.set_operand(operand,std::min(value,maximum));return true;
                        }
                        return false;
                    };
                    using CK=thandor::LevelScheduledConditionKind;
                    switch(condition.kind()){
                    case CK::NoActiveEntityWithDefinition:
                    case CK::NoActiveEntityWithDefinitionAndClassCommandGroupA:
                        script_changed|=edit_operand("Faction",0,7U);break;
                    case CK::NoActiveEntityWithDefinitionAndRuntimeId:
                        script_changed|=edit_operand("Faction",0,7U);
                        script_changed|=edit_operand("ARM asset id",2);break;
                    case CK::FactionInactiveOrRelationAtLeast8:
                        script_changed|=edit_operand("Faction A",0,7U);
                        script_changed|=edit_operand("Faction B",1,7U);break;
                    case CK::XeniteCurrentAtLeast:
                    case CK::TritiumCurrentAtLeast:
                    case CK::ActiveArmyScaleValueAtLeast:
                        script_changed|=edit_operand("Faction",0,7U);
                        script_changed|=edit_operand("Threshold Q4",1);break;
                    case CK::MatchingDefinitionAndRuntimeIdActiveEntityCountAtLeast:
                        script_changed|=edit_operand("Faction",0,7U);
                        script_changed|=edit_operand("Required active count",1);
                        script_changed|=edit_operand("ARM asset id",2);break;
                    case CK::FactionTerrainOccupancyPercentAtLeast:
                        script_changed|=edit_operand("Faction / occupancy byte offset",0,7U);
                        script_changed|=edit_operand("Percent threshold",1);break;
                    case CK::CountdownElapsed:
                        script_changed|=edit_operand("Countdown steps",1);
                        if(ImGui::IsItemHovered())show_item_tooltip("Decremented on each 20-tick condition pass by the simulation step batch (normally 1). This is not a raw simulation-tick counter.");
                        break;
                    case CK::XeniteStorageLimitAtMost0FA0:
                        script_changed|=edit_operand("Faction",0,7U);break;
                    case CK::NoActiveEntityWithClassIdOutsideClassCommandGroupA:
                        script_changed|=edit_operand("Runtime class id",0);break;
                    case CK::BooleanPostfixExpression:{
                        static char expression_text[256]{};
                        static std::array<uint8_t,15> expression_image{};
                        static int expression_slot=-1;
                        static std::string expression_error;
                        if(expression_slot!=selected_condition||expression_image!=condition.payload_bytes) {
                            std::snprintf(expression_text,sizeof(expression_text),"%s",thandor::campaign_expression_text(condition).c_str());
                            expression_slot=selected_condition;expression_image=condition.payload_bytes;expression_error.clear();
                        }
                        ImGui::InputText("Expression tokens",expression_text,sizeof(expression_text));
                        ImGui::TextDisabled("Example: C0 C1 AND C2 NOT OR. END is added automatically.");
                        if(ImGui::Button("Apply expression")) {
                            try{condition=thandor::campaign_expression_from_text(expression_text);script_changed=true;expression_error.clear();}
                            catch(const std::exception& e){expression_error=e.what();}
                        }
                        if(!expression_error.empty())ImGui::TextWrapped("%s",expression_error.c_str());
                        if(ImGui::CollapsingHeader("Advanced expression bytes")){
                            ImGui::Text("Expression: %s",format_condition_expression(condition).c_str());
                            ImGui::TextDisabled("Tokens: C0..C63 reference condition slots; FD=NOT, FE=AND, FF=OR, FC=END. Evaluation is postfix and can combine earlier or later condition slots.");
                            for(std::size_t token=0;token<condition.payload_bytes.size();++token){
                                ImGui::PushID(static_cast<int>(token));
                                uint8_t value=condition.payload_bytes[token];
                                ImGui::SetNextItemWidth(ui_px(48.0F));
                                if(ImGui::InputScalar("##token",ImGuiDataType_U8,&value,nullptr,nullptr,"%02X",ImGuiInputTextFlags_CharsHexadecimal)){
                                    condition.payload_bytes[token]=value;script_changed=true;
                                }
                                if(token+1U<condition.payload_bytes.size())ImGui::SameLine();
                                ImGui::PopID();
                            }
                        }
                        break;
                    }
                    default:
                        ImGui::TextDisabled("No operands.");break;
                    }
                    if(script_changed){
                        checkpoint();
                        document_->level().set_scheduled_condition(static_cast<uint32_t>(selected_condition),condition);
                        document_->mark_dirty();
                        status_="LEV mission condition updated.";
                    }

                    ImGui::SeparatorText("End-condition triggers");
                    ImGui::SliderInt("Trigger slot",&selected_trigger,0,15);
                    auto trigger=document_->level().end_condition_trigger(static_cast<uint32_t>(selected_trigger));
                    bool active=trigger.active();
                    bool trigger_changed=ImGui::Checkbox("Active trigger",&active);
                    if(active)trigger.state_flags=1U;else trigger.state_flags=0U;
                    int trigger_faction=trigger.faction_runtime_index;
                    int trigger_condition=trigger.condition_index;
                    int end_movie_index=trigger.end_movie_selection_index;
                    int movie_variant=trigger.movie_variant_selector;
                    bool skip_disable=trigger.skip_army_disable_when_one==1U;
                    trigger_changed|=ImGui::InputInt("Faction ended by trigger",&trigger_faction,1,1);
                    trigger_changed|=ImGui::InputInt("Condition slot used",&trigger_condition,1,1);
                    trigger_changed|=ImGui::InputInt("Campaign outcome (0-7)",&end_movie_index,1,1);
                    draw_campaign_trigger_destination(static_cast<uint8_t>(std::clamp(end_movie_index,0,7)));
                    trigger_changed|=ImGui::InputInt("Movie variant selector",&movie_variant,1,1);
                    trigger_changed|=ImGui::Checkbox("End immediately without disabling armies",&skip_disable);
                    trigger.faction_runtime_index=static_cast<uint8_t>(std::clamp(trigger_faction,0,7));
                    trigger.condition_index=static_cast<uint8_t>(std::clamp(trigger_condition,0,63));
                    trigger.end_movie_selection_index=static_cast<uint8_t>(std::clamp(end_movie_index,0,7));
                    trigger.movie_variant_selector=static_cast<uint8_t>(std::clamp(movie_variant,0,255));
                    trigger.skip_army_disable_when_one=skip_disable?1U:0U;
                    if(trigger_changed){
                        checkpoint();
                        document_->level().set_end_condition_trigger(static_cast<uint32_t>(selected_trigger),trigger);
                        document_->mark_dirty();
                        status_="LEV mission end trigger updated.";
                    }
                    ImGui::TextDisabled("When the selected condition becomes satisfied, an active trigger is marked processed, transitions its faction toward ended state, optionally disables that faction's armies, and chooses the campaign/skirmish ending movie record. It is not the ordinary notification queue.");
                }catch(const std::exception& error){
                    ImGui::TextWrapped("Mission script image unavailable: %s",error.what());
                }
            }

            // Keep the draft across frames so the separate Apply button can use it.
            ImGui::InputInt("Width",&resize_width_);
            ImGui::InputInt("Height",&resize_height_);
            if(ImGui::Button("Resize centered")){
                try{
                    if(resize_width_<0||resize_height_<0||!thandor::FieldGrid::is_game_compatible_shape(
                        static_cast<uint32_t>(resize_width_),static_cast<uint32_t>(resize_height_)))
                        throw std::runtime_error("Choose game-compatible dimensions (59..139 in 8-cell steps, area at most 131 x 131).");
                    checkpoint();
                    document_->resize_field(static_cast<uint32_t>(resize_width_),
                                            static_cast<uint32_t>(resize_height_),true);
                    reset_terrain_generator();rebuild_mesh();center_camera();
                    status_="Resized field and translated retained placements.";
                }catch(const std::exception& e){status_=e.what();}
            }
            ImGui::SeparatorText("Lighting and color");
            auto lighting=document_->level().lighting();
            bool lighting_changed=false;
            const auto edit_argb=[&](const char* label,uint32_t& packed){
                ImVec4 value=argb_to_imgui(packed);
                if(!ImGui::ColorEdit4(label,&value.x))return false;
                packed=imgui_to_argb(value);
                return true;
            };
            lighting_changed|=edit_argb("Primary base",lighting.base_color);
            lighting_changed|=edit_argb("Primary ramp 1",lighting.ramp_124);
            lighting_changed|=edit_argb("Primary color 1",lighting.color_128);
            lighting_changed|=edit_argb("Primary ramp 2",lighting.ramp_12c);
            if(ImGui::CollapsingHeader("Primary extended colors")){
                lighting_changed|=edit_argb("Primary color 2",lighting.color_130);
                lighting_changed|=edit_argb("Primary color 3",lighting.color_134);
                lighting_changed|=edit_argb("Primary color 4",lighting.color_138);
                lighting_changed|=edit_argb("Primary color 5",lighting.color_13c);
            }
            int elevation=lighting.elevation;
            int azimuth=lighting.azimuth;
            lighting_changed|=ImGui::SliderInt("Primary elevation",&elevation,0,65535);
            lighting_changed|=ImGui::SliderInt("Primary azimuth",&azimuth,0,65535);
            int field_width=lighting.field_region_width;
            int field_height=lighting.field_region_height;
            lighting_changed|=ImGui::InputInt("Field-region width",&field_width);
            lighting_changed|=ImGui::InputInt("Field-region height",&field_height);
            field_width=std::clamp(field_width,0,65535);
            field_height=std::clamp(field_height,0,65535);
            uint32_t cycle_duration=lighting.cycle_duration_ticks;
            if(ImGui::InputScalar("Lighting cycle duration ticks",ImGuiDataType_U32,&cycle_duration))
                lighting_changed=true;

            if(ImGui::CollapsingHeader("Alternate lighting endpoint")){
                int alt_elevation=lighting.alternate_elevation;
                int alt_azimuth=lighting.alternate_azimuth;
                lighting_changed|=ImGui::SliderInt("Alternate elevation",&alt_elevation,0,65535);
                lighting_changed|=ImGui::SliderInt("Alternate azimuth",&alt_azimuth,0,65535);
                int alt_width=lighting.alternate_field_region_width;
                int alt_height=lighting.alternate_field_region_height;
                lighting_changed|=ImGui::InputInt("Alternate field width",&alt_width);
                lighting_changed|=ImGui::InputInt("Alternate field height",&alt_height);
                alt_width=std::clamp(alt_width,0,65535);
                alt_height=std::clamp(alt_height,0,65535);
                lighting_changed|=edit_argb("Alternate base",lighting.alternate_base_color);
                lighting_changed|=edit_argb("Alternate ramp 1",lighting.alternate_ramp_124);
                lighting_changed|=edit_argb("Alternate color 1",lighting.alternate_color_128);
                lighting_changed|=edit_argb("Alternate ramp 2",lighting.alternate_ramp_12c);
                lighting_changed|=edit_argb("Alternate color 2",lighting.alternate_color_130);
                lighting_changed|=edit_argb("Alternate color 3",lighting.alternate_color_134);
                lighting_changed|=edit_argb("Alternate color 4",lighting.alternate_color_138);
                lighting_changed|=edit_argb("Alternate color 5",lighting.alternate_color_13c);
                lighting.alternate_elevation=static_cast<uint16_t>(alt_elevation);
                lighting.alternate_azimuth=static_cast<uint16_t>(alt_azimuth);
                lighting.alternate_field_region_width=static_cast<uint16_t>(alt_width);
                lighting.alternate_field_region_height=static_cast<uint16_t>(alt_height);
            }
            lighting.elevation=static_cast<uint16_t>(elevation);
            lighting.azimuth=static_cast<uint16_t>(azimuth);
            lighting.field_region_width=static_cast<uint16_t>(field_width);
            lighting.field_region_height=static_cast<uint16_t>(field_height);
            lighting.cycle_duration_ticks=cycle_duration;
            if(lighting_changed){
                checkpoint();
                document_->level().lighting()=lighting;
                document_->mark_dirty();
                if(lighting.cycle_duration_ticks==0U)lighting_cycle_preview_tick_=0U;
                rebuild_mesh();
            }
            bool preview_changed=ImGui::Checkbox("Preview lighting cycle",&lighting_cycle_preview_enabled_);
            if(lighting.cycle_duration_ticks==0U){
                ImGui::SameLine();ImGui::TextDisabled("(cycle duration is zero)");
            }else{
                uint32_t preview_tick=std::min(lighting_cycle_preview_tick_,lighting.cycle_duration_ticks-1U);
                const uint32_t preview_max=lighting.cycle_duration_ticks-1U;
                if(ImGui::InputScalar("Preview cycle tick",ImGuiDataType_U32,&preview_tick)){
                    lighting_cycle_preview_tick_=std::min(preview_tick,preview_max);preview_changed=true;
                }

            }
            if(preview_changed)rebuild_mesh();

            if(ImGui::Checkbox("Detailed terrain transitions",&exact_soil_packets_))
                rebuild_mesh();

            uint32_t seed=soil_transition_rng_seed_;
            if(ImGui::InputScalar("Soil transition RNG seed",ImGuiDataType_U32,&seed,nullptr,nullptr,"0x%08X",ImGuiInputTextFlags_CharsHexadecimal)){
                soil_transition_rng_seed_=seed;rebuild_mesh();
            }
            if(ImGui::Button("Reset soil RNG seed to game default",{-1.0F,0.0F})){
                soil_transition_rng_seed_=thandor::FieldGrid::kDefaultSoilRandomSeed;rebuild_mesh();
            }

            uint32_t resource_seed=resource_animation_rng_seed_;
            if(ImGui::InputScalar("Resource animation RNG seed",ImGuiDataType_U32,&resource_seed,nullptr,nullptr,"0x%08X",ImGuiInputTextFlags_CharsHexadecimal)){
                resource_animation_rng_seed_=resource_seed;rebuild_objects();
            }
            if(ImGui::Button("Reset resource animation seed",{-1.0F,0.0F})){
                resource_animation_rng_seed_=thandor::FieldGrid::kDefaultSoilRandomSeed;rebuild_objects();
            }

            ImGui::Checkbox("Object shadows",&projected_object_shadows_);

            ImGui::EndTabItem();
        }
        if(begin_inspector_tab("Export",5)){
            ImGui::SeparatorText("Editor project");
            if(ImGui::Button("Save as THMAP...",{-1,0})){
                save_project_path_[0]='\0';show_save_dialog_=true;
            }
            ImGui::TextWrapped("Save an editable .thmap project in the maps folder beside the editor executable, with terrain, objects, names and imported textures.");

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
            ImGui::Checkbox("Back up replaced level and PATCH archives",&backup_existing_level_package_);
            ImGui::TextWrapped("Installing writes levelNN.pck and updates PATCH00.PCK in the game folder, including map text aliases and conflicting overrides. With backups enabled, existing level and PATCH archives are backed up before changes.");
            if(ImGui::Button("Install playable map",{-1,0}))export_game_level_package();

            ImGui::SeparatorText("Custom game resources");
            ImGui::Checkbox("Imported/custom terrain and assets",&patch00_include_custom_assets_);
            ImGui::Checkbox("Back up modified PATCH archive",&backup_existing_patch00_);
            ImGui::TextWrapped("Update custom resources and map text in the active PATCH archive. Use level export above to install the map itself.");
            if(ImGui::Button("Update game resources",{-1,0}))update_patch00_overrides();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void EditorApp::draw_status_bar(){
    const auto bar=workspace_layout().status;
    ImGui::SetNextWindowPos({bar.x,bar.y});ImGui::SetNextWindowSize({bar.width,bar.height});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2{ui_px(12),ui_px(3)});
    ImGui::Begin("##workspace-status",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted(status_.c_str());
    ImGui::End();ImGui::PopStyleVar();
}

void EditorApp::draw_new_map_dialog(){
    if(show_new_dialog_){ImGui::OpenPopup("Create new map");show_new_dialog_=false;new_map_error_.clear();}
    if(!ImGui::BeginPopupModal("Create new map",nullptr,ImGuiWindowFlags_AlwaysAutoResize))return;
    ImGui::InputText("Internal map name",new_map_name_,sizeof(new_map_name_));
    ImGui::InputText("Display name",new_display_name_,sizeof(new_display_name_));
    bool from_template=!new_empty_map_;
    if(ImGui::Checkbox("Use terrain from an existing map",&from_template))new_empty_map_=!from_template;
    if(new_empty_map_){
        int type=static_cast<int>(new_terrain_settings_.type);
        const char* types[]{"Land / room for lakes","Shallow water","Deep water","Island"};
        if(ImGui::Combo("Map type",&type,types,4))new_terrain_settings_=thandor::new_map_terrain_defaults(static_cast<thandor::NewMapTerrainType>(type));
        if(new_terrain_settings_.type==thandor::NewMapTerrainType::Island){
            ImGui::InputScalar("Island seed",ImGuiDataType_U32,&new_terrain_settings_.seed);
            if(ImGui::Button("Reroll island"))new_terrain_settings_.seed=thandor::next_terrain_seed(new_terrain_settings_.seed);
        }
        const char* surfaces[]{"Water","Ice","Lava","From terrain style"};
        ImGui::Combo("Surface",&new_surface_type_,surfaces,4);
    }
    if(!new_empty_map_){
        ImGui::Checkbox("Use source map's original terrain dimensions",&new_use_template_dimensions_);
    }
    const bool use_source_size=!new_empty_map_&&new_use_template_dimensions_;
    if(use_source_size){
        ImGui::TextDisabled("Size: original source FLD width and height");
    }else{
        ImGui::InputInt("Width",&new_width_);ImGui::InputInt("Height",&new_height_);
    }
    const bool new_dimensions_ok=use_source_size||
        (new_width_>=0&&new_height_>=0&&
        thandor::FieldGrid::is_game_compatible_shape(static_cast<uint32_t>(new_width_),
                                                     static_cast<uint32_t>(new_height_)));
    if(!new_dimensions_ok)ImGui::TextWrapped("Game-compatible shape: each axis must be 59, 67, 75, 83, 91, 99, 107, 115, 123, 131, or 139, and total area must not exceed 131 x 131. A 139-cell axis requires a smaller opposite axis.");
    std::vector<const char*> names;for(const auto& g:catalog_.map_groups())names.push_back(g.display_name.c_str());
    if(names.empty())ImGui::TextWrapped("No stock maps were found. Load a game folder containing map FLD/LEV assets first.");
    else ImGui::Combo(new_empty_map_?"Terrain style":"Terrain template",&template_group_,names.data(),static_cast<int>(names.size()));
    if(new_empty_map_){
        using T=thandor::NewMapTerrainType;
        if(new_terrain_settings_.type==T::Land)ImGui::TextWrapped("Dry land starts above surface level, leaving room to carve lakes and rivers.");
        else if(new_terrain_settings_.type==T::ShallowWater)ImGui::TextWrapped("A flat shallow bed with a visible surface. Raise terrain to create dry ground.");
        else if(new_terrain_settings_.type==T::DeepWater)ImGui::TextWrapped("A submerged flat bed. Raise terrain to create shores and islands.");
        else ImGui::TextWrapped("A central land area with sloping shores and water around it.");
        if(ImGui::CollapsingHeader("Starting heights and material")){
            if(new_terrain_settings_.type==T::Land||new_terrain_settings_.type==T::Island)
                ImGui::DragFloat("Land height",&new_terrain_settings_.land_height,0.05F,-128,128);
            ImGui::DragFloat("Surface level",&new_terrain_settings_.surface_level,0.05F,-128,128);
            if(new_terrain_settings_.type!=T::Land)
                ImGui::DragFloat("Water depth",&new_terrain_settings_.water_depth,0.01F,0.02F,new_terrain_settings_.type==T::ShallowWater?0.1F:64.0F);
            ImGui::SliderInt("Initial material",&new_initial_material_,0,25,"Layer %d");
            ImGui::TextWrapped("A surface appears where its level is above the terrain bed. Height zero is valid.");
        }
    }else{
        ImGui::TextWrapped(use_source_size
            ? "Template mode imports the entire selected stock FLD at its original dimensions and clears the LEV placements."
            : "Template mode keeps the selected terrain centered, center-cropping smaller maps or extending nearest-edge heights, materials, water and flags into added terrain. Placements are cleared.");
    }
    if(!new_map_error_.empty())ImGui::TextWrapped("Create failed: %s",new_map_error_.c_str());
    // Keep validation independent of BeginDisabled/EndDisabled availability in
    // the game's ImGui SDK and in the minimal compilation stubs.
    if(ImGui::Button("Create",{ui_px(150.0F),0.0F})){
        if(new_dimensions_ok&&!names.empty()){
            if(create_new_map())ImGui::CloseCurrentPopup();
        }else{
            new_map_error_=names.empty()?"No stock compatibility shell is available":
                "Choose legal game-compatible map dimensions";
        }
    }
    ImGui::SameLine();if(ImGui::Button("Cancel",{ui_px(150.0F),0.0F}))ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void EditorApp::draw_load_map_dialog(){
    if(show_load_dialog_){
        refresh_custom_maps();
        // Preserve an error from File > Open map > Custom maps. The normal
        // Load command clears a previous error before reaching this point.
        ImGui::OpenPopup("Load map or project");show_load_dialog_=false;
    }
    if(!ImGui::BeginPopupModal("Load map or project",nullptr,ImGuiWindowFlags_AlwaysAutoResize))return;
    ImGui::TextWrapped("Select a map below, or enter the full path of a .thmap, .pck or .lev file.");
    ImGui::SetNextItemWidth(ui_px(620.0F));ImGui::InputText("File path",load_map_path_,sizeof(load_map_path_));
    ImGui::TextDisabled("Scanned: maps beside the editor, game/maps and game/ThandorMapEditor/maps (including subfolders)");
    if(ImGui::Button("Refresh file list"))refresh_custom_maps();
    ImGui::BeginChild("##custom-map-files",ImVec2(ui_px(630.0F),ui_px(200.0F)),true);
    if(custom_maps_.empty())ImGui::TextDisabled("No loose .lev or .thmap files found.");
    for(const auto& entry:custom_maps_){
        const bool selected=std::filesystem::path(load_map_path_)==entry.path;
        if(ImGui::Selectable(entry.label.c_str(),selected)){
            std::snprintf(load_map_path_,sizeof(load_map_path_),"%s",entry.path.string().c_str());
            load_map_error_.clear();
            // A file row chooses the path; the Load button executes the operation.
            // Do not require IsMouseDoubleClicked (absent from the minimal SDK).
        }
        if(ImGui::IsItemHovered())show_item_tooltip(entry.path.string(),540.0F);
    }
    ImGui::EndChild();
    if(!load_map_error_.empty())ImGui::TextWrapped("Load failed: %s",load_map_error_.c_str());
    if(ImGui::Button("Load selected file",{ui_px(160.0F),0.0F}) && load_map_from_path(load_map_path_))
        ImGui::CloseCurrentPopup();
    ImGui::SameLine();if(ImGui::Button("Cancel",{ui_px(150.0F),0.0F}))ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void EditorApp::draw_save_map_dialog(){
    if(show_save_dialog_){
        save_map_error_.clear();
        if(save_project_path_[0]=='\0'){
            const auto path=project_maps_directory()/(document_->map_name()+".thmap");
            std::snprintf(save_project_path_,sizeof(save_project_path_),"%s",path.string().c_str());
        }
        ImGui::OpenPopup("Save full map project");show_save_dialog_=false;
    }
    if(!ImGui::BeginPopupModal("Save full map project",nullptr,ImGuiWindowFlags_AlwaysAutoResize))return;
    ImGui::SetNextItemWidth(ui_px(620.0F));if(ImGui::InputText("Project path",save_project_path_,sizeof(save_project_path_)))save_map_error_.clear();
    ImGui::TextWrapped("The project stores LEV, FLD, names, and imported terrain textures. Install playable map exports the playable map; Update game resources exports custom textures and text.");
    if(!save_map_error_.empty())ImGui::TextWrapped("Save failed: %s",save_map_error_.c_str());
    if(ImGui::Button("Save",{ui_px(150.0F),0.0F})&&save_project(save_project_path_))ImGui::CloseCurrentPopup();
    ImGui::SameLine();if(ImGui::Button("Cancel",{ui_px(150.0F),0.0F}))ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void EditorApp::draw_material_palette(bool use_asset_filter){
    if(material_images_.empty()){ImGui::TextDisabled("No terrain material images loaded.");return;}
    const int columns=std::max(1,static_cast<int>((ImGui::GetContentRegionAvail().x+ui_px(6.0F))/ui_px(72.0F)));
    const float spacing=ImGui::GetStyle().ItemSpacing.x;
    const float width=(ImGui::GetContentRegionAvail().x-spacing*static_cast<float>(columns-1))/columns;
    const float tile=std::clamp(width,ui_px(36.0F),ui_px(80.0F));
    std::size_t visible_material=0;
    for(std::size_t material=0;material<material_images_.size();++material){
        const std::string label=std::string(1,static_cast<char>('A'+material))+" ("+std::to_string(material)+")";
        if(use_asset_filter&&!contains_ci(label,asset_filter_)&&!contains_ci(material_names_[material],asset_filter_))continue;
        if(visible_material++%columns!=0)ImGui::SameLine();
        ImGui::PushID(static_cast<int>(material));
        const ImVec2 begin=ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("material",{tile,tile+ui_px(18.0F)});
        auto* draw=ImGui::GetWindowDrawList();
        const auto& image=material_images_[material];
        constexpr int samples=24;
        for(int y=0;y<samples;++y)for(int x=0;x<samples;++x){
            const float u=(static_cast<float>(x)+0.5F)/samples;
            const float v=(static_cast<float>(y)+0.5F)/samples;
            const ImU32 color=argb_to_im_u32(sample_wrapped_argb(image,u,v));
            draw->AddRectFilled({begin.x+tile*x/samples,begin.y+tile*y/samples},
                                {begin.x+tile*(x+1)/samples,begin.y+tile*(y+1)/samples},color);
        }
        const ImU32 border=selected_material_==material?IM_COL32(55,165,255,255):IM_COL32(58,78,102,255);
        draw->AddRect(begin,{begin.x+tile,begin.y+tile},border,0.0F,0,selected_material_==material?3.0F:1.0F);

        draw->AddText({begin.x+3.0F,begin.y+tile+2.0F},IM_COL32_WHITE,label.c_str());
        if(ImGui::IsItemClicked()){selected_material_=static_cast<uint8_t>(material);choose_workspace_tool(Tool::Texture,1);}
        if(ImGui::IsItemHovered())show_item_tooltip(label+" — "+material_names_[material]+
            "\nClick to select, then paint with the Texture tool.");
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
    if(const auto* preset=catalog_.army_preset(placement.army_asset_id))
        draw_selected_object_preview(*preset,placement.faction_index);
    ImGui::InputText("Display name",selected_display_name_,sizeof(selected_display_name_));
    ImGui::InputScalar("Asset ID",ImGuiDataType_U32,&selected_edit_asset_id_);
    const uint32_t selected_object_faction=static_cast<uint32_t>(std::clamp(selected_edit_faction_,0,7));
    const uint32_t selected_object_family=graphics_family_for_faction(selected_object_faction);
    const std::string object_faction_preview=selected_object_faction==0U?"Neutral / default":
        faction_single_player_label(selected_object_faction,selected_object_family);
    if(selected_object_faction!=0U)
        ImGui::PushStyleColor(ImGuiCol_Text,graphics_family_ui_text_color(selected_object_family));
    ImGui::SetNextItemWidth(std::max(ui_px(90.0F),ImGui::GetContentRegionAvail().x-ui_px(145.0F)));
    const bool object_faction_combo_open=ImGui::BeginCombo("Faction",object_faction_preview.c_str());
    if(selected_object_faction!=0U)ImGui::PopStyleColor();
    if(object_faction_combo_open){
        for(uint32_t faction=0U;faction<=7U;++faction){
            const uint32_t family=graphics_family_for_faction(faction);
            const std::string label=faction==0U?"Neutral / default":faction_single_player_label(faction,family);
            const bool selected=static_cast<int>(faction)==selected_edit_faction_;
            if(faction!=0U)ImGui::PushStyleColor(ImGuiCol_Text,graphics_family_ui_text_color(family));
            const bool picked=ImGui::Selectable(label.c_str(),selected);
            if(faction!=0U)ImGui::PopStyleColor();
            if(picked)selected_edit_faction_=static_cast<int>(faction);
            if(selected)ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SeparatorText("Transform");
    ImGui::DragFloat3("Position XYZ",selected_edit_position_.data(),0.05F);
    ImGui::DragFloat("Yaw degrees",&selected_edit_yaw_degrees_,0.5F,-3600.0F,3600.0F);
    if(ImGui::IsItemHovered())show_item_tooltip("Z edits the terrain anchor. Stock LEV stores X, Y and yaw.");
    if(ImGui::Button("Apply selected object values",{-1.0F,0.0F})){
        try{
            const auto* preset=catalog_.army_preset(selected_edit_asset_id_);
            if(preset==nullptr)throw std::runtime_error("asset ID is not present in the ARM registry");
            if(preset->has_placement_occupancy_class){
                auto candidate=placement;
                candidate.army_asset_id=selected_edit_asset_id_;
                candidate.set_render_position_q12(thandor::float_to_q12(selected_edit_position_[0]),
                    thandor::float_to_q12(selected_edit_position_[1]));
                if(!placement_has_required_resource_support(*preset,candidate)){
                    const char* support=preset->placement_occupancy_class==0U?"Xenite field (FLD 0x0800)":
                        (preset->placement_occupancy_class==1U?"Tritium field (FLD 0x1000)":"required resource field");
                    throw std::runtime_error(std::string("runtime class 14 extractor requires ")+support+
                        " at the edited position");
                }
            }
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
    if(ImGui::CollapsingHeader("Research and damage")){

    if(preset!=nullptr){
        if(preset->has_placement_occupancy_class){
            const bool support_active=placement_has_required_resource_support(*preset,placement);
            const char* support=preset->placement_occupancy_class==0U?"Xenite":
                (preset->placement_occupancy_class==1U?"Tritium":"resource support");
            if(support_active)
                ImGui::TextColored(ImVec4(0.38F,0.88F,0.48F,1.0F),
                    "Resource field: %s",support);
            else
                ImGui::TextColored(ImVec4(1.0F,0.42F,0.32F,1.0F),
                    "Missing resource field: %s",support);

        }
        draw_faction_research_mask_editor(std::min(placement.faction_index,7U),"selected-object-research");
        auto& tags=document_->placement_tags();
        const auto tag_it=std::find_if(tags.begin(),tags.end(),[&](const auto& tag){
            return tag.placement_index==index;
        });
        if(tag_it!=tags.end()){
            int integrity=static_cast<int>(tag_it->preview_integrity_percent);
            if(ImGui::SliderInt("Integrity / damage preview %",&integrity,0,100)){
                checkpoint();
                tag_it->preview_integrity_percent=static_cast<uint32_t>(std::clamp(integrity,0,100));
                document_->mark_dirty();animate_timed_effects_=true;
                last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
            }
            const auto hierarchy=placement_hierarchy(*preset,index);
            if(preset->browser_kind==thandor::PlacementBrowserKind::Building){
                uint32_t destruction_effect_id=preset->construction_destruction_effect_id;
                for(std::size_t node_index=0U;node_index<hierarchy.size();++node_index){
                    const auto& node=hierarchy[node_index];
                    const bool runtime_root=node.parent_index==std::numeric_limits<uint32_t>::max()||
                        (node.parent_index<hierarchy.size()&&
                         hierarchy[node.parent_index].model_definition_id!=node.model_definition_id);
                    if(runtime_root&&node.simulation.construction_destruction_effect_id!=0U){
                        destruction_effect_id=node.simulation.construction_destruction_effect_id;
                        break;
                    }
                }
                ImGui::SeparatorText("Building destruction preview");

                if(const auto destruction=building_destruction_states_.find(index);
                   destruction!=building_destruction_states_.end()){
                    const auto* definition=destruction->second.effect_id==0U?nullptr:
                        catalog_.effect_definition(destruction->second.effect_id);
                    const uint32_t lifetime=definition?thandor::effect_lifetime_ticks(*definition):0U;
                    const uint32_t age=current_presentation_tick_>=destruction->second.start_tick
                        ?current_presentation_tick_-destruction->second.start_tick:0U;
                    if(lifetime!=0U&&age<lifetime){
                        const float progress=std::clamp(static_cast<float>(age)/lifetime,0.0F,1.0F);
                        ImGui::ProgressBar(progress,{-1.0F,5.0F},"Destruction");
                        ImGui::TextDisabled("%u / %u ticks",
                            age,lifetime);
                    }else{
                        ImGui::TextDisabled("Destruction complete.");
                    }
                    if(ImGui::Button("Restore destroyed building",{-1.0F,0.0F})){
                        building_destruction_states_.erase(index);
                        tag_it->preview_integrity_percent=100U;
                        document_->mark_dirty();
                        simulation_static_replacement_dirty_=true;
                        last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
                        status_="Restored building preview and cleared transient destruction state.";
                    }
                }else if(destruction_effect_id!=0U&&
                         catalog_.effect_definition(destruction_effect_id)!=nullptr){
                    if(ImGui::Button("Preview destruction",{-1.0F,0.0F})){
                        building_destruction_states_[index]={current_presentation_tick_,destruction_effect_id};
                        tag_it->preview_integrity_percent=0U;
                        document_->mark_dirty();
                        animate_timed_effects_=true;
                        simulation_static_replacement_dirty_=true;
                        last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
                        status_="Destruction preview started.";
                    }
                }else{
                    ImGui::TextDisabled("No destruction preview available.");
                }
            }
        }
    }
    }
    if(ImGui::Button("Center camera on selected",{-1.0F,0.0F})){
        camera_.target={placement.render_x(),placement.render_y(),terrain_height_near(placement.render_x(),placement.render_y())};
    }
    if(ImGui::CollapsingHeader("AI / gameplay"))draw_ingame_selection_panel();
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
    ImGui::TextDisabled("Preview production, movement, and combat for this object.");
    ImGui::TextWrapped("Viewport commands: right-click terrain to drive a movable unit; right-click terrain with an armed immobile unit/building to set an attack point; right-click a different-faction placement to attack and follow it. Right-drag still pans the camera.");

    if(simulation_lab_active()){
        ImGui::TextWrapped("Use the Simulation Lab run controls while an experiment is loaded. Clear the experiment to use manual object simulation.");
        ImGui::EndChild();ImGui::PopStyleColor(2);return;
    }
    if(ImGui::Checkbox("Simulation mode · exact 20 Hz",&simulation_mode_)){
        if(simulation_mode_){animate_buildings_=true;animate_units_=true;}
        else reset_simulation(true);
        last_simulation_tick_=current_presentation_tick_;
        last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
    }
    ImGui::Checkbox("Selected armed placement targets exclusively",&simulation_selected_only_);
    ImGui::Checkbox("Show ranges and orders",&simulation_show_ranges_);

    const auto weapon_profiles=placement_weapon_profiles(*preset,index);
    auto found=simulation_actor_states_.find(index);
    if(found!=simulation_actor_states_.end()){
        const auto& actor=found->second;
        const float movement=actor.move_target?1.0F:0.0F;
        ImGui::ProgressBar(movement,{-1.0F,5.0F},actor.move_target?"MOVING":"IDLE");
        ImGui::Text("World %.2f / %.2f / %.2f · body %.1f deg",actor.position.x,actor.position.y,
            actor.position.z,static_cast<float>(actor.body_yaw_turn16)*(360.0F/65536.0F));
    }

    if(preset->runtime_model_class_id==13U){
        ImGui::SeparatorText("Factory production");
        ensure_simulation_economy_initialized();
        const uint32_t faction=placement.faction_index;

        if(faction<simulation_faction_xenite_q4_.size())
            ImGui::Text("Transient faction Xenite: %u Q4 · queue: %zu",
                simulation_faction_xenite_q4_[faction],simulation_secondary_production_queues_[faction].size());
        std::vector<const thandor::ArmyAssetPreset*> compatible;
        std::unordered_set<uint32_t> seen;
        for(const auto& candidate:catalog_.army_presets()){
            if(candidate.registry_id==0U||candidate.browser_kind!=thandor::PlacementBrowserKind::Unit||
               !candidate.browser_visible||!seen.insert(candidate.registry_id).second)continue;
            if(thandor::simulation_class13_candidate_compatible(
                   preset->runtime_class_specific_c4_raw,candidate.army_runtime_flags14_raw))
                compatible.push_back(&candidate);
        }
        auto& selected_candidate=simulation_factory_candidate_selection_[index];
        if(selected_candidate==0U&&!compatible.empty())selected_candidate=compatible.front()->registry_id;
        const auto* selected_production_preset=catalog_.army_preset(selected_candidate);
        const char* preview=selected_production_preset?selected_production_preset->display_name.c_str():"No compatible unit";
        if(ImGui::BeginCombo("Production unit",preview)){
            for(const auto* candidate:compatible){
                const bool selected=candidate->registry_id==selected_candidate;
                std::string label=candidate->display_name+"##factory-"+std::to_string(candidate->registry_id);
                if(ImGui::Selectable(label.c_str(),selected))selected_candidate=candidate->registry_id;
                if(selected)ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        selected_production_preset=catalog_.army_preset(selected_candidate);
        const bool can_queue=selected_production_preset!=nullptr&&faction>0U&&
            faction<simulation_secondary_production_queues_.size();
        if(can_queue&&ImGui::Button("Queue selected unit for faction",{-1.0F,0.0F})){
            simulation_secondary_production_queues_[faction].push_back(selected_candidate);
            simulation_mode_=true;animate_buildings_=true;animate_units_=true;
            // Static texture-array membership must include a transient unit before it can spawn.
            simulation_static_replacement_dirty_=true;
            status_="Queued "+selected_production_preset->display_name+
                " in the recovered faction secondary production queue.";
        }else if(!can_queue){
            ImGui::TextDisabled("No compatible production unit is available for this factory mask.");
        }
        if(selected_production_preset){
            ImGui::TextDisabled("cost %u Q4 · build %u ticks (%.2f s) · active Energy %llu Q4",
                selected_production_preset->retail_xenite_cost_q4,
                selected_production_preset->retail_build_time_q5,
                static_cast<double>(selected_production_preset->retail_build_time_q5)/
                    thandor::kSimulationTicksPerSecond,
                static_cast<unsigned long long>(selected_production_preset->retail_energy_demand_q4));
        }
        if(const auto factory=simulation_factory_states_.find(index);
           factory!=simulation_factory_states_.end()){
            const auto& state=factory->second;
            const char* phase="idle";
            switch(state.phase){
                case thandor::SimulationClass13ProductionPhase::Building: phase="building";break;
                case thandor::SimulationClass13ProductionPhase::Opening: phase="opening";break;
                case thandor::SimulationClass13ProductionPhase::WaitingForProducedUnitClear: phase="drive-out";break;
                case thandor::SimulationClass13ProductionPhase::Closing: phase="closing";break;
                default: break;
            }
            const float progress=state.build_duration_ticks==0U?1.0F:
                std::clamp(static_cast<float>(state.build_elapsed_ticks)/state.build_duration_ticks,0.0F,1.0F);
            if(state.phase==thandor::SimulationClass13ProductionPhase::Building)
                ImGui::ProgressBar(progress,{-1.0F,5.0F},phase);
            ImGui::TextUnformatted(phase);
        }
        const bool custom_drive_target=simulation_production_rally_points_.contains(index);
        if(const auto drive_target=simulation_class13_effective_drive_target(index,*preset)){
            ImGui::Text("Drive target: %.2f / %.2f / %.2f · %s",drive_target->x,drive_target->y,
                drive_target->z,custom_drive_target?"custom":"default");
        }else{
            ImGui::TextDisabled("No default exit point available.");
        }
        if(custom_drive_target&&ImGui::Button("Reset drive target to factory default",{-1.0F,0.0F})){
            simulation_production_rally_points_.erase(index);
            selected_placement_.reset();selected_simulation_lab_entity_.reset();
            last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
            status_="Factory drive target reset to model lookup class 5 / index 1; custom EGATH0 marker cleared and selection reset.";
        }

    }

    if(preset->runtime_model_class_id==22U){
        ImGui::SeparatorText("Airport linked-child storage · runtime class 22");
        ensure_simulation_economy_initialized();
        const uint32_t faction=placement.faction_index;
        const uint32_t capacity=std::min<uint32_t>(preset->runtime_class_specific_c4_raw,
            static_cast<uint32_t>(thandor::kSimulationClass22StoredSlotCount));

        if(faction<simulation_faction_xenite_q4_.size())
            ImGui::Text("Transient faction Xenite: %u Q4 · shared secondary queue: %zu",
                simulation_faction_xenite_q4_[faction],simulation_secondary_production_queues_[faction].size());
        std::vector<const thandor::ArmyAssetPreset*> compatible;
        std::unordered_set<uint32_t> seen;
        for(const auto& candidate:catalog_.army_presets()){
            if(candidate.registry_id==0U||candidate.browser_kind!=thandor::PlacementBrowserKind::Unit||
               !candidate.browser_visible||!seen.insert(candidate.registry_id).second)continue;
            if(thandor::simulation_class22_candidate_compatible(candidate.army_runtime_flags14_raw))
                compatible.push_back(&candidate);
        }
        auto& selected_candidate=simulation_airport_candidate_selection_[index];
        if(selected_candidate==0U&&!compatible.empty())selected_candidate=compatible.front()->registry_id;
        const auto* selected_storage_preset=catalog_.army_preset(selected_candidate);
        const char* preview=selected_storage_preset?selected_storage_preset->display_name.c_str():"No compatible secondary asset";
        if(ImGui::BeginCombo("Stored secondary asset",preview)){
            for(const auto* candidate:compatible){
                const bool selected=candidate->registry_id==selected_candidate;
                std::string label=candidate->display_name+"##airport-store-"+std::to_string(candidate->registry_id);
                if(ImGui::Selectable(label.c_str(),selected))selected_candidate=candidate->registry_id;
                if(selected)ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        selected_storage_preset=catalog_.army_preset(selected_candidate);
        const bool can_queue=selected_storage_preset!=nullptr&&capacity!=0U&&faction>0U&&
            faction<simulation_secondary_production_queues_.size();
        if(can_queue&&ImGui::Button("Queue selected secondary asset for Airport storage",{-1.0F,0.0F})){
            simulation_secondary_production_queues_[faction].push_back(selected_candidate);
            simulation_mode_=true;animate_buildings_=true;animate_units_=true;
            status_="Queued "+selected_storage_preset->display_name+
                " in the recovered faction secondary production queue for class-22 storage.";
        }else if(!can_queue){
            ImGui::TextDisabled("No compatible storable secondary asset is available, or this Airport has zero slots.");
        }
        if(selected_storage_preset){
            ImGui::TextDisabled("cost %u Q4 · build %u ticks (%.2f s) · active Energy %llu Q4",
                selected_storage_preset->retail_xenite_cost_q4,
                selected_storage_preset->retail_build_time_q5,
                static_cast<double>(selected_storage_preset->retail_build_time_q5)/
                    thandor::kSimulationTicksPerSecond,
                static_cast<unsigned long long>(selected_storage_preset->retail_energy_demand_q4));
        }
        if(const auto airport=simulation_airport_states_.find(index);
           airport!=simulation_airport_states_.end()){
            const auto& state=airport->second;
            ImGui::Text("Stored linked assets: %u / %u · available: %u",state.stored_slot_count,
                state.storage_capacity,state.available_stored_count);
            if(state.build_phase==thandor::SimulationClass22BuildPhase::Building){
                const float progress=state.build_duration_ticks==0U?1.0F:
                    std::clamp(static_cast<float>(state.build_elapsed_ticks)/state.build_duration_ticks,0.0F,1.0F);
                ImGui::ProgressBar(progress,{-1.0F,5.0F},"building stored asset");

            }
            std::string slots;
            for(std::size_t slot=0;slot<state.storage_capacity;++slot){
                const uint32_t id=state.stored_army_asset_ids[slot];
                if(id==0U)continue;
                if(!slots.empty())slots += ", ";
                const auto* stored=catalog_.army_preset(id);
                slots += std::to_string(slot)+":"+(stored?stored->display_name:std::to_string(id));
            }
            if(!slots.empty())ImGui::TextWrapped("Persistent slots: %s",slots.c_str());
        }
        ImGui::SeparatorText("Linked class-21 sortie");
        auto& selected_lane=simulation_airport_launch_lane_selection_[index];
        selected_lane=std::min(selected_lane,2U);
        const char* lane_names[]={"Lane 0 · ARM 240 · Crow","Lane 1 · ARM 242 · Hawk",
            "Lane 2 · ARM 244 · Eagle"};
        int lane_value=static_cast<int>(selected_lane);
        if(ImGui::Combo("Stored launch lane",&lane_value,lane_names,3))
            selected_lane=static_cast<uint32_t>(std::clamp(lane_value,0,2));
        const uint32_t launch_asset=thandor::kSimulationClass22LinkedArmyAssetIds[selected_lane];
        uint32_t persistent_count=0U,free_count=0U;
        if(const auto airport=simulation_airport_states_.find(index);airport!=simulation_airport_states_.end()){
            const auto& state=airport->second;
            for(std::size_t slot=0;slot<state.storage_capacity;++slot){
                if(state.stored_army_asset_ids[slot]!=launch_asset)continue;
                ++persistent_count;
                if((state.occupied_slot_mask&(1U<<slot))==0U)++free_count;
            }
        }
        ImGui::TextDisabled("persistent %u · currently free %u · stored slot is reusable after return",
            persistent_count,free_count);
        auto& heading_degrees=simulation_airport_mission_heading_degrees_[index];
        if(simulation_target_point_&&heading_degrees==0.0F){
            heading_degrees=std::atan2(simulation_target_point_->y-placement.render_y(),
                simulation_target_point_->x-placement.render_x())*(180.0F/std::numbers::pi_v<float>);
        }
        ImGui::DragFloat("Mission pass heading (deg)",&heading_degrees,0.5F,-3600.0F,3600.0F);
        if(simulation_target_point_)
            ImGui::TextDisabled("Mission point: %.2f, %.2f",
                simulation_target_point_->x,simulation_target_point_->y);
        else
            ImGui::TextDisabled("Choose a target point first.");
        const bool launch_ready=simulation_target_point_.has_value()&&free_count>0U;
        if(launch_ready&&ImGui::Button("Launch sortie",{-1.0F,0.0F})){
            auto& state=simulation_airport_states_[index];
            const uint32_t heading=thandor::radians_to_turn16(heading_degrees*
                (std::numbers::pi_v<float>/180.0F));
            const auto command=thandor::simulation_class22_world_launch_command(heading,
                thandor::float_to_q12(simulation_target_point_->x),
                thandor::float_to_q12(simulation_target_point_->y));
            thandor::simulation_class22_queue_launch_mask(state,1U<<selected_lane,
                command.value_a,command.value_b,command.value_c);
            simulation_mode_=true;animate_buildings_=true;animate_units_=true;
            simulation_static_replacement_dirty_=true;
            status_="Queued class-22 lane "+std::to_string(selected_lane)+
                " linked-child sortie without consuming its persistent stored ARM id.";
        }else if(!launch_ready){
            ImGui::TextDisabled("Launch requires a free matching 240/242/244 stored slot and a Simulation target point.");
        }

    }

    if(!weapon_profiles.empty()){
        ImGui::SeparatorText("Weapons");

        for(std::size_t weapon_index=0;weapon_index<weapon_profiles.size();++weapon_index){
            const auto& weapon=weapon_profiles[weapon_index];
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
            ImGui::TextUnformatted(weapon_title.empty()?"Weapon":weapon_title.c_str());
            ImGui::PushID(static_cast<int>(weapon_index));

            ImGui::TextDisabled("Range %.2f · Reload %.2f s",
                thandor::q12_to_float(weapon.maximum_range_q12),
                static_cast<float>(weapon.reload_ticks)/thandor::kSimulationTicksPerSecond);
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
            tool_=Tool::SimulationTarget;simulation_mode_=true;animate_buildings_=true;animate_units_=true;
            status_="Click terrain to assign a point target to the selected armed placement.";
        }
        if(preset->browser_kind==thandor::PlacementBrowserKind::Building&&
           ImGui::Button("Bake whole placement facing toward target",{-1.0F,0.0F}))
            bake_selected_target_facing();

    }

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

        ImGui::Checkbox("Show route / 4x pathing cells",&v519_path_diagnostics_);
        if(ImGui::Button("Drive selected unit to terrain point",{-1.0F,0.0F})){
            activate_fog_on_driving_order(placement.faction_index);
            tool_=Tool::SimulationMove;simulation_mode_=true;animate_buildings_=true;animate_units_=true;
            status_="Click terrain to issue a movement destination to the selected unit.";
        }
        if(ImGui::Button("Stop selected unit",{-1.0F,0.0F})){
            auto& actor=simulation_state_for(index);
            actor.move_target.reset();actor.move_waypoint.reset();
        }
        ImGui::TextWrapped("Movement is a preview. Use Finish driving to store this unit's current position in the map.");
        if(ImGui::Button("Finish driving at current map position",{-1.0F,0.0F}))
            commit_selected_simulation_position();
    }
    if(ImGui::Button("Reset selected transient simulation",{-1.0F,0.0F})){
        simulation_attack_placement_targets_.erase(index);
        simulation_attack_point_targets_.erase(index);
        if(simulation_actor_states_.erase(index)!=0U){
            simulation_runtime_factions_.erase(index);
            ++simulation_runtime_owner_revision_;
            simulation_static_replacement_dirty_=true;
        }
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
    const auto& placement=document_->level().placements()[*selected_placement_];
    const Vec3f origin{placement.render_x(),placement.render_y(),
        terrain_height_near(placement.render_x(),placement.render_y())+0.18F};
    const float scale=std::clamp(camera_.distance*0.055F,1.25F,8.0F);
    const auto canvas=workspace_layout().canvas;
    auto projection=project_transform_gizmo(origin,camera_.view_projection(canvas.aspect()),
        canvas.width,canvas.height,scale);
    const auto offset=[&](PreviewVec2& p){p.x+=canvas.x;p.y+=canvas.y;};
    offset(projection.origin);offset(projection.x_axis);offset(projection.y_axis);offset(projection.z_axis);
    for(auto& p:projection.rotation_ring)offset(p);
    return projection;
}

GizmoHandle EditorApp::hit_test_gizmo(PreviewVec2 mouse) const{
    if(tool_!=Tool::Select||workspace_transform_action_==0||simulation_lab_active())return GizmoHandle::None;
    const auto projection=selected_gizmo_projection();
    if(!projection.visible)return GizmoHandle::None;
    if(gizmo_mode_==GizmoMode::Rotate){
        return rotation_ring_distance(mouse,projection.rotation_ring)<ui_px(8.0F)?
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
    return best!=candidates.end()&&best->distance<ui_px(8.0F)?best->handle:GizmoHandle::None;
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
        const auto heading=thandor::placement_heading_after_pointer_delta(
            gizmo_drag_.placement_start.orientation_turn16,
            static_cast<int32_t>(std::lround(mouse.x-gizmo_drag_.mouse_start.x)));
        changed=placement.orientation_turn16!=heading;
        placement.orientation_turn16=heading;
        if(auto actor=simulation_actor_states_.find(*selected_placement_);actor!=simulation_actor_states_.end())
            actor->second.body_yaw_turn16=heading;
    }
    if(changed){
        document_->mark_dirty();
        rebuild_objects();
        status_=gizmo_drag_.handle==GizmoHandle::RotateZ?
            "Rotated selected placement yaw.":"Moved selected placement on a world axis.";
    }
}

void EditorApp::draw_transform_gizmo(){
    if(tool_!=Tool::Select||workspace_transform_action_==0||simulation_lab_active())return;
    if(!document_||!selected_placement_||*selected_placement_>=document_->level().placements().size())return;
    const auto projection=selected_gizmo_projection();
    if(!projection.visible)return;
    // Keep authoring handles above world/debug overlays; the frame clips this list to the canvas.
    auto* draw=ImGui::GetForegroundDrawList();
    const auto active=[&](GizmoHandle handle){return gizmo_drag_.handle==handle||gizmo_hovered_==handle;};
    if(gizmo_mode_==GizmoMode::Translate){
        const auto axis=[&](PreviewVec2 end,ImU32 base,ImU32 highlight,const char* label,GizmoHandle handle){
            const ImU32 color=active(handle)?highlight:base;
            const ImVec2 start{projection.origin.x,projection.origin.y},tip{end.x,end.y};
            // A dark outline keeps the axes legible over snow, lava and bright selection effects.
            draw->AddLine(start,tip,IM_COL32(15,18,24,230),active(handle)?ui_px(7.0F):ui_px(6.0F));
            draw->AddLine(start,tip,color,active(handle)?ui_px(4.0F):ui_px(3.0F));
            const float dx=end.x-projection.origin.x,dy=end.y-projection.origin.y;
            const float length=std::hypot(dx,dy);
            if(length>1.0F){
                const float ux=dx/length,uy=dy/length;
                const float size=active(handle)?ui_px(14.0F):ui_px(12.0F);
                const ImVec2 left{end.x-ux*size-uy*size*0.45F,end.y-uy*size+ux*size*0.45F};
                const ImVec2 right{end.x-ux*size+uy*size*0.45F,end.y-uy*size-ux*size*0.45F};
                draw->AddTriangleFilled(tip,left,right,color);
                draw->AddTriangle(tip,left,right,IM_COL32(15,18,24,230),1.5F);
            }else draw->AddCircleFilled(tip,active(handle)?ui_px(6.0F):ui_px(5.0F),color,16);
            draw->AddText({end.x+ui_px(6.0F),end.y-ui_px(7.0F)},IM_COL32(15,18,24,255),label);
            draw->AddText({end.x+ui_px(5.0F),end.y-ui_px(8.0F)},color,label);
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
        draw->AddText({projection.origin.x+ui_px(8.0F),projection.origin.y+ui_px(8.0F)},color,"Yaw");
    }
    draw->AddCircleFilled({projection.origin.x,projection.origin.y},4.0F,IM_COL32(245,245,245,255),12);
}

void EditorApp::draw_drag_drop_preview(){
    if(!drag_preview_asset_id_||!drag_preview_cell_)return;
    const auto [cell_x,cell_y]=*drag_preview_cell_;
    if(cell_x>=document_->field().width()||cell_y>=document_->field().height())return;
    const auto& cell=document_->field().at(cell_x,cell_y);
    const Vec3f point{thandor::q12_to_float(cell.world_x_q12),thandor::q12_to_float(cell.world_y_q12),
                      cell.terrain_height()+0.12F};
    float screen_x=0.0F,screen_y=0.0F,depth=0.0F;
    const auto vp=camera_.view_projection(workspace_layout().canvas.aspect());
    if(!project_to_canvas(point,vp,screen_x,screen_y,depth))return;
    const auto* preset=catalog_.army_preset(*drag_preview_asset_id_);
    const bool valid=preset!=nullptr&&
        catalog_.placement_compatibility_diagnostic(*preset,document_->level()).empty()&&
        factory_placement_exit_clear(*preset,cell_x,cell_y,selected_orientation_)&&
        (!preset->has_placement_occupancy_class||
         !document_->field().terrain_rejects_placement_occupancy_class(
             cell_x,cell_y,preset->placement_occupancy_class));
    const ImU32 color=valid?IM_COL32(120,235,150,255):IM_COL32(245,95,85,255);
    auto* draw=ImGui::GetBackgroundDrawList();
    draw->AddCircle({screen_x,screen_y},14.0F,color,24,3.0F);
    draw->AddLine({screen_x-18.0F,screen_y},{screen_x+18.0F,screen_y},color,2.0F);
    draw->AddLine({screen_x,screen_y-18.0F},{screen_x,screen_y+18.0F},color,2.0F);

    // Retail placement keeps one live heading for both the preview runtime and the commit path.
    // Visualize that exact heading while the normal placement button is held to rotate.
    const float heading=thandor::turn16_to_radians(selected_orientation_&0xFFFFU);
    const Vec3f direction_point{point.x+std::cos(heading)*3.0F,
                                point.y+std::sin(heading)*3.0F,point.z};
    float dir_x=0.0F,dir_y=0.0F,dir_depth=0.0F;
    if(project_to_canvas(direction_point,vp,
                         dir_x,dir_y,dir_depth)){
        draw->AddLine({screen_x,screen_y},{dir_x,dir_y},color,3.0F);
        draw->AddCircleFilled({dir_x,dir_y},4.0F,color,12);
    }
    if(preset){
        draw->AddText({screen_x+18.0F,screen_y-20.0F},IM_COL32_WHITE,preset->display_name.c_str());
        char heading_text[96];
        std::snprintf(heading_text,sizeof(heading_text),
            "heading 0x%04X%s",selected_orientation_&0xFFFFU,
            placement_rotation_capture_active_?" · rotating · release to place":" · hold LMB + move horizontally");
        draw->AddText({screen_x+18.0F,screen_y-4.0F},color,heading_text);
        if(!valid&&preset->has_placement_occupancy_class){
            const char* reason=preset->placement_occupancy_class==0U?"requires Xenite support":
                (preset->placement_occupancy_class==1U?"requires Tritium support":
                 "requires matching resource support");
            draw->AddText({screen_x+18.0F,screen_y+12.0F},color,reason);
        }
    }
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
    const auto canvas=workspace_layout().canvas;
    draw->PushClipRect({canvas.x,canvas.y},{canvas.x+canvas.width,canvas.y+canvas.height},true);
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
    if(ctrl&&!io.WantTextInput&&ImGui::IsKeyPressed(ImGuiKey_N,false))show_new_dialog_=true;
    if(ctrl&&!io.WantTextInput&&ImGui::IsKeyPressed(ImGuiKey_O,false)){load_map_error_.clear();show_load_dialog_=true;}
    if(ctrl&&!io.WantTextInput&&ImGui::IsKeyPressed(ImGuiKey_Z,false)){if(generator_preview_){discard_terrain_generator_preview();generator_message_="Preview discarded.";}else if(io.KeyShift)redo();else undo();}
    if(ctrl&&!io.WantTextInput&&ImGui::IsKeyPressed(ImGuiKey_Y,false))redo();
    if(ctrl&&!io.WantTextInput&&ImGui::IsKeyPressed(ImGuiKey_E,false))export_game_level_package();
    if(ctrl&&ImGui::IsKeyPressed(ImGuiKey_S,false)&&!io.WantTextInput){
        if(current_stock_group_)save_current_map();
        else if(current_project_path_.empty())show_save_dialog_=true;
        else save_project(current_project_path_);
    }
    if(ctrl&&ImGui::IsKeyPressed(ImGuiKey_D,false)&&!io.WantTextInput){
        if(water_selection_count()!=0U)clear_water_selection();
        if(tool_==Tool::GenerateTerrain){discard_terrain_generator_preview();reset_terrain_generator();}
        selected_placement_.reset();selected_simulation_lab_entity_.reset();
    }

    if(!io.WantCaptureKeyboard&&!io.WantTextInput&&!ImGui::IsAnyItemActive()){
        if(!ctrl&&ImGui::IsKeyPressed(ImGuiKey_W,false)){gizmo_mode_=GizmoMode::Translate;workspace_transform_action_=1;tool_=Tool::Select;}
        if(!ctrl&&ImGui::IsKeyPressed(ImGuiKey_E,false)){gizmo_mode_=GizmoMode::Rotate;workspace_transform_action_=2;tool_=Tool::Select;}
        if(ctrl&&ImGui::IsKeyPressed(ImGuiKey_Delete,false)&&selected_placement_)delete_selected_placement();
        const float camera_step=std::min(io.DeltaTime,0.25F)*20.0F;
        constexpr float key_angle=1024.0F*(2.0F*std::numbers::pi_v<float>/65536.0F);
        if(!ctrl){
            float heading=0.F,tilt=0.F;
            if(ImGui::IsKeyDown(ImGuiKey_Insert))heading-=key_angle*camera_step;
            if(ImGui::IsKeyDown(ImGuiKey_Delete))heading+=key_angle*camera_step;
            if(ImGui::IsKeyDown(ImGuiKey_PageUp))tilt+=key_angle*camera_step;
            if(ImGui::IsKeyDown(ImGuiKey_PageDown))tilt-=key_angle*camera_step;
            camera_.orbit(heading,tilt);
            float zoom=0.F;
            if(ImGui::IsKeyDown(ImGuiKey_Home))zoom-=0.5F*camera_step;
            if(ImGui::IsKeyDown(ImGuiKey_End))zoom+=0.5F*camera_step;
            camera_.distance=std::clamp(camera_.distance+zoom,2.F,500.F);
        }
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
    if(camera_turn_drag_active_){
        if(ImGui::IsMouseReleased(ImGuiMouseButton_Left)||!ImGui::IsMouseDown(ImGuiMouseButton_Left)){
            camera_turn_drag_active_=false;return;
        }
        game_cursor_frame_=GameCursorAsset::kCameraTurnFrame;
        camera_.orbit(-(io.MousePos.x-camera_turn_capture_center_.x)*game_camera_turn_radians_per_pixel(),
            (io.MousePos.y-camera_turn_capture_center_.y)*game_camera_turn_radians_per_pixel());
        glfwSetCursorPos(window_,camera_turn_capture_center_.x,camera_turn_capture_center_.y);
        return;
    }
    // Once retail-style right-drag capture is active, consume the anchored delta before the
    // ordinary canvas-bounds/UI-capture guard. Otherwise a single large mouse event could escape
    // the canvas and cancel continuous panning before the pointer is restored to its anchor.
    if(right_pointer_capture_active_&&right_pointer_dragged_&&
       ImGui::IsMouseDown(ImGuiMouseButton_Right)){
        game_cursor_frame_=GameCursorAsset::kCameraMoveFrame;
        const float delta_x=io.MousePos.x-right_pointer_capture_center_.x;
        const float delta_y=io.MousePos.y-right_pointer_capture_center_.y;
        const float scale=game_camera_pan_world_units_per_pixel(camera_.distance);
        if(io.KeyCtrl)camera_.orbit(0.F,delta_y*game_camera_turn_radians_per_pixel());
        else if(io.KeyAlt)camera_.distance=std::clamp(camera_.distance-delta_y*(64.F/4096.F),2.F,500.F);
        else if(io.KeyShift||ImGui::IsMouseDown(ImGuiMouseButton_Left))
            camera_.orbit(-delta_x*game_camera_turn_radians_per_pixel(),
                io.KeyShift?0.F:delta_y*game_camera_turn_radians_per_pixel());
        else if(delta_x!=0.0F||delta_y!=0.0F)
            camera_.move_planar(-delta_x*scale,delta_y*scale);
        glfwSetCursorPos(window_,right_pointer_capture_center_.x,right_pointer_capture_center_.y);
        return;
    }

    if(simulation_lab_run_selector_hovered_&&!right_pointer_capture_active_)return;

    if(ImGui::IsMouseClicked(ImGuiMouseButton_Right)&&mouse_in_canvas(io.MousePos.x,io.MousePos.y)&&!io.WantCaptureMouse&&
       (tool_!=Tool::EffectScenery||io.KeyCtrl||io.KeyAlt||io.KeyShift)){
        right_pointer_pressed_=true;right_pointer_dragged_=false;
        right_pointer_capture_active_=false;
        right_pointer_press_={io.MousePos.x,io.MousePos.y};
    }
    if(right_pointer_pressed_&&ImGui::IsMouseDown(ImGuiMouseButton_Right)){
        const float drag_distance=std::hypot(io.MousePos.x-right_pointer_press_.x,
                                             io.MousePos.y-right_pointer_press_.y);
        if(drag_distance>=4.0F&&!right_pointer_dragged_){
            right_pointer_dragged_=true;
            right_pointer_capture_active_=true;
            const auto canvas=workspace_layout().canvas;
            right_pointer_capture_center_={canvas.x+canvas.width*0.5F,canvas.y+canvas.height*0.5F};
            // Retail pointer-drag controls repeatedly restore the pointer anchor after consuming
            // the delta. Capture at canvas center so panning remains continuous at screen edges.
            glfwSetCursorPos(window_,right_pointer_capture_center_.x,right_pointer_capture_center_.y);
            game_cursor_frame_=GameCursorAsset::kCameraMoveFrame;
            return;
        }
    }
    if(ImGui::IsMouseReleased(ImGuiMouseButton_Right)&&right_pointer_pressed_){
        const bool issue_order=!right_pointer_dragged_;
        right_pointer_pressed_=false;right_pointer_dragged_=false;
        right_pointer_capture_active_=false;
        if(issue_order&&tool_==Tool::Select){issue_context_order_at_pointer(io.MousePos.x,io.MousePos.y);return;}
    }
    drag_preview_asset_id_.reset();
    const auto placement_cell_valid=[&](uint32_t asset_id,
                                        const std::optional<std::pair<uint32_t,uint32_t>>& cell,bool check_exit=true){
        if(!cell)return false;
        const auto* preset=catalog_.army_preset(asset_id);
        if(preset==nullptr)return false;
        const auto [cell_x,cell_y]=*cell;
        if(cell_x>=document_->field().width()||cell_y>=document_->field().height())return false;
        if(!catalog_.placement_compatibility_diagnostic(*preset,document_->level()).empty())return false;
        if(check_exit&&!factory_placement_exit_clear(*preset,cell_x,cell_y,selected_orientation_))return false;
        return !preset->has_placement_occupancy_class||
            !document_->field().terrain_rejects_placement_occupancy_class(
                cell_x,cell_y,preset->placement_occupancy_class);
    };
    const auto rotate_pending_building=[&](){
        if(ImGui::IsMouseDown(ImGuiMouseButton_Right)){
            placement_rotation_pointer_anchor_={io.MousePos.x,io.MousePos.y};
            placement_rotation_cell_=pick_cell(io.MousePos.x,io.MousePos.y);return;
        }
        const int32_t delta_x=static_cast<int32_t>(std::lround(io.MousePos.x-placement_rotation_pointer_anchor_.x));
        if(delta_x!=0)selected_orientation_=thandor::placement_heading_after_pointer_delta(
            selected_orientation_,delta_x);
        drag_preview_cell_=placement_rotation_cell_;
        glfwSetCursorPos(window_,placement_rotation_pointer_anchor_.x,placement_rotation_pointer_anchor_.y);
    };

    if(const ImGuiPayload* payload=ImGui::GetDragDropPayload();
       payload&&payload->IsDataType(kAssetDragPayloadType)&&
       payload->DataSize==static_cast<int>(sizeof(AssetDragPayload))){
        const auto drag=*static_cast<const AssetDragPayload*>(payload->Data);
        drag_preview_asset_id_=drag.registry_id;
        const auto* preset=catalog_.army_preset(drag.registry_id);
        const bool building=preset!=nullptr&&
            preset->browser_kind==thandor::PlacementBrowserKind::Building;
        if(mouse_in_canvas(io.MousePos.x,io.MousePos.y))
            drag_preview_cell_=pick_cell(io.MousePos.x,io.MousePos.y);
        else drag_preview_cell_.reset();

        // Retail placement rotates the live heading while the normal placement button remains
        // held over a valid build target. RIGHT suppresses rotation; it is not the rotate button.
        // For drag/drop, begin capture only after the payload first reaches a valid building cell,
        // so motion from the asset browser into the world does not accidentally spin the preview.
        if(building&&!placement_rotation_capture_active_&&
           placement_cell_valid(drag.registry_id,drag_preview_cell_,false)&&
           ImGui::IsMouseDown(ImGuiMouseButton_Left)){
            placement_rotation_capture_active_=true;
            placement_rotation_pointer_anchor_={io.MousePos.x,io.MousePos.y};placement_rotation_cell_=drag_preview_cell_;
        }else if(building&&placement_rotation_capture_active_&&
                 ImGui::IsMouseDown(ImGuiMouseButton_Left)){
            rotate_pending_building();
        }

        if(ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
            if(building&&placement_rotation_capture_active_)drag_preview_cell_=placement_rotation_cell_;
            if(placement_cell_valid(drag.registry_id,drag_preview_cell_))
                add_asset_placement(drag_preview_cell_->first,drag_preview_cell_->second,drag.registry_id);
            else status_="Placement release is not a valid build target; nothing was placed.";
            drag_preview_asset_id_.reset();
            drag_preview_cell_.reset();
            placement_rotation_capture_active_=false;
            return;
        }
        return;
    }

    // Clicking a building card enters the same game-style placement mode without requiring a
    // drag from the browser. Press on a valid build target, hold to rotate from horizontal pointer
    // motion, and commit at the terrain cell under the mouse on button release.
    const auto* selected_placement_preset=catalog_.army_preset(selected_asset_id_);
    const bool selected_building_placement=tool_==Tool::EffectScenery&&
        selected_placement_preset!=nullptr&&
        selected_placement_preset->browser_kind==thandor::PlacementBrowserKind::Building;
    if(selected_building_placement){
        drag_preview_asset_id_=selected_asset_id_;
        if(mouse_in_canvas(io.MousePos.x,io.MousePos.y))
            drag_preview_cell_=pick_cell(io.MousePos.x,io.MousePos.y);
        else drag_preview_cell_.reset();

        if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
            if(placement_cell_valid(selected_asset_id_,drag_preview_cell_,false)){
                placement_rotation_capture_active_=true;
                placement_rotation_pointer_anchor_={io.MousePos.x,io.MousePos.y};placement_rotation_cell_=drag_preview_cell_;
                status_="Building placement captured; hold left and move horizontally to rotate, then release to place.";
            }else{
                placement_rotation_capture_active_=false;
                status_="Building placement requires a valid build target before rotation capture begins.";
            }
            return;
        }
        if(placement_rotation_capture_active_&&ImGui::IsMouseDown(ImGuiMouseButton_Left)){
            rotate_pending_building();
            return;
        }
        if(placement_rotation_capture_active_&&ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
            const auto release_cell=placement_rotation_cell_;
            if(placement_cell_valid(selected_asset_id_,release_cell))
                add_asset_placement(release_cell->first,release_cell->second,selected_asset_id_);
            else status_="Building release position is not a valid build target; nothing was placed.";
            placement_rotation_capture_active_=false;
            drag_preview_asset_id_.reset();
            drag_preview_cell_.reset();
            return;
        }
        // Placement mode owns the left pointer until commit; ordinary selection/camera-turn logic
        // must not consume the same hold. RIGHT remains available only as a rotation suppressor.
        return;
    }

    drag_preview_cell_.reset();
    placement_rotation_capture_active_=false;

    if(gizmo_drag_.handle!=GizmoHandle::None){
        if(ImGui::IsMouseDown(ImGuiMouseButton_Left))
            update_active_gizmo({io.MousePos.x,io.MousePos.y});
        if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))gizmo_drag_=GizmoDragState{};
        return;
    }

    gizmo_hovered_=mouse_in_canvas(io.MousePos.x,io.MousePos.y)&&!io.WantCaptureMouse?
        hit_test_gizmo({io.MousePos.x,io.MousePos.y}):GizmoHandle::None;
    if(tool_==Tool::Select&&workspace_transform_action_==2&&selected_placement_&&
       mouse_in_canvas(io.MousePos.x,io.MousePos.y)&&!io.WantCaptureMouse&&ImGui::IsMouseClicked(ImGuiMouseButton_Left)&&pick_placement(io.MousePos.x,io.MousePos.y)==selected_placement_)
        gizmo_hovered_=GizmoHandle::RotateZ;
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

    if(tool_==Tool::GenerateTerrain)update_terrain_generator_selection();
    if(io.WantCaptureMouse||!mouse_in_canvas(io.MousePos.x,io.MousePos.y)){
        hovered_placement_.reset();hovered_terrain_point_.reset();
        hovered_context_order_=thandor::ContextOrderKind::None;
        if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))camera_turn_drag_active_=false;
        if(ImGui::IsMouseReleased(ImGuiMouseButton_Right)){
            right_pointer_pressed_=false;right_pointer_dragged_=false;
            right_pointer_capture_active_=false;
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

    if(ImGui::IsMouseDragging(ImGuiMouseButton_Middle)){
        game_cursor_frame_=GameCursorAsset::kCameraMoveFrame;
        const float scale=game_camera_pan_world_units_per_pixel(camera_.distance);
        camera_.move_planar(-io.MouseDelta.x*scale,io.MouseDelta.y*scale);
        return;
    }
    if(tool_==Tool::Select&&workspace_transform_action_==0&&ImGui::IsMouseDragging(ImGuiMouseButton_Left,4.0F)){
        game_cursor_frame_=GameCursorAsset::kCameraTurnFrame;
        camera_turn_drag_active_=true;
        const auto canvas=workspace_layout().canvas;
        camera_turn_capture_center_={canvas.x+canvas.width*0.5F,canvas.y+canvas.height*0.5F};
        glfwSetCursorPos(window_,camera_turn_capture_center_.x,camera_turn_capture_center_.y);
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

    if(tool_==Tool::GenerateTerrain)return;

    // Object selection is independent of the terrain ray. Large buildings are selected through
    // their complete resolved-model bounds, including roofs and linked children, rather than a
    // small screen-space circle around the placement root.
    if(tool_==Tool::Select&&ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
        auto authored=pick_placement_hit(io.MousePos.x,io.MousePos.y);
        if(simulation_lab_active()){
            const auto runs=simulation_lab_runs();
            if(!runs.empty()){
                const auto run_index=static_cast<size_t>(std::clamp(simulation_lab_selected_run_,0,int(runs.size())-1));
                // Old LEV hulls must not steal clicks at a moving owner's spawn point.
                if(authored&&std::any_of(runs[run_index].entities.begin(),runs[run_index].entities.end(),
                    [&](const auto& e){return e.source_placement_index==authored->placement_index;}))authored.reset();
                if(const auto live=pick_simulation_lab_entity(io.MousePos.x,io.MousePos.y);
                   live&&(!authored||live->second<authored->ray_distance)){
                    selected_placement_.reset();selected_simulation_lab_entity_.reset();
                    selected_simulation_lab_entity_=std::pair{run_index,live->first};
                    return;
                }
            }
        }
        selected_simulation_lab_entity_.reset();
        selected_placement_=authored?std::optional<size_t>{authored->placement_index}:std::nullopt;
        return;
    }
    if((tool_==Tool::SimulationTarget||tool_==Tool::SimulationMove)&&
       ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
        const auto ray=canvas_view_ray(io.MousePos.x,io.MousePos.y);
        const auto hit=pick_terrain_cell(document_->field(),ray);
        if(!hit){status_="Simulation order missed the terrain.";return;}
        if(tool_==Tool::SimulationTarget){
            bool assigned_selected=false;
            if(selected_placement_&&*selected_placement_<document_->level().placements().size()){
                const auto& selected=document_->level().placements()[*selected_placement_];
                if(const auto* preset=catalog_.army_preset(selected.army_asset_id);
                   preset&&!placement_weapon_profiles(*preset,*selected_placement_).empty()){
                    simulation_attack_point_targets_[*selected_placement_]=hit->position;
                    simulation_attack_placement_targets_.erase(*selected_placement_);
                    auto& actor=simulation_state_for(*selected_placement_);
                    actor.move_target.reset();actor.move_waypoint.reset();
                    if(preset->browser_kind==thandor::PlacementBrowserKind::Building)animate_buildings_=true;
                    if(preset->browser_kind==thandor::PlacementBrowserKind::Unit)animate_units_=true;
                    assigned_selected=true;
                }
            }
            if(!assigned_selected)simulation_target_point_=hit->position;
            simulation_mode_=true;animate_buildings_=true;animate_units_=true;
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
            actor.move_waypoint.reset();
            activate_fog_on_driving_order(placement.faction_index);
            simulation_mode_=true;animate_buildings_=true;animate_units_=true;
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
    const auto ray=canvas_view_ray(mouse_x,mouse_y);
    const auto hit=pick_terrain_cell(document_->field(),ray);
    if(!hit)return std::nullopt;
    return std::pair<uint32_t,uint32_t>{hit->cell_x,hit->cell_y};
}

std::optional<EditorApp::PlacementPickHit> EditorApp::pick_placement_hit(
    float mouse_x,float mouse_y) const{
    if(!document_)return std::nullopt;
    const auto placements=document_->level().placements();
    if(placement_pick_bounds_.size()!=placements.size())return std::nullopt;
    std::vector<bool> runtime_owned;
    if(simulation_lab_active()){
        runtime_owned.resize(placements.size());
        const auto runs=simulation_lab_runs();
        if(!runs.empty()){
            const auto& run=runs[static_cast<size_t>(std::clamp(simulation_lab_selected_run_,0,int(runs.size())-1))];
            for(const auto& entity:run.entities)
                if(entity.source_placement_index<runtime_owned.size())runtime_owned[entity.source_placement_index]=true;
        }
    }
    const auto ray=canvas_view_ray(mouse_x,mouse_y);
    const auto terrain_hit=pick_terrain_cell(document_->field(),ray);
    float nearest=std::numeric_limits<float>::max();
    std::optional<std::size_t> selected;
    for(std::size_t index=0;index<placement_pick_bounds_.size();++index){
        if(!runtime_owned.empty()&&runtime_owned[index])continue;
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
    if(!selected)return std::nullopt;
    // The retail pointer callback rejects an entity candidate that is behind the pointer's
    // resolved surface. Do the same before contextual orders or selection: otherwise a tall
    // building/tree hidden by a ridge can still be hit through the terrain at low camera angles.
    constexpr float kTerrainOcclusionTolerance=0.025F;
    if(terrain_hit&&nearest>terrain_hit->distance+kTerrainOcclusionTolerance)return std::nullopt;
    return PlacementPickHit{*selected,nearest};
}

std::optional<std::size_t> EditorApp::pick_placement(float mouse_x,float mouse_y) const{
    const auto hit=pick_placement_hit(mouse_x,mouse_y);
    return hit?std::optional<std::size_t>{hit->placement_index}:std::nullopt;
}

std::optional<std::pair<std::size_t,float>> EditorApp::pick_simulation_lab_entity(float mouse_x,float mouse_y){
    if(!document_||!simulation_lab_active())return std::nullopt;
    const auto runs=simulation_lab_runs();if(runs.empty())return std::nullopt;
    const auto& run=runs[static_cast<size_t>(std::clamp(simulation_lab_selected_run_,0,int(runs.size())-1))];
    const auto ray=canvas_view_ray(mouse_x,mouse_y);
    const auto terrain=pick_terrain_cell(document_->field(),ray);
    float nearest=terrain?terrain->distance+.025F:std::numeric_limits<float>::max();
    std::optional<std::pair<size_t,float>> hit;
    for(size_t i=0;i<run.entities.size();++i){
        const auto& entity=run.entities[i];
        if(entity.runtime_removed||entity.root_model_terminal)continue;
        const auto* preset=catalog_.army_preset(entity.army_asset_id);
        uint32_t flags=preset?preset->model_runtime_render_flags:0x10U;
        if(entity.runtime_model_hierarchy)for(const auto& node:*entity.runtime_model_hierarchy)
            if(node.parent_index==UINT32_MAX){flags=node.simulation.runtime_render_flags;break;}
        if(!fog_preview_model_intensity(entity.actor.position,flags))continue;
        auto actor=entity.actor;
        auto hulls=simulation_transient_pick_hulls(entity.army_asset_id,entity.faction_index,actor,nullptr,&entity);
        if(hulls.empty()){
            AxisAlignedBounds fallback;const auto p=entity.actor.position;
            fallback.include({p.x-.35F,p.y-.35F,p.z-.15F});fallback.include({p.x+.35F,p.y+.35F,p.z+.85F});
            hulls.push_back(TransformedBoundsHull::from_axis_aligned(fallback));
        }
        for(const auto& hull:hulls)if(const auto distance=ray_transformed_bounds_distance(ray,hull);
            distance&&*distance<nearest){nearest=*distance;hit=std::pair{i,*distance};}
    }
    return hit;
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
           target->second!=attacker_index){
            const bool same_faction=placements[target->second].faction_index==
                                    placements[attacker_index].faction_index;
            if((same_faction&&placement_has_recovery_weapon(attacker_index))||
               (!same_faction&&placement_has_hostile_weapon(attacker_index)))
                return placement_command_target_point(target->second);
        }
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

    const auto ray=canvas_view_ray(mouse_x,mouse_y);
    const auto entity_hit=pick_placement_hit(mouse_x,mouse_y);
    if(entity_hit)hovered_placement_=entity_hit->placement_index;
    if(const auto hit=pick_terrain_cell(document_->field(),ray))hovered_terrain_point_=hit->position;

    std::optional<uint32_t> hovered_faction;
    if(hovered_placement_&&*hovered_placement_<placements.size())
        hovered_faction=placements[*hovered_placement_].faction_index;
    const bool production_rally=preset->browser_kind==thandor::PlacementBrowserKind::Building&&
        (preset->technology_branch_count==3U||preset->technology_branch_count==4U);
    const thandor::ContextOrderActorCapabilities capabilities{
        preset->simulation_motion_profile.movable(),
        placement_has_hostile_weapon(selected_index),
        preset->browser_kind==thandor::PlacementBrowserKind::Building,
        production_rally,selected.faction_index,placement_has_recovery_weapon(selected_index)};
    hovered_context_order_=thandor::resolve_context_order(
        capabilities,hovered_faction,entity_hit.has_value(),hovered_terrain_point_.has_value());
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
            actor.move_target.reset();actor.move_waypoint.reset();
            simulation_mode_=true;animate_buildings_=true;animate_units_=true;
            if(preset->browser_kind==thandor::PlacementBrowserKind::Building)animate_buildings_=true;
            if(preset->browser_kind==thandor::PlacementBrowserKind::Unit)animate_units_=true;
            const auto* target_preset=catalog_.army_preset(
                document_->level().placements()[*hovered_placement_].army_asset_id);
            const bool recovery_target=*hovered_placement_<document_->level().placements().size()&&
                document_->level().placements()[*hovered_placement_].faction_index==placement.faction_index&&
                placement_has_recovery_weapon(selected_index);
            status_=std::string(recovery_target?"Recovery target assigned to ":"Attack target assigned to ")+
                (target_preset?target_preset->display_name:std::string("placement ")+std::to_string(*hovered_placement_))+".";
            break;
        }
        case thandor::ContextOrderKind::MoveToTerrain:{
            if(!hovered_terrain_point_)return;
            checkpoint();
            auto& actor=simulation_state_for(selected_index);
            actor.move_target=*hovered_terrain_point_;
            actor.move_waypoint.reset();
            simulation_attack_placement_targets_.erase(selected_index);
            simulation_attack_point_targets_.erase(selected_index);
            activate_fog_on_driving_order(placement.faction_index);
            simulation_mode_=true;animate_buildings_=true;animate_units_=true;
            status_="Selected unit received a game-style drive destination.";
            break;
        }
        case thandor::ContextOrderKind::AttackTerrainPoint:{
            if(!hovered_terrain_point_)return;
            simulation_attack_point_targets_[selected_index]=*hovered_terrain_point_;
            simulation_attack_placement_targets_.erase(selected_index);
            auto& actor=simulation_state_for(selected_index);
            actor.move_target.reset();actor.move_waypoint.reset();
            simulation_mode_=true;animate_buildings_=true;animate_units_=true;
            if(preset->browser_kind==thandor::PlacementBrowserKind::Building)animate_buildings_=true;
            if(preset->browser_kind==thandor::PlacementBrowserKind::Unit)animate_units_=true;
            status_="Selected armed placement received a point-target order.";
            break;
        }
        case thandor::ContextOrderKind::SetProductionRallyPoint:{
            if(!hovered_terrain_point_)return;
            simulation_production_rally_points_[selected_index]=*hovered_terrain_point_;
            // Retail single-factory position command stores a custom class-13 movement target,
            // sets runtime bit 0x800 for the EGATH0 marker, then clears the active selection.
            selected_placement_.reset();selected_simulation_lab_entity_.reset();
            status_="Factory exit point updated.";
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
    if(const auto found=simulation_actor_states_.find(index);found!=simulation_actor_states_.end()){
        found->second.move_target.reset();found->second.move_waypoint.reset();
    }
    status_="Cleared the selected placement's transient movement/attack order.";
    last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
}

float EditorApp::terrain_height_near(float world_x,float world_y) const{
    return document_?document_->field().sample_terrain(world_x,world_y).height:0.0F;
}

void EditorApp::report_asset_failure(const std::string& path,const std::string& message){
    if(!asset_load_failures_.try_emplace(path,message).second)return;
    status_="Unavailable asset: "+path+" ("+message+")";
    editor_performance_log_.error("ASSET_FALLBACK path="+path+" reason="+message);
}
std::shared_ptr<EditorApp::ArmyTextureFamily> EditorApp::load_texture_family(
        const std::string& gfx,const std::string& pal){
    thandor::GfxAsset graphics;
    thandor::PalAsset palette;
    try{graphics=thandor::GfxAsset::parse(catalog_.load(gfx));}
    catch(const std::bad_alloc&){throw;}
    catch(const std::exception& error){
        report_asset_failure(gfx,error.what());
        graphics=thandor::GfxAsset::parse(thandor::encode_single_rgba_gfx(missing_texture_placeholder()));
    }
    try{palette=thandor::PalAsset::parse(catalog_.load(pal));}
    catch(const std::bad_alloc&){throw;}
    catch(const std::exception& error){report_asset_failure(pal,error.what());}
    auto family=std::make_shared<ArmyTextureFamily>(std::move(graphics),std::move(palette));
    family->decode_failure=[this,gfx](const std::string& error){report_asset_failure(gfx,error);};
    return family;
}
const thandor::SpriteAsset& EditorApp::cached_sprite(const std::string& path){
    auto found=sprite_cache_.find(path);
    if(found==sprite_cache_.end()){
        try{found=sprite_cache_.emplace(path,thandor::SpriteAsset::parse(catalog_.load(path))).first;}
        catch(const std::bad_alloc&){throw;}
        catch(const std::exception& error){
            report_asset_failure(path,error.what());
            found=sprite_cache_.emplace(path,missing_sprite_placeholder()).first;
        }
    }
    return found->second;
}

PlacementContactPose EditorApp::placement_contact_pose(
    const thandor::ArmyAssetPreset& preset,float world_x,float world_y,
    float sprite_height_offset) const{
    if(!document_)return {};
    const auto& field=document_->field();
    auto terrain=field.sample_terrain(world_x,world_y);
    const auto fixed=field.sample_retail_surface_heights_q12(thandor::float_to_q12(world_x),thandor::float_to_q12(world_y));
    if(fixed.valid)terrain.height=thandor::q12_to_float(fixed.terrain);
    auto pose=resolve_placement_contact_pose(
        preset.simulation_motion_profile.placement_contact_kind,terrain,
        fixed.valid?thandor::q12_to_float(fixed.water_surface):field.sample_water_surface_height(world_x,world_y),
        fixed.valid?thandor::q12_to_float(fixed.top):field.sample_top_surface_height(world_x,world_y),sprite_height_offset);
    if(preset.simulation_motion_profile.placement_contact_kind==2)
        pose.packed_normal_angles=thandor::retail_contact_normal_angles(field,thandor::float_to_q12(world_x),thandor::float_to_q12(world_y));
    if(preset.simulation_motion_profile.placement_contact_kind!=3)
        pose.height+=thandor::q12_to_float(preset.simulation_motion_profile.placement_height_offset_q12);
    if(preset.simulation_motion_profile.placement_contact_kind==3)
        pose.height+=thandor::q12_to_float(preset.articulated_root_height_q12);
    return pose;
}

bool EditorApp::mouse_in_canvas(float mouse_x,float mouse_y) const{
    return workspace_layout().canvas.contains(mouse_x,mouse_y);
}

AssetPreviewMesh& EditorApp::asset_preview(const thandor::ArmyAssetPreset& preset){
    const bool neutral=thandor::army_asset_preview_uses_neutral_source_color(preset);
    const uint32_t preview_runtime_faction=neutral?0U:selected_faction_;
    const uint32_t preview_graphics_family=neutral?0U:
        graphics_family_for_faction(selected_faction_);
    const auto& research_mask=document_?document_->faction_technology_mask(selected_faction_):
        thandor::FactionTechnologyMask256{};
    const uint64_t cache_key=(technology_mask_hash(research_mask)*0x9E3779B185EBCA87ULL)^
        (static_cast<uint64_t>(preview_graphics_family)<<32U)^
        (static_cast<uint64_t>(preview_runtime_faction)<<56U)^preset.registry_id;
    if(const auto found=asset_preview_cache_.find(cache_key);found!=asset_preview_cache_.end())return found->second;

    AssetPreviewMesh preview{};
    if(!document_||preset.sprite_path.empty()){
        preview.marker_only=true;
        return asset_preview_cache_.emplace(cache_key,std::move(preview)).first->second;
    }

    const std::string army_base=document_->level().paths().army_textures;
    const auto texture_family=[&](uint32_t faction)->const ArmyTextureFamily&{
        const uint32_t clamped=std::min(faction,8U);
        const std::string key=terrain_base_path(army_base)+"#"+std::to_string(clamped);
        auto found=army_texture_cache_.find(key);
        if(found==army_texture_cache_.end()){
            auto family=load_texture_family(army_texture_family_path(army_base,clamped,".gfx"),
                                            army_texture_family_path(army_base,clamped,".pal"));
            found=army_texture_cache_.emplace(key,std::move(family)).first;
        }
        return *found->second;
    };
    const auto& family=texture_family(preview_graphics_family);
    const auto texture_image=[&](uint32_t subresource)->const thandor::RgbaImage&{
        const bool use_player_override=local_player_texture_override_.has_value()&&
            preview_runtime_faction==local_player_runtime_faction_&&
            subresource==kRetailFrontendPlayerTextureSubresource;
        const uint64_t key=army_texture_layer_key(preview_runtime_faction,preview_graphics_family,
            subresource,local_player_texture_override_.has_value(),local_player_runtime_faction_);
        auto found=asset_preview_texture_cache_.find(key);
        if(found==asset_preview_texture_cache_.end()){
            if(subresource>=family.graphics.subresources().size())
                throw std::runtime_error("asset-card SPR selects missing army GFX subresource");
            const auto& metadata=family.graphics.subresources()[subresource];
            thandor::RgbaImage decoded;
            if(use_player_override&&metadata.pixel_width==64U&&metadata.pixel_height==64U)
                decoded=*local_player_texture_override_;
            else
                decoded=family.graphics.decode(thandor::GraphicsSubresourceIndex{subresource},false);
            found=asset_preview_texture_cache_.emplace(key,
                thandor::make_sprite_texture_square_canvas(decoded)).first;
        }
        return found->second;
    };

    std::vector<thandor::ModelVisualNode> fallback_nodes;
    std::vector<thandor::ModelVisualNode> researched_nodes=
        thandor::resolve_faction_technology_hierarchy(preset,research_mask);
    std::span<const thandor::ModelVisualNode> hierarchy = !researched_nodes.empty()
        ? std::span<const thandor::ModelVisualNode>(researched_nodes)
        : (preset.resolved_model_hierarchy.empty()
            ? std::span<const thandor::ModelVisualNode>(preset.model_hierarchy)
            : std::span<const thandor::ModelVisualNode>(preset.resolved_model_hierarchy));
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
        if(node_active)instance.sprite=&cached_sprite(node.sprite_path);
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
    const auto vp=camera_.view_projection(workspace_layout().canvas.aspect());
    auto* draw=ImGui::GetBackgroundDrawList();
    const auto placements=document_->level().placements();
    // Lab owners draw their frame at the live model bounds. Scenery/resources
    // without a runtime owner still need the ordinary editor selection frame.
    std::unordered_set<std::size_t> runtime_frame_placements;
    if(simulation_lab_active()){
        const auto runs=simulation_lab_runs();
        if(!runs.empty()){
            const auto& run=runs[static_cast<size_t>(std::clamp(simulation_lab_selected_run_,0,int(runs.size())-1))];
            for(const auto& entity:run.entities)
                if(entity.source_placement_index<placements.size())
                    runtime_frame_placements.insert(entity.source_placement_index);
        }
    }
    const auto draw_world_distance_ring=[&](Vec3f center,float radius,ImU32 color,float thickness){
        if(!std::isfinite(radius)||radius<=0.0F)return;
        constexpr int kSegments=64;
        std::optional<ImVec2> previous;
        for(int segment=0;segment<=kSegments;++segment){
            const float angle=static_cast<float>(segment)*
                (2.0F*std::numbers::pi_v<float>/static_cast<float>(kSegments));
            const float x=center.x+std::cos(angle)*radius;
            const float y=center.y+std::sin(angle)*radius;
            const Vec3f point{x,y,terrain_height_near(x,y)+0.04F};
            float sx,sy,depth;
            if(project_to_canvas(point,vp,sx,sy,depth)){
                const ImVec2 current{sx,sy};
                if(previous)draw->AddLine(*previous,current,color,thickness);
                previous=current;
            }else{
                previous.reset();
            }
        }
    };
    for(std::size_t i=0;i<placements.size();++i){
        const auto& placement=placements[i];
        const auto* preset=catalog_.army_preset(placement.army_asset_id);
        float sx,sy,depth;
        const float z=terrain_height_near(placement.render_x(),placement.render_y());
        if(!project_to_canvas({placement.render_x(),placement.render_y(),z+0.08F},vp,sx,sy,depth))continue;
        const float radius=selected_placement_==i?9.0F:6.0F;
        bool damaged=i<document_->placement_tags().size()&&document_->placement_tags()[i].preview_integrity_percent<100U;
        if(const auto health=simulation_integrity_states_.find(i);health!=simulation_integrity_states_.end())
            damaged|=thandor::retail_selection_show_damage_frame(health->second.integrity,health->second.maximum_integrity);
        if(!runtime_frame_placements.contains(i)&&(selected_placement_==i||damaged)){
            const bool has_bounds=i<placement_pick_bounds_.size()&&placement_pick_bounds_[i].valid();
            float min_x=std::numeric_limits<float>::max(),min_y=std::numeric_limits<float>::max();
            float max_x=std::numeric_limits<float>::lowest(),max_y=std::numeric_limits<float>::lowest();
            bool any=false;
            if(has_bounds){
              const auto& bounds=placement_pick_bounds_[i];
              for(const float bx:{bounds.minimum.x,bounds.maximum.x})
                for(const float by:{bounds.minimum.y,bounds.maximum.y})
                    for(const float bz:{bounds.minimum.z,bounds.maximum.z}){
                        float px,py,pd;
                        if(!project_to_canvas({bx,by,bz},vp,px,py,pd))continue;
                        any=true;min_x=std::min(min_x,px);min_y=std::min(min_y,py);
                        max_x=std::max(max_x,px);max_y=std::max(max_y,py);
                    }
            }
            if(!any&&selected_placement_==i){
                min_x=sx-12.F;max_x=sx+12.F;min_y=sy-12.F;max_y=sy+12.F;any=true;
            }
            if(any){
                int32_t condition=4096;
                if(i<document_->placement_tags().size())condition=int32_t(document_->placement_tags()[i].preview_integrity_percent)*4096/100;
                if(const auto health=simulation_integrity_states_.find(i);health!=simulation_integrity_states_.end()&&health->second.maximum_integrity>0)
                    condition=int32_t(int64_t(health->second.integrity)*4096/health->second.maximum_integrity);
                thandor::RetailSelectionFrameMetrics metrics;
                if(simulation_mode_&&preset){
                    if(const auto state=simulation_actor_states_.find(i);state!=simulation_actor_states_.end())
                        metrics.top_bar=thandor::retail_selection_reload(placement_weapon_profiles(*preset,i),state->second);
                }
                const std::optional<uint32_t> tint=placement.faction_index!=0U?std::optional<uint32_t>{
                    thandor::graphics_family_representative_rgb24(graphics_family_for_faction(placement.faction_index))}:std::nullopt;
                draw_retail_selection_frame(min_x,min_y,max_x,max_y,condition,tint,metrics);

            }
        }
        if(preset&&preset->visual_kind==thandor::PlacementVisualKind::PlayerStart){
            const ImU32 color=graphics_family_ui_swatch(
                graphics_family_for_faction(placement.faction_index),245U);
            draw->AddQuadFilled({sx,sy-radius},{sx+radius,sy},{sx,sy+radius},{sx-radius,sy},color);
            const std::string label="P"+std::to_string(placement.faction_index);
            draw->AddText({sx+radius+2.0F,sy-radius},IM_COL32_WHITE,label.c_str());
        }else if(preset&&(preset->visual_kind==thandor::PlacementVisualKind::PowerResource||
                          preset->visual_kind==thandor::PlacementVisualKind::BuildingResource)){
            // The visible deposit body and glow are rendered in Vulkan. Keep only a selection
            // ring here so resources are not represented by a permanent blue/gold dot.
            // Selection uses the native frame above.
        }else{
            const ImU32 color=placement.faction_index==0U?IM_COL32(235,190,60,230):
                graphics_family_ui_swatch(graphics_family_for_faction(placement.faction_index),240U);
            const bool unresolved=preset==nullptr||preset->sprite_path.empty();
            if(unresolved){
                draw->AddCircleFilled({sx,sy},radius,color);
            }else if(selected_placement_!=i){
                continue;
            }
            const std::string label=preset&&!preset->short_name.empty()?preset->short_name:
                ("ID "+std::to_string(placement.army_asset_id));
            draw->AddText({sx+radius+2.0F,sy-radius},IM_COL32_WHITE,label.c_str());
        }
    }
    if(simulation_target_point_){
        float sx,sy,depth;
        if(project_to_canvas(add(*simulation_target_point_,{0.0F,0.0F,0.12F}),vp,
            sx,sy,depth)){
            const ImU32 color=IM_COL32(255,72,72,245);
            draw->AddCircle({sx,sy},13.0F,color,24,2.5F);
            draw->AddLine({sx-17.0F,sy},{sx+17.0F,sy},color,2.0F);
            draw->AddLine({sx,sy-17.0F},{sx,sy+17.0F},color,2.0F);
            draw->AddText({sx+14.0F,sy-15.0F},IM_COL32_WHITE,"Simulation target");
        }
    }
    const auto draw_attack_marker=[&](Vec3f point,const char* label,ImU32 color){
        float sx,sy,depth;
        if(!project_to_canvas(add(point,{0.0F,0.0F,0.12F}),vp,
            sx,sy,depth))return;
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
        if(project_to_canvas(from,vp,ax,ay,ad)&&
           project_to_canvas(to,vp,bx,by,bd))
            draw->AddLine({ax,ay},{bx,by},IM_COL32(255,75,55,190),2.0F);
        draw_attack_marker(to,"Attack entity",IM_COL32(255,70,55,245));
    }
    for(const auto& [attacker,target]:simulation_attack_point_targets_){
        if(attacker>=placements.size())continue;
        const Vec3f from=actor_position(attacker);
        float ax,ay,ad,bx,by,bd;
        if(project_to_canvas(from,vp,ax,ay,ad)&&
           project_to_canvas(target,vp,bx,by,bd))
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
        if(project_to_canvas(add(*hovered_terrain_point_,{0.0F,0.0F,0.12F}),vp,
            sx,sy,depth)){
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
        if(project_to_canvas(from,vp,ax,ay,ad)&&
           project_to_canvas(target,vp,bx,by,bd))
            draw->AddLine({ax,ay},{bx,by},graphics_family_ui_swatch(
                graphics_family_for_faction(placements[producer].faction_index),205U),2.0F);
    }
    if (ai_knowledge_ && ai_knowledge_ui_.show_distance_overlay) {
        const auto* semantic = thandor::find_ai_knowledge_semantic(
            ai_knowledge_ui_.distance_parameter_index);
        if (semantic && semantic->domain == thandor::AiKnowledgeValueDomain::Q12Distance) {
            const float radius = std::abs(thandor::q12_to_float(
                ai_knowledge_->signed_raw(semantic->index)));

            const auto draw_ai_distance_ring = [&](std::size_t placement_index) {
                if (placement_index >= placements.size()) return;

                Vec3f center = placement_command_target_point(placement_index);
                center.z = terrain_height_near(center.x, center.y);
                draw_world_distance_ring(
                    center, radius, IM_COL32(120, 175, 255, 155), 1.25F);
            };

            if (ai_knowledge_ui_.show_distance_for_all_placements) {
                for (std::size_t placement_index = 0;
                     placement_index < placements.size(); ++placement_index) {
                    draw_ai_distance_ring(placement_index);
                }
            } else if (selected_placement_) {
                draw_ai_distance_ring(*selected_placement_);
            }
        }
    }
    if(fog_preview_mode_!=0&&fog_preview_show_nominal_radius_&&selected_placement_&&
       *selected_placement_<placements.size()){
        const auto index=*selected_placement_;
        const auto& placement=placements[index];
        const auto* preset=catalog_.army_preset(placement.army_asset_id);
        if(preset&&(preset->browser_kind==thandor::PlacementBrowserKind::Unit||
                    preset->browser_kind==thandor::PlacementBrowserKind::Building)){
            const uint32_t radius_q12=fog_preview_observer_radius(*preset);
            if(radius_q12!=0U){
                Vec3f center{placement.render_x(),placement.render_y(),
                    terrain_height_near(placement.render_x(),placement.render_y())};
                if(const auto found=simulation_actor_states_.find(index);
                   found!=simulation_actor_states_.end())center=found->second.position;
                draw_world_distance_ring(center,thandor::q12_to_float(
                    static_cast<int32_t>(radius_q12)),IM_COL32(90,220,255,155),1.35F);
                float sx,sy,depth;
                const Vec3f label_point{center.x+thandor::q12_to_float(
                    static_cast<int32_t>(radius_q12)),center.y,center.z+0.08F};
                if(project_to_canvas(label_point,vp,sx,sy,depth))
                    draw->AddText({sx+4.0F,sy-10.0F},IM_COL32(130,235,255,230),
                        "Fog nominal radius");
            }
        }
    }
    if(simulation_show_ranges_&&selected_placement_&&
       *selected_placement_<placements.size()){
        const auto index=*selected_placement_;
        const auto* preset=catalog_.army_preset(placements[index].army_asset_id);
        const auto weapon_profiles=preset?placement_weapon_profiles(*preset,index):
            std::span<const thandor::SimulationWeaponProfile>{};
        if(preset&&!weapon_profiles.empty()){
            Vec3f center{placements[index].render_x(),placements[index].render_y(),
                terrain_height_near(placements[index].render_x(),placements[index].render_y())};
            if(const auto found=simulation_actor_states_.find(index);found!=simulation_actor_states_.end())
                center=found->second.position;
            const int32_t maximum_range_q12=std::max_element(
                weapon_profiles.begin(),weapon_profiles.end(),
                [](const auto& left,const auto& right){return left.maximum_range_q12<right.maximum_range_q12;})
                ->maximum_range_q12;
            draw_world_distance_ring(center,thandor::q12_to_float(maximum_range_q12),
                IM_COL32(255,175,55,165),1.5F);
        }
    }
    for(const auto& [index,state]:simulation_actor_states_){
        if(!state.move_target||index>=placements.size())continue;
        const auto command_color=graphics_family_ui_swatch(graphics_family_for_faction(placements[index].faction_index),235U);
        float ax,ay,ad,bx,by,bd;
        if(project_to_canvas(state.position,vp,ax,ay,ad)&&
           project_to_canvas(*state.move_target,vp,bx,by,bd)){
            draw->AddLine({ax,ay},{bx,by},command_color,2.0F);
        }
        if(state.move_waypoint){
            float px,py,pd;
            if(project_to_canvas(state.position,vp,ax,ay,ad)&&
               project_to_canvas(*state.move_waypoint,vp,px,py,pd))
                draw->AddLine({ax,ay},{px,py},command_color,2.5F);
        }
        if(!v519_path_diagnostics_||index>=placements.size())continue;
        const auto* preset=catalog_.army_preset(placements[index].army_asset_id);
        if(preset==nullptr||!preset->has_traversal_profile)continue;
        const auto draw_scratch_cell=[&](const Vec3f& world,ImU32 color){
            const auto raw=thandor::v519_path_world_to_scratch(
                thandor::float_to_q12(world.x),thandor::float_to_q12(world.y));
            const auto cell=thandor::v519_path_clip_to_inner_scratch(document_->field(),raw);
            const auto center_q12=thandor::v519_path_scratch_center_to_world_q12(cell);
            Vec3f center{thandor::q12_to_float(center_q12.x_q12),
                thandor::q12_to_float(center_q12.y_q12),0.0F};
            center.z=terrain_height_near(center.x,center.y)+0.14F;
            float sx,sy,depth;
            if(!project_to_canvas(center,vp,sx,sy,depth))return;
            draw->AddRect({sx-4.0F,sy-4.0F},{sx+4.0F,sy+4.0F},color,0.0F,0,2.0F);
        };
        draw_scratch_cell(state.position,IM_COL32(115,225,255,235));
        if(state.move_waypoint)draw_scratch_cell(*state.move_waypoint,IM_COL32(255,145,80,245));
        draw_scratch_cell(*state.move_target,IM_COL32(245,225,95,245));
    }
}

// Live-forensic log: the GUI collects immutable snapshot observations; the joined
// background writer formats and saves them. Parallel simulation workers never write logs.
uint32_t EditorApp::fog_preview_active_faction_count()const noexcept{
    if(!document_)return 1U;
    uint32_t count=std::clamp(document_->player_ai_configuration().active_factions,1U,7U);
    if(simulation_lab_active()){
        const auto runs=simulation_lab_runs();
        if(!runs.empty()){
            const auto& run=runs[static_cast<std::size_t>(std::clamp(
                simulation_lab_selected_run_,0,static_cast<int>(runs.size())-1))];
            if(run.active_faction_count!=0U)
                return std::clamp(run.active_faction_count,1U,7U);
            for(const auto& entity:run.entities)
                if(entity.faction_index>0U&&entity.faction_index<=7U)
                    count=std::max(count,entity.faction_index);
        }
    }
    return count;
}

void EditorApp::end_simulation_lab_diagnostics(const char* reason){
    if(simulation_lab_diagnostic_writer_){
        simulation_lab_diagnostic_writer_->close(reason);
        simulation_lab_diagnostic_writer_.reset();
    }
}

void EditorApp::begin_simulation_lab_diagnostics(){
    end_simulation_lab_diagnostics("restart");
    simulation_lab_diagnostic_path_.clear();
    simulation_lab_worker_error_logged_=false;
    simulation_lab_log_initialized_=false;
    simulation_lab_log_finished_=false;
    simulation_lab_last_log_milliseconds_=0.0;
    if(!logging_enabled_)return;
    try{
        const auto directory=simulation_lab_logs_directory();
        std::filesystem::create_directories(directory);
        const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto file=directory/("simlab_"+std::to_string(stamp)+".log");
        simulation_lab_diagnostic_path_=file.string();
        std::ostringstream header;
        header<<"THANDOR SIMLAB "<<thandor::kEditorVersion<<" GUI forensic log (not a retail trace)\n"
            <<"LOG_POLICY sample_wall_ms=1000 detail_min_ticks=64 final_sample=immediate pending_expiry=none_recovered async_format=1 periodic_coalesce=1 queue_max=4\n"
            <<"START map="<<(document_?document_->map_name():"<none>")
            <<" runs="<<simulation_lab_config_.run_count
            <<" worker_thread_limit="<<simulation_lab_config_.worker_thread_limit
            <<" seed="<<simulation_lab_config_.base_seed
            <<" vary_seed_per_run="<<simulation_lab_config_.vary_seed_per_run
            <<" swept_parameter="<<simulation_lab_config_.parameter_index
            <<" duration_ticks="<<simulation_lab_config_.duration_ticks
            <<" storage_override="<<simulation_lab_storage_limit_overrides_enabled_
            <<" xenite_cap_override_q4="<<simulation_lab_xenite_storage_limit_q4_
            <<" tritium_cap_override_q4="<<simulation_lab_tritium_storage_limit_q4_
            <<" fog_mode="<<fog_preview_mode_
            <<" whole_map_explored="<<(fog_preview_all_explored_?1:0)
            <<" active_factions="<<fog_preview_active_faction_count()
            <<" simulate_all_active_factions_as_ai="
            <<(simulation_lab_simulate_all_active_factions_as_ai_?1:0)
            <<" human_faction_mask="<<simulation_lab_human_faction_mask_
            <<" game_speed_q8="<<simulation_lab_game_speed_q8_
            <<" selected_run="<<simulation_lab_selected_run_<<'\n';
        std::unordered_set<uint32_t> preset_ids;
        for(const auto& preset:catalog_.army_presets())preset_ids.insert(preset.registry_id);
        simulation_lab_diagnostic_writer_=std::make_unique<SimulationLabDiagnosticWriter>(
            file,header.str(),document_->field(),std::move(preset_ids));
        append_simulation_lab_diagnostics(true);
    }catch(const std::exception& e){
        end_simulation_lab_diagnostics("open_failure");
        simulation_lab_diagnostic_path_=std::string("Diagnostic log unavailable: ")+e.what();
    }
}

void EditorApp::append_simulation_lab_diagnostics(bool force){
    if(!logging_enabled_||!simulation_lab_diagnostic_writer_||!simulation_lab_ui_snapshot_)return;
    const auto log_start=std::chrono::steady_clock::now();
    const bool finished=simulation_lab_finished();
    const bool final_sample=finished&&!simulation_lab_log_finished_;
    if(!thandor::simulation_lab_log_sample_due(force,simulation_lab_log_initialized_,
        final_sample,log_start-simulation_lab_last_log_time_))return;
    simulation_lab_last_log_time_=log_start;
    simulation_lab_log_initialized_=true;
    simulation_lab_log_finished_=finished;
    SimulationLabDiagnosticSample sample;
    sample.snapshot=simulation_lab_ui_snapshot_;
    sample.performance=simulation_lab_worker_.performance();
    sample.pending_ticks=simulation_lab_worker_.pending_ticks();
    sample.generation_us=simulation_lab_worker_.last_generation_duration_us();
    sample.peak_generation_us=simulation_lab_worker_.maximum_generation_duration_us();
    sample.duration_ticks=simulation_lab_config_.duration_ticks;
    sample.active_factions=fog_preview_active_faction_count();
    sample.simulation_lab_last_sync_milliseconds_=simulation_lab_last_sync_milliseconds_;
    sample.dynamic_scene_build_milliseconds_=dynamic_scene_build_milliseconds_;
    sample.dynamic_scene_pre_shadow_milliseconds_=dynamic_scene_pre_shadow_milliseconds_;
    sample.dynamic_scene_fog_milliseconds_=dynamic_scene_fog_milliseconds_;
    sample.dynamic_scene_model_setup_milliseconds_=dynamic_scene_model_setup_milliseconds_;
    sample.dynamic_scene_shadow_milliseconds_=dynamic_scene_shadow_milliseconds_;
    sample.dynamic_scene_effects_milliseconds_=dynamic_scene_effects_milliseconds_;
    sample.dynamic_scene_sort_milliseconds_=dynamic_scene_sort_milliseconds_;
    sample.generated_shadow_build_milliseconds_=generated_shadow_build_milliseconds_;
    sample.projected_shadows_enabled=projected_object_shadows_;
    sample.dynamic_shadow_templates=dynamic_shadow_templates_.size();
    sample.dynamic_shadow_vertices=dynamic_scene_cpu_.shadow_vertices.size();
    sample.render_receipts=simulation_lab_render_receipts_;
    sample.command_marker_run_index_=command_marker_run_index_;
    sample.command_marker_build_milliseconds_=command_marker_build_milliseconds_;
    sample.command_marker_diagnostics_=command_marker_diagnostics_;
    sample.command_marker_count_=command_marker_count_;
    sample.fog_preview_mode_=fog_preview_mode_;
    sample.fog_preview_faction_=fog_preview_faction_;
    sample.fog_preview_all_explored_=fog_preview_all_explored_;
    sample.audio_one_shot_requests=audio_one_shot_requests_;sample.audio_submitted=audio_submitted_;
    sample.audio_fog_filtered=audio_fog_filtered_;sample.audio_inaudible=audio_inaudible_;
    sample.audio_missing_samples=audio_missing_samples_;sample.audio_backend_rejected=audio_backend_rejected_;
    sample.audio_loop_requests=audio_loop_requests_;sample.audio_path_cache_hits=audio_path_cache_hits_;
    sample.audio_active_loops=active_looping_sounds_.size();
    sample.audio_enabled=sound_playback_enabled_;sample.audio_effects_gain_q15=sound_effects_gain_q15_;
    sample.audio_map_focus=simulation_lab_active()&&simulation_sound_at_map_focus_;
    sample.audio_camera_distance=camera_.distance;
    const auto listener=spatial_audio_listener();
    sample.audio_listener_position={listener.position[0],listener.position[1],listener.position[2]};
    const auto audio_host=thandor::audio_playback_diagnostics();sample.audio_host_queued=audio_host.one_shots_queued;
    sample.audio_host_completed=audio_host.one_shots_completed;sample.audio_host_failed=audio_host.one_shots_failed;
    simulation_lab_diagnostic_writer_->append(std::move(sample),force||final_sample);
    simulation_lab_last_log_milliseconds_=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-log_start).count();
}

void EditorApp::start_simulation_lab(){
    selected_simulation_lab_entity_.reset();
    if(!document_){status_="Load a map before starting Simulation Lab.";return;}
    if(!ai_knowledge_){
        status_=ai_knowledge_load_error_.empty()?"engine/ki.dat is unavailable.":ai_knowledge_load_error_;
        return;
    }
    const auto semantics=thandor::known_ai_knowledge_semantics();
    if(semantics.empty()){status_="No GZF-verified KI parameters are available.";return;}
    simulation_lab_semantic_row_=std::clamp(simulation_lab_semantic_row_,0,
        static_cast<int>(semantics.size())-1);
    simulation_lab_config_.parameter_index=semantics[static_cast<std::size_t>(simulation_lab_semantic_row_)].index;
    simulation_lab_config_.run_count=std::clamp(simulation_lab_config_.run_count,1U,32U);
    simulation_lab_config_.duration_ticks=std::clamp(simulation_lab_config_.duration_ticks,120U,108000U);

    const auto& level=document_->level();
    const auto model_paths=level.model_resource_paths_in_load_order();
    const auto army_paths=level.army_resource_paths_in_load_order();
    const auto effect_paths=level.effect_resource_paths_in_load_order();
    const auto shot_paths=level.shot_resource_paths_in_load_order();
    std::string model_registry_key=catalog_.game_root().generic_string();
    for(const auto& path:model_paths)model_registry_key+='\n'+path;
    model_registry_key+="\nARM registry";
    for(const auto& path:army_paths)model_registry_key+='\n'+path;
    model_registry_key+="\nEFF registry";
    for(const auto& path:effect_paths)model_registry_key+='\n'+path;
    model_registry_key+="\nSHT registry";
    for(const auto& path:shot_paths)model_registry_key+='\n'+path;
    model_registry_key+="\nTEC registry\n"+level.paths().technology;
    std::vector<std::string> archive_stamps;
    for(const auto& entry:std::filesystem::directory_iterator(catalog_.game_root())){
        auto extension=entry.path().extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(extension!=".pck"||!entry.is_regular_file())continue;
        archive_stamps.push_back(entry.path().generic_string()+":"+std::to_string(entry.file_size())+":"+
            std::to_string(entry.last_write_time().time_since_epoch().count()));
    }
    std::sort(archive_stamps.begin(),archive_stamps.end());
    for(const auto& stamp:archive_stamps)model_registry_key+='\n'+stamp;
    if(!model_paths.empty()||!army_paths.empty()||!effect_paths.empty()||!shot_paths.empty()||
       !level.paths().technology.empty()){
        if(!simulation_lab_level_catalog_cache_||simulation_lab_level_catalog_key_!=model_registry_key){
            const auto registry_scope=editor_performance_log_.measure(EditorPerfStage::LabScenarioRuntimeCatalog);
            auto next=thandor::AssetCatalog::scan_archive_root(catalog_.game_root(),level);
            simulation_lab_level_catalog_cache_=std::move(next);
            simulation_lab_level_catalog_key_=model_registry_key;
            ++simulation_lab_level_catalog_misses_;
        }else ++simulation_lab_level_catalog_hits_;
    }
    const auto& simulation_catalog=(!model_paths.empty()||!army_paths.empty()||!effect_paths.empty()||
        !shot_paths.empty()||!level.paths().technology.empty())?*simulation_lab_level_catalog_cache_:catalog_;
    auto startup_stage_started=EditorPerformanceLog::Clock::now();
    const auto finish_startup_stage=[&](EditorPerfStage stage){
        const auto now=EditorPerformanceLog::Clock::now();
        editor_performance_log_.record(stage,
            std::chrono::duration<double,std::milli>(now-startup_stage_started).count());
        startup_stage_started=now;
    };
    std::vector<thandor::SimulationLabSeedEntity> seeds;
    const auto placements=document_->level().placements();
    seeds.reserve(placements.size());
    for(std::size_t index=0;index<placements.size();++index){
        const auto& placement=placements[index];
        if(placement.faction_index==0U)continue;
        const auto* preset=simulation_catalog.army_preset(placement.army_asset_id);
        if(!preset)continue;
        if(!thandor::simulation_lab_faction_placement_has_runtime_owner(*preset,placement.faction_index))continue;
        const bool building=thandor::simulation_lab_model_class_is_building(preset->runtime_model_class_id);

        thandor::SimulationLabSeedEntity seed;
        seed.source_placement_index=index;
        seed.army_asset_id=placement.army_asset_id;
        seed.faction_index=placement.faction_index;
        seed.runtime_model_class_id=preset->runtime_model_class_id;
        seed.building=building;
        seed.has_traversal_profile=preset->has_traversal_profile;
        seed.traversal_low_band_index=preset->traversal_low_band_index;
        seed.traversal_high_band_index=preset->traversal_high_band_index;
        seed.motion=preset->simulation_motion_profile;
        seed.water_damage_multiplier=preset->water_damage_multiplier;
        seed.water_damage_threshold_q12=preset->water_damage_threshold_q12;
        seed.vertical_deployment_step_q12_per_tick=preset->vertical_deployment_step_q12_per_tick;
        seed.vertical_deployment_travel_limit_q12=preset->vertical_deployment_travel_limit_q12;
        seed.class23_deployment_sound_asset_id=preset->runtime_class_effect_sound_asset_id_26c;
        seed.tank_track_texture_travel_scale=preset->track_texture_travel_scale;
        seed.articulated_contact_interpolation_rate_q12=
            preset->articulated_contact_interpolation_rate_q12;
        seed.articulated_contact_sound_asset_id=preset->articulated_contact_sound_asset_id;
        seed.runtime_sound_profile={preset->runtime_model_class_id,
            preset->projected_sound_asset_id_d0,preset->projected_sound_asset_id_d8,
            preset->projected_sound_asset_id_primary,preset->projected_sound_asset_id_secondary};
        seed.positioned_sound_gain_q15=preset->positioned_sound_gain_q15;
        seed.positioned_sound_maximum_distance_q12=preset->positioned_sound_maximum_distance_q12;
        seed.production_compatibility_mask=preset->runtime_class_specific_c4_raw;
        seed.production_storage_capacity=preset->runtime_class_specific_c4_raw;
        seed.runtime_class_specific_c0_raw=preset->runtime_class_specific_c0_raw;
        seed.runtime_class_specific_c4_raw=preset->runtime_class_specific_c4_raw;
        seed.runtime_class_specific_19c_raw=preset->runtime_class_specific_19c_raw;
        seed.runtime_class_specific_1a8_raw=preset->runtime_class_specific_1a8_raw;
        if(preset->runtime_model_class_id==18U&&preset->runtime_class_specific_c4_raw!=0U){
            if(const auto* definition=simulation_catalog.effect_definition(preset->runtime_class_specific_c4_raw))
                seed.class18_conversion_effect_lifetime_ticks=thandor::effect_model_completion_tick(*definition).value_or(0U);
        }
        seed.class17_root_child_deployment_enabled=preset->runtime_uses_class17_root_child_deployment;
        seed.root_model_maximum_integrity=preset->root_maximum_integrity;
        seed.shot_impact_class_index=preset->shot_impact_class_index;
        seed.fog_uncover_radius_q12=preset->fog_uncover_radius_q12;
        for(const auto& entry:preset->technology_panel_entries)
            if(entry.technology_id!=0U&&std::find(seed.research_technology_ids.begin(),
               seed.research_technology_ids.end(),entry.technology_id)==seed.research_technology_ids.end())
                seed.research_technology_ids.push_back(entry.technology_id);
        seed.actor.position={placement.render_x(),placement.render_y(),
            terrain_height_near(placement.render_x(),placement.render_y())};
        seed.actor.body_yaw_turn16=placement.orientation_turn16&0xFFFFU;
        seed.actor.last_updated_tick=current_presentation_tick_;
        if(preset->runtime_model_class_id==13U){
            if(const auto points=simulation_class13_spawn_and_exit_points(index,*preset)){
                seed.production_spawn_world=points->first;
                seed.production_exit_world=points->second;
                if(const auto rally=simulation_production_rally_points_.find(index);rally!=simulation_production_rally_points_.end())
                    seed.production_drive_target_world=rally->second;
            }
        }
        const auto hierarchy=placement_hierarchy(*preset,index);
        if(!hierarchy.empty())
            seed.destroyed_replacement_army_asset_id=
                hierarchy.front().simulation.destroyed_replacement_army_asset_id;
        seed.model_components=thandor::simulation_lab_model_components_from_hierarchy(hierarchy);
        for(const auto& node:hierarchy)
            if(node.creates_runtime_node())
                seed.occupancy_mark_radius_units=std::max(
                    seed.occupancy_mark_radius_units,
                    node.simulation.occupancy_mark_radius_units);
        // The retail visibility position uses each active model node's world
        // transform plus its MDL +0x70 reference height, not merely the
        // maximum +0x70 value among root/child definitions. Buildings on
        // slopes and raised attachments differ materially under this rule.
        std::vector<std::uint8_t> fog_active_nodes;
        const auto fog_transforms=simulation_hierarchy_transforms(
            index,*preset,seed.actor,&fog_active_nodes);
        // Reuse the already-resolved live model-node transforms for class-14
        // field registration. The ArmyRuntime position is not necessarily the
        // resource-claim position of a linked MDL component.
        for(auto& component:seed.model_components){
            const auto node=component.source_model_node_index;
            if(component.runtime_class_id!=14U || node>=fog_transforms.size() ||
               node>=fog_active_nodes.size() || fog_active_nodes[node]==0U)continue;
            const auto& world=fog_transforms[node].translation;
            component.extraction_model_world_offset_at_seed=thandor::Vec3f{
                world.x-seed.actor.position.x,world.y-seed.actor.position.y,
                world.z-seed.actor.position.z};
            component.extraction_model_seed_yaw_turn16=seed.actor.body_yaw_turn16;
        }
        std::size_t fog_root=std::numeric_limits<std::size_t>::max();
        for(std::size_t node=0;node<hierarchy.size() && node<fog_transforms.size() &&
            node<fog_active_nodes.size();++node){
            if(fog_active_nodes[node]!=0U &&
                hierarchy[node].parent_index==std::numeric_limits<std::uint32_t>::max()){
                fog_root=node;break;
            }
        }
        if(fog_root<fog_transforms.size()){
            const float root_z=fog_transforms[fog_root].translation.z;
            seed.runtime_model_node_height_offsets_q12.resize(hierarchy.size());
            for(std::size_t node=0;node<hierarchy.size() && node<fog_transforms.size() &&
                node<fog_active_nodes.size();++node){
                if(fog_active_nodes[node]==0U)continue;
                seed.runtime_model_node_height_offsets_q12[node]=thandor::float_to_q12(fog_transforms[node].translation.z-root_z);
                const std::int64_t offset=static_cast<std::int64_t>(
                    thandor::float_to_q12(fog_transforms[node].translation.z-root_z))+
                    hierarchy[node].simulation.fog_reference_height_offset_q12;
                seed.fog_reference_height_offset_q12=std::max(
                    seed.fog_reference_height_offset_q12,
                    static_cast<std::int32_t>(std::clamp<std::int64_t>(
                        offset,0,std::numeric_limits<std::int32_t>::max())));
            }
        }else{
            for(const auto& node:hierarchy)
                if(node.creates_runtime_node())
                    seed.fog_reference_height_offset_q12=std::max(
                        seed.fog_reference_height_offset_q12,
                        node.simulation.fog_reference_height_offset_q12);
        }
        if(const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){
               return node.parent_index==std::numeric_limits<uint32_t>::max();
           });root!=hierarchy.end()){
            seed.movement_ground_effect_interval_ticks=root->simulation.timed_effect_interval_ticks;
            seed.movement_ground_land_effect_id=root->simulation.timed_effect_id;
            seed.movement_ground_water_effect_id=root->simulation.timed_water_effect_id;
        }
        if(preset->runtime_model_class_id==2U&&preset->has_primary_texture_animation&&
           preset->has_secondary_texture_animation){
            const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){
                return node.parent_index==std::numeric_limits<uint32_t>::max();
            });
            if(root!=hierarchy.end()&&!root->sprite_path.empty()){
                seed.tank_track_animation_enabled=true;
                seed.tank_track_local_bounds_y0=cached_sprite(root->sprite_path).local_bounds_min().y;
            }
        }
        const auto weapon_profiles=placement_weapon_profiles(*preset,index);
        seed.actor.weapons.resize(weapon_profiles.size());
        seed.combat_weapons.reserve(weapon_profiles.size());
        if(preset->runtime_model_class_id==17U)
            thandor::simulation_initialize_class17_deployment(seed.actor,false,
                preset->runtime_uses_class17_root_child_deployment);
        for(std::size_t weapon_index=0;weapon_index<weapon_profiles.size();++weapon_index){
            auto weapon=weapon_profiles[weapon_index];
            thandor::ShotDefinitionRecord shot{};
            if(const auto* definition=simulation_catalog.shot_definition(weapon.shot_definition_id))shot=*definition;
            else {weapon.shot_definition_id=0U;weapon.shot_definition_resolved=false;}
            seed.combat_weapons.push_back({std::move(weapon),std::move(shot)});
            const auto& presentation_weapon=weapon_profiles[weapon_index];
            auto& state=seed.actor.weapons[weapon_index];
            state.launch_mesh_group_mask=0xFFFFFFFFU;
            if(presentation_weapon.yaw_model_node_index<hierarchy.size())
                state.yaw.angle_turn16=hierarchy[presentation_weapon.yaw_model_node_index].local_rotation_angle2&0xFFFFU;
            if(presentation_weapon.pitch_model_node_index<hierarchy.size())
                state.pitch.angle_turn16=hierarchy[presentation_weapon.pitch_model_node_index].local_rotation_angle1&0xFFFFU;
        }
        if(preset->runtime_model_class_id==3U){
            // Seed the exact retail contact ownership boundary once while editor assets are
            // available.  The Lab worker only receives body-local terminal-foot offsets and raw
            // MDL scalars, so subsequent target freezes remain deterministic and asset-I/O free.
            seed.articulated_root_height_q12=preset->articulated_root_height_q12;
            seed.articulated_contact_lift_q12=preset->articulated_contact_lift_q12;
            seed.articulated_contact_steering_limit_turn16=
                preset->articulated_contact_steering_limit_turn16;
            const auto transforms=simulation_hierarchy_transforms(index,*preset,seed.actor,nullptr,true);
            if(seed.actor.articulated_contacts_initialized&&!transforms.empty()){
                const float yaw=thandor::turn16_to_radians(seed.actor.body_yaw_turn16);
                const float cosine=std::cos(yaw),sine=std::sin(yaw);
                for(std::size_t side=0;side<2U;++side){
                    const auto& contact=seed.actor.articulated_foot_anchor_world[side];
                    const float dx=contact.x-seed.actor.position.x;
                    const float dy=contact.y-seed.actor.position.y;
                    seed.articulated_contact_local_offsets[side]={
                        dx*cosine+dy*sine,-dx*sine+dy*cosine,0.0F};
                }
                const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){
                    return node.parent_index==std::numeric_limits<uint32_t>::max();
                });
                if(root!=hierarchy.end()){
                    const uint32_t root_index=static_cast<uint32_t>(std::distance(hierarchy.begin(),root));
                    const auto right_chain=articulated_walker_detail::leg_chain(hierarchy,root_index,1U);
                    const uint32_t foot_index=right_chain[3];
                    if(foot_index!=std::numeric_limits<uint32_t>::max()&&foot_index<hierarchy.size()&&
                       !hierarchy[foot_index].sprite_path.empty())
                        seed.articulated_shared_foot_placement_offset=
                            cached_sprite(hierarchy[foot_index].sprite_path).placement_height_offset();
                }
                seed.articulated_contact_geometry_enabled=true;
            }
        }
        thandor::simulation_lab_prepare_model_technologies(*preset,simulation_catalog,seed);
        seeds.push_back(std::move(seed));
    }
    if(seeds.empty()){
        status_="Simulation Lab retail startup requires at least one faction runtime; an empty-base faction needs its class-18 Player / AI start vehicle placed in the LEV.";
        return;
    }
    // V562 snapshots the ARM production records required by classes 11/13/22. The worker remains
    // asset-I/O free: every product that can leave the faction secondary queue carries its exact
    // runtime flags, ARM +0x24/+0x28/+0x2C accounting and a minimal entity template up front.
    const auto faction_technology_masks=document_->faction_technology_masks();
    finish_startup_stage(EditorPerfStage::LabStartSeeds);
    std::vector<thandor::SimulationLabProductionAsset> production_assets;
    if(!simulation_lab_product_cache_.restore(faction_technology_masks,production_assets,model_registry_key)){
    std::unordered_set<uint32_t> production_asset_ids;
    production_assets.reserve(simulation_catalog.army_presets().size());
    for(const auto& preset:simulation_catalog.army_presets()){
        if(preset.registry_id==0U||!production_asset_ids.insert(preset.registry_id).second)continue;
        thandor::SimulationLabProductionAsset product;
        product.army_asset_id=preset.registry_id;
        product.army_runtime_flags14_raw=preset.army_runtime_flags14_raw;
        product.ai_definition_class_value70=preset.ai_definition_class_value70;
        product.ai_definition_class_value74=preset.ai_definition_class_value74;
        product.ai_definition_class_value78=preset.ai_definition_class_value78;
        // V564 retail level-start scan splits class-14 candidates on root MDL +0xC0.
        product.runtime_class_specific_c0_raw=preset.runtime_class_specific_c0_raw;
        product.xenite_cost_q4=preset.retail_xenite_cost_q4;
        product.build_duration_ticks=preset.retail_build_time_q5;
        product.energy_demand_q4=static_cast<int32_t>(preset.retail_build_energy_q4);
        product.placement_effect_id=preset.construction_destruction_effect_id;
        product.placement_effect_ids_prepared=true;
        for(std::size_t faction=0;faction<product.placement_effect_ids_by_faction.size();++faction){
            const auto& mask=faction<faction_technology_masks.size()
                ?faction_technology_masks[faction]:thandor::FactionTechnologyMask256{};
            const auto resolved=thandor::resolve_faction_technology_hierarchy(preset,mask);
            const auto root=std::find_if(resolved.begin(),resolved.end(),[](const auto& node){
                return node.parent_index==std::numeric_limits<uint32_t>::max();
            });
            product.placement_effect_ids_by_faction[faction]=root==resolved.end()
                ?preset.construction_destruction_effect_id
                :root->simulation.construction_destruction_effect_id;
        }
        auto& template_seed=product.entity_seed;
        template_seed.source_placement_index=std::numeric_limits<std::size_t>::max();
        template_seed.army_asset_id=preset.registry_id;
        template_seed.runtime_model_class_id=preset.runtime_model_class_id;
        template_seed.building=thandor::simulation_lab_model_class_is_building(preset.runtime_model_class_id);
        template_seed.has_traversal_profile=preset.has_traversal_profile;
        template_seed.traversal_low_band_index=preset.traversal_low_band_index;
        template_seed.traversal_high_band_index=preset.traversal_high_band_index;
        template_seed.motion=preset.simulation_motion_profile;
        template_seed.water_damage_multiplier=preset.water_damage_multiplier;
        template_seed.water_damage_threshold_q12=preset.water_damage_threshold_q12;
        template_seed.vertical_deployment_step_q12_per_tick=preset.vertical_deployment_step_q12_per_tick;
        template_seed.vertical_deployment_travel_limit_q12=preset.vertical_deployment_travel_limit_q12;
        template_seed.class23_deployment_sound_asset_id=preset.runtime_class_effect_sound_asset_id_26c;
        template_seed.tank_track_texture_travel_scale=preset.track_texture_travel_scale;
        template_seed.articulated_contact_interpolation_rate_q12=
            preset.articulated_contact_interpolation_rate_q12;
        template_seed.articulated_contact_sound_asset_id=preset.articulated_contact_sound_asset_id;
        template_seed.articulated_root_height_q12=preset.articulated_root_height_q12;
        template_seed.articulated_contact_lift_q12=preset.articulated_contact_lift_q12;
        template_seed.articulated_contact_steering_limit_turn16=
            preset.articulated_contact_steering_limit_turn16;
        template_seed.runtime_sound_profile={preset.runtime_model_class_id,
            preset.projected_sound_asset_id_d0,preset.projected_sound_asset_id_d8,
            preset.projected_sound_asset_id_primary,preset.projected_sound_asset_id_secondary};
        template_seed.positioned_sound_gain_q15=preset.positioned_sound_gain_q15;
        template_seed.positioned_sound_maximum_distance_q12=preset.positioned_sound_maximum_distance_q12;
        template_seed.production_compatibility_mask=preset.runtime_class_specific_c4_raw;
        template_seed.production_storage_capacity=preset.runtime_class_specific_c4_raw;
        template_seed.runtime_class_specific_c0_raw=preset.runtime_class_specific_c0_raw;
        template_seed.runtime_class_specific_c4_raw=preset.runtime_class_specific_c4_raw;
        template_seed.runtime_class_specific_19c_raw=preset.runtime_class_specific_19c_raw;
        template_seed.runtime_class_specific_1a8_raw=preset.runtime_class_specific_1a8_raw;
        if(preset.runtime_model_class_id==18U&&preset.runtime_class_specific_c4_raw!=0U){
            if(const auto* definition=simulation_catalog.effect_definition(preset.runtime_class_specific_c4_raw))
                template_seed.class18_conversion_effect_lifetime_ticks=thandor::effect_model_completion_tick(*definition).value_or(0U);
        }
        template_seed.class17_root_child_deployment_enabled=preset.runtime_uses_class17_root_child_deployment;
        template_seed.root_model_maximum_integrity=preset.root_maximum_integrity;
        template_seed.shot_impact_class_index=preset.shot_impact_class_index;
        template_seed.fog_uncover_radius_q12=preset.fog_uncover_radius_q12;
        for(const auto& entry:preset.technology_panel_entries)
            if(entry.technology_id!=0U&&std::find(template_seed.research_technology_ids.begin(),
               template_seed.research_technology_ids.end(),entry.technology_id)==template_seed.research_technology_ids.end())
                template_seed.research_technology_ids.push_back(entry.technology_id);
        const auto product_hierarchy=preset.resolved_model_hierarchy.empty()
            ?std::span<const thandor::ModelVisualNode>(preset.model_hierarchy)
            :std::span<const thandor::ModelVisualNode>(preset.resolved_model_hierarchy);
        if(!product_hierarchy.empty())
            template_seed.destroyed_replacement_army_asset_id=
                product_hierarchy.front().simulation.destroyed_replacement_army_asset_id;
        template_seed.model_components=thandor::simulation_lab_model_components_from_hierarchy(product_hierarchy);
        for(const auto& node:product_hierarchy)
            if(node.creates_runtime_node())
                template_seed.occupancy_mark_radius_units=std::max(
                    template_seed.occupancy_mark_radius_units,
                    node.simulation.occupancy_mark_radius_units);
        for(const auto& node:product_hierarchy)
            if(node.creates_runtime_node())
                template_seed.fog_reference_height_offset_q12=std::max(
                    template_seed.fog_reference_height_offset_q12,
                    node.simulation.fog_reference_height_offset_q12);
        const auto product_weapon_profiles=simulation_catalog.simulation_weapon_profiles_for_hierarchy(product_hierarchy);
        template_seed.actor.weapons.resize(product_weapon_profiles.size());
        template_seed.combat_weapons.reserve(product_weapon_profiles.size());
        for(std::size_t weapon_index=0;weapon_index<product_weapon_profiles.size();++weapon_index){
            auto weapon=product_weapon_profiles[weapon_index];
            thandor::ShotDefinitionRecord shot{};
            if(const auto* definition=simulation_catalog.shot_definition(weapon.shot_definition_id))shot=*definition;
            else {weapon.shot_definition_id=0U;weapon.shot_definition_resolved=false;}
            template_seed.combat_weapons.push_back({std::move(weapon),std::move(shot)});
            const auto& presentation_weapon=product_weapon_profiles[weapon_index];
            auto& state=template_seed.actor.weapons[weapon_index];
            state.launch_mesh_group_mask=0xFFFFFFFFU;
            if(presentation_weapon.yaw_model_node_index<product_hierarchy.size())
                state.yaw.angle_turn16=product_hierarchy[presentation_weapon.yaw_model_node_index].local_rotation_angle2&0xFFFFU;
            if(presentation_weapon.pitch_model_node_index<product_hierarchy.size())
                state.pitch.angle_turn16=product_hierarchy[presentation_weapon.pitch_model_node_index].local_rotation_angle1&0xFFFFU;
        }
        // V568 snapshots the exact ARM linked-definition selection used by workspace11 and
        // AiArmyCandidate_ComputeFactionWeightedScore.  That scorer selects ARM variants by
        // MDL +0x1C0 technology only; it does not apply the independent MDL +0x238 replacement
        // chain used by runtime rendering, so keep those two selection mechanisms separate.
        const auto select_arm_variant=[](const thandor::ArmyModelVariantGroup& group,
                                         const thandor::FactionTechnologyMask256& mask)
            ->const thandor::ModelTechnologyVariant*{
            const thandor::ModelTechnologyVariant* selected=nullptr;
            for(const auto& candidate:group.variants){
                if(candidate.variant_index==0U)selected=&candidate;
                if(thandor::faction_technology_unlocked(mask,candidate.technology_unlock_id))
                    selected=&candidate;
            }
            return selected;
        };
        const auto variant_root=[](const thandor::ModelTechnologyVariant* variant)
            ->const thandor::ModelVisualNode*{
            if(variant==nullptr)return nullptr;
            const auto found=std::find_if(variant->model_hierarchy.begin(),variant->model_hierarchy.end(),
                [](const auto& node){
                    return node.parent_index==std::numeric_limits<uint32_t>::max();
                });
            return found==variant->model_hierarchy.end()?nullptr:&*found;
        };
        uint32_t root_group_index=std::numeric_limits<uint32_t>::max();
        for(uint32_t group_index=0U;group_index<preset.model_variant_groups.size();++group_index){
            if(preset.model_variant_groups[group_index].parent_group_index==
               std::numeric_limits<uint32_t>::max()){
                root_group_index=group_index;
                break;
            }
        }
        std::vector<uint32_t> direct_child_groups;
        if(root_group_index!=std::numeric_limits<uint32_t>::max()){
            for(uint32_t group_index=0U;group_index<preset.model_variant_groups.size();++group_index)
                if(preset.model_variant_groups[group_index].parent_group_index==root_group_index)
                    direct_child_groups.push_back(group_index);
            std::sort(direct_child_groups.begin(),direct_child_groups.end(),[&](uint32_t left,uint32_t right){
                return preset.model_variant_groups[left].child_slot<
                    preset.model_variant_groups[right].child_slot;
            });
            const auto& root_group=preset.model_variant_groups[root_group_index];
            const auto baseline_variant=std::find_if(root_group.variants.begin(),root_group.variants.end(),
                [](const auto& variant){return variant.variant_index==0U;});
            if(baseline_variant!=root_group.variants.end()){
                if(const auto* root=variant_root(&*baseline_variant)){
                    product.ai_baseline_root_definition_value0c=root->simulation.class_step_q12;
                    product.ai_baseline_root_definition_value18=root->simulation.runtime_field_18_raw;
                    product.ai_baseline_root_definition_value60=root->simulation.maximum_integrity;
                    product.ai_baseline_root_category_maximum_index5c=root->shot_impact_class_index;
                }
            }
            product.ai_faction_score_seeds_prepared=true;
            for(uint32_t faction=0U;faction<product.ai_faction_score_seeds.size();++faction){
                auto& score_seed=product.ai_faction_score_seeds[faction];
                const auto& mask=faction_technology_masks[faction];
                score_seed.all_selected_model_technologies_unlocked=
                    thandor::army_asset_all_selected_model_technologies_unlocked(preset,mask);
                const auto* selected_root_variant=select_arm_variant(root_group,mask);
                const auto* selected_root=variant_root(selected_root_variant);
                if(selected_root==nullptr)continue;
                score_seed.resolved=true;
                score_seed.root_definition_value0c=selected_root->simulation.class_step_q12;
                score_seed.root_definition_value18=selected_root->simulation.runtime_field_18_raw;
                score_seed.root_definition_value60=selected_root->simulation.maximum_integrity;
                score_seed.root_category_maximum_index5c=selected_root->shot_impact_class_index;
                score_seed.child_list_count=static_cast<uint32_t>(direct_child_groups.size());
                for(uint32_t child_index=0U;child_index<score_seed.child_weapons.size()&&
                    child_index<direct_child_groups.size();++child_index){
                    const auto& child_group=preset.model_variant_groups[direct_child_groups[child_index]];
                    const auto* child=variant_root(select_arm_variant(child_group,mask));
                    if(child==nullptr||child->simulation.reload_ticks==0U)continue;
                    const auto* shot=simulation_catalog.shot_definition(child->simulation.shot_definition_id);
                    if(shot==nullptr)continue;
                    auto& weapon=score_seed.child_weapons[child_index];
                    weapon.valid=true;
                    weapon.reload_divisor30=static_cast<int32_t>(child->simulation.reload_ticks);
                    weapon.target_class_impact_damage_q12=shot->target_impact_damage_q12;
                }
            }
        }
        if(preset.runtime_model_class_id==17U)
            thandor::simulation_initialize_class17_deployment(template_seed.actor,true,
                preset.runtime_uses_class17_root_child_deployment);
        thandor::simulation_lab_prepare_ai_product(preset,simulation_catalog,product);
        production_assets.push_back(std::move(product));
    }
    simulation_lab_product_cache_.store(faction_technology_masks,production_assets,model_registry_key);
    }

    finish_startup_stage(EditorPerfStage::LabStartProducts);
    // V564 intentionally does not synthesize starter buildings. Retail level initialization
    // performs its empty-base recovery after normal LEV placements are instantiated: a faction that
    // owns class 18 but no Group-A runtime receives four ARM definitions directly in its primary
    // construction queue. SimulationLabExperiment::reset mirrors that exact branch from the catalog
    // snapshot above, so no editor-only world objects are inserted here.

    // The lab owns the transient actor replacement surface while active. Do not let the ordinary
    // single-world simulator mutate the same SimulationActorState records.
    reset_simulation(true);
    simulation_mode_=false;
    std::array<thandor::SimulationLabFactionEconomySeed,8> faction_economy{};
    for(std::uint32_t faction=1U;faction<faction_economy.size();++faction){
        auto& economy=faction_economy[faction];
        economy.xenite_current_q4=document_->level().faction_xenite_q4(faction);
        economy.tritium_current_q4=document_->level().faction_tritium_q4(faction);
        economy.baseline_xenite_storage_limit_q4=thandor::kSimulationLabRetailInitialStorageLimitQ4;
        economy.baseline_tritium_storage_limit_q4=thandor::kSimulationLabRetailInitialStorageLimitQ4;
        economy.derive_storage_limits_from_components=true;
        economy.derive_energy_allocation_from_components=true;
        // Provisional Q8 unity: original faction terrainContributionScaleQ8 is
        // mutable game runtime state not serialized in the editor's LEV snapshot.
        // Do not label the absolute extraction totals bit-identical until sourced.
        economy.terrain_contribution_scale_q8=0x100U;
        economy.baseline_energy_supply_q4=thandor::kSimulationLabRetailInitialEnergySupplyQ4;
        economy.baseline_energy_generation_capacity_q4=
            thandor::kSimulationLabRetailInitialEnergyGenerationCapacityQ4;
        if(simulation_lab_storage_limit_overrides_enabled_){
            economy.xenite_storage_limit_q4=simulation_lab_xenite_storage_limit_q4_;
            economy.tritium_storage_limit_q4=simulation_lab_tritium_storage_limit_q4_;
            economy.xenite_storage_limit_locked=true;
            economy.tritium_storage_limit_locked=true;
        }
    }
    const auto technology_assets=thandor::simulation_lab_prepare_technologies(simulation_catalog,&document_->level());
    // Lifetime extracted-resource totals are distinct from current stock. The LEV/editor metadata
    // does not persist those retail runtime counters, so a fresh Lab run starts them at zero rather
    // than falsely crediting the faction's starting Xenite/Tritium as extracted resources.
    std::array<thandor::SimulationLabFactionScoreSeed,8> faction_score_seeds{};
    thandor::SimulationLabScenarioRuntimeSeed scenario_runtime;
    scenario_runtime.terrain_lighting_cycle_active=document_->level().lighting().cycle_duration_ticks!=0U;
    {
        const auto stage=editor_performance_log_.measure(EditorPerfStage::LabScenarioBlockers);
        scenario_runtime.static_navigation_blockers=thandor::simulation_lab_prepare_static_navigation_blockers(
            simulation_catalog,document_->level());
    }
    {
        const auto stage=editor_performance_log_.measure(EditorPerfStage::LabScenarioRuntimeCatalog);
        scenario_runtime.runtime_catalog=simulation_lab_runtime_catalog_cache_.get_or_prepare(model_registry_key,[&]{
            return thandor::simulation_lab_prepare_runtime_catalog(simulation_catalog);
        });
    }
    simulation_lab_audio_catalog_=scenario_runtime.runtime_catalog;
    {
        const auto stage=editor_performance_log_.measure(EditorPerfStage::LabScenarioEnvironment);
        const auto environment=thandor::simulation_lab_prepare_environment(
            simulation_catalog,document_->level(),document_->field(),*scenario_runtime.runtime_catalog);
        scenario_runtime.crushable_scenery=environment.crushable_scenery;
        scenario_runtime.environmental_emitters=environment.emitters;
        scenario_runtime.static_shot_hulls=environment.collision_hulls;
        scenario_runtime.static_placement_owners=environment.placement_owners;
    scenario_runtime.initial_army_placements=environment.initial_army_placements;
    }
    scenario_runtime.enabled=true;
    scenario_runtime.active_view_faction_index=std::min(selected_faction_,7U);
    {
        const auto stage=editor_performance_log_.measure(EditorPerfStage::LabScenarioSound);
        scenario_runtime.mapped_terrain_sound_asset_ids=
            thandor::simulation_lab_prepare_sound_asset_indices(simulation_catalog,document_->level());
    }
    scenario_runtime.assignable_faction_count=document_->level().frontend_assignable_faction_count();
    scenario_runtime.active_faction_count=document_->level().frontend_active_faction_count();
    // Preserve the historical all-AI experiment default, but allow a recording comparison
    // to carry its explicit TICK human mask and Q8 game speed. These are transient scenario
    // inputs; they never rewrite multiplayer assignment, the LEV, or relation masks.
    scenario_runtime.simulate_all_active_factions_as_ai=
        simulation_lab_simulate_all_active_factions_as_ai_;
    if(!simulation_lab_simulate_all_active_factions_as_ai_)
        scenario_runtime.human_faction_mask=simulation_lab_human_faction_mask_;
    scenario_runtime.game_speed_q8=std::max(1U,simulation_lab_game_speed_q8_);
    scenario_runtime.relation_ui_flags=document_->level().relation_ui_flags();
    scenario_runtime.relation_state8_faction_group_masks=
        document_->level().relation_state8_faction_group_masks();
    scenario_runtime.relation_state4_faction_group_masks=
        document_->level().relation_state4_faction_group_masks();
    for(uint32_t i=0U;i<thandor::LevelAsset::kScheduledConditionCount;++i)
        scenario_runtime.scheduled_conditions[i]=document_->level().scheduled_condition(i);
    for(uint32_t i=0U;i<thandor::LevelAsset::kEndConditionTriggerCount;++i)
        scenario_runtime.end_condition_triggers[i]=document_->level().end_condition_trigger(i);
    animate_buildings_=true;
    animate_units_=true;
    simulation_static_replacement_dirty_=true;
    end_simulation_lab_diagnostics("restart");
    finish_startup_stage(EditorPerfStage::LabStartScenario);
    simulation_lab_worker_.start(simulation_lab_config_,ai_knowledge_->parameters(),seeds,
        faction_economy,production_assets,technology_assets,faction_technology_masks,
        faction_score_seeds,document_->field(),scenario_runtime);
    finish_startup_stage(EditorPerfStage::LabStartWorker);
    simulation_lab_snapshot_=simulation_lab_worker_.snapshot();
    simulation_lab_render_asset_signature_=0U;
    simulation_lab_resident_effect_ids_.clear();
    simulation_lab_suppressed_placements_.clear();
    simulation_runtime_factions_.clear();
    ++simulation_runtime_owner_revision_;
    simulation_lab_auto_fog_preview_=fog_preview_mode_==0;
    // The Lab draws all four factions over one map. The previous automatic faction-1-only
    // view made successful units from factions 2..4 appear unable to discover any terrain.
    // Keep manually selected single-faction fog unchanged, but default the Lab to the labelled
    // union view so every moving faction contributes. No alliance state is invented.
    if(simulation_lab_auto_fog_preview_)fog_preview_mode_=2;
    fog_preview_history_lab_run_=-1;
    simulation_lab_last_observed_one_shot_sequence_.assign(simulation_lab_runs().size(),0U);
    simulation_lab_snapshot_dirty_=true;
    simulation_lab_running_=true;
    simulation_lab_paused_=false;
    simulation_lab_selected_run_=std::clamp(simulation_lab_selected_run_,0,
        static_cast<int>(simulation_lab_runs().size())-1);
    simulation_lab_last_schedule_time_seconds_=ImGui::GetTime();
    simulation_lab_tick_accumulator_ms_=0.0;
    simulation_lab_schedule_initialized_=true;
    refresh_simulation_lab_ui_snapshot(simulation_lab_last_schedule_time_seconds_,true);
    sync_selected_simulation_lab_run();
    simulation_static_replacement_dirty_=true;
    last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
    status_="Started "+std::to_string(simulation_lab_runs().size())+" simulation runs.";
    begin_simulation_lab_diagnostics();
    finish_startup_stage(EditorPerfStage::LabStartFinalize);
}

void EditorApp::clear_simulation_lab(){
    selected_simulation_lab_entity_.reset();
    end_simulation_lab_diagnostics("clear");
    const bool had_replacements=!simulation_actor_states_.empty()||
        !simulation_lab_suppressed_placements_.empty();
    simulation_lab_worker_.clear();
    simulation_lab_snapshot_.reset();
    simulation_lab_audio_catalog_.reset();simulation_lab_loop_sound_requests_.clear();
    simulation_lab_ui_snapshot_.reset();
    simulation_lab_ui_debug_state_={};
    simulation_lab_ui_selected_summary_={};
    simulation_lab_ui_text_.clear();
    simulation_lab_last_ui_refresh_time_seconds_=0.0;
    simulation_lab_ui_refresh_initialized_=false;
    simulation_lab_snapshot_dirty_=false;
    simulation_lab_running_=false;
    simulation_lab_paused_=false;
    simulation_lab_last_schedule_time_seconds_=0.0;
    simulation_lab_tick_accumulator_ms_=0.0;
    simulation_lab_schedule_initialized_=false;
    simulation_lab_render_asset_signature_=0U;
    simulation_lab_resident_effect_ids_.clear();
    simulation_lab_suppressed_placements_.clear();
    if(simulation_lab_auto_fog_preview_&&fog_preview_mode_==2)fog_preview_mode_=0;
    simulation_lab_auto_fog_preview_=false;
    fog_preview_history_lab_run_=-1;
    simulation_lab_selected_run_=0;
    simulation_lab_last_observed_one_shot_sequence_.clear();
    simulation_actor_states_.clear();
    simulation_runtime_factions_.clear();
    ++simulation_runtime_owner_revision_;
    stop_all_runtime_looping_sounds();
    simulation_active_actor_count_=0U;
    if(had_replacements)simulation_static_replacement_dirty_=true;
    last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
}

void EditorApp::sync_selected_simulation_lab_run(){
    auto sync_scope=editor_performance_log_.measure(EditorPerfStage::LabSnapshotSync);
    if(!document_||!simulation_lab_active())return;
    auto runs=simulation_lab_runs();
    if(runs.empty())return;
    simulation_lab_selected_run_=std::clamp(simulation_lab_selected_run_,0,
        static_cast<int>(runs.size())-1);
    if(simulation_lab_last_observed_one_shot_sequence_.size()!=runs.size())
        simulation_lab_last_observed_one_shot_sequence_.assign(runs.size(),0U);
    const auto selected_run_index=static_cast<std::size_t>(simulation_lab_selected_run_);
    const auto& run=runs[selected_run_index];
    const std::size_t placement_count=document_->level().placements().size();
    const auto sync_started=std::chrono::steady_clock::now();

    // Dynamic Lab geometry can start referencing an ARM/MDL after the static object texture array
    // was built (most visibly when the retail primary queue places its first building). Rebuild the
    // static texture/object surface only when the selected run's render asset set or technology mask
    // changes, never merely because positions changed on another 20 Hz snapshot.
    std::uint64_t render_signature=1469598103934665603ULL;
    const auto mix_render_signature=[&](std::uint64_t value){
        render_signature^=value;
        render_signature*=1099511628211ULL;
    };
    for(const auto key:thandor::simulation_lab_resident_render_assets(run))mix_render_signature(key);
    // Depth shadows use the live vertex stream: another instance of the same
    // ARM/faction/hierarchy needs no new texture atlas or static scene rebuild.
    mix_render_signature(selected_run_index);
    std::vector<uint64_t> hierarchy_variants;
    for(const auto& entity:run.entities){
        if(!entity.synthetic&&!entity.runtime_removed&&entity.source_placement_index<placement_count){
            mix_render_signature(entity.source_placement_index);
            mix_render_signature(entity.faction_index);
        }
        if(!entity.runtime_model_hierarchy)continue;
        uint64_t variant=1469598103934665603ULL;
        const auto mix=[&](uint64_t value){variant=(variant^value)*1099511628211ULL;};
        mix(entity.army_asset_id);mix(entity.faction_index);mix(entity.runtime_model_hierarchy->size());
        for(const auto& node:*entity.runtime_model_hierarchy){
            for(const unsigned char ch:node.sprite_path)mix(ch);
            mix(node.sprite_path.size());mix(node.model_definition_id);mix(node.parent_index);
        }
        hierarchy_variants.push_back(variant);
    }
    std::sort(hierarchy_variants.begin(),hierarchy_variants.end());
    hierarchy_variants.erase(std::unique(hierarchy_variants.begin(),hierarchy_variants.end()),hierarchy_variants.end());
    for(const auto variant:hierarchy_variants)mix_render_signature(variant);
    std::unordered_set<std::size_t> suppressed_placements;
    for(const auto& entity:run.entities){
        if(!entity.synthetic&&!entity.runtime_removed)continue;
        if(entity.source_placement_index<placement_count) {
            suppressed_placements.insert(entity.source_placement_index);
            if (entity.runtime_removed && selected_placement_ == entity.source_placement_index) {
                selected_placement_.reset();selected_simulation_lab_entity_.reset(); selected_edit_index_.reset();
            }
        }
    }
    if(suppressed_placements!=simulation_lab_suppressed_placements_){
        simulation_lab_suppressed_placements_=std::move(suppressed_placements);
        simulation_static_replacement_dirty_=true;
    }
    for(const auto& mask:run.faction_technology_masks)
        for(const auto word:mask)mix_render_signature(word);
    for(const auto redirect:run.faction_merge_redirect)mix_render_signature(redirect);

    // Consume every historical one-shot generated after the previously observed worker generation.
    // The persistent event chain is part of the immutable snapshot, so UI polling may skip any
    // number of intermediate snapshots without losing the worker-tick pose that emitted the event.
    const auto one_shots=thandor::simulation_lab_one_shot_events_after(run,
        simulation_lab_last_observed_one_shot_sequence_[selected_run_index]);
    if(simulation_lab_audio_catalog_){
        auto audio=thandor::simulation_lab_audio_frame(run,*simulation_lab_audio_catalog_,one_shots,
            document_?&document_->field():nullptr);
        simulation_lab_loop_sound_requests_=std::move(audio.ordered_loops);
        for(const auto& request:audio.global_one_shots)
            if(request.listener_faction_index==std::min(selected_faction_,7U))
                play_runtime_global_sound(request.sound_table_index);
        for(const auto& request:audio.one_shots){
            if(fog_preview_mode_!=0&&!fog_preview_point_visible(request.world_position)){++audio_fog_filtered_;continue;}
            play_runtime_spatial_sound(request.sound_table_index,request.world_position,
                request.gain_q15,request.maximum_distance_q12);
        }
    }
    for(const auto& event:one_shots){
        if(event.kind==thandor::SimulationLabOneShotEventKind::GlobalMappedTerrainSound)continue;
        if(event.kind==thandor::SimulationLabOneShotEventKind::PositionedEffectSound)continue;
        if(event.kind==thandor::SimulationLabOneShotEventKind::ModelTimedEffect){
            // This historical event can also have a still-linked EFF owner.
            // Draw its live runtime slot once, not once from each representation.
            const bool has_live_owner=std::any_of(run.environmental_effects.begin(),
                run.environmental_effects.end(),[&](const auto& effect){
                    return !effect.world_pool_only&&effect.effect_id==event.effect_id && effect.spawn_tick==event.tick &&
                        std::abs(effect.pose.position.x-event.root_world_position.x)<1.e-4F &&
                        std::abs(effect.pose.position.y-event.root_world_position.y)<1.e-4F &&
                        std::abs(effect.pose.position.z-event.root_world_position.z)<1.e-4F;
                });
            if(has_live_owner)continue;
            if(event.effect_id!=0U&&catalog_.effect_definition(event.effect_id)!=nullptr){
                auto orientation=placement_root_transform(event.root_world_position.x,event.root_world_position.y,
                    event.root_world_position.z,thandor::turn16_to_radians(event.actor.body_yaw_turn16));
                if(event.effect_world_basis)orientation.basis=*event.effect_world_basis;
                movement_ground_effects_.push_back(MovementGroundEffectInstance{event.source_placement_index,
                    event.effect_id,event.tick,event.root_world_position,orientation,true,false,event.effect_world_angles});
            }
            continue;
        }
        if(event.kind==thandor::SimulationLabOneShotEventKind::ConstructionPlacementEffect){
            if(event.effect_id!=0U&&catalog_.effect_definition(event.effect_id)!=nullptr){
                Vec3f effect_root=event.root_world_position;
                auto orientation=placement_root_transform(effect_root.x,effect_root.y,effect_root.z,
                    thandor::turn16_to_radians(event.actor.body_yaw_turn16));
                const auto* preset=catalog_.army_preset(event.army_asset_id);
                if(document_&&preset!=nullptr){
                    const auto& mask=event.faction_index<run.faction_technology_masks.size()
                        ?run.faction_technology_masks[event.faction_index]
                        :thandor::FactionTechnologyMask256{};
                    const auto resolved=thandor::resolve_faction_technology_hierarchy(*preset,mask);
                    const auto root=std::find_if(resolved.begin(),resolved.end(),[](const auto& node){
                        return node.parent_index==std::numeric_limits<uint32_t>::max();
                    });
                    const int32_t contact_kind=root==resolved.end()
                        ?preset->simulation_motion_profile.placement_contact_kind
                        :root->simulation.placement_contact_kind;
                    float placement_height_offset=0.0F;
                    if(root!=resolved.end()&&!root->sprite_path.empty())
                        placement_height_offset=cached_sprite(root->sprite_path).placement_height_offset();
                    const auto& field=document_->field();
                    auto terrain=field.sample_terrain(effect_root.x,effect_root.y);
                    const auto fixed=field.sample_retail_surface_heights_q12(
                        thandor::float_to_q12(effect_root.x),thandor::float_to_q12(effect_root.y));
                    if(fixed.valid)terrain.height=thandor::q12_to_float(fixed.terrain);
                    auto contact=resolve_placement_contact_pose(contact_kind,terrain,
                        fixed.valid?thandor::q12_to_float(fixed.water_surface):field.sample_water_surface_height(effect_root.x,effect_root.y),
                        fixed.valid?thandor::q12_to_float(fixed.top):field.sample_top_surface_height(effect_root.x,effect_root.y),placement_height_offset);
                    if(contact_kind==2)contact.packed_normal_angles=thandor::retail_contact_normal_angles(field,
                        thandor::float_to_q12(effect_root.x),thandor::float_to_q12(effect_root.y));
                    if(contact_kind!=3)contact.height+=thandor::q12_to_float(root==resolved.end()
                        ?preset->simulation_motion_profile.placement_height_offset_q12
                        :root->simulation.placement_height_offset_q12);
                    if(contact_kind==3)
                        contact.height+=thandor::q12_to_float(preset->articulated_root_height_q12);
                    effect_root.z=contact.height;
                    orientation=contact.align_to_normal
                        ?terrain_contact_root_transform(effect_root,
                            thandor::turn16_to_radians(event.actor.body_yaw_turn16),contact.normal,contact.packed_normal_angles)
                        :placement_root_transform(effect_root.x,effect_root.y,effect_root.z,
                            thandor::turn16_to_radians(event.actor.body_yaw_turn16));
                }
                movement_ground_effects_.push_back(MovementGroundEffectInstance{
                    event.source_placement_index,event.effect_id,event.tick,effect_root,orientation,true,false});
            }
            continue;
        }
        if(event.kind==thandor::SimulationLabOneShotEventKind::WeaponLaunchEffect||
           event.kind==thandor::SimulationLabOneShotEventKind::WeaponImpactEffect||
           event.kind==thandor::SimulationLabOneShotEventKind::PioneerConversionEffect){
            if(event.effect_id!=0U&&catalog_.effect_definition(event.effect_id)!=nullptr){
                auto orientation=placement_root_transform(event.root_world_position.x,
                    event.root_world_position.y,event.root_world_position.z,
                    thandor::turn16_to_radians(event.actor.body_yaw_turn16));
                movement_ground_effects_.push_back(MovementGroundEffectInstance{
                    event.source_placement_index,event.effect_id,event.tick,event.root_world_position,
                    orientation,true,false});
            }
            continue;
        }
        if(event.kind==thandor::SimulationLabOneShotEventKind::Class23DeploymentSound){
            // Source MDL +0x26C is a positioned sound table index, not an EFF id.
            // The event owns its historical position, even when this GUI poll skipped ticks.
            continue;
        }
        if(event.kind==thandor::SimulationLabOneShotEventKind::TerminalAttachmentEffect &&
           !event.terminal_attachment_world_points.empty()){
            if(event.effect_id!=0U && catalog_.effect_definition(event.effect_id)!=nullptr){
                for(const auto& point:event.terminal_attachment_world_points){
                    auto orientation=placement_root_transform(point.position.x,point.position.y,
                        point.position.z,0.0F);
                    orientation.basis=point.basis;
                    movement_ground_effects_.push_back(MovementGroundEffectInstance{
                        event.source_placement_index,event.effect_id,event.tick,point.position,
                        orientation,true,false});
                }
            }
            continue;
        }
        if(event.source_placement_index>=placement_count)continue;
        const auto* preset=catalog_.army_preset(event.army_asset_id);
        if(preset==nullptr)continue;
        auto historical_actor=event.actor;
        std::vector<uint8_t> active_nodes;
        auto transforms=simulation_hierarchy_transforms(event.source_placement_index,*preset,
            historical_actor,&active_nodes,true);
        const auto hierarchy=placement_hierarchy(*preset,event.source_placement_index);
        const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){
            return node.parent_index==std::numeric_limits<uint32_t>::max();
        });
        if(root==hierarchy.end())continue;
        const uint32_t root_index=static_cast<uint32_t>(std::distance(hierarchy.begin(),root));
        if(root_index>=transforms.size())continue;

        if(event.kind==thandor::SimulationLabOneShotEventKind::TerminalAttachmentEffect){
            if(event.effect_id==0U||catalog_.effect_definition(event.effect_id)==nullptr||
               event.source_model_node_index>=hierarchy.size()||
               event.source_model_node_index>=transforms.size()||
               event.source_model_node_index>=active_nodes.size()||
               active_nodes[event.source_model_node_index]==0U)continue;
            const auto emit_channel_attachments=[&](std::size_t node_index,
                                                     const ModelHierarchyTransform& effect_orientation){
                if(node_index>=hierarchy.size()||node_index>=transforms.size()||
                   node_index>=active_nodes.size()||active_nodes[node_index]==0U||
                   hierarchy[node_index].sprite_path.empty())return;
                const auto& sprite=cached_sprite(hierarchy[node_index].sprite_path);
                for(const auto& lookup:sprite.lookup_records()){
                    if(lookup.key_class().value()!=3U||
                       lookup.key_index().value()!=event.attachment_channel)continue;
                    const Vec3f origin=transform_point(transforms[node_index],lookup.local_translation);
                    auto orientation=effect_orientation;
                    orientation.translation=origin;
                    movement_ground_effects_.push_back(MovementGroundEffectInstance{
                        event.source_placement_index,event.effect_id,event.tick,origin,orientation,true,false});
                }
            };
            // 0x00529980 first scans the selected ModelRuntimeSlot root resource using the
            // slot's live model-node orientation.
            emit_channel_attachments(event.source_model_node_index,
                                     transforms[event.source_model_node_index]);
            // It then repeats the same (channel<<4)|3 lookup for the direct authored child-zero
            // resource when that child creates a runtime node. The second EFF uses fixed retail
            // angles (0,0x4000,0), while the local point is transformed through childNodes[0].
            for(std::size_t child=0;child<hierarchy.size();++child){
                if(hierarchy[child].parent_index!=event.source_model_node_index||
                   hierarchy[child].child_slot!=0U||
                   hierarchy[child].model_definition_id!=
                       hierarchy[event.source_model_node_index].model_definition_id||
                   !hierarchy[child].creates_runtime_node())continue;
                auto fixed_orientation=fixed_model_rotation_basis(0U,0x4000U,0U);
                fixed_orientation.translation=transforms[child].translation;
                emit_channel_attachments(child,fixed_orientation);
                break;
            }
            continue;
        }

        if(event.kind==thandor::SimulationLabOneShotEventKind::ArticulatedContact){
            const Vec3f sound_origin=transforms[root_index].translation;
            (void)sound_origin; // Audio was submitted from the historical worker event above.
            const auto contact_chain=articulated_walker_detail::leg_chain(
                hierarchy,root_index,event.completed_side&1U);
            const uint32_t foot_index=contact_chain[3];
            if(foot_index==std::numeric_limits<uint32_t>::max()||foot_index>=transforms.size()||
               foot_index>=active_nodes.size()||active_nodes[foot_index]==0U)continue;
            const Vec3f origin=transforms[foot_index].translation;
            const int32_t water_delta=thandor::retail_nearest_water_delta_q12(
                document_->field(),thandor::float_to_q12(origin.x),thandor::float_to_q12(origin.y));
            const uint32_t effect_id=water_delta>0
                ?root->simulation.timed_water_effect_id:root->simulation.timed_effect_id;
            if(effect_id!=0U&&catalog_.effect_definition(effect_id)!=nullptr){
                auto orientation=transforms[foot_index];
                orientation.translation=origin;
                movement_ground_effects_.push_back(MovementGroundEffectInstance{
                    event.source_placement_index,effect_id,event.tick,origin,orientation,true});
            }
        }else if(event.kind==thandor::SimulationLabOneShotEventKind::MovementGroundEffect){
            Vec3f origin=transforms[root_index].translation;
            if(!root->sprite_path.empty()){
                const auto points=cached_sprite(root->sprite_path).type6_attachment_points();
                if(!points.empty())origin=transform_point(transforms[root_index],points.front());
            }
            const int32_t water_delta=thandor::retail_nearest_water_delta_q12(
                document_->field(),thandor::float_to_q12(origin.x),thandor::float_to_q12(origin.y));
            const uint32_t resolved_effect_id=water_delta>0
                ?root->simulation.timed_water_effect_id:root->simulation.timed_effect_id;
            if(resolved_effect_id!=0U&&catalog_.effect_definition(resolved_effect_id)!=nullptr){
                auto orientation=transforms[root_index];
                orientation.translation=origin;
                movement_ground_effects_.push_back(MovementGroundEffectInstance{
                    event.source_placement_index,resolved_effect_id,event.tick,origin,orientation,true});
            }
        }
    }
    // Lab snapshots can advance while update_simulation() is bypassed. Retire
    // their expired one-shots here rather than traversing the entire run history.
    movement_ground_effects_.erase(std::remove_if(movement_ground_effects_.begin(),
        movement_ground_effects_.end(),[&](const auto& effect){
            const auto* definition=catalog_.effect_definition(effect.effect_id);
            return !definition||(run.tick>=effect.spawn_tick&&
                run.tick-effect.spawn_tick>=thandor::effect_lifetime_ticks(*definition));
        }),movement_ground_effects_.end());
    // A one-shot can introduce a texture without introducing an ARM entity.
    // Include effect identities after consuming this snapshot's events. Repeated
    // pulses of an already-resident effect do not rebuild the texture array.
    std::vector<uint32_t> live_effect_ids;
    for(const auto& effect:movement_ground_effects_)simulation_lab_resident_effect_ids_.insert(effect.effect_id);
    if(simulation_lab_active()) {
        const auto selected_runs=simulation_lab_runs();
        if(!selected_runs.empty()) {
            const auto& selected=selected_runs[static_cast<std::size_t>(std::clamp(
                simulation_lab_selected_run_,0,static_cast<int>(selected_runs.size())-1))];
            for(const auto& effect:selected.environmental_effects)
                simulation_lab_resident_effect_ids_.insert(effect.effect_id);
        }
    }
    live_effect_ids.assign(simulation_lab_resident_effect_ids_.begin(),simulation_lab_resident_effect_ids_.end());
    std::sort(live_effect_ids.begin(),live_effect_ids.end());
    live_effect_ids.erase(std::unique(live_effect_ids.begin(),live_effect_ids.end()),live_effect_ids.end());
    for(const auto id:live_effect_ids)mix_render_signature(id);
    if(render_signature!=simulation_lab_render_asset_signature_){
        simulation_lab_render_asset_signature_=render_signature;
        simulation_static_replacement_dirty_=true;
    }
    // Mark every run observed at this snapshot generation. Events from unselected worlds are not
    // replayed later when the user changes the selected run, while the selected world consumed all
    // events since its previously observed sequence above.
    for(std::size_t run_index=0;run_index<runs.size();++run_index)
        simulation_lab_last_observed_one_shot_sequence_[run_index]=
            thandor::simulation_lab_latest_one_shot_event_sequence(runs[run_index]);

    std::unordered_map<std::size_t,thandor::SimulationActorState> selected_states;
    std::unordered_map<std::size_t,uint32_t> selected_runtime_factions;
    selected_states.reserve(run.entities.size());
    selected_runtime_factions.reserve(run.entities.size());
    for(const auto& entity:run.entities){
        if(entity.synthetic||entity.runtime_removed||entity.source_placement_index>=placement_count)continue;
        selected_states[entity.source_placement_index]=entity.actor;
        selected_runtime_factions[entity.source_placement_index]=std::min(entity.faction_index,7U);
        if(entity.runtime_model_class_id==22U&&entity.airport_initialized){
            simulation_airport_states_[entity.source_placement_index]=entity.airport;
            simulation_airport_states_[entity.source_placement_index].animated_subnode_ticks=entity.production_animated_subnode_ticks;
        }
        if(entity.runtime_model_class_id==13U){
            auto& state=simulation_factory_states_[entity.source_placement_index];
            state.phase=static_cast<thandor::SimulationClass13ProductionPhase>(entity.production_behavior_state);
            state.animated_subnode_ticks=entity.production_animated_subnode_ticks;
            state.primary_texture_offset_v=static_cast<int32_t>(entity.production_door_texture_offset_v);
            state.build_elapsed_ticks=entity.production_elapsed_ticks;
            state.build_duration_ticks=entity.production_duration_ticks;
        }
    }
    bool membership_changed=selected_states.size()!=simulation_actor_states_.size();
    if(!membership_changed){
        for(const auto& [index,state]:selected_states){
            (void)state;
            if(!simulation_actor_states_.contains(index)){membership_changed=true;break;}
        }
    }
    const bool owner_identity_changed=selected_runtime_factions!=simulation_runtime_factions_;
    simulation_actor_states_=std::move(selected_states);
    simulation_runtime_factions_=std::move(selected_runtime_factions);

    simulation_active_actor_count_=simulation_actor_states_.size();
    if(membership_changed||owner_identity_changed){
        ++simulation_runtime_owner_revision_;
        simulation_static_replacement_dirty_=true;
    }
    last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
    simulation_lab_last_sync_milliseconds_=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-sync_started).count();
}

bool EditorApp::simulation_lab_active() const noexcept{
    return simulation_lab_snapshot_&&simulation_lab_snapshot_->active();
}

bool EditorApp::simulation_lab_finished() const noexcept{
    return simulation_lab_snapshot_&&simulation_lab_snapshot_->finished();
}

std::span<const thandor::SimulationLabRun> EditorApp::simulation_lab_runs() const noexcept{
    return simulation_lab_snapshot_?simulation_lab_snapshot_->runs():std::span<const thandor::SimulationLabRun>{};
}

std::span<const thandor::SimulationLabRun> EditorApp::simulation_lab_ui_runs() const noexcept{
    return simulation_lab_ui_snapshot_?simulation_lab_ui_snapshot_->runs():std::span<const thandor::SimulationLabRun>{};
}

void EditorApp::refresh_simulation_lab_ui_snapshot(double now_seconds,bool force){
    if(!simulation_lab_snapshot_){
        simulation_lab_ui_snapshot_.reset();
        simulation_lab_ui_debug_state_={};
        simulation_lab_ui_selected_summary_={};
        simulation_lab_ui_text_.clear();
        simulation_lab_last_ui_refresh_time_seconds_=0.0;
        simulation_lab_ui_refresh_initialized_=false;
        return;
    }
    if(!force&&!simulation_lab_ui_refresh_due(now_seconds,simulation_lab_last_ui_refresh_time_seconds_,
            simulation_lab_ui_refresh_initialized_))return;

    auto previous=std::move(simulation_lab_ui_snapshot_);
    simulation_lab_ui_snapshot_=simulation_lab_snapshot_;
    if(previous&&previous!=simulation_lab_ui_snapshot_)
        simulation_lab_worker_.retire_snapshot(std::move(previous));

    refresh_simulation_lab_ui_text();
    simulation_lab_last_ui_refresh_time_seconds_=now_seconds;
    simulation_lab_ui_refresh_initialized_=true;
    append_simulation_lab_diagnostics(force);
}

void EditorApp::refresh_simulation_lab_ui_text(){
    simulation_lab_ui_selected_summary_={};
    const auto ui_runs=simulation_lab_ui_runs();
    simulation_lab_ui_text_.resize(ui_runs.size());
    simulation_lab_ui_maximum_tick_=0;
    for(std::size_t i=0;i<ui_runs.size();++i){
        const auto& run=ui_runs[i];
        auto& text=simulation_lab_ui_text_[i];
        text.label="Run "+std::to_string(i+1);
        text.value=std::to_string(run.swept_value);
        text.tick=std::to_string(run.tick);
        text.built=std::to_string(run.metrics.completed_builds);
        std::size_t buildings=0,units=0;
        for(const auto& entity:run.entities){
            if(entity.runtime_removed||entity.root_model_terminal)continue;
            if(entity.building)++buildings;
            else if(entity.runtime_model_class_id==18U)++units; // Player/AI start vehicle.
            else if(const auto* preset=catalog_.army_preset(entity.army_asset_id);
                    preset&&preset->browser_kind==thandor::PlacementBrowserKind::Unit)++units;
        }
        text.viewport_label=text.label+"   "+text.tick+" ticks\nBldgs "+
            std::to_string(buildings)+"   Units "+std::to_string(units)+"###simlab_run";
        simulation_lab_ui_maximum_tick_=std::max(simulation_lab_ui_maximum_tick_,run.tick);
    }
    simulation_lab_progress_text_=std::to_string(simulation_lab_ui_maximum_tick_)+" / "+
        std::to_string(simulation_lab_config_.duration_ticks)+" ticks";
    if(!ui_runs.empty()){
        const std::size_t run_index=static_cast<std::size_t>(std::clamp(
            simulation_lab_selected_run_,0,static_cast<int>(ui_runs.size())-1));
        const auto& run=ui_runs[run_index];
        auto& summary=simulation_lab_ui_selected_summary_;
        summary.valid=true;
        for(const auto& entity:run.entities)
            if(!entity.runtime_removed&&!entity.root_model_terminal)++summary.entity_count;
        simulation_lab_objects_text_="Objects: "+std::to_string(summary.entity_count);
        simulation_lab_results_text_="Built: "+std::to_string(run.metrics.completed_builds)+
            "   Research: "+std::to_string(run.metrics.research_jobs_completed);
    }
}

bool EditorApp::poll_simulation_lab_snapshot(){
    auto latest=simulation_lab_worker_.snapshot();
    if(!latest||latest==simulation_lab_snapshot_)return false;
    auto previous=std::move(simulation_lab_snapshot_);
    simulation_lab_snapshot_=std::move(latest);
    simulation_lab_worker_.retire_snapshot(std::move(previous));
    simulation_lab_snapshot_dirty_=true;
    sync_selected_simulation_lab_run();
    if(simulation_lab_finished()&&simulation_lab_running_){
        simulation_lab_running_=false;
        status_="Simulation Lab experiment finished.";
    }
    return true;
}

void EditorApp::update_simulation_lab(double now_seconds){
    if(const auto worker_error=simulation_lab_worker_.error();worker_error){
        simulation_lab_running_=false;
        simulation_lab_paused_=true;
        status_="Simulation Lab worker stopped: "+*worker_error;
        if(!simulation_lab_worker_error_logged_&&simulation_lab_diagnostic_writer_!=nullptr){
            simulation_lab_diagnostic_writer_->error(*worker_error);
            simulation_lab_worker_error_logged_=true;
        }
    }
    if(!simulation_lab_active())return;
    simulation_lab_worker_.set_active_view_faction(std::min(selected_faction_,7U));
    if(!simulation_lab_schedule_initialized_){
        simulation_lab_last_schedule_time_seconds_=now_seconds;
        simulation_lab_tick_accumulator_ms_=0.0;
        simulation_lab_schedule_initialized_=true;
    }
    const double elapsed_ms=std::clamp(
        (now_seconds-simulation_lab_last_schedule_time_seconds_)*1000.0,0.0,250.0);
    simulation_lab_last_schedule_time_seconds_=now_seconds;
    if(simulation_lab_running_&&!simulation_lab_paused_&&!simulation_lab_finished()){
        const uint32_t tick_ms=std::clamp(simulation_lab_tick_duration_ms_,1U,5000U);
        const double maximum_accumulator=static_cast<double>(tick_ms)*128.0;
        simulation_lab_tick_accumulator_ms_=std::min(
            simulation_lab_tick_accumulator_ms_+elapsed_ms,maximum_accumulator);
        const auto requested=static_cast<uint32_t>(std::min<double>(128.0,
            std::floor(simulation_lab_tick_accumulator_ms_/static_cast<double>(tick_ms))));
        if(requested!=0U){
            const auto accepted=simulation_lab_worker_.request_advance(requested);
            // Backpressure is not simulated time: keep the rejected ticks in the
            // bounded accumulator and retry when the worker drains its queue.
            simulation_lab_tick_accumulator_ms_-=static_cast<double>(accepted)*tick_ms;
        }
    }else{
        // Pause/finish never accumulate a catch-up burst. Resume starts from the wall-clock moment
        // at which the user explicitly resumes the experiment.
        simulation_lab_tick_accumulator_ms_=0.0;
    }
    const bool snapshot_changed=poll_simulation_lab_snapshot();
    refresh_simulation_lab_ui_snapshot(now_seconds,snapshot_changed&&simulation_lab_finished());
    const std::span<const RuntimeSpatialSoundRequest> requests=
        simulation_lab_paused_||simulation_lab_finished()
            ?std::span<const RuntimeSpatialSoundRequest>{}:simulation_lab_loop_sound_requests_;
    const auto audible=thandor::simulation_lab_audible_loops(requests,
        spatial_audio_listener(),sound_effects_gain_q15_,[&](Vec3f p){
            return fog_preview_mode_==0||fog_preview_point_visible(p);
        });
    audio_fog_filtered_+=audible.fog_filtered;
    audio_inaudible_+=audible.inaudible;
    reconcile_runtime_looping_sounds(audible.requests);
}

void EditorApp::apply_ai_profile(){
    ai_knowledge_=ai_profile_knowledge(ai_profiles_.active());
    const auto index=std::min<uint32_t>(simulation_lab_config_.parameter_index,127U);
    simulation_lab_config_.fixed_value=ai_knowledge_->signed_raw(index);
    ai_profile_draft_index_=-1;
}

void EditorApp::draw_ai_profile_controls(){
    ImGui::SeparatorText("Original KI values");
    ImGui::TextUnformatted("Profile for the next experiment");ImGui::SetNextItemWidth(-1.F);
    if(ImGui::BeginCombo("##KI profile",ai_profiles_.use_custom?"Custom (ki.dat_custom.ini)":"Default (ki.dat.ini)")){
        if(ImGui::Selectable("Default (ki.dat.ini)",!ai_profiles_.use_custom)){
            try{ai_profiles_.select(false);apply_ai_profile();}catch(const std::exception& e){ai_profiles_.message=e.what();}
        }
        if(ai_profiles_.custom_available){
            if(ImGui::Selectable("Custom (ki.dat_custom.ini)",ai_profiles_.use_custom)){
                try{ai_profiles_.select(true);apply_ai_profile();}catch(const std::exception& e){ai_profiles_.message=e.what();}
            }
        }else ImGui::TextDisabled("Save an edited value to create a custom profile.");
        ImGui::EndCombo();
    }
    if(ImGui::Button("Reload KI profile files")){
        try{
            const auto game=ai_profiles_.game;const auto root=ai_profiles_.directory;const bool custom=ai_profiles_.use_custom;
            ai_profiles_.initialize(game,root,custom);apply_ai_profile();
        }catch(const std::exception& e){ai_profiles_.message=e.what();}
    }
    if(simulation_lab_active())ImGui::TextWrapped("Running experiments keep their starting values. Restart the experiment to use a changed profile.");
    if(!ai_profiles_.message.empty())ImGui::TextWrapped("%s",ai_profiles_.message.c_str());
    if(ImGui::CollapsingHeader("Edit KI profile")){
        const auto* selected=thandor::find_ai_knowledge_semantic(ai_profile_parameter_index_);
        char preview[256];std::snprintf(preview,sizeof(preview),"%s [0x%02X]",selected?std::string(selected->label).c_str():"Unmapped original parameter",ai_profile_parameter_index_);
        ImGui::SetNextItemWidth(-1.F);
        if(ImGui::BeginCombo("##KI profile parameter",preview)){
            for(int i=0;i<128;++i){
                const auto* semantic=thandor::find_ai_knowledge_semantic(i);char label[256];
                std::snprintf(label,sizeof(label),"%s [0x%02X]",semantic?std::string(semantic->label).c_str():"Unmapped original parameter",i);
                if(ImGui::Selectable(label,i==ai_profile_parameter_index_))ai_profile_parameter_index_=i;
            }
            ImGui::EndCombo();
        }
        if(ai_profile_draft_index_!=ai_profile_parameter_index_){
            ai_profile_draft_index_=ai_profile_parameter_index_;
            ai_profile_draft_value_=static_cast<int32_t>(ai_profiles_.active()[ai_profile_parameter_index_]);
        }
        ImGui::TextUnformatted("Original integer value");ImGui::SetNextItemWidth(-1.F);
        ImGui::InputInt("##KI profile raw value",&ai_profile_draft_value_);
        if(ai_profile_parameter_index_==0x31||ai_profile_parameter_index_==0x32||ai_profile_parameter_index_==0x34||
           ai_profile_parameter_index_==0x35||ai_profile_parameter_index_==0x36||ai_profile_parameter_index_==0x38)
            ImGui::TextWrapped("This original special-site calculation reads the value from its site entry rather than KI.DAT. Editing this preserved KI slot does not change that calculation.");
        if(selected){
            if(selected->domain==thandor::AiKnowledgeValueDomain::Q12Distance)ImGui::Text("Distance: %.3f world units",double(ai_profile_draft_value_)/4096.);
            else if(selected->domain==thandor::AiKnowledgeValueDomain::Q4Amount)ImGui::Text("Resources: %.3f",double(ai_profile_draft_value_)/16.);
        }
        if(ImGui::Button("Save value to custom KI profile")){
            try{auto edited=ai_profiles_.active();edited[ai_profile_parameter_index_]=static_cast<uint32_t>(ai_profile_draft_value_);ai_profiles_.save_custom(edited);apply_ai_profile();}
            catch(const std::exception& e){ai_profiles_.message=e.what();}
        }
        ImGui::TextWrapped("Default values are kept in ki.dat.ini; saved edits go to ki.dat_custom.ini. Files are beside the editor or external AppImage. Experiment sweeps below override one parameter for that experiment only.");
        ImGui::TextWrapped("Folder: %s",ai_profiles_.directory.string().c_str());
    }
    if(ImGui::CollapsingHeader("AI planning lists")){
        for(const auto& workspace:thandor::original_ai_workspace_descriptions()){
            ImGui::TextWrapped("%.*s",int(workspace.name.size()),workspace.name.data());
            show_item_tooltip(std::string(workspace.purpose)+" (native list "+std::to_string(workspace.index)+")");
        }
    }
}

void EditorApp::draw_simulation_lab_panel(){
    if(!document_){ImGui::TextDisabled("Load a map to run experiments.");return;}
    const auto semantics=thandor::known_ai_knowledge_semantics();
    if(!ai_knowledge_){
        ImGui::TextWrapped("KI.DAT unavailable: %s",ai_knowledge_load_error_.c_str());
        return;
    }
    if(semantics.empty()){ImGui::TextDisabled("No KI parameters available.");return;}

    draw_ai_profile_controls();
    if(ImGui::Button(simulation_lab_active()?"Restart experiment":"Start experiment",{-1.0F,0.0F}))start_simulation_lab();
    if(simulation_lab_active()){
        if(ImGui::Button(simulation_lab_paused_?"Resume":"Pause")){
            simulation_lab_paused_=!simulation_lab_paused_;
            simulation_lab_worker_.set_paused(simulation_lab_paused_);
            simulation_lab_running_=!simulation_lab_finished();
            simulation_lab_last_schedule_time_seconds_=ImGui::GetTime();
            simulation_lab_tick_accumulator_ms_=0.0;
            simulation_lab_schedule_initialized_=true;
        }
        ImGui::SameLine();
        if(ImGui::Button("Clear"))clear_simulation_lab();
    }
    if(ImGui::Checkbox("Show other runs",&simulation_lab_show_all_runs_)){
        // Defer the dynamic replacement composition until the next Update stage;
        // synchronous rebuilding here also leaves a dirty flag for a redundant
        // rebuild on the following frame (an avoidable UI stall).
        simulation_static_replacement_dirty_=true;
        last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
    }
    bool logging=logging_enabled_;
    if(ImGui::Checkbox("Create diagnostic logs",&logging))set_logging_enabled(logging);
    if(ImGui::IsItemHovered()){
        if(logging_enabled_&&!simulation_lab_diagnostic_path_.empty())show_item_tooltip(simulation_lab_diagnostic_path_);
        else show_item_tooltip("Enable or stop editor performance and Simulation Lab log files. Saved for the next session.");
    }

    if(simulation_lab_active()){

    auto runs=simulation_lab_ui_runs();
    simulation_lab_selected_run_=std::clamp(simulation_lab_selected_run_,0,
        std::max(0,static_cast<int>(runs.size())-1));
    const float progress=simulation_lab_config_.duration_ticks==0U?0.0F:
        std::clamp(static_cast<float>(simulation_lab_ui_maximum_tick_)/simulation_lab_config_.duration_ticks,0.0F,1.0F);
    ImGui::ProgressBar(progress,{-1.0F,0.0F},simulation_lab_progress_text_.c_str());

    ImGui::SeparatorText("Runs");
    if(ImGui::BeginTable("simulation_lab_runs",4,
        ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,
        ImVec2(0.0F,std::min(ui_px(330.0F),ui_px(80.0F)+static_cast<float>(runs.size())*ui_px(24.0F))))){
        ImGui::TableSetupColumn("Run");ImGui::TableSetupColumn("KI value");
        ImGui::TableSetupColumn("Tick");ImGui::TableSetupColumn("Built");
        ImGui::TableHeadersRow();
        for(std::size_t row=0;row<runs.size();++row){
            const auto& text=simulation_lab_ui_text_[row];
            ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
            const bool selected=simulation_lab_selected_run_==static_cast<int>(row);
            if(ImGui::Selectable(text.label.c_str(),selected,ImGuiSelectableFlags_SpanAllColumns))
                select_simulation_lab_run(row);
            ImGui::TableSetColumnIndex(1);ImGui::TextUnformatted(text.value.c_str());
            ImGui::TableSetColumnIndex(2);ImGui::TextUnformatted(text.tick.c_str());
            ImGui::TableSetColumnIndex(3);ImGui::TextUnformatted(text.built.c_str());
        }
        ImGui::EndTable();
    }
    if(!runs.empty()){
        const auto& run=runs[static_cast<std::size_t>(simulation_lab_selected_run_)];
        ImGui::SeparatorText("Selected run");
        ImGui::TextUnformatted(simulation_lab_finished()?"Finished":simulation_lab_paused_?"Paused":"Running");
        ImGui::TextUnformatted(simulation_lab_objects_text_.c_str());
        ImGui::TextUnformatted(simulation_lab_results_text_.c_str());
        if(ImGui::TreeNode("Faction results")){
            for(std::size_t faction=1;faction<run.faction_economy.size();++faction){
                if(run.active_faction_count!=0U && faction>run.active_faction_count)break;
                const uint32_t family=graphics_family_for_faction(static_cast<uint32_t>(faction));
                ImGui::TextColored(graphics_family_ui_text_color(family),"%s · Faction %zu",
                    thandor::graphics_family_single_player_faction_name(family),faction);
                const auto& economy=run.faction_economy[faction];
                ImGui::Text("Xenite %.1f   Tritium %.1f   Points %d",
                    economy.xenite_current_q4/16.0,economy.tritium_current_q4/16.0,run.faction_score[faction].points);
            }
            ImGui::TreePop();
        }
    }
    }
    if(ImGui::CollapsingHeader("Experiment settings",ImGuiTreeNodeFlags_DefaultOpen)){
    ImGui::SeparatorText("Experiment");
    int run_count=static_cast<int>(simulation_lab_config_.run_count);
    ImGui::TextUnformatted("Concurrent games");
    ImGui::SetNextItemWidth(-1.0F);
    if(ImGui::SliderInt("##Concurrent games",&run_count,1,32))
        simulation_lab_config_.run_count=static_cast<uint32_t>(std::clamp(run_count,1,32));
    int thread_limit=static_cast<int>(simulation_lab_config_.worker_thread_limit);
    ImGui::TextUnformatted("Threads (0 = automatic)");
    ImGui::SetNextItemWidth(-1.0F);
    if(ImGui::SliderInt("##Threads (0 = automatic)",&thread_limit,0,32))
        simulation_lab_config_.worker_thread_limit=static_cast<uint32_t>(std::clamp(thread_limit,0,32));

    int tick_duration_ms=static_cast<int>(simulation_lab_tick_duration_ms_);
    ImGui::TextUnformatted("Tick interval (ms)");
    ImGui::SetNextItemWidth(-1.0F);
    if(ImGui::InputInt("##Tick interval (ms)",&tick_duration_ms,1,10)){
        simulation_lab_tick_duration_ms_=static_cast<uint32_t>(std::clamp(tick_duration_ms,1,5000));
        simulation_lab_tick_accumulator_ms_=0.0;
        simulation_lab_last_schedule_time_seconds_=ImGui::GetTime();
        simulation_lab_schedule_initialized_=true;
    }

    ImGui::Checkbox("Simulate every active faction as AI",
        &simulation_lab_simulate_all_active_factions_as_ai_);
    if(!simulation_lab_simulate_all_active_factions_as_ai_){
        ImGui::TextUnformatted("Human faction bit mask");
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputScalar("##Human faction bit mask",ImGuiDataType_U32,
            &simulation_lab_human_faction_mask_);

    }else{

    }
    ImGui::TextUnformatted("Game speed (256 = 1x)");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputScalar("##Game speed (256 = 1x)",ImGuiDataType_U32,&simulation_lab_game_speed_q8_);
    simulation_lab_game_speed_q8_=std::max(1U,simulation_lab_game_speed_q8_);

    ImGui::Checkbox("Override storage capacity",
        &simulation_lab_storage_limit_overrides_enabled_);
    if(simulation_lab_storage_limit_overrides_enabled_){
        int xenite_capacity=static_cast<int>(simulation_lab_xenite_storage_limit_q4_);
        int tritium_capacity=static_cast<int>(simulation_lab_tritium_storage_limit_q4_);
        ImGui::TextUnformatted("Xenite storage capacity Q4");
        ImGui::SetNextItemWidth(-1.0F);
        if(ImGui::InputInt("##Xenite storage capacity Q4",&xenite_capacity,16,160))
            simulation_lab_xenite_storage_limit_q4_=static_cast<uint32_t>(std::max(0,xenite_capacity));
        ImGui::TextUnformatted("Tritium storage capacity Q4");
        ImGui::SetNextItemWidth(-1.0F);
        if(ImGui::InputInt("##Tritium storage capacity Q4",&tritium_capacity,16,160))
            simulation_lab_tritium_storage_limit_q4_=static_cast<uint32_t>(std::max(0,tritium_capacity));
    }else{

    }

    if(ImGui::CollapsingHeader("Starting inventory")){

        for(std::uint32_t faction=1U;faction<simulation_lab_config_.primary_queue_overrides.size();++faction){
            auto& override=simulation_lab_config_.primary_queue_overrides[faction];
            ImGui::PushID(static_cast<int>(faction));
            const uint32_t override_family=graphics_family_for_faction(faction);
            const std::string faction_label=faction_single_player_label(faction,override_family,false)+" override";
            ImGui::PushStyleColor(ImGuiCol_Text,graphics_family_ui_text_color(override_family));
            ImGui::Checkbox(faction_label.c_str(),&override.enabled);
            ImGui::PopStyleColor();
            if(override.enabled){
                int count=static_cast<int>(std::min<std::uint32_t>(override.count,
                    static_cast<std::uint32_t>(override.army_asset_ids.size())));
                ImGui::TextUnformatted("Item count");
                ImGui::SetNextItemWidth(-1.0F);
                if(ImGui::InputInt("##Item count",&count,1,4))
                    override.count=static_cast<std::uint32_t>(std::clamp(count,0,
                        static_cast<int>(override.army_asset_ids.size())));
                const std::size_t visible_count=std::min<std::size_t>(override.count,override.army_asset_ids.size());
                if(visible_count!=0U){
                    const bool child_visible=ImGui::BeginChild("PrimaryArmList",ImVec2(0.0F,
                        std::min(ui_px(220.0F),ui_px(32.0F)*static_cast<float>(visible_count)+8.0F)),true);
                    if(child_visible){
                        for(std::size_t index=0;index<visible_count;++index){
                            ImGui::PushID(static_cast<int>(index));
                            ImGui::SetNextItemWidth(ui_px(110.0F));
                            ImGui::TextUnformatted("ARM");
                            ImGui::SetNextItemWidth(-1.0F);
                            ImGui::InputScalar("##ARM",ImGuiDataType_U32,&override.army_asset_ids[index]);
                            if(const auto* preset=catalog_.army_preset(override.army_asset_ids[index])){
                                ImGui::SameLine();
                                const std::string& label=!preset->display_name.empty()?preset->display_name:preset->short_name;
                                ImGui::TextDisabled("%s",label.empty()?"resolved":label.c_str());
                            }else{
                                ImGui::SameLine();
                                ImGui::TextDisabled("unresolved");
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();
                }
            }
            ImGui::PopID();
        }
    }

    // Retail simulation remains 20 ticks/second; wall cadence controls execution speed only.
    float duration_minutes=static_cast<float>(simulation_lab_config_.duration_ticks)/1200.0F;
    ImGui::TextUnformatted("Duration (simulated minutes)");
    ImGui::SetNextItemWidth(-1.0F);
    if(ImGui::SliderFloat("##Duration (simulated minutes)",&duration_minutes,0.1F,90.0F,"%.1f min"))
        simulation_lab_config_.duration_ticks=static_cast<uint32_t>(std::lround(
            std::clamp(duration_minutes,0.1F,90.0F)*1200.0F));
    if(ImGui::Button("Long progression preset")){
        simulation_lab_config_.duration_ticks=36000U;
        simulation_lab_tick_duration_ms_=1U;
        simulation_lab_tick_accumulator_ms_=0.0;
        simulation_lab_last_schedule_time_seconds_=ImGui::GetTime();
        simulation_lab_schedule_initialized_=true;
    }
    if(ImGui::IsItemHovered())show_item_tooltip(
        "30 simulated minutes with a 1 ms wall interval. Actual speed depends on worker load.");

    ImGui::Checkbox("Vary RNG seed per run",&simulation_lab_config_.vary_seed_per_run);
    ImGui::TextUnformatted("Base seed");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputScalar("##Base seed",ImGuiDataType_U32,&simulation_lab_config_.base_seed);
    if(ImGui::Button("Draw stock host seed")){
        auto primary_state=simulation_lab_config_.base_seed;
        simulation_lab_config_.base_seed=thandor::retail_random_next(primary_state);
    }
    if(ImGui::IsItemHovered())show_item_tooltip(
        "Use the base seed as the primary RNG state and draw a session seed the same way a stock host does.");

    simulation_lab_semantic_row_=std::clamp(simulation_lab_semantic_row_,0,
        static_cast<int>(semantics.size())-1);
    const auto& selected_semantic=semantics[static_cast<std::size_t>(simulation_lab_semantic_row_)];
    char parameter_preview[256];
    std::snprintf(parameter_preview,sizeof(parameter_preview),"KI[0x%02X] %.*s",selected_semantic.index,
        static_cast<int>(selected_semantic.label.size()),selected_semantic.label.data());
    ImGui::TextUnformatted("AI parameter");
    ImGui::SetNextItemWidth(-1.0F);
    if(ImGui::BeginCombo("##AI parameter",parameter_preview)){
        for(std::size_t row=0;row<semantics.size();++row){
            const auto& semantic=semantics[row];
            char label[256]{};
            std::snprintf(label,sizeof(label),"KI[0x%02X] %.*s",semantic.index,
                static_cast<int>(semantic.label.size()),semantic.label.data());
            const bool selected=static_cast<int>(row)==simulation_lab_semantic_row_;
            if(ImGui::Selectable(label,selected)){
                simulation_lab_semantic_row_=static_cast<int>(row);
                simulation_lab_config_.parameter_index=semantic.index;
                const int32_t base=ai_knowledge_->signed_raw(semantic.index);
                simulation_lab_config_.fixed_value=base;
                simulation_lab_config_.minimum_value=0;
                // Sweeps must remain within the signed 32-bit editor control range; avoid UB
                // when a modified KI value is near INT32_MAX. Do not clamp the loaded dword.
                simulation_lab_config_.maximum_value=base==0?0x1000:
                    static_cast<int32_t>(std::clamp<int64_t>(
                        static_cast<int64_t>(base)*2,1,
                        std::numeric_limits<int32_t>::max()));
            }
            if(selected)ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    simulation_lab_config_.parameter_index=
        semantics[static_cast<std::size_t>(simulation_lab_semantic_row_)].index;
    int sweep_mode=static_cast<int>(simulation_lab_config_.sweep_mode);
    const char* sweep_modes[]={"Fixed","Linear min -> max","Min / middle / max groups"};
    ImGui::TextUnformatted("Sweep");
    ImGui::SetNextItemWidth(-1.0F);
    if(ImGui::Combo("##Sweep",&sweep_mode,sweep_modes,3))
        simulation_lab_config_.sweep_mode=static_cast<thandor::SimulationLabSweepMode>(sweep_mode);
    if(simulation_lab_config_.sweep_mode==thandor::SimulationLabSweepMode::Fixed){
        ImGui::TextUnformatted("Experiment override (original integer units)");
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputInt("##Value",&simulation_lab_config_.fixed_value);
        if(ImGui::Button("Use selected KI profile value"))simulation_lab_config_.fixed_value=ai_knowledge_->signed_raw(simulation_lab_config_.parameter_index);
    }else{
        ImGui::TextUnformatted("Minimum");
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputInt("##Minimum",&simulation_lab_config_.minimum_value);
        ImGui::TextUnformatted("Maximum");
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputInt("##Maximum",&simulation_lab_config_.maximum_value);
    }
    if(ImGui::TreeNode("Preview sweep values")){
    const auto preview_values=thandor::simulation_lab_sweep_values(simulation_lab_config_);
    if(!preview_values.empty()){
        std::string preview="Values: ";
        const std::size_t shown=std::min<std::size_t>(preview_values.size(),12U);
        for(std::size_t i=0;i<shown;++i){if(i)preview+=", ";preview+=std::to_string(preview_values[i]);}
        if(preview_values.size()>shown)preview+=" ...";
        ImGui::TextDisabled("%s",preview.c_str());
    }

    ImGui::TreePop();
    }

    }
}

void EditorApp::draw_retail_selection_frame(float left,float top,float right,float bottom,int32_t condition_q12,
    std::optional<uint32_t> faction_rgb,const thandor::RetailSelectionFrameMetrics& metrics){
    if(!std::isfinite(left)||!std::isfinite(top)||!std::isfinite(right)||!std::isfinite(bottom))return;
    // Keep tiny/edge-on selected objects legible at map overview zoom levels.
    const float center_x=(left+right)*.5F,center_y=(top+bottom)*.5F;
    const float half_width=std::max(12.F,(right-left)*.5F),half_height=std::max(12.F,(bottom-top)*.5F);
    left=center_x-half_width;right=center_x+half_width;
    top=center_y-half_height;bottom=center_y+half_height;
    if(!retail_selection_frame_load_attempted_){
        retail_selection_frame_load_attempted_=true;
        try{retail_selection_frame_assets_=thandor::RetailSelectionFrameAssets::parse(
            catalog_.load("gfx/panel/select.dat"),
            thandor::GfxAsset::parse(catalog_.load("gfx/panel/select.gfx")));}
        catch(const std::exception& error){status_=std::string("Selection frame assets unavailable: ")+error.what();}
    }
    auto* draw=ImGui::GetForegroundDrawList();
    const auto canvas=workspace_layout().canvas;
    draw->PushClipRect({canvas.x,canvas.y},{canvas.x+canvas.width,canvas.y+canvas.height},true);
    if(!retail_selection_frame_assets_){
        const auto rgb=faction_rgb.value_or(0xE8E8E8U);
        draw->AddRect({left,top},{right,bottom},IM_COL32((rgb>>16U)&255U,(rgb>>8U)&255U,rgb&255U,255),0.F,0,1.5F);
        draw->PopClipRect();return;
    }
    const auto& assets=*retail_selection_frame_assets_;
    for(const auto& blit:thandor::retail_selection_frame_blits(assets,left,top,right,bottom,condition_q12,metrics)){
        const auto& image=assets.images.at(blit.subresource);
        if(!image.width||!image.height)continue;
        const int width=std::max(0,int(std::lround(blit.width))),height=std::max(0,int(std::lround(blit.height)));
        const int first_x=std::max(0,int(std::floor(canvas.x-blit.x))),last_x=std::min(width,int(std::ceil(canvas.x+canvas.width-blit.x)));
        const int first_y=std::max(0,int(std::floor(canvas.y-blit.y))),last_y=std::min(height,int(std::ceil(canvas.y+canvas.height-blit.y)));
        for(int y=first_y;y<last_y;++y){
            const auto iy=blit.tile_y?uint32_t(y)%image.height:std::min(uint32_t(y),image.height-1U);
            const auto pixel=[&](int x){
                const auto ix=blit.tile_x?uint32_t(x)%image.width:std::min(uint32_t(x),image.width-1U);
                const auto offset=(size_t(iy)*image.width+ix)*4U;
                const uint32_t argb=(uint32_t(image.pixels[offset+3])<<24U)|(uint32_t(image.pixels[offset])<<16U)|
                    (uint32_t(image.pixels[offset+1])<<8U)|image.pixels[offset+2];
                const auto tinted=thandor::retail_selection_pixel_argb(argb,blit.faction_tint?faction_rgb:std::nullopt);
                return IM_COL32((tinted>>16U)&255U,(tinted>>8U)&255U,tinted&255U,tinted>>24U);
            };
            for(int x=first_x;x<last_x;){const auto color=pixel(x);int end=x+1;
                while(end<last_x&&pixel(end)==color)++end;
                if((color>>24U)!=0U)draw->AddRectFilled({blit.x+x,blit.y+y},{blit.x+end,blit.y+y+1},color);
                x=end;
            }
        }
    }
    draw->PopClipRect();
}

bool EditorApp::factory_placement_exit_clear(const thandor::ArmyAssetPreset& preset,
    uint32_t cell_x,uint32_t cell_y,uint32_t heading){
    if(preset.runtime_model_class_id!=13U)return true;
    const auto& field=document_->field();const auto& cell=field.at(cell_x,cell_y);
    thandor::SimulationLabSeedEntity seed;seed.runtime_model_class_id=13U;
    const auto hierarchy=thandor::resolve_faction_technology_hierarchy(preset,
        document_->faction_technology_masks()[std::min(selected_faction_,7U)]);
    const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& n){return n.parent_index==UINT32_MAX;});
    if(root==hierarchy.end()||root->sprite_path.empty())return false;
    seed.motion.placement_contact_kind=root->simulation.placement_contact_kind;
    const auto* exit=cached_sprite(root->sprite_path).find_lookup_record(
        thandor::ModelLookupKeyIndex{1U},thandor::ModelLookupKeyClass{5U});
    if(!exit)return false;
    seed.production_exit_local=exit->local_translation;
    std::vector<thandor::SimulationLabEntity> owners;
    std::span<const thandor::SimulationLabEntity> live_owners;
    if(simulation_lab_active()){
        const auto runs=simulation_lab_runs();
        if(!runs.empty())live_owners=runs[static_cast<size_t>(std::clamp(simulation_lab_selected_run_,0,int(runs.size())-1))].entities;
    }else for(const auto& placement:document_->level().placements()){
        const auto* owner_preset=catalog_.army_preset(placement.army_asset_id);if(!owner_preset)continue;
        thandor::SimulationLabEntity owner;owner.runtime_model_class_id=owner_preset->runtime_model_class_id;
        owner.motion=owner_preset->simulation_motion_profile;
        owner.actor.position={placement.render_x(),placement.render_y(),
            terrain_height_near(placement.render_x(),placement.render_y())};
        owner.actor.body_yaw_turn16=placement.orientation_turn16;
        if(owner.runtime_model_class_id==13U){
            thandor::SimulationLabSeedEntity geometry;
            thandor::simulation_lab_prepare_model_technologies(*owner_preset,catalog_,geometry);
            owner.production_exit_local=geometry.production_exit_local;
            owner.production_root_sprite_height_offset=geometry.production_root_sprite_height_offset;
        }
        owners.push_back(std::move(owner));
    }
    const Vec3f position{thandor::q12_to_float(cell.world_x_q12),thandor::q12_to_float(cell.world_y_q12),cell.terrain_height()};
    const auto owner_view=live_owners.empty()?std::span<const thandor::SimulationLabEntity>{owners}:live_owners;
    const int32_t radius=root->simulation.movement_collision_radius_q12;
    return thandor::simulation_placement_runtime_clear(position,radius,owner_view,field)&&
        thandor::simulation_placement_terrain_clear(field,position,cell.terrain_height_q12,
            static_cast<uint32_t>(std::abs(radius)),seed.motion.placement_contact_kind)&&
        thandor::simulation_placement_factory_exit_clear(field,position,heading,seed,owner_view);
}

bool EditorApp::select_simulation_lab_run(std::size_t index){
    if(!simulation_lab_active())return false;
    const auto runs=simulation_lab_runs();
    if(index>=runs.size()||index==static_cast<std::size_t>(simulation_lab_selected_run_))return false;
    simulation_lab_selected_run_=static_cast<int>(index);
    refresh_simulation_lab_ui_text();
    // One-shot visual events and their texture residency belong to the
    // formerly selected world. Do not draw its particles/effects over the
    // newly selected world or keep their layers resident forever.
    movement_ground_effects_.clear();
    simulation_lab_resident_effect_ids_.clear();
    selected_simulation_lab_entity_.reset();
    selected_placement_.reset();
    // Only the selected world uses the real ARM/MDL/SPR renderer, fog, animation
    // and sound. Other runs keep simulating on worker threads; no per-run GPU
    // framebuffer or full render target is allocated.
    sync_selected_simulation_lab_run();
    simulation_static_replacement_dirty_=true;
    last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
    status_="Selected run "+std::to_string(index+1)+".";
    return true;
}

void EditorApp::draw_simulation_lab_run_switcher(){
    simulation_lab_run_selector_hovered_=false;
    if(!document_||!simulation_lab_active())return;
    const auto runs=simulation_lab_ui_runs();
    if(runs.empty())return;
    const auto canvas=workspace_layout().canvas;
    if(canvas.width<ui_px(205.F)||canvas.height<ui_px(110.F))return;
    // One real 3D viewport, instant selected-world switching. This overlay is
    // interactive even when the Lab inspector is hidden, unlike ghost geometry.
    const float width=std::min(ui_px(224.F),canvas.width-ui_px(24.F));
    const float height=std::min(canvas.height-ui_px(24.F),ui_px(44.F)+ui_px(44.F)*static_cast<float>(runs.size()));
    const float x=canvas.x+ui_px(12.F),y=canvas.y+ui_px(12.F);
    ImGui::SetNextWindowPos({x,y},ImGuiCond_Always);
    ImGui::SetNextWindowSize({width,height},ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88F);
    constexpr ImGuiWindowFlags flags=ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings|
        ImGuiWindowFlags_NoFocusOnAppearing;
    if(ImGui::Begin("Simulation runs##SimLabViewportRuns",nullptr,flags)){
        for(std::size_t index=0;index<runs.size();++index){
            const bool selected=index==static_cast<std::size_t>(simulation_lab_selected_run_);
            ImGui::PushID(static_cast<int>(index));
            if(ImGui::Selectable(simulation_lab_ui_text_[index].viewport_label.c_str(),selected))
                select_simulation_lab_run(index);
            ImGui::PopID();
        }
    }
    // Explicit pointer exclusion prevents an overlay click from also selecting
    // or editing terrain in update_canvas_interaction() in the same UI frame.
    const auto& mouse=ImGui::GetIO().MousePos;
    simulation_lab_run_selector_hovered_=mouse.x>=x&&mouse.y>=y&&
        mouse.x<x+width&&mouse.y<y+height;
    ImGui::End();
}

void EditorApp::draw_simulation_lab_overlay(){
    if(!document_)return;
    if(!simulation_lab_active()){
        const auto vp=camera_.view_projection(workspace_layout().canvas.aspect());
        const auto draw_transient_damage=[&](uint32_t asset,uint32_t faction,
            const thandor::SimulationActorState& state,int32_t integrity,int32_t maximum,
            const SimulationLinkedChildEntity* linked){
            if(!thandor::retail_selection_show_damage_frame(integrity,maximum)||integrity<=0)return;
            const auto* preset=catalog_.army_preset(asset);
            if(!preset||fog_preview_model_intensity(state.position,preset->model_runtime_render_flags)==0U)return;
            auto actor=state;
            const auto hulls=simulation_transient_pick_hulls(asset,faction,actor,linked);
            float left=std::numeric_limits<float>::max(),top=left;
            float right=std::numeric_limits<float>::lowest(),bottom=right;bool any=false;
            for(const auto& hull:hulls)for(const auto& point:hull.corners){
                float x,y,depth;if(!project_to_canvas(point,vp,x,y,depth))continue;
                any=true;left=std::min(left,x);top=std::min(top,y);right=std::max(right,x);bottom=std::max(bottom,y);
            }
            thandor::RetailSelectionFrameMetrics metrics;
            if(simulation_mode_)metrics.top_bar=thandor::retail_selection_reload(
                catalog_.simulation_weapon_profiles_for_hierarchy(preset->resolved_model_hierarchy),state);
            if(any)draw_retail_selection_frame(left,top,right,bottom,int32_t(int64_t(integrity)*4096/maximum),
                faction!=0U?std::optional<uint32_t>{thandor::graphics_family_representative_rgb24(graphics_family_for_faction(faction))}:std::nullopt,metrics);
        };
        for(const auto& [serial,entity]:simulation_produced_entities_){
            (void)serial;draw_transient_damage(entity.army_asset_id,entity.faction_index,entity.actor,
                entity.integrity,entity.maximum_integrity,nullptr);
        }
        for(const auto& [serial,entity]:simulation_linked_child_entities_){
            (void)serial;if(entity.visible)draw_transient_damage(entity.army_asset_id,entity.faction_index,entity.actor,
                entity.integrity,entity.maximum_integrity,&entity);
        }
        return;
    }
    const auto live_runs=simulation_lab_runs();
    if(live_runs.empty())return;
    const auto vp=camera_.view_projection(workspace_layout().canvas.aspect());
    auto* draw=ImGui::GetBackgroundDrawList();
    const auto& selected=live_runs[static_cast<size_t>(std::clamp(simulation_lab_selected_run_,0,int(live_runs.size())-1))];
    for(size_t entity_index=0;entity_index<selected.entities.size();++entity_index){
        const auto& entity=selected.entities[entity_index];
        if(entity.root_model_terminal||entity.runtime_removed)continue;
        const bool selected_entity=(selected_placement_&&entity.source_placement_index==*selected_placement_)||
            (selected_simulation_lab_entity_&&selected_simulation_lab_entity_->first==size_t(simulation_lab_selected_run_)&&
             selected_simulation_lab_entity_->second==entity_index);
        const auto condition=thandor::simulation_lab_hierarchy_condition_ratio_q12(entity);
        const auto* preset=catalog_.army_preset(entity.army_asset_id);
        uint32_t frame_fog_flags=preset?preset->model_runtime_render_flags:0x10U;
        if(entity.runtime_model_hierarchy){
            const auto& hierarchy=*entity.runtime_model_hierarchy;
            const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){
                return node.parent_index==std::numeric_limits<uint32_t>::max();});
            if(root!=hierarchy.end())frame_fog_flags=root->simulation.runtime_render_flags;
        }
        const auto metrics=thandor::retail_selection_metrics(entity);
        const bool reloading=metrics.top_bar&&(*metrics.top_bar)[1]<(*metrics.top_bar)[0];
        const bool unpowered=metrics.energy_active_q4<metrics.energy_total_q4;
        if((condition<4096||selected_entity||reloading||unpowered)&&(entity.root_model_maximum_integrity>0||selected_entity)&&
           fog_preview_model_intensity(entity.actor.position,frame_fog_flags)!=0U){
            auto actor=entity.actor;
            const auto hulls=simulation_transient_pick_hulls(entity.army_asset_id,entity.faction_index,actor,nullptr,&entity);
            float left=std::numeric_limits<float>::max(),top=left;
            float right=std::numeric_limits<float>::lowest(),bottom=right;bool any=false;
            for(const auto& hull:hulls)for(const auto& point:hull.corners){
                float x,y,depth;if(!project_to_canvas(point,vp,x,y,depth))continue;
                any=true;left=std::min(left,x);top=std::min(top,y);right=std::max(right,x);bottom=std::max(bottom,y);
            }
            if(!any&&selected_entity){
                float x,y,depth;
                if(project_to_canvas(entity.actor.position,vp,x,y,depth)){
                    left=x-12.F;right=x+12.F;top=y-12.F;bottom=y+12.F;any=true;
                }
            }
            if(any)draw_retail_selection_frame(left,top,right,bottom,condition,
                thandor::graphics_family_representative_rgb24(graphics_family_for_faction(entity.faction_index)),metrics);
        }
        if(!entity.synthetic||entity.building)continue;
        if(fog_preview_mode_!=0&&!fog_preview_point_visible(entity.actor.position))continue;
        float ax,ay,ad,bx,by,bd;
        if(!project_to_canvas(entity.actor.position,vp,ax,ay,ad))continue;
        const auto color=graphics_family_ui_swatch(graphics_family_for_faction(entity.faction_index),235U);
        draw->AddCircle({ax,ay},4.F,color,12,1.5F);
        if(entity.actor.move_target&&project_to_canvas(*entity.actor.move_target,vp,bx,by,bd)){
            draw->AddLine({ax,ay},{bx,by},color,2.F);
        }
        if(entity.actor.move_waypoint&&project_to_canvas(*entity.actor.move_waypoint,vp,bx,by,bd))
            draw->AddLine({ax,ay},{bx,by},color,2.5F);
    }
    if(!simulation_lab_show_all_runs_)return;
    const auto runs=simulation_lab_ui_runs();
    if(runs.empty())return;
    for(std::size_t run_index=0;run_index<runs.size();++run_index){
        const auto& run=runs[run_index];
        const auto first=std::find_if(run.entities.begin(),run.entities.end(),
            [](const thandor::SimulationLabEntity& entity){return entity.construction_complete;});
        if(first==run.entities.end())continue;
        const Vec3f label_position{first->actor.position.x,first->actor.position.y,
            terrain_height_near(first->actor.position.x,first->actor.position.y)+1.4F};
        float sx,sy,depth;
        if(!project_to_canvas(label_position,vp,sx,sy,depth))continue;
        char label[64]{};
        std::snprintf(label,sizeof(label),"R%zu  KI=%d",run_index+1,run.swept_value);
        const bool selected=static_cast<int>(run_index)==simulation_lab_selected_run_;
        draw->AddText({sx+5.0F,sy-8.0F},selected?IM_COL32(255,255,255,255):IM_COL32(190,205,220,210),label);
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

void EditorApp::draw_retail_field_diagnostics_overlay(){
    if(!document_||!retail_field_diagnostics_)return;
    const auto vp=camera_.view_projection(workspace_layout().canvas.aspect());
    const auto& field=document_->field();
    auto* draw=ImGui::GetBackgroundDrawList();
    const uint32_t selector=static_cast<uint32_t>(std::clamp(retail_field_resource_selector_,0,1));
    for(uint32_t y=0;y<field.height();++y){
        for(uint32_t x=0;x<field.width();++x){
            const auto& cell=field.at(x,y);
            const auto marker=thandor::retail_field_marker_audit(cell.flags_and_material,selector);
            if(!marker.selected_resource_support&&!marker.alternate_resource_support&&
               !marker.fluid_receiver_excluded&&!marker.fluid_source_excluded&&
               !marker.flag_8000_marker)continue;
            const Vec3f terrain_point{thandor::q12_to_float(cell.world_x_q12),
                                      thandor::q12_to_float(cell.world_y_q12),
                                      cell.terrain_height()};
            const Vec3f secondary_surface_point{terrain_point.x,terrain_point.y,cell.water_height()};
            float primary_x=0.0F,primary_y=0.0F,primary_depth=0.0F;
            float secondary_x=0.0F,secondary_y=0.0F,secondary_depth=0.0F;
            const bool primary_visible=project_to_canvas(terrain_point,vp,primary_x,primary_y,primary_depth);
            const bool secondary_visible=project_to_canvas(secondary_surface_point,vp,secondary_x,secondary_y,secondary_depth);
            // V530 B1/B2 and bit-8000 markers consume projectedPointA (primary terrain); AF/B0
            // consume projectedPointB (secondary/top surface). The glyph/color art below remains
            // modern editor diagnostic styling, not the original AEx/Bx sprite atlas.
            if(primary_visible&&marker.selected_resource_support){
                draw->AddCircle({primary_x,primary_y},4.5F,IM_COL32(90,235,145,230),10,1.8F);
            }
            if(primary_visible&&marker.alternate_resource_support){
                draw->AddRect({primary_x-4.0F,primary_y-4.0F},{primary_x+4.0F,primary_y+4.0F},
                    IM_COL32(255,170,70,220),0.0F,0,1.6F);
            }
            if(secondary_visible&&marker.fluid_receiver_excluded){
                draw->AddLine({secondary_x-5.0F,secondary_y},{secondary_x+5.0F,secondary_y},
                    IM_COL32(75,205,255,230),1.8F);
            }
            if(secondary_visible&&marker.fluid_source_excluded){
                draw->AddLine({secondary_x,secondary_y-5.0F},{secondary_x,secondary_y+5.0F},
                    IM_COL32(225,95,255,230),1.8F);
            }
            if(primary_visible&&marker.flag_8000_marker){
                draw->AddCircleFilled({primary_x,primary_y},2.1F,IM_COL32(255,225,80,235),8);
            }
        }
    }
}

void EditorApp::draw_water_selection_overlay(){
    if(!document_||water_selection_count()==0U)return;
    ensure_water_selection_size();
    const auto vp=camera_.view_projection(workspace_layout().canvas.aspect());
    const auto& field=document_->field();
    auto* draw=ImGui::GetBackgroundDrawList();
    for(uint32_t y=0;y<field.height();++y){
        for(uint32_t x=0;x<field.width();++x){
            const std::size_t index=static_cast<std::size_t>(y)*field.width()+x;
            if(index>=water_selection_mask_.size()||water_selection_mask_[index]==0U)continue;
            const auto& cell=field.at(x,y);
            float sx=0.0F,sy=0.0F,depth=0.0F;
            if(!project_to_canvas({thandor::q12_to_float(cell.world_x_q12),
                    thandor::q12_to_float(cell.world_y_q12),cell.terrain_height()+0.12F},
                    vp,sx,sy,depth))continue;
            const bool reference=water_reference_cell_&&water_reference_cell_->first==x&&
                water_reference_cell_->second==y;
            draw->AddCircleFilled({sx,sy},reference?5.5F:3.2F,
                reference?IM_COL32(255,220,70,235):IM_COL32(35,190,255,125),12);
            if(reference)draw->AddCircle({sx,sy},9.0F,IM_COL32(255,245,160,255),18,2.0F);
        }
    }
}

void EditorApp::refresh_custom_maps(){
    std::vector<CustomMapEntry> discovered;
    std::unordered_set<std::string> seen;
    const std::array roots{
        project_maps_directory(),
        catalog_.game_root()/"maps",
        catalog_.game_root()/"ThandorMapEditor"/"maps"
    };
    for(const auto& root:roots){
        std::error_code ec;
        if(!std::filesystem::is_directory(root,ec))continue;
        auto it=std::filesystem::recursive_directory_iterator(
            root,std::filesystem::directory_options::skip_permission_denied,ec);
        const auto end=std::filesystem::recursive_directory_iterator{};
        while(!ec&&it!=end&&discovered.size()<2048U){
            const auto item=*it;
            it.increment(ec);
            std::error_code file_ec;
            if(!item.is_regular_file(file_ec))continue;
            const auto& path=item.path();
            std::string extension=path.extension().string();
            std::transform(extension.begin(),extension.end(),extension.begin(),
                [](unsigned char c){return static_cast<char>(std::tolower(c));});
            if(extension!=".thmap"&&extension!=".lev")continue;
            const auto canonical=std::filesystem::weakly_canonical(path,file_ec);
            const auto identity=(file_ec?path:canonical).string();
            if(!seen.insert(identity).second)continue;
            std::string name=path.lexically_relative(root).generic_string();
            name+=" ["+extension.substr(1)+"]";
            const std::string prefix=root==roots[0]?"Editor maps/":
                (root==roots[1]?"Game maps/":"ThandorMapEditor/maps/");
            discovered.push_back({path,prefix+name});
        }
    }
    std::sort(discovered.begin(),discovered.end(),[](const auto& a,const auto& b){
        return a.label<b.label;
    });
    custom_maps_=std::move(discovered);
}

void EditorApp::load_map(std::size_t group_index){
    auto performance_timer=editor_performance_log_.measure(EditorPerfStage::MapLoad);
    try{
        current_group_=group_index;texture_source_group_=static_cast<int>(group_index);
        document_=std::make_unique<thandor::MapDocument>(thandor::MapDocument::open(catalog_,catalog_.map_groups()[group_index]));
        current_project_path_.clear();current_stock_group_=group_index;
        campaign_editing_mission_.reset();reset_terrain_generator();
        last_opened_map_type_="stock";
        last_opened_map_value_=catalog_.map_groups()[group_index].level_path;
        undo_.clear();redo_.clear();selected_placement_.reset();selected_simulation_lab_entity_.reset();selected_edit_index_.reset();
        reset_simulation(false);simulation_static_replacement_dirty_=false;
        water_selection_mask_.clear();water_reference_cell_.reset();water_selection_stage_=WaterSelectionStage::SelectArea;water_selection_checkpointed_=false;
        asset_preview_cache_.clear();asset_preview_texture_cache_.clear();refresh_scenario_text_buffers();load_materials();rebuild_mesh();center_camera();
        status_="Loaded "+catalog_.map_groups()[group_index].display_name+"; terrain layers "+std::to_string(loaded_material_count_)+"/26"+(terrain_texture_layout_?"; terrain DAT loaded":"; terrain DAT fallback");
    }catch(const std::exception& e){status_=e.what();}
}

bool EditorApp::load_map_from_path(const std::filesystem::path& path){
    auto performance_timer=editor_performance_log_.measure(EditorPerfStage::MapLoad);
    // Keep the core project parse separate from potentially expensive GPU and
    // scene-preview work. A well-formed map must never be reported as an invalid
    // project just because building its visualization runs out of memory.
    std::string stage="resolving the map path";
    bool installed_document=false;
    try{
        if(path.empty())throw std::runtime_error("Select a .thmap or .lev file below, or enter its complete path");
        const auto resolved=(path.is_relative() && !std::filesystem::exists(path))
            ?catalog_.game_root()/path:path;
        if(std::filesystem::is_directory(resolved))throw std::runtime_error(
            "That path is a folder, not a map. Select a file from the list below.");
        std::string extension=resolved.extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(extension!=".lev"&&extension!=".thmap"&&extension!=".pck")
            throw std::runtime_error("Expected a .thmap, .pck editor project or loose .lev file");

        stage="reading and parsing the map project";
        auto loaded=extension==".lev"?
            thandor::MapDocument::open_loose(catalog_,resolved):
            thandor::MapDocument::open_project(catalog_,resolved);
        auto next_document=std::make_unique<thandor::MapDocument>(std::move(loaded));
        stage="installing map data";
        // No edit-state mutation until the project itself has parsed successfully.
        document_=std::move(next_document);
        campaign_editing_mission_.reset();reset_terrain_generator();
        installed_document=true;
        current_group_=catalog_.map_groups().size(); // no stock PCK row is selected
        current_project_path_=extension==".lev"?std::filesystem::path{}:resolved;
        current_stock_group_.reset();
        undo_.clear();redo_.clear();selected_placement_.reset();selected_simulation_lab_entity_.reset();selected_edit_index_.reset();
        reset_simulation(false);simulation_static_replacement_dirty_=false;
        water_selection_mask_.clear();water_reference_cell_.reset();
        water_selection_stage_=WaterSelectionStage::SelectArea;
        water_selection_checkpointed_=false;
        asset_preview_cache_.clear();asset_preview_texture_cache_.clear();
        refresh_scenario_text_buffers();

        std::string preview_warning;
        stage="loading map texture previews";
        try{
            load_materials();
        }catch(const std::bad_alloc&){
            // Falling back to a single small checker texture avoids the larger
            // CPU staging allocation; LEV/FLD/placements are retained intact.
            preview_warning="Texture previews exceeded available memory; using placeholders.";
            material_images_.clear();surface_material_images_.clear();
            terrain_texture_layout_.reset();surface_texture_layout_.reset();
            material_names_.assign(26U,"[texture preview unavailable]");
            // Keep all 26 shader layers valid even if actual decoded GFX data
            // could not be kept in memory. A 1x1 image is expanded only in
            // the temporary GPU preview, never in the authored project.
            material_images_.assign(26U,
                thandor::RgbaImage{1U,1U,{120U,125U,120U,255U}});
            renderer_.update_materials(material_images_);
            renderer_.update_surface_materials({});
        }

        stage="rebuilding the terrain preview";
        bool terrain_ready=false;
        try{
            const auto active_lighting=lighting_cycle_preview_enabled_?
                document_->level().lighting().sampled_at_tick(lighting_cycle_preview_tick_):
                document_->level().lighting();
            renderer_.update_mesh(document_->field(),active_lighting,
                terrain_texture_layout_?&*terrain_texture_layout_:nullptr,
                surface_texture_layout_?&*surface_texture_layout_:nullptr,
                exact_soil_packets_,soil_transition_rng_seed_,resource_animation_rng_seed_);
            terrain_ready=true;
        }catch(const std::bad_alloc&){
            // Retry a smaller direct-cell preview without the multiple-packet
            // soil DAT triangulation or animated surface layout.
            preview_warning+=" Terrain preview needed reduced detail.";
            try{
                renderer_.update_mesh(document_->field(),document_->level().lighting(),
                    nullptr,nullptr,false,soil_transition_rng_seed_,resource_animation_rng_seed_);
                terrain_ready=true;
            }catch(const std::bad_alloc&){
                preview_warning+=" Terrain GPU/CPU preview remains unavailable.";
            }
        }

        stage="rebuilding placed-object previews";
        if(terrain_ready){
            try{
                rebuild_objects();
            }catch(const std::bad_alloc&){
                preview_warning+=" Object previews exhausted memory and were skipped.";
                // Prevent a half-built list of old/new objects being displayed.
                try{
                    renderer_.update_objects(std::span<const ObjectVertex>{},
                        std::span<const uint32_t>{},std::span<const ObjectDrawRun>{},
                        std::span<const ObjectVertex>{},std::span<const uint32_t>{},
                        std::span<const thandor::RgbaImage>{});
                }catch(const std::bad_alloc&){
                    preview_warning+=" Could not clear the partial object GPU preview.";
                }
            }
        }
        stage="centering the camera";
        center_camera();
        last_opened_map_type_="file";
        last_opened_map_value_=std::filesystem::absolute(resolved).lexically_normal().string();
        status_="Loaded "+document_->display_name()+" from "+resolved.string()+
            " with "+std::to_string(document_->custom_asset_count())+" custom assets.";
        if(!preview_warning.empty())status_+=" Preview warning:"+preview_warning;
        load_map_error_.clear();
        return true;
    }catch(const std::bad_alloc&){
        load_map_error_="Out of memory while "+stage+
            (installed_document?". Map data was parsed, but its preview could not be completed.":
                ". The editor retained the previously open map.");
    }catch(const std::exception& e){
        load_map_error_=stage+": "+e.what();
    }
    status_="Load failed: "+load_map_error_;
    return false;
}

bool EditorApp::save_project(const std::filesystem::path& path){
    try{
        if(path.empty())throw std::runtime_error("project path is empty");
        const auto extension=thandor::lower_ascii(path.extension().string());
        if(extension!=".thmap"&&extension!=".pck")throw std::runtime_error("Use a .thmap project filename.");
        if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
        document_->save_project(path,campaign_editing_mission_.has_value());current_project_path_=path;document_->mark_clean();
        if(campaign_document_&&campaign_editing_mission_) {
            campaign_document_->source(*campaign_editing_mission_).map_project=std::filesystem::absolute(path);
            for(std::size_t i=0;i<campaign_document_->campaign().records().size();++i)
                if(campaign_document_->campaign().records()[i].level_id()==*campaign_editing_mission_)
                    campaign_document_->campaign().record(i).set_level_path(document_->map_name());
            campaign_dirty_=true;refresh_campaign_labels();
        }
        last_opened_map_type_="file";
        last_opened_map_value_=std::filesystem::absolute(path).lexically_normal().string();
        std::snprintf(save_project_path_,sizeof(save_project_path_),"%s",path.string().c_str());
        status_="Saved full map project "+path.string()+" (LEV, FLD, names, and "+std::to_string(document_->custom_asset_count())+" custom assets).";
        save_map_error_.clear();
        return true;
    }catch(const std::exception& e){save_map_error_=e.what();status_="Save failed: "+save_map_error_;return false;}
}

void EditorApp::save_current_map(){
    try{
        if(!document_)throw std::runtime_error("no map is open");
        if(!current_stock_group_||*current_stock_group_>=catalog_.map_groups().size())
            throw std::runtime_error("current map is not a stock map; use Save full map project instead");
        // Runtime simulation state is deliberately session-only. MapDocument owns only authored
        // LEV/FLD/text/catalog/custom-asset data, so this path never snapshots or commits actors,
        // projectiles, runtime integrity, produced units, targets, fog, or simulation transforms.
        const auto result=document_->save_stock_map_in_place(
            catalog_,catalog_.map_groups()[*current_stock_group_],true);
        document_->mark_clean();
        std::ostringstream message;
        message<<"Saved current stock map in place: "<<result.updated_archives.size()<<" PCK";
        if(result.updated_archives.size()!=1U)message<<"s";
        message<<", "<<result.replaced_entries<<" entries updated";
        if(result.inserted_entries!=0U)message<<", "<<result.inserted_entries<<" custom entries inserted";
        if(!result.backup_archives.empty())message<<"; backup created";
        message<<". Simulation state was not saved.";
        status_=message.str();
    }catch(const std::exception& e){status_="Save current map failed: "+std::string(e.what());}
}

void EditorApp::reset_new_map_editing_view(){
    // Document changes must exit both simulators. Ordinary edit/Undo resets retain
    // fog deliberately, so this belongs to new-map creation rather than reset_simulation.
    clear_simulation_lab();simulation_mode_=false;reset_simulation(false);
    simulation_lab_auto_fog_preview_=false;
    fog_preview_mode_=0;fog_preview_faction_=1;fog_preview_all_explored_=false;
    fog_preview_history_mode_=0;fog_preview_history_faction_=1;fog_preview_history_all_explored_=false;
    fog_preview_history_lab_run_=-1;
    rebuild_fog_preview_visibility();
    // Rebuild/upload under the new document with fog disabled for every frame slot.
    dynamic_model_cache_.valid=false;last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
    draw_water_=true;tool_=Tool::Select;stroke_active_=false;
}

bool EditorApp::create_new_map(){
    try{
        if(template_group_<0||static_cast<std::size_t>(template_group_)>=catalog_.map_groups().size())
            throw std::runtime_error("Select a valid stock map as the new map's compatibility shell or terrain source");
        const auto& shell=catalog_.map_groups()[static_cast<std::size_t>(template_group_)];
        uint32_t width=static_cast<uint32_t>(std::max(new_width_,2));
        uint32_t height=static_cast<uint32_t>(std::max(new_height_,2));
        if(!new_empty_map_&&new_use_template_dimensions_){
            // Import the complete source FLD rather than silently center-cropping a
            // 131x131 stock map into the dialog's unrelated 91x91 default.
            const auto source_field=thandor::FieldGrid::parse(catalog_.load(shell.field_path));
            width=source_field.width();
            height=source_field.height();
        }
        thandor::MapDocument created=new_empty_map_?
            thandor::MapDocument::create_empty(catalog_,shell,new_map_name_,new_display_name_,
                width,height,0,
                static_cast<uint8_t>(std::clamp(new_initial_material_,0,25))):
            thandor::MapDocument::create_from_template(catalog_,shell,new_map_name_,width,height);
        if(new_empty_map_){
            thandor::initialize_new_map_terrain(created.field(),new_terrain_settings_);
            if(new_surface_type_>=0&&new_surface_type_<3){
                const thandor::TerrainSurfaceFamily families[]{thandor::TerrainSurfaceFamily::Water,thandor::TerrainSurfaceFamily::Ice,thandor::TerrainSurfaceFamily::Lava};
                bool found=false;
                for(const auto& group:catalog_.map_groups()){
                    const auto level=thandor::LevelAsset::parse(catalog_.load(group.level_path));
                    if(thandor::classify_terrain_surface_path(level.paths().surface_textures)==families[new_surface_type_]){
                        created.level().paths().surface_textures=level.paths().surface_textures;found=true;break;
                    }
                }
                if(!found)throw std::runtime_error("The selected surface is not available in this game folder. Choose another surface or From terrain style.");
            }
        }
        created.set_display_name(new_display_name_);
        auto next=std::make_unique<thandor::MapDocument>(std::move(created));
        reset_new_map_editing_view();document_=std::move(next);
        current_group_=static_cast<std::size_t>(template_group_);texture_source_group_=template_group_;current_project_path_.clear();current_stock_group_.reset();campaign_editing_mission_.reset();reset_terrain_generator();
        undo_.clear();redo_.clear();selected_placement_.reset();selected_simulation_lab_entity_.reset();selected_edit_index_.reset();
        simulation_static_replacement_dirty_=false;
        water_selection_mask_.clear();water_reference_cell_.reset();water_selection_stage_=WaterSelectionStage::SelectArea;water_selection_checkpointed_=false;
        water_height_=new_empty_map_?new_terrain_settings_.surface_level:0.0F;
        asset_preview_cache_.clear();asset_preview_texture_cache_.clear();refresh_scenario_text_buffers();load_materials();rebuild_mesh();center_camera();
        status_=new_empty_map_?"Created a new map. Fog is off and the whole map is visible.":
            "Imported terrain from "+shell.display_name+" into a new map with cleared placements.";
        new_map_error_.clear();
        return true;
    }catch(const std::exception& e){
        new_map_error_=e.what();
        status_="New map creation failed: "+new_map_error_;
        return false;
    }
}

void EditorApp::apply_texture_family_from_map(std::size_t group_index){try{if(group_index>=catalog_.map_groups().size())throw std::out_of_range("texture source map");checkpoint();const auto source=thandor::LevelAsset::parse(catalog_.load(catalog_.map_groups()[group_index].level_path));document_->level().paths().ground_textures=source.paths().ground_textures;document_->level().paths().surface_textures=source.paths().surface_textures;document_->mark_dirty();asset_preview_cache_.clear();asset_preview_texture_cache_.clear();load_materials();rebuild_mesh();status_="Loaded terrain texture family from "+catalog_.map_groups()[group_index].display_name;}catch(const std::exception& e){status_=e.what();}}

void EditorApp::load_materials(){
    auto performance_timer=editor_performance_log_.measure(EditorPerfStage::MaterialLoad);
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
                auto image=graphics.decode(
                    thandor::GraphicsSubresourceIndex{static_cast<uint32_t>(layer)},false);
                // This is an editable-map *preview*, not a rewrite of the GFX
                // asset. Normalize each layer immediately rather than retaining
                // as many as 256 full-resolution decoded layers in RAM.
                constexpr uint32_t kPreviewExtent=256U;
                if(image.width>kPreviewExtent||image.height>kPreviewExtent){
                    const uint32_t target_width=std::min(image.width,kPreviewExtent);
                    const uint32_t target_height=std::min(image.height,kPreviewExtent);
                    std::vector<uint8_t> reduced(
                        static_cast<std::size_t>(target_width)*target_height*4U);
                    if(image.width==0U||image.height==0U||
                       static_cast<uint64_t>(image.width)*image.height*4U!=image.pixels.size())
                        throw std::runtime_error("surface layer has invalid RGBA dimensions");
                    for(uint32_t y=0;y<target_height;++y){
                        const uint32_t source_y=static_cast<uint32_t>(
                            (static_cast<uint64_t>(y)*image.height)/target_height);
                        for(uint32_t x=0;x<target_width;++x){
                            const uint32_t source_x=static_cast<uint32_t>(
                                (static_cast<uint64_t>(x)*image.width)/target_width);
                            const auto src=(static_cast<std::size_t>(source_y)*image.width+source_x)*4U;
                            const auto dst=(static_cast<std::size_t>(y)*target_width+x)*4U;
                            std::copy_n(image.pixels.begin()+static_cast<std::ptrdiff_t>(src),4,
                                        reduced.begin()+static_cast<std::ptrdiff_t>(dst));
                        }
                    }
                    image.width=target_width;image.height=target_height;
                    image.pixels=std::move(reduced);
                }
                surface_material_images_.push_back(std::move(image));
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
void EditorApp::rebuild_mesh(){
    generator_preview_.reset();generator_source_field_.clear();generator_source_level_.clear();
    auto performance_timer=editor_performance_log_.measure(EditorPerfStage::TerrainRebuild);
    if(document_){
    const auto active_lighting=lighting_cycle_preview_enabled_
        ?document_->level().lighting().sampled_at_tick(lighting_cycle_preview_tick_)
        :document_->level().lighting();
    renderer_.update_mesh(
    document_->field(),active_lighting,
    terrain_texture_layout_?&*terrain_texture_layout_:nullptr,
    surface_texture_layout_?&*surface_texture_layout_:nullptr,
    exact_soil_packets_,soil_transition_rng_seed_,
    resource_animation_rng_seed_);rebuild_objects();}}

void EditorApp::rebuild_objects(){
    auto performance_timer=editor_performance_log_.measure(EditorPerfStage::ObjectRebuild);
    rendered_object_count_=0;
    marker_only_object_count_=0;
    object_texture_layer_count_=0;
    lab_selected_render_variants_=0;
    power_resource_decal_count_=0;
    building_resource_decal_count_=0;
    resource_body_triangle_count_=0;
    resource_luminous_particle_triangle_count_=0;
    resource_shading_emitter_count_=0;
    projected_shadow_triangle_count_=0;
    generated_shadow_object_count_=0;
    generated_shadow_texture_count_=0;
    generated_shadow_build_milliseconds_=0.0;
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
    dynamic_scene_pre_shadow_milliseconds_=dynamic_scene_shadow_milliseconds_=0.0;
    dynamic_scene_fog_milliseconds_=dynamic_scene_model_setup_milliseconds_=0.0;
    dynamic_scene_effects_milliseconds_=dynamic_scene_sort_milliseconds_=0.0;
    timed_effect_controllers_.clear();
    damage_effect_controllers_.clear();
    faction_technology_hierarchy_cache_.clear();
    faction_technology_weapon_profile_cache_.clear();
    animated_building_placements_.clear();
    animated_unit_placements_.clear();
    continuous_radar_placements_.clear();
    simulation_actor_placements_.clear();
    generated_shadow_compaction_saved_vertices_=0;
    generated_shadow_surface_culled_triangles_=0;
    dynamic_shadow_templates_.clear();
    const auto retain_dynamic_shadow_template=[&](DynamicShadowTemplate&& template_shadow) {
        // The original field height at this receiver vertex is invariant across
        // every animation tick. Cache the offset once, not on every reprojection.
        template_shadow.receiver_height_offsets.reserve(template_shadow.receiver_vertices.size());
        for(const auto& vertex:template_shadow.receiver_vertices)
            template_shadow.receiver_height_offsets.push_back(vertex.position.z-
                terrain_height_near(vertex.position.x,vertex.position.y));
        dynamic_shadow_templates_.push_back(std::move(template_shadow));
    };
    visible_animated_buildings_.clear();
    visible_timed_effect_controllers_.clear();
    // Preserve the previous semantic atlas indices for attribution of texture
    // churn. The new scene does not reuse numeric indices yet.
    auto old_layer_lookup=std::move(object_texture_layer_lookup_);
    dynamic_scene_cpu_={};
    placement_pick_bounds_.clear();
    placement_pick_hulls_.clear();
    placement_model_hulls_.clear();
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
    std::vector<DamageEffectControllerPreview> pending_damage_effects;
    std::vector<thandor::RgbaImage> texture_layers;
    std::unordered_map<uint64_t,uint32_t> layer_lookup;
    bool has_texture_aliases=false;
    const auto missing_texture_layer=[&](uint64_t key,const std::string& error)->uint32_t{
        report_asset_failure("texture/"+std::to_string(key),error);
        if(const auto found=layer_lookup.find(key);found!=layer_lookup.end())return found->second;
        if(texture_layers.size()>=kMaximumTextureLayers){has_texture_aliases=true;layer_lookup.emplace(key,0U);return 0U;}
        const auto layer=static_cast<uint32_t>(texture_layers.size());
        texture_layers.push_back(missing_texture_placeholder());layer_lookup.emplace(key,layer);
        return layer;
    };
    const auto assembly_started=EditorPerformanceLog::Clock::now();
    // Moving previously assembled immutable source pixels avoids repeated
    // canvas copies during Lab entity/technology residency rebuilds.
    const std::string source_signature=document_->map_name()+"|"+
        terrain_base_path(document_->level().paths().army_textures)+"|"+
        terrain_base_path(document_->level().paths().effect_textures);
    object_texture_source_reuse_.begin(source_signature);
    generated_shadow_reuse_.begin(document_->field());
    const auto level_lighting=lighting_cycle_preview_enabled_
        ?document_->level().lighting().sampled_at_tick(lighting_cycle_preview_tick_)
        :document_->level().lighting();
    const auto ramp=level_lighting.build_game_ramp();
    const auto prepared_directional_light=level_lighting.prepare_directional_light();
    const std::string army_base=document_->level().paths().army_textures;

    const auto texture_family=[&](uint32_t faction)->const ArmyTextureFamily&{
        const uint32_t clamped=std::min(faction,8U);
        const std::string key=terrain_base_path(army_base)+"#"+std::to_string(clamped);
        auto found=army_texture_cache_.find(key);
        if(found==army_texture_cache_.end()){
            auto family=load_texture_family(army_texture_family_path(army_base,clamped,".gfx"),
                                            army_texture_family_path(army_base,clamped,".pal"));
            found=army_texture_cache_.emplace(key,std::move(family)).first;
        }
        return *found->second;
    };
    const auto texture_layer=[&](uint32_t runtime_faction,uint32_t graphics_family,
                                   uint32_t subresource)->uint32_t{
        const uint32_t clamped_family=std::min(graphics_family,8U);
        const uint64_t key=army_texture_layer_key(runtime_faction,clamped_family,subresource,
            local_player_texture_override_.has_value(),local_player_runtime_faction_);
        if(const auto found=layer_lookup.find(key);found!=layer_lookup.end())return found->second;
        if(texture_layers.size()>=kMaximumTextureLayers)
            return missing_texture_layer(key,"object texture array capacity exhausted");
        const auto& family=texture_family(clamped_family);
        if(subresource>=family.graphics.subresources().size())return missing_texture_layer(key,"SPR selects missing army GFX subresource");
        const auto& metadata=family.graphics.subresources()[subresource];
        const bool use_player_override=local_player_texture_override_.has_value()&&
            runtime_faction==local_player_runtime_faction_&&
            subresource==kRetailFrontendPlayerTextureSubresource&&
            metadata.pixel_width==64U&&metadata.pixel_height==64U;
        const uint32_t layer=static_cast<uint32_t>(texture_layers.size());
        texture_layers.push_back(use_player_override
            ? thandor::make_sprite_texture_square_canvas(*local_player_texture_override_)
            : object_texture_source_reuse_.obtain(key,[&]{return family.square_canvas(subresource);}));
        layer_lookup.emplace(key,layer);
        return layer;
    };
    const std::string effect_base=terrain_base_path(document_->level().paths().effect_textures);
    const auto effect_texture_family=[&]()->const ArmyTextureFamily&{

        const std::string key=effect_base+"#effect";
        auto found=army_texture_cache_.find(key);
        if(found==army_texture_cache_.end()){
            auto family=load_texture_family(effect_base+".gfx",effect_base+".pal");
            found=army_texture_cache_.emplace(key,std::move(family)).first;
        }
        return *found->second;
    };
    const auto effect_texture_layer=[&](uint32_t subresource)->uint32_t{
        constexpr uint64_t kEffectLayerNamespace=uint64_t{1}<<63U;
        const uint64_t key=kEffectLayerNamespace|subresource;
        if(const auto found=layer_lookup.find(key);found!=layer_lookup.end())return found->second;
        if(texture_layers.size()>=kMaximumTextureLayers)
            return missing_texture_layer(key,"effect texture array capacity exhausted");
        const auto& family=effect_texture_family();
        if(subresource>=family.graphics.subresources().size())
            return missing_texture_layer(key,"EFF/SHOT sprite selects missing effect GFX subresource");
        const uint32_t layer=static_cast<uint32_t>(texture_layers.size());
        texture_layers.push_back(object_texture_source_reuse_.obtain(key,[&]{
            return family.square_canvas(subresource);
        }));
        layer_lookup.emplace(key,layer);
        return layer;
    };

    thandor::RenderTextureDependencies texture_dependencies(
        [&](uint32_t id){return catalog_.effect_definition(id);},
        [&](uint32_t id){return catalog_.shot_definition(id);},
        [&](const thandor::EffectDefinitionRecord& definition){
        const auto& sprite=cached_sprite(definition.sprite_path);
        const auto& mesh=sprite.mesh();
        const uint32_t frame_count=std::max(1U,definition.runtime_value_0c);
        for(std::size_t triangle=0;triangle+2U<mesh.indices.size();triangle+=3U){
            const auto& vertex=mesh.vertices.at(mesh.indices[triangle]);
            if(vertex.texture_subresource==std::numeric_limits<uint32_t>::max())continue;
            for(uint32_t frame=0;frame<frame_count;++frame){
                const uint32_t subresource=vertex.texture_subresource+frame;
                if((definition.creation_flags&2U)!=0U){
                    // EffectRuntimePool_CreateInstanceFromDefinitionCf selects the fixed
                    // g_ArmyGraphicsBindings[0] family for this flag. Expanding these records
                    // into every map faction both recolours effects and multiplies texture layers.
                    (void)texture_layer(0U,0U,subresource);
                }else{
                    (void)effect_texture_layer(subresource);
                }
            }
        }
        },
        [&](const thandor::ShotDefinitionRecord& shot){
        const auto& sprite=cached_sprite(shot.sprite_path);
        const auto& mesh=sprite.mesh();
        for(std::size_t triangle=0;triangle+2U<mesh.indices.size();triangle+=3U){
            const auto& vertex=mesh.vertices.at(mesh.indices[triangle]);
            if(vertex.texture_subresource==std::numeric_limits<uint32_t>::max())continue;
            for(uint32_t frame=0;frame<std::max(1U,shot.animation_frame_count);++frame)
                (void)effect_texture_layer(vertex.texture_subresource+frame);
        }
        });
    const auto preload_effect_textures=[&](uint32_t id){texture_dependencies.effect(id);};
    const auto preload_shot_textures=[&](uint32_t id){texture_dependencies.shot(id);};

    // Independent effects survive their source model, including Pioneer conversion
    // and construction one-shots. Preload their complete linked EFF/SHT chains.
    for(const auto& instance:movement_ground_effects_)
        preload_effect_textures(instance.effect_id);
    for(const auto id:simulation_lab_resident_effect_ids_)preload_effect_textures(id);
    for(const auto& [placement,state]:building_destruction_states_){
        (void)placement;
        preload_effect_textures(state.effect_id);
    }
    for(const uint32_t marker:{143U,148U,149U})
        if(catalog_.effect_definition(marker))preload_effect_textures(marker);

    // Projectiles can outlive a committed/reset actor. Keep their authored SHT/EFF layers
    // resident until the projectile lifecycle expires.
    for(const auto& projectile:simulation_projectiles_)
        preload_shot_textures(projectile.shot_definition_id);
    if(simulation_lab_active()){
        const auto runs=simulation_lab_runs();if(!runs.empty()){
            const auto& selected=runs[static_cast<size_t>(std::clamp(simulation_lab_selected_run_,0,int(runs.size())-1))];
            for(const auto& projectile:selected.projectiles)preload_shot_textures(projectile.shot_definition_id);
        }
    }

    const auto untextured_layer=[&]()->uint32_t{
        constexpr uint64_t key=std::numeric_limits<uint64_t>::max();
        if(const auto found=layer_lookup.find(key);found!=layer_lookup.end())return found->second;
        if(texture_layers.size()>=kMaximumTextureLayers)
            return missing_texture_layer(key,"object texture array capacity exhausted");
        thandor::RgbaImage white{1U,1U,{255U,255U,255U,255U}};
        const uint32_t layer=static_cast<uint32_t>(texture_layers.size());
        texture_layers.push_back(std::move(white));
        layer_lookup.emplace(key,layer);
        return layer;
    };

    // Selected-run synthetic construction is rendered through the real ARM/MDL/SPR path. Lab
    // assets can appear after experiment start, so sync_selected_simulation_lab_run() requests one
    // static texture rebuild whenever the selected run begins referencing a new asset/tech variant.
    const auto preload_simulation_lab_preset_textures=[&](const thandor::ArmyAssetPreset& preset,
                                                           uint32_t faction,
                                                           const thandor::FactionTechnologyMask256* technology_mask=nullptr,
                                                           bool preload_runtime_resources=false,
                                                           const std::vector<thandor::ModelVisualNode>* runtime_hierarchy=nullptr){
        const auto& active_mask=technology_mask!=nullptr?*technology_mask:
            document_->faction_technology_mask(std::min(faction,7U));
        // The simulation can retain a concrete hierarchy which differs from the current
        // faction's catalog/research resolution. Dynamic drawing uses that exact hierarchy;
        // its SPR materials must also be resident before the first draw after selection.
        std::vector<thandor::ModelVisualNode> researched;
        if(runtime_hierarchy==nullptr)
            researched=thandor::resolve_faction_technology_hierarchy(preset,active_mask);
        std::span<const thandor::ModelVisualNode> hierarchy=runtime_hierarchy!=nullptr
            ?std::span<const thandor::ModelVisualNode>(*runtime_hierarchy)
            :!researched.empty()?std::span<const thandor::ModelVisualNode>(researched)
            :(preset.resolved_model_hierarchy.empty()
                ?std::span<const thandor::ModelVisualNode>(preset.model_hierarchy)
                :std::span<const thandor::ModelVisualNode>(preset.resolved_model_hierarchy));
        std::vector<thandor::ModelVisualNode> fallback_nodes;
        if(hierarchy.empty()&&!preset.sprite_path.empty()){
            thandor::ModelVisualNode fallback;
            fallback.parent_index=std::numeric_limits<uint32_t>::max();
            fallback.sprite_path=preset.sprite_path;
            fallback_nodes.push_back(std::move(fallback));
            hierarchy=fallback_nodes;
        }
        const bool neutral_source_color=thandor::placement_uses_neutral_source_color(preset);
        const uint32_t texture_runtime_faction=neutral_source_color?0U:faction;
        const uint32_t texture_graphics_family=neutral_source_color?0U:
            graphics_family_for_faction(faction);
        const auto& family=texture_family(texture_graphics_family);
        bool needs_untextured=false;
        for(const auto& node:hierarchy){
            if(!node.creates_runtime_node()||node.sprite_path.empty())continue;
            const auto& mesh=cached_sprite(node.sprite_path).mesh();
            for(const auto& vertex:mesh.vertices){
                if(vertex.texture_subresource==std::numeric_limits<uint32_t>::max()){
                    needs_untextured=true;continue;
                }
                if(vertex.texture_subresource>=family.graphics.subresources().size())
                    throw std::runtime_error("Simulation Lab construction SPR selects missing army GFX subresource");
                (void)texture_layer(texture_runtime_faction,texture_graphics_family,vertex.texture_subresource);
            }
        }
        if(needs_untextured)(void)untextured_layer();
        if(preload_runtime_resources){
            // Live transient actors can emit model-owned land/water/damage EFFs and can fire
            // hierarchy-resolved SHT weapons before another unrelated static rebuild occurs.
            // Preload only this actor's resolved hierarchy instead of widening residency to every
            // armed catalog preset (which can exhaust the finite Vulkan image-array layer budget).
            for(std::size_t node_index=0U;node_index<hierarchy.size();++node_index){
                const auto& node=hierarchy[node_index];
                if(!node.creates_runtime_node()||node.sprite_path.empty())continue;
                const bool runtime_definition_root=
                    node.parent_index==std::numeric_limits<uint32_t>::max()||
                    (node.parent_index<hierarchy.size()&&
                     hierarchy[node.parent_index].model_definition_id!=node.model_definition_id);
                if(!runtime_definition_root)continue;
                preload_effect_textures(node.simulation.timed_effect_id);
                preload_effect_textures(node.simulation.timed_water_effect_id);
                if(node.simulation.damage_effect_health_threshold_percent>0)
                    preload_effect_textures(node.simulation.damage_effect_id);
            }
            // Combat can emit only the eight authored terminal MDL channels.
            // +0x190 is a construction/dismantle effect, never an extra death EFF.
            // Preload the selected-run hierarchy, not every unused variant.
            for(const auto effect_id:thandor::simulation_lab_terminal_residency_effect_ids(hierarchy))
                preload_effect_textures(effect_id);
            for(const auto& weapon:catalog_.simulation_weapon_profiles_for_hierarchy(hierarchy))
                preload_shot_textures(weapon.shot_definition_id);
        }
    };
    // Both static and dynamic model passes consume these hierarchies. Collect
    // their materials independently of static geometry emission: that pass can
    // stop at a placement error after registering an animated actor.
    const auto authored_placements=document_->level().placements();
    for(std::size_t placement_index=0;placement_index<authored_placements.size();++placement_index){
        const auto& placement=authored_placements[placement_index];
        if(const auto* preset=catalog_.army_preset(placement.army_asset_id);preset!=nullptr)
            preload_simulation_lab_preset_textures(*preset,
                runtime_faction_for_placement(placement_index,placement.faction_index));
    }
    if(simulation_lab_active()){
        // Synthetic primary-queue placements are not LEV objects, so the static placement pass did
        // not necessarily upload their ARM/MDL texture family. Preload the assets that actually
        // exist in the current Lab snapshots instead of assuming the later AI 14A/14C/14B/14D
        // candidate family. Retail empty-base initialization can place completely different
        // class-11/class-14/class-16 registry records.
        // The other Simulation Lab runs are CPU-only: loading their synthetic
        // ARM/MDL/SPR textures into the selected world's Vulkan atlas both
        // wastes CPU work and can evict/overflow layers needed by this world.
        // A run switch forces rebuild_objects() after changing selected_run_,
        // so the newly selected run becomes resident on demand.
        const auto runs=simulation_lab_runs();
        if(!runs.empty()){
            const std::size_t selected=static_cast<std::size_t>(std::clamp(
                simulation_lab_selected_run_,0,static_cast<int>(runs.size())-1));
            lab_selected_render_variants_=for_each_selected_lab_synthetic_asset(runs,selected,
                [&](const auto& run,const auto& entity,std::size_t faction){
                    if(const auto* preset=catalog_.army_preset(entity.army_asset_id);preset!=nullptr)
                        preload_simulation_lab_preset_textures(*preset,entity.faction_index,
                            &run.faction_technology_masks[faction],true);
                });
            // V694 deduplicated catalog asset/faction pairs, but the dynamic renderer can
            // override both synthetic and authored actors with an immutable runtime_model_hierarchy. Those hierarchies
            // can contain additional sprites/subresources (e.g. faction 3, GFX 13) that a
            // catalog-only pass does not visit. Visit each live override of the selected run;
            // never load overrides belonging to inactive simulations.
            for_each_selected_lab_runtime_hierarchy(runs,selected,
                [&](const auto& run,const auto& entity,std::size_t faction){
                    if(const auto* preset=catalog_.army_preset(entity.army_asset_id);preset!=nullptr)
                        preload_simulation_lab_preset_textures(*preset,entity.faction_index,
                            &run.faction_technology_masks[faction],true,
                            entity.runtime_model_hierarchy.get());
                });
        }
    }
    // Production queue choices and already-spawned transient class-13 children are not LEV
    // placements, so their texture layers must be present before the dynamic renderer sees them.
    for(uint32_t faction=1U;faction<simulation_secondary_production_queues_.size();++faction){
        for(const uint32_t asset_id:simulation_secondary_production_queues_[faction])
            if(const auto* preset=catalog_.army_preset(asset_id);preset!=nullptr)
                preload_simulation_lab_preset_textures(*preset,faction);
    }
    for(const auto& [serial,entity]:simulation_produced_entities_){
        (void)serial;
        if(const auto* preset=catalog_.army_preset(entity.army_asset_id);preset!=nullptr)
            preload_simulation_lab_preset_textures(*preset,entity.faction_index,nullptr,true);
    }
    for(const auto& [serial,entity]:simulation_linked_child_entities_){
        (void)serial;
        if(const auto* preset=catalog_.army_preset(entity.army_asset_id);preset!=nullptr)
            preload_simulation_lab_preset_textures(*preset,entity.faction_index,nullptr,true);
    }
    struct ShadowAtlasTile {
        uint32_t layer{};
        float u0{};
        float v0{};
        float u1{};
        float v1{};
    };
    const thandor::GeneratedShadowSettings generated_shadow_settings=kEditorGeneratedShadowSettings;
    if(!thandor::retail_generated_shadow_settings_valid(generated_shadow_settings))
        throw std::runtime_error("invalid default generated-shadow settings");
    const uint32_t shadow_tile_dimension=generated_shadow_settings.grid_half_size;
    const uint32_t shadow_atlas_dimension=generated_shadow_settings.texture_dimension;
    const uint32_t shadow_tiles_per_axis=shadow_atlas_dimension/shadow_tile_dimension;
    const uint32_t shadow_tiles_per_layer=
        thandor::retail_generated_shadow_tiles_per_subresource(generated_shadow_settings);
    uint32_t current_shadow_atlas_layer=std::numeric_limits<uint32_t>::max();
    uint32_t next_shadow_atlas_tile=shadow_tiles_per_layer;
    const auto shadow_atlas_tile=[&](const thandor::RgbaImage& mask)->ShadowAtlasTile{
        const std::size_t expected_mask_bytes=static_cast<std::size_t>(shadow_tile_dimension)*
                                              shadow_tile_dimension*4U;
        if(mask.width!=shadow_tile_dimension||mask.height!=shadow_tile_dimension||
           mask.pixels.size()!=expected_mask_bytes)
            throw std::runtime_error("generated shadow mask does not match the retail caster-tile dimension");
        if(next_shadow_atlas_tile>=shadow_tiles_per_layer){
            if(texture_layers.size()>=kMaximumTextureLayers)
                throw std::runtime_error("generated shadows exhausted the selected Vulkan device image-array limit of "+
                    std::to_string(kMaximumTextureLayers)+" layers");
            current_shadow_atlas_layer=static_cast<uint32_t>(texture_layers.size());
            texture_layers.push_back(thandor::RgbaImage{shadow_atlas_dimension,shadow_atlas_dimension,
                std::vector<uint8_t>(static_cast<std::size_t>(shadow_atlas_dimension)*
                                     shadow_atlas_dimension*4U,0U)});
            next_shadow_atlas_tile=0U;
            ++generated_shadow_texture_count_;
        }
        const uint32_t tile=next_shadow_atlas_tile++;
        const uint32_t tile_x=(tile%shadow_tiles_per_axis)*shadow_tile_dimension;
        const uint32_t tile_y=(tile/shadow_tiles_per_axis)*shadow_tile_dimension;
        auto& atlas=texture_layers[current_shadow_atlas_layer];
        for(uint32_t y=0;y<shadow_tile_dimension;++y){
            const std::size_t source=static_cast<std::size_t>(y)*shadow_tile_dimension*4U;
            const std::size_t destination=(static_cast<std::size_t>(tile_y+y)*shadow_atlas_dimension+tile_x)*4U;
            std::copy_n(mask.pixels.begin()+static_cast<std::ptrdiff_t>(source),
                        static_cast<std::size_t>(shadow_tile_dimension)*4U,
                        atlas.pixels.begin()+static_cast<std::ptrdiff_t>(destination));
        }
        const float inv=1.0F/static_cast<float>(shadow_atlas_dimension);
        return ShadowAtlasTile{current_shadow_atlas_layer,
            (static_cast<float>(tile_x)+0.5F)*inv,
            (static_cast<float>(tile_y)+0.5F)*inv,
            (static_cast<float>(tile_x+shadow_tile_dimension)-0.5F)*inv,
            (static_cast<float>(tile_y+shadow_tile_dimension)-0.5F)*inv};
    };
    const auto append_draw_run=[&](PrimitiveBlendPreset blend,bool two_sided,uint32_t first_index,
                                   RetailWorldModelPass retail_world_pass=RetailWorldModelPass::PostGeneratedShading){
        if(!draw_runs.empty()){
            auto& previous=draw_runs.back();
            if(previous.blend==blend&&previous.two_sided==two_sided&&
               previous.retail_world_pass==retail_world_pass&&
               previous.first_index+previous.index_count==first_index){
                previous.index_count+=3U;
                return;
            }
        }
        draw_runs.push_back(ObjectDrawRun{blend,two_sided,first_index,3U,0U,retail_world_pass});
    };

    const auto placements=document_->level().placements();
    has_factory_drive_markers_=std::any_of(placements.begin(),placements.end(),[&](const auto& p){
        const auto* preset=catalog_.army_preset(p.army_asset_id);return preset&&preset->runtime_model_class_id==13U;
    });
    placement_pick_bounds_.assign(placements.size(),AxisAlignedBounds{});
    placement_pick_hulls_.assign(placements.size(),{});
    placement_model_hulls_.assign(placements.size(),{});
    // V537: game.exe loads LEV +0x2E0 low-word azimuth/high-word elevation into
    // WorldRuntime +0xB8/+0xBC and passes that pair to both the terrain raycast
    // and FixedMath_DirectionFromAnglesScaledRegs used by generated shadows.
    const Vec3f shadow_projection_direction=level_lighting.shadow_ray_direction();
    for(std::size_t placement_index=0;placement_index<placements.size();++placement_index){
        const auto& placement=placements[placement_index];
        auto& pick_bounds=placement_pick_bounds_[placement_index];
        auto& pick_hulls=placement_pick_hulls_[placement_index];
        auto& model_hulls=placement_model_hulls_[placement_index];
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
        const bool resource_support_active=placement_has_required_resource_support(*preset,placement);
        related_effect_definition_count_+=preset->shared_sprite_effect_ids.size();
        if(preset->visual_kind==thandor::PlacementVisualKind::PlayerStart)
            ++player_start_count_;
        // Completion action 3 creates a synthetic replacement with the original placement index as
        // lineage. Keep the LEV fallback pick hull/PlayerStart marker, but suppress its authored
        // model hierarchy while the live replacement is present.
        if(simulation_lab_active()&&simulation_lab_suppressed_placements_.contains(placement_index)){
            ++marker_only_object_count_;
            continue;
        }
        if(preset->sprite_path.empty()){++marker_only_object_count_;continue;}
        try{
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
                const thandor::SpriteAsset* node_sprite=node_active?&cached_sprite(node.sprite_path):nullptr;
                if(instance.root){
                    const auto contact=placement_contact_pose(*preset,placement.render_x(),
                        placement.render_y(),node_sprite?node_sprite->placement_height_offset():0.0F);
                    const Vec3f root_position{placement.render_x(),placement.render_y(),contact.height};
                    instance.transform=contact.align_to_normal
                        ?terrain_contact_root_transform(root_position,radians,contact.normal,contact.packed_normal_angles)
                        :placement_root_transform(root_position.x,root_position.y,root_position.z,radians);
                }else{
                    if(node.parent_index>=instances.size()||!active[node.parent_index])node_active=false;
                    if(node_active&&!thandor::model_runtime_world_child_enabled(
                           hierarchy[node.parent_index].runtime_class_id,node.child_slot))
                        node_active=false;
                    if(node_active){
                        const auto& parent=instances[node.parent_index];
                        const auto* attachment=parent.sprite->child_attachment(node.child_slot);
                        if(attachment==nullptr)
                            throw std::runtime_error("MDL child slot has no matching SPR attachment transform");
                        uint32_t animated_angle2=node.local_rotation_angle2;
                        const auto& animation_owner=hierarchy[node.parent_index];
                        const bool direct_animated_parent=
                            thandor::runtime_class_directly_dispatches_animated_model_subnodes(
                                animation_owner.runtime_class_id);
                        const bool authored_animation_active=direct_animated_parent||resource_support_active;
                        const int32_t authored_yaw_step=
                            thandor::model_runtime_child_yaw_step_turn16(
                                animation_owner,node.child_slot);
                        if(authored_yaw_step!=0&&authored_animation_active){
                            animated_angle2=thandor::wrap_turn16(
                                static_cast<int64_t>(animated_angle2)+
                                static_cast<int64_t>(authored_yaw_step)*hierarchy_tick);
                            ++animated_model_node_count_;
                        }
                        uint32_t animated_angle1=node.local_rotation_angle1;
                        if(preset->runtime_uses_class17_root_child_deployment&&parent.root&&node.child_slot<3U)
                            animated_angle1=thandor::wrap_turn16(
                                static_cast<int64_t>(animated_angle1)+0x3FF0);
                        auto local=fixed_model_rotation_basis(animated_angle2,
                            animated_angle1,node.local_rotation_angle0);
                        local.translation=attachment->local_translation;
                        if(authored_animation_active&&
                           thandor::model_runtime_child_uses_bounded_vertical_channel(
                               animation_owner,node.child_slot)){
                            local.translation.z=animated_child2_translation_q12(
                                animation_owner.simulation,hierarchy_tick,local.translation.z);
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
                    model_hulls.push_back(node_hull);
                }else if(sprite.bounding_radius()>0.0F){
                    const Vec3f center=instances[instance_index].transform.translation;
                    const float radius=sprite.bounding_radius();
                    AxisAlignedBounds radius_bounds;
                    radius_bounds.include({center.x-radius,center.y-radius,center.z-radius});
                    radius_bounds.include({center.x+radius,center.y+radius,center.z+radius});
                    pick_bounds.include(radius_bounds.minimum);
                    pick_bounds.include(radius_bounds.maximum);
                    const auto radius_hull=TransformedBoundsHull::from_axis_aligned(radius_bounds);
                    pick_hulls.push_back(radius_hull);
                    model_hulls.push_back(radius_hull);
                }
            }
            // Preserve the pre-simulation static composition contract. Merely being movable or
            // armed does not make an actor an authored animated object. Only the original MDL/ARM
            // subnode animation path enters the building animation list. A transient simulation
            // actor is tagged separately and replaces only that exact placement.
            const bool class17_lifecycle_pose=preset->runtime_uses_class17_root_child_deployment&&
                simulation_actor_states_.contains(placement_index);
            // Fog visibility applies to every live world placement, including Element/General
            // scenery. G410 limited this to buildings/units, leaving trees and rocks visible.
            const bool fog_dynamic_replacement=fog_preview_mode_!=0;
            const bool destruction_dynamic_replacement=
                building_destruction_states_.contains(placement_index);
            const bool retail_runtime_unit=
                preset->browser_kind==thandor::PlacementBrowserKind::Unit ||
                (preset->runtime_model_class_id==18U &&
                 preset->visual_kind==thandor::PlacementVisualKind::PlayerStart);
            const thandor::SimulationRenderPolicy render_policy{
                preset->browser_kind==thandor::PlacementBrowserKind::Building,
                retail_runtime_unit,
                (preset->has_model_subnode_animation&&resource_support_active)||class17_lifecycle_pose,
                (simulation_mode_||simulation_lab_active())||fog_preview_mode_!=0||destruction_dynamic_replacement,
                simulation_actor_states_.contains(placement_index)||fog_dynamic_replacement||
                    destruction_dynamic_replacement,
                preset->has_continuous_radar_subnode_animation};
            const bool animated_building=render_policy.authored_animated_building();
            const bool animated_unit=render_policy.authored_animated_unit();
            const bool simulation_replacement=render_policy.simulation_replacement();
            const bool dynamic_visibility_replacement=simulation_replacement||fog_dynamic_replacement;
            if(animated_building) animated_building_placements_.push_back(placement_index);
            if(animated_unit) animated_unit_placements_.push_back(placement_index);
            if(preset->has_continuous_radar_subnode_animation)
                continuous_radar_placements_.push_back(placement_index);
            if(dynamic_visibility_replacement) simulation_actor_placements_.push_back(placement_index);
            // Simulation SHT/EFF textures share the object texture array. Preload them only for
            // actors that currently own transient simulation state; preloading all 125 stock armed
            // presets consumed the finite array and caused later units/buildings to fail midway.
            if(simulation_replacement&&simulation_actor_states_.contains(placement_index)){
                for(const auto& weapon:placement_weapon_profiles(*preset,placement_index))
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
            const uint32_t effective_runtime_faction=
                runtime_faction_for_placement(placement_index,placement.faction_index);
            const uint32_t preview_runtime_faction=neutral_source_color?0U:effective_runtime_faction;
            const uint32_t preview_graphics_family=neutral_source_color?0U:
                graphics_family_for_faction(effective_runtime_faction);
            const auto& family=texture_family(preview_graphics_family);

            // ArmyRuntime_UpdateTimedShotAndEffectEmitters is owned by each live MDL runtime,
            // not by the top-level ARM record.  A linked MDL therefore keeps its own land/water
            // EFF pair, cadence and class-6 attachment-selection policy.  Child mesh nodes that
            // belong to the same MDL copy the same profile and must not duplicate a controller.
            for(std::size_t node_index=0U;node_index<hierarchy.size();++node_index){
                if(node_index>=active.size()||!active[node_index]||instances[node_index].sprite==nullptr)continue;
                const auto& node=hierarchy[node_index];
                const bool runtime_definition_root=node.parent_index==std::numeric_limits<uint32_t>::max()||
                    (node.parent_index<hierarchy.size()&&
                     hierarchy[node.parent_index].model_definition_id!=node.model_definition_id);
                if(!runtime_definition_root||
                   (thandor::model_runtime_class_uses_resource_field_support(node.runtime_class_id)&&
                    !resource_support_active)||
                   (node.simulation.timed_effect_id==0U&&node.simulation.timed_water_effect_id==0U))continue;

                const auto* land_definition=node.simulation.timed_effect_id==0U?nullptr:
                    catalog_.effect_definition(node.simulation.timed_effect_id);
                const auto* water_definition=node.simulation.timed_water_effect_id==0U?nullptr:
                    catalog_.effect_definition(node.simulation.timed_water_effect_id);
                if(node.simulation.timed_effect_id!=0U&&land_definition==nullptr)
                    throw std::runtime_error("MDL timed land effect is missing from the EFF registry");
                if(node.simulation.timed_water_effect_id!=0U&&water_definition==nullptr)
                    throw std::runtime_error("MDL timed water effect is missing from the EFF registry");
                if(land_definition)preload_effect_textures(land_definition->definition_id);
                if(water_definition&&(!land_definition||water_definition->definition_id!=land_definition->definition_id))
                    preload_effect_textures(water_definition->definition_id);

                // Runtime classes 2 and 3 call ArmyRuntime_UpdateTimedShotAndEffectEmitters from
                // their movement callback only when the model root changes. Their land/water EFFs
                // become independent world-space tread/footprint instances. Reconstructing these
                // with the generic periodic preview would relocate every historical pulse to the
                // owner's current transform and erase the visible trail.
                if(node.runtime_class_id==2U||node.runtime_class_id==3U)continue;

                TimedEffectControllerPreview pending;
                pending.land_effect_id=node.simulation.timed_effect_id;
                pending.water_effect_id=node.simulation.timed_water_effect_id;
                pending.placement_index=placement_index;
                pending.root_model_node_index=static_cast<uint32_t>(node_index);
                pending.emitter_model_node_index=static_cast<uint32_t>(node_index);
                pending.runtime_class_id=node.runtime_class_id;
                // Runtime class 21 swaps to direct child slot zero before reading the model
                // resource and before transforming a class-6 point.  Other classes use this MDL
                // runtime root itself.
                if(node.runtime_class_id==21U){
                    for(std::size_t child_index=0;child_index<hierarchy.size();++child_index){
                        if(child_index>=active.size()||!active[child_index])continue;
                        const auto& child=hierarchy[child_index];
                        if(child.parent_index==node_index&&child.child_slot==0U){
                            pending.emitter_model_node_index=static_cast<uint32_t>(child_index);
                            break;
                        }
                    }
                }
                const std::size_t emitter_index=pending.emitter_model_node_index;
                const auto& root_instance=instances[node_index];
                const auto& emitter_instance=instances[emitter_index];
                pending.root_position=root_instance.transform.translation;
                pending.root_orientation=root_instance.transform;
                pending.emitter_orientation=emitter_instance.transform;
                if(emitter_instance.sprite!=nullptr){
                    const auto local_points=emitter_instance.sprite->type6_attachment_points();
                    pending.local_type6_attachments.assign(local_points.begin(),local_points.end());
                }
                pending.timing={std::max(1U,node.simulation.timed_effect_interval_ticks),
                    node.simulation.timed_effect_random_ticks,
                    !pending.local_type6_attachments.empty()&&
                        (node.simulation.runtime_render_flags&1U)==0U,
                    // This is only the fallback policy.  The dynamic schedule below supplies an
                    // exact per-pulse predicate after resolving land/water at the chosen emitter.
                    (land_definition&&land_definition->transition_kind==2U)||
                        (water_definition&&water_definition->transition_kind==2U)};
                pending.owner_is_building=
                    preset->browser_kind==thandor::PlacementBrowserKind::Building;
                pending.owner_has_authored_model_animation=preset->has_model_subnode_animation;
                pending.owner_is_resource_field=
                    preset->visual_kind==thandor::PlacementVisualKind::PowerResource||
                    preset->visual_kind==thandor::PlacementVisualKind::BuildingResource;
                pending_timed_effects.push_back(std::move(pending));
                ++timed_effect_controller_count_;
            }

            // ArmyRuntime_EmitDamageThresholdEffect is owned by the selected MDL runtime, not by
            // the top-level ARM record. Build one controller for each resolved MDL runtime root.
            // Child mesh nodes inside the same MDL copy the same profile and must not duplicate it.
            for(std::size_t node_index=0U;node_index<hierarchy.size();++node_index){
                if(node_index>=active.size()||!active[node_index]||instances[node_index].sprite==nullptr)continue;
                const auto& node=hierarchy[node_index];
                const bool runtime_definition_root=node.parent_index==std::numeric_limits<uint32_t>::max()||
                    (node.parent_index<hierarchy.size()&&
                     hierarchy[node.parent_index].model_definition_id!=node.model_definition_id);
                if(!runtime_definition_root||node.simulation.damage_effect_id==0U||
                   node.simulation.damage_effect_health_threshold_percent<=0)continue;
                const auto* damage_definition=catalog_.effect_definition(node.simulation.damage_effect_id);
                if(damage_definition==nullptr)
                    throw std::runtime_error("MDL damage-state effect is missing from the EFF registry");
                preload_effect_textures(damage_definition->definition_id);
                DamageEffectControllerPreview damage;
                damage.effect_id=damage_definition->definition_id;
                damage.placement_index=placement_index;
                damage.model_node_index=static_cast<uint32_t>(node_index);
                damage.health_threshold_percent=node.simulation.damage_effect_health_threshold_percent;
                damage.timing={node.simulation.damage_effect_min_cooldown_ticks,
                    node.simulation.damage_effect_random_cooldown_ticks,0U};
                damage.root_position=instances[node_index].transform.translation;
                damage.root_orientation=instances[node_index].transform;
                std::vector<std::pair<uint32_t,Vec3f>> indexed_points;
                for(const auto& lookup:instances[node_index].sprite->lookup_records())
                    if(lookup.key_class().value()==3U)
                        indexed_points.emplace_back(lookup.key_index().value(),lookup.local_translation);
                std::sort(indexed_points.begin(),indexed_points.end(),
                    [](const auto& left,const auto& right){return left.first<right.first;});
                for(const auto& [attachment_index,point]:indexed_points)
                    damage.local_type3_attachments.emplace_back(attachment_index,point);
                damage.timing.attachment_count=
                    static_cast<uint32_t>(damage.local_type3_attachments.size());
                damage.owner_is_building=
                    preset->browser_kind==thandor::PlacementBrowserKind::Building;
                pending_damage_effects.push_back(std::move(damage));
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
                            layer=texture_layer(preview_runtime_faction,preview_graphics_family,source->texture_subresource);
                            uv=thandor::normalize_sprite_texture_uv(source->texture_u,source->texture_v,
                                subresource.pixel_width,subresource.pixel_height);
                        }
                        const Vec3f world=transform_point(instance.transform,source->position);
                        pick_bounds.include(world);
                        const Vec3f normal=normalize(transform_vector(instance.transform,source->normal));
                        const uint32_t directional_light=
                            prepared_directional_light.color_for_upward_normal(normal,ramp);
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
                        uint32_t direct_tint_bypass=render_policy.packed_static_control_bits(
                            directdraw_model_vertex_uses_direct_tint(triangle_render_flags));
                        if(fog_dynamic_replacement)
                            direct_tint_bypass|=thandor::kObjectVertexFogDynamicReplacementBit;
                        const bool animated_part=instance.root&&preset->has_primary_texture_animation&&
                            preset->runtime_model_class_id!=11U&&preset->runtime_model_class_id!=13U&&
                            preset->runtime_model_class_id!=22U&&
                            source->texture_subresource==preset->primary_animated_subresource;
                        vertices.push_back(ObjectVertex{world,normal,uv.u,uv.v,layer,packed_color,
                            direct_tint_bypass,animated_part?0.5F:0.0F,animated_part?80.0F:0.0F,
                            (preset->casts_generated_shadow&&!resource_particle_triangle&&primitive.blend!=PrimitiveBlendPreset::AdditiveNoDepthWrite)?1U:0U});
                    }
                    const uint32_t first_index=static_cast<uint32_t>(indices.size());
                    indices.insert(indices.end(),{base,base+1U,base+2U});
                    append_draw_run(primitive.blend,primitive.two_sided,first_index,
                        retail_world_model_pass_from_mdl_flags(preset->model_runtime_render_flags));

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
                if(kEditorLegacyProjectedShadowMeshes&&projected_object_shadows_&&preset->casts_generated_shadow&&
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
            if(kEditorLegacyProjectedShadowMeshes&&projected_object_shadows_&&preset->casts_generated_shadow&&
               resource_kind==thandor::ResourceEnergyKind::None&&
               !generated_shadow_triangles.empty()){
                const auto generated_started=std::chrono::steady_clock::now();
                const auto& generated=generated_shadow_reuse_.obtain(
                    placement_index,generated_shadow_triangles,shadow_projection_direction,
                    generated_shadow_settings,[&]{
                        return thandor::build_game_generated_object_shadow(
                            document_->field(),generated_shadow_triangles,shadow_projection_direction,
                            generated_shadow_settings);
                    });
                generated_shadow_build_milliseconds_+=std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-generated_started).count();
                if(generated){
                    const auto tile=shadow_atlas_tile(generated->mask);
                    PreparedGeneratedShadowReceiver prepared=build_prepared_generated_shadow_receiver(
                        document_->field(),generated->receiver_vertices,generated->receiver_indices,
                        generated_shadow_settings.surface_bias,tile.u0,tile.v0,tile.u1,tile.v1,tile.layer);
                    generated_shadow_compaction_saved_vertices_+=
                        prepared.vertices_before_compaction-prepared.vertices.size();
                    generated_shadow_surface_culled_triangles_+=prepared.rejected_triangles;
                    if(dynamic_visibility_replacement){
                        std::vector<ObjectVertex> prepared_vertices=std::move(prepared.vertices);
                        std::vector<uint32_t> prepared_indices=std::move(prepared.indices);
                        // Static placement geometry is hidden while a live simulation replacement is
                        // drawn, so its original projected shadow must be hidden as well. Retain
                        // the generated retail caster tile/editor receiver as an actor-local
                        // template and move it
                        // with the current body pose every presentation tick.
                        DynamicShadowTemplate dynamic_shadow;
                        dynamic_shadow.placement_index=placement_index;
                        dynamic_shadow.requires_live_owner=simulation_replacement;
                        dynamic_shadow.anchor_position={placement.render_x(),placement.render_y(),
                            placement_base_z};
                        dynamic_shadow.anchor_yaw_turn16=placement.orientation_turn16&0xFFFFU;
                        dynamic_shadow.receiver_vertices=std::move(prepared_vertices);
                        dynamic_shadow.receiver_indices=std::move(prepared_indices);
                        retain_dynamic_shadow_template(std::move(dynamic_shadow));
                    }else{
                        if(shadow_vertices.size()+prepared.vertices.size()>kMaximumVertices||
                           shadow_indices.size()+prepared.indices.size()>kMaximumIndices)
                            throw std::runtime_error("generated shadow receiver geometry limit exceeded");
                        const uint32_t base=static_cast<uint32_t>(shadow_vertices.size());
                        shadow_vertices.insert(shadow_vertices.end(),prepared.vertices.begin(),prepared.vertices.end());
                        for(const uint32_t index:prepared.indices)
                            shadow_indices.push_back(base+index);
                    }
                    projected_shadow_triangle_count_+=prepared.indices.size()/3U;
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
    damage_effect_controllers_=std::move(pending_damage_effects);
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
    // Static scene traversal order changes when Lab assets become resident.
    // Permute ONLY semantic sprite layers; generated shadow masks are anonymous
    // image-array layers, and their existing receiver/template vertices must
    // retain their numeric layer indices. The permutation fixes those slots.
    std::optional<std::unordered_map<uint64_t,uint32_t>> unique_texture_keys;
    if(has_texture_aliases)unique_texture_keys=unique_object_texture_slot_keys(layer_lookup,texture_layers.size());
    const auto slot_permutation=stable_object_texture_slot_permutation(
        unique_texture_keys?*unique_texture_keys:layer_lookup,old_layer_lookup,texture_layers.size());
    bool slot_order_changed=false;
    for(std::size_t index=0;index<slot_permutation.size();++index)
        if(slot_permutation[index]!=index){slot_order_changed=true;break;}
    if(slot_order_changed){
        std::vector<thandor::RgbaImage> ordered(texture_layers.size());
        for(std::size_t index=0;index<slot_permutation.size();++index)
            ordered[slot_permutation[index]]=std::move(texture_layers[index]);
        texture_layers=std::move(ordered);
        for(auto& vertex:vertices){
            if(vertex.texture_layer>=slot_permutation.size())
                throw std::runtime_error("static object vertex has out-of-range texture layer");
            vertex.texture_layer=slot_permutation[vertex.texture_layer];
        }
        for(auto& [key,index]:layer_lookup){
            (void)key;
            index=slot_permutation[index];
        }
    }
    editor_performance_log_.record(EditorPerfStage::ObjectAssembly,
        std::chrono::duration<double,std::milli>(
            EditorPerformanceLog::Clock::now()-assembly_started).count());
    // Count semantic texture slots that changed even though their decoded
    // source pixels were reused. These relocations explain subsequent dirty
    // GPU array layers and motivate a stable slot allocator in a later pass.
    for(const auto& [key,index]:layer_lookup){
        const auto found=old_layer_lookup.find(key);
        if(found==old_layer_lookup.end())++object_layer_identity_totals_[2];
        else if(found->second==index)++object_layer_identity_totals_[0];
        else ++object_layer_identity_totals_[1];
    }
    for(const auto& [key,index]:old_layer_lookup){
        (void)index;
        if(!layer_lookup.contains(key))++object_layer_identity_totals_[3];
    }
    object_texture_layer_lookup_=std::move(layer_lookup);
    // The dynamic model paths own the final hierarchy/weapon/walker transforms. Collect
    // their dedicated caster meshes during this same CPU build, before uploading the atlas.
    // This includes synthetic AI buildings and factory children which have no LEV placement.
    if(kEditorLegacyProjectedShadowMeshes&&projected_object_shadows_)runtime_shadow_collector_=[&](RuntimeShadowOwner owner,
        Vec3f anchor,uint32_t yaw,uint32_t flags,
        std::span<const thandor::GeneratedShadowTriangle> triangles){
        if(std::any_of(dynamic_shadow_templates_.begin(),dynamic_shadow_templates_.end(),
            [&](const auto& shadow){return shadow.owner==owner;}))return;
        const auto generated_started=std::chrono::steady_clock::now();
        const auto generated=thandor::build_game_generated_object_shadow(
            document_->field(),triangles,shadow_projection_direction,generated_shadow_settings);
        generated_shadow_build_milliseconds_+=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-generated_started).count();
        if(!generated)return;
        const auto tile=shadow_atlas_tile(generated->mask);
        PreparedGeneratedShadowReceiver prepared=build_prepared_generated_shadow_receiver(
            document_->field(),generated->receiver_vertices,generated->receiver_indices,
            generated_shadow_settings.surface_bias,tile.u0,tile.v0,tile.u1,tile.v1,tile.layer);
        generated_shadow_compaction_saved_vertices_+=
            prepared.vertices_before_compaction-prepared.vertices.size();
        generated_shadow_surface_culled_triangles_+=prepared.rejected_triangles;
        DynamicShadowTemplate shadow;shadow.owner=owner;shadow.model_runtime_render_flags=flags;
        shadow.anchor_position=anchor;shadow.anchor_yaw_turn16=yaw;
        shadow.receiver_indices=std::move(prepared.indices);
        shadow.receiver_vertices=std::move(prepared.vertices);
        projected_shadow_triangle_count_+=shadow.receiver_indices.size()/3U;
        ++generated_shadow_object_count_;
        retain_dynamic_shadow_template(std::move(shadow));
    };
    try{rebuild_dynamic_scene_cpu();}
    catch(...){runtime_shadow_collector_={};throw;}
    runtime_shadow_collector_={};
    object_texture_layer_count_=texture_layers.size();
    {auto timer=editor_performance_log_.measure(EditorPerfStage::ObjectUpload);
    renderer_.update_objects(vertices,indices,draw_runs,shadow_vertices,shadow_indices,
                             texture_layers);}
    object_texture_source_reuse_.commit(std::move(texture_layers),object_texture_layer_lookup_);
    last_dynamic_scene_tick_=current_presentation_tick_;
}

bool EditorApp::placement_visible_in_canvas(Vec3f center,float radius) const{
    if(!document_)return false;
    const auto canvas=workspace_layout().canvas;
    const float canvas_width=canvas.width;
    const float canvas_height=canvas.height;
    const auto vp=camera_.view_projection(canvas_width/canvas_height);
    float screen_x=0.0F,screen_y=0.0F,depth=0.0F;
    if(!project_to_canvas(center,vp,screen_x,screen_y,depth)){
        const float eye_distance=std::sqrt(dot(sub(center,camera_.eye()),sub(center,camera_.eye())));
        return eye_distance<=std::max(2.0F,radius*2.5F);
    }
    const float distance=std::max(radius,std::sqrt(dot(sub(center,camera_.eye()),sub(center,camera_.eye()))));
    const float projected_radius=std::clamp(
        0.5F*canvas_height*radius/(std::tan(camera_.fov_y*0.5F)*distance),8.0F,512.0F);
    const float left=canvas.x-projected_radius;
    const float right=canvas.x+canvas.width+projected_radius;
    const float top=canvas.y-projected_radius;
    const float bottom=canvas.y+canvas.height+projected_radius;
    return screen_x>=left&&screen_x<=right&&screen_y>=top&&screen_y<=bottom;
}

bool EditorApp::bounds_visible_in_canvas(const AxisAlignedBounds& source_bounds,float padding) const{
    if(!document_||!source_bounds.valid())return false;
    AxisAlignedBounds bounds=source_bounds;
    bounds.pad(std::max(0.0F,padding));
    const auto canvas=workspace_layout().canvas;
    const float canvas_width=canvas.width;
    const float canvas_height=canvas.height;
    const auto vp=camera_.view_projection(canvas_width/canvas_height);
    const float left=canvas.x;
    const float right=canvas.x+canvas.width;
    const float top=canvas.y;
    const float bottom=canvas.y+canvas.height;
    float projected_min_x=std::numeric_limits<float>::max();
    float projected_min_y=std::numeric_limits<float>::max();
    float projected_max_x=std::numeric_limits<float>::lowest();
    float projected_max_y=std::numeric_limits<float>::lowest();
    bool projected_any=false;
    for(const float x:{bounds.minimum.x,bounds.maximum.x}){
        for(const float y:{bounds.minimum.y,bounds.maximum.y}){
            for(const float z:{bounds.minimum.z,bounds.maximum.z}){
                float screen_x=0.0F,screen_y=0.0F,depth=0.0F;
                if(!project_to_canvas({x,y,z},vp,
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
        const auto weapon_profiles=placement_weapon_profiles(*preset,placement_index);
        state.weapons.resize(weapon_profiles.size());
        if(preset->runtime_model_class_id==17U)
            thandor::simulation_initialize_class17_deployment(
                state,origin==SimulationStateOrigin::FreshPlacement,
                preset->runtime_uses_class17_root_child_deployment);
        for(std::size_t weapon_index=0;weapon_index<weapon_profiles.size();++weapon_index){
            const auto& weapon=weapon_profiles[weapon_index];
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
    if(created)++simulation_runtime_owner_revision_;
    return inserted->second;
}

std::optional<float> EditorApp::simulation_storage_child_z(const thandor::ModelVisualNode& parent,
    uint32_t slot,uint32_t faction,const thandor::SimulationLabRun* run) const {
    if(parent.runtime_class_id!=15U||slot!=0U||faction>=8U)return std::nullopt;
    if(!run){
        if(!simulation_lab_active())return std::nullopt;
        const auto runs=simulation_lab_runs();if(runs.empty())return std::nullopt;
        run=&runs[static_cast<size_t>(std::clamp(simulation_lab_selected_run_,0,static_cast<int>(runs.size())-1))];
    }
    const auto& economy=run->faction_economy[faction];
    const bool tritium=parent.simulation.class_specific_c0_raw==1;
    const auto capacity=tritium?economy.tritium_storage_limit_q4:economy.xenite_storage_limit_q4;
    if(!capacity)return std::nullopt;
    const auto z=thandor::simulation_storage_child_z_q12(parent,slot,
        tritium?economy.tritium_current_q4:economy.xenite_current_q4,*capacity);
    return z?std::optional<float>{thandor::q12_to_float(*z)}:std::nullopt;
}

std::vector<ModelHierarchyTransform> EditorApp::simulation_hierarchy_transforms(
    std::size_t placement_index,const thandor::ArmyAssetPreset& preset,
    thandor::SimulationActorState& actor,std::vector<uint8_t>* active_nodes,
    bool commit_articulated_contact_state){
    std::vector<ModelHierarchyTransform> transforms;
    if(!document_||placement_index>=document_->level().placements().size())return transforms;
    const auto hierarchy=placement_hierarchy(preset,placement_index);
    const auto weapon_profiles=placement_weapon_profiles(preset,placement_index);
    const bool resource_support_active=placement_has_required_resource_support(
        preset,document_->level().placements()[placement_index]);
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
            sprites[node_index]=&cached_sprite(node.sprite_path);
        }
        bool node_active=node.creates_runtime_node()&&sprites[node_index]!=nullptr;
        if(node.parent_index==std::numeric_limits<uint32_t>::max()){
            uint32_t root_yaw=actor.body_yaw_turn16;
            int32_t root_recoil=0;
            for(std::size_t weapon_index=0;
                weapon_index<weapon_profiles.size()&&weapon_index<actor.weapons.size();
                ++weapon_index){
                const auto& profile=weapon_profiles[weapon_index];
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
                        thandor::turn16_to_radians(root_yaw),contact.normal,contact.packed_normal_angles)
                    :placement_root_transform(contact_position.x,contact_position.y,contact_position.z,
                        thandor::turn16_to_radians(root_yaw)));
        }else{
            if(node.parent_index>=node_index||!active[node.parent_index]||sprites[node.parent_index]==nullptr)
                node_active=false;
            if(node_active&&!thandor::model_runtime_world_child_enabled(
                   hierarchy[node.parent_index].runtime_class_id,node.child_slot))
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
                        weapon_index<weapon_profiles.size()&&weapon_index<actor.weapons.size();
                        ++weapon_index){
                        const auto& profile=weapon_profiles[weapon_index];
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
                        resource_support_active&&
                        ((is_building&&animate_buildings_)||(!is_building&&animate_units_));
                    const uint32_t authored_animation_tick=is_building
                        ?current_building_animation_tick_:current_presentation_tick_;
                    const auto& animation_owner=hierarchy[node.parent_index];
                    const auto class13_factory=simulation_factory_states_.find(placement_index);
                    const bool class13_production_pose=(simulation_mode_||simulation_lab_active())&&
                        animation_owner.runtime_class_id==13U&&
                        class13_factory!=simulation_factory_states_.end();
                    const auto class22_airport=simulation_airport_states_.find(placement_index);
                    const bool class22_storage_pose=(simulation_mode_||simulation_lab_active())&&
                        animation_owner.runtime_class_id==22U&&
                        class22_airport!=simulation_airport_states_.end()&&
                        class22_airport->second.build_phase==thandor::SimulationClass22BuildPhase::Building;
                    const bool direct_animated_parent=
                        thandor::runtime_class_directly_dispatches_animated_model_subnodes(
                            animation_owner.runtime_class_id);
                    // Class 13 calls ArmyRuntime_UpdateAnimatedModelSubnodes only while the build
                    // state is serviced.  Its live child pose then remains where it stopped while
                    // the door opens, the produced unit drives clear, and the door closes.  A
                    // global presentation clock made factory internals continue spinning while
                    // retail had frozen them.
                    const bool time_driven_subnode_active=class13_production_pose||
                        class22_storage_pose||direct_animated_parent||animate_authored_subnodes;
                    const uint32_t subnode_animation_tick=class13_production_pose
                        ?class13_factory->second.animated_subnode_ticks
                        :(class22_storage_pose?class22_airport->second.animated_subnode_ticks
                          :(direct_animated_parent?current_presentation_tick_:authored_animation_tick));
                    const bool root_child=
                        animation_owner.parent_index==std::numeric_limits<uint32_t>::max();
                    if(preset.runtime_uses_class17_root_child_deployment&&root_child&&node.child_slot<3U){
                        angle1=thandor::wrap_turn16(static_cast<int64_t>(angle1)+
                            actor.class17_rotor_tilt_turn16);
                        simulated_node=true;
                    }else if(!simulated_node&&time_driven_subnode_active&&
                             !thandor::simulation_model_node_is_weapon_aim_axis(
                                 weapon_profiles,static_cast<uint32_t>(node_index))){
                        const int32_t authored_yaw_step=
                            thandor::model_runtime_child_yaw_step_turn16(
                                animation_owner,node.child_slot);
                        if(authored_yaw_step!=0)
                            angle2=thandor::wrap_turn16(static_cast<int64_t>(angle2)+
                                static_cast<int64_t>(authored_yaw_step)*subnode_animation_tick);
                    }
                    angle2=thandor::wrap_turn16(static_cast<int64_t>(angle2)+recoil_angle_turn16);
                    auto local=fixed_model_rotation_basis(angle2,angle1,node.local_rotation_angle0);
                    local.translation=attachment->local_translation;
                    if(placement_index<document_->level().placements().size())
                        if(const auto z=simulation_storage_child_z(hierarchy[node.parent_index],node.child_slot,
                            runtime_faction_for_placement(placement_index,
                                document_->level().placements()[placement_index].faction_index)))
                            local.translation.z=*z;
                    if(class22_airport!=simulation_airport_states_.end()&&root_child&&
                       node.child_slot==0U&&
                       class22_airport->second.dispatch_phase!=thandor::SimulationClass22DispatchPhase::Idle)
                        local.translation.z=thandor::q12_to_float(class22_airport->second.platform_local_z_q12);
                    if(time_driven_subnode_active&&
                       thandor::model_runtime_child_uses_bounded_vertical_channel(
                           animation_owner,node.child_slot))
                        local.translation.z=animated_child2_translation_q12(
                            animation_owner.simulation,subnode_animation_tick,local.translation.z);
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
        const auto articulated_pose=apply_articulated_walker_support_pose(hierarchy,transforms,
            preset.articulated_root_height_q12,preset.articulated_contact_interpolation_rate_q12,
            preset.articulated_contact_lift_q12,preset.articulated_contact_steering_limit_turn16,
            &actor,commit_articulated_contact_state,
            [&](float x,float y){return terrain_height_near(x,y);},
            [&](uint32_t node_index){
                return node_index<sprites.size()&&sprites[node_index]!=nullptr
                    ?sprites[node_index]->placement_height_offset():0.0F;
            });
        if(articulated_pose.applied&&commit_articulated_contact_state){
            // Only the 20 Hz simulation update may commit the solved contact-root X/Y/Z. Render,
            // attachment, and effect queries reuse the frozen contact state without advancing it.
            actor.position=articulated_pose.root_position;
        }
    }
    if(active_nodes){
        active_nodes->assign(active.size(),0U);
        for(std::size_t i=0;i<active.size();++i)(*active_nodes)[i]=active[i]?1U:0U;
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
        for(const auto& attachment:cached_sprite(node.sprite_path).lookup_records()){
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
        const auto transform_hulls=[&](std::vector<TransformedBoundsHull>& hulls,
                                       AxisAlignedBounds* updated_bounds){
            for(auto& hull:hulls){
                if(!hull.valid)continue;
                for(auto& corner:hull.corners){
                    const Vec3f relative=sub(corner,old_root);
                    corner={new_root.x+relative.x*cosine-relative.y*sine,
                            new_root.y+relative.x*sine+relative.y*cosine,
                            new_root.z+relative.z};
                    if(updated_bounds)updated_bounds->include(corner);
                }
            }
        };
        AxisAlignedBounds updated_bounds;
        transform_hulls(placement_pick_hulls_[placement_index],&updated_bounds);
        if(placement_index<placement_model_hulls_.size())
            transform_hulls(placement_model_hulls_[placement_index],nullptr);
        placement_pick_bounds_[placement_index]=updated_bounds;
        placement_pick_bounds_[placement_index].pad(0.20F);
    }
    document_->mark_dirty();
}

std::vector<TransformedBoundsHull> EditorApp::simulation_transient_pick_hulls(
    uint32_t army_asset_id,uint32_t faction_index,thandor::SimulationActorState& actor,
    const SimulationLinkedChildEntity* linked_child,const thandor::SimulationLabEntity* runtime_entity){
    std::vector<TransformedBoundsHull> hulls;
    if(!document_||faction_index==0U)return hulls;
    const auto* preset=catalog_.army_preset(army_asset_id);
    if(preset==nullptr)return hulls;
    std::vector<thandor::ModelVisualNode> resolved;
    if(!runtime_entity||!runtime_entity->runtime_model_hierarchy)
        resolved=thandor::resolve_faction_technology_hierarchy(
            *preset,document_->faction_technology_mask(std::min(faction_index,7U)));
    std::vector<thandor::ModelVisualNode> fallback_nodes;
    std::span<const thandor::ModelVisualNode> hierarchy=!resolved.empty()
        ?std::span<const thandor::ModelVisualNode>(resolved)
        :(preset->resolved_model_hierarchy.empty()
            ?std::span<const thandor::ModelVisualNode>(preset->model_hierarchy)
            :std::span<const thandor::ModelVisualNode>(preset->resolved_model_hierarchy));
    if(runtime_entity&&runtime_entity->runtime_model_hierarchy)hierarchy=*runtime_entity->runtime_model_hierarchy;
    if(hierarchy.empty()){
        thandor::ModelVisualNode fallback;
        fallback.parent_index=std::numeric_limits<uint32_t>::max();
        fallback.local_rotation_angle1=0x4000U;
        fallback.sprite_path=preset->sprite_path;
        fallback_nodes.push_back(std::move(fallback));
        hierarchy=fallback_nodes;
    }
    std::vector<ModelHierarchyInstance> instances;
    std::vector<bool> active;
    instances.reserve(hierarchy.size());active.reserve(hierarchy.size());
    for(std::size_t node_index=0;node_index<hierarchy.size();++node_index){
        const auto& node=hierarchy[node_index];
        ModelHierarchyInstance instance{};instance.node=&node;
        instance.root=node.parent_index==std::numeric_limits<uint32_t>::max();
        bool node_active=node.creates_runtime_node()&&!node.sprite_path.empty()&&
            (!runtime_entity||thandor::simulation_lab_model_node_present(*runtime_entity,node_index));
        const thandor::SpriteAsset* node_sprite=node_active?&cached_sprite(node.sprite_path):nullptr;
        if(instance.root){
            const auto contact=placement_contact_pose(*preset,actor.position.x,actor.position.y,
                node_sprite?node_sprite->placement_height_offset():0.0F);
            const Vec3f root_position{actor.position.x,actor.position.y,contact.height};
            if(preset->runtime_model_class_id==17U){
                instance.transform=class17_banked_root_transform(root_position,
                    actor.body_yaw_turn16,actor.class17_bank_heading_turn16,
                    actor.class17_bank_angle_turn16,
                    contact.align_to_normal?contact.normal:Vec3f{0.0F,0.0F,1.0F});
            }else{
                instance.transform=contact.align_to_normal
                    ?terrain_contact_root_transform(root_position,
                        thandor::turn16_to_radians(actor.body_yaw_turn16),contact.normal,contact.packed_normal_angles)
                    :placement_root_transform(root_position.x,root_position.y,root_position.z,
                        thandor::turn16_to_radians(actor.body_yaw_turn16));
            }
        }else{
            if(node.parent_index>=instances.size()||!active[node.parent_index])node_active=false;
            if(node_active){
                const auto& parent=instances[node.parent_index];
                const auto* attachment=parent.sprite->child_attachment(node.child_slot);
                if(attachment==nullptr)node_active=false;
                else{
                    uint32_t angle1=node.local_rotation_angle1;
                    if(preset->runtime_uses_class17_root_child_deployment&&parent.root&&node.child_slot<3U)
                        angle1=thandor::wrap_turn16(static_cast<int64_t>(angle1)+
                            actor.class17_rotor_tilt_turn16);
                    uint32_t angle0=node.local_rotation_angle0;
                    if(linked_child!=nullptr&&parent.root&&node.child_slot==0U){
                        const bool flight_pose=linked_child->runtime.phase!=
                            thandor::SimulationClass21LinkedChildPhase::Attached||linked_child->landed;
                        if(flight_pose){angle0=0x8000U;angle1=linked_child->child0_pitch_turn16;}
                    }
                    auto local=fixed_model_rotation_basis(node.local_rotation_angle2,angle1,angle0);
                    local.translation=attachment->local_translation;
                    if(linked_child!=nullptr&&parent.root&&node.child_slot==0U)
                        local.translation.z=thandor::q12_to_float(linked_child->child0_local_z_q12);
                    instance.transform=compose_model_transform(parent.transform,local);
                }
            }
        }
        if(node_active)instance.sprite=node_sprite;
        active.push_back(node_active);instances.push_back(instance);
    }
    apply_walker_pose_to_instances(*preset,hierarchy,instances,&actor,
        [&](float x,float y){return terrain_height_near(x,y);});
    for(std::size_t index=0;index<instances.size();++index){
        if(index>=active.size()||!active[index]||instances[index].sprite==nullptr)continue;
        const auto& sprite=*instances[index].sprite;
        if(sprite.has_valid_header_bounds()){
            const auto minimum=sprite.local_bounds_min();
            const auto maximum=sprite.local_bounds_max();
            TransformedBoundsHull hull;hull.valid=true;
            for(uint32_t corner=0;corner<8U;++corner){
                const Vec3f local{
                    (corner&1U)!=0U?maximum.x:minimum.x,
                    (corner&2U)!=0U?maximum.y:minimum.y,
                    (corner&4U)!=0U?maximum.z:minimum.z};
                hull.corners[corner]=transform_point(instances[index].transform,local);
            }
            hulls.push_back(hull);
        }else if(sprite.bounding_radius()>0.0F){
            const Vec3f center=instances[index].transform.translation;
            const float radius=sprite.bounding_radius();
            AxisAlignedBounds bounds;
            bounds.include({center.x-radius,center.y-radius,center.z-radius});
            bounds.include({center.x+radius,center.y+radius,center.z+radius});
            hulls.push_back(TransformedBoundsHull::from_axis_aligned(bounds));
        }
    }
    return hulls;
}

std::optional<EditorApp::SimulationModelRaycastHit> EditorApp::simulation_model_raycast_nearest(
    const ViewRay& ray,float maximum_distance,uint32_t query_tick,
    std::optional<std::size_t> excluded_placement,
    std::optional<SimulationTransientRef> excluded_transient){
    if(!document_||maximum_distance<=0.0F)return std::nullopt;
    const auto placements=document_->level().placements();
    std::optional<SimulationModelRaycastHit> nearest;
    const auto consider=[&](std::span<const TransformedBoundsHull> hulls,
                            const SimulationModelRaycastHit& identity){
        for(const auto& hull:hulls){
            const auto hit=ray_transformed_bounds_distance(ray,hull);
            if(!hit||*hit<0.0F||*hit>maximum_distance)continue;
            if(!nearest||*hit<nearest->distance){
                nearest=identity;
                nearest->distance=*hit;
            }
        }
    };
    for(std::size_t index=0;index<placement_model_hulls_.size()&&index<placements.size();++index){
        if(excluded_placement&&index==*excluded_placement)continue;
        if(const auto destruction=building_destruction_states_.find(index);
           destruction!=building_destruction_states_.end()){
            const auto* definition=destruction->second.effect_id==0U?nullptr:
                catalog_.effect_definition(destruction->second.effect_id);
            const uint32_t lifetime=definition?thandor::effect_lifetime_ticks(*definition):0U;
            const uint32_t age=query_tick>=destruction->second.start_tick
                ?query_tick-destruction->second.start_tick:0U;
            // Retail destruction keeps the hierarchy in the WorldRuntime owner list only until
            // the recovered +0x190 effect completes. Keep editor pick hulls for restore/select,
            // but stop the removed model hierarchy from blocking simulation LOS/projectiles.
            if(lifetime==0U||age>=lifetime)continue;
        }
        consider(placement_model_hulls_[index],SimulationModelRaycastHit{
            0.0F,index,std::nullopt,placements[index].faction_index,placements[index].army_asset_id});
    }
    for(auto& [serial,entity]:simulation_produced_entities_){
        if(entity.maximum_integrity>0&&entity.integrity<=0)continue;
        const SimulationTransientRef ref{SimulationTransientKind::ProducedClass13,serial};
        if(excluded_transient&&*excluded_transient==ref)continue;
        auto hulls=simulation_transient_pick_hulls(entity.army_asset_id,entity.faction_index,entity.actor,nullptr);
        consider(hulls,SimulationModelRaycastHit{0.0F,std::nullopt,ref,
            entity.faction_index,entity.army_asset_id});
    }
    for(auto& [serial,entity]:simulation_linked_child_entities_){
        if(!entity.visible||(entity.maximum_integrity>0&&entity.integrity<=0))continue;
        const SimulationTransientRef ref{SimulationTransientKind::LinkedClass21,serial};
        if(excluded_transient&&*excluded_transient==ref)continue;
        auto hulls=simulation_transient_pick_hulls(entity.army_asset_id,entity.faction_index,entity.actor,&entity);
        consider(hulls,SimulationModelRaycastHit{0.0F,std::nullopt,ref,
            entity.faction_index,entity.army_asset_id});
    }
    return nearest;
}

void EditorApp::simulation_refresh_mode0_projectile_raycast(
    thandor::SimulationProjectileState& projectile,const thandor::ShotDefinitionRecord& shot,
    uint32_t current_tick){
    if(!document_||shot.trajectory_mode!=0)return;
    const int64_t maximum_q12=static_cast<int64_t>(shot.launch_speed_q12)*
        static_cast<int64_t>(shot.projectile_lifetime_ticks);
    if(maximum_q12<=0)return;
    const float maximum_distance=thandor::q12_to_float(static_cast<int32_t>(
        std::min<int64_t>(maximum_q12,std::numeric_limits<int32_t>::max())));
    Vec3f direction=normalize(projectile.launch_velocity);
    if(vec3_length(direction)<=0.0001F){
        const float yaw=thandor::turn16_to_radians(projectile.launch_yaw_turn16);
        const float pitch=thandor::turn16_to_radians(projectile.launch_pitch_turn16);
        direction={std::cos(yaw)*std::cos(pitch),std::sin(yaw)*std::cos(pitch),std::sin(pitch)};
    }
    std::optional<std::size_t> excluded_placement;
    std::optional<SimulationTransientRef> excluded_transient;
    switch(projectile.owner_kind){
        case thandor::SimulationProjectileOwnerKind::Placement:
            excluded_placement=projectile.owner_placement_index;break;
        case thandor::SimulationProjectileOwnerKind::ProducedClass13:
            excluded_transient=SimulationTransientRef{SimulationTransientKind::ProducedClass13,
                projectile.owner_runtime_serial};break;
        case thandor::SimulationProjectileOwnerKind::LinkedClass21:
            excluded_transient=SimulationTransientRef{SimulationTransientKind::LinkedClass21,
                projectile.owner_runtime_serial};break;
        case thandor::SimulationProjectileOwnerKind::Environmental:
            // Retail EffectRuntime linked shots call ShotRuntimePool_CreateProjectileFromDefinition
            // with ownerArmyRuntime=null. Do not fabricate a shooter exclusion.
            break;
    }

    struct Candidate {
        float distance{std::numeric_limits<float>::max()};
        thandor::SimulationImpactKind kind{thandor::SimulationImpactKind::None};
        uint32_t material{};
        uint32_t effect_id{};
        std::optional<std::size_t> placement_index;
        std::optional<SimulationTransientRef> transient;
    } best;
    if(const auto model=simulation_model_raycast_nearest(ViewRay{projectile.launch_position,direction},
           maximum_distance,current_tick,excluded_placement,excluded_transient)){
        if(const auto* preset=catalog_.army_preset(model->army_asset_id);
           preset&&preset->shot_impact_class_index<8U){
            const uint32_t effect=shot.target_impact_effect_id(preset->shot_impact_class_index);
            if(effect!=0U)best={model->distance,thandor::SimulationImpactKind::TargetModelImpactClass,
                preset->shot_impact_class_index,effect,model->placement_index,model->transient};
        }
    }
    if(const auto terrain=thandor::raycast_field_surface_compatibility(document_->field(),
           projectile.launch_position,direction,maximum_distance,
           thandor::FieldRaycastSurface::Terrain,0x400U)){
        const uint32_t material=terrain->nearest_vertex_x<document_->field().width()&&
            terrain->nearest_vertex_y<document_->field().height()
            ?static_cast<uint32_t>(document_->field().at(
                terrain->nearest_vertex_x,terrain->nearest_vertex_y).material()):0U;
        const uint32_t effect=shot.terrain_impact_effect_id(material);
        if(effect!=0U&&terrain->distance<best.distance)
            best={terrain->distance,thandor::SimulationImpactKind::TerrainSurface,material,effect,std::nullopt,std::nullopt};
    }
    if(shot.primary_effect_id!=0U){
        if(const auto secondary=thandor::raycast_field_surface_compatibility(document_->field(),
               projectile.launch_position,direction,maximum_distance,
               thandor::FieldRaycastSurface::Secondary,0x400U);
           secondary&&secondary->distance<best.distance)
            best={secondary->distance,thandor::SimulationImpactKind::SecondarySurface,0U,
                shot.primary_effect_id,std::nullopt,std::nullopt};
    }
    const float resolved_distance=best.kind==thandor::SimulationImpactKind::None
        ?maximum_distance:best.distance;
    const Vec3f resolved=add(projectile.launch_position,mul(direction,resolved_distance));
    projectile.target_position=resolved;
    projectile.impact_position=resolved;
    projectile.impact_kind=best.kind;
    projectile.impact_material_index=best.material;
    projectile.impact_effect_id=best.effect_id;
    projectile.impact_target_placement_index=best.placement_index;
    projectile.impact_target_runtime_kind.reset();
    projectile.impact_target_runtime_serial=0U;
    if(best.transient){
        projectile.impact_target_runtime_kind=best.transient->kind==SimulationTransientKind::ProducedClass13
            ?thandor::SimulationProjectileOwnerKind::ProducedClass13
            :thandor::SimulationProjectileOwnerKind::LinkedClass21;
        projectile.impact_target_runtime_serial=best.transient->serial;
    }
    if(const auto* impact=catalog_.effect_definition(best.effect_id))
        projectile.impact_visual_lifetime_ticks=std::max(1U,thandor::effect_lifetime_ticks(*impact));
    else projectile.impact_visual_lifetime_ticks=1U;
}

void EditorApp::simulation_refresh_non_direct_projectile_model_collision(
    thandor::SimulationProjectileState& projectile,const thandor::ShotDefinitionRecord& shot,
    uint32_t current_tick){
    if(!document_||shot.trajectory_mode==0||current_tick<=projectile.spawn_tick)return;
    const uint32_t age=current_tick-projectile.spawn_tick;
    if(age>projectile.lifetime_ticks)return;
    // Retail tests collision before advancing the model for this maintenance substep. Once an
    // earlier contact tick has completed, later owner motion must not move or replace that hit.
    if(projectile.impact_kind!=thandor::SimulationImpactKind::None&&
       projectile.travel_ticks<age)return;

    const Vec3f segment_start=thandor::simulation_projectile_position(
        projectile,shot,current_tick-1U);
    const Vec3f segment_end=thandor::simulation_projectile_position(
        projectile,shot,current_tick);
    const Vec3f segment_delta=sub(segment_end,segment_start);
    const float segment_distance=vec3_length(segment_delta);
    if(!std::isfinite(segment_distance)||segment_distance<=0.0001F)return;
    const Vec3f direction=mul(segment_delta,1.0F/segment_distance);

    std::optional<std::size_t> excluded_placement;
    std::optional<SimulationTransientRef> excluded_transient;
    switch(projectile.owner_kind){
        case thandor::SimulationProjectileOwnerKind::Placement:
            excluded_placement=projectile.owner_placement_index;break;
        case thandor::SimulationProjectileOwnerKind::ProducedClass13:
            excluded_transient=SimulationTransientRef{SimulationTransientKind::ProducedClass13,
                projectile.owner_runtime_serial};break;
        case thandor::SimulationProjectileOwnerKind::LinkedClass21:
            excluded_transient=SimulationTransientRef{SimulationTransientKind::LinkedClass21,
                projectile.owner_runtime_serial};break;
        case thandor::SimulationProjectileOwnerKind::Environmental:
            // Retail EffectRuntime linked shots call ShotRuntimePool_CreateProjectileFromDefinition
            // with ownerArmyRuntime=null. Do not fabricate a shooter exclusion.
            break;
    }
    const auto model=simulation_model_raycast_nearest(ViewRay{segment_start,direction},
        segment_distance,current_tick,excluded_placement,excluded_transient);
    if(!model)return;

    // ModelRuntime_RaycastCandidateListNearestCf returns only the nearest WorldRuntime owner.
    // Shot maintenance then checks that owner's MDL impact class and SHT effect slot. A null slot
    // makes this model candidate ineligible; retail does not continue searching behind it for a
    // second model, though an already-known terrain/secondary candidate may still win this tick.
    const auto* preset=catalog_.army_preset(model->army_asset_id);
    if(!preset||preset->shot_impact_class_index>=8U)return;
    const uint32_t effect_id=shot.target_impact_effect_id(preset->shot_impact_class_index);
    if(effect_id==0U)return;

    if((projectile.impact_kind==thandor::SimulationImpactKind::TerrainSurface||
        projectile.impact_kind==thandor::SimulationImpactKind::SecondarySurface)&&
       projectile.travel_ticks==age){
        const float surface_distance=vec3_length(sub(projectile.impact_position,segment_start));
        // The retail comparisons use model <= surface, so a same-distance model contact wins.
        if(std::isfinite(surface_distance)&&surface_distance+1.0e-6F<model->distance)return;
    }

    projectile.impact_kind=thandor::SimulationImpactKind::TargetModelImpactClass;
    projectile.impact_material_index=preset->shot_impact_class_index;
    projectile.impact_effect_id=effect_id;
    projectile.impact_target_placement_index=model->placement_index;
    projectile.impact_target_runtime_kind.reset();
    projectile.impact_target_runtime_serial=0U;
    if(model->transient){
        projectile.impact_target_runtime_kind=model->transient->kind==SimulationTransientKind::ProducedClass13
            ?thandor::SimulationProjectileOwnerKind::ProducedClass13
            :thandor::SimulationProjectileOwnerKind::LinkedClass21;
        projectile.impact_target_runtime_serial=model->transient->serial;
    }
    projectile.impact_position=add(segment_start,mul(direction,model->distance));
    projectile.target_position=projectile.impact_position;
    projectile.travel_ticks=age;
    if(const auto* impact=catalog_.effect_definition(effect_id))
        projectile.impact_visual_lifetime_ticks=std::max(1U,thandor::effect_lifetime_ticks(*impact));
    else projectile.impact_visual_lifetime_ticks=1U;
}

bool EditorApp::simulation_line_of_fire_clear(std::size_t shooter_index,
    std::optional<std::size_t> intended_target_index,Vec3f source,Vec3f target,
    const thandor::ShotDefinitionRecord& shot,bool support_or_repair_command,uint32_t query_tick,
    std::optional<SimulationTransientRef> excluded_transient,
    std::optional<SimulationTransientRef> intended_transient,bool exclude_shooter_placement){
    if(!document_)return false;
    const auto placements=document_->level().placements();
    if(shooter_index>=placements.size())return false;
    const uint32_t shooter_faction=placements[shooter_index].faction_index;
    const Vec3f delta=sub(target,source);
    const float target_distance=vec3_length(delta);
    if(!std::isfinite(target_distance)||target_distance<=0.0001F)return true;

    const auto policy=thandor::shot_line_of_fire_policy(shot.trajectory_kind());
    if(!policy.test_terrain&&!policy.test_models)return true; // trajectory mode 2

    // 0x0052B9D0 recomputes the ballistic angle for LOS and rejects a negative discriminant,
    // unlike projectile creation which clamps that discriminant to zero.
    const auto los_angles=thandor::simulation_shot_line_of_fire_angles(source,target,shot);
    if(!los_angles.valid)return false;

    Vec3f direction{};
    float query_distance=target_distance;
    if(shot.trajectory_kind()==thandor::ShotTrajectoryMode::Ballistic){
        const float yaw=thandor::turn16_to_radians(los_angles.yaw_turn16);
        const float pitch=thandor::turn16_to_radians(los_angles.pitch_turn16);
        direction={std::cos(yaw)*std::cos(pitch),std::sin(yaw)*std::cos(pitch),std::sin(pitch)};
        // Retail passes the horizontal source/target length to the model candidate raycast.
        query_distance=std::hypot(delta.x,delta.y);
    }else{
        direction=mul(delta,1.0F/target_distance);
        const int64_t full_ray_q12=static_cast<int64_t>(shot.launch_speed_q12)*
            static_cast<int64_t>(shot.projectile_lifetime_ticks);
        if(full_ray_q12>0)
            query_distance=std::max(target_distance,thandor::q12_to_float(static_cast<int32_t>(
                std::min<int64_t>(full_ray_q12,std::numeric_limits<int32_t>::max()))));
    }
    if(query_distance<=0.0F)return false;

    std::optional<float> terrain_distance;
    if(policy.test_terrain){
        const auto terrain_hit=thandor::raycast_field_surface_compatibility(
            document_->field(),source,direction,query_distance,
            thandor::FieldRaycastSurface::Terrain,0x400U);
        if(terrain_hit.has_value())terrain_distance=terrain_hit->distance;
    }

    std::optional<SimulationModelRaycastHit> model_hit;
    if(policy.test_models){
        // Retail ModelRuntime_RaycastCandidateListNearestCf walks one WorldRuntime owner list.
        // Class-13 products and class-21 linked children therefore participate in the same nearest
        // model query as authored LEV placements; keep source/target identity so self and intended
        // transient hits receive the same relation handling as persisted owners.
        const std::optional<std::size_t> excluded_placement=
            exclude_shooter_placement?std::optional<std::size_t>{shooter_index}:std::nullopt;
        model_hit=simulation_model_raycast_nearest(ViewRay{source,direction},query_distance,query_tick,
            excluded_placement,excluded_transient);
    }
    const std::optional<float> model_distance=model_hit
        ?std::optional<float>{model_hit->distance}:std::nullopt;

    // Ballistic mode uses only the nearest model result. Direct/lead compare the two candidate
    // distances first, exactly like the retail branch at 0x0052B9D0.
    bool nearest_is_model=model_distance.has_value();
    if(policy.test_terrain&&terrain_distance.has_value()&&
       (!model_distance||*terrain_distance<*model_distance))nearest_is_model=false;

    if(nearest_is_model&&model_hit){
        const bool same_owner=model_hit->faction_index==shooter_faction;
        const bool relation_transparent=support_or_repair_command?!same_owner:same_owner;
        const bool intended_static=model_hit->placement_index&&intended_target_index&&
            *model_hit->placement_index==*intended_target_index;
        const bool intended_dynamic=model_hit->transient&&intended_transient&&
            *model_hit->transient==*intended_transient;
        if(!relation_transparent&&!intended_static&&!intended_dynamic)return false;
        // A transparent relation or intended target falls through directly to range evaluation;
        // retail does not continue searching for another model behind it.
    }else if(policy.test_terrain&&terrain_distance){
        // For point targets (no target entity), terrain contact within +/-0x400 Q12 of the desired
        // point is accepted. Any earlier/later terrain contact blocks.
        const bool point_target=!intended_target_index.has_value()&&!intended_transient.has_value();
        if(point_target&&std::abs(*terrain_distance-target_distance)<=thandor::q12_to_float(0x400)){
            // accepted; continue to range evaluation
        }else return false;
    }

    if(const auto line_range_q12=thandor::shot_line_of_fire_range_q12(shot);
       line_range_q12.has_value()){
        int64_t compared_distance_q12=thandor::float_to_q12(target_distance);
        int32_t target_radius_q12=0;
        if(intended_target_index&&*intended_target_index<placements.size()){
            const auto& target_placement=placements[*intended_target_index];
            if(const auto* target_preset=catalog_.army_preset(target_placement.army_asset_id))
                target_radius_q12=target_preset->simulation_motion_profile.collision_radius_q12;
        }else if(intended_transient){
            uint32_t asset_id=0U;
            if(intended_transient->kind==SimulationTransientKind::ProducedClass13){
                if(const auto found=simulation_produced_entities_.find(intended_transient->serial);
                   found!=simulation_produced_entities_.end())asset_id=found->second.army_asset_id;
            }else{
                if(const auto found=simulation_linked_child_entities_.find(intended_transient->serial);
                   found!=simulation_linked_child_entities_.end())asset_id=found->second.army_asset_id;
            }
            if(const auto* target_preset=asset_id==0U?nullptr:catalog_.army_preset(asset_id))
                target_radius_q12=target_preset->simulation_motion_profile.collision_radius_q12;
        }
        if(target_radius_q12>0)
            compared_distance_q12=(compared_distance_q12*2-static_cast<int64_t>(target_radius_q12))/2;
        if(static_cast<int64_t>(*line_range_q12)<compared_distance_q12)return false;
    }
    return true;
}

void EditorApp::update_idle_class17_deployment(uint32_t presentation_tick){
    if(simulation_mode_||!document_||!animate_units_)return;
    bool changed=false;
    const auto placements=document_->level().placements();
    for(auto& [index,actor]:simulation_actor_states_){
        if(index>=placements.size()||actor.class17_rotor_deploy_ticks_remaining==0U)continue;
        const auto* preset=catalog_.army_preset(placements[index].army_asset_id);
        if(preset==nullptr||!preset->runtime_uses_class17_root_child_deployment){
            actor.class17_rotor_deploy_ticks_remaining=0U;
            actor.class17_rotor_tilt_turn16=0U;
            continue;
        }
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
    if(simulation_lab_snapshot_||simulation_lab_worker_.snapshot()){
        simulation_lab_worker_.clear();
        simulation_lab_snapshot_.reset();
        simulation_lab_ui_snapshot_.reset();
        simulation_lab_ui_debug_state_={};
        simulation_lab_last_ui_refresh_time_seconds_=0.0;
        simulation_lab_ui_refresh_initialized_=false;
        simulation_lab_snapshot_dirty_=false;
        simulation_lab_running_=false;
        simulation_lab_paused_=false;
        simulation_lab_last_schedule_time_seconds_=0.0;
        simulation_lab_tick_accumulator_ms_=0.0;
        simulation_lab_schedule_initialized_=false;
        simulation_lab_selected_run_=0;
        simulation_lab_last_observed_one_shot_sequence_.clear();
    }
    stop_all_runtime_looping_sounds();
    looping_sound_start_failures_.clear();runtime_sound_path_cache_.clear();runtime_sound_path_pattern_.clear();
    const bool had_actor_replacements=!simulation_actor_states_.empty()||!building_destruction_states_.empty();
    simulation_actor_states_.clear();
    simulation_runtime_factions_.clear();
    ++simulation_runtime_owner_revision_;
    simulation_attack_placement_targets_.clear();
    simulation_attack_point_targets_.clear();
    simulation_production_rally_points_.clear();
    for(auto& queue:simulation_secondary_production_queues_)queue.clear();
    simulation_faction_xenite_q4_.fill(0U);
    simulation_relation_transition_ticks_.fill(0x11U);
    simulation_economy_initialized_=false;
    simulation_factory_candidate_selection_.clear();
    simulation_factory_states_.clear();
    simulation_airport_candidate_selection_.clear();
    simulation_airport_states_.clear();
    simulation_airport_launch_lane_selection_.clear();
    simulation_airport_mission_heading_degrees_.clear();
    simulation_linked_child_entities_.clear();
    next_simulation_linked_child_serial_=1U;
    simulation_produced_entities_.clear();
    next_simulation_produced_entity_serial_=1U;
    hovered_placement_.reset();hovered_terrain_point_.reset();
    hovered_context_order_=thandor::ContextOrderKind::None;
    simulation_projectiles_.clear();
    simulation_integrity_states_.clear();
    environmental_effect_runtime_pose_history_.clear();
    movement_ground_effects_.clear();
    building_destruction_states_.clear();
    next_simulation_projectile_serial_=1U;
    simulation_active_actor_count_=0U;
    simulation_projectile_count_=0U;
    simulation_volley_count_=0U;
    last_simulation_tick_=std::numeric_limits<uint32_t>::max();
    if(!keep_target)simulation_target_point_.reset();
    if(had_actor_replacements)simulation_static_replacement_dirty_=true;
    last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
}

void EditorApp::ensure_simulation_economy_initialized(){
    if(simulation_economy_initialized_||!document_)return;
    for(uint32_t faction=1U;faction<simulation_faction_xenite_q4_.size();++faction)
        simulation_faction_xenite_q4_[faction]=document_->level().faction_xenite_q4(faction);
    simulation_economy_initialized_=true;
}

std::optional<std::pair<Vec3f,Vec3f>> EditorApp::simulation_class13_spawn_and_exit_points(
    std::size_t producer_index,const thandor::ArmyAssetPreset& producer_preset){
    if(!document_||producer_index>=document_->level().placements().size())return std::nullopt;
    const auto& placement=document_->level().placements()[producer_index];
    thandor::SimulationActorState fallback_actor;
    fallback_actor.position={placement.render_x(),placement.render_y(),
        terrain_height_near(placement.render_x(),placement.render_y())};
    fallback_actor.body_yaw_turn16=placement.orientation_turn16&0xFFFFU;
    thandor::SimulationActorState* actor=&fallback_actor;
    if(const auto live=simulation_actor_states_.find(producer_index);
       live!=simulation_actor_states_.end())actor=&live->second;
    const auto hierarchy=placement_hierarchy(producer_preset,producer_index);
    if(hierarchy.empty())return std::nullopt;
    auto transforms=simulation_hierarchy_transforms(producer_index,producer_preset,*actor,nullptr,false);
    const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const thandor::ModelVisualNode& node){
        return node.parent_index==std::numeric_limits<uint32_t>::max();
    });
    if(root==hierarchy.end())return std::nullopt;
    const std::size_t root_index=static_cast<std::size_t>(std::distance(hierarchy.begin(),root));
    if(root_index>=transforms.size()||root->sprite_path.empty())return std::nullopt;
    const auto& sprite=cached_sprite(root->sprite_path);
    const auto* spawn=sprite.find_lookup_record(thandor::ModelLookupKeyIndex{0U},
        thandor::ModelLookupKeyClass{5U});
    const auto* exit=sprite.find_lookup_record(thandor::ModelLookupKeyIndex{1U},
        thandor::ModelLookupKeyClass{5U});
    if(spawn==nullptr||exit==nullptr)return std::nullopt;
    return std::pair<Vec3f,Vec3f>{transform_point(transforms[root_index],spawn->local_translation),
                                  transform_point(transforms[root_index],exit->local_translation)};
}

std::optional<Vec3f> EditorApp::simulation_class13_default_drive_target(
    std::size_t producer_index,const thandor::ArmyAssetPreset& producer_preset){
    const auto points=simulation_class13_spawn_and_exit_points(producer_index,producer_preset);
    if(!points)return std::nullopt;
    // Retail reset/reselect restores class-13 movementTarget from model lookup class 5 / index 1.
    return points->second;
}

std::optional<Vec3f> EditorApp::simulation_class13_effective_drive_target(
    std::size_t producer_index,const thandor::ArmyAssetPreset& producer_preset){
    if(const auto custom=simulation_production_rally_points_.find(producer_index);
       custom!=simulation_production_rally_points_.end())return custom->second;
    return simulation_class13_default_drive_target(producer_index,producer_preset);
}

void EditorApp::update_class13_factory_production(std::size_t producer_index,
    const thandor::ArmyAssetPreset& producer_preset,uint32_t tick){
    if(!document_||producer_preset.runtime_model_class_id!=13U||
       producer_index>=document_->level().placements().size())return;
    const auto& placement=document_->level().placements()[producer_index];
    const uint32_t faction=placement.faction_index;
    if(faction==0U||faction>=simulation_secondary_production_queues_.size())return;
    ensure_simulation_economy_initialized();
    auto& state=simulation_factory_states_[producer_index];
    bool started_this_tick=false;

    const auto play_completion_sound=[&](){
        if(producer_preset.class13_completion_sound_asset_id==0U)return;
        auto& producer_actor=simulation_state_for(producer_index);
        Vec3f sound_position=producer_actor.position;
        const auto hierarchy=placement_hierarchy(producer_preset,producer_index);
        const auto transforms=simulation_hierarchy_transforms(
            producer_index,producer_preset,producer_actor,nullptr,false);
        const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){
            return node.parent_index==std::numeric_limits<uint32_t>::max();
        });
        if(root!=hierarchy.end()){
            const auto root_index=static_cast<std::size_t>(std::distance(hierarchy.begin(),root));
            if(root_index<transforms.size())sound_position=transforms[root_index].translation;
        }
        play_runtime_spatial_sound(producer_preset.class13_completion_sound_asset_id,
            sound_position,producer_preset.positioned_sound_gain_q15,
            producer_preset.positioned_sound_maximum_distance_q12);
    };

    if(state.phase==thandor::SimulationClass13ProductionPhase::Idle){
        auto& queue=simulation_secondary_production_queues_[faction];
        for(auto it=queue.begin();it!=queue.end();++it){
            const auto* candidate=catalog_.army_preset(*it);
            if(candidate==nullptr||!thandor::simulation_class13_candidate_compatible(
                   producer_preset.runtime_class_specific_c4_raw,candidate->army_runtime_flags14_raw))
                continue;
            if(candidate->retail_xenite_cost_q4>simulation_faction_xenite_q4_[faction])continue;
            simulation_faction_xenite_q4_[faction]-=candidate->retail_xenite_cost_q4;
            thandor::simulation_class13_begin_production(state,candidate->registry_id,
                candidate->retail_build_time_q5,static_cast<int32_t>(std::min<uint64_t>(
                    candidate->retail_energy_demand_q4,
                    static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))));
            queue.erase(it);
            started_this_tick=true;
            animate_buildings_=true;
            last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
            break;
        }
    }

    // Retail state 0 changes fallbackPosition0Q12 to 1 inside the switch case and then exits the
    // callback.  It cannot fall through to state 1 in the same 20 Hz update.  Advancing here made
    // every editor factory complete one simulation step early, including zero-duration records.
    if(started_this_tick)return;

    if(state.phase==thandor::SimulationClass13ProductionPhase::Building&&
       thandor::simulation_class13_tick_build(state)){
        const auto* produced_preset=catalog_.army_preset(state.produced_army_asset_id);
        const auto points=simulation_class13_spawn_and_exit_points(producer_index,producer_preset);
        if(produced_preset!=nullptr&&points){
            const Vec3f spawn=points->first;
            const Vec3f exit=points->second;
            SimulationProducedEntity entity;
            entity.serial=next_simulation_produced_entity_serial_++;
            entity.army_asset_id=produced_preset->registry_id;
            entity.faction_index=faction;
            entity.producer_placement_index=producer_index;
            entity.actor.position=spawn;
            entity.actor.position.z=terrain_height_near(spawn.x,spawn.y);
            entity.actor.body_yaw_turn16=thandor::radians_to_turn16(std::atan2(
                exit.y-spawn.y,exit.x-spawn.x));
            entity.actor.last_updated_tick=tick;
            entity.maximum_integrity=std::max(0,produced_preset->root_maximum_integrity);
            entity.integrity=entity.maximum_integrity;
            if(produced_preset->runtime_model_class_id==17U)
                thandor::simulation_initialize_class17_deployment(entity.actor,true,
                    produced_preset->runtime_uses_class17_root_child_deployment);
            const uint64_t serial=entity.serial;
            simulation_produced_entities_[serial]=std::move(entity);
            // The new actor is dynamically drawable in this same frame. Rebuild the static
            // texture residency set after simulation update and before dynamic-scene emission.
            simulation_static_replacement_dirty_=true;
            thandor::simulation_class13_mark_spawned(state,serial);
            play_completion_sound();
            last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
        }
    }else if(state.phase==thandor::SimulationClass13ProductionPhase::Opening){
        if(thandor::simulation_class13_tick_open(state,producer_preset.runtime_class_step_q12)){
            const auto points=simulation_class13_spawn_and_exit_points(producer_index,producer_preset);
            const auto produced=simulation_produced_entities_.find(state.produced_entity_serial);
            if(points&&produced!=simulation_produced_entities_.end()){
                produced->second.actor.move_target=points->second;
                produced->second.exit_command_issued=true;
                animate_units_=true;
            }
        }
        last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
    }else if(state.phase==thandor::SimulationClass13ProductionPhase::WaitingForProducedUnitClear){
        bool linked=false;
        if(const auto produced=simulation_produced_entities_.find(state.produced_entity_serial);
           produced!=simulation_produced_entities_.end())linked=produced->second.linked_to_producer;
        if(thandor::simulation_class13_observe_produced_unit_link(state,linked)){
            play_completion_sound();
        }
    }else if(state.phase==thandor::SimulationClass13ProductionPhase::Closing){
        (void)thandor::simulation_class13_tick_close(state,producer_preset.runtime_class_step_q12);
        last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
    }
}

std::optional<int32_t> EditorApp::simulation_class22_platform_baseline_q12(
    std::size_t producer_index,const thandor::ArmyAssetPreset& producer_preset){
    if(!document_||producer_index>=document_->level().placements().size())return std::nullopt;
    const auto hierarchy=placement_hierarchy(producer_preset,producer_index);
    const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const thandor::ModelVisualNode& node){
        return node.parent_index==std::numeric_limits<uint32_t>::max();
    });
    if(root==hierarchy.end()||root->sprite_path.empty())return std::nullopt;
    const auto& sprite=cached_sprite(root->sprite_path);
    const auto* baseline=sprite.find_lookup_record(thandor::ModelLookupKeyIndex{0U},
        thandor::ModelLookupKeyClass{1U});
    if(baseline==nullptr)return std::nullopt;
    return thandor::float_to_q12(baseline->local_translation.z);
}

void EditorApp::update_class22_airport_storage(std::size_t producer_index,
    const thandor::ArmyAssetPreset& producer_preset,uint32_t tick){
    (void)tick;
    if(!document_||producer_preset.runtime_model_class_id!=22U||
       producer_index>=document_->level().placements().size())return;
    const auto& placement=document_->level().placements()[producer_index];
    const uint32_t faction=placement.faction_index;
    if(faction==0U||faction>=simulation_secondary_production_queues_.size())return;
    ensure_simulation_economy_initialized();

    auto [state_it,inserted]=simulation_airport_states_.try_emplace(producer_index);
    auto& state=state_it->second;
    if(inserted){
        const uint32_t capacity=std::min<uint32_t>(producer_preset.runtime_class_specific_c4_raw,
            static_cast<uint32_t>(thandor::kSimulationClass22StoredSlotCount));
        const int32_t baseline=simulation_class22_platform_baseline_q12(producer_index,producer_preset)
            .value_or(0);
        thandor::simulation_class22_configure_storage(state,capacity,baseline);
    }

    bool started_this_tick=false;
    if(state.build_phase==thandor::SimulationClass22BuildPhase::Idle&&
       state.stored_slot_count<state.storage_capacity){
        auto& queue=simulation_secondary_production_queues_[faction];
        for(auto it=queue.begin();it!=queue.end();++it){
            const auto* candidate=catalog_.army_preset(*it);
            if(candidate==nullptr||
               !thandor::simulation_class22_candidate_compatible(candidate->army_runtime_flags14_raw))
                continue;
            if(candidate->retail_xenite_cost_q4>simulation_faction_xenite_q4_[faction])continue;
            simulation_faction_xenite_q4_[faction]-=candidate->retail_xenite_cost_q4;
            if(thandor::simulation_class22_begin_storage_build(state,candidate->registry_id,
                   candidate->retail_build_time_q5,static_cast<int32_t>(std::min<uint64_t>(
                       candidate->retail_energy_demand_q4,
                       static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))))){
                queue.erase(it);
                started_this_tick=true;
                animate_buildings_=true;
                last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
            }
            break;
        }
    }

    // The retail mode switch starts a build in mode 0 and exits that case. Do not advance the
    // newly-created build counter until the following 20 Hz update.
    if(!started_this_tick&&state.build_phase==thandor::SimulationClass22BuildPhase::Building){
        if(thandor::simulation_class22_tick_storage_build(state)){
            // Normal Simulation does not yet emulate the faction Energy allocator; as with the
            // existing class-13 preview, a queued editor build is treated as Energy-satisfied.
            (void)thandor::simulation_class22_complete_storage_build(state,true);
        }
        last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
    }

    const auto play_creation_sound=[&](){
        Vec3f position{placement.render_x(),placement.render_y(),
            terrain_height_near(placement.render_x(),placement.render_y())};
        play_runtime_spatial_sound(producer_preset.runtime_class_effect_sound_asset_id_26c,position,
            producer_preset.positioned_sound_gain_q15,
            producer_preset.positioned_sound_maximum_distance_q12);
    };

    // Runtime state 0/6 processes pending lane counts after the door/platform switch. The
    // spawner decrements a lane count before checking the persistent slot occupancy bit; a busy
    // matching slot therefore consumes that one request and the dispatcher can try the next lane.
    bool spawned_linked_child_this_tick=false;
    if(state.dispatch_phase==thandor::SimulationClass22DispatchPhase::Reset)
        (void)thandor::simulation_class22_reset_dispatch_cycle(state);
    if(state.dispatch_phase==thandor::SimulationClass22DispatchPhase::Idle){
        for(uint32_t lane=0;lane<3;++lane){
            if(!state.pending_launch_counts[lane])continue;
            --state.pending_launch_counts[lane];
            const std::optional<thandor::SimulationClass22LaunchRequest> request=thandor::SimulationClass22LaunchRequest{
                lane,thandor::kSimulationClass22LinkedArmyAssetIds[lane],state.launch_commands[lane]};
            const auto slot=thandor::simulation_class22_find_free_stored_slot(state,request->army_asset_id);
            if(!slot)continue;
            const auto* child_preset=catalog_.army_preset(request->army_asset_id);
            if(child_preset==nullptr||child_preset->runtime_model_class_id!=21U)continue;
            if(!thandor::simulation_class22_mark_linked_child_spawned(state,*slot))continue;
            SimulationLinkedChildEntity child;
            child.serial=next_simulation_linked_child_serial_++;
            child.army_asset_id=request->army_asset_id;
            child.faction_index=faction;
            child.producer_placement_index=producer_index;
            child.storage_slot=*slot;
            child.actor.position={placement.render_x(),placement.render_y(),
                terrain_height_near(placement.render_x(),placement.render_y())};
            child.actor.body_yaw_turn16=placement.orientation_turn16&0xFFFFU;
            child.actor.last_updated_tick=tick;
            child.mission_heading_turn16=static_cast<uint32_t>(request->command.value_a)&0xFFFFU;
            child.mission_world_x_q12=request->command.value_b;
            child.mission_world_y_q12=request->command.value_c;
            child.platform_reference_z_q12=state.platform_local_z_q12;
            child.child0_local_z_q12=state.platform_local_z_q12;
            child.maximum_integrity=std::max(0,child_preset->root_maximum_integrity);
            child.integrity=child.maximum_integrity;
            simulation_linked_child_entities_[child.serial]=std::move(child);
            // Class-21 children can carry their own hierarchy textures, EFFs and SHTs. Make the
            // residency rebuild happen before this frame's dynamic draw can consume them.
            simulation_static_replacement_dirty_=true;
            spawned_linked_child_this_tick=true;
            animate_units_=true;animate_buildings_=true;
            play_creation_sound();
            last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
            break;
        }
    }

    if(spawned_linked_child_this_tick)return;

    if(state.dispatch_phase==thandor::SimulationClass22DispatchPhase::Opening){
        if(thandor::simulation_class22_tick_open(state,producer_preset.runtime_class_step_q12)){
            Vec3f position{placement.render_x(),placement.render_y(),terrain_height_near(placement.render_x(),placement.render_y())};
            play_runtime_spatial_sound(producer_preset.creation_sound_asset_id_270,position,
                producer_preset.positioned_sound_gain_q15,producer_preset.positioned_sound_maximum_distance_q12);
        }
        last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
    }else if(state.dispatch_phase==thandor::SimulationClass22DispatchPhase::RaisingPlatform){
        (void)thandor::simulation_class22_tick_raise_platform(state,
            producer_preset.runtime_class_specific_c8_raw,producer_preset.runtime_class_specific_cc_raw);
        last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
    }else if(state.dispatch_phase==thandor::SimulationClass22DispatchPhase::LoweringPlatform){
        const int32_t baseline=simulation_class22_platform_baseline_q12(producer_index,producer_preset)
            .value_or(0);
        if(thandor::simulation_class22_tick_lower_platform(state,
            producer_preset.runtime_class_specific_c8_raw,baseline))play_creation_sound();
        last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
    }else if(state.dispatch_phase==thandor::SimulationClass22DispatchPhase::Closing){
        (void)thandor::simulation_class22_tick_close(state,producer_preset.runtime_class_step_q12);
        last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
    }else if(state.dispatch_phase==thandor::SimulationClass22DispatchPhase::Reset){
        (void)thandor::simulation_class22_reset_dispatch_cycle(state);
        last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
    }
}

void EditorApp::update_class21_linked_children(uint32_t tick){
    if(!document_)return;
    const auto placements=document_->level().placements();
    std::vector<uint64_t> destroy;
    const auto clamp_i32=[](int64_t value)->int32_t{
        // Native dword stores wrap; intermediate products use signed 64-bit arithmetic.
        return std::bit_cast<int32_t>(static_cast<uint32_t>(value));
    };
    const auto contact_height=[&](float x,float y,bool top=false){
        const auto surface=document_->field().sample_retail_surface_heights_q12(
            thandor::float_to_q12(x),thandor::float_to_q12(y));
        return surface.valid?thandor::q12_to_float(top?surface.top:surface.terrain):0.0F;
    };
    const auto q12_mul=[&](int32_t a,int32_t b)->int32_t{
        return clamp_i32((static_cast<int64_t>(a)*static_cast<int64_t>(b))>>12);
    };
    const auto scale_i32=[&](int32_t value,int32_t count)->int32_t{
        return clamp_i32(static_cast<int64_t>(value)*static_cast<int64_t>(count));
    };
    const auto update_arc_pose=[&](SimulationLinkedChildEntity& child,int32_t coefficient_q12,
                                   int32_t reference_z_q12,bool terrain_relative){
        const int32_t square=q12_mul(child.movement_accumulator_q12,child.movement_accumulator_q12);
        const int32_t arc=q12_mul(coefficient_q12,square);
        int64_t local_z=static_cast<int64_t>(reference_z_q12)+arc;
        if(terrain_relative)local_z-=thandor::float_to_q12(contact_height(
            child.actor.position.x,child.actor.position.y,true));
        child.child0_local_z_q12=clamp_i32(local_z);
        child.child0_pitch_turn16=thandor::simulation_class21_flight_pitch_turn16(
            child.movement_accumulator_q12,coefficient_q12);
    };
    const auto advance=[&](SimulationLinkedChildEntity& child,int32_t speed_q12){
        const auto delta=thandor::retail_direction_scaled(0,child.actor.body_yaw_turn16,speed_q12);
        child.actor.position.x=thandor::q12_to_float(clamp_i32(
            int64_t(thandor::float_to_q12(child.actor.position.x))+delta[0]));
        child.actor.position.y=thandor::q12_to_float(clamp_i32(
            int64_t(thandor::float_to_q12(child.actor.position.y))+delta[1]));
        child.actor.position.z=contact_height(child.actor.position.x,child.actor.position.y);
    };
    const auto resolved_asset_hierarchy=[&](const thandor::ArmyAssetPreset& preset,uint32_t faction){
        std::vector<thandor::ModelVisualNode> resolved=thandor::resolve_faction_technology_hierarchy(
            preset,document_->faction_technology_mask(std::min(faction,7U)));
        if(resolved.empty()){
            const auto& source=preset.resolved_model_hierarchy.empty()?preset.model_hierarchy:
                preset.resolved_model_hierarchy;
            resolved.assign(source.begin(),source.end());
        }
        return resolved;
    };
    const auto hierarchy_damage_metrics=[&](std::span<const thandor::ModelVisualNode> nodes){
        std::array<int32_t,8> metrics{};
        for(const auto& node:nodes){
            if(!node.simulation.has_weapon_definition())continue;
            const auto* shot=catalog_.shot_definition(node.simulation.shot_definition_id);
            if(!shot)continue;
            for(std::size_t i=0;i<metrics.size();++i){
                const uint32_t sum=static_cast<uint32_t>(metrics[i])+static_cast<uint32_t>(
                    shot->target_impact_damage_q12[i]);
                metrics[i]=static_cast<int32_t>(sum);
            }
        }
        return metrics;
    };
    struct State5Candidate {
        Vec3f position{};
        int32_t collision_radius_q12{};
        uint32_t runtime_class_id{};
        uint32_t impact_class_index{};
        std::array<int32_t,8> damage_metrics{};
        std::optional<std::size_t> placement_index;
        std::optional<SimulationTransientRef> transient;
    };
    const auto choose_state5_candidate=[&](uint64_t source_serial,const SimulationLinkedChildEntity& source,
                                           const thandor::ArmyAssetPreset& source_preset,
                                           const std::array<int32_t,8>& source_metrics,
                                           const thandor::ShotDefinitionRecord& source_shot,
                                           bool source_runtime_field18_nonzero)
        ->std::optional<State5Candidate>{
        std::optional<State5Candidate> best;
        uint32_t best_score=0U;
        const int32_t sx=thandor::float_to_q12(source.actor.position.x);
        const int32_t sy=thandor::float_to_q12(source.actor.position.y);
        const auto consider=[&](uint32_t owner,const thandor::ArmyAssetPreset& candidate_preset,
                                Vec3f position,std::span<const thandor::ModelVisualNode> candidate_hierarchy,
                                std::optional<std::size_t> placement_index,
                                std::optional<SimulationTransientRef> transient){
            if(owner==0U||owner==source.faction_index)return;
            if(candidate_preset.runtime_model_class_id>=24U)return;
            const int32_t radius=candidate_preset.simulation_motion_profile.collision_radius_q12;
            if(radius<=0)return;
            State5Candidate candidate;
            candidate.position=position;
            candidate.collision_radius_q12=radius;
            candidate.runtime_class_id=candidate_preset.runtime_model_class_id;
            candidate.impact_class_index=candidate_preset.shot_impact_class_index;
            candidate.damage_metrics=hierarchy_damage_metrics(candidate_hierarchy);
            candidate.placement_index=placement_index;
            candidate.transient=transient;
            thandor::SimulationClass21CandidateScoreInput input;
            input.source_world_x_q12=sx;input.source_world_y_q12=sy;
            input.source_impact_class_index=source_preset.shot_impact_class_index;
            input.source_target_class_damage_q12=source_metrics;
            input.candidate_world_x_q12=thandor::float_to_q12(position.x);
            input.candidate_world_y_q12=thandor::float_to_q12(position.y);
            input.candidate_collision_radius_q12=radius;
            input.candidate_runtime_class_id=candidate.runtime_class_id;
            input.candidate_impact_class_index=candidate.impact_class_index;
            input.candidate_target_class_damage_q12=candidate.damage_metrics;
            const uint32_t raw_score=thandor::simulation_class21_candidate_score_full_integrity(input,best_score);
            uint32_t score=raw_score;
            if(raw_score>best_score){
                const bool clear=simulation_line_of_fire_clear(source.producer_placement_index,
                    candidate.placement_index,source.actor.position,position,source_shot,false,tick,
                    SimulationTransientRef{SimulationTransientKind::LinkedClass21,source_serial},
                    candidate.transient,false);
                score=thandor::simulation_class21_apply_candidate_line_of_fire_result(raw_score,
                    best_score,clear,source_runtime_field18_nonzero);
            }
            if(score>best_score){best_score=score;best=std::move(candidate);}
        };
        for(std::size_t index=0;index<placements.size();++index){
            const auto& placement=placements[index];
            if(placement.faction_index==0U||placement.faction_index==source.faction_index)continue;
            const auto* preset=catalog_.army_preset(placement.army_asset_id);
            if(!preset||(preset->browser_kind!=thandor::PlacementBrowserKind::Unit&&
                         preset->browser_kind!=thandor::PlacementBrowserKind::Building))continue;
            if(const auto destruction=building_destruction_states_.find(index);
               destruction!=building_destruction_states_.end()){
                const auto* effect=destruction->second.effect_id==0U?nullptr:
                    catalog_.effect_definition(destruction->second.effect_id);
                const uint32_t lifetime=effect?thandor::effect_lifetime_ticks(*effect):0U;
                const uint32_t age=tick>=destruction->second.start_tick?tick-destruction->second.start_tick:0U;
                if(lifetime==0U||age>=lifetime)continue;
            }
            Vec3f position{placement.render_x(),placement.render_y(),
                terrain_height_near(placement.render_x(),placement.render_y())};
            if(const auto actor=simulation_actor_states_.find(index);actor!=simulation_actor_states_.end())
                position=actor->second.position;
            consider(placement.faction_index,*preset,position,placement_hierarchy(*preset,index),index,std::nullopt);
        }
        for(const auto& [id,entity]:simulation_produced_entities_){
            if(entity.maximum_integrity>0&&entity.integrity<=0)continue;
            const auto* preset=catalog_.army_preset(entity.army_asset_id);if(!preset)continue;
            auto hierarchy=resolved_asset_hierarchy(*preset,entity.faction_index);
            consider(entity.faction_index,*preset,entity.actor.position,hierarchy,std::nullopt,
                SimulationTransientRef{SimulationTransientKind::ProducedClass13,id});
        }
        for(const auto& [id,entity]:simulation_linked_child_entities_){
            if(id==source_serial||!entity.visible||(entity.maximum_integrity>0&&entity.integrity<=0))continue;
            const auto* preset=catalog_.army_preset(entity.army_asset_id);if(!preset)continue;
            auto hierarchy=resolved_asset_hierarchy(*preset,entity.faction_index);
            consider(entity.faction_index,*preset,entity.actor.position,hierarchy,std::nullopt,
                SimulationTransientRef{SimulationTransientKind::LinkedClass21,id});
        }
        return best;
    };
    struct State5EmitterPose {Vec3f child0_origin{};Vec3f launch{};};
    const auto state5_emitter_pose=[&](SimulationLinkedChildEntity& child,
                                       const thandor::ArmyAssetPreset& preset,uint8_t ordinal)
        ->std::optional<State5EmitterPose>{
        auto hierarchy=resolved_asset_hierarchy(preset,child.faction_index);
        if(hierarchy.empty())return std::nullopt;
        std::size_t root_index=std::numeric_limits<std::size_t>::max();
        for(std::size_t i=0;i<hierarchy.size();++i)
            if(hierarchy[i].parent_index==std::numeric_limits<uint32_t>::max()){root_index=i;break;}
        if(root_index==std::numeric_limits<std::size_t>::max()||hierarchy[root_index].sprite_path.empty())
            return std::nullopt;
        const auto& root_sprite=cached_sprite(hierarchy[root_index].sprite_path);
        const auto contact=placement_contact_pose(preset,child.actor.position.x,child.actor.position.y,
            root_sprite.placement_height_offset());
        ModelHierarchyTransform root_transform=contact.align_to_normal
            ?terrain_contact_root_transform({child.actor.position.x,child.actor.position.y,contact.height},
                thandor::turn16_to_radians(child.actor.body_yaw_turn16),contact.normal,contact.packed_normal_angles)
            :placement_root_transform(child.actor.position.x,child.actor.position.y,contact.height,
                thandor::turn16_to_radians(child.actor.body_yaw_turn16));
        std::size_t child0_index=std::numeric_limits<std::size_t>::max();
        for(std::size_t i=0;i<hierarchy.size();++i)
            if(hierarchy[i].parent_index==root_index&&hierarchy[i].child_slot==0U){child0_index=i;break;}
        if(child0_index==std::numeric_limits<std::size_t>::max()||hierarchy[child0_index].sprite_path.empty())
            return std::nullopt;
        const auto* attachment=root_sprite.child_attachment(0U);if(!attachment)return std::nullopt;
        const auto& node=hierarchy[child0_index];
        auto local=fixed_model_rotation_basis(node.local_rotation_angle2,
            child.child0_pitch_turn16,0x8000U);
        local.translation=attachment->local_translation;
        local.translation.z=thandor::q12_to_float(child.child0_local_z_q12);
        const auto child0_transform=compose_model_transform(root_transform,local);
        const auto& sprite=cached_sprite(node.sprite_path);
        const auto* point=sprite.find_lookup_record(thandor::ModelLookupKeyIndex{ordinal},
            thandor::ModelLookupKeyClass{2U});
        if(!point)return std::nullopt;
        return State5EmitterPose{child0_transform.translation,
            transform_point(child0_transform,point->local_translation)};
    };
    std::optional<std::vector<uint64_t>> mapped_sound_occupancy;
    const uint32_t active_faction=std::min(selected_faction_,7U);
    for(auto& [serial,child]:simulation_linked_child_entities_){
        if(child.producer_placement_index>=placements.size()) { destroy.push_back(serial); continue; }
        auto parent_it=simulation_airport_states_.find(child.producer_placement_index);
        const auto* child_preset=catalog_.army_preset(child.army_asset_id);
        const auto* parent_preset=catalog_.army_preset(placements[child.producer_placement_index].army_asset_id);
        if(parent_it==simulation_airport_states_.end()||child_preset==nullptr||parent_preset==nullptr){
            destroy.push_back(serial);continue;
        }
        auto& parent=parent_it->second;
        const auto hierarchy=child_preset->resolved_model_hierarchy.empty()
            ?std::span<const thandor::ModelVisualNode>(child_preset->model_hierarchy)
            :std::span<const thandor::ModelVisualNode>(child_preset->resolved_model_hierarchy);
        const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const thandor::ModelVisualNode& node){
            return node.parent_index==std::numeric_limits<uint32_t>::max();
        });
        if(root==hierarchy.end()){destroy.push_back(serial);continue;}
        const int32_t speed_q12=root->simulation.class_step_q12;
        const int32_t launch_arc_q12=root->simulation.pitch_max_velocity_turn16; // MDL +0x14 class-21 coefficient

        const auto callback_phase=child.runtime.phase;
        if(callback_phase==thandor::SimulationClass21LinkedChildPhase::Attached){
            child.child0_local_z_q12=parent.platform_local_z_q12;
            child.platform_reference_z_q12=parent.platform_local_z_q12;
            if(parent.dispatch_phase==thandor::SimulationClass22DispatchPhase::WaitingForChildRelease){
                child.platform_reference_z_q12=parent.platform_local_z_q12;
                child.movement_accumulator_q12=0;
                (void)thandor::simulation_class21_begin_outbound_when_platform_raised(child.runtime,
                    parent.dispatch_phase,child_preset->runtime_class_specific_c0_raw,
                    static_cast<int32_t>(child_preset->runtime_class_specific_c4_raw));
            }else if(parent.dispatch_phase==thandor::SimulationClass22DispatchPhase::Idle||
                     parent.dispatch_phase==thandor::SimulationClass22DispatchPhase::Reset){
                thandor::simulation_class22_release_linked_child_slot(parent,child.storage_slot);
                destroy.push_back(serial);
                continue;
            }
        }

        if(callback_phase==thandor::SimulationClass21LinkedChildPhase::OutboundRelease){
            update_arc_pose(child,launch_arc_q12,child.platform_reference_z_q12,false);
            advance(child,speed_q12);
            child.movement_accumulator_q12=clamp_i32(static_cast<int64_t>(child.movement_accumulator_q12)+speed_q12);
            if(thandor::simulation_class21_tick_outbound_release(child.runtime)){
                if(thandor::simulation_class22_signal_child_release(parent)){
                    Vec3f sound{placements[child.producer_placement_index].render_x(),
                        placements[child.producer_placement_index].render_y(),
                        terrain_height_near(placements[child.producer_placement_index].render_x(),
                                            placements[child.producer_placement_index].render_y())};
                    play_runtime_spatial_sound(parent_preset->creation_sound_asset_id_270,sound,
                        parent_preset->positioned_sound_gain_q15,
                        parent_preset->positioned_sound_maximum_distance_q12);
                }
            }
            if(child.runtime.phase==thandor::SimulationClass21LinkedChildPhase::OffmapOutbound)
                child.visible=false;
        }

        if(callback_phase==thandor::SimulationClass21LinkedChildPhase::OffmapOutbound){
            const float target_x=thandor::q12_to_float(child.mission_world_x_q12);
            const float target_y=thandor::q12_to_float(child.mission_world_y_q12);
            { // Native approach's world-point stub always rejects; no extra in-bounds wait.
                const int32_t half_ticks=std::max(0,child_preset->runtime_class_specific_c8_raw);
                const int32_t pass_distance_q12=scale_i32(speed_q12,half_ticks);
                const auto entry_delta=thandor::retail_direction_scaled(0,
                    child.mission_heading_turn16^0x8000U,pass_distance_q12);
                child.actor.position.x=thandor::q12_to_float(clamp_i32(
                    int64_t(thandor::float_to_q12(target_x))+entry_delta[0]));
                child.actor.position.y=thandor::q12_to_float(clamp_i32(
                    int64_t(thandor::float_to_q12(target_y))+entry_delta[1]));
                child.actor.position.z=contact_height(child.actor.position.x,child.actor.position.y);
                child.actor.body_yaw_turn16=child.mission_heading_turn16;
                child.movement_accumulator_q12=-pass_distance_q12;
                int32_t maximum_height_q12=0;
                const auto scan_delta=thandor::retail_direction_scaled(0,
                    child.mission_heading_turn16,scale_i32(speed_q12,10));
                int32_t scan_x=thandor::float_to_q12(child.actor.position.x);
                int32_t scan_y=thandor::float_to_q12(child.actor.position.y);
                const int64_t total_scan_ticks=static_cast<int64_t>(half_ticks)*2LL;
                for(int64_t remaining=total_scan_ticks;remaining>=0;remaining-=10){
                    const auto surface=document_->field().sample_retail_surface_heights_q12(scan_x,scan_y);
                    if(surface.valid)maximum_height_q12=std::max(maximum_height_q12,surface.top);
                    scan_x=clamp_i32(int64_t(scan_x)+scan_delta[0]);
                    scan_y=clamp_i32(int64_t(scan_y)+scan_delta[1]);
                }
                child.mission_arc_reference_z_q12=clamp_i32(static_cast<int64_t>(maximum_height_q12)+
                    0x800LL+static_cast<int64_t>(child.platform_reference_z_q12)*2LL);
                (void)thandor::simulation_class21_begin_mission_pass(child.runtime,
                    clamp_i32(static_cast<int64_t>(half_ticks)*2LL));
                child.visible=true;
            }
        }

        if(callback_phase==thandor::SimulationClass21LinkedChildPhase::MissionPass){
            advance(child,speed_q12);
            update_arc_pose(child,child_preset->runtime_class_specific_cc_raw,
                child.mission_arc_reference_z_q12,true);
            child.movement_accumulator_q12=clamp_i32(static_cast<int64_t>(child.movement_accumulator_q12)+speed_q12);
            const auto mission_events=thandor::simulation_class21_tick_mission_pass_events(
                child.runtime,static_cast<int32_t>(root->simulation.reload_ticks));

            // ArmyRuntime_TryPlayMappedTerrainSoundAtWorldPoint is called once for every state-5
            // substep, even on the substep that transitions the child to state 6. It is global
            // stereo, not a positioned sound. LEV has no live diplomacy surface; normal Simulation
            // preserves the existing self-only visibility policy and treats different nonzero
            // editor factions as hostile rather than inventing alliances.
            for(uint32_t attempt=0;attempt<mission_events.mapped_terrain_sound_attempts;++attempt){
                (void)attempt;
                // This preview services one substep. A negative run counter moves
                // off-map before the native audio callback; missing slots return
                // before consuming the relation cooldown.
                if(mission_events.completed||!resolve_runtime_spatial_sound_path(
                       child_preset->runtime_class_effect_sound_asset_id_26c))continue;
                if(active_faction==0U||active_faction>=simulation_relation_transition_ticks_.size())continue;
                if(!mapped_sound_occupancy)
                    mapped_sound_occupancy=simulation_visibility_occupancy_for_faction(active_faction);
                const auto cell=thandor::retail_nearest_world_to_cell(document_->field(),
                    thandor::float_to_q12(child.actor.position.x),
                    thandor::float_to_q12(child.actor.position.y));
                uint8_t occupancy_byte=0U;
                if(cell&&mapped_sound_occupancy&&
                   cell->x<document_->field().width()&&cell->y<document_->field().height()){
                    const std::size_t cell_index=static_cast<std::size_t>(cell->y)*document_->field().width()+cell->x;
                    if(cell_index<mapped_sound_occupancy->size())
                        occupancy_byte=thandor::fog_occupancy_byte((*mapped_sound_occupancy)[cell_index],active_faction);
                }
                const bool hostile=child.faction_index!=0U&&active_faction!=0U&&
                    child.faction_index!=active_faction;
                if(thandor::simulation_class21_mapped_terrain_sound_gate(child.faction_index,
                       active_faction,hostile,occupancy_byte,
                       simulation_relation_transition_ticks_[active_faction])){
                    play_runtime_global_sound(child_preset->runtime_class_effect_sound_asset_id_26c);
                    simulation_relation_transition_ticks_[active_faction]=0U;
                }
            }

            if(mission_events.emission_count!=0U&&root->simulation.shot_definition_id!=0U){
                const auto* shot=catalog_.shot_definition(root->simulation.shot_definition_id);
                if(shot){
                    auto source_hierarchy=resolved_asset_hierarchy(*child_preset,child.faction_index);
                    const auto source_metrics=hierarchy_damage_metrics(source_hierarchy);
                    for(uint32_t emission=0;emission<mission_events.emission_count;++emission){
                        const uint8_t ordinal=mission_events.model_point_ordinals[emission];
                        const auto emitter=state5_emitter_pose(child,*child_preset,ordinal);
                        if(!emitter)continue;
                        Vec3f target=emitter->child0_origin;
                        target.z-=thandor::q12_to_float(0x2000);
                        std::optional<uint32_t> target_impact_class;
                        const auto candidate=choose_state5_candidate(serial,child,*child_preset,source_metrics,
                            *shot,root->simulation.runtime_field_18_raw!=0);
                        if(candidate&&thandor::simulation_class21_candidate_within_projectile_lock(
                               thandor::float_to_q12(emitter->child0_origin.x),
                               thandor::float_to_q12(emitter->child0_origin.y),
                               thandor::float_to_q12(candidate->position.x),
                               thandor::float_to_q12(candidate->position.y),
                               candidate->collision_radius_q12)){
                            target=candidate->position;
                            target_impact_class=candidate->impact_class_index;
                        }
                        thandor::SimulationWeaponProfile synthetic_weapon;
                        synthetic_weapon.shot_definition_id=root->simulation.shot_definition_id;
                        const std::array<Vec3f,1> launch{{emitter->launch}};
                        auto volley=thandor::simulation_create_volley(child.producer_placement_index,0U,
                            synthetic_weapon,launch,target,*shot,tick,next_simulation_projectile_serial_,
                            0xFFFFFFFFU,std::nullopt,
                            thandor::SimulationProjectileOwnerKind::LinkedClass21,serial);
                        for(auto& projectile:volley){
                            if(target_impact_class&&*target_impact_class<8U){
                                auto surface_candidate=projectile;
                                thandor::simulation_resolve_projectile_surface_impact(
                                    surface_candidate,*shot,document_->field());
                                const auto squared_distance=[](Vec3f a,Vec3f b){
                                    const Vec3f d=sub(a,b);return d.x*d.x+d.y*d.y+d.z*d.z;
                                };
                                const bool surface_is_nearer=
                                    surface_candidate.impact_kind!=thandor::SimulationImpactKind::None&&
                                    squared_distance(projectile.launch_position,
                                        surface_candidate.impact_position)+1.0e-6F<
                                    squared_distance(projectile.launch_position,target);
                                if(surface_is_nearer)projectile=surface_candidate;
                                else{
                                    projectile.impact_kind=thandor::SimulationImpactKind::TargetModelImpactClass;
                                    projectile.impact_position=target;
                                    projectile.target_position=target;
                                    projectile.impact_effect_id=shot->target_impact_effect_id(*target_impact_class);
                                }
                            }else{
                                thandor::simulation_resolve_projectile_surface_impact(
                                    projectile,*shot,document_->field());
                            }
                            if(const auto* impact=catalog_.effect_definition(projectile.impact_effect_id))
                                projectile.impact_visual_lifetime_ticks=
                                    std::max(1U,thandor::effect_lifetime_ticks(*impact));
                            else projectile.impact_visual_lifetime_ticks=1U;
                            if(shot->launch_effect_id!=0U){
                                if(const auto* launch_effect=catalog_.effect_definition(shot->launch_effect_id)){
                                    Vec3f launch_effect_position=projectile.launch_position;
                                    const auto initial_orientation=shot_model_transform(
                                        projectile.launch_position,projectile.launch_yaw_turn16,
                                        projectile.launch_pitch_turn16);
                                    const auto& shot_sprite=cached_sprite(shot->sprite_path);
                                    if(const auto* launch_attachment=shot_sprite.find_lookup_record(
                                           thandor::ModelLookupKeyIndex{0U},
                                           thandor::ModelLookupKeyClass{3U}))
                                        launch_effect_position=transform_point(
                                            initial_orientation,launch_attachment->local_translation);
                                    play_effect_positioned_sound(*launch_effect,launch_effect_position);
                                }
                            }
                        }
                        if(!volley.empty()){
                            simulation_projectiles_.insert(simulation_projectiles_.end(),volley.begin(),volley.end());
                            ++simulation_volley_count_;
                        }
                    }
                }
            }
            if(mission_events.completed)child.visible=false;
        }

        if(callback_phase==thandor::SimulationClass21LinkedChildPhase::OffmapReturn&&
           parent.dispatch_phase==thandor::SimulationClass22DispatchPhase::Idle){
            const auto& parent_placement=placements[child.producer_placement_index];
            const uint32_t yaw=parent_placement.orientation_turn16&0xFFFFU;
            const int32_t return_ticks=static_cast<int32_t>(child_preset->runtime_class_specific_c4_raw);
            const int32_t return_distance_q12=scale_i32(speed_q12,return_ticks);
            const auto entry_delta=thandor::retail_direction_scaled(0,yaw^0x8000U,return_distance_q12);
            child.actor.position.x=thandor::q12_to_float(clamp_i32(
                int64_t(thandor::float_to_q12(parent_placement.render_x()))+entry_delta[0]));
            child.actor.position.y=thandor::q12_to_float(clamp_i32(
                int64_t(thandor::float_to_q12(parent_placement.render_y()))+entry_delta[1]));
            child.actor.position.z=contact_height(child.actor.position.x,child.actor.position.y);
            child.actor.body_yaw_turn16=yaw;
            child.movement_accumulator_q12=-return_distance_q12;
            child.platform_reference_z_q12=parent.platform_local_z_q12;
            if(thandor::simulation_class21_begin_return_when_parent_idle(child.runtime,parent,return_ticks))
                child.visible=true;
        }

        if(callback_phase==thandor::SimulationClass21LinkedChildPhase::ReturningToPlatform){
            update_arc_pose(child,launch_arc_q12,child.platform_reference_z_q12,false);
            advance(child,speed_q12);
            child.movement_accumulator_q12=clamp_i32(static_cast<int64_t>(child.movement_accumulator_q12)+speed_q12);
            if(thandor::simulation_class21_tick_return_to_platform(child.runtime,parent)){
                child.landed=true;
                Vec3f sound{placements[child.producer_placement_index].render_x(),
                    placements[child.producer_placement_index].render_y(),
                    terrain_height_near(placements[child.producer_placement_index].render_x(),
                                        placements[child.producer_placement_index].render_y())};
                play_runtime_spatial_sound(parent_preset->creation_sound_asset_id_270,sound,
                    parent_preset->positioned_sound_gain_q15,
                    parent_preset->positioned_sound_maximum_distance_q12);
            }
        }
        child.actor.last_updated_tick=tick;
    }
    for(uint64_t serial:destroy)simulation_linked_child_entities_.erase(serial);
    if(!destroy.empty()||!simulation_linked_child_entities_.empty())
        last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
}

void EditorApp::update_produced_simulation_entities(uint32_t tick){
    if(!document_)return;
    const auto placements=document_->level().placements();
    for(auto& [serial,entity]:simulation_produced_entities_){
        (void)serial;
        const auto* preset=catalog_.army_preset(entity.army_asset_id);
        if(entity.maximum_integrity>0&&entity.integrity<=0)continue;
        if(preset==nullptr)continue;
        if(const auto damage=preview_depth_damage(document_->field(),*preset,entity.actor.position);
           damage&&entity.maximum_integrity>0){
            entity.integrity=static_cast<int32_t>(std::max<int64_t>(0,int64_t(entity.integrity)-*damage));
            last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
            if(entity.integrity<=0)continue;
        }
        const Vec3f previous=entity.actor.position;
        const uint32_t previous_yaw=entity.actor.body_yaw_turn16;
        const uint64_t previous_contact_serial=entity.actor.articulated_contact_event_serial;
        const bool moved=simulation_preview_motion(entity.actor,*preset,
            std::numeric_limits<std::size_t>::max(),serial);
        const bool translated=entity.actor.position.x!=previous.x||entity.actor.position.y!=previous.y;
        const bool turned=entity.actor.body_yaw_turn16!=previous_yaw;
        if(preset->runtime_model_class_id==2U&&(translated||turned)){
            float local_bounds_y0=0.0F;
            const auto hierarchy=preset->resolved_model_hierarchy.empty()
                ?std::span<const thandor::ModelVisualNode>(preset->model_hierarchy)
                :std::span<const thandor::ModelVisualNode>(preset->resolved_model_hierarchy);
            const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){
                return node.parent_index==std::numeric_limits<uint32_t>::max();
            });
            if(root!=hierarchy.end()&&!root->sprite_path.empty())
                local_bounds_y0=cached_sprite(root->sprite_path).local_bounds_min().y;
            thandor::simulation_update_tank_track_offsets(entity.actor,previous,previous_yaw,
                entity.actor.position,entity.actor.body_yaw_turn16,local_bounds_y0,
                preset->track_texture_travel_scale);
        }
        if(preset->runtime_model_class_id==3U)
            thandor::simulation_update_articulated_contact_transition(entity.actor,
                previous,previous_yaw,entity.actor.position,entity.actor.body_yaw_turn16,
                preset->articulated_contact_interpolation_rate_q12,
                std::abs(preset->simulation_motion_profile.speed_q12_per_tick));
        if(preset->runtime_model_class_id==17U)
            thandor::simulation_tick_class17_pose(entity.actor,moved);
        entity.actor.position.z=terrain_height_near(entity.actor.position.x,entity.actor.position.y);
        entity.actor.last_updated_tick=tick;

        const bool contact_completed=preset->runtime_model_class_id==3U&&
            entity.actor.articulated_contact_event_serial>previous_contact_serial;
        const bool root_changed=translated||turned;
        const bool movement_effect_due=(preset->runtime_model_class_id==2U||
            preset->runtime_model_class_id==3U)&&root_changed&&
            thandor::simulation_tick_movement_ground_effect_countdown(entity.actor,true,
                [&](){
                    const auto& hierarchy=preset->resolved_model_hierarchy.empty()
                        ?preset->model_hierarchy:preset->resolved_model_hierarchy;
                    const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){
                        return node.parent_index==std::numeric_limits<uint32_t>::max();
                    });
                    return root==hierarchy.end()?0U:root->simulation.timed_effect_interval_ticks;
                }());

        if(contact_completed||movement_effect_due){
            const auto hierarchy=preset->resolved_model_hierarchy.empty()
                ?std::span<const thandor::ModelVisualNode>(preset->model_hierarchy)
                :std::span<const thandor::ModelVisualNode>(preset->resolved_model_hierarchy);
            std::vector<ModelHierarchyInstance> instances;
            std::vector<bool> active;
            instances.reserve(hierarchy.size());active.reserve(hierarchy.size());
            for(std::size_t node_index=0;node_index<hierarchy.size();++node_index){
                const auto& node=hierarchy[node_index];
                ModelHierarchyInstance instance{};instance.node=&node;
                instance.root=node.parent_index==std::numeric_limits<uint32_t>::max();
                bool node_active=node.creates_runtime_node()&&!node.sprite_path.empty();
                const thandor::SpriteAsset* node_sprite=node_active?&cached_sprite(node.sprite_path):nullptr;
                if(instance.root){
                    const auto contact=placement_contact_pose(*preset,entity.actor.position.x,
                        entity.actor.position.y,node_sprite?node_sprite->placement_height_offset():0.0F);
                    const Vec3f root_position{entity.actor.position.x,entity.actor.position.y,contact.height};
                    instance.transform=contact.align_to_normal
                        ?terrain_contact_root_transform(root_position,
                            thandor::turn16_to_radians(entity.actor.body_yaw_turn16),contact.normal,contact.packed_normal_angles)
                        :placement_root_transform(root_position.x,root_position.y,root_position.z,
                            thandor::turn16_to_radians(entity.actor.body_yaw_turn16));
                }else{
                    if(node.parent_index>=instances.size()||!active[node.parent_index])node_active=false;
                    if(node_active&&!thandor::model_runtime_world_child_enabled(
                           hierarchy[node.parent_index].runtime_class_id,node.child_slot))
                        node_active=false;
                    if(node_active){
                        const auto& parent=instances[node.parent_index];
                        const auto* attachment=parent.sprite->child_attachment(node.child_slot);
                        if(attachment==nullptr)node_active=false;
                        else{
                            auto local=fixed_model_rotation_basis(node.local_rotation_angle2,
                                node.local_rotation_angle1,node.local_rotation_angle0);
                            local.translation=attachment->local_translation;
                            instance.transform=compose_model_transform(parent.transform,local);
                        }
                    }
                }
                if(node_active)instance.sprite=node_sprite;
                active.push_back(node_active);instances.push_back(instance);
            }
            apply_walker_pose_to_instances(*preset,hierarchy,instances,&entity.actor,
                [&](float x,float y){return terrain_height_near(x,y);});
            const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){
                return node.parent_index==std::numeric_limits<uint32_t>::max();
            });
            if(root!=hierarchy.end()){
                const uint32_t root_index=static_cast<uint32_t>(std::distance(hierarchy.begin(),root));
                if(contact_completed&&root_index<instances.size()){
                    if(preset->articulated_contact_sound_asset_id!=0U)
                        play_runtime_spatial_sound(preset->articulated_contact_sound_asset_id,
                            instances[root_index].transform.translation,
                            preset->positioned_sound_gain_q15,
                            preset->positioned_sound_maximum_distance_q12);
                    const auto chain=articulated_walker_detail::leg_chain(hierarchy,root_index,
                        entity.actor.articulated_last_completed_side&1U);
                    const uint32_t foot_index=chain[3];
                    if(foot_index!=std::numeric_limits<uint32_t>::max()&&
                       foot_index<instances.size()&&active[foot_index]){
                        const Vec3f origin=instances[foot_index].transform.translation;
                        const int32_t water_delta=thandor::retail_nearest_water_delta_q12(
                            document_->field(),thandor::float_to_q12(origin.x),
                            thandor::float_to_q12(origin.y));
                        const uint32_t effect_id=water_delta>0
                            ?root->simulation.timed_water_effect_id:root->simulation.timed_effect_id;
                        if(effect_id!=0U&&catalog_.effect_definition(effect_id)!=nullptr){
                            auto orientation=instances[foot_index].transform;
                            orientation.translation=origin;
                            movement_ground_effects_.push_back(MovementGroundEffectInstance{
                                std::numeric_limits<std::size_t>::max(),effect_id,tick,origin,
                                orientation,true});
                        }
                    }
                }
                if(movement_effect_due&&root_index<instances.size()&&active[root_index]){
                    Vec3f origin=instances[root_index].transform.translation;
                    if(instances[root_index].sprite!=nullptr){
                        const auto points=instances[root_index].sprite->type6_attachment_points();
                        if(!points.empty())origin=transform_point(instances[root_index].transform,points.front());
                    }
                    const int32_t water_delta=thandor::retail_nearest_water_delta_q12(
                        document_->field(),thandor::float_to_q12(origin.x),
                        thandor::float_to_q12(origin.y));
                    const uint32_t effect_id=water_delta>0
                        ?root->simulation.timed_water_effect_id:root->simulation.timed_effect_id;
                    if(effect_id!=0U&&catalog_.effect_definition(effect_id)!=nullptr){
                        auto orientation=instances[root_index].transform;
                        orientation.translation=origin;
                        movement_ground_effects_.push_back(MovementGroundEffectInstance{
                            std::numeric_limits<std::size_t>::max(),effect_id,tick,origin,
                            orientation,false});
                    }
                }
            }
        }

        if(entity.linked_to_producer&&entity.producer_placement_index<placements.size()){
            Vec3f producer_position{placements[entity.producer_placement_index].render_x(),
                placements[entity.producer_placement_index].render_y(),0.0F};
            if(const auto live=simulation_actor_states_.find(entity.producer_placement_index);
               live!=simulation_actor_states_.end())producer_position=live->second.position;
            const auto* producer_preset=catalog_.army_preset(
                placements[entity.producer_placement_index].army_asset_id);
            const float clear_radius=producer_preset?thandor::q12_to_float(std::max(0,
                producer_preset->simulation_motion_profile.collision_radius_q12)):0.0F;
            const float unit_radius=thandor::q12_to_float(std::max(0,
                preset->simulation_motion_profile.collision_radius_q12));
            const float distance=std::hypot(entity.actor.position.x-producer_position.x,
                                            entity.actor.position.y-producer_position.y);
            // Retail clears the link from collision/overlap ownership. The editor has no dynamic
            // EntityRuntime list for transient units, so use the same summed-radius geometry as its
            // recovered Army collision helper and never clear before a drive-out command exists.
            if(entity.exit_command_issued&&distance>std::max(0.25F,clear_radius+unit_radius)){
                entity.linked_to_producer=false;
                if(const auto rally=simulation_production_rally_points_.find(entity.producer_placement_index);
                   rally!=simulation_production_rally_points_.end())entity.actor.move_target=rally->second;
            }
        }
    }
}

void EditorApp::prepare_simulation_navigation_terrain() {
    if(!document_)return;
    const auto& field=document_->field();
    uint64_t signature=0xcbf29ce484222325ULL;
    for(const auto& cell:field.cells())for(uint32_t value:{cell.flags_and_material,
        static_cast<uint32_t>(cell.terrain_height_q12),static_cast<uint32_t>(cell.water_delta_q12)}){
        signature^=value;signature*=0x100000001b3ULL;
    }
    if(simulation_navigation_terrain_cache_.field!=&field ||
       simulation_navigation_terrain_cache_.states.size()!=field.cells().size()*16U ||
       signature!=simulation_navigation_terrain_signature_){
        simulation_navigation_terrain_cache_.prepare(field);
        simulation_navigation_terrain_signature_=signature;
        for(auto& [index,actor]:simulation_actor_states_){(void)index;actor.move_waypoint.reset();}
        for(auto& [serial,entity]:simulation_produced_entities_){(void)serial;entity.actor.move_waypoint.reset();}
    }
}

std::vector<thandor::V528PathRuntimeBlocker> EditorApp::simulation_movement_blockers(
    std::size_t moving_index,std::optional<uint64_t> produced_serial) const {
    std::vector<thandor::V528PathRuntimeBlocker> blockers;
    if(!document_)return blockers;
    const auto placements=document_->level().placements();
    const SimulationProducedEntity* mover=nullptr;
    if(produced_serial){const auto it=simulation_produced_entities_.find(*produced_serial);
        if(it!=simulation_produced_entities_.end())mover=&it->second;}
    for(std::size_t other=0;other<placements.size();++other){
        if(other==moving_index || (mover&&mover->linked_to_producer&&
            mover->producer_placement_index==other))continue;
        const auto* preset=catalog_.army_preset(placements[other].army_asset_id);
        if(!preset||preset->simulation_motion_profile.collision_radius_q12<=0)continue;
        Vec3f center=placement_command_target_point(other);
        if(const auto it=simulation_actor_states_.find(other);it!=simulation_actor_states_.end())center=it->second.position;
        blockers.push_back({thandor::float_to_q12(center.x),thandor::float_to_q12(center.y),
            preset->simulation_motion_profile.collision_radius_q12,preset->runtime_model_class_id});
    }
    for(const auto& [serial,entity]:simulation_produced_entities_){
        if(produced_serial==serial || (entity.linked_to_producer&&
           entity.producer_placement_index==moving_index))continue;
        const auto* preset=catalog_.army_preset(entity.army_asset_id);
        if(!preset||preset->simulation_motion_profile.collision_radius_q12<=0)continue;
        blockers.push_back({thandor::float_to_q12(entity.actor.position.x),
            thandor::float_to_q12(entity.actor.position.y),
            preset->simulation_motion_profile.collision_radius_q12,preset->runtime_model_class_id});
    }
    return blockers;
}

bool EditorApp::simulation_path_segment_clear(
    std::size_t moving_index,Vec3f from,Vec3f to,
    const thandor::ArmyAssetPreset& moving_preset,std::optional<uint64_t> produced_serial) const {
    if(!document_)return false;
    if(moving_preset.runtime_model_class_id==21U)return true;
    if(!moving_preset.has_traversal_profile)return true;
    const auto placements=document_->level().placements();
    uint32_t faction=0;
    if(produced_serial){const auto it=simulation_produced_entities_.find(*produced_serial);
        if(it==simulation_produced_entities_.end())return false;
        faction=it->second.faction_index;
    }else{if(moving_index>=placements.size())return false;faction=placements[moving_index].faction_index;}
    auto blockers=simulation_movement_blockers(moving_index,produced_serial);
    const auto masks=thandor::v519_path_masks(faction,
        moving_preset.traversal_low_band_index,moving_preset.traversal_high_band_index);
    if(!masks.traversal_bands_valid)return false;
    const auto start=thandor::v519_path_clip_to_inner_scratch(document_->field(),
        thandor::v519_path_world_to_scratch(thandor::float_to_q12(from.x),thandor::float_to_q12(from.y)));
    const auto end=thandor::v519_path_clip_to_inner_scratch(document_->field(),
        thandor::v519_path_world_to_scratch(thandor::float_to_q12(to.x),thandor::float_to_q12(to.y)));
    return thandor::v565_path_segment_clear_static_cached(document_->field(),start,end,
        masks.faction_presence_mask,masks.high_cost_mask,masks.blocking_mask,blockers,
        moving_preset.simulation_motion_profile.collision_radius_q12,simulation_navigation_terrain_cache_);
}

thandor::V528RouteResult EditorApp::simulation_resolve_v528_route(
    std::size_t moving_index,Vec3f from,Vec3f final_target,
    const thandor::ArmyAssetPreset& moving_preset,std::optional<uint64_t> produced_serial) const {
    if(!document_||!moving_preset.has_traversal_profile)return {};
    const auto placements=document_->level().placements();
    uint32_t faction=0;
    if(produced_serial){const auto it=simulation_produced_entities_.find(*produced_serial);
        if(it==simulation_produced_entities_.end())return {};
        faction=it->second.faction_index;
    }else{if(moving_index>=placements.size())return {};faction=placements[moving_index].faction_index;}
    const auto blockers=simulation_movement_blockers(moving_index,produced_serial);
    return thandor::v528_resolve_route(document_->field(),
        thandor::float_to_q12(from.x),thandor::float_to_q12(from.y),
        thandor::float_to_q12(final_target.x),thandor::float_to_q12(final_target.y),
        1U<<(faction&31U),moving_preset.traversal_low_band_index,
        moving_preset.traversal_high_band_index,blockers,
        moving_preset.simulation_motion_profile.collision_radius_q12);
}

bool EditorApp::simulation_preview_motion(thandor::SimulationActorState& actor,
    const thandor::ArmyAssetPreset& preset,std::size_t moving_index,
    std::optional<uint64_t> produced_serial) {
    if(!document_)return false;
    const auto& motion=preset.simulation_motion_profile;
    const auto before=actor;
    if(motion.ground_speed_control){
        actor.ground_normal_azimuth_turn16=0;actor.ground_normal_elevation_turn16=0x4000;
        if(motion.placement_contact_kind==2)
            if(const auto normal=thandor::retail_contact_normal_angles(document_->field(),
                thandor::float_to_q12(actor.position.x),thandor::float_to_q12(actor.position.y))){
                actor.ground_normal_azimuth_turn16=*normal&0xFFFFU;
                actor.ground_normal_elevation_turn16=static_cast<int16_t>(*normal>>16);
            }
    }
    if(motion.movable()&&actor.move_target&&preset.runtime_model_class_id!=21U){
        if(actor.move_waypoint&&!simulation_path_segment_clear(moving_index,actor.position,
            *actor.move_waypoint,preset,produced_serial))actor.move_waypoint.reset();
        if(!actor.move_waypoint&&!simulation_path_segment_clear(moving_index,actor.position,
            *actor.move_target,preset,produced_serial)){
            const auto route=simulation_resolve_v528_route(moving_index,actor.position,*actor.move_target,preset,produced_serial);
            if(!route.usable()){actor.ground_advance_q12=0;return false;}
            if(route.requested_cell_retargeted){
                actor.move_target->x=thandor::q12_to_float(route.resolved_world_q12.x_q12);
                actor.move_target->y=thandor::q12_to_float(route.resolved_world_q12.y_q12);
            }
            if(route.primary_world_q12)actor.move_waypoint=Vec3f{
                thandor::q12_to_float(route.primary_world_q12->x_q12),
                thandor::q12_to_float(route.primary_world_q12->y_q12),actor.position.z};
        }
    }
    const auto target_before=actor.move_target,waypoint_before=actor.move_waypoint;
    bool moved=preset.runtime_model_class_id==3U
        ?thandor::simulation_tick_articulated_actor_motion(actor,motion,preset.articulated_contact_interpolation_rate_q12)
        :preset.runtime_model_class_id==17U?thandor::simulation_tick_class17_actor_motion(actor,motion)
        :thandor::simulation_tick_actor_motion(actor,motion);
    if(preset.runtime_model_class_id!=21U &&
       (actor.position.x!=before.position.x||actor.position.y!=before.position.y)){
        const auto blockers=simulation_movement_blockers(moving_index,produced_serial);
        const bool clear=thandor::simulation_lab_path_world_segment_clear(
            {thandor::float_to_q12(before.position.x),thandor::float_to_q12(before.position.y)},
            {thandor::float_to_q12(actor.position.x),thandor::float_to_q12(actor.position.y)},
            blockers,motion.collision_radius_q12);
        if(!clear){
            actor.position=before.position;actor.ground_advance_q12=0;
            actor.move_target=target_before;actor.move_waypoint.reset();
            moved=actor.body_yaw_turn16!=before.body_yaw_turn16;
        }else if(preset.has_traversal_profile){
            const auto masks=thandor::v519_path_masks(0,preset.traversal_low_band_index,preset.traversal_high_band_index);
            const auto from=thandor::v519_path_world_to_scratch(thandor::float_to_q12(before.position.x),thandor::float_to_q12(before.position.y));
            const auto to=thandor::v519_path_world_to_scratch(thandor::float_to_q12(actor.position.x),thandor::float_to_q12(actor.position.y));
            if(!thandor::v565_path_segment_clear_static_cached(document_->field(),from,to,
                1U,0,masks.blocking_mask,{},0,simulation_navigation_terrain_cache_)){
                actor.position=before.position;actor.ground_advance_q12=0;
                actor.move_target=target_before;actor.move_waypoint=waypoint_before;moved=false;
            }
        }
    }
    return moved;
}

std::optional<std::string> EditorApp::resolve_runtime_spatial_sound_path(
    uint32_t sound_table_index) const {
    if(!document_||sound_table_index==0U)return std::nullopt;
    // InGameLevelRuntime_LoadResources* reads LEV soundBasePathOffset (LevelPaths::sounds), sets
    // its extension to .sam, enumerates matching package/filesystem entries, parses each matched
    // leaf's trailing decimal number, and stores the SpatialSoundSlot at that numeric world-table
    // index. Stock maps use "sound/sound??"; individual SAMs are not LEV resource_paths entries.
    std::string pattern=document_->level().paths().sounds;
    if(pattern.empty())return std::nullopt;
    if(pattern!=runtime_sound_path_pattern_){runtime_sound_path_pattern_=pattern;runtime_sound_path_cache_.clear();}
    if(const auto cached=runtime_sound_path_cache_.find(sound_table_index);cached!=runtime_sound_path_cache_.end()){
        ++audio_path_cache_hits_;return cached->second;
    }
    std::replace(pattern.begin(),pattern.end(),'\\','/');
    std::transform(pattern.begin(),pattern.end(),pattern.begin(),[](unsigned char c){
        return static_cast<char>(std::tolower(c));
    });
    const auto slash=pattern.find_last_of('/');
    const auto dot=pattern.find_last_of('.');
    if(dot==std::string::npos||(slash!=std::string::npos&&dot<slash))pattern += ".sam";
    else pattern.replace(dot,std::string::npos,".sam");
    const auto wildcard_match=[](std::string_view wildcard,std::string_view candidate){
        if(wildcard.size()!=candidate.size())return false;
        for(std::size_t i=0;i<wildcard.size();++i){
            const unsigned char a=static_cast<unsigned char>(wildcard[i]);
            unsigned char b=static_cast<unsigned char>(candidate[i]);
            if(b=='\\')b='/';
            b=static_cast<unsigned char>(std::tolower(b));
            if(a!='?'&&a!=b)return false;
        }
        return true;
    };
    std::optional<std::string> resolved;
    for(const auto& record:catalog_.assets()){
        if(record.kind!=thandor::AssetKind::Sound||
           !wildcard_match(pattern,record.normalized_path))continue;
        const auto index=thandor::sam_trailing_decimal_index(record.normalized_path);
        if(!index||*index!=sound_table_index)continue;
        if(resolved&&*resolved!=record.normalized_path){runtime_sound_path_cache_[sound_table_index]=std::nullopt;return std::nullopt;}
        resolved=record.normalized_path;
    }
    runtime_sound_path_cache_[sound_table_index]=resolved;return resolved;
}

thandor::SpatialAudioListener EditorApp::spatial_audio_listener() const noexcept{
    const Vec3f eye=camera_.eye();
    const Vec3f forward=normalize(sub(camera_.target,eye));
    Vec3f right=normalize(cross(forward,{0.0F,0.0F,1.0F}));
    if(vec3_length(right)<1.0e-6F)right={1.0F,0.0F,0.0F};
    const auto listener=thandor::spatial_sound_listener_position(
        {camera_.target.x,camera_.target.y,camera_.target.z},
        simulation_lab_active()&&simulation_sound_at_map_focus_ ? 0 : thandor::float_to_q12(camera_.distance));
    return {listener,{right.x,right.y,right.z},{forward.x,forward.y,forward.z}};
}

void EditorApp::play_runtime_spatial_sound(uint32_t sound_table_index,Vec3f world_position,
                                            uint32_t gain_q15,
                                            uint32_t maximum_distance_q12){
    ++audio_one_shot_requests_;
    if(!sound_playback_enabled_||sound_effects_gain_q15_==0U||sound_table_index==0U)return;
    const auto gains=thandor::spatial_sound_gains_q15(spatial_audio_listener(),
        {world_position.x,world_position.y,world_position.z},gain_q15,
        maximum_distance_q12,sound_effects_gain_q15_,false);
    if(!gains.audible()){++audio_inaudible_;return;}
    const auto path=resolve_runtime_spatial_sound_path(sound_table_index);
    if(!path){
        ++audio_missing_samples_;
        sound_playback_status_="Sound table index "+std::to_string(sound_table_index)+
                               " has no unique LEV sound-template-matched SAM";
        return;
    }
    try{
        auto found=sam_pcm_cache_.find(*path);
        if(found==sam_pcm_cache_.end()){
            auto decoded=std::make_shared<thandor::SamPcmData>(
                thandor::decode_sam_to_pcm(catalog_.load(*path)));
            found=sam_pcm_cache_.emplace(*path,std::move(decoded)).first;
        }
        if(thandor::play_pcm_one_shot(*found->second,gains.left_q15,gains.right_q15)){
            ++audio_submitted_;sound_playback_status_="Queued positioned "+*path;
        }else{++audio_backend_rejected_;sound_playback_status_="Audio backend unavailable for "+*path;}
    }catch(const std::exception& error){
        sound_playback_status_="Sound decode/play failed: "+std::string(error.what());
    }
}

void EditorApp::play_runtime_global_sound(uint32_t sound_table_index){
    if(!sound_playback_enabled_||sound_effects_gain_q15_==0U||sound_table_index==0U)return;
    const auto path=resolve_runtime_spatial_sound_path(sound_table_index);
    if(!path){
        sound_playback_status_="Sound table index "+std::to_string(sound_table_index)+
                               " has no unique LEV sound-template-matched SAM";
        return;
    }
    try{
        auto found=sam_pcm_cache_.find(*path);
        if(found==sam_pcm_cache_.end()){
            auto decoded=std::make_shared<thandor::SamPcmData>(
                thandor::decode_sam_to_pcm(catalog_.load(*path)));
            found=sam_pcm_cache_.emplace(*path,std::move(decoded)).first;
        }
        // ArmyRuntime_TryPlayMappedTerrainSoundAtWorldPoint dispatches g_SoundPlayOneShot with
        // the global SFX gain in both channels. It is deliberately not a positioned/spatial slot.
        if(thandor::play_pcm_one_shot(*found->second,sound_effects_gain_q15_,sound_effects_gain_q15_))
            sound_playback_status_="Playing mapped terrain "+*path;
        else sound_playback_status_="Audio backend unavailable for "+*path;
    }catch(const std::exception& error){
        sound_playback_status_="Sound decode/play failed: "+std::string(error.what());
    }
}

void EditorApp::play_effect_positioned_sound(const thandor::EffectDefinitionRecord& effect,
                                              Vec3f world_position){
    // SHT/EFF spatial submission in the executable is gated by the active faction's projected
    // field-cell visibility bits before the world sound slot is refreshed.  Fog preview Off is
    // the editor's fully-visible world; selected-faction preview uses that faction's exact current
    // occupancy.  The all-active-factions preview intentionally uses its existing union view.
    if(fog_preview_mode_!=0&&!fog_preview_point_visible(world_position))return;
    play_runtime_spatial_sound(effect.positioned_sound_table_index(),world_position,
        effect.positioned_sound_gain_q15(),effect.positioned_sound_maximum_distance_q12());
}

void EditorApp::stop_all_runtime_looping_sounds(){
    for(auto& [index,active]:active_looping_sounds_){
        (void)index;
        if(active.voice)active.voice->stop();
    }
    active_looping_sounds_.clear();
}

void EditorApp::reconcile_runtime_looping_sounds(
    const std::unordered_map<uint32_t,RuntimeSpatialSoundRequest>& desired_requests){
    if(!sound_playback_enabled_||sound_effects_gain_q15_==0U||!document_){
        stop_all_runtime_looping_sounds();
        return;
    }
    // A host/device failure is temporary. Only missing/invalid samples belong
    // in looping_sound_start_failures_; a failed open must recover automatically.
    static std::chrono::steady_clock::time_point backend_retry_after{};
    const auto backend_now=std::chrono::steady_clock::now();
    audio_loop_requests_+=desired_requests.size();
    const auto listener=spatial_audio_listener();
    std::unordered_map<uint32_t,thandor::SpatialSoundGainsQ15> desired_gains;
    desired_gains.reserve(desired_requests.size());
    for(const auto& [sound_index,request]:desired_requests){
        const auto gains=thandor::spatial_sound_gains_q15(listener,
            {request.world_position.x,request.world_position.y,request.world_position.z},
            request.gain_q15,request.maximum_distance_q12,sound_effects_gain_q15_,false);
        if(gains.audible())desired_gains.emplace(sound_index,gains);else ++audio_inaudible_;
    }
    for(auto it=active_looping_sounds_.begin();it!=active_looping_sounds_.end();){
        const auto gain=desired_gains.find(it->first);
        if(gain==desired_gains.end()||!it->second.voice||!it->second.voice->active()){
            if(it->second.voice)it->second.voice->stop();
            it=active_looping_sounds_.erase(it);
        }else{
            it->second.voice->set_gains(gain->second.left_q15,gain->second.right_q15);
            ++it;
        }
    }
    for(const auto& [sound_table_index,gains]:desired_gains){
        if(sound_table_index==0U||active_looping_sounds_.contains(sound_table_index)||
           looping_sound_start_failures_.contains(sound_table_index))continue;
        const auto path=resolve_runtime_spatial_sound_path(sound_table_index);
        if(!path){
            ++audio_missing_samples_;looping_sound_start_failures_.insert(sound_table_index);
            sound_playback_status_="Loop sound table index "+std::to_string(sound_table_index)+
                                   " has no unique LEV sound-template-matched SAM";
            continue;
        }
        try{
            auto found=sam_pcm_cache_.find(*path);
            if(found==sam_pcm_cache_.end()){
                auto decoded=std::make_shared<thandor::SamPcmData>(
                    thandor::decode_sam_to_pcm(catalog_.load(*path)));
                found=sam_pcm_cache_.emplace(*path,std::move(decoded)).first;
            }
            if(backend_now<backend_retry_after)continue;
            auto voice=thandor::play_pcm_loop(*found->second,gains.left_q15,gains.right_q15);
            if(!voice){
                ++audio_backend_rejected_;
                backend_retry_after=backend_now+std::chrono::seconds(5);
                sound_playback_status_="Audio backend unavailable for looping "+*path;
                continue;
            }
            active_looping_sounds_.emplace(sound_table_index,
                ActiveLoopingSound{*path,std::move(voice)});
            sound_playback_status_="Looping positioned "+*path;
        }catch(const std::exception& error){
            looping_sound_start_failures_.insert(sound_table_index);
            sound_playback_status_="Loop sound decode/play failed: "+std::string(error.what());
        }
    }
}

void EditorApp::spawn_environmental_linked_shots(uint32_t simulation_tick){
    // The animation checkbox controls presentation, not damaging map physics.
    if(!document_||timed_effect_controllers_.empty())return;
    const auto placements=document_->level().placements();

    struct ResolvedTimedEffectRuntimePose {
        Vec3f root_position{};
        ModelHierarchyTransform root_orientation{};
        ModelHierarchyTransform emitter_orientation{};
        std::vector<Vec3f> world_type6_attachments;
    };
    std::vector<thandor::ResourceEmitterTiming> timings;
    timings.reserve(timed_effect_controllers_.size());
    std::vector<ResolvedTimedEffectRuntimePose> resolved(timed_effect_controllers_.size());
    for(std::size_t index=0;index<timed_effect_controllers_.size();++index){
        const auto& source=timed_effect_controllers_[index];
        timings.push_back(source.timing);
        auto& pose=resolved[index];
        pose.root_position=source.root_position;
        pose.root_orientation=source.root_orientation;
        pose.emitter_orientation=source.emitter_orientation;
        if(source.placement_index<placements.size()){
            const auto& placement=placements[source.placement_index];
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
                    pose.root_orientation=transforms[source.root_model_node_index];
                    pose.root_position=pose.root_orientation.translation;
                }
                if(source.emitter_model_node_index<transforms.size())
                    pose.emitter_orientation=transforms[source.emitter_model_node_index];
            }
        }
        pose.world_type6_attachments.reserve(source.local_type6_attachments.size());
        for(const Vec3f attachment:source.local_type6_attachments)
            pose.world_type6_attachments.push_back(
                transform_point(pose.emitter_orientation,attachment));
    }

    const auto resolve_effect_for_pulse=[&](uint32_t emitter_index,uint32_t attachment_selector)
        ->std::pair<const thandor::EffectDefinitionRecord*,Vec3f>{
        if(emitter_index>=timed_effect_controllers_.size())return {nullptr,{}};
        const auto& source=timed_effect_controllers_[emitter_index];
        const auto& pose=resolved[emitter_index];
        Vec3f origin=pose.root_position;
        if(!pose.world_type6_attachments.empty())
            origin=pose.world_type6_attachments[
                attachment_selector%pose.world_type6_attachments.size()];
        const int32_t water_delta=thandor::retail_nearest_water_delta_q12(
            document_->field(),thandor::float_to_q12(origin.x),thandor::float_to_q12(origin.y));
        const uint32_t effect_id=water_delta>0?source.water_effect_id:source.land_effect_id;
        return {effect_id==0U?nullptr:catalog_.effect_definition(effect_id),origin};
    };

    const auto schedule=thandor::build_resource_emitter_schedule(timings,
        resource_animation_rng_seed_,2048U,256U,
        [&](uint32_t emitter_index,uint32_t,uint32_t attachment_selector){
            const auto [definition,unused_origin]=resolve_effect_for_pulse(
                emitter_index,attachment_selector);
            (void)unused_origin;
            return definition!=nullptr&&definition->transition_kind==2U;
        });
    if(schedule.cycle_ticks==0U)return;
    const uint32_t cycle_tick=simulation_tick%schedule.cycle_ticks;
    const auto runtime_pose_key=[](std::size_t pulse_index,uint32_t parent_spawn_tick){
        return (static_cast<uint64_t>(parent_spawn_tick)<<32U)|
            static_cast<uint64_t>(static_cast<uint32_t>(pulse_index));
    };

    // EffectRuntimePool_CreateInstanceFromDefinitionCf creates a detached root model node with
    // parentNode == null at the exact timed-emitter world point/orientation. Preserve that pulse
    // pose until transition-kind-1 has finished; a moving ArmyRuntime owner must not drag a
    // historical lava/controller EFF to its newer pose before the EFF emits its linked shot.
    for(auto it=environmental_effect_runtime_pose_history_.begin();
        it!=environmental_effect_runtime_pose_history_.end();){
        const auto& snapshot=it->second;
        if(simulation_tick>snapshot.spawn_tick&&
           simulation_tick-snapshot.spawn_tick>snapshot.lifetime_ticks)
            it=environmental_effect_runtime_pose_history_.erase(it);
        else ++it;
    }

    for(std::size_t pulse_index=0;pulse_index<schedule.pulses.size();++pulse_index){
        const auto& pulse=schedule.pulses[pulse_index];
        if(pulse.spawn_tick!=cycle_tick||pulse.emitter_index>=timed_effect_controllers_.size())continue;
        const auto [definition,origin]=resolve_effect_for_pulse(
            pulse.emitter_index,pulse.attachment_random);
        if(definition==nullptr||definition->transition_kind!=1U||
           definition->linked_shot_present==0U)continue;

        ModelHierarchyTransform effect_pose=resolved[pulse.emitter_index].root_orientation;
        effect_pose.translation=origin;
        if((definition->creation_flags&1U)!=0U){
            uint32_t random=pulse.orientation_random*1664525U+1013904223U+definition->definition_id;
            const auto random_basis=fixed_model_rotation_basis(random&0xFFFFU,
                (random>>16U)&0xFFFFU,0U);
            const Vec3f translation=effect_pose.translation;
            effect_pose=compose_model_transform(effect_pose,random_basis);
            effect_pose.translation=translation;
        }
        environmental_effect_runtime_pose_history_[runtime_pose_key(pulse_index,simulation_tick)]={
            definition->definition_id,simulation_tick,
            std::max(1U,thandor::effect_lifetime_ticks(*definition)),effect_pose};
    }

    for(std::size_t pulse_index=0;pulse_index<schedule.pulses.size();++pulse_index){
        const auto& pulse=schedule.pulses[pulse_index];
        if(pulse.emitter_index>=timed_effect_controllers_.size())continue;
        // The parent EFF definition (including land/water choice), randomized orientation and
        // world translation were fixed when ArmyRuntime_UpdateTimedShotAndEffectEmitters created
        // the EffectRuntime. Resolve linked-shot emissions from that historical instance rather
        // than re-querying the moving owner at the later emission tick.
        const std::array<uint32_t,2> candidate_effect_ids{
            timed_effect_controllers_[pulse.emitter_index].land_effect_id,
            timed_effect_controllers_[pulse.emitter_index].water_effect_id};
        for(std::size_t candidate_index=0;candidate_index<candidate_effect_ids.size();++candidate_index){
            const uint32_t candidate_effect_id=candidate_effect_ids[candidate_index];
            if(candidate_effect_id==0U||
               (candidate_index!=0U&&candidate_effect_id==candidate_effect_ids[0]))continue;
            const auto* candidate=catalog_.effect_definition(candidate_effect_id);
            if(candidate==nullptr||candidate->transition_kind!=1U||
               candidate->linked_shot_present==0U)continue;
            const auto emissions=thandor::build_linked_shot_emissions(*candidate);
            for(const auto& emission:emissions){
                const auto parent_spawn_tick=thandor::linked_shot_parent_effect_spawn_tick(
                    simulation_tick,schedule.cycle_ticks,pulse.spawn_tick,emission.parent_age_tick);
                if(!parent_spawn_tick)continue;
                const auto history=environmental_effect_runtime_pose_history_.find(
                    runtime_pose_key(pulse_index,*parent_spawn_tick));
                if(history==environmental_effect_runtime_pose_history_.end()||
                   history->second.effect_id!=candidate_effect_id)continue;

                const auto& snapshot=history->second;
                const auto* definition=catalog_.effect_definition(snapshot.effect_id);
                if(definition==nullptr)continue;
                const auto* shot=catalog_.shot_definition(definition->linked_shot_id);
                if(shot==nullptr)continue;
                const auto& effect_sprite=cached_sprite(definition->sprite_path);
                const uint32_t lifetime=snapshot.lifetime_ticks;
                const float progress=std::clamp(
                    static_cast<float>(std::min(emission.parent_age_tick,lifetime))/
                        static_cast<float>(lifetime),0.0F,1.0F);
                const float start_scale=thandor::q12_to_float(definition->model_scale_x_q12);
                const float end_scale=thandor::q12_to_float(definition->model_scale_y_q12);
                const float scale=start_scale+(end_scale-start_scale)*progress;
                const thandor::SpriteAttachmentRecord* attachment=nullptr;
                const thandor::SpriteAttachmentRecord* fallback=nullptr;
                for(const auto& lookup:effect_sprite.lookup_records()){
                    if(lookup.key_class().value()!=2U)continue;
                    if(fallback==nullptr)fallback=&lookup;
                    if(lookup.key_index().value()==emission.attachment_index){
                        attachment=&lookup;break;
                    }
                }
                if(attachment==nullptr&&emission.attachment_index==0U)attachment=fallback;
                if(attachment==nullptr)continue;
                const Vec3f origin=snapshot.effect_pose.translation;
                const Vec3f emitter=transform_point(snapshot.effect_pose,
                    mul(attachment->local_translation,scale));
                const auto geometry=thandor::simulation_linked_effect_shot_geometry(origin,emitter);

                thandor::SimulationWeaponProfile synthetic_weapon;
                synthetic_weapon.shot_definition_id=shot->definition_id;
                synthetic_weapon.reload_ticks=1U;
                synthetic_weapon.launch_attachment_count=1U;
                const std::array<Vec3f,1> launch_positions{geometry.launch_position};
                auto volley=thandor::simulation_create_volley(
                    std::numeric_limits<std::size_t>::max(),0U,synthetic_weapon,launch_positions,
                    geometry.aim_position,*shot,simulation_tick,next_simulation_projectile_serial_,
                    emission.attachment_index,std::nullopt,
                    thandor::SimulationProjectileOwnerKind::Environmental,0U);
                for(auto& projectile:volley){
                    thandor::simulation_resolve_projectile_surface_impact(
                        projectile,*shot,document_->field());
                    if(const auto* impact=catalog_.effect_definition(projectile.impact_effect_id))
                        projectile.impact_visual_lifetime_ticks=
                            std::max(1U,thandor::effect_lifetime_ticks(*impact));
                    else projectile.impact_visual_lifetime_ticks=1U;
                    if(shot->launch_effect_id!=0U){
                        if(const auto* launch_effect=catalog_.effect_definition(shot->launch_effect_id)){
                            Vec3f launch_effect_position=projectile.launch_position;
                            const auto initial_orientation=shot_model_transform(
                                projectile.launch_position,projectile.launch_yaw_turn16,
                                projectile.launch_pitch_turn16);
                            const auto& shot_sprite=cached_sprite(shot->sprite_path);
                            if(const auto* launch_attachment=shot_sprite.find_lookup_record(
                                   thandor::ModelLookupKeyIndex{0U},
                                   thandor::ModelLookupKeyClass{3U}))
                                launch_effect_position=transform_point(
                                    initial_orientation,launch_attachment->local_translation);
                            play_effect_positioned_sound(*launch_effect,launch_effect_position);
                        }
                    }
                }
                if(!volley.empty()){
                    simulation_projectiles_.insert(simulation_projectiles_.end(),
                        volley.begin(),volley.end());
                    ++simulation_volley_count_;
                }
            }
        }
    }

}

void EditorApp::update_simulation(uint32_t presentation_tick){
    const auto append_runtime_sound_requests=[&](
        std::unordered_map<uint32_t,RuntimeSpatialSoundRequest>& requests,
        std::size_t placement_index,const thandor::ArmyAssetPreset& preset,
        thandor::SimulationRuntimeSoundState state){
        if(!document_||placement_index>=document_->level().placements().size())return;
        const auto& placement=document_->level().placements()[placement_index];
        Vec3f source{placement.render_x(),placement.render_y(),
            terrain_height_near(placement.render_x(),placement.render_y())};
        if(const auto actor=simulation_actor_states_.find(placement_index);
           actor!=simulation_actor_states_.end())source=actor->second.position;
        const thandor::SimulationRuntimeSoundProfile profile{
            preset.runtime_model_class_id,preset.projected_sound_asset_id_d0,
            preset.projected_sound_asset_id_d8,preset.projected_sound_asset_id_primary,
            preset.projected_sound_asset_id_secondary};
        for(const uint32_t sound_index:thandor::simulation_runtime_looping_sound_indices(profile,state)){
            if(sound_index==0U)continue;
            // SpatialSound_UpdateDesiredPositionedGains writes the slot directly. If several live
            // runtimes share one sound-table index, the later callback in runtime traversal wins.
            requests[sound_index]=RuntimeSpatialSoundRequest{sound_index,source,
                preset.positioned_sound_gain_q15,preset.positioned_sound_maximum_distance_q12};
        }
    };
    if(!simulation_mode_){
        update_idle_class17_deployment(presentation_tick);
        std::unordered_map<uint32_t,RuntimeSpatialSoundRequest> desired_loop_sound_requests;
        if(document_){
            // Building/unit animation preview is a live-runtime visualization even when command
            // simulation is off. Keep the recovered active ambient callbacks audible there too;
            // movement-linked D0/D8 channels remain silent because no movement state is asserted.
            const auto preview_placements=document_->level().placements();
            for(std::size_t placement_index=0;placement_index<preview_placements.size();++placement_index){
                const auto& placement=preview_placements[placement_index];
                const auto* preset=catalog_.army_preset(placement.army_asset_id);
                if(!preset)continue;
                const bool enabled=(preset->browser_kind==thandor::PlacementBrowserKind::Building&&animate_buildings_)||
                                   (preset->browser_kind==thandor::PlacementBrowserKind::Unit&&animate_units_);
                if(!enabled)continue;
                append_runtime_sound_requests(desired_loop_sound_requests,placement_index,*preset,
                    {true,false,false,false});
            }
        }
        reconcile_runtime_looping_sounds(desired_loop_sound_requests);
        last_simulation_tick_=presentation_tick;
        return;
    }
    if(!document_){stop_all_runtime_looping_sounds();last_simulation_tick_=presentation_tick;return;}
    if(last_simulation_tick_==std::numeric_limits<uint32_t>::max()){
        last_simulation_tick_=presentation_tick;return;
    }
    if(presentation_tick<=last_simulation_tick_)return;
    prepare_simulation_navigation_terrain();
    const uint32_t steps=std::min<uint32_t>(presentation_tick-last_simulation_tick_,128U);
    const uint32_t first_tick=presentation_tick-steps+1U;
    const auto placements=document_->level().placements();
    std::optional<std::size_t> exclusive_target_actor;
    if(selected_placement_&&*selected_placement_<placements.size()){
        if(const auto* selected=catalog_.army_preset(placements[*selected_placement_].army_asset_id);
           selected&&!placement_weapon_profiles(*selected,*selected_placement_).empty()){
            const bool enabled=(selected->browser_kind==thandor::PlacementBrowserKind::Building&&animate_buildings_)||
                               (selected->browser_kind==thandor::PlacementBrowserKind::Unit&&animate_units_);
            if(enabled&&simulation_selected_only_)exclusive_target_actor=*selected_placement_;
        }
    }
    std::unordered_map<uint32_t,RuntimeSpatialSoundRequest> desired_loop_sound_requests;
    for(uint32_t step=0;step<steps;++step){
        desired_loop_sound_requests.clear();
        const uint32_t tick=first_tick+step;
        // EffectRuntime instances outlive the ArmyRuntime callback that emitted them. Age the
        // movement-ground history independently so stopping a unit leaves its existing marks in
        // place until the EFF lifetime/fade completes.
        movement_ground_effects_.erase(std::remove_if(movement_ground_effects_.begin(),
            movement_ground_effects_.end(),[&](const MovementGroundEffectInstance& instance){
                const auto* definition=catalog_.effect_definition(instance.effect_id);
                return definition==nullptr||tick-instance.spawn_tick>=thandor::effect_lifetime_ticks(*definition);
            }),movement_ground_effects_.end());
        // EffectRuntime transition-kind-1 linked shots are ordinary ShotRuntime owners in retail.
        // Materialize this tick's deterministic timed-map emissions before projectile maintenance so
        // they share the same model/terrain/secondary collision and damage path as weapon shots.
        spawn_environmental_linked_shots(tick);

        // Entity-targeted mode-2 shots re-read their target position every processed retail tick.
        // Mode 0 is the opposite: its launch ray is immutable, but retail re-tests that same fixed
        // ray against the current model/terrain/secondary surfaces on every maintenance tick.
        // Refresh only the nearest hit/beam distance; never bend a Laser coil/direct-line beam
        // toward a moving command target after the shot has already been created.
        for(auto& projectile:simulation_projectiles_){
            const auto* shot=catalog_.shot_definition(projectile.shot_definition_id);
            if(!shot)continue;
            if(shot->trajectory_mode==0){
                // ShotModelRuntimeMaintenance_UpdateProjectileMotionCollisionAndEffects re-runs the
                // same shared WorldRuntime owner-list model raycast every maintenance tick for
                // direct-line shots. Keep the launch direction frozen, but refresh the nearest
                // eligible model/terrain/secondary hit and therefore the beam stretch distance.
                simulation_refresh_mode0_projectile_raycast(projectile,*shot,tick);
                continue;
            }
            // Non-direct retail shots query the current owner list over exactly this tick's motion
            // segment before advancing/retargeting. This lets a moving transient or authored model
            // intercept an in-flight projectile without replacing an earlier terrain/water contact.
            simulation_refresh_non_direct_projectile_model_collision(projectile,*shot,tick);
            if(thandor::simulation_projectile_impacted(projectile,tick))continue;
            if(!projectile.live_target_placement_index||shot->trajectory_mode!=2)continue;
            const auto target_index=*projectile.live_target_placement_index;
            if(target_index>=placements.size())continue;
            thandor::simulation_update_projectile_live_target(
                projectile,*shot,placement_command_target_point(target_index));
        }
        // Shot audioRefresh owns a projected spatial slot for the live projectile. The slot is
        // reasserted every 20 Hz step and, like retail, later owners of the same slot index win.
        // Impact EFF audio is event-driven at the exact transition tick so it cannot repeat while
        // the impact sprite remains visible.
        for(auto& projectile:simulation_projectiles_){
            const auto* shot=catalog_.shot_definition(projectile.shot_definition_id);
            if(!shot)continue;
            const uint32_t impact_tick=projectile.spawn_tick+projectile.travel_ticks;
            if(tick<impact_tick&&shot->positioned_sound_table_index()!=0U){
                const Vec3f position=thandor::simulation_projectile_position(projectile,*shot,tick);
                // TerrainGrid_TestProjectedCellMaskBits01Cf gates the retail SHT audioRefresh
                // producer. Preserve it whenever the editor is displaying a runtime fog context.
                if(fog_preview_mode_==0||fog_preview_point_visible(position))
                    desired_loop_sound_requests[shot->positioned_sound_table_index()]={
                        shot->positioned_sound_table_index(),position,shot->positioned_sound_gain_q15,
                        shot->positioned_sound_maximum_distance_q12};
            }
            if(tick==impact_tick&&projectile.impact_effect_id!=0U){
                if(const auto* impact=catalog_.effect_definition(projectile.impact_effect_id))
                    play_effect_positioned_sound(*impact,projectile.impact_position);
            }
            if(tick==impact_tick&&!projectile.impact_damage_applied){
                // ShotModelRuntimeMaintenance_UpdateProjectileMotionCollisionAndEffects applies
                // target-class damage once, at the same transition that emits the target impact
                // EFF. Keep the actual raycast owner identity: an interceptor must take the hit,
                // not the originally commanded target behind it.
                projectile.impact_damage_applied=true;
                if(projectile.impact_kind==thandor::SimulationImpactKind::TargetModelImpactClass&&
                   projectile.impact_effect_id!=0U){
                    if(projectile.impact_target_placement_index&&
                       *projectile.impact_target_placement_index<placements.size()){
                        const std::size_t target_index=*projectile.impact_target_placement_index;
                        const auto* target_preset=catalog_.army_preset(placements[target_index].army_asset_id);
                        if(target_preset&&target_preset->shot_impact_class_index<8U&&
                           target_preset->root_maximum_integrity>0){
                            const int32_t impact_damage=
                                shot->target_impact_damage_q12[target_preset->shot_impact_class_index];
                            auto [damage_it,inserted]=simulation_integrity_states_.try_emplace(target_index,
                                SimulationPlacementIntegrityState{target_preset->root_maximum_integrity,
                                                                  target_preset->root_maximum_integrity});
                            auto& damage_state=damage_it->second;
                            if(inserted||damage_state.maximum_integrity<=0){
                                damage_state.maximum_integrity=target_preset->root_maximum_integrity;
                                damage_state.integrity=target_preset->root_maximum_integrity;
                            }
                            const int32_t before=damage_state.integrity;
                            const int64_t after64=static_cast<int64_t>(before)-impact_damage;
                            damage_state.integrity=static_cast<int32_t>(std::clamp<int64_t>(after64,0,
                                damage_state.maximum_integrity));
                            if(damage_state.integrity!=before){
                                animate_timed_effects_=true;
                                last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
                            }
                            if(before>0&&damage_state.integrity==0&&
                               target_preset->browser_kind==thandor::PlacementBrowserKind::Building&&
                               !building_destruction_states_.contains(target_index)){
                                uint32_t destruction_effect_id=target_preset->construction_destruction_effect_id;
                                const auto target_hierarchy=placement_hierarchy(*target_preset,target_index);
                                for(std::size_t node_index=0U;node_index<target_hierarchy.size();++node_index){
                                    const auto& node=target_hierarchy[node_index];
                                    const bool runtime_root=
                                        node.parent_index==std::numeric_limits<uint32_t>::max()||
                                        (node.parent_index<target_hierarchy.size()&&
                                         target_hierarchy[node.parent_index].model_definition_id!=
                                             node.model_definition_id);
                                    if(runtime_root&&node.simulation.construction_destruction_effect_id!=0U){
                                        destruction_effect_id=node.simulation.construction_destruction_effect_id;
                                        break;
                                    }
                                }
                                if(destruction_effect_id!=0U&&
                                   catalog_.effect_definition(destruction_effect_id)!=nullptr){
                                    building_destruction_states_[target_index]={tick,destruction_effect_id};
                                    simulation_static_replacement_dirty_=true;
                                }
                            }
                        }
                    }else if(projectile.impact_target_runtime_kind){
                        const auto apply_transient_damage=[&](auto& entity){
                            const auto* target_preset=catalog_.army_preset(entity.army_asset_id);
                            if(target_preset==nullptr||target_preset->shot_impact_class_index>=8U||
                               entity.maximum_integrity<=0)return;
                            const int32_t impact_damage=
                                shot->target_impact_damage_q12[target_preset->shot_impact_class_index];
                            const int32_t before=entity.integrity;
                            const int64_t after64=static_cast<int64_t>(before)-impact_damage;
                            entity.integrity=static_cast<int32_t>(std::clamp<int64_t>(after64,0,
                                entity.maximum_integrity));
                            if(entity.integrity!=before){
                                animate_timed_effects_=true;
                                last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
                            }
                        };
                        if(*projectile.impact_target_runtime_kind==
                           thandor::SimulationProjectileOwnerKind::ProducedClass13){
                            if(auto found=simulation_produced_entities_.find(
                                   projectile.impact_target_runtime_serial);
                               found!=simulation_produced_entities_.end())apply_transient_damage(found->second);
                        }else if(*projectile.impact_target_runtime_kind==
                                 thandor::SimulationProjectileOwnerKind::LinkedClass21){
                            if(auto found=simulation_linked_child_entities_.find(
                                   projectile.impact_target_runtime_serial);
                               found!=simulation_linked_child_entities_.end())apply_transient_damage(found->second);
                        }
                    }
                }
            }
        }
        update_produced_simulation_entities(tick);
        for(std::size_t index=0;index<placements.size();++index){
            const auto& placement=placements[index];
            const auto* preset=catalog_.army_preset(placement.army_asset_id);
            if(!preset)continue;
            // Class callbacks apply depth damage even while stationary and even
            // when the editor animation toggle is off. Sample the pre-move pose.
            const auto actor_found=simulation_actor_states_.find(index);
            const Vec3f depth_position=actor_found!=simulation_actor_states_.end()?actor_found->second.position:
                Vec3f{placement.render_x(),placement.render_y(),0};
            if(const auto damage=preview_depth_damage(document_->field(),*preset,depth_position);
               damage&&preset->root_maximum_integrity>0){
                auto [health,inserted]=simulation_integrity_states_.try_emplace(index,
                    SimulationPlacementIntegrityState{preset->root_maximum_integrity,preset->root_maximum_integrity});
                (void)inserted;
                health->second.integrity=static_cast<int32_t>(std::max<int64_t>(0,int64_t(health->second.integrity)-*damage));
                last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
            }
            if(const auto health=simulation_integrity_states_.find(index);
               health!=simulation_integrity_states_.end()&&health->second.maximum_integrity>0&&health->second.integrity<=0){
                if(actor_found!=simulation_actor_states_.end()){
                    actor_found->second.move_target.reset();actor_found->second.move_waypoint.reset();
                    actor_found->second.ground_advance_q12=0;
                }
                continue;
            }
            const bool enabled=(preset->browser_kind==thandor::PlacementBrowserKind::Building&&animate_buildings_)||
                               (preset->browser_kind==thandor::PlacementBrowserKind::Unit&&animate_units_);
            if(!enabled)continue;
            // Retail clears desired gains every frame and each live ArmyRuntime method-D callback
            // reasserts its persistent projected channel. For active editor simulation, classes
            // whose callback is an enabled/active ambient owner request their primary channel here.
            append_runtime_sound_requests(desired_loop_sound_requests,index,*preset,
                {true,false,false,false});
            if(preset->runtime_model_class_id==13U)
                update_class13_factory_production(index,*preset,tick);
            else if(preset->runtime_model_class_id==22U)
                update_class22_airport_storage(index,*preset,tick);
            const auto weapon_profiles=placement_weapon_profiles(*preset,index);
            const bool has_motion=preset->simulation_motion_profile.movable();
            const bool has_weapon=!weapon_profiles.empty();
            if(!has_motion&&!has_weapon)continue;
            std::optional<Vec3f> resolved_target=simulation_attack_target_point(index);
            const bool has_explicit_target=resolved_target.has_value();
            if(!resolved_target&&has_weapon&&placement_has_hostile_weapon(index)&&
               simulation_target_point_&&(!exclusive_target_actor||*exclusive_target_actor==index))
                resolved_target=simulation_target_point_;
            const bool receives_target=has_weapon&&resolved_target.has_value();
            const bool has_existing_state=simulation_actor_states_.contains(index);
            if(!receives_target&&!has_existing_state)continue;
            auto& actor=simulation_state_for(index);
            if(actor.weapons.size()!=weapon_profiles.size())
                actor.weapons.resize(weapon_profiles.size());
            if(preset->runtime_model_class_id==3U&&!actor.articulated_contacts_initialized){
                // Retail seeds both class-3 terrain contacts during placement, before the first
                // movement callback. Do the same before advancing the first editor movement tick;
                // lazy post-move initialization produces the visible Gorilla/Emu start-leg jump.
                (void)simulation_hierarchy_transforms(index,*preset,actor,nullptr,true);
            }
            const Vec3f previous_actor_position=actor.position;
            const uint32_t previous_body_yaw=actor.body_yaw_turn16;
            const bool actor_moved=simulation_preview_motion(actor,*preset,index);
            const bool actually_translated=actor.position.x!=previous_actor_position.x||
                                           actor.position.y!=previous_actor_position.y;
            const bool actually_turned=actor.body_yaw_turn16!=previous_body_yaw;
            append_runtime_sound_requests(desired_loop_sound_requests,index,*preset,
                {true,actually_translated,actually_turned,false});
            if(preset->runtime_model_class_id==2U&&
               preset->has_primary_texture_animation&&
               preset->has_secondary_texture_animation){
                const auto hierarchy=placement_hierarchy(*preset,index);
                const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){
                    return node.parent_index==std::numeric_limits<uint32_t>::max();
                });
                if(root!=hierarchy.end()&&!root->sprite_path.empty()){
                    const auto& root_sprite=cached_sprite(root->sprite_path);
                    thandor::simulation_update_tank_track_offsets(actor,previous_actor_position,
                        previous_body_yaw,actor.position,actor.body_yaw_turn16,
                        root_sprite.local_bounds_min().y,preset->track_texture_travel_scale);
                }
            }
            bool articulated_contact_completed=false;
            uint32_t articulated_completed_side=0U;
            if(preset->runtime_model_class_id==3U){
                const uint64_t contact_serial_before=actor.articulated_contact_event_serial;
                thandor::simulation_update_articulated_contact_transition(
                    actor,previous_actor_position,previous_body_yaw,actor.position,
                    actor.body_yaw_turn16,preset->articulated_contact_interpolation_rate_q12,
                    std::abs(preset->simulation_motion_profile.speed_q12_per_tick));
                if(actor.articulated_contact_event_serial!=contact_serial_before){
                    articulated_contact_completed=true;
                    articulated_completed_side=actor.articulated_last_completed_side&1U;
                    // Retail plays the class-3 contact sound from the army root, even though the
                    // associated land/water contact effect is emitted from the terminal foot.
                    play_runtime_spatial_sound(preset->articulated_contact_sound_asset_id,actor.position,
                        preset->positioned_sound_gain_q15,
                        preset->positioned_sound_maximum_distance_q12);
                }
            }
            thandor::simulation_tick_class17_pose(actor,actor_moved);
            auto transforms=simulation_hierarchy_transforms(index,*preset,actor,nullptr,true);
            const auto hierarchy=placement_hierarchy(*preset,index);

            // ArmyArticulatedRuntime_UpdateContactChildAndEffects is a separate class-3 footprint
            // path from the generic timed emitter. On contact completion it walks the selected root
            // child down child-slot zero three times, tests land/water at that terminal foot, then
            // creates the +0x174/+0x58 effect at the foot's world pose. The old editor omitted this
            // path and therefore rendered Orang/Gorilla marks at the root instead of alternating feet.
            if(preset->runtime_model_class_id==3U&&articulated_contact_completed){
                const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){
                    return node.parent_index==std::numeric_limits<uint32_t>::max();
                });
                if(root!=hierarchy.end()){
                    const uint32_t root_index=static_cast<uint32_t>(std::distance(hierarchy.begin(),root));
                    const auto contact_chain=articulated_walker_detail::leg_chain(
                        hierarchy,root_index,articulated_completed_side);
                    const uint32_t foot_index=contact_chain[3];
                    if(foot_index!=std::numeric_limits<uint32_t>::max()&&
                       foot_index<transforms.size()){
                        const Vec3f origin=transforms[foot_index].translation;
                        const int32_t water_delta=thandor::retail_nearest_water_delta_q12(
                            document_->field(),thandor::float_to_q12(origin.x),
                            thandor::float_to_q12(origin.y));
                        const uint32_t effect_id=water_delta>0
                            ?root->simulation.timed_water_effect_id:root->simulation.timed_effect_id;
                        if(effect_id!=0U&&catalog_.effect_definition(effect_id)!=nullptr){
                            ModelHierarchyTransform orientation=transforms[foot_index];
                            orientation.translation=origin;
                            movement_ground_effects_.push_back(MovementGroundEffectInstance{
                                index,effect_id,tick,origin,orientation,true});
                        }
                    }
                }
            }

            // RUNTIME_UPDATE[2] and the class-3 articulated movement callback also invoke the
            // generic ArmyRuntime_UpdateTimedShotAndEffectEmitters path after a root X/Y/yaw change.
            // Keep that movement dust/trail channel separate from the per-foot contact mark above.
            // The spawned EffectRuntime owns its world pose; later unit movement must not drag it.
            const bool ground_effect_root_changed=
                actor.position.x!=previous_actor_position.x||
                actor.position.y!=previous_actor_position.y||
                actor.body_yaw_turn16!=previous_body_yaw;
            if((preset->runtime_model_class_id==2U||preset->runtime_model_class_id==3U)&&
               ground_effect_root_changed){
                const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){
                    return node.parent_index==std::numeric_limits<uint32_t>::max();
                });
                if(root!=hierarchy.end()){
                    const std::size_t root_index=static_cast<std::size_t>(std::distance(hierarchy.begin(),root));
                    if(root_index<transforms.size()&&
                       thandor::simulation_tick_movement_ground_effect_countdown(actor,true,
                           root->simulation.timed_effect_interval_ticks)){
                        Vec3f origin=transforms[root_index].translation;
                        if(!root->sprite_path.empty()){
                            const auto type6=cached_sprite(root->sprite_path).type6_attachment_points();
                            if(!type6.empty())origin=transform_point(transforms[root_index],type6.front());
                        }
                        const int32_t water_delta=thandor::retail_nearest_water_delta_q12(
                            document_->field(),thandor::float_to_q12(origin.x),thandor::float_to_q12(origin.y));
                        const uint32_t effect_id=water_delta>0
                            ?root->simulation.timed_water_effect_id:root->simulation.timed_effect_id;
                        if(effect_id!=0U&&catalog_.effect_definition(effect_id)!=nullptr){
                            ModelHierarchyTransform orientation=transforms[root_index];
                            orientation.translation=origin;
                            movement_ground_effects_.push_back(
                                MovementGroundEffectInstance{index,effect_id,tick,origin,orientation});
                        }
                    }
                }
            }
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
            std::optional<bool> entity_target_same_faction;
            if(const auto entity_target=simulation_attack_placement_targets_.find(index);
               entity_target!=simulation_attack_placement_targets_.end()&&
               entity_target->second<placements.size())
                entity_target_same_faction=placements[entity_target->second].faction_index==
                                           placement.faction_index;
            Vec3f retail_los_origin=actor.position;
            for(std::size_t root_index=0;root_index<hierarchy.size()&&root_index<transforms.size();++root_index){
                if(hierarchy[root_index].parent_index==std::numeric_limits<uint32_t>::max()){
                    retail_los_origin=transforms[root_index].translation;
                    break;
                }
            }
            actor.target_in_range=false;actor.target_aligned=false;
            for(std::size_t weapon_index=0;weapon_index<weapon_profiles.size();++weapon_index){
                const auto& weapon=weapon_profiles[weapon_index];
                const auto* shot=catalog_.shot_definition(weapon.shot_definition_id);
                if(!shot)continue;
                const bool recovery_weapon=shot->is_repair_or_recovery_shot();
                const bool entity_relation_compatible=!entity_target_same_faction.has_value()||
                    (*entity_target_same_faction==recovery_weapon);
                const Vec3f* weapon_target=entity_relation_compatible?target:nullptr;
                uint32_t available_launch_selector_mask=0U;
                auto launch_points=simulation_weapon_launch_points(index,weapon,actor.weapons[weapon_index],
                    transforms,&available_launch_selector_mask);
                const Vec3f launch_origin=launch_points.empty()?actor.position:launch_points.front();
                Vec3f aim_origin=launch_origin;
                if(weapon.aim_model_node_index<transforms.size())
                    aim_origin=transforms[weapon.aim_model_node_index].translation;
                // Class 9 solves yaw/pitch from root->child[0], then rebuilds the hierarchy and
                // launches from the exact selected kind-2 point. 0x0052B9D0 tests LOS from the
                // ArmyRuntime root transform, not from the muzzle attachment.
                std::optional<std::size_t> intended_target_index;
                if(const auto target_placement=simulation_attack_placement_targets_.find(index);
                   target_placement!=simulation_attack_placement_targets_.end()&&
                   target_placement->second<placements.size()&&entity_relation_compatible)
                    intended_target_index=target_placement->second;
                const bool clear=!weapon_target||simulation_line_of_fire_clear(
                    index,intended_target_index,retail_los_origin,*weapon_target,*shot,recovery_weapon,tick);
                std::span<const float> yaw_world_basis;
                if(weapon.yaw_model_node_index<transforms.size())
                    yaw_world_basis=transforms[weapon.yaw_model_node_index].basis;
                auto result=thandor::simulation_tick_weapon(actor,actor.weapons[weapon_index],weapon,
                    *shot,aim_origin,weapon_target,clear,available_launch_selector_mask,yaw_world_basis);
                actor.target_in_range|=result.in_range;
                actor.target_aligned|=result.aligned;
                if(!result.fired||!weapon_target)continue;
                auto& weapon_state=actor.weapons[weapon_index];
                weapon_state.recoil_launch_selector=result.fired_attachment_selector!=0xFFFFFFFFU
                    ?result.fired_attachment_selector:weapon_state.next_launch_attachment;
                // Rebuild after the aim/recoil update so launch effects and SHT flight sprites
                // originate at the exact kind-2 attachment on the fired recoil leaf.
                transforms=simulation_hierarchy_transforms(index,*preset,actor);
                launch_points=simulation_weapon_launch_points(index,weapon,weapon_state,transforms);
                if(launch_points.empty())continue;
                std::optional<std::size_t> live_target_placement_index;
                if(const auto target_placement=simulation_attack_placement_targets_.find(index);
                   target_placement!=simulation_attack_placement_targets_.end()&&
                   target_placement->second<placements.size()){
                    live_target_placement_index=target_placement->second;
                }
                auto volley=thandor::simulation_create_volley(index,weapon_index,weapon,
                    launch_points,*weapon_target,*shot,tick,next_simulation_projectile_serial_,
                    result.fired_attachment_selector,live_target_placement_index);
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
                                squared_distance(projectile.launch_position,*weapon_target);
                            if(surface_is_nearer){
                                projectile=surface_candidate;
                                projectile.impact_target_placement_index.reset();
                                projectile.impact_target_runtime_kind.reset();
                                projectile.impact_target_runtime_serial=0U;
                            }else{
                                projectile.impact_kind=thandor::SimulationImpactKind::TargetModelImpactClass;
                                projectile.impact_position=*weapon_target;
                                projectile.target_position=*weapon_target;
                                projectile.impact_effect_id=
                                    shot->target_impact_effect_id(*target_impact_class);
                                projectile.impact_target_placement_index=
                                    projectile.live_target_placement_index;
                                projectile.impact_target_runtime_kind.reset();
                                projectile.impact_target_runtime_serial=0U;
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
                        if(shot->launch_effect_id!=0U){
                            if(const auto* launch_effect=catalog_.effect_definition(shot->launch_effect_id)){
                                // ShotRuntimePool_CreateProjectileFromDefinitionCf resolves the SHT
                                // SPR class-3/index-0 point before creating the launch EFF. Keep the
                                // positioned launch sound on that same world-space producer point.
                                Vec3f launch_effect_position=projectile.launch_position;
                                const auto initial_orientation=shot_model_transform(
                                    projectile.launch_position,projectile.launch_yaw_turn16,
                                    projectile.launch_pitch_turn16);
                                const auto& shot_sprite=cached_sprite(shot->sprite_path);
                                if(const auto* launch_attachment=shot_sprite.find_lookup_record(
                                       thandor::ModelLookupKeyIndex{0U},
                                       thandor::ModelLookupKeyClass{3U}))
                                    launch_effect_position=transform_point(
                                        initial_orientation,launch_attachment->local_translation);
                                play_effect_positioned_sound(*launch_effect,launch_effect_position);
                            }
                        }
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
        update_class21_linked_children(tick);
        // Native economy world-job 0 runs AFTER owner callbacks, once per eight
        // world ticks. It increments relationTransitionTick once, not by batch.
        if((tick&7U)==0U)
            for(auto& timer:simulation_relation_transition_ticks_)++timer;
    }
    reconcile_runtime_looping_sounds(desired_loop_sound_requests);
    last_simulation_tick_=presentation_tick;
    simulation_active_actor_count_=simulation_actor_states_.size()+simulation_produced_entities_.size()+
        simulation_linked_child_entities_.size();
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
    simulation_runtime_factions_.erase(*selected_placement_);
    ++simulation_runtime_owner_revision_;
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
    if(!preset||placement_weapon_profiles(*preset,*selected_placement_).empty()){
        status_="The selected placement has no recovered projectile weapon.";return;
    }
    checkpoint();
    const auto& state=simulation_state_for(*selected_placement_);
    const float dx=resolved_target->x-state.position.x;
    const float dy=resolved_target->y-state.position.y;
    placement.orientation_turn16=thandor::radians_to_turn16(std::atan2(dy,dx));
    document_->mark_dirty();
    simulation_actor_states_.erase(*selected_placement_);
    simulation_runtime_factions_.erase(*selected_placement_);
    ++simulation_runtime_owner_revision_;
    simulation_static_replacement_dirty_=false;
    rebuild_objects();
    status_="Baked whole-placement facing toward the target. LEV has no independent persistent turret-angle field.";
}

uint32_t EditorApp::fog_preview_observer_radius(
    const thandor::ArmyAssetPreset& preset)const noexcept{
    // Exact retail authority: max(MDL +0x48) across the resolved live model hierarchy.  This is
    // the input radius for the six projected hex wedges; terrain/water occlusion means the actual
    // visible boundary is not a Euclidean circle.
    return preset.fog_uncover_radius_q12<=0?0U:
        static_cast<uint32_t>(preset.fog_uncover_radius_q12);
}

void EditorApp::activate_fog_on_driving_order(uint32_t faction_index){
    if(fog_preview_mode_!=0||faction_index==0U||faction_index>7U)return;
    // Manual driving is a live simulation, not an offline static-asset inspection. Expose
    // persistent exploration automatically for the moving unit's own faction; the user may
    // still turn it off or choose the multi-faction union from the fog preview controls.
    fog_preview_mode_=1;
    fog_preview_faction_=static_cast<int>(faction_index);
    fog_preview_occupancy_history_.clear();
    // Enabling fog changes static-copy visibility as well as the dynamic unit observer.
    simulation_static_replacement_dirty_=true;
    last_dynamic_scene_tick_=std::numeric_limits<uint32_t>::max();
}

bool EditorApp::fog_preview_point_visible(Vec3f point)const noexcept{
    return fog_preview_model_intensity(point,0x10U)!=0U;
}

uint8_t EditorApp::fog_preview_model_intensity(Vec3f point,uint32_t model_definition_flags)const noexcept{
    if(fog_preview_mode_==0)return 255U;
    if(!document_)return 0U;
    const auto cell=thandor::retail_fog_world_to_cell(document_->field(),
        thandor::float_to_q12(point.x),thandor::float_to_q12(point.y));
    if(!cell)return 0U;
    const std::size_t index=static_cast<std::size_t>(cell->y)*document_->field().width()+cell->x;
    if(index>=fog_preview_occupancy_history_.size())return 0U;
    if(fog_preview_mode_==1)
        return thandor::retail_model_fog_intensity(thandor::fog_occupancy_byte(
            fog_preview_occupancy_history_[index],static_cast<uint32_t>(fog_preview_faction_)),model_definition_flags);
    uint8_t intensity=0U;
    const uint32_t active_factions=fog_preview_active_faction_count();
    for(uint32_t faction=1U;faction<=active_factions;++faction)
        intensity=std::max(intensity,thandor::retail_model_fog_intensity(thandor::fog_occupancy_byte(
            fog_preview_occupancy_history_[index],faction),model_definition_flags));
    return intensity;
}

std::vector<uint64_t> EditorApp::simulation_visibility_occupancy_for_faction(uint32_t faction_index){
    if(!document_||faction_index==0U||faction_index>7U)return {};
    std::vector<thandor::FogPreviewObserver> observers;
    const auto placements=document_->level().placements();
    for(std::size_t index=0;index<placements.size();++index){
        const auto& placement=placements[index];
        if(placement.faction_index!=faction_index)continue;
        const auto* preset=catalog_.army_preset(placement.army_asset_id);
        if(!preset||(preset->browser_kind!=thandor::PlacementBrowserKind::Unit&&
                     preset->browser_kind!=thandor::PlacementBrowserKind::Building))continue;
        if(const auto destruction=building_destruction_states_.find(index);
           destruction!=building_destruction_states_.end()){
            const auto* effect=destruction->second.effect_id==0U?nullptr:
                catalog_.effect_definition(destruction->second.effect_id);
            const uint32_t lifetime=effect?thandor::effect_lifetime_ticks(*effect):0U;
            const uint32_t age=current_presentation_tick_>=destruction->second.start_tick
                ?current_presentation_tick_-destruction->second.start_tick:0U;
            if(lifetime==0U||age>=lifetime)continue;
        }
        const uint32_t radius=fog_preview_observer_radius(*preset);
        if(radius==0U)continue;
        auto& actor=simulation_state_for(index);
        const auto hierarchy=placement_hierarchy(*preset,index);
        std::vector<uint8_t> active_nodes;
        const auto transforms=simulation_hierarchy_transforms(index,*preset,actor,&active_nodes);
        std::size_t root_index=std::numeric_limits<std::size_t>::max();
        for(std::size_t node=0;node<hierarchy.size()&&node<active_nodes.size();++node){
            if(active_nodes[node]!=0U&&hierarchy[node].parent_index==std::numeric_limits<uint32_t>::max()){
                root_index=node;break;
            }
        }
        if(root_index==std::numeric_limits<std::size_t>::max()||root_index>=transforms.size())continue;
        const Vec3f root_position=transforms[root_index].translation;
        int32_t maximum_reference_offset_q12=0;
        for(std::size_t node=0;node<hierarchy.size()&&node<transforms.size()&&node<active_nodes.size();++node){
            if(active_nodes[node]==0U)continue;
            const int32_t relative_z=thandor::float_to_q12(
                transforms[node].translation.z-root_position.z);
            const int64_t candidate=static_cast<int64_t>(relative_z)+
                hierarchy[node].simulation.fog_reference_height_offset_q12;
            if(candidate>maximum_reference_offset_q12)
                maximum_reference_offset_q12=static_cast<int32_t>(std::min<int64_t>(
                    candidate,std::numeric_limits<int32_t>::max()));
        }
        const int64_t reference=static_cast<int64_t>(thandor::float_to_q12(root_position.z))+
            maximum_reference_offset_q12;
        const uint8_t marker=thandor::retail_visibility_marker_for_model_class(
            preset->runtime_model_class_id);
        observers.push_back({thandor::float_to_q12(root_position.x),
            thandor::float_to_q12(root_position.y),
            static_cast<int32_t>(std::clamp<int64_t>(reference,
                std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max())),
            radius,thandor::retail_self_faction_visibility_mask(faction_index,marker)});
    }
    // Transient class-13 products and class-21 linked children are not LEV placements but are
    // WorldRuntime owners in retail. Include their root visibility contribution. Their hierarchy
    // reference height is static here because the editor has no generic transient hierarchy state
    // object; the root position and exact class marker/radius remain authoritative.
    const auto append_transient=[&](uint32_t asset_id,uint32_t owner,const thandor::SimulationActorState& actor){
        if(owner!=faction_index)return;
        const auto* preset=catalog_.army_preset(asset_id);
        if(!preset)return;
        const uint32_t radius=fog_preview_observer_radius(*preset);
        if(radius==0U)return;
        int32_t reference_offset=0;
        auto resolved=thandor::resolve_faction_technology_hierarchy(
            *preset,document_->faction_technology_mask(std::min(owner,7U)));
        const auto hierarchy=!resolved.empty()?std::span<const thandor::ModelVisualNode>(resolved):
            (preset->resolved_model_hierarchy.empty()?std::span<const thandor::ModelVisualNode>(preset->model_hierarchy):
             std::span<const thandor::ModelVisualNode>(preset->resolved_model_hierarchy));
        for(const auto& node:hierarchy)
            reference_offset=std::max(reference_offset,node.simulation.fog_reference_height_offset_q12);
        const uint8_t marker=thandor::retail_visibility_marker_for_model_class(preset->runtime_model_class_id);
        observers.push_back({thandor::float_to_q12(actor.position.x),thandor::float_to_q12(actor.position.y),
            static_cast<int32_t>(std::clamp<int64_t>(
                static_cast<int64_t>(thandor::float_to_q12(actor.position.z))+reference_offset,
                std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max())),
            radius,thandor::retail_self_faction_visibility_mask(faction_index,marker)});
    };
    for(const auto& [serial,entity]:simulation_produced_entities_){(void)serial;
        if(entity.maximum_integrity<=0||entity.integrity>0)
            append_transient(entity.army_asset_id,entity.faction_index,entity.actor);}
    for(const auto& [serial,child]:simulation_linked_child_entities_){(void)serial;
        if(child.visible&&(child.maximum_integrity<=0||child.integrity>0))
            append_transient(child.army_asset_id,child.faction_index,child.actor);}
    return thandor::build_projected_fog_preview_occupancy(document_->field(),observers);
}

void EditorApp::rebuild_fog_preview_visibility(){
    fog_preview_observers_.clear();
    fog_preview_visible_cell_count_=0U;
    fog_preview_explored_cell_count_=0U;
    fog_preview_observer_count_=0U;
    if(!document_||fog_preview_mode_==0){
        fog_preview_occupancy_history_.clear();
        fog_preview_runtime_cells_.clear();
        fog_preview_history_document_=nullptr;
        fog_preview_last_retail_tick_=std::numeric_limits<uint32_t>::max();
        return;
    }
    const auto placements=document_->level().placements();
    const auto lab_runs=simulation_lab_runs();
    const bool lab_fog=simulation_lab_active()&&!lab_runs.empty();
    const int selected_lab_run=lab_fog?std::clamp(simulation_lab_selected_run_,0,
        static_cast<int>(lab_runs.size())-1):-1;
    // LEV and the selected Lab snapshot may have different active-faction counts (e.g. an
    // isolated multi-faction experiment on a single-player map). Use the worker's actual
    // lifecycle when displaying the union of its owners.
    const uint32_t active_factions=fog_preview_active_faction_count();
    if(lab_fog){
        const auto& run=lab_runs[static_cast<std::size_t>(selected_lab_run)];
        for(const auto& entity:run.entities){
            // Match the worker's actual model-owner callback admission. In
            // native runs a linked terminal/construction model still emits
            // sight; a released root does not. Synthetic fixtures retain
            // their existing readiness gate through the shared helper.
            if(!thandor::simulation_lab_phase7_occlusion_owner_eligible(run,entity))continue;
            const auto radius=thandor::simulation_lab_phase7_occlusion_preview_radius(run,entity);
            if(radius==0U)continue;
            if(fog_preview_mode_==1&&entity.faction_index!=static_cast<uint32_t>(fog_preview_faction_))continue;
            if(fog_preview_mode_==2&&entity.faction_index>active_factions)continue;
            const auto marker=thandor::retail_visibility_marker_for_model_class(entity.runtime_model_class_id);
            const auto owner=entity.faction_index;
            const uint64_t mask=run.level_runtime_enabled
                ?thandor::retail_faction_visibility_mask(run.faction_packed_relation_states[owner],marker)
                :thandor::retail_self_faction_visibility_mask(owner,marker);
            fog_preview_observers_.push_back({
                thandor::float_to_q12(entity.actor.position.x),
                thandor::float_to_q12(entity.actor.position.y),
                static_cast<int32_t>(std::clamp<int64_t>(
                    static_cast<int64_t>(thandor::float_to_q12(entity.actor.position.z))+
                        entity.fog_reference_height_offset_q12,
                    std::numeric_limits<int32_t>::min(),
                    std::numeric_limits<int32_t>::max())),
                radius,mask});
        }
    }else for(std::size_t index=0;index<placements.size();++index){
        const auto& placement=placements[index];
        if(placement.faction_index==0U||placement.faction_index>7U)continue;
        if(fog_preview_mode_==1&&placement.faction_index!=static_cast<uint32_t>(fog_preview_faction_))continue;
        if(fog_preview_mode_==2&&placement.faction_index>active_factions)continue;
        const auto* preset=catalog_.army_preset(placement.army_asset_id);
        if(!preset||(preset->browser_kind!=thandor::PlacementBrowserKind::Unit&&
                     preset->browser_kind!=thandor::PlacementBrowserKind::Building))continue;
        const uint32_t uncover_radius=fog_preview_observer_radius(*preset);
        if(uncover_radius==0U)continue;

        // Retail computes ArmyRuntime +0x48 from the live hierarchy as
        // max(nodeWorldZ-rootWorldZ + MDL +0x70), then adds that to the root world Z before the
        // projected terrain scan. Reuse the editor's live hierarchy transforms so deployed
        // rotors/turrets/articulated supports contribute only when their runtime nodes exist.
        auto& actor=simulation_state_for(index);
        const auto hierarchy=placement_hierarchy(*preset,index);
        std::vector<uint8_t> active_nodes;
        const auto transforms=simulation_hierarchy_transforms(index,*preset,actor,&active_nodes);
        std::size_t root_index=std::numeric_limits<std::size_t>::max();
        for(std::size_t node=0;node<hierarchy.size()&&node<active_nodes.size();++node){
            if(active_nodes[node]!=0U&&
               hierarchy[node].parent_index==std::numeric_limits<uint32_t>::max()){
                root_index=node;break;
            }
        }
        if(root_index==std::numeric_limits<std::size_t>::max()||root_index>=transforms.size())continue;
        const Vec3f root_position=transforms[root_index].translation;
        int32_t maximum_reference_offset_q12=0;
        for(std::size_t node=0;node<hierarchy.size()&&node<transforms.size()&&node<active_nodes.size();++node){
            if(active_nodes[node]==0U)continue;
            const int32_t relative_world_z_q12=thandor::float_to_q12(
                transforms[node].translation.z-root_position.z);
            const int64_t candidate=static_cast<int64_t>(relative_world_z_q12)+
                hierarchy[node].simulation.fog_reference_height_offset_q12;
            if(candidate>maximum_reference_offset_q12)
                maximum_reference_offset_q12=static_cast<int32_t>(std::min<int64_t>(
                    candidate,std::numeric_limits<int32_t>::max()));
        }
        const int64_t reference_height=static_cast<int64_t>(thandor::float_to_q12(root_position.z))+
            maximum_reference_offset_q12;
        const uint8_t marker=thandor::retail_visibility_marker_for_model_class(
            preset->runtime_model_class_id);
        fog_preview_observers_.push_back({
            thandor::float_to_q12(root_position.x),
            thandor::float_to_q12(root_position.y),
            static_cast<int32_t>(std::clamp<int64_t>(reference_height,
                std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max())),
            uncover_radius,
            // LEV has no live diplomacy/relation state. Use the exact owner's byte only rather
            // than inventing alliance sharing; retail_faction_visibility_mask() remains available
            // to replay a real packed relation state when that runtime surface is recovered.
            thandor::retail_self_faction_visibility_mask(placement.faction_index,marker)});
    }
    fog_preview_observer_count_=fog_preview_observers_.size();
    const auto& field=document_->field();
    std::vector<std::uint64_t> current;
    if(lab_fog){
        const auto& worker_run=lab_runs[static_cast<std::size_t>(selected_lab_run)];
        // The worker has already evaluated this visibility phase from the exact simulation
        // positions. Reprojection at the latest UI pose is not the same time slice, especially
        // after crossing a cell boundary while the retail phase-7 visibility pass is pending.
        const auto cell_count=static_cast<std::size_t>(field.width())*field.height();
        if(thandor::simulation_lab_phase7_preview_uses_retained_field(worker_run,cell_count)) {
            // The last native model owner is gone and case 7 has already
            // cleared stale transient occupancy. Preserve the retained field
            // image (including explored and LOCAL_FACTION_ENDED bits) rather
            // than replaying the last projected visibility preview.
            current=worker_run.retail_scratch->field_occupancy_masks;
        } else if(worker_run.current_visibility_occupancy.size()==cell_count) {
            current=worker_run.current_visibility_occupancy;
        }
    }
    if(current.empty())
        current=thandor::build_projected_fog_preview_occupancy(field,fog_preview_observers_);
    if(lab_fog){
        const auto& run=lab_runs[static_cast<std::size_t>(selected_lab_run)];
        // The worker owns persistent explored bits, including cells uncovered by synthetic
        // mobile armies. Merge them into the preview without inventing currently-visible bits.
        for(uint32_t faction=1U;faction<run.faction_explored_cell_bits.size();++faction){
            const auto& explored=run.faction_explored_cell_bits[faction];
            for(std::size_t cell=0;cell<current.size();++cell){
                if((cell>>6U)<explored.size()&&((explored[cell>>6U]>>(cell&63U))&1U)!=0U)
                    current[cell]|=uint64_t{0x80U}<<(faction*8U);
            }
        }
    }
    if(fog_preview_all_explored_){
        uint64_t explored_mask=0U;
        if(fog_preview_mode_==1){
            explored_mask=uint64_t{0x80U}<<(static_cast<uint32_t>(fog_preview_faction_)*8U);
        }else{
            for(uint32_t faction=1U;faction<=active_factions;++faction)
                explored_mask|=uint64_t{0x80U}<<(faction*8U);
        }
        for(auto& occupancy:current)occupancy|=explored_mask;
    }
    const bool history_identity_changed=
        fog_preview_history_document_!=document_.get()||
        fog_preview_history_mode_!=fog_preview_mode_||
        fog_preview_history_faction_!=fog_preview_faction_||
        fog_preview_history_all_explored_!=fog_preview_all_explored_||
        fog_preview_history_lab_run_!=selected_lab_run||
        fog_preview_occupancy_history_.size()!=current.size()||
        fog_preview_runtime_cells_.size()!=current.size();
    constexpr uint64_t kPerFactionExploredBits=0x8080808080808080ULL;
    const uint32_t visibility_tick=lab_fog?lab_runs[static_cast<std::size_t>(selected_lab_run)].tick:
        current_presentation_tick_;

    const auto selected_source_byte=[&](std::size_t index)->uint8_t{
        if(fog_preview_mode_==1){
            return thandor::fog_occupancy_byte(
                fog_preview_occupancy_history_[index],static_cast<uint32_t>(fog_preview_faction_));
        }
        // "All active factions" is an editor-only union view. Preserve the retail endpoint and
        // temporal-clamp semantics by synthesizing only the two states needed by the lookup:
        // marker 0x88 when any active faction currently sees the cell, otherwise 0x80 when any
        // active faction has explored it.
        bool current_visible=false;
        bool explored=false;
        for(uint32_t faction=1U;faction<=active_factions;++faction){
            const uint8_t occupancy=thandor::fog_occupancy_byte(
                fog_preview_occupancy_history_[index],faction);
            current_visible|=thandor::retail_fog_currently_visible(occupancy);
            explored|=thandor::retail_fog_explored(occupancy);
        }
        return current_visible?0x88U:(explored?0x80U:0U);
    };

    if(history_identity_changed){
        // The retail load/reselection paths rebuild occupancy and call
        // FieldGrid_ClassifyCellFlagsToRuntimeByte directly, bypassing the temporal clamp. Mirror
        // that immediate initialization, then use the normal phase-3/phase-7 tick wheel below.
        fog_preview_occupancy_history_=current;
        fog_preview_runtime_cells_.resize(current.size(),0U);
        for(std::size_t index=0;index<current.size();++index)
            fog_preview_runtime_cells_[index]=
                thandor::retail_fog_runtime_visibility_byte(selected_source_byte(index));
        fog_preview_history_document_=document_.get();
        fog_preview_history_mode_=fog_preview_mode_;
        fog_preview_history_faction_=fog_preview_faction_;
        fog_preview_history_all_explored_=fog_preview_all_explored_;
        fog_preview_history_lab_run_=selected_lab_run;
        fog_preview_last_retail_tick_=visibility_tick;
    }else{
        if(fog_preview_last_retail_tick_==std::numeric_limits<uint32_t>::max()||
           visibility_tick<fog_preview_last_retail_tick_)
            fog_preview_last_retail_tick_=visibility_tick;
        const uint32_t elapsed=std::min<uint32_t>(
            visibility_tick-fog_preview_last_retail_tick_,128U);
        const uint32_t first_tick=visibility_tick-elapsed+1U;
        for(uint32_t step=0;step<elapsed;++step){
            const uint32_t retail_tick=first_tick+step;
            if((retail_tick&7U)==7U &&
               (!lab_fog || retail_tick==lab_runs[static_cast<std::size_t>(selected_lab_run)]
                                               .current_visibility_occupancy_tick)){
                // Do not replay a newly-published worker occupancy at *earlier* phase-7
                // callbacks if the UI skipped worker generations. The newest live pose did
                // not exist at those earlier ticks; the worker explored bitset supplies the
                // missed terrain history, while current-visible bits change only at their
                // actual callback tick.
                // FieldGrid_ClearOccupancyMaskBits0To6AllCells preserves explored bit 7 in every
                // faction byte before the phase-7 owner-list visibility callbacks rebuild the
                // current marker bits.
                for(std::size_t index=0;index<current.size();++index)
                    fog_preview_occupancy_history_[index]=
                        (fog_preview_occupancy_history_[index]&kPerFactionExploredBits)|current[index];
            }
            // V530 InGameRuntime_UpdateSimulationAndNetworkTick performs the second phase-3
            // clamp, and the sole phase-7 clamp, only when the world owner list is non-empty.
            const bool has_runtime_owners=lab_fog
                ?thandor::simulation_lab_phase7_preview_has_world_owner(
                     lab_runs[static_cast<std::size_t>(selected_lab_run)])
                :!placements.empty();
            const uint8_t clamp_calls=thandor::retail_fog_clamp_calls_for_simulation_tick(
                retail_tick,has_runtime_owners);
            for(uint8_t call=0U;call<clamp_calls;++call){
                for(std::size_t index=0;index<fog_preview_runtime_cells_.size();++index)
                    fog_preview_runtime_cells_[index]=thandor::retail_fog_clamp_runtime_byte(
                        fog_preview_runtime_cells_[index],selected_source_byte(index));
            }
        }
        fog_preview_last_retail_tick_=visibility_tick;
    }

    if(lab_fog){
        // Worker generations can skip several phase-7 visibility callbacks, or finish on
        // a non-phase-7 tick. Its explored bits are authoritative and monotonic: importing
        // them must NOT depend on the UI having displayed the exact visibility tick.
        // Only merge bit 7 here; never invent present-time visibility from a later pose.
        for(std::size_t index=0;index<current.size();++index){
            const uint64_t newly_explored=
                (current[index]&kPerFactionExploredBits)&
                ~fog_preview_occupancy_history_[index];
            if(newly_explored==0U)continue;
            fog_preview_occupancy_history_[index]|=newly_explored;
            // A skipped world tick already occurred in the worker. Bring the rendered
            // never-seen terrain to the explored (dim) tier, without claiming it is visible.
            const uint8_t target=thandor::retail_fog_runtime_visibility_byte(
                selected_source_byte(index));
            if(target==0x87U)
                fog_preview_runtime_cells_[index]=std::max<uint8_t>(
                    fog_preview_runtime_cells_[index],target);
        }
    }

    for(std::size_t index=0;index<fog_preview_occupancy_history_.size();++index){
        const uint8_t occupancy=selected_source_byte(index);
        if(thandor::retail_fog_currently_visible(occupancy))++fog_preview_visible_cell_count_;
        if(thandor::retail_fog_explored(occupancy))++fog_preview_explored_cell_count_;
    }
}

void EditorApp::rebuild_dynamic_scene_cpu(bool camera_only){
    auto performance_timer=editor_performance_log_.measure(EditorPerfStage::DynamicBuild);
    const auto started=std::chrono::steady_clock::now();
    simulation_lab_render_receipts_.clear();
    uint32_t runtime_visual_tick=current_presentation_tick_;
    if(simulation_lab_active()){
        const auto runs=simulation_lab_runs();
        if(!runs.empty()){
            const std::size_t run_index=static_cast<std::size_t>(std::clamp(
                simulation_lab_selected_run_,0,static_cast<int>(runs.size())-1));
            runtime_visual_tick=runs[run_index].tick;
        }
    }
    auto& cache=dynamic_model_cache_;
    const std::array<uint32_t,15> cache_settings{static_cast<uint32_t>(fog_preview_mode_),
        static_cast<uint32_t>(fog_preview_faction_),fog_preview_all_explored_,animate_buildings_,
        animate_units_,animate_timed_effects_,animate_resource_fields_,projected_object_shadows_,
        simulation_lab_show_all_runs_,static_cast<uint32_t>(simulation_lab_selected_run_),
        lighting_cycle_preview_enabled_,lighting_cycle_preview_tick_,simulation_mode_,
        show_movement_ground_marks_,simulation_runtime_owner_revision_};
    const bool reuse_state=camera_only&&cache.valid&&!runtime_shadow_collector_&&document_&&
        cache.document==document_.get()&&cache.snapshot==simulation_lab_snapshot_.get()&&
        cache.settings==cache_settings&&cache.visual_tick==runtime_visual_tick&&
        cache.building_tick==current_building_animation_tick_;
    bool reuse_models=reuse_state;
    if(reuse_models)for(const auto& query:cache.views){
        const bool visible=query.use_bounds?bounds_visible_in_canvas(query.bounds,query.margin):
            placement_visible_in_canvas(query.center,query.margin);
        if(visible!=query.visible){reuse_models=false;++dynamic_view_cache_misses_;break;}
    }
    // Any exceptional build must leave no apparently valid, partially written cache.
    cache.valid=false;
    editor_performance_log_.record(EditorPerfStage::SceneCacheCheck,
        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());
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
        dynamic_scene_pre_shadow_milliseconds_=dynamic_scene_shadow_milliseconds_=0.0;
        dynamic_scene_fog_milliseconds_=dynamic_scene_model_setup_milliseconds_=0.0;
        dynamic_scene_effects_milliseconds_=dynamic_scene_sort_milliseconds_=0.0;
        return;
    }
    const auto fog_started=std::chrono::steady_clock::now();
    if(reuse_state)++dynamic_fog_cache_hits_;
    else rebuild_fog_preview_visibility();
    const auto fog_finished=std::chrono::steady_clock::now();
    constexpr std::size_t kMaximumDynamicVertices=500'000U;
    constexpr std::size_t kMaximumDynamicIndices=1'500'000U;
    constexpr uint64_t kEffectLayerNamespace=uint64_t{1}<<63U;
    constexpr uint64_t kUntexturedLayerKey=std::numeric_limits<uint64_t>::max();
    const auto level_lighting=lighting_cycle_preview_enabled_
        ?document_->level().lighting().sampled_at_tick(lighting_cycle_preview_tick_)
        :document_->level().lighting();
    const auto ramp=level_lighting.build_game_ramp();
    const auto prepared_directional_light=level_lighting.prepare_directional_light();
    const std::string army_base=document_->level().paths().army_textures;
    const std::string effect_base=terrain_base_path(document_->level().paths().effect_textures);

    // V702: A dynamic-scene build visits many actors using the same faction's material set.
    // The old lambda rebuilt the same normalized path/string key and hashed the global map
    // on *every* sprite/actor. The owning shared_ptr lives in army_texture_cache_, so caching
    // its pointee locally is safe for this scene build; a map/root change builds a new table.
    std::array<const ArmyTextureFamily*,9> scene_army_families{};
    const ArmyTextureFamily* scene_effect_family=nullptr;
    uint64_t scene_family_fast_hits=0U;
    uint64_t scene_family_fast_misses=0U;
    const auto texture_family=[&](uint32_t faction)->const ArmyTextureFamily&{
        const uint32_t clamped=std::min(faction,8U);
        if(const auto* cached=scene_army_families[clamped]){
            ++scene_family_fast_hits;
            return *cached;
        }
        ++scene_family_fast_misses;
        const std::string key=terrain_base_path(army_base)+"#"+std::to_string(clamped);
        auto found=army_texture_cache_.find(key);
        if(found==army_texture_cache_.end()){
            auto family=load_texture_family(army_texture_family_path(army_base,clamped,".gfx"),
                                            army_texture_family_path(army_base,clamped,".pal"));
            found=army_texture_cache_.emplace(key,std::move(family)).first;
        }
        scene_army_families[clamped]=found->second.get();
        return *scene_army_families[clamped];
    };
    const auto effect_texture_family=[&]()->const ArmyTextureFamily&{
        if(scene_effect_family){
            ++scene_family_fast_hits;
            return *scene_effect_family;
        }
        ++scene_family_fast_misses;
        const std::string key=effect_base+"#effect";
        auto found=army_texture_cache_.find(key);
        if(found==army_texture_cache_.end())found=army_texture_cache_.emplace(
            key,load_texture_family(effect_base+".gfx",effect_base+".pal")).first;
        scene_effect_family=found->second.get();
        return *scene_effect_family;
    };
    const auto layer_for_key=[&](uint64_t key)->uint32_t{
        const auto found=object_texture_layer_lookup_.find(key);
        if(found==object_texture_layer_lookup_.end()){
            // Error-only diagnostic: log the selected worker snapshot identity so
            // an atlas-residency failure can be paired with its renderer source.
            std::string run_context;
            if(simulation_lab_active()){
                const auto runs=simulation_lab_runs();
                if(!runs.empty()){
                    const auto index=static_cast<std::size_t>(std::clamp(
                        simulation_lab_selected_run_,0,static_cast<int>(runs.size())-1));
                    run_context=" selected_run="+std::to_string(index)+
                        " selected_tick="+std::to_string(runs[index].tick)+
                        " selected_variants="+std::to_string(lab_selected_render_variants_);
                }
            }
            report_asset_failure("texture-layer/"+std::to_string(key),
                "dynamic texture was not preloaded"+run_context+" static_error="+first_object_build_error_);
            const auto white=object_texture_layer_lookup_.find(kUntexturedLayerKey);
            const uint32_t fallback=white==object_texture_layer_lookup_.end()?0U:white->second;
            object_texture_layer_lookup_.emplace(key,fallback);
            return fallback;
        }
        return found->second;
    };
    const auto object_layer=[&](uint32_t runtime_faction,uint32_t graphics_family,
                                  uint32_t subresource){
        return layer_for_key(army_texture_layer_key(runtime_faction,graphics_family,subresource,
            local_player_texture_override_.has_value(),local_player_runtime_faction_));
    };
    const auto effect_layer=[&](uint32_t subresource){return layer_for_key(kEffectLayerNamespace|subresource);};
    const auto untextured_layer=[&](){return layer_for_key(kUntexturedLayerKey);};
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
    if(!reuse_models){cache.views.clear();cache.steps.clear();}
    // Include nearby offscreen casters whose shadows can still reach the canvas.
    // Store the expanded query margin in the existing visibility cache as usual.
    const auto caster_ray=document_->level().lighting().sampled_at_tick(
        lighting_cycle_preview_enabled_?lighting_cycle_preview_tick_:0U).shadow_ray_direction();
    const float shadow_caster_margin=projected_object_shadows_?
        std::clamp(8.0F*std::hypot(caster_ray.x,caster_ray.y)/
                   std::max(0.05F,std::abs(caster_ray.z)),2.0F,128.0F):2.0F;
    const auto model_placement_visible=[&](Vec3f center,float radius){
        const bool visible=placement_visible_in_canvas(center,radius);
        cache.views.push_back({center,{},radius,false,visible});return visible;
    };
    const auto model_bounds_visible=[&](const AxisAlignedBounds& bounds,float padding){
        const bool visible=bounds_visible_in_canvas(bounds,padding);
        cache.views.push_back({{},bounds,padding,true,visible});return visible;
    };
    bool recording_models=false;
    std::size_t cached_vertex_cursor=0U,cached_index_cursor=0U;
    const auto record_model_step=[&](std::optional<DynamicModelEffect> effect=std::nullopt){
        cache.steps.push_back({cached_vertex_cursor,next.vertices.size(),cached_index_cursor,
            next.indices.size(),std::move(effect)});
    };
    uint32_t next_effect_sort_group=1U;
    const auto append_draw_run=[&](PrimitiveBlendPreset blend,bool two_sided,uint32_t first_index,
                                   uint32_t sort_group=0U,
                                   RetailWorldModelPass retail_world_pass=RetailWorldModelPass::PostGeneratedShading){
        if(!next.draw_runs.empty()){
            auto& previous=next.draw_runs.back();
            if(previous.blend==blend&&previous.two_sided==two_sided&&
               previous.sort_group==sort_group&&previous.retail_world_pass==retail_world_pass&&
               previous.first_index+previous.index_count==first_index){
                previous.index_count+=3U;return;
            }
        }
        next.draw_runs.push_back(ObjectDrawRun{blend,two_sided,first_index,3U,sort_group,retail_world_pass});
    };

    using EffectRuntimeAngles=std::array<uint32_t,3>; // {worldRotationAngle0,1,2}
    using EffectAppendFn=std::function<bool(const thandor::EffectDefinitionRecord&,Vec3f,
        uint32_t,float,uint32_t,const ModelHierarchyTransform*,std::optional<uint32_t>,uint32_t,
        uint32_t,std::optional<float>,std::optional<EffectRuntimeAngles>)>;
    using LinkedShotAppendFn=std::function<bool(const thandor::ShotDefinitionRecord&,Vec3f,Vec3f,
        uint32_t,uint32_t,uint32_t,uint32_t)>;
    bool emitting_command_overlay=false;
    std::optional<uint32_t> selected_lab_shot_model_tint;
    std::optional<uint32_t> selected_lab_effect_model_tint;
    bool rendering_independent_lab_effect=false;
    EffectAppendFn append_effect_visual_recursive;
    LinkedShotAppendFn append_linked_shot_visual_recursive;
    std::size_t recursive_effect_spawn_count=0U;
    constexpr std::size_t kMaximumRecursiveEffectSpawns=4096U;
    uint32_t recursive_effect_root_id=0U;
    const auto consume_recursive_effect_spawn=[&]()->bool{
        if(recursive_effect_spawn_count<kMaximumRecursiveEffectSpawns){
            ++recursive_effect_spawn_count;
            return true;
        }
        // Optional preview data must not take down the editor. Stop this root's
        // expansion, keep the geometry already emitted, and report once per owner.
        report_asset_failure("effect-preview/"+std::to_string(recursive_effect_root_id),
            "EFF/SHT preview expansion exceeded 4096 descendants; remaining children skipped");
        return false;
    };
    append_effect_visual_recursive=[&](const thandor::EffectDefinitionRecord& definition,
                                         Vec3f position,uint32_t age_ticks,float yaw_radians,
                                         uint32_t orientation_random,
                                         const ModelHierarchyTransform* authored_orientation,
                                         std::optional<uint32_t> forced_frame,
                                         uint32_t texture_faction,uint32_t recursion_depth,
                                         std::optional<float> mode0_beam_length,
                                         std::optional<EffectRuntimeAngles> exact_runtime_angles)->bool{
        const uint32_t lifetime=std::max(1U,thandor::effect_lifetime_ticks(definition));
        const auto& sprite=cached_sprite(definition.sprite_path);
        const auto& mesh=sprite.mesh();
        if(mesh.indices.size()%3U!=0U)throw std::runtime_error("EFF preview mesh is not triangle aligned");
        const Vec3f base_position=position;
        ModelHierarchyTransform base_authored;
        if(exact_runtime_angles){
            // EffectRuntimePool_CreateInstanceFromDefinitionCf stores the runtime fields as
            // {worldRotationAngle0=arg2, worldRotationAngle1=arg1, worldRotationAngle2=arg0} and
            // rebuilds the root transform directly in world space. Damage-state effects pass
            // explicit binary-derived angles here rather than inheriting their army parent's basis.
            base_authored=fixed_model_rotation_basis((*exact_runtime_angles)[2],
                (*exact_runtime_angles)[1],(*exact_runtime_angles)[0]);
            base_authored.translation=position;
        }else{
            base_authored=authored_orientation!=nullptr
                ?*authored_orientation
                :placement_root_transform(position.x,position.y,position.z,yaw_radians);
        }
        if((definition.creation_flags&1U)!=0U&&!exact_runtime_angles&&
           !(rendering_independent_lab_effect&&recursion_depth==0U)){
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
            const auto displacement=exact_runtime_angles
                ?thandor::effect_transition2_displacement_at_age_angles(definition,pose_age,
                    (*exact_runtime_angles)[0],(*exact_runtime_angles)[1])
                :thandor::effect_transition2_displacement_at_age(definition,pose_age,
                    orientation_random);
            ModelHierarchyTransform pose=base_authored;
            if(exact_runtime_angles&&definition.transition_kind==2U){
                const uint32_t pose_angle1=thandor::effect_transition2_world_rotation_angle1_at_pose_age(
                    definition,pose_age,(*exact_runtime_angles)[1]);
                pose=fixed_model_rotation_basis((*exact_runtime_angles)[2],pose_angle1,
                    (*exact_runtime_angles)[0]);
            }
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
        const bool effect_currently_visible=fog_preview_mode_==0||
            fog_preview_point_visible(pose_and_scale_at(age_ticks).first.translation);
        if(age_ticks<lifetime&&!mesh.indices.empty()&&(effect_currently_visible||emitting_command_overlay)){
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
                    (army_graphics?object_layer(0U,graphics_faction,texture_subresource):
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
                    // V530 0x0051E850 lifecycle tint: normal effect transitions use the
                    // runtime fade ARGB and EFF +0x60 through the exact MMX modulation path.
                    // The editor does not yet reconstruct the live terrain-occupancy runtime flag
                    // for every transient effect, so ordinary preview uses the visible 0x04 state;
                    // transition kind 4 retains its contact-driven alpha path below.
                    uint32_t packed_color=definition.state_tint_argb;
                    if(const auto retail_tint=thandor::retail_effect_model_tint_argb(
                           definition,age_ticks,0x04U))
                        packed_color=*retail_tint;
                    else{
                        const uint32_t serialized_alpha=(packed_color>>24U)&0xFFU;
                        const uint32_t base_alpha=serialized_alpha==0U?255U:serialized_alpha;
                        const uint32_t combined_alpha=(base_alpha*runtime_alpha+127U)/255U;
                        packed_color=(packed_color&0x00FFFFFFU)|(combined_alpha<<24U);
                    }
                    // The Lab projectile already carries the native shot model's
                    // phase-3/7 terrain visibility and MMX definition modulation.
                    // Only the synthetic SHT model uses this override; nested EFF
                    // children retain their own lifecycle tint and alpha.
                    if(selected_lab_shot_model_tint && !emitting_command_overlay && recursion_depth==0U &&
                       (definition.definition_id&0x80000000U)!=0U)
                        packed_color=*selected_lab_shot_model_tint;
                    // Only an independently linked Lab EFF owner has this tint.
                    // Never use it for editor command overlays or nested EFF children.
                    if(selected_lab_effect_model_tint && !emitting_command_overlay && recursion_depth==0U)
                        packed_color=*selected_lab_effect_model_tint;
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

        // Transition states 0 and 1 decrement EFF +0x10 only when the Q4 animation accumulator
        // completes a sprite frame. The terminal frame unlinks before transition dispatch, so it
        // cannot emit a linked effect. Invisible controller SPR records still execute this branch.
        if(recursion_depth<8U){
            if(const auto linked_effect_tick=thandor::effect_linked_effect_spawn_tick(definition);
               linked_effect_tick.has_value()&&age_ticks>=*linked_effect_tick){
                const uint32_t spawn_tick=*linked_effect_tick;
                if(const auto* child=catalog_.effect_definition(definition.linked_effect_id)){
                    if(const auto emitter=attachment_world_at(3U,0U,spawn_tick)){
                        if(!consume_recursive_effect_spawn())return emitted;
                        auto [child_orientation,ignored_scale]=pose_and_scale_at(spawn_tick);
                        (void)ignored_scale;
                        child_orientation.translation=*emitter;
                        emitted|=append_effect_visual_recursive(*child,*emitter,age_ticks-spawn_tick,
                            yaw_radians,orientation_random+spawn_tick,&child_orientation,std::nullopt,
                            texture_faction,recursion_depth+1U,std::nullopt,std::nullopt);
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
                        if(!consume_recursive_effect_spawn())return emitted;
                        ModelHierarchyTransform upright=fixed_model_rotation_basis(0U,0x4000U,0U);
                        upright.translation=*emitter;
                        emitted|=append_effect_visual_recursive(*child,*emitter,age_ticks-spawn,0.0F,
                            orientation_random+spawn,&upright,std::nullopt,texture_faction,
                            recursion_depth+1U,std::nullopt,std::nullopt);
                    }
                    if(lifetime-spawn<interval)break;
                }
            }
        }

        // Transition state 1 independently reloads the EFF +0x18 counter every maintenance
        // tick and advances through kind-2 attachments. The emitted SHT runtime survives the
        // parent controller, so historical emissions are reconstructed after parent expiry.
        if(!simulation_mode_&&!simulation_lab_active()&&recursion_depth<8U&&definition.transition_kind==1U&&
           definition.linked_shot_present!=0U){
            if(const auto* shot=catalog_.shot_definition(definition.linked_shot_id)){
                for(const auto& emission:thandor::build_linked_shot_emissions(definition)){
                    if(emission.parent_age_tick>age_ticks)break;
                    const auto [source_pose,source_scale]=pose_and_scale_at(emission.parent_age_tick);
                    const auto emitter=attachment_world_at(2U,emission.attachment_index,
                        emission.parent_age_tick);
                    if(!emitter)continue;
                    if(!consume_recursive_effect_spawn())return emitted;
                    const auto linked_geometry=thandor::simulation_linked_effect_shot_geometry(
                        source_pose.translation,*emitter);
                    emitted|=append_linked_shot_visual_recursive(*shot,
                        linked_geometry.launch_position,linked_geometry.aim_position,
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
        const auto& shot_sprite=cached_sprite(shot.sprite_path);
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
                &launch_orientation,std::nullopt,texture_faction,recursion_depth+1U,std::nullopt,std::nullopt);
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
            flight.runtime_value_0c=std::max(1U,shot.projectile_lifetime_ticks);
            flight.frame_advance_threshold_q4=16U;
            flight.state_tint_argb=shot.state_tint_argb==0U?0xFFFFFFFFU:shot.state_tint_argb;
            flight.model_scale_x_q12=0x1000;flight.model_scale_y_q12=0x1000;
            flight.sprite_path=shot.sprite_path;
            const std::optional<float> beam_length=shot.trajectory_mode==0
                ?std::optional<float>{thandor::simulation_mode0_beam_length_world(projectile,shot)}
                :std::nullopt;
            emitted|=append_effect_visual_recursive(flight,position,age_ticks,yaw,stable_seed,
                &flight_orientation,thandor::simulation_shot_frame_index(shot,age_ticks),
                texture_faction,recursion_depth+1U,beam_length,std::nullopt);
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
                    if(!consume_recursive_effect_spawn())return emitted;
                    emitted|=append_effect_visual_recursive(*trail,trail_position,age_ticks-spawn,0.0F,
                        stable_seed+spawn,&upright,std::nullopt,texture_faction,
                        recursion_depth+1U,std::nullopt,std::nullopt);
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
                    stable_seed,&upright,std::nullopt,texture_faction,recursion_depth+1U,std::nullopt,std::nullopt);
            }
        }
        return emitted;
    };

    const auto append_effect_visual=[&](const thandor::EffectDefinitionRecord& definition,
                                         Vec3f position,uint32_t age_ticks,float yaw_radians,
                                         uint32_t orientation_random=0U,
                                         const ModelHierarchyTransform* authored_orientation=nullptr,
                                         std::optional<uint32_t> forced_frame=std::nullopt,
                                         uint32_t texture_faction=0U,
                                         std::optional<EffectRuntimeAngles> exact_runtime_angles=std::nullopt)->bool{
        if(recording_models){
            DynamicModelEffect request{&definition,position,age_ticks,orientation_random,
                texture_faction,yaw_radians,
                authored_orientation?std::optional<ModelHierarchyTransform>(*authored_orientation):std::nullopt,
                forced_frame,exact_runtime_angles};
            record_model_step(std::move(request));
        }
        // This is a per-root transition-chain guard, not a limit on all effects
        // in a frame. Stock lava maps legitimately emit thousands of descendants
        // from many independent controllers, including during cached model replay.
        recursive_effect_spawn_count=0U;
        recursive_effect_root_id=definition.definition_id;
        const bool emitted=append_effect_visual_recursive(definition,position,age_ticks,yaw_radians,
            orientation_random,authored_orientation,forced_frame,texture_faction,0U,std::nullopt,
            exact_runtime_angles);
        if(recording_models){cached_vertex_cursor=next.vertices.size();cached_index_cursor=next.indices.size();}
        return emitted;
    };

    const auto placements=document_->level().placements();
    if(reuse_models){
        ++dynamic_model_cache_hits_;
        visible_animated_buildings_=cache.visible_buildings;
        simulation_lab_render_receipts_=cache.receipts;
        // All cached steps without effect billboards are already contiguous and have
        // correct indices and draw runs. Copy the unsorted mesh once, then perform only
        // the camera-dependent transparency ordering below. This also avoids scanning
        // every draw run for every cached model step.
        const bool plain_cached_geometry=std::none_of(cache.steps.begin(),cache.steps.end(),
            [](const DynamicModelStep& step){return step.effect.has_value();});
        if(plain_cached_geometry){
            ++dynamic_plain_cache_hits_;
            if(cache.scene.vertices.size()>kMaximumDynamicVertices||
               cache.scene.indices.size()>kMaximumDynamicIndices)
                throw std::runtime_error("cached simulation geometry limit exceeded");
            next.vertices=cache.scene.vertices;
            next.indices=cache.scene.indices;
            next.draw_runs=cache.scene.draw_runs;
        }else{
        next.vertices.reserve(cache.scene.vertices.size());next.indices.reserve(cache.scene.indices.size());
        const auto append_cached_run=[&](ObjectDrawRun run){
            if(!next.draw_runs.empty()){
                auto& previous=next.draw_runs.back();
                if(previous.blend==run.blend&&previous.two_sided==run.two_sided&&
                   previous.sort_group==run.sort_group&&previous.retail_world_pass==run.retail_world_pass&&
                   previous.first_index+previous.index_count==run.first_index){
                    previous.index_count+=run.index_count;return;
                }
            }
            next.draw_runs.push_back(run);
        };
        for(const auto& step:cache.steps){
            const auto base=next.vertices.size(),first=next.indices.size();
            if(base+step.end_vertex-step.first_vertex>kMaximumDynamicVertices||
               first+step.end_index-step.first_index>kMaximumDynamicIndices)
                throw std::runtime_error("cached simulation geometry limit exceeded");
            next.vertices.insert(next.vertices.end(),cache.scene.vertices.begin()+step.first_vertex,
                cache.scene.vertices.begin()+step.end_vertex);
            for(std::size_t i=step.first_index;i<step.end_index;++i)
                next.indices.push_back(static_cast<uint32_t>(base+cache.scene.indices[i]-step.first_vertex));
            for(auto run:cache.scene.draw_runs){
                const auto begin=std::max<std::size_t>(run.first_index,step.first_index);
                const auto end=std::min<std::size_t>(run.first_index+run.index_count,step.end_index);
                if(begin>=end)continue;
                run.first_index=static_cast<uint32_t>(first+begin-step.first_index);
                run.index_count=static_cast<uint32_t>(end-begin);append_cached_run(run);
            }
            for(std::size_t i=0;i<cache.receipts.size();++i){
                const auto& original=cache.receipts[i];
                if(original.bounds_valid&&original.first_vertex>=step.first_vertex&&
                   original.first_vertex<step.end_vertex)
                    simulation_lab_render_receipts_[i].first_vertex=static_cast<uint32_t>(
                        base+original.first_vertex-step.first_vertex);
            }
            if(step.effect){
                const auto& e=*step.effect;
                append_effect_visual(*e.definition,e.position,e.age,e.yaw,e.orientation_random,
                    e.orientation?&*e.orientation:nullptr,e.forced_frame,e.texture_faction,e.exact_angles);
            }
        }
        } // Step-by-step rebuilding remains required when camera-dependent effects exist.
        // Model shading emitters are world-space and independent of the view.
        next.shading_emitters=cache.scene.shading_emitters;
    }else{
    ++dynamic_model_cache_misses_;
    recording_models=true;
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
    if(simulation_mode_||simulation_lab_active()||fog_preview_mode_!=0||
       !building_destruction_states_.empty()){
        dynamic_placements.reserve(dynamic_placements.size()+simulation_actor_placements_.size());
        for(const auto index:simulation_actor_placements_){
            if(index>=placements.size())continue;
            const auto* preset=catalog_.army_preset(placements[index].army_asset_id);
            if(!preset)continue;
            const bool is_building=preset->browser_kind==thandor::PlacementBrowserKind::Building;
            const bool is_unit=preset->browser_kind==thandor::PlacementBrowserKind::Unit ||
                (preset->runtime_model_class_id==18U &&
                 preset->visual_kind==thandor::PlacementVisualKind::PlayerStart);
            const bool destruction_dynamic_replacement=building_destruction_states_.contains(index);
            const thandor::SimulationRenderPolicy render_policy{is_building,is_unit,
                preset->has_model_subnode_animation,(simulation_mode_||simulation_lab_active())||
                    fog_preview_mode_!=0||destruction_dynamic_replacement,
                simulation_actor_states_.contains(index)||fog_preview_mode_!=0||destruction_dynamic_replacement,
                preset->has_continuous_radar_subnode_animation};
            if(fog_preview_mode_!=0||destruction_dynamic_replacement||
               render_policy.dynamic_replacement_enabled(animate_buildings_,animate_units_))
                append_dynamic_placement(index,is_building);
        }
    }
    for(const auto& [placement_index,is_building]:dynamic_placements){
        if(placement_index>=placements.size())continue;
        const auto& placement=placements[placement_index];
        const auto* preset=catalog_.army_preset(placement.army_asset_id);
        if(preset==nullptr)continue;
        if(const auto destruction=building_destruction_states_.find(placement_index);
           destruction!=building_destruction_states_.end()){
            const auto* definition=destruction->second.effect_id==0U?nullptr:
                catalog_.effect_definition(destruction->second.effect_id);
            const uint32_t lifetime=definition?thandor::effect_lifetime_ticks(*definition):0U;
            const uint32_t age=runtime_visual_tick>=destruction->second.start_tick
                ?runtime_visual_tick-destruction->second.start_tick:0U;
            // Retail deconstruction keeps the hierarchy until the +0x190 effect completion
            // callback destroys it. A null/zero-lifetime definition therefore removes immediately.
            if(lifetime==0U||age>=lifetime)continue;
        }
        const bool resource_support_active=placement_has_required_resource_support(*preset,placement);
        const bool preset_is_building=
            preset->browser_kind==thandor::PlacementBrowserKind::Building;
        if(is_building!=preset_is_building)continue;
        Vec3f root_position{placement.render_x(),placement.render_y(),
            terrain_height_near(placement.render_x(),placement.render_y())};
        uint32_t root_yaw=placement.orientation_turn16&0xFFFFU;
        thandor::SimulationActorState* simulation_state=nullptr;
        if(const auto found=simulation_actor_states_.find(placement_index);
           found!=simulation_actor_states_.end()&&
           (simulation_mode_||simulation_lab_active()||preset->runtime_model_class_id==17U)){
            simulation_state=&found->second;
            if(simulation_mode_||simulation_lab_active()){
                root_position=simulation_state->position;
                root_position.z=terrain_height_near(root_position.x,root_position.y);
                root_yaw=simulation_state->body_yaw_turn16;
            }
        }
        std::span<const thandor::ModelVisualNode> hierarchy=placement_hierarchy(*preset,placement_index);
        const thandor::SimulationLabEntity* lab_runtime=nullptr;
        if(simulation_lab_active()) {
            const auto runs=simulation_lab_runs();
            if(!runs.empty()) {
                const auto run_index=static_cast<size_t>(std::clamp(simulation_lab_selected_run_,0,static_cast<int>(runs.size())-1));
                for(const auto& candidate:runs[run_index].entities)
                    if(!candidate.synthetic&&candidate.source_placement_index==placement_index) {
                        lab_runtime=&candidate;
                        if(candidate.runtime_model_hierarchy)hierarchy=*candidate.runtime_model_hierarchy;
                        break;
                    }
            }
        }
        if(lab_runtime&&lab_runtime->runtime_removed)continue;
        const auto fog_root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){
            return node.parent_index==std::numeric_limits<uint32_t>::max();});
        const uint32_t fog_flags=fog_root==hierarchy.end()?preset->model_runtime_render_flags:
            fog_root->simulation.runtime_render_flags;
        const uint32_t fog_intensity=fog_preview_model_intensity(root_position,fog_flags);
        if(fog_intensity==0U)continue;
        const uint32_t fog_tint=0xFF000000U|(fog_intensity*0x010101U);
        const bool placement_visible=simulation_state!=nullptr
            ?model_placement_visible(add(root_position,{0.0F,0.0F,4.0F}),std::max(24.0F,shadow_caster_margin))
            :(placement_index<placement_pick_bounds_.size()&&
              placement_pick_bounds_[placement_index].valid()
                ?model_bounds_visible(placement_pick_bounds_[placement_index],shadow_caster_margin)
                :model_placement_visible(add(root_position,{0.0F,0.0F,4.0F}),std::max(24.0F,shadow_caster_margin)));
        if(!placement_visible)continue;
        if(is_building)visible_animated_buildings_.push_back(placement_index);
        std::vector<thandor::ModelVisualNode> fallback_nodes;

        std::vector<thandor::SimulationWeaponProfile> lab_weapon_profiles;
        if(lab_runtime)for(const auto& weapon:lab_runtime->combat_weapons)lab_weapon_profiles.push_back(weapon.profile);
        const auto weapon_profiles=lab_runtime?std::span<const thandor::SimulationWeaponProfile>(lab_weapon_profiles)
            :placement_weapon_profiles(*preset,placement_index);
        if(hierarchy.empty()){
            thandor::ModelVisualNode fallback;
            fallback.parent_index=std::numeric_limits<uint32_t>::max();
            fallback.local_rotation_angle1=0x4000U;
            fallback.sprite_path=preset->sprite_path;
            fallback_nodes.push_back(std::move(fallback));
            hierarchy=fallback_nodes;
        }
        const uint32_t effective_runtime_faction=lab_runtime
            ?std::min(lab_runtime->faction_index,7U)
            :runtime_faction_for_placement(placement_index,placement.faction_index);
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
            bool node_active=node.creates_runtime_node()&&!node.sprite_path.empty()&&(!lab_runtime||thandor::simulation_lab_model_node_present(*lab_runtime,node_index));
            const thandor::SpriteAsset* node_sprite=node_active?&cached_sprite(node.sprite_path):nullptr;
            if(instance.root){
                uint32_t dynamic_root_yaw=root_yaw;
                int32_t root_recoil_angle=0;
                if(simulation_state){
                    for(std::size_t weapon_index=0;
                        weapon_index<weapon_profiles.size()&&
                        weapon_index<simulation_state->weapons.size();++weapon_index){
                        const auto& profile=weapon_profiles[weapon_index];
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
                            thandor::turn16_to_radians(dynamic_root_yaw),contact.normal,contact.packed_normal_angles)
                        :placement_root_transform(root_position.x,root_position.y,root_position.z,
                            thandor::turn16_to_radians(dynamic_root_yaw)));
            }else{
                if(node.parent_index>=instances.size()||!active[node.parent_index])node_active=false;
                if(node_active&&!thandor::model_runtime_world_child_enabled(
                       hierarchy[node.parent_index].runtime_class_id,node.child_slot))
                    node_active=false;
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
                            weapon_index<weapon_profiles.size()&&
                            weapon_index<simulation_state->weapons.size();++weapon_index){
                            const auto& profile=weapon_profiles[weapon_index];
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
                        resource_support_active&&
                        ((is_building&&animate_buildings_)||(!is_building&&animate_units_));
                    uint32_t authored_animation_tick=is_building
                        ?current_building_animation_tick_:runtime_visual_tick;
                    const auto& authored_parent_node=hierarchy[node.parent_index];
                    if(authored_parent_node.runtime_class_id==11U||authored_parent_node.runtime_class_id==13U||
                       authored_parent_node.runtime_class_id==22U){
                        authored_animation_tick=0U;
                        if(simulation_lab_active()){
                            const auto runs=simulation_lab_runs();
                            const auto run_index=runs.empty()?0U:static_cast<std::size_t>(std::clamp(
                                simulation_lab_selected_run_,0,static_cast<int>(runs.size())-1));
                            if(!runs.empty())for(const auto& entity:runs[run_index].entities)
                                if(!entity.synthetic&&entity.source_placement_index==placement_index){
                                    authored_animation_tick=entity.production_animated_subnode_ticks;break;
                                }
                        }else if(authored_parent_node.runtime_class_id==13U){
                            if(const auto factory=simulation_factory_states_.find(placement_index);
                               factory!=simulation_factory_states_.end())
                                authored_animation_tick=factory->second.animated_subnode_ticks;
                        }else if(authored_parent_node.runtime_class_id==22U){
                            if(const auto airport=simulation_airport_states_.find(placement_index);
                               airport!=simulation_airport_states_.end())
                                authored_animation_tick=airport->second.animated_subnode_ticks;
                        }
                    }
                    const bool direct_animated_parent=
                        thandor::runtime_class_directly_dispatches_animated_model_subnodes(
                            authored_parent_node.runtime_class_id);
                    const int32_t authored_yaw_step=
                        thandor::model_runtime_child_yaw_step_turn16(
                            authored_parent_node,node.child_slot);
                    if(!simulated_node&&authored_yaw_step!=0&&
                       (direct_animated_parent||animate_authored_subnodes)&&
                       !thandor::simulation_model_node_is_weapon_aim_axis(
                           weapon_profiles,static_cast<uint32_t>(node_index))){
                        const uint32_t animation_tick=direct_animated_parent
                            ?runtime_visual_tick:authored_animation_tick;
                        angle2=thandor::wrap_turn16(static_cast<int64_t>(angle2)+
                            static_cast<int64_t>(authored_yaw_step)*animation_tick);
                    }else if(preset->runtime_uses_class17_root_child_deployment&&parent.root&&
                       node.child_slot<3U&&simulation_state!=nullptr){
                        angle1=thandor::wrap_turn16(static_cast<int64_t>(angle1)+
                            simulation_state->class17_rotor_tilt_turn16);
                        simulated_node=true;
                    }
                    auto local=fixed_model_rotation_basis(angle2,angle1,node.local_rotation_angle0);
                    local.translation=attachment->local_translation;
                    if(const auto z=simulation_storage_child_z(hierarchy[node.parent_index],node.child_slot,
                        effective_runtime_faction))local.translation.z=*z;
                    if(recoil_translation_q12!=0){
                        const Vec3f recoil=transform_vector(local,
                            {thandor::q12_to_float(recoil_translation_q12),0.0F,0.0F});
                        local.translation=add(local.translation,recoil);
                    }
                    if((direct_animated_parent||animate_authored_subnodes)&&
                       thandor::model_runtime_child_uses_bounded_vertical_channel(
                           authored_parent_node,node.child_slot))
                        local.translation.z=animated_child2_translation_q12(
                            authored_parent_node.simulation,
                            direct_animated_parent?runtime_visual_tick:authored_animation_tick,
                            local.translation.z);
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
        const uint32_t runtime_faction=neutral?0U:effective_runtime_faction;
        const uint32_t graphics_family=neutral?0U:
            graphics_family_for_faction(effective_runtime_faction);
        const auto& family=texture_family(graphics_family);
        thandor::ResourceEnergyKind dynamic_resource_kind=thandor::ResourceEnergyKind::None;
        if(preset->visual_kind==thandor::PlacementVisualKind::PowerResource)
            dynamic_resource_kind=thandor::ResourceEnergyKind::Power;
        else if(preset->visual_kind==thandor::PlacementVisualKind::BuildingResource)
            dynamic_resource_kind=thandor::ResourceEnergyKind::Building;
        const uint32_t dynamic_resource_body=
            thandor::resource_body_subresource(dynamic_resource_kind);
        const uint32_t dynamic_resource_particle=
            thandor::resource_luminous_particle_subresource(dynamic_resource_kind);
        for(std::size_t instance_index=0;instance_index<instances.size();++instance_index){
            if(!active[instance_index])continue;
            const auto& instance=instances[instance_index];
            const auto& mesh=instance.sprite->mesh();
            ModelVertexLightingCache vertex_lighting(instance.transform,prepared_directional_light,ramp);
            for(std::size_t triangle=0;triangle<mesh.indices.size();triangle+=3U){
                const std::array<const thandor::SpriteMeshVertex*,3> corners{
                    &mesh.vertices.at(mesh.indices[triangle]),&mesh.vertices.at(mesh.indices[triangle+1U]),
                    &mesh.vertices.at(mesh.indices[triangle+2U])};
                uint32_t visible_mesh_group_mask=0xFFFFFFFFU;
                if(simulation_state!=nullptr){
                    for(std::size_t weapon_index=0;
                        weapon_index<weapon_profiles.size()&&
                        weapon_index<simulation_state->weapons.size();++weapon_index){
                        const auto& profile=weapon_profiles[weapon_index];
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
                const bool dynamic_resource_body_triangle=
                    dynamic_resource_kind!=thandor::ResourceEnergyKind::None&&
                    subresource==dynamic_resource_body;
                const bool dynamic_resource_particle_triangle=
                    dynamic_resource_kind!=thandor::ResourceEnergyKind::None&&
                    subresource==dynamic_resource_particle;
                const auto resource_role=dynamic_resource_body_triangle
                    ?ResourcePrimitiveRole::DenseBody
                    :(dynamic_resource_particle_triangle?ResourcePrimitiveRole::LuminousParticle:
                      ResourcePrimitiveRole::None);
                const auto primitive=dynamic_resource_kind==thandor::ResourceEnergyKind::None
                    ?classify_directdraw_direct3d_sprite_primitive(flags,!untextured)
                    :classify_resource_preview_primitive(flags,!untextured,resource_role);
                if(!primitive.supported)continue;
                if(next.vertices.size()+3U>kMaximumDynamicVertices||next.indices.size()+3U>kMaximumDynamicIndices)
                    throw std::runtime_error("visible simulation geometry limit exceeded");
                const uint32_t base=static_cast<uint32_t>(next.vertices.size());
                for(const auto* source:corners){
                    const uint32_t layer=untextured?untextured_layer():object_layer(runtime_faction,graphics_family,source->texture_subresource);
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
                        if(preset->runtime_model_class_id==13U&&
                           source->texture_subresource==preset->primary_animated_subresource){
                            if(const auto factory=simulation_factory_states_.find(placement_index);
                               factory!=simulation_factory_states_.end())
                                uv.v+=thandor::normalize_model_texture_offset_v(
                                    factory->second.primary_texture_offset_v,
                                    metadata.pixel_width,metadata.pixel_height);
                        }else if(preset->runtime_model_class_id==22U&&
                                 source->texture_subresource==preset->primary_animated_subresource){
                            if(const auto airport=simulation_airport_states_.find(placement_index);
                               airport!=simulation_airport_states_.end())
                                uv.v+=thandor::normalize_model_texture_offset_v(
                                    airport->second.primary_texture_offset_v,
                                    metadata.pixel_width,metadata.pixel_height);
                        }
                    }
                    const Vec3f world=transform_point(instance.transform,source->position);
                    const auto lighting=vertex_lighting.sample(source->normal);
                    const Vec3f normal=lighting.normal;
                    const uint32_t directional=lighting.directional_color;
                    uint32_t modulation=0xFFFFFFFFU;
                    if(untextured){
                        const uint32_t palette_index=sprite_palette_index(source->render_flags);
                        if(palette_index<family.palette.colors_argb().size())modulation=family.palette.colors_argb()[palette_index];
                    }
                    const uint32_t packed=thandor::multiply_rgb_opaque(directdraw_select_model_vertex_packed_color(flags,
                        modulation|0xFF000000U,thandor::multiply_rgb_opaque(directional,modulation)),fog_tint);
                    const uint32_t direct=directdraw_model_vertex_uses_direct_tint(flags)?1U:0U;
                    const bool animated_part=is_building&&instance.root&&preset->has_primary_texture_animation&&
                        preset->runtime_model_class_id!=11U&&preset->runtime_model_class_id!=13U&&
                        preset->runtime_model_class_id!=22U&&
                        source->texture_subresource==preset->primary_animated_subresource;
                    next.vertices.push_back(ObjectVertex{world,normal,uv.u,uv.v,layer,packed,direct,
                        animated_part?0.5F:0.0F,animated_part?80.0F:0.0F,(preset->casts_generated_shadow&&primitive.blend!=PrimitiveBlendPreset::AdditiveNoDepthWrite)?1U:0U});
                }
                const uint32_t first=static_cast<uint32_t>(next.indices.size());
                next.indices.insert(next.indices.end(),{base,base+1U,base+2U});
                append_draw_run(primitive.blend,primitive.two_sided,first,0U,
                    retail_world_model_pass_from_mdl_flags(preset->model_runtime_render_flags));
            }
        }
    }

    const auto runtime_shadow_owner=[&](const thandor::SimulationLabEntity* entity,
        uint64_t serial,bool linked)->RuntimeShadowOwner{
        if(entity){
            const auto runs=simulation_lab_runs();
            const uint32_t run=static_cast<uint32_t>(std::clamp(simulation_lab_selected_run_,0,
                static_cast<int>(runs.size())-1));
            return {RuntimeShadowSource::LabEntity,
                static_cast<uint64_t>(entity-runs[run].entities.data()),run};
        }
        return {linked?RuntimeShadowSource::LinkedChild:RuntimeShadowSource::Produced,serial,0U};
    };
    const auto collect_runtime_shadow=[&](RuntimeShadowOwner owner,const thandor::ArmyAssetPreset& preset,
        const auto& instances,const auto& active,Vec3f anchor,uint32_t yaw,uint32_t flags){
        if(!runtime_shadow_collector_||!preset.casts_generated_shadow||(flags&0x40U)!=0U||
           preset.visual_kind==thandor::PlacementVisualKind::PowerResource||
           preset.visual_kind==thandor::PlacementVisualKind::BuildingResource)return;
        std::vector<thandor::GeneratedShadowTriangle> triangles;
        for(std::size_t index=0;index<instances.size();++index){
            if(index>=active.size()||!active[index]||!instances[index].sprite)continue;
            append_runtime_shadow_triangles(instances[index].sprite->generated_shadow_mesh(),
                instances[index].transform,triangles);
        }
        if(!triangles.empty())runtime_shadow_collector_(owner,anchor,yaw,flags,triangles);
    };

    // Class-13 produced units are live runtime children, not LEV placements. Render them through
    // the stock ARM/MDL/SPR hierarchy while they drive out, including class-2 track UVs, class-3
    // articulated support pose, and class-17 bank/deployment state.
    const auto append_runtime_transient_model=[&](uint32_t army_asset_id,uint32_t faction_index,
                                                  thandor::SimulationActorState& actor,
                                                  const SimulationLinkedChildEntity* linked_child,
                                                  int32_t maximum_integrity,int32_t integrity,
                                                  uint64_t transient_serial,
                                                  const thandor::FactionTechnologyMask256* technology_mask,
                                                  const thandor::SimulationLabEntity* lab_runtime=nullptr,
                                                  SimulationLabRenderReceipt* receipt=nullptr)->bool{
        const auto start_index=next.indices.size();
        const auto result=[&](const char* reason,bool accepted){
            if(receipt){
                receipt->result=reason;
                record_render_receipt_geometry(*receipt,next.vertices,next.indices,start_index);
            }
            return accepted;
        };
        if(lab_runtime&&lab_runtime->runtime_removed)return result("removed",true);
        if(faction_index==0U)return result("invalid_faction",false);
        const auto* preset=catalog_.army_preset(army_asset_id);
        if(preset==nullptr||preset->sprite_path.empty())return result("missing_model",false);
        const bool in_canvas=model_placement_visible(add(actor.position,{0.0F,0.0F,4.0F}),std::max(24.0F,shadow_caster_margin));
        if(!in_canvas&&!runtime_shadow_collector_)return result("outside_canvas",true);
        const auto& resolved_mask=technology_mask?*technology_mask:
            document_->faction_technology_mask(std::min(faction_index,7U));
        std::vector<thandor::ModelVisualNode> resolved;
        if(!lab_runtime||!lab_runtime->runtime_model_hierarchy)
            resolved=thandor::resolve_faction_technology_hierarchy(*preset,resolved_mask);
        std::vector<thandor::ModelVisualNode> fallback_nodes;
        std::span<const thandor::ModelVisualNode> hierarchy=!resolved.empty()
            ?std::span<const thandor::ModelVisualNode>(resolved)
            :(preset->resolved_model_hierarchy.empty()
                ?std::span<const thandor::ModelVisualNode>(preset->model_hierarchy)
                :std::span<const thandor::ModelVisualNode>(preset->resolved_model_hierarchy));
        if(lab_runtime&&lab_runtime->runtime_model_hierarchy)hierarchy=*lab_runtime->runtime_model_hierarchy;
        if(hierarchy.empty()){
            thandor::ModelVisualNode fallback;
            fallback.parent_index=std::numeric_limits<uint32_t>::max();
            fallback.local_rotation_angle1=0x4000U;
            fallback.sprite_path=preset->sprite_path;
            fallback_nodes.push_back(std::move(fallback));
            hierarchy=fallback_nodes;
        }
        const auto fog_root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){
            return node.parent_index==std::numeric_limits<uint32_t>::max();});
        const auto fog_flags=fog_root==hierarchy.end()?preset->model_runtime_render_flags:
            fog_root->simulation.runtime_render_flags;
        if(receipt)receipt->root_model_definition=fog_root==hierarchy.end()?preset->model_definition_id:fog_root->model_definition_id;
        const uint32_t fog_intensity=fog_preview_model_intensity(actor.position,fog_flags);
        if(fog_intensity==0U&&!runtime_shadow_collector_)return result("fog_hidden",true);
        const uint32_t fog_tint=0xFF000000U|(fog_intensity*0x010101U);
        auto runtime_weapons=thandor::simulation_lab_render_weapon_profiles(lab_runtime,[&]{
            return catalog_.simulation_weapon_profiles_for_hierarchy(hierarchy);});
        std::vector<ModelHierarchyInstance> instances;
        std::vector<bool> active;
        instances.reserve(hierarchy.size());active.reserve(hierarchy.size());
        for(std::size_t node_index=0;node_index<hierarchy.size();++node_index){
            const auto& node=hierarchy[node_index];
            ModelHierarchyInstance instance{};instance.node=&node;
            instance.root=node.parent_index==std::numeric_limits<uint32_t>::max();
            bool node_active=node.creates_runtime_node()&&!node.sprite_path.empty()&&(!lab_runtime||thandor::simulation_lab_model_node_present(*lab_runtime,node_index));
            const thandor::SpriteAsset* node_sprite=node_active?&cached_sprite(node.sprite_path):nullptr;
            if(instance.root){
                uint32_t runtime_root_yaw=actor.body_yaw_turn16;
                for(std::size_t i=0;i<runtime_weapons.size()&&i<actor.weapons.size();++i)
                    if(runtime_weapons[i].yaw_model_node_index==node_index)
                        runtime_root_yaw=thandor::wrap_turn16(static_cast<int64_t>(runtime_root_yaw)+
                            actor.weapons[i].yaw.angle_turn16);
                const auto contact=placement_contact_pose(*preset,actor.position.x,actor.position.y,
                    node_sprite?node_sprite->placement_height_offset():0.0F);
                const Vec3f root_position{actor.position.x,actor.position.y,contact.height};
                if(preset->runtime_model_class_id==17U){
                    instance.transform=class17_banked_root_transform(root_position,
                        runtime_root_yaw,actor.class17_bank_heading_turn16,
                        actor.class17_bank_angle_turn16,
                        contact.align_to_normal?contact.normal:Vec3f{0.0F,0.0F,1.0F});
                }else{
                    instance.transform=contact.align_to_normal
                        ?terrain_contact_root_transform(root_position,
                            thandor::turn16_to_radians(actor.body_yaw_turn16),contact.normal,contact.packed_normal_angles)
                        :placement_root_transform(root_position.x,root_position.y,root_position.z,
                            thandor::turn16_to_radians(actor.body_yaw_turn16));
                }
            }else{
                if(node.parent_index>=instances.size()||!active[node.parent_index])node_active=false;
                if(node_active&&!thandor::model_runtime_world_child_enabled(
                       hierarchy[node.parent_index].runtime_class_id,node.child_slot))
                    node_active=false;
                if(node_active){
                    const auto& parent=instances[node.parent_index];
                    const auto* attachment=parent.sprite->child_attachment(node.child_slot);
                    if(attachment==nullptr)node_active=false;
                    else{
                        const auto pose=thandor::simulation_runtime_child_pose_v602(
                            hierarchy[node.parent_index],node,node_index,runtime_weapons,actor,
                            animate_units_,runtime_visual_tick,std::nullopt);
                        uint32_t angle1=pose.angle1;
                        if(preset->runtime_uses_class17_root_child_deployment&&parent.root&&node.child_slot<3U)
                            angle1=thandor::wrap_turn16(static_cast<int64_t>(angle1)+
                                actor.class17_rotor_tilt_turn16);
                        uint32_t angle0=pose.angle0;
                        if(linked_child!=nullptr&&parent.root&&node.child_slot==0U){
                            // Runtime class 21 writes root child-0 directly during the outbound,
                            // mission and return arcs.  After landing those angles persist until the
                            // transient child is destroyed; the initial attached pose stays authored.
                            const bool flight_pose=linked_child->runtime.phase!=
                                thandor::SimulationClass21LinkedChildPhase::Attached||linked_child->landed;
                            if(flight_pose){
                                angle0=0x8000U;
                                angle1=linked_child->child0_pitch_turn16;
                            }
                        }
                        auto local=fixed_model_rotation_basis(pose.angle2,angle1,angle0);
                        local.translation=attachment->local_translation;
                            if(const auto z=simulation_storage_child_z(hierarchy[node.parent_index],node.child_slot,faction_index))local.translation.z=*z;
                        if(pose.vertical_animation_tick)
                            local.translation.z=animated_child2_translation_q12(
                                hierarchy[node.parent_index].simulation,*pose.vertical_animation_tick,local.translation.z);
                        if(pose.recoil_translation_q12!=0)
                            local.translation=add(local.translation,transform_vector(local,
                                {thandor::q12_to_float(pose.recoil_translation_q12),0.0F,0.0F}));
                        if(linked_child!=nullptr&&parent.root&&node.child_slot==0U)
                            local.translation.z=thandor::q12_to_float(linked_child->child0_local_z_q12);
                        instance.transform=compose_model_transform(parent.transform,local);
                    }
                }
            }
            if(node_active)instance.sprite=node_sprite;
            active.push_back(node_active);instances.push_back(instance);
        }
        if(receipt)receipt->active_model_nodes=static_cast<uint32_t>(std::count(active.begin(),active.end(),true));
        apply_walker_pose_to_instances(*preset,hierarchy,instances,&actor,
            [&](float x,float y){return terrain_height_near(x,y);});
        if(runtime_shadow_collector_)collect_runtime_shadow(runtime_shadow_owner(lab_runtime,transient_serial,linked_child!=nullptr),
            *preset,instances,active,actor.position,actor.body_yaw_turn16,fog_flags);
        if(!in_canvas)return result("outside_canvas",true);
        if(fog_intensity==0U)return result("fog_hidden",true);
        const bool neutral=thandor::placement_uses_neutral_source_color(*preset);
        const uint32_t texture_runtime_faction=neutral?0U:faction_index;
        const uint32_t texture_graphics_family=neutral?0U:graphics_family_for_faction(faction_index);
        const auto& family=texture_family(texture_graphics_family);
        bool emitted=false;
        for(std::size_t instance_index=0;instance_index<instances.size();++instance_index){
            if(!active[instance_index]||instances[instance_index].sprite==nullptr)continue;
            const auto& instance=instances[instance_index];
            const auto& mesh=instance.sprite->mesh();
            ModelVertexLightingCache vertex_lighting(instance.transform,prepared_directional_light,ramp);
            for(std::size_t triangle=0;triangle+2U<mesh.indices.size();triangle+=3U){
                const std::array<const thandor::SpriteMeshVertex*,3> corners{
                    &mesh.vertices.at(mesh.indices[triangle]),&mesh.vertices.at(mesh.indices[triangle+1U]),
                    &mesh.vertices.at(mesh.indices[triangle+2U])};
                const uint32_t subresource=corners[0]->texture_subresource;
                const bool untextured=subresource==std::numeric_limits<uint32_t>::max();
                const uint32_t flags=corners[0]->render_flags;
                const auto primitive=classify_directdraw_direct3d_sprite_primitive(flags,!untextured);
                if(!primitive.supported)continue;
                if(next.vertices.size()+3U>kMaximumDynamicVertices||next.indices.size()+3U>kMaximumDynamicIndices)
                    throw std::runtime_error("transient simulation entity geometry limit exceeded");
                const uint32_t base=static_cast<uint32_t>(next.vertices.size());
                for(const auto* source:corners){
                    const uint32_t layer=untextured?untextured_layer():object_layer(texture_runtime_faction,texture_graphics_family,subresource);
                    thandor::SpriteTextureUv uv{};
                    if(!untextured){
                        if(subresource>=family.graphics.subresources().size())
                            throw std::runtime_error("transient simulation entity selects missing GFX subresource");
                        const auto& metadata=family.graphics.subresources()[subresource];
                        uv=thandor::normalize_sprite_texture_uv(source->texture_u,source->texture_v,
                            metadata.pixel_width,metadata.pixel_height);
                        if(preset->runtime_model_class_id==2U){
                            if(subresource==preset->primary_animated_subresource)
                                uv.u+=thandor::normalize_model_texture_offset_u(
                                    actor.primary_track_texture_offset_u,
                                    metadata.pixel_width,metadata.pixel_height);
                            else if(subresource==preset->secondary_animated_subresource)
                                uv.u+=thandor::normalize_model_texture_offset_u(
                                    actor.secondary_track_texture_offset_u,
                                    metadata.pixel_width,metadata.pixel_height);
                        }
                    }
                    const Vec3f world=transform_point(instance.transform,source->position);
                    const auto lighting=vertex_lighting.sample(source->normal);
                    const Vec3f normal=lighting.normal;
                    const uint32_t directional=lighting.directional_color;
                    uint32_t modulation=0xFFFFFFFFU;
                    if(untextured){
                        const uint32_t palette_index=sprite_palette_index(source->render_flags);
                        if(palette_index<family.palette.colors_argb().size())modulation=family.palette.colors_argb()[palette_index];
                    }
                    const uint32_t packed=thandor::multiply_rgb_opaque(directdraw_select_model_vertex_packed_color(flags,
                        modulation|0xFF000000U,thandor::multiply_rgb_opaque(directional,modulation)),fog_tint);
                    next.vertices.push_back(ObjectVertex{world,normal,uv.u,uv.v,layer,packed,
                        directdraw_model_vertex_uses_direct_tint(flags)?1U:0U,0.0F,0.0F,
                        (preset->casts_generated_shadow&&primitive.blend!=PrimitiveBlendPreset::AdditiveNoDepthWrite)?1U:0U});
                }
                const uint32_t first=static_cast<uint32_t>(next.indices.size());
                next.indices.insert(next.indices.end(),{base,base+1U,base+2U});
                append_draw_run(primitive.blend,primitive.two_sided,first,0U,
                    retail_world_model_pass_from_mdl_flags(preset->model_runtime_render_flags));
                emitted=true;
            }
        }
        result(emitted?"submitted":"empty_hierarchy",emitted);
        if(animate_timed_effects_&&(lab_runtime!=nullptr ||
           (maximum_integrity>0&&integrity>0&&integrity<maximum_integrity))){
            const int32_t integrity_percent=maximum_integrity>0
                ?static_cast<int32_t>(std::clamp<int64_t>(
                    static_cast<int64_t>(integrity)*100/maximum_integrity,0,100))
                :100;
            for(std::size_t node_index=0U;node_index<hierarchy.size();++node_index){
                if(node_index>=active.size()||!active[node_index]||instances[node_index].sprite==nullptr)continue;
                const auto& node=hierarchy[node_index];
                const bool runtime_root=node.parent_index==std::numeric_limits<uint32_t>::max()||
                    (node.parent_index<hierarchy.size()&&
                     hierarchy[node.parent_index].model_definition_id!=node.model_definition_id);
                if(!runtime_root||node.simulation.damage_effect_id==0U||
                   node.simulation.damage_effect_health_threshold_percent<=0)continue;
                if(lab_runtime){
                    // Per-component integrity is authoritative for selected-run mobile
                    // actors too; the root summary cannot describe a damaged turret.
                    const auto live_effect=thandor::simulation_lab_damage_emitter_for_model(
                        *lab_runtime,hierarchy,node_index);
                    if(!live_effect || *live_effect!=node.simulation.damage_effect_id)continue;
                }else if(integrity_percent>=node.simulation.damage_effect_health_threshold_percent)
                    continue;
                const auto* definition=catalog_.effect_definition(node.simulation.damage_effect_id);
                if(definition==nullptr)continue;
                std::vector<std::pair<uint32_t,Vec3f>> attachments;
                std::vector<uint32_t> keys;
                for(const auto& lookup:instances[node_index].sprite->lookup_records()){
                    if(lookup.key_class().value()!=3U)continue;
                    attachments.emplace_back(lookup.key_index().value(),
                        transform_point(instances[node_index].transform,lookup.local_translation));
                    keys.push_back(lookup.key_index().value());
                }
                const thandor::DamageEffectTiming timing{
                    node.simulation.damage_effect_min_cooldown_ticks,
                    node.simulation.damage_effect_random_cooldown_ticks,
                    static_cast<uint32_t>(keys.size())};
                const uint32_t stable_seed=resource_animation_rng_seed_^node.simulation.damage_effect_id^
                    static_cast<uint32_t>(transient_serial)^static_cast<uint32_t>(node_index*0x9E3779B9U);
                const auto schedule=thandor::build_damage_effect_schedule_for_attachment_keys(
                    timing,keys,stable_seed,2048U,128U);
                if(schedule.cycle_ticks==0U)continue;
                const uint32_t lifetime=std::max(1U,thandor::effect_lifetime_ticks(*definition));
                for(const auto& pulse:schedule.pulses){
                    const uint32_t age=(runtime_visual_tick%schedule.cycle_ticks+schedule.cycle_ticks-
                        pulse.spawn_tick%schedule.cycle_ticks)%schedule.cycle_ticks;
                    if(age>=lifetime)continue;
                    Vec3f origin=instances[node_index].transform.translation;
                    if(!attachments.empty()){
                        const auto found=std::find_if(attachments.begin(),attachments.end(),
                            [&](const auto& entry){return entry.first==pulse.attachment_index;});
                        if(found!=attachments.end())origin=found->second;
                    }
                    const EffectRuntimeAngles exact_angles{pulse.world_rotation_angle0,
                        pulse.world_rotation_angle1,pulse.world_rotation_angle2};
                    ModelHierarchyTransform effect_orientation=fixed_model_rotation_basis(
                        exact_angles[2],exact_angles[1],exact_angles[0]);
                    effect_orientation.translation=origin;
                    append_effect_visual(*definition,origin,age,0.0F,0U,&effect_orientation,
                        std::nullopt,texture_graphics_family,exact_angles);
                }
            }
        }
        return emitted;
    };
    if(simulation_mode_){
        for(auto& [serial,entity]:simulation_produced_entities_){
            (void)append_runtime_transient_model(entity.army_asset_id,entity.faction_index,
                entity.actor,nullptr,entity.maximum_integrity,entity.integrity,serial,nullptr);
        }
        for(auto& [serial,child]:simulation_linked_child_entities_){
            if(!child.visible)continue;
            (void)append_runtime_transient_model(child.army_asset_id,child.faction_index,
                child.actor,&child,child.maximum_integrity,child.integrity,serial,nullptr);
        }
    }

    // Simulation Lab presents every isolated world in one real 3D coordinate system. Persisted
    // selected-run actors reuse the ordinary dynamic placement path; selected synthetic construction
    // below resolves its stock ARM/MDL/SPR model directly. Non-selected runs remain lightweight
    // translucent diagnostic bodies only when the user explicitly enables all-run comparison.
    if(simulation_lab_active()){
        const auto runs=simulation_lab_runs();
        const std::size_t selected_run=runs.empty()?0U:static_cast<std::size_t>(std::clamp(
            simulation_lab_selected_run_,0,static_cast<int>(runs.size())-1));

        // The selected run's synthetic construction is a real stock ARM/MDL/SPR instance, not
        // merely the orange diagnostic diamond used by V539/V540. It remains
        // transient editor state (no LEV placement is inserted), but it uses the same faction
        // texture family, technology-resolved hierarchy, terrain contact, lighting and model pass
        // as a persisted placement.  Construction progress only scales the model from the terrain
        // anchor; it does not invent a retail builder animation.
        const auto append_simulation_lab_synthetic_model=[&](
            const thandor::SimulationLabRun& run,const thandor::SimulationLabEntity& entity)->bool{
            if(entity.runtime_removed)return true;
            if(!entity.synthetic||entity.faction_index==0U)return false;
            const auto* preset=catalog_.army_preset(entity.army_asset_id);
            // Primary construction-queue origin, not browser categorization, is the authority for
            // a Lab building. Special retail class-14/class-16 definitions need not live in the
            // editor's Buildings tab. The hierarchy below can also provide the root SPR when the
            // ARM-level convenience sprite_path is empty.
            if(preset==nullptr)return false;
            const bool in_canvas=model_placement_visible(add(entity.actor.position,{0.0F,0.0F,4.0F}),std::max(24.0F,shadow_caster_margin));
            if(!in_canvas&&!runtime_shadow_collector_)return true;

            std::vector<thandor::ModelVisualNode> resolved;
            if(!entity.runtime_model_hierarchy)resolved=thandor::resolve_faction_technology_hierarchy(
                *preset,run.faction_technology_masks[std::min<std::size_t>(entity.faction_index,7U)]);
            std::vector<thandor::ModelVisualNode> fallback_nodes;
            std::span<const thandor::ModelVisualNode> hierarchy=entity.runtime_model_hierarchy
                ?std::span<const thandor::ModelVisualNode>(*entity.runtime_model_hierarchy)
                :!resolved.empty()?std::span<const thandor::ModelVisualNode>(resolved)
                :(preset->resolved_model_hierarchy.empty()
                    ?std::span<const thandor::ModelVisualNode>(preset->model_hierarchy)
                    :std::span<const thandor::ModelVisualNode>(preset->resolved_model_hierarchy));
            if(entity.runtime_model_hierarchy)hierarchy=*entity.runtime_model_hierarchy;
            if(hierarchy.empty()){
                thandor::ModelVisualNode fallback;
                fallback.parent_index=std::numeric_limits<uint32_t>::max();
                fallback.local_rotation_angle1=0x4000U;
                fallback.sprite_path=preset->sprite_path;
                fallback_nodes.push_back(std::move(fallback));
                hierarchy=fallback_nodes;
            }

            const auto fog_root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){
                return node.parent_index==std::numeric_limits<uint32_t>::max();});
            const auto fog_flags=fog_root==hierarchy.end()?preset->model_runtime_render_flags:
                fog_root->simulation.runtime_render_flags;
            const uint32_t fog_intensity=fog_preview_model_intensity(entity.actor.position,fog_flags);
            if(fog_intensity==0U&&!runtime_shadow_collector_)return true;
            const uint32_t fog_tint=0xFF000000U|(fog_intensity*0x010101U);
            auto runtime_weapons=thandor::simulation_lab_render_weapon_profiles(&entity,[&]{
                return catalog_.simulation_weapon_profiles_for_hierarchy(hierarchy);});
            const bool producer=entity.runtime_model_class_id==11U||entity.runtime_model_class_id==13U||
                                entity.runtime_model_class_id==22U;
            std::optional<uint32_t> production_tick;
            if(producer)production_tick=entity.production_animated_subnode_ticks;
            Vec3f terrain_anchor=entity.actor.position;
            terrain_anchor.z=terrain_height_near(terrain_anchor.x,terrain_anchor.y);
            const uint32_t root_yaw=entity.actor.body_yaw_turn16&0xFFFFU;
            std::vector<ModelHierarchyInstance> instances;
            std::vector<bool> active;
            instances.reserve(hierarchy.size());active.reserve(hierarchy.size());
            for(std::size_t node_index=0;node_index<hierarchy.size();++node_index){
                const auto& node=hierarchy[node_index];
                ModelHierarchyInstance instance{};instance.node=&node;
                instance.root=node.parent_index==std::numeric_limits<uint32_t>::max();
                bool node_active=node.creates_runtime_node()&&!node.sprite_path.empty()&&thandor::simulation_lab_model_node_present(entity,node_index);
                const thandor::SpriteAsset* node_sprite=node_active?&cached_sprite(node.sprite_path):nullptr;
                if(instance.root){
                    uint32_t runtime_root_yaw=root_yaw;
                    for(std::size_t i=0;i<runtime_weapons.size()&&i<entity.actor.weapons.size();++i)
                        if(runtime_weapons[i].yaw_model_node_index==node_index)
                            runtime_root_yaw=thandor::wrap_turn16(static_cast<int64_t>(runtime_root_yaw)+
                                entity.actor.weapons[i].yaw.angle_turn16);
                    const auto contact=placement_contact_pose(*preset,terrain_anchor.x,terrain_anchor.y,
                        node_sprite?node_sprite->placement_height_offset():0.0F);
                    const Vec3f root_position{terrain_anchor.x,terrain_anchor.y,
                        contact.height+(entity.runtime_model_class_id==12U
                            ?thandor::q12_to_float(entity.class12_vertical_offset_q12):0.0F)};
                    instance.transform=contact.align_to_normal
                        ?terrain_contact_root_transform(root_position,thandor::turn16_to_radians(runtime_root_yaw),contact.normal,contact.packed_normal_angles)
                        :placement_root_transform(root_position.x,root_position.y,root_position.z,
                            thandor::turn16_to_radians(runtime_root_yaw));
                }else{
                    if(node.parent_index>=instances.size()||!active[node.parent_index])node_active=false;
                    if(node_active&&!thandor::model_runtime_world_child_enabled(
                           hierarchy[node.parent_index].runtime_class_id,node.child_slot))
                        node_active=false;
                    if(node_active){
                        const auto& parent=instances[node.parent_index];
                        const auto* attachment=parent.sprite->child_attachment(node.child_slot);
                        if(attachment==nullptr)node_active=false;
                        else{
                            const auto pose=thandor::simulation_runtime_child_pose_v602(
                                hierarchy[node.parent_index],node,node_index,runtime_weapons,entity.actor,
                                animate_buildings_,run.tick,production_tick);
                            auto local=fixed_model_rotation_basis(pose.angle2,pose.angle1,pose.angle0);
                            local.translation=attachment->local_translation;
                            if(const auto z=simulation_storage_child_z(hierarchy[node.parent_index],node.child_slot,entity.faction_index,&run))local.translation.z=*z;
                            if(parent.root&&node.child_slot==0U){
                                if(entity.runtime_model_class_id==23U)
                                    local.translation.z+=thandor::q12_to_float(entity.vertical_deployment_child_local_z_q12);
                                if(entity.runtime_model_class_id==22U&&entity.airport_initialized)
                                    local.translation.z=thandor::q12_to_float(entity.airport.platform_local_z_q12);
                                if(entity.runtime_model_class_id==21U&&entity.aircraft.initialized){
                                    const auto translation=local.translation;
                                    local=fixed_model_rotation_basis(node.local_rotation_angle2,entity.aircraft.body_pitch,0x8000U);
                                    local.translation=translation;
                                    local.translation.z=thandor::q12_to_float(entity.aircraft.body_height);
                                }
                            }
                            if(pose.vertical_animation_tick)
                                local.translation.z=animated_child2_translation_q12(
                                    hierarchy[node.parent_index].simulation,*pose.vertical_animation_tick,local.translation.z);
                            if(pose.recoil_translation_q12!=0)
                                local.translation=add(local.translation,transform_vector(local,
                                    {thandor::q12_to_float(pose.recoil_translation_q12),0.0F,0.0F}));
                            instance.transform=compose_model_transform(parent.transform,local);
                        }
                    }
                }
                if(node_active)instance.sprite=node_sprite;
                active.push_back(node_active);instances.push_back(instance);
            }

            if(runtime_shadow_collector_)collect_runtime_shadow(runtime_shadow_owner(&entity,0U,false),*preset,instances,active,
                entity.actor.position,entity.actor.body_yaw_turn16,fog_flags);
            if(!in_canvas||fog_intensity==0U)return true;

            // Retail placement creates the complete model hierarchy immediately. Visible placement
            // feedback comes from the authored MDL +0x190 one-shot EFF, not a model-scale animation.
            constexpr float build_scale=1.0F;
            const bool neutral_source_color=thandor::placement_uses_neutral_source_color(*preset);
            const uint32_t texture_runtime_faction=neutral_source_color?0U:entity.faction_index;
            const uint32_t texture_graphics_family=neutral_source_color?0U:
                graphics_family_for_faction(entity.faction_index);
            const auto& family=texture_family(texture_graphics_family);
            bool emitted=false;
            for(std::size_t instance_index=0;instance_index<instances.size();++instance_index){
                if(!active[instance_index]||instances[instance_index].sprite==nullptr)continue;
                const auto& instance=instances[instance_index];
                const auto& sprite=*instance.sprite;
                const auto& mesh=sprite.mesh();
                ModelVertexLightingCache vertex_lighting(instance.transform,prepared_directional_light,ramp);
                for(std::size_t triangle=0;triangle+2U<mesh.indices.size();triangle+=3U){
                    const uint32_t ia=mesh.indices[triangle],ib=mesh.indices[triangle+1U],
                                   ic=mesh.indices[triangle+2U];
                    if(ia>=mesh.vertices.size()||ib>=mesh.vertices.size()||ic>=mesh.vertices.size())
                        throw std::runtime_error("Simulation Lab construction SPR index exceeds vertex stream");
                    const std::array<const thandor::SpriteMeshVertex*,3> corners{
                        &mesh.vertices[ia],&mesh.vertices[ib],&mesh.vertices[ic]};
                    const uint32_t subresource=corners[0]->texture_subresource;
                    const bool untextured=subresource==std::numeric_limits<uint32_t>::max();
                    if(corners[1]->texture_subresource!=subresource||corners[2]->texture_subresource!=subresource)
                        throw std::runtime_error("Simulation Lab construction triangle mixes texture subresources");
                    const uint32_t render_flags=corners[0]->render_flags;
                    const auto primitive=classify_resource_preview_primitive(
                        render_flags,!untextured,ResourcePrimitiveRole::None);
                    if(!primitive.supported)continue;
                    if(next.vertices.size()+3U>kMaximumDynamicVertices||
                       next.indices.size()+3U>kMaximumDynamicIndices)
                        throw std::runtime_error("Simulation Lab construction geometry limit exceeded");
                    const uint32_t base=static_cast<uint32_t>(next.vertices.size());
                    for(const auto* source:corners){
                        uint32_t layer=untextured?untextured_layer():object_layer(texture_runtime_faction,texture_graphics_family,subresource);
                        thandor::SpriteTextureUv uv{};
                        if(!untextured){
                            if(subresource>=family.graphics.subresources().size())
                                throw std::runtime_error("Simulation Lab construction selects missing GFX subresource");
                            const auto& metadata=family.graphics.subresources()[subresource];
                            uv=thandor::normalize_sprite_texture_uv(source->texture_u,source->texture_v,
                                metadata.pixel_width,metadata.pixel_height);
                            if(instance.root&&producer&&subresource==preset->primary_animated_subresource)
                                uv.v+=thandor::normalize_model_texture_offset_v(
                                    static_cast<int32_t>(entity.production_door_texture_offset_v),
                                    metadata.pixel_width,metadata.pixel_height);
                        }
                        Vec3f world=transform_point(instance.transform,source->position);
                        world.z=terrain_anchor.z+(world.z-terrain_anchor.z)*build_scale;
                        const auto lighting=vertex_lighting.sample(source->normal);
                        const Vec3f normal=lighting.normal;
                        const uint32_t directional=lighting.directional_color;
                        uint32_t modulation=0xFFFFFFFFU;
                        if(untextured){
                            const uint32_t palette_index=sprite_palette_index(source->render_flags);
                            if(palette_index<family.palette.colors_argb().size())
                                modulation=family.palette.colors_argb()[palette_index];
                        }
                        const uint32_t packed=thandor::multiply_rgb_opaque(directdraw_select_model_vertex_packed_color(render_flags,
                            modulation|0xFF000000U,thandor::multiply_rgb_opaque(directional,modulation)),fog_tint);
                        next.vertices.push_back(ObjectVertex{world,normal,uv.u,uv.v,layer,packed,
                            directdraw_model_vertex_uses_direct_tint(render_flags)?1U:0U,0.0F,0.0F,
                            (preset->casts_generated_shadow&&primitive.blend!=PrimitiveBlendPreset::AdditiveNoDepthWrite)?1U:0U});
                    }
                    const uint32_t first=static_cast<uint32_t>(next.indices.size());
                    next.indices.insert(next.indices.end(),{base,base+1U,base+2U});
                    append_draw_run(primitive.blend,primitive.two_sided,first,0U,
                        retail_world_model_pass_from_mdl_flags(preset->model_runtime_render_flags));
                    emitted=true;
                }
            }
            // The selected Lab synthetic-building path bypasses append_runtime_transient_model,
            // which already emits these authored MDL +0x250/+0x254 damage effects for units.
            // Select by live component integrity, not the root-only compatibility summary:
            // a damaged turret can smoke while the building root remains healthy.
            if(emitted && animate_timed_effects_ && entity.construction_complete){
                for(std::size_t node_index=0;node_index<hierarchy.size();++node_index){
                    if(node_index>=active.size() || !active[node_index] ||
                       instances[node_index].sprite==nullptr)continue;
                    const auto effect_id=thandor::simulation_lab_damage_emitter_for_model(
                        entity,hierarchy,node_index);
                    if(!effect_id)continue;
                    const auto* definition=catalog_.effect_definition(*effect_id);
                    if(!definition)continue;
                    std::vector<std::pair<uint32_t,Vec3f>> attachments;
                    std::vector<uint32_t> attachment_keys;
                    const auto& instance=instances[node_index];
                    for(const auto& lookup:instance.sprite->lookup_records()){
                        if(lookup.key_class().value()!=3U)continue;
                        const uint32_t key=lookup.key_index().value();
                        attachments.emplace_back(key,
                            transform_point(instance.transform,lookup.local_translation));
                        attachment_keys.push_back(key);
                    }
                    const auto& rule=hierarchy[node_index].simulation;
                    const thandor::DamageEffectTiming timing{
                        rule.damage_effect_min_cooldown_ticks,
                        rule.damage_effect_random_cooldown_ticks,
                        static_cast<uint32_t>(attachment_keys.size())};
                    const uint32_t entity_index=static_cast<uint32_t>(&entity-run.entities.data());
                    const uint32_t seed=resource_animation_rng_seed_^*effect_id^
                        (entity_index*0x9E3779B9U)^static_cast<uint32_t>(node_index*0x85EBCA6BU);
                    const auto schedule=thandor::build_damage_effect_schedule_for_attachment_keys(
                        timing,attachment_keys,seed,2048U,128U);
                    if(schedule.cycle_ticks==0U)continue;
                    const uint32_t lifetime=std::max(1U,thandor::effect_lifetime_ticks(*definition));
                    for(const auto& pulse:schedule.pulses){
                        const uint32_t age=(runtime_visual_tick%schedule.cycle_ticks+
                            schedule.cycle_ticks-pulse.spawn_tick%schedule.cycle_ticks)%schedule.cycle_ticks;
                        if(age>=lifetime)continue;
                        Vec3f origin=instance.transform.translation;
                        if(!attachments.empty()){
                            const auto match=std::find_if(attachments.begin(),attachments.end(),
                                [&](const auto& entry){return entry.first==pulse.attachment_index;});
                            if(match!=attachments.end())origin=match->second;
                        }
                        const EffectRuntimeAngles angles{pulse.world_rotation_angle0,
                            pulse.world_rotation_angle1,pulse.world_rotation_angle2};
                        auto orientation=fixed_model_rotation_basis(
                            angles[2],angles[1],angles[0]);
                        orientation.translation=origin;
                        append_effect_visual(*definition,origin,age,0.0F,0U,&orientation,
                            std::nullopt,texture_graphics_family,angles);
                    }
                }
            }
            return emitted;
        };

        constexpr std::array<uint32_t,8> kRunGhostRgb{
            0x00A9D6FFU,0x00FFB56BU,0x0098E38FU,0x00D9A0FFU,
            0x00FFE176U,0x007FD8D4U,0x00FF94A8U,0x00B8C4D8U};
        const uint32_t ghost_layer=untextured_layer();
        const auto append_ghost_triangle=[&](Vec3f a,Vec3f b,Vec3f c,uint32_t color){
            if(next.vertices.size()+3U>kMaximumDynamicVertices||
               next.indices.size()+3U>kMaximumDynamicIndices)
                throw std::runtime_error("Simulation Lab ghost geometry limit exceeded");
            const Vec3f normal=normalize(cross(sub(b,a),sub(c,a)));
            const uint32_t base=static_cast<uint32_t>(next.vertices.size());
            next.vertices.push_back(ObjectVertex{a,normal,0.0F,0.0F,ghost_layer,color,1U,0.0F,0.0F});
            next.vertices.push_back(ObjectVertex{b,normal,0.0F,0.0F,ghost_layer,color,1U,0.0F,0.0F});
            next.vertices.push_back(ObjectVertex{c,normal,0.0F,0.0F,ghost_layer,color,1U,0.0F,0.0F});
            const uint32_t first=static_cast<uint32_t>(next.indices.size());
            next.indices.insert(next.indices.end(),{base,base+1U,base+2U});
            append_draw_run(PrimitiveBlendPreset::AlphaNoDepthWrite,true,first);
        };
        const auto append_ghost_body=[&](const thandor::SimulationLabEntity& entity,
                                         std::size_t run_index,bool selected_synthetic,
                                         float construction_progress){
            if(entity.runtime_removed)return;
            const bool constructing=entity.synthetic&&!entity.construction_complete;
            (void)construction_progress;
            Vec3f center=entity.actor.position;
            center.z=terrain_height_near(center.x,center.y)+(entity.building?0.9F:0.55F);
            const float radius=entity.building?0.55F:0.34F;
            const float height=entity.building?0.82F:0.52F;
            const uint32_t rgb=kRunGhostRgb[run_index%kRunGhostRgb.size()]&0x00FFFFFFU;
            const uint32_t alpha=constructing?0x98000000U:(selected_synthetic?0xD0000000U:0x70000000U);
            const uint32_t color=alpha|rgb;
            const Vec3f top{center.x,center.y,center.z+height};
            const Vec3f bottom{center.x,center.y,center.z-height*0.55F};
            const std::array<Vec3f,4> ring{{
                {center.x+radius,center.y,center.z},
                {center.x,center.y+radius,center.z},
                {center.x-radius,center.y,center.z},
                {center.x,center.y-radius,center.z}}};
            for(std::size_t side=0;side<4U;++side){
                const auto next_side=(side+1U)%4U;
                append_ghost_triangle(top,ring[side],ring[next_side],color);
                append_ghost_triangle(bottom,ring[next_side],ring[side],color);
            }
            // Unit orientation remains comparable across runs. Building heading belongs to placement preview.
            if(!entity.building){
            const float yaw=thandor::turn16_to_radians(entity.actor.body_yaw_turn16);
            const Vec3f forward{std::cos(yaw),std::sin(yaw),0.0F};
            const Vec3f right{-forward.y,forward.x,0.0F};
            const Vec3f arrow_origin{center.x,center.y,center.z+0.08F};
            const Vec3f nose=add(arrow_origin,mul(forward,radius*1.8F));
            const Vec3f left=add(add(arrow_origin,mul(forward,radius*0.35F)),mul(right,-radius*0.45F));
            const Vec3f right_point=add(add(arrow_origin,mul(forward,radius*0.35F)),mul(right,radius*0.45F));
            append_ghost_triangle(nose,left,right_point,(std::min<uint32_t>(0xE0U,(color>>24U)+0x40U)<<24U)|rgb);
            }
        };
        for(std::size_t run_index=0;run_index<runs.size();++run_index){
            const bool selected=run_index==selected_run;
            if(selected){
                for(const auto& entity:runs[run_index].entities){
                    if(!entity.synthetic)continue;
                    if(entity.building){
                        bool rendered=append_simulation_lab_synthetic_model(runs[run_index],entity);
                        if(!rendered){
                            // Keep an asset-backed fallback for unusual construction definitions
                            // whose hierarchy cannot emit the dedicated building pass.
                            auto render_actor=entity.actor;
                            rendered=append_runtime_transient_model(entity.army_asset_id,entity.faction_index,
                                render_actor,nullptr,entity.root_model_maximum_integrity,
                                entity.root_model_integrity,0U,
                                &runs[run_index].faction_technology_masks[std::min<std::size_t>(entity.faction_index,7U)],&entity);
                        }
                        if(!rendered){
                            // Last-resort visibility only: a corrupt/custom ARM with no renderable
                            // SPR must not disappear while the worker reports a completed building.
                            // Stock assets use one of the two model paths above; this marker is only
                            // a diagnostic proof of the run-local runtime's location.
                            append_ghost_body(entity,run_index,true,
                                thandor::simulation_lab_construction_progress(runs[run_index],entity));
                        }
                    }else {
                        auto render_actor=entity.actor;
                        SimulationLabRenderReceipt receipt;
                        receipt.entity=static_cast<std::size_t>(&entity-runs[run_index].entities.data());
                        receipt.run=static_cast<uint32_t>(run_index);receipt.tick=runs[run_index].tick;
                        receipt.army=entity.army_asset_id;receipt.faction=entity.faction_index;
                        bool rendered=append_runtime_transient_model(entity.army_asset_id,entity.faction_index,
                            render_actor,nullptr,entity.root_model_maximum_integrity,
                            entity.root_model_integrity,0U,
                            &runs[run_index].faction_technology_masks[std::min<std::size_t>(entity.faction_index,7U)],&entity,&receipt);
                        // Unlike buildings, mobile synthetic models previously had no secondary
                        // hierarchy path. A failed transient draw made a correctly spawned unit
                        // completely invisible, concealing production/debugging errors.
                        if(!rendered){
                            const auto start=next.indices.size();
                            rendered=append_simulation_lab_synthetic_model(runs[run_index],entity);
                            record_render_receipt_geometry(receipt,next.vertices,next.indices,start);
                            if(rendered)receipt.result="fallback_model";
                        }
                        if(!rendered){append_ghost_body(entity,run_index,true,1.0F);receipt.result="diagnostic_marker";}
                        simulation_lab_render_receipts_.push_back(receipt);
                    }
                }
                // Persisted selected-run actors/buildings already use the full dynamic placement
                // renderer above.  Never overlay V539/V540 diagnostic diamonds on the selected
                // world again; they were the orange artifacts reported in the GUI.
                continue;
            }
            if(!simulation_lab_show_all_runs_)continue;
            for(const auto& entity:runs[run_index].entities)
                append_ghost_body(entity,run_index,false,
                    thandor::simulation_lab_construction_progress(runs[run_index],entity));
        }
    }

    recording_models=false;
    record_model_step();
    // Preserve the unsorted source so transparent ordering can be recomputed for every camera.
    cache.scene.vertices=next.vertices;cache.scene.indices=next.indices;
    cache.scene.draw_runs=next.draw_runs;cache.scene.shading_emitters=next.shading_emitters;
    cache.visible_buildings=visible_animated_buildings_;cache.receipts=simulation_lab_render_receipts_;
    }
    const auto shadow_started=std::chrono::steady_clock::now();
    if(reuse_state){
        ++dynamic_shadow_cache_hits_;
        next.shadow_vertices=cache.scene.shadow_vertices;next.shadow_indices=cache.scene.shadow_indices;
    }else{

    // Move each simulation replacement's projected shadow with its current LEV-backed
    // position and whole-body yaw. The generated alpha mask remains in the static object
    // texture atlas; only the small terrain-conforming receiver geometry is rebuilt per tick.
    for(auto& shadow:dynamic_shadow_templates_){
        if(shadow.owner.source==RuntimeShadowSource::LevelPlacement&&simulation_lab_active()&&
           simulation_lab_suppressed_placements_.contains(shadow.placement_index))continue;
        if(shadow.owner.source==RuntimeShadowSource::LevelPlacement&&shadow.requires_live_owner&&
           !simulation_actor_states_.contains(shadow.placement_index))continue;
        Vec3f shadow_position=shadow.anchor_position;
        uint32_t shadow_yaw=shadow.anchor_yaw_turn16;
        if(const auto actor_found=simulation_actor_states_.find(shadow.placement_index);
           actor_found!=simulation_actor_states_.end()&&(simulation_mode_||simulation_lab_active())){
            shadow_position=actor_found->second.position;
            shadow_yaw=actor_found->second.body_yaw_turn16;
        }
        if(shadow.owner.source==RuntimeShadowSource::LabEntity){
            const auto runs=simulation_lab_runs();
            if(!simulation_lab_active()||shadow.owner.run>=runs.size()||
               shadow.owner.run!=static_cast<uint32_t>(simulation_lab_selected_run_))continue;
            const auto& entities=runs[shadow.owner.run].entities;
            if(shadow.owner.id>=entities.size()||entities[shadow.owner.id].runtime_removed)continue;
            shadow_position=entities[shadow.owner.id].actor.position;
            shadow_yaw=entities[shadow.owner.id].actor.body_yaw_turn16;
        }else if(shadow.owner.source==RuntimeShadowSource::Produced){
            const auto found=simulation_produced_entities_.find(shadow.owner.id);
            if(!simulation_mode_||found==simulation_produced_entities_.end())continue;
            shadow_position=found->second.actor.position;shadow_yaw=found->second.actor.body_yaw_turn16;
        }else if(shadow.owner.source==RuntimeShadowSource::LinkedChild){
            const auto found=simulation_linked_child_entities_.find(shadow.owner.id);
            if(!simulation_mode_||found==simulation_linked_child_entities_.end()||!found->second.visible)continue;
            shadow_position=found->second.actor.position;shadow_yaw=found->second.actor.body_yaw_turn16;
        }
        uint32_t shadow_model_flags=shadow.model_runtime_render_flags;
        if(shadow.placement_index<placements.size())
            if(const auto* shadow_preset=catalog_.army_preset(placements[shadow.placement_index].army_asset_id))
                shadow_model_flags=shadow_preset->model_runtime_render_flags;
        if(fog_preview_model_intensity(shadow_position,shadow_model_flags)==0U)continue;
        const float yaw_delta=thandor::turn16_to_radians(thandor::wrap_turn16(
            static_cast<int64_t>(shadow_yaw)-shadow.anchor_yaw_turn16));
        const float c=std::cos(yaw_delta),s=std::sin(yaw_delta);
        const uint32_t base=static_cast<uint32_t>(next.shadow_vertices.size());
        if(next.shadow_vertices.size()+shadow.receiver_vertices.size()>kMaximumDynamicVertices||
           next.shadow_indices.size()+shadow.receiver_indices.size()>kMaximumDynamicIndices)
            throw std::runtime_error("dynamic actor shadow geometry limit exceeded");
        const auto* projected=shadow.projected_pose_cache.find(shadow_position,shadow_yaw);
        if(projected){
            ++generated_shadow_pose_cache_hits_;
            generated_shadow_pose_cache_reused_vertices_+=projected->size();
        }else{
            ++generated_shadow_pose_cache_misses_;
            std::vector<ObjectVertex> rebuilt;
            rebuilt.reserve(shadow.receiver_vertices.size());
            for(std::size_t vertex_index=0;vertex_index<shadow.receiver_vertices.size();++vertex_index){
                const auto& source=shadow.receiver_vertices[vertex_index];
                const float local_x=source.position.x-shadow.anchor_position.x;
                const float local_y=source.position.y-shadow.anchor_position.y;
                const float world_x=shadow_position.x+c*local_x-s*local_y;
                const float world_y=shadow_position.y+s*local_x+c*local_y;
                const float receiver_offset=shadow.receiver_height_offsets[vertex_index];
                ObjectVertex moved=source;
                moved.position={world_x,world_y,terrain_height_near(world_x,world_y)+receiver_offset};
                rebuilt.push_back(moved);
            }
            shadow.projected_pose_cache.store(shadow_position,shadow_yaw,std::move(rebuilt));
            projected=shadow.projected_pose_cache.find(shadow_position,shadow_yaw);
        }
        next.shadow_vertices.insert(next.shadow_vertices.end(),projected->begin(),projected->end());
        // Moving a cached receiver across a cliff can introduce a nearly vertical
        // triangle even if its original authored placement was valid. Discard only
        // these pathological faces; keep the already-cached vertex array intact.
        for(std::size_t index=0;index+2U<shadow.receiver_indices.size();index+=3U){
            const uint32_t ia=shadow.receiver_indices[index];
            const uint32_t ib=shadow.receiver_indices[index+1U];
            const uint32_t ic=shadow.receiver_indices[index+2U];
            if(ia>=projected->size()||ib>=projected->size()||ic>=projected->size())
                throw std::runtime_error("dynamic shadow receiver index exceeds cached vertex stream");
            if(shadow_receiver_triangle_is_upright((*projected)[ia].position,
                                                  (*projected)[ib].position,
                                                  (*projected)[ic].position)) {
                ++generated_shadow_dynamic_culled_triangles_;
                continue;
            }
            next.shadow_indices.insert(next.shadow_indices.end(),{base+ia,base+ib,base+ic});
        }
    }

    cache.scene.shadow_vertices=next.shadow_vertices;cache.scene.shadow_indices=next.shadow_indices;
    }
    cache.visual_tick=runtime_visual_tick;cache.building_tick=current_building_animation_tick_;
    cache.document=document_.get();cache.snapshot=simulation_lab_snapshot_.get();cache.settings=cache_settings;
    const auto shadow_finished=std::chrono::steady_clock::now();
    if(!timed_effect_controllers_.empty()&&(animate_timed_effects_||animate_resource_fields_)){
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
            bool animation_enabled=source.owner_is_resource_field?animate_resource_fields_:
                thandor::timed_effect_preview_enabled(
                    source.owner_is_building,source.owner_has_authored_model_animation,
                    animate_timed_effects_,animate_buildings_,simulation_mode_||simulation_lab_active());
            // Runtime class 13 services the timed SHT/EFF emitter while production state 1 is active.
            // Do not leave factory smoke running through idle/open/drive-out/close merely because
            // generic building animation is enabled in Simulation mode.
            if((simulation_mode_||simulation_lab_active())&&source.runtime_class_id==13U){
                const auto factory=simulation_factory_states_.find(source.placement_index);
                animation_enabled=animate_timed_effects_&&factory!=simulation_factory_states_.end()&&
                    factory->second.phase==thandor::SimulationClass13ProductionPhase::Building;
            }
            if(simulation_lab_active()) {
                const auto runs=simulation_lab_runs();
                if(!runs.empty()) {
                    const auto ri=static_cast<size_t>(std::clamp(simulation_lab_selected_run_,0,static_cast<int>(runs.size())-1));
                    for(const auto& entity:runs[ri].entities) {
                        if(entity.synthetic||entity.source_placement_index!=source.placement_index)continue;
                        if(!thandor::simulation_lab_model_node_present(entity,source.root_model_node_index))
                            animation_enabled=false;
                        else if(source.runtime_class_id==4U||source.runtime_class_id==11U||
                                source.runtime_class_id==13U||source.runtime_class_id==14U)
                            // The Lab journals these live callbacks, including authored factories.
                            // Rendering the independent preview schedule would duplicate each puff.
                            animation_enabled=false;
                        break;
                    }
                }
                if(simulation_lab_suppressed_placements_.contains(source.placement_index))animation_enabled=false;
            }
            if(animation_enabled&&bounds_visible){
                visible[index]=1U;visible_timed_effect_controllers_.push_back(index);
            }
        }
        const auto resolve_effect_for_pulse=[&](uint32_t emitter_index,
                                                  uint32_t attachment_selector)
            ->std::pair<uint32_t,Vec3f>{
            if(emitter_index>=timed_effect_controllers_.size())return {0U,{}};
            const auto& source=timed_effect_controllers_[emitter_index];
            const auto& pose=resolved_poses[emitter_index];
            Vec3f origin=pose.root_position;
            if(!pose.world_type6_attachments.empty())
                origin=pose.world_type6_attachments[
                    attachment_selector%pose.world_type6_attachments.size()];
            const int32_t water_delta=thandor::retail_nearest_water_delta_q12(
                document_->field(),thandor::float_to_q12(origin.x),thandor::float_to_q12(origin.y));
            const uint32_t effect_id=water_delta>0?source.water_effect_id:source.land_effect_id;
            return {effect_id,origin};
        };
        const auto schedule=thandor::build_resource_emitter_schedule(timings,
            resource_animation_rng_seed_,2048U,256U,
            [&](uint32_t emitter_index,uint32_t,uint32_t attachment_selector){
                const auto [effect_id,unused_origin]=
                    resolve_effect_for_pulse(emitter_index,attachment_selector);
                (void)unused_origin;
                const auto* definition=effect_id==0U?nullptr:catalog_.effect_definition(effect_id);
                return definition!=nullptr&&definition->transition_kind==2U;
            });
        const auto wrapped_age=[&](uint32_t spawn_tick)->uint32_t{
            if(schedule.cycle_ticks==0U)return 0U;
            return (runtime_visual_tick%schedule.cycle_ticks+schedule.cycle_ticks-
                    spawn_tick%schedule.cycle_ticks)%schedule.cycle_ticks;
        };
        for(const auto& pulse:schedule.pulses){
            if(pulse.emitter_index>=timed_effect_controllers_.size()||!visible[pulse.emitter_index])continue;
            const auto& pose=resolved_poses[pulse.emitter_index];
            const auto [effect_id,origin]=
                resolve_effect_for_pulse(pulse.emitter_index,pulse.attachment_random);
            const auto* definition=effect_id==0U?nullptr:catalog_.effect_definition(effect_id);
            if(fog_preview_mode_!=0&&!fog_preview_point_visible(origin))continue;
            // A positive-water emitter can deliberately select a null alternate EFF.  Retail still
            // advances the interval and persistent class-6 selector, but the null definition
            // produces no visible effect instance.
            if(definition==nullptr)continue;
            const float live_root_yaw=std::atan2(
                pose.root_orientation.basis[3],pose.root_orientation.basis[0]);
            const auto timed_transition2_angles=
                thandor::timed_effect_transition2_runtime_angles(pulse.orientation_random);
            const float randomized_yaw=definition->transition_kind==2U
                ?static_cast<float>(timed_transition2_angles[0])*kTurn16ToRadians
                :live_root_yaw;
            const uint32_t age=wrapped_age(pulse.spawn_tick);
            if((definition->shading_color_argb&0x00FFFFFFU)!=0U){
                const auto profile=thandor::make_resource_shading_glow_profile(*definition);
                const auto color=thandor::unpack_argb(0xFF000000U|profile.packed_color_rgb);
                next.shading_emitters.push_back(ResourceShadingEmitter{origin,profile.target_radius_world,
                    color.r,color.g,color.b,static_cast<float>(schedule.cycle_ticks),
                    static_cast<float>(pulse.spawn_tick),profile.grow_transition_ticks,
                    profile.shading_stop_tick,profile.release_transition_ticks});
            }
            ModelHierarchyTransform effect_orientation=pose.root_orientation;
            std::optional<EffectRuntimeAngles> exact_runtime_angles;
            if(definition->transition_kind==2U){
                exact_runtime_angles=EffectRuntimeAngles{timed_transition2_angles[0],
                    timed_transition2_angles[1],timed_transition2_angles[2]};
                effect_orientation=fixed_model_rotation_basis(
                    timed_transition2_angles[2],timed_transition2_angles[1],
                    timed_transition2_angles[0]);
            }
            effect_orientation.translation=origin;
            append_effect_visual(*definition,origin,age,randomized_yaw,
                pulse.orientation_random,&effect_orientation,std::nullopt,0U,exact_runtime_angles);

        }
    }

    // MDL +0x190 is the model-owned construction/deconstruction effect. The recovered
    // deconstruction path keeps the hierarchy alive until this effect completes, then destroys it.
    for(const auto& [placement_index,state]:building_destruction_states_){
        if(placement_index>=placements.size()||runtime_visual_tick<state.start_tick||state.effect_id==0U)continue;
        const auto* definition=catalog_.effect_definition(state.effect_id);
        if(definition==nullptr)continue;
        const uint32_t age=runtime_visual_tick-state.start_tick;
        if(age>=thandor::effect_lifetime_ticks(*definition))continue;
        const auto& placement=placements[placement_index];
        Vec3f origin{placement.render_x(),placement.render_y(),
            terrain_height_near(placement.render_x(),placement.render_y())};
        if(const auto actor=simulation_actor_states_.find(placement_index);
           actor!=simulation_actor_states_.end())origin=actor->second.position;
        auto orientation=placement_root_transform(origin.x,origin.y,origin.z,
            thandor::turn16_to_radians(placement.orientation_turn16&0xFFFFU));
        append_effect_visual(*definition,origin,age,
            thandor::turn16_to_radians(placement.orientation_turn16&0xFFFFU),0U,&orientation);
    }

    // Live, independently linked EFF owners in the selected Simulation Lab run.
    // Their lifetime and phase-3/7 terrain tint belong to the worker; the
    // editor's one-shot journals are not a substitute for these identities.
    if(simulation_lab_active()) {
        const auto runs=simulation_lab_runs();
        if(!runs.empty()) {
            const auto& run=runs[static_cast<std::size_t>(std::clamp(
                simulation_lab_selected_run_,0,static_cast<int>(runs.size())-1))];
            for(const auto& effect:run.environmental_effects) {
                if(effect.world_pool_only||run.tick<effect.spawn_tick)continue;
                const auto* definition=catalog_.effect_definition(effect.effect_id);
                if(!definition)continue;
                const uint32_t age=run.tick-effect.spawn_tick;
                if(age>=thandor::effect_lifetime_ticks(*definition))continue;
                ModelHierarchyTransform orientation;
                orientation.basis=effect.pose.basis;
                orientation.translation=effect.pose.position;
                const float yaw=std::atan2(orientation.basis[3],orientation.basis[0]);
                selected_lab_effect_model_tint=(effect.terrain_refresh_tick!=0U)
                    ?std::optional<uint32_t>(effect.terrain_tint_argb):std::nullopt;
                rendering_independent_lab_effect=true;
                append_effect_visual(*definition,effect.pose.position,age,yaw,0U,&orientation);
                rendering_independent_lab_effect=false;
                selected_lab_effect_model_tint.reset();
            }
        }
    }

    // Retail EGATH0/EWAYP0/ETARG0 geometry; faction tint is an editor overlay choice.
    const auto command_marker_start=std::chrono::steady_clock::now();
    command_marker_diagnostics_={};
    command_marker_run_index_=-1;
    size_t command_marker_count=0;
    command_marker_count_=0;command_marker_triangle_count_=0;
    const auto append_command_marker=[&](uint32_t id,Vec3f point,uint32_t faction){
        if(command_marker_count>=512U)return;
        const auto* source=catalog_.effect_definition(id);if(!source)return;
        // Intent belongs to a visible owner. The destination itself need not be in LOS.
        auto definition=*source;
        // The stock mesh is roughly half a world unit wide. Keep its native geometry readable
        // at editor zoom levels, without changing the destination or gameplay footprint.
        const float diameter=std::max(0.01F,cached_sprite(definition.sprite_path).bounding_radius()*2.F);
        const auto eye_delta=sub(point,camera_.eye());
        const float eye_distance=std::sqrt(dot(eye_delta,eye_delta));
        const float units_per_pixel=2.F*eye_distance*std::tan(camera_.fov_y*0.5F)/std::max(1.F,workspace_layout().canvas.height);
        const float size_multiplier=std::clamp(14.F*units_per_pixel/diameter,1.F,64.F);
        definition.model_scale_x_q12=int32_t(std::lround(definition.model_scale_x_q12*size_multiplier));
        definition.model_scale_y_q12=int32_t(std::lround(definition.model_scale_y_q12*size_multiplier));
        definition.state_tint_argb=0xFF000000U|thandor::graphics_family_representative_rgb24(graphics_family_for_faction(faction));
        point.z=document_->field().sample_top_surface_height(point.x,point.y)+0.04F;
        auto orientation=fixed_model_rotation_basis(0U,0x4000U,0U);orientation.translation=point;
        const size_t first_run=next.draw_runs.size();
        emitting_command_overlay=true;
        const bool emitted=append_effect_visual(definition,point,
            runtime_visual_tick%std::max(1U,thandor::effect_lifetime_ticks(definition)),0.F,0U,&orientation);
        emitting_command_overlay=false;
        for(size_t i=first_run;i<next.draw_runs.size();++i){
            next.draw_runs[i].retail_world_pass=RetailWorldModelPass::EditorCommandOverlay;
            command_marker_triangle_count_+=next.draw_runs[i].index_count/3U;
        }
        if(emitted)++command_marker_count;
        command_marker_count_=command_marker_count;
    };
    if(simulation_lab_active()){
        const auto runs=simulation_lab_runs();
        if(!runs.empty()){
            const auto& run=runs[static_cast<size_t>(std::clamp(simulation_lab_selected_run_,0,int(runs.size())-1))];
            command_marker_run_index_=static_cast<int>(run.run_index);
            thandor::simulation_lab_visit_command_markers(run,
                [&](size_t,const thandor::SimulationLabEntity& owner){
                    return fog_preview_mode_==0||fog_preview_point_visible(owner.actor.position);
                },[&](const thandor::SimulationCommandMarker& marker){
                    append_command_marker(marker.effect_id,marker.point,marker.faction_index);
                    return command_marker_count<512U;
                },&command_marker_diagnostics_);
        }
    }else{
        for(const auto& [index,actor]:simulation_actor_states_){
            if(index>=placements.size())continue;
            if(fog_preview_mode_!=0&&!fog_preview_point_visible(actor.position))continue;
            if(const auto target=simulation_attack_placement_targets_.find(index);target!=simulation_attack_placement_targets_.end()&&target->second<placements.size())
                append_command_marker(149U,placement_command_target_point(target->second),placements[index].faction_index);
            else if(const auto target=simulation_attack_point_targets_.find(index);target!=simulation_attack_point_targets_.end())
                append_command_marker(149U,target->second,placements[index].faction_index);
            else if(actor.move_target&&thandor::simulation_drive_marker_pending(actor,{},*actor.move_target))
                append_command_marker(148U,*actor.move_target,placements[index].faction_index);
            if(actor.move_target&&actor.move_waypoint&&thandor::simulation_drive_marker_pending(actor,{},*actor.move_waypoint,true))
                append_command_marker(148U,*actor.move_waypoint,placements[index].faction_index);
        }
        for(const auto& [serial,entity]:simulation_produced_entities_){
            (void)serial;
            if(entity.actor.move_target&&thandor::simulation_drive_marker_pending(entity.actor,{},*entity.actor.move_target))
                append_command_marker(148U,*entity.actor.move_target,entity.faction_index);
            if(entity.actor.move_target&&entity.actor.move_waypoint&&thandor::simulation_drive_marker_pending(entity.actor,{},*entity.actor.move_waypoint,true))
                append_command_marker(148U,*entity.actor.move_waypoint,entity.faction_index);
        }
        for(size_t index=0;index<placements.size();++index){
            const auto* factory=catalog_.army_preset(placements[index].army_asset_id);
            if(!factory||factory->runtime_model_class_id!=13U)continue;
            const auto custom=simulation_production_rally_points_.find(index);
            if(custom!=simulation_production_rally_points_.end())append_command_marker(143U,custom->second,placements[index].faction_index);
            else if(const auto points=simulation_class13_spawn_and_exit_points(index,*factory))
                append_command_marker(143U,points->second,placements[index].faction_index);
        }
    }

    command_marker_build_milliseconds_=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-command_marker_start).count();

    if(!movement_ground_effects_.empty()){
        for(const auto& instance:movement_ground_effects_){
            if(instance.ground_mark&&!show_movement_ground_marks_)continue;
            const auto* definition=catalog_.effect_definition(instance.effect_id);
            if(definition==nullptr||runtime_visual_tick<instance.spawn_tick)continue;
            const uint32_t age=runtime_visual_tick-instance.spawn_tick;
            if(age>=thandor::effect_lifetime_ticks(*definition))continue;
            if(fog_preview_mode_!=0&&!fog_preview_point_visible(instance.origin))continue;
            const float yaw=std::atan2(instance.orientation.basis[3],instance.orientation.basis[0]);
            append_effect_visual(*definition,instance.origin,age,yaw,0U,&instance.orientation,
                std::nullopt,0U,instance.exact_runtime_angles);
        }
    }

    if(animate_timed_effects_&&!damage_effect_controllers_.empty()){
        const auto lab_damage_runs=simulation_lab_active()?simulation_lab_runs():
            std::span<const thandor::SimulationLabRun>{};
        const thandor::SimulationLabRun* lab_damage_run=lab_damage_runs.empty()?nullptr:
            &lab_damage_runs[static_cast<std::size_t>(std::clamp(simulation_lab_selected_run_,0,
                static_cast<int>(lab_damage_runs.size())-1))];
        // Avoid a linear entity scan for every authored damage controller at every UI tick.
        std::unordered_map<std::size_t,const thandor::SimulationLabEntity*> lab_damage_owners;
        if(lab_damage_run){
            lab_damage_owners.reserve(lab_damage_run->entities.size());
            for(const auto& entity:lab_damage_run->entities){
                if(!entity.synthetic)
                    lab_damage_owners.try_emplace(entity.source_placement_index,&entity);
            }
        }
        for(std::size_t controller_index=0U;controller_index<damage_effect_controllers_.size();++controller_index){
            const auto& source=damage_effect_controllers_[controller_index];
            if(simulation_lab_active()&&simulation_lab_suppressed_placements_.contains(source.placement_index))continue;
            // Authored LEV objects also belong to the selected Lab runtime. A document's
            // editor-only preview integrity is not authoritative while that runtime owns it.
            bool lab_live_damage_authorized=false;
            if(lab_damage_run){
                const auto owner=lab_damage_owners.find(source.placement_index);
                // A retained terminal owner must not fall back to the editor-only preview
                // health after its runtime geometry and effects have been released.
                if(owner!=lab_damage_owners.end() && owner->second->runtime_removed)continue;
                if(owner!=lab_damage_owners.end() && owner->second->runtime_model_hierarchy){
                    const auto live_effect=thandor::simulation_lab_damage_emitter_for_model(
                        *owner->second,*owner->second->runtime_model_hierarchy,source.model_node_index);
                    if(!live_effect || *live_effect!=source.effect_id)continue;
                    lab_live_damage_authorized=true;
                }
            }
            if(source.placement_index>=placements.size()||
               source.placement_index>=document_->placement_tags().size())continue;
            const auto& tag=document_->placement_tags()[source.placement_index];
            int32_t integrity_percent=static_cast<int32_t>(tag.preview_integrity_percent);
            if(const auto runtime_integrity=simulation_integrity_states_.find(source.placement_index);
               runtime_integrity!=simulation_integrity_states_.end()&&
               runtime_integrity->second.maximum_integrity>0){
                const int64_t scaled=static_cast<int64_t>(runtime_integrity->second.integrity)*100;
                integrity_percent=static_cast<int32_t>(std::clamp<int64_t>(
                    scaled/runtime_integrity->second.maximum_integrity,0,100));
            }
            // A live selected-run component already passed the authored strict threshold;
            // do not veto it with the unrelated editor preview integrity percentage.
            if(!lab_live_damage_authorized &&
               integrity_percent>=source.health_threshold_percent)continue;
            const auto& placement=placements[source.placement_index];
            if(!placement_visible_in_canvas(source.root_position,source.visibility_radius)&&
               !(source.placement_index<placement_pick_bounds_.size()&&
                 bounds_visible_in_canvas(placement_pick_bounds_[source.placement_index],2.0F)))continue;
            const auto* preset=catalog_.army_preset(placement.army_asset_id);
            if(preset==nullptr)continue;
            ModelHierarchyTransform runtime_transform=source.root_orientation;
            thandor::SimulationActorState fallback_actor;
            fallback_actor.position={placement.render_x(),placement.render_y(),
                terrain_height_near(placement.render_x(),placement.render_y())};
            fallback_actor.body_yaw_turn16=placement.orientation_turn16&0xFFFFU;
            thandor::SimulationActorState* actor=&fallback_actor;
            if(const auto live=simulation_actor_states_.find(source.placement_index);
               live!=simulation_actor_states_.end())actor=&live->second;
            const auto transforms=simulation_hierarchy_transforms(source.placement_index,*preset,*actor);
            if(source.model_node_index<transforms.size())runtime_transform=transforms[source.model_node_index];
            std::vector<std::pair<uint32_t,Vec3f>> attachments;
            std::vector<uint32_t> attachment_keys;
            attachments.reserve(source.local_type3_attachments.size());
            attachment_keys.reserve(source.local_type3_attachments.size());
            for(const auto& [key_index,local]:source.local_type3_attachments){
                attachments.emplace_back(key_index,transform_point(runtime_transform,local));
                attachment_keys.push_back(key_index);
            }

            const uint32_t stable_seed=resource_animation_rng_seed_^source.effect_id^
                static_cast<uint32_t>(source.placement_index*0x9E3779B9U)^static_cast<uint32_t>(controller_index);
            const auto schedule=thandor::build_damage_effect_schedule_for_attachment_keys(
                source.timing,attachment_keys,stable_seed,2048U,128U);
            if(schedule.cycle_ticks==0U)continue;
            const auto* definition=catalog_.effect_definition(source.effect_id);
            if(definition==nullptr)continue;
            const uint32_t lifetime=std::max(1U,thandor::effect_lifetime_ticks(*definition));
            for(const auto& pulse:schedule.pulses){
                const uint32_t age=(runtime_visual_tick%schedule.cycle_ticks+
                    schedule.cycle_ticks-pulse.spawn_tick%schedule.cycle_ticks)%schedule.cycle_ticks;
                if(age>=lifetime)continue;
                Vec3f origin=runtime_transform.translation;
                if(!attachments.empty()){
                    const auto found=std::find_if(attachments.begin(),attachments.end(),
                        [&](const auto& entry){return entry.first==pulse.attachment_index;});
                    if(found!=attachments.end())origin=found->second;
                }
                const EffectRuntimeAngles exact_angles{pulse.world_rotation_angle0,
                    pulse.world_rotation_angle1,pulse.world_rotation_angle2};
                ModelHierarchyTransform effect_orientation=fixed_model_rotation_basis(
                    exact_angles[2],exact_angles[1],exact_angles[0]);
                effect_orientation.translation=origin;
                // The damage updater calls EffectRuntimePool_CreateInstanceFromDefinitionCf with
                // creationFlags=0/owner=0 and three explicit random angles. These are world-space
                // effect angles; they do not inherit the damaged army node's orientation.
                append_effect_visual(*definition,origin,age,0.0F,0U,&effect_orientation,std::nullopt,
                    graphics_family_for_faction(placement.faction_index),exact_angles);
            }
        }
    }

    // Editor-only weapon test simulation. This follows the executable's separate runtime
    // objects: one launch EFF at the SHT kind-3 attachment, one SHT model, periodic secondary
    // EFF instances from that same attachment, and one nearest-surface impact EFF.
    std::span<const thandor::SimulationProjectileState> visible_projectiles=simulation_projectiles_;
    const thandor::SimulationLabRun* projectile_lab_run=nullptr;
    if(simulation_lab_active()){
        const auto runs=simulation_lab_runs();if(!runs.empty()){
            projectile_lab_run=&runs[static_cast<size_t>(std::clamp(simulation_lab_selected_run_,0,int(runs.size())-1))];
            visible_projectiles=projectile_lab_run->projectiles;
        }
    }
    if((simulation_mode_||projectile_lab_run)&&!visible_projectiles.empty()){
        constexpr float kOneShotPreviewCycle=16777216.0F;
        const auto placements=document_->level().placements();
        for(const auto& projectile:visible_projectiles){
            const auto* shot=catalog_.shot_definition(projectile.shot_definition_id);
            if(!shot)continue;
            const uint32_t age=runtime_visual_tick-projectile.spawn_tick;
            const float yaw=thandor::turn16_to_radians(projectile.launch_yaw_turn16);
            const uint32_t owner_faction=projectile_lab_run
                ?(projectile.owner_placement_index<projectile_lab_run->entities.size()
                    ?projectile_lab_run->entities[projectile.owner_placement_index].faction_index:0U)
                :(projectile.owner_placement_index<placements.size()?placements[projectile.owner_placement_index].faction_index:0U);
            const uint32_t faction=graphics_family_for_faction(owner_faction);
            const auto initial_orientation=shot_model_transform(projectile.launch_position,
                projectile.launch_yaw_turn16,projectile.launch_pitch_turn16);
            const auto& shot_sprite=cached_sprite(shot->sprite_path);
            Vec3f shot_emitter=projectile.launch_position;
            for(const auto& attachment:shot_sprite.lookup_records()){
                if(attachment.key_class().value()==3U){
                    shot_emitter=transform_point(initial_orientation,attachment.local_translation);
                    break;
                }
            }
            if(const auto* launch=catalog_.effect_definition(shot->launch_effect_id);launch&&(!projectile_lab_run||projectile.owner_kind==thandor::SimulationProjectileOwnerKind::Environmental)){
                ModelHierarchyTransform launch_orientation=initial_orientation;
                launch_orientation.translation=shot_emitter;
                append_effect_visual(*launch,shot_emitter,age,yaw,
                    static_cast<uint32_t>(projectile.serial),&launch_orientation,std::nullopt,faction);
            }

            const bool flight_visible=shot->trajectory_mode==0
                ?age<projectile.lifetime_ticks:age<projectile.travel_ticks;
            if(flight_visible){
                const Vec3f position=thandor::simulation_projectile_position(
                    projectile,*shot,runtime_visual_tick);
                const Vec3f next_position=thandor::simulation_projectile_position(
                    projectile,*shot,runtime_visual_tick+1U);
                const auto flight_orientation=projectile_model_pose(projectile,*shot,age,
                    position,next_position);
                // The SHT model is one model node. Mode zero does not advance a projectile: the
                // renderer stretches the authored local +Z geometry to the nearest ray distance.
                thandor::EffectDefinitionRecord flight;
                flight.definition_id=0x80000000U|projectile.shot_definition_id;
                flight.runtime_value_0c=std::max(1U,shot->projectile_lifetime_ticks);
                flight.frame_advance_threshold_q4=16U;
                flight.state_tint_argb=shot->state_tint_argb==0U?0xFFFFFFFFU:shot->state_tint_argb;
                flight.model_scale_x_q12=0x1000;flight.model_scale_y_q12=0x1000;
                flight.sprite_path=shot->sprite_path;
                const std::optional<float> beam_length=shot->trajectory_mode==0
                    ?std::optional<float>{thandor::simulation_mode0_beam_length_world(projectile,*shot)}
                    :std::nullopt;
                selected_lab_shot_model_tint=(projectile_lab_run && projectile.terrain_refresh_tick!=0U)
                    ?std::optional<uint32_t>{projectile.terrain_tint_argb}:std::nullopt;
                append_effect_visual_recursive(flight,position,age,yaw,
                    static_cast<uint32_t>(projectile.serial),&flight_orientation,
                    thandor::simulation_shot_frame_index(*shot,age),faction,0U,beam_length,std::nullopt);
                selected_lab_shot_model_tint.reset();

                if(const auto* trail=catalog_.effect_definition(shot->secondary_effect_id)){
                    const uint32_t interval=std::max(1U,shot->secondary_effect_interval_ticks);
                    const uint32_t life=thandor::effect_lifetime_ticks(*trail);
                    const uint32_t first=age>life?((age-life)/interval+1U)*interval:interval;
                    for(uint32_t trail_spawn=first;trail_spawn<=age;trail_spawn+=interval){
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
                    // ShotRuntimePool_CreateProjectileFromDefinitionCf resolves packed lookup
                    // class 4/index 0 once at creation, transforms that point by the launch pose,
                    // and allocates the GraphicsShadingRuntime record there. Non-direct-line
                    // maintenance translates that record by the same delta as the shot model; it
                    // does not rotate the offset again if the model presentation pitch changes.
                    Vec3f shading_position=position;
                    if(const auto* shading_attachment=shot_sprite.find_lookup_record(
                           thandor::ModelLookupKeyIndex{0U},thandor::ModelLookupKeyClass{4U})){
                        const Vec3f launch_shading_position=transform_point(
                            initial_orientation,shading_attachment->local_translation);
                        shading_position=add(launch_shading_position,
                            sub(position,projectile.launch_position));
                    }
                    const auto color=thandor::unpack_argb(0xFF000000U|
                        (shot->shading_color_argb&0x00FFFFFFU));
                    next.shading_emitters.push_back(ResourceShadingEmitter{shading_position,
                        static_cast<float>(radius_byte)/16.0F,color.r,color.g,color.b,
                        kOneShotPreviewCycle,static_cast<float>(projectile.spawn_tick),
                        static_cast<float>(std::max(1,shot->shading_transition_ticks)),
                        static_cast<float>(projectile.travel_ticks),
                        static_cast<float>(std::max(1,shot->shading_release_transition_ticks))});
                }
            }

            if(!projectile_lab_run&&age>=projectile.travel_ticks){
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
    // Fog is uploaded as per-cell runtime visibility and consumed by the terrain/surface shaders.
    // Do not recreate G410's coplanar fog geometry: it depth-rejected against scenery and
    // intersected the independently displaced water/lava surface.
    const auto sort_started=std::chrono::steady_clock::now();
    order_dynamic_object_queue_like_game(next.vertices,next.indices,next.draw_runs,camera_);
    const auto sort_finished=std::chrono::steady_clock::now();
    const auto elapsed_ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
    dynamic_scene_fog_milliseconds_=elapsed_ms(fog_started,fog_finished);
    dynamic_scene_model_setup_milliseconds_=elapsed_ms(fog_finished,shadow_started);
    dynamic_scene_pre_shadow_milliseconds_=elapsed_ms(started,shadow_started);
    editor_performance_log_.record(EditorPerfStage::Fog,dynamic_scene_fog_milliseconds_);
    editor_performance_log_.record(EditorPerfStage::Models,dynamic_scene_model_setup_milliseconds_);
    editor_performance_log_.record(EditorPerfStage::Shadows,elapsed_ms(shadow_started,shadow_finished));
    editor_performance_log_.record(EditorPerfStage::Effects,elapsed_ms(shadow_finished,sort_started));
    editor_performance_log_.record(EditorPerfStage::DynamicSort,elapsed_ms(sort_started,sort_finished));
    dynamic_scene_shadow_milliseconds_=elapsed_ms(shadow_started,shadow_finished);
    dynamic_scene_effects_milliseconds_=elapsed_ms(shadow_finished,sort_started);
    dynamic_scene_sort_milliseconds_=elapsed_ms(sort_started,sort_finished);
    dynamic_texture_family_fast_hits_+=scene_family_fast_hits;
    dynamic_texture_family_fast_misses_+=scene_family_fast_misses;
    visible_animated_building_count_=visible_animated_buildings_.size();
    visible_timed_effect_controller_count_=visible_timed_effect_controllers_.size();
    dynamic_triangle_count_=next.indices.size()/3U;
    resource_shading_emitter_count_=next.shading_emitters.size();
    cache.valid=!runtime_shadow_collector_;
    dynamic_scene_cpu_=std::move(next);
    ++dynamic_scene_revision_;
    dynamic_scene_build_milliseconds_=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-started).count();
}

void EditorApp::upload_dynamic_scene(uint32_t frame_index){
    if(frame_index>=VulkanContext::kFramesInFlight)throw std::runtime_error("dynamic upload frame index is out of range");
    if(uploaded_dynamic_scene_revisions_[frame_index]==dynamic_scene_revision_)return;
    auto performance_timer=editor_performance_log_.measure(EditorPerfStage::DynamicUpload);
    renderer_.update_dynamic_objects(frame_index,dynamic_scene_cpu_.vertices,
        dynamic_scene_cpu_.indices,dynamic_scene_cpu_.draw_runs,
        dynamic_scene_cpu_.shadow_vertices,dynamic_scene_cpu_.shadow_indices);
    renderer_.update_resource_shading(frame_index,dynamic_scene_cpu_.shading_emitters);
    renderer_.update_fog_visibility(frame_index,fog_preview_runtime_cells_,fog_preview_mode_!=0);
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
    const uint32_t family=graphics_family_for_faction(faction_index);
    status_=std::string("Loaded ")+thandor::graphics_family_single_player_faction_name(family)+
        " (faction "+std::to_string(faction_index)+") camera start from the LEV header.";
}

void EditorApp::aim_camera_at_faction_start_vehicle(uint32_t faction_index){
    if(!document_)return;
    for(const auto& placement:document_->level().placements()){
        if(placement.army_asset_id!=50U||placement.faction_index!=faction_index)continue;
        const float x=placement.render_x();
        const float y=placement.render_y();
        camera_.target={x,y,terrain_height_near(x,y)};
        const uint32_t family=graphics_family_for_faction(faction_index);
        status_=std::string("Aimed the editor view at ")+
            thandor::graphics_family_single_player_faction_name(family)+" (faction "+
            std::to_string(faction_index)+") start vehicle. Adjust the view, then save it as the faction camera.";
        return;
    }
    const uint32_t family=graphics_family_for_faction(faction_index);
    status_=std::string(thandor::graphics_family_single_player_faction_name(family))+" (faction "+
        std::to_string(faction_index)+") has no stock player-start vehicle (ARM 50).";
}

void EditorApp::center_camera(){if(!document_)return;const auto& f=document_->field();const auto& a=f.at(0,0);const auto& b=f.at(f.width()-1,f.height()-1);camera_.target={(thandor::q12_to_float(a.world_x_q12)+thandor::q12_to_float(b.world_x_q12))*0.5f,(thandor::q12_to_float(a.world_y_q12)+thandor::q12_to_float(b.world_y_q12))*0.5f,0};camera_.distance=std::max(15.0f,std::hypot(thandor::q12_to_float(b.world_x_q12-a.world_x_q12),thandor::q12_to_float(b.world_y_q12-a.world_y_q12))*0.9f);}
EditorApp::Snapshot EditorApp::snapshot()const{return {document_->field().serialize(),document_->level().serialize(),std::vector<thandor::PlacementTag>(document_->placement_tags().begin(),document_->placement_tags().end()),document_->faction_technology_masks(),document_->map_name(),document_->display_name(),document_->author(),document_->default_human_faction(),document_->scenario_record_bytes(),document_->level_text_bytes()};}
void EditorApp::restore(const Snapshot& s){reset_terrain_generator();document_->field()=thandor::FieldGrid::parse(s.field);document_->level()=thandor::LevelAsset::parse(s.level);document_->placement_tags()=s.tags;document_->faction_technology_masks()=s.faction_technology_masks;document_->set_map_name(s.map_name);document_->set_display_name(s.display_name);document_->set_author(s.author);document_->set_default_human_faction(s.default_human_faction);document_->restore_editor_metadata(s.scenario_record,s.level_text);document_->mark_dirty();selected_edit_index_.reset();faction_technology_hierarchy_cache_.clear();faction_technology_weapon_profile_cache_.clear();asset_preview_cache_.clear();reset_simulation(true);refresh_scenario_text_buffers();load_materials();rebuild_mesh();}
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

bool EditorApp::placement_has_required_resource_support(
    const thandor::ArmyAssetPreset& preset,const thandor::LevelPlacement& placement) const noexcept{
    if(!document_||!preset.has_placement_occupancy_class)return true;
    const auto sample=document_->field().sample_terrain(placement.render_x(),placement.render_y());
    if(!sample.inside)return false;
    const uint32_t x=static_cast<uint32_t>(std::clamp<long>(
        std::lround(sample.grid_x),0L,static_cast<long>(document_->field().width()-1U)));
    const uint32_t y=static_cast<uint32_t>(std::clamp<long>(
        std::lround(sample.grid_y),0L,static_cast<long>(document_->field().height()-1U)));
    // The live class-14 updater uses the same center-cell support selector. Keep the strict
    // one-cell border rule from the placement callback too: an extractor at that border is not a
    // retail-valid class-14 placement even if a hand-edited FLD happens to carry the support bit.
    return !document_->field().terrain_rejects_placement_occupancy_class(
        x,y,preset.placement_occupancy_class);
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

std::span<const thandor::ModelVisualNode> EditorApp::placement_hierarchy(
    const thandor::ArmyAssetPreset& preset,std::size_t placement_index)const noexcept{
    if(!document_||placement_index>=document_->level().placements().size()){
        if(!preset.resolved_model_hierarchy.empty())return preset.resolved_model_hierarchy;
        return preset.model_hierarchy;
    }
    const uint32_t faction=std::min(document_->level().placements()[placement_index].faction_index,7U);
    const auto& mask=document_->faction_technology_mask(faction);
    const std::string key=technology_hierarchy_cache_key(preset.registry_id,faction,mask);
    if(const auto found=faction_technology_hierarchy_cache_.find(key);
       found!=faction_technology_hierarchy_cache_.end())return found->second;
    auto resolved=thandor::resolve_faction_technology_hierarchy(preset,mask);
    if(resolved.empty())resolved=!preset.resolved_model_hierarchy.empty()?preset.resolved_model_hierarchy:
        preset.model_hierarchy;
    const auto inserted=faction_technology_hierarchy_cache_.emplace(key,std::move(resolved));
    return inserted.first->second;
}

std::span<const thandor::SimulationWeaponProfile> EditorApp::placement_weapon_profiles(
    const thandor::ArmyAssetPreset& preset,std::size_t placement_index)const{
    if(!document_||placement_index>=document_->level().placements().size())
        return preset.simulation_weapon_profiles;
    const uint32_t faction=std::min(document_->level().placements()[placement_index].faction_index,7U);
    const auto& mask=document_->faction_technology_mask(faction);
    const std::string key=technology_hierarchy_cache_key(preset.registry_id,faction,mask);
    if(const auto found=faction_technology_weapon_profile_cache_.find(key);
       found!=faction_technology_weapon_profile_cache_.end())return found->second;
    auto profiles=catalog_.simulation_weapon_profiles_for_hierarchy(
        placement_hierarchy(preset,placement_index));
    const auto inserted=faction_technology_weapon_profile_cache_.emplace(key,std::move(profiles));
    return inserted.first->second;
}

bool EditorApp::placement_has_recovery_weapon(std::size_t placement_index)const{
    if(!document_||placement_index>=document_->level().placements().size())return false;
    const auto& placement=document_->level().placements()[placement_index];
    const auto* preset=catalog_.army_preset(placement.army_asset_id);
    if(!preset)return false;
    for(const auto& weapon:placement_weapon_profiles(*preset,placement_index)){
        if(const auto* shot=catalog_.shot_definition(weapon.shot_definition_id);
           shot&&shot->is_repair_or_recovery_shot())return true;
    }
    return false;
}

bool EditorApp::placement_has_hostile_weapon(std::size_t placement_index)const{
    if(!document_||placement_index>=document_->level().placements().size())return false;
    const auto& placement=document_->level().placements()[placement_index];
    const auto* preset=catalog_.army_preset(placement.army_asset_id);
    if(!preset)return false;
    for(const auto& weapon:placement_weapon_profiles(*preset,placement_index)){
        if(const auto* shot=catalog_.shot_definition(weapon.shot_definition_id);
           shot&&!shot->is_repair_or_recovery_shot())return true;
    }
    return false;
}

void EditorApp::add_asset_placement(uint32_t x,uint32_t y,uint32_t asset_id){
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

    if(preset->has_placement_occupancy_class&&
       document_->field().terrain_rejects_placement_occupancy_class(
           x,y,preset->placement_occupancy_class)){
        const char* support=preset->placement_occupancy_class==0U?"Xenite field (FLD 0x0800)":
            (preset->placement_occupancy_class==1U?"Tritium field (FLD 0x1000)":"required resource field");
        status_="Cannot place "+preset->display_name+": runtime class 14 requires "+support+
            " at the building centre. Place/paint the matching deposit field first.";
        return;
    }

    checkpoint();
    const auto& cell=document_->field().at(x,y);
    thandor::LevelPlacement placement{};
    const uint32_t placed_asset_id=thandor::canonical_placement_registry_id(*preset);
    placement.army_asset_id=placed_asset_id;
    placement.faction_index=faction;
    placement.set_render_position_q12(cell.world_x_q12,cell.world_y_q12);
    placement.orientation_turn16=selected_orientation_;
    document_->level().add_placement(placement);
    document_->placement_tags().push_back({document_->level().placements().size()-1U,role,
        preset->display_name,100U});
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
    if(preset->runtime_uses_class17_root_child_deployment){
        animate_units_=true;
        simulation_state_for(*selected_placement_,SimulationStateOrigin::FreshPlacement);
    }
    rebuild_objects();

    status_="Placed "+preset->display_name+" (ARM "+std::to_string(placed_asset_id)+") from the 3D asset browser.";
    if(placed_asset_id!=asset_id)
        status_+=" The selected stock alias "+std::to_string(asset_id)+" was normalized to the canonical ID.";
    if(preset->has_placement_occupancy_class){
        const char* support=preset->placement_occupancy_class==0U?"Xenite":"Tritium";
        status_+=" Resource extractor is bound to the underlying "+std::string(support)+
            " support field; its class-14 model animation is active only while that support bit is present.";
    }
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
void EditorApp::delete_selected_placement(){if(!selected_placement_)return;checkpoint();const auto index=*selected_placement_;reset_simulation(false);simulation_static_replacement_dirty_=false;document_->level().erase_placement(index);auto& tags=document_->placement_tags();tags.erase(std::remove_if(tags.begin(),tags.end(),[&](auto& t){if(t.placement_index==index)return true;if(t.placement_index>index)--t.placement_index;return false;}),tags.end());selected_placement_.reset();selected_simulation_lab_entity_.reset();selected_edit_index_.reset();document_->mark_dirty();rebuild_objects();}
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
