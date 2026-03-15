---
paths:
  - "src/monitor/**/*"
---

# 센서 수정 규칙

- `#ifdef Q_OS_WIN` / `Q_OS_MACOS` / `Q_OS_LINUX` 가드 필수
- 3개 플랫폼 모두 컴파일 가능 확인
- Windows: WMI COM 캐싱, GetSystemTimes, NVML, ADL2 (GPU 온도/팬/전력)
- macOS: NSProcessInfo, IOKit Battery, Mach host_processor_info
- Linux: sysfs hwmon, /proc/stat
- LHM Bridge: `lhm-sensor-reader.exe` 헬퍼 (30s 타임아웃, 5회 재시도, `logs/lhm_bridge.log` 자동 로깅)
- ADL: `atiadlxx.dll` 동적 로딩, ADL2 함수 resolve (3개 온도 함수명 순차 시도)
- 편의 메서드: `get_fan_speeds()`, `get_voltages()` (unit 기반 필터링)
