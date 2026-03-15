#include "vulkan_backend.h"

#ifdef OCCT_HAS_VULKAN

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>

namespace occt { namespace gpu {

// ─── Helpers ─────────────────────────────────────────────────────────────────

static std::string vk_version_string(uint32_t version) {
    std::ostringstream oss;
    oss << VK_VERSION_MAJOR(version) << "."
        << VK_VERSION_MINOR(version) << "."
        << VK_VERSION_PATCH(version);
    return oss.str();
}

static std::string vendor_name_from_id(uint32_t vendor_id) {
    switch (vendor_id) {
    case 0x1002: return "AMD";
    case 0x10DE: return "NVIDIA";
    case 0x8086: return "Intel";
    case 0x106B: return "Apple";
    case 0x13B5: return "ARM";
    case 0x5143: return "Qualcomm";
    default:     return "Unknown";
    }
}

// ─── VulkanContext ───────────────────────────────────────────────────────────

VulkanContext::VulkanContext() = default;

VulkanContext::~VulkanContext() {
    if (command_pool_ != VK_NULL_HANDLE)
        vkDestroyCommandPool(device_, command_pool_, nullptr);
    if (device_ != VK_NULL_HANDLE)
        vkDestroyDevice(device_, nullptr);
    if (instance_ != VK_NULL_HANDLE)
        vkDestroyInstance(instance_, nullptr);
}

std::vector<VulkanDeviceInfo> VulkanContext::enumerate_devices() const {
    if (!cached_devices_.empty()) return cached_devices_;

    std::vector<VulkanDeviceInfo> result;

    // Create a temporary instance if we don't have one yet
    VkInstance tmp_instance = instance_;
    bool owns_instance = false;

    if (tmp_instance == VK_NULL_HANDLE) {
        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "OCCT Stress Test";
        app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.pEngineName = "OCCT";
        app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &app_info;

#ifdef OCCT_PLATFORM_MACOS
        create_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        const char* extensions[] = {
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
        };
        create_info.enabledExtensionCount = 2;
        create_info.ppEnabledExtensionNames = extensions;
#endif

        if (vkCreateInstance(&create_info, nullptr, &tmp_instance) != VK_SUCCESS) {
            return result;
        }
        owns_instance = true;
    }

    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(tmp_instance, &device_count, nullptr);
    if (device_count == 0) {
        if (owns_instance) vkDestroyInstance(tmp_instance, nullptr);
        return result;
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(tmp_instance, &device_count, devices.data());

    for (auto& pd : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pd, &props);

        VkPhysicalDeviceMemoryProperties mem_props;
        vkGetPhysicalDeviceMemoryProperties(pd, &mem_props);

        // Find queue families
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &queue_family_count, queue_families.data());

        VulkanDeviceInfo info;
        info.name = props.deviceName;
        info.vendor = vendor_name_from_id(props.vendorID);
        info.driver_version = vk_version_string(props.driverVersion);
        // Do NOT store pd from a temporary instance -- the handle becomes
        // dangling after vkDestroyInstance. Set to VK_NULL_HANDLE; a valid
        // handle will be obtained when init() creates the real instance.
        info.physical_device = owns_instance ? VK_NULL_HANDLE : pd;

        // Sum device-local memory heaps for VRAM
        for (uint32_t i = 0; i < mem_props.memoryHeapCount; ++i) {
            if (mem_props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                info.vram_bytes += mem_props.memoryHeaps[i].size;
            }
        }

        // Find graphics+compute queue family
        for (uint32_t i = 0; i < queue_family_count; ++i) {
            if ((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                (queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                info.graphics_queue_family = i;
                break;
            }
        }

        // Find compute-only if no combined family
        if (info.graphics_queue_family == UINT32_MAX) {
            for (uint32_t i = 0; i < queue_family_count; ++i) {
                if (queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                    info.compute_queue_family = i;
                    break;
                }
            }
        }

        result.push_back(std::move(info));
    }

    // Destroy temp instance AFTER all device info has been fully copied
    if (owns_instance) vkDestroyInstance(tmp_instance, nullptr);

    // Cache for later use
    const_cast<std::vector<VulkanDeviceInfo>&>(cached_devices_) = result;
    return result;
}

bool VulkanContext::init(int device_index) {
    // Guard against re-initialization: clean up existing resources first
    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
    physical_device_ = VK_NULL_HANDLE;
    graphics_queue_ = VK_NULL_HANDLE;
    graphics_queue_family_ = UINT32_MAX;

    // Create instance
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "OCCT Stress Test";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "OCCT";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo inst_ci{};
    inst_ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_ci.pApplicationInfo = &app_info;

#ifdef OCCT_PLATFORM_MACOS
    inst_ci.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    const char* inst_extensions[] = {
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
    };
    inst_ci.enabledExtensionCount = 2;
    inst_ci.ppEnabledExtensionNames = inst_extensions;
#endif

    if (vkCreateInstance(&inst_ci, nullptr, &instance_) != VK_SUCCESS) {
        std::cerr << "[Vulkan] Failed to create instance" << std::endl;
        return false;
    }

    // Enumerate devices
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
    if (device_count == 0) {
        std::cerr << "[Vulkan] No physical devices found" << std::endl;
        return false;
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());

    if (device_index < 0 || device_index >= static_cast<int>(device_count)) {
        std::cerr << "[Vulkan] Invalid device index: " << device_index << std::endl;
        return false;
    }

    physical_device_ = devices[device_index];
    vkGetPhysicalDeviceProperties(physical_device_, &device_props_);
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props_);

    // Find graphics queue family
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &qf_count, nullptr);
    std::vector<VkQueueFamilyProperties> qf_props(qf_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &qf_count, qf_props.data());

    for (uint32_t i = 0; i < qf_count; ++i) {
        if ((qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            (qf_props[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
            graphics_queue_family_ = i;
            break;
        }
    }

    if (graphics_queue_family_ == UINT32_MAX) {
        // Fall back to any graphics queue
        for (uint32_t i = 0; i < qf_count; ++i) {
            if (qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphics_queue_family_ = i;
                break;
            }
        }
    }

    if (graphics_queue_family_ == UINT32_MAX) {
        std::cerr << "[Vulkan] No graphics queue family found" << std::endl;
        return false;
    }

    // Create logical device
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_ci{};
    queue_ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_ci.queueFamilyIndex = graphics_queue_family_;
    queue_ci.queueCount = 1;
    queue_ci.pQueuePriorities = &queue_priority;

    VkPhysicalDeviceFeatures device_features{};

    std::vector<const char*> dev_extensions;
#ifdef OCCT_PLATFORM_MACOS
    dev_extensions.push_back("VK_KHR_portability_subset");
#endif

    VkDeviceCreateInfo dev_ci{};
    dev_ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_ci.queueCreateInfoCount = 1;
    dev_ci.pQueueCreateInfos = &queue_ci;
    dev_ci.pEnabledFeatures = &device_features;
    dev_ci.enabledExtensionCount = static_cast<uint32_t>(dev_extensions.size());
    dev_ci.ppEnabledExtensionNames = dev_extensions.empty() ? nullptr : dev_extensions.data();

    if (vkCreateDevice(physical_device_, &dev_ci, nullptr, &device_) != VK_SUCCESS) {
        std::cerr << "[Vulkan] Failed to create logical device" << std::endl;
        return false;
    }

    vkGetDeviceQueue(device_, graphics_queue_family_, 0, &graphics_queue_);

    // Create command pool
    VkCommandPoolCreateInfo pool_ci{};
    pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_ci.queueFamilyIndex = graphics_queue_family_;

    if (vkCreateCommandPool(device_, &pool_ci, nullptr, &command_pool_) != VK_SUCCESS) {
        std::cerr << "[Vulkan] Failed to create command pool" << std::endl;
        return false;
    }

    std::cout << "[Vulkan] Initialized: " << device_props_.deviceName << std::endl;
    return true;
}

VkShaderModule VulkanContext::create_shader_module(const uint32_t* spirv, size_t spirv_size_bytes) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spirv_size_bytes;
    ci.pCode = spirv;

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device_, &ci, nullptr, &module) != VK_SUCCESS) {
        std::cerr << "[Vulkan] Failed to create shader module" << std::endl;
        return VK_NULL_HANDLE;
    }
    return module;
}

bool VulkanContext::create_offscreen_target(uint32_t width, uint32_t height,
                                            VkRenderPass render_pass,
                                            OffscreenTarget& out) {
    out.width = width;
    out.height = height;

    // Create image
    VkImageCreateInfo img_ci{};
    img_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_ci.imageType = VK_IMAGE_TYPE_2D;
    img_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    img_ci.extent = {width, height, 1};
    img_ci.mipLevels = 1;
    img_ci.arrayLayers = 1;
    img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    img_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    img_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device_, &img_ci, nullptr, &out.image) != VK_SUCCESS) {
        std::cerr << "[Vulkan] Failed to create offscreen image" << std::endl;
        return false;
    }

    // Allocate memory
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device_, out.image, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (alloc_info.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(device_, &alloc_info, nullptr, &out.memory) != VK_SUCCESS) {
        vkDestroyImage(device_, out.image, nullptr);
        out.image = VK_NULL_HANDLE;
        return false;
    }

    vkBindImageMemory(device_, out.image, out.memory, 0);

    // Create image view
    VkImageViewCreateInfo view_ci{};
    view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.image = out.image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.baseMipLevel = 0;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.baseArrayLayer = 0;
    view_ci.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &view_ci, nullptr, &out.view) != VK_SUCCESS) {
        vkFreeMemory(device_, out.memory, nullptr);
        vkDestroyImage(device_, out.image, nullptr);
        out.image = VK_NULL_HANDLE;
        out.memory = VK_NULL_HANDLE;
        return false;
    }

    // Create framebuffer
    VkFramebufferCreateInfo fb_ci{};
    fb_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_ci.renderPass = render_pass;
    fb_ci.attachmentCount = 1;
    fb_ci.pAttachments = &out.view;
    fb_ci.width = width;
    fb_ci.height = height;
    fb_ci.layers = 1;

    if (vkCreateFramebuffer(device_, &fb_ci, nullptr, &out.framebuffer) != VK_SUCCESS) {
        vkDestroyImageView(device_, out.view, nullptr);
        vkFreeMemory(device_, out.memory, nullptr);
        vkDestroyImage(device_, out.image, nullptr);
        out = {};
        return false;
    }

    return true;
}

void VulkanContext::destroy_offscreen_target(OffscreenTarget& target) {
    if (device_ == VK_NULL_HANDLE) return;
    if (target.framebuffer) vkDestroyFramebuffer(device_, target.framebuffer, nullptr);
    if (target.view) vkDestroyImageView(device_, target.view, nullptr);
    if (target.memory) vkFreeMemory(device_, target.memory, nullptr);
    if (target.image) vkDestroyImage(device_, target.image, nullptr);
    target = {};
}

bool VulkanContext::create_readback_buffer(size_t size_bytes, ReadbackBuffer& out) {
    out.size = size_bytes;

    VkBufferCreateInfo buf_ci{};
    buf_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_ci.size = size_bytes;
    buf_ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &buf_ci, nullptr, &out.buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device_, out.buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (alloc_info.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(device_, &alloc_info, nullptr, &out.memory) != VK_SUCCESS) {
        vkDestroyBuffer(device_, out.buffer, nullptr);
        out.buffer = VK_NULL_HANDLE;
        return false;
    }

    vkBindBufferMemory(device_, out.buffer, out.memory, 0);
    vkMapMemory(device_, out.memory, 0, size_bytes, 0, &out.mapped);

    return true;
}

void VulkanContext::destroy_readback_buffer(ReadbackBuffer& buf) {
    if (device_ == VK_NULL_HANDLE) return;
    if (buf.mapped) {
        vkUnmapMemory(device_, buf.memory);
        buf.mapped = nullptr;
    }
    if (buf.buffer) vkDestroyBuffer(device_, buf.buffer, nullptr);
    if (buf.memory) vkFreeMemory(device_, buf.memory, nullptr);
    buf = {};
}

bool VulkanContext::create_gpu_buffer(VkBufferUsageFlags usage, const void* data,
                                      size_t size_bytes, GpuBuffer& out) {
    out.size = size_bytes;

    // Create staging buffer
    VkBuffer staging_buf = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;

    VkBufferCreateInfo staging_ci{};
    staging_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    staging_ci.size = size_bytes;
    staging_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    staging_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &staging_ci, nullptr, &staging_buf) != VK_SUCCESS)
        return false;

    VkMemoryRequirements staging_reqs;
    vkGetBufferMemoryRequirements(device_, staging_buf, &staging_reqs);

    VkMemoryAllocateInfo staging_alloc{};
    staging_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    staging_alloc.allocationSize = staging_reqs.size;
    staging_alloc.memoryTypeIndex = find_memory_type(
        staging_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (staging_alloc.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(device_, &staging_alloc, nullptr, &staging_mem) != VK_SUCCESS) {
        vkDestroyBuffer(device_, staging_buf, nullptr);
        return false;
    }

    vkBindBufferMemory(device_, staging_buf, staging_mem, 0);

    // Copy data to staging
    void* mapped = nullptr;
    vkMapMemory(device_, staging_mem, 0, size_bytes, 0, &mapped);
    std::memcpy(mapped, data, size_bytes);
    vkUnmapMemory(device_, staging_mem);

    // Create device-local buffer
    VkBufferCreateInfo buf_ci{};
    buf_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_ci.size = size_bytes;
    buf_ci.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &buf_ci, nullptr, &out.buffer) != VK_SUCCESS) {
        vkFreeMemory(device_, staging_mem, nullptr);
        vkDestroyBuffer(device_, staging_buf, nullptr);
        return false;
    }

    VkMemoryRequirements buf_reqs;
    vkGetBufferMemoryRequirements(device_, out.buffer, &buf_reqs);

    VkMemoryAllocateInfo buf_alloc{};
    buf_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    buf_alloc.allocationSize = buf_reqs.size;
    buf_alloc.memoryTypeIndex = find_memory_type(
        buf_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (buf_alloc.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(device_, &buf_alloc, nullptr, &out.memory) != VK_SUCCESS) {
        vkDestroyBuffer(device_, out.buffer, nullptr);
        vkFreeMemory(device_, staging_mem, nullptr);
        vkDestroyBuffer(device_, staging_buf, nullptr);
        out.buffer = VK_NULL_HANDLE;
        return false;
    }

    vkBindBufferMemory(device_, out.buffer, out.memory, 0);

    // Copy staging -> device-local
    VkCommandBuffer cmd = allocate_command_buffer();
    if (cmd == VK_NULL_HANDLE) {
        destroy_gpu_buffer(out);
        vkFreeMemory(device_, staging_mem, nullptr);
        vkDestroyBuffer(device_, staging_buf, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    VkBufferCopy copy_region{};
    copy_region.size = size_bytes;
    vkCmdCopyBuffer(cmd, staging_buf, out.buffer, 1, &copy_region);

    vkEndCommandBuffer(cmd);
    submit_and_wait(cmd);
    vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);

    // Cleanup staging
    vkFreeMemory(device_, staging_mem, nullptr);
    vkDestroyBuffer(device_, staging_buf, nullptr);

    return true;
}

void VulkanContext::destroy_gpu_buffer(GpuBuffer& buf) {
    if (device_ == VK_NULL_HANDLE) return;
    if (buf.buffer) vkDestroyBuffer(device_, buf.buffer, nullptr);
    if (buf.memory) vkFreeMemory(device_, buf.memory, nullptr);
    buf = {};
}

VkCommandBuffer VulkanContext::allocate_command_buffer() {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &alloc_info, &cmd) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return cmd;
}

bool VulkanContext::submit_and_wait(VkCommandBuffer cmd) {
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;

    VkFenceCreateInfo fence_ci{};
    fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(device_, &fence_ci, nullptr, &fence) != VK_SUCCESS)
        return false;

    VkResult result = vkQueueSubmit(graphics_queue_, 1, &submit_info, fence);
    if (result != VK_SUCCESS) {
        vkDestroyFence(device_, fence, nullptr);
        return false;
    }

    vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device_, fence, nullptr);
    return true;
}

uint32_t VulkanContext::find_memory_type(uint32_t type_filter,
                                          VkMemoryPropertyFlags properties) const {
    for (uint32_t i = 0; i < mem_props_.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) &&
            (mem_props_.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

}} // namespace occt::gpu

#endif // OCCT_HAS_VULKAN
