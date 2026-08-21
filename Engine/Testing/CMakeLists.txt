# akeir_testing — 설계 문서 §23 (Test Scenario), §23.1 (assertion 문법/의미론), §24 (results.json / JUnit), §22.2 (run-twice 결정성)
add_library(akeir_testing STATIC
  src/Expr.cpp
  src/TestRunner.cpp
)
add_library(akeir::testing ALIAS akeir_testing)
target_include_directories(akeir_testing PUBLIC include)
target_link_libraries(akeir_testing PUBLIC akeir::core akeir::runtime akeir::serialization akeir::ecs PRIVATE akeir_warnings)
