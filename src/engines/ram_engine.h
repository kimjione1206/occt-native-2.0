#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "base_engine.h"

namespace occt {

struct MemoryError {
    uint64_t address;
    uint64_t expected;
    uint64_t actual;
    uint64_t bit_diff;      // expected XOR actual — which bits flipped
    int flipped_bits;       // popcount of bit_diff
    double timestamp_secs;  // time since test start
};

enum class RamPattern {
    MARCH_C_MINUS,  // March C- algorithm (stuck-at / coupling faults)
    WALKING_ONES,   // Walking 1 bit pattern
    WALKING_ZEROS,  // Walking 0 bit pattern
    CHECKERBOARD,   // 0xAA / 0x55 alternating
    RANDOM,         // Pseudo-random pattern (xoshiro256**)
    BANDWIDTH       // Memory bandwidth measurement (streaming stores)
};

struct RamMetrics {
    double bandwidth_mbs  = 0.0;  // MB/s throughput
    uint64_t errors_found = 0;    // bit-error count
    double memory_used_mb = 0.0;  // allocated test region in MB
    double elapsed_secs   = 0.0;
    double progress_pct   = 0.0;  // 0 ~ 100
    bool pages_locked     = true; // false if VirtualLock failed (results may be less accurate)
    std::vector<MemoryError> error_log;  // detailed error records (capped at 1000)
};

class RamEngine : public IEngine {
public:
    RamEngine();
    ~RamEngine() override;

    RamEngine(const RamEngine&) = delete;
    RamEngine& operator=(const RamEngine&) = delete;

    /// Start RAM stress test.
    /// @param pattern    Test pattern to apply.
    /// @param memory_pct Fraction of total physical RAM to allocate (0.0 ~ 1.0).
    /// @param passes     Number of full passes over the buffer.
    void start(RamPattern pattern, double memory_pct = 0.70, int passes = 1);

    /// Direct MB specification instead of percentage.
    /// When set to a value > 0, start() will use this instead of memory_pct.
    void set_memory_mb(uint64_t mb);

    void stop() override;
    bool is_running() const override;
    std::string name() const override { return "RAM"; }

    RamMetrics get_metrics() const;

    using MetricsCallback = std::function<void(const RamMetrics&)>;
    void set_metrics_callback(MetricsCallback cb);

private:
    void run(RamPattern pattern, size_t total_bytes, int passes);

    // Pattern implementations (operate on 64-bit words)
    void march_c_minus(uint8_t* buf, size_t size);
    void walking_ones(uint8_t* buf, size_t size);
    void walking_zeros(uint8_t* buf, size_t size);
    void checkerboard(uint8_t* buf, size_t size);
    void random_pattern(uint8_t* buf, size_t size);
    void bandwidth_test(uint8_t* buf, size_t size);

    void report_error(uint64_t address, uint64_t expected, uint64_t actual);
    void update_progress(double pct);
    size_t get_total_physical_ram() const;

    std::thread worker_;
    std::mutex start_stop_mutex_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> locked_pages_{false}; // Whether memory pages were successfully locked

    mutable std::mutex metrics_mutex_;
    RamMetrics metrics_;
    std::chrono::steady_clock::time_point test_start_time_;

    MetricsCallback metrics_cb_;
    std::mutex cb_mutex_;

    uint64_t memory_mb_override_ = 0; // 0 = use percentage mode
};

} // namespace occt
