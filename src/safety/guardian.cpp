#include "guardian.h"
#include "monitor/whea_monitor.h"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace occt {

// ─── Constructor / Destructor ────────────────────────────────────────────────

SafetyGuardian::SafetyGuardian(SensorManager* sensors)
    : sensors_(sensors)
{
}

SafetyGuardian::~SafetyGuardian() {
    stop();
}

// ─── Public API ──────────────────────────────────────────────────────────────

void SafetyGuardian::start() {
    if (running_.load()) return;
    running_.store(true);
    check_thread_ = std::thread(&SafetyGuardian::check_loop, this);
}

void SafetyGuardian::stop() {
    running_.store(false);
    if (check_thread_.joinable()) {
        check_thread_.join();
    }
}

void SafetyGuardian::set_limits(const SafetyLimits& limits) {
    std::lock_guard<std::mutex> lk(limits_mutex_);
    limits_ = limits;
}

SafetyLimits SafetyGuardian::get_limits() const {
    std::lock_guard<std::mutex> lk(limits_mutex_);
    return limits_;
}

void SafetyGuardian::register_engine(IEngine* engine) {
    if (!engine) return;
    std::lock_guard<std::mutex> lk(engines_mutex_);
    // Avoid duplicates
    auto it = std::find(engines_.begin(), engines_.end(), engine);
    if (it == engines_.end()) {
        engines_.push_back(engine);
    }
}

void SafetyGuardian::unregister_engine(IEngine* engine) {
    std::lock_guard<std::mutex> lk(engines_mutex_);
    engines_.erase(
        std::remove(engines_.begin(), engines_.end(), engine),
        engines_.end());
}

void SafetyGuardian::set_emergency_callback(EmergencyCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    emergency_cb_ = std::move(cb);
}

void SafetyGuardian::set_whea_monitor(WheaMonitor* whea) {
    whea_monitor_ = whea;
    last_whea_count_ = whea ? whea->error_count() : 0;
}

// ─── Check Loop (200 ms interval) ───────────────────────────────────────────

void SafetyGuardian::check_loop() {
    while (running_.load()) {
        SafetyLimits current_limits;
        {
            std::lock_guard<std::mutex> lk(limits_mutex_);
            current_limits = limits_;
        }

        // Read sensors
        double cpu_temp = sensors_->get_cpu_temperature();
        double gpu_temp = sensors_->get_gpu_temperature();
        double cpu_power = sensors_->get_cpu_power();

        // Check CPU temperature
        if (cpu_temp > 0.0 && cpu_temp >= current_limits.cpu_temp_max) {
            std::ostringstream oss;
            oss << "CPU temperature critical: " << cpu_temp
                << " C (limit: " << current_limits.cpu_temp_max << " C)";
            emergency_stop(oss.str());
        }

        // Check GPU temperature
        if (gpu_temp > 0.0 && gpu_temp >= current_limits.gpu_temp_max) {
            std::ostringstream oss;
            oss << "GPU temperature critical: " << gpu_temp
                << " C (limit: " << current_limits.gpu_temp_max << " C)";
            emergency_stop(oss.str());
        }

        // Check CPU power draw
        if (cpu_power > 0.0 && cpu_power >= current_limits.cpu_power_max) {
            std::ostringstream oss;
            oss << "CPU power draw critical: " << cpu_power
                << " W (limit: " << current_limits.cpu_power_max << " W)";
            emergency_stop(oss.str());
        }

        // Check WHEA errors
        if (whea_monitor_) {
            int current_count = whea_monitor_->error_count();
            if (current_count > last_whea_count_) {
                int new_errors = current_count - last_whea_count_;
                last_whea_count_ = current_count;
                std::ostringstream oss;
                oss << "WHEA hardware error detected (" << new_errors
                    << " new error" << (new_errors > 1 ? "s" : "")
                    << ", " << current_count << " total)";
                emergency_stop(oss.str());
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

// ─── Emergency Stop ──────────────────────────────────────────────────────────

void SafetyGuardian::emergency_stop(const std::string& reason) {
    // Prevent multiple triggers: atomically set from false to true
    bool expected = false;
    if (!emergency_triggered_.compare_exchange_strong(expected, true))
        return;  // Already triggered

    // Stop all registered engines
    {
        std::lock_guard<std::mutex> lk(engines_mutex_);
        for (auto* engine : engines_) {
            if (engine && engine->is_running()) {
                engine->stop();
            }
        }
    }

    // Fire callback
    {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        if (emergency_cb_) {
            emergency_cb_(reason);
        }
    }

    // Stop the guardian itself after emergency
    running_.store(false);
}

} // namespace occt
