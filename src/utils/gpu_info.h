#pragma once

#include <cstdint>
#include <string>

namespace occt { namespace utils {

/// GPU thermal and power metrics retrieved via vendor-specific APIs.
struct GpuSensorData {
    double temperature_c = -1.0;  // -1 means unavailable
    double power_watts = -1.0;
    double gpu_usage_pct = -1.0;
    double mem_usage_pct = -1.0;
    uint64_t vram_used_bytes = 0;
    uint64_t vram_total_bytes = 0;
};

/// Vendor identifier for selecting the correct monitoring API.
enum class GpuVendor {
    UNKNOWN,
    NVIDIA,
    AMD,
    INTEL,
    APPLE
};

/// Parse a vendor string (from OpenCL CL_DEVICE_VENDOR) into an enum.
GpuVendor parse_gpu_vendor(const std::string& vendor_string);

/// Initialize vendor-specific GPU monitoring libraries.
/// Call once at startup. Returns true if at least one backend was loaded.
bool gpu_monitor_init();

/// Shut down vendor-specific GPU monitoring.
void gpu_monitor_shutdown();

/// Query sensor data for the GPU at the given device index.
/// The index corresponds to the vendor-specific device ordering.
GpuSensorData gpu_query_sensors(GpuVendor vendor, int device_index);

}} // namespace occt::utils
