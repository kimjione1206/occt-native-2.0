#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <QString>

#include "base_engine.h"
#include "storage/verify_block.h"

namespace occt {

enum class StorageMode {
    SEQ_WRITE,   // Sequential write
    SEQ_READ,    // Sequential read
    RAND_WRITE,  // Random 4K write
    RAND_READ,   // Random 4K read
    MIXED,       // Mixed 70% read / 30% write
    VERIFY_SEQ,  // Sequential write all → sequential read+verify
    VERIFY_RAND, // Random block write → random read+verify
    FILL_VERIFY, // H2testw-style: fill entire file → verify all
    BUTTERFLY    // Converging write from both ends → verify all
};

struct StorageMetrics {
    double write_mbs   = 0.0;   // MB/s write throughput
    double read_mbs    = 0.0;   // MB/s read throughput
    double iops        = 0.0;   // I/O operations per second
    double latency_us  = 0.0;   // Average latency in microseconds
    double elapsed_secs = 0.0;
    double progress_pct = 0.0;  // 0 ~ 100
    int error_count = 0;
    std::string state;    // "preparing", "testing", "completed", "error"

    // Verification metrics (appended for backward compatibility)
    uint64_t blocks_written  = 0;
    uint64_t blocks_verified = 0;
    uint64_t verify_errors   = 0;
    uint64_t crc_errors      = 0;
    uint64_t magic_errors    = 0;
    uint64_t index_errors    = 0;
    uint64_t pattern_errors  = 0;
    uint64_t io_errors       = 0;
    uint64_t first_error_block = 0;
    uint64_t last_error_block  = 0;
    double   verify_mbs      = 0.0;
    std::vector<StorageError> error_log; // capped at 1000
};

struct StorageBenchmarkResult {
    struct TestResult {
        std::string test_name;   // e.g. "SEQ1M Q8T1 Read"
        double throughput_mbs;   // MB/s
        double iops;             // IOPS
        double latency_us;       // Average latency in microseconds
    };
    std::vector<TestResult> results;
    std::string device_path;
    std::string timestamp;
};

class StorageEngine : public IEngine {
public:
    StorageEngine();
    ~StorageEngine() override;

    StorageEngine(const StorageEngine&) = delete;
    StorageEngine& operator=(const StorageEngine&) = delete;

    /// Start storage I/O stress test.
    /// @param mode          I/O pattern.
    /// @param path          Directory/file path for test file.
    /// @param file_size_mb  Total test file size in MB.
    /// @param queue_depth   Number of concurrent I/O threads.
    /// @param duration_secs Maximum test duration in seconds (0 = unlimited).
    bool start(StorageMode mode, const std::string& path,
               uint64_t file_size_mb = 2048, int queue_depth = 4,
               uint64_t duration_secs = 0);

    /// Run a CrystalDiskMark-style storage benchmark (synchronous).
    /// Returns results for 8 standard tests (SEQ/RND, read/write).
    StorageBenchmarkResult run_benchmark(const std::string& path,
                                         int file_size_mb = 1024);

    /// Returns last error message if start failed.
    std::string last_error() const;

    void stop() override;
    bool is_running() const override;
    std::string name() const override { return "Storage"; }

    StorageMetrics get_metrics() const;

    using MetricsCallback = std::function<void(const StorageMetrics&)>;
    void set_metrics_callback(MetricsCallback cb);

private:
    void run(StorageMode mode, const std::string& path,
             uint64_t file_size_bytes, int queue_depth);

    void seq_write(intptr_t fd, uint8_t* buf, size_t buf_size,
                   uint64_t file_size, int queue_depth);
    void seq_read(intptr_t fd, uint8_t* buf, size_t buf_size,
                  uint64_t file_size, int queue_depth);
    void rand_write(intptr_t fd, uint8_t* buf, uint64_t file_size, int queue_depth);
    void rand_read(intptr_t fd, uint8_t* buf, uint64_t file_size, int queue_depth);
    void mixed_io(intptr_t fd, uint8_t* buf, uint64_t file_size, int queue_depth);

    // Verification modes
    void verify_seq(const std::string& path, uint64_t file_size, int queue_depth);
    void verify_rand(const std::string& path, uint64_t file_size, int queue_depth);
    void fill_verify(const std::string& path, uint64_t file_size, int queue_depth);
    void butterfly_verify(const std::string& path, uint64_t file_size, int queue_depth);

    void report_storage_error(const StorageError& err);

    // Benchmark helper: run a single timed I/O test
    StorageBenchmarkResult::TestResult run_bench_test(
        const std::string& test_name, const std::string& file_path,
        bool is_sequential, bool is_read, size_t block_size,
        int queue_depth, int thread_count, uint64_t file_size_bytes,
        double duration_secs);

    // Platform-specific helpers
    intptr_t open_direct(const std::string& path, bool read_only);
    void close_file(intptr_t fd);
    uint8_t* alloc_aligned(size_t size);
    void free_aligned(uint8_t* ptr);

    std::thread worker_;
    std::mutex start_stop_mutex_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<uint64_t> duration_secs_{0};

    mutable std::mutex metrics_mutex_;
    StorageMetrics metrics_;

    MetricsCallback metrics_cb_;
    std::mutex cb_mutex_;

    std::string test_file_path_;
    std::string last_error_;

    void log(const std::string& msg);
    QString log_file_;

    uint32_t block_size_kb_ = 4;      // Random I/O block size in KB (default 4KB)
    bool force_direct_io_ = true;      // Use O_DIRECT / FILE_FLAG_NO_BUFFERING

public:
    /// Set the block size for random I/O operations (in KB). Must be called before start().
    void set_block_size_kb(uint32_t kb);

    /// Enable or disable direct I/O (bypass OS cache). Must be called before start().
    void set_direct_io(bool enabled);
};

} // namespace occt
