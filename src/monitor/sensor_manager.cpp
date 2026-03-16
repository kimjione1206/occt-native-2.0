#include "sensor_manager.h"
#include "sensor_model.h"
#include "lhm_bridge.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <comdef.h>
    #include <wbemidl.h>
    #include <pdh.h>
    #pragma comment(lib, "wbemuuid.lib")
    #pragma comment(lib, "ole32.lib")
    #pragma comment(lib, "oleaut32.lib")
    #pragma comment(lib, "pdh.lib")
#elif defined(__linux__)
    #include <dirent.h>
    #include <dlfcn.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <fstream>
    #include <sstream>
#elif defined(__APPLE__)
    #include <sys/types.h>
    #include <sys/sysctl.h>
    #include <mach/mach.h>
    #include <mach/host_info.h>
    #include <mach/mach_host.h>
    #include <CoreFoundation/CoreFoundation.h>
    #include <IOKit/IOKitLib.h>
    #include <IOKit/ps/IOPowerSources.h>
    #include <IOKit/ps/IOPSKeys.h>
    #include <dlfcn.h>
    #include <objc/objc.h>
    #include <objc/message.h>
#endif

// NVML function pointer types (dynamically loaded)
#if !defined(__APPLE__)
typedef int (*nvmlInit_t)();
typedef int (*nvmlShutdown_t)();
typedef int (*nvmlDeviceGetCount_t)(unsigned int*);
typedef int (*nvmlDeviceGetHandleByIndex_t)(unsigned int, void**);
typedef int (*nvmlDeviceGetTemperature_t)(void*, int, unsigned int*);
typedef int (*nvmlDeviceGetPowerUsage_t)(void*, unsigned int*);
typedef int (*nvmlDeviceGetName_t)(void*, char*, unsigned int);
#endif

// ─── CPU TDP estimation helper (Windows only) ───────────────────────────────
#if defined(_WIN32)
static double estimate_tdp(const std::string& brand) {
    // Common TDP values by CPU family (desktop defaults)
    if (brand.find("i9-14") != std::string::npos || brand.find("i9-13") != std::string::npos) return 253.0;
    if (brand.find("i7-14") != std::string::npos || brand.find("i7-13") != std::string::npos) return 253.0;
    if (brand.find("i5-14") != std::string::npos || brand.find("i5-13") != std::string::npos) return 154.0;
    if (brand.find("i3-14") != std::string::npos || brand.find("i3-13") != std::string::npos) return 89.0;
    if (brand.find("i9-12") != std::string::npos) return 241.0;
    if (brand.find("i7-12") != std::string::npos) return 190.0;
    if (brand.find("i5-12") != std::string::npos) return 148.0;
    if (brand.find("Ryzen 9 7") != std::string::npos) return 170.0;
    if (brand.find("Ryzen 7 7") != std::string::npos) return 105.0;
    if (brand.find("Ryzen 5 7") != std::string::npos) return 105.0;
    if (brand.find("Ryzen 9 5") != std::string::npos) return 105.0;
    if (brand.find("Ryzen 7 5") != std::string::npos) return 65.0;
    if (brand.find("Ryzen 5 5") != std::string::npos) return 65.0;
    if (brand.find("Ryzen 9 9") != std::string::npos) return 170.0;
    if (brand.find("Ryzen 7 9") != std::string::npos) return 105.0;
    if (brand.find("Ryzen 5 9") != std::string::npos) return 65.0;
    if (brand.find("EPYC") != std::string::npos) return 280.0;
    if (brand.find("Xeon") != std::string::npos) return 150.0;
    // Default desktop TDP
    return 95.0;
}
#endif

namespace occt {

// ─── Sensor-related constants ────────────────────────────────────────────────

// IOHIDSensor fixed-point format: raw value is in 16.16 fixed-point
static constexpr double IOKIT_FIXED_POINT_DIVISOR = 65536.0;

// IOHIDSensor usage page identifying temperature sensors
static constexpr int32_t IOKIT_USAGE_PAGE_TEMPERATURE = 0xFF00;

#if defined(_WIN32)
// WMI reports temperature in tenths of Kelvin; convert to Celsius
static constexpr double WMI_TEMP_TENTHS_TO_DEGREES = 10.0;
static constexpr double KELVIN_TO_CELSIUS_OFFSET = 273.15;

// Maximum length for truncated WHEA event descriptions
static constexpr int WHEA_DESCRIPTION_MAX_LENGTH = 512;
#endif

#if defined(__linux__)
// Polling constants for sysfs enumeration
static constexpr int MAX_THERMAL_ZONES = 16;
static constexpr int MAX_HWMON_TEMPS   = 32;
static constexpr int MAX_HWMON_FANS    = 8;
#endif

// ─── Constructor / Destructor ────────────────────────────────────────────────

SensorManager::SensorManager() = default;

SensorManager::~SensorManager() {
    stop();

#ifdef _WIN32
    cleanup_wmi();
    cleanup_pdh();
    delete lhm_bridge_;
    lhm_bridge_ = nullptr;
#endif

    // Unload NVML
    if (nvml_handle_) {
#if defined(_WIN32)
        using ShutdownFn = int (*)();
        auto fn = reinterpret_cast<ShutdownFn>(
            GetProcAddress(static_cast<HMODULE>(nvml_handle_), "nvmlShutdown"));
        if (fn) fn();
        FreeLibrary(static_cast<HMODULE>(nvml_handle_));
#elif defined(__linux__)
        using ShutdownFn = int (*)();
        auto fn = reinterpret_cast<ShutdownFn>(dlsym(nvml_handle_, "nvmlShutdown"));
        if (fn) fn();
        dlclose(nvml_handle_);
#endif
        nvml_handle_ = nullptr;
    }

    // Unload ADL
    if (adl_handle_) {
#if defined(_WIN32)
        if (adl_destroy_ && adl_context_) {
            adl_destroy_(adl_context_);
            adl_context_ = nullptr;
        }
        FreeLibrary(static_cast<HMODULE>(adl_handle_));
#elif defined(__linux__)
        dlclose(adl_handle_);
#endif
        adl_handle_ = nullptr;
    }
}

// ─── Public API ──────────────────────────────────────────────────────────────

bool SensorManager::initialize() {
    bool any = false;

#if defined(_WIN32)
    has_wmi_ = init_wmi();
    any |= has_wmi_;

    // Initialize PDH for dynamic CPU frequency
    init_pdh();

    // Initialize LHM bridge for accurate sensor data
    lhm_bridge_ = new LhmBridge(nullptr);
    if (lhm_bridge_->initialize()) {
        std::cout << "[Sensor] LHM bridge active" << std::endl;
    }
#elif defined(__linux__)
    has_sysfs_ = init_sysfs();
    any |= has_sysfs_;
#elif defined(__APPLE__)
    has_iokit_ = init_iokit();
    any |= has_iokit_;
#endif

    // GPU backends (cross-platform where applicable)
    has_nvml_ = init_nvml();
    any |= has_nvml_;

    has_adl_ = init_adl();
    any |= has_adl_;

    return any;
}

void SensorManager::start_polling(int interval_ms) {
    if (running_.load()) return;
    running_.store(true);
    poll_thread_ = std::thread(&SensorManager::poll_thread_func, this, interval_ms);
}

void SensorManager::stop() {
    running_.store(false);
    if (poll_thread_.joinable()) {
        poll_thread_.join();
    }
}

std::vector<SensorReading> SensorManager::get_all_readings() const {
    std::lock_guard<std::mutex> lk(readings_mutex_);
    return readings_;
}

QVector<HardwareNode> SensorManager::get_hardware_tree() const {
    std::lock_guard<std::mutex> lk(readings_mutex_);
    QVector<SensorReading> qreadings;
    qreadings.reserve(static_cast<int>(readings_.size()));
    for (const auto& r : readings_) {
        qreadings.append(r);
    }
    return build_hardware_tree(qreadings);
}

double SensorManager::get_cpu_temperature() const {
    std::lock_guard<std::mutex> lk(readings_mutex_);
    for (const auto& r : readings_) {
        if (r.category == "CPU" && r.unit == "C" && r.value > 0.0) {
            return r.value;
        }
    }
    return 0.0;
}

double SensorManager::get_gpu_temperature() const {
    std::lock_guard<std::mutex> lk(readings_mutex_);
    for (const auto& r : readings_) {
        if (r.category == "GPU" && r.unit == "C") {
            return r.value;
        }
    }
    return 0.0;
}

double SensorManager::get_cpu_power() const {
    std::lock_guard<std::mutex> lk(readings_mutex_);
    for (const auto& r : readings_) {
        if (r.category == "CPU" && r.unit == "W") {
            return r.value;
        }
    }
    return 0.0;
}

bool SensorManager::is_cpu_power_estimated() const {
    std::lock_guard<std::mutex> lk(readings_mutex_);
    return cpu_power_estimated_;
}

void SensorManager::set_alert_callback(AlertCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    alert_cb_ = std::move(cb);
}

// ─── Polling Thread ──────────────────────────────────────────────────────────

void SensorManager::poll_thread_func(int interval_ms) {
    while (running_.load()) {
#if defined(_WIN32)
        if (has_wmi_) poll_wmi();
#elif defined(__linux__)
        if (has_sysfs_) poll_sysfs();
#elif defined(__APPLE__)
        if (has_iokit_) poll_iokit();
#endif

        if (has_nvml_) poll_nvml();
        if (has_adl_) poll_adl();

        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
}

// ─── Helper: update or insert a reading ──────────────────────────────────────

void SensorManager::update_reading(const std::string& sensor_name,
                                   const std::string& category,
                                   double value, const std::string& unit) {
    std::lock_guard<std::mutex> lk(readings_mutex_);

    double now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    for (auto& r : readings_) {
        if (r.name == sensor_name && r.category == category) {
            r.value = value;
            r.min_value = std::min(r.min_value, value);
            r.max_value = std::max(r.max_value, value);
            r.last_update_epoch = now;
            return;
        }
    }

    // New sensor
    SensorReading reading;
    reading.name = sensor_name;
    reading.category = category;
    reading.value = value;
    reading.min_value = value;
    reading.max_value = value;
    reading.unit = unit;
    reading.last_update_epoch = now;
    readings_.push_back(std::move(reading));
}

// ─── Linux: sysfs backend ────────────────────────────────────────────────────

#if defined(__linux__)

bool SensorManager::init_sysfs() {
    // Check /sys/class/thermal exists
    DIR* dir = opendir("/sys/class/thermal");
    if (dir) {
        closedir(dir);
        return true;
    }
    // Check /sys/class/hwmon
    dir = opendir("/sys/class/hwmon");
    if (dir) {
        closedir(dir);
        return true;
    }
    return false;
}

void SensorManager::poll_sysfs() {
    // Read thermal zones
    for (int i = 0; i < MAX_THERMAL_ZONES; ++i) {
        std::string path = "/sys/class/thermal/thermal_zone" +
                           std::to_string(i) + "/temp";
        std::ifstream f(path);
        if (!f.is_open()) break;

        int temp_milli = 0;
        f >> temp_milli;
        double temp_c = temp_milli / 1000.0;

        // Read zone type
        std::string type_path = "/sys/class/thermal/thermal_zone" +
                                std::to_string(i) + "/type";
        std::ifstream tf(type_path);
        std::string zone_type = "zone" + std::to_string(i);
        if (tf.is_open()) {
            std::getline(tf, zone_type);
        }

        std::string category = "CPU";
        if (zone_type.find("gpu") != std::string::npos ||
            zone_type.find("GPU") != std::string::npos) {
            category = "GPU";
        }

        update_reading(zone_type, category, temp_c, "C");
    }

    // Read hwmon sensors (coretemp, etc.)
    DIR* dir = opendir("/sys/class/hwmon");
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        std::string hwmon_path = std::string("/sys/class/hwmon/") + entry->d_name;

        // Read sensor name
        std::string name_path = hwmon_path + "/name";
        std::ifstream nf(name_path);
        std::string sensor_name = entry->d_name;
        if (nf.is_open()) std::getline(nf, sensor_name);

        // Read temp inputs
        for (int j = 1; j <= MAX_HWMON_TEMPS; ++j) {
            std::string temp_path = hwmon_path + "/temp" + std::to_string(j) + "_input";
            std::ifstream tf(temp_path);
            if (!tf.is_open()) break;

            int temp_milli = 0;
            tf >> temp_milli;
            double temp_c = temp_milli / 1000.0;

            std::string label = sensor_name + "_temp" + std::to_string(j);

            // Try reading label
            std::string label_path = hwmon_path + "/temp" + std::to_string(j) + "_label";
            std::ifstream lf(label_path);
            if (lf.is_open()) {
                std::getline(lf, label);
            }

            std::string category = "Motherboard";
            if (sensor_name == "coretemp" || sensor_name == "k10temp" ||
                sensor_name == "zenpower") {
                category = "CPU";
            } else if (sensor_name.find("gpu") != std::string::npos ||
                       sensor_name == "amdgpu" || sensor_name == "nouveau") {
                category = "GPU";
            }

            update_reading(label, category, temp_c, "C");
        }

        // Read power (in1_input is voltage in mV, power1_input in uW)
        std::string power_path = hwmon_path + "/power1_input";
        std::ifstream pf(power_path);
        if (pf.is_open()) {
            uint64_t power_uw = 0;
            pf >> power_uw;
            double watts = power_uw / 1e6;
            update_reading(sensor_name + "_power", "CPU", watts, "W");
        }

        // Read fan RPM
        for (int j = 1; j <= MAX_HWMON_FANS; ++j) {
            std::string fan_path = hwmon_path + "/fan" + std::to_string(j) + "_input";
            std::ifstream ff(fan_path);
            if (!ff.is_open()) break;

            int rpm = 0;
            ff >> rpm;
            update_reading(sensor_name + "_fan" + std::to_string(j),
                           "Motherboard", static_cast<double>(rpm), "RPM");
        }
    }

    closedir(dir);
}

#else // !__linux__
bool SensorManager::init_sysfs() { return false; }
void SensorManager::poll_sysfs() {}
#endif

// ─── macOS: IOKit + Apple Silicon sensor backend ─────────────────────────────

#if defined(__APPLE__)

// Helper: get NSProcessInfo thermalState via Objective-C runtime
static int get_thermal_state() {
    // NSProcessInfo.processInfo.thermalState
    // 0=nominal, 1=fair, 2=serious, 3=critical
    Class cls = objc_getClass("NSProcessInfo");
    if (!cls) return -1;
    using MsgSend = id(*)(Class, SEL);
    auto processInfo = reinterpret_cast<MsgSend>(objc_msgSend)(
        cls, sel_registerName("processInfo"));
    if (!processInfo) return -1;
    using MsgSendInt = long(*)(id, SEL);
    return static_cast<int>(reinterpret_cast<MsgSendInt>(objc_msgSend)(
        processInfo, sel_registerName("thermalState")));
}

// Helper: read battery properties from IOKit (works on all macOS)
struct BatteryInfo {
    double temperature = 0;  // degrees C
    double voltage = 0;      // volts
    double amperage = 0;     // amps (negative = discharging)
    double watts = 0;        // power draw
    bool valid = false;
};

static BatteryInfo read_battery_info() {
    BatteryInfo info;
    io_iterator_t iter = 0;
    kern_return_t kr = IOServiceGetMatchingServices(
        kIOMainPortDefault,
        IOServiceMatching("AppleSmartBattery"),
        &iter);
    if (kr != KERN_SUCCESS) return info;

    io_object_t service = IOIteratorNext(iter);
    IOObjectRelease(iter);
    if (!service) return info;

    CFMutableDictionaryRef props = nullptr;
    kr = IORegistryEntryCreateCFProperties(service, &props,
                                            kCFAllocatorDefault, 0);
    IOObjectRelease(service);
    if (kr != KERN_SUCCESS || !props) return info;

    auto getInt = [&](const char* key) -> int64_t {
        CFNumberRef num = static_cast<CFNumberRef>(
            CFDictionaryGetValue(props, CFStringCreateWithCString(
                kCFAllocatorDefault, key, kCFStringEncodingUTF8)));
        if (!num || CFGetTypeID(num) != CFNumberGetTypeID()) return 0;
        int64_t val = 0;
        CFNumberGetValue(num, kCFNumberSInt64Type, &val);
        return val;
    };

    int64_t temp_raw = getInt("Temperature");  // centi-degrees C (e.g. 2984 = 29.84°C)
    int64_t voltage_mv = getInt("Voltage");    // millivolts
    int64_t amperage_ma = getInt("Amperage");  // milliamps (signed)

    if (temp_raw != 0) {
        info.temperature = temp_raw / 100.0;
        info.valid = true;
    }
    if (voltage_mv != 0) {
        info.voltage = voltage_mv / 1000.0;
    }
    if (amperage_ma != 0) {
        info.amperage = amperage_ma / 1000.0;
    }
    info.watts = std::abs(info.voltage * info.amperage);

    CFRelease(props);
    return info;
}

// Helper: get CPU usage per core via Mach host_processor_info
static std::vector<double> get_cpu_usage() {
    std::vector<double> usage;
    static std::vector<uint64_t> prev_user, prev_system, prev_idle;

    natural_t cpu_count = 0;
    processor_info_array_t info_array = nullptr;
    mach_msg_type_number_t info_count = 0;

    kern_return_t kr = host_processor_info(
        mach_host_self(), PROCESSOR_CPU_LOAD_INFO,
        &cpu_count, &info_array, &info_count);
    if (kr != KERN_SUCCESS) return usage;

    bool first_call = prev_user.empty();
    if (first_call) {
        prev_user.resize(cpu_count, 0);
        prev_system.resize(cpu_count, 0);
        prev_idle.resize(cpu_count, 0);
    }

    for (natural_t i = 0; i < cpu_count; ++i) {
        auto* ticks = reinterpret_cast<integer_t*>(info_array) +
                      i * CPU_STATE_MAX;
        uint64_t user = ticks[CPU_STATE_USER] + ticks[CPU_STATE_NICE];
        uint64_t sys  = ticks[CPU_STATE_SYSTEM];
        uint64_t idle = ticks[CPU_STATE_IDLE];

        if (!first_call && i < prev_user.size()) {
            uint64_t du = user - prev_user[i];
            uint64_t ds = sys - prev_system[i];
            uint64_t di = idle - prev_idle[i];
            uint64_t total = du + ds + di;
            double pct = total > 0 ? 100.0 * (du + ds) / total : 0.0;
            usage.push_back(pct);
        }

        if (i < prev_user.size()) {
            prev_user[i] = user;
            prev_system[i] = sys;
            prev_idle[i] = idle;
        }
    }

    vm_deallocate(mach_task_self_,
                  reinterpret_cast<vm_address_t>(info_array),
                  info_count * sizeof(integer_t));
    return usage;
}

bool SensorManager::init_iokit() {
    // macOS always has sensor info available through various APIs
    return true;
}

void SensorManager::poll_iokit() {
    // --- 1. Thermal state (works on all macOS including Apple Silicon) ---
    int thermal_state = get_thermal_state();
    if (thermal_state >= 0) {
        // Map thermal state to approximate temperature range for display
        // 0=nominal(~40°C), 1=fair(~65°C), 2=serious(~85°C), 3=critical(~100°C)
        static const double temp_map[] = {40.0, 65.0, 85.0, 100.0};
        double approx_temp = (thermal_state >= 0 && thermal_state <= 3)
                           ? temp_map[thermal_state] : 0.0;

        static const char* state_names[] = {"Nominal", "Fair", "Serious", "Critical"};
        const char* state_name = (thermal_state >= 0 && thermal_state <= 3)
                               ? state_names[thermal_state] : "Unknown";

        update_reading("Thermal State", "CPU",
                       static_cast<double>(thermal_state), "level");

        // Use thermal state as CPU temperature estimate on Apple Silicon
        // (actual die temp requires root/entitlements)
        update_reading("CPU Temperature", "CPU", approx_temp, "C");

        (void)state_name; // Used for logging if needed
    }

    // --- 2. Intel thermal level (only works on Intel Macs) ---
    {
        int thermal_level = 0;
        size_t len = sizeof(thermal_level);
        if (sysctlbyname("machdep.xcpm.cpu_thermal_level", &thermal_level,
                         &len, nullptr, 0) == 0) {
            update_reading("CPU Thermal Level", "CPU",
                           static_cast<double>(thermal_level), "%");
        }
    }

    // --- 3. IOHIDSensor (works on some Intel Macs) ---
    {
        io_iterator_t iter = 0;
        kern_return_t kr = IOServiceGetMatchingServices(
            kIOMainPortDefault,
            IOServiceMatching("IOHIDSensor"),
            &iter);

        if (kr == KERN_SUCCESS) {
            io_object_t sensor;
            while ((sensor = IOIteratorNext(iter)) != 0) {
                CFTypeRef product = IORegistryEntryCreateCFProperty(
                    sensor, CFSTR("Product"), kCFAllocatorDefault, 0);
                CFTypeRef primary = IORegistryEntryCreateCFProperty(
                    sensor, CFSTR("PrimaryUsagePage"), kCFAllocatorDefault, 0);

                if (product && primary) {
                    int32_t usage_page = 0;
                    if (CFGetTypeID(primary) == CFNumberGetTypeID()) {
                        CFNumberGetValue(static_cast<CFNumberRef>(primary),
                                         kCFNumberSInt32Type, &usage_page);
                    }
                    if (usage_page == IOKIT_USAGE_PAGE_TEMPERATURE) {
                        CFTypeRef current_val = IORegistryEntryCreateCFProperty(
                            sensor, CFSTR("CurrentValue"), kCFAllocatorDefault, 0);
                        if (current_val && CFGetTypeID(current_val) == CFNumberGetTypeID()) {
                            int64_t raw_val = 0;
                            CFNumberGetValue(static_cast<CFNumberRef>(current_val),
                                             kCFNumberSInt64Type, &raw_val);
                            double temp = raw_val / IOKIT_FIXED_POINT_DIVISOR;

                            char name_buf[128] = "Unknown";
                            if (CFGetTypeID(product) == CFStringGetTypeID()) {
                                CFStringGetCString(static_cast<CFStringRef>(product),
                                                   name_buf, sizeof(name_buf),
                                                   kCFStringEncodingUTF8);
                            }
                            std::string sname(name_buf);
                            std::string cat = "Motherboard";
                            if (sname.find("CPU") != std::string::npos ||
                                sname.find("Die") != std::string::npos) {
                                cat = "CPU";
                            } else if (sname.find("GPU") != std::string::npos) {
                                cat = "GPU";
                            }
                            update_reading(sname, cat, temp, "C");
                            CFRelease(current_val);
                        }
                    }
                }
                if (product) CFRelease(product);
                if (primary) CFRelease(primary);
                IOObjectRelease(sensor);
            }
            IOObjectRelease(iter);
        }
    }

    // --- 4. Battery info (temperature + system power on laptops) ---
    {
        auto batt = read_battery_info();
        if (batt.valid) {
            update_reading("Battery Temperature", "Motherboard",
                           batt.temperature, "C");
            if (batt.watts > 0.1) {
                update_reading("System Power", "CPU", batt.watts, "W");
            }
            if (batt.voltage > 0) {
                update_reading("Battery Voltage", "Motherboard",
                               batt.voltage, "V");
            }
        }
    }

    // --- 5. CPU usage per core ---
    {
        auto usage = get_cpu_usage();
        double total = 0;
        for (size_t i = 0; i < usage.size(); ++i) {
            update_reading("Core " + std::to_string(i) + " Usage", "CPU",
                           usage[i], "%");
            total += usage[i];
        }
        if (!usage.empty()) {
            update_reading("CPU Usage", "CPU",
                           total / usage.size(), "%");
        }
    }

    // --- 6. Memory pressure ---
    {
        int mem_level = 0;
        size_t len = sizeof(mem_level);
        if (sysctlbyname("kern.memorystatus_level", &mem_level,
                         &len, nullptr, 0) == 0) {
            // kern.memorystatus_level: percentage of memory available (0-100)
            update_reading("Memory Available", "System",
                           static_cast<double>(mem_level), "%");
            update_reading("Memory Usage", "System",
                           100.0 - mem_level, "%");
        }
    }

    // --- 7. CPU frequency info ---
    {
        int64_t freq = 0;
        size_t len = sizeof(freq);
        // Try hw.cpufrequency_max (Intel) then hw.tbfrequency (Apple Silicon timebase)
        if (sysctlbyname("hw.cpufrequency_max", &freq, &len, nullptr, 0) == 0 && freq > 0) {
            update_reading("CPU Max Frequency", "CPU",
                           freq / 1e6, "MHz");
        }

        // Apple Silicon: report number of P-cores and E-cores
        int pcores = 0, ecores = 0;
        len = sizeof(pcores);
        if (sysctlbyname("hw.perflevel0.physicalcpu", &pcores, &len, nullptr, 0) == 0) {
            update_reading("P-Cores", "CPU", static_cast<double>(pcores), "count");
        }
        len = sizeof(ecores);
        if (sysctlbyname("hw.perflevel1.physicalcpu", &ecores, &len, nullptr, 0) == 0) {
            update_reading("E-Cores", "CPU", static_cast<double>(ecores), "count");
        }
    }
}

#else // !__APPLE__
bool SensorManager::init_iokit() { return false; }
void SensorManager::poll_iokit() {}
#endif

// ─── Windows: WMI backend ────────────────────────────────────────────────────

#if defined(_WIN32)

void SensorManager::cleanup_wmi() {
    if (wmi_svc_cimv2_) {
        static_cast<IWbemServices*>(wmi_svc_cimv2_)->Release();
        wmi_svc_cimv2_ = nullptr;
    }
    if (wmi_svc_root_wmi_) {
        static_cast<IWbemServices*>(wmi_svc_root_wmi_)->Release();
        wmi_svc_root_wmi_ = nullptr;
    }
    if (wmi_locator_) {
        static_cast<IWbemLocator*>(wmi_locator_)->Release();
        wmi_locator_ = nullptr;
    }
}

bool SensorManager::reconnect_wmi() {
    cleanup_wmi();

    IWbemLocator* locator = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, reinterpret_cast<void**>(&locator));
    if (FAILED(hr)) return false;
    wmi_locator_ = locator;

    // Connect to ROOT\WMI (for thermal zones)
    IWbemServices* svc_root_wmi = nullptr;
    hr = locator->ConnectServer(
        _bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr,
        0, nullptr, nullptr, &svc_root_wmi);
    if (SUCCEEDED(hr)) {
        wmi_svc_root_wmi_ = svc_root_wmi;
    }

    // Connect to ROOT\CIMV2 (for fans, CPU info, battery)
    IWbemServices* svc_cimv2 = nullptr;
    hr = locator->ConnectServer(
        _bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr,
        0, nullptr, nullptr, &svc_cimv2);
    if (SUCCEEDED(hr)) {
        wmi_svc_cimv2_ = svc_cimv2;
    }

    return (wmi_svc_root_wmi_ != nullptr || wmi_svc_cimv2_ != nullptr);
}

bool SensorManager::init_wmi() {
    HRESULT hr;

    try {
        hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    } catch (...) {
        std::cerr << "[Sensor] Warning: CoInitializeEx threw exception, trying fallback" << std::endl;
        hr = E_FAIL;
    }

    if (hr == RPC_E_CHANGED_MODE) {
        // COM already initialized with a different threading model -
        // this is fine, we can still use it.
        std::cerr << "[Sensor] COM already initialized (apartment-threaded), continuing" << std::endl;
    } else if (FAILED(hr)) {
        std::cerr << "[Sensor] Warning: COM initialization failed (0x"
                  << std::hex << hr << std::dec << "), falling back to basic system info" << std::endl;
        collect_basic_system_info();
        return false;
    }

    hr = CoInitializeSecurity(
        nullptr, -1, nullptr, nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr, EOAC_NONE, nullptr);

    // RPC_E_TOO_LATE is OK if already initialized
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
        std::cerr << "[Sensor] Warning: CoInitializeSecurity failed (0x"
                  << std::hex << hr << std::dec << "), WMI may have limited access" << std::endl;
        // Don't return false - WMI might still work without explicit security setup
    }

    // Cache WMI locator and service connections
    if (!reconnect_wmi()) {
        std::cerr << "[Sensor] Warning: WMI connection failed, falling back to basic system info" << std::endl;
        collect_basic_system_info();
        return false;
    }

    return true;
}

void SensorManager::collect_basic_system_info() {
    // Fallback when WMI is not available: use basic Win32 API
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    update_reading("CPU Cores", "CPU",
                   static_cast<double>(si.dwNumberOfProcessors), "count");

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        update_reading("Memory Load", "System",
                       static_cast<double>(mem.dwMemoryLoad), "%");
        update_reading("Total Physical", "System",
                       static_cast<double>(mem.ullTotalPhys) / (1024.0 * 1024.0), "MB");
        update_reading("Available Physical", "System",
                       static_cast<double>(mem.ullAvailPhys) / (1024.0 * 1024.0), "MB");
    }
}

void SensorManager::init_pdh() {
    PDH_STATUS status = PdhOpenQuery(nullptr, 0, &pdh_query_);
    if (status != ERROR_SUCCESS) {
        std::cerr << "[Sensor] PDH: failed to open query (0x"
                  << std::hex << status << std::dec << ")" << std::endl;
        pdh_query_ = nullptr;
        return;
    }

    status = PdhAddEnglishCounterW(pdh_query_,
        L"\\Processor Information(_Total)\\% of Maximum Frequency",
        0, &pdh_freq_counter_);
    if (status != ERROR_SUCCESS) {
        std::cerr << "[Sensor] PDH: failed to add frequency counter (0x"
                  << std::hex << status << std::dec << ")" << std::endl;
        pdh_freq_counter_ = nullptr;
    }

    // Initial data collection (PDH requires two samples)
    PdhCollectQueryData(pdh_query_);
    std::cout << "[Sensor] PDH dynamic frequency monitoring initialized" << std::endl;
}

void SensorManager::cleanup_pdh() {
    if (pdh_query_) {
        PdhCloseQuery(pdh_query_);
        pdh_query_ = nullptr;
        pdh_freq_counter_ = nullptr;
    }
}

void SensorManager::poll_wmi() {
    // WMI query timeout: 5 seconds (prevents hangs on some machines)
    static const long kWmiTimeoutMs = 5000;

    // Use cached WMI connections; reconnect if they were lost
    if (!wmi_locator_) {
        if (!reconnect_wmi()) {
            collect_basic_system_info();
            return;
        }
    }

    auto* locator = static_cast<IWbemLocator*>(wmi_locator_);
    HRESULT hr;

    // Helper lambda: detect transport/connection failures that require reconnect
    auto is_connection_error = [](HRESULT h) {
        return h == WBEM_E_TRANSPORT_FAILURE ||
               h == WBEM_E_INVALID_NAMESPACE ||
               h == RPC_E_DISCONNECTED ||
               h == RPC_E_SERVER_DIED ||
               h == RPC_E_SERVER_DIED_DNE;
    };

    // --- Try LHM bridge first (most accurate) ---
    bool have_lhm_data = false;
    if (lhm_bridge_ && lhm_bridge_->is_available()) {
        std::vector<SensorReading> lhm_readings;
        lhm_bridge_->poll(lhm_readings);
        fprintf(stderr, "[Sensor-DIAG] LHM available=%d, lhm_readings=%zu, have_lhm_data=%d\n",
            lhm_bridge_->is_available(), lhm_readings.size(), have_lhm_data);
        if (!lhm_readings.empty()) {
            for (const auto& r : lhm_readings) {
                update_reading(r.name, r.category, r.value, r.unit);
            }
            have_lhm_data = true;

            // LHM stale data detection: check if CPU temp is unchanged
            double cur_temp = 0.0;
            double cur_power = 0.0;
            for (const auto& r : lhm_readings) {
                if (r.category == "CPU" && r.unit == "C" && r.value > cur_temp) {
                    cur_temp = r.value;
                }
                if (r.category == "CPU" && r.unit == "W" && r.value > cur_power) {
                    cur_power = r.value;
                }
            }
            if (cur_temp > 0.0 && cur_temp == prev_lhm_cpu_temp_ &&
                cur_power == prev_lhm_cpu_power_) {
                lhm_stale_count_++;
                if (lhm_stale_count_ >= 20) {  // 10s at 500ms interval
                    std::cerr << "[Sensor] LHM data appears stale (unchanged for "
                              << (lhm_stale_count_ / 2) << "s)" << std::endl;
                }
            } else {
                lhm_stale_count_ = 0;
            }
            prev_lhm_cpu_temp_ = cur_temp;
            prev_lhm_cpu_power_ = cur_power;
        }
    }

    if (!have_lhm_data) {
        // --- Query ROOT\WMI for thermal zones (cached connection) ---
        int zone_idx = 0;
        if (wmi_svc_root_wmi_) {
            auto* services = static_cast<IWbemServices*>(wmi_svc_root_wmi_);
            IEnumWbemClassObject* enumerator = nullptr;
            hr = services->ExecQuery(
                _bstr_t(L"WQL"),
                _bstr_t(L"SELECT * FROM MSAcpi_ThermalZoneTemperature"),
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                nullptr, &enumerator);

            if (is_connection_error(hr)) {
                // Connection lost - try to reconnect on next poll
                std::cerr << "[Sensor] WMI ROOT\\WMI connection lost, will reconnect" << std::endl;
                cleanup_wmi();
                collect_basic_system_info();
                return;
            }

            if (SUCCEEDED(hr) && enumerator) {
                IWbemClassObject* obj = nullptr;
                ULONG returned = 0;

                while (enumerator->Next(kWmiTimeoutMs, 1, &obj, &returned) == S_OK) {
                    VARIANT vt;
                    hr = obj->Get(L"CurrentTemperature", 0, &vt, nullptr, nullptr);
                    if (SUCCEEDED(hr)) {
                        double temp_c = (vt.intVal / WMI_TEMP_TENTHS_TO_DEGREES) - KELVIN_TO_CELSIUS_OFFSET;
                        update_reading("ACPI Zone " + std::to_string(zone_idx),
                                       "CPU", temp_c, "C");
                        VariantClear(&vt);
                    }
                    obj->Release();
                    zone_idx++;
                }
                enumerator->Release();
            } else if (hr == WBEM_E_ACCESS_DENIED) {
                std::cerr << "[Sensor] Warning: WMI thermal query access denied" << std::endl;
            }
        }

        // Log when MSAcpi returns 0 zones and try fallback
        if (zone_idx == 0) {
            static bool thermal_fallback_logged = false;
            if (!thermal_fallback_logged) {
                std::cerr << "[Sensor] MSAcpi_ThermalZoneTemperature returned 0 zones, trying fallback" << std::endl;
                thermal_fallback_logged = true;
            }

            // Fallback: try performance counter thermal zones on CIMV2
            if (wmi_svc_cimv2_) {
                auto* cimv2 = static_cast<IWbemServices*>(wmi_svc_cimv2_);
                IEnumWbemClassObject* enumerator = nullptr;
                hr = cimv2->ExecQuery(
                    _bstr_t(L"WQL"),
                    _bstr_t(L"SELECT Temperature FROM Win32_PerfFormattedData_Counters_ThermalZoneInformation"),
                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                    nullptr, &enumerator);
                if (SUCCEEDED(hr) && enumerator) {
                    IWbemClassObject* obj = nullptr;
                    ULONG returned = 0;
                    int tz_idx = 0;
                    while (enumerator->Next(kWmiTimeoutMs, 1, &obj, &returned) == S_OK) {
                        VARIANT vt;
                        hr = obj->Get(L"Temperature", 0, &vt, nullptr, nullptr);
                        if (SUCCEEDED(hr)) {
                            double temp_c = static_cast<double>(vt.intVal) - KELVIN_TO_CELSIUS_OFFSET;
                            if (temp_c > 0 && temp_c < 150) {
                                update_reading("Thermal Zone " + std::to_string(tz_idx),
                                               "CPU", temp_c, "C");
                            }
                            VariantClear(&vt);
                        }
                        obj->Release();
                        tz_idx++;
                    }
                    enumerator->Release();

                    if (tz_idx == 0) {
                        static bool perf_thermal_logged = false;
                        if (!perf_thermal_logged) {
                            std::cerr << "[Sensor] Win32_PerfFormattedData_Counters_ThermalZoneInformation also returned 0 zones" << std::endl;
                            perf_thermal_logged = true;
                        }
                        // No thermal data available — do NOT write 0 to readings_
                        // (preserves any previous valid LHM reading)
                    }
                }
            }
        }

        // --- Query ROOT\CIMV2 for fans, CPU info, battery (cached connection) ---
        if (wmi_svc_cimv2_) {
            auto* cimv2 = static_cast<IWbemServices*>(wmi_svc_cimv2_);

            // Fans
            {
                IEnumWbemClassObject* enumerator = nullptr;
                hr = cimv2->ExecQuery(
                    _bstr_t(L"WQL"),
                    _bstr_t(L"SELECT * FROM Win32_Fan"),
                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                    nullptr, &enumerator);

                if (is_connection_error(hr)) {
                    std::cerr << "[Sensor] WMI CIMV2 connection lost, will reconnect" << std::endl;
                    cleanup_wmi();
                    collect_basic_system_info();
                    return;
                }

                if (SUCCEEDED(hr) && enumerator) {
                    IWbemClassObject* obj = nullptr;
                    ULONG returned = 0;
                    int fan_idx = 0;

                    while (enumerator->Next(kWmiTimeoutMs, 1, &obj, &returned) == S_OK) {
                        VARIANT vt;
                        hr = obj->Get(L"DesiredSpeed", 0, &vt, nullptr, nullptr);
                        if (SUCCEEDED(hr)) {
                            update_reading("Fan " + std::to_string(fan_idx),
                                           "Motherboard",
                                           static_cast<double>(vt.intVal), "RPM");
                            VariantClear(&vt);
                        }
                        obj->Release();
                        fan_idx++;
                    }
                    enumerator->Release();

                    if (fan_idx == 0) {
                        static bool fan_empty_logged = false;
                        if (!fan_empty_logged) {
                            std::cerr << "[Sensor] Win32_Fan returned 0 fans" << std::endl;
                            fan_empty_logged = true;
                        }
                    }
                }
            }

            // CPU info: MaxClockSpeed, NumberOfCores, Name (brand)
            // Always query to cache max_clock_speed_ and cpu_brand_
            {
                static bool cpu_info_collected = false;
                IEnumWbemClassObject* enumerator = nullptr;
                hr = cimv2->ExecQuery(
                    _bstr_t(L"WQL"),
                    _bstr_t(L"SELECT MaxClockSpeed, NumberOfCores, Name FROM Win32_Processor"),
                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                    nullptr, &enumerator);
                if (SUCCEEDED(hr) && enumerator) {
                    IWbemClassObject* obj = nullptr;
                    ULONG returned = 0;
                    bool got_data = false;
                    if (enumerator->Next(kWmiTimeoutMs, 1, &obj, &returned) == S_OK) {
                        VARIANT vt;
                        hr = obj->Get(L"MaxClockSpeed", 0, &vt, nullptr, nullptr);
                        if (SUCCEEDED(hr)) {
                            max_clock_speed_ = static_cast<double>(vt.intVal);
                            update_reading("CPU Max Frequency", "CPU",
                                           max_clock_speed_, "MHz");
                            VariantClear(&vt);
                        }
                        hr = obj->Get(L"NumberOfCores", 0, &vt, nullptr, nullptr);
                        if (SUCCEEDED(hr)) {
                            update_reading("CPU Cores", "CPU",
                                           static_cast<double>(vt.intVal), "count");
                            VariantClear(&vt);
                        }
                        // Get CPU brand name for TDP estimation
                        if (!cpu_info_collected) {
                            hr = obj->Get(L"Name", 0, &vt, nullptr, nullptr);
                            if (SUCCEEDED(hr) && vt.vt == VT_BSTR) {
                                int len = WideCharToMultiByte(CP_UTF8, 0, vt.bstrVal, -1,
                                                              nullptr, 0, nullptr, nullptr);
                                if (len > 0) {
                                    cpu_brand_.resize(len - 1);
                                    WideCharToMultiByte(CP_UTF8, 0, vt.bstrVal, -1,
                                                        &cpu_brand_[0], len, nullptr, nullptr);
                                }
                                VariantClear(&vt);
                            }
                        }
                        obj->Release();
                        got_data = true;
                        cpu_info_collected = true;
                    }
                    enumerator->Release();

                    if (!got_data) {
                        static bool cpu_empty_logged = false;
                        if (!cpu_empty_logged) {
                            std::cerr << "[Sensor] Win32_Processor returned no data" << std::endl;
                            cpu_empty_logged = true;
                        }
                    }
                }
            }

            // Battery info for laptops
            {
                IEnumWbemClassObject* enumerator = nullptr;
                hr = cimv2->ExecQuery(
                    _bstr_t(L"WQL"),
                    _bstr_t(L"SELECT EstimatedChargeRemaining FROM Win32_Battery"),
                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                    nullptr, &enumerator);
                if (SUCCEEDED(hr) && enumerator) {
                    IWbemClassObject* obj = nullptr;
                    ULONG returned = 0;
                    if (enumerator->Next(kWmiTimeoutMs, 1, &obj, &returned) == S_OK) {
                        VARIANT vt;
                        hr = obj->Get(L"EstimatedChargeRemaining", 0, &vt, nullptr, nullptr);
                        if (SUCCEEDED(hr)) {
                            update_reading("Battery Level", "System",
                                           static_cast<double>(vt.intVal), "%");
                            VariantClear(&vt);
                        }
                        obj->Release();
                    }
                    enumerator->Release();
                }
            }
        }
    } // end if (!have_lhm_data)

    // --- A) CPU usage via GetSystemTimes() (always collected) ---
    double cpu_pct = 0.0;
    {
        static ULARGE_INTEGER prev_idle = {0}, prev_kernel = {0}, prev_user = {0};
        FILETIME idle_ft, kernel_ft, user_ft;
        if (GetSystemTimes(&idle_ft, &kernel_ft, &user_ft)) {
            ULARGE_INTEGER cur_idle, cur_kernel, cur_user;
            cur_idle.LowPart = idle_ft.dwLowDateTime;
            cur_idle.HighPart = idle_ft.dwHighDateTime;
            cur_kernel.LowPart = kernel_ft.dwLowDateTime;
            cur_kernel.HighPart = kernel_ft.dwHighDateTime;
            cur_user.LowPart = user_ft.dwLowDateTime;
            cur_user.HighPart = user_ft.dwHighDateTime;

            if (prev_idle.QuadPart != 0 || prev_kernel.QuadPart != 0) {
                ULONGLONG d_idle = cur_idle.QuadPart - prev_idle.QuadPart;
                ULONGLONG d_kernel = cur_kernel.QuadPart - prev_kernel.QuadPart;
                ULONGLONG d_user = cur_user.QuadPart - prev_user.QuadPart;
                ULONGLONG d_total = d_kernel + d_user;
                cpu_pct = (d_total > 0)
                    ? 100.0 * (d_total - d_idle) / d_total
                    : 0.0;
                update_reading("CPU Usage", "CPU", cpu_pct, "%");
            }

            prev_idle = cur_idle;
            prev_kernel = cur_kernel;
            prev_user = cur_user;
        }
    }

    // --- B) CPU power estimation (from usage * TDP) ---
    if (!have_lhm_data && cpu_pct > 0.0) {
        double tdp = estimate_tdp(cpu_brand_);
        double estimated_power = tdp * (cpu_pct / 100.0);
        update_reading("CPU Power", "CPU", estimated_power, "W");
        {
            std::lock_guard<std::mutex> lk(readings_mutex_);
            cpu_power_estimated_ = true;
        }
    } else if (have_lhm_data) {
        std::lock_guard<std::mutex> lk(readings_mutex_);
        cpu_power_estimated_ = false;
    }

    // --- C) PDH dynamic CPU frequency ---
    if (pdh_query_ && pdh_freq_counter_ && max_clock_speed_ > 0.0) {
        PdhCollectQueryData(pdh_query_);
        PDH_FMT_COUNTERVALUE val;
        PDH_STATUS status = PdhGetFormattedCounterValue(
            pdh_freq_counter_, PDH_FMT_DOUBLE, nullptr, &val);
        if (status == ERROR_SUCCESS) {
            double pct = val.doubleValue; // % of maximum frequency
            double current_mhz = max_clock_speed_ * pct / 100.0;
            update_reading("CPU Frequency", "CPU", current_mhz, "MHz");
        }
    }

    // --- D) Memory info (always collected, no WMI needed) ---
    {
        MEMORYSTATUSEX mem{};
        mem.dwLength = sizeof(mem);
        if (GlobalMemoryStatusEx(&mem)) {
            update_reading("Memory Load", "System",
                           static_cast<double>(mem.dwMemoryLoad), "%");
            update_reading("Total Physical", "System",
                           static_cast<double>(mem.ullTotalPhys) / (1024.0 * 1024.0), "MB");
            update_reading("Available Physical", "System",
                           static_cast<double>(mem.ullAvailPhys) / (1024.0 * 1024.0), "MB");
        }
    }
}

#else // !_WIN32
bool SensorManager::init_wmi() { return false; }
void SensorManager::poll_wmi() {}
void SensorManager::collect_basic_system_info() {}
#endif

// ─── NVIDIA NVML (dynamic loading) ──────────────────────────────────────────

bool SensorManager::init_nvml() {
#if defined(_WIN32)
    nvml_handle_ = LoadLibraryA("nvml.dll");
    if (!nvml_handle_) return false;

    auto init_fn = reinterpret_cast<nvmlInit_t>(
        GetProcAddress(static_cast<HMODULE>(nvml_handle_), "nvmlInit_v2"));
    if (!init_fn) {
        FreeLibrary(static_cast<HMODULE>(nvml_handle_));
        nvml_handle_ = nullptr;
        return false;
    }
    return init_fn() == 0; // NVML_SUCCESS = 0

#elif defined(__linux__)
    nvml_handle_ = dlopen("libnvidia-ml.so.1", RTLD_NOW);
    if (!nvml_handle_) {
        nvml_handle_ = dlopen("libnvidia-ml.so", RTLD_NOW);
    }
    if (!nvml_handle_) return false;

    auto init_fn = reinterpret_cast<nvmlInit_t>(dlsym(nvml_handle_, "nvmlInit_v2"));
    if (!init_fn) {
        dlclose(nvml_handle_);
        nvml_handle_ = nullptr;
        return false;
    }
    return init_fn() == 0;

#else
    // macOS: NVIDIA drivers not available on modern macOS
    return false;
#endif
}

void SensorManager::poll_nvml() {
    if (!nvml_handle_) return;

#if defined(_WIN32)
    #define LOAD_NVML(name) reinterpret_cast<name##_t>( \
        GetProcAddress(static_cast<HMODULE>(nvml_handle_), #name))
#elif defined(__linux__)
    #define LOAD_NVML(name) reinterpret_cast<name##_t>(dlsym(nvml_handle_, #name))
#else
    return;
    #define LOAD_NVML(name) nullptr
#endif

#if !defined(__APPLE__)
    auto getCount = LOAD_NVML(nvmlDeviceGetCount);
    auto getHandle = LOAD_NVML(nvmlDeviceGetHandleByIndex);
    auto getTemp = LOAD_NVML(nvmlDeviceGetTemperature);
    auto getPower = LOAD_NVML(nvmlDeviceGetPowerUsage);
    auto getName = LOAD_NVML(nvmlDeviceGetName);

    if (!getCount || !getHandle) return;

    unsigned int count = 0;
    if (getCount(&count) != 0) return;

    for (unsigned int i = 0; i < count; ++i) {
        void* device = nullptr;
        if (getHandle(i, &device) != 0) continue;

        char dev_name[96] = "GPU";
        if (getName) getName(device, dev_name, sizeof(dev_name));

        std::string gpu_label = std::string(dev_name);
        if (count > 1) gpu_label += " #" + std::to_string(i);

        if (getTemp) {
            unsigned int temp = 0;
            auto ret = getTemp(device, 0 /*NVML_TEMPERATURE_GPU*/, &temp);
            if (ret == 0 && temp > 0) {
                update_reading(gpu_label + " Temp", "GPU",
                               static_cast<double>(temp), "C");
            } else {
                update_reading(gpu_label + " Temp", "GPU", 0.0, "C");  // stale prevention
            }
        }

        if (getPower) {
            unsigned int power_mw = 0;
            auto ret = getPower(device, &power_mw);
            if (ret == 0 && power_mw > 0) {
                update_reading(gpu_label + " Power", "GPU",
                               power_mw / 1000.0, "W");
            } else {
                update_reading(gpu_label + " Power", "GPU", 0.0, "W");  // stale prevention
            }
        }
    }
#endif

    #undef LOAD_NVML
}

// ─── AMD ADL (dynamic loading) ───────────────────────────────────────────────

// ADL memory allocation callback required by ADL2_Main_Control_Create
#if defined(_WIN32)
// Proper ADL memory allocation callback: receives size, returns pointer via malloc
static void* __stdcall adl_malloc_callback(int size) {
    return malloc(static_cast<size_t>(size));
}
#endif

bool SensorManager::init_adl() {
#if defined(_WIN32)
    adl_handle_ = LoadLibraryA("atiadlxx.dll");
    if (!adl_handle_) {
        adl_handle_ = LoadLibraryA("atiadlxy.dll"); // 32-bit fallback
    }
    if (!adl_handle_) {
        std::cerr << "[Sensor] ADL: could not load atiadlxx.dll or atiadlxy.dll" << std::endl;
        return false;
    }

    // Resolve core functions
    adl_create_ = reinterpret_cast<ADL2_MAIN_CONTROL_CREATE>(
        GetProcAddress(static_cast<HMODULE>(adl_handle_), "ADL2_Main_Control_Create"));
    adl_destroy_ = reinterpret_cast<ADL2_MAIN_CONTROL_DESTROY>(
        GetProcAddress(static_cast<HMODULE>(adl_handle_), "ADL2_Main_Control_Destroy"));
    adl_num_adapters_ = reinterpret_cast<ADL2_ADAPTER_NUMBEROFADAPTERS_GET>(
        GetProcAddress(static_cast<HMODULE>(adl_handle_), "ADL2_Adapter_NumberOfAdapters_Get"));
    adl_active_ = reinterpret_cast<ADL2_ADAPTER_ACTIVE_GET>(
        GetProcAddress(static_cast<HMODULE>(adl_handle_), "ADL2_Adapter_Active_Get"));

    // Try multiple temperature function names (different Overdrive versions)
    const char* temp_func_names[] = {
        "ADL2_OverdriveN_Temperature_Get",
        "ADL2_Overdrive6_Temperature_Get",
        "ADL2_Overdrive5_Temperature_Get",
        nullptr
    };
    for (int i = 0; temp_func_names[i] != nullptr; ++i) {
        adl_temp_ = reinterpret_cast<ADL2_OVERDRIVE_TEMPERATURE_GET>(
            GetProcAddress(static_cast<HMODULE>(adl_handle_), temp_func_names[i]));
        if (adl_temp_) {
            std::cerr << "[Sensor] ADL: resolved temperature function: "
                      << temp_func_names[i] << std::endl;
            break;
        } else {
            std::cerr << "[Sensor] ADL: " << temp_func_names[i] << " not found" << std::endl;
        }
    }

    if (!adl_create_ || !adl_num_adapters_) {
        std::cerr << "[Sensor] ADL: critical functions not found in DLL" << std::endl;
        FreeLibrary(static_cast<HMODULE>(adl_handle_));
        adl_handle_ = nullptr;
        return false;
    }

    // Initialize ADL2 context
    // ADL2_Main_Control_Create expects: callback(int) -> void*, enumConnectedAdapters, context**
    // We cast our malloc callback to the expected signature.
    using AdlMallocCallback = void* (__stdcall *)(int);
    auto malloc_cb = reinterpret_cast<int (*)(int)>(
        static_cast<AdlMallocCallback>(adl_malloc_callback));
    int adl_status = adl_create_(malloc_cb, 1, &adl_context_);
    if (adl_status != 0) { // ADL_OK == 0
        std::cerr << "[Sensor] ADL: ADL2_Main_Control_Create failed with code "
                  << adl_status << std::endl;
        FreeLibrary(static_cast<HMODULE>(adl_handle_));
        adl_handle_ = nullptr;
        adl_create_ = nullptr;
        return false;
    }

    // Get adapter count
    adl_adapter_count_ = 0;
    if (adl_num_adapters_(adl_context_, &adl_adapter_count_) != 0 || adl_adapter_count_ <= 0) {
        std::cerr << "[Sensor] ADL: no adapters found" << std::endl;
        if (adl_destroy_) adl_destroy_(adl_context_);
        adl_context_ = nullptr;
        FreeLibrary(static_cast<HMODULE>(adl_handle_));
        adl_handle_ = nullptr;
        return false;
    }

    std::cerr << "[Sensor] ADL initialized successfully, " << adl_adapter_count_
              << " adapter(s) found" << std::endl;
    return true;

#elif defined(__linux__)
    adl_handle_ = dlopen("libatiadlxx.so", RTLD_NOW);
    if (!adl_handle_) {
        std::cerr << "[Sensor] ADL: could not load libatiadlxx.so" << std::endl;
        return false;
    }
    // On Linux, AMD GPU temperature is typically available through sysfs/hwmon
    // (amdgpu driver). ADL on Linux is uncommon, so we just note its presence.
    std::cerr << "[Sensor] ADL: loaded on Linux, but sysfs hwmon is preferred for AMD GPU" << std::endl;
    return false; // Don't activate ADL polling on Linux; sysfs handles it

#else
    return false;
#endif
}

void SensorManager::poll_adl() {
#if defined(_WIN32)
    if (!adl_context_ || !adl_active_) {
        if (!adl_stub_logged_) {
            std::cerr << "[Sensor] ADL poll skipped - context or functions unavailable" << std::endl;
            adl_stub_logged_ = true;
        }
        return;
    }

    for (int i = 0; i < adl_adapter_count_; ++i) {
        // Check if adapter is active
        int active = 0;
        if (adl_active_(adl_context_, i, &active) != 0 || !active) {
            continue;
        }

        // Get temperature if function is available
        if (adl_temp_) {
            int temperature = 0;
            // Domain 1 = GPU core temperature for most Overdrive versions
            int status = adl_temp_(adl_context_, i, 1, &temperature);

            std::string sensor_name = "GPU Temperature";
            if (adl_adapter_count_ > 1) {
                sensor_name = "GPU " + std::to_string(i) + " Temperature";
            }

            if (status == 0) { // ADL_OK
                // Temperature may be in millidegrees (1000x) depending on the
                // Overdrive version. Values > 1000 suggest millidegrees.
                double temp_c = static_cast<double>(temperature);
                if (temp_c > 1000.0) {
                    temp_c /= 1000.0;
                }
                update_reading(sensor_name, "GPU", temp_c, "C");
            } else {
                update_reading(sensor_name, "GPU", 0.0, "C");  // stale prevention
            }
        }
    }
#else
    // On non-Windows platforms, ADL polling is not active.
    if (!adl_stub_logged_) {
        std::cerr << "[Sensor] ADL poll skipped - not supported on this platform" << std::endl;
        adl_stub_logged_ = true;
    }
    (void)adl_handle_;
#endif
}

// ─── Convenience filtered accessors ──────────────────────────────────────────

std::vector<SensorReading> SensorManager::get_fan_speeds() const {
    auto all = get_all_readings();
    std::vector<SensorReading> result;
    for (auto& r : all) {
        if (r.unit == "RPM") {
            result.push_back(std::move(r));
        }
    }
    return result;
}

std::vector<SensorReading> SensorManager::get_voltages() const {
    auto all = get_all_readings();
    std::vector<SensorReading> result;
    for (auto& r : all) {
        if (r.unit == "V") {
            result.push_back(std::move(r));
        }
    }
    return result;
}

} // namespace occt
