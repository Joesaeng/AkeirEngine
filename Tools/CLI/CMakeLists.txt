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
target_link_libraries(akeir PRIVATE pme::core pme::runtime pme::reflection pme::serialization pme::ecs pme::physics pme::commands pme::testing
  $<LINK_LIBRARY:WHOLE_ARCHIVE,pme_game>   # Game/ component 등록(static initializer) 보존
  pme_warnings)
if(WIN32)
  target_link_libraries(akeir PRIVATE ws2_32)
endif()
if(PME_WITH_SDL)
  target_link_libraries(akeir PRIVATE pme::platform pme::render)
  target_compile_definitions(akeir PRIVATE PME_HAS_SDL=1)
endif()
set_target_properties(akeir PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
