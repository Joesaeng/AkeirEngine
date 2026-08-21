# akeir_render — 설계 문서 §27 (capture), §27.1 (golden 비교). SDL_Renderer 기반 placeholder 스프라이트 + software rasterizer capture (ADR-0026). AKEIR_WITH_SDL=ON 일 때만.
add_library(akeir_render STATIC
  src/Renderer2D.cpp
)
add_library(akeir::render ALIAS akeir_render)
target_include_directories(akeir_render PUBLIC include)
target_link_libraries(akeir_render PUBLIC akeir::core akeir::runtime akeir::ecs SDL3::SDL3-static PRIVATE akeir_warnings)
