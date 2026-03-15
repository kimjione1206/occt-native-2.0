// =============================================================================
// OCCT Native - Phase 1 Bug Fix Tests
// Tests for B1 (artifact_detector), B2 (psu_engine), B3 (ram_engine)
// =============================================================================
// Build:  g++ -std=c++17 -I../src -o test_phase1 test_phase1_bugfixes.cpp
//         (Note: these are self-contained unit tests, not integration tests)
// Run:    ./test_phase1
// =============================================================================

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

// ─── Test Helpers ────────────────────────────────────────────────────────────

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        std::cout << "  TEST: " << name << " ... "; \
    } while(0)

#define PASS() \
    do { \
        tests_passed++; \
        std::cout << "PASS" << std::endl; \
    } while(0)

#define FAIL(msg) \
    do { \
        std::cout << "FAIL: " << msg << std::endl; \
    } while(0)

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { FAIL(msg); return; } \
    } while(0)

#define ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { FAIL(msg); return; } \
    } while(0)

// =============================================================================
// B1: ArtifactDetector self-comparison fix
// =============================================================================
// We replicate the core comparison logic to verify it works correctly
// without needing the full GPU engine dependencies.

namespace test_b1 {

struct ArtifactResult {
    uint64_t error_pixels = 0;
    double error_rate = 0.0;
    float max_error = 0.0f;
};

// Simulates the fixed artifact detection pixel comparison
ArtifactResult compare_pixels(const uint8_t* reference, const uint8_t* actual,
                               uint32_t width, uint32_t height, int tolerance) {
    ArtifactResult result;
    size_t pixel_count = static_cast<size_t>(width) * height;

    for (size_t i = 0; i < pixel_count; ++i) {
        size_t offset = i * 4;
        bool has_error = false;

        for (int c = 0; c < 4; ++c) {
            // FIXED: compare actual[...] vs reference[...] (not reference vs reference)
            int diff = static_cast<int>(actual[offset + c]) -
                       static_cast<int>(reference[offset + c]);
            if (std::abs(diff) > tolerance) {
                has_error = true;
            }
            float err = std::abs(
                static_cast<float>(actual[offset + c]) -
                static_cast<float>(reference[offset + c]));
            result.max_error = std::max(result.max_error, err);
        }

        if (has_error) {
            result.error_pixels++;
        }
    }

    result.error_rate = static_cast<double>(result.error_pixels) /
                        static_cast<double>(pixel_count);
    return result;
}

// Old buggy version for comparison
ArtifactResult compare_pixels_buggy(const uint8_t* reference, const uint8_t* /*actual*/,
                                     uint32_t width, uint32_t height, int /*tolerance*/) {
    ArtifactResult result;
    size_t pixel_count = static_cast<size_t>(width) * height;

    for (size_t i = 0; i < pixel_count; ++i) {
        size_t offset = i * 4;
        for (int c = 0; c < 4; ++c) {
            // BUG: comparing reference with itself -> always 0
            float err = std::abs(
                static_cast<float>(reference[offset + c]) -
                static_cast<float>(reference[offset + c]));
            result.max_error = std::max(result.max_error, err);
        }
    }
    return result;
}

void test_fixed_detects_differences() {
    TEST("B1: Fixed detector finds pixel differences");

    const uint32_t w = 4, h = 4;
    std::vector<uint8_t> ref(w * h * 4, 0);     // All black
    std::vector<uint8_t> actual(w * h * 4, 0);   // All black

    // Introduce a difference at pixel (1,1)
    actual[( 1 * w + 1) * 4 + 0] = 255; // R channel = 255

    auto result = compare_pixels(ref.data(), actual.data(), w, h, 1);

    ASSERT_TRUE(result.error_pixels > 0, "Should detect at least 1 error pixel");
    ASSERT_TRUE(result.max_error >= 254.0f, "Max error should be ~255");
    ASSERT_TRUE(result.error_rate > 0.0, "Error rate should be > 0");
    PASS();
}

void test_buggy_misses_differences() {
    TEST("B1: Old buggy detector misses pixel differences");

    const uint32_t w = 4, h = 4;
    std::vector<uint8_t> ref(w * h * 4, 0);
    std::vector<uint8_t> actual(w * h * 4, 255); // Completely different!

    auto result = compare_pixels_buggy(ref.data(), actual.data(), w, h, 1);

    // Buggy version compares reference with itself -> always 0 error
    ASSERT_TRUE(result.max_error == 0.0f, "Buggy version should always report 0 error");
    PASS();
}

void test_identical_frames_no_errors() {
    TEST("B1: Identical frames produce zero errors");

    const uint32_t w = 8, h = 8;
    std::vector<uint8_t> ref(w * h * 4, 128);
    std::vector<uint8_t> actual(w * h * 4, 128);

    auto result = compare_pixels(ref.data(), actual.data(), w, h, 1);

    ASSERT_EQ(result.error_pixels, (uint64_t)0, "No errors for identical frames");
    ASSERT_TRUE(result.max_error == 0.0f, "Max error should be 0");
    PASS();
}

void test_tolerance_threshold() {
    TEST("B1: Tolerance threshold filters small differences");

    const uint32_t w = 2, h = 2;
    std::vector<uint8_t> ref(w * h * 4, 100);
    std::vector<uint8_t> actual(w * h * 4, 100);

    // Pixel (0,0) has diff of 3 in R channel
    actual[0] = 103;

    // With tolerance=5, should NOT be an error
    auto result_high_tol = compare_pixels(ref.data(), actual.data(), w, h, 5);
    ASSERT_EQ(result_high_tol.error_pixels, (uint64_t)0, "Diff 3 within tolerance 5");

    // With tolerance=1, SHOULD be an error
    auto result_low_tol = compare_pixels(ref.data(), actual.data(), w, h, 1);
    ASSERT_TRUE(result_low_tol.error_pixels > 0, "Diff 3 exceeds tolerance 1");

    PASS();
}

} // namespace test_b1

// =============================================================================
// B2: PSU Engine error counter fix
// =============================================================================
// We verify the error counting logic independently of the full engine.

namespace test_b2 {

// Simulate the relevant metric structs
struct CpuMetrics {
    int error_count = 0;
};

struct GpuMetrics {
    uint64_t vram_errors = 0;
};

struct PsuMetrics {
    int errors_cpu = 0;
    int errors_gpu = 0;
};

// Simulate the fixed metrics polling logic
PsuMetrics update_metrics(const CpuMetrics& cpu_m, const GpuMetrics& gpu_m) {
    PsuMetrics current{};
    // FIXED: actually update error counts (was missing before)
    current.errors_cpu = cpu_m.error_count;
    current.errors_gpu = static_cast<int>(gpu_m.vram_errors);
    return current;
}

void test_error_counters_propagate() {
    TEST("B2: PSU metrics propagate CPU/GPU error counts");

    CpuMetrics cpu_m;
    cpu_m.error_count = 42;

    GpuMetrics gpu_m;
    gpu_m.vram_errors = 7;

    auto psu_m = update_metrics(cpu_m, gpu_m);

    ASSERT_EQ(psu_m.errors_cpu, 42, "CPU errors should be 42");
    ASSERT_EQ(psu_m.errors_gpu, 7, "GPU errors should be 7");
    PASS();
}

void test_zero_errors_initial() {
    TEST("B2: PSU metrics start at zero errors");

    PsuMetrics m{};
    ASSERT_EQ(m.errors_cpu, 0, "Initial CPU errors should be 0");
    ASSERT_EQ(m.errors_gpu, 0, "Initial GPU errors should be 0");
    PASS();
}

void test_large_vram_errors_cast() {
    TEST("B2: Large VRAM error count cast to int safely");

    CpuMetrics cpu_m;
    cpu_m.error_count = 0;

    GpuMetrics gpu_m;
    gpu_m.vram_errors = 100000;

    auto psu_m = update_metrics(cpu_m, gpu_m);

    ASSERT_EQ(psu_m.errors_gpu, 100000, "Large VRAM error count preserved");
    PASS();
}

} // namespace test_b2

// =============================================================================
// B3: RAM Engine report_error fix
// =============================================================================
// We verify error detail storage independently.

namespace test_b3 {

struct MemoryError {
    uint64_t address;
    uint64_t expected;
    uint64_t actual;
    double timestamp_secs;
};

struct RamMetrics {
    uint64_t errors_found = 0;
    std::vector<MemoryError> error_log;
};

static constexpr size_t MAX_ERROR_LOG = 1000;

// Simulate the fixed report_error logic
void report_error(RamMetrics& metrics, uint64_t address,
                   uint64_t expected, uint64_t actual, double timestamp) {
    metrics.errors_found++;

    if (metrics.error_log.size() < MAX_ERROR_LOG) {
        metrics.error_log.push_back({address, expected, actual, timestamp});
    }
}

void test_error_details_stored() {
    TEST("B3: report_error stores address/expected/actual");

    RamMetrics metrics{};
    report_error(metrics, 0x1000, 0xDEAD, 0xBEEF, 1.5);

    ASSERT_EQ(metrics.errors_found, (uint64_t)1, "Error count should be 1");
    ASSERT_EQ(metrics.error_log.size(), (size_t)1, "Error log should have 1 entry");
    ASSERT_EQ(metrics.error_log[0].address, (uint64_t)0x1000, "Address mismatch");
    ASSERT_EQ(metrics.error_log[0].expected, (uint64_t)0xDEAD, "Expected mismatch");
    ASSERT_EQ(metrics.error_log[0].actual, (uint64_t)0xBEEF, "Actual mismatch");
    ASSERT_TRUE(std::abs(metrics.error_log[0].timestamp_secs - 1.5) < 0.01,
                "Timestamp mismatch");
    PASS();
}

void test_error_log_cap() {
    TEST("B3: Error log capped at 1000 entries");

    RamMetrics metrics{};

    for (int i = 0; i < 1500; ++i) {
        report_error(metrics, static_cast<uint64_t>(i) * 8, 0, 1, 0.0);
    }

    ASSERT_EQ(metrics.errors_found, (uint64_t)1500, "Total count should be 1500");
    ASSERT_EQ(metrics.error_log.size(), (size_t)1000, "Log should be capped at 1000");
    PASS();
}

void test_multiple_errors_preserved() {
    TEST("B3: Multiple error details preserved correctly");

    RamMetrics metrics{};
    report_error(metrics, 0x0008, 0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL, 0.1);
    report_error(metrics, 0x0010, 0xFFFFFFFFFFFFFFFFULL, 0x0000000000000000ULL, 0.2);
    report_error(metrics, 0x0018, 0x1234567890ABCDEFULL, 0xFEDCBA0987654321ULL, 0.3);

    ASSERT_EQ(metrics.errors_found, (uint64_t)3, "Should have 3 errors");
    ASSERT_EQ(metrics.error_log.size(), (size_t)3, "Log should have 3 entries");

    ASSERT_EQ(metrics.error_log[1].address, (uint64_t)0x0010, "2nd error address");
    ASSERT_EQ(metrics.error_log[2].expected, (uint64_t)0x1234567890ABCDEFULL, "3rd expected");
    PASS();
}

void test_empty_metrics_initial() {
    TEST("B3: Fresh RamMetrics has empty error log");

    RamMetrics metrics{};
    ASSERT_EQ(metrics.errors_found, (uint64_t)0, "Initial errors should be 0");
    ASSERT_TRUE(metrics.error_log.empty(), "Initial error log should be empty");
    PASS();
}

} // namespace test_b3

// =============================================================================
// Main
// =============================================================================

int main() {
    std::cout << "=== OCCT Native Phase 1 Bug Fix Tests ===" << std::endl;

    std::cout << std::endl << "--- B1: ArtifactDetector ---" << std::endl;
    test_b1::test_fixed_detects_differences();
    test_b1::test_buggy_misses_differences();
    test_b1::test_identical_frames_no_errors();
    test_b1::test_tolerance_threshold();

    std::cout << std::endl << "--- B2: PSU Error Counter ---" << std::endl;
    test_b2::test_error_counters_propagate();
    test_b2::test_zero_errors_initial();
    test_b2::test_large_vram_errors_cast();

    std::cout << std::endl << "--- B3: RAM Error Reporting ---" << std::endl;
    test_b3::test_error_details_stored();
    test_b3::test_error_log_cap();
    test_b3::test_multiple_errors_preserved();
    test_b3::test_empty_metrics_initial();

    std::cout << std::endl << "=== Results: " << tests_passed << " / "
              << tests_run << " passed ===" << std::endl;

    return (tests_passed == tests_run) ? 0 : 1;
}
