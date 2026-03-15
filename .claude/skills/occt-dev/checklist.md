# OCCT Native 2.0 수정 체크리스트

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
- [ ] 테스트 중지 시 `emit testStopRequested()` 확인 (로그 전송 트리거 B)

## 업데이터 수정 (2.0 전용)

- [ ] UpdateChecker: GitHub API 엔드포인트 및 파싱 로직 확인
- [ ] UpdateDownloader: SHA256 검증 로직 유지
- [ ] UpdateDialog: 단계별 UI 전환 (알림 → 진행 → 카운트다운)
- [ ] UpdateInstaller: Windows bat 스크립트 / macOS-Linux 직접 복사 분기
- [ ] LogUploader: Gist 토큰 우선순위 (setToken > 환경변수 > AppConfig)
- [ ] PostUpdateRunner: 스모크 테스트 설정 (CPU AUTO, 반 스레드, 30초)
- [ ] MainWindow: `onTestStopped()` 슬롯에서 메트릭 수집 + LogUploader 호출

## 로그 전송 수정 (2.0 전용)

- [ ] 트리거 A (post-update): PostUpdateRunner → LogUploader 연결 확인
- [ ] 트리거 B (수동 중지): 5개 패널 `testStopRequested()` → MainWindow 연결 확인
- [ ] Gist 페이로드: test_results.json + system_info.json + app.log 3개 파일
- [ ] 토큰 없을 때 graceful skip (업로드 안 함, 에러 없음)

## CI/빌드

- [ ] CMake 옵션 변경 → CI yml 양쪽 반영
- [ ] `windows-test.yml`: 테스트 스텝 의존성 확인
- [ ] `build-windows.yml`: latest.json 생성 + Internal Build 명시
- [ ] 필수 옵션: `OCCT_PORTABLE=ON`, `OCCT_CONSOLE=ON`
- [ ] Qt6::Network 의존성 확인 (CMakeLists.txt, gui/CMakeLists.txt, updater/CMakeLists.txt)

## 소유권 규칙

- [ ] MainWindow: sensorMgr_, safetyGuardian_ → `unique_ptr`
- [ ] MainWindow: updateChecker_, logUploader_ → raw pointer (Qt parent 소유)
- [ ] 패널 → 엔진 `unique_ptr` 소유, 외부에 raw pointer 전달
- [ ] Qt 위젯 → parent-child 소유권 (raw pointer 정상)

## UI 문자열 수정

- [ ] 패널 문자열 변경 시 `gui-smoke-test.yml` assertion 라벨도 한글로 업데이트
- [ ] 사이드바 버튼명 변경 시 `Click-SidebarPanel` 인자도 업데이트
- [ ] 버튼명 변경 시 `Click-ButtonByName` 인자도 업데이트

## 작업 완료

- [ ] `docs/CODE_STRUCTURE.md` 업데이트 (파일 추가/삭제/구조 변경 시)
- [ ] CLI 종료코드 확인: `0`=PASS, `1`=FAIL, `2`=ERROR
