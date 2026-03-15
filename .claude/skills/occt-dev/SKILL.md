---
name: occt-dev-2.0
description: >
  OCCT Native 2.0 프로젝트 전체 코드 구조를 주입하는 배경지식 스킬.
  v1.0의 모든 기능 + 자동 업데이트(UpdateChecker, Downloader, Installer),
  로그 전송(LogUploader, PostUpdateRunner), Qt6::Network 연동 포함.
  코드 수정, 기능 추가, 버그 수정, 리팩토링, 엔진 변경, 패널 수정,
  모드 추가, CLI 변경, GUI 변경, 센서 수정, 빌드 설정 변경,
  업데이터 수정, 로그 전송 수정 등 모든 개발 작업 시 자동 로드.
  Use when modifying any source code in the occt-native-2.0 project.
user-invocable: false
allowed-tools: Read, Grep, Glob, Bash, Edit, Write
---

# OCCT Native 2.0 코드 구조 컨텍스트

## 현재 코드 구조
!`cat docs/CODE_STRUCTURE.md`

## 현재 변경 상태
!`git diff --stat 2>/dev/null || echo "변경 없음"`

---

## 규칙 요약

위 코드 구조를 참고하여 영향 범위를 분석한 후 작업을 진행하세요.

**모드/기능 추가 시 반드시 5곳 수정:**
1. enum — `src/engines/<type>_engine.h`
2. CLI — `src/cli/cli_args.cpp`
3. GUI — `src/gui/panels/<type>_panel.cpp`
4. 스케줄러 — `src/scheduler/test_scheduler.cpp`
5. 프리셋 — `src/scheduler/preset_schedules.cpp`

**업데이터 수정 시 추가 체크:**
1. `src/updater/` 파일 수정 → `docs/CODE_STRUCTURE.md` 업데이터 섹션 업데이트
2. 로그 전송 형식 변경 → LogUploader Gist 페이로드 확인
3. 업데이트 흐름 변경 → UpdateDialog 단계별 UI 동기화
4. MainWindow의 `onTestStopped()` → 로그 전송 트리거 B 동작 확인

**로그 전송 트리거:**
- 트리거 A: `--post-update` → PostUpdateRunner → LogUploader (자동)
- 트리거 B: 테스트 중지 (수동/자동) → `emit testStopRequested()` → MainWindow::onTestStopped() → LogUploader

**작업 완료 시:**
1. 파일 추가/삭제/구조 변경 → `docs/CODE_STRUCTURE.md` 업데이트
2. CI yml과의 정합성 확인
