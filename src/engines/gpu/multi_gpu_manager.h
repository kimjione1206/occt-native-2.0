#pragma once

#include "engines/gpu_engine.h"

#include <memory>
#include <string>
#include <vector>

namespace occt { namespace gpu {

/// Manages multiple GPU engines, one per detected GPU device.
class MultiGpuManager {
public:
    MultiGpuManager();
    ~MultiGpuManager();

    MultiGpuManager(const MultiGpuManager&) = delete;
    MultiGpuManager& operator=(const MultiGpuManager&) = delete;

    /// Initialize and detect all GPUs. Returns number of GPUs found.
    int initialize();

    /// Start stress test on all GPUs.
    void start_all(GpuStressMode mode, int duration_secs = 0);

    /// Stop stress test on all GPUs.
    void stop_all();

    /// Check if any GPU is currently running a test.
    bool any_running() const;

    /// Get metrics from all GPUs.
    struct GpuMetricsEntry {
        int gpu_index;
        std::string gpu_name;
        GpuMetrics metrics;
    };
    std::vector<GpuMetricsEntry> get_all_metrics() const;

    /// Get number of detected GPUs.
    int gpu_count() const { return static_cast<int>(engines_.size()); }

    /// Access individual engine by index.
    GpuEngine* get_engine(int index);

private:
    std::vector<std::unique_ptr<GpuEngine>> engines_;
    std::vector<GpuInfo> gpu_infos_;
};

}} // namespace occt::gpu
