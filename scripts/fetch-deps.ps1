<#
.SYNOPSIS
  .cpm-cache/ 에 고정 버전 의존성 소스를 shallow clone 한다 (Docs/BUILD.md, DECISIONS.md ADR-0006).
  태그는 cmake/Dependencies.cmake 의 AKEIR_DEP_*_TAG 와 같아야 한다. 바꿀 때 두 곳을 함께 고친다.
#>
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$cache = Join-Path $root ".cpm-cache"
New-Item -ItemType Directory -Force $cache | Out-Null

$deps = @(
  @{ dir = "cpm";     url = "https://github.com/cpm-cmake/CPM.cmake.git";   tag = "v0.43.1" },
  @{ dir = "json";    url = "https://github.com/nlohmann/json.git";         tag = "v3.12.0" },
  @{ dir = "flecs";   url = "https://github.com/SanderMertens/flecs.git";   tag = "v4.1.6" },
  @{ dir = "box2d";   url = "https://github.com/erincatto/box2d.git";       tag = "v3.1.1" },
  @{ dir = "doctest"; url = "https://github.com/doctest/doctest.git";       tag = "v2.5.3" },
  @{ dir = "sdl";     url = "https://github.com/libsdl-org/SDL.git";        tag = "release-3.4.14" }
)
foreach ($d in $deps) {
  $target = Join-Path $cache $d.dir
  if (Test-Path $target) { Write-Host "exists: $($d.dir)"; continue }
  Write-Host "clone: $($d.dir) @ $($d.tag)"
  git clone --quiet --depth 1 --branch $d.tag $d.url $target
}
Write-Host "done. cmake/CPM.cmake is vendored from .cpm-cache/cpm/cmake/CPM.cmake"
