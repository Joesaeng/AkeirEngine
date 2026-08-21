# akeir_platform — 설계 문서 §20 (SDL3 창/driver), §20.1 (accumulator 는 창 모드에만), §88.3 (input.json → InputFrame). AKEIR_WITH_SDL=ON 일 때만.
add_library(akeir_platform STATIC
  src/Platform.cpp
  src/InputMap.cpp
  src/Interactive.cpp
)
add_library(akeir::platform ALIAS akeir_platform)
target_include_directories(akeir_platform PUBLIC include)
target_link_libraries(akeir_platform PUBLIC akeir::core akeir::runtime akeir::serialization akeir::ecs akeir::render SDL3::SDL3-static PRIVATE akeir_warnings)
