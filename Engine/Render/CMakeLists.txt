# pme_render — 설계 문서 §27 (capture), §27.1 (golden 비교). SDL_Renderer 기반 placeholder 스프라이트 + software rasterizer capture (ADR-0026). PME_WITH_SDL=ON 일 때만.
add_library(pme_render STATIC
  src/Renderer2D.cpp
)
add_library(pme::render ALIAS pme_render)
target_include_directories(pme_render PUBLIC include)
target_link_libraries(pme_render PUBLIC pme::core pme::runtime pme::ecs SDL3::SDL3-static PRIVATE pme_warnings)
