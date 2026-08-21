# pme_game — 샘플 게임 "TestArena" 의 component / system. 설계 문서 §59 (좁은 엔진 API), §60 (Game/ 은 AI 가 자주 수정), §71 (PoC 시나리오), §76 (Game → Runtime, Core → Game 금지)
# 정적 라이브러리: component 등록이 static initializer 이므로 실행 파일은 $<LINK_LIBRARY:WHOLE_ARCHIVE,pme_game> 로 링크한다.
add_library(pme_game STATIC
  GameComponents.cpp
  GameSystems.cpp
)
add_library(pme::game ALIAS pme_game)
target_include_directories(pme_game PUBLIC .)
target_link_libraries(pme_game PUBLIC pme::ecs PRIVATE pme_warnings)
