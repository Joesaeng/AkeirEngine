# pme_ecs — 설계 문서 §3.1 (Flecs 투영), §16 (query), §22.2 (결정적 순서·hash), §25 (dump), §26.1 (snapshot), §57 (physics sync)
add_library(pme_ecs STATIC
  src/PlayWorld.cpp
)
add_library(pme::ecs ALIAS pme_ecs)
target_include_directories(pme_ecs PUBLIC include)
target_link_libraries(pme_ecs PUBLIC pme::core pme::reflection pme::serialization pme::runtime pme::physics PRIVATE flecs::flecs_static pme_warnings)
