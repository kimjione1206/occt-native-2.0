# OCCT Native 프로젝트 규칙

> 모든 대화에서 자동 로드. 코드 수정 시 반드시 따를 것.

## 프로젝트 개요

- **언어**: C++17 + Qt6 | **빌드**: CMake 3.21+ | **주 타겟**: Windows
- **목적**: 하드웨어 스트레스 테스트 + 오류 검출 (단순 벤치마크 X)
- **실행 모드**: GUI (기본) / CLI (`--cli`)
- **코드 구조 레퍼런스**: `docs/CODE_STRUCTURE.md` — 대규모 수정 전 반드시 참고

## 핵심 원칙

| 원칙 | 내용 |
|------|------|
| **Windows 우선** | 주 타겟. macOS/Linux는 `#ifdef Q_OS_WIN/MACOS/LINUX` 분기 |
| **오류 검출 목적** | CPU: FMA 비트비교, GPU: Artifact, RAM: 패턴비교, Storage: CRC32C |
| **CI가 빌드/테스트** | GitHub 푸시 → Actions 자동 빌드+51개 테스트 |

## 모드/기능 추가 시 반드시 5곳 수정

1. **enum** — `src/engines/<type>_engine.h`
2. **CLI** — `src/cli/cli_args.cpp`
3. **GUI** — `src/gui/panels/<type>_panel.cpp`
4. **스케줄러** — `src/scheduler/test_scheduler.cpp`
5. **프리셋** — `src/scheduler/preset_schedules.cpp`

## 작업 완료 시 (필수)

1. 파일 추가/삭제/구조 변경 → **`docs/CODE_STRUCTURE.md` 업데이트**
2. CI yml과의 정합성 확인
3. CLI 종료코드: `0`=PASS, `1`=FAIL, `2`=ERROR
