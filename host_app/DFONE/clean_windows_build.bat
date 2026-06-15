@echo off
setlocal

cd /d "%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\clean_windows_build.ps1" %*
if errorlevel 1 (
    echo.
    echo Clean failed.
    echo If a file is being used by another process, close the running GUI/CLI/CMake tools and run this again.
    pause
    exit /b 1
)

echo.
echo Clean finished.
pause
