# Project_ME — AI-Native Game Framework (PoC)
# 설계 문서: AKEIR.md  (§41 Build 구조, §74 Phase, §75 Repository 구조)
# 빌드 방법: Docs/BUILD.md
cmake_minimum_required(VERSION 3.28)

# CMake 4.x 는 cmake_minimum_required < 3.5 인 서드파티를 거부한다 (§41). 구형 의존성 호환용.
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

project(AkeirEngine VERSION 0.1.1 LANGUAGES C CXX)   # AKEIR Engine. 코드 네임스페이스/타깃 접두어는 akeir

# ---- 언어 표준 (§41: floor C++20) ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)

# 정적 라이브러리만 쓴다 (DLL 경계 문제는 §39/§88.5 에서 cr.h 도입 시 다룬다)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(AKEIR_BUILD_TESTS "Build unit tests (doctest)" ON)
option(AKEIR_BUILD_CLI   "Build the `akeir` CLI"        ON)
# (AKEIR_WITH_SDL 은 cmake/Dependencies.cmake 에서 선언)

include(ProjectWarnings)     # 경고 설정 (엔진 타깃에만 적용, 서드파티 제외)
include(DetFpFlags)          # §22.2 / §41: det_fp_flags INTERFACE target
include(Dependencies)        # CPM 으로 버전 고정 의존성 (§3 표) — 위 option 들을 참조한다

add_subdirectory(Engine)
if(AKEIR_BUILD_CLI)
  add_subdirectory(Tools/CLI)
endif()
if(AKEIR_BUILD_TESTS)
  enable_testing()
  add_subdirectory(Tests)
endif()

# 샘플 게임 코드 (Game/Source). Engine 은 Game 을 모른다 (§76); akeir.exe 와 테스트가 링크한다.
add_subdirectory(Game/Source)
# 게임 실행 파일 bin/<ProjectName>.exe — 더블클릭으로 플레이 (SDL 빌드에서만). Tools/Player
if(AKEIR_WITH_SDL)
  add_subdirectory(Tools/Player)
endif()
