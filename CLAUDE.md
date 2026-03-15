# OCCT Native 2.0 프로젝트 규칙

> 모든 대화에서 자동 로드. 코드 수정 시 반드시 따를 것.
> **v2.0**: 자동 업데이트 + 로그 전송 파이프라인 포함 (occt-native v1.0 기반)

## 프로젝트 개요

- **언어**: C++17 + Qt6 | **빌드**: CMake 3.21+ | **주 타겟**: Windows
- **목적**: 하드웨어 스트레스 테스트 + 오류 검출 (단순 벤치마크 X)
- **실행 모드**: GUI (기본) / CLI (`--cli`) / Post-Update (`--post-update`)
- **코드 구조 레퍼런스**: `docs/CODE_STRUCTURE.md` — 대규모 수정 전 반드시 참고
- **GitHub**: `kimjione1206/occt-native-2.0` (Public)
- **로그 레포**: `kimjione1206/occt-native-logs` (Private)

## v1.0과의 차이점 (2.0 전용)

| 기능 | v1.0 | v2.0 |
|------|------|------|
| 자동 업데이트 | X | UpdateChecker + Downloader + Installer |
| 로그 전송 | X | GitHub Secret Gist (LogUploader) |
| 업데이트 후 테스트 | X | PostUpdateRunner (스모크 테스트) |
| Qt6::Network | X | O (업데이트/API용) |
| Release 형식 | 기본 | Internal Build 명시 + latest.json |

## 핵심 원칙

| 원칙 | 내용 |
|------|------|
| **Windows 우선** | 주 타겟. macOS/Linux는 `#ifdef Q_OS_WIN/MACOS/LINUX` 분기 |
| **오류 검출 목적** | CPU: FMA 비트비교, GPU: Artifact, RAM: 패턴비교, Storage: CRC32C |
| **CI가 빌드/테스트** | GitHub 푸시 → Actions 자동 빌드+51개 테스트 |
| **업데이트 자동화** | Release → 앱 감지 → 다운로드 → SHA256 검증 → 교체 → 재시작 → 스모크 테스트 → Gist 전송 |

## 모드/기능 추가 시 반드시 5곳 수정

1. **enum** — `src/engines/<type>_engine.h`
2. **CLI** — `src/cli/cli_args.cpp`
3. **GUI** — `src/gui/panels/<type>_panel.cpp`
4. **스케줄러** — `src/scheduler/test_scheduler.cpp`
5. **프리셋** — `src/scheduler/preset_schedules.cpp`

## 업데이터 수정 시 추가 체크

1. `src/updater/` 파일 수정 → `docs/CODE_STRUCTURE.md` 업데이터 섹션 업데이트
2. 로그 전송 형식 변경 → LogUploader의 Gist 페이로드 구조 확인
3. 업데이트 흐름 변경 → UpdateDialog 단계별 UI 동기화
4. GitHub API 변경 → UpdateChecker의 엔드포인트/파싱 확인

## 로그 전송 트리거 (2가지)

| 트리거 | 발동 시점 | 동작 |
|--------|----------|------|
| **A. 업데이트 후** | `--post-update`로 재시작 시 | CPU 30초 스모크 → 자동 Gist |
| **B. 수동 중지** | 중지 버튼 또는 duration 만료 | 해당 테스트 결과 → 자동 Gist |

## 작업 완료 시 (필수)

1. 파일 추가/삭제/구조 변경 → **`docs/CODE_STRUCTURE.md` 업데이트**
2. CI yml과의 정합성 확인
3. CLI 종료코드: `0`=PASS, `1`=FAIL, `2`=ERROR
