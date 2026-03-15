# Real OCCT (OCBASE) vs occt-native -- Detailed Feature Comparison

> Generated: 2026-03-14
> Real OCCT version reference: v15.x (2024-2025 feature set)
> occt-native: current codebase analysis

---

## Overall Summary

| Category | Real OCCT | occt-native | Coverage |
|----------|-----------|-------------|----------|
| A. CPU Test | 10/10 | 8/10 | **80%** |
| B. GPU Test | 10/10 | 7/10 | **70%** |
| C. Memory (RAM) Test | 10/10 | 8/10 | **80%** |
| D. PSU Test | 10/10 | 8/10 | **80%** |
| E. Storage / VRAM Test | 9/10 | 8/10 | **85%** |
| F. Monitoring | 10/10 | 5/10 | **50%** |
| G. Safety / Protection | 10/10 | 8/10 | **80%** |
| H. Reporting | 10/10 | 8/10 | **80%** |
| I. GUI Features | 10/10 | 6/10 | **60%** |
| J. Scheduling / Automation | 9/10 | 8/10 | **85%** |
| K. Certification System | 10/10 | 7/10 | **70%** |
| L. Settings / Configuration | 9/10 | 6/10 | **65%** |
| M. Licensing / Distribution | N/A | N/A | N/A |
| **Overall** | | | **~73%** |

---

## A. CPU Test

### A1. Test Modes / Instruction Sets

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| SSE floating point | Yes | `SSE_FLOAT` mode | :white_check_mark: Implemented and equivalent |
| AVX (pure, no FMA) | Yes (separate from AVX2) | Not separate; closest is AVX2_FMA | :warning: AVX and AVX2 are merged into AVX2_FMA, no pure AVX-only mode |
| AVX2 / FMA | Yes | `AVX2_FMA` mode | :white_check_mark: Implemented and equivalent |
| AVX-512 | Yes | `AVX512_FMA` mode | :white_check_mark: Implemented and equivalent |
| Linpack (DGEMM) | Yes (multiple revisions: AMD64, 2012, Large) | `LINPACK` mode (tiled naive DGEMM + residual verification) | :warning: Implemented but uses naive DGEMM, not OpenBLAS/MKL-accelerated |
| NEON (ARM) | No (Windows/Linux x86 only) | Yes (Apple Silicon) | :white_check_mark: occt-native is superior here |
| Auto instruction set selection | Yes (detects best ISA automatically) | CPUID detection exists but no `AUTO` enum for CpuStressMode | :warning: Detection infrastructure present, but no auto-select mode enum |
| Prime number test | No (not in real OCCT) | `PRIME` mode (Miller-Rabin + Lucas-Lehmer) | :white_check_mark: Additional feature in occt-native |
| Cache-only (small data set) stress | Yes (Small data set) | `CACHE_ONLY` mode | :white_check_mark: Implemented and equivalent |
| Large data set (memory bus stress) | Yes | `LARGE_DATA_SET` mode | :white_check_mark: Implemented and equivalent |
| Medium data set | Yes | Not present as separate mode | :x: Not implemented |
| ALL (mixed workload) | Yes | `ALL` mode | :white_check_mark: Implemented and equivalent |

**Missing: Pure AVX mode (without AVX2/FMA)**
- Priority: Low
- Difficulty: Easy
- Suggestion: Add `AVX_FLOAT` mode to `CpuStressMode` enum that uses only 256-bit AVX instructions without FMA3.

**Missing: Medium data set**
- Priority: Low
- Difficulty: Easy
- Suggestion: Add `MEDIUM_DATA_SET` to `CpuStressMode` with a buffer size of ~4-8 MB (L3-fitting).

**Missing: Fully automatic ISA selection mode**
- Priority: Medium
- Difficulty: Easy
- Suggestion: Add `AUTO` to `CpuStressMode` that uses CPUID (already in `src/utils/cpuid.h`) to pick the highest supported ISA.

### A2. Data Set Sizes

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Small (fits in L1/L2 cache) | Yes | `CACHE_ONLY` | :white_check_mark: Implemented and equivalent |
| Medium (fits in L3 cache) | Yes | Not present | :x: Not implemented |
| Large (exceeds all caches) | Yes | `LARGE_DATA_SET` | :white_check_mark: Implemented and equivalent |

### A3. Thread Control

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Auto (all logical cores) | Yes | `num_threads = 0` auto-detects | :white_check_mark: Implemented and equivalent |
| User-specified thread count | Yes | `num_threads` parameter | :white_check_mark: Implemented and equivalent |
| Physical-only threads option | Yes (Linpack mode) | Not explicitly exposed | :warning: Auto detects logical cores but no "physical only" toggle |
| Core Cycling (one core at a time) | Yes | `LoadPattern::CORE_CYCLING` (rotates every 150ms) | :white_check_mark: Implemented and equivalent |
| Per-core thread affinity | Yes | `set_thread_affinity()` implemented (Win/Linux/macOS) | :white_check_mark: Implemented and equivalent |
| P-core/E-core identification | Yes | `per_core_type` ("P-core", "E-core", "Unknown") | :white_check_mark: Implemented and equivalent |

### A4. Error Detection Method

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Computational verification (result vs expected) | Yes (two-phase: compute + verify) | `ErrorVerifier` with deterministic FMA chain, bit-exact comparison | :white_check_mark: Implemented and equivalent |
| Per-core error tracking | Yes | `core_has_error`, `per_core_error_count` vectors | :white_check_mark: Implemented and equivalent |
| Per-core error log files (ThreadX.txt) | Yes | Errors stored in memory only | :warning: Implemented but incomplete -- no file output |
| Bit-flip location identification | Unclear | XOR of expected vs actual to identify flipped bits | :white_check_mark: Implemented and equivalent |
| Error summary string | Yes | `error_summary()` method | :white_check_mark: Implemented and equivalent |

**Missing: Per-core error log files (ThreadX.txt)**
- Priority: Medium
- Difficulty: Easy
- Suggestion: In `CpuEngine::worker_thread()`, open a per-thread log file and append each `CpuError` with timestamp, expected, actual, bit-mask.

### A5. Load Patterns

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Steady (constant load) | Yes | `LoadPattern::STEADY` | :white_check_mark: Implemented and equivalent |
| Variable (change operands every 10 min) | Yes | `LoadPattern::VARIABLE` | :white_check_mark: Implemented and equivalent |
| Core Cycling | Yes | `LoadPattern::CORE_CYCLING` | :white_check_mark: Implemented and equivalent |

### A6. Intensity Modes

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Normal mode (more verification, less stress) | Yes | `CpuIntensityMode::NORMAL` (verify every batch) | :white_check_mark: Implemented and equivalent |
| Extreme mode (max stress, less verification) | Yes | `CpuIntensityMode::EXTREME` (verify every 10th batch) | :white_check_mark: Implemented and equivalent |

### A7. CPU Benchmarks

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| SSE Single/Multi benchmark | Yes | GFLOPS reported in CpuMetrics | :warning: Reports GFLOPS but no dedicated benchmark mode with standardized scoring |
| AVX Single/Multi benchmark | Yes | Same as above | :warning: Same |
| Cache/Memory latency benchmark | Yes (new in v14/v15) | `CacheBenchmark` class (L1/L2/L3/DRAM latency + bandwidth) | :white_check_mark: Implemented and equivalent |
| Online leaderboard upload | Yes | `Leaderboard` class (local file-based only) | :warning: Local-only, no online submission |

**Missing: Dedicated CPU benchmark mode with standardized scoring**
- Priority: Medium
- Difficulty: Medium
- Suggestion: Add a `--benchmark cpu` CLI mode that runs fixed-duration SSE/AVX/AVX2 benchmarks and outputs standardized, comparable scores.

---

## B. GPU Test

### B1. 3D Rendering Stress

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| 3D Standard (custom rendering engine) | Yes (Vulkan-based) | `VULKAN_3D` mode (offscreen Vulkan rendering) | :white_check_mark: Implemented and equivalent |
| Shader complexity levels | Yes (adjustable) | 5 levels via `set_shader_complexity(1-5)` | :white_check_mark: Implemented and equivalent |
| 3D Adaptive (Unreal Engine based) | Yes (Unreal Engine) | `VULKAN_ADAPTIVE` mode (custom, not Unreal) | :warning: Different approach -- custom adaptive load instead of Unreal Engine |
| DirectX 3D Standard | Yes | Not implemented (Vulkan-only) | :x: Not implemented |
| Resolution / fullscreen settings | Yes | Not implemented | :x: Not implemented |
| FPS limit setting | Yes | Not implemented | :x: Not implemented |
| GPU usage limit (%) | Yes | Not implemented | :x: Not implemented |

**Missing: DirectX 3D Standard**
- Priority: Low (conflicts with cross-platform goal)
- Difficulty: Hard
- Suggestion: Vulkan covers the same use case cross-platform. DirectX only needed for Windows-specific parity.

**Missing: Resolution/fullscreen settings**
- Priority: Medium
- Difficulty: Medium
- Suggestion: Add `set_resolution(width, height)` and `set_fullscreen(bool)` to `GpuEngine`. For offscreen rendering, this controls the render target size.

**Missing: FPS limit**
- Priority: Low
- Difficulty: Easy
- Suggestion: Add frame-rate cap via `set_fps_limit(int fps)` using a simple frame-time sleep.

**Missing: GPU usage limit**
- Priority: Low
- Difficulty: Medium
- Suggestion: Implement a duty-cycle approach: run for X ms, sleep for Y ms to achieve target utilization percentage.

### B2. VRAM Test

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| VRAM allocation (95%+) | Yes (CUDA Memtest based) | `VRAM_TEST` mode | :white_check_mark: Implemented and equivalent |
| Walking ones/zeros patterns | Yes | Yes (in OpenCL VRAM test kernel) | :white_check_mark: Implemented and equivalent |
| Bit error detection | Yes | `vram_errors` in GpuMetrics | :white_check_mark: Implemented and equivalent |
| Address-based testing | Yes | Yes | :white_check_mark: Implemented and equivalent |
| CUDA-based VRAM test | Yes | No (OpenCL only) | :warning: OpenCL covers NVIDIA via ICD, but no native CUDA path |

### B3. Compute Test

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| OpenCL compute workloads | Yes (Enterprise+) | FP32/FP64 matrix mul, FMA, Trig | :white_check_mark: Implemented and equivalent |
| CUDA-specific compute | Yes | Not implemented (OpenCL only) | :warning: OpenCL works on NVIDIA GPUs via ICD driver, but no native CUDA |

### B4. Artifact Detection

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Automatic artifact detection | Yes | `ArtifactDetector` (pixel comparison with reference frame) | :white_check_mark: Implemented and equivalent |
| Artifact classification | Yes | `ArtifactType::SINGLE_PIXEL`, `BLOCK`, `FULL_FRAME` | :white_check_mark: Implemented and equivalent |
| Artifact severity levels | Yes | `ArtifactSeverity::NONE/LOW/MEDIUM/HIGH/CRITICAL` | :white_check_mark: Implemented and equivalent |
| Artifact location tracking | Yes | `ArtifactLocation` with x, y, width, height | :white_check_mark: Implemented and equivalent |
| Cumulative statistics | Yes | `total_artifacts_detected()`, `total_frames_compared()` | :white_check_mark: Implemented and equivalent |

### B5. Multi-GPU Support

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Parallel multi-GPU testing | Yes (different brands simultaneously) | `MultiGpuManager` class | :white_check_mark: Implemented and equivalent |
| Display-less GPU testing | Yes | Yes (offscreen Vulkan rendering) | :white_check_mark: Implemented and equivalent |
| Per-GPU metrics | Yes | `GpuMetricsEntry` per GPU | :white_check_mark: Implemented and equivalent |

### B6. Adaptive Load / Coil Whine

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Variable adaptive (+5%/20s) | Yes | `AdaptiveMode::VARIABLE` | :white_check_mark: Implemented and equivalent |
| Switch adaptive (20%<->80%) | Yes | `AdaptiveMode::SWITCH` + configurable interval | :white_check_mark: Implemented and equivalent |
| Coil whine detection | Yes (v15 experimental) | `AdaptiveMode::COIL_WHINE` (square wave oscillation, configurable Hz) | :white_check_mark: Implemented and equivalent |
| Coil whine frequency sweep | Yes | `set_coil_whine_freq(0)` = sweep mode | :white_check_mark: Implemented and equivalent |

---

## C. Memory (RAM) Test

### C1. Test Algorithms

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| March C- algorithm | Yes | `RamPattern::MARCH_C_MINUS` | :white_check_mark: Implemented and equivalent |
| Walking Ones | Yes | `RamPattern::WALKING_ONES` | :white_check_mark: Implemented and equivalent |
| Walking Zeros | Yes | `RamPattern::WALKING_ZEROS` | :white_check_mark: Implemented and equivalent |
| Checkerboard (0xAA/0x55) | Yes | `RamPattern::CHECKERBOARD` | :white_check_mark: Implemented and equivalent |
| Random pattern | Yes | `RamPattern::RANDOM` (xoshiro256**) | :white_check_mark: Implemented and equivalent |
| Bandwidth measurement | Yes | `RamPattern::BANDWIDTH` (streaming stores) | :white_check_mark: Implemented and equivalent |
| "All" meta-pattern (run all tests) | Yes | Not present as single option | :x: Not implemented |
| Moving Inversions | Yes (Memtest86 style) | Not implemented | :x: Not implemented |
| Block Move | Yes | Not implemented | :x: Not implemented |
| Bit Fade test | Yes (data retention) | Not implemented | :x: Not implemented |

**Missing: "All" meta-pattern**
- Priority: Low
- Difficulty: Easy
- Suggestion: Add `RamPattern::ALL` that runs each pattern sequentially on the allocated buffer.

**Missing: Moving Inversions**
- Priority: Low
- Difficulty: Easy
- Suggestion: Implement Memtest86-style moving inversions pattern -- write ascending address pattern, then verify in reverse order.

**Missing: Block Move**
- Priority: Low
- Difficulty: Easy
- Suggestion: Implement DMA-style block move test using `memmove()` on overlapping regions and verify integrity.

**Missing: Bit Fade test**
- Priority: Low
- Difficulty: Easy
- Suggestion: Write a pattern, wait N seconds (configurable), then verify. Tests data retention under various conditions.

### C2. Memory Region Selection

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Configurable memory percentage (70-95%) | Yes | `memory_pct` parameter (0.0-1.0), CLI `--memory-percent` | :white_check_mark: Implemented and equivalent |
| Page locking (prevent swap) | Yes | `VirtualLock`/`mlock` on Win/Linux/macOS | :white_check_mark: Implemented and equivalent |
| Lock failure indication | Unclear | `pages_locked` flag in RamMetrics | :white_check_mark: Implemented and equivalent |

### C3. Instruction Set for RAM Test

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| SSE memory test | Yes (selectable) | Auto-only (no manual selection) | :warning: Uses best available ISA automatically, no manual override |
| AVX memory test | Yes (selectable) | Auto-only | :warning: Same |
| AVX2 memory test | Yes (selectable) | Auto-only | :warning: Same |

**Missing: Instruction set selection for RAM test**
- Priority: Low
- Difficulty: Easy
- Suggestion: Add an `InstructionSet` enum parameter to `RamEngine::start()` to force SSE/AVX/AVX2.

### C4. Error Reporting

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Error count | Yes | `errors_found` in RamMetrics | :white_check_mark: Implemented and equivalent |
| Error address reporting | Yes | `MemoryError::address` | :white_check_mark: Implemented and equivalent |
| Expected vs actual value | Yes | `MemoryError::expected` and `MemoryError::actual` | :white_check_mark: Implemented and equivalent |
| Timestamp per error | Yes | `MemoryError::timestamp_secs` | :white_check_mark: Implemented and equivalent |
| Error log cap | Unclear | Capped at 1000 entries | :white_check_mark: Implemented and equivalent |

### C5. Multi-Pass Support

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Multi-pass testing | Yes | `passes` parameter in `start()`, CLI `--passes` | :white_check_mark: Implemented and equivalent |
| Progress tracking | Yes | `progress_pct` in RamMetrics | :white_check_mark: Implemented and equivalent |

---

## D. Power Supply (PSU) Test

### D1. Combined CPU+GPU Load

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| CPU + GPU simultaneous stress | Yes | `PsuEngine` combines `CpuEngine` (Linpack) + `GpuEngine` | :white_check_mark: Implemented and equivalent |
| All-GPU option | Yes | `set_use_all_gpus(bool)` | :white_check_mark: Implemented and equivalent |

### D2. Load Patterns

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Steady (constant max load) | Yes | `PsuLoadPattern::STEADY` | :white_check_mark: Implemented and equivalent |
| Spike (5s idle -> 5s max, repeat) | Yes | `PsuLoadPattern::SPIKE` | :white_check_mark: Implemented and equivalent |
| Ramp (0% -> 100% gradual) | Yes | `PsuLoadPattern::RAMP` | :white_check_mark: Implemented and equivalent |

### D3. Power Measurement

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Total system power | Yes | `PsuMetrics::total_power_watts` | :white_check_mark: Implemented and equivalent |
| CPU power breakdown | Yes | `PsuMetrics::cpu_power_watts` | :white_check_mark: Implemented and equivalent |
| GPU power breakdown | Yes | `PsuMetrics::gpu_power_watts` | :white_check_mark: Implemented and equivalent |
| CPU/GPU error tracking during PSU test | Yes | `PsuMetrics::errors_cpu` and `errors_gpu` | :white_check_mark: Implemented and equivalent |
| Voltage stability curves (+12V/+5V/+3.3V) | Yes | Not implemented | :x: Not implemented |
| PSU protection mode detection (OCP/OPP) | Yes | No explicit detection logic | :warning: Implicit via sudden stop detection only |

**Missing: Voltage stability curves**
- Priority: Medium
- Difficulty: Hard
- Suggestion: Requires reading 12V/5V/3.3V rail voltages from motherboard sensors. Needs deeper sensor integration (LHM or direct SMBus). Display as time-series chart during PSU test.

**Missing: PSU protection mode detection**
- Priority: Low
- Difficulty: Medium
- Suggestion: If GPU/CPU connectivity is suddenly lost during PSU test, log as potential PSU protection trip. Detect via loss of sensor readings or engine crash.

---

## E. Storage / VRAM Test

### E1. Storage Test (OCCT v15 stable)

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Sequential read/write | Yes | `SEQ_READ`, `SEQ_WRITE` | :white_check_mark: Implemented and equivalent |
| Random 4K read/write | Yes | `RAND_READ`, `RAND_WRITE` | :white_check_mark: Implemented and equivalent |
| Mixed I/O | Yes | `MIXED` (70% read / 30% write) | :white_check_mark: Implemented and equivalent |
| Data verification (write-then-verify) | Yes | `VERIFY_SEQ`, `VERIFY_RAND`, `FILL_VERIFY`, `BUTTERFLY` | :white_check_mark: occt-native has more verification modes |
| Direct I/O (bypass OS cache) | Yes | `set_direct_io(bool)` -- O_DIRECT/F_NOCACHE/FILE_FLAG_NO_BUFFERING | :white_check_mark: Implemented and equivalent |
| Configurable queue depth | Yes | `queue_depth` parameter | :white_check_mark: Implemented and equivalent |
| Configurable block size | Yes | `set_block_size_kb()` (4, 8, 64, 128, 1024, 4096) | :white_check_mark: Implemented and equivalent |
| CrystalDiskMark-style benchmark | Yes | `run_benchmark()` method (8 standard tests) | :white_check_mark: Implemented and equivalent |
| CRC32C error detection | Yes | `crc_errors` and `pattern_errors` in StorageMetrics | :white_check_mark: Implemented and equivalent |
| H2testw-style fill+verify | Yes | `FILL_VERIFY` mode | :white_check_mark: Implemented and equivalent |
| Butterfly verification | No | `BUTTERFLY` mode (converging from both ends) | :white_check_mark: Additional feature in occt-native |
| SSD temperature monitoring | Yes (via HWInfo) | Not implemented | :x: Not implemented |
| Online comparison database | Yes (building) | Not implemented | :x: Not implemented |
| Test file logging | Yes | `log()` method with `log_file_` | :white_check_mark: Implemented and equivalent |

**Missing: SSD temperature monitoring**
- Priority: Medium
- Difficulty: Medium
- Suggestion: Read SMART data via nvme-cli (Linux), smartmontools (cross-platform), or WMI MSFT_Disk (Windows).

**Missing: Online comparison database**
- Priority: Low
- Difficulty: Hard
- Suggestion: Requires web backend. Defer until online infrastructure exists.

### E2. VRAM Test (separate from Storage in real OCCT)

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Dedicated VRAM test tab | Yes (separate from GPU 3D) | `GpuStressMode::VRAM_TEST` (within GPU engine) | :warning: Implemented as GPU sub-mode, not a separate tab/engine |
| VRAM pattern testing | Yes (CUDA Memtest based) | Walking ones/zeros, address test | :white_check_mark: Implemented and equivalent |
| VRAM error count | Yes | `GpuMetrics::vram_errors` | :white_check_mark: Implemented and equivalent |

---

## F. Monitoring

### F1. CPU Monitoring

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| CPU temperature | Yes | `get_cpu_temperature()` | :white_check_mark: Implemented and equivalent |
| CPU voltage (Vcore) | Yes | Via WMI/sysfs if available | :warning: Limited -- only when WMI/LHM provides it |
| CPU power consumption | Yes | `get_cpu_power()` (RAPL or TDP estimate fallback) | :warning: Real reading via LHM/RAPL, fallback is estimated |
| CPU power estimated flag | No | `is_cpu_power_estimated()` | :white_check_mark: Transparency feature in occt-native |
| CPU frequency (per-core) | Yes | PDH-based on Windows only | :warning: Aggregate only, not per-core on all platforms |
| CPU usage (per-core) | Yes | `per_core_usage` in CpuMetrics | :white_check_mark: Implemented and equivalent |
| P-core / E-core identification | Yes | `per_core_type` | :white_check_mark: Implemented and equivalent |

### F2. GPU Monitoring

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| GPU temperature | Yes | `get_gpu_temperature()` via NVML | :white_check_mark: Implemented and equivalent |
| GPU usage % | Yes | `gpu_usage_pct` in GpuMetrics | :white_check_mark: Implemented and equivalent |
| VRAM usage % | Yes | `vram_usage_pct` in GpuMetrics | :white_check_mark: Implemented and equivalent |
| GPU power | Yes | `power_watts` in GpuMetrics | :white_check_mark: Implemented and equivalent |
| GPU clock speed | Yes | Not explicitly tracked | :x: Not implemented |
| AMD GPU monitoring (ADL) | Yes | ADL library load exists but is a **STUB** | :x: Not implemented (stub only) |

**Missing: GPU clock speed monitoring**
- Priority: Medium
- Difficulty: Easy
- Suggestion: NVML provides `nvmlDeviceGetClockInfo()`. Add `clock_mhz` to `GpuMetrics` and poll it.

**Missing: AMD ADL sensor data**
- Priority: High
- Difficulty: Medium
- Suggestion: Complete ADL implementation. Call `ADL2_Overdrive_Temperature_Get`, `ADL2_Overdrive_FanSpeed_Get`, etc.

### F3. System-Wide Monitoring

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Hierarchical hardware tree | Yes (HwInfo) | `HardwareNode` tree via `get_hardware_tree()` | :white_check_mark: Implemented and equivalent |
| Flat sensor reading list | Yes | `get_all_readings()` | :white_check_mark: Implemented and equivalent |
| Fan speeds (all headers) | Yes | Not implemented | :x: Not implemented |
| Motherboard temperatures (VRM, chipset) | Yes | Not implemented | :x: Not implemented |
| All voltages (12V, 5V, 3.3V, VDIMM) | Yes | Not implemented | :x: Not implemented |
| Storage SMART data | Yes | Not implemented | :x: Not implemented |
| Network stats | Yes | Not implemented | :x: Not implemented |
| Battery info | Yes | Not implemented (macOS IOKit battery exists but limited) | :x: Not implemented |
| 200+ sensors (HwInfo engine) | Yes | Self-implemented via WMI/sysfs/IOKit (limited coverage) | :warning: Significantly fewer sensors than real OCCT |

**Missing: Fan speeds, motherboard temps, voltages, SMART**
- Priority: High
- Difficulty: Hard
- Suggestion: Complete the `LhmBridge` integration on Windows (LibreHardwareMonitor provides 200+ sensors including VRM temps, all voltages, fan RPMs). For Linux, expand sysfs hwmon parsing. The `lhm_bridge.h` file already exists but needs full implementation.

### F4. Real-Time Graphs

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Live time-series charts | Yes | `RealtimeChart` widget (grid, fill, auto-scale) | :white_check_mark: Implemented and equivalent |
| Multiple parameters overlay | Yes (toggle parameters on/off) | Single series per chart | :warning: Each chart shows one series only |
| Current/Min/Average/Max values | Yes | Min/Max in `SensorReading`, no running average | :warning: Missing average value |
| Adjustable update interval | Yes (2s default) | 500ms default polling | :white_check_mark: Implemented (faster than real OCCT) |
| Monitoring-only mode (no stress) | Yes | CLI `--monitor-only` flag, GUI monitor panel exists | :warning: GUI panel exists but may not be fully independent |

**Missing: Multi-series chart overlay**
- Priority: Medium
- Difficulty: Medium
- Suggestion: Extend `RealtimeChart` to accept multiple data series with independent colors and per-series toggling.

**Missing: Running average in sensor readings**
- Priority: Low
- Difficulty: Easy
- Suggestion: Add `avg_value` to `SensorReading` and compute running average in the polling loop.

### F5. Sensor History / Logging

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Date-organized data folders | Yes | Not implemented | :x: Not implemented |
| Auto-save PNG graphs after test | Yes | Not implemented | :x: Not implemented |
| Continuous sensor logging to file | Yes | Not implemented (CSV export is manual/post-test) | :x: Not implemented |

**Missing: Auto-save sensor graphs and date-organized logging**
- Priority: Medium
- Difficulty: Medium
- Suggestion: After each test completes, auto-save sensor time-series as PNG and CSV to `~/OCCT/Results/YYYY-MM-DD_HH-MM/` structure.

---

## G. Safety / Protection

### G1. Temperature Limits

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| CPU temperature limit | Yes | `SafetyLimits::cpu_temp_max` (default 95C) | :white_check_mark: Implemented and equivalent |
| GPU temperature limit | Yes | `SafetyLimits::gpu_temp_max` (default 90C) | :white_check_mark: Implemented and equivalent |
| Configurable thresholds | Yes | `set_limits(SafetyLimits)`, CLI `--cpu-temp-limit`, `--gpu-temp-limit` | :white_check_mark: Implemented and equivalent |

### G2. Emergency Stop

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Automatic emergency stop | Yes | `SafetyGuardian::emergency_stop()` | :white_check_mark: Implemented and equivalent |
| Emergency callback notification | Yes | `EmergencyCallback` with reason string | :white_check_mark: Implemented and equivalent |
| Engine registration for stop | Yes | `register_engine()` / `unregister_engine()` | :white_check_mark: Implemented and equivalent |
| 200ms check interval | Yes | Yes (200ms check loop) | :white_check_mark: Implemented and equivalent |
| Emergency triggered flag (prevent re-trigger) | Yes | `emergency_triggered_` atomic | :white_check_mark: Implemented and equivalent |

### G3. Power Limit

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| CPU power limit | Yes | `SafetyLimits::cpu_power_max` (default 300W) | :white_check_mark: Implemented and equivalent |
| CLI configurable | Yes | `--power-limit` CLI flag | :white_check_mark: Implemented and equivalent |

### G4. WHEA Error Detection

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| WHEA-Logger event monitoring | Yes | `WheaMonitor` class (Windows Event Log subscription) | :white_check_mark: Implemented and equivalent |
| MCE detection | Yes | `WheaError::Type::MCE` | :white_check_mark: Implemented and equivalent |
| PCIe error detection | Yes | `WheaError::Type::PCIe` | :white_check_mark: Implemented and equivalent |
| NMI detection | Yes | `WheaError::Type::NMI` | :white_check_mark: Implemented and equivalent |
| Auto-stop on WHEA error | Yes | `set_auto_stop(bool)`, integrated with SafetyGuardian | :white_check_mark: Implemented and equivalent |
| WHEA error callback + Qt signal | Yes | `ErrorCallback` + `errorDetected` signal | :white_check_mark: Implemented and equivalent |
| Cross-platform (non-Windows) | N/A (Windows only) | Harmless no-op on Linux/macOS | :white_check_mark: Clean cross-platform handling |
| Error history | Yes | `QVector<WheaError> errors()` (thread-safe copy) | :white_check_mark: Implemented and equivalent |

### G5. Per-Sensor Custom Alarms

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Custom alarm per individual sensor | Yes | Only global CPU temp, GPU temp, power thresholds | :warning: Limited to 3 global thresholds |
| Alert callback | Yes | `SensorManager::set_alert_callback()` | :white_check_mark: Implemented and equivalent |

**Missing: Per-sensor custom alarms**
- Priority: Medium
- Difficulty: Medium
- Suggestion: Add a `std::map<std::string, double>` threshold map to `SafetyGuardian` so users can set custom limits per sensor name.

---

## H. Reporting

### H1. Report Formats

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| HTML reports (self-contained) | Yes (Enterprise) | `HtmlReport::save()` (inline CSS/JS, offline viewable) | :white_check_mark: Implemented and equivalent |
| PNG summary screenshots | Yes (Patreon/Steam) | `ReportManager::save_png()` (800x600) | :white_check_mark: Implemented and equivalent |
| CSV data export | Yes (Pro) | `ReportManager::save_csv()` (sensor time-series) | :white_check_mark: Implemented and equivalent |
| JSON full data | Yes (Enterprise) | `ReportManager::save_json()` | :white_check_mark: Implemented and equivalent |
| CLI report format selection | Yes | `--report-format html/png/csv/json` | :white_check_mark: Implemented and equivalent |
| CLI output path | Yes | `--output-path <path>` | :white_check_mark: Implemented and equivalent |

### H2. Report Comparison

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Compare two reports side-by-side | Yes (Enterprise) | `compare_reports()` function | :white_check_mark: Implemented and equivalent |
| Diff metrics (improved/regressed/unchanged) | Yes | `ComparisonEntry` with diff_abs, diff_pct, direction | :white_check_mark: Implemented and equivalent |
| ASCII table output | Yes | `format_comparison_table()` | :white_check_mark: Implemented and equivalent |
| CLI report comparison | Yes | `--compare-a` and `--compare-b` flags | :white_check_mark: Implemented and equivalent |

### H3. Error Logs

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Per-thread error log files (ThreadX.txt) | Yes | In-memory only | :warning: Errors tracked but not written to per-thread files |
| Error summary in report | Yes | `error_summary()` in CpuEngine | :white_check_mark: Implemented and equivalent |
| Storage error log | Yes | `StorageError` vector + `StorageMetrics::error_log` | :white_check_mark: Implemented and equivalent |

---

## I. GUI Features

### I1. Theme / Visual Design

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Dark theme | Yes | Yes (GitHub-dark-inspired via `panel_styles.h`) | :white_check_mark: Implemented and equivalent |
| Custom skins (LTT, Corsair) | Yes (v15+) | Not implemented | :x: Not implemented |
| Material Design elements | Yes (v5+) | Not implemented | :x: Not implemented |

**Missing: Custom skins / Material Design**
- Priority: Low
- Difficulty: Hard
- Suggestion: Implement a QSS (Qt Style Sheet) theming system. Load `.qss` files from a `skins/` directory.

### I2. Layout / Navigation

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Sidebar navigation | Yes (3-section) | Yes (sidebar with nav buttons and icon chars) | :white_check_mark: Implemented and equivalent |
| Stacked content area | Yes | `QStackedWidget` in MainWindow | :white_check_mark: Implemented and equivalent |
| Header bar | Yes | `createHeaderBar()` | :white_check_mark: Implemented and equivalent |
| Menu bar (File/Help) | Yes | `createMenuBar()` with Export Report, Exit, About | :white_check_mark: Implemented and equivalent |
| Resizable window with state persistence | Yes | Saved geometry via `AppConfig` | :white_check_mark: Implemented and equivalent |
| Section separators in sidebar | Yes | `addSeparator()` method | :white_check_mark: Implemented and equivalent |

### I3. Real-Time Visualization

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Temperature chart | Yes | `RealtimeChart` widget | :white_check_mark: Implemented and equivalent |
| Power chart | Yes | `RealtimeChart` widget | :white_check_mark: Implemented and equivalent |
| Voltage chart | Yes | Not available (no voltage sensor data) | :x: Not implemented (blocked by missing sensor data) |
| Frequency chart | Yes | Limited (PDH-based on Windows only) | :warning: Windows only |
| Chart grid lines | Yes | `setGridVisible(bool)` | :white_check_mark: Implemented and equivalent |
| Chart fill/area | Yes | `setFillEnabled(bool)` | :white_check_mark: Implemented and equivalent |
| Chart auto-scaling | Yes | `setAutoScale(bool)` | :white_check_mark: Implemented and equivalent |
| Toggle parameters on/off | Yes | Not implemented (one series per chart) | :x: Not implemented |

### I4. Per-Core Status Display

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Per-core usage bars | Yes | Yes (CPU panel shows per-core usage) | :white_check_mark: Implemented and equivalent |
| Per-core error indicators | Yes | `core_has_error` vector, `core_error_flags_` atomics | :white_check_mark: Implemented and equivalent |
| P-core/E-core labels | Yes | `per_core_type` | :white_check_mark: Implemented and equivalent |

### I5. Widgets

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Circular gauges | Yes | `CircularGauge` widget (animated via QPropertyAnimation, color-coded) | :white_check_mark: Implemented and equivalent |
| Gauge overlay text (N/A) | Yes | `setOverlayText()` | :white_check_mark: Implemented and equivalent |
| Configurable arc color | Yes | `setArcColor()` | :white_check_mark: Implemented and equivalent |

### I6. Panels

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| CPU panel | Yes | `cpu_panel.h` | :white_check_mark: Implemented |
| GPU panel | Yes | `gpu_panel.h` | :white_check_mark: Implemented |
| RAM panel | Yes | `ram_panel.h` | :white_check_mark: Implemented |
| Storage panel | Yes | `storage_panel.h` | :white_check_mark: Implemented |
| PSU panel | Yes | `psu_panel.h` | :white_check_mark: Implemented |
| Monitor panel | Yes | `monitor_panel.h` | :white_check_mark: Implemented |
| System Info panel | Yes | `sysinfo_panel.h` | :white_check_mark: Implemented |
| Schedule panel | Yes | `schedule_panel.h` | :white_check_mark: Implemented |
| Certificate panel | Yes | `certificate_panel.h` | :white_check_mark: Implemented |
| Benchmark panel | Yes | `benchmark_panel.h` | :white_check_mark: Implemented |
| Results/Dashboard panel | Yes | `results_panel.h` + `dashboard_panel.h` | :white_check_mark: Implemented |

### I7. Concurrent Test Execution in GUI

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Run multiple tests in parallel from GUI | Yes | Scheduler supports `parallel_with_next`, but GUI runs single test at a time | :warning: Scheduler supports it but GUI is single-test |

**Missing: GUI parallel test execution**
- Priority: High
- Difficulty: Medium
- Suggestion: Allow starting CPU + RAM + GPU panels simultaneously. Track multiple active engines in MainWindow and display composite status.

---

## J. Scheduling / Automation

### J1. Multi-Step Test Sequences

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Sequential test steps | Yes | `TestScheduler` with `QVector<TestStep>` | :white_check_mark: Implemented and equivalent |
| Parallel test steps | Yes | `parallel_with_next` flag in TestStep | :white_check_mark: Implemented and equivalent |
| Stop on error option | Yes | `set_stop_on_error(bool)`, CLI `--stop-on-error` | :white_check_mark: Implemented and equivalent |
| Step progress tracking | Yes | `stepStarted`, `stepCompleted`, `progressChanged` signals | :white_check_mark: Implemented and equivalent |
| Step results history | Yes | `QVector<StepResult> results()` | :white_check_mark: Implemented and equivalent |
| Active parallel step tracking | Yes | `active_step_indices_` vector | :white_check_mark: Implemented and equivalent |

### J2. Preset Profiles

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Quick check (~5 min) | Yes | `preset_quick_check()` | :white_check_mark: Implemented and equivalent |
| Standard (~30 min) | Yes | `preset_standard()` | :white_check_mark: Implemented and equivalent |
| Extreme (~1 hour) | Yes | `preset_extreme()` | :white_check_mark: Implemented and equivalent |
| OC Validation (~2 hours) | Yes | `preset_oc_validation()` | :white_check_mark: Implemented and equivalent |
| CLI preset selection | Yes | `--preset quick/standard/extreme/oc_validation` | :white_check_mark: Implemented and equivalent |

### J3. Schedule Persistence

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Save/load JSON schedule | Yes | `save_to_json()` / `load_from_json()` | :white_check_mark: Implemented and equivalent |
| CLI schedule file | Yes | `--schedule-file <path>` | :white_check_mark: Implemented and equivalent |

### J4. CLI Automation

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Headless CLI mode | Yes (Enterprise only) | `--cli` flag (free) | :white_check_mark: Implemented (free, no license needed) |
| Exit codes (pass/fail/error) | Yes | 0=PASS, 1=FAIL, 2=ERROR | :white_check_mark: Implemented and equivalent |
| JSON stdout output | Yes | Yes (progress, metrics, errors) | :white_check_mark: Implemented and equivalent |
| Combined multi-engine test | Yes | `--engines cpu,gpu,ram,storage` | :white_check_mark: Implemented and equivalent |
| Monitor-only mode | Yes | `--monitor-only` | :white_check_mark: Implemented and equivalent |
| Remote diagnostics | Yes (Enterprise+) | Not implemented | :x: Not implemented |
| Device history tracking | Yes (Enterprise+) | Not implemented | :x: Not implemented |

**Missing: Remote diagnostics**
- Priority: Low
- Difficulty: Hard
- Suggestion: Requires network server component. Out of scope.

**Missing: Device history tracking**
- Priority: Low
- Difficulty: Medium
- Suggestion: Store test results in a local SQLite database keyed by hardware fingerprint.

---

## K. Certification System

### K1. Certification Tiers

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Bronze (~1-2 hours) | Yes | `CertTier::BRONZE` + `preset_cert_bronze()` | :white_check_mark: Implemented and equivalent |
| Silver (~3-4 hours) | Yes | `CertTier::SILVER` + `preset_cert_silver()` | :white_check_mark: Implemented and equivalent |
| Gold (~6-8 hours) | Yes | `CertTier::GOLD` + `preset_cert_gold()` | :white_check_mark: Implemented and equivalent |
| Platinum (~12 hours) | Yes | `CertTier::PLATINUM` + `preset_cert_platinum()` | :white_check_mark: Implemented and equivalent |

### K2. Certificate Content

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| System info in certificate | Yes | `Certificate::system_info_json` | :white_check_mark: Implemented and equivalent |
| Per-test results | Yes | `QVector<TestResult>` (engine, mode, passed, errors, duration) | :white_check_mark: Implemented and equivalent |
| Pass/fail (zero errors required) | Yes | `Certificate::passed` | :white_check_mark: Implemented and equivalent |
| SHA-256 tamper detection | Yes | `Certificate::hash_sha256` | :white_check_mark: Implemented and equivalent |
| Expiration date | Yes | `Certificate::expires_at` (issued + 90 days) | :white_check_mark: Implemented and equivalent |
| Tier-colored badges | Yes | `cert_tier_color()` (Bronze=#CD7F32, Silver=#C0C0C0, Gold=#FFD700, Platinum=#E5E4E2) | :white_check_mark: Implemented and equivalent |

### K3. Certificate Types

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| System certificate (whole system) | Yes | Yes (default) | :white_check_mark: Implemented and equivalent |
| CPU-only certificate | Yes | Not implemented | :x: Not implemented |
| GPU-only certificate | Yes | Not implemented | :x: Not implemented |
| Memory-only certificate | Yes | Not implemented | :x: Not implemented |
| Custom certificates | Yes (Enterprise+) | Not implemented | :x: Not implemented |

**Missing: Per-component certificates**
- Priority: Medium
- Difficulty: Easy
- Suggestion: Add `CertificateType` enum (SYSTEM, CPU, GPU, MEMORY) and create preset schedules that test only the relevant component.

### K4. Certificate Distribution

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Online sharing (ocbase.com URL) | Yes | Not implemented (no server) | :x: Not implemented |
| Local certificate store | Yes | `CertStore` class (local JSON file) | :white_check_mark: Implemented and equivalent |
| Certificate verification by hash | Yes | `CertStore::verify()` | :white_check_mark: Implemented and equivalent |
| CLI certificate operations | Yes | `--upload-cert`, `--verify-hash`, `--list-certs` | :white_check_mark: Implemented and equivalent |
| HTML/PNG certificate output | Yes | Via `CertGenerator` | :white_check_mark: Implemented and equivalent |
| CLI tier selection | Yes | `--cert-tier bronze/silver/gold/platinum` | :white_check_mark: Implemented and equivalent |

**Missing: Online certificate sharing**
- Priority: Low
- Difficulty: Hard
- Suggestion: Requires web server. Could implement as optional integration with a hosted service.

---

## L. Settings / Configuration

### L1. Persistent Settings

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Window geometry persistence | Yes | `AppConfig::windowGeometry()` / `setWindowGeometry()` | :white_check_mark: Implemented and equivalent |
| Window state persistence | Yes | `AppConfig::windowState()` / `setWindowState()` | :white_check_mark: Implemented and equivalent |
| Last CPU test settings | Yes | `lastCpuSettings()` / `setLastCpuSettings()` | :white_check_mark: Implemented and equivalent |
| Last GPU test settings | Yes | `lastGpuSettings()` / `setLastGpuSettings()` | :white_check_mark: Implemented and equivalent |
| Last RAM test settings | Yes | `lastRamSettings()` / `setLastRamSettings()` | :white_check_mark: Implemented and equivalent |
| Last Storage test settings | Yes | `lastStorageSettings()` / `setLastStorageSettings()` | :white_check_mark: Implemented and equivalent |
| Generic key-value storage | Yes | `AppConfig::value()` / `setValue()` | :white_check_mark: Implemented and equivalent |

### L2. Portable Mode

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| USB portable mode | Yes | `PortablePaths` class (auto-detect writable app dir) | :white_check_mark: Implemented and equivalent |
| Portable detection | Yes | `PortablePaths::isPortable()` | :white_check_mark: Implemented and equivalent |
| Config/logs/temp path management | Yes | `configDir()`, `logsDir()`, `tempDir()` | :white_check_mark: Implemented and equivalent |

### L3. Multi-Language Support

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Multi-language UI | Yes (multiple languages) | Not implemented | :x: Not implemented |

**Missing: Multi-language support**
- Priority: Low
- Difficulty: Medium
- Suggestion: Qt has built-in i18n via `tr()` and `.ts` translation files. Wrap all user-visible strings in `tr()` and create translation files.

### L4. Auto-Update

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Automatic update checking | Yes | Not implemented | :x: Not implemented |
| In-app update | Yes | Not implemented | :x: Not implemented |

**Missing: Auto-update**
- Priority: Low
- Difficulty: Medium
- Suggestion: Check a version endpoint on startup via `QNetworkAccessManager` and notify if a newer release is available.

### L5. Distribution

| Feature | Real OCCT | occt-native | Status |
|---------|-----------|-------------|--------|
| Steam distribution | Yes | Not implemented | :x: Not implemented |
| Installer (NSIS/MSI) | Yes | Not implemented (CPack ZIP) | :warning: ZIP distribution only |

---

## M. Licensing / Distribution

### Real OCCT Licensing Model

| Edition | Price | Key Limitations |
|---------|-------|-----------------|
| **Free** | Free | 1-hour test limit, 10-second startup delay, no CSV/JSON export |
| **Patreon/Steam** | ~$5/month or one-time | Unlimited testing, certificates, PNG reports |
| **Pro** | Paid | CSV export, commercial use allowed |
| **Enterprise** | Paid | CLI mode, HTML/JSON reports, report comparison |
| **Enterprise+** | Paid | Remote diagnostics, cloud dashboard, custom certificates |

### occt-native: Which Real OCCT Paid Features Are Implemented for Free?

| Paid Feature in Real OCCT | Required Edition | occt-native | Status |
|---------------------------|-----------------|-------------|--------|
| Unlimited test duration | Patreon+ | Yes, free | :white_check_mark: |
| No startup delay | Patreon+ | Yes, free | :white_check_mark: |
| Stability certificates | Patreon+ | Yes, free | :white_check_mark: |
| PNG reports | Patreon+ | Yes, free | :white_check_mark: |
| CSV data export | Pro | Yes, free | :white_check_mark: |
| Commercial use | Pro | Yes, free | :white_check_mark: |
| CLI headless mode | Enterprise | Yes, free | :white_check_mark: |
| HTML reports | Enterprise | Yes, free | :white_check_mark: |
| JSON reports | Enterprise | Yes, free | :white_check_mark: |
| Report comparison | Enterprise | Yes, free | :white_check_mark: |
| Remote diagnostics | Enterprise+ | No | :x: |
| Cloud dashboard | Enterprise+ | No | :x: |
| Custom branded certificates | Enterprise+ | No | :x: |
| Online certificate sharing | Enterprise+ | No | :x: |

---

## Priority Summary of All Missing/Incomplete Features

### High Priority (3 items)

| # | Missing Feature | Category | Difficulty | Impact |
|---|----------------|----------|------------|--------|
| 1 | AMD ADL GPU sensor implementation (currently stub) | F. Monitoring | Medium | AMD GPU users get no temperature/power/clock data |
| 2 | Fan speed / motherboard temp / voltage monitoring (complete LHM bridge) | F. Monitoring | Hard | Monitoring is the weakest area vs real OCCT; 200+ sensors vs handful |
| 3 | GUI parallel test execution | I. GUI | Medium | Real OCCT allows running CPU+GPU+RAM simultaneously from GUI |

### Medium Priority (10 items)

| # | Missing Feature | Category | Difficulty | Impact |
|---|----------------|----------|------------|--------|
| 4 | Auto ISA selection (`AUTO` enum for CpuStressMode) | A. CPU | Easy | Convenience for users |
| 5 | Per-thread error log files (ThreadX.txt) | A/H. CPU/Reporting | Easy | Useful for overclockers |
| 6 | GPU clock speed monitoring | F. Monitoring | Easy | Common expected metric |
| 7 | Multi-series chart overlay | F/I. Monitoring/GUI | Medium | Toggle multiple sensors on one graph |
| 8 | Auto-save sensor graphs after test | F. Monitoring | Medium | Automatic test documentation |
| 9 | Per-sensor custom alarms | G. Safety | Medium | Set threshold per individual sensor |
| 10 | Per-component certificates (CPU, GPU, Memory) | K. Certification | Easy | Certify individual components |
| 11 | GPU resolution / fullscreen settings | B. GPU | Medium | Control render target size |
| 12 | Voltage stability curves during PSU test | D. PSU | Hard | Visualize PSU rail stability |
| 13 | SSD temperature monitoring | E. Storage | Medium | SMART-based temperature |

### Low Priority (18 items)

| # | Missing Feature | Category | Difficulty | Impact |
|---|----------------|----------|------------|--------|
| 14 | Pure AVX-only stress mode | A. CPU | Easy | Niche |
| 15 | Medium data set size | A. CPU | Easy | Minor |
| 16 | Physical-only threads option | A. CPU | Easy | Niche Linpack option |
| 17 | RAM test instruction set selection | C. RAM | Easy | Auto works fine |
| 18 | "All" RAM pattern meta-mode | C. RAM | Easy | Users can run sequentially |
| 19 | Moving Inversions pattern | C. RAM | Easy | Additional coverage |
| 20 | Block Move pattern | C. RAM | Easy | Additional coverage |
| 21 | Bit Fade test | C. RAM | Easy | Data retention test |
| 22 | Custom UI skins (LTT, Corsair) | I. GUI | Hard | Cosmetic, partner-specific |
| 23 | Material Design elements | I. GUI | Hard | Cosmetic |
| 24 | Multi-language UI | L. Settings | Medium | i18n |
| 25 | Auto-update checking | L. Settings | Medium | Distribution |
| 26 | Online certificate sharing | K. Certification | Hard | Requires server |
| 27 | Online benchmark database | E. Storage | Hard | Requires server |
| 28 | Remote diagnostics | J. Scheduling | Hard | Enterprise+ |
| 29 | Device history tracking | J. Scheduling | Medium | Nice-to-have |
| 30 | GPU FPS limit | B. GPU | Easy | Minor |
| 31 | GPU usage limit (%) | B. GPU | Medium | Minor |
| 32 | PSU protection mode detection | D. PSU | Medium | Edge case |
| 33 | Native CUDA compute path | B. GPU | Hard | OpenCL via ICD covers NVIDIA |
| 34 | Running average in sensor readings | F. Monitoring | Easy | Minor display |
| 35 | DirectX 3D Standard | B. GPU | Hard | Conflicts with cross-platform |

---

## Areas Where occt-native Is Superior to Real OCCT

| Feature | Advantage |
|---------|-----------|
| **macOS / Apple Silicon support** | Real OCCT does not support macOS at all |
| **ARM NEON SIMD stress test** | Native ARM stress testing on Apple Silicon |
| **All features free and unlimited** | Real OCCT gates CLI, CSV, HTML reports behind paid tiers |
| **No test duration limit** | Real OCCT Free limits tests to 1 hour |
| **No startup delay** | Real OCCT Free has a 10-second startup delay |
| **More storage verification modes** | 4 verification modes (VERIFY_SEQ, VERIFY_RAND, FILL_VERIFY, BUTTERFLY) |
| **Butterfly storage verify** | Unique converging-from-both-ends verification pattern |
| **Prime number CPU test** | Additional test mode not in real OCCT |
| **5-level shader complexity** | Granular GPU stress control vs automatic-only in real OCCT |
| **Coil whine frequency control** | Configurable Hz + sweep mode vs simple modulation |
| **Report comparison tool** | Free, built-in two-report diff |
| **CPU power estimation transparency** | `is_cpu_power_estimated()` flag informs users when power is estimated vs measured |
| **Open-source architecture** | Extensible and auditable codebase |
| **Cross-platform from day one** | Windows, Linux, macOS with platform-specific optimizations |

---

## Recommended Next Steps (Priority Order)

1. **Complete AMD ADL sensor implementation** -- Unblock AMD GPU monitoring (High, Medium effort)
2. **Complete LHM bridge for Windows** -- Get fan speeds, VRM temps, all voltages (High, Hard effort)
3. **Enable GUI parallel test execution** -- Match real OCCT simultaneous CPU+GPU+RAM (High, Medium effort)
4. **Add auto ISA selection** -- Small quality-of-life improvement (Medium, Easy effort)
5. **Implement per-thread error log files** -- ThreadX.txt for overclockers (Medium, Easy effort)
6. **Add GPU clock speed to metrics** -- Simple NVML call (Medium, Easy effort)
7. **Multi-series chart overlay** -- Better monitoring visualization (Medium, Medium effort)
8. **Per-component certificates** -- CPU/GPU/Memory individual certs (Medium, Easy effort)
9. **Auto-save sensor data after test** -- Date-organized results (Medium, Medium effort)
10. **Per-sensor custom alarms** -- Configurable per-sensor thresholds (Medium, Medium effort)

---

## Sources

- [OCBASE Official Site](https://www.ocbase.com/)
- [OCCT on Steam](https://store.steampowered.com/app/3515100/OCCT/)
- [OCCT CPU Test Configuration](https://www.ocbase.com/support/stability-testing-configuration-cpu)
- [OCCT Stability Certificate Overview](https://www.ocbase.com/support/stability-certificate-overview)
- [How to use OCCT - rTS Wiki](https://rtech.support/guides/how-to-use-occt/)
- [OCCT With Four Options - Tom's Hardware](https://www.tomshardware.com/reviews/stress-test-cpu-pc-guide,5461-3.html)
- [6 Reasons OCCT Is My Go-To - Make Tech Easier](https://www.maketecheasier.com/occt-stress-testing-cpus-gpus/)
- [OCCT Tutorial - Mediaket](https://tutorials.mediaket.net/software-tutorials/occt-tutorial.html)
- [OCCT Error Detected - 10Scopes](https://10scopes.com/occt-error-detected/)
- [OCCT Enterprise+ License - OC3D](https://overclock3d.net/news/software/occt-publisher-ocbase-launches-their-enterprise-license/)
- [7 GHz OCCT Bronze Certificate - SkatterBencher](https://skatterbencher.com/2024/06/18/7-ghz-occt-bronze-stability-certificate/)
- [OCCT Purchase Page](https://www.ocbase.com/purchase)
- [How to perform stability testing with OCCT - MundoBytes](https://mundobytes.com/en/How-to-perform-stability-tests-with-OCCT-on-CPU--GPU--RAM--and-PSU/)
