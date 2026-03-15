@echo off
echo Cleaning build directory...
rmdir /s /q "%~dp0build" 2>nul
echo Done.
pause
