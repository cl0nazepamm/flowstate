@echo off
setlocal

:: FlowState suite quick dev cycle for 3ds Max 2027.
:: Kills Max, builds all suite plugins, deploys, and relaunches.
:: Usage: dev_2027.bat [scenefile.max]

set NATIVE_DIR=%~dp0
set CMAKE="C:\Program Files\CMake\bin\cmake.exe"
set MAX_VERSION=2027
set MAX_EXE="C:\Program Files\Autodesk\3ds Max %MAX_VERSION%\3dsmax.exe"
set MAX_PLUGINS=C:\Program Files\Autodesk\3ds Max %MAX_VERSION%\plugins
set MAXSDK=C:/Program Files/Autodesk/3ds Max %MAX_VERSION% SDK/maxsdk
set BUILD_DIR=%NATIVE_DIR%build_%MAX_VERSION%
set USERMACROS=%LOCALAPPDATA%\Autodesk\3dsMax\%MAX_VERSION% - 64bit\ENU\usermacros

set SCENE=%~1

echo [1/5] Killing 3ds Max...
taskkill /F /IM 3dsmax.exe >nul 2>&1
%SystemRoot%\System32\timeout.exe /t 2 /nobreak >nul

echo [2/5] Configuring for 3ds Max %MAX_VERSION%...
%CMAKE% -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DMAX_VERSION=%MAX_VERSION% -DMAXSDK_PATH="%MAXSDK%" -DMAX_PLUGINS_DIR="%MAX_PLUGINS%" "%NATIVE_DIR%"
if %ERRORLEVEL% NEQ 0 goto :fail

echo [2/5] Building suite...
%CMAKE% --build "%BUILD_DIR%" --config Release
if %ERRORLEVEL% NEQ 0 goto :fail

echo [3/5] Deploying plugin...
copy /Y "%BUILD_DIR%\Release\FlowState.gup" "%MAX_PLUGINS%\FlowState.gup" >nul
if ERRORLEVEL 1 (
    echo Deploy failed for FlowState.gup - need admin. Elevating...
    powershell -Command "Start-Process cmd -ArgumentList '/c copy /Y \"%BUILD_DIR%\Release\FlowState.gup\" \"%MAX_PLUGINS%\FlowState.gup\" && del /Q \"%MAX_PLUGINS%\PowerCut.dlm\" \"%MAX_PLUGINS%\normalize_poly.dlm\" 2^>nul' -Verb RunAs -Wait"
)
del /Q "%MAX_PLUGINS%\PowerCut.dlm" "%MAX_PLUGINS%\normalize_poly.dlm" >nul 2>&1

echo [4/5] Deploying macros...
if not exist "%USERMACROS%" mkdir "%USERMACROS%"
for %%M in ("%NATIVE_DIR%macros\*.ms" "%NATIVE_DIR%macros\*.mcr") do (
    if exist "%%~fM" copy /Y "%%~fM" "%USERMACROS%\%%~nxM" >nul
)

echo [5/5] Launching 3ds Max %MAX_VERSION%...
if "%SCENE%"=="" (
    start "" %MAX_EXE%
) else (
    start "" %MAX_EXE% "%SCENE%"
)

echo.
echo === Done! Max %MAX_VERSION% is starting with fresh FlowState suite plugins. ===
exit /b 0

:fail
echo.
echo === BUILD FAILED ===
pause
exit /b 1
