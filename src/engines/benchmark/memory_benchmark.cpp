#include "memory_benchmark.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #define OCCT_X86 1
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
    #define OCCT_ARM64 1
#endif

#if OCCT_X86
    #if defined(__AVX2__) || defined(__AVX__)
        #include <immintrin.h>
        #define OCCT_MEM_AVX2 1
    #elif defined(__SSE2__) || defined(_MSC_VER)
        #include <emmintrin.h>
        #define OCCT_MEM_SSE2 1
    #endif
#elif OCCT_ARM64
    #include <arm_neon.h>
    #define OCCT_MEM_NEON 1
#endif

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <cstdlib>
#endif

namespace occt {

MemoryBenchmark::MemoryBenchmark() = default;
MemoryBenchmark::~MemoryBenchmark() = default;

// ─── Aligned allocation helpers ──────────────────────────────────────────────

static uint8_t* alloc_aligned(size_t size) {
    uint8_t* ptr = nullptr;
#if defined(_WIN32)
    ptr = static_cast<uint8_t*>(_aligned_malloc(size, 64));
#else
    if (posix_memalign(reinterpret_cast<void**>(&ptr), 64, size) != 0) {
        return nullptr;
    }
#endif
    return ptr;
}

static void free_aligned(uint8_t* ptr) {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

// ─── Read bandwidth ──────────────────────────────────────────────────────────

double MemoryBenchmark::measure_read_bw(size_t buffer_size) {
    uint8_t* buffer = alloc_aligned(buffer_size);
    if (!buffer) return 0.0;

    std::memset(buffer, 0xAA, buffer_size);

    volatile uint64_t sink = 0;
    auto best_time = (std::chrono::duration<double>::max)();

    for (int pass = 0; pass < NUM_PASSES; ++pass) {
        auto start = std::chrono::steady_clock::now();

#if defined(OCCT_MEM_AVX2)
        const size_t avx_count = buffer_size / 32;
        const auto* src = reinterpret_cast<const __m256i*>(buffer);
        __m256i acc = _mm256_setzero_si256();
        for (size_t i = 0; i < avx_count; ++i) {
            acc = _mm256_xor_si256(acc, _mm256_load_si256(&src[i]));
        }
        alignas(32) uint64_t tmp[4];
        _mm256_store_si256(reinterpret_cast<__m256i*>(tmp), acc);
        sink += tmp[0];
#elif defined(OCCT_MEM_SSE2)
        const size_t sse_count = buffer_size / 16;
        const auto* src = reinterpret_cast<const __m128i*>(buffer);
        __m128i acc = _mm_setzero_si128();
        for (size_t i = 0; i < sse_count; ++i) {
            acc = _mm_xor_si128(acc, _mm_load_si128(&src[i]));
        }
        alignas(16) uint64_t tmp[2];
        _mm_store_si128(reinterpret_cast<__m128i*>(tmp), acc);
        sink += tmp[0];
#elif defined(OCCT_MEM_NEON)
        const size_t neon_count = buffer_size / 16;
        const auto* src = reinterpret_cast<const uint8x16_t*>(buffer);
        uint8x16_t acc = vdupq_n_u8(0);
        for (size_t i = 0; i < neon_count; ++i) {
            acc = veorq_u8(acc, src[i]);
        }
        sink += vgetq_lane_u64(vreinterpretq_u64_u8(acc), 0);
#else
        const size_t count = buffer_size / sizeof(uint64_t);
        const auto* src64 = reinterpret_cast<const uint64_t*>(buffer);
        uint64_t acc = 0;
        for (size_t i = 0; i < count; ++i) {
            acc ^= src64[i];
        }
        sink += acc;
#endif

        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed < best_time) best_time = elapsed;
    }
    (void)sink;

    double secs = best_time.count();
    double gb = static_cast<double>(buffer_size) / (1024.0 * 1024.0 * 1024.0);

    free_aligned(buffer);
    return (secs > 0.0) ? gb / secs : 0.0;
}

// ─── Write bandwidth ─────────────────────────────────────────────────────────

double MemoryBenchmark::measure_write_bw(size_t buffer_size) {
    uint8_t* buffer = alloc_aligned(buffer_size);
    if (!buffer) return 0.0;

    std::memset(buffer, 0, buffer_size);

    auto best_time = (std::chrono::duration<double>::max)();

    for (int pass = 0; pass < NUM_PASSES; ++pass) {
        auto start = std::chrono::steady_clock::now();

#if defined(OCCT_MEM_AVX2)
        const size_t avx_count = buffer_size / 32;
        auto* dst = reinterpret_cast<__m256i*>(buffer);
        __m256i val = _mm256_set1_epi64x(static_cast<long long>(0xCAFEBABEDEADBEEFULL));
        for (size_t i = 0; i < avx_count; ++i) {
            _mm256_stream_si256(&dst[i], val);
        }
        _mm_sfence();
#elif defined(OCCT_MEM_SSE2)
        const size_t sse_count = buffer_size / 16;
        auto* dst = reinterpret_cast<__m128i*>(buffer);
        __m128i val = _mm_set1_epi64x(static_cast<long long>(0xCAFEBABEDEADBEEFULL));
        for (size_t i = 0; i < sse_count; ++i) {
            _mm_stream_si128(&dst[i], val);
        }
        _mm_sfence();
#elif defined(OCCT_MEM_NEON)
        const size_t neon_count = buffer_size / 16;
        auto* dst = reinterpret_cast<uint64x2_t*>(buffer);
        uint64x2_t val = vdupq_n_u64(0xCAFEBABEDEADBEEFULL);
        for (size_t i = 0; i < neon_count; ++i) {
            vst1q_u64(reinterpret_cast<uint64_t*>(&dst[i]), val);
        }
#else
        const size_t count = buffer_size / sizeof(uint64_t);
        auto* dst64 = reinterpret_cast<uint64_t*>(buffer);
        for (size_t i = 0; i < count; ++i) {
            dst64[i] = 0xCAFEBABEDEADBEEFULL;
        }
#endif

        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed < best_time) best_time = elapsed;
    }

    double secs = best_time.count();
    double gb = static_cast<double>(buffer_size) / (1024.0 * 1024.0 * 1024.0);

    free_aligned(buffer);
    return (secs > 0.0) ? gb / secs : 0.0;
}

// ─── Copy bandwidth ──────────────────────────────────────────────────────────

double MemoryBenchmark::measure_copy_bw(size_t buffer_size) {
    uint8_t* src_buf = alloc_aligned(buffer_size);
    uint8_t* dst_buf = alloc_aligned(buffer_size);
    if (!src_buf || !dst_buf) {
        if (src_buf) free_aligned(src_buf);
        if (dst_buf) free_aligned(dst_buf);
        return 0.0;
    }

    std::memset(src_buf, 0xAA, buffer_size);
    std::memset(dst_buf, 0, buffer_size);

    auto best_time = (std::chrono::duration<double>::max)();

    for (int pass = 0; pass < NUM_PASSES; ++pass) {
        auto start = std::chrono::steady_clock::now();

        std::memcpy(dst_buf, src_buf, buffer_size);

        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed < best_time) best_time = elapsed;
    }

    double secs = best_time.count();
    // Copy moves buffer_size bytes read + buffer_size bytes written
    double gb = static_cast<double>(buffer_size * 2) / (1024.0 * 1024.0 * 1024.0);

    free_aligned(src_buf);
    free_aligned(dst_buf);
    return (secs > 0.0) ? gb / secs : 0.0;
}

// ─── Latency (pointer chasing on large buffer) ──────────────────────────────

double MemoryBenchmark::measure_latency(size_t buffer_size) {
    const size_t count = buffer_size / sizeof(size_t);
    if (count < 2) return 0.0;

    size_t* buffer = reinterpret_cast<size_t*>(alloc_aligned(buffer_size));
    if (!buffer) return 0.0;

    // Sattolo's algorithm for single-cycle random permutation
    std::vector<size_t> indices(count);
    std::iota(indices.begin(), indices.end(), 0);
    std::mt19937_64 rng(12345);
    for (size_t i = count - 1; i > 0; --i) {
        std::uniform_int_distribution<size_t> dist(0, i - 1);
        size_t j = dist(rng);
        std::swap(indices[i], indices[j]);
    }
    for (size_t i = 0; i < count; ++i) {
        buffer[indices[i]] = indices[(i + 1) % count];
    }

    // Warmup
    size_t p = 0;
    for (size_t i = 0; i < count; ++i) {
        p = buffer[p];
    }

    // Measure
    const size_t iterations = 10'000'000;
    auto start = std::chrono::steady_clock::now();
    p = 0;
    for (size_t i = 0; i < iterations; ++i) {
        p = buffer[p];
    }
    auto end = std::chrono::steady_clock::now();

    volatile size_t sink = p;
    (void)sink;

    double secs = std::chrono::duration<double>(end - start).count();
    double latency_ns = (secs / static_cast<double>(iterations)) * 1e9;

    free_aligned(reinterpret_cast<uint8_t*>(buffer));
    return latency_ns;
}

// ─── Channel estimation ──────────────────────────────────────────────────────

int MemoryBenchmark::estimate_channels(double bw_gbs) {
    // Rough heuristic: DDR4-3200 single channel ~ 25 GB/s
    // DDR5-4800 single channel ~ 38 GB/s
    if (bw_gbs < 15.0) return 1;
    if (bw_gbs < 35.0) return 1;
    if (bw_gbs < 60.0) return 2;
    if (bw_gbs < 120.0) return 4;
    return 8;
}

// ─── Full run ────────────────────────────────────────────────────────────────

MemoryBenchmarkResult MemoryBenchmark::run() {
    MemoryBenchmarkResult result;

    std::cout << "[MemoryBenchmark] Measuring read bandwidth..." << std::endl;
    result.read_bw_gbs = measure_read_bw(TEST_BUFFER_SIZE);

    std::cout << "[MemoryBenchmark] Measuring write bandwidth..." << std::endl;
    result.write_bw_gbs = measure_write_bw(TEST_BUFFER_SIZE);

    std::cout << "[MemoryBenchmark] Measuring copy bandwidth..." << std::endl;
    result.copy_bw_gbs = measure_copy_bw(TEST_BUFFER_SIZE);

    std::cout << "[MemoryBenchmark] Measuring latency..." << std::endl;
    result.latency_ns = measure_latency(TEST_BUFFER_SIZE);

    result.channels_detected = estimate_channels(result.read_bw_gbs);

    std::cout << "[MemoryBenchmark] Complete." << std::endl;
    std::cout << "  Read:    " << result.read_bw_gbs << " GB/s" << std::endl;
    std::cout << "  Write:   " << result.write_bw_gbs << " GB/s" << std::endl;
    std::cout << "  Copy:    " << result.copy_bw_gbs << " GB/s" << std::endl;
    std::cout << "  Latency: " << result.latency_ns << " ns" << std::endl;
    std::cout << "  Channels: " << result.channels_detected << std::endl;

    return result;
}

} // namespace occt
