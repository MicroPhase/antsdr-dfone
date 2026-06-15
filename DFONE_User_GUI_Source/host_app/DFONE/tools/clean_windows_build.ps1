param(
    [switch]$Yes
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Resolve-Path (Join-Path $ScriptDir "..")

function Resolve-InProjectPath {
    param([string]$RelativePath)

    $combined = Join-Path $ProjectDir $RelativePath
    $full = [System.IO.Path]::GetFullPath($combined)
    $projectFull = [System.IO.Path]::GetFullPath($ProjectDir)

    if (-not $full.StartsWith($projectFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean outside project directory: $full"
    }

    return $full
}

$Targets = @(
    "build",
    "build-vcpkg-diagnose",
    "build-windows-release",
    "dist",
    "user_gui\build",
    "user_gui\build-win",
    "user_gui\build-win-single-exe",
    "user_gui\dist",
    "examples\windows_cli_capture\build",
    "examples\windows_cli_capture\build-shared",
    "examples\windows_cli_capture\build-verify"
)

$ExistingTargets = @()
foreach ($target in $Targets) {
    $path = Resolve-InProjectPath $target
    if (Test-Path $path) {
        $ExistingTargets += $path
    }
}

if ($ExistingTargets.Count -eq 0) {
    Write-Host "Nothing to clean."
    exit 0
}

Write-Host "The following generated directories/files will be removed:"
foreach ($path in $ExistingTargets) {
    Write-Host "  $path"
}

if (-not $Yes) {
    $answer = Read-Host "Continue? Type YES to clean"
    if ($answer -ne "YES") {
        Write-Host "Clean cancelled."
        exit 0
    }
}

foreach ($path in $ExistingTargets) {
    Write-Host "Removing $path"
    Remove-Item -LiteralPath $path -Recurse -Force
}

Write-Host "Clean complete."
