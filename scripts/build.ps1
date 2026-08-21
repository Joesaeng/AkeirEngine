<#
.SYNOPSIS
  Project_ME 빌드 스크립트 — VS2022 환경(vcvars64)을 잡고 CMake preset 으로 configure/build/test 한다.
.DESCRIPTION
  문서: Docs/BUILD.md. 설계 문서 §41.
  PATH 에 cmake/ninja 가 없어도 된다 — VS 번들 CMake/Ninja 를 vcvars64 가 PATH 에 넣는다.
.EXAMPLE
  .\scripts\build.ps1                      # msvc-debug configure + build
  .\scripts\build.ps1 -Preset msvc-headless -Test
  .\scripts\build.ps1 -Target akeir        # 특정 타깃만
  .\scripts\build.ps1 -Clean
#>
param(
  [string]$Preset = "msvc-debug",
  [string]$Target = "",
  [switch]$Test,
  [switch]$Clean,
  [switch]$ConfigureOnly
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found — Visual Studio 2022 is required (Docs/BUILD.md)." }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "No Visual Studio with C++ tools found." }
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"

if ($Clean -and (Test-Path "$root\build\$Preset")) { Remove-Item -Recurse -Force "$root\build\$Preset" }

$steps = "cmake --preset $Preset"
if (-not $ConfigureOnly) {
  $steps += " && cmake --build --preset $Preset"
  if ($Target) { $steps += " --target $Target" }
}
if ($Test) { $steps += " && ctest --preset $Preset" }

# cmd.exe 에서 vcvars64 를 call 한 뒤 같은 셸에서 cmake 를 실행한다 (환경변수 전파).
# 따옴표 중첩 문제를 피하려고 임시 .cmd 파일을 만든다.
$batch = Join-Path $env:TEMP ("pme-build-" + [guid]::NewGuid().ToString("N") + ".cmd")
$installerDir = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
@(
  "@echo off",
  "set `"PATH=$installerDir;%SystemRoot%\System32;%SystemRoot%;%PATH%`"",
  "call `"$vcvars`" >nul",
  "if errorlevel 1 exit /b 1",
  "cd /d `"$root`"",
  $steps
) | Set-Content -Path $batch -Encoding ASCII
try {
  & cmd.exe /c $batch
  exit $LASTEXITCODE
} finally {
  Remove-Item $batch -ErrorAction SilentlyContinue
}
