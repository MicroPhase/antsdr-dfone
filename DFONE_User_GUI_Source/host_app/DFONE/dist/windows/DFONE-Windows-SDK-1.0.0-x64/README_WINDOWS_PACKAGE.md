# DFONE Windows Release Package

## Contents

- apps/dfone_user_gui.exe: DFONE Windows GUI application.
- apps/dfone_user_cli.exe: DFONE command-line example application.
- sdk/static: static C++ SDK, including headers and dfone_host.lib.
- sdk/shared: dynamic C++ SDK, including headers, dfone_host.dll, and import dfone_host.lib.
- docs: public API documentation.
- examples/windows_cli_capture: customer-side CMake CLI example.
- windows-driver: Windows RNDIS INF and installation notes.

## Prerequisites

- Visual Studio 2026 or Visual Studio 2022 with Desktop development with C++.
- CMake 3.16 or newer.
- A Visual Studio developer PowerShell for command-line builds.

## Use the Static SDK

Add sdk/static/include to your include path and link sdk/static/lib/dfone_host.lib.

With CMake:

```cmake
add_executable(my_dfone_app main.cpp)
target_include_directories(my_dfone_app PRIVATE path/to/sdk/static/include)
target_link_libraries(my_dfone_app PRIVATE path/to/sdk/static/lib/dfone_host.lib ws2_32)
target_compile_definitions(my_dfone_app PRIVATE DFONE_STATIC)
```

Visual Studio property page equivalent:

- C/C++ > General > Additional Include Directories: `path\to\sdk\static\include`
- C/C++ > Preprocessor > Preprocessor Definitions: add `DFONE_STATIC`
- Linker > General > Additional Library Directories: `path\to\sdk\static\lib`
- Linker > Input > Additional Dependencies: add `dfone_host.lib;ws2_32.lib`

## Use the Shared SDK

Add sdk/shared/include to your include path, link sdk/shared/lib/dfone_host.lib,
and place sdk/shared/bin/dfone_host.dll next to your executable or on PATH.

With CMake:

```cmake
add_executable(my_dfone_app main.cpp)
target_include_directories(my_dfone_app PRIVATE path/to/sdk/shared/include)
target_link_libraries(my_dfone_app PRIVATE path/to/sdk/shared/lib/dfone_host.lib ws2_32)
```

Visual Studio property page equivalent:

- C/C++ > General > Additional Include Directories: `path\to\sdk\shared\include`
- Linker > General > Additional Library Directories: `path\to\sdk\shared\lib`
- Linker > Input > Additional Dependencies: add `dfone_host.lib;ws2_32.lib`
- Copy `sdk\shared\bin\dfone_host.dll` next to your application `.exe`.

## Build the Customer CLI Example

The `examples\windows_cli_capture` directory is a standalone customer project.
It uses only the SDK files in this package.

Build it with the static SDK:

```powershell
cd examples\windows_cli_capture
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
  -DDFONE_SDK_ROOT=..\..\sdk\static `
  -DDFONE_USE_SHARED=OFF
cmake --build build --config Release
```

Run it:

```powershell
.\build\Release\dfone_customer_capture.exe --device-ip 192.168.7.2 --frames 65536 --output iq.cs16
```

Build it with the shared SDK:

```powershell
cd examples\windows_cli_capture
cmake -S . -B build-shared -G "Visual Studio 18 2026" -A x64 `
  -DDFONE_SDK_ROOT=..\..\sdk\shared `
  -DDFONE_USE_SHARED=ON
cmake --build build-shared --config Release
```

The shared build copies `dfone_host.dll` next to `dfone_customer_capture.exe`.

## Build Command

Run this script from a Visual Studio developer PowerShell:

```powershell
.\tools\package_windows_release.ps1
```
