// ─── FP32 Matrix Multiplication Stress Kernel ────────────────────────────────
__kernel void matmul_stress(
    __global float* A,
    __global float* B,
    __global float* C,
    const int N)
{
    int row = get_global_id(0);
    int col = get_global_id(1);

    float sum = 0.0f;
    for (int k = 0; k < N; ++k) {
        sum = fma(A[row * N + k], B[k * N + col], sum);
    }
    C[row * N + col] = sum;
}

// ─── FMA Stress Kernel ───────────────────────────────────────────────────────
// Uses 4 independent accumulators to maximize FMA unit utilization.
__kernel void fma_stress(__global float* data, const int iterations) {
    int gid = get_global_id(0);
    float acc0 = data[gid];
    float acc1 = acc0 + 0.1f;
    float acc2 = acc0 + 0.2f;
    float acc3 = acc0 + 0.3f;
    float mul = 0.9999f;
    float add = 0.0001f;

    for (int i = 0; i < iterations; ++i) {
        acc0 = fma(acc0, mul, add);
        acc1 = fma(acc1, mul, add);
        acc2 = fma(acc2, mul, add);
        acc3 = fma(acc3, mul, add);
    }
    data[gid] = acc0 + acc1 + acc2 + acc3;
}

// ─── Transcendental Function Stress Kernel ───────────────────────────────────
// Heavy use of sin, cos, exp to stress special function units.
__kernel void trig_stress(__global float* data, const int iterations) {
    int gid = get_global_id(0);
    float x = data[gid];
    for (int i = 0; i < iterations; ++i) {
        x = sin(x) * cos(x) + exp(x * 0.001f);
    }
    data[gid] = x;
}
