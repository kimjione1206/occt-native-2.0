#pragma once

#include <cstdint>

namespace occt { namespace cpu {

// Runtime ISA detection
bool has_sse42();
bool has_avx2();
bool has_avx512f();
bool has_fma();

// Stress functions - run FMA loop for specified duration (nanoseconds)
// Returns: number of FMA operations executed
uint64_t stress_sse(uint64_t duration_ns);
uint64_t stress_avx2(uint64_t duration_ns);
uint64_t stress_avx512(uint64_t duration_ns);

// 다중 시드로 피연산자 의존적 에러 검출
struct VerifySeedSet {
    double seed;
    double mul;
    double add;
};

static constexpr VerifySeedSet VERIFY_SEEDS[] = {
    {1.0, 0.9999999999, 0.0000000001},           // 기존 시드
    {0.5, 1.0000000001, -0.0000000001},           // 음수 가산
    {3.141592653589793, 0.9999999997, 0.0000000003}, // 파이 기반
    {2.718281828459045, 1.0000000003, -0.0000000003}, // 자연상수 기반
};
static constexpr int VERIFY_SEED_COUNT = 4;

// Verification result from stress_and_verify functions
struct VerifyResult {
    uint64_t ops = 0;          // Number of operations executed
    bool passed = true;        // All results matched expected
    int lane_errors = 0;       // Number of SIMD lanes with errors
    double expected[8] = {};   // Expected values per lane (up to 8 for AVX-512)
    double actual[8] = {};     // Actual values per lane
    int lane_count = 0;        // How many lanes were checked
};

// Pure AVX 256-bit stress without FMA (uses mul+add separately)
uint64_t stress_avx_nofma(uint64_t duration_ns);
VerifyResult stress_and_verify_avx_nofma(uint64_t duration_ns);

// Stress + verify functions - run deterministic FMA chain and verify results
// These run a fixed number of iterations, verify, then repeat until duration_ns
VerifyResult stress_and_verify_sse(uint64_t duration_ns);
VerifyResult stress_and_verify_avx2(uint64_t duration_ns);
VerifyResult stress_and_verify_avx512(uint64_t duration_ns);
VerifyResult stress_and_verify_neon(uint64_t duration_ns);

}} // namespace occt::cpu
