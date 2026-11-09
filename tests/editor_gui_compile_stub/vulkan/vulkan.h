#pragma once
#include <cstddef>
#include <cstdint>
#ifdef __cplusplus
extern "C" {
#endif
#define VK_NULL_HANDLE nullptr
using VkFlags = std::uint32_t;
using VkBool32 = std::uint32_t;
using VkDeviceSize = std::uint64_t;
using VkResult = std::int32_t;
using VkFormat = std::int32_t;
using VkPresentModeKHR = std::int32_t;
using VkBufferUsageFlags = std::uint32_t;
using VkMemoryPropertyFlags = std::uint32_t;
using VkSamplerAddressMode = std::int32_t;
#define VK_FORMAT_UNDEFINED 0
#define VK_FORMAT_D32_SFLOAT 126
#define VK_SUCCESS 0
struct VkInstance_T; using VkInstance = VkInstance_T*;
struct VkPhysicalDevice_T; using VkPhysicalDevice = VkPhysicalDevice_T*;
struct VkDevice_T; using VkDevice = VkDevice_T*;
struct VkQueue_T; using VkQueue = VkQueue_T*;
struct VkSurfaceKHR_T; using VkSurfaceKHR = VkSurfaceKHR_T*;
struct VkSwapchainKHR_T; using VkSwapchainKHR = VkSwapchainKHR_T*;
struct VkImage_T; using VkImage = VkImage_T*;
struct VkImageView_T; using VkImageView = VkImageView_T*;
struct VkDeviceMemory_T; using VkDeviceMemory = VkDeviceMemory_T*;
struct VkRenderPass_T; using VkRenderPass = VkRenderPass_T*;
struct VkFramebuffer_T; using VkFramebuffer = VkFramebuffer_T*;
struct VkCommandPool_T; using VkCommandPool = VkCommandPool_T*;
struct VkCommandBuffer_T; using VkCommandBuffer = VkCommandBuffer_T*;
struct VkDescriptorPool_T; using VkDescriptorPool = VkDescriptorPool_T*;
struct VkDescriptorSet_T; using VkDescriptorSet = VkDescriptorSet_T*;
struct VkDescriptorSetLayout_T; using VkDescriptorSetLayout = VkDescriptorSetLayout_T*;
struct VkFence_T; using VkFence = VkFence_T*;
struct VkSemaphore_T; using VkSemaphore = VkSemaphore_T*;
struct VkBuffer_T; using VkBuffer = VkBuffer_T*;
struct VkPipeline_T; using VkPipeline = VkPipeline_T*;
struct VkPipelineLayout_T; using VkPipelineLayout = VkPipelineLayout_T*;
struct VkSampler_T; using VkSampler = VkSampler_T*;
struct VkShaderModule_T; using VkShaderModule = VkShaderModule_T*;
struct VkExtent2D { std::uint32_t width; std::uint32_t height; };
struct VkSurfaceFormatKHR { VkFormat format; std::int32_t colorSpace; };
#ifdef __cplusplus
}
#endif
