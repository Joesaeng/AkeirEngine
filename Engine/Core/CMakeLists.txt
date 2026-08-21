# pme_core — 설계 문서 §7 (ID), §13 (exit code), §22.2 (RNG/Time/Hash), §28 (Log), §79 (Diagnostic), §88.4 (Crash)
add_library(pme_core STATIC
  src/Id.cpp
  src/Hash.cpp
  src/Rng.cpp
  src/Log.cpp
  src/Crash.cpp
  src/Diagnostic.cpp
  src/Envelope.cpp
  src/FpEnv.cpp
)
add_library(pme::core ALIAS pme_core)
target_include_directories(pme_core PUBLIC include)
target_link_libraries(pme_core
  PUBLIC  nlohmann_json::nlohmann_json det_fp_flags
  PRIVATE pme_warnings)
if(WIN32)
  target_link_libraries(pme_core PRIVATE dbghelp)
endif()
target_compile_definitions(pme_core PUBLIC PME_VERSION_STRING="${PROJECT_VERSION}")
