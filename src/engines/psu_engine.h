#pragma once

#include "base_engine.h"
#include "cpu_engine.h"
#include "gpu_engine.h"

namespace occt { class SensorManager; } // forward declaration

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace occt {

enum class PsuLoadPattern {
    STEADY,  // CPU Linpack + GPU max load simultaneously
    SPIKE,   // 5s idle -> 5s max load, repeating
    RAMP     // 0% -> 100% gradual increase
};

enum class PsuHealthStatus {
    HEALTHY,        // 정상
    MARGINAL,       // 주의 (전력 변동 있으나 에러 없음)
    UNSTABLE,       // 불안정 (전력 변동 + 에러 상관)
    FAILED          // 실패 (엔진 중단 또는 과다 에러)
};

struct PsuMetrics {
    double total_power_watts = 0.0;
    double cpu_power_watts = 0.0;
    double gpu_power_watts = 0.0;
    bool cpu_running = false;
    bool gpu_running = false;
    double elapsed_secs = 0.0;
    int errors_cpu = 0;
    int errors_gpu = 0;

    // 전력 안정성 분석
    double power_stability_pct = 100.0;  // 전력 변동 안정도 (100=안정)
    double max_power_drop_watts = 0.0;   // 최대 순간 전력 강하
    int power_drop_events = 0;           // 급격한 전력 변동 횟수
    bool power_correlated_errors = false; // 전력 변동과 동시에 에러 발생

    // PSU 상태 판정
    PsuHealthStatus health = PsuHealthStatus::HEALTHY;
};

class PsuEngine : public IEngine {
public:
    PsuEngine();
    ~PsuEngine() override;

    PsuEngine(const PsuEngine&) = delete;
    PsuEngine& operator=(const PsuEngine&) = delete;

    /// Start PSU stress test.
    /// @param pattern     Load pattern to apply.
    /// @param duration_secs  0 = run until stop() is called.
    void start(PsuLoadPattern pattern, int duration_secs = 0);

    void stop() override;
    bool is_running() const override;
    std::string name() const override { return "PSU"; }

    PsuMetrics get_metrics() const;

    using MetricsCallback = std::function<void(const PsuMetrics&)>;
    void set_metrics_callback(MetricsCallback cb);

    /// Whether to use all available GPUs (default: false, uses first GPU only).
    void set_use_all_gpus(bool use_all) { use_all_gpus_ = use_all; }

    /// Set SensorManager so the internal CpuEngine can read power/temperature.
    void set_sensor_manager(SensorManager* mgr) { cpu_.set_sensor_manager(mgr); }

private:
    void controller_thread_func(PsuLoadPattern pattern, int duration_secs);
    void metrics_poller_func();
    void start_cpu_load();
    void stop_cpu_load();
    void start_gpu_load();
    void stop_gpu_load();

    CpuEngine cpu_;
    GpuEngine gpu_;

    std::thread controller_thread_;
    std::thread metrics_thread_;
    std::mutex start_stop_mutex_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::chrono::steady_clock::time_point start_time_;

    mutable std::mutex metrics_mutex_;
    PsuMetrics current_metrics_;

    MetricsCallback metrics_cb_;
    std::mutex cb_mutex_;

    bool use_all_gpus_ = false;

    // 전력-에러 상관분석 추적용
    double prev_total_power_ = 0.0;
    double power_sum_ = 0.0;
    int power_sample_count_ = 0;
    double power_min_ = 1e9;
    double power_max_ = 0.0;
    int prev_total_errors_ = 0;
    std::chrono::steady_clock::time_point last_power_drop_time_;
    bool recent_power_drop_ = false;
};

} // namespace occt
