# Engine — 설계 문서 §75 트리 / §76 의존 방향 (Core ← Runtime ← Game; Commands → Runtime)
#
# 타깃 (정적 라이브러리):
#   akeir_core            Engine/Core           §7 ID, §22.2 RNG/시간/해시, §28 로그, §88.4 크래시, §13 exit code
#   akeir_reflection      Engine/Reflection     §42.2 PropertyMeta registry, §43 속성 어휘
#   akeir_serialization   Engine/Serialization  §5.3 canonical JSON, §14 JSON Schema 생성
#   akeir_runtime         Engine/Runtime        §6 authoring document model (World/Prefab), §20.1 tick loop
#   akeir_ecs             Engine/ECS            §3.1 Flecs 투영 (authoring → runtime world)
#   akeir_physics         Engine/Physics        §57 PhysicsWorld 인터페이스 + Box2D
#   akeir_commands        Engine/Commands       §8–§10, §78 CommandBus / ChangeSet / Transaction / History   (Phase 3)
#   akeir_testing         Engine/Testing        §23–§24 데이터화 테스트 러너 (Expr, TestRunner)                    (Phase 5)
#   akeir_render          Engine/Render         §27 Renderer2D (SDL_Renderer, software capture), golden 비교        (Phase 2, AKEIR_WITH_SDL)
#   akeir_platform        Engine/Platform       §20 SDL3 Platform/InputMap/창 모드 루프                             (Phase 2, AKEIR_WITH_SDL)
#   akeir_validation      Engine/Validation     §29, §79 rule registry + Diagnostic                            (예정)
#
# 각 디렉터리의 README.md 가 모듈 ↔ 설계 § 대응과 현재 구현 범위를 적는다.

add_subdirectory(Core)
# 아래는 구현되는 순서대로 주석을 푼다 (Docs/STATUS.md 의 Phase 진행과 일치해야 한다)
add_subdirectory(Reflection)
add_subdirectory(Serialization)
add_subdirectory(Runtime)
add_subdirectory(Physics)
add_subdirectory(ECS)
add_subdirectory(Commands)
add_subdirectory(Testing)
if(AKEIR_WITH_SDL)
  add_subdirectory(Render)
  add_subdirectory(Platform)
endif()
# add_subdirectory(Validation)
