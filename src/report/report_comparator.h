#pragma once

#include <string>
#include <vector>

namespace occt {

struct ComparisonEntry {
    std::string metric_name;    // e.g. "CPU GFLOPS", "RAM Bandwidth"
    double value_a = 0.0;       // Report A value
    double value_b = 0.0;       // Report B value
    double diff_abs = 0.0;      // Absolute difference
    double diff_pct = 0.0;      // Percentage change
    std::string direction;      // "improved", "regressed", "unchanged"
};

struct ComparisonResult {
    std::string report_a_path;
    std::string report_b_path;
    std::vector<ComparisonEntry> entries;
    std::string summary;        // "3 improved, 1 regressed, 2 unchanged"
};

/// Compare two JSON report files and produce a diff of key metrics.
ComparisonResult compare_reports(const std::string& path_a, const std::string& path_b);

/// Format a ComparisonResult as an ASCII table for terminal output.
std::string format_comparison_table(const ComparisonResult& result);

} // namespace occt
