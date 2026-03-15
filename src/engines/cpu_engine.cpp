#include "cpu_engine.h"
#include "cpu/avx_stress.h"
#include "cpu/linpack.h"
#include "cpu/prime.h"
#include "../utils/cpuid.h"
#include "../monitor/sensor_manager.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#elif defined(__linux__)
    #include <pthread.h>
    #include <sched.h>
#elif defined(__APPLE__)
    #include <mach/thread_act.h>
    #include <mach/thread_policy.h>
    #include <pthread.h>
#endif

namespace occt {

CpuEngine::CpuEngine() = default;

CpuEngine::~CpuEngine() {
    stop();
}

void CpuEngine::set_thread_affinity(int core_id) {
#if defined(_WIN32)
    DWORD_PTR mask = 1ULL << core_id;
    DWORD_PTR result = SetThreadAffinityMask(GetCurrentThread(), mask);
    if (result == 0) {
        // Affinity setting can fail on certain Windows configurations
        // (e.g., processor groups with >64 cores, restricted accounts).
        // Log and continue - the thread will still stress the CPU, just
        // without being pinned to a specific core.
        std::cerr << "[CPU] Warning: SetThreadAffinityMask failed for core "
                  << core_id << " (error " << GetLastError()
                  << "), continuing without affinity pinning" << std::endl;
    }
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        std::cerr << "[CPU] Warning: pthread_setaffinity_np failed for core "
                  << core_id << " (error " << ret
                  << "), continuing without affinity pinning" << std::endl;
    }
#elif defined(__APPLE__)
    // macOS does not support strict CPU affinity.
    // Use thread affinity policy as a hint to the scheduler.
    thread_affinity_policy_data_t policy;
    policy.affinity_tag = core_id + 1; // 0 means no affinity
    kern_return_t kr = thread_policy_set(
        pthread_mach_thread_np(pthread_self()),
        THREAD_AFFINITY_POLICY,
        reinterpret_cast<thread_policy_t>(&policy),
        THREAD_AFFINITY_POLICY_COUNT
    );
    if (kr != KERN_SUCCESS) {
        std::cerr << "[CPU] Warning: thread_policy_set failed for core "
                  << core_id << ", continuing without affinity hint" << std::endl;
    }
#endif
}

void CpuEngine::start(CpuStressMode mode, int num_threads, int duration_secs,
                       LoadPattern pattern, CpuIntensityMode intensity) {
    std::lock_guard<std::mutex> guard(start_stop_mutex_);

    if (running_.load()) {
        // Must release start_stop_mutex_ before calling stop() to avoid deadlock,
        // but since we hold it, do the stop logic inline instead.
        running_.store(false);

        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
        workers_.clear();

        if (cycling_thread_.joinable()) cycling_thread_.join();
        if (metrics_thread_.joinable()) metrics_thread_.join();
    }

    mode_ = mode;
    duration_secs_ = duration_secs;
    load_pattern_ = pattern;
    intensity_mode_ = intensity;

    // Resolve AUTO to best available ISA
    if (mode_ == CpuStressMode::AUTO) {
        if (cpu::has_avx512f()) mode_ = CpuStressMode::AVX512_FMA;
        else if (cpu::has_avx2() && cpu::has_fma()) mode_ = CpuStressMode::AVX2_FMA;
        else mode_ = CpuStressMode::SSE_FLOAT;
    }

    // Auto-detect thread count
    if (num_threads <= 0) {
        num_threads_ = static_cast<int>(std::thread::hardware_concurrency());
        if (num_threads_ <= 0) num_threads_ = 4;
    } else {
        num_threads_ = num_threads;
    }

    // Initialize per-thread counters
    thread_ops_ = std::vector<std::atomic<uint64_t>>(num_threads_);
    for (auto& ops : thread_ops_) {
        ops.store(0);
    }

    // Initialize per-core error flags
    core_error_flags_ = std::vector<std::atomic<bool>>(num_threads_);
    for (auto& flag : core_error_flags_) {
        flag.store(false);
    }

    // Initialize per-core error counts
    core_error_counts_ = std::vector<std::atomic<int>>(num_threads_);
    for (auto& cnt : core_error_counts_) {
        cnt.store(0);
    }

    // Clear previous errors
    error_verifier_.clear();

    // Reset metrics
    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        current_metrics_ = CpuMetrics();
        current_metrics_.active_threads = num_threads_;
        current_metrics_.per_core_usage.resize(num_threads_, 0.0);
        current_metrics_.core_has_error.resize(num_threads_, false);
    }

    running_.store(true);
    start_time_ = std::chrono::steady_clock::now();

    // Initialize core cycling
    active_core_.store(0, std::memory_order_relaxed);

    // Launch worker threads
    workers_.reserve(num_threads_);
    for (int i = 0; i < num_threads_; ++i) {
        workers_.emplace_back(&CpuEngine::worker_thread, this, i, i);
    }

    // Launch core cycling thread if needed
    if (load_pattern_ == LoadPattern::CORE_CYCLING) {
        cycling_thread_ = std::thread([this]() {
            while (running_.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                if (!running_.load(std::memory_order_relaxed)) break;
                int next = (active_core_.load(std::memory_order_relaxed) + 1) % num_threads_;
                active_core_.store(next, std::memory_order_relaxed);
            }
        });
    }

    // Launch metrics collection thread
    metrics_thread_ = std::thread(&CpuEngine::metrics_thread_func, this);
}

void CpuEngine::stop() {
    std::lock_guard<std::mutex> guard(start_stop_mutex_);

    running_.store(false);

    for (auto& w : workers_) {
        if (w.joinable()) {
            w.join();
        }
    }
    workers_.clear();

    if (cycling_thread_.joinable()) {
        cycling_thread_.join();
    }

    if (metrics_thread_.joinable()) {
        metrics_thread_.join();
    }
}

bool CpuEngine::is_running() const {
    return running_.load();
}

CpuMetrics CpuEngine::get_metrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return current_metrics_;
}

void CpuEngine::set_metrics_callback(MetricsCallback cb) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    metrics_callback_ = std::move(cb);
}

void CpuEngine::set_sensor_manager(SensorManager* mgr) {
    sensor_mgr_ = mgr;
}

void CpuEngine::worker_thread(int thread_id, int core_id) {
    // Set CPU affinity to pin thread to specific core
    set_thread_affinity(core_id);

    // Detect available instruction sets
    bool can_avx512 = cpu::has_avx512f();
    bool can_avx2 = cpu::has_avx2() && cpu::has_fma();
    (void)can_avx512; // suppress unused warning on ARM

    // Batch duration: run stress for 100ms at a time, then update counter
    const uint64_t batch_ns = 100'000'000ULL; // 100ms

    // Variable mode: track when to change operands (every 10 minutes)
    auto last_operand_change = std::chrono::steady_clock::now();
    int operand_phase = 0; // Changes operand selection in variable mode

    // Batch iteration counter for phase rotation and verification scheduling
    uint64_t batch_count = 0;

    // Pre-allocate buffers for CACHE_ONLY and LARGE_DATA_SET modes (C1 fix)
    const size_t cache_buf_size = 4 * 1024 * 1024; // 4MB
    const size_t cache_num_doubles = cache_buf_size / sizeof(double);
    std::vector<double> cache_buf;
    if (mode_ == CpuStressMode::CACHE_ONLY) {
        cache_buf.resize(cache_num_doubles);
        for (size_t i = 0; i < cache_num_doubles; ++i) {
            cache_buf[i] = static_cast<double>(i + 1) * 0.001;
        }
    }

    const size_t large_buf_size = 256 * 1024 * 1024; // 256MB
    const size_t large_num_doubles = large_buf_size / sizeof(double);
    std::vector<double> large_buf;
    if (mode_ == CpuStressMode::LARGE_DATA_SET) {
        large_buf.resize(large_num_doubles);
        for (size_t i = 0; i < large_num_doubles; ++i) {
            large_buf[i] = static_cast<double>(i % 1000 + 1) * 0.001;
        }
    }

    while (running_.load(std::memory_order_relaxed)) {
        uint64_t ops = 0;

        // Variable mode: check if 10 minutes have elapsed for operand change
        if (load_pattern_ == LoadPattern::VARIABLE) {
            auto now = std::chrono::steady_clock::now();
            double elapsed_since_change =
                std::chrono::duration<double>(now - last_operand_change).count();
            if (elapsed_since_change >= 600.0) { // 10 minutes
                operand_phase++;
                last_operand_change = now;
            }
        }

        // Core cycling: only the active core does real work, others yield
        if (load_pattern_ == LoadPattern::CORE_CYCLING) {
            if (active_core_.load(std::memory_order_relaxed) != thread_id) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
        }

        // Determine whether to use verification mode
        bool do_verify;
        if (intensity_mode_ == CpuIntensityMode::NORMAL) {
            do_verify = true;  // Verify every batch for maximum error detection
        } else {
            // EXTREME mode: 10% random sampling instead of fixed interval
            // This prevents errors that only appear on specific batches from being missed
            thread_local std::mt19937 rng(std::random_device{}());
            do_verify = (std::uniform_int_distribution<>(0, 9)(rng) == 0);
        }

        switch (mode_) {
        case CpuStressMode::AUTO:
            // AUTO is resolved before the loop; should never reach here
            break;

        case CpuStressMode::AVX512_FMA:
            if (do_verify) {
                cpu::VerifyResult vr;
                if (can_avx512) {
                    vr = cpu::stress_and_verify_avx512(batch_ns);
                } else if (can_avx2) {
                    vr = cpu::stress_and_verify_avx2(batch_ns);
                } else {
                    vr = cpu::stress_and_verify_sse(batch_ns);
                }
                ops = vr.ops;
                if (!vr.passed) {
                    auto now = std::chrono::steady_clock::now();
                    uint64_t ts_ms = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count());
                    error_verifier_.verify_array(core_id, vr.expected, vr.actual, vr.lane_count, ts_ms);
                    core_error_flags_[thread_id].store(true, std::memory_order_relaxed);
                    core_error_counts_[thread_id].fetch_add(1, std::memory_order_relaxed);
                    if (stop_on_error_) {
                        running_.store(false, std::memory_order_relaxed);
                    }
                }
            } else {
                if (can_avx512) {
                    ops = cpu::stress_avx512(batch_ns);
                } else if (can_avx2) {
                    ops = cpu::stress_avx2(batch_ns);
                } else {
                    ops = cpu::stress_sse(batch_ns);
                }
            }
            break;

        case CpuStressMode::AVX2_FMA:
            if (do_verify) {
                cpu::VerifyResult vr;
                if (can_avx2) {
                    vr = cpu::stress_and_verify_avx2(batch_ns);
                } else {
                    vr = cpu::stress_and_verify_sse(batch_ns);
                }
                ops = vr.ops;
                if (!vr.passed) {
                    auto now = std::chrono::steady_clock::now();
                    uint64_t ts_ms = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count());
                    error_verifier_.verify_array(core_id, vr.expected, vr.actual, vr.lane_count, ts_ms);
                    core_error_flags_[thread_id].store(true, std::memory_order_relaxed);
                    core_error_counts_[thread_id].fetch_add(1, std::memory_order_relaxed);
                    if (stop_on_error_) {
                        running_.store(false, std::memory_order_relaxed);
                    }
                }
            } else {
                if (can_avx2) {
                    ops = cpu::stress_avx2(batch_ns);
                } else {
                    ops = cpu::stress_sse(batch_ns);
                }
            }
            break;

        case CpuStressMode::AVX_FLOAT:
            if (do_verify) {
                cpu::VerifyResult vr = cpu::stress_and_verify_avx_nofma(batch_ns);
                ops = vr.ops;
                if (!vr.passed) {
                    auto now = std::chrono::steady_clock::now();
                    uint64_t ts_ms = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count());
                    error_verifier_.verify_array(core_id, vr.expected, vr.actual, vr.lane_count, ts_ms);
                    core_error_flags_[thread_id].store(true, std::memory_order_relaxed);
                    core_error_counts_[thread_id].fetch_add(1, std::memory_order_relaxed);
                    if (stop_on_error_) {
                        running_.store(false, std::memory_order_relaxed);
                    }
                }
            } else {
                ops = cpu::stress_avx_nofma(batch_ns);
            }
            break;

        case CpuStressMode::SSE_FLOAT:
            if (do_verify) {
                cpu::VerifyResult vr = cpu::stress_and_verify_sse(batch_ns);
                ops = vr.ops;
                if (!vr.passed) {
                    auto now = std::chrono::steady_clock::now();
                    uint64_t ts_ms = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count());
                    error_verifier_.verify_array(core_id, vr.expected, vr.actual, vr.lane_count, ts_ms);
                    core_error_flags_[thread_id].store(true, std::memory_order_relaxed);
                    core_error_counts_[thread_id].fetch_add(1, std::memory_order_relaxed);
                    if (stop_on_error_) {
                        running_.store(false, std::memory_order_relaxed);
                    }
                }
            } else {
                ops = cpu::stress_sse(batch_ns);
            }
            break;

        case CpuStressMode::LINPACK:
            if (do_verify) {
                cpu::LinpackStressResult lsr = cpu::stress_linpack_verified(batch_ns, 2048);
                ops = lsr.total_flops;
                if (!lsr.verified) {
                    auto now = std::chrono::steady_clock::now();
                    uint64_t ts_ms = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count());
                    CpuError err;
                    err.core_id = core_id;
                    err.expected = 0.0;
                    err.actual = lsr.worst_residual;
                    err.timestamp = ts_ms;
                    err.error_code = CpuErrorCode::LINPACK_RESIDUAL;
                    err.description = "Linpack residual exceeds threshold: " +
                                     std::to_string(lsr.worst_residual);
                    // Record via verify (will always fail since values differ)
                    error_verifier_.verify(core_id, 0.0, lsr.worst_residual, ts_ms);
                    core_error_flags_[thread_id].store(true, std::memory_order_relaxed);
                    core_error_counts_[thread_id].fetch_add(1, std::memory_order_relaxed);
                    if (stop_on_error_) {
                        running_.store(false, std::memory_order_relaxed);
                    }
                }
            } else {
                ops = cpu::stress_linpack(batch_ns, 2048);
            }
            break;

        case CpuStressMode::PRIME:
            if (do_verify) {
                cpu::PrimeStressResult psr = cpu::stress_prime_verified(batch_ns);
                ops = psr.ops;
                if (!psr.verified) {
                    auto now = std::chrono::steady_clock::now();
                    uint64_t ts_ms = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count());
                    const auto& vr = !psr.mr_verify.passed ? psr.mr_verify : psr.ll_verify;
                    double exp_val = vr.expected_prime ? 1.0 : 0.0;
                    double act_val = vr.got_prime ? 1.0 : 0.0;
                    error_verifier_.verify(core_id, exp_val, act_val, ts_ms);
                    core_error_flags_[thread_id].store(true, std::memory_order_relaxed);
                    core_error_counts_[thread_id].fetch_add(1, std::memory_order_relaxed);
                    if (stop_on_error_) {
                        running_.store(false, std::memory_order_relaxed);
                    }
                }
            } else {
                ops = cpu::stress_prime(batch_ns);
            }
            break;

        case CpuStressMode::CACHE_ONLY: {
            // Run FMA operations on the pre-allocated cache-resident buffer
            auto batch_start = std::chrono::steady_clock::now();
            const double a = 1.0000001;
            const double b = 0.0000001;
            uint64_t local_ops = 0;

            while (true) {
                for (size_t i = 0; i < cache_num_doubles; ++i) {
                    cache_buf[i] = cache_buf[i] * a + b; // FMA: multiply-add
                }
                local_ops += cache_num_doubles * 2; // 1 mul + 1 add per element

                auto now = std::chrono::steady_clock::now();
                uint64_t elapsed_ns = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(now - batch_start).count());
                if (elapsed_ns >= batch_ns) break;
            }

            // Verification: check for NaN/Inf (indicates computation error)
            if (do_verify) {
                for (size_t i = 0; i < cache_num_doubles; ++i) {
                    if (std::isnan(cache_buf[i]) || std::isinf(cache_buf[i])) {
                        auto now = std::chrono::steady_clock::now();
                        uint64_t ts_ms = static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count());
                        error_verifier_.verify(core_id, 0.0, cache_buf[i], ts_ms);
                        core_error_flags_[thread_id].store(true, std::memory_order_relaxed);
                        core_error_counts_[thread_id].fetch_add(1, std::memory_order_relaxed);
                        if (stop_on_error_) {
                            running_.store(false, std::memory_order_relaxed);
                        }
                        break;
                    }
                }
            }

            ops = local_ops;
            break;
        }

        case CpuStressMode::LARGE_DATA_SET: {
            // Streaming FMA sweep through pre-allocated large buffer - forces memory bus traffic
            auto batch_start = std::chrono::steady_clock::now();
            const double a = 1.0000001;
            const double b = 0.0000001;
            uint64_t local_ops = 0;

            while (true) {
                // Sequential sweep to maximize memory bandwidth utilization
                for (size_t i = 0; i < large_num_doubles; ++i) {
                    large_buf[i] = large_buf[i] * a + b; // FMA: multiply-add
                }
                local_ops += large_num_doubles * 2;

                auto now = std::chrono::steady_clock::now();
                uint64_t elapsed_ns = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(now - batch_start).count());
                if (elapsed_ns >= batch_ns) break;
            }

            // Verification: compare computed results against expected values
            if (do_verify) {
                for (size_t i = 0; i < large_num_doubles; i += 4096) { // Sample every 4096th element
                    if (std::isnan(large_buf[i]) || std::isinf(large_buf[i])) {
                        auto now = std::chrono::steady_clock::now();
                        uint64_t ts_ms = static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count());
                        error_verifier_.verify(core_id, 0.0, large_buf[i], ts_ms);
                        core_error_flags_[thread_id].store(true, std::memory_order_relaxed);
                        core_error_counts_[thread_id].fetch_add(1, std::memory_order_relaxed);
                        if (stop_on_error_) {
                            running_.store(false, std::memory_order_relaxed);
                        }
                        break;
                    }
                }
            }

            ops = local_ops;
            break;
        }

        case CpuStressMode::ALL: {
            // Rotate through all modes based on batch count
            int phase = static_cast<int>(batch_count % 4);

            // Add operand_phase offset for variable mode
            if (load_pattern_ == LoadPattern::VARIABLE) {
                phase = (phase + operand_phase) % 4;
            }

            switch (phase) {
            case 0:
                if (do_verify) {
                    cpu::VerifyResult vr;
                    if (can_avx2)
                        vr = cpu::stress_and_verify_avx2(batch_ns);
                    else
                        vr = cpu::stress_and_verify_sse(batch_ns);
                    ops = vr.ops;
                    if (!vr.passed) {
                        auto now = std::chrono::steady_clock::now();
                        uint64_t ts_ms = static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count());
                        error_verifier_.verify_array(core_id, vr.expected, vr.actual, vr.lane_count, ts_ms);
                        core_error_flags_[thread_id].store(true, std::memory_order_relaxed);
                        core_error_counts_[thread_id].fetch_add(1, std::memory_order_relaxed);
                        if (stop_on_error_) {
                            running_.store(false, std::memory_order_relaxed);
                        }
                    }
                } else {
                    if (can_avx2)
                        ops = cpu::stress_avx2(batch_ns);
                    else
                        ops = cpu::stress_sse(batch_ns);
                }
                break;
            case 1:
                if (do_verify) {
                    cpu::VerifyResult vr = cpu::stress_and_verify_sse(batch_ns);
                    ops = vr.ops;
                    if (!vr.passed) {
                        auto now = std::chrono::steady_clock::now();
                        uint64_t ts_ms = static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count());
                        error_verifier_.verify_array(core_id, vr.expected, vr.actual, vr.lane_count, ts_ms);
                        core_error_flags_[thread_id].store(true, std::memory_order_relaxed);
                        core_error_counts_[thread_id].fetch_add(1, std::memory_order_relaxed);
                        if (stop_on_error_) {
                            running_.store(false, std::memory_order_relaxed);
                        }
                    }
                } else {
                    ops = cpu::stress_sse(batch_ns);
                }
                break;
            case 2:
                if (do_verify) {
                    cpu::LinpackStressResult lsr = cpu::stress_linpack_verified(batch_ns, 1024);
                    ops = lsr.total_flops;
                    if (!lsr.verified) {
                        auto now = std::chrono::steady_clock::now();
                        uint64_t ts_ms = static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count());
                        error_verifier_.verify(core_id, 0.0, lsr.worst_residual, ts_ms);
                        core_error_flags_[thread_id].store(true, std::memory_order_relaxed);
                        core_error_counts_[thread_id].fetch_add(1, std::memory_order_relaxed);
                        if (stop_on_error_) {
                            running_.store(false, std::memory_order_relaxed);
                        }
                    }
                } else {
                    ops = cpu::stress_linpack(batch_ns, 1024);
                }
                break;
            case 3:
                if (do_verify) {
                    cpu::PrimeStressResult psr = cpu::stress_prime_verified(batch_ns);
                    ops = psr.ops;
                    if (!psr.verified) {
                        auto now = std::chrono::steady_clock::now();
                        uint64_t ts_ms = static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count());
                        const auto& vr = !psr.mr_verify.passed ? psr.mr_verify : psr.ll_verify;
                        double exp_val = vr.expected_prime ? 1.0 : 0.0;
                        double act_val = vr.got_prime ? 1.0 : 0.0;
                        error_verifier_.verify(core_id, exp_val, act_val, ts_ms);
                        core_error_flags_[thread_id].store(true, std::memory_order_relaxed);
                        core_error_counts_[thread_id].fetch_add(1, std::memory_order_relaxed);
                        if (stop_on_error_) {
                            running_.store(false, std::memory_order_relaxed);
                        }
                    }
                } else {
                    ops = cpu::stress_prime(batch_ns);
                }
                break;
            }
            break;
        }
        }

        // Accumulate ops for this thread
        thread_ops_[thread_id].fetch_add(ops, std::memory_order_relaxed);

        // Check duration limit
        if (duration_secs_ > 0) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start_time_).count();
            if (elapsed >= static_cast<double>(duration_secs_)) {
                running_.store(false, std::memory_order_relaxed);
                break;
            }
        }

        ++batch_count;
    }
}

void CpuEngine::metrics_thread_func() {
    uint64_t prev_total_ops = 0;
    auto prev_time = std::chrono::steady_clock::now();

    while (running_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (!running_.load(std::memory_order_relaxed)) break;

        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - prev_time).count();
        double total_elapsed = std::chrono::duration<double>(now - start_time_).count();

        // Sum all thread ops
        uint64_t total_ops = 0;
        for (int i = 0; i < num_threads_; ++i) {
            total_ops += thread_ops_[i].load(std::memory_order_relaxed);
        }

        uint64_t delta_ops = total_ops - prev_total_ops;
        double gflops = static_cast<double>(delta_ops) / dt / 1e9;

        CpuMetrics metrics;
        metrics.gflops = gflops;
        metrics.total_ops = total_ops;
        metrics.elapsed_secs = total_elapsed;
        metrics.active_threads = num_threads_;

        // Per-core usage (approximation: all pinned threads at 100%)
        metrics.per_core_usage.resize(num_threads_, 100.0);

        // Per-core type from CPUID hybrid detection
        {
            static const auto cpu_info = utils::detect_cpu();
            metrics.per_core_type.resize(num_threads_);
            for (int i = 0; i < num_threads_; ++i) {
                if (i < static_cast<int>(cpu_info.core_types.size())) {
                    switch (cpu_info.core_types[i]) {
                    case utils::CoreType::PERFORMANCE:
                        metrics.per_core_type[i] = "P-core";
                        break;
                    case utils::CoreType::EFFICIENCY:
                        metrics.per_core_type[i] = "E-core";
                        break;
                    default:
                        metrics.per_core_type[i] = "Unknown";
                        break;
                    }
                } else {
                    metrics.per_core_type[i] = "Unknown";
                }
            }
        }

        // Error information
        metrics.error_count = error_verifier_.error_count();
        metrics.errors = error_verifier_.get_errors();
        metrics.core_has_error.resize(num_threads_, false);
        metrics.per_core_error_count.resize(num_threads_, 0);
        for (int i = 0; i < num_threads_; ++i) {
            metrics.core_has_error[i] = core_error_flags_[i].load(std::memory_order_relaxed);
            metrics.per_core_error_count[i] = core_error_counts_[i].load(std::memory_order_relaxed);
        }

        // Read temperature and power from SensorManager
        if (sensor_mgr_) {
            metrics.temperature = sensor_mgr_->get_cpu_temperature();
            metrics.power_watts = sensor_mgr_->get_cpu_power();
            metrics.power_estimated = sensor_mgr_->is_cpu_power_estimated();
        }

        // Track peak GFLOPS
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            if (gflops > current_metrics_.peak_gflops) {
                metrics.peak_gflops = gflops;
            } else {
                metrics.peak_gflops = current_metrics_.peak_gflops;
            }
            current_metrics_ = metrics;
        }

        // Fire callback
        MetricsCallback cb;
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            cb = metrics_callback_;
        }
        if (cb) {
            cb(metrics);
        }

        prev_total_ops = total_ops;
        prev_time = now;
    }
}

std::string CpuEngine::error_summary() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    int total = current_metrics_.error_count;
    if (total == 0) return "No errors detected";

    int cores_affected = 0;
    std::ostringstream oss;
    oss << total << " error(s) on ";

    std::string detail;
    for (int i = 0; i < (int)current_metrics_.per_core_error_count.size(); ++i) {
        int cnt = current_metrics_.per_core_error_count[i];
        if (cnt > 0) {
            cores_affected++;
            if (!detail.empty()) detail += ", ";
            detail += "Core #" + std::to_string(i) + " (" + std::to_string(cnt) + ")";
        }
    }

    oss << cores_affected << " core(s): " << detail;
    return oss.str();
}

} // namespace occt
