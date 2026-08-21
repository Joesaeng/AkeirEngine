# det_fp_flags — 결정론 계약의 컴파일러 플래그를 한 곳에서 정의한다.
# 설계 문서 §22.2 (Build flags) / §41.
#
# 규칙:
#   MSVC (VS2022+): /fp:precise (기본값이지만 명시). VS2022부터 precise 에서 FMA contraction 을 내지 않는다.
#                   /fp:fast, /fp:contract 금지.
#   Clang/GCC     : -ffp-contract=off -fno-fast-math.  (GCC GNU 모드 기본 fast, Clang 기본 on — 둘 다 FMA 허용이므로 명시)
#   clang-cl      : /clang:-ffp-contract=off
#
# 모든 sim TU(Engine/*, Game/*)와 ThirdParty physics(Box2D/Jolt)가 이 target 을 link 해야 한다.
# `game project info --json` 은 PME_FP_FLAGS_HASH 를 노출해 replay header(§22.3)와 비교한다.

add_library(det_fp_flags INTERFACE)

if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(_pme_fp_flags "/fp:precise")
  set(PME_FP_FLAGS_OPTIONS /fp:precise)
  if(MSVC_VERSION LESS 1930)
    message(FATAL_ERROR "det_fp_flags: VS2022 (MSVC 19.30+) 이상이 필요하다. 이전 버전은 /fp:precise 에서도 FMA contraction 을 낼 수 있다 (§22.2).")
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  # clang-cl
  set(_pme_fp_flags "/clang:-ffp-contract=off /clang:-fno-fast-math")
  set(PME_FP_FLAGS_OPTIONS /clang:-ffp-contract=off /clang:-fno-fast-math)
else()
  set(_pme_fp_flags "-ffp-contract=off -fno-fast-math")
  set(PME_FP_FLAGS_OPTIONS -ffp-contract=off -fno-fast-math)
endif()
target_compile_options(det_fp_flags INTERFACE ${PME_FP_FLAGS_OPTIONS})
# 서드파티(export set 에 묶인 타깃)에는 INTERFACE target 을 link 하지 말고 이 목록을 직접 건다:
#   target_compile_options(box2d PRIVATE ${PME_FP_FLAGS_OPTIONS})

# 플래그 문자열의 해시 → replay header / project info 에 기록 (§22.3, §41)
string(SHA256 _pme_fp_flags_hash "${_pme_fp_flags}|${CMAKE_CXX_COMPILER_ID}|${CMAKE_CXX_COMPILER_VERSION}|${CMAKE_SYSTEM_PROCESSOR}")
string(SUBSTRING "${_pme_fp_flags_hash}" 0 16 PME_FP_FLAGS_HASH)
set(PME_FP_FLAGS_STRING "${_pme_fp_flags}")
target_compile_definitions(det_fp_flags INTERFACE
  PME_FP_FLAGS_HASH="${PME_FP_FLAGS_HASH}"
  PME_FP_FLAGS_STRING="${_pme_fp_flags}")

message(STATUS "det_fp_flags: ${_pme_fp_flags}  (hash ${PME_FP_FLAGS_HASH})")
