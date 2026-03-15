#pragma once

#include <string>
#include <vector>

namespace occt {

struct BenchmarkEntry {
    std::string system_name;     // e.g. "i9-13900K + RTX 4090"
    double cpu_score = 0.0;      // GFLOPS
    double gpu_score = 0.0;      // GFLOPS
    double ram_score = 0.0;      // GB/s bandwidth
    double storage_score = 0.0;  // MB/s sequential
    double overall_score = 0.0;  // Weighted composite
    std::string timestamp;
};

class Leaderboard {
public:
    /// @param path  Path to the leaderboard file. Empty string uses default config dir.
    explicit Leaderboard(const std::string& path = "");

    /// Add a benchmark entry to the leaderboard.
    void submit(const BenchmarkEntry& entry);

    /// Get rankings sorted by category: "cpu", "gpu", "ram", "storage", "overall".
    std::vector<BenchmarkEntry> get_rankings(const std::string& category = "overall") const;

    /// Format the leaderboard as an ASCII table for terminal output.
    std::string format_table() const;

    /// Scoring formula: CPU 40% + GPU 30% + RAM 15% + Storage 15%
    static double compute_overall_score(double cpu, double gpu, double ram, double storage);

private:
    std::string path_;
    std::vector<BenchmarkEntry> entries_;

    void load();
    void save() const;
};

} // namespace occt
