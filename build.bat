@echo off
setlocal

:: ── Self-elevate to Administrator ───────────────────────────────
net session >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo Requesting Administrator privileges...
    echo Set UAC = CreateObject^("Shell.Application"^) > "%temp%\walker_elevate.vbs"
    echo UAC.ShellExecute "cmd.exe", "/c ""%~s0""", "%~dp0", "runas", 1 >> "%temp%\walker_elevate.vbs"
    cscript //nologo "%temp%\walker_elevate.vbs"
    del "%temp%\walker_elevate.vbs"
    exit /b
)

:: Ensure we're in the right directory
cd /d "%~dp0"

set NATIVE_DIR=%~dp0
set CMAKE="C:\Program Files\CMake\bin\cmake.exe"

:: Configure if needed
if not exist "%NATIVE_DIR%build\CMakeCache.txt" (
    echo [1/3] Configuring...
    %CMAKE% -B "%NATIVE_DIR%build" -G "Visual Studio 17 2022" -A x64 "%NATIVE_DIR%"
    if %ERRORLEVEL% NEQ 0 goto :fail
)

:: Build
echo [2/3] Building...
%CMAKE% --build "%NATIVE_DIR%build" --config Release
if %ERRORLEVEL% NEQ 0 goto :fail

:: Read plugin name and type from CMakeCache
for /f "tokens=2 delims==" %%a in ('findstr "PLUGIN_NAME:INTERNAL" "%NATIVE_DIR%build\CMakeCache.txt"') do set PNAME=%%a
for /f "tokens=2 delims==" %%a in ('findstr "PLUGIN_TYPE:INTERNAL" "%NATIVE_DIR%build\CMakeCache.txt"') do set PTYPE=%%a

:: Fallback
if "%PNAME%"=="" set PNAME=PowerParams
if "%PTYPE%"=="" set PTYPE=gup

set PLUGIN_FILE=%PNAME%.%PTYPE%
set PLUGIN_SRC=%NATIVE_DIR%build\Release\%PLUGIN_FILE%
set PLUGIN_DST=C:\Program Files\Autodesk\3ds Max 2026\plugins\%PLUGIN_FILE%

:: Deploy
echo [3/3] Deploying %PLUGIN_FILE% to 3ds Max plugins...
copy /Y "%PLUGIN_SRC%" "%PLUGIN_DST%"
if %ERRORLEVEL% NEQ 0 goto :fail

echo.
echo === Done! Restart 3ds Max to load %PLUGIN_FILE% ===
goto :done

:fail
echo.
echo === BUILD FAILED ===
pause
exit /b 1

:done
pause
