@echo off
setlocal

:: Cross-compile FlowState.gup against any installed 3ds Max SDK.
:: Builds into build_<ver>\Release\FlowState.gup. Deploys to the Max plugins
:: dir only if that Max version is actually installed (best-effort copy);
:: otherwise it just leaves the artifact in the build dir.
::
:: Usage: build_version.bat <2024|2025|2026|2027>
::
:: For the versions you actually run (2026/2027), use dev.bat / dev_2027.bat
:: instead -- those kill, deploy, install macros and relaunch Max.

set NATIVE_DIR=%~dp0
set CMAKE="C:\Program Files\CMake\bin\cmake.exe"

set MAX_VERSION=%~1
if "%MAX_VERSION%"=="" (
    echo Usage: build_version.bat ^<2024^|2025^|2026^|2027^>
    exit /b 1
)

set MAXSDK=C:/Program Files/Autodesk/3ds Max %MAX_VERSION% SDK/maxsdk
set MAX_PLUGINS=C:\Program Files\Autodesk\3ds Max %MAX_VERSION%\plugins
set BUILD_DIR=%NATIVE_DIR%build_%MAX_VERSION%
set USERMACROS=%LOCALAPPDATA%\Autodesk\3dsMax\%MAX_VERSION% - 64bit\ENU\usermacros

if not exist "%MAXSDK%/include/max.h" (
    echo ERROR: 3ds Max %MAX_VERSION% SDK not found at "%MAXSDK%".
    echo Install the matching SDK or pass a different version.
    exit /b 1
)

echo [1/3] Configuring for 3ds Max %MAX_VERSION%...
%CMAKE% -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DMAX_VERSION=%MAX_VERSION% -DMAXSDK_PATH="%MAXSDK%" -DMAX_PLUGINS_DIR="%MAX_PLUGINS%" "%NATIVE_DIR%"
if %ERRORLEVEL% NEQ 0 goto :fail

echo [2/3] Building suite (Release, x64)...
%CMAKE% --build "%BUILD_DIR%" --config Release
if %ERRORLEVEL% NEQ 0 goto :fail

echo [3/3] Deploying...
if exist "%MAX_PLUGINS%" (
    copy /Y "%BUILD_DIR%\Release\FlowState.gup" "%MAX_PLUGINS%\FlowState.gup" >nul
    if ERRORLEVEL 1 (
        echo   Could not copy to "%MAX_PLUGINS%" - run dev_%MAX_VERSION%.bat / dev.bat elevated.
    ) else (
        del /Q "%MAX_PLUGINS%\PowerCut.dlm" "%MAX_PLUGINS%\normalize_poly.dlm" >nul 2>&1
        if not exist "%USERMACROS%" mkdir "%USERMACROS%"
        for %%M in ("%NATIVE_DIR%macros\*.ms" "%NATIVE_DIR%macros\*.mcr") do (
            if exist "%%~fM" copy /Y "%%~fM" "%USERMACROS%\%%~nxM" >nul
        )
        echo   Deployed to Max %MAX_VERSION% plugins + usermacros.
    )
) else (
    echo   Max %MAX_VERSION% is not installed - artifact only.
)

echo.
echo === Done. FlowState.gup for Max %MAX_VERSION%: ===
echo     %BUILD_DIR%\Release\FlowState.gup
exit /b 0

:fail
echo.
echo === BUILD FAILED for Max %MAX_VERSION% ===
exit /b 1
