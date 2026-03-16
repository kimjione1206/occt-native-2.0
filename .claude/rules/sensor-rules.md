---
paths:
  - "src/monitor/**/*"
  - "tools/lhm-sensor-reader/**/*"
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
- 헬퍼: `tools/lhm/lhm-sensor-reader.exe --loop 500` 상주 프로세스 (폴더 배포, PublishSingleFile 금지)
- stdin: NUL 디바이스 전달 (INVALID_HANDLE_VALUE 금지 — C# stdin EOF 문제)
- stderr: 부모의 STD_ERROR_HANDLE (stdout 파이프와 분리 필수 — JSON 파싱 오류 방지)
- ReadFile 버퍼: 16384바이트, 가용 데이터 전부 읽기 (while PeekNamedPipe > 0)
- 첫 데이터 대기: 최대 15초 (1초 간격 PeekNamedPipe + WaitForSingleObject)
- 프로세스 죽음 시 백오프 재시도 (5회 → 30초, 10회 → 60초, 20회 → 120초)
- 영구 비활성화 없음 — 항상 재시도
- 로그: `logs/lhm_bridge.log`

## C# LHM 헬퍼 (Program.cs)

- Infinity/NaN 센서 값 필터링 (스킵)
- JsonNumberHandling.AllowNamedFloatingPointLiterals 안전장치
- stdin 모니터 사용 금지 (NUL stdin에서 즉시 EOF → 루프 종료)
- 부모 종료 감지: stdout IOException으로 처리
- **TODO**: computer.Accept(visitor) → 타입별 분리 Update() (빠른/느린 루프)

## 센서값 선택 우선순위

- `get_cpu_temperature()`: value > 0인 첫 번째 CPU/C 센서
- `get_cpu_power()`: LHM "Package" 센서(RAPL) 우선, 없으면 WMI TDP 추정
- `is_cpu_power_estimated()`: Package 센서 있으면 false

## 스레딩 제약 (중요)

- sensor_manager의 poll_thread는 `std::thread` (Qt 이벤트 루프 없음)
- **QProcess를 std::thread에서 사용 금지** — waitForReadyRead()가 즉시 반환됨
- LHM bridge는 Win32 API만 사용하여 스레드 안전성 보장
- sensor_manager.cpp의 구조 변경은 최소화 (CI Monitor 테스트 hang 위험)

## 기타

- ADL: `atiadlxx.dll` 동적 로딩, ADL2 함수 resolve (3개 온도 함수명 순차 시도)
- 편의 메서드: `get_fan_speeds()`, `get_voltages()` (unit 기반 필터링)
- 에러 시 update_reading(0) 호출 (stale 값 방지)
- LHM stale 감지: 20회 연속 동일값 시 경고 (10초@500ms)
- WMI 온도 센서 없을 때 0 저장 금지 (이전 값 보존)
