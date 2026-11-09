#pragma once
// Static buffers are per-frame in both builds. Full atlas versioning is optional.
#define THANDOR_FRAME_VERSIONED_OBJECT_ATLAS 1
#include "editor_workspace.hpp"

#include "math.hpp"
#include "primitive_render.hpp"
#include "retail_world_render.hpp"
#include "texture_sampling.hpp"
#include "object_texture_updates.hpp"
#include "vulkan_context.hpp"
#include "thandor/field.hpp"
#include "thandor/gfx.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace editor {

struct ObjectVertex {
    Vec3f position;
    Vec3f normal;
    float u{};
    float v{};
    uint32_t texture_layer{};
    uint32_t packed_color_argb{};
    // Nonzero mirrors the DirectDraw model-vertex 0x0200 direct-tint branch.  The
    // vertex shader must not feed those vertices through dynamic resource lighting.
    uint32_t direct_tint_bypass{};
    // Building runtime class 13 animates a selected root texture subresource by
    // changing the model-node V offset in the stock 0..0x80000 Q12 range.
    float animation_v_amplitude{};
    float animation_cycle_ticks{};
    // World model geometry only: particles, editor markers and ghosts never cast.
    uint32_t casts_shadow{};
};

struct ObjectDrawRun {
    PrimitiveBlendPreset blend{PrimitiveBlendPreset::OpaqueDepthWrite};
    bool two_sided{};
    uint32_t first_index{};
    uint32_t index_count{};
    // Zero keeps the historical triangle-granular sort. Nonzero keeps all triangles from one
    // billboard/effect instance atomic while the packet itself is depth sorted.
    uint32_t sort_group{};
    RetailWorldModelPass retail_world_pass{RetailWorldModelPass::PostGeneratedShading};
};

// Terrain and model shaders consume these as scheduled GraphicsShadingRuntime pulse previews.
// A static LEV has no persisted live timer/RNG state, so the CPU builds a deterministic repeating
// schedule from the user-visible DirectDraw resource-animation seed.
struct ResourceShadingEmitter {
    Vec3f position;
    float target_radius{};
    float color_r{};
    float color_g{};
    float color_b{};
    float cycle_ticks{};
    float spawn_tick{};
    float grow_ticks{};
    float stop_ticks{};
    float release_ticks{};
};

class TerrainRenderer {
public:
    explicit TerrainRenderer(VulkanContext& context);
    ~TerrainRenderer();
    TerrainRenderer(const TerrainRenderer&) = delete;
    TerrainRenderer& operator=(const TerrainRenderer&) = delete;

    void update_mesh(const thandor::FieldGrid& field, const thandor::TerrainLighting& lighting,
                     const thandor::TerrainTextureLayout* texture_layout,
                     const thandor::TerrainSurfaceTextureLayout* surface_layout,
                     bool exact_soil_packets = true,
                     uint32_t soil_random_seed = thandor::FieldGrid::kDefaultSoilRandomSeed,
                     uint32_t surface_wave_random_seed =
                         thandor::FieldGrid::kDefaultSurfaceWaveRandomSeed);
    void update_materials(std::span<const thandor::RgbaImage> images);
    void update_surface_materials(std::span<const thandor::RgbaImage> images);
    void update_objects(std::span<const ObjectVertex> vertices,
                        std::span<const uint32_t> indices,
                        std::span<const ObjectDrawRun> runs,
                        std::span<const ObjectVertex> shadow_vertices,
                        std::span<const uint32_t> shadow_indices,
                        std::span<const thandor::RgbaImage> texture_layers);
    void update_dynamic_objects(uint32_t frame_index,
                                std::span<const ObjectVertex> vertices,
                                std::span<const uint32_t> indices,
                                std::span<const ObjectDrawRun> runs,
                                std::span<const ObjectVertex> shadow_vertices,
                                std::span<const uint32_t> shadow_indices);
    void update_resource_shading(uint32_t frame_index,
                                 std::span<const ResourceShadingEmitter> emitters);
    // Retail fog is per-FLD-cell runtime state. Upload the temporally clamped visibility byte
    // instead of drawing a second terrain mesh, so hills and animated water/lava share depth.
    void update_fog_visibility(uint32_t frame_index, std::span<const uint8_t> runtime_visibility,
                               bool enabled);
    // The original primitive queue keeps opaque packets in material order and stable-sorts every
    // alpha/additive packet by projected depth. Re-sort only the retained static index packets when
    // the camera changes; vertices and texture arrays stay untouched.
    void update_depth_sorted_static_objects(uint32_t frame_index, const Camera& camera);
    void update_depth_sorted_surface(uint32_t frame_index, const Camera& camera, bool animate_surface);
    void set_presentation_tick(uint32_t tick) noexcept { presentation_tick_ = tick; }
    uint32_t presentation_tick() const noexcept { return presentation_tick_; }
    void set_building_animation_tick(uint32_t tick) noexcept { building_animation_tick_ = tick; }
    uint32_t building_animation_tick() const noexcept { return building_animation_tick_; }
    uint32_t object_index_count() const noexcept { return object_index_count_; }
    uint32_t shadow_index_count() const noexcept { return shadow_index_count_; }
    uint32_t object_draw_run_count() const noexcept { return static_cast<uint32_t>(object_draw_runs_.size()); }
    uint32_t resource_shading_record_count() const noexcept { return resource_shading_record_count_; }
    const ObjectUploadStats& object_upload_stats()const noexcept{return object_upload_stats_;}
    uint32_t object_texture_layer_count() const noexcept { return object_texture_layer_count_; }
    // Applied after the CPU rebuild and before acquiring this slot for drawing.
    // No other frame slot's sampled images or static vertex buffers are changed.
    void prepare_static_frame(uint32_t frame_index, VkCommandBuffer command);
    uint32_t maximum_object_texture_layers() const noexcept {
        return context_.max_image_array_layers();
    }
    void prepare_shadow_frame(uint32_t frame_index, VkCommandBuffer command,
                              const Camera& camera, float aspect, bool enabled);
    void render(VkCommandBuffer command, const Camera& camera, bool draw_surface, bool wireframe,
                uint32_t frame_index, bool animate_surface, bool animate_buildings, bool animate_units,
                bool hide_simulated_buildings, bool hide_simulated_units,
                WorkspaceRect canvas, std::array<float,2> logical_size);
    std::span<const thandor::TerrainVertex> cpu_vertices() const noexcept { return cpu_vertices_; }

private:
    struct Buffer {
        VkBuffer buffer{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkDeviceSize size{};
    };
    struct ImageArray {
        VkImage image{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VkSampler sampler{VK_NULL_HANDLE};
        uint32_t layers{1};
        uint32_t capacity_layers{1};
        uint32_t extent{1};
        uint32_t mip_levels{1};
    };

    static constexpr std::size_t kBlendPresetCount = 4U;
    static constexpr std::size_t kCullVariantCount = 2U;
    static constexpr std::size_t kObjectPipelineCount = kBlendPresetCount * kCullVariantCount;

    VulkanContext& context_;
    Buffer vertex_buffer_, index_buffer_, surface_vertex_buffer_, surface_index_buffer_;
    std::array<Buffer,VulkanContext::kFramesInFlight> object_vertex_buffers_{},object_index_buffers_{};
    std::array<Buffer,VulkanContext::kFramesInFlight> shadow_vertex_buffers_{},shadow_index_buffers_{};
    std::array<std::array<uint64_t,4>,VulkanContext::kFramesInFlight> static_buffer_frame_revisions_{};
    std::array<uint64_t,4> static_buffer_revisions_{1,1,1,1};
    std::array<Buffer, VulkanContext::kFramesInFlight> dynamic_object_vertex_buffers_{};
    std::array<Buffer, VulkanContext::kFramesInFlight> dynamic_object_index_buffers_{};
    std::array<Buffer, VulkanContext::kFramesInFlight> dynamic_shadow_vertex_buffers_{};
    std::array<Buffer, VulkanContext::kFramesInFlight> dynamic_shadow_index_buffers_{};
    std::array<Buffer, VulkanContext::kFramesInFlight> depth_sorted_static_index_buffers_{};
    std::array<Buffer, VulkanContext::kFramesInFlight> depth_sorted_surface_index_buffers_{};
    std::array<Buffer, VulkanContext::kFramesInFlight> resource_shading_buffers_{};
    std::array<Buffer, VulkanContext::kFramesInFlight> fog_visibility_buffers_{};
    std::array<Buffer, VulkanContext::kFramesInFlight> uniform_buffers_{};
    uint32_t index_count_{};
    uint32_t surface_index_count_{};
    uint32_t object_index_count_{};
    uint32_t shadow_index_count_{};
    uint32_t resource_shading_record_count_{};
    std::vector<ObjectDrawRun> object_draw_runs_;
    struct StaticTransparentTriangle {
        std::array<uint32_t,3> indices{};
        Vec3f centroid{};
        PrimitiveBlendPreset blend{PrimitiveBlendPreset::AlphaNoDepthWrite};
        bool two_sided{};
        uint32_t source_order{};
        RetailWorldModelPass retail_world_pass{RetailWorldModelPass::PostGeneratedShading};
    };
    std::vector<StaticTransparentTriangle> static_transparent_triangles_;
    std::vector<uint32_t> sorted_static_indices_;
    std::vector<ObjectDrawRun> sorted_static_draw_runs_;
    std::array<float,6> sorted_static_camera_state_{};
    bool sorted_static_camera_valid_{};
    uint64_t sorted_static_revision_{1U};
    std::array<uint64_t,VulkanContext::kFramesInFlight> uploaded_sorted_static_revisions_{};
    std::array<uint32_t,VulkanContext::kFramesInFlight> depth_sorted_static_index_counts_{};
    std::array<uint32_t,VulkanContext::kFramesInFlight> depth_sorted_surface_index_counts_{};
    std::array<std::vector<ObjectDrawRun>,VulkanContext::kFramesInFlight>
        depth_sorted_static_draw_runs_{};
    std::array<uint32_t, VulkanContext::kFramesInFlight> dynamic_object_index_counts_{};
    std::array<uint32_t, VulkanContext::kFramesInFlight> dynamic_shadow_index_counts_{};
    std::array<std::vector<ObjectDrawRun>, VulkanContext::kFramesInFlight> dynamic_object_draw_runs_{};
    std::vector<thandor::TerrainVertex> cpu_vertices_;
    std::vector<thandor::TerrainVertex> cpu_surface_vertices_;
    std::vector<uint32_t> sorted_surface_indices_;
    std::array<float,6> sorted_surface_camera_state_{};
    uint32_t sorted_surface_tick_{};
    bool sorted_surface_valid_{};
    bool sorted_surface_animated_{};
    uint64_t sorted_surface_revision_{1U};
    std::array<uint64_t,VulkanContext::kFramesInFlight> uploaded_surface_revisions_{};
    VkDescriptorSetLayout descriptor_layout_{VK_NULL_HANDLE};
    VkDescriptorPool descriptor_pool_{VK_NULL_HANDLE};
    std::array<VkDescriptorSet, VulkanContext::kFramesInFlight> descriptor_sets_{};
    VkPipelineLayout pipeline_layout_{VK_NULL_HANDLE};
    VkPipeline terrain_pipeline_{VK_NULL_HANDLE};
    VkPipeline wire_pipeline_{VK_NULL_HANDLE};
    VkPipeline surface_pipeline_{VK_NULL_HANDLE};
    static constexpr uint32_t kShadowExtent = 2048U;
    std::array<ImageArray,VulkanContext::kFramesInFlight> directional_shadow_images_{};
    std::array<VkFramebuffer,VulkanContext::kFramesInFlight> directional_shadow_frames_{};
    VkRenderPass directional_shadow_pass_{VK_NULL_HANDLE};
    VkPipeline directional_shadow_pipeline_{VK_NULL_HANDLE};
    Mat4 directional_shadow_matrix_{identity()};
    Vec3f shadow_ray_{0.5F,0.5F,-1.0F};
    float shadow_scene_radius_{256.0F};
    bool directional_shadows_enabled_{};
    void create_directional_shadows();
    VkPipeline shadow_pipeline_{VK_NULL_HANDLE};
    std::array<VkPipeline, kObjectPipelineCount> object_pipelines_{};
    std::array<VkPipeline,2> command_overlay_pipelines_{};
    VkRenderPass pipeline_render_pass_{VK_NULL_HANDLE};
    ImageArray terrain_textures_;
    // Initial fallback is kept alive; only frame-slot descriptors are updated for
    // versioned atlases, so submitted draws never see an overwritten descriptor.
    ImageArray object_textures_;
    std::array<ImageArray,VulkanContext::kFramesInFlight> object_texture_frame_images_{};
    // Retained after vkQueueSubmit: recycled only after the slot fence completes.
    std::array<Buffer,VulkanContext::kFramesInFlight> object_texture_frame_staging_{};
    std::array<ObjectTextureUpdateCache,VulkanContext::kFramesInFlight> object_texture_frame_caches_{};
    uint32_t object_texture_layer_count_{1};
    ObjectTextureUpdateCache object_texture_cache_;
    ObjectUploadStats object_upload_stats_;
    bool object_surface_valid_{};
    std::vector<ObjectVertex> retained_object_vertices_,retained_shadow_vertices_;
    std::vector<uint32_t> retained_object_indices_,retained_shadow_indices_;
    std::vector<ObjectDrawRun> retained_object_runs_;
    ImageArray surface_textures_;
    uint32_t presentation_tick_{};
    uint32_t building_animation_tick_{};

    static constexpr std::size_t object_pipeline_index(PrimitiveBlendPreset blend,
                                                        bool two_sided) noexcept {
        return static_cast<std::size_t>(blend) * kCullVariantCount + (two_sided ? 1U : 0U);
    }

    void create_descriptors();
    void create_default_materials();
    void create_pipelines();
    void destroy_pipelines();
    Buffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags properties, const void* initial = nullptr);
    void destroy_buffer(Buffer& buffer);
    void update_buffer(Buffer& destination, std::span<const std::byte> data,
                       VkBufferUsageFlags usage);
    bool update_reusable_host_buffer(Buffer& destination, std::span<const std::byte> data,
                                     VkBufferUsageFlags usage);
    VkShaderModule load_shader(const std::filesystem::path& path) const;
    void destroy_image_array(ImageArray& array);
    void upload_image_array(ImageArray& destination, std::span<const thandor::RgbaImage> images,
                            uint32_t extent, uint32_t maximum_layers,
                            VkSamplerAddressMode address_mode, uint32_t descriptor_binding,
                            TextureSamplingMode sampling_mode, VkFormat image_format,
                            uint32_t allocation_layers=0,
                            std::span<const uint8_t> prepared_pixels={},
                            uint32_t descriptor_frame=VulkanContext::kFramesInFlight);
    ImageArray create_async_object_frame_image(uint32_t frame_index, uint32_t capacity, uint32_t layers);
    void record_async_object_texture_copy(uint32_t frame_index, VkCommandBuffer command,
        ImageArray& destination, const ObjectTextureUpdate& plan,
        std::span<const uint8_t> pixels, bool first_use);
    void upload_object_texture_changes(ImageArray& destination, const ObjectTextureUpdate& plan,
                                      std::span<const uint8_t> pixels);
};

} // namespace editor
