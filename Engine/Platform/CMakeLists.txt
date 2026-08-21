# pme_platform — 설계 문서 §20 (SDL3 창/driver), §20.1 (accumulator 는 창 모드에만), §88.3 (input.json → InputFrame). PME_WITH_SDL=ON 일 때만.
add_library(pme_platform STATIC
  src/Platform.cpp
  src/InputMap.cpp
  src/Interactive.cpp
)
add_library(pme::platform ALIAS pme_platform)
target_include_directories(pme_platform PUBLIC include)
target_link_libraries(pme_platform PUBLIC pme::core pme::runtime pme::serialization pme::ecs pme::render SDL3::SDL3-static PRIVATE pme_warnings)
