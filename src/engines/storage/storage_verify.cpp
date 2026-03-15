// =============================================================================
// Storage Engine — Data Integrity Verification Modes
// Implements VERIFY_SEQ, VERIFY_RAND, FILL_VERIFY, BUTTERFLY
// =============================================================================

#include "../storage_engine.h"
#include "verify_block.h"
#include "utils/crc32.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <numeric>
#include <optional>
#include <random>
#include <vector>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace occt {

static constexpr size_t VBLOCK = 4096; // block size for verification

// ─── xoshiro256** (same as storage_engine.cpp) ─────────────────────────────

static inline uint64_t vrotl64(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

struct VerifyRng {
    uint64_t s[4];

    void seed(uint64_t v) {
        for (int i = 0; i < 4; ++i) {
            v += 0x9e3779b97f4a7c15ULL;
            uint64_t z = v;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            s[i] = z ^ (z >> 31);
        }
    }

    uint64_t next() {
        const uint64_t result = vrotl64(s[1] * 5, 7) * 9;
        const uint64_t t = s[1] << 17;
        s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
        s[2] ^= t; s[3] = vrotl64(s[3], 45);
        return result;
    }
};

// ─── Pattern generation for payload bytes [32..4095] ────────────────────────

static void generate_pattern(uint8_t* payload, size_t len,
                             PatternID pattern, uint32_t block_index) {
    VerifyRng rng;
    rng.seed(static_cast<uint64_t>(block_index) * 0x12345678ULL + static_cast<uint32_t>(pattern));

    switch (pattern) {
        case PatternID::SEQUENTIAL:
            for (size_t i = 0; i < len; ++i) {
                payload[i] = static_cast<uint8_t>((block_index + i) & 0xFF);
            }
            break;

        case PatternID::CHECKERBOARD:
            for (size_t i = 0; i < len; ++i) {
                payload[i] = static_cast<uint8_t>(((i + block_index) & 1) ? 0xAA : 0x55);
            }
            break;

        case PatternID::WALKING_ONES:
            for (size_t i = 0; i < len; ++i) {
                payload[i] = static_cast<uint8_t>(1 << ((i + block_index) & 7));
            }
            break;

        case PatternID::WALKING_ZEROS:
            for (size_t i = 0; i < len; ++i) {
                payload[i] = static_cast<uint8_t>(~(1 << ((i + block_index) & 7)));
            }
            break;

        case PatternID::RANDOM:
            for (size_t i = 0; i < len; i += 8) {
                uint64_t val = rng.next();
                size_t rem = std::min(len - i, size_t(8));
                std::memcpy(payload + i, &val, rem);
            }
            break;

        case PatternID::ALL_ONES:
            std::memset(payload, 0xFF, len);
            break;

        case PatternID::ALL_ZEROS:
            std::memset(payload, 0x00, len);
            break;
    }
}

// ─── Fill a 4K block with header + pattern + CRC ────────────────────────────

void fill_verify_block(uint8_t* block_4k, uint32_t block_index,
                       PatternID pattern, uint32_t pass, uint64_t timestamp_ns) {
    // Generate payload first (bytes 32..4095)
    uint8_t* payload = block_4k + VERIFY_PAYLOAD_OFFSET;
    generate_pattern(payload, VERIFY_PAYLOAD_SIZE, pattern, block_index);

    // Compute CRC of payload
    uint32_t crc = utils::crc32c(payload, VERIFY_PAYLOAD_SIZE);

    // Write header
    VerifyBlockHeader hdr{};
    hdr.magic = VERIFY_BLOCK_MAGIC;
    hdr.block_index = block_index;
    hdr.pattern_id = static_cast<uint32_t>(pattern);
    hdr.payload_crc32 = crc;
    hdr.write_timestamp = timestamp_ns;
    hdr.pass_number = pass;
    hdr.reserved = 0;

    std::memcpy(block_4k, &hdr, sizeof(hdr));
}

// ─── Verify a 4K block, returning error if any ─────────────────────────────

std::optional<StorageError> verify_block(const uint8_t* block_4k,
                                          uint32_t expected_index,
                                          PatternID expected_pattern) {
    VerifyBlockHeader hdr;
    std::memcpy(&hdr, block_4k, sizeof(hdr));

    StorageError err{};
    err.block_offset = static_cast<uint64_t>(expected_index) * VBLOCK;
    err.block_index = expected_index;
    err.expected_crc = 0;
    err.actual_crc = 0;
    err.first_diff_offset = 0;
    err.expected_byte = 0;
    err.actual_byte = 0;
    err.timestamp_secs = 0;

    // 1. Magic check
    if (hdr.magic != VERIFY_BLOCK_MAGIC) {
        err.type = ErrorType::MAGIC_MISMATCH;
        err.expected_byte = static_cast<uint8_t>(VERIFY_BLOCK_MAGIC & 0xFF);
        err.actual_byte = static_cast<uint8_t>(hdr.magic & 0xFF);
        return err;
    }

    // 2. Block index check
    if (hdr.block_index != expected_index) {
        err.type = ErrorType::BLOCK_INDEX_MISMATCH;
        return err;
    }

    // 3. CRC32C check
    const uint8_t* payload = block_4k + VERIFY_PAYLOAD_OFFSET;
    uint32_t actual_crc = utils::crc32c(payload, VERIFY_PAYLOAD_SIZE);
    if (actual_crc != hdr.payload_crc32) {
        err.type = ErrorType::CRC_MISMATCH;
        err.expected_crc = hdr.payload_crc32;
        err.actual_crc = actual_crc;
        return err;
    }

    // 4. Pattern verification — regenerate expected payload and byte-compare
    uint8_t expected_payload[VERIFY_PAYLOAD_SIZE];
    generate_pattern(expected_payload, VERIFY_PAYLOAD_SIZE, expected_pattern, expected_index);

    for (size_t i = 0; i < VERIFY_PAYLOAD_SIZE; ++i) {
        if (payload[i] != expected_payload[i]) {
            err.type = ErrorType::PATTERN_MISMATCH;
            err.first_diff_offset = VERIFY_PAYLOAD_OFFSET + i;
            err.expected_byte = expected_payload[i];
            err.actual_byte = payload[i];
            return err;
        }
    }

    return std::nullopt; // PASS
}

// ─── Report error (thread-safe, capped at 1000) ────────────────────────────

void StorageEngine::report_storage_error(const StorageError& err) {
    std::lock_guard<std::mutex> lk(metrics_mutex_);
    metrics_.verify_errors++;
    metrics_.error_count++;

    switch (err.type) {
        case ErrorType::CRC_MISMATCH:
            metrics_.crc_errors++;
            break;
        case ErrorType::MAGIC_MISMATCH:
            metrics_.magic_errors++;
            break;
        case ErrorType::BLOCK_INDEX_MISMATCH:
            metrics_.index_errors++;
            break;
        case ErrorType::PATTERN_MISMATCH:
            metrics_.pattern_errors++;
            break;
        case ErrorType::IO_ERROR:
            metrics_.io_errors++;
            break;
    }

    // Track error block range (first and last error block indices)
    uint64_t blk = static_cast<uint64_t>(err.block_index);
    if (metrics_.verify_errors == 1) {
        // First error — initialize both
        metrics_.first_error_block = blk;
        metrics_.last_error_block = blk;
    } else {
        if (blk < metrics_.first_error_block)
            metrics_.first_error_block = blk;
        if (blk > metrics_.last_error_block)
            metrics_.last_error_block = blk;
    }

    if (metrics_.error_log.size() < 1000) {
        metrics_.error_log.push_back(err);
    }
}

// ─── Flush and reopen file for read-only verification ───────────────────────

static void flush_file(intptr_t fd) {
#if defined(_WIN32)
    FlushFileBuffers(reinterpret_cast<HANDLE>(fd));
#else
    fsync(static_cast<int>(fd));
#endif
}

// ─── Helper: write a single block at offset ─────────────────────────────────

static bool write_block_at(intptr_t fd, const uint8_t* block, uint64_t offset) {
#if defined(_WIN32)
    OVERLAPPED ov{};
    ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
    ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD written = 0;
    BOOL ok = WriteFile(reinterpret_cast<HANDLE>(fd),
              block, static_cast<DWORD>(VBLOCK), &written, &ov);
    return ok && written == VBLOCK;
#else
    ssize_t ret = pwrite(static_cast<int>(fd), block, VBLOCK, static_cast<off_t>(offset));
    return ret == static_cast<ssize_t>(VBLOCK);
#endif
}

// ─── Helper: read a single block at offset ──────────────────────────────────

static bool read_block_at(intptr_t fd, uint8_t* block, uint64_t offset) {
#if defined(_WIN32)
    OVERLAPPED ov{};
    ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
    ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD bytes_read = 0;
    BOOL ok = ReadFile(reinterpret_cast<HANDLE>(fd),
             block, static_cast<DWORD>(VBLOCK), &bytes_read, &ov);
    return ok && bytes_read == VBLOCK;
#else
    ssize_t ret = pread(static_cast<int>(fd), block, VBLOCK, static_cast<off_t>(offset));
    return ret == static_cast<ssize_t>(VBLOCK);
#endif
}

// ─── Helper: update progress metrics (write phase 0-50%, verify phase 50-100%) ─

static constexpr int PATTERN_COUNT = 7;

static PatternID pattern_for_block(uint32_t block_index) {
    return static_cast<PatternID>(block_index % PATTERN_COUNT);
}

// =============================================================================
// VERIFY_SEQ — Sequential write all blocks → flush → sequential read+verify
// =============================================================================

void StorageEngine::verify_seq(const std::string& path, uint64_t file_size, int /*queue_depth*/) {
    const uint32_t total_blocks = static_cast<uint32_t>(file_size / VBLOCK);
    if (total_blocks == 0) return;
    const uint64_t dur = duration_secs_.load();

    auto test_start = std::chrono::steady_clock::now();
    auto get_ns = [&]() -> uint64_t {
        auto now = std::chrono::steady_clock::now();
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - test_start).count());
    };

    // ── Phase 1: Write ──────────────────────────────────────────────────────
    intptr_t fd = open_direct(path, false);
    if (fd < 0) {
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            last_error_ = "Failed to open test file: " + test_file_path_;
            metrics_.state = "error";
            metrics_.error_count++;
        }
        return;
    }

    uint8_t* block = alloc_aligned(VBLOCK);
    if (!block) { close_file(fd); return; }

    for (uint32_t i = 0; i < total_blocks && !stop_requested_.load(); ++i) {
        if (dur > 0) {
            double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - test_start).count();
            if (el >= static_cast<double>(dur)) { stop_requested_.store(true); break; }
        }

        PatternID pat = pattern_for_block(i);
        fill_verify_block(block, i, pat, 0, get_ns());

        uint64_t offset = static_cast<uint64_t>(i) * VBLOCK;
        if (!write_block_at(fd, block, offset)) {
            StorageError err{};
            err.block_offset = offset;
            err.block_index = i;
            err.type = ErrorType::IO_ERROR;
            err.timestamp_secs = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            report_storage_error(err);
            if (stop_on_error()) { stop_requested_.store(true); break; }
        }

        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.blocks_written++;
            double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            metrics_.elapsed_secs = elapsed;
            metrics_.write_mbs = elapsed > 0 ?
                (static_cast<double>(metrics_.blocks_written) * VBLOCK / (1024.0 * 1024.0)) / elapsed : 0;
            metrics_.progress_pct = (static_cast<double>(i + 1) / total_blocks) * 50.0;
        }
    }

    // ── Sync + reopen read-only ─────────────────────────────────────────────
    flush_file(fd);
    close_file(fd);

    if (stop_requested_.load()) { free_aligned(block); return; }

    fd = open_direct(path, true);
    if (fd < 0) {
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            last_error_ = "Failed to open test file: " + test_file_path_;
            metrics_.state = "error";
            metrics_.error_count++;
        }
        free_aligned(block);
        return;
    }

    // ── Phase 2: Verify ─────────────────────────────────────────────────────
    for (uint32_t i = 0; i < total_blocks && !stop_requested_.load(); ++i) {
        if (dur > 0) {
            double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - test_start).count();
            if (el >= static_cast<double>(dur)) { stop_requested_.store(true); break; }
        }

        uint64_t offset = static_cast<uint64_t>(i) * VBLOCK;
        if (!read_block_at(fd, block, offset)) {
            StorageError err{};
            err.block_offset = offset;
            err.block_index = i;
            err.type = ErrorType::IO_ERROR;
            err.timestamp_secs = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            report_storage_error(err);
            if (stop_on_error()) { stop_requested_.store(true); break; }
            continue;
        }

        PatternID pat = pattern_for_block(i);
        auto result = verify_block(block, i, pat);
        if (result) {
            result->timestamp_secs = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            report_storage_error(*result);
            if (stop_on_error()) { stop_requested_.store(true); break; }
        }

        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.blocks_verified++;
            double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            metrics_.elapsed_secs = elapsed;
            metrics_.verify_mbs = elapsed > 0 ?
                (static_cast<double>(metrics_.blocks_verified) * VBLOCK / (1024.0 * 1024.0)) / elapsed : 0;
            metrics_.progress_pct = 50.0 + (static_cast<double>(i + 1) / total_blocks) * 50.0;
        }
    }

    close_file(fd);
    free_aligned(block);
}

// =============================================================================
// VERIFY_RAND — Shuffled write → flush → same-order read+verify
// =============================================================================

void StorageEngine::verify_rand(const std::string& path, uint64_t file_size, int queue_depth) {
    const uint32_t total_blocks = static_cast<uint32_t>(file_size / VBLOCK);
    if (total_blocks == 0) return;
    const uint64_t dur = duration_secs_.load();

    auto test_start = std::chrono::steady_clock::now();
    auto get_ns = [&]() -> uint64_t {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - test_start).count());
    };

    // Generate shuffled index list with fixed seed for reproducibility
    std::vector<uint32_t> indices(total_blocks);
    std::iota(indices.begin(), indices.end(), 0);
    std::mt19937 gen(42); // fixed seed
    std::shuffle(indices.begin(), indices.end(), gen);

    // ── Phase 1: Write in shuffled order ────────────────────────────────────
    intptr_t fd = open_direct(path, false);
    if (fd < 0) {
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            last_error_ = "Failed to open test file: " + test_file_path_;
            metrics_.state = "error";
            metrics_.error_count++;
        }
        return;
    }

    uint8_t* block = alloc_aligned(VBLOCK);
    if (!block) { close_file(fd); return; }

    for (uint32_t n = 0; n < total_blocks && !stop_requested_.load(); ++n) {
        if (dur > 0) {
            double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - test_start).count();
            if (el >= static_cast<double>(dur)) { stop_requested_.store(true); break; }
        }

        uint32_t i = indices[n];
        PatternID pat = pattern_for_block(i);
        fill_verify_block(block, i, pat, 0, get_ns());

        uint64_t offset = static_cast<uint64_t>(i) * VBLOCK;
        if (!write_block_at(fd, block, offset)) {
            StorageError err{};
            err.block_offset = offset;
            err.block_index = i;
            err.type = ErrorType::IO_ERROR;
            err.timestamp_secs = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            report_storage_error(err);
            if (stop_on_error()) { stop_requested_.store(true); break; }
        }

        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.blocks_written++;
            double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            metrics_.elapsed_secs = elapsed;
            metrics_.write_mbs = elapsed > 0 ?
                (static_cast<double>(metrics_.blocks_written) * VBLOCK / (1024.0 * 1024.0)) / elapsed : 0;
            metrics_.progress_pct = (static_cast<double>(n + 1) / total_blocks) * 50.0;
        }
    }

    flush_file(fd);
    close_file(fd);

    if (stop_requested_.load()) { free_aligned(block); return; }

    // ── Phase 2: Read+verify in same shuffled order ─────────────────────────
    fd = open_direct(path, true);
    if (fd < 0) {
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            last_error_ = "Failed to open test file: " + test_file_path_;
            metrics_.state = "error";
            metrics_.error_count++;
        }
        free_aligned(block);
        return;
    }

    for (uint32_t n = 0; n < total_blocks && !stop_requested_.load(); ++n) {
        if (dur > 0) {
            double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - test_start).count();
            if (el >= static_cast<double>(dur)) { stop_requested_.store(true); break; }
        }

        uint32_t i = indices[n];
        uint64_t offset = static_cast<uint64_t>(i) * VBLOCK;

        if (!read_block_at(fd, block, offset)) {
            StorageError err{};
            err.block_offset = offset;
            err.block_index = i;
            err.type = ErrorType::IO_ERROR;
            err.timestamp_secs = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            report_storage_error(err);
            if (stop_on_error()) { stop_requested_.store(true); break; }
            continue;
        }

        PatternID pat = pattern_for_block(i);
        auto result = verify_block(block, i, pat);
        if (result) {
            result->timestamp_secs = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            report_storage_error(*result);
            if (stop_on_error()) { stop_requested_.store(true); break; }
        }

        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.blocks_verified++;
            double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            metrics_.elapsed_secs = elapsed;
            metrics_.verify_mbs = elapsed > 0 ?
                (static_cast<double>(metrics_.blocks_verified) * VBLOCK / (1024.0 * 1024.0)) / elapsed : 0;
            metrics_.progress_pct = 50.0 + (static_cast<double>(n + 1) / total_blocks) * 50.0;
        }
    }

    close_file(fd);
    free_aligned(block);
}

// =============================================================================
// FILL_VERIFY — H2testw-style: fill entire file → verify all (fake USB/SSD detection)
// =============================================================================

void StorageEngine::fill_verify(const std::string& path, uint64_t file_size, int /*queue_depth*/) {
    const uint32_t total_blocks = static_cast<uint32_t>(file_size / VBLOCK);
    if (total_blocks == 0) return;
    const uint64_t dur = duration_secs_.load();

    auto test_start = std::chrono::steady_clock::now();
    auto get_ns = [&]() -> uint64_t {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - test_start).count());
    };

    // ── Phase 1: Sequential fill with cycling patterns ──────────────────────
    intptr_t fd = open_direct(path, false);
    if (fd < 0) {
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            last_error_ = "Failed to open test file: " + test_file_path_;
            metrics_.state = "error";
            metrics_.error_count++;
        }
        return;
    }

    uint8_t* block = alloc_aligned(VBLOCK);
    if (!block) { close_file(fd); return; }

    for (uint32_t i = 0; i < total_blocks && !stop_requested_.load(); ++i) {
        if (dur > 0) {
            double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - test_start).count();
            if (el >= static_cast<double>(dur)) { stop_requested_.store(true); break; }
        }

        // Cycle through all pattern types
        PatternID pat = static_cast<PatternID>(i % PATTERN_COUNT);
        fill_verify_block(block, i, pat, 0, get_ns());

        uint64_t offset = static_cast<uint64_t>(i) * VBLOCK;
        if (!write_block_at(fd, block, offset)) {
            StorageError err{};
            err.block_offset = offset;
            err.block_index = i;
            err.type = ErrorType::IO_ERROR;
            err.timestamp_secs = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            report_storage_error(err);
            if (stop_on_error()) { stop_requested_.store(true); break; }
        }

        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.blocks_written++;
            double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            metrics_.elapsed_secs = elapsed;
            metrics_.write_mbs = elapsed > 0 ?
                (static_cast<double>(metrics_.blocks_written) * VBLOCK / (1024.0 * 1024.0)) / elapsed : 0;
            metrics_.progress_pct = (static_cast<double>(i + 1) / total_blocks) * 50.0;
        }
    }

    flush_file(fd);
    close_file(fd);

    if (stop_requested_.load()) { free_aligned(block); return; }

    // ── Phase 2: Sequential verify all ──────────────────────────────────────
    fd = open_direct(path, true);
    if (fd < 0) {
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            last_error_ = "Failed to open test file: " + test_file_path_;
            metrics_.state = "error";
            metrics_.error_count++;
        }
        free_aligned(block);
        return;
    }

    for (uint32_t i = 0; i < total_blocks && !stop_requested_.load(); ++i) {
        if (dur > 0) {
            double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - test_start).count();
            if (el >= static_cast<double>(dur)) { stop_requested_.store(true); break; }
        }

        uint64_t offset = static_cast<uint64_t>(i) * VBLOCK;
        if (!read_block_at(fd, block, offset)) {
            StorageError err{};
            err.block_offset = offset;
            err.block_index = i;
            err.type = ErrorType::IO_ERROR;
            err.timestamp_secs = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            report_storage_error(err);
            if (stop_on_error()) { stop_requested_.store(true); break; }
            continue;
        }

        PatternID pat = static_cast<PatternID>(i % PATTERN_COUNT);
        auto result = verify_block(block, i, pat);
        if (result) {
            result->timestamp_secs = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            report_storage_error(*result);
            if (stop_on_error()) { stop_requested_.store(true); break; }
        }

        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.blocks_verified++;
            double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            metrics_.elapsed_secs = elapsed;
            metrics_.verify_mbs = elapsed > 0 ?
                (static_cast<double>(metrics_.blocks_verified) * VBLOCK / (1024.0 * 1024.0)) / elapsed : 0;
            metrics_.progress_pct = 50.0 + (static_cast<double>(i + 1) / total_blocks) * 50.0;
        }
    }

    close_file(fd);
    free_aligned(block);
}

// =============================================================================
// BUTTERFLY — Converging write from both ends → sequential verify
// =============================================================================

void StorageEngine::butterfly_verify(const std::string& path, uint64_t file_size, int /*queue_depth*/) {
    const uint32_t total_blocks = static_cast<uint32_t>(file_size / VBLOCK);
    if (total_blocks == 0) return;
    const uint64_t dur = duration_secs_.load();

    auto test_start = std::chrono::steady_clock::now();
    auto get_ns = [&]() -> uint64_t {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - test_start).count());
    };

    // ── Phase 1: Converging write — low goes up, high goes down ─────────────
    intptr_t fd = open_direct(path, false);
    if (fd < 0) {
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            last_error_ = "Failed to open test file: " + test_file_path_;
            metrics_.state = "error";
            metrics_.error_count++;
        }
        return;
    }

    uint8_t* block_lo = alloc_aligned(VBLOCK);
    uint8_t* block_hi = alloc_aligned(VBLOCK);
    if (!block_lo || !block_hi) {
        free_aligned(block_lo);
        free_aligned(block_hi);
        close_file(fd);
        return;
    }

    uint32_t low = 0;
    uint32_t high = total_blocks - 1;
    uint32_t writes_done = 0;

    while (low <= high && !stop_requested_.load()) {
        if (dur > 0) {
            double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - test_start).count();
            if (el >= static_cast<double>(dur)) { stop_requested_.store(true); break; }
        }

        // Write low block
        PatternID pat_lo = pattern_for_block(low);
        fill_verify_block(block_lo, low, pat_lo, 0, get_ns());
        uint64_t off_lo = static_cast<uint64_t>(low) * VBLOCK;
        if (!write_block_at(fd, block_lo, off_lo)) {
            StorageError err{};
            err.block_offset = off_lo;
            err.block_index = low;
            err.type = ErrorType::IO_ERROR;
            err.timestamp_secs = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            report_storage_error(err);
            if (stop_on_error()) { stop_requested_.store(true); break; }
        }
        writes_done++;

        // Write high block (if different from low)
        if (low != high) {
            PatternID pat_hi = pattern_for_block(high);
            fill_verify_block(block_hi, high, pat_hi, 0, get_ns());
            uint64_t off_hi = static_cast<uint64_t>(high) * VBLOCK;
            if (!write_block_at(fd, block_hi, off_hi)) {
                StorageError err{};
                err.block_offset = off_hi;
                err.block_index = high;
                err.type = ErrorType::IO_ERROR;
                err.timestamp_secs = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - test_start).count();
                report_storage_error(err);
                if (stop_on_error()) { stop_requested_.store(true); break; }
            }
            writes_done++;
        }

        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.blocks_written = writes_done;
            double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            metrics_.elapsed_secs = elapsed;
            metrics_.write_mbs = elapsed > 0 ?
                (static_cast<double>(writes_done) * VBLOCK / (1024.0 * 1024.0)) / elapsed : 0;
            metrics_.progress_pct = (static_cast<double>(writes_done) / total_blocks) * 50.0;
        }

        low++;
        if (high == 0) break;
        high--;
    }

    flush_file(fd);
    close_file(fd);
    free_aligned(block_hi);

    if (stop_requested_.load()) { free_aligned(block_lo); return; }

    // ── Phase 2: Sequential verify ──────────────────────────────────────────
    fd = open_direct(path, true);
    if (fd < 0) {
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            last_error_ = "Failed to open test file: " + test_file_path_;
            metrics_.state = "error";
            metrics_.error_count++;
        }
        free_aligned(block_lo);
        return;
    }

    for (uint32_t i = 0; i < total_blocks && !stop_requested_.load(); ++i) {
        if (dur > 0) {
            double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - test_start).count();
            if (el >= static_cast<double>(dur)) { stop_requested_.store(true); break; }
        }

        uint64_t offset = static_cast<uint64_t>(i) * VBLOCK;
        if (!read_block_at(fd, block_lo, offset)) {
            StorageError err{};
            err.block_offset = offset;
            err.block_index = i;
            err.type = ErrorType::IO_ERROR;
            err.timestamp_secs = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            report_storage_error(err);
            if (stop_on_error()) { stop_requested_.store(true); break; }
            continue;
        }

        PatternID pat = pattern_for_block(i);
        auto result = verify_block(block_lo, i, pat);
        if (result) {
            result->timestamp_secs = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            report_storage_error(*result);
            if (stop_on_error()) { stop_requested_.store(true); break; }
        }

        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.blocks_verified++;
            double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - test_start).count();
            metrics_.elapsed_secs = elapsed;
            metrics_.verify_mbs = elapsed > 0 ?
                (static_cast<double>(metrics_.blocks_verified) * VBLOCK / (1024.0 * 1024.0)) / elapsed : 0;
            metrics_.progress_pct = 50.0 + (static_cast<double>(i + 1) / total_blocks) * 50.0;
        }
    }

    close_file(fd);
    free_aligned(block_lo);
}

} // namespace occt
