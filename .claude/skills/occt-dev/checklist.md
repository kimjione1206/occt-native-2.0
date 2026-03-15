# OCCT Native 수정 체크리스트

## 엔진 수정

- [ ] CLI 출력 형식과 GUI 표시 일관성 확인
- [ ] 새 엔진은 `IEngine` 상속 필수
- [ ] SafetyGuardian 등록 (GUI: `main_window.cpp`, CLI: `cli_runner.cpp`)
- [ ] `start()`/`stop()`에 `start_stop_mutex_` 보호 패턴 적용
- [ ] 메트릭 접근은 `metrics_mutex_` 보호

## 모드 추가

- [ ] enum 정의 — `src/engines/<type>_engine.h`
- [ ] CLI 매핑 — `src/cli/cli_args.cpp`
- [ ] GUI 콤보박스 — `src/gui/panels/<type>_panel.cpp`
- [ ] 스케줄러 매핑 — `src/scheduler/test_scheduler.cpp`
- [ ] 프리셋 반영 — `src/scheduler/preset_schedules.cpp`
- [ ] CLI `--mode` 옵션과 GUI 콤보박스 1:1 대응 확인
- [ ] `windows-test.yml`에 테스트 스텝 추가

## 센서 수정

- [ ] `#ifdef Q_OS_WIN` / `Q_OS_MACOS` / `Q_OS_LINUX` 가드 사용
- [ ] 3개 플랫폼 모두 컴파일 가능 확인
- [ ] Windows: WMI, GetSystemTimes, NVML/ADL
- [ ] macOS: NSProcessInfo, IOKit Battery, Mach host_processor_info
- [ ] Linux: sysfs hwmon, /proc/stat

## GUI 패널 수정

- [ ] SensorManager 폴백 패턴 유지
- [ ] `panel_styles.h` 공용 상수 활용
- [ ] 새 패널은 `engine()` getter 제공 (SafetyGuardian 등록용)

## CI/빌드

- [ ] CMake 옵션 변경 → CI yml 양쪽 반영
- [ ] `windows-test.yml`: 테스트 스텝 의존성 확인
- [ ] `build-windows.yml`: 빌드 옵션 확인
- [ ] 필수 옵션: `OCCT_PORTABLE=ON`, `OCCT_CONSOLE=ON`

## 소유권 규칙

- [ ] MainWindow: sensorMgr_, safetyGuardian_ → `unique_ptr`
- [ ] 패널 → 엔진 `unique_ptr` 소유, 외부에 raw pointer 전달
- [ ] Qt 위젯 → parent-child 소유권 (raw pointer 정상)

## UI 문자열 수정

- [ ] 패널 문자열 변경 시 `gui-smoke-test.yml` assertion 라벨도 한글로 업데이트
- [ ] 사이드바 버튼명 변경 시 `Click-SidebarPanel` 인자도 업데이트
- [ ] 버튼명 변경 시 `Click-ButtonByName` 인자도 업데이트

## 작업 완료

- [ ] `docs/CODE_STRUCTURE.md` 업데이트 (파일 추가/삭제/구조 변경 시)
- [ ] CLI 종료코드 확인: `0`=PASS, `1`=FAIL, `2`=ERROR
