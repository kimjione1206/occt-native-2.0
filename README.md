# OCCT Native Stress Test

OCCT 스타일 하드웨어 스트레스 테스트 도구의 네이티브 C++ 구현.

## 주요 기능

- **CPU 스트레스 테스트**: AVX2/AVX-512 SIMD 연산, Linpack, Prime95 스타일 부하
- **GPU 스트레스 테스트**: OpenCL/Vulkan 컴퓨트 셰이더 기반 부하
- **RAM 테스트**: 메모리 패턴 검증, 대역폭 측정
- **Storage 테스트**: 순차/랜덤 읽기-쓰기 벤치마크
- **실시간 모니터링**: 온도, 전력, 클럭, 사용률 그래프
- **안전 시스템**: 자동 온도 감지, 임계치 초과 시 자동 중단

## 의존성

| 패키지 | 버전 | 필수 |
|--------|------|------|
| CMake | >= 3.21 | O |
| Qt6 (Widgets, Charts) | >= 6.5 | O |
| C++17 컴파일러 | MSVC 2019+ / GCC 10+ / Clang 12+ | O |
| OpenCL | 1.2+ | X (선택) |
| Vulkan | 1.2+ | X (선택) |

## 빌드 방법

### Windows (MSVC + vcpkg)

```batch
# 1. vcpkg 설치 (이미 설치된 경우 건너뛰기)
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg && bootstrap-vcpkg.bat
set VCPKG_ROOT=%CD%

# 2. 의존성 설치
vcpkg install qt6-base:x64-windows qt6-charts:x64-windows

# 3. 빌드
cmake --preset windows-msvc-release
cmake --build build/windows-msvc-release --config Release

# 또는 자동화 스크립트 사용
scripts\build_windows.bat
```

### Linux (GCC)

```bash
# 1. 의존성 설치 (Ubuntu/Debian)
sudo apt install cmake ninja-build qt6-base-dev libqt6charts6-dev

# 2. 빌드
cmake --preset linux-gcc-release
cmake --build build/linux-gcc-release

# 또는 자동화 스크립트 사용
chmod +x scripts/build_linux.sh
./scripts/build_linux.sh
```

### macOS (Clang)

```bash
# 1. 의존성 설치
brew install cmake ninja qt@6

# 2. 빌드
cmake --preset macos-clang-release
cmake --build build/macos-clang-release
```

## CMake 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `OCCT_ENABLE_AVX512` | OFF | AVX-512 명령어 셋 지원 |
| `OCCT_ENABLE_OPENCL` | OFF | OpenCL GPU 스트레스 테스트 |
| `OCCT_ENABLE_VULKAN` | OFF | Vulkan 컴퓨트 스트레스 테스트 |

## 프로젝트 구조

```
occt-native/
  CMakeLists.txt            # 루트 CMake 빌드 시스템
  CMakePresets.json          # 빌드 프리셋 (Windows/Linux/macOS)
  vcpkg.json                 # vcpkg 의존성 매니페스트
  src/
    main.cpp                 # 애플리케이션 엔트리포인트
    config.h.in              # 빌드 시 config.h 생성
    engines/                 # 스트레스 테스트 엔진 (CPU/GPU/RAM/Storage)
    gui/                     # Qt6 GUI 컴포넌트
    monitor/                 # 하드웨어 모니터링
    safety/                  # 안전 시스템 (온도 감시, 자동 중단)
    utils/                   # 유틸리티 (로깅, 설정, 리포트)
  resources/
    occt_native.qrc          # Qt 리소스 파일
    styles/dark_theme.qss    # OCCT 다크 테마 스타일시트
    icons/                   # 아이콘 리소스
  scripts/                   # 빌드 자동화 스크립트
```

## 라이선스

Private - All rights reserved.
