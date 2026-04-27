# Builds the static-CRT release binary, then compiles the Inno Setup installer.
# Run from any directory; paths are anchored to this script's location.
#
# Usage:
#   pwsh -ExecutionPolicy Bypass -File installer\build-installer.ps1
#
# Output:
#   installer\Output\agents-hub-setup-<version>.exe

param(
	[string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = Resolve-Path (Join-Path $ScriptDir "..")
$BuildDir  = Join-Path $RepoRoot "cmake-build-installer"
$IssScript = Join-Path $ScriptDir "agents-hub.iss"

function Find-Iscc {
	$candidates = @(
		"C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
		"C:\Program Files\Inno Setup 6\ISCC.exe"
	)
	foreach ($p in $candidates) { if (Test-Path $p) { return $p } }
	$onPath = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
	if ($onPath) { return $onPath.Source }
	throw "Inno Setup 6 not found. Install from https://jrsoftware.org/isinfo.php"
}

# Use the Visual Studio 17 2022 generator instead of Ninja. Reasons:
#   - The VS generator is multi-config and handles `$Configuration` tokens
#     CMake emits for C++ module scanning (CMake 4.1 + Ninja single-config
#     emits these tokens literally, breaking the build).
#   - It auto-discovers the VS install — no vcvars import needed.
#   - Slower than Ninja, but this is a one-shot release artifact build.
# Output for a multi-config generator lands in <build_dir>/<config>/, so the
# installer script's BuildDir points at cmake-build-installer/Release.

Write-Host "==> Configuring (STATIC_RUNTIME=ON, BUILD_TESTS=OFF, generator=Visual Studio 17 2022)"
cmake -B $BuildDir -S $RepoRoot -DSTATIC_RUNTIME=ON -DBUILD_TESTS=OFF -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

Write-Host "==> Building agents-hub.exe ($Configuration)"
cmake --build $BuildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

$Iscc = Find-Iscc
Write-Host "==> Compiling installer with $Iscc"
& $Iscc $IssScript
if ($LASTEXITCODE -ne 0) { throw "ISCC failed" }

$OutputDir = Join-Path $ScriptDir "Output"
Write-Host ""
Write-Host "Installer ready in $OutputDir"
Get-ChildItem $OutputDir -Filter "*.exe" | Sort-Object LastWriteTime -Descending | Select-Object -First 1 | Format-List Name, Length, LastWriteTime
