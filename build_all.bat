@echo off
setlocal enabledelayedexpansion

:: Cross-compile FlowState for every 3ds Max SDK installed on this machine.
:: Each version builds into its own build_<ver>\ dir via build_version.bat.
:: Usage: build_all.bat            (builds 2024..2027 where the SDK exists)
::        build_all.bat 2024 2025  (builds only the versions you list)

set NATIVE_DIR=%~dp0
set VERSIONS=%*
if "%VERSIONS%"=="" set VERSIONS=2024 2025 2026 2027

set BUILT=
set SKIPPED=
set FAILED=

for %%V in (%VERSIONS%) do (
    if exist "C:\Program Files\Autodesk\3ds Max %%V SDK\maxsdk\include\max.h" (
        echo ============================================================
        echo  Building FlowState for 3ds Max %%V
        echo ============================================================
        call "%NATIVE_DIR%build_version.bat" %%V
        if !ERRORLEVEL! NEQ 0 (
            set FAILED=!FAILED! %%V
        ) else (
            set BUILT=!BUILT! %%V
        )
    ) else (
        set SKIPPED=!SKIPPED! %%V
    )
)

echo.
echo ============================================================
echo  Summary
echo ============================================================
echo  Built:   !BUILT!
echo  Skipped: !SKIPPED!   ^(no SDK installed^)
echo  Failed:  !FAILED!
if not "!FAILED!"=="" exit /b 1
exit /b 0
