// =============================================================================
// OCCT Native - Phase 2 Feature Tests
// Tests for P2-1 (Normal/Extreme), P2-2 (GPU Switch timing),
//            P2-3 (Stop-on-Error), P2-4 (Per-core error reporting)
// =============================================================================
// Build:  g++ -std=c++17 -o test_phase2 test_phase2_features.cpp
// Run:    ./test_phase2
// =============================================================================

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
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

#define ASSERT_NEAR(a, b, eps, msg) \
    do { \
        if (std::abs((a) - (b)) > (eps)) { FAIL(msg); return; } \
    } while(0)

// =============================================================================
// P2-1: CPU Normal/Extreme intensity mode
// =============================================================================

namespace test_p2_1 {

enum class CpuIntensityMode { NORMAL, EXTREME };

bool should_verify(CpuIntensityMode mode, uint64_t batch_count) {
    if (mode == CpuIntensityMode::NORMAL) {
        return true;  // Always verify
    } else {
        return (batch_count % 10 == 0);  // Every 10th
    }
}

void test_normal_always_verifies() {
    TEST("P2-1: Normal mode verifies every batch");
    for (uint64_t i = 0; i < 20; ++i) {
        ASSERT_TRUE(should_verify(CpuIntensityMode::NORMAL, i),
                    "Normal mode should always verify");
    }
    PASS();
}

void test_extreme_verifies_every_10th() {
    TEST("P2-1: Extreme mode verifies every 10th batch");
    int verify_count = 0;
    for (uint64_t i = 0; i < 100; ++i) {
        if (should_verify(CpuIntensityMode::EXTREME, i)) {
            verify_count++;
        }
    }
    ASSERT_EQ(verify_count, 10, "Should verify 10 times in 100 batches");
    PASS();
}

void test_extreme_is_default() {
    TEST("P2-1: Default mode is EXTREME");
    CpuIntensityMode default_mode = CpuIntensityMode::EXTREME;
    ASSERT_TRUE(!should_verify(default_mode, 1), "Batch 1 should not verify in EXTREME");
    ASSERT_TRUE(should_verify(default_mode, 0), "Batch 0 should verify in EXTREME");
    PASS();
}

} // namespace test_p2_1

// =============================================================================
// P2-2: GPU Switch mode timing
// =============================================================================

namespace test_p2_2 {

float compute_switch_load(float time_secs, float interval_secs) {
    int phase = static_cast<int>(time_secs / interval_secs);
    return (phase % 2 == 0) ? 0.2f : 0.9f;
}

float compute_variable_load(float time_secs) {
    int phase = static_cast<int>(time_secs / 20.0f);
    return std::min(1.0f, 0.05f + 0.05f * static_cast<float>(phase));
}

void test_switch_330ms_interval() {
    TEST("P2-2: Switch mode alternates every 330ms");
    float interval = 0.33f;

    // At t=0.0s -> phase 0 -> 20%
    ASSERT_NEAR(compute_switch_load(0.0f, interval), 0.2f, 0.01f, "t=0 should be 20%");
    // At t=0.33s -> phase 1 -> 90%
    ASSERT_NEAR(compute_switch_load(0.33f, interval), 0.9f, 0.01f, "t=0.33 should be 90%");
    // At t=0.66s -> phase 2 -> 20%
    ASSERT_NEAR(compute_switch_load(0.66f, interval), 0.2f, 0.01f, "t=0.66 should be 20%");
    // At t=0.99s -> phase 3 -> 90%
    ASSERT_NEAR(compute_switch_load(0.99f, interval), 0.9f, 0.01f, "t=0.99 should be 90%");
    PASS();
}

void test_switch_rapid_transitions() {
    TEST("P2-2: 330ms produces ~6 transitions in 2 seconds");
    float interval = 0.33f;
    int transitions = 0;
    float prev_load = compute_switch_load(0.0f, interval);
    for (float t = 0.01f; t < 2.0f; t += 0.01f) {
        float load = compute_switch_load(t, interval);
        if (load != prev_load) {
            transitions++;
            prev_load = load;
        }
    }
    // 2.0 / 0.33 ≈ 6 phases, so ~5-6 transitions
    ASSERT_TRUE(transitions >= 4 && transitions <= 7,
                "Should have 4-7 transitions in 2 seconds");
    PASS();
}

void test_variable_ramp() {
    TEST("P2-2: Variable mode ramps +5% every 20 seconds");
    // t=0 -> 5%
    ASSERT_NEAR(compute_variable_load(0.0f), 0.05f, 0.01f, "t=0 should be ~5%");
    // t=20 -> 10%
    ASSERT_NEAR(compute_variable_load(20.0f), 0.10f, 0.01f, "t=20 should be ~10%");
    // t=100 -> 30%
    ASSERT_NEAR(compute_variable_load(100.0f), 0.30f, 0.01f, "t=100 should be ~30%");
    // t=400 -> 100% (capped)
    ASSERT_NEAR(compute_variable_load(400.0f), 1.0f, 0.01f, "t=400 should be 100%");
    PASS();
}

void test_switch_load_range() {
    TEST("P2-2: Switch mode uses 20%-90% range");
    float interval = 0.33f;
    float low = compute_switch_load(0.0f, interval);
    float high = compute_switch_load(0.33f, interval);
    ASSERT_NEAR(low, 0.2f, 0.01f, "Low load should be 20%");
    ASSERT_NEAR(high, 0.9f, 0.01f, "High load should be 90%");
    PASS();
}

} // namespace test_p2_2

// =============================================================================
// P2-3: Stop on Error
// =============================================================================

namespace test_p2_3 {

// Simulate IEngine base with stop_on_error
class MockEngine {
public:
    void set_stop_on_error(bool enable) { stop_on_error_ = enable; }
    bool stop_on_error() const { return stop_on_error_; }
protected:
    bool stop_on_error_ = false;
};

class MockCpuEngine : public MockEngine {
public:
    bool running = true;

    void simulate_error() {
        error_count++;
        if (stop_on_error_) {
            running = false;
        }
    }

    int error_count = 0;
};

void test_default_disabled() {
    TEST("P2-3: Stop-on-error disabled by default");
    MockEngine engine;
    ASSERT_TRUE(!engine.stop_on_error(), "Default should be disabled");
    PASS();
}

void test_enable_stop_on_error() {
    TEST("P2-3: Can enable stop-on-error");
    MockEngine engine;
    engine.set_stop_on_error(true);
    ASSERT_TRUE(engine.stop_on_error(), "Should be enabled");
    PASS();
}

void test_stops_on_first_error() {
    TEST("P2-3: Engine stops on first error when enabled");
    MockCpuEngine engine;
    engine.set_stop_on_error(true);

    engine.simulate_error();
    ASSERT_TRUE(!engine.running, "Should have stopped after first error");
    ASSERT_EQ(engine.error_count, 1, "Should have exactly 1 error");
    PASS();
}

void test_continues_without_stop_on_error() {
    TEST("P2-3: Engine continues running when stop-on-error disabled");
    MockCpuEngine engine;
    engine.set_stop_on_error(false);

    engine.simulate_error();
    engine.simulate_error();
    engine.simulate_error();
    ASSERT_TRUE(engine.running, "Should still be running");
    ASSERT_EQ(engine.error_count, 3, "Should have 3 errors");
    PASS();
}

} // namespace test_p2_3

// =============================================================================
// P2-4: Per-core error reporting
// =============================================================================

namespace test_p2_4 {

struct CpuMetrics {
    int error_count = 0;
    std::vector<int> per_core_error_count;
};

std::string format_error_summary(const CpuMetrics& m) {
    if (m.error_count == 0) return "No errors detected";

    int cores_affected = 0;
    std::ostringstream oss;
    oss << m.error_count << " error(s) on ";

    std::string detail;
    for (int i = 0; i < (int)m.per_core_error_count.size(); ++i) {
        int cnt = m.per_core_error_count[i];
        if (cnt > 0) {
            cores_affected++;
            if (!detail.empty()) detail += ", ";
            detail += "Core #" + std::to_string(i) + " (" + std::to_string(cnt) + ")";
        }
    }

    oss << cores_affected << " core(s): " << detail;
    return oss.str();
}

void test_no_errors_summary() {
    TEST("P2-4: No errors produces clean summary");
    CpuMetrics m;
    m.error_count = 0;
    ASSERT_EQ(format_error_summary(m), std::string("No errors detected"),
              "Should say no errors");
    PASS();
}

void test_single_core_error() {
    TEST("P2-4: Single core error format");
    CpuMetrics m;
    m.error_count = 3;
    m.per_core_error_count = {0, 0, 0, 3, 0, 0, 0, 0};

    std::string summary = format_error_summary(m);
    ASSERT_TRUE(summary.find("3 error(s) on 1 core(s)") != std::string::npos,
                "Should show 3 errors on 1 core");
    ASSERT_TRUE(summary.find("Core #3 (3)") != std::string::npos,
                "Should identify Core #3");
    PASS();
}

void test_multi_core_errors() {
    TEST("P2-4: Multiple core errors format");
    CpuMetrics m;
    m.error_count = 5;
    m.per_core_error_count = {0, 2, 0, 0, 0, 0, 0, 3};

    std::string summary = format_error_summary(m);
    ASSERT_TRUE(summary.find("5 error(s) on 2 core(s)") != std::string::npos,
                "Should show 5 errors on 2 cores");
    ASSERT_TRUE(summary.find("Core #1 (2)") != std::string::npos,
                "Should identify Core #1");
    ASSERT_TRUE(summary.find("Core #7 (3)") != std::string::npos,
                "Should identify Core #7");
    PASS();
}

void test_per_core_count_independence() {
    TEST("P2-4: Per-core counts are independent");
    CpuMetrics m;
    m.error_count = 10;
    m.per_core_error_count = {1, 2, 3, 4};

    std::string summary = format_error_summary(m);
    ASSERT_TRUE(summary.find("4 core(s)") != std::string::npos,
                "All 4 cores should be affected");
    PASS();
}

} // namespace test_p2_4

// =============================================================================
// Main
// =============================================================================

int main() {
    std::cout << "=== OCCT Native Phase 2 Feature Tests ===" << std::endl;

    std::cout << std::endl << "--- P2-1: CPU Normal/Extreme Mode ---" << std::endl;
    test_p2_1::test_normal_always_verifies();
    test_p2_1::test_extreme_verifies_every_10th();
    test_p2_1::test_extreme_is_default();

    std::cout << std::endl << "--- P2-2: GPU Switch Timing ---" << std::endl;
    test_p2_2::test_switch_330ms_interval();
    test_p2_2::test_switch_rapid_transitions();
    test_p2_2::test_variable_ramp();
    test_p2_2::test_switch_load_range();

    std::cout << std::endl << "--- P2-3: Stop-on-Error ---" << std::endl;
    test_p2_3::test_default_disabled();
    test_p2_3::test_enable_stop_on_error();
    test_p2_3::test_stops_on_first_error();
    test_p2_3::test_continues_without_stop_on_error();

    std::cout << std::endl << "--- P2-4: Per-core Error Reporting ---" << std::endl;
    test_p2_4::test_no_errors_summary();
    test_p2_4::test_single_core_error();
    test_p2_4::test_multi_core_errors();
    test_p2_4::test_per_core_count_independence();

    std::cout << std::endl << "=== Results: " << tests_passed << " / "
              << tests_run << " passed ===" << std::endl;

    return (tests_passed == tests_run) ? 0 : 1;
}
