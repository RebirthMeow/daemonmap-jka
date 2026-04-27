@echo off
REM ============================================================
REM  daemonmap-jka.bat - drag-and-drop navmesh compiler for JKA
REM ============================================================
REM
REM  USAGE:
REM    1. Drag a JKA .bsp file onto this .bat file in Explorer.
REM    2. The navmesh is written next to the BSP as <name>.navmesh.
REM    3. Copy the .navmesh into your JKA "maps\" folder so Bot2
REM       can load it (it expects "maps\<mapname>.navmesh").
REM
REM  Or from a command prompt:
REM    daemonmap-jka.bat path\to\map.bsp
REM
REM  This wrapper just invokes:
REM    daemonmap.exe -game ja -nav <your.bsp>
REM  with the JKA-tuned defaults (-meters and -solomesh on).
REM
REM ============================================================

setlocal

REM --- Locate daemonmap.exe ---
REM Look first in the same directory as this .bat, then in install\.
set "SCRIPT_DIR=%~dp0"
set "EXE=%SCRIPT_DIR%daemonmap.exe"
if not exist "%EXE%" set "EXE=%SCRIPT_DIR%install\daemonmap.exe"

if not exist "%EXE%" (
    echo.
    echo ERROR: daemonmap.exe not found.
    echo.
    echo Looked in:
    echo   %SCRIPT_DIR%daemonmap.exe
    echo   %SCRIPT_DIR%install\daemonmap.exe
    echo.
    echo Build the project first ^(.\build.ps1^) or place daemonmap.exe
    echo next to this .bat file.
    echo.
    pause
    exit /b 1
)

REM --- Validate the dropped file ---
if "%~1"=="" (
    echo.
    echo USAGE: drag a .bsp file onto this .bat, or run:
    echo   daemonmap-jka.bat path\to\map.bsp
    echo.
    pause
    exit /b 1
)

if not exist "%~1" (
    echo.
    echo ERROR: file not found: %~1
    echo.
    pause
    exit /b 1
)

REM --- Detect optional .nav_connections sidecar (wallrun pass 2) ---
set "SIDECAR=%~dpn1.nav_connections"
echo.
echo Compiling navmesh for: %~1
echo Using daemonmap:        %EXE%
if exist "%SIDECAR%" (
    echo Sidecar found:          %SIDECAR%
    echo                         ^(wallruns will be baked into the navmesh^)
) else (
    echo Sidecar:                none
    echo                         ^(no .nav_connections next to the .bsp ^=
    echo                          this build will not include wallruns^)
)
echo.

"%EXE%" -game ja -nav "%~1"
set ERR=%ERRORLEVEL%

echo.
if %ERR%==0 (
    echo === DONE ===
    echo Output: %~dpn1.navmesh
    echo.
    echo NEXT STEPS:
    echo   1. Copy the .navmesh into your JKA install's "base\maps\" folder.
    echo   2. Load the map in JKA with Bot2. Bots can now navigate.
    echo.
    if not exist "%SIDECAR%" (
        echo TO ADD WALLRUNS ^(optional, advanced^):
        echo   3. In-game, run:  /bot_scan_wallruns
        echo      Bot2 writes:  base\maps\%~n1.nav_connections
        echo   4. COPY that .nav_connections file from base\maps\ to:
        echo        %~dp1
        echo      ^(it must sit next to your source .bsp, not in base\maps\^)
        echo   5. Drag the .bsp onto this .bat AGAIN. The new .navmesh will
        echo      include wallrun connections. Re-copy it into base\maps\.
    )
) else (
    echo === FAILED ^(exit code %ERR%^) ===
)

echo.
pause
exit /b %ERR%
