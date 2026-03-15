#pragma once

#include <cstdint>

namespace occt { namespace cpu {

struct LinpackResult {
    double gflops;
    uint64_t total_flops;
    double elapsed_secs;
    int matrix_size;
    bool verified = true;          // Residual check passed
    double residual_norm = 0.0;    // ||Ax - b|| / (n * ||A|| * ||x||)
};

// Run DGEMM-based stress for specified duration (nanoseconds)
// matrix_size: N for NxN matrix (default 2048)
// Returns total floating-point operations executed
uint64_t stress_linpack(uint64_t duration_ns, int matrix_size = 2048);

// Single DGEMM run, returns GFLOPS achieved
LinpackResult run_dgemm(int matrix_size);

// Single DGEMM run with residual verification
// Solves Ax = b, then checks ||Ax - b|| / (n * ||A|| * ||x||)
// If residual > threshold, verified = false (hardware error suspected)
LinpackResult run_dgemm_verified(int matrix_size, double threshold = 1e-10);

// Linpack stress with verification - returns total flops and sets verified=false on error
struct LinpackStressResult {
    uint64_t total_flops = 0;
    bool verified = true;
    double worst_residual = 0.0;
};

LinpackStressResult stress_linpack_verified(uint64_t duration_ns, int matrix_size = 2048,
                                             double threshold = 1e-10);

}} // namespace occt::cpu
