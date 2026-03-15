#pragma once

#include <string>
#include <vector>

namespace occt { namespace utils {

enum class CoreType { PERFORMANCE, EFFICIENCY, UNKNOWN };

struct CpuInfo {
    std::string brand;
    int physical_cores = 0;
    int logical_cores = 0;
    int l1_cache_kb = 0;
    int l2_cache_kb = 0;
    int l3_cache_kb = 0;
    bool has_sse42 = false;
    bool has_avx = false;
    bool has_avx2 = false;
    bool has_avx512f = false;
    bool has_fma = false;

    // Hybrid core topology
    int p_cores = 0;          // Performance core count
    int e_cores = 0;          // Efficiency core count
    bool is_hybrid = false;   // True if heterogeneous (Intel 12th+ gen, Apple Silicon)
    std::vector<CoreType> core_types;  // Per-core type mapping
};

CpuInfo detect_cpu();

/// Detect the core type for a specific logical core.
CoreType detect_core_type(int core_id);

}} // namespace occt::utils
