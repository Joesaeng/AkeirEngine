# pme_physics — 설계 문서 §57 (PhysicsWorld 인터페이스 + Box2D v3.1.1), §22.2 (contact 이벤트 정렬, body 순서)
add_library(pme_physics STATIC
  src/Box2DWorld.cpp
)
add_library(pme::physics ALIAS pme_physics)
target_include_directories(pme_physics PUBLIC include)
target_link_libraries(pme_physics PUBLIC pme::core PRIVATE box2d::box2d pme_warnings)
