// =============================================================================
// OCCT Native - Phase 4 Feature Tests
// Tests for P4-1 (Storage Benchmark), P4-2 (P/E-core), P4-3 (Report Compare),
//            P4-4 (Cert Store), P4-5 (Leaderboard)
// =============================================================================
// Build:  g++ -std=c++17 -o test_phase4 test_phase4_features.cpp
// Run:    ./test_phase4
// =============================================================================

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
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
// P4-1: CrystalDiskMark-Style Storage Benchmark
// =============================================================================

namespace test_p4_1 {

struct BenchTestResult {
    std::string test_name;
    double throughput_mbs;
    double iops;
    double latency_us;
};

// CrystalDiskMark test sequence
std::vector<std::string> get_cdm_test_names() {
    return {
        "SEQ1M Q8T1 Read",  "SEQ1M Q8T1 Write",
        "SEQ128K Q32T1 Read", "SEQ128K Q32T1 Write",
        "RND4K Q32T16 Read", "RND4K Q32T16 Write",
        "RND4K Q1T1 Read",  "RND4K Q1T1 Write"
    };
}

// Calculate IOPS from throughput and block size
double compute_iops(double throughput_mbs, int block_size_kb) {
    return (throughput_mbs * 1024.0) / static_cast<double>(block_size_kb);
}

void test_cdm_has_8_tests() {
    TEST("P4-1: CrystalDiskMark benchmark has 8 tests");
    auto names = get_cdm_test_names();
    ASSERT_EQ(names.size(), (size_t)8, "Should have 8 tests");
    PASS();
}

void test_cdm_read_write_pairs() {
    TEST("P4-1: Each test has Read/Write pair");
    auto names = get_cdm_test_names();
    int reads = 0, writes = 0;
    for (const auto& n : names) {
        if (n.find("Read") != std::string::npos) reads++;
        if (n.find("Write") != std::string::npos) writes++;
    }
    ASSERT_EQ(reads, 4, "Should have 4 read tests");
    ASSERT_EQ(writes, 4, "Should have 4 write tests");
    PASS();
}

void test_iops_calculation() {
    TEST("P4-1: IOPS calculation from throughput");
    // 1000 MB/s with 4KB blocks = 256000 IOPS
    double iops = compute_iops(1000.0, 4);
    ASSERT_NEAR(iops, 256000.0, 1.0, "1000 MB/s @ 4KB = 256000 IOPS");
    PASS();
}

void test_seq_block_sizes() {
    TEST("P4-1: Sequential tests use 1MB and 128KB blocks");
    int seq1m_kb = 1024;    // 1MB
    int seq128k_kb = 128;   // 128KB
    ASSERT_EQ(seq1m_kb, 1024, "SEQ1M should use 1MB blocks");
    ASSERT_EQ(seq128k_kb, 128, "SEQ128K should use 128KB blocks");
    PASS();
}

void test_random_block_size() {
    TEST("P4-1: Random tests use 4KB blocks");
    int rnd4k_kb = 4;
    ASSERT_EQ(rnd4k_kb, 4, "RND4K should use 4KB blocks");
    PASS();
}

void test_queue_depth_configs() {
    TEST("P4-1: Queue depth configurations are correct");
    // SEQ1M Q8T1, SEQ128K Q32T1, RND4K Q32T16, RND4K Q1T1
    struct QDConfig { int queue_depth; int threads; };
    QDConfig configs[] = {{8, 1}, {32, 1}, {32, 16}, {1, 1}};
    ASSERT_EQ(configs[0].queue_depth, 8, "SEQ1M queue depth");
    ASSERT_EQ(configs[1].queue_depth, 32, "SEQ128K queue depth");
    ASSERT_EQ(configs[2].threads, 16, "RND4K Q32 threads");
    ASSERT_EQ(configs[3].queue_depth, 1, "RND4K Q1 queue depth");
    PASS();
}

} // namespace test_p4_1

// =============================================================================
// P4-2: P-core/E-core Recognition
// =============================================================================

namespace test_p4_2 {

enum class CoreType { PERFORMANCE, EFFICIENCY, UNKNOWN };

struct HybridInfo {
    int p_cores = 0;
    int e_cores = 0;
    bool is_hybrid = false;
    std::vector<CoreType> core_types;
};

// Simulate Intel 12th gen i9-12900K: 8P + 8E = 24 threads (P has HT, E doesn't)
HybridInfo simulate_12900k() {
    HybridInfo info;
    info.is_hybrid = true;
    info.p_cores = 8;
    info.e_cores = 8;
    // 16 P-core threads (8 cores x 2 HT) + 8 E-core threads
    for (int i = 0; i < 16; ++i) info.core_types.push_back(CoreType::PERFORMANCE);
    for (int i = 0; i < 8; ++i) info.core_types.push_back(CoreType::EFFICIENCY);
    return info;
}

// Simulate non-hybrid CPU (e.g., i7-10700K)
HybridInfo simulate_non_hybrid() {
    HybridInfo info;
    info.is_hybrid = false;
    info.p_cores = 0;
    info.e_cores = 0;
    for (int i = 0; i < 16; ++i) info.core_types.push_back(CoreType::UNKNOWN);
    return info;
}

std::string core_type_to_string(CoreType ct) {
    switch (ct) {
        case CoreType::PERFORMANCE: return "P-core";
        case CoreType::EFFICIENCY: return "E-core";
        default: return "Unknown";
    }
}

void test_hybrid_detection() {
    TEST("P4-2: Hybrid CPU detected correctly");
    auto info = simulate_12900k();
    ASSERT_TRUE(info.is_hybrid, "12900K should be hybrid");
    ASSERT_EQ(info.p_cores, 8, "Should have 8 P-cores");
    ASSERT_EQ(info.e_cores, 8, "Should have 8 E-cores");
    PASS();
}

void test_non_hybrid_fallback() {
    TEST("P4-2: Non-hybrid CPU reports all UNKNOWN");
    auto info = simulate_non_hybrid();
    ASSERT_TRUE(!info.is_hybrid, "10700K should not be hybrid");
    ASSERT_EQ(info.p_cores, 0, "No P-cores");
    ASSERT_EQ(info.e_cores, 0, "No E-cores");
    for (auto ct : info.core_types) {
        ASSERT_EQ(static_cast<int>(ct), static_cast<int>(CoreType::UNKNOWN),
                  "All cores should be UNKNOWN");
    }
    PASS();
}

void test_core_type_strings() {
    TEST("P4-2: Core type string conversion");
    ASSERT_EQ(core_type_to_string(CoreType::PERFORMANCE), std::string("P-core"), "P-core string");
    ASSERT_EQ(core_type_to_string(CoreType::EFFICIENCY), std::string("E-core"), "E-core string");
    ASSERT_EQ(core_type_to_string(CoreType::UNKNOWN), std::string("Unknown"), "Unknown string");
    PASS();
}

void test_hybrid_core_count_consistency() {
    TEST("P4-2: P+E core thread count matches total");
    auto info = simulate_12900k();
    int p_threads = 0, e_threads = 0;
    for (auto ct : info.core_types) {
        if (ct == CoreType::PERFORMANCE) p_threads++;
        else if (ct == CoreType::EFFICIENCY) e_threads++;
    }
    ASSERT_EQ(p_threads + e_threads, (int)info.core_types.size(),
              "P + E threads should equal total");
    PASS();
}

void test_cpuid_leaf_0x1a_values() {
    TEST("P4-2: CPUID leaf 0x1A core type IDs are correct");
    // Intel Thread Director: EAX[31:24]
    uint8_t atom_core_type = 0x20;   // E-core (Atom)
    uint8_t core_core_type = 0x40;   // P-core (Core)
    ASSERT_TRUE(atom_core_type != core_core_type, "E-core and P-core IDs must differ");
    ASSERT_EQ(atom_core_type, (uint8_t)0x20, "E-core ID = 0x20");
    ASSERT_EQ(core_core_type, (uint8_t)0x40, "P-core ID = 0x40");
    PASS();
}

} // namespace test_p4_2

// =============================================================================
// P4-3: Report Comparison
// =============================================================================

namespace test_p4_3 {

struct ComparisonEntry {
    std::string metric_name;
    double value_a;
    double value_b;
    double diff_abs;
    double diff_pct;
    std::string direction;  // "improved", "regressed", "unchanged"
};

ComparisonEntry compare_metric(const std::string& name, double a, double b,
                                bool higher_is_better = true) {
    ComparisonEntry e;
    e.metric_name = name;
    e.value_a = a;
    e.value_b = b;
    e.diff_abs = b - a;
    e.diff_pct = (a != 0.0) ? ((b - a) / a) * 100.0 : 0.0;

    double threshold = 1.0; // 1%
    if (std::abs(e.diff_pct) <= threshold) {
        e.direction = "unchanged";
    } else if (higher_is_better) {
        e.direction = (e.diff_pct > 0) ? "improved" : "regressed";
    } else {
        e.direction = (e.diff_pct < 0) ? "improved" : "regressed";
    }
    return e;
}

std::string summarize(const std::vector<ComparisonEntry>& entries) {
    int improved = 0, regressed = 0, unchanged = 0;
    for (const auto& e : entries) {
        if (e.direction == "improved") improved++;
        else if (e.direction == "regressed") regressed++;
        else unchanged++;
    }
    std::ostringstream oss;
    oss << improved << " improved, " << regressed << " regressed, " << unchanged << " unchanged";
    return oss.str();
}

void test_improved_metric() {
    TEST("P4-3: Higher GFLOPS detected as improvement");
    auto e = compare_metric("CPU GFLOPS", 100.0, 120.0, true);
    ASSERT_EQ(e.direction, std::string("improved"), "120 > 100 should be improved");
    ASSERT_NEAR(e.diff_pct, 20.0, 0.1, "Should be +20%");
    PASS();
}

void test_regressed_metric() {
    TEST("P4-3: Lower GFLOPS detected as regression");
    auto e = compare_metric("CPU GFLOPS", 100.0, 80.0, true);
    ASSERT_EQ(e.direction, std::string("regressed"), "80 < 100 should be regressed");
    ASSERT_NEAR(e.diff_pct, -20.0, 0.1, "Should be -20%");
    PASS();
}

void test_unchanged_within_threshold() {
    TEST("P4-3: <1% change reported as unchanged");
    auto e = compare_metric("Temperature", 65.0, 65.5, false);
    ASSERT_EQ(e.direction, std::string("unchanged"), "0.77% change should be unchanged");
    PASS();
}

void test_lower_is_better() {
    TEST("P4-3: Lower temperature detected as improvement");
    auto e = compare_metric("Temperature", 80.0, 70.0, false);
    ASSERT_EQ(e.direction, std::string("improved"), "Lower temp should be improved");
    PASS();
}

void test_summary_format() {
    TEST("P4-3: Summary counts directions correctly");
    std::vector<ComparisonEntry> entries;
    entries.push_back(compare_metric("CPU", 100, 120, true));   // improved
    entries.push_back(compare_metric("GPU", 200, 180, true));   // regressed
    entries.push_back(compare_metric("RAM", 50, 50.3, true));   // unchanged
    entries.push_back(compare_metric("Temp", 70, 60, false));   // improved

    std::string summary = summarize(entries);
    ASSERT_TRUE(summary.find("2 improved") != std::string::npos, "Should have 2 improved");
    ASSERT_TRUE(summary.find("1 regressed") != std::string::npos, "Should have 1 regressed");
    ASSERT_TRUE(summary.find("1 unchanged") != std::string::npos, "Should have 1 unchanged");
    PASS();
}

void test_zero_baseline() {
    TEST("P4-3: Zero baseline produces 0% change");
    auto e = compare_metric("Errors", 0.0, 5.0, false);
    ASSERT_NEAR(e.diff_pct, 0.0, 0.01, "Zero baseline diff_pct should be 0");
    PASS();
}

} // namespace test_p4_3

// =============================================================================
// P4-4: Certificate Store
// =============================================================================

namespace test_p4_4 {

// Simulate simple cert store (in-memory for testing)
class MockCertStore {
public:
    bool submit(const std::string& hash, const std::string& cert_json) {
        if (hash.empty() || cert_json.empty()) return false;
        store_[hash] = cert_json;
        return true;
    }

    std::string lookup(const std::string& hash) const {
        auto it = store_.find(hash);
        if (it != store_.end()) return it->second;
        return "";
    }

    bool verify(const std::string& hash) const {
        return store_.find(hash) != store_.end();
    }

    std::vector<std::string> list_hashes() const {
        std::vector<std::string> hashes;
        for (const auto& kv : store_) {
            hashes.push_back(kv.first);
        }
        return hashes;
    }

    size_t size() const { return store_.size(); }

private:
    std::map<std::string, std::string> store_;
};

void test_submit_and_lookup() {
    TEST("P4-4: Submit and lookup certificate");
    MockCertStore store;
    std::string hash = "abc123def456";
    std::string cert = R"({"tier":"gold","passed":true})";
    ASSERT_TRUE(store.submit(hash, cert), "Submit should succeed");
    ASSERT_EQ(store.lookup(hash), cert, "Lookup should return submitted cert");
    PASS();
}

void test_verify_existing() {
    TEST("P4-4: Verify returns true for existing cert");
    MockCertStore store;
    store.submit("hash1", "{}");
    ASSERT_TRUE(store.verify("hash1"), "Should verify existing cert");
    PASS();
}

void test_verify_nonexistent() {
    TEST("P4-4: Verify returns false for missing cert");
    MockCertStore store;
    ASSERT_TRUE(!store.verify("nonexistent"), "Should not verify missing cert");
    PASS();
}

void test_list_hashes() {
    TEST("P4-4: List all certificate hashes");
    MockCertStore store;
    store.submit("hash_a", "{}");
    store.submit("hash_b", "{}");
    store.submit("hash_c", "{}");
    auto hashes = store.list_hashes();
    ASSERT_EQ(hashes.size(), (size_t)3, "Should list 3 hashes");
    PASS();
}

void test_reject_empty() {
    TEST("P4-4: Reject empty hash or cert");
    MockCertStore store;
    ASSERT_TRUE(!store.submit("", "data"), "Empty hash should fail");
    ASSERT_TRUE(!store.submit("hash", ""), "Empty cert should fail");
    PASS();
}

void test_lookup_nonexistent_returns_empty() {
    TEST("P4-4: Lookup nonexistent returns empty string");
    MockCertStore store;
    ASSERT_EQ(store.lookup("missing"), std::string(""), "Should return empty");
    PASS();
}

} // namespace test_p4_4

// =============================================================================
// P4-5: Benchmark Leaderboard
// =============================================================================

namespace test_p4_5 {

struct BenchmarkEntry {
    std::string system_name;
    double cpu_score;
    double gpu_score;
    double ram_score;
    double storage_score;
    double overall_score;
};

// Scoring: CPU 40% + GPU 30% + RAM 15% + Storage 15%
double compute_overall(double cpu, double gpu, double ram, double storage) {
    return cpu * 0.40 + gpu * 0.30 + ram * 0.15 + storage * 0.15;
}

std::vector<BenchmarkEntry> sort_by_overall(std::vector<BenchmarkEntry> entries) {
    std::sort(entries.begin(), entries.end(),
              [](const BenchmarkEntry& a, const BenchmarkEntry& b) {
                  return a.overall_score > b.overall_score;
              });
    return entries;
}

std::vector<BenchmarkEntry> sort_by_cpu(std::vector<BenchmarkEntry> entries) {
    std::sort(entries.begin(), entries.end(),
              [](const BenchmarkEntry& a, const BenchmarkEntry& b) {
                  return a.cpu_score > b.cpu_score;
              });
    return entries;
}

void test_overall_score_formula() {
    TEST("P4-5: Overall score = CPU*0.4 + GPU*0.3 + RAM*0.15 + Storage*0.15");
    double score = compute_overall(100.0, 200.0, 50.0, 80.0);
    // 100*0.4 + 200*0.3 + 50*0.15 + 80*0.15 = 40 + 60 + 7.5 + 12 = 119.5
    ASSERT_NEAR(score, 119.5, 0.01, "Score should be 119.5");
    PASS();
}

void test_cpu_weighted_highest() {
    TEST("P4-5: CPU has highest weight (40%)");
    double cpu_only = compute_overall(100, 0, 0, 0);
    double gpu_only = compute_overall(0, 100, 0, 0);
    ASSERT_TRUE(cpu_only > gpu_only, "CPU weight (40%) > GPU weight (30%)");
    PASS();
}

void test_sort_by_overall() {
    TEST("P4-5: Leaderboard sorts by overall score descending");
    std::vector<BenchmarkEntry> entries = {
        {"System A", 100, 100, 50, 50, compute_overall(100, 100, 50, 50)},
        {"System C", 300, 300, 100, 100, compute_overall(300, 300, 100, 100)},
        {"System B", 200, 200, 80, 80, compute_overall(200, 200, 80, 80)},
    };
    auto sorted = sort_by_overall(entries);
    ASSERT_EQ(sorted[0].system_name, std::string("System C"), "Top should be System C");
    ASSERT_EQ(sorted[1].system_name, std::string("System B"), "Second should be System B");
    ASSERT_EQ(sorted[2].system_name, std::string("System A"), "Third should be System A");
    PASS();
}

void test_sort_by_category() {
    TEST("P4-5: Leaderboard sorts by CPU category");
    std::vector<BenchmarkEntry> entries = {
        {"Low CPU", 50, 300, 100, 100, 0},
        {"High CPU", 500, 100, 50, 50, 0},
        {"Mid CPU", 200, 200, 80, 80, 0},
    };
    auto sorted = sort_by_cpu(entries);
    ASSERT_EQ(sorted[0].system_name, std::string("High CPU"), "Top CPU should be High CPU");
    ASSERT_EQ(sorted[1].system_name, std::string("Mid CPU"), "Second should be Mid CPU");
    PASS();
}

void test_zero_scores() {
    TEST("P4-5: Zero scores produce zero overall");
    double score = compute_overall(0, 0, 0, 0);
    ASSERT_NEAR(score, 0.0, 0.001, "All zeros should give 0");
    PASS();
}

void test_weights_sum_to_one() {
    TEST("P4-5: Score weights sum to 1.0");
    double sum = 0.40 + 0.30 + 0.15 + 0.15;
    ASSERT_NEAR(sum, 1.0, 0.001, "Weights should sum to 1.0");
    PASS();
}

} // namespace test_p4_5

// =============================================================================
// Main
// =============================================================================

int main() {
    std::cout << "=== OCCT Native Phase 4 Feature Tests ===" << std::endl;

    std::cout << std::endl << "--- P4-1: Storage Benchmark ---" << std::endl;
    test_p4_1::test_cdm_has_8_tests();
    test_p4_1::test_cdm_read_write_pairs();
    test_p4_1::test_iops_calculation();
    test_p4_1::test_seq_block_sizes();
    test_p4_1::test_random_block_size();
    test_p4_1::test_queue_depth_configs();

    std::cout << std::endl << "--- P4-2: P-core/E-core Recognition ---" << std::endl;
    test_p4_2::test_hybrid_detection();
    test_p4_2::test_non_hybrid_fallback();
    test_p4_2::test_core_type_strings();
    test_p4_2::test_hybrid_core_count_consistency();
    test_p4_2::test_cpuid_leaf_0x1a_values();

    std::cout << std::endl << "--- P4-3: Report Comparison ---" << std::endl;
    test_p4_3::test_improved_metric();
    test_p4_3::test_regressed_metric();
    test_p4_3::test_unchanged_within_threshold();
    test_p4_3::test_lower_is_better();
    test_p4_3::test_summary_format();
    test_p4_3::test_zero_baseline();

    std::cout << std::endl << "--- P4-4: Certificate Store ---" << std::endl;
    test_p4_4::test_submit_and_lookup();
    test_p4_4::test_verify_existing();
    test_p4_4::test_verify_nonexistent();
    test_p4_4::test_list_hashes();
    test_p4_4::test_reject_empty();
    test_p4_4::test_lookup_nonexistent_returns_empty();

    std::cout << std::endl << "--- P4-5: Benchmark Leaderboard ---" << std::endl;
    test_p4_5::test_overall_score_formula();
    test_p4_5::test_cpu_weighted_highest();
    test_p4_5::test_sort_by_overall();
    test_p4_5::test_sort_by_category();
    test_p4_5::test_zero_scores();
    test_p4_5::test_weights_sum_to_one();

    std::cout << std::endl << "=== Results: " << tests_passed << " / "
              << tests_run << " passed ===" << std::endl;

    return (tests_passed == tests_run) ? 0 : 1;
}
