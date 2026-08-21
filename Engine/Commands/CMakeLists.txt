# pme_commands — 설계 문서 §8 (CommandBus/CommandKind/ChangeBuilder), §9 (Transaction/journal/commit), §10 (History/undo/redo), §49 (apply), §50 (dry-run), §78 (ChangeSet)
add_library(pme_commands STATIC
  src/ChangeSet.cpp
  src/History.cpp
  src/CommandBus.cpp
  src/BuiltinCommands.cpp
)
add_library(pme::commands ALIAS pme_commands)
target_include_directories(pme_commands PUBLIC include)
target_link_libraries(pme_commands PUBLIC pme::core pme::reflection pme::serialization pme::runtime PRIVATE pme_warnings)
