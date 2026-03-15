#include "gpu_info.h"
#include "config.h"

#include <algorithm>
#include <cctype>
#include <iostream>

// Platform-specific dynamic library loading
#ifdef OCCT_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
using LibHandle = HMODULE;
#define LIB_OPEN(name)      LoadLibraryA(name)
#define LIB_SYM(lib, sym)   GetProcAddress(lib, sym)
#define LIB_CLOSE(lib)      FreeLibrary(lib)
#else
#include <dlfcn.h>
using LibHandle = void*;
#define LIB_OPEN(name)      dlopen(name, RTLD_LAZY)
#define LIB_SYM(lib, sym)   dlsym(lib, sym)
#define LIB_CLOSE(lib)      dlclose(lib)
#endif

namespace occt { namespace utils {

// ─── Vendor detection ────────────────────────────────────────────────────────

GpuVendor parse_gpu_vendor(const std::string& vendor_string) {
    std::string lower = vendor_string;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower.find("nvidia") != std::string::npos) return GpuVendor::NVIDIA;
    if (lower.find("amd") != std::string::npos ||
        lower.find("advanced micro") != std::string::npos)
        return GpuVendor::AMD;
    if (lower.find("intel") != std::string::npos) return GpuVendor::INTEL;
    if (lower.find("apple") != std::string::npos) return GpuVendor::APPLE;
    return GpuVendor::UNKNOWN;
}

// ─── NVML types and function pointers ────────────────────────────────────────

namespace {

using nvmlReturn_t = int;
constexpr nvmlReturn_t NVML_SUCCESS = 0;

enum nvmlTemperatureSensors_t { NVML_TEMPERATURE_GPU = 0 };

struct nvmlUtilization_t {
    unsigned int gpu;
    unsigned int memory;
};

struct nvmlMemory_t {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

using nvmlDevice_t = void*;

// Function pointer types
using pfn_nvmlInit = nvmlReturn_t(*)();
using pfn_nvmlShutdown = nvmlReturn_t(*)();
using pfn_nvmlDeviceGetHandleByIndex = nvmlReturn_t(*)(unsigned int, nvmlDevice_t*);
using pfn_nvmlDeviceGetTemperature = nvmlReturn_t(*)(nvmlDevice_t, nvmlTemperatureSensors_t, unsigned int*);
using pfn_nvmlDeviceGetPowerUsage = nvmlReturn_t(*)(nvmlDevice_t, unsigned int*);
using pfn_nvmlDeviceGetUtilizationRates = nvmlReturn_t(*)(nvmlDevice_t, nvmlUtilization_t*);
using pfn_nvmlDeviceGetMemoryInfo = nvmlReturn_t(*)(nvmlDevice_t, nvmlMemory_t*);

struct NvmlState {
    LibHandle lib = nullptr;
    pfn_nvmlInit                      init = nullptr;
    pfn_nvmlShutdown                  shutdown = nullptr;
    pfn_nvmlDeviceGetHandleByIndex    getHandle = nullptr;
    pfn_nvmlDeviceGetTemperature      getTemp = nullptr;
    pfn_nvmlDeviceGetPowerUsage       getPower = nullptr;
    pfn_nvmlDeviceGetUtilizationRates getUtil = nullptr;
    pfn_nvmlDeviceGetMemoryInfo       getMemInfo = nullptr;
    bool loaded = false;
};

NvmlState g_nvml;

bool load_nvml() {
    if (g_nvml.loaded) return true;

#ifdef OCCT_PLATFORM_WINDOWS
    g_nvml.lib = LIB_OPEN("nvml.dll");
#elif defined(OCCT_PLATFORM_LINUX)
    g_nvml.lib = LIB_OPEN("libnvidia-ml.so.1");
    if (!g_nvml.lib) g_nvml.lib = LIB_OPEN("libnvidia-ml.so");
#elif defined(OCCT_PLATFORM_MACOS)
    // NVML is not typically available on macOS
    g_nvml.lib = nullptr;
#endif

    if (!g_nvml.lib) return false;

    g_nvml.init      = reinterpret_cast<pfn_nvmlInit>(LIB_SYM(g_nvml.lib, "nvmlInit_v2"));
    g_nvml.shutdown  = reinterpret_cast<pfn_nvmlShutdown>(LIB_SYM(g_nvml.lib, "nvmlShutdown"));
    g_nvml.getHandle = reinterpret_cast<pfn_nvmlDeviceGetHandleByIndex>(
                           LIB_SYM(g_nvml.lib, "nvmlDeviceGetHandleByIndex_v2"));
    g_nvml.getTemp   = reinterpret_cast<pfn_nvmlDeviceGetTemperature>(
                           LIB_SYM(g_nvml.lib, "nvmlDeviceGetTemperature"));
    g_nvml.getPower  = reinterpret_cast<pfn_nvmlDeviceGetPowerUsage>(
                           LIB_SYM(g_nvml.lib, "nvmlDeviceGetPowerUsage"));
    g_nvml.getUtil   = reinterpret_cast<pfn_nvmlDeviceGetUtilizationRates>(
                           LIB_SYM(g_nvml.lib, "nvmlDeviceGetUtilizationRates"));
    g_nvml.getMemInfo = reinterpret_cast<pfn_nvmlDeviceGetMemoryInfo>(
                           LIB_SYM(g_nvml.lib, "nvmlDeviceGetMemoryInfo"));

    // Fallback for older NVML versions
    if (!g_nvml.init)
        g_nvml.init = reinterpret_cast<pfn_nvmlInit>(LIB_SYM(g_nvml.lib, "nvmlInit"));
    if (!g_nvml.getHandle)
        g_nvml.getHandle = reinterpret_cast<pfn_nvmlDeviceGetHandleByIndex>(
                               LIB_SYM(g_nvml.lib, "nvmlDeviceGetHandleByIndex"));

    if (!g_nvml.init || !g_nvml.getHandle) {
        LIB_CLOSE(g_nvml.lib);
        g_nvml.lib = nullptr;
        return false;
    }

    if (g_nvml.init() != NVML_SUCCESS) {
        LIB_CLOSE(g_nvml.lib);
        g_nvml.lib = nullptr;
        return false;
    }

    g_nvml.loaded = true;
    return true;
}

void unload_nvml() {
    if (g_nvml.loaded && g_nvml.shutdown) {
        g_nvml.shutdown();
    }
    if (g_nvml.lib) {
        LIB_CLOSE(g_nvml.lib);
    }
    g_nvml = {};
}

GpuSensorData query_nvml(int device_index) {
    GpuSensorData data;
    if (!g_nvml.loaded) return data;

    nvmlDevice_t device = nullptr;
    if (g_nvml.getHandle(static_cast<unsigned int>(device_index), &device) != NVML_SUCCESS)
        return data;

    // Temperature
    if (g_nvml.getTemp) {
        unsigned int temp = 0;
        if (g_nvml.getTemp(device, NVML_TEMPERATURE_GPU, &temp) == NVML_SUCCESS)
            data.temperature_c = static_cast<double>(temp);
    }

    // Power (NVML returns milliwatts)
    if (g_nvml.getPower) {
        unsigned int power_mw = 0;
        if (g_nvml.getPower(device, &power_mw) == NVML_SUCCESS)
            data.power_watts = static_cast<double>(power_mw) / 1000.0;
    }

    // Utilization
    if (g_nvml.getUtil) {
        nvmlUtilization_t util = {};
        if (g_nvml.getUtil(device, &util) == NVML_SUCCESS) {
            data.gpu_usage_pct = static_cast<double>(util.gpu);
            data.mem_usage_pct = static_cast<double>(util.memory);
        }
    }

    // Memory
    if (g_nvml.getMemInfo) {
        nvmlMemory_t mem = {};
        if (g_nvml.getMemInfo(device, &mem) == NVML_SUCCESS) {
            data.vram_total_bytes = mem.total;
            data.vram_used_bytes = mem.used;
        }
    }

    return data;
}

// ─── AMD ADL types and function pointers ─────────────────────────────────────

// ADL function pointer types (simplified subset)
using ADL_MAIN_CONTROL_CREATE = int(*)(void* (*)(int), int);
using ADL_MAIN_CONTROL_DESTROY = int(*)();
using ADL_OVERDRIVE5_TEMPERATURE_GET = int(*)(int, int, void*);

struct AdlState {
    LibHandle lib = nullptr;
    ADL_MAIN_CONTROL_DESTROY destroy = nullptr;
    ADL_OVERDRIVE5_TEMPERATURE_GET getTemp = nullptr;
    bool loaded = false;
};

AdlState g_adl;

// ADL memory allocation callback
static void* adl_malloc(int size) {
    return malloc(static_cast<size_t>(size));
}

bool load_adl() {
    if (g_adl.loaded) return true;

#ifdef OCCT_PLATFORM_WINDOWS
    g_adl.lib = LIB_OPEN("atiadlxx.dll");
    if (!g_adl.lib) g_adl.lib = LIB_OPEN("atiadlxy.dll");
#elif defined(OCCT_PLATFORM_LINUX)
    g_adl.lib = LIB_OPEN("libatiadlxx.so");
#else
    g_adl.lib = nullptr;
#endif

    if (!g_adl.lib) return false;

    auto create = reinterpret_cast<ADL_MAIN_CONTROL_CREATE>(
                      LIB_SYM(g_adl.lib, "ADL_Main_Control_Create"));
    g_adl.destroy = reinterpret_cast<ADL_MAIN_CONTROL_DESTROY>(
                        LIB_SYM(g_adl.lib, "ADL_Main_Control_Destroy"));
    g_adl.getTemp = reinterpret_cast<ADL_OVERDRIVE5_TEMPERATURE_GET>(
                        LIB_SYM(g_adl.lib, "ADL_Overdrive5_Temperature_Get"));

    if (!create) {
        LIB_CLOSE(g_adl.lib);
        g_adl.lib = nullptr;
        return false;
    }

    if (create(adl_malloc, 1) != 0) {
        LIB_CLOSE(g_adl.lib);
        g_adl.lib = nullptr;
        return false;
    }

    g_adl.loaded = true;
    return true;
}

void unload_adl() {
    if (g_adl.loaded && g_adl.destroy) {
        g_adl.destroy();
    }
    if (g_adl.lib) {
        LIB_CLOSE(g_adl.lib);
    }
    g_adl = {};
}

GpuSensorData query_adl(int device_index) {
    GpuSensorData data;
    if (!g_adl.loaded) return data;

    // ADL temperature struct: { int iSize; int iTemperature; }
    // Temperature is in millidegrees Celsius
    if (g_adl.getTemp) {
        struct { int iSize; int iTemperature; } temp_data;
        temp_data.iSize = sizeof(temp_data);
        temp_data.iTemperature = 0;
        if (g_adl.getTemp(device_index, 0, &temp_data) == 0) {
            data.temperature_c = static_cast<double>(temp_data.iTemperature) / 1000.0;
        }
    }

    return data;
}

} // anonymous namespace

// ─── Public API ──────────────────────────────────────────────────────────────

bool gpu_monitor_init() {
    bool any = false;
    if (load_nvml()) {
        std::cerr << "[GPU Monitor] NVML loaded successfully" << std::endl;
        any = true;
    }
    if (load_adl()) {
        std::cerr << "[GPU Monitor] ADL loaded successfully" << std::endl;
        any = true;
    }
    return any;
}

void gpu_monitor_shutdown() {
    unload_nvml();
    unload_adl();
}

GpuSensorData gpu_query_sensors(GpuVendor vendor, int device_index) {
    switch (vendor) {
    case GpuVendor::NVIDIA:
        return query_nvml(device_index);
    case GpuVendor::AMD:
        return query_adl(device_index);
    case GpuVendor::INTEL:
    case GpuVendor::APPLE:
    case GpuVendor::UNKNOWN:
    default:
        return {};
    }
}

}} // namespace occt::utils
