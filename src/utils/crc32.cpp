#include "crc32.h"
#include "cpuid.h"

#include <cstring>

#if defined(OCCT_ARCH_X64) || defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
    #define OCCT_CRC32_HW_POSSIBLE 1
    #ifdef _MSC_VER
        #include <intrin.h>
    #else
        #include <nmmintrin.h> // SSE4.2 CRC32 intrinsics
    #endif
#endif

namespace occt { namespace utils {

// ─── Software CRC32C (Castagnoli polynomial 0x82F63B78) ─────────────────────

static uint32_t sw_crc32c_table[256];
static bool sw_table_initialized = false;

static void init_sw_table() {
    if (sw_table_initialized) return;
    const uint32_t poly = 0x82F63B78u; // Castagnoli
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1)
                crc = (crc >> 1) ^ poly;
            else
                crc >>= 1;
        }
        sw_crc32c_table[i] = crc;
    }
    sw_table_initialized = true;
}

static uint32_t crc32c_software(const void* data, size_t length, uint32_t initial_crc) {
    init_sw_table();
    uint32_t crc = ~initial_crc;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < length; ++i) {
        crc = sw_crc32c_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

// ─── Hardware CRC32C (SSE4.2) ───────────────────────────────────────────────

#if defined(OCCT_CRC32_HW_POSSIBLE)

static uint32_t crc32c_hardware(const void* data, size_t length, uint32_t initial_crc) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint64_t crc = ~static_cast<uint64_t>(initial_crc);

    // Process 8 bytes at a time using _mm_crc32_u64
    while (length >= 8) {
        uint64_t val;
        std::memcpy(&val, p, 8);
        crc = _mm_crc32_u64(crc, val);
        p += 8;
        length -= 8;
    }

    // Process remaining bytes one at a time
    while (length > 0) {
        crc = _mm_crc32_u8(static_cast<uint32_t>(crc), *p);
        ++p;
        --length;
    }

    return ~static_cast<uint32_t>(crc);
}

#endif // OCCT_CRC32_HW_POSSIBLE

// ─── Runtime dispatch ───────────────────────────────────────────────────────

using crc32c_fn = uint32_t(*)(const void*, size_t, uint32_t);

static crc32c_fn resolve_crc32c() {
#if defined(OCCT_CRC32_HW_POSSIBLE)
    auto info = detect_cpu();
    if (info.has_sse42) {
        return crc32c_hardware;
    }
#endif
    return crc32c_software;
}

static crc32c_fn g_crc32c = resolve_crc32c();

uint32_t crc32c(const void* data, size_t length, uint32_t initial_crc) {
    return g_crc32c(data, length, initial_crc);
}

}} // namespace occt::utils
