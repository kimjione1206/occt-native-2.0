# ============================================================================
# OCCT Native Stress Test - Portable Package Script (Windows)
# ============================================================================
# Creates a self-contained portable ZIP with all dependencies.
#
# Usage:
#   .\package-portable.ps1
#   .\package-portable.ps1 -BuildDir build -BuildType Release
# ============================================================================

param(
    [string]$BuildDir = "build",
    [string]$BuildType = "Release"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ── Helper: print step banner ──────────────────────────────────────────────
function Write-Step {
    param([string]$Step, [string]$Message)
    Write-Host ""
    Write-Host "[$Step] $Message" -ForegroundColor Cyan
}

# ── Locate project root (relative to script location) ─────────────────────
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir

# Resolve BuildDir to absolute path if relative
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $ProjectDir $BuildDir
}

# ── Read version from CMakeLists.txt ───────────────────────────────────────
Write-Step "1/7" "Reading version from CMakeLists.txt..."

$CMakeFile = Join-Path $ProjectDir "CMakeLists.txt"
if (-not (Test-Path $CMakeFile)) {
    Write-Error "CMakeLists.txt not found at: $CMakeFile"
    exit 1
}

$cmakeContent = Get-Content $CMakeFile -Raw
$vMajor = if ($cmakeContent -match 'set\(OCCT_VERSION_MAJOR\s+(\d+)\)') { $Matches[1] } else { "0" }
$vMinor = if ($cmakeContent -match 'set\(OCCT_VERSION_MINOR\s+(\d+)\)') { $Matches[1] } else { "0" }
$vPatch = if ($cmakeContent -match 'set\(OCCT_VERSION_PATCH\s+(\d+)\)') { $Matches[1] } else { "0" }
$Version = "$vMajor.$vMinor.$vPatch"

Write-Host "  Version: $Version"

# ── Verify build output exists ─────────────────────────────────────────────
Write-Step "2/7" "Verifying build output..."

$ExePath = Join-Path $BuildDir "$BuildType\occt_native.exe"
if (-not (Test-Path $ExePath)) {
    # Try flat layout (Ninja/single-config)
    $ExePath = Join-Path $BuildDir "occt_native.exe"
}
if (-not (Test-Path $ExePath)) {
    Write-Error "Build output not found. Expected: $BuildDir\$BuildType\occt_native.exe"
    Write-Error "Did you build the project first? Run: cmake --build $BuildDir --config $BuildType"
    exit 1
}

Write-Host "  Found: $ExePath"

# ── Prepare staging directory ──────────────────────────────────────────────
Write-Step "3/7" "Preparing staging directory..."

$StagingDir = Join-Path $BuildDir "portable-staging"
$AppName = "OCCT-StressTest"

# Clean previous staging
if (Test-Path $StagingDir) {
    Remove-Item $StagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null

# Copy the executable
Copy-Item $ExePath $StagingDir

# Copy license files
$licenseFiles = @("LICENSE", "THIRD_PARTY_LICENSES.txt")
foreach ($lf in $licenseFiles) {
    $lfPath = Join-Path $ProjectDir $lf
    if (Test-Path $lfPath) {
        Copy-Item $lfPath $StagingDir
        Write-Host "  Copied: $lf"
    }
}

# Copy LHM sensor reader helper if it exists
$lhmExe = Join-Path $BuildDir "$BuildType\tools\lhm-sensor-reader.exe"
if (Test-Path $lhmExe) {
    $toolsDir = Join-Path $StagingDir "tools"
    New-Item -ItemType Directory -Path $toolsDir -Force | Out-Null
    Copy-Item -Path $lhmExe -Destination "$toolsDir/" -Force
    Write-Host "  Copied: lhm-sensor-reader.exe -> tools/"
} else {
    Write-Host "  Note: lhm-sensor-reader.exe not found, skipping."
}

Write-Host "  Staging: $StagingDir"

# ── Run windeployqt ────────────────────────────────────────────────────────
Write-Step "4/7" "Running windeployqt to gather Qt dependencies..."

$windeployqt = Get-Command windeployqt -ErrorAction SilentlyContinue
if (-not $windeployqt) {
    # Try to find in Qt installation
    $qtBinPaths = @(
        (Join-Path $env:QT_ROOT_DIR "bin" -ErrorAction SilentlyContinue),
        (Join-Path $env:Qt6_DIR "..\..\..\bin" -ErrorAction SilentlyContinue)
    ) | Where-Object { $_ -and (Test-Path $_) }

    foreach ($p in $qtBinPaths) {
        $candidate = Join-Path $p "windeployqt.exe"
        if (Test-Path $candidate) {
            $windeployqt = Get-Item $candidate
            break
        }
    }
}

if ($windeployqt) {
    $deployTarget = Join-Path $StagingDir "occt_native.exe"
    & $windeployqt.Source $deployTarget `
        --no-translations `
        --no-system-d3d-compiler `
        --no-opengl-sw `
        --no-compiler-runtime `
        --release
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "windeployqt returned non-zero exit code: $LASTEXITCODE"
    }
    Write-Host "  windeployqt completed."
} else {
    Write-Warning "windeployqt not found. Qt DLLs must be manually copied."
}

# ── Clean up unnecessary Qt files ──────────────────────────────────────────
Write-Step "5/7" "Cleaning up unnecessary files..."

$removePatterns = @(
    "translations",           # Translation files not needed for portable
    "imageformats\qgif.dll",  # Unused image format plugins
    "imageformats\qtga.dll",
    "imageformats\qtiff.dll",
    "imageformats\qwbmp.dll",
    "imageformats\qwebp.dll",
    "D3Dcompiler_*.dll",
    "opengl32sw.dll"
)

$removedCount = 0
foreach ($pattern in $removePatterns) {
    $target = Join-Path $StagingDir $pattern
    if (Test-Path $target) {
        Remove-Item $target -Recurse -Force
        $removedCount++
        Write-Host "  Removed: $pattern"
    }
}
Write-Host "  Cleaned $removedCount items."

# ── Create portable marker and directory structure ─────────────────────────
Write-Step "6/7" "Creating portable structure..."

# Create subdirectories for portable mode
$configDir = Join-Path $StagingDir "config"
$logsDir = Join-Path $StagingDir "logs"
New-Item -ItemType Directory -Path $configDir -Force | Out-Null
New-Item -ItemType Directory -Path $logsDir -Force | Out-Null

# Create portable.ini marker file
$portableIni = Join-Path $StagingDir "portable.ini"
$buildDate = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
@"
[Portable]
Version=$Version
BuildDate=$buildDate
BuildType=$BuildType
Architecture=x64

[Paths]
ConfigDir=config
LogsDir=logs
"@ | Set-Content -Path $portableIni -Encoding UTF8

Write-Host "  Created: portable.ini"
Write-Host "  Created: config/"
Write-Host "  Created: logs/"

# ── Create ZIP and checksum ────────────────────────────────────────────────
Write-Step "7/7" "Creating portable ZIP archive..."

$DistDir = Join-Path $ProjectDir "dist"
if (-not (Test-Path $DistDir)) {
    New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
}

$ZipName = "$AppName-v$Version-win64-portable.zip"
$ZipPath = Join-Path $DistDir $ZipName

# Remove old zip if it exists
if (Test-Path $ZipPath) {
    Remove-Item $ZipPath -Force
}

# Create ZIP archive
Compress-Archive -Path "$StagingDir\*" -DestinationPath $ZipPath -CompressionLevel Optimal

if (-not (Test-Path $ZipPath)) {
    Write-Error "Failed to create ZIP archive"
    exit 1
}

# Generate SHA256 checksum
$hash = (Get-FileHash -Path $ZipPath -Algorithm SHA256).Hash.ToLower()
$checksumFile = Join-Path $DistDir "$ZipName.sha256"
"$hash  $ZipName" | Set-Content -Path $checksumFile -Encoding ASCII

$zipSize = [math]::Round((Get-Item $ZipPath).Length / 1MB, 2)

# ── Summary ────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "================================================================" -ForegroundColor Green
Write-Host "  Portable package created successfully!" -ForegroundColor Green
Write-Host "================================================================" -ForegroundColor Green
Write-Host ""
Write-Host "  Archive:  $ZipPath"
Write-Host "  Size:     $zipSize MB"
Write-Host "  SHA256:   $hash"
Write-Host "  Checksum: $checksumFile"
Write-Host ""
