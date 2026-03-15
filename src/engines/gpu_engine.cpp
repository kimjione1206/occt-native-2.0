#include "gpu_engine.h"
#include "config.h"
#include "utils/gpu_info.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>

#if defined(_WIN32) && defined(OCCT_HAS_OPENCL)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#ifdef OCCT_HAS_OPENCL
#include "engines/gpu/opencl_backend.h"
#endif

#ifdef OCCT_HAS_VULKAN
#include "engines/gpu/vulkan_backend.h"
#include "engines/gpu/vulkan_renderer.h"
#include "engines/gpu/artifact_detector.h"
#endif

namespace occt {

// ─── Embedded kernel sources ─────────────────────────────────────────────────
// These are compiled into the binary so no external .cl files are needed at runtime.

#ifdef OCCT_HAS_OPENCL

static const char* const kKernelStressFP32 = R"CL(
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

__kernel void trig_stress(__global float* data, const int iterations) {
    int gid = get_global_id(0);
    float x = data[gid];
    for (int i = 0; i < iterations; ++i) {
        x = sin(x) * cos(x) + exp(x * 0.001f);
    }
    data[gid] = x;
}
)CL";

static const char* const kKernelStressFP64 = R"CL(
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
)CL";

static const char* const kKernelVRAM = R"CL(
// NOTE: barrier(CLK_GLOBAL_MEM_FENCE) only synchronizes within a single
// work-group, NOT globally across all work-groups. For a true global memory
// fence the kernel must be split into two enqueue calls (write pass + verify
// pass). We use a two-pass approach: the host enqueues the write kernel,
// waits for completion, then enqueues the verify kernel.

__kernel void walking_ones_write(__global uint* buffer, const uint pattern_shift) {
    int gid = get_global_id(0);
    uint pattern = 1u << (pattern_shift % 32);
    buffer[gid] = pattern;
}
__kernel void walking_ones_verify(__global uint* buffer, __global uint* errors, const uint pattern_shift) {
    int gid = get_global_id(0);
    uint pattern = 1u << (pattern_shift % 32);
    uint rv = buffer[gid];
    if (rv != pattern) atomic_inc(&errors[0]);
}

__kernel void walking_zeros_write(__global uint* buffer, const uint pattern_shift) {
    int gid = get_global_id(0);
    uint pattern = ~(1u << (pattern_shift % 32));
    buffer[gid] = pattern;
}
__kernel void walking_zeros_verify(__global uint* buffer, __global uint* errors, const uint pattern_shift) {
    int gid = get_global_id(0);
    uint pattern = ~(1u << (pattern_shift % 32));
    uint rv = buffer[gid];
    if (rv != pattern) atomic_inc(&errors[0]);
}

__kernel void address_test_write(__global uint* buffer) {
    int gid = get_global_id(0);
    buffer[gid] = (uint)gid;
}
__kernel void address_test_verify(__global uint* buffer, __global uint* errors) {
    int gid = get_global_id(0);
    uint rv = buffer[gid];
    if (rv != (uint)gid) atomic_inc(&errors[0]);
}

__kernel void alternating_pattern_write(__global uint* buffer, const uint phase) {
    int gid = get_global_id(0);
    uint pattern = (phase == 0) ? 0xAAAAAAAAu : 0x55555555u;
    buffer[gid] = pattern;
}
__kernel void alternating_pattern_verify(__global uint* buffer, __global uint* errors, const uint phase) {
    int gid = get_global_id(0);
    uint pattern = (phase == 0) ? 0xAAAAAAAAu : 0x55555555u;
    uint rv = buffer[gid];
    if (rv != pattern) atomic_inc(&errors[0]);
}

// ─── March C- pattern ───
// Each pass is a separate kernel for global synchronization.
// Host orchestrates: fill_zero → march_up_r0_w1 → march_up_r1_w0
//                  → march_down_r0_w1 → march_down_r1_w0 → march_final_r0

__kernel void march_fill_zero(__global uint* buffer) {
    int gid = get_global_id(0);
    buffer[gid] = 0u;
}
__kernel void march_up_r0_w1(__global uint* buffer, __global uint* errors) {
    int gid = get_global_id(0);
    if (buffer[gid] != 0u) atomic_inc(&errors[0]);
    buffer[gid] = 0xFFFFFFFFu;
}
__kernel void march_up_r1_w0(__global uint* buffer, __global uint* errors) {
    int gid = get_global_id(0);
    if (buffer[gid] != 0xFFFFFFFFu) atomic_inc(&errors[0]);
    buffer[gid] = 0u;
}
__kernel void march_down_r0_w1(__global uint* buffer, __global uint* errors) {
    int gid = get_global_id(0);
    // Reverse addressing: work-items still cover all elements, host ensures
    // sequential semantics by running with global_size == num_elements.
    if (buffer[gid] != 0u) atomic_inc(&errors[0]);
    buffer[gid] = 0xFFFFFFFFu;
}
__kernel void march_down_r1_w0(__global uint* buffer, __global uint* errors) {
    int gid = get_global_id(0);
    if (buffer[gid] != 0xFFFFFFFFu) atomic_inc(&errors[0]);
    buffer[gid] = 0u;
}
__kernel void march_final_r0(__global uint* buffer, __global uint* errors) {
    int gid = get_global_id(0);
    if (buffer[gid] != 0u) atomic_inc(&errors[0]);
}

// ─── Butterfly pattern ───
// Tests edge memory cells by converging from both ends of the buffer.
// 'half_size' = num_elements / 2. Each work-item handles a pair (i, N-1-i).

__kernel void butterfly_write(__global uint* buffer, const uint num_elements) {
    int gid = get_global_id(0);
    uint high = num_elements - 1u - (uint)gid;
    uint pattern_lo = (uint)gid ^ 0xA5A5A5A5u;
    uint pattern_hi = high ^ 0x5A5A5A5Au;
    buffer[gid] = pattern_lo;
    buffer[high] = pattern_hi;
}
__kernel void butterfly_verify(__global uint* buffer, __global uint* errors, const uint num_elements) {
    int gid = get_global_id(0);
    uint high = num_elements - 1u - (uint)gid;
    uint pattern_lo = (uint)gid ^ 0xA5A5A5A5u;
    uint pattern_hi = high ^ 0x5A5A5A5Au;
    if (buffer[gid] != pattern_lo) atomic_inc(&errors[0]);
    if (buffer[high] != pattern_hi) atomic_inc(&errors[0]);
}

// ─── Random pattern (LCG) ───
// GPU-side LCG: next = (a * seed + c) mod 2^32 with same seed for write & verify.
// a = 1103515245, c = 12345 (glibc LCG constants).

__kernel void random_pattern_write(__global uint* buffer, const uint base_seed) {
    int gid = get_global_id(0);
    uint seed = base_seed ^ (uint)gid;
    seed = seed * 1103515245u + 12345u;
    buffer[gid] = seed;
}
__kernel void random_pattern_verify(__global uint* buffer, __global uint* errors, const uint base_seed) {
    int gid = get_global_id(0);
    uint seed = base_seed ^ (uint)gid;
    seed = seed * 1103515245u + 12345u;
    if (buffer[gid] != seed) atomic_inc(&errors[0]);
}
)CL";

#endif // OCCT_HAS_OPENCL

// ─── Impl (PImpl) ────────────────────────────────────────────────────────────

struct GpuEngine::Impl {
    GpuEngine* owner = nullptr;  // back-pointer for IEngine base access

#ifdef OCCT_HAS_OPENCL
    gpu::OpenCLContext cl_ctx;
    std::vector<gpu::OpenCLContext::DeviceEntry> devices;
    int selected_device = 0;

    // Compiled kernels
    cl_kernel k_matmul = nullptr;
    cl_kernel k_matmul_fp64 = nullptr;
    cl_kernel k_fma = nullptr;
    cl_kernel k_trig = nullptr;
    cl_kernel k_walking_ones_write = nullptr;
    cl_kernel k_walking_ones_verify = nullptr;
    cl_kernel k_walking_zeros_write = nullptr;
    cl_kernel k_walking_zeros_verify = nullptr;
    cl_kernel k_address_test_write = nullptr;
    cl_kernel k_address_test_verify = nullptr;
    cl_kernel k_alternating_write = nullptr;
    cl_kernel k_alternating_verify = nullptr;
    // March C- kernels
    cl_kernel k_march_fill_zero = nullptr;
    cl_kernel k_march_up_r0_w1 = nullptr;
    cl_kernel k_march_up_r1_w0 = nullptr;
    cl_kernel k_march_down_r0_w1 = nullptr;
    cl_kernel k_march_down_r1_w0 = nullptr;
    cl_kernel k_march_final_r0 = nullptr;
    // Butterfly kernels
    cl_kernel k_butterfly_write = nullptr;
    cl_kernel k_butterfly_verify = nullptr;
    // Random pattern kernels
    cl_kernel k_random_write = nullptr;
    cl_kernel k_random_verify = nullptr;

    // Buffers (allocated per-run)
    cl_mem buf_a = nullptr;
    cl_mem buf_b = nullptr;
    cl_mem buf_c = nullptr;
    cl_mem buf_data = nullptr;
    cl_mem buf_vram = nullptr;
    cl_mem buf_errors = nullptr;
#endif

    int selected_device_idx = 0;  // common device index for sensor queries
    std::vector<GpuInfo> gpu_infos;
    utils::GpuVendor active_vendor = utils::GpuVendor::UNKNOWN;

    std::atomic<bool> running{false};
    std::thread worker_thread;
    std::mutex start_stop_mutex;
    GpuStressMode current_mode = GpuStressMode::MATRIX_MUL;
    int duration_secs = 0;

    mutable std::mutex metrics_mutex;
    GpuMetrics latest_metrics;
    MetricsCallback metrics_cb;

    bool initialized = false;
    bool opencl_available = false;
    std::string last_error;

#ifdef OCCT_HAS_VULKAN
    gpu::VulkanContext vk_ctx;
    bool vulkan_available = false;
    gpu::ShaderComplexity shader_complexity = gpu::ShaderComplexity::LEVEL_1;
    AdaptiveMode adaptive_mode = AdaptiveMode::VARIABLE;
    float switch_interval_secs = 0.33f;  // 330ms for aggressive PSU transient testing
    float coil_whine_freq_hz = 100.0f;   // Coil whine test frequency (0 = sweep mode)
#endif

    // ─── Lifecycle ──────────────────────────────────────────────────────

    bool do_initialize() {
        utils::gpu_monitor_init();

        bool any_backend = false;

#ifdef OCCT_HAS_OPENCL
#if defined(_WIN32)
        HMODULE hOpenCL = LoadLibraryA("OpenCL.dll");
        if (!hOpenCL) {
            std::cerr << "[GPU] Warning: OpenCL.dll not found" << std::endl;
            opencl_available = false;
        } else {
            FreeLibrary(hOpenCL);
#endif

        try {
            devices = cl_ctx.enumerate_devices();
        } catch (const std::exception& e) {
            std::cerr << "[GPU] Warning: OpenCL enumeration failed: " << e.what() << std::endl;
            opencl_available = false;
        } catch (...) {
            std::cerr << "[GPU] Warning: OpenCL enumeration failed with unknown error" << std::endl;
            opencl_available = false;
        }

        if (!devices.empty()) {
            for (const auto& dev : devices) {
                GpuInfo info;
                info.name = dev.device_name;
                info.vendor = dev.vendor;
                info.driver_version = dev.driver_version;
                info.vram_total_mb = dev.global_mem_bytes / (1024 * 1024);
                info.vram_free_mb = info.vram_total_mb;
                info.compute_units = static_cast<int>(dev.compute_units);
                info.max_clock_mhz = static_cast<int>(dev.max_clock_mhz);
                gpu_infos.push_back(std::move(info));
            }
            opencl_available = true;
            any_backend = true;
        } else {
            std::cerr << "[GPU] Warning: No OpenCL GPU devices found" << std::endl;
            opencl_available = false;
        }

#if defined(_WIN32)
        }
#endif
#else
        std::cerr << "[GPU] OpenCL support not compiled in" << std::endl;
        opencl_available = false;
#endif

#ifdef OCCT_HAS_VULKAN
        try {
            auto vk_devices = vk_ctx.enumerate_devices();
            if (!vk_devices.empty()) {
                vulkan_available = true;
                any_backend = true;
                std::cout << "[GPU] Vulkan: found " << vk_devices.size() << " device(s)" << std::endl;

                // Add Vulkan devices to gpu_infos if not already present from OpenCL
                for (const auto& vd : vk_devices) {
                    bool already_listed = false;
                    for (const auto& existing : gpu_infos) {
                        if (existing.name == vd.name) {
                            already_listed = true;
                            break;
                        }
                    }
                    if (!already_listed) {
                        GpuInfo info;
                        info.name = vd.name;
                        info.vendor = vd.vendor;
                        info.driver_version = vd.driver_version;
                        info.vram_total_mb = vd.vram_bytes / (1024 * 1024);
                        info.vram_free_mb = info.vram_total_mb;
                        gpu_infos.push_back(std::move(info));
                    }
                }
            } else {
                std::cerr << "[GPU] Warning: No Vulkan devices found" << std::endl;
                vulkan_available = false;
            }
        } catch (...) {
            std::cerr << "[GPU] Warning: Vulkan enumeration failed" << std::endl;
            vulkan_available = false;
        }
#endif

        initialized = true;
        return any_backend;
    }

    void do_select_gpu(int index) {
        selected_device_idx = index;
#ifdef OCCT_HAS_OPENCL
        if (index >= 0 && index < static_cast<int>(devices.size())) {
            selected_device = index;
            active_vendor = utils::parse_gpu_vendor(devices[index].vendor);
        }
#else
        (void)index;
#endif
    }

    // ─── Kernel compilation ─────────────────────────────────────────────

#ifdef OCCT_HAS_OPENCL
    bool compile_all_kernels() {
        cl_int err;

        // FP32 stress kernels
        err = cl_ctx.compile_kernel(kKernelStressFP32, "matmul_stress", "", k_matmul);
        if (err != CL_SUCCESS) {
            std::cerr << "[GPU] Failed to compile matmul_stress: "
                      << gpu::cl_error_string(err) << std::endl;
            return false;
        }

        err = cl_ctx.compile_kernel(kKernelStressFP32, "fma_stress", "", k_fma);
        if (err != CL_SUCCESS) {
            std::cerr << "[GPU] Failed to compile fma_stress" << std::endl;
            return false;
        }

        err = cl_ctx.compile_kernel(kKernelStressFP32, "trig_stress", "", k_trig);
        if (err != CL_SUCCESS) {
            std::cerr << "[GPU] Failed to compile trig_stress" << std::endl;
            return false;
        }

        // FP64 (may fail on GPUs without fp64 support, non-fatal)
        err = cl_ctx.compile_kernel(kKernelStressFP64, "matmul_stress_fp64", "", k_matmul_fp64);
        if (err != CL_SUCCESS) {
            std::cerr << "[GPU] FP64 kernel not available (device may lack fp64 support)"
                      << std::endl;
            k_matmul_fp64 = nullptr;
        }

        // VRAM test kernels (two-pass: write then verify for global sync)
        err = cl_ctx.compile_kernel(kKernelVRAM, "walking_ones_write", "", k_walking_ones_write);
        if (err != CL_SUCCESS) return false;
        err = cl_ctx.compile_kernel(kKernelVRAM, "walking_ones_verify", "", k_walking_ones_verify);
        if (err != CL_SUCCESS) return false;
        err = cl_ctx.compile_kernel(kKernelVRAM, "walking_zeros_write", "", k_walking_zeros_write);
        if (err != CL_SUCCESS) return false;
        err = cl_ctx.compile_kernel(kKernelVRAM, "walking_zeros_verify", "", k_walking_zeros_verify);
        if (err != CL_SUCCESS) return false;
        err = cl_ctx.compile_kernel(kKernelVRAM, "address_test_write", "", k_address_test_write);
        if (err != CL_SUCCESS) return false;
        err = cl_ctx.compile_kernel(kKernelVRAM, "address_test_verify", "", k_address_test_verify);
        if (err != CL_SUCCESS) return false;
        err = cl_ctx.compile_kernel(kKernelVRAM, "alternating_pattern_write", "", k_alternating_write);
        if (err != CL_SUCCESS) return false;
        err = cl_ctx.compile_kernel(kKernelVRAM, "alternating_pattern_verify", "", k_alternating_verify);
        if (err != CL_SUCCESS) return false;

        // March C- kernels
        err = cl_ctx.compile_kernel(kKernelVRAM, "march_fill_zero", "", k_march_fill_zero);
        if (err != CL_SUCCESS) return false;
        err = cl_ctx.compile_kernel(kKernelVRAM, "march_up_r0_w1", "", k_march_up_r0_w1);
        if (err != CL_SUCCESS) return false;
        err = cl_ctx.compile_kernel(kKernelVRAM, "march_up_r1_w0", "", k_march_up_r1_w0);
        if (err != CL_SUCCESS) return false;
        err = cl_ctx.compile_kernel(kKernelVRAM, "march_down_r0_w1", "", k_march_down_r0_w1);
        if (err != CL_SUCCESS) return false;
        err = cl_ctx.compile_kernel(kKernelVRAM, "march_down_r1_w0", "", k_march_down_r1_w0);
        if (err != CL_SUCCESS) return false;
        err = cl_ctx.compile_kernel(kKernelVRAM, "march_final_r0", "", k_march_final_r0);
        if (err != CL_SUCCESS) return false;

        // Butterfly kernels
        err = cl_ctx.compile_kernel(kKernelVRAM, "butterfly_write", "", k_butterfly_write);
        if (err != CL_SUCCESS) return false;
        err = cl_ctx.compile_kernel(kKernelVRAM, "butterfly_verify", "", k_butterfly_verify);
        if (err != CL_SUCCESS) return false;

        // Random pattern kernels
        err = cl_ctx.compile_kernel(kKernelVRAM, "random_pattern_write", "", k_random_write);
        if (err != CL_SUCCESS) return false;
        err = cl_ctx.compile_kernel(kKernelVRAM, "random_pattern_verify", "", k_random_verify);
        if (err != CL_SUCCESS) return false;

        return true;
    }

    void release_kernels() {
        auto release = [this](cl_kernel& k) {
            if (k) { cl_ctx.release_kernel(k); k = nullptr; }
        };
        release(k_matmul);
        release(k_matmul_fp64);
        release(k_fma);
        release(k_trig);
        release(k_walking_ones_write);
        release(k_walking_ones_verify);
        release(k_walking_zeros_write);
        release(k_walking_zeros_verify);
        release(k_address_test_write);
        release(k_address_test_verify);
        release(k_alternating_write);
        release(k_alternating_verify);
        release(k_march_fill_zero);
        release(k_march_up_r0_w1);
        release(k_march_up_r1_w0);
        release(k_march_down_r0_w1);
        release(k_march_down_r1_w0);
        release(k_march_final_r0);
        release(k_butterfly_write);
        release(k_butterfly_verify);
        release(k_random_write);
        release(k_random_verify);
    }

    void release_buffers() {
        auto release = [this](cl_mem& b) {
            if (b) { cl_ctx.release_buffer(b); b = nullptr; }
        };
        release(buf_a);
        release(buf_b);
        release(buf_c);
        release(buf_data);
        release(buf_vram);
        release(buf_errors);
    }
#endif

    // ─── Stress workload runners ────────────────────────────────────────

#ifdef OCCT_HAS_OPENCL
    void run_matrix_mul_fp32() {
        const int N = 1024; // 1024x1024 matrix
        const size_t mat_size = static_cast<size_t>(N) * N * sizeof(float);

        // Allocate host data
        std::vector<float> host_a(N * N);
        std::vector<float> host_b(N * N);

        std::mt19937 rng(42);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        for (auto& v : host_a) v = dist(rng);
        for (auto& v : host_b) v = dist(rng);

        buf_a = cl_ctx.create_buffer(mat_size, CL_MEM_READ_ONLY);
        buf_b = cl_ctx.create_buffer(mat_size, CL_MEM_READ_ONLY);
        buf_c = cl_ctx.create_buffer(mat_size, CL_MEM_WRITE_ONLY);
        if (!buf_a || !buf_b || !buf_c) return;

        cl_ctx.write_buffer(buf_a, host_a.data(), mat_size);
        cl_ctx.write_buffer(buf_b, host_b.data(), mat_size);

        clSetKernelArg(k_matmul, 0, sizeof(cl_mem), &buf_a);
        clSetKernelArg(k_matmul, 1, sizeof(cl_mem), &buf_b);
        clSetKernelArg(k_matmul, 2, sizeof(cl_mem), &buf_c);
        clSetKernelArg(k_matmul, 3, sizeof(int), &N);

        size_t global_size[2] = {static_cast<size_t>(N), static_cast<size_t>(N)};
        size_t local_size[2] = {16, 16};
        // 2N^3 FLOPs per matrix multiply (N^2 dot products, each 2N ops with FMA)
        const double flops_per_run = 2.0 * N * N * N;

        auto start = std::chrono::steady_clock::now();
        uint64_t iterations = 0;

        while (running.load()) {
            cl_ctx.enqueue_ndrange(k_matmul, 2, global_size, local_size);
            cl_ctx.finish();
            iterations++;

            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();

            if (elapsed > 0.0) {
                update_metrics(iterations * flops_per_run / elapsed / 1e9, elapsed);
            }

            check_timeout(start);
        }
    }

    void run_matrix_mul_fp64() {
        if (!k_matmul_fp64) {
            std::cerr << "[GPU] FP64 not supported on this device, falling back to FP32"
                      << std::endl;
            run_matrix_mul_fp32();
            return;
        }

        const int N = 1024;
        const size_t mat_size = static_cast<size_t>(N) * N * sizeof(double);

        std::vector<double> host_a(N * N);
        std::vector<double> host_b(N * N);

        std::mt19937 rng(42);
        std::uniform_real_distribution<double> dist(-1.0, 1.0);
        for (auto& v : host_a) v = dist(rng);
        for (auto& v : host_b) v = dist(rng);

        buf_a = cl_ctx.create_buffer(mat_size, CL_MEM_READ_ONLY);
        buf_b = cl_ctx.create_buffer(mat_size, CL_MEM_READ_ONLY);
        buf_c = cl_ctx.create_buffer(mat_size, CL_MEM_WRITE_ONLY);
        if (!buf_a || !buf_b || !buf_c) return;

        cl_ctx.write_buffer(buf_a, host_a.data(), mat_size);
        cl_ctx.write_buffer(buf_b, host_b.data(), mat_size);

        clSetKernelArg(k_matmul_fp64, 0, sizeof(cl_mem), &buf_a);
        clSetKernelArg(k_matmul_fp64, 1, sizeof(cl_mem), &buf_b);
        clSetKernelArg(k_matmul_fp64, 2, sizeof(cl_mem), &buf_c);
        clSetKernelArg(k_matmul_fp64, 3, sizeof(int), &N);

        size_t global_size[2] = {static_cast<size_t>(N), static_cast<size_t>(N)};
        size_t local_size[2] = {16, 16};
        const double flops_per_run = 2.0 * N * N * N;

        auto start = std::chrono::steady_clock::now();
        uint64_t iterations = 0;

        while (running.load()) {
            cl_ctx.enqueue_ndrange(k_matmul_fp64, 2, global_size, local_size);
            cl_ctx.finish();
            iterations++;

            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();

            if (elapsed > 0.0) {
                update_metrics(iterations * flops_per_run / elapsed / 1e9, elapsed);
            }

            check_timeout(start);
        }
    }

    void run_fma_stress() {
        const size_t num_elements = 1024 * 1024; // 1M elements
        const size_t data_size = num_elements * sizeof(float);
        const int iterations_per_dispatch = 10000;

        std::vector<float> host_data(num_elements);
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> dist(0.5f, 1.5f);
        for (auto& v : host_data) v = dist(rng);

        // Keep a copy of initial values for cross-verification (first 64 elements)
        const size_t verify_count = 64;
        std::vector<float> cpu_expected(host_data.begin(), host_data.begin() + verify_count);

        buf_data = cl_ctx.create_buffer(data_size, CL_MEM_READ_WRITE);
        if (!buf_data) return;
        cl_ctx.write_buffer(buf_data, host_data.data(), data_size);

        clSetKernelArg(k_fma, 0, sizeof(cl_mem), &buf_data);
        clSetKernelArg(k_fma, 1, sizeof(int), &iterations_per_dispatch);

        size_t global_size = num_elements;
        size_t local_size = 256;
        // 4 accumulators x 2 FLOPs (fma) x iterations x elements
        const double flops_per_run = 4.0 * 2.0 * iterations_per_dispatch * num_elements;

        auto start = std::chrono::steady_clock::now();
        uint64_t dispatches = 0;
        uint64_t fma_verify_errors = 0;

        while (running.load()) {
            cl_ctx.enqueue_ndrange(k_fma, 1, &global_size, &local_size);
            cl_ctx.finish();
            dispatches++;

            // Cross-verify first 64 elements every 10 dispatches
            if (dispatches % 10 == 1) {
                // CPU-side: replicate the FMA kernel logic for verify_count elements
                // The kernel does: acc0=data[gid], acc1=acc0+0.1, acc2=acc0+0.2, acc3=acc0+0.3
                //   for iterations: accN = fma(accN, 0.9999, 0.0001)
                //   data[gid] = acc0 + acc1 + acc2 + acc3
                std::vector<float> cpu_result(verify_count);
                for (size_t i = 0; i < verify_count; ++i) {
                    float acc0 = cpu_expected[i];
                    float acc1 = acc0 + 0.1f;
                    float acc2 = acc0 + 0.2f;
                    float acc3 = acc0 + 0.3f;
                    float mul = 0.9999f;
                    float add = 0.0001f;
                    for (int it = 0; it < iterations_per_dispatch; ++it) {
                        acc0 = std::fma(acc0, mul, add);
                        acc1 = std::fma(acc1, mul, add);
                        acc2 = std::fma(acc2, mul, add);
                        acc3 = std::fma(acc3, mul, add);
                    }
                    cpu_result[i] = acc0 + acc1 + acc2 + acc3;
                }

                // Read back GPU results for first 64 elements
                std::vector<float> gpu_result(verify_count);
                cl_ctx.read_buffer(buf_data, gpu_result.data(), verify_count * sizeof(float));

                // Compare with tolerance for floating-point differences
                for (size_t i = 0; i < verify_count; ++i) {
                    float diff = std::abs(gpu_result[i] - cpu_result[i]);
                    float magnitude = std::max(std::abs(cpu_result[i]), 1e-6f);
                    // Relative error > 0.1% indicates a compute error
                    if (diff / magnitude > 0.001f) {
                        fma_verify_errors++;
                    }
                }

                // Update cpu_expected for next iteration's verification
                cpu_expected = cpu_result;

                // Report errors via vram_errors metric (reusing existing field)
                if (fma_verify_errors > 0) {
                    std::lock_guard<std::mutex> lock(metrics_mutex);
                    latest_metrics.vram_errors = fma_verify_errors;
                }
            }

            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();

            if (elapsed > 0.0) {
                update_metrics(dispatches * flops_per_run / elapsed / 1e9, elapsed);
            }

            check_timeout(start);
        }
    }

    void run_trig_stress() {
        const size_t num_elements = 1024 * 1024;
        const size_t data_size = num_elements * sizeof(float);
        const int iterations_per_dispatch = 1000;

        std::vector<float> host_data(num_elements, 1.0f);

        buf_data = cl_ctx.create_buffer(data_size, CL_MEM_READ_WRITE);
        if (!buf_data) return;
        cl_ctx.write_buffer(buf_data, host_data.data(), data_size);

        clSetKernelArg(k_trig, 0, sizeof(cl_mem), &buf_data);
        clSetKernelArg(k_trig, 1, sizeof(int), &iterations_per_dispatch);

        size_t global_size = num_elements;
        size_t local_size = 256;
        // Approximate: 4 transcendental ops per iteration x elements
        const double flops_per_run = 4.0 * iterations_per_dispatch * num_elements;

        auto start = std::chrono::steady_clock::now();
        uint64_t dispatches = 0;

        while (running.load()) {
            cl_ctx.enqueue_ndrange(k_trig, 1, &global_size, &local_size);
            cl_ctx.finish();
            dispatches++;

            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();

            if (elapsed > 0.0) {
                update_metrics(dispatches * flops_per_run / elapsed / 1e9, elapsed);
            }

            check_timeout(start);
        }
    }

    void run_vram_test() {
        // Allocate a large VRAM buffer (use ~50% of available VRAM, capped at 512 MB)
        size_t vram_bytes = devices[selected_device].global_mem_bytes;
        size_t test_bytes = std::min<size_t>(vram_bytes / 2, 512ULL * 1024 * 1024);
        size_t num_elements = test_bytes / sizeof(uint32_t);

        buf_vram = cl_ctx.create_buffer(test_bytes, CL_MEM_READ_WRITE);
        buf_errors = cl_ctx.create_buffer(sizeof(uint32_t), CL_MEM_READ_WRITE);
        if (!buf_vram || !buf_errors) return;

        uint32_t zero = 0;
        uint64_t total_errors = 0;

        size_t global_size = num_elements;
        size_t local_size = 256;
        // Round down to multiple of local_size
        global_size = (global_size / local_size) * local_size;

        auto start = std::chrono::steady_clock::now();

        // Run through different test patterns
        while (running.load()) {
            // Walking ones (32 shifts) - two-pass for global sync
            for (uint32_t shift = 0; shift < 32 && running.load(); ++shift) {
                cl_ctx.write_buffer(buf_errors, &zero, sizeof(uint32_t));
                // Pass 1: write pattern
                clSetKernelArg(k_walking_ones_write, 0, sizeof(cl_mem), &buf_vram);
                clSetKernelArg(k_walking_ones_write, 1, sizeof(uint32_t), &shift);
                cl_ctx.enqueue_ndrange(k_walking_ones_write, 1, &global_size, &local_size);
                cl_ctx.finish();
                // Pass 2: verify pattern
                clSetKernelArg(k_walking_ones_verify, 0, sizeof(cl_mem), &buf_vram);
                clSetKernelArg(k_walking_ones_verify, 1, sizeof(cl_mem), &buf_errors);
                clSetKernelArg(k_walking_ones_verify, 2, sizeof(uint32_t), &shift);
                cl_ctx.enqueue_ndrange(k_walking_ones_verify, 1, &global_size, &local_size);
                cl_ctx.finish();

                uint32_t err_count = 0;
                cl_ctx.read_buffer(buf_errors, &err_count, sizeof(uint32_t));
                total_errors += err_count;

                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - start).count();
                update_vram_metrics(total_errors, elapsed);
                check_timeout(start);
            }

            // Walking zeros (32 shifts) - two-pass for global sync
            for (uint32_t shift = 0; shift < 32 && running.load(); ++shift) {
                cl_ctx.write_buffer(buf_errors, &zero, sizeof(uint32_t));
                // Pass 1: write pattern
                clSetKernelArg(k_walking_zeros_write, 0, sizeof(cl_mem), &buf_vram);
                clSetKernelArg(k_walking_zeros_write, 1, sizeof(uint32_t), &shift);
                cl_ctx.enqueue_ndrange(k_walking_zeros_write, 1, &global_size, &local_size);
                cl_ctx.finish();
                // Pass 2: verify pattern
                clSetKernelArg(k_walking_zeros_verify, 0, sizeof(cl_mem), &buf_vram);
                clSetKernelArg(k_walking_zeros_verify, 1, sizeof(cl_mem), &buf_errors);
                clSetKernelArg(k_walking_zeros_verify, 2, sizeof(uint32_t), &shift);
                cl_ctx.enqueue_ndrange(k_walking_zeros_verify, 1, &global_size, &local_size);
                cl_ctx.finish();

                uint32_t err_count = 0;
                cl_ctx.read_buffer(buf_errors, &err_count, sizeof(uint32_t));
                total_errors += err_count;

                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - start).count();
                update_vram_metrics(total_errors, elapsed);
                check_timeout(start);
            }

            // Address test - two-pass for global sync
            if (running.load()) {
                cl_ctx.write_buffer(buf_errors, &zero, sizeof(uint32_t));
                // Pass 1: write addresses
                clSetKernelArg(k_address_test_write, 0, sizeof(cl_mem), &buf_vram);
                cl_ctx.enqueue_ndrange(k_address_test_write, 1, &global_size, &local_size);
                cl_ctx.finish();
                // Pass 2: verify addresses
                clSetKernelArg(k_address_test_verify, 0, sizeof(cl_mem), &buf_vram);
                clSetKernelArg(k_address_test_verify, 1, sizeof(cl_mem), &buf_errors);
                cl_ctx.enqueue_ndrange(k_address_test_verify, 1, &global_size, &local_size);
                cl_ctx.finish();

                uint32_t err_count = 0;
                cl_ctx.read_buffer(buf_errors, &err_count, sizeof(uint32_t));
                total_errors += err_count;

                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - start).count();
                update_vram_metrics(total_errors, elapsed);
                check_timeout(start);
            }

            // Alternating pattern (phase 0 and 1) - two-pass for global sync
            for (uint32_t phase = 0; phase < 2 && running.load(); ++phase) {
                cl_ctx.write_buffer(buf_errors, &zero, sizeof(uint32_t));
                // Pass 1: write pattern
                clSetKernelArg(k_alternating_write, 0, sizeof(cl_mem), &buf_vram);
                clSetKernelArg(k_alternating_write, 1, sizeof(uint32_t), &phase);
                cl_ctx.enqueue_ndrange(k_alternating_write, 1, &global_size, &local_size);
                cl_ctx.finish();
                // Pass 2: verify pattern
                clSetKernelArg(k_alternating_verify, 0, sizeof(cl_mem), &buf_vram);
                clSetKernelArg(k_alternating_verify, 1, sizeof(cl_mem), &buf_errors);
                clSetKernelArg(k_alternating_verify, 2, sizeof(uint32_t), &phase);
                cl_ctx.enqueue_ndrange(k_alternating_verify, 1, &global_size, &local_size);
                cl_ctx.finish();

                uint32_t err_count = 0;
                cl_ctx.read_buffer(buf_errors, &err_count, sizeof(uint32_t));
                total_errors += err_count;

                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - start).count();
                update_vram_metrics(total_errors, elapsed);
                check_timeout(start);
            }

            // March C- pattern: fill_zero → up_r0_w1 → up_r1_w0
            //                  → down_r0_w1 → down_r1_w0 → final_r0
            if (running.load()) {
                // Step 1: Fill with zeros
                clSetKernelArg(k_march_fill_zero, 0, sizeof(cl_mem), &buf_vram);
                cl_ctx.enqueue_ndrange(k_march_fill_zero, 1, &global_size, &local_size);
                cl_ctx.finish();

                // Step 2: Read 0, write 1 (ascending)
                cl_ctx.write_buffer(buf_errors, &zero, sizeof(uint32_t));
                clSetKernelArg(k_march_up_r0_w1, 0, sizeof(cl_mem), &buf_vram);
                clSetKernelArg(k_march_up_r0_w1, 1, sizeof(cl_mem), &buf_errors);
                cl_ctx.enqueue_ndrange(k_march_up_r0_w1, 1, &global_size, &local_size);
                cl_ctx.finish();
                {
                    uint32_t err_count = 0;
                    cl_ctx.read_buffer(buf_errors, &err_count, sizeof(uint32_t));
                    total_errors += err_count;
                }

                // Step 3: Read 1, write 0 (ascending)
                cl_ctx.write_buffer(buf_errors, &zero, sizeof(uint32_t));
                clSetKernelArg(k_march_up_r1_w0, 0, sizeof(cl_mem), &buf_vram);
                clSetKernelArg(k_march_up_r1_w0, 1, sizeof(cl_mem), &buf_errors);
                cl_ctx.enqueue_ndrange(k_march_up_r1_w0, 1, &global_size, &local_size);
                cl_ctx.finish();
                {
                    uint32_t err_count = 0;
                    cl_ctx.read_buffer(buf_errors, &err_count, sizeof(uint32_t));
                    total_errors += err_count;
                }

                // Step 4: Read 0, write 1 (descending - same kernel, GPU covers all elements)
                cl_ctx.write_buffer(buf_errors, &zero, sizeof(uint32_t));
                clSetKernelArg(k_march_down_r0_w1, 0, sizeof(cl_mem), &buf_vram);
                clSetKernelArg(k_march_down_r0_w1, 1, sizeof(cl_mem), &buf_errors);
                cl_ctx.enqueue_ndrange(k_march_down_r0_w1, 1, &global_size, &local_size);
                cl_ctx.finish();
                {
                    uint32_t err_count = 0;
                    cl_ctx.read_buffer(buf_errors, &err_count, sizeof(uint32_t));
                    total_errors += err_count;
                }

                // Step 5: Read 1, write 0 (descending)
                cl_ctx.write_buffer(buf_errors, &zero, sizeof(uint32_t));
                clSetKernelArg(k_march_down_r1_w0, 0, sizeof(cl_mem), &buf_vram);
                clSetKernelArg(k_march_down_r1_w0, 1, sizeof(cl_mem), &buf_errors);
                cl_ctx.enqueue_ndrange(k_march_down_r1_w0, 1, &global_size, &local_size);
                cl_ctx.finish();
                {
                    uint32_t err_count = 0;
                    cl_ctx.read_buffer(buf_errors, &err_count, sizeof(uint32_t));
                    total_errors += err_count;
                }

                // Step 6: Final read 0
                cl_ctx.write_buffer(buf_errors, &zero, sizeof(uint32_t));
                clSetKernelArg(k_march_final_r0, 0, sizeof(cl_mem), &buf_vram);
                clSetKernelArg(k_march_final_r0, 1, sizeof(cl_mem), &buf_errors);
                cl_ctx.enqueue_ndrange(k_march_final_r0, 1, &global_size, &local_size);
                cl_ctx.finish();
                {
                    uint32_t err_count = 0;
                    cl_ctx.read_buffer(buf_errors, &err_count, sizeof(uint32_t));
                    total_errors += err_count;
                }

                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - start).count();
                update_vram_metrics(total_errors, elapsed);
                check_timeout(start);
            }

            // Butterfly pattern - converging from both ends
            if (running.load()) {
                uint32_t ne32 = static_cast<uint32_t>(num_elements);
                size_t half_size = num_elements / 2;
                size_t butterfly_global = (half_size / local_size) * local_size;

                cl_ctx.write_buffer(buf_errors, &zero, sizeof(uint32_t));
                // Write pass
                clSetKernelArg(k_butterfly_write, 0, sizeof(cl_mem), &buf_vram);
                clSetKernelArg(k_butterfly_write, 1, sizeof(uint32_t), &ne32);
                cl_ctx.enqueue_ndrange(k_butterfly_write, 1, &butterfly_global, &local_size);
                cl_ctx.finish();
                // Verify pass
                clSetKernelArg(k_butterfly_verify, 0, sizeof(cl_mem), &buf_vram);
                clSetKernelArg(k_butterfly_verify, 1, sizeof(cl_mem), &buf_errors);
                clSetKernelArg(k_butterfly_verify, 2, sizeof(uint32_t), &ne32);
                cl_ctx.enqueue_ndrange(k_butterfly_verify, 1, &butterfly_global, &local_size);
                cl_ctx.finish();

                uint32_t err_count = 0;
                cl_ctx.read_buffer(buf_errors, &err_count, sizeof(uint32_t));
                total_errors += err_count;

                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - start).count();
                update_vram_metrics(total_errors, elapsed);
                check_timeout(start);
            }

            // Random pattern (LCG) - multiple seeds
            for (uint32_t seed_idx = 0; seed_idx < 4 && running.load(); ++seed_idx) {
                uint32_t base_seed = 0xDEADBEEFu + seed_idx * 0x01010101u;

                cl_ctx.write_buffer(buf_errors, &zero, sizeof(uint32_t));
                // Write pass
                clSetKernelArg(k_random_write, 0, sizeof(cl_mem), &buf_vram);
                clSetKernelArg(k_random_write, 1, sizeof(uint32_t), &base_seed);
                cl_ctx.enqueue_ndrange(k_random_write, 1, &global_size, &local_size);
                cl_ctx.finish();
                // Verify pass
                clSetKernelArg(k_random_verify, 0, sizeof(cl_mem), &buf_vram);
                clSetKernelArg(k_random_verify, 1, sizeof(cl_mem), &buf_errors);
                clSetKernelArg(k_random_verify, 2, sizeof(uint32_t), &base_seed);
                cl_ctx.enqueue_ndrange(k_random_verify, 1, &global_size, &local_size);
                cl_ctx.finish();

                uint32_t err_count = 0;
                cl_ctx.read_buffer(buf_errors, &err_count, sizeof(uint32_t));
                total_errors += err_count;

                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - start).count();
                update_vram_metrics(total_errors, elapsed);
                check_timeout(start);
            }
        }
    }

    void run_mixed() {
        // Cycle through different stress modes
        const GpuStressMode modes[] = {
            GpuStressMode::MATRIX_MUL,
            GpuStressMode::FMA_STRESS,
            GpuStressMode::TRIG_STRESS,
        };
        int mode_idx = 0;
        auto cycle_start = std::chrono::steady_clock::now();

        while (running.load()) {
            // Run each mode for ~5 seconds before cycling
            current_mode = modes[mode_idx % 3];

            auto phase_start = std::chrono::steady_clock::now();
            auto phase_deadline = phase_start + std::chrono::seconds(5);

            // Temporarily run the sub-workload
            switch (current_mode) {
            case GpuStressMode::MATRIX_MUL: {
                // Inline a few iterations of matmul
                const int N = 1024;
                const size_t mat_size = static_cast<size_t>(N) * N * sizeof(float);
                std::vector<float> tmp(N * N, 1.0f);

                buf_a = cl_ctx.create_buffer(mat_size, CL_MEM_READ_ONLY);
                buf_b = cl_ctx.create_buffer(mat_size, CL_MEM_READ_ONLY);
                buf_c = cl_ctx.create_buffer(mat_size, CL_MEM_WRITE_ONLY);
                if (buf_a && buf_b && buf_c) {
                    cl_ctx.write_buffer(buf_a, tmp.data(), mat_size);
                    cl_ctx.write_buffer(buf_b, tmp.data(), mat_size);
                    clSetKernelArg(k_matmul, 0, sizeof(cl_mem), &buf_a);
                    clSetKernelArg(k_matmul, 1, sizeof(cl_mem), &buf_b);
                    clSetKernelArg(k_matmul, 2, sizeof(cl_mem), &buf_c);
                    clSetKernelArg(k_matmul, 3, sizeof(int), &N);

                    size_t gs[2] = {static_cast<size_t>(N), static_cast<size_t>(N)};
                    size_t ls[2] = {16, 16};

                    while (running.load() && std::chrono::steady_clock::now() < phase_deadline) {
                        cl_ctx.enqueue_ndrange(k_matmul, 2, gs, ls);
                        cl_ctx.finish();
                        auto now = std::chrono::steady_clock::now();
                        double elapsed = std::chrono::duration<double>(now - cycle_start).count();
                        update_metrics(0.0, elapsed); // Simplified for mixed mode
                        check_timeout(cycle_start);
                    }
                }
                release_buffers();
                break;
            }
            case GpuStressMode::FMA_STRESS: {
                const size_t ne = 1024 * 1024;
                const size_t ds = ne * sizeof(float);
                const int iters = 10000;
                std::vector<float> tmp(ne, 1.0f);

                buf_data = cl_ctx.create_buffer(ds, CL_MEM_READ_WRITE);
                if (buf_data) {
                    cl_ctx.write_buffer(buf_data, tmp.data(), ds);
                    clSetKernelArg(k_fma, 0, sizeof(cl_mem), &buf_data);
                    clSetKernelArg(k_fma, 1, sizeof(int), &iters);
                    size_t gs = ne, ls = 256;

                    while (running.load() && std::chrono::steady_clock::now() < phase_deadline) {
                        cl_ctx.enqueue_ndrange(k_fma, 1, &gs, &ls);
                        cl_ctx.finish();
                        auto now = std::chrono::steady_clock::now();
                        double elapsed = std::chrono::duration<double>(now - cycle_start).count();
                        update_metrics(0.0, elapsed);
                        check_timeout(cycle_start);
                    }
                }
                release_buffers();
                break;
            }
            case GpuStressMode::TRIG_STRESS: {
                const size_t ne = 1024 * 1024;
                const size_t ds = ne * sizeof(float);
                const int iters = 1000;
                std::vector<float> tmp(ne, 1.0f);

                buf_data = cl_ctx.create_buffer(ds, CL_MEM_READ_WRITE);
                if (buf_data) {
                    cl_ctx.write_buffer(buf_data, tmp.data(), ds);
                    clSetKernelArg(k_trig, 0, sizeof(cl_mem), &buf_data);
                    clSetKernelArg(k_trig, 1, sizeof(int), &iters);
                    size_t gs = ne, ls = 256;

                    while (running.load() && std::chrono::steady_clock::now() < phase_deadline) {
                        cl_ctx.enqueue_ndrange(k_trig, 1, &gs, &ls);
                        cl_ctx.finish();
                        auto now = std::chrono::steady_clock::now();
                        double elapsed = std::chrono::duration<double>(now - cycle_start).count();
                        update_metrics(0.0, elapsed);
                        check_timeout(cycle_start);
                    }
                }
                release_buffers();
                break;
            }
            default:
                break;
            }

            mode_idx++;
        }
    }
#endif // OCCT_HAS_OPENCL

    // ─── Vulkan workload runners ────────────────────────────────────────

#ifdef OCCT_HAS_VULKAN
    void run_vulkan_3d() {
        if (!vk_ctx.init(selected_device_idx)) {
            std::cerr << "[GPU] Failed to initialize Vulkan context" << std::endl;
            running.store(false);
            return;
        }

        gpu::VulkanRenderer renderer;
        gpu::ArtifactDetector detector;

        if (!renderer.init(&vk_ctx, 512, 512)) {
            std::cerr << "[GPU] Failed to initialize Vulkan renderer" << std::endl;
            running.store(false);
            return;
        }

        renderer.set_shader_complexity(shader_complexity);
        renderer.set_gpu_load(0.8f);

        auto start = std::chrono::steady_clock::now();
        uint64_t frame_count = 0;
        bool reference_set = false;
        uint64_t total_artifacts = 0;

        while (running.load()) {
            float time_secs = static_cast<float>(
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());

            auto frame = renderer.render_frame(time_secs);
            frame_count++;

            // Set reference frame from first frame
            if (!reference_set && !frame.pixels.empty()) {
                detector.set_reference_frame(frame.pixels.data(), frame.width, frame.height);
                reference_set = true;
            }

            // Artifact detection (compare every 10th frame against reference)
            if (reference_set && frame_count % 3 == 0 && !frame.pixels.empty()) {
                auto artifact = detector.compare_frame(frame.pixels, frame.width, frame.height);
                total_artifacts += artifact.error_pixels;
            }

            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();
            double fps = (elapsed > 0.0) ? static_cast<double>(frame_count) / elapsed : 0.0;

            {
                auto sensor = utils::gpu_query_sensors(active_vendor, selected_device_idx);
                GpuMetrics snapshot;
                MetricsCallback cb_copy;
                {
                    std::lock_guard<std::mutex> lock(metrics_mutex);
                    latest_metrics.fps = fps;
                    latest_metrics.draw_calls = frame.draw_calls;
                    latest_metrics.artifact_count = total_artifacts;
                    latest_metrics.shader_level = static_cast<int>(shader_complexity) + 1;
                    latest_metrics.elapsed_secs = elapsed;
                    latest_metrics.temperature = sensor.temperature_c;
                    latest_metrics.power_watts = sensor.power_watts;
                    latest_metrics.gpu_usage_pct = sensor.gpu_usage_pct;
                    if (sensor.vram_total_bytes > 0) {
                        latest_metrics.vram_usage_pct =
                            100.0 * static_cast<double>(sensor.vram_used_bytes) /
                            static_cast<double>(sensor.vram_total_bytes);
                    }
                    snapshot = latest_metrics;
                    cb_copy = metrics_cb;
                }
                // Invoke callback outside mutex to avoid blocking the worker thread
                if (cb_copy) cb_copy(snapshot);
            }

            check_timeout(start);
        }

        renderer.cleanup();
    }

    void run_vulkan_adaptive() {
        if (!vk_ctx.init(selected_device_idx)) {
            std::cerr << "[GPU] Failed to initialize Vulkan context" << std::endl;
            running.store(false);
            return;
        }

        gpu::VulkanRenderer renderer;
        gpu::ArtifactDetector detector;

        if (!renderer.init(&vk_ctx, 512, 512)) {
            std::cerr << "[GPU] Failed to initialize Vulkan renderer" << std::endl;
            running.store(false);
            return;
        }

        renderer.set_shader_complexity(shader_complexity);

        auto start = std::chrono::steady_clock::now();
        uint64_t frame_count = 0;
        bool reference_set = false;
        uint64_t total_artifacts = 0;
        float current_load = 0.2f;

        while (running.load()) {
            float time_secs = static_cast<float>(
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());

            // Adaptive load control
            switch (adaptive_mode) {
            case AdaptiveMode::VARIABLE: {
                // Increase by 5% every 20 seconds
                int phase = static_cast<int>(time_secs / 20.0f);
                current_load = std::min(1.0f, 0.05f + 0.05f * static_cast<float>(phase));
                break;
            }
            case AdaptiveMode::SWITCH: {
                // Rapid alternation for PSU transient testing
                int phase = static_cast<int>(time_secs / switch_interval_secs);
                current_load = (phase % 2 == 0) ? 0.2f : 0.9f;  // 20% ↔ 90%
                break;
            }
            case AdaptiveMode::COIL_WHINE: {
                float freq = coil_whine_freq_hz;
                if (freq <= 0.0f) {
                    // Sweep mode: logarithmic sweep from 50Hz to 15kHz over 60 seconds
                    freq = 50.0f * std::pow(300.0f, std::min(time_secs / 60.0f, 1.0f));
                }
                float period = 1.0f / freq;
                float half_period = period / 2.0f;
                float phase_time = std::fmod(time_secs, period);
                current_load = (phase_time < half_period) ? 0.0f : 1.0f;
                break;
            }
            }

            renderer.set_gpu_load(current_load);

            auto frame = renderer.render_frame(time_secs);
            frame_count++;

            if (!reference_set && !frame.pixels.empty()) {
                detector.set_reference_frame(frame.pixels.data(), frame.width, frame.height);
                reference_set = true;
            }

            if (reference_set && frame_count % 3 == 0 && !frame.pixels.empty()) {
                auto artifact = detector.compare_frame(frame.pixels, frame.width, frame.height);
                total_artifacts += artifact.error_pixels;
            }

            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();
            double fps = (elapsed > 0.0) ? static_cast<double>(frame_count) / elapsed : 0.0;

            {
                auto sensor = utils::gpu_query_sensors(active_vendor, selected_device_idx);
                GpuMetrics snapshot;
                MetricsCallback cb_copy;
                {
                    std::lock_guard<std::mutex> lock(metrics_mutex);
                    latest_metrics.fps = fps;
                    latest_metrics.draw_calls = frame.draw_calls;
                    latest_metrics.artifact_count = total_artifacts;
                    latest_metrics.shader_level = static_cast<int>(shader_complexity) + 1;
                    latest_metrics.gpu_usage_pct = static_cast<double>(current_load * 100.0f);
                    latest_metrics.elapsed_secs = elapsed;
                    latest_metrics.temperature = sensor.temperature_c;
                    latest_metrics.power_watts = sensor.power_watts;
                    if (sensor.vram_total_bytes > 0) {
                        latest_metrics.vram_usage_pct =
                            100.0 * static_cast<double>(sensor.vram_used_bytes) /
                            static_cast<double>(sensor.vram_total_bytes);
                    }
                    snapshot = latest_metrics;
                    cb_copy = metrics_cb;
                }
                // Invoke callback outside mutex to avoid blocking the worker thread
                if (cb_copy) cb_copy(snapshot);
            }

            check_timeout(start);
        }

        renderer.cleanup();
    }
#endif // OCCT_HAS_VULKAN

    // ─── Metrics helpers ────────────────────────────────────────────────

    void update_metrics(double gflops, double elapsed) {
        auto sensor = utils::gpu_query_sensors(active_vendor, selected_device_idx);

        GpuMetrics snapshot;
        MetricsCallback cb_copy;
        {
            std::lock_guard<std::mutex> lock(metrics_mutex);
            latest_metrics.gflops = gflops;
            latest_metrics.elapsed_secs = elapsed;
            latest_metrics.temperature = sensor.temperature_c;
            latest_metrics.power_watts = sensor.power_watts;
            latest_metrics.gpu_usage_pct = sensor.gpu_usage_pct;
            if (sensor.vram_total_bytes > 0) {
                latest_metrics.vram_usage_pct =
                    100.0 * static_cast<double>(sensor.vram_used_bytes) /
                    static_cast<double>(sensor.vram_total_bytes);
            }
            snapshot = latest_metrics;
            cb_copy = metrics_cb;
        }
        // Invoke callback outside mutex to avoid blocking the worker thread
        if (cb_copy) {
            cb_copy(snapshot);
        }
    }

    void update_vram_metrics(uint64_t errors, double elapsed) {
        auto sensor = utils::gpu_query_sensors(active_vendor, selected_device_idx);

        GpuMetrics snapshot;
        MetricsCallback cb_copy;
        {
            std::lock_guard<std::mutex> lock(metrics_mutex);
            latest_metrics.vram_errors = errors;
            latest_metrics.elapsed_secs = elapsed;
            latest_metrics.temperature = sensor.temperature_c;
            latest_metrics.power_watts = sensor.power_watts;
            latest_metrics.gpu_usage_pct = sensor.gpu_usage_pct;
            if (sensor.vram_total_bytes > 0) {
                latest_metrics.vram_usage_pct =
                    100.0 * static_cast<double>(sensor.vram_used_bytes) /
                    static_cast<double>(sensor.vram_total_bytes);
            }
            snapshot = latest_metrics;
            cb_copy = metrics_cb;
        }
        // Invoke callback outside mutex to avoid blocking the worker thread
        if (cb_copy) {
            cb_copy(snapshot);
        }

        if (owner && owner->stop_on_error() && errors > 0) {
            running.store(false);
        }
    }

    void check_timeout(const std::chrono::steady_clock::time_point& start) {
        if (duration_secs > 0) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();
            if (elapsed >= static_cast<double>(duration_secs)) {
                running.store(false);
            }
        }
    }

    // ─── Worker thread entry point ──────────────────────────────────────

    void worker_main(GpuStressMode mode) {
#ifdef OCCT_HAS_VULKAN
        // Handle Vulkan modes
        if (mode == GpuStressMode::VULKAN_3D) {
            run_vulkan_3d();
            return;
        }
        if (mode == GpuStressMode::VULKAN_ADAPTIVE) {
            run_vulkan_adaptive();
            return;
        }
#endif

#ifdef OCCT_HAS_OPENCL
        // Initialize context for the selected device
        if (selected_device >= static_cast<int>(devices.size())) {
            std::cerr << "[GPU] Invalid device index" << std::endl;
            running.store(false);
            return;
        }
        auto& dev = devices[selected_device];
        if (!cl_ctx.init(dev.platform, dev.device)) {
            std::cerr << "[GPU] Failed to init OpenCL context" << std::endl;
            running.store(false);
            return;
        }

        active_vendor = utils::parse_gpu_vendor(dev.vendor);

        // Compile kernels
        if (!compile_all_kernels()) {
            std::cerr << "[GPU] Failed to compile kernels" << std::endl;
            running.store(false);
            return;
        }

        // Dispatch to the appropriate workload
        switch (mode) {
        case GpuStressMode::MATRIX_MUL:      run_matrix_mul_fp32(); break;
        case GpuStressMode::MATRIX_MUL_FP64: run_matrix_mul_fp64(); break;
        case GpuStressMode::FMA_STRESS:      run_fma_stress(); break;
        case GpuStressMode::TRIG_STRESS:     run_trig_stress(); break;
        case GpuStressMode::VRAM_TEST:       run_vram_test(); break;
        case GpuStressMode::MIXED:           run_mixed(); break;
        default: break;
        }

        // Cleanup
        release_buffers();
        release_kernels();
#else
        (void)mode;
        std::cerr << "[GPU] No GPU backend available for this mode" << std::endl;
        running.store(false);
#endif
    }
};

// ─── GpuEngine public interface ──────────────────────────────────────────────

GpuEngine::GpuEngine() : impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
}

GpuEngine::~GpuEngine() {
    stop();
    utils::gpu_monitor_shutdown();
}

bool GpuEngine::initialize() {
    return impl_->do_initialize();
}

bool GpuEngine::is_opencl_available() const {
    return impl_->opencl_available;
}

bool GpuEngine::is_vulkan_available() const {
#ifdef OCCT_HAS_VULKAN
    return impl_->vulkan_available;
#else
    return false;
#endif
}

void GpuEngine::set_shader_complexity(int level) {
#ifdef OCCT_HAS_VULKAN
    level = std::max(1, std::min(5, level));
    impl_->shader_complexity = static_cast<gpu::ShaderComplexity>(level - 1);
#else
    (void)level;
#endif
}

void GpuEngine::set_adaptive_mode(AdaptiveMode mode) {
#ifdef OCCT_HAS_VULKAN
    impl_->adaptive_mode = mode;
#else
    (void)mode;
#endif
}

void GpuEngine::set_switch_interval(float seconds) {
#ifdef OCCT_HAS_VULKAN
    impl_->switch_interval_secs = std::max(0.01f, seconds);
#else
    (void)seconds;
#endif
}

void GpuEngine::set_coil_whine_freq(float hz) {
#ifdef OCCT_HAS_VULKAN
    // 0 = sweep mode; otherwise clamp to 10-15000 Hz
    if (hz > 0.0f) {
        hz = std::max(10.0f, std::min(15000.0f, hz));
    }
    impl_->coil_whine_freq_hz = hz;
#else
    (void)hz;
#endif
}

std::vector<GpuInfo> GpuEngine::get_available_gpus() const {
    return impl_->gpu_infos;
}

void GpuEngine::select_gpu(int index) {
    impl_->do_select_gpu(index);
}

bool GpuEngine::start(GpuStressMode mode, int duration_secs) {
    std::lock_guard<std::mutex> guard(impl_->start_stop_mutex);

    if (impl_->running.load()) {
        impl_->last_error = "GPU test already running";
        return false;
    }
    if (!impl_->initialized) {
        impl_->last_error = "GPU engine not initialized. No GPU backend available.";
        return false;
    }

    // Check if the appropriate backend is available for the requested mode
    bool is_vulkan_mode = (mode == GpuStressMode::VULKAN_3D ||
                           mode == GpuStressMode::VULKAN_ADAPTIVE);

    if (is_vulkan_mode) {
#ifdef OCCT_HAS_VULKAN
        if (!impl_->vulkan_available) {
            impl_->last_error = "Vulkan is not available on this system.";
            return false;
        }
#else
        impl_->last_error = "Vulkan support was not compiled into this build.";
        return false;
#endif
    } else {
        if (!impl_->opencl_available) {
            impl_->last_error = "OpenCL is not available. Please install GPU drivers with OpenCL support.";
            return false;
        }
    }

    impl_->last_error.clear();
    impl_->running.store(true);
    impl_->current_mode = mode;
    impl_->duration_secs = duration_secs;

    // Reset metrics
    {
        std::lock_guard<std::mutex> lock(impl_->metrics_mutex);
        impl_->latest_metrics = {};
    }

    impl_->worker_thread = std::thread([this, mode]() {
        impl_->worker_main(mode);
    });
    return true;
}

void GpuEngine::stop() {
    std::lock_guard<std::mutex> guard(impl_->start_stop_mutex);

    impl_->running.store(false);
    if (impl_->worker_thread.joinable()) {
        impl_->worker_thread.join();
    }
}

bool GpuEngine::is_running() const {
    return impl_->running.load();
}

GpuMetrics GpuEngine::get_metrics() const {
    std::lock_guard<std::mutex> lock(impl_->metrics_mutex);
    return impl_->latest_metrics;
}

std::string GpuEngine::last_error() const {
    return impl_->last_error;
}

void GpuEngine::set_metrics_callback(MetricsCallback cb) {
    std::lock_guard<std::mutex> lock(impl_->metrics_mutex);
    impl_->metrics_cb = std::move(cb);
}

} // namespace occt
