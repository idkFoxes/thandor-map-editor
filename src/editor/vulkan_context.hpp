#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace editor {

class VulkanContext {
public:
    static constexpr uint32_t kFramesInFlight = 2;
    [[nodiscard]] bool fill_mode_non_solid_supported() const noexcept {
        return fill_mode_non_solid_supported_;
    }

    struct Frame {
        VkCommandBuffer command_buffer{VK_NULL_HANDLE};
        uint32_t image_index{};
        uint32_t frame_index{};
    };

    explicit VulkanContext(GLFWwindow* window);
    ~VulkanContext();
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    void initialize_imgui();
    void shutdown_imgui();
    void prepare_imgui_font_update();
    void finish_imgui_font_update();
    std::optional<Frame> begin_frame();
    // Transfer commands for this frame may be recorded before its render pass.
    void begin_render_pass(const Frame& frame);
    void end_frame(const Frame& frame);
    void wait_idle() const;
    // Wait only for previously submitted renderer command buffers. This does not
    // wait for presentation or unrelated device queues and must not be invoked
    // after begin_frame() has reset an as-yet-unsubmitted frame fence.
    void wait_for_render_frames() const;
    // This slot may be overwritten only after its last submitted draw is done.
    void wait_for_render_frame(uint32_t frame_index) const;
    uint32_t current_frame_index() const noexcept { return current_frame_; }
    void immediate_submit(const std::function<void(VkCommandBuffer)>& recorder) const;

    VkInstance instance() const noexcept { return instance_; }
    VkPhysicalDevice physical_device() const noexcept { return physical_device_; }
    VkDevice device() const noexcept { return device_; }
    VkQueue graphics_queue() const noexcept { return graphics_queue_; }
    uint32_t graphics_queue_family() const noexcept { return graphics_queue_family_; }
    VkRenderPass render_pass() const noexcept { return render_pass_; }
    VkDescriptorPool imgui_descriptor_pool() const noexcept { return imgui_descriptor_pool_; }
    VkExtent2D extent() const noexcept { return swapchain_extent_; }
    VkFormat color_format() const noexcept { return swapchain_format_; }
    VkFormat depth_format() const noexcept { return depth_format_; }
    GLFWwindow* window() const noexcept { return window_; }

    uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags properties) const;
    VkCommandPool command_pool() const noexcept { return command_pool_; }
    bool sampler_anisotropy_supported() const noexcept { return sampler_anisotropy_supported_; }
    bool wireframe_supported() const noexcept { return fill_mode_non_solid_supported_; }
    uint32_t max_image_array_layers() const noexcept { return max_image_array_layers_; }

private:
    struct SyncFrame {
        VkSemaphore image_available{VK_NULL_HANDLE};
        VkFence fence{VK_NULL_HANDLE};
        VkCommandBuffer command_buffer{VK_NULL_HANDLE};
    };

    GLFWwindow* window_{};
    VkInstance instance_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    uint32_t graphics_queue_family_{};
    VkQueue graphics_queue_{VK_NULL_HANDLE};
    VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
    VkFormat swapchain_format_{VK_FORMAT_UNDEFINED};
    VkExtent2D swapchain_extent_{};
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_views_;
    VkFormat depth_format_{VK_FORMAT_D32_SFLOAT};
    // Every swapchain framebuffer needs an independent depth attachment: two
    // submissions may overlap even though the shared render pass clears depth.
    std::vector<VkImage> depth_images_;
    std::vector<VkDeviceMemory> depth_memories_;
    std::vector<VkImageView> depth_views_;
    // Presentation retains the semaphore until the associated image is
    // reacquired. Frame-slot fences do not prove that present has completed.
    std::vector<VkSemaphore> render_finished_for_image_;
    VkRenderPass render_pass_{VK_NULL_HANDLE};
    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool command_pool_{VK_NULL_HANDLE};
    std::array<SyncFrame, kFramesInFlight> frames_{};
    uint32_t current_frame_{};
    bool frame_recording_{};
    VkDescriptorPool imgui_descriptor_pool_{VK_NULL_HANDLE};
    bool imgui_initialized_{};
    bool framebuffer_resized_{};
    bool sampler_anisotropy_supported_{};
    bool fill_mode_non_solid_supported_{};
    uint32_t max_image_array_layers_{1U};

    void create_instance();
    void select_physical_device();
    void create_device();
    void create_command_resources();
    void create_sync_resources();
    void create_imgui_descriptor_pool();
    void initialize_imgui_renderer_backend(const char* failure_message);
    void create_swapchain_resources();
    void destroy_swapchain_resources();
    void recreate_swapchain();
    VkSurfaceFormatKHR choose_surface_format(std::span<const VkSurfaceFormatKHR> formats) const;
    VkPresentModeKHR choose_present_mode(std::span<const VkPresentModeKHR> modes) const;
    VkFormat choose_depth_format() const;
    static void framebuffer_resize_callback(GLFWwindow* window, int width, int height);
    void check(VkResult result, const char* operation) const;
};

} // namespace editor
