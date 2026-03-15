#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace occt {

struct MemoryBenchmarkResult {
    double read_bw_gbs = 0.0;
    double write_bw_gbs = 0.0;
    double copy_bw_gbs = 0.0;
    double latency_ns = 0.0;
    int channels_detected = 0;
};

class MemoryBenchmark {
public:
    MemoryBenchmark();
    ~MemoryBenchmark();

    MemoryBenchmark(const MemoryBenchmark&) = delete;
    MemoryBenchmark& operator=(const MemoryBenchmark&) = delete;

    /// Run full memory benchmark (read, write, copy bandwidth + latency).
    MemoryBenchmarkResult run();

private:
    /// Measure sequential read bandwidth using large buffer.
    double measure_read_bw(size_t buffer_size);

    /// Measure sequential write bandwidth using streaming stores.
    double measure_write_bw(size_t buffer_size);

    /// Measure memory copy bandwidth (read + write).
    double measure_copy_bw(size_t buffer_size);

    /// Measure random access latency using pointer chasing on large buffer.
    double measure_latency(size_t buffer_size);

    /// Estimate number of memory channels from bandwidth pattern.
    int estimate_channels(double bw_gbs);

    // Buffer size for DRAM-level tests (must exceed L3)
    static constexpr size_t TEST_BUFFER_SIZE = 128 * 1024 * 1024; // 128 MB
    static constexpr int NUM_PASSES = 5;
};

} // namespace occt
