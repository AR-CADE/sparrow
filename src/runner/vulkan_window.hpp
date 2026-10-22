#ifndef VULKAN_WINDOW_HPP
#define VULKAN_WINDOW_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>
#include <wayland-client.h>

#include "flutter_embedder.h"

class VulkanWindow
{
  public:
    VulkanWindow();
    ~VulkanWindow();

    bool init(struct wl_display *display,
        struct wl_surface *surface,
        int32_t width,
        int32_t height,
        bool enable_validation = false,
        VkPresentModeKHR preferred_present_mode = VK_PRESENT_MODE_FIFO_KHR);

    void shutdown();

    void mark_resize(int32_t width, int32_t height);

    // Flutter Engine callbacks & accessors
    FlutterVulkanImage get_next_image(const FlutterFrameInfo *frame_info);
    bool present_image(const FlutterVulkanImage *image);

    static void * get_instance_proc_address(void *user_data,
        FlutterVulkanInstanceHandle instance,
        const char *name);

    VkInstance get_instance() const
    {
        return instance_;
    }

    VkPhysicalDevice get_physical_device() const
    {
        return physical_device_;
    }

    VkDevice get_device() const
    {
        return device_;
    }

    uint32_t get_queue_family_index() const
    {
        return queue_family_index_;
    }

    VkQueue get_queue() const
    {
        return queue_;
    }

    const std::vector<const char*>& get_enabled_instance_extensions() const
    {
        return enabled_instance_extensions_;
    }

    const std::vector<const char*>& get_enabled_device_extensions() const
    {
        return enabled_device_extensions_;
    }

    const std::vector<const char*>& get_enabled_layer_names() const
    {
        return enabled_layer_names_;
    }

  private:
    bool create_instance();
    bool create_surface(struct wl_display *display, struct wl_surface *surface);
    bool select_physical_device();
    bool create_logical_device();
    bool create_swapchain();
    void destroy_swapchain();
    bool record_transition_command_buffers();
    void destroy_transition_command_buffers();

    struct wl_display *wl_display_ = nullptr;
    struct wl_surface *wl_surface_ = nullptr;

    std::mutex resize_mutex_;
    int32_t width_  = 1280;
    int32_t height_ = 720;
    std::atomic<bool> resize_pending_{false};

    VkInstance instance_  = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t queue_family_index_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::mutex queue_mutex_;

    VkSurfaceFormatKHR surface_format_{};
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkExtent2D swapchain_extent_{};
    std::vector<VkImage> swapchain_images_;
    uint32_t last_image_index_ = 0;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> transition_command_buffers_;

    VkFence image_ready_fence_ = VK_NULL_HANDLE;
    std::vector<VkSemaphore> present_semaphores_;

    bool enable_validation_ = false;
    VkPresentModeKHR preferred_present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    std::vector<const char*> enabled_instance_extensions_;
    std::vector<const char*> enabled_device_extensions_;
    std::vector<const char*> enabled_layer_names_;
};

#endif // VULKAN_WINDOW_HPP
