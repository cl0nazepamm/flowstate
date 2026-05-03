@echo off
setlocal

:: ── FlowState quick dev cycle ───────────────────────────────────
:: Kills Max, builds, deploys, relaunches with last scene.
:: Usage: dev.bat [scenefile.max]

set NATIVE_DIR=%~dp0
set CMAKE="C:\Program Files\CMake\bin\cmake.exe"
set MAX_EXE="C:\Program Files\Autodesk\3ds Max 2026\3dsmax.exe"
set MAX_PLUGINS=C:\Program Files\Autodesk\3ds Max 2026\plugins
set BUILD_DIR=%NATIVE_DIR%build

set SCENE=%~1

:: ── Kill Max ────────────────────────────────────────────────────
echo [1/5] Killing 3ds Max...
taskkill /F /IM 3dsmax.exe >nul 2>&1
%SystemRoot%\System32\timeout.exe /t 2 /nobreak >nul

:: ── Build ───────────────────────────────────────────────────────
echo [2/5] Building...
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    %CMAKE% -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 "%NATIVE_DIR%"
    if %ERRORLEVEL% NEQ 0 goto :fail
)
%CMAKE% --build "%BUILD_DIR%" --config Release
if %ERRORLEVEL% NEQ 0 goto :fail

:: ── Deploy GUP ──────────────────────────────────────────────────
echo [3/5] Deploying GUP...
copy /Y "%BUILD_DIR%\Release\FlowState.gup" "%MAX_PLUGINS%\FlowState.gup" >nul
if %ERRORLEVEL% NEQ 0 (
    echo Deploy failed - need admin. Elevating...
    powershell -Command "Start-Process cmd -ArgumentList '/c copy /Y \"%BUILD_DIR%\Release\FlowState.gup\" \"%MAX_PLUGINS%\FlowState.gup\"' -Verb RunAs -Wait"
)

:: ── Deploy MCR ─────────────────────────────────────────────────
echo [4/5] Deploying config macro...
set MCR_DST=%LOCALAPPDATA%\Autodesk\3dsMax\2026 - 64bit\ENU\usermacros\flowstate_config.ms
if exist "%NATIVE_DIR%macros\flowstate_config.ms" (
    copy /Y "%NATIVE_DIR%macros\flowstate_config.ms" "%MCR_DST%" >nul
)

:: ── Relaunch ────────────────────────────────────────────────────
echo [5/5] Launching 3ds Max...
if "%SCENE%"=="" (
    start "" %MAX_EXE%
) else (
    start "" %MAX_EXE% "%SCENE%"
)

echo.
echo === Done! Max is starting. ===
exit /b 0

:fail
echo.
echo === BUILD FAILED ===
pause
exit /b 1
