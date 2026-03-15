---
name: occt-dev
description: >
  OCCT Native 프로젝트 전체 코드 구조를 주입하는 배경지식 스킬.
  코드 수정, 기능 추가, 버그 수정, 리팩토링, 엔진 변경, 패널 수정,
  모드 추가, CLI 변경, GUI 변경, 센서 수정, 빌드 설정 변경 등
  모든 개발 작업 시 자동으로 CODE_STRUCTURE.md를 로드하여 영향 범위를 파악.
  Use when modifying any source code, adding features, fixing bugs,
  or changing build configuration in the occt-native project.
user-invocable: false
allowed-tools: Read, Grep, Glob, Bash, Edit, Write
---

# OCCT Native 코드 구조 컨텍스트

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

**작업 완료 시:**
1. 파일 추가/삭제/구조 변경 → `docs/CODE_STRUCTURE.md` 업데이트
2. CI yml과의 정합성 확인
