# akeir_fixture_game — the FROZEN copy of the TestArena sample used by the engine unit tests (Tests/).
# Game/ is the user's project and may be replaced wholesale (ADR-0036); the engine tests must not notice.
# Same namespace `game` and header names as Game/Source so the tests compile unchanged — the test binary
# links this library instead of akeir_game, never both.
add_library(akeir_fixture_game STATIC
  GameComponents.cpp
  GameSystems.cpp
)
target_include_directories(akeir_fixture_game PUBLIC .)
target_link_libraries(akeir_fixture_game PUBLIC akeir::ecs PRIVATE akeir_warnings)
