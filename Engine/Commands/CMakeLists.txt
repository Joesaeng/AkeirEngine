# akeir_commands — 설계 문서 §8 (CommandBus/CommandKind/ChangeBuilder), §9 (Transaction/journal/commit), §10 (History/undo/redo), §49 (apply), §50 (dry-run), §78 (ChangeSet)
add_library(akeir_commands STATIC
  src/ChangeSet.cpp
  src/History.cpp
  src/CommandBus.cpp
  src/BuiltinCommands.cpp
)
add_library(akeir::commands ALIAS akeir_commands)
target_include_directories(akeir_commands PUBLIC include)
target_link_libraries(akeir_commands PUBLIC akeir::core akeir::reflection akeir::serialization akeir::runtime PRIVATE akeir_warnings)
