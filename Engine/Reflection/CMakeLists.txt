# pme_reflection — 설계 문서 §42.2 (PropertyMeta / registry / REFLECT 매크로), §14 (schema 생성), §14.1 (wire_format), §43.1 (속성 어휘)
add_library(pme_reflection STATIC
  src/PropertyMeta.cpp
  src/Registry.cpp
)
add_library(pme::reflection ALIAS pme_reflection)
target_include_directories(pme_reflection PUBLIC include)
target_link_libraries(pme_reflection PUBLIC pme::core PRIVATE pme_warnings)
