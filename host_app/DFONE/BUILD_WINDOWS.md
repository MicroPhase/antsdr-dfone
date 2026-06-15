# DFONE Windows Build Guide

This guide builds the Windows customer package from the DFONE public host SDK.

## Prerequisites

- Visual Studio 2026, or Visual Studio 2022 with the "Desktop development with C++" workload
- CMake 3.16 or newer
- vcpkg, used by the GUI build for GLFW and GLEW

The easiest path is to double-click one of the root `.bat` files:

- `build_windows_release.bat`: builds the full customer package, including SDK
  static library, SDK DLL, GUI, CLI, documentation, driver notes, and examples.
- `build_windows_gui.bat`: builds only the GUI single executable for quick
  iteration after changing the upper-computer GUI code.
- `clean_windows_build.bat`: removes generated Windows build and package
  outputs so the source tree returns to a clean build state.

The manual commands below are kept for debugging or CI. Open a Visual Studio
developer PowerShell before running them.

## One-Click Clean

From Windows Explorer, double-click:

```text
clean_windows_build.bat
```

The script asks for confirmation and removes generated outputs such as:

```text
build-windows-release
build-vcpkg-diagnose
dist
user_gui\build-win-single-exe
user_gui\dist
examples\windows_cli_capture\build*
```

It does not remove source directories such as `src`, `public`, `user_gui\src`,
`examples`, `windows`, or `third_party`.

To skip the confirmation prompt from PowerShell:

```powershell
.\tools\clean_windows_build.ps1 -Yes
```

## Build the Static SDK Library

```powershell
cmake -S . -B build\windows-sdk-static -G "Visual Studio 18 2026" -A x64 `
  -DDFONE_BUILD_SHARED=OFF `
  -DCMAKE_INSTALL_PREFIX=dist\windows-sdk-static

cmake --build build\windows-sdk-static --config Release --target install
```

Output:

```text
dist\windows-sdk-static\include\dfone\*.hpp
dist\windows-sdk-static\lib\dfone_host.lib
```

Customer applications using this library should define `DFONE_STATIC` and link
`ws2_32`.

## Build the Shared SDK Library

```powershell
cmake -S . -B build\windows-sdk-shared -G "Visual Studio 18 2026" -A x64 `
  -DDFONE_BUILD_SHARED=ON `
  -DCMAKE_INSTALL_PREFIX=dist\windows-sdk-shared

cmake --build build\windows-sdk-shared --config Release --target install
```

Output:

```text
dist\windows-sdk-shared\include\dfone\*.hpp
dist\windows-sdk-shared\lib\dfone_host.lib
dist\windows-sdk-shared\bin\dfone_host.dll
```

Customer applications link the import library `dfone_host.lib` and deploy
`dfone_host.dll` next to their executable or on `PATH`.

## Build the GUI and CLI

```powershell
$env:VCPKG_ROOT = "D:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg"

cmake -S user_gui -B user_gui\build-win -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static `
  -DDFONE_USER_BUILD_GUI=ON `
  -DDFONE_USER_STATIC_MSVC_RUNTIME=ON `
  -DDFONE_USER_STATIC_GLEW=ON `
  -DDFONE_USER_PACKAGE_RUNTIME_DEPS=OFF

cmake --build user_gui\build-win --config Release --target dfone_user_cli dfone_user_gui
```

Output:

```text
user_gui\build-win\Release\dfone_user_cli.exe
user_gui\build-win\Release\dfone_user_gui.exe
```

## Build the Full Windows Release Package

```powershell
$env:VCPKG_ROOT = "D:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg"
.\tools\package_windows_release.ps1
```

Output:

```text
dist\windows\DFONE-Windows-SDK-1.0.0-x64
dist\windows\DFONE-Windows-SDK-1.0.0-x64.zip
```

The package contains:

- `apps`: GUI and CLI executables
- `sdk/static`: headers and static `dfone_host.lib`
- `sdk/shared`: headers, import library, and `dfone_host.dll`
- `docs`: public API documentation
- `examples/windows_cli_capture`: standalone customer CLI example
- `windows-driver`: RNDIS INF and installation notes

## Build the Customer CLI Example from the Package

After creating the full Windows package, a customer can build the example
without the DFONE source tree:

```powershell
cd dist\windows\DFONE-Windows-SDK-1.0.0-x64\examples\windows_cli_capture

cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
  -DDFONE_SDK_ROOT=..\..\sdk\static `
  -DDFONE_USE_SHARED=OFF

cmake --build build --config Release
```

Run:

```powershell
.\build\Release\dfone_customer_capture.exe --device-ip 192.168.7.2 --frames 65536 --output iq.cs16
```

To use the DLL SDK instead:

```powershell
cmake -S . -B build-shared -G "Visual Studio 18 2026" -A x64 `
  -DDFONE_SDK_ROOT=..\..\sdk\shared `
  -DDFONE_USE_SHARED=ON

cmake --build build-shared --config Release
```
