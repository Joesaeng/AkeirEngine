# akeir_warnings — 엔진/툴/게임 타깃에만 적용하는 경고 설정. 서드파티에는 적용하지 않는다.
add_library(akeir_warnings INTERFACE)

if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(akeir_warnings INTERFACE
    /W4 /permissive- /utf-8 /Zc:preprocessor /Zc:__cplusplus /EHsc
    /wd4100   # unreferenced formal parameter (인터페이스 구현에서 흔함)
  )
  target_compile_definitions(akeir_warnings INTERFACE
    _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  target_compile_options(akeir_warnings INTERFACE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
  if(WIN32)
    target_compile_definitions(akeir_warnings INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
  endif()
endif()
