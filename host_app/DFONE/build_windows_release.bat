@echo off
setlocal

cd /d "%~dp0"

set "VSDEVCMD="
if exist "D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" (
    set "VSDEVCMD=D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
)
if not defined VSDEVCMD if exist "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" (
    set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
)
if not defined VSDEVCMD if exist "D:\Program Files\Microsoft Visual Studio\17\Community\Common7\Tools\VsDevCmd.bat" (
    set "VSDEVCMD=D:\Program Files\Microsoft Visual Studio\17\Community\Common7\Tools\VsDevCmd.bat"
)
if not defined VSDEVCMD if exist "C:\Program Files\Microsoft Visual Studio\17\Community\Common7\Tools\VsDevCmd.bat" (
    set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\17\Community\Common7\Tools\VsDevCmd.bat"
)

if not defined VSDEVCMD (
    echo Visual Studio developer command script was not found.
    echo Please install Visual Studio C++ workload or edit this .bat with your VsDevCmd.bat path.
    pause
    exit /b 1
)

if not defined VCPKG_ROOT if exist "D:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg\vcpkg.exe" (
    set "VCPKG_ROOT=D:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg"
)
if not defined VCPKG_ROOT if exist "C:\vcpkg\vcpkg.exe" (
    set "VCPKG_ROOT=C:\vcpkg"
)

echo Using Visual Studio:
echo   %VSDEVCMD%
if defined VCPKG_ROOT (
    echo Using vcpkg:
    echo   %VCPKG_ROOT%
)
echo.

call "%VSDEVCMD%" -arch=x64 -host_arch=x64
if errorlevel 1 (
    echo Failed to initialize Visual Studio build environment.
    pause
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\package_windows_release.ps1" %*
if errorlevel 1 (
    echo.
    echo Build failed.
    echo If the error says a file is being used by another process, close the old GUI/CLI executable and run this again.
    pause
    exit /b 1
)

echo.
echo Build complete.
echo Output:
echo   %~dp0dist\windows\DFONE-Windows-SDK-1.0.0-x64
echo   %~dp0dist\windows\DFONE-Windows-SDK-1.0.0-x64.zip
pause
