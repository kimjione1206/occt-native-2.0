#pragma once

#include "base_engine.h"
#include "cpu/error_verifier.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace occt {

class SensorManager; // forward declaration

enum class CpuStressMode {
    AUTO,           // Auto-detect best ISA for this CPU
    AVX2_FMA,       // AVX2 FMA intensive load
    AVX512_FMA,     // AVX-512 FMA load
    AVX_FLOAT,      // Pure AVX 256-bit without FMA
    SSE_FLOAT,      // SSE floating point
    LINPACK,        // DGEMM matrix multiply
    PRIME,          // Prime number test
    CACHE_ONLY,     // Small data set fitting in CPU caches (L1/L2/L3)
    LARGE_DATA_SET, // Large data set stressing CPU + memory bus
    ALL             // Mixed workload
};

enum class LoadPattern {
    STEADY,         // Constant load with fixed operands
    VARIABLE,       // Change operands every 10 minutes
    CORE_CYCLING    // Test one core at a time, rotating every 150ms
};

enum class CpuIntensityMode {
    NORMAL,         // Verify every batch for maximum error detection
    EXTREME         // Maximum stress, verify every 10th batch
};

struct CpuMetrics {
    double gflops = 0.0;               // Current GFLOPS
    double peak_gflops = 0.0;          // Peak GFLOPS
    double temperature = 0.0;          // CPU temperature (Celsius)
    double power_watts = 0.0;          // Power consumption
    bool power_estimated = false;      // True if power is TDP*usage% estimate
    int active_threads = 0;            // Active thread count
    uint64_t total_ops = 0;            // Total operations
    double elapsed_secs = 0.0;         // Elapsed time
    std::vector<double> per_core_usage; // Per-core utilization (0.0 - 100.0)
    std::vector<std::string> per_core_type;  // "P-core", "E-core", "Unknown"

    // Error verification
    int error_count = 0;                  // Total errors detected
    std::vector<CpuError> errors;         // All detected errors
    std::vector<bool> core_has_error;     // Per-core error status (true = error detected)
    std::vector<int> per_core_error_count; // Error count per physical core
};

class CpuEngine : public IEngine {
public:
    CpuEngine();
    ~CpuEngine() override;

    // Start stress test
    // num_threads: 0 = auto-detect (all logical cores)
    // duration_secs: 0 = run until stop() is called
    void start(CpuStressMode mode, int num_threads = 0, int duration_secs = 0,
               LoadPattern pattern = LoadPattern::STEADY,
               CpuIntensityMode intensity = CpuIntensityMode::EXTREME);
    void stop() override;
    bool is_running() const override;
    std::string name() const override { return "CPU"; }
    CpuMetrics get_metrics() const;

    /// Set SensorManager for temperature/power readings.
    void set_sensor_manager(SensorManager* mgr);

    /// Get formatted error summary string.
    /// Example: "2 error(s) on 2 core(s): Core #3 (1), Core #7 (1)"
    std::string error_summary() const;

    // Metrics callback - called every 500ms from metrics thread
    using MetricsCallback = std::function<void(const CpuMetrics&)>;
    void set_metrics_callback(MetricsCallback cb);

private:
    void worker_thread(int thread_id, int core_id);
    void metrics_thread_func();
    void set_thread_affinity(int core_id);

    std::vector<std::thread> workers_;
    std::thread metrics_thread_;
    std::atomic<bool> running_{false};
    CpuStressMode mode_ = CpuStressMode::AVX2_FMA;
    LoadPattern load_pattern_ = LoadPattern::STEADY;
    CpuIntensityMode intensity_mode_ = CpuIntensityMode::EXTREME;
    int num_threads_ = 0;
    int duration_secs_ = 0;
    std::chrono::steady_clock::time_point start_time_;

    mutable std::mutex metrics_mutex_;
    CpuMetrics current_metrics_;
    MetricsCallback metrics_callback_;

    // Per-thread operation counters
    std::vector<std::atomic<uint64_t>> thread_ops_;

    // Core cycling support
    std::atomic<int> active_core_{0};   // Currently active core for CORE_CYCLING pattern
    std::thread cycling_thread_;        // Thread that rotates active_core_ every 150ms

    // Sensor manager for temperature/power (non-owning)
    SensorManager* sensor_mgr_ = nullptr;

    // Protects start()/stop() against concurrent calls
    std::mutex start_stop_mutex_;

    // Error verification system
    ErrorVerifier error_verifier_;
    // Per-core error flag for quick GUI lookup
    std::vector<std::atomic<bool>> core_error_flags_;
    // Per-core error count
    std::vector<std::atomic<int>> core_error_counts_;
};

} // namespace occt
