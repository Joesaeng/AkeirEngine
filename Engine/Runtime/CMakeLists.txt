# akeir_runtime — 설계 문서 §6 (authoring document model), §20.1 (fixed-tick loop), §22.2 (SimTime, InputFrame, world hash), §34 (prefab resolve), §88.3 (InputFrame)
add_library(akeir_runtime STATIC
  src/Application.cpp
  src/Components.cpp
  src/Project.cpp
)
add_library(akeir::runtime ALIAS akeir_runtime)
target_include_directories(akeir_runtime PUBLIC include)
target_link_libraries(akeir_runtime PUBLIC akeir::core akeir::reflection akeir::serialization PRIVATE akeir_warnings)
