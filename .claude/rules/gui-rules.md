---
paths:
  - "src/gui/**/*"
---

# GUI 패널 수정 규칙

- SensorManager 폴백 패턴 유지 (null 체크 후 기본값)
- `panel_styles.h` 공용 상수 활용 (kErrorText, kWarningBanner 등)
- 새 패널은 `engine()` getter 제공 (SafetyGuardian 등록용)
- Qt parent-child 소유권 (raw pointer 정상)
- MainWindow: sensorMgr_, safetyGuardian_ → `std::unique_ptr`
