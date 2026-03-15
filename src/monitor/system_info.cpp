#include "system_info.h"

#include <QSysInfo>

#include <array>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#if defined(__x86_64__) || defined(__i386__)
#ifndef _MSC_VER
// Inline asm wrappers matching MSVC intrinsic signatures
static inline void __cpuid(int info[4], int leaf) {
    __asm__ __volatile__ (
        "cpuid"
        : "=a"(info[0]), "=b"(info[1]), "=c"(info[2]), "=d"(info[3])
        : "a"(leaf), "c"(0)
    );
}
static inline void __cpuidex(int info[4], int leaf, int sub) {
    __asm__ __volatile__ (
        "cpuid"
        : "=a"(info[0]), "=b"(info[1]), "=c"(info[2]), "=d"(info[3])
        : "a"(leaf), "c"(sub)
    );
}
#endif
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#elif defined(__linux__)
#include <fstream>
#include <sstream>
#include <unistd.h>
#endif

namespace occt {

// ─── CPUID leaf and mask constants ──────────────────────────────────────────

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
static constexpr uint32_t CPUID_EXTENDED_MAX       = 0x80000000;
static constexpr uint32_t CPUID_BRAND_STRING_START = 0x80000002;
static constexpr uint32_t CPUID_BRAND_STRING_END   = 0x80000004;
static constexpr int      CPUID_BRAND_BUF_SIZE     = 49;  // 3 * 16 bytes + null

// CPUID leaf 4 cache descriptor bit masks
static constexpr int CACHE_TYPE_MASK       = 0x1F;   // bits [4:0] of EAX
static constexpr int CACHE_LEVEL_SHIFT     = 5;
static constexpr int CACHE_LEVEL_MASK      = 0x7;    // bits [7:5] of EAX
static constexpr int CACHE_WAYS_SHIFT      = 22;
static constexpr int CACHE_WAYS_MASK       = 0x3FF;  // bits [31:22] of EBX
static constexpr int CACHE_PARTITIONS_SHIFT = 12;
static constexpr int CACHE_PARTITIONS_MASK = 0x3FF;  // bits [21:12] of EBX
static constexpr int CACHE_LINE_SIZE_MASK  = 0xFFF;  // bits [11:0] of EBX
#endif

// ─── CPU detection (platform-independent CPUID + OS-specific) ───────────────

static CpuInfoDetail detect_cpu_info() {
    CpuInfoDetail info;

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    // Use CPUID for brand string
    std::array<int, 4> cpui = {};
    char brand[CPUID_BRAND_BUF_SIZE] = {};

    __cpuid(cpui.data(), static_cast<int>(CPUID_EXTENDED_MAX));
    unsigned max_ext = static_cast<unsigned>(cpui[0]);

    if (max_ext >= CPUID_BRAND_STRING_END) {
        __cpuid(cpui.data(), static_cast<int>(CPUID_BRAND_STRING_START));
        std::memcpy(brand, cpui.data(), 16);
        __cpuid(cpui.data(), static_cast<int>(CPUID_BRAND_STRING_START + 1));
        std::memcpy(brand + 16, cpui.data(), 16);
        __cpuid(cpui.data(), static_cast<int>(CPUID_BRAND_STRING_END));
        std::memcpy(brand + 32, cpui.data(), 16);
        brand[48] = '\0';
        info.model = QString::fromLatin1(brand).trimmed();
    }

    // Cache info from CPUID leaf 4 (Intel) or leaf 0x8000001D (AMD)
    __cpuid(cpui.data(), 0);
    int max_leaf = cpui[0];
    if (max_leaf >= 4) {
        for (int sub = 0; sub < 16; ++sub) {
            __cpuidex(cpui.data(), 4, sub);
            int type = cpui[0] & CACHE_TYPE_MASK;
            if (type == 0) break;
            int ways   = ((cpui[1] >> CACHE_WAYS_SHIFT) & CACHE_WAYS_MASK) + 1;
            int parts  = ((cpui[1] >> CACHE_PARTITIONS_SHIFT) & CACHE_PARTITIONS_MASK) + 1;
            int line   = (cpui[1] & CACHE_LINE_SIZE_MASK) + 1;
            int sets   = cpui[2] + 1;
            int size_kb = (ways * parts * line * sets) / 1024;

            int level = (cpui[0] >> CACHE_LEVEL_SHIFT) & CACHE_LEVEL_MASK;
            if (level == 1)      info.l1_cache_kb += size_kb;
            else if (level == 2) info.l2_cache_kb += size_kb;
            else if (level == 3) info.l3_cache_kb += size_kb;
        }
    }
#endif

    // Core counts (OS-specific)
#ifdef _WIN32
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    info.logical_cores = static_cast<int>(si.dwNumberOfProcessors);

    DWORD buf_len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &buf_len);
    if (buf_len > 0) {
        std::vector<uint8_t> buf(buf_len);
        if (GetLogicalProcessorInformationEx(
                RelationProcessorCore,
                reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()),
                &buf_len)) {
            int cores = 0;
            DWORD offset = 0;
            while (offset < buf_len) {
                auto* p = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data() + offset);
                if (p->Relationship == RelationProcessorCore) ++cores;
                offset += p->Size;
            }
            info.physical_cores = cores;
        }
    }
    if (info.physical_cores == 0) info.physical_cores = info.logical_cores;

#elif defined(__APPLE__)
    int ncpu = 0;
    size_t len = sizeof(ncpu);
    sysctlbyname("hw.physicalcpu", &ncpu, &len, nullptr, 0);
    info.physical_cores = ncpu;
    sysctlbyname("hw.logicalcpu", &ncpu, &len, nullptr, 0);
    info.logical_cores = ncpu;

    if (info.model.isEmpty()) {
        char buf[256] = {};
        size_t blen = sizeof(buf);
        sysctlbyname("machdep.cpu.brand_string", buf, &blen, nullptr, 0);
        info.model = QString::fromLatin1(buf).trimmed();
    }

#elif defined(__linux__)
    info.logical_cores = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    // Count physical cores from /proc/cpuinfo
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    std::set<std::string> core_ids;
    while (std::getline(cpuinfo, line)) {
        if (info.model.isEmpty() && line.find("model name") == 0) {
            auto pos = line.find(':');
            if (pos != std::string::npos)
                info.model = QString::fromStdString(line.substr(pos + 2));
        }
        if (line.find("core id") == 0) {
            core_ids.insert(line);
        }
    }
    info.physical_cores = core_ids.empty() ? info.logical_cores : static_cast<int>(core_ids.size());
#endif

    return info;
}

// ─── RAM detection ──────────────────────────────────────────────────────────

static RamInfoDetail detect_ram_info() {
    RamInfoDetail info;

#ifdef _WIN32
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        info.total_mb = static_cast<int>(mem.ullTotalPhys / (1024 * 1024));
    }
#elif defined(__APPLE__)
    int64_t mem = 0;
    size_t len = sizeof(mem);
    sysctlbyname("hw.memsize", &mem, &len, nullptr, 0);
    info.total_mb = static_cast<int>(mem / (1024 * 1024));
#elif defined(__linux__)
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page_size > 0) {
        info.total_mb = static_cast<int>((static_cast<int64_t>(pages) * page_size) / (1024 * 1024));
    }
#endif

    return info;
}

// ─── OS detection ───────────────────────────────────────────────────────────

static OsInfoDetail detect_os_info() {
    OsInfoDetail info;
    info.name = QSysInfo::prettyProductName();
    info.version = QSysInfo::productVersion();
    info.architecture = QSysInfo::currentCpuArchitecture();

#ifdef _WIN32
    // Build number from registry or RtlGetVersion
    OSVERSIONINFOW ovi{};
    ovi.dwOSVersionInfoSize = sizeof(ovi);
    // Use RtlGetVersion to bypass compat shims
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
        if (fn && fn(reinterpret_cast<PRTL_OSVERSIONINFOW>(&ovi)) == 0) {
            info.build = QString::number(ovi.dwBuildNumber);
        }
    }
#elif defined(__APPLE__)
    info.build = QSysInfo::kernelVersion();
#elif defined(__linux__)
    info.build = QSysInfo::kernelVersion();
#endif

    return info;
}

// ─── Public API ─────────────────────────────────────────────────────────────

SystemInfo collect_system_info() {
    SystemInfo si;
    si.cpu = detect_cpu_info();
    si.ram = detect_ram_info();
    si.os  = detect_os_info();

    // GPU and storage detection is limited without vendor SDKs.
    // The GPU info is typically populated via OpenCL/NVML at a higher level.
    // Provide a placeholder GPU entry from OS info.
    GpuInfoDetail gpu;
#ifdef __APPLE__
    // Apple Silicon integrated GPU
    char buf[256] = {};
    size_t len = sizeof(buf);
    if (sysctlbyname("machdep.cpu.brand_string", buf, &len, nullptr, 0) == 0) {
        if (std::string(buf).find("Apple") != std::string::npos) {
            gpu.model = QString::fromLatin1(buf).trimmed() + " (Integrated GPU)";
        }
    }
#endif
    if (!gpu.model.isEmpty()) {
        si.gpus.append(gpu);
    }

    return si;
}

} // namespace occt
