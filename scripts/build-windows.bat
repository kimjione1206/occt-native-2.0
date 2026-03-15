@echo off
setlocal enabledelayedexpansion

:: ============================================================================
:: OCCT Native Stress Test - Windows Local Build Script
:: ============================================================================
:: Detects MSVC via vswhere, configures environment, builds the project,
:: and creates a portable ZIP package.
::
:: Usage:
::   build-windows.bat              (Release build)
::   build-windows.bat debug        (Debug build)
::   build-windows.bat release      (Release build, explicit)
:: ============================================================================

echo.
echo ================================================================
echo   OCCT Native Stress Test - Windows Build Script
echo ================================================================
echo.

:: ── Parse build type argument ─────────────────────────────────────────────
set BUILD_TYPE=Release
if /i "%~1"=="debug" set BUILD_TYPE=Debug
if /i "%~1"=="Debug" set BUILD_TYPE=Debug

:: ── Detect project root (script is in scripts/) ──────────────────────────
set "SCRIPT_DIR=%~dp0"
:: Remove trailing backslash
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
:: Go up one level to project root
for %%I in ("%SCRIPT_DIR%\..") do set "PROJECT_DIR=%%~fI"

set "BUILD_DIR=%PROJECT_DIR%\build"

:: ── Step 1: Detect MSVC via vswhere ───────────────────────────────────────
echo [1/5] Detecting Visual Studio installation...

:: Try standard vswhere location
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
)
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found. Is Visual Studio installed?
    echo         Download from: https://visualstudio.microsoft.com/
    exit /b 1
)

:: Find latest VS installation with C++ workload
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)

if not defined VS_PATH (
    echo [ERROR] No Visual Studio installation with C++ tools found.
    echo         Install "Desktop development with C++" workload.
    exit /b 1
)

echo   Found: %VS_PATH%

:: ── Step 2: Initialize MSVC environment ───────────────────────────────────
echo.
echo [2/5] Initializing MSVC x64 environment...

set "VCVARSALL=%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARSALL%" (
    echo [ERROR] vcvarsall.bat not found at: %VCVARSALL%
    exit /b 1
)

call "%VCVARSALL%" x64
if errorlevel 1 (
    echo [ERROR] Failed to initialize MSVC environment.
    exit /b 1
)

echo   MSVC environment initialized (x64).

:: ── Step 3: CMake configure ───────────────────────────────────────────────
echo.
echo [3/5] Configuring CMake... (Build Type: %BUILD_TYPE%)

:: Check if cmake is available
where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cmake not found in PATH.
    echo         Install CMake from https://cmake.org/download/
    exit /b 1
)

cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE%

if errorlevel 1 (
    echo [ERROR] CMake configuration failed.
    exit /b 1
)

:: ── Step 4: Build ─────────────────────────────────────────────────────────
echo.
echo [4/5] Building project...

cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --parallel
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo   Build succeeded.

:: ── Step 5: Package portable ZIP ──────────────────────────────────────────
echo.
echo [5/5] Creating portable package...

:: Check if PowerShell is available
where pwsh >nul 2>&1
if errorlevel 1 (
    where powershell >nul 2>&1
    if errorlevel 1 (
        echo [WARNING] PowerShell not found. Skipping portable packaging.
        echo           Run manually: pwsh scripts\package-portable.ps1
        goto :done
    )
    :: Use Windows PowerShell as fallback
    powershell -ExecutionPolicy Bypass -File "%PROJECT_DIR%\scripts\package-portable.ps1" -BuildDir "%BUILD_DIR%" -BuildType %BUILD_TYPE%
    goto :check_package
)

:: Use PowerShell Core (pwsh)
pwsh -ExecutionPolicy Bypass -File "%PROJECT_DIR%\scripts\package-portable.ps1" -BuildDir "%BUILD_DIR%" -BuildType %BUILD_TYPE%

:check_package
if errorlevel 1 (
    echo [WARNING] Portable packaging failed. The build itself succeeded.
    echo           You can find the executable at: %BUILD_DIR%\%BUILD_TYPE%\occt_native.exe
    goto :done
)

:done
echo.
echo ================================================================
echo   Build Complete!
echo ================================================================
echo.
echo   Build Type:  %BUILD_TYPE%
echo   Executable:  %BUILD_DIR%\%BUILD_TYPE%\occt_native.exe
echo   Portable:    %PROJECT_DIR%\dist\OCCT-StressTest-v*-win64-portable.zip
echo.

endlocal
