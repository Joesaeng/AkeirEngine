# pme_serialization — 설계 문서 §5.3 (canonical JSON, JCS), §9.2 (temp+rename), §14 (schema 검증 코드), §26.1/§88.8 (visibility)
add_library(pme_serialization STATIC
  src/Canonical.cpp
  src/ComponentJson.cpp
)
add_library(pme::serialization ALIAS pme_serialization)
target_include_directories(pme_serialization PUBLIC include)
target_link_libraries(pme_serialization PUBLIC pme::core pme::reflection PRIVATE pme_warnings)
