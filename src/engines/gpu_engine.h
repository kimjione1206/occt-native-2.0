#pragma once

#include "base_engine.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace occt {

enum class GpuStressMode {
    MATRIX_MUL,      // Matrix multiplication (FP32)
    MATRIX_MUL_FP64, // Matrix multiplication (FP64)
    FMA_STRESS,      // FMA loop
    TRIG_STRESS,     // Transcendental functions (sin/cos/exp)
    VRAM_TEST,       // VRAM pattern test
    MIXED,           // Mixed workload
    VULKAN_3D,       // Vulkan 3D rendering stress test
    VULKAN_ADAPTIVE, // Vulkan adaptive load stress test
};

/// Adaptive load mode for VULKAN_ADAPTIVE stress mode.
enum class AdaptiveMode {
    VARIABLE,    // Gradually increase load +5% every 20 seconds
    SWITCH,      // Alternate between 20% and 80% load
    COIL_WHINE,  // Square wave oscillation at specified frequency for coil whine testing
};

struct GpuInfo {
    std::string name;
    std::string vendor;         // "NVIDIA", "AMD", "Intel", "Apple"
    std::string driver_version;
    uint64_t vram_total_mb = 0;
    uint64_t vram_free_mb = 0;
    int compute_units = 0;
    int max_clock_mhz = 0;
};

struct GpuMetrics {
    double gflops = 0.0;
    double temperature = 0.0;
    double power_watts = 0.0;
    double gpu_usage_pct = 0.0;
    double vram_usage_pct = 0.0;
    uint64_t vram_errors = 0;
    double elapsed_secs = 0.0;
    // Vulkan 3D-specific
    double fps = 0.0;
    uint32_t draw_calls = 0;
    uint64_t artifact_count = 0;
    int shader_level = 0;
};

class GpuEngine : public IEngine {
public:
    GpuEngine();
    ~GpuEngine() override;

    // Non-copyable
    GpuEngine(const GpuEngine&) = delete;
    GpuEngine& operator=(const GpuEngine&) = delete;

    /// Detect GPUs and initialize OpenCL. Returns false if no GPU or OpenCL unavailable.
    bool initialize();

    /// Check whether OpenCL is available at runtime.
    /// If false, OpenCL GPU tests are disabled but the app continues to work.
    bool is_opencl_available() const;

    /// Check whether Vulkan is available at runtime.
    bool is_vulkan_available() const;

    /// Set shader complexity for Vulkan 3D mode (1-5).
    void set_shader_complexity(int level);

    /// Set adaptive mode for VULKAN_ADAPTIVE mode.
    void set_adaptive_mode(AdaptiveMode mode);

    /// Set the switch interval for SWITCH adaptive mode (default 0.33s = 330ms).
    void set_switch_interval(float seconds);

    /// Set frequency for COIL_WHINE adaptive mode (10-15000 Hz, 0 = sweep mode).
    void set_coil_whine_freq(float hz);

    /// List detected GPUs.
    std::vector<GpuInfo> get_available_gpus() const;

    /// Select which GPU to stress (0-based index).
    void select_gpu(int index);

    /// Start stress test. duration_secs=0 means run until stop() is called.
    /// Returns false if the test cannot start (check last_error() for details).
    bool start(GpuStressMode mode, int duration_secs = 0);

    /// Returns the last error message from a failed start() call.
    std::string last_error() const;

    /// Stop the running stress test.
    void stop() override;

    /// Check if stress test is currently running.
    bool is_running() const override;

    /// Human-readable engine name.
    std::string name() const override { return "GPU"; }

    /// Get latest metrics snapshot.
    GpuMetrics get_metrics() const;

    using MetricsCallback = std::function<void(const GpuMetrics&)>;
    void set_metrics_callback(MetricsCallback cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace occt
