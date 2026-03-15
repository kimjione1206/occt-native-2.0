#pragma once

#include "config.h"

#include <cstdint>
#include <string>
#include <vector>

#ifdef OCCT_HAS_VULKAN
#include <vulkan/vulkan.h>
#endif

namespace occt { namespace gpu {

#ifdef OCCT_HAS_VULKAN

/// Information about a Vulkan-capable physical device.
struct VulkanDeviceInfo {
    std::string name;
    std::string vendor;
    std::string driver_version;
    uint64_t vram_bytes = 0;
    uint32_t compute_queue_family = UINT32_MAX;
    uint32_t graphics_queue_family = UINT32_MAX;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
};

/// RAII wrapper around a Vulkan instance, device, and related objects.
/// Supports offscreen rendering (no swapchain).
class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    /// Enumerate available Vulkan physical devices.
    std::vector<VulkanDeviceInfo> enumerate_devices() const;

    /// Initialize context for a specific device (by index from enumerate_devices).
    bool init(int device_index);

    /// Check if context is valid and usable.
    bool is_valid() const { return device_ != VK_NULL_HANDLE; }

    /// Create a shader module from SPIR-V bytecode.
    VkShaderModule create_shader_module(const uint32_t* spirv, size_t spirv_size_bytes);

    /// Create an offscreen framebuffer with color attachment.
    /// Returns true on success; populates the image, view, memory, and framebuffer handles.
    struct OffscreenTarget {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
    };
    bool create_offscreen_target(uint32_t width, uint32_t height,
                                 VkRenderPass render_pass,
                                 OffscreenTarget& out);
    void destroy_offscreen_target(OffscreenTarget& target);

    /// Create a host-visible buffer for pixel readback.
    struct ReadbackBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        size_t size = 0;
        void* mapped = nullptr;
    };
    bool create_readback_buffer(size_t size_bytes, ReadbackBuffer& out);
    void destroy_readback_buffer(ReadbackBuffer& buf);

    /// Create a GPU-local vertex buffer with data.
    struct GpuBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        size_t size = 0;
    };
    bool create_gpu_buffer(VkBufferUsageFlags usage, const void* data,
                           size_t size_bytes, GpuBuffer& out);
    void destroy_gpu_buffer(GpuBuffer& buf);

    /// Allocate a primary command buffer from the pool.
    VkCommandBuffer allocate_command_buffer();

    /// Submit a command buffer and wait for completion.
    bool submit_and_wait(VkCommandBuffer cmd);

    // Accessors
    VkDevice device() const { return device_; }
    VkPhysicalDevice physical_device() const { return physical_device_; }
    VkQueue graphics_queue() const { return graphics_queue_; }
    VkCommandPool command_pool() const { return command_pool_; }
    VkPhysicalDeviceProperties device_properties() const { return device_props_; }

private:
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    uint32_t graphics_queue_family_ = UINT32_MAX;
    VkPhysicalDeviceProperties device_props_{};
    VkPhysicalDeviceMemoryProperties mem_props_{};

    std::vector<VulkanDeviceInfo> cached_devices_;
};

#else // !OCCT_HAS_VULKAN

/// Stub when Vulkan is not available.
struct VulkanDeviceInfo {
    std::string name;
    std::string vendor;
    std::string driver_version;
    uint64_t vram_bytes = 0;
};

class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext() = default;

    std::vector<VulkanDeviceInfo> enumerate_devices() const { return {}; }
    bool init(int) { return false; }
    bool is_valid() const { return false; }
};

#endif // OCCT_HAS_VULKAN

}} // namespace occt::gpu
