#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace occt {

// ─── Pattern identification ─────────────────────────────────────────────────

enum class PatternID : uint32_t {
    SEQUENTIAL     = 0,
    CHECKERBOARD   = 1,
    WALKING_ONES   = 2,
    WALKING_ZEROS  = 3,
    RANDOM         = 4,
    ALL_ONES       = 5,
    ALL_ZEROS      = 6
};

// ─── Block header (32 bytes, placed at start of every 4K block) ─────────────

struct VerifyBlockHeader {
    uint32_t magic;           // 0x4F435654 ("OCVT")
    uint32_t block_index;     // Block number within file
    uint32_t pattern_id;      // PatternID enum value
    uint32_t payload_crc32;   // CRC32C of bytes [32..4095]
    uint64_t write_timestamp; // Nanoseconds since test start
    uint32_t pass_number;     // Pass number for multi-pass tests
    uint32_t reserved;        // Must be 0
};

static_assert(sizeof(VerifyBlockHeader) == 32, "VerifyBlockHeader must be exactly 32 bytes");

static constexpr uint32_t VERIFY_BLOCK_MAGIC = 0x4F435654u; // "OCVT"
static constexpr size_t   VERIFY_BLOCK_SIZE  = 4096;
static constexpr size_t   VERIFY_PAYLOAD_OFFSET = sizeof(VerifyBlockHeader); // 32
static constexpr size_t   VERIFY_PAYLOAD_SIZE   = VERIFY_BLOCK_SIZE - VERIFY_PAYLOAD_OFFSET; // 4064

// ─── Error types ────────────────────────────────────────────────────────────

enum class ErrorType : uint32_t {
    CRC_MISMATCH        = 0,
    MAGIC_MISMATCH      = 1,
    BLOCK_INDEX_MISMATCH = 2,
    PATTERN_MISMATCH    = 3,
    IO_ERROR            = 4,
};

struct StorageError {
    uint64_t  block_offset;       // Byte offset in file
    uint32_t  block_index;        // Expected block index
    ErrorType type;
    uint32_t  expected_crc;
    uint32_t  actual_crc;
    uint64_t  first_diff_offset;  // Offset of first mismatch within block
    uint8_t   expected_byte;
    uint8_t   actual_byte;
    double    timestamp_secs;     // Seconds since test start
};

} // namespace occt
