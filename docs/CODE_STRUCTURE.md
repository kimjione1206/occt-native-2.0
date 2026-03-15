# OCCT Native - 전체 코드 구조

> 이 문서는 프로젝트 수정 시 반드시 참고해야 하는 코드 구조 레퍼런스입니다.
> 최종 업데이트: 2026-03-14 (CPU AUTO/AVX_FLOAT 모드, 유니코드 스토리지, AMD ADL, LHM 재시도, lhm-sensor-reader, GUI 멀티시리즈 차트/트레이/단축키/사운드, 한국어 UI, 파일 로깅)

## 프로젝트 개요

- **언어**: C++17 + Qt6
- **빌드**: CMake 3.21+
- **플랫폼**: Windows (주), macOS, Linux
- **실행 모드**: GUI (기본) / CLI (`--cli`)

## 디렉토리 구조

```
occt-native/
├── CMakeLists.txt              # 루트 빌드 설정
├── config.h.in                 # 버전/플랫폼 매크로 생성
├── src/
│   ├── main.cpp                # 진입점 (GUI/CLI 분기)
│   ├── engines/                # 스트레스 테스트 엔진
│   │   ├── base_engine.h       # IEngine 인터페이스
│   │   ├── cpu_engine.h/cpp    # CPU 스트레스 (IEngine + SensorManager 연동)
│   │   ├── gpu_engine.h/cpp    # GPU 스트레스 (IEngine + Pimpl)
│   │   ├── ram_engine.h/cpp    # RAM 패턴 테스트
│   │   ├── storage_engine.h/cpp# 스토리지 I/O
│   │   ├── psu_engine.h/cpp    # PSU (CPU+GPU 동시)
│   │   ├── cpu/                # CPU 서브모듈
│   │   │   ├── avx_stress.h/cpp    # SSE/AVX2/AVX512/AVX_FLOAT/NEON FMA (+stress_avx_nofma)
│   │   │   ├── error_verifier.h/cpp# IEEE 754 검증
│   │   │   ├── linpack.h/cpp       # DGEMM 행렬연산
│   │   │   └── prime.h/cpp         # 소수 검증
│   │   ├── gpu/                # GPU 서브모듈
│   │   │   ├── opencl_backend.h/cpp
│   │   │   ├── vulkan_backend.h/cpp
│   │   │   ├── vulkan_renderer.h/cpp
│   │   │   ├── artifact_detector.h/cpp
│   │   │   ├── multi_gpu_manager.h/cpp
│   │   │   └── shaders/stress_shaders.h
│   │   ├── storage/            # 스토리지 검증
│   │   │   ├── verify_block.h      # 블록 헤더/CRC 구조
│   │   │   └── storage_verify.cpp  # 4개 검증 모드
│   │   └── benchmark/          # 벤치마크
│   │       ├── cache_benchmark.h/cpp
│   │       └── memory_benchmark.h/cpp
│   ├── monitor/                # 하드웨어 모니터링
│   │   ├── sensor_manager.h/cpp    # 통합 센서 관리 (WMI/IOKit/sysfs)
│   │   ├── sensor_model.h/cpp      # 하드웨어 트리 구조
│   │   ├── system_info.h/cpp       # 시스템 사양 수집
│   │   ├── whea_monitor.h/cpp      # Windows WHEA 이벤트
│   │   └── lhm_bridge.h/cpp       # LibreHardwareMonitor 연동 (30s 타임아웃, 5회 재시도, fail_count_, 파일 로깅)
│   ├── safety/                 # 안전 시스템
│   │   └── guardian.h/cpp          # 온도/전력 리밋 → 긴급정지
│   ├── gui/                    # Qt GUI
│   │   ├── main_window.h/cpp       # 메인 윈도우 (트레이 아이콘, 단축키 Ctrl+1~6/Esc, stopAllTests(), 사운드 알림)
│   │   ├── panels/             # 12개 패널
│   │   │   ├── panel_styles.h           # 공용 스타일 상수 (12개 패널 공유)
│   │   │   ├── dashboard_panel.h/cpp   # 대시보드 (게이지)
│   │   │   ├── cpu_panel.h/cpp         # CPU 테스트
│   │   │   ├── gpu_panel.h/cpp         # GPU 테스트
│   │   │   ├── ram_panel.h/cpp         # RAM 테스트
│   │   │   ├── storage_panel.h/cpp     # Storage 테스트
│   │   │   ├── psu_panel.h/cpp         # PSU 테스트
│   │   │   ├── monitor_panel.h/cpp     # 센서 모니터링
│   │   │   ├── benchmark_panel.h/cpp   # 벤치마크
│   │   │   ├── schedule_panel.h/cpp    # 스케줄 실행
│   │   │   ├── certificate_panel.h/cpp # 인증서
│   │   │   ├── sysinfo_panel.h/cpp     # 시스템 정보
│   │   │   └── results_panel.h/cpp     # 결과 조회
│   │   └── widgets/            # 공용 위젯
│   │       ├── realtime_chart.h/cpp    # 실시간 차트 (멀티시리즈: ChartSeries, addSeries(), 범례)
│   │       └── circular_gauge.h/cpp    # 원형 게이지
│   ├── cli/                    # CLI 인터페이스
│   │   ├── cli_args.h/cpp          # 인자 파싱 (70+ 옵션)
│   │   └── cli_runner.h/cpp        # CLI 실행기
│   ├── scheduler/              # 테스트 스케줄러
│   │   ├── test_scheduler.h/cpp    # 멀티스텝 오케스트레이션
│   │   └── preset_schedules.h/cpp  # 8개 프리셋
│   ├── report/                 # 리포트 생성
│   │   ├── report_manager.h/cpp    # PNG/HTML/CSV/JSON
│   │   ├── png_report.h/cpp
│   │   ├── html_report.h/cpp
│   │   └── csv_exporter.h/cpp
│   ├── certification/          # 인증서 시스템
│   ├── api/                    # cert_store
│   ├── leaderboard/            # 성능 랭킹
│   └── utils/                  # 유틸리티
│       ├── cpuid.h/cpp             # CPU 감지 (CPUID)
│       ├── gpu_info.h/cpp          # GPU 센서 (NVML/ADL)
│       ├── crc32.h/cpp             # CRC32C (HW+SW)
│       ├── app_config.h/cpp        # QSettings 래퍼
│       ├── file_logger.h/cpp       # 로그 파일 (5MB 로테이션)
│       └── portable_paths.h/cpp    # 포터블 경로
├── tests/                      # 단위 테스트
├── resources/                  # QRC, 스타일시트
├── tools/
│   └── lhm-sensor-reader/         # C# LHM 헬퍼 (JsonSerializerContext 소스 생성, self-contained ~15-20MB)
├── logs/                          # 런타임 로그 출력 디렉토리
│   ├── lhm_bridge.log             # LHM 브리지 로그
│   └── storage_engine.log         # 스토리지 엔진 로그
├── .github/workflows/             # CI/CD
│   ├── windows-test.yml            # 46개 Windows 테스트
│   ├── build-windows.yml           # 포터블 릴리즈 빌드 (.NET SDK + LHM 헬퍼 빌드 포함)
│   ├── build.yml                   # 멀티플랫폼 빌드 (.NET SDK + LHM 헬퍼 빌드 포함)
│   ├── gpu-test.yml                # GPU 테스트
│   └── gui-smoke-test.yml          # GUI 스크린샷 + UI Automation (~94 위젯, 13 assertions) + LHM 헬퍼 빌드
└── docs/
    └── CODE_STRUCTURE.md           # 이 문서
```

## 엔진 라이프사이클 패턴

모든 엔진은 `IEngine` 인터페이스를 구현 (GPU 포함 — Pimpl + IEngine 상속):

```
start(mode, ...) → is_running() == true
                 → get_metrics() (500ms 간격)
                 → stop() → is_running() == false
```

| 엔진 | 모드 수 | 스레드 구조 | 검증 방식 |
|------|---------|------------|----------|
| CPU | 10 (AVX2, AVX512, SSE, AVX_FLOAT, AUTO, Linpack, Prime...) | N workers + metrics | IEEE 754 FMA 체인 비트 비교 (10,000회 반복, 4종 시드, Prime DB 검증, CACHE/LARGE_DATA 체크섬) |
| GPU | 8 (Matrix, FMA, Trig, VRAM, Vulkan...) | 1 worker + metrics | Artifact Detector (3프레임/1회 샘플링) + VRAM 7종 패턴 (March C-, Butterfly, Random 포함) + FMA 교차검증 |
| RAM | 6 (March C-, Walking 1/0, Checker...) | 1 worker | 패턴 쓰기 → 캐시 플러시(clflush) → 읽기 비교 + 비트레벨 XOR 분석 + 상보 Checkerboard |
| Storage | 9 (Seq/Rand R/W, Verify, Fill...) | 1 worker + I/O | CRC32C + 블록 헤더 검증 + FUA(O_SYNC/WRITE_THROUGH) + 타입별 에러 통계 |
| PSU | 3 (Steady, Spike, Ramp) | CPU+GPU 동시 | 전력-에러 상관분석 + 4단계 상태 판정 (HEALTHY/MARGINAL/UNSTABLE/FAILED) |

## GUI 아키텍처

```
MainWindow
├── statusTimer_ (1s) → updateStatusBar(), timeLabel_
├── sensorMgr_ (unique_ptr<SensorManager>) → 5개 패널에 분배
│   ├── monitorPanel->setSensorManager()
│   ├── cpuPanel->setSensorManager() → engine_->set_sensor_manager()
│   ├── gpuPanel->setSensorManager()
│   ├── psuPanel->setSensorManager() → engine_->set_sensor_manager() (내부 CPU 엔진)
│   └── dashboardPanel->setSensorManager()
├── safetyGuardian_ (unique_ptr<SafetyGuardian>) → 5개 엔진 등록
│   ├── cpuPanel->engine()
│   ├── gpuPanel->engine()
│   ├── ramPanel->engine()
│   ├── storagePanel->engine()
│   └── psuPanel->engine()
├── trayIcon_ (QSystemTrayIcon) → 시스템 트레이 아이콘
├── keyboard shortcuts (Ctrl+1~6 패널 전환, Escape → stopAllTests())
├── sound alerts (playTestCompleteSound(), playTestErrorSound())
├── sidebar_ (12 nav buttons) → setActiveTab()
└── contentStack_ (QStackedWidget, 12 panels)
```

**패널별 엔진 소유:**
- CPU Panel → `CpuEngine` (unique_ptr)
- GPU Panel → `GpuEngine` (unique_ptr)
- RAM Panel → `RamEngine` (unique_ptr)
- Storage Panel → `StorageEngine` (unique_ptr)
- PSU Panel → `PsuEngine` (unique_ptr)

**센서 데이터 흐름:**
```
SensorManager::poll_thread (500ms)
  ├── [Windows] LHM bridge (lhm_bridge_) → 정확한 하드웨어 모니터링 (최우선)
  │   ├── LibreHardwareMonitor DLL → CPU/GPU/MB 온도, 전력, 팬 RPM
  │   ├── 타임아웃 30초 (기존 5초), 5회 재시도 후 비활성화 (fail_count_)
  │   └── 파일 로깅 → logs/lhm_bridge.log
  ├── [Windows] poll_wmi() → 캐시된 WMI COM 연결 사용 (wmi_svc_root_wmi_, wmi_svc_cimv2_)
  │   ├── Temperature fallback chain:
  │   │   1. MSAcpi_ThermalZoneTemperature (WMI ROOT\WMI)
  │   │   2. Win32_PerfFormattedData (Performance Counter)
  │   │   3. N/A (GUI shows "-- °C" or "N/A")
  │   ├── GetSystemTimes → CPU 사용률
  │   ├── PDH-based dynamic CPU frequency:
  │   │   └── PdhCollectQueryData → \Processor Information(_Total)\% Processor Performance
  │   │       → actual_freq = max_clock_speed * (perf_pct / 100.0)
  │   └── CPU power estimation (TDP × usage%):
  │       └── estimate_tdp(brand) → TDP lookup by CPU family
  │           → estimated_power = TDP * (cpu_pct / 100.0)
  │           → is_cpu_power_estimated() returns true
  ├── [macOS]   poll_iokit() → ThermalState + Battery + Mach CPU + Memory
  ├── [Linux]   poll_sysfs() → /sys/class/hwmon + /sys/class/thermal
  ├── poll_nvml() → NVIDIA GPU temp/power
  └── poll_adl()  → AMD GPU (ADL2 함수 포인터: 온도, 팬 → 활성 어댑터 순회)
        + get_fan_speeds(), get_voltages() 편의 메서드
        ↓
  update_reading(name, category, value, unit)
        ↓
  GUI panels: get_cpu_temperature(), get_gpu_temperature(), get_all_readings()
  CLI output: temperature=null when 0, power_estimated=true when TDP-based
```

**WMI COM 캐싱 (Windows):**
- `wmi_locator_`, `wmi_svc_root_wmi_`, `wmi_svc_cimv2_` → 멤버 변수로 캐시
- 초기화 1회, 이후 500ms 폴링마다 재사용 (ConnectServer 호출 제거)
- 연결 끊김 시 `reconnect_wmi()` → 자동 재연결
- 종료 시 `cleanup_wmi()` → COM 리소스 정리

**PSU 센서 체인:**
```
PsuPanel::setSensorManager(mgr)
  → PsuEngine::set_sensor_manager(mgr)
    → 내부 cpu_ (CpuEngine)::set_sensor_manager(mgr)
      → CPU 전력 읽기 → PSU 메트릭에 반영
```

**패널 에러 표시:**
- `panel_styles.h`: `kErrorText` (빨간 볼드), `kWarningBanner` (노란 테두리)
- GPU Panel: `statusBanner_` → "GPU backend not available" 경고
- Storage Panel: `statusLabel_` → 파일 오픈 실패 시 빨간 에러 표시
- Main Window: 센서 초기화 실패 시 상태바 경고 메시지

## CLI 구조

```bash
occt_native --cli --test <type> --mode <mode> [옵션]

# 테스트 타입: cpu, gpu, ram, storage, psu, benchmark
# 공통 옵션: --duration, --threads, --report-dir, --json
# CPU 전용: --mode auto|avx2|avx512|sse|avx_float|linpack|prime|cache|large_data|all
#           --load-pattern steady|variable|core_cycling
#           --intensity normal|extreme
# 종료코드: 0=PASS, 1=FAIL, 2=ERROR
```

## CMake 빌드 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `OCCT_ENABLE_AVX512` | OFF | AVX-512 지원 |
| `OCCT_ENABLE_OPENCL` | OFF | OpenCL GPU 테스트 |
| `OCCT_ENABLE_VULKAN` | OFF | Vulkan 컴퓨트 |
| `OCCT_PORTABLE` | ON | 포터블 빌드 (정적 CRT) |
| `OCCT_CONSOLE` | OFF | 콘솔 서브시스템 (CI용) |
| `OCCT_BUILD_LHM_HELPER` | OFF | lhm-sensor-reader C# 헬퍼 빌드 |
| `BUILD_TESTS` | ON | 테스트 빌드 |

## CI/CD 워크플로우 (windows-test.yml)

**46개 테스트**, 13개 카테고리:

| 카테고리 | 테스트 수 | 내용 |
|---------|----------|------|
| A: Basic | 2 | --help, --version |
| B: CPU | 11 | 모든 모드, 로드패턴, 스레딩 |
| C: RAM | 6 | 모든 패턴 |
| D: Storage | 9 | 모든 I/O + 검증 모드 |
| E: Benchmark | 3 | CPU/Storage/Cache |
| F: GPU | 1 | Graceful failure (exit 2 허용) |
| G: PSU | 1 | Graceful failure (exit 2 허용) |
| H: Monitor | 2 | 센서 폴링, CSV 출력 |
| I: Report | 4 | PNG/HTML/CSV/JSON |
| J: Schedule | 1 | Quick 스케줄 |
| K: Cert | 1 | Bronze 인증 |
| L: Negative | 3 | 잘못된 입력 처리 |
| M: Verify | 2 | 리포트 파일 검증 |

**핵심 CI 설정:**
- `-DOCCT_CONSOLE=ON` → WIN32_EXECUTABLE 비활성화 (PowerShell 동기 실행)
- GPU/PSU 테스트: exit code 2 허용 (하드웨어 없음)
- `$PSNativeCommandUseErrorActionPreference = $false` → exit code 전파 방지

## GUI 스모크 테스트 (gui-smoke-test.yml)

**2개 잡:**

| 잡 | 빌드 모드 | 내용 |
|----|----------|------|
| cli-log-test | `OCCT_CONSOLE=ON` | 8개 CLI 테스트 + 로그 파일 캡처 |
| gui-screenshot-test | 기본 (GUI) | 29개 스크린샷 + UI Automation 조작 |

**CLI 로그 테스트:** CPU/RAM/Storage/Benchmark/Monitor/PSU/GPU 순차 실행, `.log` 파일로 저장

**GUI 스크린샷 테스트:**
- Windows UI Automation API (`System.Windows.Automation`) 사용
- `Click-ButtonByName`: 버튼 이름으로 검색+클릭 (InvokePattern)
- `Click-SidebarPanel`: 사이드바 버튼을 텍스트 부분 매칭으로 탐색
- 실제 테스트 시작/정지 수행 (CPU 13초, RAM 13초, Storage 8초 등)
- `setAccessibleDescription()` 설정된 위젯 약 94개 → label-name 형제 탐색으로 값 읽기
- 13개 자동화 검증 (GFLOPS, 에러 수, 온도 등)
- 아티팩트: `gui-screenshots` (29장 PNG), `cli-test-logs` (8개 .log)

## 안전 시스템

```
SafetyGuardian (200ms 폴링)
├── SensorManager → CPU/GPU 온도 체크
├── WheaMonitor → Windows 하드웨어 에러 감지
├── 등록된 IEngine 목록 (5개: CPU, GPU, RAM, Storage, PSU) → 긴급 정지
└── 기본 리밋: CPU 95°C, GPU 90°C, 전력 300W
```

**스레드 안전성:**
- 모든 엔진의 `start()`/`stop()`은 `start_stop_mutex_`로 보호
- `is_running()`은 `std::atomic<bool>`로 락 프리 접근
- 메트릭 접근은 별도 `metrics_mutex_`로 보호

**소유권 모델:**
- `MainWindow`: sensorMgr_, safetyGuardian_ → `std::unique_ptr` (소유)
- 패널들: sensorMgr → raw pointer (비소유, 참조만)
- `MonitorPanel`: ownedSensorMgr_ → `std::unique_ptr` (자체 생성 시 소유)
- 패널 위젯: Qt parent-child 소유권 (raw pointer 정상)

## 플랫폼별 센서 데이터

| 센서 | Windows | macOS | Linux |
|------|---------|-------|-------|
| CPU 온도 | LHM > MSAcpi > PerfCounter > N/A | ThermalState 추정 | sysfs hwmon |
| CPU 전력 | LHM > TDP×usage% (estimated) | 배터리 V×A | sysfs power |
| CPU 사용률 | GetSystemTimes | Mach host_processor_info | /proc/stat |
| CPU 주파수 | PDH dynamic (% Processor Performance × base) | sysctl hw.cpufrequency | sysfs cpufreq |
| GPU 온도 | NVML/ADL | IOHIDSensor | sysfs amdgpu |
| 메모리 | GlobalMemoryStatusEx | kern.memorystatus_level | /proc/meminfo |
| 팬 RPM | WMI Win32_Fan | - | sysfs hwmon |
| WHEA | WEVTAPI | - | - |

## 한국어 UI

- 전체 14개 패널 파일에 한국어 문자열 적용
- CLI 도움말 텍스트도 한국어
- 주요 번역: 대시보드("대시보드"), CPU("CPU 스트레스 테스트"), GPU, RAM, Storage, PSU, 모니터, 벤치마크, 스케줄, 인증서, 시스템정보, 결과 등

## GUI 추가 기능

- **시스템 트레이**: QSystemTrayIcon (MainWindow)
- **키보드 단축키**: Ctrl+1~6 (패널 전환), Escape (긴급 정지 → `stopAllTests()`)
- **사운드 알림**: `playTestCompleteSound()`, `playTestErrorSound()`
- **RAM 패널**: 직접 MB 지정 모드 (QCheckBox + QSpinBox)
- **Storage 패널**: Duration 콤보 추가

## 스토리지 엔진 유니코드 지원

- `CreateFileA` → `CreateFileW`, `DeleteFileA` → `DeleteFileW`
- `utf8_to_wide()` 헬퍼 함수로 UTF-8 경로를 와이드 문자열로 변환
- `seq_read`/`seq_write`: duration > 0일 때 seek-to-start로 반복 루프
- 파일 로깅 → `logs/storage_engine.log`

## lhm-sensor-reader (C# 헬퍼)

- 위치: `tools/lhm-sensor-reader/`
- `JsonSerializerContext` 소스 생성기 사용 (트리밍 호환)
- Self-contained 단일 파일 게시 (~15-20MB)
- CI 워크플로우(`build.yml`, `build-windows.yml`, `gui-smoke-test.yml`)에서 `dotnet publish`로 빌드

## 파일 로깅

- `lhm_bridge.cpp` → `logs/lhm_bridge.log`
- `storage_engine.cpp` → `logs/storage_engine.log`
- `file_logger.h/cpp` 유틸리티 사용 (5MB 로테이션)

## 파일 수정 시 체크리스트

1. **엔진 수정** → CLI 출력 형식과 GUI 표시 일관성 확인
2. **엔진 추가** → IEngine 상속 필수, SafetyGuardian 등록 (GUI + CLI 양쪽)
3. **센서 수정** → 3개 플랫폼 모두 컴파일 가능한지 확인 (#ifdef 가드)
4. **GUI 패널 수정** → SensorManager 폴백 패턴 유지, panel_styles.h 공용 상수 활용
5. **모드 추가** → enum + CLI 매핑 + GUI 콤보 + 스케줄러 문자열 + 프리셋
6. **CI 관련** → windows-test.yml 테스트 스텝 의존성 확인
7. **빌드 옵션** → CMakeLists.txt + CI yml 양쪽 반영
8. **스레드 안전** → start()/stop()은 start_stop_mutex_ 보호 패턴 유지
