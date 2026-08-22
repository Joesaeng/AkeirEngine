# The game executable — `build/<preset>/bin/<ProjectName>.exe` (name from Game/project.json; "TestArena.exe" for the sample).
# Double-click to play: no console, logs in <project>/Cache/player.log. Only in SDL builds.
file(READ "${CMAKE_SOURCE_DIR}/Game/project.json" _akeir_project_json)
string(JSON _akeir_game_name ERROR_VARIABLE _akeir_json_err GET "${_akeir_project_json}" name)
if(_akeir_json_err OR NOT _akeir_game_name)
  set(_akeir_game_name "Game")
endif()
string(REGEX REPLACE "[^A-Za-z0-9_.-]" "" AKEIR_GAME_EXE "${_akeir_game_name}")
if(AKEIR_GAME_EXE STREQUAL "")
  set(AKEIR_GAME_EXE "Game")
endif()

add_executable(akeir_player Player.cpp)
target_link_libraries(akeir_player PRIVATE
  akeir::core akeir::runtime akeir::reflection akeir::ecs akeir::physics akeir::platform akeir::render
  $<LINK_LIBRARY:WHOLE_ARCHIVE,akeir_game>   # Game/ component registration (static initializers)
  akeir_warnings)
set_target_properties(akeir_player PROPERTIES
  OUTPUT_NAME "${AKEIR_GAME_EXE}"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(WIN32)
  # GUI subsystem (no console window) but keep the plain main(): mainCRTStartup
  set_target_properties(akeir_player PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_options(akeir_player PRIVATE "/ENTRY:mainCRTStartup")
  add_custom_command(TARGET akeir_player PRE_LINK
    COMMAND "${CMAKE_COMMAND}" -DEXE=$<TARGET_FILE:akeir_player> -P "${CMAKE_SOURCE_DIR}/cmake/UnlockExe.cmake"
    COMMENT "${AKEIR_GAME_EXE}: move a running game aside before linking"
    VERBATIM)
endif()
if(WIN32 AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_custom_command(TARGET akeir_player POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -DEXE=$<TARGET_FILE:akeir_player> -DDEST=${CMAKE_SOURCE_DIR}/bin/${AKEIR_GAME_EXE}.exe -P "${CMAKE_SOURCE_DIR}/cmake/InstallToBin.cmake"
    COMMENT "${AKEIR_GAME_EXE}: refresh <repo>/bin/${AKEIR_GAME_EXE}.exe if the release-zip layout is present"
    VERBATIM)
endif()
message(STATUS "AKEIR game executable: bin/${AKEIR_GAME_EXE}.exe")
