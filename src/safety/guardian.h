#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "engines/base_engine.h"
#include "monitor/sensor_manager.h"

namespace occt {

class WheaMonitor;

struct SafetyLimits {
    double cpu_temp_max  = 95.0;   // degrees C
    double gpu_temp_max  = 90.0;   // degrees C
    double cpu_power_max = 300.0;  // watts
};

class SafetyGuardian {
public:
    /// @param sensors  Pointer to an initialized SensorManager (must outlive this).
    explicit SafetyGuardian(SensorManager* sensors);
    ~SafetyGuardian();

    SafetyGuardian(const SafetyGuardian&) = delete;
    SafetyGuardian& operator=(const SafetyGuardian&) = delete;

    /// Start the safety-check loop (200 ms interval).
    void start();

    /// Stop the safety-check loop.
    void stop();

    /// Update safety thresholds.
    void set_limits(const SafetyLimits& limits);

    /// Get current safety thresholds.
    SafetyLimits get_limits() const;

    /// Register an engine to be stopped on emergency.
    void register_engine(IEngine* engine);

    /// Unregister a previously registered engine.
    void unregister_engine(IEngine* engine);

    /// Callback fired on emergency stop.
    using EmergencyCallback = std::function<void(const std::string& reason)>;
    void set_emergency_callback(EmergencyCallback cb);

    /// Set the WHEA monitor to check during the safety loop.
    /// When set and WHEA errors are detected, triggers emergency stop.
    void set_whea_monitor(WheaMonitor* whea);

private:
    void check_loop();
    void emergency_stop(const std::string& reason);

    SensorManager* sensors_;

    mutable std::mutex limits_mutex_;
    SafetyLimits limits_;

    std::mutex engines_mutex_;
    std::vector<IEngine*> engines_;

    std::thread check_thread_;
    std::atomic<bool> running_{false};

    EmergencyCallback emergency_cb_;
    std::mutex cb_mutex_;

    std::atomic<bool> emergency_triggered_{false};

    WheaMonitor* whea_monitor_ = nullptr;
    int last_whea_count_ = 0;
};

} // namespace occt
