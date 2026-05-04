@echo off
setlocal

:: Build and deploy the FlowState plugin suite for 3ds Max 2026.

net session >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo Requesting Administrator privileges...
    echo Set UAC = CreateObject^("Shell.Application"^) > "%temp%\flowstate_elevate.vbs"
    echo UAC.ShellExecute "cmd.exe", "/c ""%~s0""", "%~dp0", "runas", 1 >> "%temp%\flowstate_elevate.vbs"
    cscript //nologo "%temp%\flowstate_elevate.vbs"
    del "%temp%\flowstate_elevate.vbs"
    exit /b
)

cd /d "%~dp0"

set NATIVE_DIR=%~dp0
set CMAKE="C:\Program Files\CMake\bin\cmake.exe"
set MAX_VERSION=2026
set MAX_PLUGINS=C:\Program Files\Autodesk\3ds Max %MAX_VERSION%\plugins
set MAXSDK=C:/Program Files/Autodesk/3ds Max %MAX_VERSION% SDK/maxsdk
set BUILD_DIR=%NATIVE_DIR%build
set USERMACROS=%LOCALAPPDATA%\Autodesk\3dsMax\%MAX_VERSION% - 64bit\ENU\usermacros

echo [1/4] Configuring...
%CMAKE% -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DMAX_VERSION=%MAX_VERSION% -DMAXSDK_PATH="%MAXSDK%" -DMAX_PLUGINS_DIR="%MAX_PLUGINS%" "%NATIVE_DIR%"
if %ERRORLEVEL% NEQ 0 goto :fail

echo [2/4] Building...
%CMAKE% --build "%BUILD_DIR%" --config Release
if %ERRORLEVEL% NEQ 0 goto :fail

echo [3/4] Deploying plugins...
for %%F in (FlowState.gup PowerCut.dlm normalize_poly.dlm) do (
    copy /Y "%BUILD_DIR%\Release\%%F" "%MAX_PLUGINS%\%%F"
    if ERRORLEVEL 1 goto :fail
)

echo [4/4] Deploying macros...
if not exist "%USERMACROS%" mkdir "%USERMACROS%"
for %%M in ("%NATIVE_DIR%macros\*.ms" "%NATIVE_DIR%macros\*.mcr") do (
    if exist "%%~fM" copy /Y "%%~fM" "%USERMACROS%\%%~nxM"
)

echo.
echo === Done! Restart 3ds Max to load the FlowState suite. ===
goto :done

:fail
echo.
echo === BUILD FAILED ===
pause
exit /b 1

:done
pause
