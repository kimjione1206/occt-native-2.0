#include "error_verifier.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace occt {

ErrorVerifier::ErrorVerifier() = default;

double ErrorVerifier::compute_expected(double seed, double mul_val, double add_val, int iterations) {
    double acc = seed;
    for (int i = 0; i < iterations; ++i) {
        acc = std::fma(acc, mul_val, add_val);
    }
    return acc;
}

CpuError ErrorVerifier::make_error(int core_id, double expected, double actual,
                                    uint64_t timestamp_ms, CpuErrorCode code) {
    CpuError err;
    err.core_id = core_id;
    err.expected = expected;
    err.actual = actual;
    err.timestamp = timestamp_ms;
    err.error_code = code;

    // Compute bit-level XOR to identify flipped bits
    uint64_t exp_bits = 0;
    uint64_t act_bits = 0;
    std::memcpy(&exp_bits, &expected, sizeof(double));
    std::memcpy(&act_bits, &actual, sizeof(double));
    err.bit_mask = exp_bits ^ act_bits;

    // Count flipped bits
    int flipped = 0;
    uint64_t mask = err.bit_mask;
    while (mask) {
        flipped += (mask & 1);
        mask >>= 1;
    }

    std::ostringstream oss;
    oss << "Core " << core_id << ": ";
    if (flipped == 1) {
        oss << "Single-bit flip detected";
    } else {
        oss << flipped << "-bit flip detected";
    }
    oss << " (expected=" << expected << ", actual=" << actual
        << ", XOR=0x" << std::hex << err.bit_mask << ")";
    err.description = oss.str();

    return err;
}

bool ErrorVerifier::verify(int core_id, double expected, double actual, uint64_t timestamp_ms) {
    // Use bitwise comparison for deterministic FMA results
    uint64_t exp_bits = 0;
    uint64_t act_bits = 0;
    std::memcpy(&exp_bits, &expected, sizeof(double));
    std::memcpy(&act_bits, &actual, sizeof(double));

    if (exp_bits != act_bits) {
        CpuError err = make_error(core_id, expected, actual, timestamp_ms, CpuErrorCode::FMA_MISMATCH);
        std::lock_guard<std::mutex> lock(mutex_);
        errors_.push_back(std::move(err));
        return false;
    }
    return true;
}

bool ErrorVerifier::verify_array(int core_id, const double* expected, const double* actual,
                                  int count, uint64_t timestamp_ms) {
    bool all_ok = true;
    for (int i = 0; i < count; ++i) {
        uint64_t exp_bits = 0;
        uint64_t act_bits = 0;
        std::memcpy(&exp_bits, &expected[i], sizeof(double));
        std::memcpy(&act_bits, &actual[i], sizeof(double));

        if (exp_bits != act_bits) {
            CpuError err = make_error(core_id, expected[i], actual[i],
                                       timestamp_ms, CpuErrorCode::SIMD_LANE_ERROR);
            std::lock_guard<std::mutex> lock(mutex_);
            errors_.push_back(std::move(err));
            all_ok = false;
        }
    }
    return all_ok;
}

std::vector<CpuError> ErrorVerifier::get_errors() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return errors_;
}

std::vector<CpuError> ErrorVerifier::get_errors_for_core(int core_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CpuError> result;
    for (const auto& err : errors_) {
        if (err.core_id == core_id) {
            result.push_back(err);
        }
    }
    return result;
}

int ErrorVerifier::error_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(errors_.size());
}

void ErrorVerifier::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    errors_.clear();
}

} // namespace occt
