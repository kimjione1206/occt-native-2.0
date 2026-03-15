#include "avx_stress.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

// Prevent compiler from optimizing away results
#ifdef _MSC_VER
    #define DO_NOT_OPTIMIZE(x) { volatile auto _v = (x); (void)_v; }
#else
    #define DO_NOT_OPTIMIZE(x) asm volatile("" : : "r,m"(x) : "memory")
#endif

// Verification constants: deterministic FMA chain parameters
// seed * mul + add, repeated VERIFY_ITERATIONS times
// 하위호환을 위해 기존 상수 유지 (첫 번째 시드)
static constexpr double VERIFY_SEED = 1.0;
static constexpr double VERIFY_MUL  = 0.9999999999;
static constexpr double VERIFY_ADD  = 0.0000000001;
static constexpr int    VERIFY_ITERATIONS = 10000;  // 10,000회: 실제 OCCT 수준

// Pre-compute expected value using scalar std::fma (IEEE 754 guaranteed)
static double compute_scalar_expected() {
    double acc = VERIFY_SEED;
    for (int i = 0; i < VERIFY_ITERATIONS; ++i) {
        acc = std::fma(acc, VERIFY_MUL, VERIFY_ADD);
    }
    return acc;
}

// 다중 시드용 오버로드: 시드 세트를 받아 scalar expected 계산
static double compute_scalar_expected(const occt::cpu::VerifySeedSet& seeds, int iterations) {
    double acc = seeds.seed;
    for (int i = 0; i < iterations; ++i) {
        acc = std::fma(acc, seeds.mul, seeds.add);
    }
    return acc;
}

// 다중 시드용 non-FMA 오버로드
// volatile로 mul/add 분리를 강제하여 컴파일러의 FMA 최적화를 방지
// (SSE intrinsic의 separate mul+add와 정확히 동일한 결과 보장)
static double compute_scalar_expected_nofma(const occt::cpu::VerifySeedSet& seeds, int iterations) {
    double acc = seeds.seed;
    for (int i = 0; i < iterations; ++i) {
        volatile double tmp = acc * seeds.mul;
        acc = tmp + seeds.add;
    }
    return acc;
}

// ============================================================
// x86/x86_64: Full SIMD implementation with SSE/AVX2/AVX-512
// ============================================================
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)

// Compiler-specific intrinsic headers
#if defined(_MSC_VER)
    #include <intrin.h>
    #include <immintrin.h>
#elif defined(__GNUC__) || defined(__clang__)
    #if defined(__SSE2__)
        #include <emmintrin.h>
    #endif
    #if defined(__SSE4_2__)
        #include <nmmintrin.h>
    #endif
    #if defined(__FMA__)
        #include <immintrin.h>
    #elif defined(__AVX2__)
        #include <immintrin.h>
    #elif defined(__AVX__)
        #include <immintrin.h>
    #else
        #include <immintrin.h>
    #endif
#endif

namespace occt { namespace cpu {

// --- Runtime ISA detection ---

static void cpuid_query(int info[4], int leaf) {
#if defined(_MSC_VER)
    __cpuid(info, leaf);
#elif defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__ (
        "cpuid"
        : "=a"(info[0]), "=b"(info[1]), "=c"(info[2]), "=d"(info[3])
        : "a"(leaf), "c"(0)
    );
#endif
}

static void cpuidex_query(int info[4], int leaf, int sub) {
#if defined(_MSC_VER)
    __cpuidex(info, leaf, sub);
#elif defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__ (
        "cpuid"
        : "=a"(info[0]), "=b"(info[1]), "=c"(info[2]), "=d"(info[3])
        : "a"(leaf), "c"(sub)
    );
#endif
}

bool has_sse42() {
    int info[4] = {};
    cpuid_query(info, 1);
    return (info[2] & (1 << 20)) != 0;
}

bool has_avx2() {
    int info[4] = {};
    cpuidex_query(info, 7, 0);
    return (info[1] & (1 << 5)) != 0;
}

bool has_avx512f() {
    int info[4] = {};
    cpuidex_query(info, 7, 0);
    return (info[1] & (1 << 16)) != 0;
}

bool has_fma() {
    int info[4] = {};
    cpuid_query(info, 1);
    return (info[2] & (1 << 12)) != 0;
}

// --- SSE Stress ---
// Uses __m128d (2 doubles per register), 8 independent accumulators
// With FMA: _mm_fmadd_pd, without FMA: _mm_add_pd + _mm_mul_pd

#if defined(__SSE2__) || defined(_MSC_VER)

uint64_t stress_sse(uint64_t duration_ns) {
    __m128d acc0 = _mm_set1_pd(1.0);
    __m128d acc1 = _mm_set1_pd(1.1);
    __m128d acc2 = _mm_set1_pd(1.2);
    __m128d acc3 = _mm_set1_pd(1.3);
    __m128d acc4 = _mm_set1_pd(1.4);
    __m128d acc5 = _mm_set1_pd(1.5);
    __m128d acc6 = _mm_set1_pd(1.6);
    __m128d acc7 = _mm_set1_pd(1.7);

    const __m128d mul = _mm_set1_pd(0.9999999999);
    const __m128d add = _mm_set1_pd(0.0000000001);

    uint64_t ops = 0;
    auto start = std::chrono::high_resolution_clock::now();

    for (;;) {
        // Inner loop: 1000 iterations of FMA on all 8 accumulators
        for (int i = 0; i < 1000; ++i) {
            // Without FMA intrinsic, use mul+add (2 ops simulating FMA)
            // The compiler may fuse these if -mfma is enabled
            acc0 = _mm_add_pd(_mm_mul_pd(acc0, mul), add);
            acc1 = _mm_add_pd(_mm_mul_pd(acc1, mul), add);
            acc2 = _mm_add_pd(_mm_mul_pd(acc2, mul), add);
            acc3 = _mm_add_pd(_mm_mul_pd(acc3, mul), add);
            acc4 = _mm_add_pd(_mm_mul_pd(acc4, mul), add);
            acc5 = _mm_add_pd(_mm_mul_pd(acc5, mul), add);
            acc6 = _mm_add_pd(_mm_mul_pd(acc6, mul), add);
            acc7 = _mm_add_pd(_mm_mul_pd(acc7, mul), add);
        }
        // 8 accumulators * 1000 iterations * 2 doubles * 2 ops (mul+add) = 32000 FLOPs
        ops += 8ULL * 1000 * 2 * 2;

        // Check elapsed time every batch
        auto now = std::chrono::high_resolution_clock::now();
        uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
        if (elapsed >= duration_ns) break;
    }

    // Prevent dead code elimination
    DO_NOT_OPTIMIZE(acc0);
    DO_NOT_OPTIMIZE(acc1);
    DO_NOT_OPTIMIZE(acc2);
    DO_NOT_OPTIMIZE(acc3);
    DO_NOT_OPTIMIZE(acc4);
    DO_NOT_OPTIMIZE(acc5);
    DO_NOT_OPTIMIZE(acc6);
    DO_NOT_OPTIMIZE(acc7);

    return ops;
}

VerifyResult stress_and_verify_sse(uint64_t duration_ns) {
    VerifyResult result;
    result.lane_count = 2;

    auto start = std::chrono::high_resolution_clock::now();

    for (;;) {
        // 다중 시드 순차 테스트
        for (int s = 0; s < VERIFY_SEED_COUNT; ++s) {
            const auto& seeds = VERIFY_SEEDS[s];
            // SSE는 separate mul+add이므로 nofma expected 사용
            const double expected = compute_scalar_expected_nofma(seeds, VERIFY_ITERATIONS);

            const __m128d mul = _mm_set1_pd(seeds.mul);
            const __m128d add = _mm_set1_pd(seeds.add);
            __m128d acc = _mm_set1_pd(seeds.seed);

            for (int i = 0; i < VERIFY_ITERATIONS; ++i) {
                acc = _mm_add_pd(_mm_mul_pd(acc, mul), add);
            }
            result.ops += 2ULL * VERIFY_ITERATIONS * 2; // 2 doubles * 2 ops

            // Extract and verify
            alignas(16) double vals[2];
            _mm_store_pd(vals, acc);

            for (int lane = 0; lane < 2; ++lane) {
                uint64_t exp_bits, act_bits;
                std::memcpy(&exp_bits, &expected, sizeof(double));
                std::memcpy(&act_bits, &vals[lane], sizeof(double));
                if (exp_bits != act_bits) {
                    result.passed = false;
                    result.lane_errors++;
                    result.expected[lane] = expected;
                    result.actual[lane] = vals[lane];
                }
            }

            DO_NOT_OPTIMIZE(acc);

            if (!result.passed) break;
        }

        if (!result.passed) break;

        auto now = std::chrono::high_resolution_clock::now();
        uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
        if (elapsed >= duration_ns) break;
    }

    return result;
}

#else

uint64_t stress_sse(uint64_t /*duration_ns*/) {
    return 0; // SSE2 not available at compile time
}

VerifyResult stress_and_verify_sse(uint64_t /*duration_ns*/) {
    return VerifyResult{};
}

#endif

// --- AVX2 FMA Stress ---
// Uses __m256d (4 doubles per register), 8 independent accumulators
// _mm256_fmadd_pd: a*b+c in one instruction

#if defined(__AVX2__) && defined(__FMA__) || defined(_MSC_VER)

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2,fma")))
#endif
uint64_t stress_avx2(uint64_t duration_ns) {
    __m256d acc0 = _mm256_set1_pd(1.0);
    __m256d acc1 = _mm256_set1_pd(1.1);
    __m256d acc2 = _mm256_set1_pd(1.2);
    __m256d acc3 = _mm256_set1_pd(1.3);
    __m256d acc4 = _mm256_set1_pd(1.4);
    __m256d acc5 = _mm256_set1_pd(1.5);
    __m256d acc6 = _mm256_set1_pd(1.6);
    __m256d acc7 = _mm256_set1_pd(1.7);

    const __m256d mul = _mm256_set1_pd(0.9999999999);
    const __m256d add = _mm256_set1_pd(0.0000000001);

    uint64_t ops = 0;
    auto start = std::chrono::high_resolution_clock::now();

    for (;;) {
        for (int i = 0; i < 1000; ++i) {
            acc0 = _mm256_fmadd_pd(acc0, mul, add);
            acc1 = _mm256_fmadd_pd(acc1, mul, add);
            acc2 = _mm256_fmadd_pd(acc2, mul, add);
            acc3 = _mm256_fmadd_pd(acc3, mul, add);
            acc4 = _mm256_fmadd_pd(acc4, mul, add);
            acc5 = _mm256_fmadd_pd(acc5, mul, add);
            acc6 = _mm256_fmadd_pd(acc6, mul, add);
            acc7 = _mm256_fmadd_pd(acc7, mul, add);
        }
        // 8 accumulators * 1000 iterations * 4 doubles * 2 ops (FMA = mul+add)
        ops += 8ULL * 1000 * 4 * 2;

        auto now = std::chrono::high_resolution_clock::now();
        uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
        if (elapsed >= duration_ns) break;
    }

    DO_NOT_OPTIMIZE(acc0);
    DO_NOT_OPTIMIZE(acc1);
    DO_NOT_OPTIMIZE(acc2);
    DO_NOT_OPTIMIZE(acc3);
    DO_NOT_OPTIMIZE(acc4);
    DO_NOT_OPTIMIZE(acc5);
    DO_NOT_OPTIMIZE(acc6);
    DO_NOT_OPTIMIZE(acc7);

    return ops;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2,fma")))
#endif
VerifyResult stress_and_verify_avx2(uint64_t duration_ns) {
    VerifyResult result;
    result.lane_count = 4;

    auto start = std::chrono::high_resolution_clock::now();

    for (;;) {
        // 다중 시드 순차 테스트
        for (int s = 0; s < VERIFY_SEED_COUNT; ++s) {
            const auto& seeds = VERIFY_SEEDS[s];
            const double expected = compute_scalar_expected(seeds, VERIFY_ITERATIONS);

            const __m256d mul = _mm256_set1_pd(seeds.mul);
            const __m256d add = _mm256_set1_pd(seeds.add);
            __m256d acc = _mm256_set1_pd(seeds.seed);

            for (int i = 0; i < VERIFY_ITERATIONS; ++i) {
                acc = _mm256_fmadd_pd(acc, mul, add);
            }
            result.ops += 4ULL * VERIFY_ITERATIONS * 2;

            // Extract and verify all 4 lanes
            alignas(32) double vals[4];
            _mm256_store_pd(vals, acc);

            for (int lane = 0; lane < 4; ++lane) {
                uint64_t exp_bits, act_bits;
                std::memcpy(&exp_bits, &expected, sizeof(double));
                std::memcpy(&act_bits, &vals[lane], sizeof(double));
                if (exp_bits != act_bits) {
                    result.passed = false;
                    result.lane_errors++;
                    result.expected[lane] = expected;
                    result.actual[lane] = vals[lane];
                }
            }

            DO_NOT_OPTIMIZE(acc);

            if (!result.passed) break;
        }

        if (!result.passed) break;

        auto now = std::chrono::high_resolution_clock::now();
        uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
        if (elapsed >= duration_ns) break;
    }

    return result;
}

#else

uint64_t stress_avx2(uint64_t duration_ns) {
    // Fallback: try runtime detection, but compile-time AVX2+FMA not available
    // Use SSE stress as fallback
    return stress_sse(duration_ns);
}

VerifyResult stress_and_verify_avx2(uint64_t duration_ns) {
    return stress_and_verify_sse(duration_ns);
}

#endif

// --- AVX No-FMA Stress ---
// Uses __m256d (4 doubles per register), 8 independent accumulators
// _mm256_mul_pd + _mm256_add_pd (NO FMA intrinsic)

#if defined(__AVX__) || defined(_MSC_VER)

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx")))
#endif
uint64_t stress_avx_nofma(uint64_t duration_ns) {
    __m256d acc0 = _mm256_set1_pd(1.0);
    __m256d acc1 = _mm256_set1_pd(1.1);
    __m256d acc2 = _mm256_set1_pd(1.2);
    __m256d acc3 = _mm256_set1_pd(1.3);
    __m256d acc4 = _mm256_set1_pd(1.4);
    __m256d acc5 = _mm256_set1_pd(1.5);
    __m256d acc6 = _mm256_set1_pd(1.6);
    __m256d acc7 = _mm256_set1_pd(1.7);

    const __m256d mul = _mm256_set1_pd(0.9999999999);
    const __m256d add = _mm256_set1_pd(0.0000000001);

    uint64_t ops = 0;
    auto start = std::chrono::high_resolution_clock::now();

    for (;;) {
        for (int i = 0; i < 1000; ++i) {
            acc0 = _mm256_add_pd(_mm256_mul_pd(acc0, mul), add);
            acc1 = _mm256_add_pd(_mm256_mul_pd(acc1, mul), add);
            acc2 = _mm256_add_pd(_mm256_mul_pd(acc2, mul), add);
            acc3 = _mm256_add_pd(_mm256_mul_pd(acc3, mul), add);
            acc4 = _mm256_add_pd(_mm256_mul_pd(acc4, mul), add);
            acc5 = _mm256_add_pd(_mm256_mul_pd(acc5, mul), add);
            acc6 = _mm256_add_pd(_mm256_mul_pd(acc6, mul), add);
            acc7 = _mm256_add_pd(_mm256_mul_pd(acc7, mul), add);
        }
        // 8 accumulators * 1000 iterations * 4 doubles * 2 ops (mul+add)
        ops += 8ULL * 1000 * 4 * 2;

        auto now = std::chrono::high_resolution_clock::now();
        uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
        if (elapsed >= duration_ns) break;
    }

    DO_NOT_OPTIMIZE(acc0);
    DO_NOT_OPTIMIZE(acc1);
    DO_NOT_OPTIMIZE(acc2);
    DO_NOT_OPTIMIZE(acc3);
    DO_NOT_OPTIMIZE(acc4);
    DO_NOT_OPTIMIZE(acc5);
    DO_NOT_OPTIMIZE(acc6);
    DO_NOT_OPTIMIZE(acc7);

    return ops;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx")))
#endif
VerifyResult stress_and_verify_avx_nofma(uint64_t duration_ns) {
    VerifyResult result;
    result.lane_count = 4;

    auto start = std::chrono::high_resolution_clock::now();

    for (;;) {
        // 다중 시드 순차 테스트
        for (int s = 0; s < VERIFY_SEED_COUNT; ++s) {
            const auto& seeds = VERIFY_SEEDS[s];
            const double expected = compute_scalar_expected_nofma(seeds, VERIFY_ITERATIONS);

            const __m256d mul = _mm256_set1_pd(seeds.mul);
            const __m256d add = _mm256_set1_pd(seeds.add);
            __m256d acc = _mm256_set1_pd(seeds.seed);

            for (int i = 0; i < VERIFY_ITERATIONS; ++i) {
                acc = _mm256_add_pd(_mm256_mul_pd(acc, mul), add);
            }
            result.ops += 4ULL * VERIFY_ITERATIONS * 2;

            // Extract and verify all 4 lanes
            alignas(32) double vals[4];
            _mm256_store_pd(vals, acc);

            for (int lane = 0; lane < 4; ++lane) {
                uint64_t exp_bits, act_bits;
                std::memcpy(&exp_bits, &expected, sizeof(double));
                std::memcpy(&act_bits, &vals[lane], sizeof(double));
                if (exp_bits != act_bits) {
                    result.passed = false;
                    result.lane_errors++;
                    result.expected[lane] = expected;
                    result.actual[lane] = vals[lane];
                }
            }

            DO_NOT_OPTIMIZE(acc);

            if (!result.passed) break;
        }

        if (!result.passed) break;

        auto now = std::chrono::high_resolution_clock::now();
        uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
        if (elapsed >= duration_ns) break;
    }

    return result;
}

#else

uint64_t stress_avx_nofma(uint64_t duration_ns) {
    // AVX not available, fall back to SSE
    return stress_sse(duration_ns);
}

VerifyResult stress_and_verify_avx_nofma(uint64_t duration_ns) {
    return stress_and_verify_sse(duration_ns);
}

#endif

// --- AVX-512 FMA Stress ---
// Uses __m512d (8 doubles per register), 8 independent accumulators
// _mm512_fmadd_pd: a*b+c on 8 doubles at once

#if defined(__AVX512F__) || (defined(_MSC_VER) && defined(__AVX512F__))

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx512f")))
#endif
uint64_t stress_avx512(uint64_t duration_ns) {
    __m512d acc0 = _mm512_set1_pd(1.0);
    __m512d acc1 = _mm512_set1_pd(1.1);
    __m512d acc2 = _mm512_set1_pd(1.2);
    __m512d acc3 = _mm512_set1_pd(1.3);
    __m512d acc4 = _mm512_set1_pd(1.4);
    __m512d acc5 = _mm512_set1_pd(1.5);
    __m512d acc6 = _mm512_set1_pd(1.6);
    __m512d acc7 = _mm512_set1_pd(1.7);

    const __m512d mul = _mm512_set1_pd(0.9999999999);
    const __m512d add = _mm512_set1_pd(0.0000000001);

    uint64_t ops = 0;
    auto start = std::chrono::high_resolution_clock::now();

    for (;;) {
        for (int i = 0; i < 1000; ++i) {
            acc0 = _mm512_fmadd_pd(acc0, mul, add);
            acc1 = _mm512_fmadd_pd(acc1, mul, add);
            acc2 = _mm512_fmadd_pd(acc2, mul, add);
            acc3 = _mm512_fmadd_pd(acc3, mul, add);
            acc4 = _mm512_fmadd_pd(acc4, mul, add);
            acc5 = _mm512_fmadd_pd(acc5, mul, add);
            acc6 = _mm512_fmadd_pd(acc6, mul, add);
            acc7 = _mm512_fmadd_pd(acc7, mul, add);
        }
        // 8 accumulators * 1000 iterations * 8 doubles * 2 ops (FMA = mul+add)
        ops += 8ULL * 1000 * 8 * 2;

        auto now = std::chrono::high_resolution_clock::now();
        uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
        if (elapsed >= duration_ns) break;
    }

    DO_NOT_OPTIMIZE(acc0);
    DO_NOT_OPTIMIZE(acc1);
    DO_NOT_OPTIMIZE(acc2);
    DO_NOT_OPTIMIZE(acc3);
    DO_NOT_OPTIMIZE(acc4);
    DO_NOT_OPTIMIZE(acc5);
    DO_NOT_OPTIMIZE(acc6);
    DO_NOT_OPTIMIZE(acc7);

    return ops;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx512f")))
#endif
VerifyResult stress_and_verify_avx512(uint64_t duration_ns) {
    VerifyResult result;
    result.lane_count = 8;

    auto start = std::chrono::high_resolution_clock::now();

    for (;;) {
        // 다중 시드 순차 테스트
        for (int s = 0; s < VERIFY_SEED_COUNT; ++s) {
            const auto& seeds = VERIFY_SEEDS[s];
            const double expected = compute_scalar_expected(seeds, VERIFY_ITERATIONS);

            const __m512d mul = _mm512_set1_pd(seeds.mul);
            const __m512d add = _mm512_set1_pd(seeds.add);
            __m512d acc = _mm512_set1_pd(seeds.seed);

            for (int i = 0; i < VERIFY_ITERATIONS; ++i) {
                acc = _mm512_fmadd_pd(acc, mul, add);
            }
            result.ops += 8ULL * VERIFY_ITERATIONS * 2;

            // Extract and verify all 8 lanes
            alignas(64) double vals[8];
            _mm512_store_pd(vals, acc);

            for (int lane = 0; lane < 8; ++lane) {
                uint64_t exp_bits, act_bits;
                std::memcpy(&exp_bits, &expected, sizeof(double));
                std::memcpy(&act_bits, &vals[lane], sizeof(double));
                if (exp_bits != act_bits) {
                    result.passed = false;
                    result.lane_errors++;
                    result.expected[lane] = expected;
                    result.actual[lane] = vals[lane];
                }
            }

            DO_NOT_OPTIMIZE(acc);

            if (!result.passed) break;
        }

        if (!result.passed) break;

        auto now = std::chrono::high_resolution_clock::now();
        uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
        if (elapsed >= duration_ns) break;
    }

    return result;
}

#else

uint64_t stress_avx512(uint64_t duration_ns) {
    // AVX-512 not available, fall back to AVX2
    return stress_avx2(duration_ns);
}

VerifyResult stress_and_verify_avx512(uint64_t duration_ns) {
    return stress_and_verify_avx2(duration_ns);
}

#endif

// NEON stub for x86 builds
VerifyResult stress_and_verify_neon(uint64_t duration_ns) {
    return stress_and_verify_sse(duration_ns);
}

// AVX no-FMA stubs already defined above via #if defined(__AVX__) block

}} // namespace occt::cpu

// ============================================================
// ARM64 (Apple Silicon / AArch64): NEON SIMD implementation
// ============================================================
#elif defined(__aarch64__) || defined(_M_ARM64)

#include <arm_neon.h>

namespace occt { namespace cpu {

bool has_sse42()   { return false; }
bool has_avx2()    { return false; }
bool has_avx512f() { return false; }
bool has_fma()     { return false; }

// ARM64 NEON stress: uses float64x2_t (2 doubles per register), 8 accumulators
// vfmaq_f64: fused multiply-add on ARM64

static uint64_t stress_neon_fp64(uint64_t duration_ns) {
    float64x2_t acc0 = vdupq_n_f64(1.0);
    float64x2_t acc1 = vdupq_n_f64(1.1);
    float64x2_t acc2 = vdupq_n_f64(1.2);
    float64x2_t acc3 = vdupq_n_f64(1.3);
    float64x2_t acc4 = vdupq_n_f64(1.4);
    float64x2_t acc5 = vdupq_n_f64(1.5);
    float64x2_t acc6 = vdupq_n_f64(1.6);
    float64x2_t acc7 = vdupq_n_f64(1.7);

    const float64x2_t mul = vdupq_n_f64(0.9999999999);
    const float64x2_t add = vdupq_n_f64(0.0000000001);

    uint64_t ops = 0;
    auto start = std::chrono::high_resolution_clock::now();

    for (;;) {
        for (int i = 0; i < 1000; ++i) {
            // vfmaq_f64(addend, a, b) = addend + a*b
            acc0 = vfmaq_f64(add, acc0, mul);
            acc1 = vfmaq_f64(add, acc1, mul);
            acc2 = vfmaq_f64(add, acc2, mul);
            acc3 = vfmaq_f64(add, acc3, mul);
            acc4 = vfmaq_f64(add, acc4, mul);
            acc5 = vfmaq_f64(add, acc5, mul);
            acc6 = vfmaq_f64(add, acc6, mul);
            acc7 = vfmaq_f64(add, acc7, mul);
        }
        // 8 accumulators * 1000 iterations * 2 doubles * 2 ops (FMA = mul+add)
        ops += 8ULL * 1000 * 2 * 2;

        auto now = std::chrono::high_resolution_clock::now();
        uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
        if (elapsed >= duration_ns) break;
    }

    DO_NOT_OPTIMIZE(acc0);
    DO_NOT_OPTIMIZE(acc1);
    DO_NOT_OPTIMIZE(acc2);
    DO_NOT_OPTIMIZE(acc3);
    DO_NOT_OPTIMIZE(acc4);
    DO_NOT_OPTIMIZE(acc5);
    DO_NOT_OPTIMIZE(acc6);
    DO_NOT_OPTIMIZE(acc7);

    return ops;
}

uint64_t stress_sse(uint64_t duration_ns) {
    return stress_neon_fp64(duration_ns);
}

uint64_t stress_avx2(uint64_t duration_ns) {
    return stress_neon_fp64(duration_ns);
}

uint64_t stress_avx512(uint64_t duration_ns) {
    return stress_neon_fp64(duration_ns);
}

// NEON verification: deterministic FMA chain with result checking
VerifyResult stress_and_verify_neon(uint64_t duration_ns) {
    VerifyResult result;
    result.lane_count = 2;

    auto start = std::chrono::high_resolution_clock::now();

    for (;;) {
        // 다중 시드 순차 테스트
        for (int s = 0; s < VERIFY_SEED_COUNT; ++s) {
            const auto& seeds = VERIFY_SEEDS[s];
            const double expected = compute_scalar_expected(seeds, VERIFY_ITERATIONS);

            const float64x2_t mul = vdupq_n_f64(seeds.mul);
            const float64x2_t add = vdupq_n_f64(seeds.add);
            float64x2_t acc = vdupq_n_f64(seeds.seed);

            for (int i = 0; i < VERIFY_ITERATIONS; ++i) {
                acc = vfmaq_f64(add, acc, mul);
            }
            result.ops += 2ULL * VERIFY_ITERATIONS * 2;

            // Extract and verify both lanes
            double vals[2];
            vst1q_f64(vals, acc);

            for (int lane = 0; lane < 2; ++lane) {
                uint64_t exp_bits, act_bits;
                std::memcpy(&exp_bits, &expected, sizeof(double));
                std::memcpy(&act_bits, &vals[lane], sizeof(double));
                if (exp_bits != act_bits) {
                    result.passed = false;
                    result.lane_errors++;
                    result.expected[lane] = expected;
                    result.actual[lane] = vals[lane];
                }
            }

            DO_NOT_OPTIMIZE(acc);

            if (!result.passed) break;
        }

        if (!result.passed) break;

        auto now = std::chrono::high_resolution_clock::now();
        uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
        if (elapsed >= duration_ns) break;
    }

    return result;
}

// ARM fallbacks for x86-named verify functions
VerifyResult stress_and_verify_sse(uint64_t duration_ns) {
    return stress_and_verify_neon(duration_ns);
}

VerifyResult stress_and_verify_avx2(uint64_t duration_ns) {
    return stress_and_verify_neon(duration_ns);
}

VerifyResult stress_and_verify_avx512(uint64_t duration_ns) {
    return stress_and_verify_neon(duration_ns);
}

uint64_t stress_avx_nofma(uint64_t duration_ns) {
    return stress_neon_fp64(duration_ns);
}

VerifyResult stress_and_verify_avx_nofma(uint64_t duration_ns) {
    return stress_and_verify_neon(duration_ns);
}

}} // namespace occt::cpu

// ============================================================
// Other architectures: Scalar double fallback
// ============================================================
#else

#include <cmath>

namespace occt { namespace cpu {

bool has_sse42()   { return false; }
bool has_avx2()    { return false; }
bool has_avx512f() { return false; }
bool has_fma()     { return false; }

static uint64_t stress_scalar(uint64_t duration_ns) {
    double acc0 = 1.0, acc1 = 1.1, acc2 = 1.2, acc3 = 1.3;
    double acc4 = 1.4, acc5 = 1.5, acc6 = 1.6, acc7 = 1.7;
    const double mul = 0.9999999999;
    const double add = 0.0000000001;

    uint64_t ops = 0;
    auto start = std::chrono::high_resolution_clock::now();

    for (;;) {
        for (int i = 0; i < 1000; ++i) {
            acc0 = std::fma(acc0, mul, add);
            acc1 = std::fma(acc1, mul, add);
            acc2 = std::fma(acc2, mul, add);
            acc3 = std::fma(acc3, mul, add);
            acc4 = std::fma(acc4, mul, add);
            acc5 = std::fma(acc5, mul, add);
            acc6 = std::fma(acc6, mul, add);
            acc7 = std::fma(acc7, mul, add);
        }
        ops += 8ULL * 1000 * 2;

        auto now = std::chrono::high_resolution_clock::now();
        uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
        if (elapsed >= duration_ns) break;
    }

    DO_NOT_OPTIMIZE(acc0);
    DO_NOT_OPTIMIZE(acc1);
    DO_NOT_OPTIMIZE(acc2);
    DO_NOT_OPTIMIZE(acc3);
    DO_NOT_OPTIMIZE(acc4);
    DO_NOT_OPTIMIZE(acc5);
    DO_NOT_OPTIMIZE(acc6);
    DO_NOT_OPTIMIZE(acc7);

    return ops;
}

uint64_t stress_sse(uint64_t duration_ns) {
    return stress_scalar(duration_ns);
}

uint64_t stress_avx2(uint64_t duration_ns) {
    return stress_scalar(duration_ns);
}

uint64_t stress_avx512(uint64_t duration_ns) {
    return stress_scalar(duration_ns);
}

// Scalar verify: single-lane deterministic FMA check
static VerifyResult stress_and_verify_scalar(uint64_t duration_ns) {
    VerifyResult result;
    result.lane_count = 1;

    auto start = std::chrono::high_resolution_clock::now();

    for (;;) {
        // 다중 시드 순차 테스트
        for (int s = 0; s < VERIFY_SEED_COUNT; ++s) {
            const auto& seeds = VERIFY_SEEDS[s];
            const double expected = compute_scalar_expected(seeds, VERIFY_ITERATIONS);

            double acc = seeds.seed;
            for (int i = 0; i < VERIFY_ITERATIONS; ++i) {
                acc = std::fma(acc, seeds.mul, seeds.add);
            }
            result.ops += VERIFY_ITERATIONS * 2;

            uint64_t exp_bits, act_bits;
            std::memcpy(&exp_bits, &expected, sizeof(double));
            std::memcpy(&act_bits, &acc, sizeof(double));
            if (exp_bits != act_bits) {
                result.passed = false;
                result.lane_errors++;
                result.expected[0] = expected;
                result.actual[0] = acc;
            }

            DO_NOT_OPTIMIZE(acc);

            if (!result.passed) break;
        }

        if (!result.passed) break;

        auto now = std::chrono::high_resolution_clock::now();
        uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
        if (elapsed >= duration_ns) break;
    }

    return result;
}

VerifyResult stress_and_verify_sse(uint64_t duration_ns) {
    return stress_and_verify_scalar(duration_ns);
}

VerifyResult stress_and_verify_avx2(uint64_t duration_ns) {
    return stress_and_verify_scalar(duration_ns);
}

VerifyResult stress_and_verify_avx512(uint64_t duration_ns) {
    return stress_and_verify_scalar(duration_ns);
}

VerifyResult stress_and_verify_neon(uint64_t duration_ns) {
    return stress_and_verify_scalar(duration_ns);
}

uint64_t stress_avx_nofma(uint64_t duration_ns) {
    return stress_scalar(duration_ns);
}

VerifyResult stress_and_verify_avx_nofma(uint64_t duration_ns) {
    return stress_and_verify_scalar(duration_ns);
}

}} // namespace occt::cpu

#endif // architecture dispatch
