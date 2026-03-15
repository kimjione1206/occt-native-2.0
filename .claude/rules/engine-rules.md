---
paths:
  - "src/engines/**/*"
---

# 엔진 수정 규칙

- CLI 출력 형식과 GUI 표시 일관성 확인
- 새 엔진은 `IEngine` 상속 필수
- SafetyGuardian 등록: GUI(`main_window.cpp`), CLI(`cli_runner.cpp`) 양쪽
- `start()`/`stop()`에 `start_stop_mutex_` 보호 패턴 적용
- 메트릭 접근은 `metrics_mutex_` 보호
- 패널이 엔진 `unique_ptr` 소유, 외부에 raw pointer 전달
