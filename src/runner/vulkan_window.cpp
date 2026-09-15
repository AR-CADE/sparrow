#include "vulkan_window.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK_VK(expr)                                                             \
        do {                                                                            \
            VkResult _res = (expr);                                                    \
            if (_res != VK_SUCCESS) {                                                  \
                fprintf(stderr, "[vulkan-window] %s failed with VkResult=%d (%s:%d)\n", \
    #expr, _res, __FILE__, __LINE__);                                   \
                return false;                                                          \
            }                                                                          \
        } while (0)

VulkanWindow::VulkanWindow() = default;

VulkanWindow::~VulkanWindow()
{
    shutdown();
}

bool VulkanWindow::init(struct wl_display *display,
    struct wl_surface *surface,
    int32_t width,
    int32_t height,
    bool enable_validation,
    VkPresentModeKHR preferred_present_mode)
{
    wl_display_ = display;
    wl_surface_ = surface;
    width_  = width > 0 ? width : 1280;
    height_ = height > 0 ? height : 720;
    enable_validation_ = enable_validation;
    preferred_present_mode_ = preferred_present_mode;

    if (!create_instance())
    {
        fprintf(stderr, "[vulkan-window] Failed to create Vulkan instance\n");
        return false;
    }

    if (!create_surface(display, surface))
    {
        fprintf(stderr, "[vulkan-window] Failed to create Wayland Vulkan surface\n");
        return false;
    }

    if (!select_physical_device())
    {
        fprintf(stderr, "[vulkan-window] Failed to select compatible physical device\n");
        return false;
    }

    if (!create_logical_device())
    {
        fprintf(stderr, "[vulkan-window] Failed to create Vulkan logical device\n");
        return false;
    }

    // Command pool
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family_index_;
    CHECK_VK(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_));

    // Sync primitives
    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    CHECK_VK(vkCreateFence(device_, &fence_info, nullptr, &image_ready_fence_));

    if (!create_swapchain())
    {
        fprintf(stderr, "[vulkan-window] Failed to create swapchain\n");
        return false;
    }

    printf(
        "[vulkan-window] Vulkan Wayland renderer successfully initialized (%dx%d, swapchain images=%zu, validation=%s)\n",
        swapchain_extent_.width, swapchain_extent_.height, swapchain_images_.size(),
        enable_validation_ ? "enabled" : "disabled");
    return true;
}

bool VulkanWindow::create_instance()
{
    enabled_instance_extensions_.clear();
    enabled_instance_extensions_.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    enabled_instance_extensions_.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);

    enabled_layer_names_.clear();
    if (enable_validation_)
    {
        uint32_t layer_count = 0;
        vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
        std::vector<VkLayerProperties> available_layers(layer_count);
        vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

        for (const auto & layer : available_layers)
        {
            if (strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
            {
                enabled_layer_names_.push_back("VK_LAYER_KHRONOS_validation");
                printf("[vulkan-window] Enabled Khronos Validation Layer\n");
                break;
            }
        }
    }

    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName   = "Sparrow App Runner";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName   = "Flutter Embedder";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion    = VK_API_VERSION_1_2;

    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount   = static_cast<uint32_t>(enabled_instance_extensions_.size());
    create_info.ppEnabledExtensionNames = enabled_instance_extensions_.data();
    create_info.enabledLayerCount   = static_cast<uint32_t>(enabled_layer_names_.size());
    create_info.ppEnabledLayerNames = enabled_layer_names_.data();

    CHECK_VK(vkCreateInstance(&create_info, nullptr, &instance_));
    return true;
}

bool VulkanWindow::create_surface(struct wl_display *display, struct wl_surface *surface)
{
    VkWaylandSurfaceCreateInfoKHR create_info = {};
    create_info.sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    create_info.display = display;
    create_info.surface = surface;

    CHECK_VK(vkCreateWaylandSurfaceKHR(instance_, &create_info, nullptr, &surface_));
    return true;
}

bool VulkanWindow::select_physical_device()
{
    uint32_t device_count = 0;
    CHECK_VK(vkEnumeratePhysicalDevices(instance_, &device_count, nullptr));
    if (device_count == 0)
    {
        fprintf(stderr, "[vulkan-window] No Vulkan physical devices found\n");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    CHECK_VK(vkEnumeratePhysicalDevices(instance_, &device_count, devices.data()));

    int best_score = -1;
    for (const auto & dev : devices)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);

        uint32_t qf_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qf_count, nullptr);
        std::vector<VkQueueFamilyProperties> qf_props(qf_count);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qf_count, qf_props.data());

        int graphics_qf = -1;
        for (uint32_t i = 0; i < qf_count; ++i)
        {
            VkBool32 present_supported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &present_supported);
            if ((qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (present_supported == VK_TRUE))
            {
                graphics_qf = static_cast<int>(i);
                break;
            }
        }

        if (graphics_qf < 0)
        {
            continue;
        }

        // Check for VK_KHR_swapchain extension
        uint32_t ext_count = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &ext_count, nullptr);
        std::vector<VkExtensionProperties> available_exts(ext_count);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &ext_count, available_exts.data());

        bool swapchain_supported = false;
        for (const auto & ext : available_exts)
        {
            if (strcmp(ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
            {
                swapchain_supported = true;
                break;
            }
        }

        if (!swapchain_supported)
        {
            continue;
        }

        int score = 0;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            score += 1000;
        } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        {
            score += 500;
        }

        if (score > best_score)
        {
            best_score = score;
            physical_device_    = dev;
            queue_family_index_ = static_cast<uint32_t>(graphics_qf);
        }
    }

    if (physical_device_ == VK_NULL_HANDLE)
    {
        fprintf(stderr, "[vulkan-window] No suitable Vulkan device with Wayland present support found\n");
        return false;
    }

    VkPhysicalDeviceProperties chosen_props;
    vkGetPhysicalDeviceProperties(physical_device_, &chosen_props);
    printf("[vulkan-window] Selected GPU: %s (API: %u.%u.%u)\n",
        chosen_props.deviceName,
        VK_API_VERSION_MAJOR(chosen_props.apiVersion),
        VK_API_VERSION_MINOR(chosen_props.apiVersion),
        VK_API_VERSION_PATCH(chosen_props.apiVersion));

    return true;
}

bool VulkanWindow::create_logical_device()
{
    enabled_device_extensions_.clear();
    enabled_device_extensions_.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = queue_family_index_;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    VkPhysicalDeviceFeatures device_features = {};

    VkDeviceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = 1;
    create_info.pQueueCreateInfos    = &queue_info;
    create_info.pEnabledFeatures     = &device_features;
    create_info.enabledExtensionCount   = static_cast<uint32_t>(enabled_device_extensions_.size());
    create_info.ppEnabledExtensionNames = enabled_device_extensions_.data();

    CHECK_VK(vkCreateDevice(physical_device_, &create_info, nullptr, &device_));
    vkGetDeviceQueue(device_, queue_family_index_, 0, &queue_);
    return true;
}

bool VulkanWindow::create_swapchain()
{
    if ((device_ == VK_NULL_HANDLE) || (surface_ == VK_NULL_HANDLE))
    {
        return false;
    }

    // Ensure all submitted GPU work completes before recreating the swapchain
    vkDeviceWaitIdle(device_);

    VkSurfaceCapabilitiesKHR caps;
    CHECK_VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &caps));

    uint32_t format_count = 0;
    CHECK_VK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    CHECK_VK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, formats.data()));

    surface_format_ = formats[0];
    for (const auto & f : formats)
    {
        if (((f.format == VK_FORMAT_B8G8R8A8_UNORM) || (f.format == VK_FORMAT_R8G8B8A8_UNORM)) &&
            (f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR))
        {
            surface_format_ = f;
            break;
        }
    }

    uint32_t mode_count = 0;
    CHECK_VK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &mode_count, nullptr));
    std::vector<VkPresentModeKHR> modes(mode_count);
    CHECK_VK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &mode_count,
        modes.data()));

    // Priority to requested present mode (FIFO by default for Wayland vsync compliance)
    VkPresentModeKHR chosen_mode = preferred_present_mode_;
    bool preferred_found = false;
    for (const auto & m : modes)
    {
        if (m == preferred_present_mode_)
        {
            preferred_found = true;
            break;
        }
    }

    if (!preferred_found)
    {
        // Fallback: search for FIFO (guaranteed by Vulkan specification)
        bool fifo_found = false;
        for (const auto & m : modes)
        {
            if (m == VK_PRESENT_MODE_FIFO_KHR)
            {
                fifo_found  = true;
                chosen_mode = VK_PRESENT_MODE_FIFO_KHR;
                break;
            }
        }

        if (!fifo_found && !modes.empty())
        {
            chosen_mode = modes[0];
        }
    }

    const char *mode_str = "FIFO";
    if (chosen_mode == VK_PRESENT_MODE_MAILBOX_KHR)
    {
        mode_str = "MAILBOX";
    } else if (chosen_mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
    {
        mode_str = "IMMEDIATE";
    } else if (chosen_mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR)
    {
        mode_str = "FIFO_RELAXED";
    }

    printf("[vulkan-window] Selected Swapchain present mode: %s (FIFO prioritized: %s)\n",
        mode_str, (chosen_mode == VK_PRESENT_MODE_FIFO_KHR) ? "yes" : "no");

    int32_t current_w = 1280;
    int32_t current_h = 720;
    {
        std::lock_guard<std::mutex> lock(resize_mutex_);
        current_w = width_;
        current_h = height_;
    }

    if (caps.currentExtent.width != UINT32_MAX)
    {
        swapchain_extent_ = caps.currentExtent;
    } else
    {
        swapchain_extent_.width = std::clamp(static_cast<uint32_t>(current_w), caps.minImageExtent.width,
            caps.maxImageExtent.width);
        swapchain_extent_.height = std::clamp(static_cast<uint32_t>(current_h), caps.minImageExtent.height,
            caps.maxImageExtent.height);
    }

    uint32_t image_count = caps.minImageCount + 1;
    if ((caps.maxImageCount > 0) && (image_count > caps.maxImageCount))
    {
        image_count = caps.maxImageCount;
    }

    VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
    {
        composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    } else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
    {
        composite_alpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    } else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
    {
        composite_alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }

    VkSwapchainKHR old_swapchain = swapchain_;

    VkSwapchainCreateInfoKHR create_info = {};
    create_info.sType   = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = surface_;
    create_info.minImageCount   = image_count;
    create_info.imageFormat     = surface_format_.format;
    create_info.imageColorSpace = surface_format_.colorSpace;
    create_info.imageExtent     = swapchain_extent_;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.preTransform     = caps.currentTransform;
    create_info.compositeAlpha   = composite_alpha;
    create_info.presentMode = chosen_mode;
    create_info.clipped     = VK_TRUE;
    create_info.oldSwapchain = old_swapchain;

    CHECK_VK(vkCreateSwapchainKHR(device_, &create_info, nullptr, &swapchain_));

    if (old_swapchain != VK_NULL_HANDLE)
    {
        destroy_transition_command_buffers();
        for (auto & sem : present_semaphores_)
        {
            if (sem != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device_, sem, nullptr);
                sem = VK_NULL_HANDLE;
            }
        }

        present_semaphores_.clear();
        vkDestroySwapchainKHR(device_, old_swapchain, nullptr);
    }

    uint32_t actual_image_count = 0;
    CHECK_VK(vkGetSwapchainImagesKHR(device_, swapchain_, &actual_image_count, nullptr));
    swapchain_images_.resize(actual_image_count);
    CHECK_VK(vkGetSwapchainImagesKHR(device_, swapchain_, &actual_image_count, swapchain_images_.data()));

    present_semaphores_.resize(actual_image_count, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo sem_info = {};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (size_t i = 0; i < actual_image_count; ++i)
    {
        CHECK_VK(vkCreateSemaphore(device_, &sem_info, nullptr, &present_semaphores_[i]));
    }

    return record_transition_command_buffers();
}

void VulkanWindow::destroy_swapchain()
{
    destroy_transition_command_buffers();

    for (auto & sem : present_semaphores_)
    {
        if (sem != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device_, sem, nullptr);
            sem = VK_NULL_HANDLE;
        }
    }

    present_semaphores_.clear();

    if (swapchain_ != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }

    swapchain_images_.clear();
}

bool VulkanWindow::record_transition_command_buffers()
{
    transition_command_buffers_.resize(swapchain_images_.size());

    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = static_cast<uint32_t>(transition_command_buffers_.size());

    CHECK_VK(vkAllocateCommandBuffers(device_, &alloc_info, transition_command_buffers_.data()));

    for (size_t i = 0; i < swapchain_images_.size(); ++i)
    {
        VkCommandBuffer cb = transition_command_buffers_[i];
        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        CHECK_VK(vkBeginCommandBuffer(cb, &begin_info));

        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = swapchain_images_[i];
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;

        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        CHECK_VK(vkEndCommandBuffer(cb));
    }

    return true;
}

void VulkanWindow::destroy_transition_command_buffers()
{
    if (!transition_command_buffers_.empty() && (command_pool_ != VK_NULL_HANDLE))
    {
        vkFreeCommandBuffers(device_, command_pool_,
            static_cast<uint32_t>(transition_command_buffers_.size()),
            transition_command_buffers_.data());
        transition_command_buffers_.clear();
    }
}

void VulkanWindow::mark_resize(int32_t width, int32_t height)
{
    if ((width > 0) && (height > 0))
    {
        {
            std::lock_guard<std::mutex> lock(resize_mutex_);
            width_  = width;
            height_ = height;
        }
        resize_pending_.store(true, std::memory_order_release);
    }
}

FlutterVulkanImage VulkanWindow::get_next_image(const FlutterFrameInfo *frame_info)
{
    (void)frame_info;
    if (resize_pending_.load(std::memory_order_acquire))
    {
        resize_pending_.store(false, std::memory_order_release);
        create_swapchain();
    }

    VkResult res = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
        VK_NULL_HANDLE, image_ready_fence_, &last_image_index_);

    if ((res == VK_ERROR_OUT_OF_DATE_KHR) || (res == VK_SUBOPTIMAL_KHR))
    {
        if (res == VK_SUBOPTIMAL_KHR)
        {
            vkWaitForFences(device_, 1, &image_ready_fence_, VK_TRUE, UINT64_MAX);
            vkResetFences(device_, 1, &image_ready_fence_);
        }

        create_swapchain();
        vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
            VK_NULL_HANDLE, image_ready_fence_, &last_image_index_);
    }

    vkWaitForFences(device_, 1, &image_ready_fence_, VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &image_ready_fence_);

    FlutterVulkanImage fl_image = {};
    fl_image.struct_size = sizeof(FlutterVulkanImage);
    fl_image.image  = reinterpret_cast<uint64_t>(swapchain_images_[last_image_index_]);
    fl_image.format = static_cast<uint32_t>(surface_format_.format);
    return fl_image;
}

bool VulkanWindow::present_image(const FlutterVulkanImage *image)
{
    (void)image;
    if (last_image_index_ >= present_semaphores_.size())
    {
        return false;
    }

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers    = &transition_command_buffers_[last_image_index_];
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores    = &present_semaphores_[last_image_index_];

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (vkQueueSubmit(queue_, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS)
        {
            return false;
        }

        VkPresentInfoKHR present_info = {};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores    = &present_semaphores_[last_image_index_];
        present_info.swapchainCount     = 1;
        present_info.pSwapchains   = &swapchain_;
        present_info.pImageIndices = &last_image_index_;

        VkResult res = vkQueuePresentKHR(queue_, &present_info);
        if ((res == VK_ERROR_OUT_OF_DATE_KHR) || (res == VK_SUBOPTIMAL_KHR))
        {
            resize_pending_.store(true, std::memory_order_release);
        }
    }

    return true;
}

void* VulkanWindow::get_instance_proc_address(void *user_data,
    FlutterVulkanInstanceHandle instance,
    const char *name)
{
    if (name == nullptr)
    {
        return nullptr;
    }

    auto *self = static_cast<VulkanWindow*>(user_data);
    VkInstance vk_inst =
        instance ? static_cast<VkInstance>(instance) : (self ? self->instance_ : VK_NULL_HANDLE);
    return reinterpret_cast<void*>(vkGetInstanceProcAddr(vk_inst, name));
}

void VulkanWindow::shutdown()
{
    if (device_ != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device_);
        destroy_swapchain();

        if (image_ready_fence_ != VK_NULL_HANDLE)
        {
            vkDestroyFence(device_, image_ready_fence_, nullptr);
            image_ready_fence_ = VK_NULL_HANDLE;
        }

        if (command_pool_ != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device_, command_pool_, nullptr);
            command_pool_ = VK_NULL_HANDLE;
        }

        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (surface_ != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}
