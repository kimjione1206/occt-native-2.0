#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace occt {

struct CacheLatencyResult {
    double l1_latency_ns = 0.0;
    double l2_latency_ns = 0.0;
    double l3_latency_ns = 0.0;
    double dram_latency_ns = 0.0;

    double l1_bw_gbs = 0.0;
    double l2_bw_gbs = 0.0;
    double l3_bw_gbs = 0.0;
    double dram_bw_gbs = 0.0;
};

class CacheBenchmark {
public:
    CacheBenchmark();
    ~CacheBenchmark();

    CacheBenchmark(const CacheBenchmark&) = delete;
    CacheBenchmark& operator=(const CacheBenchmark&) = delete;

    /// Run complete cache benchmark (latency + bandwidth for all levels).
    CacheLatencyResult run();

    /// Run latency test only for a specific buffer size.
    double measure_latency_ns(size_t buffer_size_bytes);

    /// Run bandwidth test for a specific buffer size (read bandwidth in GB/s).
    double measure_read_bandwidth_gbs(size_t buffer_size_bytes);

    /// Run write bandwidth test for a specific buffer size.
    double measure_write_bandwidth_gbs(size_t buffer_size_bytes);

private:
    /// Create a random permutation array for pointer chasing.
    /// Each element contains the index of the next element to visit.
    /// The permutation forms a single Hamiltonian cycle to prevent
    /// HW prefetcher from detecting access patterns.
    void create_pointer_chase_array(size_t* array, size_t count);

    /// Get high-resolution timestamp (RDTSC on x86, clock_gettime on ARM).
    static uint64_t rdtsc_or_equivalent();

    /// Estimate CPU frequency in GHz for converting cycles to nanoseconds.
    static double estimate_cpu_freq_ghz();

    // Buffer sizes for each cache level
    static constexpr size_t L1_SIZE  = 32 * 1024;         // 32 KB
    static constexpr size_t L2_SIZE  = 256 * 1024;        // 256 KB
    static constexpr size_t L3_SIZE  = 8 * 1024 * 1024;   // 8 MB
    static constexpr size_t DRAM_SIZE = 64 * 1024 * 1024; // 64 MB

    // Number of pointer-chase iterations for latency measurement
    static constexpr size_t LATENCY_ITERATIONS = 10'000'000;

    // Number of passes for bandwidth measurement
    static constexpr int BANDWIDTH_PASSES = 10;
};

} // namespace occt
