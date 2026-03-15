#pragma once

#include <QString>
#include <QVector>

namespace occt {

struct CpuInfoDetail {
    QString model;              // e.g. "Intel Core i9-14900K"
    int     physical_cores = 0;
    int     logical_cores  = 0;
    int     base_clock_mhz = 0;
    int     boost_clock_mhz = 0;
    int     l1_cache_kb    = 0;
    int     l2_cache_kb    = 0;
    int     l3_cache_kb    = 0;
    QString microarchitecture;  // e.g. "Raptor Lake", "Zen 4"
};

struct GpuInfoDetail {
    QString model;              // e.g. "NVIDIA GeForce RTX 4090"
    int     vram_mb        = 0;
    QString driver_version;
    bool    has_opencl      = false;
    bool    has_vulkan      = false;
};

struct RamInfoDetail {
    int     total_mb       = 0;
    int     speed_mhz      = 0;
    QString timing;             // e.g. "CL36-38-38-96" (best effort)
    int     slot_count     = 0;
};

struct StorageInfoDetail {
    QString model;
    int     capacity_gb    = 0;
    QString interface_type; // "NVMe", "SATA", "USB", "Unknown"
};

struct OsInfoDetail {
    QString name;               // e.g. "Windows 11 Pro"
    QString version;            // e.g. "23H2"
    QString build;              // e.g. "22631.3007"
    QString architecture;       // "x86_64", "arm64"
};

/// Aggregated system information.
struct SystemInfo {
    CpuInfoDetail              cpu;
    QVector<GpuInfoDetail>     gpus;
    RamInfoDetail              ram;
    QVector<StorageInfoDetail> storage;
    OsInfoDetail               os;
};

/// Collect system information.  This can be slow on first call (especially
/// for GPU and storage) so callers should invoke it once and cache.
SystemInfo collect_system_info();

} // namespace occt
