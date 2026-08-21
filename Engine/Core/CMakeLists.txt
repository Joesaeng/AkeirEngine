# akeir_core — 설계 문서 §7 (ID), §13 (exit code), §22.2 (RNG/Time/Hash), §28 (Log), §79 (Diagnostic), §88.4 (Crash)
add_library(akeir_core STATIC
  src/Id.cpp
  src/Hash.cpp
  src/Rng.cpp
  src/Log.cpp
  src/Crash.cpp
  src/Diagnostic.cpp
  src/Envelope.cpp
  src/FpEnv.cpp
)
add_library(akeir::core ALIAS akeir_core)
target_include_directories(akeir_core PUBLIC include)
target_link_libraries(akeir_core
  PUBLIC  nlohmann_json::nlohmann_json det_fp_flags
  PRIVATE akeir_warnings)
if(WIN32)
  target_link_libraries(akeir_core PRIVATE dbghelp)
endif()
target_compile_definitions(akeir_core PUBLIC AKEIR_VERSION_STRING="${PROJECT_VERSION}")
