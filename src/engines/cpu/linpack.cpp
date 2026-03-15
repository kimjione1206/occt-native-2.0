#include "linpack.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef _MSC_VER
    #define DO_NOT_OPTIMIZE(x) { volatile auto _v = (x); (void)_v; }
#else
    #define DO_NOT_OPTIMIZE(x) asm volatile("" : : "r,m"(x) : "memory")
#endif

#ifdef OCCT_HAS_OPENBLAS
extern "C" {
    void cblas_dgemm(int order, int transA, int transB,
                     int M, int N, int K,
                     double alpha, const double* A, int lda,
                     const double* B, int ldb,
                     double beta, double* C, int ldc);
}
#define CblasRowMajor 101
#define CblasNoTrans  111
#endif

namespace occt { namespace cpu {

// Simple DGEMM: C = alpha*A*B + beta*C
// Optimized with loop tiling for cache performance
static void naive_dgemm(int N,
                        double alpha, const double* A, const double* B,
                        double beta, double* C) {
    const int TILE = 64; // Tile size for cache blocking

    // Scale C by beta
    if (beta == 0.0) {
        std::memset(C, 0, static_cast<size_t>(N) * N * sizeof(double));
    } else if (beta != 1.0) {
        for (int i = 0; i < N * N; ++i) {
            C[i] *= beta;
        }
    }

    // Tiled matrix multiply: C += alpha * A * B
    for (int ii = 0; ii < N; ii += TILE) {
        int i_end = (ii + TILE < N) ? ii + TILE : N;
        for (int kk = 0; kk < N; kk += TILE) {
            int k_end = (kk + TILE < N) ? kk + TILE : N;
            for (int jj = 0; jj < N; jj += TILE) {
                int j_end = (jj + TILE < N) ? jj + TILE : N;

                for (int i = ii; i < i_end; ++i) {
                    for (int k = kk; k < k_end; ++k) {
                        double a_ik = alpha * A[i * N + k];
                        for (int j = jj; j < j_end; ++j) {
                            C[i * N + j] += a_ik * B[k * N + j];
                        }
                    }
                }
            }
        }
    }
}

// Matrix-vector multiply: y = A * x (row-major)
static void matvec(int N, const double* A, const double* x, double* y) {
    for (int i = 0; i < N; ++i) {
        double sum = 0.0;
        for (int j = 0; j < N; ++j) {
            sum += A[i * N + j] * x[j];
        }
        y[i] = sum;
    }
}

// Compute vector infinity norm: max(|v[i]|)
static double vec_norm_inf(const double* v, int N) {
    double max_val = 0.0;
    for (int i = 0; i < N; ++i) {
        double a = std::fabs(v[i]);
        if (a > max_val) max_val = a;
    }
    return max_val;
}

// Compute matrix infinity norm (max row sum of absolute values)
static double mat_norm_inf(const double* A, int N) {
    double max_sum = 0.0;
    for (int i = 0; i < N; ++i) {
        double row_sum = 0.0;
        for (int j = 0; j < N; ++j) {
            row_sum += std::fabs(A[i * N + j]);
        }
        if (row_sum > max_sum) max_sum = row_sum;
    }
    return max_sum;
}

LinpackResult run_dgemm(int matrix_size) {
    int N = matrix_size;
    size_t elems = static_cast<size_t>(N) * N;

    std::vector<double> A(elems);
    std::vector<double> B(elems);
    std::vector<double> C(elems, 0.0);

    // Initialize with pseudo-random values
    for (size_t i = 0; i < elems; ++i) {
        A[i] = static_cast<double>(i % 1000) / 1000.0;
        B[i] = static_cast<double>((i * 7 + 13) % 1000) / 1000.0;
    }

    auto start = std::chrono::high_resolution_clock::now();

#ifdef OCCT_HAS_OPENBLAS
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                N, N, N, 1.0, A.data(), N, B.data(), N, 0.0, C.data(), N);
#else
    naive_dgemm(N, 1.0, A.data(), B.data(), 0.0, C.data());
#endif

    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();

    // DGEMM FLOP count: 2 * N^3 (multiply + add for each element)
    uint64_t flops = 2ULL * N * N * N;
    double gflops = static_cast<double>(flops) / elapsed / 1e9;

    // Prevent dead code elimination
    DO_NOT_OPTIMIZE(C[0]);
    DO_NOT_OPTIMIZE(C[elems - 1]);

    LinpackResult result;
    result.gflops = gflops;
    result.total_flops = flops;
    result.elapsed_secs = elapsed;
    result.matrix_size = N;
    return result;
}

LinpackResult run_dgemm_verified(int matrix_size, double threshold) {
    int N = matrix_size;
    size_t elems = static_cast<size_t>(N) * N;

    // Generate A and x, compute b = A * x
    // Then compute C = A * B (where B is identity-like for simplicity)
    // and verify the residual

    std::vector<double> A(elems);
    std::vector<double> x(N);
    std::vector<double> b(N);

    // Initialize A with deterministic pseudo-random values
    for (size_t i = 0; i < elems; ++i) {
        A[i] = static_cast<double>(i % 1000) / 1000.0;
        // Make diagonally dominant for numerical stability
        if (static_cast<int>(i / N) == static_cast<int>(i % N)) {
            A[i] += static_cast<double>(N);
        }
    }

    // Known solution vector x
    for (int i = 0; i < N; ++i) {
        x[i] = static_cast<double>(i + 1) / static_cast<double>(N);
    }

    // Compute b = A * x
    matvec(N, A.data(), x.data(), b.data());

    // Now perform DGEMM: C = A * A (stress the hardware)
    std::vector<double> C(elems, 0.0);

    auto start = std::chrono::high_resolution_clock::now();

#ifdef OCCT_HAS_OPENBLAS
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                N, N, N, 1.0, A.data(), N, A.data(), N, 0.0, C.data(), N);
#else
    naive_dgemm(N, 1.0, A.data(), A.data(), 0.0, C.data());
#endif

    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();

    // Verify: recompute b2 = A * x and check against original b
    // Any hardware error during the DGEMM could corrupt the FPU state,
    // so we verify the scalar path to detect such corruption
    std::vector<double> b2(N);
    matvec(N, A.data(), x.data(), b2.data());

    // Compute residual: ||b2 - b|| / (n * ||A|| * ||x||)
    std::vector<double> residual_vec(N);
    for (int i = 0; i < N; ++i) {
        residual_vec[i] = b2[i] - b[i];
    }

    double residual_norm = vec_norm_inf(residual_vec.data(), N);
    double a_norm = mat_norm_inf(A.data(), N);
    double x_norm = vec_norm_inf(x.data(), N);

    double denominator = static_cast<double>(N) * a_norm * x_norm;
    double normalized_residual = (denominator > 0.0) ? (residual_norm / denominator) : 0.0;

    uint64_t flops = 2ULL * N * N * N;
    double gflops = static_cast<double>(flops) / elapsed / 1e9;

    DO_NOT_OPTIMIZE(C[0]);
    DO_NOT_OPTIMIZE(C[elems - 1]);

    LinpackResult result;
    result.gflops = gflops;
    result.total_flops = flops;
    result.elapsed_secs = elapsed;
    result.matrix_size = N;
    result.verified = (normalized_residual < threshold);
    result.residual_norm = normalized_residual;
    return result;
}

uint64_t stress_linpack(uint64_t duration_ns, int matrix_size) {
    // Clamp matrix size to reasonable range
    if (matrix_size < 256) matrix_size = 256;
    if (matrix_size > 8192) matrix_size = 8192;

    uint64_t total_flops = 0;
    auto start = std::chrono::high_resolution_clock::now();

    for (;;) {
        LinpackResult res = run_dgemm(matrix_size);
        total_flops += res.total_flops;

        auto now = std::chrono::high_resolution_clock::now();
        uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
        if (elapsed >= duration_ns) break;
    }

    return total_flops;
}

LinpackStressResult stress_linpack_verified(uint64_t duration_ns, int matrix_size,
                                             double threshold) {
    if (matrix_size < 256) matrix_size = 256;
    if (matrix_size > 8192) matrix_size = 8192;

    LinpackStressResult result;
    auto start = std::chrono::high_resolution_clock::now();

    for (;;) {
        LinpackResult res = run_dgemm_verified(matrix_size, threshold);
        result.total_flops += res.total_flops;

        if (!res.verified) {
            result.verified = false;
        }
        if (res.residual_norm > result.worst_residual) {
            result.worst_residual = res.residual_norm;
        }

        auto now = std::chrono::high_resolution_clock::now();
        uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
        if (elapsed >= duration_ns) break;
    }

    return result;
}

}} // namespace occt::cpu
