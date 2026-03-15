#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace occt {

// Error codes for CPU verification failures
enum class CpuErrorCode : int {
    NONE = 0,
    FMA_MISMATCH = 1,       // FMA chain result mismatch
    LINPACK_RESIDUAL = 2,   // Linpack residual too large
    BIT_FLIP = 3,           // Single/multi-bit flip detected
    SIMD_LANE_ERROR = 4     // Specific SIMD lane error
};

struct CpuError {
    int core_id = -1;
    uintptr_t address = 0;          // Address of the computation (if applicable)
    double expected = 0.0;
    double actual = 0.0;
    uint64_t bit_mask = 0;          // XOR of expected vs actual (flipped bits)
    uint64_t timestamp = 0;         // Milliseconds since test start
    CpuErrorCode error_code = CpuErrorCode::NONE;
    std::string description;
};

class ErrorVerifier {
public:
    ErrorVerifier();

    // Compute the expected result of a deterministic FMA chain
    // seed -> N iterations of FMA(acc, mul_val, add_val) -> result
    // IEEE 754 guarantees identical results on any conforming implementation
    static double compute_expected(double seed, double mul_val, double add_val, int iterations);

    // Verify actual vs expected, returns true if match
    // On mismatch, creates a CpuError with bit-level flip information
    bool verify(int core_id, double expected, double actual, uint64_t timestamp_ms);

    // Verify an array of doubles (for SIMD lane-level checking)
    bool verify_array(int core_id, const double* expected, const double* actual,
                      int count, uint64_t timestamp_ms);

    // Get all errors recorded so far
    std::vector<CpuError> get_errors() const;

    // Get errors for a specific core
    std::vector<CpuError> get_errors_for_core(int core_id) const;

    // Total error count
    int error_count() const;

    // Clear all errors
    void clear();

private:
    mutable std::mutex mutex_;
    std::vector<CpuError> errors_;

    // Create error with bit-level XOR analysis
    CpuError make_error(int core_id, double expected, double actual,
                        uint64_t timestamp_ms, CpuErrorCode code);
};

} // namespace occt
