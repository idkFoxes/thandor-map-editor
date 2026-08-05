#include "terrain_renderer.hpp"
#include "thandor/simulation.hpp"
#include "thandor/binary.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace editor {
namespace {
std::filesystem::path executable_directory() {
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return std::filesystem::current_path();
    path.resize(length);
    return std::filesystem::path(path).parent_path();
#else
    std::error_code error;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::current_path() : path.parent_path();
#endif
}

std::filesystem::path runtime_shader_directory() {
    if (const char* configured = std::getenv("THANDOR_SHADER_DIR");
        configured != nullptr && configured[0] != '\0') {
        return std::filesystem::path(configured);
    }

    const auto packaged = executable_directory() / THANDOR_SHADER_DIR_NAME;
    std::error_code error;
    if (std::filesystem::is_directory(packaged, error)) return packaged;

    return std::filesystem::path(THANDOR_SHADER_BUILD_DIR);
}

struct UniformBlock {
    Mat4 view_projection;
    float camera[4];
    float material_scale[4];
    float animation_controls[4];
};

struct alignas(16) ResourceShadingBufferHeader {
    uint32_t count{};
    uint32_t reserved0{};
    uint32_t reserved1{};
    uint32_t reserved2{};
};

struct alignas(16) ResourceShadingGpuRecord {
    float position_radius[4]{};
    float color_period[4]{};
    float phase_grow_stop_release[4]{};
};
static_assert(sizeof(ResourceShadingBufferHeader) == 16);
static_assert(sizeof(ResourceShadingGpuRecord) == 48);

template <class T>
std::span<const std::byte> bytes_of(const std::vector<T>& value) {
    return {reinterpret_cast<const std::byte*>(value.data()), value.size() * sizeof(T)};
}

std::vector<uint8_t> resample_rgba(const thandor::RgbaImage& source, uint32_t extent) {
    std::vector<uint8_t> result(static_cast<std::size_t>(extent) * extent * 4U, 0);
    const uint64_t source_size = static_cast<uint64_t>(source.width) * source.height * 4U;
    if (source.width == 0 || source.height == 0 || source_size != source.pixels.size()) return result;
    for (uint32_t y = 0; y < extent; ++y) {
        for (uint32_t x = 0; x < extent; ++x) {
            const uint32_t sx = std::min(source.width - 1U,
                static_cast<uint32_t>((static_cast<uint64_t>(x) * source.width) / extent));
            const uint32_t sy = std::min(source.height - 1U,
                static_cast<uint32_t>((static_cast<uint64_t>(y) * source.height) / extent));
            const std::size_t source_offset = (static_cast<std::size_t>(sy) * source.width + sx) * 4U;
            const std::size_t destination_offset = (static_cast<std::size_t>(y) * extent + x) * 4U;
            std::copy_n(source.pixels.begin() + static_cast<std::ptrdiff_t>(source_offset), 4,
                        result.begin() + static_cast<std::ptrdiff_t>(destination_offset));
        }
    }
    return result;
}

std::vector<uint8_t> downsample_rgba_2x(const std::vector<uint8_t>& source,
                                        uint32_t source_extent) {
    const uint32_t destination_extent = std::max(1U, source_extent / 2U);
    std::vector<uint8_t> result(static_cast<std::size_t>(destination_extent) *
                                destination_extent * 4U, 0U);
    for (uint32_t y = 0; y < destination_extent; ++y) {
        for (uint32_t x = 0; x < destination_extent; ++x) {
            std::array<uint32_t, 4> sum{};
            for (uint32_t oy = 0; oy < 2U; ++oy) {
                for (uint32_t ox = 0; ox < 2U; ++ox) {
                    const uint32_t sx = std::min(source_extent - 1U, x * 2U + ox);
                    const uint32_t sy = std::min(source_extent - 1U, y * 2U + oy);
                    const std::size_t source_offset =
                        (static_cast<std::size_t>(sy) * source_extent + sx) * 4U;
                    for (std::size_t channel = 0; channel < sum.size(); ++channel) {
                        sum[channel] += source[source_offset + channel];
                    }
                }
            }
            const std::size_t destination_offset =
                (static_cast<std::size_t>(y) * destination_extent + x) * 4U;
            for (std::size_t channel = 0; channel < sum.size(); ++channel) {
                result[destination_offset + channel] =
                    static_cast<uint8_t>((sum[channel] + 2U) / 4U);
            }
        }
    }
    return result;
}

thandor::RgbaImage checker(uint32_t extent, bool transparent) {
    thandor::RgbaImage image{extent, extent, {}};
    image.pixels.resize(static_cast<std::size_t>(extent) * extent * 4U);
    for (uint32_t y = 0; y < extent; ++y) {
        for (uint32_t x = 0; x < extent; ++x) {
            const uint8_t c = ((x / 16U + y / 16U) & 1U) != 0U ? 110U : 80U;
            const auto offset = (static_cast<std::size_t>(y) * extent + x) * 4U;
            image.pixels[offset] = c;
            image.pixels[offset + 1] = static_cast<uint8_t>(c + 25U);
            image.pixels[offset + 2] = c;
            image.pixels[offset + 3] = transparent ? 0U : 255U;
        }
    }
    return image;
}
} // namespace

TerrainRenderer::TerrainRenderer(VulkanContext& context) : context_(context) {
    create_descriptors();
    create_default_materials();
    create_pipelines();
}

TerrainRenderer::~TerrainRenderer() {
    context_.wait_idle();
    destroy_pipelines();
    destroy_image_array(terrain_textures_);
    destroy_image_array(object_textures_);
    destroy_image_array(surface_textures_);
    destroy_buffer(vertex_buffer_);
    destroy_buffer(index_buffer_);
    destroy_buffer(surface_vertex_buffer_);
    destroy_buffer(surface_index_buffer_);
    destroy_buffer(object_vertex_buffer_);
    destroy_buffer(object_index_buffer_);
    destroy_buffer(shadow_vertex_buffer_);
    destroy_buffer(shadow_index_buffer_);
    for (auto& buffer : dynamic_object_vertex_buffers_) destroy_buffer(buffer);
    for (auto& buffer : dynamic_object_index_buffers_) destroy_buffer(buffer);
    for (auto& buffer : dynamic_shadow_vertex_buffers_) destroy_buffer(buffer);
    for (auto& buffer : dynamic_shadow_index_buffers_) destroy_buffer(buffer);
    for (auto& buffer : depth_sorted_static_index_buffers_) destroy_buffer(buffer);
    for (auto& buffer : resource_shading_buffers_) destroy_buffer(buffer);
    for (auto& buffer : uniform_buffers_) destroy_buffer(buffer);
    if (descriptor_pool_) vkDestroyDescriptorPool(context_.device(), descriptor_pool_, nullptr);
    if (descriptor_layout_) vkDestroyDescriptorSetLayout(context_.device(), descriptor_layout_, nullptr);
}

TerrainRenderer::Buffer TerrainRenderer::create_buffer(
    VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, const void* initial) {
    Buffer result{};
    result.size = size;
    VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    create.size = std::max<VkDeviceSize>(size, 4);
    create.usage = usage;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(context_.device(), &create, nullptr, &result.buffer) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateBuffer failed");
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(context_.device(), result.buffer, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = context_.find_memory_type(requirements.memoryTypeBits, properties);
    if (vkAllocateMemory(context_.device(), &allocation, nullptr, &result.memory) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateMemory(buffer) failed");
    }
    vkBindBufferMemory(context_.device(), result.buffer, result.memory, 0);
    if (initial && size) {
        void* mapped = nullptr;
        vkMapMemory(context_.device(), result.memory, 0, size, 0, &mapped);
        std::memcpy(mapped, initial, static_cast<std::size_t>(size));
        vkUnmapMemory(context_.device(), result.memory);
    }
    return result;
}

void TerrainRenderer::destroy_buffer(Buffer& buffer) {
    if (buffer.buffer) vkDestroyBuffer(context_.device(), buffer.buffer, nullptr);
    if (buffer.memory) vkFreeMemory(context_.device(), buffer.memory, nullptr);
    buffer = {};
}

void TerrainRenderer::update_buffer(Buffer& destination, std::span<const std::byte> data,
                                    VkBufferUsageFlags usage) {
    destroy_buffer(destination);
    destination = create_buffer(data.size(), usage,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, data.data());
}

bool TerrainRenderer::update_reusable_host_buffer(
    Buffer& destination, std::span<const std::byte> data, VkBufferUsageFlags usage) {
    if (data.empty()) return false;
    bool reallocated = false;
    if (destination.buffer == VK_NULL_HANDLE || destination.size < data.size()) {
        destroy_buffer(destination);
        VkDeviceSize capacity = 4096U;
        while (capacity < data.size()) capacity *= 2U;
        destination = create_buffer(capacity, usage,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        reallocated = true;
    }
    void* mapped = nullptr;
    if (vkMapMemory(context_.device(), destination.memory, 0, data.size(), 0, &mapped) != VK_SUCCESS) {
        throw std::runtime_error("dynamic host-buffer mapping failed");
    }
    std::memcpy(mapped, data.data(), data.size());
    vkUnmapMemory(context_.device(), destination.memory);
    return reallocated;
}

void TerrainRenderer::create_descriptors() {
    const std::array<VkDescriptorSetLayoutBinding, 5> bindings{{
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    }};
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = static_cast<uint32_t>(bindings.size());
    layout.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(context_.device(), &layout, nullptr, &descriptor_layout_) != VK_SUCCESS) {
        throw std::runtime_error("descriptor layout failed");
    }
    const std::array<VkDescriptorPoolSize, 3> sizes{{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VulkanContext::kFramesInFlight},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3U * VulkanContext::kFramesInFlight},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VulkanContext::kFramesInFlight},
    }};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = VulkanContext::kFramesInFlight;
    pool.poolSizeCount = static_cast<uint32_t>(sizes.size());
    pool.pPoolSizes = sizes.data();
    if (vkCreateDescriptorPool(context_.device(), &pool, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        throw std::runtime_error("descriptor pool failed");
    }
    std::array<VkDescriptorSetLayout, VulkanContext::kFramesInFlight> layouts{};
    layouts.fill(descriptor_layout_);
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = descriptor_pool_;
    allocate.descriptorSetCount = VulkanContext::kFramesInFlight;
    allocate.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(context_.device(), &allocate, descriptor_sets_.data()) != VK_SUCCESS) {
        throw std::runtime_error("descriptor set allocation failed");
    }

    const ResourceShadingBufferHeader empty_records{};
    for (uint32_t frame_index = 0; frame_index < VulkanContext::kFramesInFlight; ++frame_index) {
        uniform_buffers_[frame_index] = create_buffer(sizeof(UniformBlock),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        resource_shading_buffers_[frame_index] = create_buffer(sizeof(empty_records),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &empty_records);
        const VkDescriptorBufferInfo uniform_info{
            uniform_buffers_[frame_index].buffer, 0, sizeof(UniformBlock)};
        const VkDescriptorBufferInfo shading_info{
            resource_shading_buffers_[frame_index].buffer, 0,
            resource_shading_buffers_[frame_index].size};
        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[0].dstSet = descriptor_sets_[frame_index];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &uniform_info;
        writes[1] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[1].dstSet = descriptor_sets_[frame_index];
        writes[1].dstBinding = 3;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &shading_info;
        vkUpdateDescriptorSets(context_.device(), static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
}

void TerrainRenderer::destroy_image_array(ImageArray& array) {
    if (array.sampler) vkDestroySampler(context_.device(), array.sampler, nullptr);
    if (array.view) vkDestroyImageView(context_.device(), array.view, nullptr);
    if (array.image) vkDestroyImage(context_.device(), array.image, nullptr);
    if (array.memory) vkFreeMemory(context_.device(), array.memory, nullptr);
    array = {};
}

void TerrainRenderer::upload_image_array(ImageArray& destination,
                                         std::span<const thandor::RgbaImage> images,
                                         uint32_t extent, uint32_t maximum_layers,
                                         VkSamplerAddressMode address_mode,
                                         uint32_t descriptor_binding,
                                         TextureSamplingMode sampling_mode) {
    if (images.empty()) throw std::runtime_error("cannot upload an empty image array");
    context_.wait_idle();
    destroy_image_array(destination);
    destination.layers = thandor::simulation_validate_texture_layer_capacity(
        images.size(), maximum_layers);
    destination.extent = extent;
    const auto sampling_plan = texture_sampling_plan(extent, sampling_mode);
    destination.mip_levels = sampling_plan.mip_levels;

    struct MipLayout {
        uint32_t extent{};
        std::size_t offset{};
        std::size_t byte_size{};
    };
    std::vector<MipLayout> mip_layouts;
    mip_layouts.reserve(destination.mip_levels);
    std::size_t layer_stride = 0U;
    for (uint32_t level = 0U, size = extent; level < destination.mip_levels;
         ++level, size = std::max(1U, size / 2U)) {
        const std::size_t byte_size = static_cast<std::size_t>(size) * size * 4U;
        mip_layouts.push_back({size, layer_stride, byte_size});
        layer_stride += byte_size;
    }
    std::vector<uint8_t> pixels(static_cast<std::size_t>(destination.layers) * layer_stride);
    for (uint32_t layer = 0; layer < destination.layers; ++layer) {
        std::vector<uint8_t> current = resample_rgba(images[layer], extent);
        for (uint32_t level = 0; level < destination.mip_levels; ++level) {
            const auto& layout = mip_layouts[level];
            const std::size_t destination_offset =
                static_cast<std::size_t>(layer) * layer_stride + layout.offset;
            std::copy(current.begin(), current.end(),
                      pixels.begin() + static_cast<std::ptrdiff_t>(destination_offset));
            if (level + 1U < destination.mip_levels) {
                current = downsample_rgba_2x(current, layout.extent);
            }
        }
    }
    auto staging = create_buffer(pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, pixels.data());

    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = VK_FORMAT_R8G8B8A8_SRGB;
    image.extent = {extent, extent, 1};
    image.mipLevels = destination.mip_levels;
    image.arrayLayers = destination.layers;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(context_.device(), &image, nullptr, &destination.image) != VK_SUCCESS) {
        destroy_buffer(staging);
        throw std::runtime_error("texture array image creation failed");
    }
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(context_.device(), destination.image, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = context_.find_memory_type(requirements.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(context_.device(), &allocation, nullptr, &destination.memory) != VK_SUCCESS) {
        destroy_buffer(staging);
        throw std::runtime_error("texture array memory allocation failed");
    }
    vkBindImageMemory(context_.device(), destination.image, destination.memory, 0);
    context_.immediate_submit([&](VkCommandBuffer command) {
        VkImageMemoryBarrier to_copy{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        to_copy.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        to_copy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_copy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_copy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_copy.image = destination.image;
        to_copy.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                    destination.mip_levels, 0, destination.layers};
        to_copy.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_copy);
        std::vector<VkBufferImageCopy> copies;
        copies.reserve(static_cast<std::size_t>(destination.layers) * destination.mip_levels);
        for (uint32_t layer = 0; layer < destination.layers; ++layer) {
            for (uint32_t level = 0; level < destination.mip_levels; ++level) {
                const auto& mip = mip_layouts[level];
                VkBufferImageCopy copy{};
                copy.bufferOffset = static_cast<VkDeviceSize>(
                    static_cast<std::size_t>(layer) * layer_stride + mip.offset);
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1};
                copy.imageExtent = {mip.extent, mip.extent, 1};
                copies.push_back(copy);
            }
        }
        vkCmdCopyBufferToImage(command, staging.buffer, destination.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(copies.size()), copies.data());
        VkImageMemoryBarrier to_shader = to_copy;
        to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_shader);
    });
    destroy_buffer(staging);

    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = destination.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    view.format = VK_FORMAT_R8G8B8A8_SRGB;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0,
                             destination.mip_levels, 0, destination.layers};
    if (vkCreateImageView(context_.device(), &view, nullptr, &destination.view) != VK_SUCCESS) {
        throw std::runtime_error("texture array view creation failed");
    }
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = sampling_plan.blend_between_mip_levels
        ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = address_mode;
    sampler.addressModeV = address_mode;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    const bool use_anisotropy = sampling_plan.allow_anisotropy &&
                                context_.sampler_anisotropy_supported();
    sampler.maxAnisotropy = use_anisotropy ? 8.0F : 1.0F;
    sampler.anisotropyEnable = use_anisotropy ? VK_TRUE : VK_FALSE;
    sampler.minLod = 0.0F;
    sampler.maxLod = static_cast<float>(destination.mip_levels - 1U);
    if (vkCreateSampler(context_.device(), &sampler, nullptr, &destination.sampler) != VK_SUCCESS) {
        throw std::runtime_error("texture array sampler creation failed");
    }
    VkDescriptorImageInfo image_info{destination.sampler, destination.view,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    for (const auto descriptor_set : descriptor_sets_) {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptor_set;
        write.dstBinding = descriptor_binding;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image_info;
        vkUpdateDescriptorSets(context_.device(), 1, &write, 0, nullptr);
    }
}

void TerrainRenderer::create_default_materials() {
    const auto terrain = checker(256, false);
    upload_image_array(terrain_textures_, std::span<const thandor::RgbaImage>(&terrain, 1),
                       256, 32, VK_SAMPLER_ADDRESS_MODE_REPEAT, 1,
                       TextureSamplingMode::MipmappedAnisotropic);
    const auto object = checker(128, true);
    upload_image_array(object_textures_, std::span<const thandor::RgbaImage>(&object, 1),
                       128, 512, VK_SAMPLER_ADDRESS_MODE_REPEAT, 2,
                       TextureSamplingMode::DirectDrawSingleLevelLinear);
    const auto surface = checker(256, false);
    upload_image_array(surface_textures_, std::span<const thandor::RgbaImage>(&surface, 1),
                       256, 256, VK_SAMPLER_ADDRESS_MODE_REPEAT, 4,
                       TextureSamplingMode::DirectDrawSingleLevelLinear);
}

void TerrainRenderer::update_materials(std::span<const thandor::RgbaImage> images) {
    if (images.empty()) {
        const auto terrain = checker(256, false);
        upload_image_array(terrain_textures_, std::span<const thandor::RgbaImage>(&terrain, 1),
                           256, 32, VK_SAMPLER_ADDRESS_MODE_REPEAT, 1,
                           TextureSamplingMode::MipmappedAnisotropic);
    } else {
        upload_image_array(terrain_textures_,
                           images.first(std::min<std::size_t>(images.size(), 32)), 256, 32,
                           VK_SAMPLER_ADDRESS_MODE_REPEAT, 1,
                           TextureSamplingMode::MipmappedAnisotropic);
    }
}

void TerrainRenderer::update_surface_materials(
    std::span<const thandor::RgbaImage> images) {
    if (images.empty()) {
        const auto surface = checker(256, false);
        upload_image_array(surface_textures_,
                           std::span<const thandor::RgbaImage>(&surface, 1),
                           256, 256, VK_SAMPLER_ADDRESS_MODE_REPEAT, 4,
                           TextureSamplingMode::DirectDrawSingleLevelLinear);
    } else {
        upload_image_array(surface_textures_,
                           images.first(std::min<std::size_t>(images.size(), 256)),
                           256, 256, VK_SAMPLER_ADDRESS_MODE_REPEAT, 4,
                           TextureSamplingMode::DirectDrawSingleLevelLinear);
    }
}

VkShaderModule TerrainRenderer::load_shader(const std::filesystem::path& path) const {
    auto data = thandor::read_file(path);
    if ((data.size() & 3U) != 0) throw std::runtime_error("SPIR-V size is not aligned");
    VkShaderModuleCreateInfo create{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    create.codeSize = data.size();
    create.pCode = reinterpret_cast<const uint32_t*>(data.data());
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(context_.device(), &create, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("shader module failed: " + path.string());
    }
    return module;
}

void TerrainRenderer::destroy_pipelines() {
    if (terrain_pipeline_) vkDestroyPipeline(context_.device(), terrain_pipeline_, nullptr);
    if (wire_pipeline_) vkDestroyPipeline(context_.device(), wire_pipeline_, nullptr);
    if (surface_pipeline_) vkDestroyPipeline(context_.device(), surface_pipeline_, nullptr);
    if (shadow_pipeline_) vkDestroyPipeline(context_.device(), shadow_pipeline_, nullptr);
    for (auto& pipeline : object_pipelines_) {
        if (pipeline) vkDestroyPipeline(context_.device(), pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipeline_layout_) vkDestroyPipelineLayout(context_.device(), pipeline_layout_, nullptr);
    terrain_pipeline_ = wire_pipeline_ = surface_pipeline_ = shadow_pipeline_ = VK_NULL_HANDLE;
    pipeline_layout_ = VK_NULL_HANDLE;
    pipeline_render_pass_ = VK_NULL_HANDLE;
}

void TerrainRenderer::create_pipelines() {
    destroy_pipelines();
    pipeline_render_pass_ = context_.render_pass();
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &descriptor_layout_;
    if (vkCreatePipelineLayout(context_.device(), &layout, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        throw std::runtime_error("pipeline layout failed");
    }

    const std::filesystem::path shader_dir = runtime_shader_directory();
    const VkShaderModule terrain_vertex = load_shader(shader_dir / "terrain.vert.spv");
    const VkShaderModule terrain_fragment = load_shader(shader_dir / "terrain.frag.spv");
    const VkShaderModule surface_vertex = load_shader(shader_dir / "surface.vert.spv");
    const VkShaderModule surface_fragment = load_shader(shader_dir / "surface.frag.spv");
    const VkShaderModule model_vertex = load_shader(shader_dir / "model.vert.spv");
    const VkShaderModule model_fragment = load_shader(shader_dir / "model.frag.spv");
    const VkShaderModule shadow_fragment = load_shader(shader_dir / "shadow.frag.spv");

    const VkVertexInputBindingDescription terrain_binding{0, sizeof(thandor::TerrainVertex),
                                                           VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 7> terrain_attributes{{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(thandor::TerrainVertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(thandor::TerrainVertex, normal)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(thandor::TerrainVertex, u)},
        {3, 0, VK_FORMAT_R32_UINT, offsetof(thandor::TerrainVertex, material)},
        {4, 0, VK_FORMAT_R32_UINT, offsetof(thandor::TerrainVertex, packed_light_argb)},
        {5, 0, VK_FORMAT_R32G32B32_UINT, offsetof(thandor::TerrainVertex, blend_materials)},
        {6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(thandor::TerrainVertex, blend_weights)},
    }};
    VkPipelineVertexInputStateCreateInfo terrain_input{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    terrain_input.vertexBindingDescriptionCount = 1;
    terrain_input.pVertexBindingDescriptions = &terrain_binding;
    terrain_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(terrain_attributes.size());
    terrain_input.pVertexAttributeDescriptions = terrain_attributes.data();

    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0F;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState color{};
    color.colorWriteMask = 0xFU;
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &color;
    const std::array<VkDynamicState, 2> dynamics{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamics.size());
    dynamic.pDynamicStates = dynamics.data();

    const auto build_surface = [&](VkShaderModule vertex, VkShaderModule fragment,
                                   VkPipelineVertexInputStateCreateInfo* input,
                                   VkPolygonMode mode, bool alpha, bool depth_write,
                                   VkCullModeFlags cull_mode,
                                   VkBlendFactor source_color = VK_BLEND_FACTOR_SRC_ALPHA,
                                   VkBlendFactor destination_color = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                   VkBlendFactor source_alpha = VK_BLEND_FACTOR_ONE,
                                   VkBlendFactor destination_alpha = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                   bool depth_test = true) {
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr},
        }};
        auto local_raster = raster;
        local_raster.polygonMode = mode;
        local_raster.cullMode = cull_mode;
        auto local_depth = depth;
        local_depth.depthTestEnable = depth_test ? VK_TRUE : VK_FALSE;
        local_depth.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
        auto local_color = color;
        if (alpha) {
            local_color.blendEnable = VK_TRUE;
            local_color.srcColorBlendFactor = source_color;
            local_color.dstColorBlendFactor = destination_color;
            local_color.colorBlendOp = VK_BLEND_OP_ADD;
            local_color.srcAlphaBlendFactor = source_alpha;
            local_color.dstAlphaBlendFactor = destination_alpha;
            local_color.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        auto local_blend = blend;
        local_blend.pAttachments = &local_color;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.stageCount = static_cast<uint32_t>(stages.size());
        info.pStages = stages.data();
        info.pVertexInputState = input;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &local_raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &local_depth;
        info.pColorBlendState = &local_blend;
        info.pDynamicState = &dynamic;
        info.layout = pipeline_layout_;
        info.renderPass = context_.render_pass();
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(context_.device(), VK_NULL_HANDLE, 1, &info, nullptr,
                                      &pipeline) != VK_SUCCESS) {
            throw std::runtime_error("graphics pipeline failed");
        }
        return pipeline;
    };

    // Exact soil.dat transition packets are queued sequentially at equal depth. Variant alpha
    // composes the irregular boundaries; LESS_OR_EQUAL and depth writes preserve the game order.
    terrain_pipeline_ = build_surface(terrain_vertex, terrain_fragment, &terrain_input,
                                      VK_POLYGON_MODE_FILL, true, true, VK_CULL_MODE_BACK_BIT);
    wire_pipeline_ = context_.fill_mode_non_solid_supported()
        ? build_surface(terrain_vertex, terrain_fragment, &terrain_input,
                        VK_POLYGON_MODE_LINE, true, true, VK_CULL_MODE_BACK_BIT)
        : VK_NULL_HANDLE;
    // The shared water/lava/ice surface packets use renderFlags 0x6000 | 0x10000 and Direct3D textured preset 3:
    // SRCALPHA/INVSRCALPHA with alpha blending and Z writes enabled.
    surface_pipeline_ = build_surface(surface_vertex, surface_fragment, &terrain_input,
                                    VK_POLYGON_MODE_FILL, true, true, VK_CULL_MODE_BACK_BIT);

    const VkVertexInputBindingDescription object_binding{0, sizeof(ObjectVertex),
                                                          VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 8> object_attributes{{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ObjectVertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ObjectVertex, normal)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ObjectVertex, u)},
        {3, 0, VK_FORMAT_R32_UINT, offsetof(ObjectVertex, texture_layer)},
        {4, 0, VK_FORMAT_R32_UINT, offsetof(ObjectVertex, packed_color_argb)},
        {5, 0, VK_FORMAT_R32_UINT, offsetof(ObjectVertex, direct_tint_bypass)},
        {6, 0, VK_FORMAT_R32_SFLOAT, offsetof(ObjectVertex, animation_v_amplitude)},
        {7, 0, VK_FORMAT_R32_SFLOAT, offsetof(ObjectVertex, animation_cycle_ticks)},
    }};
    VkPipelineVertexInputStateCreateInfo object_input{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    object_input.vertexBindingDescriptionCount = 1;
    object_input.pVertexBindingDescriptions = &object_binding;
    object_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(object_attributes.size());
    object_input.pVertexAttributeDescriptions = object_attributes.data();

    // Projected shadows reuse the object vertex/texture layout, test against terrain depth, do not
    // write depth, and are two-sided because a projected silhouette may reverse winding on slopes.
    shadow_pipeline_ = build_surface(model_vertex, shadow_fragment, &object_input,
                                     VK_POLYGON_MODE_FILL, true, false, VK_CULL_MODE_NONE);

    for (const bool two_sided : {false, true}) {
        const VkCullModeFlags cull = two_sided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
        object_pipelines_[object_pipeline_index(PrimitiveBlendPreset::OpaqueDepthWrite, two_sided)] =
            build_surface(model_vertex, model_fragment, &object_input, VK_POLYGON_MODE_FILL,
                          false, true, cull);
        object_pipelines_[object_pipeline_index(PrimitiveBlendPreset::AlphaDepthWrite, two_sided)] =
            build_surface(model_vertex, model_fragment, &object_input, VK_POLYGON_MODE_FILL,
                          true, true, cull);
        object_pipelines_[object_pipeline_index(PrimitiveBlendPreset::AlphaNoDepthWrite, two_sided)] =
            build_surface(model_vertex, model_fragment, &object_input, VK_POLYGON_MODE_FILL,
                          true, false, cull);
        // DirectDraw/Direct3D preset 4: ONE/ONE, alpha blending enabled, Z writes disabled.
        // Opaque-black texels therefore contribute zero RGB instead of a rectangular plate.
        object_pipelines_[object_pipeline_index(PrimitiveBlendPreset::AdditiveNoDepthWrite, two_sided)] =
            build_surface(model_vertex, model_fragment, &object_input, VK_POLYGON_MODE_FILL,
                          true, false, cull, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
                          VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
        object_pipelines_[object_pipeline_index(PrimitiveBlendPreset::FogOverlayNoDepthTest, two_sided)] =
            build_surface(model_vertex, model_fragment, &object_input, VK_POLYGON_MODE_FILL,
                          true, false, cull, VK_BLEND_FACTOR_SRC_ALPHA,
                          VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE,
                          VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, false);
    }

    for (const auto module : {terrain_vertex, terrain_fragment, surface_vertex, surface_fragment,
                              model_vertex, model_fragment, shadow_fragment}) {
        vkDestroyShaderModule(context_.device(), module, nullptr);
    }
}

void TerrainRenderer::update_mesh(const thandor::FieldGrid& field,
                                  const thandor::TerrainLighting& lighting,
                                  const thandor::TerrainTextureLayout* texture_layout,
                                  const thandor::TerrainSurfaceTextureLayout* surface_layout,
                                  bool exact_soil_packets,
                                  uint32_t soil_random_seed,
                                  uint32_t surface_wave_random_seed) {
    context_.wait_idle();
    cpu_vertices_ = field.build_vertices(
        lighting, 4.0F, exact_soil_packets
            ? thandor::TerrainMaterialPreviewMode::ExactSoilDatPackets
            : thandor::TerrainMaterialPreviewMode::DirectCellMaterial,
        texture_layout, soil_random_seed);
    std::vector<uint32_t> indices(cpu_vertices_.size());
    for (std::size_t index = 0; index < indices.size(); ++index)
        indices[index] = static_cast<uint32_t>(index);
    auto surface_vertices = field.build_animated_surface_vertices(
        surface_layout, &lighting, surface_wave_random_seed, soil_random_seed);
    std::vector<uint32_t> surface_indices(surface_vertices.size());
    for (std::size_t index = 0; index < surface_indices.size(); ++index) {
        surface_indices[index] = static_cast<uint32_t>(index);
    }
    update_buffer(vertex_buffer_, bytes_of(cpu_vertices_), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    update_buffer(index_buffer_, bytes_of(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    update_buffer(surface_vertex_buffer_, bytes_of(surface_vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    update_buffer(surface_index_buffer_, bytes_of(surface_indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    index_count_ = static_cast<uint32_t>(indices.size());
    surface_index_count_ = static_cast<uint32_t>(surface_indices.size());
}

void TerrainRenderer::update_objects(std::span<const ObjectVertex> vertices,
                                     std::span<const uint32_t> indices,
                                     std::span<const ObjectDrawRun> runs,
                                     std::span<const ObjectVertex> shadow_vertices,
                                     std::span<const uint32_t> shadow_indices,
                                     std::span<const thandor::RgbaImage> texture_layers) {
    context_.wait_idle();
    destroy_buffer(object_vertex_buffer_);
    destroy_buffer(object_index_buffer_);
    destroy_buffer(shadow_vertex_buffer_);
    destroy_buffer(shadow_index_buffer_);
    if (!vertices.empty()) {
        object_vertex_buffer_ = create_buffer(vertices.size_bytes(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertices.data());
    }
    if (!indices.empty()) {
        object_index_buffer_ = create_buffer(indices.size_bytes(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, indices.data());
    }
    if (!shadow_vertices.empty()) {
        shadow_vertex_buffer_ = create_buffer(shadow_vertices.size_bytes(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            shadow_vertices.data());
    }
    if (!shadow_indices.empty()) {
        shadow_index_buffer_ = create_buffer(shadow_indices.size_bytes(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            shadow_indices.data());
    }
    object_index_count_ = static_cast<uint32_t>(indices.size());
    shadow_index_count_ = static_cast<uint32_t>(shadow_indices.size());
    object_draw_runs_.clear();
    static_transparent_triangles_.clear();
    uint32_t source_order=0U;
    for (const auto& run : runs) {
        if (run.index_count == 0U || run.index_count % 3U != 0U ||
            run.first_index > object_index_count_ ||
            run.index_count > object_index_count_ - run.first_index) {
            throw std::runtime_error("object draw run exceeds uploaded triangle index buffer");
        }
        if (run.blend == PrimitiveBlendPreset::OpaqueDepthWrite) {
            object_draw_runs_.push_back(run);
            source_order += run.index_count / 3U;
            continue;
        }
        for (uint32_t offset=0U;offset<run.index_count;offset+=3U,++source_order) {
            const std::array<uint32_t,3> triangle{
                indices[run.first_index+offset],indices[run.first_index+offset+1U],
                indices[run.first_index+offset+2U]};
            for (const uint32_t index : triangle) {
                if (index >= vertices.size())
                    throw std::runtime_error("transparent object index exceeds vertex stream");
            }
            const Vec3f centroid=mul(add(add(vertices[triangle[0]].position,
                                               vertices[triangle[1]].position),
                                           vertices[triangle[2]].position),1.0F/3.0F);
            static_transparent_triangles_.push_back(StaticTransparentTriangle{
                triangle,centroid,run.blend,run.two_sided,source_order});
        }
    }
    sorted_static_indices_.clear();
    sorted_static_draw_runs_.clear();
    sorted_static_camera_valid_=false;
    ++sorted_static_revision_;
    uploaded_sorted_static_revisions_.fill(0U);
    depth_sorted_static_index_counts_.fill(0U);
    for(auto& sorted_runs:depth_sorted_static_draw_runs_)sorted_runs.clear();
    if (texture_layers.empty()) {
        const auto object = checker(128, true);
        upload_image_array(object_textures_, std::span<const thandor::RgbaImage>(&object, 1),
                           128, 512, VK_SAMPLER_ADDRESS_MODE_REPEAT, 2,
                           TextureSamplingMode::DirectDrawSingleLevelLinear);
    } else {
        upload_image_array(object_textures_, texture_layers, 128,
                           maximum_object_texture_layers(),
                           VK_SAMPLER_ADDRESS_MODE_REPEAT, 2,
                           TextureSamplingMode::DirectDrawSingleLevelLinear);
    }
}

void TerrainRenderer::update_dynamic_objects(
    uint32_t frame_index, std::span<const ObjectVertex> vertices,
    std::span<const uint32_t> indices, std::span<const ObjectDrawRun> runs,
    std::span<const ObjectVertex> shadow_vertices,
    std::span<const uint32_t> shadow_indices) {
    if (frame_index >= VulkanContext::kFramesInFlight) {
        throw std::runtime_error("dynamic object frame index is out of range");
    }
    auto& vertex_buffer = dynamic_object_vertex_buffers_[frame_index];
    auto& index_buffer = dynamic_object_index_buffers_[frame_index];
    if (!vertices.empty()) {
        update_reusable_host_buffer(vertex_buffer,
            {reinterpret_cast<const std::byte*>(vertices.data()), vertices.size_bytes()},
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }
    if (!indices.empty()) {
        update_reusable_host_buffer(index_buffer,
            {reinterpret_cast<const std::byte*>(indices.data()), indices.size_bytes()},
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    }
    dynamic_object_index_counts_[frame_index] = static_cast<uint32_t>(indices.size());
    auto& shadow_vertex_buffer=dynamic_shadow_vertex_buffers_[frame_index];
    auto& shadow_index_buffer=dynamic_shadow_index_buffers_[frame_index];
    if(!shadow_vertices.empty()){
        update_reusable_host_buffer(shadow_vertex_buffer,
            {reinterpret_cast<const std::byte*>(shadow_vertices.data()),shadow_vertices.size_bytes()},
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }
    if(!shadow_indices.empty()){
        update_reusable_host_buffer(shadow_index_buffer,
            {reinterpret_cast<const std::byte*>(shadow_indices.data()),shadow_indices.size_bytes()},
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    }
    dynamic_shadow_index_counts_[frame_index]=static_cast<uint32_t>(shadow_indices.size());
    auto& target_runs = dynamic_object_draw_runs_[frame_index];
    target_runs.assign(runs.begin(), runs.end());
    for (const auto& run : target_runs) {
        if (run.index_count == 0U || run.first_index > dynamic_object_index_counts_[frame_index] ||
            run.index_count > dynamic_object_index_counts_[frame_index] - run.first_index) {
            throw std::runtime_error("dynamic object draw run exceeds uploaded index buffer");
        }
    }
}

void TerrainRenderer::update_depth_sorted_static_objects(
    uint32_t frame_index, const Camera& camera) {
    if (frame_index >= VulkanContext::kFramesInFlight)
        throw std::runtime_error("static transparent sort frame index is out of range");
    const Vec3f eye=camera.eye();
    const std::array<float,6> camera_state{
        eye.x,eye.y,eye.z,camera.target.x,camera.target.y,camera.target.z};
    if(!sorted_static_camera_valid_||camera_state!=sorted_static_camera_state_){
        sorted_static_camera_state_=camera_state;
        sorted_static_camera_valid_=true;
        const Vec3f forward=normalize(sub(camera.target,eye));
        struct DepthEntry { const StaticTransparentTriangle* triangle{}; float depth{}; };
        std::vector<DepthEntry> order;
        order.reserve(static_transparent_triangles_.size());
        for(const auto& triangle:static_transparent_triangles_)
            order.push_back(DepthEntry{&triangle,dot(sub(triangle.centroid,eye),forward)});
        std::stable_sort(order.begin(),order.end(),[](const DepthEntry& left,const DepthEntry& right){
            if(left.depth!=right.depth)return left.depth>right.depth;
            return left.triangle->source_order<right.triangle->source_order;
        });
        sorted_static_indices_.clear();
        sorted_static_indices_.reserve(order.size()*3U);
        sorted_static_draw_runs_.clear();
        for(const auto& entry:order){
            const auto& triangle=*entry.triangle;
            const uint32_t first=static_cast<uint32_t>(sorted_static_indices_.size());
            sorted_static_indices_.insert(sorted_static_indices_.end(),
                triangle.indices.begin(),triangle.indices.end());
            if(!sorted_static_draw_runs_.empty()){
                auto& previous=sorted_static_draw_runs_.back();
                if(previous.blend==triangle.blend&&previous.two_sided==triangle.two_sided&&
                   previous.first_index+previous.index_count==first){
                    previous.index_count+=3U;continue;
                }
            }
            sorted_static_draw_runs_.push_back(ObjectDrawRun{
                triangle.blend,triangle.two_sided,first,3U});
        }
        ++sorted_static_revision_;
    }
    if(uploaded_sorted_static_revisions_[frame_index]==sorted_static_revision_)return;
    if(!sorted_static_indices_.empty()){
        update_reusable_host_buffer(depth_sorted_static_index_buffers_[frame_index],
            {reinterpret_cast<const std::byte*>(sorted_static_indices_.data()),
             sorted_static_indices_.size()*sizeof(uint32_t)},VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    }
    depth_sorted_static_index_counts_[frame_index]=
        static_cast<uint32_t>(sorted_static_indices_.size());
    depth_sorted_static_draw_runs_[frame_index]=sorted_static_draw_runs_;
    uploaded_sorted_static_revisions_[frame_index]=sorted_static_revision_;
}

void TerrainRenderer::update_resource_shading(
    uint32_t frame_index, std::span<const ResourceShadingEmitter> emitters) {
    if (frame_index >= VulkanContext::kFramesInFlight) {
        throw std::runtime_error("resource shading frame index is out of range");
    }
    if (emitters.size() > 256U) {
        throw std::runtime_error("resource shading record count exceeds the original 256-slot table");
    }
    std::vector<std::byte> payload(sizeof(ResourceShadingBufferHeader) +
                                   emitters.size() * sizeof(ResourceShadingGpuRecord));
    auto* header = reinterpret_cast<ResourceShadingBufferHeader*>(payload.data());
    header->count = static_cast<uint32_t>(emitters.size());
    auto* records = reinterpret_cast<ResourceShadingGpuRecord*>(payload.data() +
                                                                 sizeof(ResourceShadingBufferHeader));
    for (std::size_t index = 0; index < emitters.size(); ++index) {
        const auto& source = emitters[index];
        auto& target = records[index];
        target.position_radius[0] = source.position.x;
        target.position_radius[1] = source.position.y;
        target.position_radius[2] = source.position.z;
        target.position_radius[3] = source.target_radius;
        target.color_period[0] = source.color_r;
        target.color_period[1] = source.color_g;
        target.color_period[2] = source.color_b;
        target.color_period[3] = source.cycle_ticks;
        target.phase_grow_stop_release[0] = source.spawn_tick;
        target.phase_grow_stop_release[1] = source.grow_ticks;
        target.phase_grow_stop_release[2] = source.stop_ticks;
        target.phase_grow_stop_release[3] = source.release_ticks;
    }
    auto& buffer = resource_shading_buffers_[frame_index];
    const bool reallocated = update_reusable_host_buffer(buffer, payload,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (reallocated) {
        const VkDescriptorBufferInfo info{buffer.buffer, 0, buffer.size};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptor_sets_[frame_index];
        write.dstBinding = 3;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &info;
        vkUpdateDescriptorSets(context_.device(), 1, &write, 0, nullptr);
    }
    resource_shading_record_count_ = static_cast<uint32_t>(emitters.size());
}

void TerrainRenderer::render(VkCommandBuffer command, const Camera& camera,
                             bool draw_surface, bool wireframe, uint32_t frame_index,
                             bool animate_surface, bool animate_buildings, bool animate_units) {
    if (frame_index >= VulkanContext::kFramesInFlight) {
        throw std::runtime_error("render frame index is out of range");
    }
    if (pipeline_render_pass_ != context_.render_pass()) create_pipelines();
    const auto extent = context_.extent();
    UniformBlock uniform{};
    uniform.view_projection = camera.view_projection(
        static_cast<float>(extent.width) / std::max(1.0F, static_cast<float>(extent.height)));
    const auto eye = camera.eye();
    uniform.camera[0] = eye.x;
    uniform.camera[1] = eye.y;
    uniform.camera[2] = eye.z;
    uniform.camera[3] = static_cast<float>(surface_textures_.layers);
    uniform.material_scale[0] = 1.0F;
    uniform.material_scale[1] = static_cast<float>(terrain_textures_.layers);
    uniform.material_scale[2] = static_cast<float>(object_textures_.layers);
    // InGamePresentationTick advances the 256 terrain-direction records, model animation and
    // periodic effect runtimes from one persistent 20 Hz integer timeline. EditorApp publishes
    // that tick once per frame; buffer uploads never reset it.
    // G346 compatibility marker: constexpr float kWaterPresentationTicksPerSecond = 20.0F;
    uniform.material_scale[3] = static_cast<float>(presentation_tick_);
    uniform.animation_controls[0] = animate_surface ? static_cast<float>(presentation_tick_) : 0.0F;
    uniform.animation_controls[1] = animate_buildings ? static_cast<float>(building_animation_tick_) : 0.0F;
    uniform.animation_controls[2] = animate_buildings ? 1.0F : 0.0F;
    uniform.animation_controls[3] = animate_units ? 1.0F : 0.0F;
    void* mapped = nullptr;
    auto& uniform_buffer = uniform_buffers_[frame_index];
    vkMapMemory(context_.device(), uniform_buffer.memory, 0, sizeof(uniform), 0, &mapped);
    std::memcpy(mapped, &uniform, sizeof(uniform));
    vkUnmapMemory(context_.device(), uniform_buffer.memory);

    VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    const auto descriptor_set = descriptor_sets_[frame_index];
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_,
                            0, 1, &descriptor_set, 0, nullptr);
    VkDeviceSize offset = 0;
    if (index_count_) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
            wireframe && wire_pipeline_ != VK_NULL_HANDLE ? wire_pipeline_ : terrain_pipeline_);
        vkCmdBindVertexBuffers(command, 0, 1, &vertex_buffer_.buffer, &offset);
        vkCmdBindIndexBuffer(command, index_buffer_.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(command, index_count_, 1, 0, 0, 0);
    }
    if (shadow_index_count_) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline_);
        vkCmdBindVertexBuffers(command, 0, 1, &shadow_vertex_buffer_.buffer, &offset);
        vkCmdBindIndexBuffer(command, shadow_index_buffer_.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(command, shadow_index_count_, 1, 0, 0, 0);
    }
    if (object_index_count_) {
        vkCmdBindVertexBuffers(command, 0, 1, &object_vertex_buffer_.buffer, &offset);
        vkCmdBindIndexBuffer(command, object_index_buffer_.buffer, 0, VK_INDEX_TYPE_UINT32);
        for (const auto& run : object_draw_runs_) {
            const auto pipeline = object_pipelines_[object_pipeline_index(run.blend, run.two_sided)];
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdDrawIndexed(command, run.index_count, 1, run.first_index, 0, 0);
        }
    }
    if (depth_sorted_static_index_counts_[frame_index]) {
        vkCmdBindVertexBuffers(command, 0, 1, &object_vertex_buffer_.buffer, &offset);
        vkCmdBindIndexBuffer(command, depth_sorted_static_index_buffers_[frame_index].buffer,
                             0, VK_INDEX_TYPE_UINT32);
        for (const auto& run : depth_sorted_static_draw_runs_[frame_index]) {
            const auto pipeline = object_pipelines_[object_pipeline_index(run.blend, run.two_sided)];
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdDrawIndexed(command, run.index_count, 1, run.first_index, 0, 0);
        }
    }
    if(dynamic_shadow_index_counts_[frame_index]){
        const auto& dynamic_shadow_vertex=dynamic_shadow_vertex_buffers_[frame_index];
        const auto& dynamic_shadow_index=dynamic_shadow_index_buffers_[frame_index];
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,shadow_pipeline_);
        vkCmdBindVertexBuffers(command,0,1,&dynamic_shadow_vertex.buffer,&offset);
        vkCmdBindIndexBuffer(command,dynamic_shadow_index.buffer,0,VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(command,dynamic_shadow_index_counts_[frame_index],1,0,0,0);
    }
    // Animated water/lava writes depth before live models and effects. Opaque dynamic models still
    // pass when they are physically in front; smoke and other no-depth-write effects now blend
    // after the surface and can no longer be overwritten merely because lava was submitted later.
    if (draw_surface && surface_index_count_) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, surface_pipeline_);
        vkCmdBindVertexBuffers(command, 0, 1, &surface_vertex_buffer_.buffer, &offset);
        vkCmdBindIndexBuffer(command, surface_index_buffer_.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(command, surface_index_count_, 1, 0, 0, 0);
    }
    if (dynamic_object_index_counts_[frame_index]) {
        const auto& vertex_buffer = dynamic_object_vertex_buffers_[frame_index];
        const auto& index_buffer = dynamic_object_index_buffers_[frame_index];
        vkCmdBindVertexBuffers(command, 0, 1, &vertex_buffer.buffer, &offset);
        vkCmdBindIndexBuffer(command, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        for (const auto& run : dynamic_object_draw_runs_[frame_index]) {
            const auto pipeline = object_pipelines_[object_pipeline_index(run.blend, run.two_sided)];
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdDrawIndexed(command, run.index_count, 1, run.first_index, 0, 0);
        }
    }
}

} // namespace editor
