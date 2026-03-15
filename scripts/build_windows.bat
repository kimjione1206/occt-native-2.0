@echo off
setlocal enabledelayedexpansion

echo ================================================================
echo   OCCT Native Stress Test - Windows Build Script
echo ================================================================
echo.

:: ─── vcpkg 확인 ─────────────────────────────────────────────────────────
if "%VCPKG_ROOT%"=="" (
    echo [ERROR] VCPKG_ROOT 환경 변수가 설정되지 않았습니다.
    echo         set VCPKG_ROOT=C:\path\to\vcpkg
    exit /b 1
)

if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo [ERROR] vcpkg.exe를 찾을 수 없습니다: %VCPKG_ROOT%\vcpkg.exe
    exit /b 1
)

echo [1/4] vcpkg 의존성 설치 중...
"%VCPKG_ROOT%\vcpkg.exe" install qt6-base:x64-windows qt6-charts:x64-windows
if errorlevel 1 (
    echo [ERROR] vcpkg 패키지 설치 실패
    exit /b 1
)

:: ─── 빌드 타입 ──────────────────────────────────────────────────────────
set BUILD_TYPE=Release
if "%1"=="debug" set BUILD_TYPE=Debug
if "%1"=="Debug" set BUILD_TYPE=Debug

set BUILD_DIR=build\windows-%BUILD_TYPE%
set PRESET_NAME=windows-msvc-release
if "%BUILD_TYPE%"=="Debug" set PRESET_NAME=windows-msvc-debug

echo.
echo [2/4] CMake 구성 중... (Build Type: %BUILD_TYPE%)
cmake -S . -B "%BUILD_DIR%" ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if errorlevel 1 (
    echo [ERROR] CMake 구성 실패
    exit /b 1
)

echo.
echo [3/4] 빌드 중...
cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --parallel
if errorlevel 1 (
    echo [ERROR] 빌드 실패
    exit /b 1
)

echo.
echo [4/4] 빌드 완료!
echo   실행 파일: %BUILD_DIR%\%BUILD_TYPE%\occt_native.exe
echo.

endlocal
