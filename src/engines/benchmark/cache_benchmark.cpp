#include "cache_benchmark.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #define OCCT_X86 1
    #ifdef _MSC_VER
        #include <intrin.h>
    #else
        #include <x86intrin.h>
    #endif
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
    #define OCCT_ARM64 1
#endif

// SIMD headers for bandwidth tests
#if OCCT_X86
    #if defined(__AVX2__) || defined(__AVX__)
        #include <immintrin.h>
        #define OCCT_HAS_AVX2_BW 1
    #endif
    #if defined(__SSE2__) || defined(_MSC_VER)
        #include <emmintrin.h>
        #define OCCT_HAS_SSE2_BW 1
    #endif
#elif OCCT_ARM64
    #include <arm_neon.h>
    #define OCCT_HAS_NEON_BW 1
#endif

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
    #include <sys/mman.h>
    #include <unistd.h>
    #include <time.h>
#endif

namespace occt {

CacheBenchmark::CacheBenchmark() = default;
CacheBenchmark::~CacheBenchmark() = default;

// ─── High-resolution timing ─────────────────────────────────────────────────

uint64_t CacheBenchmark::rdtsc_or_equivalent() {
#if OCCT_X86
    #ifdef _MSC_VER
        unsigned int aux;
        return __rdtscp(&aux);
    #else
        unsigned int lo, hi;
        __asm__ volatile("rdtscp" : "=a"(lo), "=d"(hi) :: "ecx");
        return (static_cast<uint64_t>(hi) << 32) | lo;
    #endif
#elif OCCT_ARM64
    // Use clock_gettime for ARM64
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
#else
    auto now = std::chrono::high_resolution_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count());
#endif
}

double CacheBenchmark::estimate_cpu_freq_ghz() {
#if OCCT_X86
    // Measure TSC frequency by timing a known interval
    auto t0 = std::chrono::steady_clock::now();
    uint64_t tsc0 = rdtsc_or_equivalent();

    // Busy-wait for ~50ms
    while (true) {
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (ms >= 50.0) break;
    }

    uint64_t tsc1 = rdtsc_or_equivalent();
    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    double freq = static_cast<double>(tsc1 - tsc0) / secs / 1e9;
    return (freq > 0.1) ? freq : 3.0; // fallback to 3 GHz
#else
    // ARM: rdtsc_or_equivalent already returns nanoseconds
    return 1.0; // cycles == nanoseconds for our ARM implementation
#endif
}

// ─── Pointer chase array creation ────────────────────────────────────────────

void CacheBenchmark::create_pointer_chase_array(size_t* array, size_t count) {
    // Build a random Hamiltonian cycle (single cycle random permutation)
    // This prevents HW prefetcher from detecting the pattern.

    // Create sequential indices
    std::vector<size_t> indices(count);
    std::iota(indices.begin(), indices.end(), 0);

    // Fisher-Yates shuffle to create random permutation, but ensure single cycle
    // Using Sattolo's algorithm to guarantee a single cycle
    std::mt19937_64 rng(42); // Fixed seed for reproducibility
    for (size_t i = count - 1; i > 0; --i) {
        std::uniform_int_distribution<size_t> dist(0, i - 1);
        size_t j = dist(rng);
        std::swap(indices[i], indices[j]);
    }

    // Convert permutation to pointer-chase format:
    // array[indices[i]] = indices[i+1] (circular)
    for (size_t i = 0; i < count; ++i) {
        size_t next = (i + 1) % count;
        array[indices[i]] = indices[next];
    }
}

// ─── Latency measurement ─────────────────────────────────────────────────────

double CacheBenchmark::measure_latency_ns(size_t buffer_size_bytes) {
    // Ensure buffer is aligned to cache line (64 bytes)
    const size_t element_size = sizeof(size_t);
    const size_t count = buffer_size_bytes / element_size;
    if (count < 2) return 0.0;

    // Allocate aligned buffer
    size_t* buffer = nullptr;
#if defined(_WIN32)
    buffer = static_cast<size_t*>(_aligned_malloc(buffer_size_bytes, 64));
#else
    if (posix_memalign(reinterpret_cast<void**>(&buffer), 64, buffer_size_bytes) != 0) {
        return 0.0;
    }
#endif
    if (!buffer) return 0.0;

    // Create random pointer-chase permutation
    create_pointer_chase_array(buffer, count);

    // Warm up: traverse the chain once
    size_t p = 0;
    for (size_t i = 0; i < count; ++i) {
        p = buffer[p];
    }

    // Measure: traverse LATENCY_ITERATIONS times
    uint64_t start = rdtsc_or_equivalent();
    p = 0;
    for (size_t i = 0; i < LATENCY_ITERATIONS; ++i) {
        p = buffer[p];
    }
    uint64_t end = rdtsc_or_equivalent();

    // Prevent compiler from optimizing away the loop
    volatile size_t sink = p;
    (void)sink;

    double cycles_or_ns = static_cast<double>(end - start);
    double latency_ns;

#if OCCT_X86
    // Convert cycles to nanoseconds: ns = cycles / freq_ghz
    double freq_ghz = estimate_cpu_freq_ghz();
    latency_ns = (cycles_or_ns / static_cast<double>(LATENCY_ITERATIONS)) / freq_ghz;
#else
    // ARM: rdtsc_or_equivalent already returns nanoseconds
    latency_ns = cycles_or_ns / static_cast<double>(LATENCY_ITERATIONS);
#endif

    // Free buffer
#if defined(_WIN32)
    _aligned_free(buffer);
#else
    free(buffer);
#endif

    return latency_ns;
}

// ─── Bandwidth measurement ───────────────────────────────────────────────────

double CacheBenchmark::measure_read_bandwidth_gbs(size_t buffer_size_bytes) {
    // Allocate aligned buffer
    uint8_t* buffer = nullptr;
#if defined(_WIN32)
    buffer = static_cast<uint8_t*>(_aligned_malloc(buffer_size_bytes, 64));
#else
    if (posix_memalign(reinterpret_cast<void**>(&buffer), 64, buffer_size_bytes) != 0) {
        return 0.0;
    }
#endif
    if (!buffer) return 0.0;

    // Initialize buffer
    std::memset(buffer, 0xAA, buffer_size_bytes);

    volatile uint64_t sink = 0;
    auto best_time = (std::chrono::duration<double>::max)();

    for (int pass = 0; pass < BANDWIDTH_PASSES; ++pass) {
        auto start = std::chrono::steady_clock::now();

#if defined(OCCT_HAS_AVX2_BW)
        // AVX2 streaming read
        const size_t avx_count = buffer_size_bytes / 32;
        const auto* src = reinterpret_cast<const __m256i*>(buffer);
        __m256i acc = _mm256_setzero_si256();
        for (size_t i = 0; i < avx_count; ++i) {
            acc = _mm256_xor_si256(acc, _mm256_load_si256(&src[i]));
        }
        // Extract to prevent optimization
        alignas(32) uint64_t tmp[4];
        _mm256_store_si256(reinterpret_cast<__m256i*>(tmp), acc);
        sink += tmp[0];
#elif defined(OCCT_HAS_SSE2_BW)
        const size_t sse_count = buffer_size_bytes / 16;
        const auto* src = reinterpret_cast<const __m128i*>(buffer);
        __m128i acc = _mm_setzero_si128();
        for (size_t i = 0; i < sse_count; ++i) {
            acc = _mm_xor_si128(acc, _mm_load_si128(&src[i]));
        }
        alignas(16) uint64_t tmp[2];
        _mm_store_si128(reinterpret_cast<__m128i*>(tmp), acc);
        sink += tmp[0];
#elif defined(OCCT_HAS_NEON_BW)
        const size_t neon_count = buffer_size_bytes / 16;
        const auto* src = reinterpret_cast<const uint8x16_t*>(buffer);
        uint8x16_t acc = vdupq_n_u8(0);
        for (size_t i = 0; i < neon_count; ++i) {
            acc = veorq_u8(acc, src[i]);
        }
        sink += vgetq_lane_u64(vreinterpretq_u64_u8(acc), 0);
#else
        // Scalar fallback
        const size_t count = buffer_size_bytes / sizeof(uint64_t);
        const auto* src64 = reinterpret_cast<const uint64_t*>(buffer);
        uint64_t acc = 0;
        for (size_t i = 0; i < count; ++i) {
            acc ^= src64[i];
        }
        sink += acc;
#endif

        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed < best_time) {
            best_time = elapsed;
        }
    }

    (void)sink;

    double secs = best_time.count();
    double gb = static_cast<double>(buffer_size_bytes) / (1024.0 * 1024.0 * 1024.0);
    double bw = (secs > 0.0) ? gb / secs : 0.0;

#if defined(_WIN32)
    _aligned_free(buffer);
#else
    free(buffer);
#endif

    return bw;
}

double CacheBenchmark::measure_write_bandwidth_gbs(size_t buffer_size_bytes) {
    uint8_t* buffer = nullptr;
#if defined(_WIN32)
    buffer = static_cast<uint8_t*>(_aligned_malloc(buffer_size_bytes, 64));
#else
    if (posix_memalign(reinterpret_cast<void**>(&buffer), 64, buffer_size_bytes) != 0) {
        return 0.0;
    }
#endif
    if (!buffer) return 0.0;

    std::memset(buffer, 0, buffer_size_bytes);

    auto best_time = (std::chrono::duration<double>::max)();

    for (int pass = 0; pass < BANDWIDTH_PASSES; ++pass) {
        auto start = std::chrono::steady_clock::now();

#if defined(OCCT_HAS_AVX2_BW)
        const size_t avx_count = buffer_size_bytes / 32;
        auto* dst = reinterpret_cast<__m256i*>(buffer);
        __m256i val = _mm256_set1_epi64x(static_cast<long long>(0xDEADBEEFCAFEBABEULL));
        for (size_t i = 0; i < avx_count; ++i) {
            _mm256_stream_si256(&dst[i], val);
        }
        _mm_sfence();
#elif defined(OCCT_HAS_SSE2_BW)
        const size_t sse_count = buffer_size_bytes / 16;
        auto* dst = reinterpret_cast<__m128i*>(buffer);
        __m128i val = _mm_set1_epi64x(static_cast<long long>(0xDEADBEEFCAFEBABEULL));
        for (size_t i = 0; i < sse_count; ++i) {
            _mm_stream_si128(&dst[i], val);
        }
        _mm_sfence();
#elif defined(OCCT_HAS_NEON_BW)
        const size_t neon_count = buffer_size_bytes / 16;
        auto* dst = reinterpret_cast<uint64x2_t*>(buffer);
        uint64x2_t val = vdupq_n_u64(0xDEADBEEFCAFEBABEULL);
        for (size_t i = 0; i < neon_count; ++i) {
            vst1q_u64(reinterpret_cast<uint64_t*>(&dst[i]), val);
        }
#else
        const size_t count = buffer_size_bytes / sizeof(uint64_t);
        auto* dst64 = reinterpret_cast<uint64_t*>(buffer);
        for (size_t i = 0; i < count; ++i) {
            dst64[i] = 0xDEADBEEFCAFEBABEULL;
        }
#endif

        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed < best_time) {
            best_time = elapsed;
        }
    }

    double secs = best_time.count();
    double gb = static_cast<double>(buffer_size_bytes) / (1024.0 * 1024.0 * 1024.0);
    double bw = (secs > 0.0) ? gb / secs : 0.0;

#if defined(_WIN32)
    _aligned_free(buffer);
#else
    free(buffer);
#endif

    return bw;
}

// ─── Full benchmark run ──────────────────────────────────────────────────────

CacheLatencyResult CacheBenchmark::run() {
    CacheLatencyResult result;

    std::cout << "[CacheBenchmark] Measuring L1 latency (32KB)..." << std::endl;
    result.l1_latency_ns = measure_latency_ns(L1_SIZE);

    std::cout << "[CacheBenchmark] Measuring L2 latency (256KB)..." << std::endl;
    result.l2_latency_ns = measure_latency_ns(L2_SIZE);

    std::cout << "[CacheBenchmark] Measuring L3 latency (8MB)..." << std::endl;
    result.l3_latency_ns = measure_latency_ns(L3_SIZE);

    std::cout << "[CacheBenchmark] Measuring DRAM latency (64MB)..." << std::endl;
    result.dram_latency_ns = measure_latency_ns(DRAM_SIZE);

    std::cout << "[CacheBenchmark] Measuring L1 bandwidth..." << std::endl;
    result.l1_bw_gbs = measure_read_bandwidth_gbs(L1_SIZE);

    std::cout << "[CacheBenchmark] Measuring L2 bandwidth..." << std::endl;
    result.l2_bw_gbs = measure_read_bandwidth_gbs(L2_SIZE);

    std::cout << "[CacheBenchmark] Measuring L3 bandwidth..." << std::endl;
    result.l3_bw_gbs = measure_read_bandwidth_gbs(L3_SIZE);

    std::cout << "[CacheBenchmark] Measuring DRAM bandwidth..." << std::endl;
    result.dram_bw_gbs = measure_read_bandwidth_gbs(DRAM_SIZE);

    std::cout << "[CacheBenchmark] Complete." << std::endl;
    std::cout << "  L1:   " << result.l1_latency_ns << " ns, " << result.l1_bw_gbs << " GB/s" << std::endl;
    std::cout << "  L2:   " << result.l2_latency_ns << " ns, " << result.l2_bw_gbs << " GB/s" << std::endl;
    std::cout << "  L3:   " << result.l3_latency_ns << " ns, " << result.l3_bw_gbs << " GB/s" << std::endl;
    std::cout << "  DRAM: " << result.dram_latency_ns << " ns, " << result.dram_bw_gbs << " GB/s" << std::endl;

    return result;
}

} // namespace occt
