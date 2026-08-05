#include "vulkan_context.hpp"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

namespace editor {
namespace {

struct QueueChoice { uint32_t family{}; bool valid{}; };

QueueChoice find_graphics_present_queue(VkPhysicalDevice device, VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());
    for (uint32_t i = 0; i < count; ++i) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present);
        if ((properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && present == VK_TRUE) return {i, true};
    }
    return {};
}

bool has_device_extension(VkPhysicalDevice device, const char* required) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    return std::any_of(extensions.begin(), extensions.end(), [&](const auto& ext) { return std::strcmp(ext.extensionName, required) == 0; });
}

} // namespace

VulkanContext::VulkanContext(GLFWwindow* window) : window_(window) {
    if (!window_) throw std::invalid_argument("VulkanContext requires a GLFW window");
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebuffer_resize_callback);
    create_instance();
    check(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_), "glfwCreateWindowSurface");
    select_physical_device();
    create_device();
    create_command_resources();
    create_sync_resources();
    create_imgui_descriptor_pool();
    create_swapchain_resources();
}

VulkanContext::~VulkanContext() {
    if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
    shutdown_imgui();
    destroy_swapchain_resources();
    for (auto& frame : frames_) {
        if (frame.image_available) vkDestroySemaphore(device_, frame.image_available, nullptr);
        if (frame.render_finished) vkDestroySemaphore(device_, frame.render_finished, nullptr);
        if (frame.fence) vkDestroyFence(device_, frame.fence, nullptr);
    }
    if (imgui_descriptor_pool_) vkDestroyDescriptorPool(device_, imgui_descriptor_pool_, nullptr);
    if (command_pool_) vkDestroyCommandPool(device_, command_pool_, nullptr);
    if (device_) vkDestroyDevice(device_, nullptr);
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);
}

void VulkanContext::check(VkResult result, const char* operation) const {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + " failed with VkResult " + std::to_string(result));
}

void VulkanContext::create_instance() {
    if (glfwVulkanSupported() != GLFW_TRUE) throw std::runtime_error("GLFW reports no Vulkan loader");
    uint32_t extension_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&extension_count);
    if (!glfw_extensions || extension_count == 0) throw std::runtime_error("GLFW returned no Vulkan instance extensions");
    std::vector<const char*> extensions(glfw_extensions, glfw_extensions + extension_count);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Thandor Vulkan Map Editor";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "ThandorEditor";
    app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo = &app;
    create.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create.ppEnabledExtensionNames = extensions.data();
    check(vkCreateInstance(&create, nullptr, &instance_), "vkCreateInstance");
}

void VulkanContext::select_physical_device() {
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "vkEnumeratePhysicalDevices");
    if (count == 0) throw std::runtime_error("no Vulkan physical devices found");
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), "vkEnumeratePhysicalDevices");
    int best_score = std::numeric_limits<int>::min();
    for (const auto device : devices) {
        const auto queue = find_graphics_present_queue(device, surface_);
        if (!queue.valid || !has_device_extension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
        uint32_t format_count = 0, mode_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &format_count, nullptr);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &mode_count, nullptr);
        if (format_count == 0 || mode_count == 0) continue;
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : 100;
        score += static_cast<int>(properties.limits.maxImageDimension2D / 1024);
        if (score > best_score) { best_score = score; physical_device_ = device; graphics_queue_family_ = queue.family; }
    }
    if (physical_device_ == VK_NULL_HANDLE) throw std::runtime_error("no Vulkan device supports graphics, presentation, and VK_KHR_swapchain");
    VkPhysicalDeviceProperties selected_properties{};
    vkGetPhysicalDeviceProperties(physical_device_, &selected_properties);
    max_image_array_layers_ = std::max(1U, selected_properties.limits.maxImageArrayLayers);
    depth_format_ = choose_depth_format();
}

VkFormat VulkanContext::choose_depth_format() const {
    for (const auto format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physical_device_, format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) return format;
    }
    throw std::runtime_error("no supported Vulkan depth format");
}

void VulkanContext::create_device() {
    const float priority = 1.0F;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = graphics_queue_family_;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(physical_device_, &supported);
    sampler_anisotropy_supported_ = supported.samplerAnisotropy == VK_TRUE;
    fill_mode_non_solid_supported_ = supported.fillModeNonSolid == VK_TRUE;
    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = sampler_anisotropy_supported_ ? VK_TRUE : VK_FALSE;
    features.fillModeNonSolid = fill_mode_non_solid_supported_ ? VK_TRUE : VK_FALSE;
    VkDeviceCreateInfo create{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    create.queueCreateInfoCount = 1;
    create.pQueueCreateInfos = &queue;
    create.enabledExtensionCount = 1;
    create.ppEnabledExtensionNames = extensions;
    create.pEnabledFeatures = &features;
    check(vkCreateDevice(physical_device_, &create, nullptr, &device_), "vkCreateDevice");
    vkGetDeviceQueue(device_, graphics_queue_family_, 0, &graphics_queue_);
}

void VulkanContext::create_command_resources() {
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = graphics_queue_family_;
    check(vkCreateCommandPool(device_, &pool, nullptr, &command_pool_), "vkCreateCommandPool");
    std::array<VkCommandBuffer, kFramesInFlight> buffers{};
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = command_pool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = kFramesInFlight;
    check(vkAllocateCommandBuffers(device_, &allocate, buffers.data()), "vkAllocateCommandBuffers");
    for (uint32_t i = 0; i < kFramesInFlight; ++i) frames_[i].command_buffer = buffers[i];
}

void VulkanContext::create_sync_resources() {
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (auto& frame : frames_) {
        check(vkCreateSemaphore(device_, &semaphore, nullptr, &frame.image_available), "vkCreateSemaphore");
        check(vkCreateSemaphore(device_, &semaphore, nullptr, &frame.render_finished), "vkCreateSemaphore");
        check(vkCreateFence(device_, &fence, nullptr, &frame.fence), "vkCreateFence");
    }
}

void VulkanContext::create_imgui_descriptor_pool() {
    const std::array<VkDescriptorPoolSize, 11> sizes{{
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
    }};
    VkDescriptorPoolCreateInfo create{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    create.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    create.maxSets = 1000 * static_cast<uint32_t>(sizes.size());
    create.poolSizeCount = static_cast<uint32_t>(sizes.size());
    create.pPoolSizes = sizes.data();
    check(vkCreateDescriptorPool(device_, &create, nullptr, &imgui_descriptor_pool_), "vkCreateDescriptorPool");
}

VkSurfaceFormatKHR VulkanContext::choose_surface_format(std::span<const VkSurfaceFormatKHR> formats) const {
    // DirectDraw/Direct3D 2 predates sRGB render targets. The game blends packed framebuffer
    // values directly, including selector-18 ONE/ONE resource primitives. Prefer an UNORM image
    // in the nonlinear presentation color space so Vulkan blending happens in that same encoded
    // byte domain. Shaders explicitly reconstruct encoded texture bytes before modulation.
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return format;
    }
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return format;
    }
    return formats.front();
}

VkPresentModeKHR VulkanContext::choose_present_mode(std::span<const VkPresentModeKHR> modes) const {
    for (const auto mode : modes) if (mode == VK_PRESENT_MODE_MAILBOX_KHR) return mode;
    return VK_PRESENT_MODE_FIFO_KHR;
}

void VulkanContext::create_swapchain_resources() {
    VkSurfaceCapabilitiesKHR capabilities{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &capabilities), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    uint32_t format_count = 0, mode_count = 0;
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR");
    check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &mode_count, nullptr), "vkGetPhysicalDeviceSurfacePresentModesKHR");
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    std::vector<VkPresentModeKHR> modes(mode_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, formats.data());
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &mode_count, modes.data());
    const auto surface_format = choose_surface_format(formats);
    const auto present_mode = choose_present_mode(modes);
    int width = 0, height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) swapchain_extent_ = capabilities.currentExtent;
    else {
        swapchain_extent_.width = std::clamp(static_cast<uint32_t>(std::max(width, 1)), capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        swapchain_extent_.height = std::clamp(static_cast<uint32_t>(std::max(height, 1)), capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }
    uint32_t image_count = std::max(capabilities.minImageCount + 1, 2U);
    if (capabilities.maxImageCount != 0) image_count = std::min(image_count, capabilities.maxImageCount);
    VkSwapchainCreateInfoKHR create{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    create.surface = surface_;
    create.minImageCount = image_count;
    create.imageFormat = surface_format.format;
    create.imageColorSpace = surface_format.colorSpace;
    create.imageExtent = swapchain_extent_;
    create.imageArrayLayers = 1;
    create.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create.preTransform = capabilities.currentTransform;
    create.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create.presentMode = present_mode;
    create.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(device_, &create, nullptr, &swapchain_), "vkCreateSwapchainKHR");
    swapchain_format_ = surface_format.format;
    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr);
    swapchain_images_.resize(image_count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, swapchain_images_.data());
    swapchain_views_.resize(image_count);
    for (uint32_t i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = swapchain_images_[i]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = swapchain_format_;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; view.subresourceRange.levelCount = 1; view.subresourceRange.layerCount = 1;
        check(vkCreateImageView(device_, &view, nullptr, &swapchain_views_[i]), "vkCreateImageView");
    }

    VkImageCreateInfo depth{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    depth.imageType = VK_IMAGE_TYPE_2D; depth.format = depth_format_;
    depth.extent = {swapchain_extent_.width, swapchain_extent_.height, 1}; depth.mipLevels = 1; depth.arrayLayers = 1;
    depth.samples = VK_SAMPLE_COUNT_1_BIT; depth.tiling = VK_IMAGE_TILING_OPTIMAL; depth.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    check(vkCreateImage(device_, &depth, nullptr, &depth_image_), "vkCreateImage(depth)");
    VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(device_, depth_image_, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device_, &allocation, nullptr, &depth_memory_), "vkAllocateMemory(depth)");
    check(vkBindImageMemory(device_, depth_image_, depth_memory_, 0), "vkBindImageMemory(depth)");
    VkImageViewCreateInfo depth_view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; depth_view.image = depth_image_; depth_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depth_view.format = depth_format_; depth_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; depth_view.subresourceRange.levelCount = 1; depth_view.subresourceRange.layerCount = 1;
    check(vkCreateImageView(device_, &depth_view, nullptr, &depth_view_), "vkCreateImageView(depth)");

    VkAttachmentDescription color{}; color.format = swapchain_format_; color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentDescription depth_attachment{}; depth_attachment.format = depth_format_; depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const std::array<VkAttachmentDescription,2> attachments{color,depth_attachment};
    VkAttachmentReference color_ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount=1; subpass.pColorAttachments=&color_ref; subpass.pDepthStencilAttachment=&depth_ref;
    VkSubpassDependency dependency{}; dependency.srcSubpass=VK_SUBPASS_EXTERNAL; dependency.dstSubpass=0;
    dependency.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask=dependency.srcStageMask; dependency.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; render.attachmentCount=2; render.pAttachments=attachments.data(); render.subpassCount=1; render.pSubpasses=&subpass; render.dependencyCount=1; render.pDependencies=&dependency;
    check(vkCreateRenderPass(device_, &render, nullptr, &render_pass_), "vkCreateRenderPass");
    framebuffers_.resize(image_count);
    for (uint32_t i=0;i<image_count;++i){ const std::array<VkImageView,2> views{swapchain_views_[i],depth_view_};
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; framebuffer.renderPass=render_pass_; framebuffer.attachmentCount=2; framebuffer.pAttachments=views.data(); framebuffer.width=swapchain_extent_.width; framebuffer.height=swapchain_extent_.height; framebuffer.layers=1;
        check(vkCreateFramebuffer(device_,&framebuffer,nullptr,&framebuffers_[i]),"vkCreateFramebuffer"); }
}

void VulkanContext::destroy_swapchain_resources() {
    if (device_ == VK_NULL_HANDLE) return;
    for (auto framebuffer : framebuffers_) if (framebuffer) vkDestroyFramebuffer(device_, framebuffer, nullptr);
    framebuffers_.clear();
    if (render_pass_) { vkDestroyRenderPass(device_, render_pass_, nullptr); render_pass_=VK_NULL_HANDLE; }
    if (depth_view_) { vkDestroyImageView(device_, depth_view_, nullptr); depth_view_=VK_NULL_HANDLE; }
    if (depth_image_) { vkDestroyImage(device_, depth_image_, nullptr); depth_image_=VK_NULL_HANDLE; }
    if (depth_memory_) { vkFreeMemory(device_, depth_memory_, nullptr); depth_memory_=VK_NULL_HANDLE; }
    for (auto view : swapchain_views_) if (view) vkDestroyImageView(device_, view, nullptr);
    swapchain_views_.clear(); swapchain_images_.clear();
    if (swapchain_) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_=VK_NULL_HANDLE; }
}

void VulkanContext::initialize_imgui_renderer_backend(const char* failure_message) {
    ImGui_ImplVulkan_InitInfo init{};
    init.Instance = instance_;
    init.PhysicalDevice = physical_device_;
    init.Device = device_;
    init.QueueFamily = graphics_queue_family_;
    init.Queue = graphics_queue_;
    init.PipelineCache = VK_NULL_HANDLE;
    init.DescriptorPool = imgui_descriptor_pool_;
    init.MinImageCount = 2;
    init.ImageCount = static_cast<uint32_t>(swapchain_images_.size());
    init.Allocator = nullptr;
    init.CheckVkResultFn = nullptr;

#if defined(THANDOR_IMGUI_VULKAN_API_PIPELINE_INFO_MAIN)
#  if defined(THANDOR_IMGUI_VULKAN_HAS_API_VERSION)
    init.ApiVersion = VK_API_VERSION_1_1;
#  endif
    init.PipelineInfoMain.RenderPass = render_pass_;
    init.PipelineInfoMain.Subpass = 0;
    init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    const bool initialized = ImGui_ImplVulkan_Init(&init);
#elif defined(THANDOR_IMGUI_VULKAN_API_RENDER_PASS_INIT_INFO)
    init.RenderPass = render_pass_;
    init.Subpass = 0;
    init.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    const bool initialized = ImGui_ImplVulkan_Init(&init);
#elif defined(THANDOR_IMGUI_VULKAN_API_LEGACY_TWO_ARGUMENT)
    init.Subpass = 0;
    init.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    const bool initialized = ImGui_ImplVulkan_Init(&init, render_pass_);
#else
#  error "No supported Dear ImGui Vulkan initialization API was selected"
#endif

    if (!initialized) throw std::runtime_error(failure_message);

#if defined(THANDOR_IMGUI_VULKAN_MANUAL_FONT_UPLOAD)
    immediate_submit([](VkCommandBuffer command) { ImGui_ImplVulkan_CreateFontsTexture(command); });
    ImGui_ImplVulkan_DestroyFontUploadObjects();
#endif
}

void VulkanContext::recreate_swapchain() {
    int width = 0;
    int height = 0;
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window_, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(device_);
    if (imgui_initialized_) ImGui_ImplVulkan_Shutdown();
    destroy_swapchain_resources();
    create_swapchain_resources();

    if (imgui_initialized_) {
        initialize_imgui_renderer_backend("ImGui Vulkan renderer reinitialization failed");
    }
    framebuffer_resized_ = false;
}

void VulkanContext::initialize_imgui() {
    if (imgui_initialized_) return;
    if (!ImGui_ImplGlfw_InitForVulkan(window_, true)) throw std::runtime_error("ImGui GLFW backend initialization failed");
    initialize_imgui_renderer_backend("ImGui Vulkan backend initialization failed");
    imgui_initialized_ = true;
}

void VulkanContext::shutdown_imgui() {
    if (!imgui_initialized_) return;
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    imgui_initialized_=false;
}

std::optional<VulkanContext::Frame> VulkanContext::begin_frame() {
    auto& frame=frames_[current_frame_];
    check(vkWaitForFences(device_,1,&frame.fence,VK_TRUE,UINT64_MAX),"vkWaitForFences");
    uint32_t image_index=0;
    const auto acquired=vkAcquireNextImageKHR(device_,swapchain_,UINT64_MAX,frame.image_available,VK_NULL_HANDLE,&image_index);
    if (acquired==VK_ERROR_OUT_OF_DATE_KHR){recreate_swapchain();return std::nullopt;}
    if (acquired!=VK_SUCCESS && acquired!=VK_SUBOPTIMAL_KHR) check(acquired,"vkAcquireNextImageKHR");
    check(vkResetFences(device_,1,&frame.fence),"vkResetFences");
    check(vkResetCommandBuffer(frame.command_buffer,0),"vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(frame.command_buffer,&begin),"vkBeginCommandBuffer");
    const std::array<VkClearValue,2> clear{{{{0.035f,0.045f,0.055f,1.0f}},{{1.0f,0}}}};
    VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; render.renderPass=render_pass_;render.framebuffer=framebuffers_[image_index];render.renderArea.extent=swapchain_extent_;render.clearValueCount=2;render.pClearValues=clear.data();
    vkCmdBeginRenderPass(frame.command_buffer,&render,VK_SUBPASS_CONTENTS_INLINE);
    return Frame{frame.command_buffer,image_index,current_frame_};
}

void VulkanContext::end_frame(const Frame& submitted) {
    vkCmdEndRenderPass(submitted.command_buffer);
    check(vkEndCommandBuffer(submitted.command_buffer),"vkEndCommandBuffer");
    auto& frame=frames_[submitted.frame_index];
    const VkPipelineStageFlags wait_stage=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&frame.image_available;submit.pWaitDstStageMask=&wait_stage;submit.commandBufferCount=1;submit.pCommandBuffers=&submitted.command_buffer;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&frame.render_finished;
    check(vkQueueSubmit(graphics_queue_,1,&submit,frame.fence),"vkQueueSubmit");
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount=1;present.pWaitSemaphores=&frame.render_finished;present.swapchainCount=1;present.pSwapchains=&swapchain_;present.pImageIndices=&submitted.image_index;
    const auto result=vkQueuePresentKHR(graphics_queue_,&present);
    if (result==VK_ERROR_OUT_OF_DATE_KHR||result==VK_SUBOPTIMAL_KHR||framebuffer_resized_) recreate_swapchain();
    else if(result!=VK_SUCCESS) check(result,"vkQueuePresentKHR");
    current_frame_=(current_frame_+1)%kFramesInFlight;
}

void VulkanContext::wait_idle() const { if(device_) vkDeviceWaitIdle(device_); }

void VulkanContext::immediate_submit(const std::function<void(VkCommandBuffer)>& recorder) const {
    VkCommandBuffer command=VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocate.commandPool=command_pool_;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
    if(vkAllocateCommandBuffers(device_,&allocate,&command)!=VK_SUCCESS)throw std::runtime_error("immediate command allocation failed");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(command,&begin);recorder(command);vkEndCommandBuffer(command);
    VkFence fence=VK_NULL_HANDLE;VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};vkCreateFence(device_,&fence_info,nullptr,&fence);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
    vkQueueSubmit(graphics_queue_,1,&submit,fence);vkWaitForFences(device_,1,&fence,VK_TRUE,UINT64_MAX);vkDestroyFence(device_,fence,nullptr);vkFreeCommandBuffers(device_,command_pool_,1,&command);
}

uint32_t VulkanContext::find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(physical_device_,&memory);
    for(uint32_t i=0;i<memory.memoryTypeCount;++i)if((type_bits&(1U<<i))&&(memory.memoryTypes[i].propertyFlags&properties)==properties)return i;
    throw std::runtime_error("no compatible Vulkan memory type");
}

void VulkanContext::framebuffer_resize_callback(GLFWwindow* window,int,int){
    if(auto* context=static_cast<VulkanContext*>(glfwGetWindowUserPointer(window)))context->framebuffer_resized_=true;
}

} // namespace editor
