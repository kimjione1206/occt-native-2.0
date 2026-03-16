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

## LHM Bridge (Windows)

- **Win32 API 사용** (QProcess 금지 — std::thread에서 Qt thread affinity 문제)
- CreateProcess + CreatePipe + PeekNamedPipe + ReadFile로 헬퍼 관리
- 헬퍼: `lhm-sensor-reader.exe --loop 500` 상주 프로세스 (500ms 간격 JSON Lines 출력)
- 첫 데이터 대기: 최대 15초 (1초 간격 PeekNamedPipe 폴링)
- 프로세스 죽음 시 백오프 재시도 (5회 → 30초, 10회 → 60초, 20회 → 120초)
- 영구 비활성화 없음 — 항상 재시도
- 로그: `logs/lhm_bridge.log`

## 스레딩 제약 (중요)

- sensor_manager의 poll_thread는 `std::thread` (Qt 이벤트 루프 없음)
- **QProcess를 std::thread에서 사용 금지** — waitForReadyRead()가 즉시 반환됨
- LHM bridge는 Win32 API만 사용하여 스레드 안전성 보장
- sensor_manager.cpp의 구조 변경 금지 (CI Monitor 테스트 hang 위험)

## 기타

- ADL: `atiadlxx.dll` 동적 로딩, ADL2 함수 resolve (3개 온도 함수명 순차 시도)
- 편의 메서드: `get_fan_speeds()`, `get_voltages()` (unit 기반 필터링)
- 에러 시 update_reading(0) 호출 (stale 값 방지)
- LHM stale 감지: 20회 연속 동일값 시 경고 (10초@500ms)
