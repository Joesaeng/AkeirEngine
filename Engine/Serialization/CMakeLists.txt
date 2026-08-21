# akeir_serialization — 설계 문서 §5.3 (canonical JSON, JCS), §9.2 (temp+rename), §14 (schema 검증 코드), §26.1/§88.8 (visibility)
add_library(akeir_serialization STATIC
  src/Canonical.cpp
  src/ComponentJson.cpp
)
add_library(akeir::serialization ALIAS akeir_serialization)
target_include_directories(akeir_serialization PUBLIC include)
target_link_libraries(akeir_serialization PUBLIC akeir::core akeir::reflection PRIVATE akeir_warnings)
