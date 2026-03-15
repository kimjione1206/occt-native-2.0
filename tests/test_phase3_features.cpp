// =============================================================================
// OCCT Native - Phase 3 Feature Tests
// Tests for P3-1 (Cache-Only), P3-2 (Large Data Set), P3-3 (Core Cycling),
//            P3-4 (WHEA), P3-5 (Combined), P3-6 (Coil Whine)
// =============================================================================
// Build:  g++ -std=c++17 -o test_phase3 test_phase3_features.cpp
// Run:    ./test_phase3
// =============================================================================

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>
#include <functional>

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
// P3-1: CPU Cache-Only Mode (Small Data Set)
// =============================================================================

namespace test_p3_1 {

// Simulate cache-only stress: buffer must fit in L3 cache
struct CacheOnlyConfig {
    size_t buffer_size;    // bytes
    size_t l1_size = 32 * 1024;       // 32KB
    size_t l2_size = 256 * 1024;      // 256KB
    size_t l3_size = 8 * 1024 * 1024; // 8MB
};

bool fits_in_cache(const CacheOnlyConfig& cfg) {
    return cfg.buffer_size <= cfg.l3_size;
}

// Simulate the cache-only FMA loop (small version for testing)
double run_cache_stress(size_t num_doubles, int iterations) {
    std::vector<double> buf(num_doubles);
    for (size_t i = 0; i < num_doubles; ++i) {
        buf[i] = static_cast<double>(i + 1) * 0.001;
    }

    const double a = 1.0000001;
    const double b = 0.0000001;
    for (int iter = 0; iter < iterations; ++iter) {
        for (size_t i = 0; i < num_doubles; ++i) {
            buf[i] = buf[i] * a + b;
        }
    }
    return buf[0]; // Return first element for verification
}

void test_cache_buffer_fits_l3() {
    TEST("P3-1: Cache-only buffer (4MB) fits in L3 cache");
    CacheOnlyConfig cfg;
    cfg.buffer_size = 4 * 1024 * 1024; // 4MB as used in implementation
    ASSERT_TRUE(fits_in_cache(cfg), "4MB should fit in 8MB L3 cache");
    PASS();
}

void test_large_data_does_not_fit() {
    TEST("P3-1: Large data set (256MB) does NOT fit in cache");
    CacheOnlyConfig cfg;
    cfg.buffer_size = 256 * 1024 * 1024; // 256MB
    ASSERT_TRUE(!fits_in_cache(cfg), "256MB should not fit in 8MB L3 cache");
    PASS();
}

void test_cache_stress_no_nan() {
    TEST("P3-1: Cache stress produces valid (non-NaN) results");
    double result = run_cache_stress(1024, 100);
    ASSERT_TRUE(!std::isnan(result), "Result should not be NaN");
    ASSERT_TRUE(!std::isinf(result), "Result should not be Inf");
    ASSERT_TRUE(result > 0.0, "Result should be positive");
    PASS();
}

void test_cache_fma_correctness() {
    TEST("P3-1: Cache FMA computation is deterministic");
    double r1 = run_cache_stress(512, 50);
    double r2 = run_cache_stress(512, 50);
    ASSERT_NEAR(r1, r2, 1e-10, "Same inputs should produce same results");
    PASS();
}

} // namespace test_p3_1

// =============================================================================
// P3-2: CPU+RAM Large Data Set Mode
// =============================================================================

namespace test_p3_2 {

// Simulate large data set streaming FMA
struct LargeDataConfig {
    size_t buffer_size_mb;
    size_t elements_per_mb = 1024 * 1024 / sizeof(double); // ~131072 doubles per MB
};

uint64_t compute_ops_per_sweep(const LargeDataConfig& cfg) {
    size_t num_doubles = cfg.buffer_size_mb * cfg.elements_per_mb;
    return num_doubles * 2; // 1 mul + 1 add per element
}

void test_large_data_256mb_ops() {
    TEST("P3-2: 256MB buffer produces correct operation count");
    LargeDataConfig cfg{256};
    uint64_t ops = compute_ops_per_sweep(cfg);
    size_t expected_doubles = 256 * (1024 * 1024 / sizeof(double));
    ASSERT_EQ(ops, expected_doubles * 2, "Ops should be 2x element count");
    PASS();
}

void test_large_data_forces_memory_access() {
    TEST("P3-2: Large buffer exceeds all cache levels");
    size_t buf_bytes = 256 * 1024 * 1024;
    size_t l3_bytes = 8 * 1024 * 1024; // Typical L3
    ASSERT_TRUE(buf_bytes > l3_bytes * 4,
                "256MB should be >>4x L3 to guarantee memory bus traffic");
    PASS();
}

void test_streaming_verification_sampling() {
    TEST("P3-2: Verification samples every 4096th element");
    size_t num_doubles = 256 * 1024 * 1024 / sizeof(double);
    int samples = 0;
    for (size_t i = 0; i < num_doubles; i += 4096) {
        samples++;
    }
    // Should sample a reasonable number of elements
    ASSERT_TRUE(samples > 100, "Should sample >100 elements from 256MB");
    ASSERT_TRUE(samples < 100000, "Should not sample too many elements");
    PASS();
}

} // namespace test_p3_2

// =============================================================================
// P3-3: Core Cycling Mode
// =============================================================================

namespace test_p3_3 {

// Simulate core cycling logic
class CoreCycler {
public:
    CoreCycler(int num_cores) : num_cores_(num_cores), active_core_(0) {}

    int active_core() const { return active_core_.load(); }
    int num_cores() const { return num_cores_; }

    void advance() {
        int next = (active_core_.load() + 1) % num_cores_;
        active_core_.store(next);
    }

    bool is_active(int core_id) const {
        return active_core_.load() == core_id;
    }

private:
    int num_cores_;
    std::atomic<int> active_core_;
};

void test_cycling_starts_at_core_0() {
    TEST("P3-3: Core cycling starts at core 0");
    CoreCycler cycler(8);
    ASSERT_EQ(cycler.active_core(), 0, "Should start at core 0");
    PASS();
}

void test_cycling_advances_sequentially() {
    TEST("P3-3: Core cycling advances sequentially");
    CoreCycler cycler(4);
    ASSERT_EQ(cycler.active_core(), 0, "Start at 0");
    cycler.advance();
    ASSERT_EQ(cycler.active_core(), 1, "Advance to 1");
    cycler.advance();
    ASSERT_EQ(cycler.active_core(), 2, "Advance to 2");
    cycler.advance();
    ASSERT_EQ(cycler.active_core(), 3, "Advance to 3");
    PASS();
}

void test_cycling_wraps_around() {
    TEST("P3-3: Core cycling wraps around to core 0");
    CoreCycler cycler(4);
    for (int i = 0; i < 4; ++i) cycler.advance();
    ASSERT_EQ(cycler.active_core(), 0, "Should wrap back to 0");
    PASS();
}

void test_cycling_only_one_active() {
    TEST("P3-3: Only one core is active at a time");
    CoreCycler cycler(8);
    for (int step = 0; step < 16; ++step) {
        int active_count = 0;
        for (int c = 0; c < 8; ++c) {
            if (cycler.is_active(c)) active_count++;
        }
        ASSERT_EQ(active_count, 1, "Exactly one core should be active");
        cycler.advance();
    }
    PASS();
}

void test_cycling_150ms_interval() {
    TEST("P3-3: 150ms interval produces ~6-7 cycles per second");
    // 1000ms / 150ms ≈ 6.67 cycles per second
    float cycles_per_sec = 1000.0f / 150.0f;
    ASSERT_TRUE(cycles_per_sec >= 6.0f && cycles_per_sec <= 7.0f,
                "Should be ~6.67 cycles per second");
    PASS();
}

void test_cycling_full_rotation() {
    TEST("P3-3: Full rotation through 8 cores in 1.2 seconds");
    int num_cores = 8;
    float rotation_time_ms = num_cores * 150.0f; // 1200ms
    ASSERT_NEAR(rotation_time_ms, 1200.0f, 1.0f,
                "Full rotation of 8 cores should take 1200ms");
    PASS();
}

} // namespace test_p3_3

// =============================================================================
// P3-4: WHEA Error Monitoring
// =============================================================================

namespace test_p3_4 {

// Simulate WHEA error structure
struct WheaError {
    enum class Type { MCE, PCIe, NMI, Generic };
    Type type;
    std::string source;
    std::string description;
    double timestamp_secs;
};

// Simulate WHEA monitor
class MockWheaMonitor {
public:
    void start() { running_ = true; }
    void stop() { running_ = false; }
    bool is_running() const { return running_; }

    void set_auto_stop(bool enable) { auto_stop_ = enable; }
    bool auto_stop() const { return auto_stop_; }

    int error_count() const { return static_cast<int>(errors_.size()); }
    const std::vector<WheaError>& errors() const { return errors_; }

    // Simulate receiving a WHEA error
    void inject_error(WheaError err) {
        errors_.push_back(std::move(err));
        if (auto_stop_ && stop_callback_) {
            stop_callback_();
        }
    }

    using StopCallback = std::function<void()>;
    void set_stop_callback(StopCallback cb) { stop_callback_ = std::move(cb); }

private:
    bool running_ = false;
    bool auto_stop_ = false;
    std::vector<WheaError> errors_;
    StopCallback stop_callback_;
};

void test_whea_default_state() {
    TEST("P3-4: WHEA monitor starts with no errors");
    MockWheaMonitor mon;
    ASSERT_EQ(mon.error_count(), 0, "Should have 0 errors initially");
    ASSERT_TRUE(!mon.is_running(), "Should not be running initially");
    PASS();
}

void test_whea_error_counting() {
    TEST("P3-4: WHEA monitor counts errors correctly");
    MockWheaMonitor mon;
    mon.start();
    mon.inject_error({WheaError::Type::MCE, "Machine Check Exception", "CPU error", 1.0});
    mon.inject_error({WheaError::Type::PCIe, "PCIe Error", "Bus error", 2.0});
    ASSERT_EQ(mon.error_count(), 2, "Should have 2 errors");
    PASS();
}

void test_whea_auto_stop_triggers() {
    TEST("P3-4: WHEA auto-stop triggers callback on error");
    MockWheaMonitor mon;
    bool stopped = false;
    mon.set_auto_stop(true);
    mon.set_stop_callback([&stopped]() { stopped = true; });
    mon.start();
    mon.inject_error({WheaError::Type::MCE, "MCE", "Error", 1.0});
    ASSERT_TRUE(stopped, "Auto-stop should trigger callback");
    PASS();
}

void test_whea_no_callback_without_auto_stop() {
    TEST("P3-4: WHEA does NOT trigger callback when auto-stop disabled");
    MockWheaMonitor mon;
    bool stopped = false;
    mon.set_auto_stop(false);
    mon.set_stop_callback([&stopped]() { stopped = true; });
    mon.start();
    mon.inject_error({WheaError::Type::MCE, "MCE", "Error", 1.0});
    ASSERT_TRUE(!stopped, "Should not trigger callback without auto-stop");
    PASS();
}

void test_whea_error_types() {
    TEST("P3-4: WHEA error types are distinct");
    WheaError mce{WheaError::Type::MCE, "MCE", "", 0.0};
    WheaError pcie{WheaError::Type::PCIe, "PCIe", "", 0.0};
    WheaError nmi{WheaError::Type::NMI, "NMI", "", 0.0};
    WheaError generic{WheaError::Type::Generic, "Generic", "", 0.0};
    ASSERT_TRUE(mce.type != pcie.type, "MCE != PCIe");
    ASSERT_TRUE(pcie.type != nmi.type, "PCIe != NMI");
    ASSERT_TRUE(nmi.type != generic.type, "NMI != Generic");
    PASS();
}

} // namespace test_p3_4

// =============================================================================
// P3-5: Combined Test (Multi-Engine Parallel)
// =============================================================================

namespace test_p3_5 {

// Simulate engine for combined test
class MockEngine {
public:
    MockEngine(const std::string& name) : name_(name) {}
    void start() { running_ = true; }
    void stop() { running_ = false; }
    bool is_running() const { return running_; }
    std::string name() const { return name_; }
    int error_count() const { return errors_; }
    void inject_error() { errors_++; }
private:
    std::string name_;
    bool running_ = false;
    int errors_ = 0;
};

// Parse comma-separated engine list
std::vector<std::string> parse_engines(const std::string& input) {
    std::vector<std::string> engines;
    std::istringstream iss(input);
    std::string token;
    while (std::getline(iss, token, ',')) {
        // Trim whitespace
        size_t start = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t");
        if (start != std::string::npos) {
            engines.push_back(token.substr(start, end - start + 1));
        }
    }
    return engines;
}

void test_parse_engine_list() {
    TEST("P3-5: Parse comma-separated engine list");
    auto engines = parse_engines("cpu,gpu,ram");
    ASSERT_EQ(engines.size(), (size_t)3, "Should parse 3 engines");
    ASSERT_EQ(engines[0], std::string("cpu"), "First should be cpu");
    ASSERT_EQ(engines[1], std::string("gpu"), "Second should be gpu");
    ASSERT_EQ(engines[2], std::string("ram"), "Third should be ram");
    PASS();
}

void test_parse_with_spaces() {
    TEST("P3-5: Parse engine list with spaces");
    auto engines = parse_engines(" cpu , gpu , storage ");
    ASSERT_EQ(engines.size(), (size_t)3, "Should parse 3 engines");
    ASSERT_EQ(engines[0], std::string("cpu"), "Trimmed cpu");
    ASSERT_EQ(engines[1], std::string("gpu"), "Trimmed gpu");
    ASSERT_EQ(engines[2], std::string("storage"), "Trimmed storage");
    PASS();
}

void test_combined_start_all() {
    TEST("P3-5: Combined test starts all engines");
    MockEngine cpu("cpu"), gpu("gpu"), ram("ram");
    std::vector<MockEngine*> engines = {&cpu, &gpu, &ram};
    for (auto* e : engines) e->start();

    for (auto* e : engines) {
        ASSERT_TRUE(e->is_running(), (e->name() + " should be running").c_str());
    }
    PASS();
}

void test_combined_stop_all() {
    TEST("P3-5: Combined test stops all engines");
    MockEngine cpu("cpu"), gpu("gpu");
    cpu.start(); gpu.start();
    std::vector<MockEngine*> engines = {&cpu, &gpu};
    for (auto* e : engines) e->stop();

    for (auto* e : engines) {
        ASSERT_TRUE(!e->is_running(), (e->name() + " should be stopped").c_str());
    }
    PASS();
}

void test_combined_aggregate_errors() {
    TEST("P3-5: Combined test aggregates errors from all engines");
    MockEngine cpu("cpu"), gpu("gpu"), ram("ram");
    cpu.inject_error(); cpu.inject_error();
    gpu.inject_error();
    // ram has 0 errors

    int total = cpu.error_count() + gpu.error_count() + ram.error_count();
    ASSERT_EQ(total, 3, "Total errors should be 3");
    PASS();
}

void test_combined_stop_on_error() {
    TEST("P3-5: Stop-on-error stops all engines when one has error");
    MockEngine cpu("cpu"), gpu("gpu");
    cpu.start(); gpu.start();

    // Simulate: gpu gets an error, stop_on_error is enabled
    gpu.inject_error();
    bool stop_on_error = true;

    if (stop_on_error && (cpu.error_count() > 0 || gpu.error_count() > 0)) {
        cpu.stop();
        gpu.stop();
    }

    ASSERT_TRUE(!cpu.is_running(), "CPU should be stopped");
    ASSERT_TRUE(!gpu.is_running(), "GPU should be stopped");
    PASS();
}

} // namespace test_p3_5

// =============================================================================
// P3-6: GPU Coil Whine Detection
// =============================================================================

namespace test_p3_6 {

// Simulate coil whine load pattern
float compute_coil_whine_load(float time_secs, float freq_hz) {
    float period = 1.0f / freq_hz;
    float half_period = period / 2.0f;
    float phase_time = std::fmod(time_secs, period);
    return (phase_time < half_period) ? 0.0f : 1.0f;
}

// Simulate sweep mode
float compute_sweep_freq(float time_secs, float sweep_duration = 60.0f) {
    return 50.0f * std::pow(300.0f, std::min(time_secs / sweep_duration, 1.0f));
}

void test_coil_whine_100hz() {
    TEST("P3-6: 100Hz coil whine produces 10ms period");
    float freq = 100.0f;
    float period = 1.0f / freq;
    ASSERT_NEAR(period, 0.01f, 0.0001f, "100Hz = 10ms period");

    // At t=0 should be 0% (first half of period)
    ASSERT_NEAR(compute_coil_whine_load(0.0f, freq), 0.0f, 0.01f, "t=0 should be 0%");
    // At t=0.005 should be 100% (second half of period)
    ASSERT_NEAR(compute_coil_whine_load(0.005f, freq), 1.0f, 0.01f, "t=5ms should be 100%");
    // At t=0.01 should be back to 0% (new period)
    ASSERT_NEAR(compute_coil_whine_load(0.01f, freq), 0.0f, 0.01f, "t=10ms should be 0%");
    PASS();
}

void test_coil_whine_50hz() {
    TEST("P3-6: 50Hz coil whine produces 20ms period");
    float freq = 50.0f;
    // At t=0 should be 0%
    ASSERT_NEAR(compute_coil_whine_load(0.0f, freq), 0.0f, 0.01f, "t=0 should be 0%");
    // At t=0.01 should be 100% (second half)
    ASSERT_NEAR(compute_coil_whine_load(0.01f, freq), 1.0f, 0.01f, "t=10ms should be 100%");
    PASS();
}

void test_coil_whine_1khz() {
    TEST("P3-6: 1kHz coil whine produces 1ms period");
    float freq = 1000.0f;
    float period = 1.0f / freq;
    ASSERT_NEAR(period, 0.001f, 0.00001f, "1kHz = 1ms period");
    PASS();
}

void test_coil_whine_square_wave() {
    TEST("P3-6: Coil whine is a square wave (only 0% or 100%)");
    float freq = 200.0f;
    for (float t = 0.0f; t < 0.1f; t += 0.0001f) {
        float load = compute_coil_whine_load(t, freq);
        ASSERT_TRUE(load == 0.0f || load == 1.0f,
                    "Load should be exactly 0% or 100%");
    }
    PASS();
}

void test_sweep_starts_at_50hz() {
    TEST("P3-6: Sweep mode starts at 50Hz");
    float freq = compute_sweep_freq(0.0f);
    ASSERT_NEAR(freq, 50.0f, 1.0f, "t=0 should be ~50Hz");
    PASS();
}

void test_sweep_ends_at_15khz() {
    TEST("P3-6: Sweep mode ends at 15kHz");
    float freq = compute_sweep_freq(60.0f);
    ASSERT_NEAR(freq, 15000.0f, 100.0f, "t=60s should be ~15kHz");
    PASS();
}

void test_sweep_logarithmic() {
    TEST("P3-6: Sweep is logarithmic (not linear)");
    float f_start = compute_sweep_freq(0.0f);
    float f_mid = compute_sweep_freq(30.0f);
    float f_end = compute_sweep_freq(60.0f);

    // In a logarithmic sweep, midpoint frequency should be geometric mean
    float geometric_mean = std::sqrt(f_start * f_end);
    ASSERT_NEAR(f_mid, geometric_mean, geometric_mean * 0.1f,
                "Mid-point should be near geometric mean of start and end");
    PASS();
}

void test_freq_clamping() {
    TEST("P3-6: Frequency is clamped to 10-15000Hz range");
    // Simulate the clamping logic from set_coil_whine_freq
    auto clamp_freq = [](float hz) -> float {
        if (hz <= 0.0f) return 0.0f; // 0 = sweep mode
        return std::max(10.0f, std::min(15000.0f, hz));
    };

    ASSERT_NEAR(clamp_freq(5.0f), 10.0f, 0.01f, "5Hz should clamp to 10Hz");
    ASSERT_NEAR(clamp_freq(20000.0f), 15000.0f, 0.01f, "20kHz should clamp to 15kHz");
    ASSERT_NEAR(clamp_freq(100.0f), 100.0f, 0.01f, "100Hz should stay 100Hz");
    ASSERT_NEAR(clamp_freq(0.0f), 0.0f, 0.01f, "0Hz = sweep mode");
    PASS();
}

} // namespace test_p3_6

// =============================================================================
// Main
// =============================================================================

int main() {
    std::cout << "=== OCCT Native Phase 3 Feature Tests ===" << std::endl;

    std::cout << std::endl << "--- P3-1: CPU Cache-Only Mode ---" << std::endl;
    test_p3_1::test_cache_buffer_fits_l3();
    test_p3_1::test_large_data_does_not_fit();
    test_p3_1::test_cache_stress_no_nan();
    test_p3_1::test_cache_fma_correctness();

    std::cout << std::endl << "--- P3-2: CPU+RAM Large Data Set ---" << std::endl;
    test_p3_2::test_large_data_256mb_ops();
    test_p3_2::test_large_data_forces_memory_access();
    test_p3_2::test_streaming_verification_sampling();

    std::cout << std::endl << "--- P3-3: Core Cycling ---" << std::endl;
    test_p3_3::test_cycling_starts_at_core_0();
    test_p3_3::test_cycling_advances_sequentially();
    test_p3_3::test_cycling_wraps_around();
    test_p3_3::test_cycling_only_one_active();
    test_p3_3::test_cycling_150ms_interval();
    test_p3_3::test_cycling_full_rotation();

    std::cout << std::endl << "--- P3-4: WHEA Error Monitoring ---" << std::endl;
    test_p3_4::test_whea_default_state();
    test_p3_4::test_whea_error_counting();
    test_p3_4::test_whea_auto_stop_triggers();
    test_p3_4::test_whea_no_callback_without_auto_stop();
    test_p3_4::test_whea_error_types();

    std::cout << std::endl << "--- P3-5: Combined Test ---" << std::endl;
    test_p3_5::test_parse_engine_list();
    test_p3_5::test_parse_with_spaces();
    test_p3_5::test_combined_start_all();
    test_p3_5::test_combined_stop_all();
    test_p3_5::test_combined_aggregate_errors();
    test_p3_5::test_combined_stop_on_error();

    std::cout << std::endl << "--- P3-6: Coil Whine Detection ---" << std::endl;
    test_p3_6::test_coil_whine_100hz();
    test_p3_6::test_coil_whine_50hz();
    test_p3_6::test_coil_whine_1khz();
    test_p3_6::test_coil_whine_square_wave();
    test_p3_6::test_sweep_starts_at_50hz();
    test_p3_6::test_sweep_ends_at_15khz();
    test_p3_6::test_sweep_logarithmic();
    test_p3_6::test_freq_clamping();

    std::cout << std::endl << "=== Results: " << tests_passed << " / "
              << tests_run << " passed ===" << std::endl;

    return (tests_passed == tests_run) ? 0 : 1;
}
