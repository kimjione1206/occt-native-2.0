#pragma once

#include "config.h"

#include <cstdint>
#include <memory>
#include <vector>

#ifdef OCCT_HAS_VULKAN
#include <vulkan/vulkan.h>
#endif

namespace occt { namespace gpu {

class VulkanContext;

/// Shader complexity levels for GPU stress testing.
enum class ShaderComplexity {
    LEVEL_1 = 0,  // Basic Phong
    LEVEL_2 = 1,  // + Normal mapping + specular
    LEVEL_3 = 2,  // + Complex math (procedural texturing, FBM)
    LEVEL_4 = 3,  // + Volumetric effects
    LEVEL_5 = 4,  // + Ray marching
};

/// Result of a single rendered frame.
struct FrameResult {
    std::vector<uint8_t> pixels;  // RGBA8 pixel data
    uint32_t width = 0;
    uint32_t height = 0;
    double frame_time_ms = 0.0;
    uint32_t draw_calls = 0;
    uint32_t triangle_count = 0;
};

#ifdef OCCT_HAS_VULKAN

/// Renders stress-test frames using Vulkan offscreen rendering.
class VulkanRenderer {
public:
    VulkanRenderer();
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    /// Initialize the renderer with a VulkanContext.
    /// width/height: offscreen render target resolution.
    bool init(VulkanContext* ctx, uint32_t width, uint32_t height);

    /// Set shader complexity level.
    void set_shader_complexity(ShaderComplexity level);
    ShaderComplexity shader_complexity() const { return complexity_; }

    /// Set GPU load percentage (0.0 - 1.0).
    /// Higher values mean more draw calls and tessellation.
    void set_gpu_load(float load_pct);

    /// Render one frame and return result with pixel data.
    FrameResult render_frame(float time_secs);

    /// Cleanup all Vulkan resources.
    void cleanup();

    bool is_initialized() const { return initialized_; }

private:
    bool create_render_pass();
    bool create_pipeline(ShaderComplexity level);
    bool create_sphere_geometry(int subdivisions);
    void destroy_pipeline();

    // Sphere mesh generation
    struct Vertex {
        float pos[3];
        float normal[3];
    };

    VulkanContext* ctx_ = nullptr;
    ShaderComplexity complexity_ = ShaderComplexity::LEVEL_1;
    float gpu_load_ = 0.5f;
    bool initialized_ = false;

    uint32_t render_width_ = 512;
    uint32_t render_height_ = 512;

    // Vulkan resources
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    struct OffscreenTarget {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    } offscreen_;

    struct ReadbackBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        size_t size = 0;
    } readback_;

    struct GpuBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        size_t size = 0;
    };
    GpuBuffer vertex_buffer_{};
    GpuBuffer index_buffer_{};
    uint32_t index_count_ = 0;

    // Compiled SPIR-V modules
    VkShaderModule vert_module_ = VK_NULL_HANDLE;
    VkShaderModule frag_module_ = VK_NULL_HANDLE;
};

#else // !OCCT_HAS_VULKAN

class VulkanRenderer {
public:
    VulkanRenderer() = default;
    ~VulkanRenderer() = default;

    bool init(VulkanContext*, uint32_t, uint32_t) { return false; }
    void set_shader_complexity(ShaderComplexity) {}
    ShaderComplexity shader_complexity() const { return ShaderComplexity::LEVEL_1; }
    void set_gpu_load(float) {}
    FrameResult render_frame(float) { return {}; }
    void cleanup() {}
    bool is_initialized() const { return false; }
};

#endif // OCCT_HAS_VULKAN

}} // namespace occt::gpu
