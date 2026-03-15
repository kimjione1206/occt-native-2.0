---
paths:
  - ".github/**/*"
  - "CMakeLists.txt"
---

# CI/빌드 규칙

- CMake 옵션 변경 → CI yml 양쪽 반영
- `windows-test.yml`: 테스트 스텝 의존성 확인
- `build.yml`, `build-windows.yml`, `gui-smoke-test.yml`: .NET SDK + LHM 헬퍼 빌드 포함 필수
- 필수 옵션: `OCCT_PORTABLE=ON`, `OCCT_CONSOLE=ON` (CI용)
- GPU/PSU 테스트: exit code 2 허용 (하드웨어 없음)
- UI 문자열 변경 시 `gui-smoke-test.yml`의 assertion 라벨도 함께 업데이트 (한글 라벨 사용 중)
