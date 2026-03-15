#pragma once

#include <QString>

namespace occt {

struct CliOptions {
    bool cli_mode = false;
    QString test;             // "cpu", "gpu", "ram", "storage", "psu", "schedule", "benchmark", "certificate"
    QString mode;             // "avx2", "linpack", "prime", etc.
    int threads = 0;          // 0 = auto-detect
    int duration = 0;         // seconds, 0 = default
    QString schedule_file;    // JSON schedule file path
    QString report_format;    // "html", "png", "csv", "json"
    QString output_path;      // report output path
    bool monitor_only = false;
    int memory_percent = 90;  // RAM test: percentage of memory to use
    bool show_help = false;
    bool show_version = false;

    // CPU options (Fix 1-3)
    QString load_pattern;     // "steady" or "variable"

    // RAM options (Fix 1-5)
    int passes = 1;           // Number of RAM test passes

    // Storage options (Fix 1-6)
    int file_size_mb = 256;   // Storage test file size in MB
    int queue_depth = 4;      // Storage I/O queue depth
    QString storage_path;     // Storage test path (empty = temp dir)
    QString benchmark_path;   // Storage benchmark target path (default: current dir)

    // GPU options (Fix 2-1)
    int gpu_index = -1;       // GPU index (-1 = default)
    int shader_complexity = 1; // Vulkan shader complexity (1-5)
    QString adaptive_mode;    // "variable", "switch", or "coil_whine"
    float coil_freq = 100.0f; // Coil whine frequency in Hz (0 = sweep mode)

    // PSU options (Fix 2-2)
    bool use_all_gpus = false;

    // Schedule options (Fix 3-1)
    bool stop_on_error = false;

    // Certificate options (Fix 3-2)
    QString cert_tier;        // "bronze", "silver", "gold", "platinum"

    // WHEA monitoring (P3-4)
    bool whea = false;        // Enable WHEA error monitoring during tests

    // Combined test (P3-5)
    QString engines;          // Comma-separated engine list for combined test: "cpu,gpu,ram,storage"

    // Report comparison (P4-3)
    QString compare_a;        // First report path for comparison
    QString compare_b;        // Second report path for comparison

    // Certificate store (P4-4)
    QString upload_cert;      // Certificate JSON file to upload
    QString verify_hash;      // Hash to verify
    bool list_certs = false;  // List all stored certificates

    // Leaderboard (P4-5)
    QString leaderboard_cmd;  // "show" or "submit"

    // GPU backend (Gap 3)
    QString backend;          // "opencl" or "vulkan" (empty = auto)

    // CPU intensity (Gap 5)
    QString intensity;        // "normal" or "extreme" (empty = extreme)

    // Storage block size (Gap 6)
    int block_size_kb = 4;    // Block size in KB (4, 8, 64, 128, 1024, 4096)

    // Post-update auto test
    bool post_update = false; // Run smoke test after auto-update

    // Storage direct I/O (Gap 7)
    bool direct_io = false;   // Use O_DIRECT / FILE_FLAG_NO_BUFFERING

    // Safety thresholds (Gap 8)
    int cpu_temp_limit = 95;  // CPU temperature limit in degrees C
    int gpu_temp_limit = 90;  // GPU temperature limit in degrees C
    int power_limit = 300;    // Power limit in Watts

    // Schedule preset (Gap 9)
    QString preset;           // "quick", "standard", "extreme", "oc_validation"
};

/// Parse command-line arguments into CliOptions.
CliOptions parse_args(int argc, char** argv);

/// Print usage help to stdout.
void print_usage();

} // namespace occt
