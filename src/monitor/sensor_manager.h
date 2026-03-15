#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <QVector>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <pdh.h>
#endif

namespace occt {

struct HardwareNode; // forward decl from sensor_model.h
class LhmBridge;     // forward decl from lhm_bridge.h

struct SensorReading {
    std::string name;
    std::string category;   // "CPU", "GPU", "Motherboard", "Storage"
    double value     = 0.0;
    double min_value = 0.0;
    double max_value = 0.0;
    std::string unit;       // "C", "W", "V", "RPM", "%"
};

class SensorManager {
public:
    SensorManager();
    ~SensorManager();

    SensorManager(const SensorManager&) = delete;
    SensorManager& operator=(const SensorManager&) = delete;

    /// Initialize sensor backends. Returns true if at least one backend is available.
    bool initialize();

    /// Start periodic polling.
    /// @param interval_ms  Polling interval in milliseconds.
    void start_polling(int interval_ms = 500);

    /// Stop polling.
    void stop();

    /// Get a snapshot of all current sensor readings (flat list, backward compat).
    std::vector<SensorReading> get_all_readings() const;

    /// Get hierarchical hardware tree built from current readings.
    QVector<HardwareNode> get_hardware_tree() const;

    /// Convenience accessors (return 0 if sensor not available).
    double get_cpu_temperature() const;
    double get_gpu_temperature() const;
    double get_cpu_power() const;

    /// Convenience filtered accessors.
    std::vector<SensorReading> get_fan_speeds() const;
    std::vector<SensorReading> get_voltages() const;

    /// Returns true when get_cpu_power() is a TDP*usage% estimate rather than
    /// a real hardware reading (e.g. from LHM or RAPL).
    bool is_cpu_power_estimated() const;

    /// Alert callback: fired when a sensor exceeds a threshold.
    using AlertCallback = std::function<void(const std::string& sensor,
                                             double value, double threshold)>;
    void set_alert_callback(AlertCallback cb);

private:
    void poll_thread_func(int interval_ms);

    // Platform-specific sensor backends
    bool init_sysfs();      // Linux: /sys/class/hwmon, /sys/class/thermal
    bool init_iokit();      // macOS: IOKit SMC
    bool init_wmi();        // Windows: WMI + MSAcpi

    void poll_sysfs();
    void poll_iokit();
    void poll_wmi();

    // GPU-specific (dynamically loaded)
    bool init_nvml();       // NVIDIA Management Library
    bool init_adl();        // AMD Display Library

    void poll_nvml();
    void poll_adl();

    void update_reading(const std::string& name, const std::string& category,
                        double value, const std::string& unit);

    // Windows fallback when WMI is not available (no admin, COM failure, etc.)
    void collect_basic_system_info();

    std::thread poll_thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex readings_mutex_;
    std::vector<SensorReading> readings_;
    bool cpu_power_estimated_ = false;  // True when using TDP*usage% fallback

    AlertCallback alert_cb_;
    std::mutex cb_mutex_;

    // Backend availability flags (platform-specific; unused ones are expected)
    [[maybe_unused]] bool has_sysfs_ = false;
    [[maybe_unused]] bool has_iokit_ = false;
    [[maybe_unused]] bool has_wmi_   = false;
    bool has_nvml_  = false;
    bool has_adl_   = false;
    bool adl_stub_logged_ = false;

    // NVML dynamic handles
    void* nvml_handle_ = nullptr;
    // ADL dynamic handles
    void* adl_handle_  = nullptr;

#ifdef _WIN32
    // Cached WMI COM objects (avoid re-creating every poll cycle)
    void* wmi_locator_       = nullptr;  // IWbemLocator*
    void* wmi_svc_root_wmi_  = nullptr;  // IWbemServices* for ROOT\WMI
    void* wmi_svc_cimv2_     = nullptr;  // IWbemServices* for ROOT\CIMV2

    void cleanup_wmi();
    bool reconnect_wmi();

    // PDH-based dynamic CPU frequency
    PDH_HQUERY   pdh_query_        = nullptr;
    PDH_HCOUNTER pdh_freq_counter_ = nullptr;
    void init_pdh();
    void cleanup_pdh();

    // Cached CPU info for power estimation and frequency calculation
    double      max_clock_speed_ = 0.0;   // MHz from Win32_Processor
    std::string cpu_brand_;                // CPU brand string

    // LHM bridge for accurate hardware monitoring
    LhmBridge*  lhm_bridge_ = nullptr;

    // ADL2 function pointer types
    typedef int (*ADL2_MAIN_CONTROL_CREATE)(int (*)(int), int, void**);
    typedef int (*ADL2_MAIN_CONTROL_DESTROY)(void*);
    typedef int (*ADL2_ADAPTER_NUMBEROFADAPTERS_GET)(void*, int*);
    typedef int (*ADL2_ADAPTER_ACTIVE_GET)(void*, int, int*);
    typedef int (*ADL2_OVERDRIVE_TEMPERATURE_GET)(void*, int, int, int*);
    typedef int (*ADL2_OVERDRIVE5_FANSPEED_GET)(void*, int, int, void*);

    // ADL members
    void* adl_context_ = nullptr;
    ADL2_MAIN_CONTROL_CREATE adl_create_ = nullptr;
    ADL2_MAIN_CONTROL_DESTROY adl_destroy_ = nullptr;
    ADL2_ADAPTER_NUMBEROFADAPTERS_GET adl_num_adapters_ = nullptr;
    ADL2_ADAPTER_ACTIVE_GET adl_active_ = nullptr;
    ADL2_OVERDRIVE_TEMPERATURE_GET adl_temp_ = nullptr;
    int adl_adapter_count_ = 0;
#endif
};

} // namespace occt
