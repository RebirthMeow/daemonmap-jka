# package-release.ps1 - Build a Windows release zip for GitHub Releases
#
# Produces: release\daemonmap-jka-<version>-windows.zip
#
# The zip contains:
#   - daemonmap.exe
#   - daemonmap-jka.bat  (drag-and-drop wrapper)
#   - all required DLLs that sit next to daemonmap.exe in install\
#   - README-RELEASE.txt  (short usage notes for non-technical users)
#
# USAGE:
#   .\package-release.ps1 v1.0.0
#
# The version string becomes part of the zip filename. Use anything you
# want (e.g. v1.0.0, jka-gold, 2026-04-27); GitHub doesn't care.
#
# NEXT STEP after running this:
#   1. Go to https://github.com/RebirthMeow/daemonmap-jka/releases/new
#   2. Pick a tag (matching the version arg is conventional)
#   3. Drag the produced .zip into the Release page's "Attach binaries" box
#   4. Hit "Publish release"
#   Done. Users go to /releases and click Download.

param(
    [Parameter(Mandatory = $true)]
    [string]$Version
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

$InstallDir = Join-Path $ScriptDir "install"
$ReleaseDir = Join-Path $ScriptDir "release"
$StagingDir = Join-Path $ReleaseDir "staging"

# --- Validate prerequisites ---

if (-not (Test-Path "$InstallDir\daemonmap.exe")) {
    Write-Host "ERROR: install\daemonmap.exe not found." -ForegroundColor Red
    Write-Host "Run .\build.ps1 first." -ForegroundColor Red
    exit 1
}

if (-not (Test-Path "$ScriptDir\daemonmap-jka.bat")) {
    Write-Host "ERROR: daemonmap-jka.bat not found at repo root." -ForegroundColor Red
    exit 1
}

# --- Clean and recreate staging ---

if (Test-Path $StagingDir) { Remove-Item -Recurse -Force $StagingDir }
New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null

$ReleaseName = "daemonmap-jka-$Version-windows"
$PayloadDir = Join-Path $StagingDir $ReleaseName
New-Item -ItemType Directory -Path $PayloadDir -Force | Out-Null

# --- Copy payload ---

Write-Host "Staging release: $ReleaseName" -ForegroundColor Green

# Everything from install\ (exe + DLLs + any data files the build dropped there)
Copy-Item -Path "$InstallDir\*" -Destination $PayloadDir -Recurse -Force

# Drag-and-drop wrapper
Copy-Item -Path "$ScriptDir\daemonmap-jka.bat" -Destination $PayloadDir -Force

# Short release-only README aimed at non-technical users
$ReleaseReadme = @"
daemonmap (JKA fork) - Windows release $Version
=================================================

WHAT THIS IS
  Compiles a navigation mesh (.navmesh) from a Jedi Academy .bsp map
  file. Bot2 (the JKA bot AI mod) reads the .navmesh at runtime so the
  bots know how to move around the map.

QUICK START (basic navmesh, no wallruns)
  1. Drag your .bsp file onto daemonmap-jka.bat.
  2. A .navmesh file appears next to your .bsp.
  3. Copy the .navmesh into your JKA install's "GameData\base\maps\"
     folder (or your mod's equivalent maps\ folder).
  4. Load the map in JKA with Bot2. Bots can now navigate it.

If a window flashes and disappears, run daemonmap-jka.bat from a
Command Prompt so you can read any error messages.

ADDING WALLRUNS (advanced, two-pass workflow)
  Wallruns need a second pass because Bot2's wallrun scanner has to
  walk on an existing navmesh to validate them.

  After step 4 above:

  5. In-game, open the console (~) and run:
        /bot_scan_wallruns
     Bot2 writes a sidecar file:
        GameData\base\maps\<mapname>.nav_connections

  6. MOVE that .nav_connections file out of base\maps\ and place it
     next to your source .bsp (the one you drag onto the bat). The
     two files must sit in the same folder for daemonmap to find it.
     Example:
        from:  GameData\base\maps\ctf_kejim.nav_connections
        to:    C:\Users\You\Maps\ctf_kejim.nav_connections
               (next to ctf_kejim.bsp)

  7. Drag your .bsp onto daemonmap-jka.bat AGAIN. The bat will print
     "Sidecar found: ..." this time, and the new .navmesh will include
     wallrun connections.

  8. Copy the new .navmesh into base\maps\ (overwriting the old one).
     Bots now perform wallruns in combat on this map.

LICENSE
  GPLv2. See the LICENSE / GPL files in the source repository.

SOURCE
  https://github.com/RebirthMeow/daemonmap-jka
"@

$ReleaseReadme | Set-Content -LiteralPath (Join-Path $PayloadDir "README-RELEASE.txt") -Encoding UTF8

# --- Zip it up ---

$ZipPath = Join-Path $ReleaseDir "$ReleaseName.zip"
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }

Write-Host "Compressing to: $ZipPath" -ForegroundColor Green
Compress-Archive -Path "$PayloadDir\*" -DestinationPath $ZipPath -CompressionLevel Optimal

# --- Cleanup staging ---

Remove-Item -Recurse -Force $StagingDir

# --- Report ---

$zipInfo = Get-Item $ZipPath
$sizeMb = [math]::Round($zipInfo.Length / 1MB, 2)

Write-Host ""
Write-Host "=== Release packaged ===" -ForegroundColor Green
Write-Host "  $ZipPath  ($sizeMb MB)"
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "  1. Open https://github.com/RebirthMeow/daemonmap-jka/releases/new"
Write-Host "  2. Tag: $Version"
Write-Host "  3. Drag the zip into the 'Attach binaries' box"
Write-Host "  4. Publish."
