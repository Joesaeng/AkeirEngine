# pme_testing — 설계 문서 §23 (Test Scenario), §23.1 (assertion 문법/의미론), §24 (results.json / JUnit), §22.2 (run-twice 결정성)
add_library(pme_testing STATIC
  src/Expr.cpp
  src/TestRunner.cpp
)
add_library(pme::testing ALIAS pme_testing)
target_include_directories(pme_testing PUBLIC include)
target_link_libraries(pme_testing PUBLIC pme::core pme::runtime pme::serialization pme::ecs PRIVATE pme_warnings)
