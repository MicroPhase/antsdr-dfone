param(
    [string]$Arch = "x64",
    [string]$Config = "Release",
    [string]$Generator = "Visual Studio 18 2026",
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$DistRoot
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Resolve-Path (Join-Path $ScriptDir "..")

if (-not $DistRoot) {
    $DistRoot = Join-Path $ProjectDir "dist\windows"
}

$PackageName = "DFONE-Windows-SDK-1.0.0-$Arch"
$PackageDir = Join-Path $DistRoot $PackageName
$BuildRoot = Join-Path $ProjectDir "build-windows-release"
$StaticBuildDir = Join-Path $BuildRoot "sdk-static"
$SharedBuildDir = Join-Path $BuildRoot "sdk-shared"
$AppsBuildDir = Join-Path $BuildRoot "apps-static"

function Invoke-CMakeBuild {
    param(
        [string]$SourceDir,
        [string]$BuildDir,
        [string[]]$ConfigureArgs,
        [string[]]$Targets,
        [switch]$RetryConfigureOnce
    )

    cmake -S $SourceDir -B $BuildDir -G $Generator -A $Arch @ConfigureArgs
    if ($LASTEXITCODE -ne 0) {
        if ($RetryConfigureOnce) {
            Write-Warning "CMake configure failed for $BuildDir; removing this build directory and retrying once."
            Remove-Item $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
            cmake -S $SourceDir -B $BuildDir -G $Generator -A $Arch @ConfigureArgs
        }
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configure failed for $BuildDir"
        }
    }
    foreach ($Target in $Targets) {
        cmake --build $BuildDir --config $Config --target $Target
        if ($LASTEXITCODE -ne 0) {
            throw "CMake build failed for target $Target in $BuildDir"
        }
    }
}

function Copy-IfExists {
    param(
        [string]$Path,
        [string]$Destination
    )

    if (Test-Path $Path) {
        New-Item -ItemType Directory -Force -Path $Destination | Out-Null
        Copy-Item $Path $Destination -Force
    }
}

function Copy-DirectoryIfExists {
    param(
        [string]$Path,
        [string]$Destination
    )

    if (Test-Path $Path) {
        New-Item -ItemType Directory -Force -Path $Destination | Out-Null
        Copy-Item $Path $Destination -Recurse -Force
    }
}

if (-not $VcpkgRoot -and (Test-Path "C:\vcpkg")) {
    $VcpkgRoot = "C:\vcpkg"
}
if (-not $VcpkgRoot -and (Test-Path "D:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg")) {
    $VcpkgRoot = "D:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg"
}

Remove-Item $PackageDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null

$StaticInstallDir = Join-Path $PackageDir "sdk\static"
$SharedInstallDir = Join-Path $PackageDir "sdk\shared"

Invoke-CMakeBuild `
    -SourceDir $ProjectDir `
    -BuildDir $StaticBuildDir `
    -ConfigureArgs @(
        "-DDFONE_BUILD_SHARED=OFF",
        "-DCMAKE_INSTALL_PREFIX=$StaticInstallDir"
    ) `
    -Targets @("install")

Invoke-CMakeBuild `
    -SourceDir $ProjectDir `
    -BuildDir $SharedBuildDir `
    -ConfigureArgs @(
        "-DDFONE_BUILD_SHARED=ON",
        "-DCMAKE_INSTALL_PREFIX=$SharedInstallDir"
    ) `
    -Targets @("install")

if (-not $VcpkgRoot) {
    throw "Set VCPKG_ROOT to build the GUI, for example: `$env:VCPKG_ROOT='C:\vcpkg'"
}

$Toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $Toolchain)) {
    throw "vcpkg toolchain file was not found: $Toolchain"
}

$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
$GuiSourceDir = Join-Path $ProjectDir "user_gui"
if (Test-Path (Join-Path $GuiSourceDir "vcpkg.json")) {
    Push-Location $GuiSourceDir
    try {
        & $VcpkgExe x-update-baseline --add-initial-baseline
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "vcpkg baseline update failed; continuing with existing manifest."
        }
    } finally {
        Pop-Location
    }
}

Invoke-CMakeBuild `
    -SourceDir $GuiSourceDir `
    -BuildDir $AppsBuildDir `
    -ConfigureArgs @(
        "-DCMAKE_TOOLCHAIN_FILE=$Toolchain",
        "-DVCPKG_TARGET_TRIPLET=x64-windows-static",
        "-DDFONE_USER_BUILD_GUI=ON",
        "-DDFONE_USER_STATIC_MSVC_RUNTIME=ON",
        "-DDFONE_USER_STATIC_GLEW=ON",
        "-DDFONE_USER_PACKAGE_RUNTIME_DEPS=OFF"
    ) `
    -Targets @("dfone_user_cli", "dfone_user_gui") `
    -RetryConfigureOnce

$AppsDir = Join-Path $PackageDir "apps"
Copy-IfExists (Join-Path $AppsBuildDir "$Config\dfone_user_cli.exe") $AppsDir
Copy-IfExists (Join-Path $AppsBuildDir "$Config\dfone_user_gui.exe") $AppsDir

$DocsDir = Join-Path $PackageDir "docs"
Copy-IfExists (Join-Path $ProjectDir "README.md") $DocsDir
Copy-IfExists (Join-Path $ProjectDir "API_USAGE.md") $DocsDir
Copy-IfExists (Join-Path $ProjectDir "PUBLIC_API_MANUAL.md") $DocsDir

$DriverDir = Join-Path $PackageDir "windows-driver"
Copy-IfExists (Join-Path $ProjectDir "windows\README_RNDIS.md") $DriverDir
Copy-IfExists (Join-Path $ProjectDir "windows\dfone_rndis.inf") $DriverDir

$ExamplesDir = Join-Path $PackageDir "examples"
Copy-DirectoryIfExists (Join-Path $ProjectDir "examples\windows_cli_capture") $ExamplesDir

$Readme = @'
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
'@

Set-Content -Path (Join-Path $PackageDir "README_WINDOWS_PACKAGE.md") -Value $Readme -Encoding UTF8

$ZipPath = Join-Path $DistRoot "$PackageName.zip"
Remove-Item $ZipPath -Force -ErrorAction SilentlyContinue
Compress-Archive -Path $PackageDir -DestinationPath $ZipPath -Force

Write-Host "Windows release package:"
Write-Host $PackageDir
Write-Host $ZipPath
