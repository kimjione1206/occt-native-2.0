// ─── FP64 Matrix Multiplication Stress Kernel ────────────────────────────────
// Requires cl_khr_fp64 extension.
#pragma OPENCL EXTENSION cl_khr_fp64 : enable

__kernel void matmul_stress_fp64(
    __global double* A,
    __global double* B,
    __global double* C,
    const int N)
{
    int row = get_global_id(0);
    int col = get_global_id(1);

    double sum = 0.0;
    for (int k = 0; k < N; ++k) {
        sum = fma(A[row * N + k], B[k * N + col], sum);
    }
    C[row * N + col] = sum;
}
