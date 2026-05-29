@echo off
echo Cleaning build directories...
rmdir /s /q "%~dp0build" 2>nul
rmdir /s /q "%~dp0build_2024" 2>nul
rmdir /s /q "%~dp0build_2025" 2>nul
rmdir /s /q "%~dp0build_2026" 2>nul
rmdir /s /q "%~dp0build_2027" 2>nul
echo Done.
pause
