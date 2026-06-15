$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Resolve-Path (Join-Path $ScriptDir "..")
$BuildDir = Join-Path $ProjectDir "build-win-single-exe"
$DistDir = Join-Path $ProjectDir "dist"

$VcpkgRoot = $env:VCPKG_ROOT
if (-not $VcpkgRoot) {
    $DefaultVcpkg = "C:\vcpkg"
    if (Test-Path $DefaultVcpkg) {
        $VcpkgRoot = $DefaultVcpkg
    }
}
if (-not $VcpkgRoot) {
    $DefaultVcpkg = "D:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg"
    if (Test-Path $DefaultVcpkg) {
        $VcpkgRoot = $DefaultVcpkg
    }
}

if (-not $VcpkgRoot) {
    throw "Set VCPKG_ROOT to your vcpkg path, for example: `$env:VCPKG_ROOT='C:\vcpkg'"
}

$Toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $Toolchain)) {
    throw "vcpkg toolchain file was not found: $Toolchain"
}

$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
if (Test-Path (Join-Path $ProjectDir "vcpkg.json")) {
    Push-Location $ProjectDir
    try {
        & $VcpkgExe x-update-baseline --add-initial-baseline
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "vcpkg baseline update failed; continuing with existing manifest."
        }
    } finally {
        Pop-Location
    }
}

cmake -S $ProjectDir -B $BuildDir `
    -G "Visual Studio 18 2026" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="$Toolchain" `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static `
    -DDFONE_USER_BUILD_GUI=ON `
    -DDFONE_USER_STATIC_MSVC_RUNTIME=ON `
    -DDFONE_USER_STATIC_GLEW=ON `
    -DDFONE_USER_PACKAGE_RUNTIME_DEPS=OFF
if ($LASTEXITCODE -ne 0) {
    Write-Warning "CMake configure failed; removing GUI build directory and retrying once."
    Remove-Item $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
    cmake -S $ProjectDir -B $BuildDir `
        -G "Visual Studio 18 2026" -A x64 `
        -DCMAKE_TOOLCHAIN_FILE="$Toolchain" `
        -DVCPKG_TARGET_TRIPLET=x64-windows-static `
        -DDFONE_USER_BUILD_GUI=ON `
        -DDFONE_USER_STATIC_MSVC_RUNTIME=ON `
        -DDFONE_USER_STATIC_GLEW=ON `
        -DDFONE_USER_PACKAGE_RUNTIME_DEPS=OFF
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed for $BuildDir"
    }
}

cmake --build $BuildDir --config Release --target dfone_user_gui
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed for dfone_user_gui"
}

New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
$Exe = Join-Path $BuildDir "Release\dfone_user_gui.exe"
$Out = Join-Path $DistDir "DFONE_User_GUI.exe"
Copy-Item $Exe $Out -Force

Write-Host "Single EXE output:"
Write-Host $Out
