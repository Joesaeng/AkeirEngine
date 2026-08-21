# 의존성 — 설계 문서 §3 표의 "검증된 버전"을 그대로 고정한다. (2026-08-21 기준)
# 관리 방식: CPM.cmake (§41: "CPM 또는 vcpkg 중 하나만" → CPM 선택. DECISIONS.md ADR-0006)
#
# 오프라인/빠른 configure: 저장소 루트의 .cpm-cache/<name>/ 에 같은 태그의 clone 이 있으면 그것을 쓴다
# (CPM_<name>_SOURCE 오버라이드). 없으면 GitHub 에서 받는다. .cpm-cache 는 .gitignore 대상이며
# 새 머신에서는 `scripts/fetch-deps.ps1` 로 다시 만들 수 있다.

# vendored CPM.cmake 는 git clone 본이라 버전 문자열이 치환되어 있지 않다 → 여기서 지정 (경고 억제)
set(EXTRACTED_CPM_VERSION 0.43.1)
include(${CMAKE_CURRENT_LIST_DIR}/CPM.cmake)

set(PME_DEP_CACHE "${CMAKE_SOURCE_DIR}/.cpm-cache")

# pme_add_dep(NAME <cpm name> DIR <cache dir> REPO <owner/repo> TAG <tag> OPTIONS <opt>...)
#   .cpm-cache/<dir> 가 있으면 SOURCE_DIR 로 쓰고, 없으면 GitHub 에서 TAG 를 받는다.
macro(pme_add_dep)
  # macro: CPMAddPackage 가 PARENT_SCOPE 로 내보내는 <name>_SOURCE_DIR 등이 호출자에게 보여야 한다
  cmake_parse_arguments(D "" "NAME;DIR;REPO;TAG" "OPTIONS" ${ARGN})
  if(EXISTS "${PME_DEP_CACHE}/${D_DIR}/CMakeLists.txt")
    message(STATUS "deps: ${D_NAME} ${D_TAG} <- .cpm-cache/${D_DIR}")
    CPMAddPackage(NAME ${D_NAME} SOURCE_DIR "${PME_DEP_CACHE}/${D_DIR}" OPTIONS ${D_OPTIONS})
  else()
    message(STATUS "deps: ${D_NAME} ${D_TAG} <- github.com/${D_REPO}")
    # 캐시가 없으면 GitHub 에서 shallow clone (flecs/json 은 full clone 이 각 ~280MB 라 GIT_SHALLOW 필수)
    CPMAddPackage(NAME ${D_NAME} GITHUB_REPOSITORY ${D_REPO} GIT_TAG ${D_TAG} GIT_SHALLOW TRUE OPTIONS ${D_OPTIONS})
  endif()
endmacro()

# ---- 버전 고정 표 (§3) ----
set(PME_DEP_FLECS_TAG    v4.1.6)         # MIT, C99 core + C++17 API  (2026-06-29)
set(PME_DEP_BOX2D_TAG    v3.1.1)         # MIT, C17                    (2025-06-04)
set(PME_DEP_JSON_TAG     v3.12.0)        # MIT, nlohmann/json          (2025-04-11)
set(PME_DEP_DOCTEST_TAG  v2.5.3)         # MIT                         (2026-07-06)
set(PME_DEP_SDL_TAG      release-3.4.14) # zlib                        (2026-08-03)

# ---- nlohmann/json ----
pme_add_dep(NAME nlohmann_json DIR json REPO nlohmann/json TAG ${PME_DEP_JSON_TAG}
  OPTIONS "JSON_BuildTests OFF" "JSON_Install OFF")

# ---- Flecs (ECS, §3.1 기본 후보) ----
pme_add_dep(NAME flecs DIR flecs REPO SanderMertens/flecs TAG ${PME_DEP_FLECS_TAG}
  OPTIONS "FLECS_STATIC ON" "FLECS_SHARED OFF" "FLECS_PIC OFF" "FLECS_TESTS OFF")

# ---- Box2D v3 (2D physics, §57) ----
pme_add_dep(NAME box2d DIR box2d REPO erincatto/box2d TAG ${PME_DEP_BOX2D_TAG}
  OPTIONS "BOX2D_SAMPLES OFF" "BOX2D_UNIT_TESTS OFF" "BOX2D_BENCHMARKS OFF" "BOX2D_VALIDATE OFF")

# ---- doctest (unit tests) ----
if(PME_BUILD_TESTS)
  pme_add_dep(NAME doctest DIR doctest REPO doctest/doctest TAG ${PME_DEP_DOCTEST_TAG}
    OPTIONS "DOCTEST_WITH_TESTS OFF" "DOCTEST_NO_INSTALL ON")
endif()

# ---- SDL3 (platform / window / input / headless drivers, §20) ----
option(PME_WITH_SDL "Build with SDL3 (window/input). OFF = headless-only build" ON)
if(PME_WITH_SDL)
  pme_add_dep(NAME SDL3 DIR sdl REPO libsdl-org/SDL TAG ${PME_DEP_SDL_TAG}
    OPTIONS "SDL_STATIC ON" "SDL_SHARED OFF" "SDL_TEST_LIBRARY OFF" "SDL_TESTS OFF" "SDL_EXAMPLES OFF" "SDL_INSTALL OFF")
endif()

# 서드파티 physics 에도 결정론 플래그를 건다 (§41). Box2D 자체 CMake 는 GCC/Clang 에 -ffp-contract=off 를 이미 넣지만
# MSVC 경로와 통일하기 위해 명시한다. (export set 때문에 INTERFACE target 대신 옵션 목록을 직접 건다)
if(TARGET box2d)
  target_compile_options(box2d PRIVATE ${PME_FP_FLAGS_OPTIONS})
endif()
