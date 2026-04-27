# build.ps1 - Windows build script for DaemonMap
#
# APPROACH: MSYS2 + MinGW-w64 (GCC toolchain)
#
# Why not Visual Studio / MSVC?
#   The codebase uses GCC flags (-fno-exceptions, -fno-rtti, -Wall, etc.),
#   GCC-based threading, and pkg-config for library discovery. MSYS2/MinGW
#   is a drop-in GCC environment that produces native Windows .exe files and
#   requires zero source changes.
#
# Requirements:
#   MSYS2 installed (https://www.msys2.org/) - free, ~500 MB
#   The script installs all other dependencies automatically.
#
# Usage:  .\build.ps1
# Output: install\daemonmap.exe  (plus required DLLs alongside it)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# -----------------------------------------------------------------------
# Locate MSYS2
# -----------------------------------------------------------------------

$msys2Candidates = @(
    "C:\msys64",
    "C:\msys2",
    "$env:SystemDrive\msys64",
    "$env:LOCALAPPDATA\msys64",
    "$env:USERPROFILE\msys64"
)

$Msys2Root = $null
foreach ($candidate in $msys2Candidates) {
    if (Test-Path "$candidate\usr\bin\bash.exe") {
        $Msys2Root = $candidate
        break
    }
}

if (-not $Msys2Root) {
    Write-Host ""
    Write-Host "MSYS2 not found." -ForegroundColor Red
    Write-Host ""
    Write-Host "DaemonMap builds on Windows using MSYS2 (MinGW-w64 / GCC)." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  1. Download and install MSYS2 from: https://www.msys2.org/" -ForegroundColor Cyan
    Write-Host "     Accept all defaults -- installs to C:\msys64"
    Write-Host ""
    Write-Host "  2. Run this script again -- it will install all build" -ForegroundColor Cyan
    Write-Host "     dependencies and compile automatically."
    Write-Host ""
    Write-Host "  Alternatively, open 'MSYS2 MinGW 64-bit' from the Start Menu"
    Write-Host "  and run:  ./build-msys2.sh"
    Write-Host ""
    exit 1
}

Write-Host ""
Write-Host "Found MSYS2 at: $Msys2Root" -ForegroundColor Green

$Bash   = "$Msys2Root\usr\bin\bash.exe"
$Pacman = "$Msys2Root\usr\bin\pacman.exe"

# -----------------------------------------------------------------------
# Install MinGW-w64 build dependencies via pacman
# -----------------------------------------------------------------------

Write-Host ""
Write-Host "Checking build dependencies..." -ForegroundColor Cyan

$Packages = @(
    "mingw-w64-x86_64-gcc",
    "mingw-w64-x86_64-cmake",
    "mingw-w64-x86_64-make",
    "mingw-w64-x86_64-pkgconf",
    "mingw-w64-x86_64-glib2",
    "mingw-w64-x86_64-libxml2",
    "mingw-w64-x86_64-minizip",
    "mingw-w64-x86_64-ntldd",
    "git"
)

& $Pacman -S --needed --noconfirm @Packages
if ($LASTEXITCODE -ne 0) {
    Write-Host "Failed to install dependencies via pacman!" -ForegroundColor Red
    Write-Host "Try opening MSYS2 and running 'pacman -Syu' first." -ForegroundColor Yellow
    exit 1
}

# -----------------------------------------------------------------------
# Clear any stale CMake cache from a previous wrong-generator run
# -----------------------------------------------------------------------

$BuildCache = Join-Path $ScriptDir "build\CMakeCache.txt"
if (Test-Path $BuildCache) {
    $CacheContent = Get-Content $BuildCache -Raw -ErrorAction SilentlyContinue
    if ($CacheContent -match "Visual Studio|MSVC|vcpkg") {
        Write-Host ""
        Write-Host "Removing stale Visual Studio CMake cache..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force (Join-Path $ScriptDir "build")
    }
}

# -----------------------------------------------------------------------
# Convert Windows path to MSYS2 Unix path  (C:\foo\bar -> /c/foo/bar)
# -----------------------------------------------------------------------

$DriveLetter     = $ScriptDir.Substring(0, 1).ToLower()
$PathRemainder   = $ScriptDir.Substring(2) -replace '\\', '/'
$UnixProjectPath = "/$DriveLetter$PathRemainder"
$UnixBuildPath   = "$UnixProjectPath/build"
$UnixInstallPath = "$UnixProjectPath/install"

# -----------------------------------------------------------------------
# Step 1: CMake configure
# -----------------------------------------------------------------------

Write-Host ""
Write-Host "Configuring..." -ForegroundColor Cyan

$ConfigCmd = @"
export MSYSTEM=MINGW64 && source /etc/profile &&
mkdir -p '$UnixBuildPath' &&
cd '$UnixBuildPath' &&
cmake '$UnixProjectPath' \
    -G 'Unix Makefiles' \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TOOLS=ON \
    -DBUNDLE_LIBRARIES=OFF \
    -DCMAKE_INSTALL_PREFIX='$UnixInstallPath'
"@

& $Bash -lc $ConfigCmd
if ($LASTEXITCODE -ne 0) {
    Write-Host "CMake configure failed!" -ForegroundColor Red
    exit 1
}

# -----------------------------------------------------------------------
# Step 2: Build
# -----------------------------------------------------------------------

Write-Host ""
Write-Host "Building..." -ForegroundColor Cyan

$CpuCount = [Environment]::ProcessorCount

$BuildCmd = @"
export MSYSTEM=MINGW64 && source /etc/profile &&
cd '$UnixBuildPath' &&
make -j$CpuCount
"@

& $Bash -lc $BuildCmd
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}

# -----------------------------------------------------------------------
# Step 3: Copy exe to install\ and bundle DLLs with ntldd
#
# We write the bash logic to a .sh file rather than passing it inline.
# This avoids PowerShell interpreting $(...) inside double-quoted strings.
# PowerShell variables ($UnixBuildPath etc.) are expanded when we build
# the string; bash variables use single-quoted sections so PS ignores them.
# -----------------------------------------------------------------------

Write-Host ""
Write-Host "Installing (statically linked -- no DLLs needed)..." -ForegroundColor Cyan

# Write the bundle script to a file so PowerShell never mis-parses bash syntax.
# $UnixBuildPath and $UnixInstallPath are PowerShell vars expanded here.
# Every other $ is protected inside single-quoted PS strings.
$BundleSh  = @()
$BundleSh += '#!/bin/bash'
$BundleSh += 'source /etc/profile'
$BundleSh += "EXE='$UnixBuildPath/daemonmap.exe'"
$BundleSh += "DEST='$UnixInstallPath'"
$BundleSh += 'MINGW_BIN=/mingw64/bin'
$BundleSh += 'mkdir -p "$DEST"'
$BundleSh += 'if [ ! -f "$EXE" ]; then echo "ERROR: daemonmap.exe not found at $EXE"; exit 1; fi'
$BundleSh += 'cp "$EXE" "$DEST/"'
$BundleSh += 'strip "$DEST/daemonmap.exe"'
$BundleSh += '# Copy the MinGW app-level DLLs daemonmap.exe depends on.'
$BundleSh += '# libgcc_s_seh-1.dll and libstdc++-6.dll are statically embedded.'
$BundleSh += '# The four below are app-level and not available as static .a in MSYS2.'
$BundleSh += 'echo "Bundling required DLLs:"'
$BundleSh += 'DLLS="libglib-2.0-0.dll libminizip-1.dll libwinpthread-1.dll libxml2-2.dll"'
$BundleSh += 'for dll in $DLLS; do'
$BundleSh += '    src="$MINGW_BIN/$dll"'
$BundleSh += '    if [ -f "$src" ]; then'
$BundleSh += '        echo "  $dll"'
$BundleSh += '        cp "$src" "$DEST/"'
$BundleSh += '    else'
$BundleSh += '        echo "  WARNING: $dll not found at $src"'
$BundleSh += '    fi'
$BundleSh += 'done'
$BundleSh += 'echo ""'
$BundleSh += 'echo "install/ contents:"'
$BundleSh += 'ls -lh "$DEST/"'

$BundleShWindows = Join-Path $ScriptDir "build\_bundle.sh"
[System.IO.File]::WriteAllText($BundleShWindows, ($BundleSh -join "`n") + "`n")

& $Bash -lc "export MSYSTEM=MINGW64 && source /etc/profile && bash '$UnixBuildPath/_bundle.sh'"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Install step failed!" -ForegroundColor Red
    Write-Host "The exe is at: $ScriptDir\build\daemonmap.exe" -ForegroundColor Yellow
    exit 1
}

# -----------------------------------------------------------------------
# Done
# -----------------------------------------------------------------------

Write-Host ""
Write-Host "Build complete!" -ForegroundColor Green
Write-Host ""
Write-Host "  Output:     $ScriptDir\install\" -ForegroundColor Cyan
Write-Host "  Executable: $ScriptDir\install\daemonmap.exe" -ForegroundColor Cyan
Write-Host ""
Write-Host "The install\ folder is self-contained -- copy it anywhere." -ForegroundColor White
Write-Host ""
