# akeir_ecs — 설계 문서 §3.1 (Flecs 투영), §16 (query), §22.2 (결정적 순서·hash), §25 (dump), §26.1 (snapshot), §57 (physics sync)
add_library(akeir_ecs STATIC
  src/PlayWorld.cpp
  src/Screen.cpp
)
add_library(akeir::ecs ALIAS akeir_ecs)
target_include_directories(akeir_ecs PUBLIC include)
target_link_libraries(akeir_ecs PUBLIC akeir::core akeir::reflection akeir::serialization akeir::runtime akeir::physics PRIVATE flecs::flecs_static akeir_warnings)
