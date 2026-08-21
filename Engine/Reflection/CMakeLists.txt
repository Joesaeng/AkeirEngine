# akeir_reflection — 설계 문서 §42.2 (PropertyMeta / registry / REFLECT 매크로), §14 (schema 생성), §14.1 (wire_format), §43.1 (속성 어휘)
add_library(akeir_reflection STATIC
  src/PropertyMeta.cpp
  src/Registry.cpp
)
add_library(akeir::reflection ALIAS akeir_reflection)
target_include_directories(akeir_reflection PUBLIC include)
target_link_libraries(akeir_reflection PUBLIC akeir::core PRIVATE akeir_warnings)
