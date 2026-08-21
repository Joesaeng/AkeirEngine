# akeir_physics — 설계 문서 §57 (PhysicsWorld 인터페이스 + Box2D v3.1.1), §22.2 (contact 이벤트 정렬, body 순서)
add_library(akeir_physics STATIC
  src/Box2DWorld.cpp
)
add_library(akeir::physics ALIAS akeir_physics)
target_include_directories(akeir_physics PUBLIC include)
target_link_libraries(akeir_physics PUBLIC akeir::core PRIVATE box2d::box2d akeir_warnings)
