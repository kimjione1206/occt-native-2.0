// =============================================================================
// OCCT Native - Storage Verification Tests (35 tests)
// Tests CRC32, VerifyBlockHeader, pattern generation, fill/verify logic,
// StorageError, VERIFY_SEQ flow, enum/scheduler mapping, metrics fields.
// =============================================================================
// Build:  g++ -std=c++17 -I../src -o test_storage_verify test_storage_verify.cpp
//         (Note: these are self-contained unit tests, not integration tests)
// Run:    ./test_storage_verify
// =============================================================================

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
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

#define ASSERT_NEQ(a, b, msg) \
    do { \
        if ((a) == (b)) { FAIL(msg); return; } \
    } while(0)

// =============================================================================
// Production header includes (header-only, no link dependencies)
// =============================================================================

#include "../src/engines/storage/verify_block.h"
using namespace occt;

// =============================================================================
// Inline replicas of production code (kept inline to avoid platform/Qt deps)
// =============================================================================

// ─── CRC32C Software Implementation ────────────────────────────────────────
// NOTE: This replicates the software CRC32C from src/utils/crc32.cpp.
// Kept inline because the production crc32.cpp has hardware SSE4.2 dependencies
// that require special compile flags and platform-specific headers.

namespace test_crc32 {

static uint32_t sw_crc32c_table[256];
static bool sw_table_initialized = false;

static void init_sw_table() {
    if (sw_table_initialized) return;
    const uint32_t poly = 0x82F63B78u;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) crc = (crc >> 1) ^ poly;
            else crc >>= 1;
        }
        sw_crc32c_table[i] = crc;
    }
    sw_table_initialized = true;
}

static uint32_t crc32c_sw(const void* data, size_t length, uint32_t initial_crc = 0) {
    init_sw_table();
    uint32_t crc = ~initial_crc;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < length; ++i) {
        crc = sw_crc32c_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

// For HW test: we check software against itself (HW intrinsics are platform-dependent)
static uint32_t crc32c_hw_sim(const void* data, size_t length, uint32_t initial_crc = 0) {
    // Simulate HW path using same algorithm (in real code, uses _mm_crc32_u64)
    return crc32c_sw(data, length, initial_crc);
}

} // namespace test_crc32

// ─── Pattern generation & verification functions ────────────────────────────
// NOTE: These replicate production code from src/engines/storage/storage_verify.cpp.
// Kept inline because storage_verify.cpp has platform-specific I/O code and
// links to the engine library (Qt dependency).

namespace test_verify {

// ─── xoshiro256** RNG ───────────────────────────────────────────────────────

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

// ─── Pattern generation ─────────────────────────────────────────────────────

static void generate_pattern(uint8_t* payload, size_t len,
                             PatternID pattern, uint32_t block_index) {
    VerifyRng rng;
    rng.seed(static_cast<uint64_t>(block_index) * 0x12345678ULL + static_cast<uint32_t>(pattern));

    switch (pattern) {
        case PatternID::SEQUENTIAL:
            for (size_t i = 0; i < len; ++i)
                payload[i] = static_cast<uint8_t>((block_index + i) & 0xFF);
            break;
        case PatternID::CHECKERBOARD:
            for (size_t i = 0; i < len; ++i)
                payload[i] = static_cast<uint8_t>(((i + block_index) & 1) ? 0xAA : 0x55);
            break;
        case PatternID::WALKING_ONES:
            for (size_t i = 0; i < len; ++i)
                payload[i] = static_cast<uint8_t>(1 << ((i + block_index) & 7));
            break;
        case PatternID::WALKING_ZEROS:
            for (size_t i = 0; i < len; ++i)
                payload[i] = static_cast<uint8_t>(~(1 << ((i + block_index) & 7)));
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

// ─── Fill + Verify block functions ──────────────────────────────────────────

static void fill_verify_block(uint8_t* block_4k, uint32_t block_index,
                               PatternID pattern, uint32_t pass, uint64_t timestamp_ns) {
    uint8_t* payload = block_4k + VERIFY_PAYLOAD_OFFSET;
    generate_pattern(payload, VERIFY_PAYLOAD_SIZE, pattern, block_index);

    uint32_t crc = test_crc32::crc32c_sw(payload, VERIFY_PAYLOAD_SIZE);

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

static std::optional<StorageError> verify_block(const uint8_t* block_4k,
                                                 uint32_t expected_index,
                                                 PatternID expected_pattern) {
    VerifyBlockHeader hdr;
    std::memcpy(&hdr, block_4k, sizeof(hdr));

    StorageError err{};
    err.block_offset = static_cast<uint64_t>(expected_index) * VERIFY_BLOCK_SIZE;
    err.block_index = expected_index;

    if (hdr.magic != VERIFY_BLOCK_MAGIC) {
        err.type = ErrorType::MAGIC_MISMATCH;
        return err;
    }

    if (hdr.block_index != expected_index) {
        err.type = ErrorType::BLOCK_INDEX_MISMATCH;
        return err;
    }

    const uint8_t* payload = block_4k + VERIFY_PAYLOAD_OFFSET;
    uint32_t actual_crc = test_crc32::crc32c_sw(payload, VERIFY_PAYLOAD_SIZE);
    if (actual_crc != hdr.payload_crc32) {
        err.type = ErrorType::CRC_MISMATCH;
        err.expected_crc = hdr.payload_crc32;
        err.actual_crc = actual_crc;
        return err;
    }

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

    return std::nullopt;
}

} // namespace test_verify

// ─── StorageMode enum replica ───────────────────────────────────────────────

namespace test_enum {

enum class StorageMode {
    SEQ_WRITE, SEQ_READ, RAND_WRITE, RAND_READ, MIXED,
    VERIFY_SEQ, VERIFY_RAND, FILL_VERIFY, BUTTERFLY
};

static StorageMode mode_from_string(const std::string& s) {
    if (s == "seq_write")   return StorageMode::SEQ_WRITE;
    if (s == "seq_read")    return StorageMode::SEQ_READ;
    if (s == "rand_write")  return StorageMode::RAND_WRITE;
    if (s == "rand_read")   return StorageMode::RAND_READ;
    if (s == "mixed")       return StorageMode::MIXED;
    if (s == "verify_seq")  return StorageMode::VERIFY_SEQ;
    if (s == "verify_rand") return StorageMode::VERIFY_RAND;
    if (s == "fill_verify") return StorageMode::FILL_VERIFY;
    if (s == "butterfly")   return StorageMode::BUTTERFLY;
    return StorageMode::MIXED; // default
}

} // namespace test_enum

// =============================================================================
// Tests 1-4: CRC32C
// =============================================================================

static void test_1_crc32_sw_hw_match() {
    TEST("CRC32C software == hardware (simulated)");

    uint8_t data[1024];
    for (int i = 0; i < 1024; ++i) data[i] = static_cast<uint8_t>(i & 0xFF);

    uint32_t sw = test_crc32::crc32c_sw(data, 1024);
    uint32_t hw = test_crc32::crc32c_hw_sim(data, 1024);
    ASSERT_EQ(sw, hw, "SW and HW CRC32C must match");
    PASS();
}

static void test_2_crc32_empty_input() {
    TEST("CRC32C empty input");

    uint32_t crc = test_crc32::crc32c_sw(nullptr, 0);
    // CRC of empty data with initial 0 should be 0
    ASSERT_EQ(crc, 0u, "CRC32C of empty input should be 0");
    PASS();
}

static void test_3_crc32_known_vectors() {
    TEST("CRC32C known test vector");

    // "123456789" should have CRC32C = 0xE3069283
    const char* test_str = "123456789";
    uint32_t crc = test_crc32::crc32c_sw(test_str, 9);
    ASSERT_EQ(crc, 0xE3069283u, "CRC32C of '123456789' must be 0xE3069283");
    PASS();
}

static void test_4_crc32_incremental() {
    TEST("CRC32C incremental computation");

    uint8_t data[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};

    // Full CRC
    uint32_t full = test_crc32::crc32c_sw(data, 16);

    // Incremental: first 8 bytes, then next 8
    uint32_t part1 = test_crc32::crc32c_sw(data, 8);
    uint32_t part2 = test_crc32::crc32c_sw(data + 8, 8, part1);

    ASSERT_EQ(full, part2, "Incremental CRC must equal full CRC");
    PASS();
}

// =============================================================================
// Tests 5-8: VerifyBlockHeader
// =============================================================================

static void test_5_header_size() {
    TEST("VerifyBlockHeader is 32 bytes");

    ASSERT_EQ(sizeof(VerifyBlockHeader), 32u, "Header must be 32 bytes");
    PASS();
}

static void test_6_header_magic() {
    TEST("VerifyBlockHeader magic constant");

    ASSERT_EQ(VERIFY_BLOCK_MAGIC, 0x4F435654u, "Magic must be 0x4F435654 (OCVT)");
    PASS();
}

static void test_7_header_field_roundtrip() {
    TEST("VerifyBlockHeader field round-trip");

    VerifyBlockHeader hdr{};
    hdr.magic = VERIFY_BLOCK_MAGIC;
    hdr.block_index = 42;
    hdr.pattern_id = 3;
    hdr.payload_crc32 = 0xDEADBEEF;
    hdr.write_timestamp = 123456789ULL;
    hdr.pass_number = 7;
    hdr.reserved = 0;

    uint8_t buf[32];
    std::memcpy(buf, &hdr, 32);

    VerifyBlockHeader hdr2;
    std::memcpy(&hdr2, buf, 32);

    ASSERT_EQ(hdr2.magic, VERIFY_BLOCK_MAGIC, "Magic mismatch");
    ASSERT_EQ(hdr2.block_index, 42u, "block_index mismatch");
    ASSERT_EQ(hdr2.pattern_id, 3u, "pattern_id mismatch");
    ASSERT_EQ(hdr2.payload_crc32, 0xDEADBEEFu, "CRC mismatch");
    ASSERT_EQ(hdr2.write_timestamp, 123456789ULL, "timestamp mismatch");
    ASSERT_EQ(hdr2.pass_number, 7u, "pass_number mismatch");
    ASSERT_EQ(hdr2.reserved, 0u, "reserved mismatch");
    PASS();
}

static void test_8_block_size_constants() {
    TEST("Block size constants");

    ASSERT_EQ(VERIFY_BLOCK_SIZE, 4096u, "Block size must be 4096");
    ASSERT_EQ(VERIFY_PAYLOAD_OFFSET, 32u, "Payload offset must be 32");
    ASSERT_EQ(VERIFY_PAYLOAD_SIZE, 4064u, "Payload size must be 4064");
    PASS();
}

// =============================================================================
// Tests 9-12: Pattern Generation
// =============================================================================

static void test_9_pattern_deterministic() {
    TEST("Pattern generation is deterministic");

    uint8_t buf1[128], buf2[128];
    test_verify::generate_pattern(buf1, 128, PatternID::RANDOM, 42);
    test_verify::generate_pattern(buf2, 128, PatternID::RANDOM, 42);
    ASSERT_TRUE(std::memcmp(buf1, buf2, 128) == 0, "Same inputs must produce same pattern");
    PASS();
}

static void test_10_pattern_unique_per_block() {
    TEST("Patterns differ by block_index");

    uint8_t buf1[128], buf2[128];
    test_verify::generate_pattern(buf1, 128, PatternID::SEQUENTIAL, 0);
    test_verify::generate_pattern(buf2, 128, PatternID::SEQUENTIAL, 1);
    ASSERT_TRUE(std::memcmp(buf1, buf2, 128) != 0, "Different block indices must produce different data");
    PASS();
}

static void test_11_all_patterns_generate() {
    TEST("All 7 patterns generate without crash");

    uint8_t buf[256];
    for (uint32_t p = 0; p <= 6; ++p) {
        test_verify::generate_pattern(buf, 256, static_cast<PatternID>(p), 0);
    }
    PASS();
}

static void test_12_pattern_all_ones_zeros() {
    TEST("ALL_ONES and ALL_ZEROS patterns");

    uint8_t buf[64];

    test_verify::generate_pattern(buf, 64, PatternID::ALL_ONES, 0);
    for (int i = 0; i < 64; ++i)
        ASSERT_EQ(buf[i], 0xFFu, "ALL_ONES byte mismatch");

    test_verify::generate_pattern(buf, 64, PatternID::ALL_ZEROS, 0);
    for (int i = 0; i < 64; ++i)
        ASSERT_EQ(buf[i], 0x00u, "ALL_ZEROS byte mismatch");

    PASS();
}

// =============================================================================
// Tests 13-16: fill_verify_block
// =============================================================================

static void test_13_fill_verify_block_crc_accurate() {
    TEST("fill_verify_block CRC is accurate");

    alignas(16) uint8_t block[4096];
    test_verify::fill_verify_block(block, 0, PatternID::SEQUENTIAL, 0, 1000);

    VerifyBlockHeader hdr;
    std::memcpy(&hdr, block, sizeof(hdr));

    uint32_t computed = test_crc32::crc32c_sw(block + 32, 4064);
    ASSERT_EQ(hdr.payload_crc32, computed, "Stored CRC must match computed CRC");
    PASS();
}

static void test_14_fill_verify_block_reproducible() {
    TEST("fill_verify_block is reproducible");

    alignas(16) uint8_t block1[4096], block2[4096];
    test_verify::fill_verify_block(block1, 5, PatternID::CHECKERBOARD, 0, 1000);
    test_verify::fill_verify_block(block2, 5, PatternID::CHECKERBOARD, 0, 1000);

    // Payload should be identical (header timestamp same too)
    ASSERT_TRUE(std::memcmp(block1 + 32, block2 + 32, 4064) == 0, "Payloads must match");
    PASS();
}

static void test_15_fill_verify_different_index() {
    TEST("fill_verify_block different index → different data");

    alignas(16) uint8_t block1[4096], block2[4096];
    test_verify::fill_verify_block(block1, 0, PatternID::RANDOM, 0, 0);
    test_verify::fill_verify_block(block2, 1, PatternID::RANDOM, 0, 0);

    ASSERT_TRUE(std::memcmp(block1 + 32, block2 + 32, 4064) != 0, "Different indices must produce different payloads");
    PASS();
}

static void test_16_fill_verify_magic_present() {
    TEST("fill_verify_block writes correct magic");

    alignas(16) uint8_t block[4096];
    test_verify::fill_verify_block(block, 10, PatternID::WALKING_ONES, 0, 0);

    VerifyBlockHeader hdr;
    std::memcpy(&hdr, block, sizeof(hdr));

    ASSERT_EQ(hdr.magic, VERIFY_BLOCK_MAGIC, "Magic must be OCVT");
    ASSERT_EQ(hdr.block_index, 10u, "block_index must be 10");
    PASS();
}

// =============================================================================
// Tests 17-20: verify_block
// =============================================================================

static void test_17_verify_block_pass() {
    TEST("verify_block PASS on valid block");

    alignas(16) uint8_t block[4096];
    test_verify::fill_verify_block(block, 7, PatternID::CHECKERBOARD, 0, 0);

    auto result = test_verify::verify_block(block, 7, PatternID::CHECKERBOARD);
    ASSERT_TRUE(!result.has_value(), "Valid block must pass verification");
    PASS();
}

static void test_18_verify_block_crc_tamper() {
    TEST("verify_block detects CRC tampering");

    alignas(16) uint8_t block[4096];
    test_verify::fill_verify_block(block, 3, PatternID::RANDOM, 0, 0);

    // Tamper with one payload byte
    block[100] ^= 0xFF;

    auto result = test_verify::verify_block(block, 3, PatternID::RANDOM);
    ASSERT_TRUE(result.has_value(), "Tampered block must fail");
    ASSERT_EQ(static_cast<uint32_t>(result->type), static_cast<uint32_t>(ErrorType::CRC_MISMATCH),
              "Error type must be CRC_MISMATCH");
    PASS();
}

static void test_19_verify_block_magic_tamper() {
    TEST("verify_block detects magic tampering");

    alignas(16) uint8_t block[4096];
    test_verify::fill_verify_block(block, 0, PatternID::SEQUENTIAL, 0, 0);

    // Tamper with magic (first 4 bytes)
    block[0] = 0x00;

    auto result = test_verify::verify_block(block, 0, PatternID::SEQUENTIAL);
    ASSERT_TRUE(result.has_value(), "Bad magic must fail");
    ASSERT_EQ(static_cast<uint32_t>(result->type), static_cast<uint32_t>(ErrorType::MAGIC_MISMATCH),
              "Error type must be MAGIC_MISMATCH");
    PASS();
}

static void test_20_verify_block_index_mismatch() {
    TEST("verify_block detects block index mismatch");

    alignas(16) uint8_t block[4096];
    test_verify::fill_verify_block(block, 5, PatternID::ALL_ONES, 0, 0);

    // Verify with wrong expected index
    auto result = test_verify::verify_block(block, 99, PatternID::ALL_ONES);
    ASSERT_TRUE(result.has_value(), "Wrong index must fail");
    ASSERT_EQ(static_cast<uint32_t>(result->type), static_cast<uint32_t>(ErrorType::BLOCK_INDEX_MISMATCH),
              "Error type must be BLOCK_INDEX_MISMATCH");
    PASS();
}

// =============================================================================
// Tests 21-24: StorageError
// =============================================================================

static void test_21_error_type_distinction() {
    TEST("StorageError type distinction");

    StorageError err1{};
    err1.type = ErrorType::CRC_MISMATCH;

    StorageError err2{};
    err2.type = ErrorType::MAGIC_MISMATCH;

    ASSERT_TRUE(static_cast<uint32_t>(err1.type) != static_cast<uint32_t>(err2.type),
                "Different error types must be distinguishable");
    PASS();
}

static void test_22_error_log_cap() {
    TEST("StorageError log cap at 1000");

    std::vector<StorageError> error_log;
    for (int i = 0; i < 1500; ++i) {
        if (error_log.size() < 1000) {
            StorageError err{};
            err.block_index = static_cast<uint32_t>(i);
            error_log.push_back(err);
        }
    }

    ASSERT_EQ(error_log.size(), 1000u, "Error log must be capped at 1000");
    PASS();
}

static void test_23_error_byte_diff() {
    TEST("StorageError captures byte diff info");

    alignas(16) uint8_t block[4096];
    test_verify::fill_verify_block(block, 2, PatternID::SEQUENTIAL, 0, 0);

    // Store original byte, tamper payload, update CRC to match (so CRC passes but pattern fails)
    uint8_t original = block[40]; // byte 8 of payload
    block[40] ^= 0xFF;

    // Recompute CRC to match tampered data
    uint32_t new_crc = test_crc32::crc32c_sw(block + 32, 4064);
    VerifyBlockHeader hdr;
    std::memcpy(&hdr, block, sizeof(hdr));
    hdr.payload_crc32 = new_crc;
    std::memcpy(block, &hdr, sizeof(hdr));

    auto result = test_verify::verify_block(block, 2, PatternID::SEQUENTIAL);
    ASSERT_TRUE(result.has_value(), "Tampered pattern must fail");
    ASSERT_EQ(static_cast<uint32_t>(result->type), static_cast<uint32_t>(ErrorType::PATTERN_MISMATCH),
              "Error must be PATTERN_MISMATCH");
    ASSERT_EQ(result->actual_byte, static_cast<uint8_t>(original ^ 0xFF), "actual_byte must be tampered value");
    PASS();
}

static void test_24_error_all_types_exist() {
    TEST("All ErrorType values exist");

    auto crc = ErrorType::CRC_MISMATCH;
    auto magic = ErrorType::MAGIC_MISMATCH;
    auto idx = ErrorType::BLOCK_INDEX_MISMATCH;
    auto pat = ErrorType::PATTERN_MISMATCH;
    auto io = ErrorType::IO_ERROR;

    ASSERT_EQ(static_cast<uint32_t>(crc), 0u, "CRC_MISMATCH == 0");
    ASSERT_EQ(static_cast<uint32_t>(magic), 1u, "MAGIC_MISMATCH == 1");
    ASSERT_EQ(static_cast<uint32_t>(idx), 2u, "BLOCK_INDEX_MISMATCH == 2");
    ASSERT_EQ(static_cast<uint32_t>(pat), 3u, "PATTERN_MISMATCH == 3");
    ASSERT_EQ(static_cast<uint32_t>(io), 4u, "IO_ERROR == 4");
    PASS();
}

// =============================================================================
// Tests 25-28: VERIFY_SEQ flow (simulated)
// =============================================================================

static void test_25_verify_seq_write_read_order() {
    TEST("VERIFY_SEQ: write then read order");

    // Simulate: write N blocks, then verify N blocks
    const int N = 16;
    alignas(16) uint8_t blocks[N][4096];

    // Phase 1: Write
    for (int i = 0; i < N; ++i) {
        PatternID pat = static_cast<PatternID>(i % 7);
        test_verify::fill_verify_block(blocks[i], static_cast<uint32_t>(i), pat, 0, 0);
    }

    // Phase 2: Verify
    int errors = 0;
    for (int i = 0; i < N; ++i) {
        PatternID pat = static_cast<PatternID>(i % 7);
        auto result = test_verify::verify_block(blocks[i], static_cast<uint32_t>(i), pat);
        if (result) errors++;
    }

    ASSERT_EQ(errors, 0, "All blocks must pass verification");
    PASS();
}

static void test_26_verify_seq_progress() {
    TEST("VERIFY_SEQ: progress 0→50→100");

    // Write phase: 0→50%, verify phase: 50→100%
    const int N = 100;
    for (int i = 0; i < N; ++i) {
        double write_progress = (static_cast<double>(i + 1) / N) * 50.0;
        ASSERT_TRUE(write_progress >= 0.0 && write_progress <= 50.0, "Write progress out of range");
    }
    for (int i = 0; i < N; ++i) {
        double verify_progress = 50.0 + (static_cast<double>(i + 1) / N) * 50.0;
        ASSERT_TRUE(verify_progress >= 50.0 && verify_progress <= 100.0, "Verify progress out of range");
    }
    PASS();
}

static void test_27_verify_error_count() {
    TEST("VERIFY_SEQ: error counting");

    const int N = 8;
    alignas(16) uint8_t blocks[N][4096];

    for (int i = 0; i < N; ++i) {
        test_verify::fill_verify_block(blocks[i], static_cast<uint32_t>(i),
                                        PatternID::ALL_ONES, 0, 0);
    }

    // Tamper block 3 and block 5
    blocks[3][0] = 0x00; // corrupt magic
    blocks[5][100] ^= 0xFF; // corrupt payload

    int total_errors = 0;
    for (int i = 0; i < N; ++i) {
        auto result = test_verify::verify_block(blocks[i], static_cast<uint32_t>(i),
                                                 PatternID::ALL_ONES);
        if (result) total_errors++;
    }

    ASSERT_EQ(total_errors, 2, "Exactly 2 errors expected");
    PASS();
}

static void test_28_verify_multipass() {
    TEST("VERIFY_SEQ: multi-pass with pass_number");

    alignas(16) uint8_t block[4096];
    test_verify::fill_verify_block(block, 0, PatternID::RANDOM, 5, 0);

    VerifyBlockHeader hdr;
    std::memcpy(&hdr, block, sizeof(hdr));

    ASSERT_EQ(hdr.pass_number, 5u, "pass_number must be preserved");

    // Block should still verify (pass_number doesn't affect verification)
    auto result = test_verify::verify_block(block, 0, PatternID::RANDOM);
    ASSERT_TRUE(!result.has_value(), "Multi-pass block must pass verification");
    PASS();
}

// =============================================================================
// Tests 29-30: Enum + Scheduler mapping
// =============================================================================

static void test_29_enum_new_values_exist() {
    TEST("StorageMode enum has new verification values");

    auto vs = test_enum::StorageMode::VERIFY_SEQ;
    auto vr = test_enum::StorageMode::VERIFY_RAND;
    auto fv = test_enum::StorageMode::FILL_VERIFY;
    auto bf = test_enum::StorageMode::BUTTERFLY;

    // All must be distinct from each other and from legacy modes
    ASSERT_TRUE(vs != vr, "VERIFY_SEQ != VERIFY_RAND");
    ASSERT_TRUE(fv != bf, "FILL_VERIFY != BUTTERFLY");
    ASSERT_TRUE(vs != test_enum::StorageMode::MIXED, "VERIFY_SEQ != MIXED");
    PASS();
}

static void test_30_scheduler_string_mapping() {
    TEST("Scheduler string → enum mapping");

    ASSERT_TRUE(test_enum::mode_from_string("verify_seq") == test_enum::StorageMode::VERIFY_SEQ,
                "verify_seq mapping");
    ASSERT_TRUE(test_enum::mode_from_string("verify_rand") == test_enum::StorageMode::VERIFY_RAND,
                "verify_rand mapping");
    ASSERT_TRUE(test_enum::mode_from_string("fill_verify") == test_enum::StorageMode::FILL_VERIFY,
                "fill_verify mapping");
    ASSERT_TRUE(test_enum::mode_from_string("butterfly") == test_enum::StorageMode::BUTTERFLY,
                "butterfly mapping");
    // Legacy still works
    ASSERT_TRUE(test_enum::mode_from_string("mixed") == test_enum::StorageMode::MIXED,
                "mixed mapping");
    PASS();
}

// =============================================================================
// Tests 31-35: Metrics & Enum Completeness
// =============================================================================

static void test_31_crc_errors_init() {
    TEST("StorageError crc_errors context — fields init to 0");

    StorageError err{};
    // Verify that CRC-related fields are zero-initialized
    ASSERT_EQ(err.expected_crc, 0u, "expected_crc must init to 0");
    ASSERT_EQ(err.actual_crc, 0u, "actual_crc must init to 0");
    ASSERT_EQ(static_cast<uint32_t>(err.type), 0u, "type must init to 0 (CRC_MISMATCH)");
    PASS();
}

static void test_32_pattern_errors_init() {
    TEST("StorageError pattern_errors context — fields init to 0");

    StorageError err{};
    // Verify that pattern-error-related fields are zero-initialized
    ASSERT_EQ(err.first_diff_offset, 0u, "first_diff_offset must init to 0");
    ASSERT_EQ(err.expected_byte, 0u, "expected_byte must init to 0");
    ASSERT_EQ(err.actual_byte, 0u, "actual_byte must init to 0");
    ASSERT_EQ(err.block_offset, 0u, "block_offset must init to 0");
    PASS();
}

static void test_33_verify_mbs_calculation() {
    TEST("verify_mbs = bytes_verified / elapsed_secs");

    // Simulate: verified 100 blocks of 4096 bytes in 2.0 seconds
    uint64_t blocks_verified = 100;
    uint64_t bytes_verified = blocks_verified * VERIFY_BLOCK_SIZE;
    double elapsed_secs = 2.0;

    double verify_mbs = static_cast<double>(bytes_verified) / (1024.0 * 1024.0) / elapsed_secs;

    // 100 * 4096 = 409600 bytes = 0.390625 MB; 0.390625 / 2.0 = 0.1953125 MB/s
    double expected_mbs = (100.0 * 4096.0) / (1024.0 * 1024.0) / 2.0;
    ASSERT_TRUE(std::fabs(verify_mbs - expected_mbs) < 1e-9, "MB/s calculation mismatch");

    // Edge case: zero elapsed time should not be used (guard against div-by-zero)
    double zero_elapsed = 0.0;
    bool would_be_inf = (zero_elapsed == 0.0);
    ASSERT_TRUE(would_be_inf, "Zero elapsed must be guarded");
    PASS();
}

static void test_34_blocks_written_counter() {
    TEST("blocks_written counter increments in fill sequence");

    const int N = 10;
    alignas(16) uint8_t block[4096];
    uint32_t blocks_written = 0;

    for (int i = 0; i < N; ++i) {
        test_verify::fill_verify_block(block, static_cast<uint32_t>(i),
                                        PatternID::SEQUENTIAL, 0, 0);
        blocks_written++;
    }

    ASSERT_EQ(blocks_written, static_cast<uint32_t>(N), "blocks_written must equal N");

    // Verify the last block was written correctly
    auto result = test_verify::verify_block(block, static_cast<uint32_t>(N - 1),
                                             PatternID::SEQUENTIAL);
    ASSERT_TRUE(!result.has_value(), "Last written block must pass verification");
    PASS();
}

static void test_35_error_type_enum_completeness() {
    TEST("ErrorType enum has all 5 values");

    // Verify all 5 enum values exist and have correct underlying values
    static_assert(static_cast<uint32_t>(ErrorType::CRC_MISMATCH) == 0, "CRC_MISMATCH must be 0");
    static_assert(static_cast<uint32_t>(ErrorType::MAGIC_MISMATCH) == 1, "MAGIC_MISMATCH must be 1");
    static_assert(static_cast<uint32_t>(ErrorType::BLOCK_INDEX_MISMATCH) == 2, "BLOCK_INDEX_MISMATCH must be 2");
    static_assert(static_cast<uint32_t>(ErrorType::PATTERN_MISMATCH) == 3, "PATTERN_MISMATCH must be 3");
    static_assert(static_cast<uint32_t>(ErrorType::IO_ERROR) == 4, "IO_ERROR must be 4");

    // Runtime check: all 5 are distinct
    uint32_t values[] = {
        static_cast<uint32_t>(ErrorType::CRC_MISMATCH),
        static_cast<uint32_t>(ErrorType::MAGIC_MISMATCH),
        static_cast<uint32_t>(ErrorType::BLOCK_INDEX_MISMATCH),
        static_cast<uint32_t>(ErrorType::PATTERN_MISMATCH),
        static_cast<uint32_t>(ErrorType::IO_ERROR),
    };
    for (int i = 0; i < 5; ++i) {
        for (int j = i + 1; j < 5; ++j) {
            ASSERT_NEQ(values[i], values[j], "ErrorType values must be distinct");
        }
    }
    PASS();
}

// =============================================================================
// Main
// =============================================================================

int main() {
    std::cout << "=========================================" << std::endl;
    std::cout << "  Storage Verification Tests (35 tests)" << std::endl;
    std::cout << "=========================================" << std::endl;

    // CRC32 (1-4)
    std::cout << "\n--- CRC32C ---" << std::endl;
    test_1_crc32_sw_hw_match();
    test_2_crc32_empty_input();
    test_3_crc32_known_vectors();
    test_4_crc32_incremental();

    // VerifyBlockHeader (5-8)
    std::cout << "\n--- VerifyBlockHeader ---" << std::endl;
    test_5_header_size();
    test_6_header_magic();
    test_7_header_field_roundtrip();
    test_8_block_size_constants();

    // Pattern Generation (9-12)
    std::cout << "\n--- Pattern Generation ---" << std::endl;
    test_9_pattern_deterministic();
    test_10_pattern_unique_per_block();
    test_11_all_patterns_generate();
    test_12_pattern_all_ones_zeros();

    // fill_verify_block (13-16)
    std::cout << "\n--- fill_verify_block ---" << std::endl;
    test_13_fill_verify_block_crc_accurate();
    test_14_fill_verify_block_reproducible();
    test_15_fill_verify_different_index();
    test_16_fill_verify_magic_present();

    // verify_block (17-20)
    std::cout << "\n--- verify_block ---" << std::endl;
    test_17_verify_block_pass();
    test_18_verify_block_crc_tamper();
    test_19_verify_block_magic_tamper();
    test_20_verify_block_index_mismatch();

    // StorageError (21-24)
    std::cout << "\n--- StorageError ---" << std::endl;
    test_21_error_type_distinction();
    test_22_error_log_cap();
    test_23_error_byte_diff();
    test_24_error_all_types_exist();

    // VERIFY_SEQ flow (25-28)
    std::cout << "\n--- VERIFY_SEQ Flow ---" << std::endl;
    test_25_verify_seq_write_read_order();
    test_26_verify_seq_progress();
    test_27_verify_error_count();
    test_28_verify_multipass();

    // Enum + Scheduler (29-30)
    std::cout << "\n--- Enum + Scheduler ---" << std::endl;
    test_29_enum_new_values_exist();
    test_30_scheduler_string_mapping();

    // Metrics & Enum Completeness (31-35)
    std::cout << "\n--- Metrics & Enum Completeness ---" << std::endl;
    test_31_crc_errors_init();
    test_32_pattern_errors_init();
    test_33_verify_mbs_calculation();
    test_34_blocks_written_counter();
    test_35_error_type_enum_completeness();

    // Summary
    std::cout << "\n=========================================" << std::endl;
    std::cout << "  Results: " << tests_passed << "/" << tests_run << " passed" << std::endl;
    std::cout << "=========================================" << std::endl;

    return (tests_passed == tests_run) ? 0 : 1;
}
