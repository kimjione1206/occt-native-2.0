#!/usr/bin/env bash
set -euo pipefail

echo "================================================================"
echo "  OCCT Native Stress Test - Linux Build Script"
echo "================================================================"
echo ""

# ─── 변수 ────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_TYPE="${1:-Release}"
BUILD_DIR="$PROJECT_DIR/build/linux-${BUILD_TYPE,,}"

# ─── 의존성 확인 ─────────────────────────────────────────────────────────
echo "[1/4] 빌드 도구 확인 중..."

check_command() {
    if ! command -v "$1" &> /dev/null; then
        echo "[ERROR] '$1'이(가) 설치되지 않았습니다."
        echo "        sudo apt install $2"
        exit 1
    fi
}

check_command cmake cmake
check_command ninja-build ninja-build 2>/dev/null || check_command ninja ninja-build

# Qt6 확인
if ! pkg-config --exists Qt6Widgets 2>/dev/null && ! find /usr -name "Qt6WidgetsConfig.cmake" 2>/dev/null | head -1 | grep -q .; then
    echo "[WARNING] Qt6 개발 패키지가 감지되지 않았습니다."
    echo "          sudo apt install qt6-base-dev libqt6charts6-dev"
fi

echo "  cmake: $(cmake --version | head -1)"
echo "  컴파일러: $(g++ --version | head -1)"

# ─── CMake 구성 ──────────────────────────────────────────────────────────
echo ""
echo "[2/4] CMake 구성 중... (Build Type: $BUILD_TYPE)"

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# ─── 빌드 ───────────────────────────────────────────────────────────────
echo ""
echo "[3/4] 빌드 중..."

NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
cmake --build "$BUILD_DIR" --parallel "$NPROC"

# ─── 완료 ───────────────────────────────────────────────────────────────
echo ""
echo "[4/4] 빌드 완료!"
echo "  실행 파일: $BUILD_DIR/occt_native"
echo ""
