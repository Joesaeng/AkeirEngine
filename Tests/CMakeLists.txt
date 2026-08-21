# Engine 단위 테스트 (doctest v2.5.3). 실행: Docs/BUILD.md 의 `ctest` 또는 `build/<preset>/Tests/akeir_tests.exe`
# 테스트 파일은 모듈별: Core_*.cpp, Reflection_*.cpp … 설계 § 를 주석으로 단다.
add_executable(akeir_tests
  main.cpp
  Core_Id.cpp
  Core_Hash.cpp
  Core_Rng.cpp
  Core_Log.cpp
  Core_Diagnostic.cpp
  Runtime_Application.cpp
  Reflection_Registry.cpp
  Serialization_Canonical.cpp
  Serialization_ComponentJson.cpp
  Runtime_Project.cpp
  ECS_PlayWorld.cpp
  Commands_ChangeSet.cpp
  Commands_Bus.cpp
  Testing_Expr.cpp
  Testing_Runner.cpp
)
target_link_libraries(akeir_tests PRIVATE akeir::core akeir::runtime akeir::reflection akeir::serialization akeir::ecs akeir::physics akeir::commands akeir::testing
  $<LINK_LIBRARY:WHOLE_ARCHIVE,akeir_game>   # component 등록(static initializer)이 버려지지 않게
  doctest::doctest akeir_warnings)
# MSVC 14.44 (VS 17.14) STL 의 <string_view> 가 doctest 의 ostream forward-declaration 과 충돌한다 → 표준 헤더 사용 (doctest 권장 회피책)
target_compile_definitions(akeir_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
if(AKEIR_WITH_SDL)
  target_sources(akeir_tests PRIVATE Render_Capture.cpp)
  target_link_libraries(akeir_tests PRIVATE akeir::platform akeir::render)
endif()

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(akeir_tests)
