# setup-build.ps1 - AgentGateway Build Setup Script for Windows
# This script checks dependencies and configures the build environment

param(
    [string]$BuildType = "Release",
    [switch]$SkipVcpkgCheck = $false
)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "AgentGateway Build Setup (Windows)" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# Check for CMake
Write-Host "`nChecking for CMake..." -ForegroundColor Yellow
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    Write-Host "ERROR: CMake not found. Please install CMake 3.20 or later." -ForegroundColor Red
    exit 1
}
$cmakeVersion = & cmake --version | Select-String -Pattern "\d+\.\d+\.\d+" | ForEach-Object { $_.Matches.Value }
Write-Host "  Found CMake $cmakeVersion" -ForegroundColor Green

# Check for vcpkg
if (-not $SkipVcpkgCheck) {
    Write-Host "`nChecking for vcpkg..." -ForegroundColor Yellow
    if (-not $env:VCPKG_ROOT) {
        Write-Host "WARNING: VCPKG_ROOT environment variable not set." -ForegroundColor Yellow
        Write-Host "Please install vcpkg and set VCPKG_ROOT, or use -SkipVcpkgCheck flag." -ForegroundColor Yellow
        Write-Host "`nTo install vcpkg:" -ForegroundColor Cyan
        Write-Host "  git clone https://github.com/Microsoft/vcpkg.git" -ForegroundColor White
        Write-Host "  cd vcpkg" -ForegroundColor White
        Write-Host "  .\bootstrap-vcpkg.bat" -ForegroundColor White
        Write-Host "  `$env:VCPKG_ROOT = (Get-Location).Path" -ForegroundColor White
        exit 1
    }
    Write-Host "  Found vcpkg at: $env:VCPKG_ROOT" -ForegroundColor Green
} else {
    Write-Host "`nSkipping vcpkg check (manual dependency management)" -ForegroundColor Yellow
}

# Check for MSVC
Write-Host "`nChecking for MSVC compiler..." -ForegroundColor Yellow
$cl = Get-Command cl -ErrorAction SilentlyContinue
if (-not $cl) {
    Write-Host "WARNING: MSVC compiler (cl.exe) not found in PATH." -ForegroundColor Yellow
    Write-Host "Please run this script from a Visual Studio Developer Command Prompt." -ForegroundColor Yellow
} else {
    Write-Host "  Found MSVC compiler" -ForegroundColor Green
}

# Check for Ninja (optional but recommended)
Write-Host "`nChecking for Ninja build system..." -ForegroundColor Yellow
$ninja = Get-Command ninja -ErrorAction SilentlyContinue
if (-not $ninja) {
    Write-Host "  Ninja not found (optional, will use default generator)" -ForegroundColor Yellow
} else {
    Write-Host "  Found Ninja" -ForegroundColor Green
}

# Determine preset
$preset = "windows-msvc-$($BuildType.ToLower())"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Configuring build with preset: $preset" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# Configure
Write-Host "`nRunning CMake configure..." -ForegroundColor Yellow
& cmake --preset $preset

if ($LASTEXITCODE -ne 0) {
    Write-Host "`nERROR: CMake configuration failed!" -ForegroundColor Red
    exit 1
}

Write-Host "`n========================================" -ForegroundColor Green
Write-Host "Build environment configured successfully!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green

Write-Host "`nTo build the project, run:" -ForegroundColor Cyan
Write-Host "  cmake --build --preset $preset" -ForegroundColor White

Write-Host "`nTo run tests, run:" -ForegroundColor Cyan
Write-Host "  ctest --preset $preset" -ForegroundColor White
