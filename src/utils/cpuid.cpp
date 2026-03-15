#include "cpuid.h"

#include <cstring>
#include <thread>

// x86 CPUID support - only available on x86/x86_64
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #define OCCT_X86 1
    #if defined(_MSC_VER)
        #include <intrin.h>
        #define CPUID(info, leaf) __cpuid(info, leaf)
        #define CPUIDEX(info, leaf, sub) __cpuidex(info, leaf, sub)
    #elif defined(__GNUC__) || defined(__clang__)
        static inline void cpuid_impl(int info[4], int leaf) {
            __asm__ __volatile__ (
                "cpuid"
                : "=a"(info[0]), "=b"(info[1]), "=c"(info[2]), "=d"(info[3])
                : "a"(leaf), "c"(0)
            );
        }
        static inline void cpuidex_impl(int info[4], int leaf, int sub) {
            __asm__ __volatile__ (
                "cpuid"
                : "=a"(info[0]), "=b"(info[1]), "=c"(info[2]), "=d"(info[3])
                : "a"(leaf), "c"(sub)
            );
        }
        #define CPUID(info, leaf) cpuid_impl(info, leaf)
        #define CPUIDEX(info, leaf, sub) cpuidex_impl(info, leaf, sub)
    #endif
#else
    #define OCCT_X86 0
#endif

#if defined(__APPLE__)
    #include <sys/sysctl.h>
#elif defined(__linux__)
    #include <fstream>
#endif

// Thread affinity headers for hybrid detection
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#elif defined(__linux__)
    #include <pthread.h>
    #include <sched.h>
#endif

namespace occt { namespace utils {

#if OCCT_X86

static std::string get_brand_string() {
    int info[4] = {};
    char brand[49] = {};

    CPUID(info, 0x80000000);
    unsigned int max_ext = static_cast<unsigned int>(info[0]);
    if (max_ext < 0x80000004u) {
        return "Unknown CPU";
    }

    CPUID(info, 0x80000002);
    std::memcpy(brand, info, 16);
    CPUID(info, 0x80000003);
    std::memcpy(brand + 16, info, 16);
    CPUID(info, 0x80000004);
    std::memcpy(brand + 32, info, 16);
    brand[48] = '\0';

    // Trim leading spaces
    std::string result(brand);
    size_t start = result.find_first_not_of(' ');
    if (start != std::string::npos) {
        result = result.substr(start);
    }
    return result;
}

static void detect_features(CpuInfo& info) {
    int regs[4] = {};

    // Leaf 1: basic features
    CPUID(regs, 1);
    info.has_sse42  = (regs[2] & (1 << 20)) != 0;
    info.has_avx    = (regs[2] & (1 << 28)) != 0;
    info.has_fma    = (regs[2] & (1 << 12)) != 0;

    // Leaf 7, sub 0: extended features
    CPUIDEX(regs, 7, 0);
    info.has_avx2    = (regs[1] & (1 <<  5)) != 0;
    info.has_avx512f = (regs[1] & (1 << 16)) != 0;
}

static void detect_cache(CpuInfo& info) {
    // Use deterministic cache parameters (leaf 4) for x86
    for (int sub = 0; sub < 16; ++sub) {
        int regs[4] = {};
        CPUIDEX(regs, 4, sub);

        int type = regs[0] & 0x1F;
        if (type == 0) break; // No more caches

        int level = (regs[0] >> 5) & 0x7;
        int line_size = (regs[1] & 0xFFF) + 1;
        int partitions = ((regs[1] >> 12) & 0x3FF) + 1;
        int ways = ((regs[1] >> 22) & 0x3FF) + 1;
        int sets = regs[2] + 1;

        int size_kb = (ways * partitions * line_size * sets) / 1024;

        if (level == 1 && (type == 1 || type == 3)) {
            info.l1_cache_kb = size_kb;
        } else if (level == 2) {
            info.l2_cache_kb = size_kb;
        } else if (level == 3) {
            info.l3_cache_kb = size_kb;
        }
    }
}

#else // ARM / non-x86

static std::string get_brand_string() {
#if defined(__APPLE__)
    char brand[256] = {};
    size_t size = sizeof(brand);
    if (sysctlbyname("machdep.cpu.brand_string", brand, &size, nullptr, 0) == 0) {
        return std::string(brand);
    }
#endif
    return "Unknown CPU";
}

static void detect_features(CpuInfo& info) {
    // AVX/SSE are x86-only instruction sets; not available on ARM
    info.has_sse42   = false;
    info.has_avx     = false;
    info.has_avx2    = false;
    info.has_avx512f = false;
    info.has_fma     = false;
}

static void detect_cache(CpuInfo& info) {
#if defined(__APPLE__)
    int64_t val = 0;
    size_t size = sizeof(val);
    if (sysctlbyname("hw.l1dcachesize", &val, &size, nullptr, 0) == 0) {
        info.l1_cache_kb = static_cast<int>(val / 1024);
    }
    if (sysctlbyname("hw.l2cachesize", &val, &size, nullptr, 0) == 0) {
        info.l2_cache_kb = static_cast<int>(val / 1024);
    }
    if (sysctlbyname("hw.l3cachesize", &val, &size, nullptr, 0) == 0) {
        info.l3_cache_kb = static_cast<int>(val / 1024);
    }
#endif
    (void)info;
}

#endif // OCCT_X86

static int get_physical_cores() {
#if defined(__APPLE__)
    int count = 0;
    size_t size = sizeof(count);
    if (sysctlbyname("hw.physicalcpu", &count, &size, nullptr, 0) == 0) {
        return count;
    }
#elif defined(__linux__)
    // Count unique physical core IDs
    std::ifstream f("/proc/cpuinfo");
    if (f.is_open()) {
        std::string line;
        int cores = 0;
        while (std::getline(f, line)) {
            if (line.find("cpu cores") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    cores = std::stoi(line.substr(pos + 1));
                    return cores;
                }
            }
        }
    }
#elif defined(_WIN32)
    // Use CPUID leaf 4 for x86 Windows
    #if OCCT_X86
    int regs[4] = {};
    CPUIDEX(regs, 4, 0);
    return ((regs[0] >> 26) & 0x3F) + 1;
    #endif
#endif
    return static_cast<int>(std::thread::hardware_concurrency());
}

// --- Hybrid core topology detection ---

#if OCCT_X86

CoreType detect_core_type(int core_id) {
    (void)core_id;
    // Check if CPUID leaf 0x1A is supported (Intel Hybrid Technology / Thread Director)
    int info[4] = {};
    CPUID(info, 0);
    unsigned int max_leaf = static_cast<unsigned int>(info[0]);
    if (max_leaf < 0x1A) {
        return CoreType::UNKNOWN;
    }

    // Note: On the current thread's core, CPUID leaf 0x1A returns the core type
    // of the core executing this instruction. For accurate per-core detection,
    // the caller must set thread affinity to the target core before calling.
    CPUID(info, 0x1A);
    unsigned int core_type_id = (static_cast<unsigned int>(info[0]) >> 24) & 0xFF;

    // Intel core type encoding:
    // 0x20 = Intel Atom (Efficiency core)
    // 0x40 = Intel Core (Performance core)
    if (core_type_id == 0x40) {
        return CoreType::PERFORMANCE;
    } else if (core_type_id == 0x20) {
        return CoreType::EFFICIENCY;
    }

    return CoreType::UNKNOWN;
}

static void detect_hybrid_topology(CpuInfo& info) {
    int logical = info.logical_cores;
    if (logical <= 0) return;

    info.core_types.resize(logical, CoreType::UNKNOWN);

    // Check if this is an Intel CPU with hybrid support (leaf 7, EDX bit 15)
    int regs[4] = {};
    CPUIDEX(regs, 7, 0);
    bool has_hybrid = (regs[3] & (1 << 15)) != 0;

    if (!has_hybrid) {
        // No hybrid architecture - all cores are the same type
        return;
    }

    // Detect core type per logical core using thread affinity + CPUID 0x1A
    int p_count = 0;
    int e_count = 0;

#if defined(_WIN32)
    for (int i = 0; i < logical; ++i) {
        HANDLE thread = GetCurrentThread();
        DWORD_PTR old_mask = SetThreadAffinityMask(thread, 1ULL << i);
        if (old_mask == 0) {
            info.core_types[i] = CoreType::UNKNOWN;
            continue;
        }
        // Force reschedule to target core
        SwitchToThread();

        CoreType ct = detect_core_type(i);
        info.core_types[i] = ct;
        if (ct == CoreType::PERFORMANCE) p_count++;
        else if (ct == CoreType::EFFICIENCY) e_count++;

        // Restore original affinity
        SetThreadAffinityMask(thread, old_mask);
    }
#elif defined(__linux__)
    cpu_set_t original_set;
    CPU_ZERO(&original_set);
    pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &original_set);

    for (int i = 0; i < logical; ++i) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i, &cpuset);
        if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
            info.core_types[i] = CoreType::UNKNOWN;
            continue;
        }
        // Yield to ensure we're on the target core
        sched_yield();

        CoreType ct = detect_core_type(i);
        info.core_types[i] = ct;
        if (ct == CoreType::PERFORMANCE) p_count++;
        else if (ct == CoreType::EFFICIENCY) e_count++;
    }

    // Restore original affinity
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &original_set);
#else
    // macOS x86 with hybrid (unlikely but handle gracefully)
    for (int i = 0; i < logical; ++i) {
        info.core_types[i] = CoreType::UNKNOWN;
    }
#endif

    info.p_cores = p_count;
    info.e_cores = e_count;
    info.is_hybrid = (p_count > 0 && e_count > 0);
}

#else // ARM / non-x86

CoreType detect_core_type(int core_id) {
    (void)core_id;
#if defined(__APPLE__)
    // On Apple Silicon, we cannot query per-core type via a simple call.
    // Use detect_hybrid_topology() for aggregate counts.
    int p_cores = 0;
    size_t size = sizeof(p_cores);
    if (sysctlbyname("hw.perflevel0.physicalcpu", &p_cores, &size, nullptr, 0) == 0) {
        if (core_id < p_cores) {
            return CoreType::PERFORMANCE;
        }
        return CoreType::EFFICIENCY;
    }
#endif
    return CoreType::UNKNOWN;
}

static void detect_hybrid_topology(CpuInfo& info) {
#if defined(__APPLE__)
    int p_cores = 0, e_cores = 0;
    size_t size = sizeof(int);

    // Apple Silicon: perflevel0 = Performance, perflevel1 = Efficiency
    if (sysctlbyname("hw.perflevel0.physicalcpu", &p_cores, &size, nullptr, 0) == 0) {
        info.p_cores = p_cores;
    }
    size = sizeof(int);
    if (sysctlbyname("hw.perflevel1.physicalcpu", &e_cores, &size, nullptr, 0) == 0) {
        info.e_cores = e_cores;
    }

    info.is_hybrid = (info.p_cores > 0 && info.e_cores > 0);

    // Build per-core type mapping
    // Apple Silicon typically schedules P-cores as the first logical cores
    int logical = info.logical_cores;
    info.core_types.resize(logical, CoreType::UNKNOWN);

    if (info.is_hybrid) {
        // P-cores first, then E-cores
        // Note: logical cores per P-core may differ from E-core (e.g., M1 Pro has
        // no SMT, so logical == physical for both). We use physical counts as
        // the boundary since Apple Silicon doesn't have SMT.
        for (int i = 0; i < logical; ++i) {
            if (i < info.p_cores) {
                info.core_types[i] = CoreType::PERFORMANCE;
            } else if (i < info.p_cores + info.e_cores) {
                info.core_types[i] = CoreType::EFFICIENCY;
            }
        }
    }
#else
    // Non-Apple ARM or other: no hybrid detection available
    int logical = info.logical_cores;
    info.core_types.resize(logical, CoreType::UNKNOWN);
    (void)info;
#endif
}

#endif // OCCT_X86

CpuInfo detect_cpu() {
    CpuInfo info;

    info.brand = get_brand_string();
    info.logical_cores = static_cast<int>(std::thread::hardware_concurrency());
    info.physical_cores = get_physical_cores();

    if (info.physical_cores <= 0) {
        info.physical_cores = info.logical_cores;
    }

    detect_features(info);
    detect_cache(info);
    detect_hybrid_topology(info);

    return info;
}

}} // namespace occt::utils
