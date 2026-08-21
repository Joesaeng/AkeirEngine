# pme_runtime — 설계 문서 §6 (authoring document model), §20.1 (fixed-tick loop), §22.2 (SimTime, InputFrame, world hash), §34 (prefab resolve), §88.3 (InputFrame)
add_library(pme_runtime STATIC
  src/Application.cpp
  src/Components.cpp
  src/Project.cpp
)
add_library(pme::runtime ALIAS pme_runtime)
target_include_directories(pme_runtime PUBLIC include)
target_link_libraries(pme_runtime PUBLIC pme::core pme::reflection pme::serialization PRIVATE pme_warnings)
