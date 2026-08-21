# `akeir` CLI — 설계 문서 §11 (1급 인터페이스), §12 (envelope), §13 (exit code), §15 (capabilities)
add_executable(akeir
  src/main.cpp
  src/Args.cpp
  src/Commands.cpp
  src/ProjectCommands.cpp
  src/InitCommand.cpp
  src/RunCommands.cpp
  src/MutationCommands.cpp
  src/TestCommands.cpp
  src/SdlCommands.cpp
  src/Serve.cpp
  src/Mcp.cpp
)
target_include_directories(akeir PRIVATE src)
target_link_libraries(akeir PRIVATE akeir::core akeir::runtime akeir::reflection akeir::serialization akeir::ecs akeir::physics akeir::commands akeir::testing
  $<LINK_LIBRARY:WHOLE_ARCHIVE,akeir_game>   # Game/ component 등록(static initializer) 보존
  akeir_warnings)
if(WIN32)
  target_link_libraries(akeir PRIVATE ws2_32)
endif()
if(AKEIR_WITH_SDL)
  target_link_libraries(akeir PRIVATE akeir::platform akeir::render)
  target_compile_definitions(akeir PRIVATE AKEIR_HAS_SDL=1)
endif()
set_target_properties(akeir PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
