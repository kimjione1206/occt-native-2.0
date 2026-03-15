#pragma once

#include <cstddef>
#include <cstdint>

namespace occt { namespace utils {

/// Compute CRC32C (Castagnoli) checksum.
/// Uses hardware SSE4.2 intrinsics when available, software fallback otherwise.
/// @param data      Pointer to input data.
/// @param length    Number of bytes.
/// @param initial_crc  Initial CRC value (for incremental computation).
/// @return CRC32C checksum.
uint32_t crc32c(const void* data, size_t length, uint32_t initial_crc = 0);

}} // namespace occt::utils
